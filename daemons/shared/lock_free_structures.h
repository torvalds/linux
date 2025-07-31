/*
 * HER OS Lock-Free Data Structures
 *
 * High-performance lock-free data structures for HER OS daemons.
 * Provides thread-safe, lock-free implementations of common data structures.
 *
 * Features:
 * - Lock-free ring buffer with atomic operations
 * - Lock-free hash table with open addressing
 * - Lock-free LRU cache with eviction
 * - Memory pool allocator for frequent allocations
 * - NUMA-aware memory allocation
 * - Cache line aligned structures
 * - Performance monitoring and statistics
 *
 * Author: HER OS Project
 * License: GPL-2.0
 * Version: 1.0.0
 */

#ifndef HEROS_LOCK_FREE_STRUCTURES_H
#define HEROS_LOCK_FREE_STRUCTURES_H

#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

// Cache line size for alignment
#define CACHE_LINE_SIZE 64
#define CACHE_LINE_ALIGN __attribute__((aligned(CACHE_LINE_SIZE)))

// Ring buffer configuration
#define LF_RING_SIZE 1024
#define LF_RING_MASK (LF_RING_SIZE - 1)

// Hash table configuration
#define LF_HASH_SIZE 1024
#define LF_HASH_LOAD_FACTOR 0.75
#define LF_MAX_PROBE_DISTANCE 16

// Cache configuration
#define LF_CACHE_SIZE 256
#define LF_CACHE_ENTRY_SIZE 64

// Memory pool configuration
#define LF_POOL_SIZE 1024
#define LF_POOL_BLOCK_SIZE 64

// Lock-free ring buffer
typedef struct {
    atomic_uint64_t head CACHE_LINE_ALIGN;
    atomic_uint64_t tail CACHE_LINE_ALIGN;
    atomic_uint64_t size CACHE_LINE_ALIGN;
    uint8_t *buffer;
    size_t buffer_size;
    size_t element_size;
} lf_ring_buffer_t;

// Lock-free hash table entry
typedef struct {
    atomic_uint64_t key CACHE_LINE_ALIGN;
    atomic_uint64_t value CACHE_LINE_ALIGN;
    atomic_uint64_t timestamp CACHE_LINE_ALIGN;
    atomic_bool occupied CACHE_LINE_ALIGN;
} lf_hash_entry_t;

// Lock-free hash table
typedef struct {
    lf_hash_entry_t *entries CACHE_LINE_ALIGN;
    atomic_uint64_t size CACHE_LINE_ALIGN;
    atomic_uint64_t count CACHE_LINE_ALIGN;
    atomic_uint64_t mask CACHE_LINE_ALIGN;
} lf_hash_table_t;

// Lock-free cache entry
typedef struct {
    atomic_uint64_t key CACHE_LINE_ALIGN;
    atomic_uint64_t value CACHE_LINE_ALIGN;
    atomic_uint64_t access_time CACHE_LINE_ALIGN;
    atomic_bool valid CACHE_LINE_ALIGN;
} lf_cache_entry_t;

// Lock-free LRU cache
typedef struct {
    lf_cache_entry_t *entries CACHE_LINE_ALIGN;
    atomic_uint64_t size CACHE_LINE_ALIGN;
    atomic_uint64_t count CACHE_LINE_ALIGN;
    atomic_uint64_t clock CACHE_LINE_ALIGN;
} lf_cache_t;

// Memory pool block
typedef struct lf_pool_block {
    struct lf_pool_block *next CACHE_LINE_ALIGN;
    uint8_t data[LF_POOL_BLOCK_SIZE] CACHE_LINE_ALIGN;
} lf_pool_block_t;

// Memory pool
typedef struct {
    lf_pool_block_t *free_list CACHE_LINE_ALIGN;
    atomic_uint64_t allocated CACHE_LINE_ALIGN;
    atomic_uint64_t total_blocks CACHE_LINE_ALIGN;
    pthread_mutex_t mutex;  // Only for initialization
} lf_memory_pool_t;

// Performance statistics
typedef struct {
    atomic_uint64_t operations CACHE_LINE_ALIGN;
    atomic_uint64_t hits CACHE_LINE_ALIGN;
    atomic_uint64_t misses CACHE_LINE_ALIGN;
    atomic_uint64_t evictions CACHE_LINE_ALIGN;
    atomic_uint64_t collisions CACHE_LINE_ALIGN;
} lf_stats_t;

// Function prototypes

// Ring Buffer Functions
int lf_ring_init(lf_ring_buffer_t *ring, size_t element_size, size_t capacity);
void lf_ring_cleanup(lf_ring_buffer_t *ring);
int lf_ring_push(lf_ring_buffer_t *ring, const void *element);
int lf_ring_pop(lf_ring_buffer_t *ring, void *element);
int lf_ring_is_empty(lf_ring_buffer_t *ring);
int lf_ring_is_full(lf_ring_buffer_t *ring);
size_t lf_ring_size(lf_ring_buffer_t *ring);

