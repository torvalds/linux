/*
 *  Generic function for frame buffer with packed pixels of any depth.
 *
 *      Copyright (C)  1999-2005 James Simmons <jsimmons@www.infradead.org>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 *
 * NOTES:
 *
 *  This is for cfb packed pixels. Iplan and such are incorporated in the
 *  drivers that need them.
 *
 *  FIXME
 *
 *  Also need to add code to deal with cards endians that are different than
 *  the native cpu endians. I also need to deal with MSB position in the word.
 *
 *  The two functions or copying forward and backward could be split up like
 *  the ones for filling, i.e. in aligned and unaligned versions. This would
 *  help moving some redundant computations and branches out of the loop, too.
 */



#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fb.h>
#include <linux/slab.h>
#include <asm/types.h>
#include <asm/io.h>

#if BITS_PER_LONG == 32
#  define FB_WRITEL fb_writel
#  define FB_READL  fb_readl
#else
#  define FB_WRITEL fb_writeq
#  define FB_READL  fb_readq
#endif

    /*
     *  Compose two values, using a bitmask as decision value
     *  This is equivalent to (a & mask) | (b & ~mask)
     */

static inline unsigned long
comp(unsigned long a, unsigned long b, unsigned long mask)
{
    return ((a ^ b) & mask) ^ b;
}

    /*
     *  Generic bitwise copy algorithm
     */

static void
bitcpy(unsigned long __iomem *dst, int dst_idx, const unsigned long __iomem *src,
	int src_idx, int bits, unsigned n)
{
	unsigned long first, last;
	int const shift = dst_idx-src_idx;
	int left, right;

	first = ~0UL >> dst_idx;
	last = ~(~0UL >> ((dst_idx+n) % bits));

	if (!shift) {
		// Same alignment for source and dest

		if (dst_idx+n <= bits) {
			// Single word
			if (last)
				first &= last;
			FB_WRITEL( comp( FB_READL(src), FB_READL(dst), first), dst);
		} else {
			// Multiple destination words

			// Leading bits
			if (first != ~0UL) {
				FB_WRITEL( comp( FB_READL(src), FB_READL(dst), first), dst);
				dst++;
				src++;
				n -= bits - dst_idx;
			}

			// Main chunk
			n /= bits;
			while (n >= 8) {
				FB_WRITEL(FB_READL(src++), dst++);
				FB_WRITEL(FB_READL(src++), dst++);
				FB_WRITEL(FB_READL(src++), dst++);
				FB_WRITEL(FB_READL(src++), dst++);
				FB_WRITEL(FB_READL(src++), dst++);
				FB_WRITEL(FB_READL(src++), dst++);
				FB_WRITEL(FB_READL(src++), dst++);
				FB_WRITEL(FB_READL(src++), dst++);
				n -= 8;
			}
			while (n--)
				FB_WRITEL(FB_READL(src++), dst++);

			// Trailing bits
			if (last)
				FB_WRITEL( comp( FB_READL(src), FB_READL(dst), last), dst);
		}
	} else {
		unsigned long d0, d1;
		int m;
		// Different alignment for source and dest

		right = shift & (bits - 1);
		left = -shift & (bits - 1);

		if (dst_idx+n <= bits) {
			// Single destination word
			if (last)
				first &= last;
			if (shift > 0) {
				// Single source word
				FB_WRITEL( comp( FB_READL(src) >> right, FB_READL(dst), first), dst);
			} else if (src_idx+n <= bits) {
				// Single source word
				FB_WRITEL( comp(FB_READL(src) << left, FB_READL(dst), first), dst);
			} else {
				// 2 source words
				d0 = FB_READL(src++);
				d1 = FB_READL(src);
				FB_WRITEL( comp(d0<<left | d1>>right, FB_READL(dst), first), dst);
			}
		} else {
			// Multiple destination words
			/** We must always remember the last value read, because in case
			SRC and DST overlap bitwise (e.g. when moving just one pixel in
			1bpp), we always collect one full long for DST and that might
			overlap with the current long from SRC. We store this value in
			'd0'. */
			d0 = FB_READL(src++);
			// Leading bits
			if (shift > 0) {
				// Single source word
				FB_WRITEL( comp(d0 >> right, FB_READL(dst), first), dst);
				dst++;
				n -= bits - dst_idx;
			} else {
				// 2 source words
				d1 = FB_READL(src++);
				FB_WRITEL( comp(d0<<left | d1>>right, FB_READL(dst), first), dst);
				d0 = d1;
				dst++;
				n -= bits - dst_idx;
			}

			// Main chunk
			m = n % bits;
			n /= bits;
			while (n >= 4) {
				d1 = FB_READL(src++);
				FB_WRITEL(d0 << left | d1 >> right, dst++);
				d0 = d1;
				d1 = FB_READL(src++);
				FB_WRITEL(d0 << left | d1 >> right, dst++);
				d0 = d1;
				d1 = FB_READL(src++);
				FB_WRITEL(d0 << left | d1 >> right, dst++);
				d0 = d1;
				d1 = FB_READL(src++);
				FB_WRITEL(d0 << left | d1 >> right, dst++);
				d0 = d1;
				n -= 4;
			}
			while (n--) {
				d1 = FB_READL(src++);
				FB_WRITEL(d0 << left | d1 >> right, dst++);
				d0 = d1;
			}

			// Trailing bits
			if (last) {
				if (m <= right) {
					// Single source word
					FB_WRITEL( comp(d0 << left, FB_READL(dst), last), dst);
				} else {
					// 2 source words
					d1 = FB_READL(src);
					FB_WRITEL( comp(d0<<left | d1>>right, FB_READL(dst), last), dst);
				}
			}
		}
	}
}

    /*
     *  Generic bitwise copy algorithm, operating backward
     */

