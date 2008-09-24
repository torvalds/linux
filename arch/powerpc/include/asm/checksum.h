#ifndef _ASM_POWERPC_CHECKSUM_H
#define _ASM_POWERPC_CHECKSUM_H
#ifdef __KERNEL__

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * This is a version of ip_compute_csum() optimized for IP headers,
 * which always checksum on 4 octet boundaries.  ihl is the number
 * of 32-bit words and is always >= 5.
 */
extern __sum16 ip_fast_csum(const void *iph, unsigned int ihl);

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
extern __sum16 csum_tcpudp_magic(__be32 saddr, __be32 daddr,
					unsigned short len,
					unsigned short proto,
					__wsum sum);

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
 * Computes the checksum of a memory block at src, length len,
 * and adds in "sum" (32-bit), while copying the block to dst.
 * If an access exception occurs on src or dst, it stores -EFAULT
 * to *src_err or *dst_err respectively (if that pointer is not
 * NULL), and, for an error on src, zeroes the rest of dst.
 *
 * Like csum_partial, this must be called with even lengths,
 * except for the last fragment.
 */
extern __wsum csum_partial_copy_generic(const void *src, void *dst,
					      int len, __wsum sum,
					      int *src_err, int *dst_err);
/*
 * the same as csum_partial, but copies from src to dst while it
 * checksums.
 */
#define csum_partial_copy_from_user(src, dst, len, sum, errp)   \
        csum_partial_copy_generic((__force const void *)(src), (dst), (len), (sum), (errp), NULL)

#define csum_partial_copy_nocheck(src, dst, len, sum)   \
        csum_partial_copy_generic((src), (dst), (len), (sum), NULL, NULL)


/*
 * turns a 32-bit partial checksum (e.g. from csum_partial) into a
 * 1's complement 16-bit checksum.
 */
static inline __sum16 csum_fold(__wsum sum)
{
	unsigned int tmp;

	/* swap the two 16-bit halves of sum */
	__asm__("rlwinm %0,%1,16,0,31" : "=r" (tmp) : "r" (sum));
	/* if there is a carry from adding the two 16-bit halves,
	   it will carry from the lower half into the upper half,
	   giving us the correct sum in the upper half. */
	return (__force __sum16)(~((__force u32)sum + tmp) >> 16);
}

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
static inline __sum16 ip_compute_csum(const void *buff, int len)
{
	return csum_fold(csum_partial(buff, len, 0));
}

static inline __wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr,
                                     unsigned short len,
                                     unsigned short proto,
                                     __wsum sum)
{
#ifdef __powerpc64__
	unsigned long s = (__force u32)sum;

	s += (__force u32)saddr;
	s += (__force u32)daddr;
	s += proto + len;
	s += (s >> 32);
	return (__force __wsum) s;
#else
    __asm__("\n\
	addc %0,%0,%1 \n\
	adde %0,%0,%2 \n\
	adde %0,%0,%3 \n\
	addze %0,%0 \n\
	"
	: "=r" (sum)
	: "r" (daddr), "r"(saddr), "r"(proto + len), "0"(sum));
	return sum;
#endif
}
#endif /* __KERNEL__ */
#endif
