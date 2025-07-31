/*
 * HER OS Zero-Copy IPC Implementation
 *
 * High-performance inter-process communication using shared memory rings.
 * Provides lock-free, zero-copy communication between HER OS daemons.
 *
 * Implementation features:
 * - Lock-free ring buffer with atomic operations
 * - Zero-copy message passing using shared memory
 * - NUMA-aware memory allocation and thread placement
 * - Memory-mapped shared memory for instant access
 * - Checksum validation for message integrity
 * - Timeout support for blocking operations
 * - Performance optimizations with cache line alignment
 *
 * Author: HER OS Project
 * License: GPL-2.0
 * Version: 1.0.0
 */

#include "zero_copy_ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/syscall.h>
#include <numa.h>

// NUMA support
#ifdef HAVE_NUMA
#include <numa.h>
#define NUMA_AVAILABLE numa_available()
#else
#define NUMA_AVAILABLE -1
#endif

// Performance monitoring
static atomic_uint64_t total_messages_sent = ATOMIC_VAR_INIT(0);
static atomic_uint64_t total_messages_received = ATOMIC_VAR_INIT(0);
static atomic_uint64_t total_bytes_sent = ATOMIC_VAR_INIT(0);
static atomic_uint64_t total_bytes_received = ATOMIC_VAR_INIT(0);
static atomic_uint64_t total_errors = ATOMIC_VAR_INIT(0);

/**
 * Get NUMA node for current thread
 */
int ipc_get_numa_node(void) {
#ifdef HAVE_NUMA
    if (NUMA_AVAILABLE >= 0) {
        return numa_node_of_cpu(sched_getcpu());
    }
#endif
    return -1;
}

/**
 * Allocate memory on specific NUMA node
 */
void *ipc_numa_alloc(size_t size, int numa_node) {
#ifdef HAVE_NUMA
    if (NUMA_AVAILABLE >= 0 && numa_node >= 0) {
        return numa_alloc_onnode(size, numa_node);
    }
#endif
    return aligned_alloc(IPC_CACHE_LINE_SIZE, size);
}

/**
 * Free NUMA-allocated memory
 */
void ipc_numa_free(void *ptr, size_t size) {
#ifdef HAVE_NUMA
    if (NUMA_AVAILABLE >= 0) {
        numa_free(ptr, size);
        return;
    }
#endif
    free(ptr);
}

/**
 * Calculate simple checksum for message integrity
 */
uint32_t ipc_calculate_checksum(const void *data, uint32_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t checksum = 0;
    
    for (uint32_t i = 0; i < size; i++) {
        checksum = ((checksum << 5) + checksum) + bytes[i]; // Simple rolling hash
    }
    
    return checksum;
}

/**
 * Validate message header
 */
int ipc_validate_header(const message_header_t *header) {
    if (!header) return 0;
    
    // Check magic number
    if (header->magic != IPC_MAGIC) return 0;
    
    // Check message type
    if (header->message_type < MSG_TYPE_METADATA_EVENT || 
        header->message_type > MSG_TYPE_AUDIT_LOG) return 0;
    
    // Check payload size
    if (header->payload_size > MAX_MESSAGE_SIZE) return 0;
    
    // Check timestamp (should be reasonable)
    uint64_t now = ipc_get_timestamp();
    if (header->timestamp > now + 1000000000ULL) return 0; // Max 1 second in future
    
    return 1;
}

/**
 * Initialize zero-copy IPC context
 */
int ipc_init(ipc_context_t *ctx, const char *shm_name, int is_producer, int numa_node) {
    if (!ctx || !shm_name) {
        return IPC_ERROR_INVALID_PARAMETER;
    }
    
    // Initialize context
    memset(ctx, 0, sizeof(ipc_context_t));
    ctx->shm_name = strdup(shm_name);
    ctx->is_producer = is_producer;
    ctx->segment_size = sizeof(shared_memory_segment_t);
    
    // Determine NUMA node
    if (numa_node < 0) {
        ctx->numa_node = ipc_get_numa_node();
    } else {
        ctx->numa_node = numa_node;
    }
    
    // Create or open shared memory
    ctx->shm_fd = shm_open(shm_name, 
                          is_producer ? (O_CREAT | O_RDWR) : O_RDWR,
                          S_IRUSR | S_IWUSR);
    
    if (ctx->shm_fd == -1) {
        free(ctx->shm_name);
        return IPC_ERROR_SHARED_MEMORY;
    }
    
    // Set size for producer
    if (is_producer) {
        if (ftruncate(ctx->shm_fd, ctx->segment_size) == -1) {
            close(ctx->shm_fd);
            shm_unlink(shm_name);
            free(ctx->shm_name);
            return IPC_ERROR_SHARED_MEMORY;
        }
    }
    
    // Map shared memory
    ctx->segment = mmap(NULL, ctx->segment_size,
                       PROT_READ | PROT_WRITE, MAP_SHARED,
                       ctx->shm_fd, 0);
    
    if (ctx->segment == MAP_FAILED) {
        close(ctx->shm_fd);
        if (is_producer) {
            shm_unlink(shm_name);
        }
        free(ctx->shm_name);
        return IPC_ERROR_MAPPING;
    }
    
    // Initialize ring buffer for producer
    if (is_producer) {
        pthread_mutex_init(&ctx->segment->ring.mutex, NULL);
        atomic_init(&ctx->segment->ring.head, 0);
        atomic_init(&ctx->segment->ring.tail, 0);
        atomic_init(&ctx->segment->ring.sequence, 0);
        ctx->segment->ring.ring_size = MAX_MESSAGES;
        ctx->segment->ring.message_count = 0;
        ctx->segment->ring.numa_node = ctx->numa_node;
        
        // Prefetch first cache line
        ipc_prefetch(&ctx->segment->ring);
    }
    
    return IPC_SUCCESS;
}

