/*
 *  Generic fillrect for frame buffers in system RAM with packed pixels of
 *  any depth.
 *
 *  Based almost entirely from cfbfillrect.c (which is based almost entirely
 *  on Geert Uytterhoeven's fillrect routine)
 *
 *      Copyright (C)  2007 Antonino Daplas <adaplas@pol.net>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */
#include <linux/module.h>
#include <linux/string.h>
#include <linux/fb.h>
#include <asm/types.h>
#include "fb_draw.h"

    /*
     *  Aligned pattern fill using 32/64-bit memory accesses
     */

static void
bitfill_aligned(struct fb_info *p, unsigned long *dst, int dst_idx,
		unsigned long pat, unsigned n, int bits)
{
	unsigned long first, last;

	if (!n)
		return;

	first = FB_SHIFT_HIGH(p, ~0UL, dst_idx);
	last = ~(FB_SHIFT_HIGH(p, ~0UL, (dst_idx+n) % bits));

	if (dst_idx+n <= bits) {
		/* Single word */
		if (last)
			first &= last;
		*dst = comp(pat, *dst, first);
	} else {
		/* Multiple destination words */

		/* Leading bits */
 		if (first!= ~0UL) {
			*dst = comp(pat, *dst, first);
			dst++;
			n -= bits - dst_idx;
		}

		/* Main chunk */
		n /= bits;
		memset_l(dst, pat, n);
		dst += n;

		/* Trailing bits */
		if (last)
			*dst = comp(pat, *dst, last);
	}
}


    /*
     *  Unaligned generic pattern fill using 32/64-bit memory accesses
     *  The pattern must have been expanded to a full 32/64-bit value
     *  Left/right are the appropriate shifts to convert to the pattern to be
     *  used for the next 32/64-bit word
     */

static void
bitfill_unaligned(struct fb_info *p, unsigned long *dst, int dst_idx,
		  unsigned long pat, int left, int right, unsigned n, int bits)
{
	unsigned long first, last;

	if (!n)
		return;

	first = FB_SHIFT_HIGH(p, ~0UL, dst_idx);
	last = ~(FB_SHIFT_HIGH(p, ~0UL, (dst_idx+n) % bits));

	if (dst_idx+n <= bits) {
		/* Single word */
		if (last)
			first &= last;
		*dst = comp(pat, *dst, first);
	} else {
		/* Multiple destination words */
		/* Leading bits */
		if (first) {
			*dst = comp(pat, *dst, first);
			dst++;
			pat = pat << left | pat >> right;
			n -= bits - dst_idx;
		}

		/* Main chunk */
		n /= bits;
		while (n >= 4) {
			*dst++ = pat;
			pat = pat << left | pat >> right;
			*dst++ = pat;
			pat = pat << left | pat >> right;
			*dst++ = pat;
			pat = pat << left | pat >> right;
			*dst++ = pat;
			pat = pat << left | pat >> right;
			n -= 4;
		}
		while (n--) {
			*dst++ = pat;
			pat = pat << left | pat >> right;
		}

		/* Trailing bits */
		if (last)
			*dst = comp(pat, *dst, last);
	}
}

    /*
     *  Aligned pattern invert using 32/64-bit memory accesses
     */
static void
bitfill_aligned_rev(struct fb_info *p, unsigned long *dst, int dst_idx,
		    unsigned long pat, unsigned n, int bits)
{
	unsigned long val = pat;
	unsigned long first, last;

	if (!n)
		return;

	first = FB_SHIFT_HIGH(p, ~0UL, dst_idx);
	last = ~(FB_SHIFT_HIGH(p, ~0UL, (dst_idx+n) % bits));

	if (dst_idx+n <= bits) {
		/* Single word */
		if (last)
			first &= last;
		*dst = comp(*dst ^ val, *dst, first);
	} else {
		/* Multiple destination words */
		/* Leading bits */
		if (first!=0UL) {
			*dst = comp(*dst ^ val, *dst, first);
			dst++;
			n -= bits - dst_idx;
		}

		/* Main chunk */
		n /= bits;
		while (n >= 8) {
			*dst++ ^= val;
			*dst++ ^= val;
			*dst++ ^= val;
			*dst++ ^= val;
			*dst++ ^= val;
			*dst++ ^= val;
			*dst++ ^= val;
			*dst++ ^= val;
			n -= 8;
		}
		while (n--)
			*dst++ ^= val;
		/* Trailing bits */
		if (last)
			*dst = comp(*dst ^ val, *dst, last);
	}
}


    /*
     *  Unaligned generic pattern invert using 32/64-bit memory accesses
     *  The pattern must have been expanded to a full 32/64-bit value
     *  Left/right are the appropriate shifts to convert to the pattern to be
     *  used for the next 32/64-bit word
     */

