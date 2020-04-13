/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *  Rewritten for 2.6.3x: Mark Salter <msalter@redhat.com>
 */
#ifndef _ASM_C6X_UNALIGNED_H
#define _ASM_C6X_UNALIGNED_H

#include <linux/swab.h>
#include <linux/unaligned/generic.h>

/*
 * The C64x+ can do unaligned word and dword accesses in hardware
 * using special load/store instructions.
 */

static inline u16 get_unaligned_le16(const void *p)
{
	const u8 *_p = p;
	return _p[0] | _p[1] << 8;
}

static inline u16 get_unaligned_be16(const void *p)
{
	const u8 *_p = p;
	return _p[0] << 8 | _p[1];
}

static inline void put_unaligned_le16(u16 val, void *p)
{
	u8 *_p = p;
	_p[0] = val;
	_p[1] = val >> 8;
}

static inline void put_unaligned_be16(u16 val, void *p)
{
	u8 *_p = p;
	_p[0] = val >> 8;
	_p[1] = val;
}

static inline u32 get_unaligned32(const void *p)
{
	u32 val = (u32) p;
	asm (" ldnw	.d1t1	*%0,%0\n"
	     " nop     4\n"
	     : "+a"(val));
	return val;
}

static inline void put_unaligned32(u32 val, void *p)
{
	asm volatile (" stnw	.d2t1	%0,*%1\n"
		      : : "a"(val), "b"(p) : "memory");
}

static inline u64 get_unaligned64(const void *p)
{
	u64 val;
	asm volatile (" ldndw	.d1t1	*%1,%0\n"
		      " nop     4\n"
		      : "=a"(val) : "a"(p));
	return val;
}

static inline void put_unaligned64(u64 val, const void *p)
{
	asm volatile (" stndw	.d2t1	%0,*%1\n"
		      : : "a"(val), "b"(p) : "memory");
}

#ifdef CONFIG_CPU_BIG_ENDIAN

#define get_unaligned_le32(p)	 __swab32(get_unaligned32(p))
#define get_unaligned_le64(p)	 __swab64(get_unaligned64(p))
#define get_unaligned_be32(p)	 get_unaligned32(p)
#define get_unaligned_be64(p)	 get_unaligned64(p)
#define put_unaligned_le32(v, p) put_unaligned32(__swab32(v), (p))
#define put_unaligned_le64(v, p) put_unaligned64(__swab64(v), (p))
#define put_unaligned_be32(v, p) put_unaligned32((v), (p))
#define put_unaligned_be64(v, p) put_unaligned64((v), (p))
#define get_unaligned	__get_unaligned_be
#define put_unaligned	__put_unaligned_be

#else

#define get_unaligned_le32(p)	 get_unaligned32(p)
#define get_unaligned_le64(p)	 get_unaligned64(p)
#define get_unaligned_be32(p)	 __swab32(get_unaligned32(p))
#define get_unaligned_be64(p)	 __swab64(get_unaligned64(p))
#define put_unaligned_le32(v, p) put_unaligned32((v), (p))
#define put_unaligned_le64(v, p) put_unaligned64((v), (p))
#define put_unaligned_be32(v, p) put_unaligned32(__swab32(v), (p))
#define put_unaligned_be64(v, p) put_unaligned64(__swab64(v), (p))
#define get_unaligned	__get_unaligned_le
#define put_unaligned	__put_unaligned_le

#endif

#endif /* _ASM_C6X_UNALIGNED_H */
