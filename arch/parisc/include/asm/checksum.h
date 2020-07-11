/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PARISC_CHECKSUM_H
#define _PARISC_CHECKSUM_H

#include <linux/in6.h>

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
extern __wsum csum_partial(const void *, int, __wsum);

/*
 *	Optimized for IP headers, which always checksum on 4 octet boundaries.
 *
 *	Written by Randolph Chung <tausq@debian.org>, and then mucked with by
 *	LaMont Jones <lamont@debian.org>
 */
static inline __sum16 ip_fast_csum(const void *iph, unsigned int ihl)
{
	unsigned int sum;
	unsigned long t0, t1, t2;

	__asm__ __volatile__ (
"	ldws,ma		4(%1), %0\n"
"	addib,<=	-4, %2, 2f\n"
"\n"
"	ldws		4(%1), %4\n"
"	ldws		8(%1), %5\n"
"	add		%0, %4, %0\n"
"	ldws,ma		12(%1), %3\n"
"	addc		%0, %5, %0\n"
"	addc		%0, %3, %0\n"
"1:	ldws,ma		4(%1), %3\n"
"	addib,<		0, %2, 1b\n"
"	addc		%0, %3, %0\n"
"\n"
"	extru		%0, 31, 16, %4\n"
"	extru		%0, 15, 16, %5\n"
"	addc		%4, %5, %0\n"
"	extru		%0, 15, 16, %5\n"
"	add		%0, %5, %0\n"
"	subi		-1, %0, %0\n"
"2:\n"
	: "=r" (sum), "=r" (iph), "=r" (ihl), "=r" (t0), "=r" (t1), "=r" (t2)
	: "1" (iph), "2" (ihl)
	: "memory");

	return (__force __sum16)sum;
}

/*
 *	Fold a partial checksum
 */
static inline __sum16 csum_fold(__wsum csum)
{
	u32 sum = (__force u32)csum;
	/* add the swapped two 16-bit halves of sum,
	   a possible carry from adding the two 16-bit halves,
	   will carry from the lower half into the upper half,
	   giving us the correct sum in the upper half. */
	sum += (sum << 16) + (sum >> 16);
	return (__force __sum16)(~sum >> 16);
}
 
static inline __wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr,
					__u32 len, __u8 proto,
					__wsum sum)
{
	__asm__(
	"	add  %1, %0, %0\n"
	"	addc %2, %0, %0\n"
	"	addc %3, %0, %0\n"
	"	addc %%r0, %0, %0\n"
		: "=r" (sum)
		: "r" (daddr), "r"(saddr), "r"(proto+len), "0"(sum));
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

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
static inline __sum16 ip_compute_csum(const void *buf, int len)
{
	 return csum_fold (csum_partial(buf, len, 0));
}


#define _HAVE_ARCH_IPV6_CSUM
static __inline__ __sum16 csum_ipv6_magic(const struct in6_addr *saddr,
					  const struct in6_addr *daddr,
					  __u32 len, __u8 proto,
					  __wsum sum)
{
	unsigned long t0, t1, t2, t3;

	len += proto;	/* add 16-bit proto + len */

	__asm__ __volatile__ (

#if BITS_PER_LONG > 32

	/*
	** We can execute two loads and two adds per cycle on PA 8000.
	** But add insn's get serialized waiting for the carry bit.
	** Try to keep 4 registers with "live" values ahead of the ALU.
	*/

"	ldd,ma		8(%1), %4\n"	/* get 1st saddr word */
"	ldd,ma		8(%2), %5\n"	/* get 1st daddr word */
"	add		%4, %0, %0\n"
"	ldd,ma		8(%1), %6\n"	/* 2nd saddr */
"	ldd,ma		8(%2), %7\n"	/* 2nd daddr */
"	add,dc		%5, %0, %0\n"
"	add,dc		%6, %0, %0\n"
"	add,dc		%7, %0, %0\n"
"	add,dc		%3, %0, %0\n"  /* fold in proto+len | carry bit */
"	extrd,u		%0, 31, 32, %4\n"/* copy upper half down */
"	depdi		0, 31, 32, %0\n"/* clear upper half */
"	add		%4, %0, %0\n"	/* fold into 32-bits */
"	addc		0, %0, %0\n"	/* add carry */

#else

	/*
	** For PA 1.x, the insn order doesn't matter as much.
	** Insn stream is serialized on the carry bit here too.
	** result from the previous operation (eg r0 + x)
	*/
"	ldw,ma		4(%1), %4\n"	/* get 1st saddr word */
"	ldw,ma		4(%2), %5\n"	/* get 1st daddr word */
"	add		%4, %0, %0\n"
"	ldw,ma		4(%1), %6\n"	/* 2nd saddr */
"	addc		%5, %0, %0\n"
"	ldw,ma		4(%2), %7\n"	/* 2nd daddr */
"	addc		%6, %0, %0\n"
"	ldw,ma		4(%1), %4\n"	/* 3rd saddr */
"	addc		%7, %0, %0\n"
"	ldw,ma		4(%2), %5\n"	/* 3rd daddr */
"	addc		%4, %0, %0\n"
"	ldw,ma		4(%1), %6\n"	/* 4th saddr */
"	addc		%5, %0, %0\n"
"	ldw,ma		4(%2), %7\n"	/* 4th daddr */
"	addc		%6, %0, %0\n"
"	addc		%7, %0, %0\n"
"	addc		%3, %0, %0\n"	/* fold in proto+len, catch carry */

#endif
	: "=r" (sum), "=r" (saddr), "=r" (daddr), "=r" (len),
	  "=r" (t0), "=r" (t1), "=r" (t2), "=r" (t3)
	: "0" (sum), "1" (saddr), "2" (daddr), "3" (len)
	: "memory");
	return csum_fold(sum);
}

#endif

