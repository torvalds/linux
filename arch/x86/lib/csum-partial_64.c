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

static inline __wsum csum_finalize_sum(u64 temp64)
{
	return (__force __wsum)((temp64 + ror64(temp64, 32)) >> 32);
}

static inline unsigned long update_csum_40b(unsigned long sum, const unsigned long m[5])
{
	asm("addq %1,%0\n\t"
	     "adcq %2,%0\n\t"
	     "adcq %3,%0\n\t"
	     "adcq %4,%0\n\t"
	     "adcq %5,%0\n\t"
	     "adcq $0,%0"
		:"+r" (sum)
		:"m" (m[0]), "m" (m[1]), "m" (m[2]),
		 "m" (m[3]), "m" (m[4]));
	return sum;
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

	/* Do two 40-byte chunks in parallel to get better ILP */
	if (likely(len >= 80)) {
		u64 temp64_2 = 0;
		do {
			temp64 = update_csum_40b(temp64, buff);
			temp64_2 = update_csum_40b(temp64_2, buff + 40);
			buff += 80;
			len -= 80;
		} while (len >= 80);

		asm("addq %1,%0\n\t"
		    "adcq $0,%0"
		    :"+r" (temp64): "r" (temp64_2));
	}

	/*
	 * len == 40 is the hot case due to IPv6 headers, so return
	 * early for that exact case without checking the tail bytes.
	 */
	if (len >= 40) {
		temp64 = update_csum_40b(temp64, buff);
		len -= 40;
		if (!len)
			return csum_finalize_sum(temp64);
		buff += 40;
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
	return csum_finalize_sum(temp64);
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