static void
bitcpy_rev(unsigned long __iomem *dst, int dst_idx, const unsigned long __iomem *src,
		int src_idx, int bits, unsigned n)
{
	unsigned long first, last;
	int shift;

	dst += (n-1)/bits;
	src += (n-1)/bits;
	if ((n-1) % bits) {
		dst_idx += (n-1) % bits;
		dst += dst_idx >> (ffs(bits) - 1);
		dst_idx &= bits - 1;
		src_idx += (n-1) % bits;
		src += src_idx >> (ffs(bits) - 1);
		src_idx &= bits - 1;
	}

	shift = dst_idx-src_idx;

	first = ~0UL << (bits - 1 - dst_idx);
	last = ~(~0UL << (bits - 1 - ((dst_idx-n) % bits)));

	if (!shift) {
		// Same alignment for source and dest

		if ((unsigned long)dst_idx+1 >= n) {
			// Single word
			if (last)
				first &= last;
			FB_WRITEL( comp( FB_READL(src), FB_READL(dst), first), dst);
		} else {
			// Multiple destination words

			// Leading bits
			if (first != ~0UL) {
				FB_WRITEL( comp( FB_READL(src), FB_READL(dst), first), dst);
				dst--;
				src--;
				n -= dst_idx+1;
			}

			// Main chunk
			n /= bits;
			while (n >= 8) {
				FB_WRITEL(FB_READL(src--), dst--);
				FB_WRITEL(FB_READL(src--), dst--);
				FB_WRITEL(FB_READL(src--), dst--);
				FB_WRITEL(FB_READL(src--), dst--);
				FB_WRITEL(FB_READL(src--), dst--);
				FB_WRITEL(FB_READL(src--), dst--);
				FB_WRITEL(FB_READL(src--), dst--);
				FB_WRITEL(FB_READL(src--), dst--);
				n -= 8;
			}
			while (n--)
				FB_WRITEL(FB_READL(src--), dst--);

			// Trailing bits
			if (last)
				FB_WRITEL( comp( FB_READL(src), FB_READL(dst), last), dst);
		}
	} else {
		// Different alignment for source and dest

		int const left = -shift & (bits-1);
		int const right = shift & (bits-1);

		if ((unsigned long)dst_idx+1 >= n) {
			// Single destination word
			if (last)
				first &= last;
			if (shift < 0) {
				// Single source word
				FB_WRITEL( comp( FB_READL(src)<<left, FB_READL(dst), first), dst);
			} else if (1+(unsigned long)src_idx >= n) {
				// Single source word
				FB_WRITEL( comp( FB_READL(src)>>right, FB_READL(dst), first), dst);
			} else {
				// 2 source words
				FB_WRITEL( comp( (FB_READL(src)>>right | FB_READL(src-1)<<left), FB_READL(dst), first), dst);
			}
		} else {
			// Multiple destination words
			/** We must always remember the last value read, because in case
			SRC and DST overlap bitwise (e.g. when moving just one pixel in
			1bpp), we always collect one full long for DST and that might
			overlap with the current long from SRC. We store this value in
			'd0'. */
			unsigned long d0, d1;
			int m;

			d0 = FB_READL(src--);
			// Leading bits
			if (shift < 0) {
				// Single source word
				FB_WRITEL( comp( (d0 << left), FB_READL(dst), first), dst);
			} else {
				// 2 source words
				d1 = FB_READL(src--);
				FB_WRITEL( comp( (d0>>right | d1<<left), FB_READL(dst), first), dst);
				d0 = d1;
			}
			dst--;
			n -= dst_idx+1;

			// Main chunk
			m = n % bits;
			n /= bits;
			while (n >= 4) {
				d1 = FB_READL(src--);
				FB_WRITEL(d0 >> right | d1 << left, dst--);
				d0 = d1;
				d1 = FB_READL(src--);
				FB_WRITEL(d0 >> right | d1 << left, dst--);
				d0 = d1;
				d1 = FB_READL(src--);
				FB_WRITEL(d0 >> right | d1 << left, dst--);
				d0 = d1;
				d1 = FB_READL(src--);
				FB_WRITEL(d0 >> right | d1 << left, dst--);
				d0 = d1;
				n -= 4;
			}
			while (n--) {
				d1 = FB_READL(src--);
				FB_WRITEL(d0 >> right | d1 << left, dst--);
				d0 = d1;
			}

			// Trailing bits
			if (last) {
				if (m <= left) {
					// Single source word
					FB_WRITEL( comp(d0 >> right, FB_READL(dst), last), dst);
				} else {
					// 2 source words
					d1 = FB_READL(src);
					FB_WRITEL( comp(d0>>right | d1<<left, FB_READL(dst), last), dst);
				}
			}
		}
	}
}

