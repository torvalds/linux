/* SPDX-License-Identifier: GPL-2.0-only
 *
 *  Generic bit area copy and twister engine for packed pixel framebuffers
 *
 *      Rewritten by:
 *	Copyright (C)  2025 Zsolt Kajtar (soci@c64.rulez.org)
 *
 *	Based on previous work of:
 *	Copyright (C)  1999-2005 James Simmons <jsimmons@www.infradead.org>
 *	Anton Vorontsov <avorontsov@ru.mvista.com>
 *	Pavel Pisa <pisa@cmp.felk.cvut.cz>
 *	Antonino Daplas <adaplas@hotpop.com>
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
 * Optimized routines for word aligned copying and byte aligned copying
 * on reverse pixel framebuffers.
 */
#include "fb_draw.h"

/* used when no reversing is necessary */
static inline unsigned long fb_no_reverse(unsigned long val, struct fb_reverse reverse)
{
	return val;
}

/* modifies the masked area in a word */
static inline void fb_copy_offset_masked(unsigned long mask, int offset,
					 const struct fb_address *dst,
					 const struct fb_address *src)
{
	fb_modify_offset(fb_read_offset(offset, src), mask, offset, dst);
}

/* copies the whole word */
static inline void fb_copy_offset(int offset, const struct fb_address *dst,
				  const struct fb_address *src)
{
	fb_write_offset(fb_read_offset(offset, src), offset, dst);
}

/* forward aligned copy */
static inline void fb_copy_aligned_fwd(const struct fb_address *dst,
				       const struct fb_address *src,
				       int end, struct fb_reverse reverse)
{
	unsigned long first, last;

	first = fb_pixel_mask(dst->bits, reverse);
	last = ~fb_pixel_mask(end & (BITS_PER_LONG-1), reverse);

	/* Same alignment for source and dest */
	if (end <= BITS_PER_LONG) {
		/* Single word */
		last = last ? (last & first) : first;

		/* Trailing bits */
		if (last == ~0UL)
			fb_copy_offset(0, dst, src);
		else
			fb_copy_offset_masked(last, 0, dst, src);
	} else {
		/* Multiple destination words */
		int offset = first != ~0UL;

		/* Leading bits */
		if (offset)
			fb_copy_offset_masked(first, 0, dst, src);

		/* Main chunk */
		end /= BITS_PER_LONG;
		while (offset + 4 <= end) {
			fb_copy_offset(offset + 0, dst, src);
			fb_copy_offset(offset + 1, dst, src);
			fb_copy_offset(offset + 2, dst, src);
			fb_copy_offset(offset + 3, dst, src);
			offset += 4;
		}
		while (offset < end)
			fb_copy_offset(offset++, dst, src);

		/* Trailing bits */
		if (last)
			fb_copy_offset_masked(last, offset, dst, src);
	}
}

/* reverse aligned copy */
static inline void fb_copy_aligned_rev(const struct fb_address *dst,
				       const struct fb_address *src,
				       int end, struct fb_reverse reverse)
{
	unsigned long first, last;

	first = fb_pixel_mask(dst->bits, reverse);
	last = ~fb_pixel_mask(end & (BITS_PER_LONG-1), reverse);

	if (end <= BITS_PER_LONG) {
		/* Single word */
		if (last)
			first &= last;
		if (first == ~0UL)
			fb_copy_offset(0, dst, src);
		else
			fb_copy_offset_masked(first, 0, dst, src);
	} else {
		/* Multiple destination words */
		int offset = first != ~0UL;

		/* Trailing bits */
		end /= BITS_PER_LONG;

		if (last)
			fb_copy_offset_masked(last, end, dst, src);

		/* Main chunk */
		while (end >= offset + 4) {
			fb_copy_offset(end - 1, dst, src);
			fb_copy_offset(end - 2, dst, src);
			fb_copy_offset(end - 3, dst, src);
			fb_copy_offset(end - 4, dst, src);
			end -= 4;
		}
		while (end > offset)
			fb_copy_offset(--end, dst, src);

		/* Leading bits */
		if (offset)
			fb_copy_offset_masked(first, 0, dst, src);
	}
}

static inline void fb_copy_aligned(struct fb_address *dst, struct fb_address *src,
				   int width, u32 height, unsigned int bits_per_line,
				   struct fb_reverse reverse, bool rev_copy)
{
	if (rev_copy)
		while (height--) {
			fb_copy_aligned_rev(dst, src, width + dst->bits, reverse);
			fb_address_backward(dst, bits_per_line);
			fb_address_backward(src, bits_per_line);
		}
	else
		while (height--) {
			fb_copy_aligned_fwd(dst, src, width + dst->bits, reverse);
			fb_address_forward(dst, bits_per_line);
			fb_address_forward(src, bits_per_line);
		}
}

