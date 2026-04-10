/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PARISC_CHECKSUM_H
#define _PARISC_CHECKSUM_H

#include <linux/in6.h>

#define csum_tcpudp_nofold csum_tcpudp_nofold
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

#include <asm-generic/checksum.h>

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

"	depdi		0, 31, 32, %0\n"/* clear upper half of incoming checksum */
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
"	add,dc		%4, %0, %0\n"	/* fold into 32-bits, plus carry */
"	addc		0, %0, %0\n"	/* add final carry */

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
"	addc		%3, %0, %0\n"	/* fold in proto+len */
"	addc		0, %0, %0\n"	/* add carry */

#endif
	: "=r" (sum), "=r" (saddr), "=r" (daddr), "=r" (len),
	  "=r" (t0), "=r" (t1), "=r" (t2), "=r" (t3)
	: "0" (sum), "1" (saddr), "2" (daddr), "3" (len)
	: "memory");
	return csum_fold(sum);
}

#endif

