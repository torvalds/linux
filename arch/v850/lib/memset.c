/*
 * arch/v850/lib/memset.c -- Memory initialization
 *
 *  Copyright (C) 2001,02,04  NEC Corporation
 *  Copyright (C) 2001,02,04  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/types.h>

void *memset (void *dst, int val, __kernel_size_t count)
{
	if (count) {
		register unsigned loop;
		register void *ptr asm ("ep") = dst;

		/* replicate VAL into a long.  */
		val &= 0xff;
		val |= val << 8;
		val |= val << 16;

		/* copy initial unaligned bytes.  */
		if ((long)ptr & 1) {
			*(char *)ptr = val;
			ptr = (void *)((char *)ptr + 1);
			count--;
		}
		if (count > 2 && ((long)ptr & 2)) {
			*(short *)ptr = val;
			ptr = (void *)((short *)ptr + 1);
			count -= 2;
		}

		/* 32-byte copying loop.  */
		for (loop = count / 32; loop; loop--) {
			asm ("sst.w %0, 0[ep]; sst.w %0, 4[ep];"
			     "sst.w %0, 8[ep]; sst.w %0, 12[ep];"
			     "sst.w %0, 16[ep]; sst.w %0, 20[ep];"
			     "sst.w %0, 24[ep]; sst.w %0, 28[ep]"
			     :: "r" (val) : "memory");
			ptr += 32;
		}
		count %= 32;

		/* long copying loop.  */
		for (loop = count / 4; loop; loop--) {
			*(long *)ptr = val;
			ptr = (void *)((long *)ptr + 1);
		}
		count %= 4;

		/* finish up with any trailing bytes.  */
		if (count & 2) {
			*(short *)ptr = val;
			ptr = (void *)((short *)ptr + 1);
		}
		if (count & 1) {
			*(char *)ptr = val;
		}
	}

	return dst;
}
