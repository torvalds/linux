/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/include/asm/checksum.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * IP checksum routines
 */
#ifndef __UNICORE_CHECKSUM_H__
#define __UNICORE_CHECKSUM_H__

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */

static inline __wsum
csum_tcpudp_nofold(__be32 saddr, __be32 daddr, __u32 len,
		   __u8 proto, __wsum sum)
{
	__asm__(
	"add.a	%0, %1, %2\n"
	"addc.a	%0, %0, %3\n"
	"addc.a	%0, %0, %4 << #8\n"
	"addc.a	%0, %0, %5\n"
	"addc	%0, %0, #0\n"
	: "=&r"(sum)
	: "r" (sum), "r" (daddr), "r" (saddr), "r" (len), "Ir" (htons(proto))
	: "cc");
	return sum;
}
#define csum_tcpudp_nofold	csum_tcpudp_nofold

#include <asm-generic/checksum.h>

#endif
