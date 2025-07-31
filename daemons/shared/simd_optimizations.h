/*
 * HER OS SIMD Optimizations
 *
 * High-performance SIMD optimizations for HER OS daemons.
 * Provides AVX2/AVX-512 implementations for common operations.
 *
 * Features:
 * - SIMD-optimized string validation and matching
 * - SIMD-optimized hash computation (BLAKE3, SHA-256)
 * - SIMD-optimized vector similarity search
 * - SIMD-optimized JSON parsing for common patterns
 * - Runtime CPU detection and fallback
 * - Cache-friendly memory access patterns
 * - Performance monitoring and benchmarking
 *
 * Author: HER OS Project
 * License: GPL-2.0
 * Version: 1.0.0
 */

#ifndef HEROS_SIMD_OPTIMIZATIONS_H
#define HEROS_SIMD_OPTIMIZATIONS_H

#include <stdint.h>
#include <stddef.h>
#include <immintrin.h>
#include <cpuid.h>

// SIMD instruction set detection
typedef enum {
    SIMD_NONE = 0,
    SIMD_SSE2 = 1,
    SIMD_SSE4_1 = 2,
    SIMD_AVX = 3,
    SIMD_AVX2 = 4,
    SIMD_AVX512 = 5
} simd_level_t;

// SIMD configuration
#define SIMD_VECTOR_SIZE_256 32
#define SIMD_VECTOR_SIZE_512 64
#define SIMD_ALIGNMENT 64

// String validation patterns
typedef struct {
    __m256i pattern;
    __m256i mask;
    size_t length;
} simd_string_pattern_t;

// Vector similarity search context
typedef struct {
    __m256i *vectors;
    size_t vector_count;
    size_t vector_dimensions;
    float *similarities;
} simd_vector_context_t;

// Hash computation context
typedef struct {
    __m256i state[8];
    __m256i block[16];
    size_t total_length;
    size_t block_count;
} simd_hash_context_t;

// Performance statistics
typedef struct {
    uint64_t operations;
    uint64_t simd_operations;
    uint64_t fallback_operations;
    uint64_t cache_misses;
    double simd_utilization;
} simd_stats_t;

// Function prototypes

// CPU Detection and Initialization
simd_level_t simd_detect_cpu(void);
void simd_init(void);
const char *simd_level_name(simd_level_t level);

// String Operations
int simd_string_validate(const char *str, size_t length, const simd_string_pattern_t *patterns, size_t pattern_count);
int simd_string_match(const char *str, size_t length, const char *pattern, size_t pattern_length);
int simd_string_contains(const char *str, size_t length, const char *substr, size_t substr_length);
int simd_string_replace(char *str, size_t length, char old_char, char new_char);
size_t simd_string_count(const char *str, size_t length, char target);

// Vector Operations
int simd_vector_similarity_search(const float *query, const float *vectors, size_t vector_count, 
                                 size_t dimensions, float *similarities, size_t top_k);
int simd_vector_add(const float *a, const float *b, float *result, size_t count);
int simd_vector_multiply(const float *a, const float *b, float *result, size_t count);
int simd_vector_dot_product(const float *a, const float *b, float *result, size_t count);
int simd_vector_normalize(float *vector, size_t dimensions);

// Hash Operations
int simd_hash_init(simd_hash_context_t *ctx);
int simd_hash_update(simd_hash_context_t *ctx, const void *data, size_t length);
int simd_hash_final(simd_hash_context_t *ctx, uint8_t *hash, size_t hash_size);
int simd_blake3_hash(const void *data, size_t length, uint8_t *hash, size_t hash_size);
int simd_sha256_hash(const void *data, size_t length, uint8_t *hash, size_t hash_size);

// JSON Operations
int simd_json_parse_simple(const char *json, size_t length, char *key, char *value);
int simd_json_extract_field(const char *json, size_t length, const char *field_name, char *value);
int simd_json_validate_syntax(const char *json, size_t length);
int simd_json_count_fields(const char *json, size_t length);

// Memory Operations
void *simd_aligned_alloc(size_t size);
void simd_aligned_free(void *ptr);
int simd_memcpy(void *dest, const void *src, size_t size);
int simd_memset(void *ptr, int value, size_t size);
int simd_memcmp(const void *ptr1, const void *ptr2, size_t size);

