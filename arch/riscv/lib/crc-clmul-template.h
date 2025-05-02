/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2025 Google LLC */

/*
 * This file is a "template" that generates a CRC function optimized using the
 * RISC-V Zbc (scalar carryless multiplication) extension.  The includer of this
 * file must define the following parameters to specify the type of CRC:
 *
 *	crc_t: the data type of the CRC, e.g. u32 for a 32-bit CRC
 *	LSB_CRC: 0 for a msb (most-significant-bit) first CRC, i.e. natural
 *		 mapping between bits and polynomial coefficients
 *	         1 for a lsb (least-significant-bit) first CRC, i.e. reflected
 *	         mapping between bits and polynomial coefficients
 */

#include <asm/byteorder.h>
#include <linux/minmax.h>

#define CRC_BITS	(8 * sizeof(crc_t))	/* a.k.a. 'n' */

static inline unsigned long clmul(unsigned long a, unsigned long b)
{
	unsigned long res;

	asm(".option push\n"
	    ".option arch,+zbc\n"
	    "clmul %0, %1, %2\n"
	    ".option pop\n"
	    : "=r" (res) : "r" (a), "r" (b));
	return res;
}

static inline unsigned long clmulh(unsigned long a, unsigned long b)
{
	unsigned long res;

	asm(".option push\n"
	    ".option arch,+zbc\n"
	    "clmulh %0, %1, %2\n"
	    ".option pop\n"
	    : "=r" (res) : "r" (a), "r" (b));
	return res;
}

static inline unsigned long clmulr(unsigned long a, unsigned long b)
{
	unsigned long res;

	asm(".option push\n"
	    ".option arch,+zbc\n"
	    "clmulr %0, %1, %2\n"
	    ".option pop\n"
	    : "=r" (res) : "r" (a), "r" (b));
	return res;
}

/*
 * crc_load_long() loads one "unsigned long" of aligned data bytes, producing a
 * polynomial whose bit order matches the CRC's bit order.
 */
#ifdef CONFIG_64BIT
#  if LSB_CRC
#    define crc_load_long(x)	le64_to_cpup(x)
#  else
#    define crc_load_long(x)	be64_to_cpup(x)
#  endif
#else
#  if LSB_CRC
#    define crc_load_long(x)	le32_to_cpup(x)
#  else
#    define crc_load_long(x)	be32_to_cpup(x)
#  endif
#endif

/* XOR @crc into the end of @msgpoly that represents the high-order terms. */
static inline unsigned long
crc_clmul_prep(crc_t crc, unsigned long msgpoly)
{
#if LSB_CRC
	return msgpoly ^ crc;
#else
	return msgpoly ^ ((unsigned long)crc << (BITS_PER_LONG - CRC_BITS));
#endif
}

/*
 * Multiply the long-sized @msgpoly by x^n (a.k.a. x^CRC_BITS) and reduce it
 * modulo the generator polynomial G.  This gives the CRC of @msgpoly.
 */
static inline crc_t
crc_clmul_long(unsigned long msgpoly, const struct crc_clmul_consts *consts)
{
	unsigned long tmp;

	/*
	 * First step of Barrett reduction with integrated multiplication by
	 * x^n: calculate floor((msgpoly * x^n) / G).  This is the value by
	 * which G needs to be multiplied to cancel out the x^n and higher terms
	 * of msgpoly * x^n.  Do it using the following formula:
	 *
	 * msb-first:
	 *    floor((msgpoly * floor(x^(BITS_PER_LONG-1+n) / G)) / x^(BITS_PER_LONG-1))
	 * lsb-first:
	 *    floor((msgpoly * floor(x^(BITS_PER_LONG-1+n) / G) * x) / x^BITS_PER_LONG)
	 *
	 * barrett_reduction_const_1 contains floor(x^(BITS_PER_LONG-1+n) / G),
	 * which fits a long exactly.  Using any lower power of x there would
	 * not carry enough precision through the calculation, while using any
	 * higher power of x would require extra instructions to handle a wider
	 * multiplication.  In the msb-first case, using this power of x results
	 * in needing a floored division by x^(BITS_PER_LONG-1), which matches
	 * what clmulr produces.  In the lsb-first case, a factor of x gets
	 * implicitly introduced by each carryless multiplication (shown as
	 * '* x' above), and the floored division instead needs to be by
	 * x^BITS_PER_LONG which matches what clmul produces.
	 */
#if LSB_CRC
	tmp = clmul(msgpoly, consts->barrett_reduction_const_1);
#else
	tmp = clmulr(msgpoly, consts->barrett_reduction_const_1);
#endif

	/*
	 * Second step of Barrett reduction:
	 *
	 *    crc := (msgpoly * x^n) + (G * floor((msgpoly * x^n) / G))
	 *
	 * This reduces (msgpoly * x^n) modulo G by adding the appropriate
	 * multiple of G to it.  The result uses only the x^0..x^(n-1) terms.
	 * HOWEVER, since the unreduced value (msgpoly * x^n) is zero in those
	 * terms in the first place, it is more efficient to do the equivalent:
	 *
	 *    crc := ((G - x^n) * floor((msgpoly * x^n) / G)) mod x^n
	 *
	 * In the lsb-first case further modify it to the following which avoids
	 * a shift, as the crc ends up in the physically low n bits from clmulr:
	 *
	 *    product := ((G - x^n) * x^(BITS_PER_LONG - n)) * floor((msgpoly * x^n) / G) * x
	 *    crc := floor(product / x^(BITS_PER_LONG + 1 - n)) mod x^n
	 *
	 * barrett_reduction_const_2 contains the constant multiplier (G - x^n)
	 * or (G - x^n) * x^(BITS_PER_LONG - n) from the formulas above.  The
	 * cast of the result to crc_t is essential, as it applies the mod x^n!
	 */
#if LSB_CRC
	return clmulr(tmp, consts->barrett_reduction_const_2);
#else
	return clmul(tmp, consts->barrett_reduction_const_2);
#endif
}

