/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 ARM Ltd.
 */
#ifndef __ASM_CHECKSUM_H
#define __ASM_CHECKSUM_H

#include <linux/in6.h>

#define _HAVE_ARCH_IPV6_CSUM
__sum16 csum_ipv6_magic(const struct in6_addr *saddr,
			const struct in6_addr *daddr,
			__u32 len, __u8 proto, __wsum sum);

static inline __sum16 csum_fold(__wsum csum)
{
	u32 sum = (__force u32)csum;
	sum += (sum >> 16) | (sum << 16);
	return ~(__force __sum16)(sum >> 16);
}
#define csum_fold csum_fold

static inline __sum16 ip_fast_csum(const void *iph, unsigned int ihl)
{
	__uint128_t tmp;
	u64 sum;
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

	sum += ((sum >> 32) | (sum << 32));
	return csum_fold((__force u32)(sum >> 32));
}
#define ip_fast_csum ip_fast_csum

extern unsigned int do_csum(const unsigned char *buff, int len);
#define do_csum do_csum

#include <asm-generic/checksum.h>

#endif	/* __ASM_CHECKSUM_H */
