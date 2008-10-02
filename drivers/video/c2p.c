/*
 *  Fast C2P (Chunky-to-Planar) Conversion
 *
 *  Copyright (C) 2003 Geert Uytterhoeven
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
#include "c2p.h"


    /*
     *  Basic transpose step
     */

#define _transp(d, i1, i2, shift, mask)			\
    do {						\
	u32 t = (d[i1] ^ (d[i2] >> shift)) & mask;	\
	d[i1] ^= t;					\
	d[i2] ^= t << shift;				\
    } while (0)

static inline u32 get_mask(int n)
{
    switch (n) {
	case 1:
	    return 0x55555555;
	    break;

	case 2:
	    return 0x33333333;
	    break;

	case 4:
	    return 0x0f0f0f0f;
	    break;

	case 8:
	    return 0x00ff00ff;
	    break;

	case 16:
	    return 0x0000ffff;
	    break;
    }
    return 0;
}

#define transp_nx1(d, n)				\
    do {						\
	u32 mask = get_mask(n);				\
	/* First block */				\
	_transp(d, 0, 1, n, mask);			\
	/* Second block */				\
	_transp(d, 2, 3, n, mask);			\
	/* Third block */				\
	_transp(d, 4, 5, n, mask);			\
	/* Fourth block */				\
	_transp(d, 6, 7, n, mask);			\
    } while (0)

#define transp_nx2(d, n)				\
    do {						\
	u32 mask = get_mask(n);				\
	/* First block */				\
	_transp(d, 0, 2, n, mask);			\
	_transp(d, 1, 3, n, mask);			\
	/* Second block */				\
	_transp(d, 4, 6, n, mask);			\
	_transp(d, 5, 7, n, mask);			\
    } while (0)

#define transp_nx4(d, n)				\
    do {						\
	u32 mask = get_mask(n);				\
	_transp(d, 0, 4, n, mask);			\
	_transp(d, 1, 5, n, mask);			\
	_transp(d, 2, 6, n, mask);			\
	_transp(d, 3, 7, n, mask);			\
    } while (0)

#define transp(d, n, m)	transp_nx ## m(d, n)


    /*
     *  Perform a full C2P step on 32 8-bit pixels, stored in 8 32-bit words
     *  containing
     *    - 32 8-bit chunky pixels on input
     *    - permuted planar data on output
     */

static void c2p_8bpp(u32 d[8])
{
    transp(d, 16, 4);
    transp(d, 8, 2);
    transp(d, 4, 1);
    transp(d, 2, 4);
    transp(d, 1, 2);
}


    /*
     *  Array containing the permution indices of the planar data after c2p
     */

static const int perm_c2p_8bpp[8] = { 7, 5, 3, 1, 6, 4, 2, 0 };


    /*
     *  Compose two values, using a bitmask as decision value
     *  This is equivalent to (a & mask) | (b & ~mask)
     */

static inline unsigned long comp(unsigned long a, unsigned long b,
				 unsigned long mask)
{
	return ((a ^ b) & mask) ^ b;
}


    /*
     *  Store a full block of planar data after c2p conversion
     */

static inline void store_planar(char *dst, u32 dst_inc, u32 bpp, u32 d[8])
{
    int i;

    for (i = 0; i < bpp; i++, dst += dst_inc)
	*(u32 *)dst = d[perm_c2p_8bpp[i]];
}


    /*
     *  Store a partial block of planar data after c2p conversion
     */

static inline void store_planar_masked(char *dst, u32 dst_inc, u32 bpp,
				       u32 d[8], u32 mask)
{
    int i;

    for (i = 0; i < bpp; i++, dst += dst_inc)
	*(u32 *)dst = comp(d[perm_c2p_8bpp[i]], *(u32 *)dst, mask);
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

void c2p(u8 *dst, const u8 *src, u32 dx, u32 dy, u32 width, u32 height,
	 u32 dst_nextline, u32 dst_nextplane, u32 src_nextline, u32 bpp)
{
    int dst_idx;
    u32 d[8], first, last, w;
    const u8 *c;
    u8 *p;

    dst += dy*dst_nextline+(dx & ~31);
    dst_idx = dx % 32;
    first = ~0UL >> dst_idx;
    last = ~(~0UL >> ((dst_idx+width) % 32));
    while (height--) {
	c = src;
	p = dst;
	w = width;
	if (dst_idx+width <= 32) {
	    /* Single destination word */
	    first &= last;
	    memset(d, 0, sizeof(d));
	    memcpy((u8 *)d+dst_idx, c, width);
	    c += width;
	    c2p_8bpp(d);
	    store_planar_masked(p, dst_nextplane, bpp, d, first);
	    p += 4;
	} else {
	    /* Multiple destination words */
	    w = width;
	    /* Leading bits */
	    if (dst_idx) {
		w = 32 - dst_idx;
		memset(d, 0, dst_idx);
		memcpy((u8 *)d+dst_idx, c, w);
		c += w;
		c2p_8bpp(d);
		store_planar_masked(p, dst_nextplane, bpp, d, first);
		p += 4;
		w = width-w;
	    }
	    /* Main chunk */
	    while (w >= 32) {
		memcpy(d, c, 32);
		c += 32;
		c2p_8bpp(d);
		store_planar(p, dst_nextplane, bpp, d);
		p += 4;
		w -= 32;
	    }
	    /* Trailing bits */
	    w %= 32;
	    if (w > 0) {
		memcpy(d, c, w);
		memset((u8 *)d+w, 0, 32-w);
		c2p_8bpp(d);
		store_planar_masked(p, dst_nextplane, bpp, d, last);
	    }
	}
	src += src_nextline;
	dst += dst_nextline;
    }
}
EXPORT_SYMBOL_GPL(c2p);

MODULE_LICENSE("GPL");
