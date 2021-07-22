/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    S390 fast network checksum routines
 *
 *  S390 version
 *    Copyright IBM Corp. 1999
 *    Author(s): Ulrich Hild        (first version)
 *               Martin Schwidefsky (heavily optimized CKSM version)
 *               D.J. Barrow        (third attempt) 
 */

#ifndef _S390_CHECKSUM_H
#define _S390_CHECKSUM_H

#include <linux/uaccess.h>
#include <linux/in6.h>

/*
 * Computes the checksum of a memory block at buff, length len,
 * and adds in "sum" (32-bit).
 *
 * Returns a 32-bit number suitable for feeding into itself
 * or csum_tcpudp_magic.
 *
 * This function must be called with even lengths, except
 * for the last fragment, which may be odd.
 *
 * It's best to have buff aligned on a 32-bit boundary.
 */
static inline __wsum csum_partial(const void *buff, int len, __wsum sum)
{
	union register_pair rp = {
		.even = (unsigned long) buff,
		.odd = (unsigned long) len,
	};

	asm volatile(
		"0:	cksm	%[sum],%[rp]\n"
		"	jo	0b\n"
		: [sum] "+&d" (sum), [rp] "+&d" (rp.pair) : : "cc", "memory");
	return sum;
}

/*
 * Fold a partial checksum without adding pseudo headers.
 */
static inline __sum16 csum_fold(__wsum sum)
{
	u32 csum = (__force u32) sum;

	csum += (csum >> 16) | (csum << 16);
	csum >>= 16;
	return (__force __sum16) ~csum;
}

/*
 * This is a version of ip_compute_csum() optimized for IP headers,
 * which always checksums on 4 octet boundaries.
 */
static inline __sum16 ip_fast_csum(const void *iph, unsigned int ihl)
{
	__u64 csum = 0;
	__u32 *ptr = (u32 *)iph;

	csum += *ptr++;
	csum += *ptr++;
	csum += *ptr++;
	csum += *ptr++;
	ihl -= 4;
	while (ihl--)
		csum += *ptr++;
	csum += (csum >> 32) | (csum << 32);
	return csum_fold((__force __wsum)(csum >> 32));
}

/*
 * Computes the checksum of the TCP/UDP pseudo-header.
 * Returns a 32-bit checksum.
 */
static inline __wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr, __u32 len,
					__u8 proto, __wsum sum)
{
	__u64 csum = (__force __u64)sum;

	csum += (__force __u32)saddr;
	csum += (__force __u32)daddr;
	csum += len;
	csum += proto;
	csum += (csum >> 32) | (csum << 32);
	return (__force __wsum)(csum >> 32);
}

/*
 * Computes the checksum of the TCP/UDP pseudo-header.
 * Returns a 16-bit checksum, already complemented.
 */
static inline __sum16 csum_tcpudp_magic(__be32 saddr, __be32 daddr, __u32 len,
					__u8 proto, __wsum sum)
{
	return csum_fold(csum_tcpudp_nofold(saddr, daddr, len, proto, sum));
}

/*
 * Used for miscellaneous IP-like checksums, mainly icmp.
 */
static inline __sum16 ip_compute_csum(const void *buff, int len)
{
	return csum_fold(csum_partial(buff, len, 0));
}

#define _HAVE_ARCH_IPV6_CSUM
static inline __sum16 csum_ipv6_magic(const struct in6_addr *saddr,
				      const struct in6_addr *daddr,
				      __u32 len, __u8 proto, __wsum csum)
{
	__u64 sum = (__force __u64)csum;

	sum += (__force __u32)saddr->s6_addr32[0];
	sum += (__force __u32)saddr->s6_addr32[1];
	sum += (__force __u32)saddr->s6_addr32[2];
	sum += (__force __u32)saddr->s6_addr32[3];
	sum += (__force __u32)daddr->s6_addr32[0];
	sum += (__force __u32)daddr->s6_addr32[1];
	sum += (__force __u32)daddr->s6_addr32[2];
	sum += (__force __u32)daddr->s6_addr32[3];
	sum += len;
	sum += proto;
	sum += (sum >> 32) | (sum << 32);
	return csum_fold((__force __wsum)(sum >> 32));
}

#endif /* _S390_CHECKSUM_H */
