#ifndef _BFIN_CHECKSUM_H
#define _BFIN_CHECKSUM_H

/*
 * MODIFIED FOR BFIN April 30, 2001 akbar.hussain@lineo.com
 *
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
__wsum csum_partial(const void *buff, int len, __wsum sum);

/*
 * the same as csum_partial, but copies from src while it
 * checksums
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */

__wsum csum_partial_copy(const void *src, void *dst,
			       int len, __wsum sum);

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

__sum16 ip_fast_csum(unsigned char *iph, unsigned int ihl);

/*
 *	Fold a partial checksum
 */

static inline __sum16 csum_fold(__wsum sum)
{
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return ((~(sum << 16)) >> 16);
}

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */

static inline __wsum
csum_tcpudp_nofold(__be32 saddr, __be32 daddr, unsigned short len,
		   unsigned short proto, __wsum sum)
{
	unsigned int carry;

	__asm__ ("%0 = %0 + %2;\n\t"
		"CC = AC0;\n\t"
		"%1 = CC;\n\t"
		"%0 = %0 + %1;\n\t"
		"%0 = %0 + %3;\n\t"
		"CC = AC0;\n\t"
		"%1 = CC;\n\t"
		"%0 = %0 + %1;\n\t"
		"%0 = %0 + %4;\n\t"
		"CC = AC0;\n\t"
		"%1 = CC;\n\t"
		"%0 = %0 + %1;\n\t"
		: "=d" (sum), "=&d" (carry)
		: "d" (daddr), "d" (saddr), "d" ((len + proto) << 8), "0"(sum)
		: "CC");

	return (sum);
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

extern __sum16 ip_compute_csum(const void *buff, int len);

#endif				/* _BFIN_CHECKSUM_H */
