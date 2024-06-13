/*
 *  linux/drivers/video/iplan2p8.c -- Low level frame buffer operations for
 *				      interleaved bitplanes à la Atari (8
 *				      planes, 2 bytes interleave)
 *
 *	Created 5 Apr 1997 by Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/string.h>
#include <linux/fb.h>

#include <asm/setup.h>

#include "atafb.h"

#define BPL	8
#include "atafb_utils.h"


/* Copies a 8 plane column from 's', height 'h', to 'd'. */

/* This expands a 8 bit color into two longs for two movepl (8 plane)
 * operations.
 */

void atafb_iplan2p8_copyarea(struct fb_info *info, u_long next_line,
			     int sy, int sx, int dy, int dx,
			     int height, int width)
{
	/*  bmove() has to distinguish two major cases: If both, source and
	 *  destination, start at even addresses or both are at odd
	 *  addresses, just the first odd and last even column (if present)
	 *  require special treatment (memmove_col()). The rest between
	 *  then can be copied by normal operations, because all adjacent
	 *  bytes are affected and are to be stored in the same order.
	 *    The pathological case is when the move should go from an odd
	 *  address to an even or vice versa. Since the bytes in the plane
	 *  words must be assembled in new order, it seems wisest to make
	 *  all movements by memmove_col().
	 */

	u8 *src, *dst;
	u32 *s, *d;
	int w, l , i, j;
	u_int colsize;
	u_int upwards = (dy < sy) || (dy == sy && dx < sx);

	colsize = height;
	if (!((sx ^ dx) & 15)) {
		/* odd->odd or even->even */

		if (upwards) {
			src = (u8 *)info->screen_base + sy * next_line + (sx & ~15) / (8 / BPL);
			dst = (u8 *)info->screen_base + dy * next_line + (dx & ~15) / (8 / BPL);
			if (sx & 15) {
				memmove32_col(dst, src, 0xff00ff, height, next_line - BPL * 2);
				src += BPL * 2;
				dst += BPL * 2;
				width -= 8;
			}
			w = width >> 4;
			if (w) {
				s = (u32 *)src;
				d = (u32 *)dst;
				w *= BPL / 2;
				l = next_line - w * 4;
				for (j = height; j > 0; j--) {
					for (i = w; i > 0; i--)
						*d++ = *s++;
					s = (u32 *)((u8 *)s + l);
					d = (u32 *)((u8 *)d + l);
				}
			}
			if (width & 15)
				memmove32_col(dst + width / (8 / BPL), src + width / (8 / BPL),
					      0xff00ff00, height, next_line - BPL * 2);
		} else {
			src = (u8 *)info->screen_base + (sy - 1) * next_line + ((sx + width + 8) & ~15) / (8 / BPL);
			dst = (u8 *)info->screen_base + (dy - 1) * next_line + ((dx + width + 8) & ~15) / (8 / BPL);

			if ((sx + width) & 15) {
				src -= BPL * 2;
				dst -= BPL * 2;
				memmove32_col(dst, src, 0xff00ff00, colsize, -next_line - BPL * 2);
				width -= 8;
			}
			w = width >> 4;
			if (w) {
				s = (u32 *)src;
				d = (u32 *)dst;
				w *= BPL / 2;
				l = next_line - w * 4;
				for (j = height; j > 0; j--) {
					for (i = w; i > 0; i--)
						*--d = *--s;
					s = (u32 *)((u8 *)s - l);
					d = (u32 *)((u8 *)d - l);
				}
			}
			if (sx & 15)
				memmove32_col(dst - (width - 16) / (8 / BPL),
					      src - (width - 16) / (8 / BPL),
					      0xff00ff, colsize, -next_line - BPL * 2);
		}
	} else {
		/* odd->even or even->odd */
		if (upwards) {
			u32 *src32, *dst32;
			u32 pval[4], v, v1, mask;
			int i, j, w, f;

			src = (u8 *)info->screen_base + sy * next_line + (sx & ~15) / (8 / BPL);
			dst = (u8 *)info->screen_base + dy * next_line + (dx & ~15) / (8 / BPL);

			mask = 0xff00ff00;
			f = 0;
			w = width;
			if (sx & 15) {
				f = 1;
				w += 8;
			}
			if ((sx + width) & 15)
				f |= 2;
			w >>= 4;
			for (i = height; i; i--) {
				src32 = (u32 *)src;
				dst32 = (u32 *)dst;

				if (f & 1) {
					pval[0] = (*src32++ << 8) & mask;
					pval[1] = (*src32++ << 8) & mask;
					pval[2] = (*src32++ << 8) & mask;
					pval[3] = (*src32++ << 8) & mask;
				} else {
					pval[0] = dst32[0] & mask;
					pval[1] = dst32[1] & mask;
					pval[2] = dst32[2] & mask;
					pval[3] = dst32[3] & mask;
				}

				for (j = w; j > 0; j--) {
					v = *src32++;
					v1 = v & mask;
					*dst32++ = pval[0] | (v1 >> 8);
					pval[0] = (v ^ v1) << 8;
					v = *src32++;
					v1 = v & mask;
					*dst32++ = pval[1] | (v1 >> 8);
					pval[1] = (v ^ v1) << 8;
					v = *src32++;
					v1 = v & mask;
					*dst32++ = pval[2] | (v1 >> 8);
					pval[2] = (v ^ v1) << 8;
					v = *src32++;
					v1 = v & mask;
					*dst32++ = pval[3] | (v1 >> 8);
					pval[3] = (v ^ v1) << 8;
				}

				if (f & 2) {
					dst32[0] = (dst32[0] & mask) | pval[0];
					dst32[1] = (dst32[1] & mask) | pval[1];
					dst32[2] = (dst32[2] & mask) | pval[2];
					dst32[3] = (dst32[3] & mask) | pval[3];
				}

				src += next_line;
				dst += next_line;
			}
		} else {
			u32 *src32, *dst32;
			u32 pval[4], v, v1, mask;
			int i, j, w, f;

			src = (u8 *)info->screen_base + (sy - 1) * next_line + ((sx + width + 8) & ~15) / (8 / BPL);
			dst = (u8 *)info->screen_base + (dy - 1) * next_line + ((dx + width + 8) & ~15) / (8 / BPL);

			mask = 0xff00ff;
			f = 0;
			w = width;
			if ((dx + width) & 15)
				f = 1;
			if (sx & 15) {
				f |= 2;
				w += 8;
			}
			w >>= 4;
			for (i = height; i; i--) {
				src32 = (u32 *)src;
				dst32 = (u32 *)dst;

				if (f & 1) {
					pval[0] = dst32[-1] & mask;
					pval[1] = dst32[-2] & mask;
					pval[2] = dst32[-3] & mask;
					pval[3] = dst32[-4] & mask;
				} else {
					pval[0] = (*--src32 >> 8) & mask;
					pval[1] = (*--src32 >> 8) & mask;
					pval[2] = (*--src32 >> 8) & mask;
					pval[3] = (*--src32 >> 8) & mask;
				}

				for (j = w; j > 0; j--) {
					v = *--src32;
					v1 = v & mask;
					*--dst32 = pval[0] | (v1 << 8);
					pval[0] = (v ^ v1) >> 8;
					v = *--src32;
					v1 = v & mask;
					*--dst32 = pval[1] | (v1 << 8);
					pval[1] = (v ^ v1) >> 8;
					v = *--src32;
					v1 = v & mask;
					*--dst32 = pval[2] | (v1 << 8);
					pval[2] = (v ^ v1) >> 8;
					v = *--src32;
					v1 = v & mask;
					*--dst32 = pval[3] | (v1 << 8);
					pval[3] = (v ^ v1) >> 8;
				}

				if (!(f & 2)) {
					dst32[-1] = (dst32[-1] & mask) | pval[0];
					dst32[-2] = (dst32[-2] & mask) | pval[1];
					dst32[-3] = (dst32[-3] & mask) | pval[2];
					dst32[-4] = (dst32[-4] & mask) | pval[3];
				}

				src -= next_line;
				dst -= next_line;
			}
		}
	}
}

