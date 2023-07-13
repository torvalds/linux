// SPDX-License-Identifier: GPL-2.0
/*
 * arch/x86_64/lib/csum-partial.c
 *
 * This file contains network checksum routines that are better done
 * in an architecture-specific manner due to speed.
 */

#include <linux/compiler.h>
#include <linux/export.h>
#include <asm/checksum.h>
#include <asm/word-at-a-time.h>

static inline unsigned short from32to16(unsigned a)
{
	unsigned short b = a >> 16;
	asm("addw %w2,%w0\n\t"
	    "adcw $0,%w0\n"
	    : "=r" (b)
	    : "0" (b), "r" (a));
	return b;
}

static inline __wsum csum_tail(u64 temp64, int odd)
{
	unsigned int result;

	result = add32_with_carry(temp64 >> 32, temp64 & 0xffffffff);
	if (unlikely(odd)) {
		result = from32to16(result);
		result = ((result >> 8) & 0xff) | ((result & 0xff) << 8);
	}
	return (__force __wsum)result;
}

/*
 * Do a checksum on an arbitrary memory area.
 * Returns a 32bit checksum.
 *
 * This isn't as time critical as it used to be because many NICs
 * do hardware checksumming these days.
 *
 * Still, with CHECKSUM_COMPLETE this is called to compute
 * checksums on IPv6 headers (40 bytes) and other small parts.
 * it's best to have buff aligned on a 64-bit boundary
 */
__wsum csum_partial(const void *buff, int len, __wsum sum)
{
	u64 temp64 = (__force u64)sum;
	unsigned odd;

	odd = 1 & (unsigned long) buff;
	if (unlikely(odd)) {
		if (unlikely(len == 0))
			return sum;
		temp64 = ror32((__force u32)sum, 8);
		temp64 += (*(unsigned char *)buff << 8);
		len--;
		buff++;
	}

	/*
	 * len == 40 is the hot case due to IPv6 headers, but annotating it likely()
	 * has noticeable negative affect on codegen for all other cases with
	 * minimal performance benefit here.
	 */
	if (len == 40) {
		asm("addq 0*8(%[src]),%[res]\n\t"
		    "adcq 1*8(%[src]),%[res]\n\t"
		    "adcq 2*8(%[src]),%[res]\n\t"
		    "adcq 3*8(%[src]),%[res]\n\t"
		    "adcq 4*8(%[src]),%[res]\n\t"
		    "adcq $0,%[res]"
		    : [res] "+r"(temp64)
		    : [src] "r"(buff), "m"(*(const char(*)[40])buff));
		return csum_tail(temp64, odd);
	}
	if (unlikely(len >= 64)) {
		/*
		 * Extra accumulators for better ILP in the loop.
		 */
		u64 tmp_accum, tmp_carries;

		asm("xorl %k[tmp_accum],%k[tmp_accum]\n\t"
		    "xorl %k[tmp_carries],%k[tmp_carries]\n\t"
		    "subl $64, %[len]\n\t"
		    "1:\n\t"
		    "addq 0*8(%[src]),%[res]\n\t"
		    "adcq 1*8(%[src]),%[res]\n\t"
		    "adcq 2*8(%[src]),%[res]\n\t"
		    "adcq 3*8(%[src]),%[res]\n\t"
		    "adcl $0,%k[tmp_carries]\n\t"
		    "addq 4*8(%[src]),%[tmp_accum]\n\t"
		    "adcq 5*8(%[src]),%[tmp_accum]\n\t"
		    "adcq 6*8(%[src]),%[tmp_accum]\n\t"
		    "adcq 7*8(%[src]),%[tmp_accum]\n\t"
		    "adcl $0,%k[tmp_carries]\n\t"
		    "addq $64, %[src]\n\t"
		    "subl $64, %[len]\n\t"
		    "jge 1b\n\t"
		    "addq %[tmp_accum],%[res]\n\t"
		    "adcq %[tmp_carries],%[res]\n\t"
		    "adcq $0,%[res]"
		    : [tmp_accum] "=&r"(tmp_accum),
		      [tmp_carries] "=&r"(tmp_carries), [res] "+r"(temp64),
		      [len] "+r"(len), [src] "+r"(buff)
		    : "m"(*(const char *)buff));
	}

	if (len & 32) {
		asm("addq 0*8(%[src]),%[res]\n\t"
		    "adcq 1*8(%[src]),%[res]\n\t"
		    "adcq 2*8(%[src]),%[res]\n\t"
		    "adcq 3*8(%[src]),%[res]\n\t"
		    "adcq $0,%[res]"
		    : [res] "+r"(temp64)
		    : [src] "r"(buff), "m"(*(const char(*)[32])buff));
		buff += 32;
	}
	if (len & 16) {
		asm("addq 0*8(%[src]),%[res]\n\t"
		    "adcq 1*8(%[src]),%[res]\n\t"
		    "adcq $0,%[res]"
		    : [res] "+r"(temp64)
		    : [src] "r"(buff), "m"(*(const char(*)[16])buff));
		buff += 16;
	}
	if (len & 8) {
		asm("addq 0*8(%[src]),%[res]\n\t"
		    "adcq $0,%[res]"
		    : [res] "+r"(temp64)
		    : [src] "r"(buff), "m"(*(const char(*)[8])buff));
		buff += 8;
	}
	if (len & 7) {
		unsigned int shift = (-len << 3) & 63;
		unsigned long trail;

		trail = (load_unaligned_zeropad(buff) << shift) >> shift;

		asm("addq %[trail],%[res]\n\t"
		    "adcq $0,%[res]"
		    : [res] "+r"(temp64)
		    : [trail] "r"(trail));
	}
	return csum_tail(temp64, odd);
}
EXPORT_SYMBOL(csum_partial);

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
__sum16 ip_compute_csum(const void *buff, int len)
{
	return csum_fold(csum_partial(buff, len, 0));
}
EXPORT_SYMBOL(ip_compute_csum);