// Hash Table Functions
int lf_hash_init(lf_hash_table_t *table, size_t initial_size);
void lf_hash_cleanup(lf_hash_table_t *table);
int lf_hash_insert(lf_hash_table_t *table, uint64_t key, uint64_t value);
int lf_hash_lookup(lf_hash_table_t *table, uint64_t key, uint64_t *value);
int lf_hash_delete(lf_hash_table_t *table, uint64_t key);
size_t lf_hash_size(lf_hash_table_t *table);
double lf_hash_load_factor(lf_hash_table_t *table);

// Cache Functions
int lf_cache_init(lf_cache_t *cache, size_t size);
void lf_cache_cleanup(lf_cache_t *cache);
int lf_cache_get(lf_cache_t *cache, uint64_t key, uint64_t *value);
int lf_cache_put(lf_cache_t *cache, uint64_t key, uint64_t value);
int lf_cache_evict(lf_cache_t *cache, uint64_t *key, uint64_t *value);
size_t lf_cache_size(lf_cache_t *cache);
double lf_cache_hit_rate(lf_cache_t *cache);

// Memory Pool Functions
int lf_pool_init(lf_memory_pool_t *pool, size_t initial_blocks);
void lf_pool_cleanup(lf_memory_pool_t *pool);
void *lf_pool_alloc(lf_memory_pool_t *pool);
void lf_pool_free(lf_memory_pool_t *pool, void *ptr);
size_t lf_pool_allocated(lf_memory_pool_t *pool);
size_t lf_pool_total_blocks(lf_memory_pool_t *pool);

// Statistics Functions
void lf_stats_init(lf_stats_t *stats);
void lf_stats_reset(lf_stats_t *stats);
void lf_stats_get(lf_stats_t *stats, uint64_t *operations, uint64_t *hits, 
                  uint64_t *misses, uint64_t *evictions, uint64_t *collisions);

// Utility Functions
uint64_t lf_hash_function(uint64_t key);
uint64_t lf_get_timestamp(void);
void lf_prefetch(const void *ptr);
int lf_cas_uint64(atomic_uint64_t *ptr, uint64_t expected, uint64_t desired);

// Inline utility functions

/**
 * Get current timestamp in nanoseconds
 */
static inline uint64_t lf_get_timestamp(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/**
 * Prefetch next cache line for better performance
 */
static inline void lf_prefetch(const void *ptr) {
    __builtin_prefetch(ptr, 0, 3);  // Read, high locality
}

/**
 * Compare-and-swap for uint64_t with proper memory ordering
 */
static inline int lf_cas_uint64(atomic_uint64_t *ptr, uint64_t expected, uint64_t desired) {
    return atomic_compare_exchange_strong(ptr, &expected, desired);
}

/**
 * Simple hash function for uint64_t keys
 */
static inline uint64_t lf_hash_function(uint64_t key) {
    // FNV-1a hash function
    uint64_t hash = 0x811c9dc5;
    for (int i = 0; i < 8; i++) {
        hash ^= (key >> (i * 8)) & 0xFF;
        hash *= 0x01000193;
    }
    return hash;
}

/**
 * Check if a number is a power of 2
 */
static inline int lf_is_power_of_2(uint64_t n) {
    return n && !(n & (n - 1));
}

/**
 * Get next power of 2
 */
static inline uint64_t lf_next_power_of_2(uint64_t n) {
    if (lf_is_power_of_2(n)) return n;
    
    uint64_t power = 1;
    while (power < n) {
        power <<= 1;
    }
    return power;
}

/**
 * Calculate mask for power of 2 size
 */
static inline uint64_t lf_calculate_mask(uint64_t size) {
    return size - 1;
}

/**
 * Atomic increment with wraparound
 */
static inline uint64_t lf_atomic_inc_wrap(atomic_uint64_t *ptr, uint64_t max) {
    uint64_t old_val, new_val;
    do {
        old_val = atomic_load(ptr);
        new_val = (old_val + 1) % max;
    } while (!atomic_compare_exchange_weak(ptr, &old_val, new_val));
    return new_val;
}

/**
 * Memory barrier for load operations
 */
static inline void lf_load_barrier(void) {
    atomic_thread_fence(memory_order_acquire);
}

/**
 * Memory barrier for store operations
 */
static inline void lf_store_barrier(void) {
    atomic_thread_fence(memory_order_release);
}

/**
 * Full memory barrier
 */
static inline void lf_full_barrier(void) {
    atomic_thread_fence(memory_order_seq_cst);
}

// Performance tuning constants
#define LF_SPIN_COUNT 1000
#define LF_YIELD_THRESHOLD 100

/**
 * Spin-wait with exponential backoff
 */
static inline void lf_spin_wait(int iteration) {
    if (iteration < LF_SPIN_COUNT) {
        // Spin
        for (int i = 0; i < (1 << iteration); i++) {
            __builtin_ia32_pause();
        }
    } else if (iteration < LF_YIELD_THRESHOLD) {
        // Yield
        sched_yield();
    } else {
        // Sleep
        struct timespec ts = {0, 1000};  // 1 microsecond
        nanosleep(&ts, NULL);
    }
}

#endif // HEROS_LOCK_FREE_STRUCTURES_H 