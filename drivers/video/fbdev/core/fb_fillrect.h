/* SPDX-License-Identifier: GPL-2.0-only
 *
 *  Generic bit area filler and twister engine for packed pixel framebuffers
 *
 *	Rewritten by:
 *	Copyright (C)  2025 Zsolt Kajtar (soci@c64.rulez.org)
 *
 *	Based on earlier work of:
 *	Copyright (C)  2000 James Simmons (jsimmons@linux-fbdev.org)
 *	Michal Januszewski <spock@gentoo.org>
 *	Anton Vorontsov <avorontsov@ru.mvista.com>
 *	Pavel Pisa <pisa@cmp.felk.cvut.cz>
 *	Antonino A. Daplas <adaplas@gmail.com>
 *	Geert Uytterhoeven
 *	and others
 *
 * NOTES:
 *
 * Handles native and foreign byte order on both endians, standard and
 * reverse pixel order in a byte (<8 BPP), word length of 32/64 bits,
 * bits per pixel from 1 to the word length. Handles line lengths at byte
 * granularity while maintaining aligned accesses.
 *
 * Optimized path for power of two bits per pixel modes.
 */
#include "fb_draw.h"

/* inverts bits at a given offset */
static inline void fb_invert_offset(unsigned long pat, int offset, const struct fb_address *dst)
{
	fb_write_offset(fb_read_offset(offset, dst) ^ pat, offset, dst);
}

/* state for pattern generator and whether swapping is necessary */
struct fb_pattern {
	unsigned long pixels;
	int left, right;
	struct fb_reverse reverse;
};

/* used to get the pattern in native order */
static unsigned long fb_pattern_get(struct fb_pattern *pattern)
{
	return pattern->pixels;
}

/* used to get the pattern in reverse order */
static unsigned long fb_pattern_get_reverse(struct fb_pattern *pattern)
{
	return swab_long(pattern->pixels);
}

/* next static pattern */
static void fb_pattern_static(struct fb_pattern *pattern)
{
	/* nothing to do */
}

/* next rotating pattern */
static void fb_pattern_rotate(struct fb_pattern *pattern)
{
	pattern->pixels = fb_left(pattern->pixels, pattern->left)
		| fb_right(pattern->pixels, pattern->right);
}

#define FB_PAT(i) (((1UL<<(BITS_PER_LONG-1)/(i)*(i))/((1<<(i))-1)<<(i))|1)

/* create the filling pattern from a given color */
static unsigned long pixel_to_pat(int bpp, u32 color)
{
	static const unsigned long mulconst[BITS_PER_LONG/4] = {
		0, ~0UL, FB_PAT(2), FB_PAT(3),
		FB_PAT(4), FB_PAT(5), FB_PAT(6), FB_PAT(7),
#if BITS_PER_LONG == 64
		FB_PAT(8), FB_PAT(9), FB_PAT(10), FB_PAT(11),
		FB_PAT(12), FB_PAT(13), FB_PAT(14), FB_PAT(15),
#endif
	};
	unsigned long pattern;

	switch (bpp) {
	case 0 ... BITS_PER_LONG/4-1:
		pattern = mulconst[bpp] * color;
		break;
	case BITS_PER_LONG/4 ... BITS_PER_LONG/2-1:
		pattern = color;
		pattern = pattern | pattern << bpp;
		pattern = pattern | pattern << bpp*2;
		break;
	case BITS_PER_LONG/2 ... BITS_PER_LONG-1:
		pattern = color;
		pattern = pattern | pattern << bpp;
		break;
	default:
		pattern = color;
		break;
	}
#ifndef __LITTLE_ENDIAN
	pattern <<= (BITS_PER_LONG % bpp);
	pattern |= pattern >> bpp;
#endif
	return pattern;
}

/* overwrite bits according to a pattern in a line */
static __always_inline void bitfill(const struct fb_address *dst,
				    struct fb_pattern *pattern,
				    unsigned long (*get)(struct fb_pattern *pattern),
				    void (*rotate)(struct fb_pattern *pattern),
				    int end)
{
	unsigned long first, last;

	end += dst->bits;
	first = fb_pixel_mask(dst->bits, pattern->reverse);
	last = ~fb_pixel_mask(end & (BITS_PER_LONG-1), pattern->reverse);

	if (end <= BITS_PER_LONG) {
		last = last ? (last & first) : first;
		first = get(pattern);
		if (last == ~0UL)
			fb_write_offset(first, 0, dst);
		else if (last)
			fb_modify_offset(first, last, 0, dst);
	} else {
		int offset = first != ~0UL;

		if (offset) {
			fb_modify_offset(get(pattern), first, 0, dst);
			rotate(pattern);
		}
		end /= BITS_PER_LONG;
		for (; offset + 4 <= end; offset += 4) {
			fb_write_offset(get(pattern), offset + 0, dst);
			rotate(pattern);
			fb_write_offset(get(pattern), offset + 1, dst);
			rotate(pattern);
			fb_write_offset(get(pattern), offset + 2, dst);
			rotate(pattern);
			fb_write_offset(get(pattern), offset + 3, dst);
			rotate(pattern);
		}
		while (offset < end) {
			fb_write_offset(get(pattern), offset++, dst);
			rotate(pattern);
		}

		if (last)
			fb_modify_offset(get(pattern), last, offset, dst);
	}
}

