/*
 * HER OS Zero-Copy IPC Implementation
 *
 * High-performance inter-process communication using shared memory rings.
 * Provides lock-free, zero-copy communication between HER OS daemons.
 *
 * Features:
 * - Lock-free ring buffer implementation
 * - Zero-copy message passing
 * - NUMA-aware memory allocation
 * - Atomic operations for thread safety
 * - Memory-mapped shared memory
 * - Support for multiple message types
 *
 * Author: HER OS Project
 * License: GPL-2.0
 * Version: 1.0.0
 */

#ifndef HEROS_ZERO_COPY_IPC_H
#define HEROS_ZERO_COPY_IPC_H

#include <stdint.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

// Ring buffer configuration
#define RING_SIZE (1024 * 1024)  // 1MB ring buffer
#define MAX_MESSAGE_SIZE 4096
#define MAX_MESSAGES (RING_SIZE / MAX_MESSAGE_SIZE)

// Message types for HER OS daemons
typedef enum {
    MSG_TYPE_METADATA_EVENT = 1,
    MSG_TYPE_PDP_REQUEST = 2,
    MSG_TYPE_WAL_ENTRY = 3,
    MSG_TYPE_DEDUP_TRIGGER = 4,
    MSG_TYPE_TIERING_MIGRATE = 5,
    MSG_TYPE_AI_ANALYSIS = 6,
    MSG_TYPE_ACTION_UI = 7,
    MSG_TYPE_HEALTH_CHECK = 8,
    MSG_TYPE_METRICS_UPDATE = 9,
    MSG_TYPE_AUDIT_LOG = 10
} message_type_t;

// Message header structure
typedef struct {
    uint32_t magic;           // Magic number for validation
    uint32_t message_type;    // Type of message
    uint32_t payload_size;    // Size of payload in bytes
    uint64_t timestamp;       // Timestamp in nanoseconds
    uint32_t sequence;        // Sequence number for ordering
    uint32_t checksum;        // Simple checksum for integrity
} __attribute__((packed)) message_header_t;

// Complete message structure
typedef struct {
    message_header_t header;
    uint8_t payload[MAX_MESSAGE_SIZE];
} __attribute__((packed)) message_t;

// Ring buffer structure
typedef struct {
    atomic_uint64_t head;     // Producer position
    atomic_uint64_t tail;     // Consumer position
    atomic_uint64_t sequence; // Global sequence counter
    uint32_t ring_size;       // Size of ring buffer
    uint32_t message_count;   // Number of messages in ring
    pthread_mutex_t mutex;    // Mutex for initialization
    int numa_node;           // NUMA node for allocation
} ring_buffer_t;

// Shared memory segment structure
typedef struct {
    ring_buffer_t ring;
    message_t messages[MAX_MESSAGES];
    uint8_t padding[64];     // Cache line padding
} __attribute__((packed)) shared_memory_segment_t;

// IPC context structure
typedef struct {
    char *shm_name;                    // Shared memory name
    int shm_fd;                        // Shared memory file descriptor
    shared_memory_segment_t *segment;  // Mapped shared memory
    size_t segment_size;               // Size of mapped segment
    int numa_node;                     // NUMA node for allocation
    int is_producer;                   // Producer or consumer
    uint64_t last_sequence;            // Last processed sequence
} ipc_context_t;

// Error codes
typedef enum {
    IPC_SUCCESS = 0,
    IPC_ERROR_INVALID_PARAMETER = -1,
    IPC_ERROR_SHARED_MEMORY = -2,
    IPC_ERROR_MAPPING = -3,
    IPC_ERROR_RING_FULL = -4,
    IPC_ERROR_RING_EMPTY = -5,
    IPC_ERROR_MESSAGE_TOO_LARGE = -6,
    IPC_ERROR_CHECKSUM = -7,
    IPC_ERROR_TIMEOUT = -8,
    IPC_ERROR_NUMA = -9
} ipc_error_t;

// Function prototypes

/**
 * Initialize zero-copy IPC context
 * @param ctx IPC context to initialize
 * @param shm_name Shared memory name
 * @param is_producer True if producer, false if consumer
 * @param numa_node NUMA node for allocation (-1 for auto)
 * @return IPC_SUCCESS on success, error code on failure
 */
int ipc_init(ipc_context_t *ctx, const char *shm_name, int is_producer, int numa_node);

/**
 * Clean up IPC context
 * @param ctx IPC context to clean up
 * @return IPC_SUCCESS on success, error code on failure
 */