void atafb_iplan2p8_fillrect(struct fb_info *info, u_long next_line, u32 color,
                             int sy, int sx, int height, int width)
{
	u32 *dest;
	int rows, i;
	u32 cval[4];

	dest = (u32 *)(info->screen_base + sy * next_line + (sx & ~15) / (8 / BPL));
	if (sx & 15) {
		u8 *dest8 = (u8 *)dest + 1;

		expand8_col2mask(color, cval);

		for (i = height; i; i--) {
			fill8_col(dest8, cval);
			dest8 += next_line;
		}
		dest += BPL / 2;
		width -= 8;
	}

	expand16_col2mask(color, cval);
	rows = width >> 4;
	if (rows) {
		u32 *d = dest;
		u32 off = next_line - rows * BPL * 2;
		for (i = height; i; i--) {
			d = fill16_col(d, rows, cval);
			d = (u32 *)((long)d + off);
		}
		dest += rows * BPL / 2;
		width &= 15;
	}

	if (width) {
		u8 *dest8 = (u8 *)dest;

		expand8_col2mask(color, cval);

		for (i = height; i; i--) {
			fill8_col(dest8, cval);
			dest8 += next_line;
		}
	}
}

void atafb_iplan2p8_linefill(struct fb_info *info, u_long next_line,
			     int dy, int dx, u32 width,
			     const u8 *data, u32 bgcolor, u32 fgcolor)
{
	u32 *dest;
	const u16 *data16;
	int rows;
	u32 fgm[4], bgm[4], m;

	dest = (u32 *)(info->screen_base + dy * next_line + (dx & ~15) / (8 / BPL));
	if (dx & 15) {
		fill8_2col((u8 *)dest + 1, fgcolor, bgcolor, *data++);
		dest += BPL / 2;
		width -= 8;
	}

	if (width >= 16) {
		data16 = (const u16 *)data;
		expand16_2col2mask(fgcolor, bgcolor, fgm, bgm);

		for (rows = width / 16; rows; rows--) {
			u16 d = *data16++;
			m = d | ((u32)d << 16);
			*dest++ = (m & fgm[0]) ^ bgm[0];
			*dest++ = (m & fgm[1]) ^ bgm[1];
			*dest++ = (m & fgm[2]) ^ bgm[2];
			*dest++ = (m & fgm[3]) ^ bgm[3];
		}

		data = (const u8 *)data16;
		width &= 15;
	}

	if (width)
		fill8_2col((u8 *)dest, fgcolor, bgcolor, *data);
}
