/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * vineetg: May 2011
 *  -Support single cycle endian-swap insn in ARC700 4.10
 *
 * vineetg: June 2009
 *  -Better htonl implementation (5 instead of 9 ALU instructions)
 *  -Hardware assisted single cycle bswap (Use Case of ARC custom instrn)
 */

#ifndef __ASM_ARC_SWAB_H
#define __ASM_ARC_SWAB_H

#include <linux/types.h>

/* Native single cycle endian swap insn */
#ifdef CONFIG_ARC_HAS_SWAPE

#define __arch_swab32(x)		\
({					\
	unsigned int tmp = x;		\
	__asm__(			\
	"	swape	%0, %1	\n"	\
	: "=r" (tmp)			\
	: "r" (tmp));			\
	tmp;				\
})

#else

/* Several ways of Endian-Swap Emulation for ARC
 * 0: kernel generic
 * 1: ARC optimised "C"
 * 2: ARC Custom instruction
 */
#define ARC_BSWAP_TYPE	1

#if (ARC_BSWAP_TYPE == 1)		/******* Software only ********/

/* The kernel default implementation of htonl is
 *		return  x<<24 | x>>24 |
 *		 (x & (__u32)0x0000ff00UL)<<8 | (x & (__u32)0x00ff0000UL)>>8;
 *
 * This generates 9 instructions on ARC (excluding the ld/st)
 *
 * 8051fd8c:	ld     r3,[r7,20]	; Mem op : Get the value to be swapped
 * 8051fd98:	asl    r5,r3,24		; get  3rd Byte
 * 8051fd9c:	lsr    r2,r3,24		; get  0th Byte
 * 8051fda0:	and    r4,r3,0xff00
 * 8051fda8:	asl    r4,r4,8		; get 1st Byte
 * 8051fdac:	and    r3,r3,0x00ff0000
 * 8051fdb4:	or     r2,r2,r5		; combine 0th and 3rd Bytes
 * 8051fdb8:	lsr    r3,r3,8		; 2nd Byte at correct place in Dst Reg
 * 8051fdbc:	or     r2,r2,r4		; combine 0,3 Bytes with 1st Byte
 * 8051fdc0:	or     r2,r2,r3		; combine 0,3,1 Bytes with 2nd Byte
 * 8051fdc4:	st     r2,[r1,20]	; Mem op : save result back to mem
 *
 * Joern suggested a better "C" algorithm which is great since
 * (1) It is portable to any architecure
 * (2) At the same time it takes advantage of ARC ISA (rotate intrns)
 */

#define __arch_swab32(x)					\
({	unsigned long __in = (x), __tmp;			\
	__tmp = __in << 8 | __in >> 24; /* ror tmp,in,24 */	\
	__in = __in << 24 | __in >> 8; /* ror in,in,8 */	\
	__tmp ^= __in;						\
	__tmp &= 0xff00ff;					\
	__tmp ^ __in;						\
})

#elif (ARC_BSWAP_TYPE == 2)	/* Custom single cycle bwap instruction */

#define __arch_swab32(x)						\
({									\
	unsigned int tmp = x;						\
	__asm__(							\
	"	.extInstruction	bswap, 7, 0x00, SUFFIX_NONE, SYNTAX_2OP	\n"\
	"	bswap  %0, %1						\n"\
	: "=r" (tmp)							\
	: "r" (tmp));							\
	tmp;								\
})

#endif /* ARC_BSWAP_TYPE=zzz */

#endif /* CONFIG_ARC_HAS_SWAPE */

#if !defined(__STRICT_ANSI__) || defined(__KERNEL__)
#define __SWAB_64_THRU_32__
#endif

#endif
