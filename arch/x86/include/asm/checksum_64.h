#ifndef _ASM_X86_CHECKSUM_64_H
#define _ASM_X86_CHECKSUM_64_H

/*
 * Checksums for x86-64
 * Copyright 2002 by Andi Kleen, SuSE Labs
 * with some code from asm-x86/checksum.h
 */

#include <linux/compiler.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>

/**
 * csum_fold - Fold and invert a 32bit checksum.
 * sum: 32bit unfolded sum
 *
 * Fold a 32bit running checksum to 16bit and invert it. This is usually
 * the last step before putting a checksum into a packet.
 * Make sure not to mix with 64bit checksums.
 */
static inline __sum16 csum_fold(__wsum sum)
{
	asm("  addl %1,%0\n"
	    "  adcl $0xffff,%0"
	    : "=r" (sum)
	    : "r" ((__force u32)sum << 16),
	      "0" ((__force u32)sum & 0xffff0000));
	return (__force __sum16)(~(__force u32)sum >> 16);
}

/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 *
 *	By Jorge Cwik <jorge@laser.satlink.net>, adapted for linux by
 *	Arnt Gulbrandsen.
 */

/**
 * ip_fast_csum - Compute the IPv4 header checksum efficiently.
 * iph: ipv4 header
 * ihl: length of header / 4
 */
static inline __sum16 ip_fast_csum(const void *iph, unsigned int ihl)
{
	unsigned int sum;

	asm("  movl (%1), %0\n"
	    "  subl $4, %2\n"
	    "  jbe 2f\n"
	    "  addl 4(%1), %0\n"
	    "  adcl 8(%1), %0\n"
	    "  adcl 12(%1), %0\n"
	    "1: adcl 16(%1), %0\n"
	    "  lea 4(%1), %1\n"
	    "  decl %2\n"
	    "  jne	1b\n"
	    "  adcl $0, %0\n"
	    "  movl %0, %2\n"
	    "  shrl $16, %0\n"
	    "  addw %w2, %w0\n"
	    "  adcl $0, %0\n"
	    "  notl %0\n"
	    "2:"
	/* Since the input registers which are loaded with iph and ihl
	   are modified, we must also specify them as outputs, or gcc
	   will assume they contain their original values. */
	    : "=r" (sum), "=r" (iph), "=r" (ihl)
	    : "1" (iph), "2" (ihl)
	    : "memory");
	return (__force __sum16)sum;
}

/**
 * csum_tcpup_nofold - Compute an IPv4 pseudo header checksum.
 * @saddr: source address
 * @daddr: destination address
 * @len: length of packet
 * @proto: ip protocol of packet
 * @sum: initial sum to be added in (32bit unfolded)
 *
 * Returns the pseudo header checksum the input data. Result is
 * 32bit unfolded.
 */
static inline __wsum
csum_tcpudp_nofold(__be32 saddr, __be32 daddr, unsigned short len,
		   unsigned short proto, __wsum sum)
{
	asm("  addl %1, %0\n"
	    "  adcl %2, %0\n"
	    "  adcl %3, %0\n"
	    "  adcl $0, %0\n"
	    : "=r" (sum)
	    : "g" (daddr), "g" (saddr),
	      "g" ((len + proto)<<8), "0" (sum));
	return sum;
}


/**
 * csum_tcpup_magic - Compute an IPv4 pseudo header checksum.
 * @saddr: source address
 * @daddr: destination address
 * @len: length of packet
 * @proto: ip protocol of packet
 * @sum: initial sum to be added in (32bit unfolded)
 *
 * Returns the 16bit pseudo header checksum the input data already
 * complemented and ready to be filled in.
 */
static inline __sum16 csum_tcpudp_magic(__be32 saddr, __be32 daddr,
					unsigned short len,
					unsigned short proto, __wsum sum)
{
	return csum_fold(csum_tcpudp_nofold(saddr, daddr, len, proto, sum));
}

/**
 * csum_partial - Compute an internet checksum.
 * @buff: buffer to be checksummed
 * @len: length of buffer.
 * @sum: initial sum to be added in (32bit unfolded)
 *
 * Returns the 32bit unfolded internet checksum of the buffer.
 * Before filling it in it needs to be csum_fold()'ed.
 * buff should be aligned to a 64bit boundary if possible.
 */
extern __wsum csum_partial(const void *buff, int len, __wsum sum);

#define  _HAVE_ARCH_COPY_AND_CSUM_FROM_USER 1
#define HAVE_CSUM_COPY_USER 1


/* Do not call this directly. Use the wrappers below */
extern __wsum csum_partial_copy_generic(const void *src, const void *dst,
					int len, __wsum sum,
					int *src_err_ptr, int *dst_err_ptr);


extern __wsum csum_partial_copy_from_user(const void __user *src, void *dst,
					  int len, __wsum isum, int *errp);
extern __wsum csum_partial_copy_to_user(const void *src, void __user *dst,
					int len, __wsum isum, int *errp);
extern __wsum csum_partial_copy_nocheck(const void *src, void *dst,
					int len, __wsum sum);

/* Old names. To be removed. */
#define csum_and_copy_to_user csum_partial_copy_to_user
#define csum_and_copy_from_user csum_partial_copy_from_user

/**
 * ip_compute_csum - Compute an 16bit IP checksum.
 * @buff: buffer address.
 * @len: length of buffer.
 *
 * Returns the 16bit folded/inverted checksum of the passed buffer.
 * Ready to fill in.
 */
extern __sum16 ip_compute_csum(const void *buff, int len);

/**
 * csum_ipv6_magic - Compute checksum of an IPv6 pseudo header.
 * @saddr: source address
 * @daddr: destination address
 * @len: length of packet
 * @proto: protocol of packet
 * @sum: initial sum (32bit unfolded) to be added in
 *
 * Computes an IPv6 pseudo header checksum. This sum is added the checksum
 * into UDP/TCP packets and contains some link layer information.
 * Returns the unfolded 32bit checksum.
 */

struct in6_addr;

#define _HAVE_ARCH_IPV6_CSUM 1
extern __sum16
csum_ipv6_magic(const struct in6_addr *saddr, const struct in6_addr *daddr,
		__u32 len, unsigned short proto, __wsum sum);

static inline unsigned add32_with_carry(unsigned a, unsigned b)
{
	asm("addl %2,%0\n\t"
	    "adcl $0,%0"
	    : "=r" (a)
	    : "0" (a), "r" (b));
	return a;
}

#endif /* _ASM_X86_CHECKSUM_64_H */
