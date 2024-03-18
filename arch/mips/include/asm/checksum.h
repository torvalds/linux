/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 96, 97, 98, 99, 2001 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 2001 Thiemo Seufer.
 * Copyright (C) 2002 Maciej W. Rozycki
 * Copyright (C) 2014 Imagination Technologies Ltd.
 */
#ifndef _ASM_CHECKSUM_H
#define _ASM_CHECKSUM_H

#ifdef CONFIG_GENERIC_CSUM
#include <asm-generic/checksum.h>
#else

#include <linux/in6.h>

#include <linux/uaccess.h>

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
__wsum csum_partial(const void *buff, int len, __wsum sum);

__wsum __csum_partial_copy_from_user(const void __user *src, void *dst, int len);
__wsum __csum_partial_copy_to_user(const void *src, void __user *dst, int len);

#define _HAVE_ARCH_COPY_AND_CSUM_FROM_USER
static inline
__wsum csum_and_copy_from_user(const void __user *src, void *dst, int len)
{
	might_fault();
	if (!access_ok(src, len))
		return 0;
	return __csum_partial_copy_from_user(src, dst, len);
}

/*
 * Copy and checksum to user
 */
#define HAVE_CSUM_COPY_USER
static inline
__wsum csum_and_copy_to_user(const void *src, void __user *dst, int len)
{
	might_fault();
	if (!access_ok(dst, len))
		return 0;
	return __csum_partial_copy_to_user(src, dst, len);
}

/*
 * the same as csum_partial, but copies from user space (but on MIPS
 * we have just one address space, so this is identical to the above)
 */
#define _HAVE_ARCH_CSUM_AND_COPY
__wsum __csum_partial_copy_nocheck(const void *src, void *dst, int len);
static inline __wsum csum_partial_copy_nocheck(const void *src, void *dst, int len)
{
	return __csum_partial_copy_nocheck(src, dst, len);
}

/*
 *	Fold a partial checksum without adding pseudo headers
 */
static inline __sum16 csum_fold(__wsum csum)
{
	u32 sum = (__force u32)csum;

	sum += (sum << 16);
	csum = (__force __wsum)(sum < (__force u32)csum);
	sum >>= 16;
	sum += (__force u32)csum;

	return (__force __sum16)~sum;
}
#define csum_fold csum_fold

/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 *
 *	By Jorge Cwik <jorge@laser.satlink.net>, adapted for linux by
 *	Arnt Gulbrandsen.
 */
static inline __sum16 ip_fast_csum(const void *iph, unsigned int ihl)
{
	const unsigned int *word = iph;
	const unsigned int *stop = word + ihl;
	unsigned int csum;
	int carry;

	csum = word[0];
	csum += word[1];
	carry = (csum < word[1]);
	csum += carry;

	csum += word[2];
	carry = (csum < word[2]);
	csum += carry;

	csum += word[3];
	carry = (csum < word[3]);
	csum += carry;

	word += 4;
	do {
		csum += *word;
		carry = (csum < *word);
		csum += carry;
		word++;
	} while (word != stop);

	return csum_fold(csum);
}
#define ip_fast_csum ip_fast_csum

static inline __wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr,
					__u32 len, __u8 proto,
					__wsum isum)
{
	const unsigned int sh32 = IS_ENABLED(CONFIG_64BIT) ? 32 : 0;
	unsigned long sum = (__force unsigned long)daddr;
	unsigned long tmp;
	__u32 osum;

	tmp = (__force unsigned long)saddr;
	sum += tmp;

	if (IS_ENABLED(CONFIG_32BIT))
		sum += sum < tmp;

	/*
	 * We know PROTO + LEN has the sign bit clear, so cast to a signed
	 * type to avoid an extraneous zero-extension where TMP is 64-bit.
	 */
	tmp = (__s32)(proto + len);
	tmp <<= IS_ENABLED(CONFIG_CPU_LITTLE_ENDIAN) ? 8 : 0;
	sum += tmp;
	if (IS_ENABLED(CONFIG_32BIT))
		sum += sum < tmp;

	tmp = (__force unsigned long)isum;
	sum += tmp;

	if (IS_ENABLED(CONFIG_32BIT)) {
		sum += sum < tmp;
		osum = sum;
	} else if (IS_ENABLED(CONFIG_64BIT)) {
		tmp = sum << sh32;
		sum += tmp;
		osum = sum < tmp;
		osum += sum >> sh32;
	} else {
		BUILD_BUG();
	}

	return (__force __wsum)osum;
}
#define csum_tcpudp_nofold csum_tcpudp_nofold

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
static inline __sum16 ip_compute_csum(const void *buff, int len)
{
	return csum_fold(csum_partial(buff, len, 0));
}

#define _HAVE_ARCH_IPV6_CSUM
static __inline__ __sum16 csum_ipv6_magic(const struct in6_addr *saddr,
					  const struct in6_addr *daddr,
					  __u32 len, __u8 proto,
					  __wsum sum)
{
	__wsum tmp;

	__asm__(
	"	.set	push		# csum_ipv6_magic\n"
	"	.set	noreorder	\n"
	"	.set	noat		\n"
	"	addu	%0, %5		# proto (long in network byte order)\n"
	"	sltu	$1, %0, %5	\n"
	"	addu	%0, $1		\n"

	"	addu	%0, %6		# csum\n"
	"	sltu	$1, %0, %6	\n"
	"	lw	%1, 0(%2)	# four words source address\n"
	"	addu	%0, $1		\n"
	"	addu	%0, %1		\n"
	"	sltu	$1, %0, %1	\n"

	"	lw	%1, 4(%2)	\n"
	"	addu	%0, $1		\n"
	"	addu	%0, %1		\n"
	"	sltu	$1, %0, %1	\n"

	"	lw	%1, 8(%2)	\n"
	"	addu	%0, $1		\n"
	"	addu	%0, %1		\n"
	"	sltu	$1, %0, %1	\n"

	"	lw	%1, 12(%2)	\n"
	"	addu	%0, $1		\n"
	"	addu	%0, %1		\n"
	"	sltu	$1, %0, %1	\n"

	"	lw	%1, 0(%3)	\n"
	"	addu	%0, $1		\n"
	"	addu	%0, %1		\n"
	"	sltu	$1, %0, %1	\n"

	"	lw	%1, 4(%3)	\n"
	"	addu	%0, $1		\n"
	"	addu	%0, %1		\n"
	"	sltu	$1, %0, %1	\n"

	"	lw	%1, 8(%3)	\n"
	"	addu	%0, $1		\n"
	"	addu	%0, %1		\n"
	"	sltu	$1, %0, %1	\n"

	"	lw	%1, 12(%3)	\n"
	"	addu	%0, $1		\n"
	"	addu	%0, %1		\n"
	"	sltu	$1, %0, %1	\n"

	"	addu	%0, $1		# Add final carry\n"
	"	.set	pop"
	: "=&r" (sum), "=&r" (tmp)
	: "r" (saddr), "r" (daddr),
	  "0" (htonl(len)), "r" (htonl(proto)), "r" (sum)
	: "memory");

	return csum_fold(sum);
}

#include <asm-generic/checksum.h>
#endif /* CONFIG_GENERIC_CSUM */

#endif /* _ASM_CHECKSUM_H */