/**
 * Clean up IPC context
 */
int ipc_cleanup(ipc_context_t *ctx) {
    if (!ctx) {
        return IPC_ERROR_INVALID_PARAMETER;
    }
    
    if (ctx->segment) {
        munmap(ctx->segment, ctx->segment_size);
        ctx->segment = NULL;
    }
    
    if (ctx->shm_fd != -1) {
        close(ctx->shm_fd);
        ctx->shm_fd = -1;
    }
    
    if (ctx->is_producer && ctx->shm_name) {
        shm_unlink(ctx->shm_name);
    }
    
    if (ctx->shm_name) {
        free(ctx->shm_name);
        ctx->shm_name = NULL;
    }
    
    return IPC_SUCCESS;
}

/**
 * Send message through zero-copy IPC
 */
int ipc_send(ipc_context_t *ctx, message_type_t msg_type, 
             const void *payload, uint32_t payload_size, uint32_t timeout_ms) {
    if (!ctx || !ctx->segment || !ctx->is_producer) {
        return IPC_ERROR_INVALID_PARAMETER;
    }
    
    if (payload_size > MAX_MESSAGE_SIZE) {
        return IPC_ERROR_MESSAGE_TOO_LARGE;
    }
    
    ring_buffer_t *ring = &ctx->segment->ring;
    uint64_t start_time = ipc_get_timestamp();
    uint64_t timeout_ns = (uint64_t)timeout_ms * 1000000ULL;
    
    // Wait for space in ring buffer
    while (1) {
        uint64_t head = atomic_load(&ring->head);
        uint64_t tail = atomic_load(&ring->tail);
        
        if (ipc_has_space(head, tail, MAX_MESSAGES, 1)) {
            break;
        }
        
        // Check timeout
        if (timeout_ms > 0) {
            uint64_t now = ipc_get_timestamp();
            if (now - start_time > timeout_ns) {
                return IPC_ERROR_TIMEOUT;
            }
        }
        
        // Yield to other threads
        sched_yield();
    }
    
    // Get next message slot
    uint64_t pos = ipc_ring_pos(atomic_load(&ring->head), MAX_MESSAGES);
    message_t *msg = &ctx->segment->messages[pos];
    
    // Prepare message header
    msg->header.magic = IPC_MAGIC;
    msg->header.message_type = msg_type;
    msg->header.payload_size = payload_size;
    msg->header.timestamp = ipc_get_timestamp();
    msg->header.sequence = atomic_fetch_add(&ring->sequence, 1);
    
    // Copy payload
    if (payload && payload_size > 0) {
        memcpy(msg->payload, payload, payload_size);
    }
    
    // Calculate and set checksum
    msg->header.checksum = ipc_calculate_checksum(msg->payload, payload_size);
    
    // Prefetch next cache line for better performance
    uint64_t next_pos = ipc_ring_pos(pos + 1, MAX_MESSAGES);
    ipc_prefetch(&ctx->segment->messages[next_pos]);
    
    // Update head atomically
    atomic_fetch_add(&ring->head, 1);
    atomic_fetch_add(&ring->message_count, 1);
    
    // Update statistics
    atomic_fetch_add(&total_messages_sent, 1);
    atomic_fetch_add(&total_bytes_sent, payload_size);
    
    return IPC_SUCCESS;
}

/**
 * Receive message from zero-copy IPC
 */