int ipc_cleanup(ipc_context_t *ctx);

/**
 * Send message through zero-copy IPC
 * @param ctx IPC context
 * @param msg_type Message type
 * @param payload Message payload
 * @param payload_size Size of payload
 * @param timeout_ms Timeout in milliseconds (0 for non-blocking)
 * @return IPC_SUCCESS on success, error code on failure
 */
int ipc_send(ipc_context_t *ctx, message_type_t msg_type, 
             const void *payload, uint32_t payload_size, uint32_t timeout_ms);

/**
 * Receive message from zero-copy IPC
 * @param ctx IPC context
 * @param msg_type Pointer to store message type
 * @param payload Buffer to store payload
 * @param payload_size Pointer to store payload size
 * @param timeout_ms Timeout in milliseconds (0 for non-blocking)
 * @return IPC_SUCCESS on success, error code on failure
 */
int ipc_receive(ipc_context_t *ctx, message_type_t *msg_type,
                void *payload, uint32_t *payload_size, uint32_t timeout_ms);

/**
 * Check if ring buffer is empty
 * @param ctx IPC context
 * @return 1 if empty, 0 if not empty
 */
int ipc_is_empty(ipc_context_t *ctx);

/**
 * Check if ring buffer is full
 * @param ctx IPC context
 * @return 1 if full, 0 if not full
 */
int ipc_is_full(ipc_context_t *ctx);

/**
 * Get ring buffer statistics
 * @param ctx IPC context
 * @param head Pointer to store head position
 * @param tail Pointer to store tail position
 * @param message_count Pointer to store message count
 * @return IPC_SUCCESS on success, error code on failure
 */
int ipc_get_stats(ipc_context_t *ctx, uint64_t *head, uint64_t *tail, uint32_t *message_count);

/**
 * Calculate simple checksum for message integrity
 * @param data Data to checksum
 * @param size Size of data
 * @return Checksum value
 */
uint32_t ipc_calculate_checksum(const void *data, uint32_t size);

/**
 * Validate message header
 * @param header Message header to validate
 * @return 1 if valid, 0 if invalid
 */
int ipc_validate_header(const message_header_t *header);

/**
 * Get NUMA node for current thread
 * @return NUMA node number, -1 if not available
 */
int ipc_get_numa_node(void);

/**
 * Allocate memory on specific NUMA node
 * @param size Size to allocate
 * @param numa_node NUMA node for allocation
 * @return Pointer to allocated memory, NULL on failure
 */
void *ipc_numa_alloc(size_t size, int numa_node);

/**
 * Free NUMA-allocated memory
 * @param ptr Pointer to free
 * @param size Size of allocation
 */
void ipc_numa_free(void *ptr, size_t size);

// Inline functions for performance-critical operations

/**
 * Get current timestamp in nanoseconds
 * @return Current timestamp
 */
static inline uint64_t ipc_get_timestamp(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/**
 * Calculate ring buffer position
 * @param pos Position in ring
 * @param size Ring size
 * @return Wrapped position
 */
static inline uint64_t ipc_ring_pos(uint64_t pos, uint32_t size) {
    return pos % size;
}

/**
 * Check if ring buffer has space for message
 * @param head Head position
 * @param tail Tail position
 * @param size Ring size
 * @param msg_size Message size
 * @return 1 if space available, 0 if not
 */
static inline int ipc_has_space(uint64_t head, uint64_t tail, uint32_t size, uint32_t msg_size) {
    uint64_t used = head - tail;
    return (used + msg_size) <= size;
}

/**
 * Atomic increment with wraparound
 * @param ptr Pointer to atomic value
 * @param max Maximum value before wraparound
 * @return New value
 */
static inline uint64_t ipc_atomic_inc_wrap(atomic_uint64_t *ptr, uint64_t max) {
    uint64_t old_val, new_val;
    do {
        old_val = atomic_load(ptr);
        new_val = (old_val + 1) % max;
    } while (!atomic_compare_exchange_weak(ptr, &old_val, new_val));
    return new_val;
}

// Magic number for message validation
#define IPC_MAGIC 0x4845524F  // "HERO"

// Performance tuning constants
#define IPC_CACHE_LINE_SIZE 64
#define IPC_PREFETCH_DISTANCE 4

// Prefetch next cache line for better performance
static inline void ipc_prefetch(const void *ptr) {
    __builtin_prefetch(ptr, 0, 3);  // Read, high locality
}

#endif // HEROS_ZERO_COPY_IPC_H 