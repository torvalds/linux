/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SPARC_CHECKSUM_H
#define __SPARC_CHECKSUM_H

/*  checksum.h:  IP/UDP/TCP checksum routines on the Sparc.
 *
 *  Copyright(C) 1995 Linus Torvalds
 *  Copyright(C) 1995 Miguel de Icaza
 *  Copyright(C) 1996 David S. Miller
 *  Copyright(C) 1996 Eddie C. Dost
 *  Copyright(C) 1997 Jakub Jelinek
 *
 * derived from:
 *	Alpha checksum c-code
 *      ix86 inline assembly
 *      RFC1071 Computing the Internet Checksum
 */

#include <linux/in6.h>
#include <linux/uaccess.h>

/* computes the checksum of a memory block at buff, length len,
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

/* the same as csum_partial, but copies from fs:src while it
 * checksums
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */

unsigned int __csum_partial_copy_sparc_generic (const unsigned char *, unsigned char *);

static inline __wsum
csum_partial_copy_nocheck(const void *src, void *dst, int len)
{
	register unsigned int ret asm("o0") = (unsigned int)src;
	register char *d asm("o1") = dst;
	register int l asm("g1") = len;

	__asm__ __volatile__ (
		"call __csum_partial_copy_sparc_generic\n\t"
		" mov -1, %%g7\n"
	: "=&r" (ret), "=&r" (d), "=&r" (l)
	: "0" (ret), "1" (d), "2" (l)
	: "o2", "o3", "o4", "o5", "o7",
	  "g2", "g3", "g4", "g5", "g7",
	  "memory", "cc");
	return (__force __wsum)ret;
}

static inline __wsum
csum_and_copy_from_user(const void __user *src, void *dst, int len)
{
	if (unlikely(!access_ok(src, len)))
		return 0;
	return csum_partial_copy_nocheck((__force void *)src, dst, len);
}

static inline __wsum
csum_and_copy_to_user(const void *src, void __user *dst, int len)
{
	if (!access_ok(dst, len))
		return 0;
	return csum_partial_copy_nocheck(src, (__force void *)dst, len);
}

/* ihl is always 5 or greater, almost always is 5, and iph is word aligned
 * the majority of the time.
 */
static inline __sum16 ip_fast_csum(const void *iph, unsigned int ihl)
{
	__sum16 sum;

	/* Note: We must read %2 before we touch %0 for the first time,
	 *       because GCC can legitimately use the same register for
	 *       both operands.
	 */
	__asm__ __volatile__("sub\t%2, 4, %%g4\n\t"
			     "ld\t[%1 + 0x00], %0\n\t"
			     "ld\t[%1 + 0x04], %%g2\n\t"
			     "ld\t[%1 + 0x08], %%g3\n\t"
			     "addcc\t%%g2, %0, %0\n\t"
			     "addxcc\t%%g3, %0, %0\n\t"
			     "ld\t[%1 + 0x0c], %%g2\n\t"
			     "ld\t[%1 + 0x10], %%g3\n\t"
			     "addxcc\t%%g2, %0, %0\n\t"
			     "addx\t%0, %%g0, %0\n"
			     "1:\taddcc\t%%g3, %0, %0\n\t"
			     "add\t%1, 4, %1\n\t"
			     "addxcc\t%0, %%g0, %0\n\t"
			     "subcc\t%%g4, 1, %%g4\n\t"
			     "be,a\t2f\n\t"
			     "sll\t%0, 16, %%g2\n\t"
			     "b\t1b\n\t"
			     "ld\t[%1 + 0x10], %%g3\n"
			     "2:\taddcc\t%0, %%g2, %%g2\n\t"
			     "srl\t%%g2, 16, %0\n\t"
			     "addx\t%0, %%g0, %0\n\t"
			     "xnor\t%%g0, %0, %0"
			     : "=r" (sum), "=&r" (iph)
			     : "r" (ihl), "1" (iph)
			     : "g2", "g3", "g4", "cc", "memory");
	return sum;
}

/* Fold a partial checksum without adding pseudo headers. */
static inline __sum16 csum_fold(__wsum sum)
{
	unsigned int tmp;

	__asm__ __volatile__("addcc\t%0, %1, %1\n\t"
			     "srl\t%1, 16, %1\n\t"
			     "addx\t%1, %%g0, %1\n\t"
			     "xnor\t%%g0, %1, %0"
			     : "=&r" (sum), "=r" (tmp)
			     : "0" (sum), "1" ((__force u32)sum<<16)
			     : "cc");
	return (__force __sum16)sum;
}

static inline __wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr,
					__u32 len, __u8 proto,
					__wsum sum)
{
	__asm__ __volatile__("addcc\t%1, %0, %0\n\t"
			     "addxcc\t%2, %0, %0\n\t"
			     "addxcc\t%3, %0, %0\n\t"
			     "addx\t%0, %%g0, %0\n\t"
			     : "=r" (sum), "=r" (saddr)
			     : "r" (daddr), "r" (proto + len), "0" (sum),
			       "1" (saddr)
			     : "cc");
	return sum;
}

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
static inline __sum16 csum_tcpudp_magic(__be32 saddr, __be32 daddr,
					__u32 len, __u8 proto,
					__wsum sum)
{
	return csum_fold(csum_tcpudp_nofold(saddr,daddr,len,proto,sum));
}

#define _HAVE_ARCH_IPV6_CSUM

static inline __sum16 csum_ipv6_magic(const struct in6_addr *saddr,
				      const struct in6_addr *daddr,
				      __u32 len, __u8 proto, __wsum sum)
{
	__asm__ __volatile__ (
		"addcc	%3, %4, %%g4\n\t"
		"addxcc	%5, %%g4, %%g4\n\t"
		"ld	[%2 + 0x0c], %%g2\n\t"
		"ld	[%2 + 0x08], %%g3\n\t"
		"addxcc	%%g2, %%g4, %%g4\n\t"
		"ld	[%2 + 0x04], %%g2\n\t"
		"addxcc	%%g3, %%g4, %%g4\n\t"
		"ld	[%2 + 0x00], %%g3\n\t"
		"addxcc	%%g2, %%g4, %%g4\n\t"
		"ld	[%1 + 0x0c], %%g2\n\t"
		"addxcc	%%g3, %%g4, %%g4\n\t"
		"ld	[%1 + 0x08], %%g3\n\t"
		"addxcc	%%g2, %%g4, %%g4\n\t"
		"ld	[%1 + 0x04], %%g2\n\t"
		"addxcc	%%g3, %%g4, %%g4\n\t"
		"ld	[%1 + 0x00], %%g3\n\t"
		"addxcc	%%g2, %%g4, %%g4\n\t"
		"addxcc	%%g3, %%g4, %0\n\t"
		"addx	0, %0, %0\n"
		: "=&r" (sum)
		: "r" (saddr), "r" (daddr),
		  "r"(htonl(len)), "r"(htonl(proto)), "r"(sum)
		: "g2", "g3", "g4", "cc");

	return csum_fold(sum);
}

/* this routine is used for miscellaneous IP-like checksums, mainly in icmp.c */
static inline __sum16 ip_compute_csum(const void *buff, int len)
{
	return csum_fold(csum_partial(buff, len, 0));
}

#define HAVE_ARCH_CSUM_ADD
static inline __wsum csum_add(__wsum csum, __wsum addend)
{
	__asm__ __volatile__(
		"addcc   %0, %1, %0\n"
		"addx    %0, %%g0, %0"
		: "=r" (csum)
		: "r" (addend), "0" (csum));

	return csum;
}

#endif /* !(__SPARC_CHECKSUM_H) */
