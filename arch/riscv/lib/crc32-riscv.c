// SPDX-License-Identifier: GPL-2.0-only
/*
 * Accelerated CRC32 implementation with Zbc extension.
 *
 * Copyright (C) 2024 Intel Corporation
 */

#include <asm/hwcap.h>
#include <asm/alternative-macros.h>
#include <asm/byteorder.h>

#include <linux/types.h>
#include <linux/minmax.h>
#include <linux/crc32poly.h>
#include <linux/crc32.h>
#include <linux/byteorder/generic.h>
#include <linux/module.h>

/*
 * Refer to https://www.corsix.org/content/barrett-reduction-polynomials for
 * better understanding of how this math works.
 *
 * let "+" denotes polynomial add (XOR)
 * let "-" denotes polynomial sub (XOR)
 * let "*" denotes polynomial multiplication
 * let "/" denotes polynomial floor division
 * let "S" denotes source data, XLEN bit wide
 * let "P" denotes CRC32 polynomial
 * let "T" denotes 2^(XLEN+32)
 * let "QT" denotes quotient of T/P, with the bit for 2^XLEN being implicit
 *
 * crc32(S, P)
 * => S * (2^32) - S * (2^32) / P * P
 * => lowest 32 bits of: S * (2^32) / P * P
 * => lowest 32 bits of: S * (2^32) * (T / P) / T * P
 * => lowest 32 bits of: S * (2^32) * quotient / T * P
 * => lowest 32 bits of: S * quotient / 2^XLEN * P
 * => lowest 32 bits of: (clmul_high_part(S, QT) + S) * P
 * => clmul_low_part(clmul_high_part(S, QT) + S, P)
 *
 * In terms of below implementations, the BE case is more intuitive, since the
 * higher order bit sits at more significant position.
 */

#if __riscv_xlen == 64
/* Slide by XLEN bits per iteration */
# define STEP_ORDER 3

/* Each below polynomial quotient has an implicit bit for 2^XLEN */

/* Polynomial quotient of (2^(XLEN+32))/CRC32_POLY, in LE format */
# define CRC32_POLY_QT_LE	0x5a72d812fb808b20

/* Polynomial quotient of (2^(XLEN+32))/CRC32C_POLY, in LE format */
# define CRC32C_POLY_QT_LE	0xa434f61c6f5389f8

/* Polynomial quotient of (2^(XLEN+32))/CRC32_POLY, in BE format, it should be
 * the same as the bit-reversed version of CRC32_POLY_QT_LE
 */
# define CRC32_POLY_QT_BE	0x04d101df481b4e5a

static inline u64 crc32_le_prep(u32 crc, unsigned long const *ptr)
{
	return (u64)crc ^ (__force u64)__cpu_to_le64(*ptr);
}

static inline u32 crc32_le_zbc(unsigned long s, u32 poly, unsigned long poly_qt)
{
	u32 crc;

	/* We don't have a "clmulrh" insn, so use clmul + slli instead. */
	asm volatile (".option push\n"
		      ".option arch,+zbc\n"
		      "clmul	%0, %1, %2\n"
		      "slli	%0, %0, 1\n"
		      "xor	%0, %0, %1\n"
		      "clmulr	%0, %0, %3\n"
		      "srli	%0, %0, 32\n"
		      ".option pop\n"
		      : "=&r" (crc)
		      : "r" (s),
			"r" (poly_qt),
			"r" ((u64)poly << 32)
		      :);
	return crc;
}

static inline u64 crc32_be_prep(u32 crc, unsigned long const *ptr)
{
	return ((u64)crc << 32) ^ (__force u64)__cpu_to_be64(*ptr);
}

#elif __riscv_xlen == 32
# define STEP_ORDER 2
/* Each quotient should match the upper half of its analog in RV64 */
# define CRC32_POLY_QT_LE	0xfb808b20
# define CRC32C_POLY_QT_LE	0x6f5389f8
# define CRC32_POLY_QT_BE	0x04d101df

static inline u32 crc32_le_prep(u32 crc, unsigned long const *ptr)
{
	return crc ^ (__force u32)__cpu_to_le32(*ptr);
}

static inline u32 crc32_le_zbc(unsigned long s, u32 poly, unsigned long poly_qt)
{
	u32 crc;

	/* We don't have a "clmulrh" insn, so use clmul + slli instead. */
	asm volatile (".option push\n"
		      ".option arch,+zbc\n"
		      "clmul	%0, %1, %2\n"
		      "slli	%0, %0, 1\n"
		      "xor	%0, %0, %1\n"
		      "clmulr	%0, %0, %3\n"
		      ".option pop\n"
		      : "=&r" (crc)
		      : "r" (s),
			"r" (poly_qt),
			"r" (poly)
		      :);
	return crc;
}

static inline u32 crc32_be_prep(u32 crc, unsigned long const *ptr)
{
	return crc ^ (__force u32)__cpu_to_be32(*ptr);
}

#else
# error "Unexpected __riscv_xlen"
#endif

static inline u32 crc32_be_zbc(unsigned long s)
{
	u32 crc;

	asm volatile (".option push\n"
		      ".option arch,+zbc\n"
		      "clmulh	%0, %1, %2\n"
		      "xor	%0, %0, %1\n"
		      "clmul	%0, %0, %3\n"
		      ".option pop\n"
		      : "=&r" (crc)
		      : "r" (s),
			"r" (CRC32_POLY_QT_BE),
			"r" (CRC32_POLY_BE)
		      :);
	return crc;
}

