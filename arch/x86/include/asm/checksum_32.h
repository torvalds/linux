#ifndef _ASM_X86_CHECKSUM_32_H
#define _ASM_X86_CHECKSUM_32_H

#include <linux/in6.h>

#include <asm/uaccess.h>

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
asmlinkage __wsum csum_partial(const void *buff, int len, __wsum sum);

/*
 * the same as csum_partial, but copies from src while it
 * checksums, and handles user-space pointer exceptions correctly, when needed.
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */

asmlinkage __wsum csum_partial_copy_generic(const void *src, void *dst,
					    int len, __wsum sum,
					    int *src_err_ptr, int *dst_err_ptr);

/*
 *	Note: when you get a NULL pointer exception here this means someone
 *	passed in an incorrect kernel address to one of these functions.
 *
 *	If you use these functions directly please don't forget the
 *	access_ok().
 */
static inline __wsum csum_partial_copy_nocheck(const void *src, void *dst,
					       int len, __wsum sum)
{
	return csum_partial_copy_generic(src, dst, len, sum, NULL, NULL);
}

static inline __wsum csum_partial_copy_from_user(const void __user *src,
						 void *dst,
						 int len, __wsum sum,
						 int *err_ptr)
{
	__wsum ret;

	might_sleep();
	stac();
	ret = csum_partial_copy_generic((__force void *)src, dst,
					len, sum, err_ptr, NULL);
	clac();

	return ret;
}

/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 *
 *	By Jorge Cwik <jorge@laser.satlink.net>, adapted for linux by
 *	Arnt Gulbrandsen.
 */
static inline __sum16 ip_fast_csum(const void *iph, unsigned int ihl)
{
	unsigned int sum;

	asm volatile("movl (%1), %0	;\n"
		     "subl $4, %2	;\n"
		     "jbe 2f		;\n"
		     "addl 4(%1), %0	;\n"
		     "adcl 8(%1), %0	;\n"
		     "adcl 12(%1), %0;\n"
		     "1:	adcl 16(%1), %0	;\n"
		     "lea 4(%1), %1	;\n"
		     "decl %2	;\n"
		     "jne 1b		;\n"
		     "adcl $0, %0	;\n"
		     "movl %0, %2	;\n"
		     "shrl $16, %0	;\n"
		     "addw %w2, %w0	;\n"
		     "adcl $0, %0	;\n"
		     "notl %0	;\n"
		     "2:		;\n"
	/* Since the input registers which are loaded with iph and ihl
	   are modified, we must also specify them as outputs, or gcc
	   will assume they contain their original values. */
		     : "=r" (sum), "=r" (iph), "=r" (ihl)
		     : "1" (iph), "2" (ihl)
		     : "memory");
	return (__force __sum16)sum;
}

/*
 *	Fold a partial checksum
 */

static inline __sum16 csum_fold(__wsum sum)
{
	asm("addl %1, %0		;\n"
	    "adcl $0xffff, %0	;\n"
	    : "=r" (sum)
	    : "r" ((__force u32)sum << 16),
	      "0" ((__force u32)sum & 0xffff0000));
	return (__force __sum16)(~(__force u32)sum >> 16);
}

static inline __wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr,
					__u32 len, __u8 proto,
					__wsum sum)
{
	asm("addl %1, %0	;\n"
	    "adcl %2, %0	;\n"
	    "adcl %3, %0	;\n"
	    "adcl $0, %0	;\n"
	    : "=r" (sum)
	    : "g" (daddr), "g"(saddr),
	      "g" ((len + proto) << 8), "0" (sum));
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

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */

static inline __sum16 ip_compute_csum(const void *buff, int len)
{
    return csum_fold(csum_partial(buff, len, 0));
}

#define _HAVE_ARCH_IPV6_CSUM
static inline __sum16 csum_ipv6_magic(const struct in6_addr *saddr,
				      const struct in6_addr *daddr,
				      __u32 len, __u8 proto, __wsum sum)
{
	asm("addl 0(%1), %0	;\n"
	    "adcl 4(%1), %0	;\n"
	    "adcl 8(%1), %0	;\n"
	    "adcl 12(%1), %0	;\n"
	    "adcl 0(%2), %0	;\n"
	    "adcl 4(%2), %0	;\n"
	    "adcl 8(%2), %0	;\n"
	    "adcl 12(%2), %0	;\n"
	    "adcl %3, %0	;\n"
	    "adcl %4, %0	;\n"
	    "adcl $0, %0	;\n"
	    : "=&r" (sum)
	    : "r" (saddr), "r" (daddr),
	      "r" (htonl(len)), "r" (htonl(proto)), "0" (sum)
	    : "memory");

	return csum_fold(sum);
}

/*
 *	Copy and checksum to user
 */
#define HAVE_CSUM_COPY_USER
static inline __wsum csum_and_copy_to_user(const void *src,
					   void __user *dst,
					   int len, __wsum sum,
					   int *err_ptr)
{
	__wsum ret;

	might_sleep();
	if (access_ok(VERIFY_WRITE, dst, len)) {
		stac();
		ret = csum_partial_copy_generic(src, (__force void *)dst,
						len, sum, NULL, err_ptr);
		clac();
		return ret;
	}

	if (len)
		*err_ptr = -EFAULT;

	return (__force __wsum)-1; /* invalid checksum */
}

#endif /* _ASM_X86_CHECKSUM_32_H */
