/*
 * SpanDSP - a series of DSP components for telephony
 *
 * bit_operations.h - Various bit level operations, such as bit reversal
 *
 * Written by Steve Underwood <steveu@coppice.org>
 *
 * Copyright (C) 2006 Steve Underwood
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id: bit_operations.h,v 1.11 2006/11/28 15:37:03 steveu Exp $
 */

/*! \file */

#if !defined(_BIT_OPERATIONS_H_)
#define _BIT_OPERATIONS_H_

#if defined(__i386__)  ||  defined(__x86_64__)
/*! \brief Find the bit position of the highest set bit in a word
    \param bits The word to be searched
    \return The bit number of the highest set bit, or -1 if the word is zero. */
static __inline__ int top_bit(unsigned int bits)
{
	int res;

	__asm__(" xorl %[res],%[res];\n"
		" decl %[res];\n"
		" bsrl %[bits],%[res]\n"
		:[res] "=&r" (res)
		:[bits] "rm"(bits)
	);
	return res;
}

/*! \brief Find the bit position of the lowest set bit in a word
    \param bits The word to be searched
    \return The bit number of the lowest set bit, or -1 if the word is zero. */
static __inline__ int bottom_bit(unsigned int bits)
{
	int res;

	__asm__(" xorl %[res],%[res];\n"
		" decl %[res];\n"
		" bsfl %[bits],%[res]\n"
		:[res] "=&r" (res)
		:[bits] "rm"(bits)
	);
	return res;
}
#else
static __inline__ int top_bit(unsigned int bits)
{
	int i;

	if (bits == 0)
		return -1;
	i = 0;
	if (bits & 0xFFFF0000) {
		bits &= 0xFFFF0000;
		i += 16;
	}
	if (bits & 0xFF00FF00) {
		bits &= 0xFF00FF00;
		i += 8;
	}
	if (bits & 0xF0F0F0F0) {
		bits &= 0xF0F0F0F0;
		i += 4;
	}
	if (bits & 0xCCCCCCCC) {
		bits &= 0xCCCCCCCC;
		i += 2;
	}
	if (bits & 0xAAAAAAAA) {
		bits &= 0xAAAAAAAA;
		i += 1;
	}
	return i;
}

static __inline__ int bottom_bit(unsigned int bits)
{
	int i;

	if (bits == 0)
		return -1;
	i = 32;
	if (bits & 0x0000FFFF) {
		bits &= 0x0000FFFF;
		i -= 16;
	}
	if (bits & 0x00FF00FF) {
		bits &= 0x00FF00FF;
		i -= 8;
	}
	if (bits & 0x0F0F0F0F) {
		bits &= 0x0F0F0F0F;
		i -= 4;
	}
	if (bits & 0x33333333) {
		bits &= 0x33333333;
		i -= 2;
	}
	if (bits & 0x55555555) {
		bits &= 0x55555555;
		i -= 1;
	}
	return i;
}
#endif

/*! \brief Bit reverse a byte.
    \param data The byte to be reversed.
    \return The bit reversed version of data. */
static __inline__ uint8_t bit_reverse8(uint8_t x)
{
#if defined(__i386__)  ||  defined(__x86_64__)
	/* If multiply is fast */
	return ((x * 0x0802U & 0x22110U) | (x * 0x8020U & 0x88440U)) *
	    0x10101U >> 16;
#else
	/* If multiply is slow, but we have a barrel shifter */
	x = (x >> 4) | (x << 4);
	x = ((x & 0xCC) >> 2) | ((x & 0x33) << 2);
	return ((x & 0xAA) >> 1) | ((x & 0x55) << 1);
#endif
}

/*! \brief Bit reverse a 16 bit word.
    \param data The word to be reversed.
    \return The bit reversed version of data. */
uint16_t bit_reverse16(uint16_t data);

/*! \brief Bit reverse a 32 bit word.
    \param data The word to be reversed.
    \return The bit reversed version of data. */
uint32_t bit_reverse32(uint32_t data);

/*! \brief Bit reverse each of the four bytes in a 32 bit word.
    \param data The word to be reversed.
    \return The bit reversed version of data. */
uint32_t bit_reverse_4bytes(uint32_t data);

/*! \brief Find the number of set bits in a 32 bit word.
    \param x The word to be searched.
    \return The number of set bits. */
int one_bits32(uint32_t x);

/*! \brief Create a mask as wide as the number in a 32 bit word.
    \param x The word to be searched.
    \return The mask. */
uint32_t make_mask32(uint32_t x);

/*! \brief Create a mask as wide as the number in a 16 bit word.
    \param x The word to be searched.
    \return The mask. */
uint16_t make_mask16(uint16_t x);

/*! \brief Find the least significant one in a word, and return a word
           with just that bit set.
    \param x The word to be searched.
    \return The word with the single set bit. */
static __inline__ uint32_t least_significant_one32(uint32_t x)
{
	return (x & (-(int32_t) x));
}

/*! \brief Find the most significant one in a word, and return a word
           with just that bit set.
    \param x The word to be searched.
    \return The word with the single set bit. */
static __inline__ uint32_t most_significant_one32(uint32_t x)
{
#if defined(__i386__)  ||  defined(__x86_64__)
	return 1 << top_bit(x);
#else
	x = make_mask32(x);
	return (x ^ (x >> 1));
#endif
}

/*! \brief Find the parity of a byte.
    \param x The byte to be checked.
    \return 1 for odd, or 0 for even. */
static __inline__ int parity8(uint8_t x)
{
	x = (x ^ (x >> 4)) & 0x0F;
	return (0x6996 >> x) & 1;
}

/*! \brief Find the parity of a 16 bit word.
    \param x The word to be checked.
    \return 1 for odd, or 0 for even. */
static __inline__ int parity16(uint16_t x)
{
	x ^= (x >> 8);
	x = (x ^ (x >> 4)) & 0x0F;
	return (0x6996 >> x) & 1;
}

/*! \brief Find the parity of a 32 bit word.
    \param x The word to be checked.
    \return 1 for odd, or 0 for even. */
static __inline__ int parity32(uint32_t x)
{
	x ^= (x >> 16);
	x ^= (x >> 8);
	x = (x ^ (x >> 4)) & 0x0F;
	return (0x6996 >> x) & 1;
}

#endif
/*- End of file ------------------------------------------------------------*/
