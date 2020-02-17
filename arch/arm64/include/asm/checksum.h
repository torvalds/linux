/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 ARM Ltd.
 */
#ifndef __ASM_CHECKSUM_H
#define __ASM_CHECKSUM_H

#include <linux/types.h>

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

	tmp = *(const __uint128_t *)iph;
	iph += 16;
	ihl -= 4;
	tmp += ((tmp >> 64) | (tmp << 64));
	sum = tmp >> 64;
	do {
		sum += *(const u32 *)iph;
		iph += 4;
	} while (--ihl);

	sum += ((sum >> 32) | (sum << 32));
	return csum_fold((__force u32)(sum >> 32));
}
#define ip_fast_csum ip_fast_csum

extern unsigned int do_csum(const unsigned char *buff, int len);
#define do_csum do_csum

#include <asm-generic/checksum.h>

#endif	/* __ASM_CHECKSUM_H */
