/*
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS_CHECKSUM_H
#define _ASM_NIOS_CHECKSUM_H

/* Take these from lib/checksum.c */
extern __wsum csum_partial(const void *buff, int len, __wsum sum);
extern __wsum csum_partial_copy(const void *src, void *dst, int len,
				__wsum sum);
extern __wsum csum_partial_copy_from_user(const void __user *src, void *dst,
					int len, __wsum sum, int *csum_err);
#define csum_partial_copy_nocheck(src, dst, len, sum)	\
	csum_partial_copy((src), (dst), (len), (sum))

extern __sum16 ip_fast_csum(const void *iph, unsigned int ihl);
extern __sum16 ip_compute_csum(const void *buff, int len);

/*
 * Fold a partial checksum
 */
static inline __sum16 csum_fold(__wsum sum)
{
	__asm__ __volatile__(
		"add	%0, %1, %0\n"
		"cmpltu	r8, %0, %1\n"
		"srli	%0, %0, 16\n"
		"add	%0, %0, r8\n"
		"nor	%0, %0, %0\n"
		: "=r" (sum)
		: "r" (sum << 16), "0" (sum)
		: "r8");
	return (__force __sum16) sum;
}

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
#define csum_tcpudp_nofold csum_tcpudp_nofold
static inline __wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr,
					unsigned short len,
					unsigned short proto,
					__wsum sum)
{
	__asm__ __volatile__(
		"add	%0, %1, %0\n"
		"cmpltu	r8, %0, %1\n"
		"add	%0, %0, r8\n"	/* add carry */
		"add	%0, %2, %0\n"
		"cmpltu	r8, %0, %2\n"
		"add	%0, %0, r8\n"	/* add carry */
		"add	%0, %3, %0\n"
		"cmpltu	r8, %0, %3\n"
		"add	%0, %0, r8\n"	/* add carry */
		: "=r" (sum), "=r" (saddr)
		: "r" (daddr), "r" ((ntohs(len) << 16) + (proto * 256)),
		  "0" (sum),
		  "1" (saddr)
		: "r8");

	return sum;
}

static inline __sum16 csum_tcpudp_magic(__be32 saddr, __be32 daddr,
					unsigned short len,
					unsigned short proto, __wsum sum)
{
	return csum_fold(csum_tcpudp_nofold(saddr, daddr, len, proto, sum));
}

#endif /* _ASM_NIOS_CHECKSUM_H */