static __always_inline void fb_copy_fwd(const struct fb_address *dst,
					const struct fb_address *src, int width,
					unsigned long (*reorder)(unsigned long val,
								 struct fb_reverse reverse),
					struct fb_reverse reverse)
{
	unsigned long first, last;
	unsigned long d0, d1;
	int end = dst->bits + width;
	int shift, left, right;

	first = fb_pixel_mask(dst->bits, reverse);
	last = ~fb_pixel_mask(end & (BITS_PER_LONG-1), reverse);

	shift = dst->bits - src->bits;
	right = shift & (BITS_PER_LONG - 1);
	left = -shift & (BITS_PER_LONG - 1);

	if (end <= BITS_PER_LONG) {
		/* Single destination word */
		last = last ? (last & first) : first;
		if (shift < 0) {
			d0 = fb_left(reorder(fb_read_offset(-1, src), reverse), left);
			if (src->bits + width > BITS_PER_LONG)
				d0 |= fb_right(reorder(fb_read_offset(0, src), reverse), right);

			if (last == ~0UL)
				fb_write_offset(reorder(d0, reverse), 0, dst);
			else
				fb_modify_offset(reorder(d0, reverse), last, 0, dst);
		} else {
			d0 = fb_right(reorder(fb_read_offset(0, src), reverse), right);
			fb_modify_offset(reorder(d0, reverse), last, 0, dst);
		}
	} else {
		/* Multiple destination words */
		int offset = first != ~0UL;

		/* Leading bits */
		if (shift < 0)
			d0 = reorder(fb_read_offset(-1, src), reverse);
		else
			d0 = 0;

		/* 2 source words */
		if (offset) {
			d1 = reorder(fb_read_offset(0, src), reverse);
			d0 = fb_left(d0, left) | fb_right(d1, right);
			fb_modify_offset(reorder(d0, reverse), first, 0, dst);
			d0 = d1;
		}

		/* Main chunk */
		end /= BITS_PER_LONG;
		if (reorder == fb_no_reverse)
			while (offset + 4 <= end) {
				d1 = fb_read_offset(offset + 0, src);
				d0 = fb_left(d0, left) | fb_right(d1, right);
				fb_write_offset(d0, offset + 0, dst);
				d0 = d1;
				d1 = fb_read_offset(offset + 1, src);
				d0 = fb_left(d0, left) | fb_right(d1, right);
				fb_write_offset(d0, offset + 1, dst);
				d0 = d1;
				d1 = fb_read_offset(offset + 2, src);
				d0 = fb_left(d0, left) | fb_right(d1, right);
				fb_write_offset(d0, offset + 2, dst);
				d0 = d1;
				d1 = fb_read_offset(offset + 3, src);
				d0 = fb_left(d0, left) | fb_right(d1, right);
				fb_write_offset(d0, offset + 3, dst);
				d0 = d1;
				offset += 4;
			}

		while (offset < end) {
			d1 = reorder(fb_read_offset(offset, src), reverse);
			d0 = fb_left(d0, left) | fb_right(d1, right);
			fb_write_offset(reorder(d0, reverse), offset, dst);
			d0 = d1;
			offset++;
		}

		/* Trailing bits */
		if (last) {
			d0 = fb_left(d0, left);
			if (src->bits + width
			    > offset * BITS_PER_LONG + ((shift < 0) ? BITS_PER_LONG : 0))
				d0 |= fb_right(reorder(fb_read_offset(offset, src), reverse),
					       right);
			fb_modify_offset(reorder(d0, reverse), last, offset, dst);
		}
	}
}

static __always_inline void fb_copy_rev(const struct fb_address *dst,
					const struct fb_address *src, int end,
					unsigned long (*reorder)(unsigned long val,
								 struct fb_reverse reverse),
					struct fb_reverse reverse)
{
	unsigned long first, last;
	unsigned long d0, d1;
	int shift, left, right;

	first = fb_pixel_mask(dst->bits, reverse);
	last = ~fb_pixel_mask(end & (BITS_PER_LONG-1), reverse);

	shift = dst->bits - src->bits;
	right = shift & (BITS_PER_LONG-1);
	left = -shift & (BITS_PER_LONG-1);

