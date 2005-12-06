/*
 *  Generic fillrect for frame buffers with packed pixels of any depth.
 *
 *      Copyright (C)  2000 James Simmons (jsimmons@linux-fbdev.org)
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 *
 * NOTES:
 *
 *  The code for depths like 24 that don't have integer number of pixels per
 *  long is broken and needs to be fixed. For now I turned these types of
 *  mode off.
 *
 *  Also need to add code to deal with cards endians that are different than
 *  the native cpu endians. I also need to deal with MSB position in the word.
 *
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/fb.h>
#include <asm/types.h>

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
     *  Create a pattern with the given pixel's color
     */

#if BITS_PER_LONG == 64
static inline unsigned long
pixel_to_pat( u32 bpp, u32 pixel)
{
	switch (bpp) {
	case 1:
		return 0xfffffffffffffffful*pixel;
	case 2:
		return 0x5555555555555555ul*pixel;
	case 4:
		return 0x1111111111111111ul*pixel;
	case 8:
		return 0x0101010101010101ul*pixel;
	case 12:
		return 0x0001001001001001ul*pixel;
	case 16:
		return 0x0001000100010001ul*pixel;
	case 24:
		return 0x0000000001000001ul*pixel;
	case 32:
		return 0x0000000100000001ul*pixel;
	default:
		panic("pixel_to_pat(): unsupported pixelformat\n");
    }
}
#else
static inline unsigned long
pixel_to_pat( u32 bpp, u32 pixel)
{
	switch (bpp) {
	case 1:
		return 0xfffffffful*pixel;
	case 2:
		return 0x55555555ul*pixel;
	case 4:
		return 0x11111111ul*pixel;
	case 8:
		return 0x01010101ul*pixel;
	case 12:
		return 0x00001001ul*pixel;
	case 16:
		return 0x00010001ul*pixel;
	case 24:
		return 0x00000001ul*pixel;
	case 32:
		return 0x00000001ul*pixel;
	default:
		panic("pixel_to_pat(): unsupported pixelformat\n");
    }
}
#endif

    /*
     *  Aligned pattern fill using 32/64-bit memory accesses
     */

static void
bitfill_aligned(unsigned long __iomem *dst, int dst_idx, unsigned long pat, unsigned n, int bits)
{
	unsigned long first, last;

	if (!n)
		return;

	first = ~0UL >> dst_idx;
	last = ~(~0UL >> ((dst_idx+n) % bits));

	if (dst_idx+n <= bits) {
		// Single word
		if (last)
			first &= last;
		FB_WRITEL(comp(pat, FB_READL(dst), first), dst);
	} else {
		// Multiple destination words

		// Leading bits
		if (first!= ~0UL) {
			FB_WRITEL(comp(pat, FB_READL(dst), first), dst);
			dst++;
			n -= bits - dst_idx;
		}

		// Main chunk
		n /= bits;
		while (n >= 8) {
			FB_WRITEL(pat, dst++);
			FB_WRITEL(pat, dst++);
			FB_WRITEL(pat, dst++);
			FB_WRITEL(pat, dst++);
			FB_WRITEL(pat, dst++);
			FB_WRITEL(pat, dst++);
			FB_WRITEL(pat, dst++);
			FB_WRITEL(pat, dst++);
			n -= 8;
		}
		while (n--)
			FB_WRITEL(pat, dst++);

		// Trailing bits
		if (last)
			FB_WRITEL(comp(pat, FB_READL(dst), last), dst);
	}
}


    /*
     *  Unaligned generic pattern fill using 32/64-bit memory accesses
     *  The pattern must have been expanded to a full 32/64-bit value
     *  Left/right are the appropriate shifts to convert to the pattern to be
     *  used for the next 32/64-bit word
     */

