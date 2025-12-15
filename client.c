#define _POSIX_C_SOURCE 200809L

// Implementation of TCP connection on client
#include "client.h"
#include <signal.h>
#include <time.h>
#include <stdlib.h>   // for exit()
#include <sys/stat.h> // ===== ADDED: for stat()


// ===== PHASE 4: global socket for clean shutdown =====
static int g_sockfd = -1;
// ====================================================

static const char *base_name(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static void send_all(int fd, const void *buf, size_t len)
{
    size_t off = 0;
    while (off < len)
    {
        ssize_t n = send(fd, (const char *)buf + off, len - off, 0);
        if (n <= 0)
            return;
        off += (size_t)n;
    }
}

// ===== PHASE 4: SIGINT handler for client =====
void client_sigint(int sig)
{
    (void)sig;
    if (g_sockfd >= 0)
        close(g_sockfd);
    printf("\nClient exiting cleanly\n");
    exit(0);
}
// ==============================================

int main()
{
    // ===== PHASE 4: register SIGINT handler =====
    signal(SIGINT, client_sigint);
    // ==========================================

    int port;
    char server_IP[64] = "";
    /* ===== ADDED: explicit path variables ===== */
    char data_path[2048] = ""; // from DATA_FILE_PATH
    char file_path[2048] = ""; // resolved file path
    /* ========================================== */
    char buf[64];

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    uint64_t bytes_sent = 0;

    // Read the config file
    FILE *cfg = fopen("client_conf", "r");
    if (!cfg)
        return 1;

    fscanf(cfg, "%s %d", buf, &port);
    fscanf(cfg, "%s %s", buf, server_IP);
    fscanf(cfg, "%s %s", buf, data_path);

    /* ===== CONFIG VALIDATION (SAFE ADD) ===== */
    if (strcmp(buf, "DATA_FILE_PATH") != 0)
    {
        fprintf(stderr, "Expected DATA_FILE_PATH in client_conf\n");
        fclose(cfg);
        return 1;
    }
    /* ======================================== */

    fclose(cfg);

    /* ===== DATA_FILE_PATH HANDLING (SAFE ADD) ===== */
    struct stat st;
    if (stat(data_path, &st) < 0)
    {
        perror("DATA_FILE_PATH invalid");
        return 1;
    }

    if (S_ISDIR(st.st_mode))
    {
        /* ===== WARNING-FREE PATH BUILD ===== */
        strncpy(file_path, data_path, sizeof(file_path) - 1);
        file_path[sizeof(file_path) - 1] = '\0';
        strncat(file_path, "/text1.txt",
                sizeof(file_path) - strlen(file_path) - 1);
        /* ================================== */
    }
    else
    {
        // If already a file, use directly
        strncpy(file_path, data_path, sizeof(file_path) - 1);
        file_path[sizeof(file_path) - 1] = '\0';
    }
    /* ============================================ */
    // Open the data file from file path..
    FILE *in = fopen(file_path, "rb");
    if (!in)
        return 1;

    // Make socket & connect
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        return 1;

    // ===== PHASE 4: save socket globally =====
    g_sockfd = sockfd;
    // ========================================

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    inet_pton(AF_INET, server_IP, &addr.sin_addr);
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        return 1;

    // ===== PHASE 1 PARTIAL: HANDSHAKE =====
    char hello[64];
    snprintf(hello, sizeof(hello), "HELLO client_%d\n", getpid());
    send_all(sockfd, hello, strlen(hello));

    char hresp[64];
    ssize_t hr = recv(sockfd, hresp, sizeof(hresp) - 1, 0);
    if (hr <= 0)
    {
        close(sockfd);
        return 1;
    }
    hresp[hr] = '\0';
    if (strncmp(hresp, "OK", 2) != 0)
    {
        printf("Handshake failed: %s\n", hresp);
        close(sockfd);
        return 1;
    }
    // ====================================

    // Send the file name
    const char *fname = base_name(file_path);
    char header[1024];
    int hl = snprintf(header, sizeof(header), "WRITE %s\n", fname);
    send_all(sockfd, header, (size_t)hl);

    // Stream file bytes
    char sendbuf[65536];
    size_t n;
    while ((n = fread(sendbuf, 1, sizeof(sendbuf), in)) > 0)
    {
        send_all(sockfd, sendbuf, n);
        bytes_sent += (uint64_t)n;
    }
    fclose(in);

    shutdown(sockfd, SHUT_WR);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    char reply[128];
    ssize_t r = recv(sockfd, reply, sizeof(reply) - 1, 0);
    if (r > 0)
    {
        reply[r] = '\0';

        // ===== PHASE 4: handle server shutdown =====
        if (strncmp(reply, "SERVER_SHUTDOWN", 15) == 0)
        {
            printf("Server is shutting down. Client exiting.\n");
            close(sockfd);
            return 0;
        }
        // ==========================================

        printf("%s", reply);
    }
    else
    {
        printf("No Confirmation from the server\n");
    }

    // From the total sent bytes
    double dt = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    double MB = bytes_sent / 1e6;
    printf("TCP: sent %" PRIu64 " bytes in %.3f s (%.2f MB/s)\n", bytes_sent, dt, MB / dt);

    close(sockfd);
    return 0;
}
