/*
 *  Copyright (C) 2011 Texas Instruments Incorporated
 *  Author: Mark Salter <msalter@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef _ASM_C6X_CHECKSUM_H
#define _ASM_C6X_CHECKSUM_H

static inline __wsum
csum_tcpudp_nofold(__be32 saddr, __be32 daddr, __u32 len,
		   __u8 proto, __wsum sum)
{
	unsigned long long tmp;

	asm ("add     .d1   %1,%5,%1\n"
	     "|| addu .l1   %3,%4,%0\n"
	     "addu    .l1   %2,%0,%0\n"
#ifndef CONFIG_CPU_BIG_ENDIAN
	     "|| shl  .s1   %1,8,%1\n"
#endif
	     "addu    .l1   %1,%0,%0\n"
	     "add     .l1   %P0,%p0,%2\n"
	     : "=&a"(tmp), "+a"(len), "+a"(sum)
	     : "a" (saddr), "a" (daddr), "a" (proto));
	return sum;
}
#define csum_tcpudp_nofold csum_tcpudp_nofold

#include <asm-generic/checksum.h>

#endif /* _ASM_C6X_CHECKSUM_H */
