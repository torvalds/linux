/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 ARM Ltd.
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */
#ifndef __ASM_CHECKSUM_H
#define __ASM_CHECKSUM_H

#include <linux/bitops.h>
#include <linux/in6.h>

#define _HAVE_ARCH_IPV6_CSUM
__sum16 csum_ipv6_magic(const struct in6_addr *saddr,
			const struct in6_addr *daddr,
			__u32 len, __u8 proto, __wsum sum);

/*
 * turns a 32-bit partial checksum (e.g. from csum_partial) into a
 * 1's complement 16-bit checksum.
 */
static inline __sum16 csum_fold(__wsum sum)
{
	u32 tmp = (__force u32)sum;

	/*
	 * swap the two 16-bit halves of sum
	 * if there is a carry from adding the two 16-bit halves,
	 * it will carry from the lower half into the upper half,
	 * giving us the correct sum in the upper half.
	 */
	return (__force __sum16)(~(tmp + rol32(tmp, 16)) >> 16);
}
#define csum_fold csum_fold

/*
 * This is a version of ip_compute_csum() optimized for IP headers,
 * which always checksum on 4 octet boundaries.  ihl is the number
 * of 32-bit words and is always >= 5.
 */
static inline __sum16 ip_fast_csum(const void *iph, unsigned int ihl)
{
	u64 sum;
	__uint128_t tmp;
	int n = ihl; /* we want it signed */

	tmp = *(const __uint128_t *)iph;
	iph += 16;
	n -= 4;
	tmp += ((tmp >> 64) | (tmp << 64));
	sum = tmp >> 64;
	do {
		sum += *(const u32 *)iph;
		iph += 4;
	} while (--n > 0);

	sum += ror64(sum, 32);
	return csum_fold((__force __wsum)(sum >> 32));
}
#define ip_fast_csum ip_fast_csum

extern unsigned int do_csum(const unsigned char *buff, int len);
#define do_csum do_csum

#include <asm-generic/checksum.h>

#endif	/* __ASM_CHECKSUM_H */
