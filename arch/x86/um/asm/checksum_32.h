/*
 * Licensed under the GPL
 */

#ifndef __UM_SYSDEP_CHECKSUM_H
#define __UM_SYSDEP_CHECKSUM_H

static inline __sum16 ip_compute_csum(const void *buff, int len)
{
    return csum_fold (csum_partial(buff, len, 0));
}

#define _HAVE_ARCH_IPV6_CSUM
static __inline__ __sum16 csum_ipv6_magic(const struct in6_addr *saddr,
					  const struct in6_addr *daddr,
					  __u32 len, __u8 proto,
					  __wsum sum)
{
	__asm__(
		"addl 0(%1), %0		;\n"
		"adcl 4(%1), %0		;\n"
		"adcl 8(%1), %0		;\n"
		"adcl 12(%1), %0	;\n"
		"adcl 0(%2), %0		;\n"
		"adcl 4(%2), %0		;\n"
		"adcl 8(%2), %0		;\n"
		"adcl 12(%2), %0	;\n"
		"adcl %3, %0		;\n"
		"adcl %4, %0		;\n"
		"adcl $0, %0		;\n"
		: "=&r" (sum)
		: "r" (saddr), "r" (daddr),
		  "r"(htonl(len)), "r"(htonl(proto)), "0"(sum));

	return csum_fold(sum);
}

#endif