#define STEP		(1 << STEP_ORDER)
#define OFFSET_MASK	(STEP - 1)

typedef u32 (*fallback)(u32 crc, unsigned char const *p, size_t len);

static inline u32 crc32_le_unaligned(u32 crc, unsigned char const *p,
				     size_t len, u32 poly,
				     unsigned long poly_qt)
{
	size_t bits = len * 8;
	unsigned long s = 0;
	u32 crc_low = 0;

	for (int i = 0; i < len; i++)
		s = ((unsigned long)*p++ << (__riscv_xlen - 8)) | (s >> 8);

	s ^= (unsigned long)crc << (__riscv_xlen - bits);
	if (__riscv_xlen == 32 || len < sizeof(u32))
		crc_low = crc >> bits;

	crc = crc32_le_zbc(s, poly, poly_qt);
	crc ^= crc_low;

	return crc;
}

static inline u32 __pure crc32_le_generic(u32 crc, unsigned char const *p,
					  size_t len, u32 poly,
					  unsigned long poly_qt,
					  fallback crc_fb)
{
	size_t offset, head_len, tail_len;
	unsigned long const *p_ul;
	unsigned long s;

	asm goto(ALTERNATIVE("j %l[legacy]", "nop", 0,
			     RISCV_ISA_EXT_ZBC, 1)
		 : : : : legacy);

	/* Handle the unaligned head. */
	offset = (unsigned long)p & OFFSET_MASK;
	if (offset && len) {
		head_len = min(STEP - offset, len);
		crc = crc32_le_unaligned(crc, p, head_len, poly, poly_qt);
		p += head_len;
		len -= head_len;
	}

	tail_len = len & OFFSET_MASK;
	len = len >> STEP_ORDER;
	p_ul = (unsigned long const *)p;

	for (int i = 0; i < len; i++) {
		s = crc32_le_prep(crc, p_ul);
		crc = crc32_le_zbc(s, poly, poly_qt);
		p_ul++;
	}

	/* Handle the tail bytes. */
	p = (unsigned char const *)p_ul;
	if (tail_len)
		crc = crc32_le_unaligned(crc, p, tail_len, poly, poly_qt);

	return crc;

legacy:
	return crc_fb(crc, p, len);
}

u32 __pure crc32_le_arch(u32 crc, const u8 *p, size_t len)
{
	return crc32_le_generic(crc, p, len, CRC32_POLY_LE, CRC32_POLY_QT_LE,
				crc32_le_base);
}
EXPORT_SYMBOL(crc32_le_arch);

u32 __pure crc32c_le_arch(u32 crc, const u8 *p, size_t len)
{
	return crc32_le_generic(crc, p, len, CRC32C_POLY_LE,
				CRC32C_POLY_QT_LE, crc32c_le_base);
}
EXPORT_SYMBOL(crc32c_le_arch);

static inline u32 crc32_be_unaligned(u32 crc, unsigned char const *p,
				     size_t len)
{
	size_t bits = len * 8;
	unsigned long s = 0;
	u32 crc_low = 0;

	s = 0;
	for (int i = 0; i < len; i++)
		s = *p++ | (s << 8);

	if (__riscv_xlen == 32 || len < sizeof(u32)) {
		s ^= crc >> (32 - bits);
		crc_low = crc << bits;
	} else {
		s ^= (unsigned long)crc << (bits - 32);
	}

	crc = crc32_be_zbc(s);
	crc ^= crc_low;

	return crc;
}

u32 __pure crc32_be_arch(u32 crc, const u8 *p, size_t len)
{
	size_t offset, head_len, tail_len;
	unsigned long const *p_ul;
	unsigned long s;

	asm goto(ALTERNATIVE("j %l[legacy]", "nop", 0,
			     RISCV_ISA_EXT_ZBC, 1)
		 : : : : legacy);

	/* Handle the unaligned head. */
	offset = (unsigned long)p & OFFSET_MASK;
	if (offset && len) {
		head_len = min(STEP - offset, len);
		crc = crc32_be_unaligned(crc, p, head_len);
		p += head_len;
		len -= head_len;
	}

	tail_len = len & OFFSET_MASK;
	len = len >> STEP_ORDER;
	p_ul = (unsigned long const *)p;

	for (int i = 0; i < len; i++) {
		s = crc32_be_prep(crc, p_ul);
		crc = crc32_be_zbc(s);
		p_ul++;
	}

	/* Handle the tail bytes. */
	p = (unsigned char const *)p_ul;
	if (tail_len)
		crc = crc32_be_unaligned(crc, p, tail_len);

	return crc;

legacy:
	return crc32_be_base(crc, p, len);
}
EXPORT_SYMBOL(crc32_be_arch);

u32 crc32_optimizations(void)
{
	if (riscv_has_extension_likely(RISCV_ISA_EXT_ZBC))
		return CRC32_LE_OPTIMIZATION |
		       CRC32_BE_OPTIMIZATION |
		       CRC32C_OPTIMIZATION;
	return 0;
}
EXPORT_SYMBOL(crc32_optimizations);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Accelerated CRC32 implementation with Zbc extension");
