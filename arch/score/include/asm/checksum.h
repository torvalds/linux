#ifndef _ASM_SCORE_CHECKSUM_H
#define _ASM_SCORE_CHECKSUM_H

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
unsigned int csum_partial(const void *buff, int len, __wsum sum);
unsigned int csum_partial_copy_from_user(const char *src, char *dst, int len,
					unsigned int sum, int *csum_err);
unsigned int csum_partial_copy(const char *src, char *dst,
					int len, unsigned int sum);

/*
 * this is a new version of the above that records errors it finds in *errp,
 * but continues and zeros the rest of the buffer.
 */

/*
 * Copy and checksum to user
 */
#define HAVE_CSUM_COPY_USER
static inline
__wsum csum_and_copy_to_user(const void *src, void __user *dst, int len,
			__wsum sum, int *err_ptr)
{
	sum = csum_partial(src, len, sum);
	if (copy_to_user(dst, src, len)) {
		*err_ptr = -EFAULT;
		return (__force __wsum) -1; /* invalid checksum */
	}
	return sum;
}


#define csum_partial_copy_nocheck csum_partial_copy
/*
 *	Fold a partial checksum without adding pseudo headers
 */

static inline __sum16 csum_fold(__wsum sum)
{
	/* the while loop is unnecessary really, it's always enough with two
	   iterations */
	__asm__ __volatile__(
		".set volatile\n\t"
		".set\tr1\n\t"
		"slli\tr1,%0, 16\n\t"
		"add\t%0,%0, r1\n\t"
		"cmp.c\tr1, %0\n\t"
		"srli\t%0, %0, 16\n\t"
		"bleu\t1f\n\t"
		"addi\t%0, 0x1\n\t"
		"1:ldi\tr30, 0xffff\n\t"
		"xor\t%0, %0, r30\n\t"
		"slli\t%0, %0, 16\n\t"
		"srli\t%0, %0, 16\n\t"
		".set\tnor1\n\t"
		".set optimize\n\t"
		: "=r" (sum)
		: "0" (sum));
	return sum;
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
	unsigned long dummy;

	__asm__ __volatile__(
		".set volatile\n\t"
		".set\tnor1\n\t"
		"lw\t%0, [%1]\n\t"
		"subri\t%2, %2, 4\n\t"
		"slli\t%2, %2, 2\n\t"
		"lw\t%3, [%1, 4]\n\t"
		"add\t%2, %2, %1\n\t"
		"add\t%0, %0, %3\n\t"
		"cmp.c\t%3, %0\n\t"
		"lw\t%3, [%1, 8]\n\t"
		"bleu\t1f\n\t"
		"addi\t%0, 0x1\n\t"
		"1:\n\t"
		"add\t%0, %0, %3\n\t"
		"cmp.c\t%3, %0\n\t"
		"lw\t%3, [%1, 12]\n\t"
		"bleu\t1f\n\t"
		"addi\t%0, 0x1\n\t"
		"1:add\t%0, %0, %3\n\t"
		"cmp.c\t%3, %0\n\t"
		"bleu\t1f\n\t"
		"addi\t%0, 0x1\n"

		"1:\tlw\t%3, [%1, 16]\n\t"
		"addi\t%1, 4\n\t"
		"add\t%0, %0, %3\n\t"
		"cmp.c\t%3, %0\n\t"
		"bleu\t2f\n\t"
		"addi\t%0, 0x1\n"
		"2:cmp.c\t%2, %1\n\t"
		"bne\t1b\n\t"

		".set\tr1\n\t"
		".set optimize\n\t"
		: "=&r" (sum), "=&r" (iph), "=&r" (ihl), "=&r" (dummy)
		: "1" (iph), "2" (ihl));

	return csum_fold(sum);
}