/* Update @crc with the data from @msgpoly. */
static inline crc_t
crc_clmul_update_long(crc_t crc, unsigned long msgpoly,
		      const struct crc_clmul_consts *consts)
{
	return crc_clmul_long(crc_clmul_prep(crc, msgpoly), consts);
}

/* Update @crc with 1 <= @len < sizeof(unsigned long) bytes of data. */
static inline crc_t
crc_clmul_update_partial(crc_t crc, const u8 *p, size_t len,
			 const struct crc_clmul_consts *consts)
{
	unsigned long msgpoly;
	size_t i;

#if LSB_CRC
	msgpoly = (unsigned long)p[0] << (BITS_PER_LONG - 8);
	for (i = 1; i < len; i++)
		msgpoly = (msgpoly >> 8) ^ ((unsigned long)p[i] << (BITS_PER_LONG - 8));
#else
	msgpoly = p[0];
	for (i = 1; i < len; i++)
		msgpoly = (msgpoly << 8) ^ p[i];
#endif

	if (len >= sizeof(crc_t)) {
	#if LSB_CRC
		msgpoly ^= (unsigned long)crc << (BITS_PER_LONG - 8*len);
	#else
		msgpoly ^= (unsigned long)crc << (8*len - CRC_BITS);
	#endif
		return crc_clmul_long(msgpoly, consts);
	}
#if LSB_CRC
	msgpoly ^= (unsigned long)crc << (BITS_PER_LONG - 8*len);
	return crc_clmul_long(msgpoly, consts) ^ (crc >> (8*len));
#else
	msgpoly ^= crc >> (CRC_BITS - 8*len);
	return crc_clmul_long(msgpoly, consts) ^ (crc << (8*len));
#endif
}

static inline crc_t
crc_clmul(crc_t crc, const void *p, size_t len,
	  const struct crc_clmul_consts *consts)
{
	size_t align;

	/* This implementation assumes that the CRC fits in an unsigned long. */
	BUILD_BUG_ON(sizeof(crc_t) > sizeof(unsigned long));

	/* If the buffer is not long-aligned, align it. */
	align = (unsigned long)p % sizeof(unsigned long);
	if (align && len) {
		align = min(sizeof(unsigned long) - align, len);
		crc = crc_clmul_update_partial(crc, p, align, consts);
		p += align;
		len -= align;
	}

	if (len >= 4 * sizeof(unsigned long)) {
		unsigned long m0, m1;

		m0 = crc_clmul_prep(crc, crc_load_long(p));
		m1 = crc_load_long(p + sizeof(unsigned long));
		p += 2 * sizeof(unsigned long);
		len -= 2 * sizeof(unsigned long);
		/*
		 * Main loop.  Each iteration starts with a message polynomial
		 * (x^BITS_PER_LONG)*m0 + m1, then logically extends it by two
		 * more longs of data to form x^(3*BITS_PER_LONG)*m0 +
		 * x^(2*BITS_PER_LONG)*m1 + x^BITS_PER_LONG*m2 + m3, then
		 * "folds" that back into a congruent (modulo G) value that uses
		 * just m0 and m1 again.  This is done by multiplying m0 by the
		 * precomputed constant (x^(3*BITS_PER_LONG) mod G) and m1 by
		 * the precomputed constant (x^(2*BITS_PER_LONG) mod G), then
		 * adding the results to m2 and m3 as appropriate.  Each such
		 * multiplication produces a result twice the length of a long,
		 * which in RISC-V is two instructions clmul and clmulh.
		 *
		 * This could be changed to fold across more than 2 longs at a
		 * time if there is a CPU that can take advantage of it.
		 */
		do {
			unsigned long p0, p1, p2, p3;

			p0 = clmulh(m0, consts->fold_across_2_longs_const_hi);
			p1 = clmul(m0, consts->fold_across_2_longs_const_hi);
			p2 = clmulh(m1, consts->fold_across_2_longs_const_lo);
			p3 = clmul(m1, consts->fold_across_2_longs_const_lo);
			m0 = (LSB_CRC ? p1 ^ p3 : p0 ^ p2) ^ crc_load_long(p);
			m1 = (LSB_CRC ? p0 ^ p2 : p1 ^ p3) ^
			     crc_load_long(p + sizeof(unsigned long));

			p += 2 * sizeof(unsigned long);
			len -= 2 * sizeof(unsigned long);
		} while (len >= 2 * sizeof(unsigned long));

		crc = crc_clmul_long(m0, consts);
		crc = crc_clmul_update_long(crc, m1, consts);
	}

	while (len >= sizeof(unsigned long)) {
		crc = crc_clmul_update_long(crc, crc_load_long(p), consts);
		p += sizeof(unsigned long);
		len -= sizeof(unsigned long);
	}

	if (len)
		crc = crc_clmul_update_partial(crc, p, len, consts);

	return crc;
}
