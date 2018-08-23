#include <iostream>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <libgo/libgo.h>
#include <atomic>
using namespace std;

#define OUT(x) cout << #x << " = " << x << endl
#define O(x) cout << x << endl

#define ASSERT_RES(res) \
    if (res < 0) { \
        printf("LINE:%d\n", __LINE__); \
        perror(""); \
        exit(1); \
    }

enum eTestType {
    oneway,
    pingpong,
};

int testType = 0;
int cBufSize = 64 * 1024;
int nConnection = 1;
bool openCoroutine = true;
std::atomic<long> gBytes{0};
std::atomic<long> gQps{0};

void show() {
    long lastBytes = 0;
    long lastQps = 0;
    for (;;) {
        sleep(1);
        long bytes = gBytes - lastBytes;
        long qps = gQps - lastQps;
        printf("%ld MB/s, QPS: %ld\n", bytes / 1024 / 1024, qps);
        lastBytes += bytes;
        lastQps += qps;
    }
}

template <typename F>
void routineCreate(F f) {
    if (openCoroutine) {
        printf("routine create by libgo\n");
        go f;
    } else {
        printf("routine create by std::thread\n");
        std::thread(f).detach();
    }
}

void doRecv(int sock) {
    printf("fd %d connected.\n", sock);
    char *buf = new char[cBufSize];
    for (;;) {
        ssize_t res = read(sock, buf, cBufSize);
        if (res == -1 && res == EAGAIN)
            continue;

        if (res <= 0) {
            printf("fd %d closed.\n", sock);
            close(sock);
            return ;
        }

        gBytes += res;
        gQps ++;

        if (testType == pingpong) {
            write(sock, buf, res);
        }
    }
}

void doAccept() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_RES(sock);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9007);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    int res = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    ASSERT_RES(res);

    res = listen(sock, 128);
    ASSERT_RES(res);

    for (;;) {
        struct sockaddr_in addr = {};
        socklen_t len = sizeof(addr);
        res = accept(sock, (struct sockaddr*)&addr, &len);
        if (res == -1)
            continue;

        routineCreate([=]{
            doRecv(res);
        });
    }
}

void doSend(int sock) {
    printf("fd %d connected.\n", sock);
    char *buf = new char[cBufSize];
    for (;;) {
        ssize_t res = write(sock, buf, cBufSize);
        if (res == -1 && res == EAGAIN)
            continue;

        if (res <= 0) {
            printf("fd %d closed.\n", sock);
            close(sock);
            return ;
        }

        gBytes += res;
        gQps ++;

        if (testType == pingpong) {
            read(sock, buf, cBufSize);
        }
    }
}

void doConnect() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_RES(sock);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9007);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    int res = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    ASSERT_RES(res);

    doSend(sock);
}

int main(int argc, char** argv) {
    if (argc <= 1) {
        printf("Usage: %s ClientOrServer OpenCoroutine TestType Conn BufSize\n", argv[0]);
        printf("\n");
        printf("TestType: 0 - oneway, 1 - pingpong\n");
        exit(1);
    }

    int clientOrServer = 0;
    if (argc >= 2) clientOrServer = atoi(argv[1]);
    if (argc >= 3) openCoroutine = !!atoi(argv[2]);
    if (argc >= 4) testType = atoi(argv[3]);
    if (argc >= 5) nConnection = atoi(argv[4]);
    if (argc >= 6) cBufSize = atoi(argv[5]);

    std::thread(&show).detach();
    if (clientOrServer == 1) {
        for (int i = 0; i < nConnection; ++i)
            routineCreate(&doConnect);
    } else
        routineCreate(&doAccept);
    co_sched.Start();
}

