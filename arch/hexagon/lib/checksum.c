// SPDX-License-Identifier: GPL-2.0-only
/*
 * Checksum functions for Hexagon
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

/*  This was derived from arch/alpha/lib/checksum.c  */


#include <linux/module.h>
#include <linux/string.h>

#include <asm/byteorder.h>
#include <net/checksum.h>
#include <linux/uaccess.h>
#include <asm/intrinsics.h>


/*  Vector value operations  */
#define SIGN(x, y)	((0x8000ULL*x)<<y)
#define CARRY(x, y)	((0x0002ULL*x)<<y)
#define SELECT(x, y)	((0x0001ULL*x)<<y)

#define VR_NEGATE(a, b, c, d)	(SIGN(a, 48) + SIGN(b, 32) + SIGN(c, 16) \
	+ SIGN(d, 0))
#define VR_CARRY(a, b, c, d)	(CARRY(a, 48) + CARRY(b, 32) + CARRY(c, 16) \
	+ CARRY(d, 0))
#define VR_SELECT(a, b, c, d)	(SELECT(a, 48) + SELECT(b, 32) + SELECT(c, 16) \
	+ SELECT(d, 0))


/* optimized HEXAGON V3 intrinsic version */
static inline unsigned short from64to16(u64 x)
{
	u64 sum;

	sum = HEXAGON_P_vrmpyh_PP(x^VR_NEGATE(1, 1, 1, 1),
			     VR_SELECT(1, 1, 1, 1));
	sum += VR_CARRY(0, 0, 1, 0);
	sum = HEXAGON_P_vrmpyh_PP(sum, VR_SELECT(0, 0, 1, 1));

	return 0xFFFF & sum;
}

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented.
 */
__sum16 csum_tcpudp_magic(__be32 saddr, __be32 daddr,
			  __u32 len, __u8 proto, __wsum sum)
{
	return (__force __sum16)~from64to16(
		(__force u64)saddr + (__force u64)daddr +
		(__force u64)sum + ((len + proto) << 8));
}

__wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr,
			  __u32 len, __u8 proto, __wsum sum)
{
	u64 result;

	result = (__force u64)saddr + (__force u64)daddr +
		 (__force u64)sum + ((len + proto) << 8);

	/* Fold down to 32-bits so we don't lose in the typedef-less
	   network stack.  */
	/* 64 to 33 */
	result = (result & 0xffffffffUL) + (result >> 32);
	/* 33 to 32 */
	result = (result & 0xffffffffUL) + (result >> 32);
	return (__force __wsum)result;
}
EXPORT_SYMBOL(csum_tcpudp_nofold);

/*
 * Do a 64-bit checksum on an arbitrary memory area..
 *
 * This isn't a great routine, but it's not _horrible_ either. The
 * inner loop could be unrolled a bit further, and there are better
 * ways to do the carry, but this is reasonable.
 */

/* optimized HEXAGON intrinsic version, with over read fixed */
unsigned int do_csum(const void *voidptr, int len)
{
	u64 sum0, sum1, x0, x1, *ptr8_o, *ptr8_e, *ptr8;
	int i, start, mid, end, mask;
	const char *ptr = voidptr;
	unsigned short *ptr2;
	unsigned int *ptr4;

	if (len <= 0)
		return 0;

	start = 0xF & (16-(((int) ptr) & 0xF)) ;
	mask  = 0x7fffffffUL >> HEXAGON_R_cl0_R(len);
	start = start & mask ;

	mid = len - start;
	end = mid & 0xF;
	mid = mid>>4;
	sum0 = mid << 18;
	sum1 = 0;

	if (start & 1)
		sum0 += (u64) (ptr[0] << 8);
	ptr2 = (unsigned short *) &ptr[start & 1];
	if (start & 2)
		sum1 += (u64) ptr2[0];
	ptr4 = (unsigned int *) &ptr[start & 3];
	if (start & 4) {
		sum0 = HEXAGON_P_vrmpyhacc_PP(sum0,
			VR_NEGATE(0, 0, 1, 1)^((u64)ptr4[0]),
			VR_SELECT(0, 0, 1, 1));
		sum0 += VR_SELECT(0, 0, 1, 0);
	}
	ptr8 = (u64 *) &ptr[start & 7];
	if (start & 8) {
		sum1 = HEXAGON_P_vrmpyhacc_PP(sum1,
			VR_NEGATE(1, 1, 1, 1)^(ptr8[0]),
			VR_SELECT(1, 1, 1, 1));
		sum1 += VR_CARRY(0, 0, 1, 0);
	}
	ptr8_o = (u64 *) (ptr + start);
	ptr8_e = (u64 *) (ptr + start + 8);

	if (mid) {
		x0 = *ptr8_e; ptr8_e += 2;
		x1 = *ptr8_o; ptr8_o += 2;
		if (mid > 1)
			for (i = 0; i < mid-1; i++) {
				sum0 = HEXAGON_P_vrmpyhacc_PP(sum0,
					x0^VR_NEGATE(1, 1, 1, 1),
					VR_SELECT(1, 1, 1, 1));
				sum1 = HEXAGON_P_vrmpyhacc_PP(sum1,
					x1^VR_NEGATE(1, 1, 1, 1),
					VR_SELECT(1, 1, 1, 1));
				x0 = *ptr8_e; ptr8_e += 2;
				x1 = *ptr8_o; ptr8_o += 2;
			}
		sum0 = HEXAGON_P_vrmpyhacc_PP(sum0, x0^VR_NEGATE(1, 1, 1, 1),
			VR_SELECT(1, 1, 1, 1));
		sum1 = HEXAGON_P_vrmpyhacc_PP(sum1, x1^VR_NEGATE(1, 1, 1, 1),
			VR_SELECT(1, 1, 1, 1));
	}

	ptr4 = (unsigned int *) &ptr[start + (mid * 16) + (end & 8)];
	if (end & 4) {
		sum1 = HEXAGON_P_vrmpyhacc_PP(sum1,
			VR_NEGATE(0, 0, 1, 1)^((u64)ptr4[0]),
			VR_SELECT(0, 0, 1, 1));
		sum1 += VR_SELECT(0, 0, 1, 0);
	}
	ptr2 = (unsigned short *) &ptr[start + (mid * 16) + (end & 12)];
	if (end & 2)
		sum0 += (u64) ptr2[0];

	if (end & 1)
		sum1 += (u64) ptr[start + (mid * 16) + (end & 14)];

	ptr8 = (u64 *) &ptr[start + (mid * 16)];
	if (end & 8) {
		sum0 = HEXAGON_P_vrmpyhacc_PP(sum0,
			VR_NEGATE(1, 1, 1, 1)^(ptr8[0]),
			VR_SELECT(1, 1, 1, 1));
		sum0 += VR_CARRY(0, 0, 1, 0);
	}
	sum0 = HEXAGON_P_vrmpyh_PP((sum0+sum1)^VR_NEGATE(0, 0, 0, 1),
		VR_SELECT(0, 0, 1, 1));
	sum0 += VR_NEGATE(0, 0, 0, 1);
	sum0 = HEXAGON_P_vrmpyh_PP(sum0, VR_SELECT(0, 0, 1, 1));

	if (start & 1)
		sum0 = (sum0 << 8) | (0xFF & (sum0 >> 8));

	return 0xFFFF & sum0;
}
