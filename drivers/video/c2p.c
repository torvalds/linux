/*
 *  Fast C2P (Chunky-to-Planar) Conversion
 *
 *  Copyright (C) 2003-2008 Geert Uytterhoeven
 *
 *  NOTES:
 *    - This code was inspired by Scout's C2P tutorial
 *    - It assumes to run on a big endian system
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive
 *  for more details.
 */

#include <linux/module.h>
#include <linux/string.h>

#include <asm/unaligned.h>

#include "c2p.h"


    /*
     *  Basic transpose step
     */

static inline void _transp(u32 d[], unsigned int i1, unsigned int i2,
			   unsigned int shift, u32 mask)
{
	u32 t = (d[i1] ^ (d[i2] >> shift)) & mask;

	d[i1] ^= t;
	d[i2] ^= t << shift;
}

extern void c2p_unsupported(void);

static inline u32 get_mask(unsigned int n)
{
	switch (n) {
	case 1:
		return 0x55555555;

	case 2:
		return 0x33333333;

	case 4:
		return 0x0f0f0f0f;

	case 8:
		return 0x00ff00ff;

	case 16:
		return 0x0000ffff;
	}

	c2p_unsupported();
	return 0;
}

static inline void transp8(u32 d[], unsigned int n, unsigned int m)
{
	u32 mask = get_mask(n);

	switch (m) {
	case 1:
		/* First n x 1 block */
		_transp(d, 0, 1, n, mask);
		/* Second n x 1 block */
		_transp(d, 2, 3, n, mask);
		/* Third n x 1 block */
		_transp(d, 4, 5, n, mask);
		/* Fourth n x 1 block */
		_transp(d, 6, 7, n, mask);
		return;

	case 2:
		/* First n x 2 block */
		_transp(d, 0, 2, n, mask);
		_transp(d, 1, 3, n, mask);
		/* Second n x 2 block */
		_transp(d, 4, 6, n, mask);
		_transp(d, 5, 7, n, mask);
		return;

	case 4:
		/* Single n x 4 block */
		_transp(d, 0, 4, n, mask);
		_transp(d, 1, 5, n, mask);
		_transp(d, 2, 6, n, mask);
		_transp(d, 3, 7, n, mask);
		return;
	}

	c2p_unsupported();
}


    /*
     *  Perform a full C2P step on 32 8-bit pixels, stored in 8 32-bit words
     *  containing
     *    - 32 8-bit chunky pixels on input
     *    - permutated planar data (1 plane per 32-bit word) on output
     */

static void c2p_32x8(u32 d[8])
{
	transp8(d, 16, 4);
	transp8(d, 8, 2);
	transp8(d, 4, 1);
	transp8(d, 2, 4);
	transp8(d, 1, 2);
}


    /*
     *  Array containing the permutation indices of the planar data after c2p
     */

static const int perm_c2p_32x8[8] = { 7, 5, 3, 1, 6, 4, 2, 0 };


    /*
     *  Compose two values, using a bitmask as decision value
     *  This is equivalent to (a & mask) | (b & ~mask)
     */

static inline u32 comp(u32 a, u32 b, u32 mask)
{
	return ((a ^ b) & mask) ^ b;
}


    /*
     *  Store a full block of planar data after c2p conversion
     */

static inline void store_planar(void *dst, u32 dst_inc, u32 bpp, u32 d[8])
{
	int i;

	for (i = 0; i < bpp; i++, dst += dst_inc)
		put_unaligned_be32(d[perm_c2p_32x8[i]], dst);
}


    /*
     *  Store a partial block of planar data after c2p conversion
     */

static inline void store_planar_masked(void *dst, u32 dst_inc, u32 bpp,
				       u32 d[8], u32 mask)
{
	int i;

	for (i = 0; i < bpp; i++, dst += dst_inc)
		put_unaligned_be32(comp(d[perm_c2p_32x8[i]],
					get_unaligned_be32(dst), mask),
				   dst);
}


    /*
     *  c2p - Copy 8-bit chunky image data to a planar frame buffer
     *  @dst: Starting address of the planar frame buffer
     *  @dx: Horizontal destination offset (in pixels)
     *  @dy: Vertical destination offset (in pixels)
     *  @width: Image width (in pixels)
     *  @height: Image height (in pixels)
     *  @dst_nextline: Frame buffer offset to the next line (in bytes)
     *  @dst_nextplane: Frame buffer offset to the next plane (in bytes)
     *  @src_nextline: Image offset to the next line (in bytes)
     *  @bpp: Bits per pixel of the planar frame buffer (1-8)
     */

void c2p(void *dst, const void *src, u32 dx, u32 dy, u32 width, u32 height,
	 u32 dst_nextline, u32 dst_nextplane, u32 src_nextline, u32 bpp)
{
	union {
		u8 pixels[32];
		u32 words[8];
	} d;
	u32 dst_idx, first, last, w;
	const u8 *c;
	void *p;

	dst += dy*dst_nextline+(dx & ~31);
	dst_idx = dx % 32;
	first = 0xffffffffU >> dst_idx;
	last = ~(0xffffffffU >> ((dst_idx+width) % 32));
	while (height--) {
		c = src;
		p = dst;
		w = width;
		if (dst_idx+width <= 32) {
			/* Single destination word */
			first &= last;
			memset(d.pixels, 0, sizeof(d));
			memcpy(d.pixels+dst_idx, c, width);
			c += width;
			c2p_32x8(d.words);
			store_planar_masked(p, dst_nextplane, bpp, d.words,
					    first);
			p += 4;
		} else {
			/* Multiple destination words */
			w = width;
			/* Leading bits */
			if (dst_idx) {
				w = 32 - dst_idx;
				memset(d.pixels, 0, dst_idx);
				memcpy(d.pixels+dst_idx, c, w);
				c += w;
				c2p_32x8(d.words);
				store_planar_masked(p, dst_nextplane, bpp,
						    d.words, first);
				p += 4;
				w = width-w;
			}
			/* Main chunk */
			while (w >= 32) {
				memcpy(d.pixels, c, 32);
				c += 32;
				c2p_32x8(d.words);
				store_planar(p, dst_nextplane, bpp, d.words);
				p += 4;
				w -= 32;
			}
			/* Trailing bits */
			w %= 32;
			if (w > 0) {
				memcpy(d.pixels, c, w);
				memset(d.pixels+w, 0, 32-w);
				c2p_32x8(d.words);
				store_planar_masked(p, dst_nextplane, bpp,
						    d.words, last);
			}
		}
		src += src_nextline;
		dst += dst_nextline;
	}
}
EXPORT_SYMBOL_GPL(c2p);

MODULE_LICENSE("GPL");