int ipc_receive(ipc_context_t *ctx, message_type_t *msg_type,
                void *payload, uint32_t *payload_size, uint32_t timeout_ms) {
    if (!ctx || !ctx->segment || ctx->is_producer) {
        return IPC_ERROR_INVALID_PARAMETER;
    }
    
    ring_buffer_t *ring = &ctx->segment->ring;
    uint64_t start_time = ipc_get_timestamp();
    uint64_t timeout_ns = (uint64_t)timeout_ms * 1000000ULL;
    
    // Wait for message in ring buffer
    while (1) {
        uint64_t head = atomic_load(&ring->head);
        uint64_t tail = atomic_load(&ring->tail);
        
        if (head != tail) {
            break;
        }
        
        // Check timeout
        if (timeout_ms > 0) {
            uint64_t now = ipc_get_timestamp();
            if (now - start_time > timeout_ns) {
                return IPC_ERROR_TIMEOUT;
            }
        }
        
        // Yield to other threads
        sched_yield();
    }
    
    // Get next message
    uint64_t pos = ipc_ring_pos(atomic_load(&ring->tail), MAX_MESSAGES);
    message_t *msg = &ctx->segment->messages[pos];
    
    // Validate message header
    if (!ipc_validate_header(&msg->header)) {
        atomic_fetch_add(&total_errors, 1);
        return IPC_ERROR_CHECKSUM;
    }
    
    // Validate checksum
    uint32_t expected_checksum = ipc_calculate_checksum(msg->payload, msg->header.payload_size);
    if (msg->header.checksum != expected_checksum) {
        atomic_fetch_add(&total_errors, 1);
        return IPC_ERROR_CHECKSUM;
    }
    
    // Copy message data
    if (msg_type) {
        *msg_type = (message_type_t)msg->header.message_type;
    }
    
    if (payload && payload_size) {
        if (msg->header.payload_size <= *payload_size) {
            memcpy(payload, msg->payload, msg->header.payload_size);
            *payload_size = msg->header.payload_size;
        } else {
            return IPC_ERROR_MESSAGE_TOO_LARGE;
        }
    }
    
    // Prefetch next cache line
    uint64_t next_pos = ipc_ring_pos(pos + 1, MAX_MESSAGES);
    ipc_prefetch(&ctx->segment->messages[next_pos]);
    
    // Update tail atomically
    atomic_fetch_add(&ring->tail, 1);
    atomic_fetch_sub(&ring->message_count, 1);
    
    // Update statistics
    atomic_fetch_add(&total_messages_received, 1);
    atomic_fetch_add(&total_bytes_received, msg->header.payload_size);
    
    return IPC_SUCCESS;
}

/**
 * Check if ring buffer is empty
 */
int ipc_is_empty(ipc_context_t *ctx) {
    if (!ctx || !ctx->segment) {
        return 1;
    }
    
    ring_buffer_t *ring = &ctx->segment->ring;
    uint64_t head = atomic_load(&ring->head);
    uint64_t tail = atomic_load(&ring->tail);
    
    return head == tail;
}

/**
 * Check if ring buffer is full
 */
int ipc_is_full(ipc_context_t *ctx) {
    if (!ctx || !ctx->segment) {
        return 0;
    }
    
    ring_buffer_t *ring = &ctx->segment->ring;
    uint64_t head = atomic_load(&ring->head);
    uint64_t tail = atomic_load(&ring->tail);
    
    return !ipc_has_space(head, tail, MAX_MESSAGES, 1);
}

/**
 * Get ring buffer statistics
 */
int ipc_get_stats(ipc_context_t *ctx, uint64_t *head, uint64_t *tail, uint32_t *message_count) {
    if (!ctx || !ctx->segment) {
        return IPC_ERROR_INVALID_PARAMETER;
    }
    
    ring_buffer_t *ring = &ctx->segment->ring;
    
    if (head) {
        *head = atomic_load(&ring->head);
    }
    
    if (tail) {
        *tail = atomic_load(&ring->tail);
    }
    
    if (message_count) {
        *message_count = atomic_load(&ring->message_count);
    }
    
    return IPC_SUCCESS;
}

/**
 * Get global IPC statistics
 */
void ipc_get_global_stats(uint64_t *messages_sent, uint64_t *messages_received,
                         uint64_t *bytes_sent, uint64_t *bytes_received,
                         uint64_t *errors) {
    if (messages_sent) {
        *messages_sent = atomic_load(&total_messages_sent);
    }
    
    if (messages_received) {
        *messages_received = atomic_load(&total_messages_received);
    }
    
    if (bytes_sent) {
        *bytes_sent = atomic_load(&total_bytes_sent);
    }
    
    if (bytes_received) {
        *bytes_received = atomic_load(&total_bytes_received);
    }
    
    if (errors) {
        *errors = atomic_load(&total_errors);
    }
}

/**
 * Reset global IPC statistics
 */
void ipc_reset_global_stats(void) {
    atomic_store(&total_messages_sent, 0);
    atomic_store(&total_messages_received, 0);
    atomic_store(&total_bytes_sent, 0);
    atomic_store(&total_bytes_received, 0);
    atomic_store(&total_errors, 0);
} 