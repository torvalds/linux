/*
 *
 * (C) COPYRIGHT 2008-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file mali_osk_bitops.h
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#ifndef _OSK_BITOPS_H_
#define _OSK_BITOPS_H_

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#include <osk/mali_osk_arch_bitops.h>

/**
 * @addtogroup base_api
 * @{
 */

/**
 * @addtogroup base_osk_api
 * @{
 */

/** @defgroup oskbitops Bit-operations
 *
 * These bit-operations do not work atomically, and so locks must be used if
 * atomicity is required.
 *
 * Reference implementations for Little Endian are provided, and so it should
 * not normally be necessary to re-implement these. Efficient bit-twiddling
 * techniques are used where possible, implemented in portable C.
 *
 * Note that these reference implementations rely on osk_clz() being
 * implemented.
 *
 * @{
 */

/**
 * @brief Tests if a bit is set in a unsigned long value (internal function)
 * @param[in] bit  bit number to test [0..OSK_BITS_PER_LONG-1], starting from the (Little-endian) least significant bit
 * @param[in] value unsigned long value
 * @return zero if bit was clear, non-zero if set. Do not rely on the return
 * value being related to the actual word under test.
 */
OSK_STATIC_INLINE unsigned long oskp_test_bit(unsigned long bit, unsigned long value)
{
	OSK_ASSERT( bit < OSK_BITS_PER_LONG );

	return value & (1UL << bit);
}

/**
 * @brief Find the first zero bit in a unsigned long value
 * @param[in] value unsigned long value
 * @return a postive number [0..OSK_BITS_PER_LONG-1], starting from the least significant bit,
 * indicating the first zero bit found, or a negative number if no zero bit was found.
 */
CHECK_RESULT OSK_STATIC_INLINE long oskp_find_first_zero_bit(unsigned long value)
{
	unsigned long inverted;
	unsigned long negated;
	unsigned long isolated;
	unsigned long leading_zeros;

	/* Begin with xxx...x0yyy...y, where ys are 1, number of ys is in range  0..31/63 */
	inverted = ~value; /* zzz...z1000...0 */
	/* Using count_trailing_zeros on inverted value -
	 * See ARM System Developers Guide for details of count_trailing_zeros */

	/* Isolate the zero: it is preceeded by a run of 1s, so add 1 to it */
	negated = (unsigned long)-inverted ; /* -a == ~a + 1 (mod 2^n) for n-bit numbers */
	/* negated = xxx...x1000...0 */

	isolated = negated & inverted ; /* xxx...x1000...0 & zzz...z1000...0, zs are ~xs */
	/* And so the first zero bit is in the same position as the 1 == number of 1s that preceeded it
	 * Note that the output is zero if value was all 1s */

	leading_zeros = osk_clz( isolated );

	return (OSK_BITS_PER_LONG - 1) - leading_zeros;
}

/**
 * @brief Clear a bit in a sequence of unsigned longs
 * @param[in] nr       bit number to clear, starting from the (Little-endian) least
 *                     significant bit
 * @param[in,out] addr starting point for counting.
 */
OSK_STATIC_INLINE void osk_bitarray_clear_bit(unsigned long nr, unsigned long *addr )
{
	OSK_ASSERT(NULL != addr);
	addr += nr / OSK_BITS_PER_LONG; /* find the correct word */
	nr = nr & (OSK_BITS_PER_LONG - 1); /* The bit number within the word */
	*addr &= ~(1UL << nr);
}

/**
 * @brief Set a bit in a sequence of unsigned longs
 * @param[in] nr       bit number to set, starting from the (Little-endian) least
 *                     significant bit
 * @param[in,out] addr starting point for counting.
 */
OSK_STATIC_INLINE void osk_bitarray_set_bit(unsigned long nr, unsigned long *addr)
{
	OSK_ASSERT(NULL != addr);
	addr += nr / OSK_BITS_PER_LONG; /* find the correct word */
	nr = nr & (OSK_BITS_PER_LONG - 1); /* The bit number within the word */
	*addr |= (1UL << nr);
}

/**
 * @brief Test a bit in a sequence of unsigned longs
 * @param[in] nr       bit number to test, starting from the (Little-endian) least
 *                     significant bit
 * @param[in,out] addr starting point for counting.
 * @return zero if bit was clear, non-zero if set. Do not rely on the return
 * value being related to the actual word under test.
 */
CHECK_RESULT OSK_STATIC_INLINE unsigned long osk_bitarray_test_bit(unsigned long nr, unsigned long *addr)
{
	OSK_ASSERT(NULL != addr);
	addr += nr / OSK_BITS_PER_LONG; /* find the correct word */
	nr = nr & (OSK_BITS_PER_LONG - 1); /* The bit number within the word */
	return *addr & (1UL << nr);
}

/**
 * @brief Find the first zero bit in a sequence of unsigned longs
 * @param[in] addr   starting point for search.
 * @param[in] maxbit the maximum number of bits to search
 * @return the number of the first zero bit found, or maxbit if none were found
 * in the specified range.
 */
CHECK_RESULT unsigned long osk_bitarray_find_first_zero_bit(const unsigned long *addr, unsigned long maxbit);

/**
 * @brief Find the first set bit in a unsigned long
 * @param val value to find first set bit in
 * @return the number of the set bit found (starting from 0), -1 if no bits set
 */
CHECK_RESULT OSK_STATIC_INLINE long osk_find_first_set_bit(unsigned long val)
{
	return (OSK_BITS_PER_LONG - 1) - osk_clz( val & -val );
}

/**
 * @brief Count leading zeros in an unsigned long
 *
 * Same behavior as ARM CLZ instruction.
 *
 * Returns the number of binary zero bits before the first (most significant)
 * binary one bit in \a val.
 *
 * If \a val is zero, this function returns the number of bits in an unsigned
 * long, ie. sizeof(unsigned long) * 8.
 *
 * @param val unsigned long value to count leading zeros in
 * @return the number of leading zeros
 */
CHECK_RESULT OSK_STATIC_INLINE long osk_clz(unsigned long val);

/**
 * @brief Count leading zeros in an u64
 *
 * Same behavior as ARM CLZ instruction.
 *
 * Returns the number of binary zero bits before the first (most significant)
 * binary one bit in \a val.
 *
 * If \a val is zero, this function returns the number of bits in an u64,
 * ie. sizeof(u64) * 8 = 64
 *
 * Note that on platforms where an unsigned long is 64 bits then this is the same as osk_clz.
 *
 * @param val value to count leading zeros in
 * @return the number of leading zeros
 */
CHECK_RESULT OSK_STATIC_INLINE long osk_clz_64(u64 val);

/**
 * @brief Count the number of bits set in an unsigned long
 *
 * This returns the number of bits set in a unsigned long value.
 *
 * @param val The value to count bits set in
 * @return The number of bits set in \c val.
 */
OSK_STATIC_INLINE int osk_count_set_bits(unsigned long val) CHECK_RESULT;

/**
 * @brief Count the number of bits set in an u64
 *
 * This returns the number of bits set in a u64 value.
 *
 * @param val The value to count bits set in
 * @return The number of bits set in \c val.
 */
CHECK_RESULT OSK_STATIC_INLINE int osk_count_set_bits64(u64 val)
{
	return osk_count_set_bits(val & U32_MAX)
	     + osk_count_set_bits((val >> 32) & U32_MAX);
}

/** @} */ /* end group oskbitops */

/** @} */ /* end group base_osk_api */

/** @} */ /* end group base_api */

#ifdef __cplusplus
}
#endif

#endif /* _OSK_BITOPS_H_ */
