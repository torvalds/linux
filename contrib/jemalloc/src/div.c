#include "jemalloc/internal/jemalloc_preamble.h"

#include "jemalloc/internal/div.h"

#include "jemalloc/internal/assert.h"

/*
 * Suppose we have n = q * d, all integers. We know n and d, and want q = n / d.
 *
 * For any k, we have (here, all division is exact; not C-style rounding):
 * floor(ceil(2^k / d) * n / 2^k) = floor((2^k + r) / d * n / 2^k), where
 * r = (-2^k) mod d.
 *
 * Expanding this out:
 * ... = floor(2^k / d * n / 2^k + r / d * n / 2^k)
 *     = floor(n / d + (r / d) * (n / 2^k)).
 *
 * The fractional part of n / d is 0 (because of the assumption that d divides n
 * exactly), so we have:
 * ... = n / d + floor((r / d) * (n / 2^k))
 *
 * So that our initial expression is equal to the quantity we seek, so long as
 * (r / d) * (n / 2^k) < 1.
 *
 * r is a remainder mod d, so r < d and r / d < 1 always. We can make
 * n / 2 ^ k < 1 by setting k = 32. This gets us a value of magic that works.
 */

void
div_init(div_info_t *div_info, size_t d) {
	/* Nonsensical. */
	assert(d != 0);
	/*
	 * This would make the value of magic too high to fit into a uint32_t
	 * (we would want magic = 2^32 exactly). This would mess with code gen
	 * on 32-bit machines.
	 */
	assert(d != 1);

	uint64_t two_to_k = ((uint64_t)1 << 32);
	uint32_t magic = (uint32_t)(two_to_k / d);

	/*
	 * We want magic = ceil(2^k / d), but C gives us floor. We have to
	 * increment it unless the result was exact (i.e. unless d is a power of
	 * two).
	 */
	if (two_to_k % d != 0) {
		magic++;
	}
	div_info->magic = magic;
#ifdef JEMALLOC_DEBUG
	div_info->d = d;
#endif
}
