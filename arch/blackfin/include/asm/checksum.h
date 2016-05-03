/*
 * Copyright 2004-2009 Analog Devices Inc.
 *                     akbar.hussain@lineo.com
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _BFIN_CHECKSUM_H
#define _BFIN_CHECKSUM_H

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */

static inline __wsum
__csum_tcpudp_nofold(__be32 saddr, __be32 daddr, __u32 len,
		     __u8 proto, __wsum sum)
{
	unsigned int carry;

	__asm__ ("%0 = %0 + %2;\n\t"
		"CC = AC0;\n\t"
		"%1 = CC;\n\t"
		"%0 = %0 + %1;\n\t"
		"%0 = %0 + %3;\n\t"
		"CC = AC0;\n\t"
		"%1 = CC;\n\t"
		"%0 = %0 + %1;\n\t"
		"%0 = %0 + %4;\n\t"
		"CC = AC0;\n\t"
		"%1 = CC;\n\t"
		"%0 = %0 + %1;\n\t"
		: "=d" (sum), "=&d" (carry)
		: "d" (daddr), "d" (saddr), "d" ((len + proto) << 8), "0"(sum)
		: "CC");

	return (sum);
}
#define csum_tcpudp_nofold __csum_tcpudp_nofold

#include <asm-generic/checksum.h>

#endif