static void
bitfill_unaligned(unsigned long __iomem *dst, int dst_idx, unsigned long pat,
			int left, int right, unsigned n, int bits)
{
	unsigned long first, last;

	if (!n)
		return;

	first = ~0UL >> dst_idx;
	last = ~(~0UL >> ((dst_idx+n) % bits));

	if (dst_idx+n <= bits) {
		// Single word
		if (last)
			first &= last;
		FB_WRITEL(comp(pat, FB_READL(dst), first), dst);
	} else {
		// Multiple destination words
		// Leading bits
		if (first) {
			FB_WRITEL(comp(pat, FB_READL(dst), first), dst);
			dst++;
			pat = pat << left | pat >> right;
			n -= bits - dst_idx;
		}

		// Main chunk
		n /= bits;
		while (n >= 4) {
			FB_WRITEL(pat, dst++);
			pat = pat << left | pat >> right;
			FB_WRITEL(pat, dst++);
			pat = pat << left | pat >> right;
			FB_WRITEL(pat, dst++);
			pat = pat << left | pat >> right;
			FB_WRITEL(pat, dst++);
			pat = pat << left | pat >> right;
			n -= 4;
		}
		while (n--) {
			FB_WRITEL(pat, dst++);
			pat = pat << left | pat >> right;
		}

		// Trailing bits
		if (last)
			FB_WRITEL(comp(pat, FB_READL(dst), first), dst);
	}
}

    /*
     *  Aligned pattern invert using 32/64-bit memory accesses
     */
static void
bitfill_aligned_rev(unsigned long __iomem *dst, int dst_idx, unsigned long pat, unsigned n, int bits)
{
	unsigned long val = pat, dat;
	unsigned long first, last;

	if (!n)
		return;

	first = ~0UL >> dst_idx;
	last = ~(~0UL >> ((dst_idx+n) % bits));

	if (dst_idx+n <= bits) {
		// Single word
		if (last)
			first &= last;
		dat = FB_READL(dst);
		FB_WRITEL(comp(dat ^ val, dat, first), dst);
	} else {
		// Multiple destination words
		// Leading bits
		if (first!=0UL) {
			dat = FB_READL(dst);
			FB_WRITEL(comp(dat ^ val, dat, first), dst);
			dst++;
			n -= bits - dst_idx;
		}

		// Main chunk
		n /= bits;
		while (n >= 8) {
			FB_WRITEL(FB_READL(dst) ^ val, dst);
			dst++;
			FB_WRITEL(FB_READL(dst) ^ val, dst);
			dst++;
			FB_WRITEL(FB_READL(dst) ^ val, dst);
			dst++;
			FB_WRITEL(FB_READL(dst) ^ val, dst);
			dst++;
			FB_WRITEL(FB_READL(dst) ^ val, dst);
			dst++;
			FB_WRITEL(FB_READL(dst) ^ val, dst);
			dst++;
			FB_WRITEL(FB_READL(dst) ^ val, dst);
			dst++;
			FB_WRITEL(FB_READL(dst) ^ val, dst);
			dst++;
			n -= 8;
		}
		while (n--) {
			FB_WRITEL(FB_READL(dst) ^ val, dst);
			dst++;
		}
		// Trailing bits
		if (last) {
			dat = FB_READL(dst);
			FB_WRITEL(comp(dat ^ val, dat, last), dst);
		}
	}
}


    /*
     *  Unaligned generic pattern invert using 32/64-bit memory accesses
     *  The pattern must have been expanded to a full 32/64-bit value
     *  Left/right are the appropriate shifts to convert to the pattern to be
     *  used for the next 32/64-bit word
     */

static void
bitfill_unaligned_rev(unsigned long __iomem *dst, int dst_idx, unsigned long pat,
			int left, int right, unsigned n, int bits)
{
	unsigned long first, last, dat;

	if (!n)
		return;

	first = ~0UL >> dst_idx;
	last = ~(~0UL >> ((dst_idx+n) % bits));

	if (dst_idx+n <= bits) {
		// Single word
		if (last)
			first &= last;
		dat = FB_READL(dst);
		FB_WRITEL(comp(dat ^ pat, dat, first), dst);
	} else {
		// Multiple destination words

		// Leading bits
		if (first != 0UL) {
			dat = FB_READL(dst);
			FB_WRITEL(comp(dat ^ pat, dat, first), dst);
			dst++;
			pat = pat << left | pat >> right;
			n -= bits - dst_idx;
		}

		// Main chunk
		n /= bits;
		while (n >= 4) {
			FB_WRITEL(FB_READL(dst) ^ pat, dst);
			dst++;
			pat = pat << left | pat >> right;
			FB_WRITEL(FB_READL(dst) ^ pat, dst);
			dst++;
			pat = pat << left | pat >> right;
			FB_WRITEL(FB_READL(dst) ^ pat, dst);
			dst++;
			pat = pat << left | pat >> right;
			FB_WRITEL(FB_READL(dst) ^ pat, dst);
			dst++;
			pat = pat << left | pat >> right;
			n -= 4;
		}
		while (n--) {
			FB_WRITEL(FB_READL(dst) ^ pat, dst);
			dst++;
			pat = pat << left | pat >> right;
		}

		// Trailing bits
		if (last) {
			dat = FB_READL(dst);
			FB_WRITEL(comp(dat ^ pat, dat, last), dst);
		}
	}
}

