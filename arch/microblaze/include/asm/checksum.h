/*
 * Copyright (C) 2008 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_CHECKSUM_H
#define _ASM_MICROBLAZE_CHECKSUM_H

#include <linux/in6.h>

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
static inline __wsum
csum_tcpudp_nofold(__be32 saddr, __be32 daddr, unsigned short len,
		   unsigned short proto, __wsum sum)
{
	__asm__("add %0, %0, %1\n\t"
		"addc %0, %0, %2\n\t"
		"addc %0, %0, %3\n\t"
		"addc %0, %0, r0\n\t"
		: "+&d" (sum)
		: "d" (saddr), "d" (daddr), "d" (len + proto));

	return sum;
}

/*
 * computes the checksum of a memory block at buff, length len,
 * and adds in "sum" (32-bit)
 *
 * returns a 32-bit number suitable for feeding into itself
 * or csum_tcpudp_magic
 *
 * this function must be called with even lengths, except
 * for the last fragment, which may be odd
 *
 * it's best to have buff aligned on a 32-bit boundary
 */
extern __wsum csum_partial(const void *buff, int len, __wsum sum);

/*
 * the same as csum_partial, but copies from src while it
 * checksums
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */
extern __wsum csum_partial_copy(const char *src, char *dst, int len, int sum);

/*
 * the same as csum_partial_copy, but copies from user space.
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */
extern __wsum csum_partial_copy_from_user(const char *src, char *dst,
					int len, int sum, int *csum_err);

#define csum_partial_copy_nocheck(src, dst, len, sum)	\
	csum_partial_copy((src), (dst), (len), (sum))

/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 *
 */
extern __sum16 ip_fast_csum(const void *iph, unsigned int ihl);

/*
 *	Fold a partial checksum
 */
static inline __sum16 csum_fold(unsigned int sum)
{
	sum = (sum & 0xffff) + (sum >> 16);
	sum = (sum & 0xffff) + (sum >> 16);
	return ~sum;
}

static inline __sum16
csum_tcpudp_magic(__be32 saddr, __be32 daddr, unsigned short len,
		  unsigned short proto, __wsum sum)
{
	return csum_fold(csum_tcpudp_nofold(saddr, daddr, len, proto, sum));
}

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
extern __sum16 ip_compute_csum(const unsigned char *buff, int len);

#endif /* _ASM_MICROBLAZE_CHECKSUM_H */