/* inverts bits according to a pattern in a line */
static __always_inline void bitinvert(const struct fb_address *dst,
				      struct fb_pattern *pattern,
				      unsigned long (*get)(struct fb_pattern *pattern),
				      void (*rotate)(struct fb_pattern *pattern),
				      int end)
{
	unsigned long first, last;
	int offset;

	end += dst->bits;
	first = fb_pixel_mask(dst->bits, pattern->reverse);
	last = ~fb_pixel_mask(end & (BITS_PER_LONG-1), pattern->reverse);

	if (end <= BITS_PER_LONG) {
		offset = 0;
		last = last ? (last & first) : first;
	} else {
		offset = first != ~0UL;

		if (offset) {
			first &= get(pattern);
			if (first)
				fb_invert_offset(first, 0, dst);
			rotate(pattern);
		}

		end /= BITS_PER_LONG;
		for (; offset + 4 <= end; offset += 4) {
			fb_invert_offset(get(pattern), offset + 0, dst);
			rotate(pattern);
			fb_invert_offset(get(pattern), offset + 1, dst);
			rotate(pattern);
			fb_invert_offset(get(pattern), offset + 2, dst);
			rotate(pattern);
			fb_invert_offset(get(pattern), offset + 3, dst);
			rotate(pattern);
		}
		while (offset < end) {
			fb_invert_offset(get(pattern), offset++, dst);
			rotate(pattern);
		}
	}

	last &= get(pattern);
	if (last)
		fb_invert_offset(last, offset, dst);
}

/* pattern doesn't change. 1, 2, 4, 8, 16, 32, 64 bpp */
static inline void fb_fillrect_static(const struct fb_fillrect *rect, int bpp,
				      struct fb_address *dst, struct fb_pattern *pattern,
				      unsigned int bits_per_line)
{
	u32 height = rect->height;
	int width = rect->width * bpp;

	if (bpp > 8 && pattern->reverse.byte)
		pattern->pixels = swab_long(pattern->pixels);

	if (rect->rop == ROP_XOR)
		while (height--) {
			bitinvert(dst, pattern, fb_pattern_get, fb_pattern_static, width);
			fb_address_forward(dst, bits_per_line);
		}
	else
		while (height--) {
			bitfill(dst, pattern, fb_pattern_get, fb_pattern_static, width);
			fb_address_forward(dst, bits_per_line);
		}
}

/* rotate pattern to the correct position */
static inline unsigned long fb_rotate(unsigned long pattern, int shift, int bpp)
{
	shift %= bpp;
	return fb_right(pattern, shift) | fb_left(pattern, bpp - shift);
}

/* rotating pattern, for example 24 bpp */
static __always_inline void fb_fillrect_rotating(const struct fb_fillrect *rect,
						 int bpp, struct fb_address *dst,
						 struct fb_pattern *pattern,
						 unsigned long (*get)(struct fb_pattern *pattern),
						 unsigned int bits_per_line)
{
	unsigned long pat = pattern->pixels;
	u32 height = rect->height;
	int width = rect->width * bpp;

	if (rect->rop == ROP_XOR)
		while (height--) {
			pattern->pixels = fb_rotate(pat, dst->bits, bpp);
			bitinvert(dst, pattern, get, fb_pattern_rotate, width);
			fb_address_forward(dst, bits_per_line);
		}
	else
		while (height--) {
			pattern->pixels = fb_rotate(pat, dst->bits, bpp);
			bitfill(dst, pattern, get, fb_pattern_rotate, width);
			fb_address_forward(dst, bits_per_line);
		}
}

static inline void fb_fillrect(struct fb_info *p, const struct fb_fillrect *rect)
{
	int bpp = p->var.bits_per_pixel;
	unsigned int bits_per_line = BYTES_TO_BITS(p->fix.line_length);
	const u32 *palette = fb_palette(p);
	struct fb_address dst = fb_address_init(p);
	struct fb_pattern pattern;

	fb_address_forward(&dst, rect->dy * bits_per_line + rect->dx * bpp);

	pattern.pixels = pixel_to_pat(bpp, palette ? palette[rect->color] : rect->color);
	pattern.reverse = fb_reverse_init(p);
	pattern.left = BITS_PER_LONG % bpp;
	if (pattern.left) {
		pattern.right = bpp - pattern.left;
		if (pattern.reverse.byte)
			fb_fillrect_rotating(rect, bpp, &dst, &pattern,
					     fb_pattern_get_reverse, bits_per_line);
		else
			fb_fillrect_rotating(rect, bpp, &dst, &pattern,
					     fb_pattern_get, bits_per_line);
	} else
		fb_fillrect_static(rect, bpp, &dst, &pattern, bits_per_line);
}
