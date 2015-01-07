/*
 * Copyright (C) 2010, 2013-2014 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_osk_bitops.h
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#ifndef __MALI_OSK_BITOPS_H__
#define __MALI_OSK_BITOPS_H__

#ifdef __cplusplus
extern "C" {
#endif

MALI_STATIC_INLINE void _mali_internal_clear_bit(u32 bit, u32 *addr)
{
	MALI_DEBUG_ASSERT(bit < 32);
	MALI_DEBUG_ASSERT(NULL != addr);

	(*addr) &= ~(1 << bit);
}

MALI_STATIC_INLINE void _mali_internal_set_bit(u32 bit, u32 *addr)
{
	MALI_DEBUG_ASSERT(bit < 32);
	MALI_DEBUG_ASSERT(NULL != addr);

	(*addr) |= (1 << bit);
}

MALI_STATIC_INLINE u32 _mali_internal_test_bit(u32 bit, u32 value)
{
	MALI_DEBUG_ASSERT(bit < 32);
	return value & (1 << bit);
}

MALI_STATIC_INLINE int _mali_internal_find_first_zero_bit(u32 value)
{
	u32 inverted;
	u32 negated;
	u32 isolated;
	u32 leading_zeros;

	/* Begin with xxx...x0yyy...y, where ys are 1, number of ys is in range  0..31 */
	inverted = ~value; /* zzz...z1000...0 */
	/* Using count_trailing_zeros on inverted value -
	 * See ARM System Developers Guide for details of count_trailing_zeros */

	/* Isolate the zero: it is preceeded by a run of 1s, so add 1 to it */
	negated = (u32) - inverted ; /* -a == ~a + 1 (mod 2^n) for n-bit numbers */
	/* negated = xxx...x1000...0 */

	isolated = negated & inverted ; /* xxx...x1000...0 & zzz...z1000...0, zs are ~xs */
	/* And so the first zero bit is in the same position as the 1 == number of 1s that preceeded it
	 * Note that the output is zero if value was all 1s */

	leading_zeros = _mali_osk_clz(isolated);

	return 31 - leading_zeros;
}


/** @defgroup _mali_osk_bitops OSK Non-atomic Bit-operations
 * @{ */

/**
 * These bit-operations do not work atomically, and so locks must be used if
 * atomicity is required.
 *
 * Reference implementations for Little Endian are provided, and so it should
 * not normally be necessary to re-implement these. Efficient bit-twiddling
 * techniques are used where possible, implemented in portable C.
 *
 * Note that these reference implementations rely on _mali_osk_clz() being
 * implemented.
 */

/** @brief Clear a bit in a sequence of 32-bit words
 * @param nr bit number to clear, starting from the (Little-endian) least
 * significant bit
 * @param addr starting point for counting.
 */
MALI_STATIC_INLINE void _mali_osk_clear_nonatomic_bit(u32 nr, u32 *addr)
{
	addr += nr >> 5; /* find the correct word */
	nr = nr & ((1 << 5) - 1); /* The bit number within the word */

	_mali_internal_clear_bit(nr, addr);
}

/** @brief Set a bit in a sequence of 32-bit words
 * @param nr bit number to set, starting from the (Little-endian) least
 * significant bit
 * @param addr starting point for counting.
 */
MALI_STATIC_INLINE void _mali_osk_set_nonatomic_bit(u32 nr, u32 *addr)
{
	addr += nr >> 5; /* find the correct word */
	nr = nr & ((1 << 5) - 1); /* The bit number within the word */

	_mali_internal_set_bit(nr, addr);
}

/** @brief Test a bit in a sequence of 32-bit words
 * @param nr bit number to test, starting from the (Little-endian) least
 * significant bit
 * @param addr starting point for counting.
 * @return zero if bit was clear, non-zero if set. Do not rely on the return
 * value being related to the actual word under test.
 */
MALI_STATIC_INLINE u32 _mali_osk_test_bit(u32 nr, u32 *addr)
{
	addr += nr >> 5; /* find the correct word */
	nr = nr & ((1 << 5) - 1); /* The bit number within the word */

	return _mali_internal_test_bit(nr, *addr);
}

/* Return maxbit if not found */
/** @brief Find the first zero bit in a sequence of 32-bit words
 * @param addr starting point for search.
 * @param maxbit the maximum number of bits to search
 * @return the number of the first zero bit found, or maxbit if none were found
 * in the specified range.
 */
MALI_STATIC_INLINE u32 _mali_osk_find_first_zero_bit(const u32 *addr, u32 maxbit)
{
	u32 total;

	for (total = 0; total < maxbit; total += 32, ++addr) {
		int result;
		result = _mali_internal_find_first_zero_bit(*addr);

		/* non-negative signifies the bit was found */
		if (result >= 0) {
			total += (u32)result;
			break;
		}
	}

	/* Now check if we reached maxbit or above */
	if (total >= maxbit) {
		total = maxbit;
	}

	return total; /* either the found bit nr, or maxbit if not found */
}
/** @} */ /* end group _mali_osk_bitops */

#ifdef __cplusplus
}
#endif

#endif /* __MALI_OSK_BITOPS_H__ */