// Statistics and Monitoring
void simd_stats_init(simd_stats_t *stats);
void simd_stats_reset(simd_stats_t *stats);
void simd_stats_get(simd_stats_t *stats, uint64_t *operations, uint64_t *simd_ops, 
                    uint64_t *fallback_ops, uint64_t *cache_misses, double *utilization);

// Utility Functions
uint64_t simd_get_timestamp(void);
void simd_prefetch(const void *ptr);
int simd_is_aligned(const void *ptr, size_t alignment);

// Inline utility functions

/**
 * Get current timestamp in nanoseconds
 */
static inline uint64_t simd_get_timestamp(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/**
 * Prefetch data for SIMD operations
 */
static inline void simd_prefetch(const void *ptr) {
    __builtin_prefetch(ptr, 0, 3);  // Read, high locality
}

/**
 * Check if pointer is aligned
 */
static inline int simd_is_aligned(const void *ptr, size_t alignment) {
    return ((uintptr_t)ptr & (alignment - 1)) == 0;
}

/**
 * Align pointer to specified alignment
 */
static inline void *simd_align_ptr(const void *ptr, size_t alignment) {
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    return (void *)aligned;
}

/**
 * Get SIMD level name
 */
static inline const char *simd_level_name(simd_level_t level) {
    switch (level) {
        case SIMD_NONE: return "NONE";
        case SIMD_SSE2: return "SSE2";
        case SIMD_SSE4_1: return "SSE4.1";
        case SIMD_AVX: return "AVX";
        case SIMD_AVX2: return "AVX2";
        case SIMD_AVX512: return "AVX512";
        default: return "UNKNOWN";
    }
}

// SIMD instruction set detection
static inline simd_level_t simd_detect_cpu(void) {
    unsigned int eax, ebx, ecx, edx;
    
    // Check for basic CPUID support
    if (!__get_cpuid(0, &eax, &ebx, &ecx, &edx)) {
        return SIMD_NONE;
    }
    
    // Check for SSE2
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        return SIMD_NONE;
    }
    
    if (!(edx & (1 << 26))) {  // SSE2 bit
        return SIMD_NONE;
    }
    
    // Check for SSE4.1
    if (!(ecx & (1 << 19))) {  // SSE4.1 bit
        return SIMD_SSE2;
    }
    
    // Check for AVX
    if (!(ecx & (1 << 28))) {  // AVX bit
        return SIMD_SSE4_1;
    }
    
    // Check for AVX2
    if (!__get_cpuid(7, &eax, &ebx, &ecx, &edx)) {
        return SIMD_AVX;
    }
    
    if (!(ebx & (1 << 5))) {   // AVX2 bit
        return SIMD_AVX;
    }
    
    // Check for AVX-512
    if (!(ebx & (1 << 16))) {  // AVX-512F bit
        return SIMD_AVX2;
    }
    
    return SIMD_AVX512;
}

// SIMD-optimized string operations

/**
 * SIMD-optimized string length calculation
 */
static inline size_t simd_strlen_avx2(const char *str) {
    const char *ptr = str;
    
    // Align to 32-byte boundary
    while ((uintptr_t)ptr & 31) {
        if (*ptr == '\0') return ptr - str;
        ptr++;
    }
    
    // Process 32 bytes at a time
    while (1) {
        __m256i chunk = _mm256_load_si256((__m256i *)ptr);
        __m256i zeros = _mm256_setzero_si256();
        __m256i cmp = _mm256_cmpeq_epi8(chunk, zeros);
        int mask = _mm256_movemask_epi8(cmp);
        
        if (mask != 0) {
            // Found null terminator
            int index = __builtin_ctz(mask);
            return ptr + index - str;
        }
        
        ptr += 32;
    }
}

/**
 * SIMD-optimized character search
 */