void cfb_copyarea(struct fb_info *p, const struct fb_copyarea *area)
{
	u32 dx = area->dx, dy = area->dy, sx = area->sx, sy = area->sy;
	u32 height = area->height, width = area->width;
	unsigned long const bits_per_line = p->fix.line_length*8u;
	unsigned long __iomem *dst = NULL, *src = NULL;
	int bits = BITS_PER_LONG, bytes = bits >> 3;
	int dst_idx = 0, src_idx = 0, rev_copy = 0;

	if (p->state != FBINFO_STATE_RUNNING)
		return;

	/* if the beginning of the target area might overlap with the end of
	the source area, be have to copy the area reverse. */
	if ((dy == sy && dx > sx) || (dy > sy)) {
		dy += height;
		sy += height;
		rev_copy = 1;
	}

	// split the base of the framebuffer into a long-aligned address and the
	// index of the first bit
	dst = src = (unsigned long __iomem *)((unsigned long)p->screen_base & ~(bytes-1));
	dst_idx = src_idx = 8*((unsigned long)p->screen_base & (bytes-1));
	// add offset of source and target area
	dst_idx += dy*bits_per_line + dx*p->var.bits_per_pixel;
	src_idx += sy*bits_per_line + sx*p->var.bits_per_pixel;

	if (p->fbops->fb_sync)
		p->fbops->fb_sync(p);

	if (rev_copy) {
		while (height--) {
			dst_idx -= bits_per_line;
			src_idx -= bits_per_line;
			dst += dst_idx >> (ffs(bits) - 1);
			dst_idx &= (bytes - 1);
			src += src_idx >> (ffs(bits) - 1);
			src_idx &= (bytes - 1);
			bitcpy_rev(dst, dst_idx, src, src_idx, bits,
				width*p->var.bits_per_pixel);
		}
	} else {
		while (height--) {
			dst += dst_idx >> (ffs(bits) - 1);
			dst_idx &= (bytes - 1);
			src += src_idx >> (ffs(bits) - 1);
			src_idx &= (bytes - 1);
			bitcpy(dst, dst_idx, src, src_idx, bits,
				width*p->var.bits_per_pixel);
			dst_idx += bits_per_line;
			src_idx += bits_per_line;
		}
	}
}

EXPORT_SYMBOL(cfb_copyarea);

MODULE_AUTHOR("James Simmons <jsimmons@users.sf.net>");
MODULE_DESCRIPTION("Generic software accelerated copyarea");
MODULE_LICENSE("GPL");