static void
bitfill_unaligned_rev(struct fb_info *p, unsigned long *dst, int dst_idx,
		      unsigned long pat, int left, int right, unsigned n,
		      int bits)
{
	unsigned long first, last;

	if (!n)
		return;

	first = FB_SHIFT_HIGH(p, ~0UL, dst_idx);
	last = ~(FB_SHIFT_HIGH(p, ~0UL, (dst_idx+n) % bits));

	if (dst_idx+n <= bits) {
		/* Single word */
		if (last)
			first &= last;
		*dst = comp(*dst ^ pat, *dst, first);
	} else {
		/* Multiple destination words */

		/* Leading bits */
		if (first != 0UL) {
			*dst = comp(*dst ^ pat, *dst, first);
			dst++;
			pat = pat << left | pat >> right;
			n -= bits - dst_idx;
		}

		/* Main chunk */
		n /= bits;
		while (n >= 4) {
			*dst++ ^= pat;
			pat = pat << left | pat >> right;
			*dst++ ^= pat;
			pat = pat << left | pat >> right;
			*dst++ ^= pat;
			pat = pat << left | pat >> right;
			*dst++ ^= pat;
			pat = pat << left | pat >> right;
			n -= 4;
		}
		while (n--) {
			*dst ^= pat;
			pat = pat << left | pat >> right;
		}

		/* Trailing bits */
		if (last)
			*dst = comp(*dst ^ pat, *dst, last);
	}
}

void sys_fillrect(struct fb_info *p, const struct fb_fillrect *rect)
{
	unsigned long pat, pat2, fg;
	unsigned long width = rect->width, height = rect->height;
	int bits = BITS_PER_LONG, bytes = bits >> 3;
	u32 bpp = p->var.bits_per_pixel;
	unsigned long *dst;
	int dst_idx, left;

	if (p->state != FBINFO_STATE_RUNNING)
		return;

	if (p->fix.visual == FB_VISUAL_TRUECOLOR ||
	    p->fix.visual == FB_VISUAL_DIRECTCOLOR )
		fg = ((u32 *) (p->pseudo_palette))[rect->color];
	else
		fg = rect->color;

	pat = pixel_to_pat( bpp, fg);

	dst = (unsigned long *)((unsigned long)p->screen_base & ~(bytes-1));
	dst_idx = ((unsigned long)p->screen_base & (bytes - 1))*8;
	dst_idx += rect->dy*p->fix.line_length*8+rect->dx*bpp;
	/* FIXME For now we support 1-32 bpp only */
	left = bits % bpp;
	if (p->fbops->fb_sync)
		p->fbops->fb_sync(p);
	if (!left) {
		void (*fill_op32)(struct fb_info *p, unsigned long *dst,
				  int dst_idx, unsigned long pat, unsigned n,
				  int bits) = NULL;

		switch (rect->rop) {
		case ROP_XOR:
			fill_op32 = bitfill_aligned_rev;
			break;
		case ROP_COPY:
			fill_op32 = bitfill_aligned;
			break;
		default:
			printk( KERN_ERR "cfb_fillrect(): unknown rop, "
				"defaulting to ROP_COPY\n");
			fill_op32 = bitfill_aligned;
			break;
		}
		while (height--) {
			dst += dst_idx >> (ffs(bits) - 1);
			dst_idx &= (bits - 1);
			fill_op32(p, dst, dst_idx, pat, width*bpp, bits);
			dst_idx += p->fix.line_length*8;
		}
	} else {
		int right, r;
		void (*fill_op)(struct fb_info *p, unsigned long *dst,
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
			printk(KERN_ERR "sys_fillrect(): unknown rop, "
				"defaulting to ROP_COPY\n");
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

EXPORT_SYMBOL(sys_fillrect);

MODULE_AUTHOR("Antonino Daplas <adaplas@pol.net>");
MODULE_DESCRIPTION("Generic fill rectangle (sys-to-sys)");
MODULE_LICENSE("GPL");