static inline const char *simd_memchr_avx2(const char *str, char target, size_t length) {
    const char *ptr = str;
    const char *end = str + length;
    
    // Align to 32-byte boundary
    while ((uintptr_t)ptr & 31 && ptr < end) {
        if (*ptr == target) return ptr;
        ptr++;
    }
    
    // Process 32 bytes at a time
    __m256i target_vec = _mm256_set1_epi8(target);
    
    while (ptr + 32 <= end) {
        __m256i chunk = _mm256_load_si256((__m256i *)ptr);
        __m256i cmp = _mm256_cmpeq_epi8(chunk, target_vec);
        int mask = _mm256_movemask_epi8(cmp);
        
        if (mask != 0) {
            int index = __builtin_ctz(mask);
            return ptr + index;
        }
        
        ptr += 32;
    }
    
    // Handle remaining bytes
    while (ptr < end) {
        if (*ptr == target) return ptr;
        ptr++;
    }
    
    return NULL;
}

/**
 * SIMD-optimized string comparison
 */
static inline int simd_strcmp_avx2(const char *str1, const char *str2) {
    const char *ptr1 = str1;
    const char *ptr2 = str2;
    
    // Align to 32-byte boundary
    while ((uintptr_t)ptr1 & 31) {
        if (*ptr1 != *ptr2) return (*ptr1 < *ptr2) ? -1 : 1;
        if (*ptr1 == '\0') return 0;
        ptr1++;
        ptr2++;
    }
    
    // Process 32 bytes at a time
    while (1) {
        __m256i chunk1 = _mm256_load_si256((__m256i *)ptr1);
        __m256i chunk2 = _mm256_load_si256((__m256i *)ptr2);
        __m256i cmp = _mm256_cmpeq_epi8(chunk1, chunk2);
        int mask = _mm256_movemask_epi8(cmp);
        
        if (mask != 0xFFFFFFFF) {
            // Found difference
            int index = __builtin_ctz(~mask);
            return (ptr1[index] < ptr2[index]) ? -1 : 1;
        }
        
        // Check for null terminator
        __m256i zeros = _mm256_setzero_si256();
        __m256i null_cmp = _mm256_cmpeq_epi8(chunk1, zeros);
        int null_mask = _mm256_movemask_epi8(null_cmp);
        
        if (null_mask != 0) {
            // Found null terminator
            return 0;
        }
        
        ptr1 += 32;
        ptr2 += 32;
    }
}

// SIMD-optimized memory operations

/**
 * SIMD-optimized memory copy
 */
static inline void simd_memcpy_avx2(void *dest, const void *src, size_t size) {
    char *d = (char *)dest;
    const char *s = (const char *)src;
    
    // Align destination to 32-byte boundary
    while ((uintptr_t)d & 31 && size > 0) {
        *d++ = *s++;
        size--;
    }
    
    // Copy 32 bytes at a time
    while (size >= 32) {
        __m256i chunk = _mm256_loadu_si256((__m256i *)s);
        _mm256_store_si256((__m256i *)d, chunk);
        d += 32;
        s += 32;
        size -= 32;
    }
    
    // Copy remaining bytes
    while (size > 0) {
        *d++ = *s++;
        size--;
    }
}

/**
 * SIMD-optimized memory set
 */
static inline void simd_memset_avx2(void *ptr, int value, size_t size) {
    char *p = (char *)ptr;
    
    // Align to 32-byte boundary
    while ((uintptr_t)p & 31 && size > 0) {
        *p++ = value;
        size--;
    }
    
    // Set 32 bytes at a time
    __m256i val_vec = _mm256_set1_epi8(value);
    
    while (size >= 32) {
        _mm256_store_si256((__m256i *)p, val_vec);
        p += 32;
        size -= 32;
    }
    
    // Set remaining bytes
    while (size > 0) {
        *p++ = value;
        size--;
    }
}

// Performance monitoring macros
#define SIMD_START_TIMER() uint64_t simd_start_time = simd_get_timestamp()
#define SIMD_END_TIMER() uint64_t simd_end_time = simd_get_timestamp()
#define SIMD_GET_DURATION() (simd_end_time - simd_start_time)

// SIMD operation counters
#define SIMD_OP_COUNT() __sync_fetch_and_add(&simd_operation_count, 1)
#define SIMD_FALLBACK_COUNT() __sync_fetch_and_add(&simd_fallback_count, 1)

// Global statistics
extern uint64_t simd_operation_count;
extern uint64_t simd_fallback_count;
extern simd_level_t current_simd_level;

#endif // HEROS_SIMD_OPTIMIZATIONS_H 