	if (end <= BITS_PER_LONG) {
		/* Single destination word */
		if (last)
			first &= last;

		if (shift > 0) {
			d0 = fb_right(reorder(fb_read_offset(1, src), reverse), right);
			if (src->bits > left)
				d0 |= fb_left(reorder(fb_read_offset(0, src), reverse), left);
			fb_modify_offset(reorder(d0, reverse), first, 0, dst);
		} else {
			d0 = fb_left(reorder(fb_read_offset(0, src), reverse), left);
			if (src->bits + end - dst->bits > BITS_PER_LONG)
				d0 |= fb_right(reorder(fb_read_offset(1, src), reverse), right);
			if (first == ~0UL)
				fb_write_offset(reorder(d0, reverse), 0, dst);
			else
				fb_modify_offset(reorder(d0, reverse), first, 0, dst);
		}
	} else {
		/* Multiple destination words */
		int offset = first != ~0UL;

		end /= BITS_PER_LONG;

		/* 2 source words */
		if (fb_right(~0UL, right) & last)
			d0 = fb_right(reorder(fb_read_offset(end + 1, src), reverse), right);
		else
			d0 = 0;

		/* Trailing bits */
		d1 = reorder(fb_read_offset(end, src), reverse);
		if (last)
			fb_modify_offset(reorder(fb_left(d1, left) | d0, reverse),
					 last, end, dst);
		d0 = d1;

		/* Main chunk */
		if (reorder == fb_no_reverse)
			while (end >= offset + 4) {
				d1 = fb_read_offset(end - 1, src);
				d0 = fb_left(d1, left) | fb_right(d0, right);
				fb_write_offset(d0, end - 1, dst);
				d0 = d1;
				d1 = fb_read_offset(end - 2, src);
				d0 = fb_left(d1, left) | fb_right(d0, right);
				fb_write_offset(d0, end - 2, dst);
				d0 = d1;
				d1 = fb_read_offset(end - 3, src);
				d0 = fb_left(d1, left) | fb_right(d0, right);
				fb_write_offset(d0, end - 3, dst);
				d0 = d1;
				d1 = fb_read_offset(end - 4, src);
				d0 = fb_left(d1, left) | fb_right(d0, right);
				fb_write_offset(d0, end - 4, dst);
				d0 = d1;
				end -= 4;
			}

		while (end > offset) {
			end--;
			d1 = reorder(fb_read_offset(end, src), reverse);
			d0 = fb_left(d1, left) | fb_right(d0, right);
			fb_write_offset(reorder(d0, reverse), end, dst);
			d0 = d1;
		}

		/* Leading bits */
		if (offset) {
			d0 = fb_right(d0, right);
			if (src->bits > left)
				d0 |= fb_left(reorder(fb_read_offset(0, src), reverse), left);
			fb_modify_offset(reorder(d0, reverse), first, 0, dst);
		}
	}
}

static __always_inline void fb_copy(struct fb_address *dst, struct fb_address *src,
				    int width, u32 height, unsigned int bits_per_line,
				    unsigned long (*reorder)(unsigned long val,
							     struct fb_reverse reverse),
				    struct fb_reverse reverse, bool rev_copy)
{
	if (rev_copy)
		while (height--) {
			int move = src->bits < dst->bits ? -1 : 0;

			fb_address_move_long(src, move);
			fb_copy_rev(dst, src, width + dst->bits, reorder, reverse);
			fb_address_backward(dst, bits_per_line);
			fb_address_backward(src, bits_per_line);
			fb_address_move_long(src, -move);
		}
	else
		while (height--) {
			int move = src->bits > dst->bits ? 1 : 0;

			fb_address_move_long(src, move);
			fb_copy_fwd(dst, src, width, reorder, reverse);
			fb_address_forward(dst, bits_per_line);
			fb_address_forward(src, bits_per_line);
			fb_address_move_long(src, -move);
		}
}

static inline void fb_copyarea(struct fb_info *p, const struct fb_copyarea *area)
{
	int bpp = p->var.bits_per_pixel;
	u32 dy = area->dy;
	u32 sy = area->sy;
	u32 height = area->height;
	int width = area->width * bpp;
	unsigned int bits_per_line = BYTES_TO_BITS(p->fix.line_length);
	struct fb_reverse reverse = fb_reverse_init(p);
	struct fb_address dst = fb_address_init(p);
	struct fb_address src = dst;
	bool rev_copy = (dy > sy) || (dy == sy && area->dx > area->sx);

	if (rev_copy) {
		dy += height - 1;
		sy += height - 1;
	}
	fb_address_forward(&dst, dy*bits_per_line + area->dx*bpp);
	fb_address_forward(&src, sy*bits_per_line + area->sx*bpp);

	if (src.bits == dst.bits)
		fb_copy_aligned(&dst, &src, width, height, bits_per_line, reverse, rev_copy);
	else if (!reverse.byte && (!reverse.pixel ||
				     !((src.bits ^ dst.bits) & (BITS_PER_BYTE-1)))) {
		fb_copy(&dst, &src, width, height, bits_per_line,
			fb_no_reverse, reverse, rev_copy);
	} else
		fb_copy(&dst, &src, width, height, bits_per_line,
			fb_reverse_long, reverse, rev_copy);
}
