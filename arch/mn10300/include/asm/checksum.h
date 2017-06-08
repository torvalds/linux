/* MN10300 Optimised checksumming code
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_CHECKSUM_H
#define _ASM_CHECKSUM_H

extern __wsum csum_partial(const void *buff, int len, __wsum sum);
extern __wsum csum_partial_copy_nocheck(const void *src, void *dst,
					int len, __wsum sum);
extern __wsum csum_partial_copy_from_user(const void *src, void *dst,
					  int len, __wsum sum,
					  int *err_ptr);
extern __sum16 ip_fast_csum(const void *iph, unsigned int ihl);
extern __wsum csum_partial(const void *buff, int len, __wsum sum);
extern __sum16 ip_compute_csum(const void *buff, int len);

#define csum_partial_copy_fromuser csum_partial_copy
extern __wsum csum_partial_copy(const void *src, void *dst, int len,
				__wsum sum);

static inline __sum16 csum_fold(__wsum sum)
{
	asm(
		"	add	%1,%0		\n"
		"	addc	0xffff,%0	\n"
		: "=r" (sum)
		: "r" (sum << 16), "0" (sum & 0xffff0000)
		: "cc"
	    );
	return (~sum) >> 16;
}

static inline __wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr,
					__u32 len, __u8 proto,
					__wsum sum)
{
	__wsum tmp = (__wsum)((len + proto) << 8);

	asm(
		"	add	%1,%0		\n"
		"	addc	%2,%0		\n"
		"	addc	%3,%0		\n"
		"	addc	0,%0		\n"
		: "=r" (sum)
		: "r" (daddr), "r"(saddr), "r"(tmp), "0"(sum)
		: "cc"
	    );
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
	return csum_fold(csum_tcpudp_nofold(saddr, daddr, len, proto, sum));
}

#undef _HAVE_ARCH_IPV6_CSUM

/*
 *	Copy and checksum to user
 */
#define HAVE_CSUM_COPY_USER
extern __wsum csum_and_copy_to_user(const void *src, void *dst, int len,
				    __wsum sum, int *err_ptr);


#endif /* _ASM_CHECKSUM_H */
