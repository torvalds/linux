#ifndef JEMALLOC_INTERNAL_DIV_H
#define JEMALLOC_INTERNAL_DIV_H

#include "jemalloc/internal/assert.h"

/*
 * This module does the division that computes the index of a region in a slab,
 * given its offset relative to the base.
 * That is, given a divisor d, an n = i * d (all integers), we'll return i.
 * We do some pre-computation to do this more quickly than a CPU division
 * instruction.
 * We bound n < 2^32, and don't support dividing by one.
 */

typedef struct div_info_s div_info_t;
struct div_info_s {
	uint32_t magic;
#ifdef JEMALLOC_DEBUG
	size_t d;
#endif
};

void div_init(div_info_t *div_info, size_t divisor);

static inline size_t
div_compute(div_info_t *div_info, size_t n) {
	assert(n <= (uint32_t)-1);
	/*
	 * This generates, e.g. mov; imul; shr on x86-64. On a 32-bit machine,
	 * the compilers I tried were all smart enough to turn this into the
	 * appropriate "get the high 32 bits of the result of a multiply" (e.g.
	 * mul; mov edx eax; on x86, umull on arm, etc.).
	 */
	size_t i = ((uint64_t)n * (uint64_t)div_info->magic) >> 32;
#ifdef JEMALLOC_DEBUG
	assert(i * div_info->d == n);
#endif
	return i;
}

#endif /* JEMALLOC_INTERNAL_DIV_H */