void cfb_fillrect(struct fb_info *p, const struct fb_fillrect *rect)
{
	unsigned long pat, fg;
	unsigned long width = rect->width, height = rect->height;
	int bits = BITS_PER_LONG, bytes = bits >> 3;
	u32 bpp = p->var.bits_per_pixel;
	unsigned long __iomem *dst;
	int dst_idx, left;

	if (p->state != FBINFO_STATE_RUNNING)
		return;

	if (p->fix.visual == FB_VISUAL_TRUECOLOR ||
	    p->fix.visual == FB_VISUAL_DIRECTCOLOR )
		fg = ((u32 *) (p->pseudo_palette))[rect->color];
	else
		fg = rect->color;

	pat = pixel_to_pat( bpp, fg);

	dst = (unsigned long __iomem *)((unsigned long)p->screen_base & ~(bytes-1));
	dst_idx = ((unsigned long)p->screen_base & (bytes - 1))*8;
	dst_idx += rect->dy*p->fix.line_length*8+rect->dx*bpp;
	/* FIXME For now we support 1-32 bpp only */
	left = bits % bpp;
	if (p->fbops->fb_sync)
		p->fbops->fb_sync(p);
	if (!left) {
		void (*fill_op32)(unsigned long __iomem *dst, int dst_idx,
		                  unsigned long pat, unsigned n, int bits) = NULL;

		switch (rect->rop) {
		case ROP_XOR:
			fill_op32 = bitfill_aligned_rev;
			break;
		case ROP_COPY:
			fill_op32 = bitfill_aligned;
			break;
		default:
			printk( KERN_ERR "cfb_fillrect(): unknown rop, defaulting to ROP_COPY\n");
			fill_op32 = bitfill_aligned;
			break;
		}
		while (height--) {
			dst += dst_idx >> (ffs(bits) - 1);
			dst_idx &= (bits - 1);
			fill_op32(dst, dst_idx, pat, width*bpp, bits);
			dst_idx += p->fix.line_length*8;
		}
	} else {
		int right;
		int r;
		int rot = (left-dst_idx) % bpp;
		void (*fill_op)(unsigned long __iomem *dst, int dst_idx,
		                unsigned long pat, int left, int right,
		                unsigned n, int bits) = NULL;

		/* rotate pattern to correct start position */
		pat = pat << rot | pat >> (bpp-rot);

		right = bpp-left;
		switch (rect->rop) {
		case ROP_XOR:
			fill_op = bitfill_unaligned_rev;
			break;
		case ROP_COPY:
			fill_op = bitfill_unaligned;
			break;
		default:
			printk( KERN_ERR "cfb_fillrect(): unknown rop, defaulting to ROP_COPY\n");
			fill_op = bitfill_unaligned;
			break;
		}
		while (height--) {
			dst += dst_idx >> (ffs(bits) - 1);
			dst_idx &= (bits - 1);
			fill_op(dst, dst_idx, pat, left, right,
				width*bpp, bits);
			r = (p->fix.line_length*8) % bpp;
			pat = pat << (bpp-r) | pat >> r;
			dst_idx += p->fix.line_length*8;
		}
	}
}

EXPORT_SYMBOL(cfb_fillrect);

MODULE_AUTHOR("James Simmons <jsimmons@users.sf.net>");
MODULE_DESCRIPTION("Generic software accelerated fill rectangle");
MODULE_LICENSE("GPL");
