/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _METAG_CHECKSUM_H
#define _METAG_CHECKSUM_H

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
extern __wsum csum_partial_copy(const void *src, void *dst, int len,
				__wsum sum);

/*
 * the same as csum_partial_copy, but copies from user space.
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */
extern __wsum csum_partial_copy_from_user(const void __user *src, void *dst,
					int len, __wsum sum, int *csum_err);

#define csum_partial_copy_nocheck(src, dst, len, sum)	\
	csum_partial_copy((src), (dst), (len), (sum))

/*
 * Fold a partial checksum
 */
static inline __sum16 csum_fold(__wsum csum)
{
	u32 sum = (__force u32)csum;
	sum = (sum & 0xffff) + (sum >> 16);
	sum = (sum & 0xffff) + (sum >> 16);
	return (__force __sum16)~sum;
}

/*
 * This is a version of ip_compute_csum() optimized for IP headers,
 * which always checksum on 4 octet boundaries.
 */
extern __sum16 ip_fast_csum(const void *iph, unsigned int ihl);

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
static inline __wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr,
					__u32 len, __u8 proto,
					__wsum sum)
{
	unsigned long len_proto = (proto + len) << 8;
	asm ("ADDS   %0, %0, %1\n"
	     "ADDCS  %0, %0, #1\n"
	     "ADDS   %0, %0, %2\n"
	     "ADDCS  %0, %0, #1\n"
	     "ADDS   %0, %0, %3\n"
	     "ADDCS  %0, %0, #1\n"
	     : "=d" (sum)
	     : "d" (daddr), "d" (saddr), "d" (len_proto),
	       "0" (sum)
	     : "cc");
	return sum;
}

static inline __sum16
csum_tcpudp_magic(__be32 saddr, __be32 daddr, __u32 len,
		  __u8 proto, __wsum sum)
{
	return csum_fold(csum_tcpudp_nofold(saddr, daddr, len, proto, sum));
}

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
extern __sum16 ip_compute_csum(const void *buff, int len);

#endif /* _METAG_CHECKSUM_H */
