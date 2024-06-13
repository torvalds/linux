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
 *  Also need to add code to deal with cards endians that are different than
 *  the native cpu endians. I also need to deal with MSB position in the word.
 *
 */
#include <linux/module.h>
#include <linux/string.h>
#include <linux/fb.h>
#include <asm/types.h>
#include "fb_draw.h"

#if BITS_PER_LONG == 32
#  define FB_WRITEL fb_writel
#  define FB_READL  fb_readl
#else
#  define FB_WRITEL fb_writeq
#  define FB_READL  fb_readq
#endif

    /*
     *  Aligned pattern fill using 32/64-bit memory accesses
     */

static void
bitfill_aligned(struct fb_info *p, unsigned long __iomem *dst, int dst_idx,
		unsigned long pat, unsigned n, int bits, u32 bswapmask)
{
	unsigned long first, last;

	if (!n)
		return;

	first = fb_shifted_pixels_mask_long(p, dst_idx, bswapmask);
	last = ~fb_shifted_pixels_mask_long(p, (dst_idx+n) % bits, bswapmask);

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
bitfill_unaligned(struct fb_info *p, unsigned long __iomem *dst, int dst_idx,
		  unsigned long pat, int left, int right, unsigned n, int bits)
{
	unsigned long first, last;

	if (!n)
		return;

	first = FB_SHIFT_HIGH(p, ~0UL, dst_idx);
	last = ~(FB_SHIFT_HIGH(p, ~0UL, (dst_idx+n) % bits));

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
			FB_WRITEL(comp(pat, FB_READL(dst), last), dst);
	}
}

    /*
     *  Aligned pattern invert using 32/64-bit memory accesses
     */
static void
bitfill_aligned_rev(struct fb_info *p, unsigned long __iomem *dst,
		    int dst_idx, unsigned long pat, unsigned n, int bits,
		    u32 bswapmask)
{
	unsigned long val = pat, dat;
	unsigned long first, last;

	if (!n)
		return;

	first = fb_shifted_pixels_mask_long(p, dst_idx, bswapmask);
	last = ~fb_shifted_pixels_mask_long(p, (dst_idx+n) % bits, bswapmask);

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
bitfill_unaligned_rev(struct fb_info *p, unsigned long __iomem *dst,
		      int dst_idx, unsigned long pat, int left, int right,
		      unsigned n, int bits)
{
	unsigned long first, last, dat;

	if (!n)
		return;

	first = FB_SHIFT_HIGH(p, ~0UL, dst_idx);
	last = ~(FB_SHIFT_HIGH(p, ~0UL, (dst_idx+n) % bits));

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
	unsigned long pat, pat2, fg;
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

	pat = pixel_to_pat(bpp, fg);

	dst = (unsigned long __iomem *)((unsigned long)p->screen_base & ~(bytes-1));
	dst_idx = ((unsigned long)p->screen_base & (bytes - 1))*8;
	dst_idx += rect->dy*p->fix.line_length*8+rect->dx*bpp;
	/* FIXME For now we support 1-32 bpp only */
	left = bits % bpp;
	if (p->fbops->fb_sync)
		p->fbops->fb_sync(p);
	if (!left) {
		u32 bswapmask = fb_compute_bswapmask(p);
		void (*fill_op32)(struct fb_info *p,
				  unsigned long __iomem *dst, int dst_idx,
		                  unsigned long pat, unsigned n, int bits,
				  u32 bswapmask) = NULL;

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
			fill_op32(p, dst, dst_idx, pat, width*bpp, bits,
				  bswapmask);
			dst_idx += p->fix.line_length*8;
		}
	} else {
		int right, r;
		void (*fill_op)(struct fb_info *p, unsigned long __iomem *dst,
				int dst_idx, unsigned long pat, int left,
				int right, unsigned n, int bits) = NULL;
#ifdef __LITTLE_ENDIAN
		right = left;
		left = bpp - right;
#else
		right = bpp - left;
#endif
		switch (rect->rop) {
		case ROP_XOR:
			fill_op = bitfill_unaligned_rev;
			break;
		case ROP_COPY:
			fill_op = bitfill_unaligned;
			break;
		default:
			printk(KERN_ERR "cfb_fillrect(): unknown rop, defaulting to ROP_COPY\n");
			fill_op = bitfill_unaligned;
			break;
		}
		while (height--) {
			dst += dst_idx / bits;
			dst_idx &= (bits - 1);
			r = dst_idx % bpp;
			/* rotate pattern to the correct start position */
			pat2 = le_long_to_cpu(rolx(cpu_to_le_long(pat), r, bpp));
			fill_op(p, dst, dst_idx, pat2, left, right,
				width*bpp, bits);
			dst_idx += p->fix.line_length*8;
		}
	}
}

EXPORT_SYMBOL(cfb_fillrect);

MODULE_AUTHOR("James Simmons <jsimmons@users.sf.net>");
MODULE_DESCRIPTION("Generic software accelerated fill rectangle");
MODULE_LICENSE("GPL");