static inline __wsum
csum_tcpudp_nofold(__be32 saddr, __be32 daddr, unsigned short len,
		unsigned short proto, __wsum sum)
{
	unsigned long tmp = (ntohs(len) << 16) + proto * 256;
	__asm__ __volatile__(
		".set volatile\n\t"
		"add\t%0, %0, %2\n\t"
		"cmp.c\t%2, %0\n\t"
		"bleu\t1f\n\t"
		"addi\t%0, 0x1\n\t"
		"1:\n\t"
		"add\t%0, %0, %3\n\t"
		"cmp.c\t%3, %0\n\t"
		"bleu\t1f\n\t"
		"addi\t%0, 0x1\n\t"
		"1:\n\t"
		"add\t%0, %0, %4\n\t"
		"cmp.c\t%4, %0\n\t"
		"bleu\t1f\n\t"
		"addi\t%0, 0x1\n\t"
		"1:\n\t"
		".set optimize\n\t"
		: "=r" (sum)
		: "0" (daddr), "r"(saddr),
		"r" (tmp),
		"r" (sum));
	return sum;
}

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
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

static inline unsigned short ip_compute_csum(const void *buff, int len)
{
	return csum_fold(csum_partial(buff, len, 0));
}

#define _HAVE_ARCH_IPV6_CSUM
static inline __sum16 csum_ipv6_magic(const struct in6_addr *saddr,
				const struct in6_addr *daddr,
				__u32 len, unsigned short proto,
				__wsum sum)
{
	__asm__ __volatile__(
		".set\tvolatile\t\t\t# csum_ipv6_magic\n\t"
		"add\t%0, %0, %5\t\t\t# proto (long in network byte order)\n\t"
		"cmp.c\t%5, %0\n\t"
		"bleu 1f\n\t"
		"addi\t%0, 0x1\n\t"
		"1:add\t%0, %0, %6\t\t\t# csum\n\t"
		"cmp.c\t%6, %0\n\t"
		"lw\t%1, [%2, 0]\t\t\t# four words source address\n\t"
		"bleu 1f\n\t"
		"addi\t%0, 0x1\n\t"
		"1:add\t%0, %0, %1\n\t"
		"cmp.c\t%1, %0\n\t"
		"1:lw\t%1, [%2, 4]\n\t"
		"bleu 1f\n\t"
		"addi\t%0, 0x1\n\t"
		"1:add\t%0, %0, %1\n\t"
		"cmp.c\t%1, %0\n\t"
		"lw\t%1, [%2,8]\n\t"
		"bleu 1f\n\t"
		"addi\t%0, 0x1\n\t"
		"1:add\t%0, %0, %1\n\t"
		"cmp.c\t%1, %0\n\t"
		"lw\t%1, [%2, 12]\n\t"
		"bleu 1f\n\t"
		"addi\t%0, 0x1\n\t"
		"1:add\t%0, %0,%1\n\t"
		"cmp.c\t%1, %0\n\t"
		"lw\t%1, [%3, 0]\n\t"
		"bleu 1f\n\t"
		"addi\t%0, 0x1\n\t"
		"1:add\t%0, %0, %1\n\t"
		"cmp.c\t%1, %0\n\t"
		"lw\t%1, [%3, 4]\n\t"
		"bleu 1f\n\t"
		"addi\t%0, 0x1\n\t"
		"1:add\t%0, %0, %1\n\t"
		"cmp.c\t%1, %0\n\t"
		"lw\t%1, [%3, 8]\n\t"
		"bleu 1f\n\t"
		"addi\t%0, 0x1\n\t"
		"1:add\t%0, %0, %1\n\t"
		"cmp.c\t%1, %0\n\t"
		"lw\t%1, [%3, 12]\n\t"
		"bleu 1f\n\t"
		"addi\t%0, 0x1\n\t"
		"1:add\t%0, %0, %1\n\t"
		"cmp.c\t%1, %0\n\t"
		"bleu 1f\n\t"
		"addi\t%0, 0x1\n\t"
		"1:\n\t"
		".set\toptimize"
		: "=r" (sum), "=r" (proto)
		: "r" (saddr), "r" (daddr),
		  "0" (htonl(len)), "1" (htonl(proto)), "r" (sum));

	return csum_fold(sum);
}
#endif /* _ASM_SCORE_CHECKSUM_H */
