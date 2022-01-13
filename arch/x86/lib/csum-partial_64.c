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
	unsigned odd, result;

	odd = 1 & (unsigned long) buff;
	if (unlikely(odd)) {
		if (unlikely(len == 0))
			return sum;
		temp64 = ror32((__force u32)sum, 8);
		temp64 += (*(unsigned char *)buff << 8);
		len--;
		buff++;
	}

	while (unlikely(len >= 64)) {
		asm("addq 0*8(%[src]),%[res]\n\t"
		    "adcq 1*8(%[src]),%[res]\n\t"
		    "adcq 2*8(%[src]),%[res]\n\t"
		    "adcq 3*8(%[src]),%[res]\n\t"
		    "adcq 4*8(%[src]),%[res]\n\t"
		    "adcq 5*8(%[src]),%[res]\n\t"
		    "adcq 6*8(%[src]),%[res]\n\t"
		    "adcq 7*8(%[src]),%[res]\n\t"
		    "adcq $0,%[res]"
		    : [res] "+r" (temp64)
		    : [src] "r" (buff)
		    : "memory");
		buff += 64;
		len -= 64;
	}

	if (len & 32) {
		asm("addq 0*8(%[src]),%[res]\n\t"
		    "adcq 1*8(%[src]),%[res]\n\t"
		    "adcq 2*8(%[src]),%[res]\n\t"
		    "adcq 3*8(%[src]),%[res]\n\t"
		    "adcq $0,%[res]"
			: [res] "+r" (temp64)
			: [src] "r" (buff)
			: "memory");
		buff += 32;
	}
	if (len & 16) {
		asm("addq 0*8(%[src]),%[res]\n\t"
		    "adcq 1*8(%[src]),%[res]\n\t"
		    "adcq $0,%[res]"
			: [res] "+r" (temp64)
			: [src] "r" (buff)
			: "memory");
		buff += 16;
	}
	if (len & 8) {
		asm("addq 0*8(%[src]),%[res]\n\t"
		    "adcq $0,%[res]"
			: [res] "+r" (temp64)
			: [src] "r" (buff)
			: "memory");
		buff += 8;
	}
	if (len & 7) {
#ifdef CONFIG_DCACHE_WORD_ACCESS
		unsigned int shift = (8 - (len & 7)) * 8;
		unsigned long trail;

		trail = (load_unaligned_zeropad(buff) << shift) >> shift;

		asm("addq %[trail],%[res]\n\t"
		    "adcq $0,%[res]"
			: [res] "+r" (temp64)
			: [trail] "r" (trail));
#else
		if (len & 4) {
			asm("addq %[val],%[res]\n\t"
			    "adcq $0,%[res]"
				: [res] "+r" (temp64)
				: [val] "r" ((u64)*(u32 *)buff)
				: "memory");
			buff += 4;
		}
		if (len & 2) {
			asm("addq %[val],%[res]\n\t"
			    "adcq $0,%[res]"
				: [res] "+r" (temp64)
				: [val] "r" ((u64)*(u16 *)buff)
				: "memory");
			buff += 2;
		}
		if (len & 1) {
			asm("addq %[val],%[res]\n\t"
			    "adcq $0,%[res]"
				: [res] "+r" (temp64)
				: [val] "r" ((u64)*(u8 *)buff)
				: "memory");
		}
#endif
	}
	result = add32_with_carry(temp64 >> 32, temp64 & 0xffffffff);
	if (unlikely(odd)) {
		result = from32to16(result);
		result = ((result >> 8) & 0xff) | ((result & 0xff) << 8);
	}
	return (__force __wsum)result;
}
EXPORT_SYMBOL(csum_partial);

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
__sum16 ip_compute_csum(const void *buff, int len)
{
	return csum_fold(csum_partial(buff,len,0));
}
EXPORT_SYMBOL(ip_compute_csum);
