/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_CHECKSUM_H
#define _ASM_CHECKSUM_H

#define do_csum	do_csum
unsigned int do_csum(const void *voidptr, int len);

/*
 * the same as csum_partial, but copies from src while it
 * checksums
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */
#define csum_partial_copy_nocheck csum_partial_copy_nocheck
__wsum csum_partial_copy_nocheck(const void *src, void *dst,
					int len, __wsum sum);

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
#define csum_tcpudp_nofold csum_tcpudp_nofold
__wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr,
			  __u32 len, __u8 proto, __wsum sum);

#define csum_tcpudp_magic csum_tcpudp_magic
__sum16 csum_tcpudp_magic(__be32 saddr, __be32 daddr,
			  __u32 len, __u8 proto, __wsum sum);

#include <asm-generic/checksum.h>

#endif
