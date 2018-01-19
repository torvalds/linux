/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_CHECKSUM_H
#define _ASM_IA64_CHECKSUM_H

/*
 * Modified 1998, 1999
 *	David Mosberger-Tang <davidm@hpl.hp.com>, Hewlett-Packard Co
 */

/*
 * This is a version of ip_compute_csum() optimized for IP headers,
 * which always checksum on 4 octet boundaries.
 */
extern __sum16 ip_fast_csum(const void *iph, unsigned int ihl);

/*
 * Computes the checksum of the TCP/UDP pseudo-header returns a 16-bit
 * checksum, already complemented
 */
extern __sum16 csum_tcpudp_magic(__be32 saddr, __be32 daddr,
				 __u32 len, __u8 proto, __wsum sum);

extern __wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr,
				 __u32 len, __u8 proto, __wsum sum);

/*
 * Computes the checksum of a memory block at buff, length len,
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
 * Same as csum_partial, but copies from src while it checksums.
 *
 * Here it is even more important to align src and dst on a 32-bit (or
 * even better 64-bit) boundary.
 */
extern __wsum csum_partial_copy_from_user(const void __user *src, void *dst,
						 int len, __wsum sum,
						 int *errp);

extern __wsum csum_partial_copy_nocheck(const void *src, void *dst,
					       int len, __wsum sum);

/*
 * This routine is used for miscellaneous IP-like checksums, mainly in
 * icmp.c
 */
extern __sum16 ip_compute_csum(const void *buff, int len);

/*
 * Fold a partial checksum without adding pseudo headers.
 */
static inline __sum16 csum_fold(__wsum csum)
{
	u32 sum = (__force u32)csum;
	sum = (sum & 0xffff) + (sum >> 16);
	sum = (sum & 0xffff) + (sum >> 16);
	return (__force __sum16)~sum;
}

#define _HAVE_ARCH_IPV6_CSUM	1
struct in6_addr;
extern __sum16 csum_ipv6_magic(const struct in6_addr *saddr,
			       const struct in6_addr *daddr,
			       __u32 len, __u8 proto, __wsum csum);

#endif /* _ASM_IA64_CHECKSUM_H */
