/* SPDX-License-Identifier: GPL-2.0-only
 *
 *  Generic bitmap / 8 bpp image bitstreamer for packed pixel framebuffers
 *
 *	Rewritten by:
 *	Copyright (C)  2025 Zsolt Kajtar (soci@c64.rulez.org)
 *
 *	Based on previous work of:
 *	Copyright (C)  June 1999 James Simmons
 *	Anton Vorontsov <avorontsov@ru.mvista.com>
 *	Pavel Pisa <pisa@cmp.felk.cvut.cz>
 *	Antonino A. Daplas <adaplas@gmail.com>
 *	and others
 *
 * NOTES:
 *
 * Handles native and foreign byte order on both endians, standard and
 * reverse pixel order in a byte (<8 BPP), word length of 32/64 bits,
 * bits per pixel from 1 to the word length. Handles line lengths at byte
 * granularity while maintaining aligned accesses.
 *
 * Optimized routines for word aligned 1, 2, 4 pixel per word for high
 * bpp modes and 4 pixel at a time operation for low bpp.
 *
 * The color image is expected to be one byte per pixel, and values should
 * not exceed the bitdepth or the pseudo palette (if used).
 */
#include "fb_draw.h"

/* bitmap image iterator, one pixel at a time */
struct fb_bitmap_iter {
	const u8 *data;
	unsigned long colors[2];
	int width, i;
};

static bool fb_bitmap_image(void *iterator, unsigned long *pixels, int *bits)
{
	struct fb_bitmap_iter *iter = iterator;

	if (iter->i < iter->width) {
		int bit = ~iter->i & (BITS_PER_BYTE-1);
		int byte = iter->i++ / BITS_PER_BYTE;

		*pixels = iter->colors[(iter->data[byte] >> bit) & 1];
		return true;
	}
	iter->data += BITS_TO_BYTES(iter->width);
	iter->i = 0;
	return false;
}

/* color image iterator, one pixel at a time */
struct fb_color_iter {
	const u8 *data;
	const u32 *palette;
	struct fb_reverse reverse;
	int shift;
	int width, i;
};

static bool fb_color_image(void *iterator, unsigned long *pixels, int *bits)
{
	struct fb_color_iter *iter = iterator;

	if (iter->i < iter->width) {
		unsigned long color = iter->data[iter->i++];

		if (iter->palette)
			color = iter->palette[color];
		*pixels = color << iter->shift;
		if (iter->reverse.pixel)
			*pixels = fb_reverse_bits_long(*pixels);
		return true;
	}
	iter->data += iter->width;
	iter->i = 0;
	return false;
}

/* bitmap image iterator, 4 pixels at a time */
struct fb_bitmap4x_iter {
	const u8 *data;
	u32 fgxcolor, bgcolor;
	int width, i;
	const u32 *expand;
	int bpp;
	bool top;
};

static bool fb_bitmap4x_image(void *iterator, unsigned long *pixels, int *bits)
{
	struct fb_bitmap4x_iter *iter = iterator;
	u8 data;

	if (iter->i >= BITS_PER_BYTE/2) {
		iter->i -= BITS_PER_BYTE/2;
		iter->top = !iter->top;
		if (iter->top)
			data = *iter->data++ >> BITS_PER_BYTE/2;
		else
			data = iter->data[-1] & ((1 << BITS_PER_BYTE/2)-1);
	} else if (iter->i != 0) {
		*bits = iter->bpp * iter->i;
		if (iter->top)
			data = iter->data[-1] & ((1 << BITS_PER_BYTE/2)-1);
		else
			data = *iter->data++ >> BITS_PER_BYTE/2;
#ifndef __LITTLE_ENDIAN
		data >>= BITS_PER_BYTE/2 - iter->i;
#endif
		iter->i = 0;
	} else {
		*bits = iter->bpp * BITS_PER_BYTE/2;
		iter->i = iter->width;
		iter->top = false;
		return false;
	}
	*pixels = (iter->fgxcolor & iter->expand[data]) ^ iter->bgcolor;
#ifndef __LITTLE_ENDIAN
	*pixels <<= BITS_PER_LONG - *bits;
#endif
	return true;
}

/* draw a line a group of pixels at a time */
static __always_inline void fb_bitblit(bool (*get)(void *iter, unsigned long *pixels,
						   int *bits),
				       void *iter, int bits, struct fb_address *dst,
				       struct fb_reverse reverse)
{
	unsigned long pixels, val, mask, old;
	int offset = 0;
	int shift = dst->bits;

	if (shift) {
		old = fb_read_offset(0, dst);
		val = fb_reverse_long(old, reverse);
		val &= ~fb_right(~0UL, shift);
	} else {
		old = 0;
		val = 0;
	}

	while (get(iter, &pixels, &bits)) {
		val |= fb_right(pixels, shift);
		shift += bits;

		if (shift < BITS_PER_LONG)
			continue;

		val = fb_reverse_long(val, reverse);
		fb_write_offset(val, offset++, dst);
		shift &= BITS_PER_LONG - 1;
		val = !shift ? 0 : fb_left(pixels, bits - shift);
	}

	if (shift) {
		mask = ~fb_pixel_mask(shift, reverse);
		val = fb_reverse_long(val, reverse);
		if (offset || !dst->bits)
			old = fb_read_offset(offset, dst);
		fb_write_offset(fb_comp(val, old, mask), offset, dst);
	}
}

/* draw a color image a pixel at a time */
static inline void fb_color_imageblit(const struct fb_image *image, struct fb_address *dst,
				      unsigned int bits_per_line, const u32 *palette, int bpp,
				      struct fb_reverse reverse)
{
	struct fb_color_iter iter;
	u32 height;

	iter.data = (const u8 *)image->data;
	iter.palette = palette;
	iter.reverse = reverse;
#ifdef __LITTLE_ENDIAN
	if (reverse.pixel)
		iter.shift = BITS_PER_BYTE - bpp;
	else
		iter.shift = 0;
#else
	if (reverse.pixel)
		iter.shift = BITS_PER_LONG - BITS_PER_BYTE;
	else
		iter.shift = BITS_PER_LONG - bpp;
#endif
	iter.width = image->width;
	iter.i = 0;

	height = image->height;
	while (height--) {
		fb_bitblit(fb_color_image, &iter, bpp, dst, reverse);
		fb_address_forward(dst, bits_per_line);
	}
}

#ifdef __LITTLE_ENDIAN
#define FB_GEN(a, b) (((a)/8+(((a)&4)<<((b)-2)) \
		       +(((a)&2)<<((b)*2-1))+(((a)&1u)<<((b)*3)))*((1<<(b))-1))
#define FB_GEN1(a) ((a)/8+((a)&4)/2+((a)&2)*2+((a)&1)*8)
#else
#define FB_GEN(a, b) (((((a)/8)<<((b)*3))+(((a)&4)<<((b)*2-2)) \
		       +(((a)&2)<<(b-1))+((a)&1u))*((1<<(b))-1))
#define FB_GEN1(a) (a)
#endif

#define FB_GENx(a) { FB_GEN(0, (a)), FB_GEN(1, (a)), FB_GEN(2, (a)), FB_GEN(3, (a)),	\
	FB_GEN(4, (a)), FB_GEN(5, (a)), FB_GEN(6, (a)), FB_GEN(7, (a)),			\
	FB_GEN(8, (a)), FB_GEN(9, (a)), FB_GEN(10, (a)), FB_GEN(11, (a)),		\
	FB_GEN(12, (a)), FB_GEN(13, (a)), FB_GEN(14, (a)), FB_GEN(15, (a)) }

/* draw a 2 color image four pixels at a time (for 1-8 bpp only) */
static inline void fb_bitmap4x_imageblit(const struct fb_image *image, struct fb_address *dst,
					 unsigned long fgcolor, unsigned long bgcolor, int bpp,
					 unsigned int bits_per_line, struct fb_reverse reverse)
{
	static const u32 mul[BITS_PER_BYTE] = {
		0xf, 0x55, 0x249, 0x1111, 0x8421, 0x41041, 0x204081, 0x01010101
	};
	static const u32 expand[BITS_PER_BYTE][1 << 4] = {
		{
			FB_GEN1(0), FB_GEN1(1), FB_GEN1(2), FB_GEN1(3),
			FB_GEN1(4), FB_GEN1(5), FB_GEN1(6), FB_GEN1(7),
			FB_GEN1(8), FB_GEN1(9), FB_GEN1(10), FB_GEN1(11),
			FB_GEN1(12), FB_GEN1(13), FB_GEN1(14), FB_GEN1(15)
		},
		FB_GENx(2), FB_GENx(3), FB_GENx(4),
		FB_GENx(5), FB_GENx(6), FB_GENx(7), FB_GENx(8),
	};
	struct fb_bitmap4x_iter iter;
	u32 height;

	iter.data = (const u8 *)image->data;
	if (reverse.pixel) {
		fgcolor = fb_reverse_bits_long(fgcolor << (BITS_PER_BYTE - bpp));
		bgcolor = fb_reverse_bits_long(bgcolor << (BITS_PER_BYTE - bpp));
	}
	iter.fgxcolor = (fgcolor ^ bgcolor) * mul[bpp-1];
	iter.bgcolor = bgcolor * mul[bpp-1];
	iter.width = image->width;
	iter.i = image->width;
	iter.expand = expand[bpp-1];
	iter.bpp = bpp;
	iter.top = false;

	height = image->height;
	while (height--) {
		fb_bitblit(fb_bitmap4x_image, &iter, bpp * BITS_PER_BYTE/2, dst, reverse);
		fb_address_forward(dst, bits_per_line);
	}
}

/* draw a bitmap image 1 pixel at a time (for >8 bpp) */
static inline void fb_bitmap1x_imageblit(const struct fb_image *image, struct fb_address *dst,
					 unsigned long fgcolor, unsigned long bgcolor, int bpp,
					 unsigned int bits_per_line, struct fb_reverse reverse)
{
	struct fb_bitmap_iter iter;
	u32 height;

	iter.colors[0] = bgcolor;
	iter.colors[1] = fgcolor;
#ifndef __LITTLE_ENDIAN
	iter.colors[0] <<= BITS_PER_LONG - bpp;
	iter.colors[1] <<= BITS_PER_LONG - bpp;
#endif
	iter.data = (const u8 *)image->data;
	iter.width = image->width;
	iter.i = 0;

	height = image->height;
	while (height--) {
		fb_bitblit(fb_bitmap_image, &iter, bpp, dst, reverse);
		fb_address_forward(dst, bits_per_line);
	}
}

/* one pixel per word, 64/32 bpp blitting */
static inline void fb_bitmap_1ppw(const struct fb_image *image, struct fb_address *dst,
				  unsigned long fgcolor, unsigned long bgcolor,
				  int words_per_line, struct fb_reverse reverse)
{
	unsigned long tab[2];
	const u8 *src = (u8 *)image->data;
	int width = image->width;
	int offset;
	u32 height;

	if (reverse.byte) {
		tab[0] = swab_long(bgcolor);
		tab[1] = swab_long(fgcolor);
	} else {
		tab[0] = bgcolor;
		tab[1] = fgcolor;
	}

	height = image->height;
	while (height--) {
		for (offset = 0; offset + 8 <= width; offset += 8) {
			unsigned int srcbyte = *src++;

			fb_write_offset(tab[(srcbyte >> 7) & 1], offset + 0, dst);
			fb_write_offset(tab[(srcbyte >> 6) & 1], offset + 1, dst);
			fb_write_offset(tab[(srcbyte >> 5) & 1], offset + 2, dst);
			fb_write_offset(tab[(srcbyte >> 4) & 1], offset + 3, dst);
			fb_write_offset(tab[(srcbyte >> 3) & 1], offset + 4, dst);
			fb_write_offset(tab[(srcbyte >> 2) & 1], offset + 5, dst);
			fb_write_offset(tab[(srcbyte >> 1) & 1], offset + 6, dst);
			fb_write_offset(tab[(srcbyte >> 0) & 1], offset + 7, dst);
		}

		if (offset < width) {
			unsigned int srcbyte = *src++;

			while (offset < width) {
				fb_write_offset(tab[(srcbyte >> 7) & 1], offset, dst);
				srcbyte <<= 1;
				offset++;
			}
		}
		fb_address_move_long(dst, words_per_line);
	}
}

static inline unsigned long fb_pack(unsigned long left, unsigned long right, int bits)
{
#ifdef __LITTLE_ENDIAN
	return left | right << bits;
#else
	return right | left << bits;
#endif
}

/* aligned 32/16 bpp blitting */
static inline void fb_bitmap_2ppw(const struct fb_image *image, struct fb_address *dst,
				  unsigned long fgcolor, unsigned long bgcolor,
				  int words_per_line, struct fb_reverse reverse)
{
	unsigned long tab[4];
	const u8 *src = (u8 *)image->data;
	int width = image->width / 2;
	int offset;
	u32 height;

	tab[0] = fb_pack(bgcolor, bgcolor, BITS_PER_LONG/2);
	tab[1] = fb_pack(bgcolor, fgcolor, BITS_PER_LONG/2);
	tab[2] = fb_pack(fgcolor, bgcolor, BITS_PER_LONG/2);
	tab[3] = fb_pack(fgcolor, fgcolor, BITS_PER_LONG/2);

	if (reverse.byte) {
		tab[0] = swab_long(tab[0]);
		tab[1] = swab_long(tab[1]);
		tab[2] = swab_long(tab[2]);
		tab[3] = swab_long(tab[3]);
	}

	height = image->height;
	while (height--) {
		for (offset = 0; offset + 4 <= width; offset += 4) {
			unsigned int srcbyte = *src++;

			fb_write_offset(tab[(srcbyte >> 6) & 3], offset + 0, dst);
			fb_write_offset(tab[(srcbyte >> 4) & 3], offset + 1, dst);
			fb_write_offset(tab[(srcbyte >> 2) & 3], offset + 2, dst);
			fb_write_offset(tab[(srcbyte >> 0) & 3], offset + 3, dst);
		}

		if (offset < width) {
			unsigned int srcbyte = *src++;

			while (offset < width) {
				fb_write_offset(tab[(srcbyte >> 6) & 3], offset, dst);
				srcbyte <<= 2;
				offset++;
			}
		}
		fb_address_move_long(dst, words_per_line);
	}
}

#define FB_PATP(a, b) (((a)<<((b)*BITS_PER_LONG/4))*((1UL<<BITS_PER_LONG/4)-1UL))
#define FB_PAT4(a) (FB_PATP((a)&1, 0)|FB_PATP(((a)&2)/2, 1)| \
	FB_PATP(((a)&4)/4, 2)|FB_PATP(((a)&8)/8, 3))

/* aligned 16/8 bpp blitting */
static inline void fb_bitmap_4ppw(const struct fb_image *image, struct fb_address *dst,
				  unsigned long fgcolor, unsigned long bgcolor,
				  int words_per_line, struct fb_reverse reverse)
{
	static const unsigned long tab16_be[] = {
		0, FB_PAT4(1UL), FB_PAT4(2UL), FB_PAT4(3UL),
		FB_PAT4(4UL), FB_PAT4(5UL), FB_PAT4(6UL), FB_PAT4(7UL),
		FB_PAT4(8UL), FB_PAT4(9UL), FB_PAT4(10UL), FB_PAT4(11UL),
		FB_PAT4(12UL), FB_PAT4(13UL), FB_PAT4(14UL), ~0UL
	};
	static const unsigned long tab16_le[] = {
		0, FB_PAT4(8UL), FB_PAT4(4UL), FB_PAT4(12UL),
		FB_PAT4(2UL), FB_PAT4(10UL), FB_PAT4(6UL), FB_PAT4(14UL),
		FB_PAT4(1UL), FB_PAT4(9UL), FB_PAT4(5UL), FB_PAT4(13UL),
		FB_PAT4(3UL), FB_PAT4(11UL), FB_PAT4(7UL), ~0UL
	};
	const unsigned long *tab;
	const u8 *src = (u8 *)image->data;
	int width = image->width / 4;
	int offset;
	u32 height;

	fgcolor = fgcolor | fgcolor << BITS_PER_LONG/4;
	bgcolor = bgcolor | bgcolor << BITS_PER_LONG/4;
	fgcolor = fgcolor | fgcolor << BITS_PER_LONG/2;
	bgcolor = bgcolor | bgcolor << BITS_PER_LONG/2;
	fgcolor ^= bgcolor;

	if (BITS_PER_LONG/4 > BITS_PER_BYTE && reverse.byte) {
		fgcolor = swab_long(fgcolor);
		bgcolor = swab_long(bgcolor);
	}

#ifdef __LITTLE_ENDIAN
	tab = reverse.byte ? tab16_be : tab16_le;
#else
	tab = reverse.byte ? tab16_le : tab16_be;
#endif

	height = image->height;
	while (height--) {
		for (offset = 0; offset + 2 <= width; offset += 2, src++) {
			fb_write_offset((fgcolor & tab[*src >> 4]) ^ bgcolor, offset + 0, dst);
			fb_write_offset((fgcolor & tab[*src & 0xf]) ^ bgcolor, offset + 1, dst);
		}

		if (offset < width)
			fb_write_offset((fgcolor & tab[*src++ >> 4]) ^ bgcolor, offset, dst);

		fb_address_move_long(dst, words_per_line);
	}
}

static inline void fb_bitmap_imageblit(const struct fb_image *image, struct fb_address *dst,
				       unsigned int bits_per_line, const u32 *palette, int bpp,
				       struct fb_reverse reverse)
{
	unsigned long fgcolor, bgcolor;

	if (palette) {
		fgcolor = palette[image->fg_color];
		bgcolor = palette[image->bg_color];
	} else {
		fgcolor = image->fg_color;
		bgcolor = image->bg_color;
	}

	if (!dst->bits && !(bits_per_line & (BITS_PER_LONG-1))) {
		if (bpp == BITS_PER_LONG && BITS_PER_LONG == 32) {
			fb_bitmap_1ppw(image, dst, fgcolor, bgcolor,
				       bits_per_line / BITS_PER_LONG, reverse);
			return;
		}
		if (bpp == BITS_PER_LONG/2 && !(image->width & 1)) {
			fb_bitmap_2ppw(image, dst, fgcolor, bgcolor,
				       bits_per_line / BITS_PER_LONG, reverse);
			return;
		}
		if (bpp == BITS_PER_LONG/4 && !(image->width & 3)) {
			fb_bitmap_4ppw(image, dst, fgcolor, bgcolor,
				       bits_per_line / BITS_PER_LONG, reverse);
			return;
		}
	}

	if (bpp > 0 && bpp <= BITS_PER_BYTE)
		fb_bitmap4x_imageblit(image, dst, fgcolor, bgcolor, bpp,
				     bits_per_line, reverse);
	else if (bpp > BITS_PER_BYTE && bpp <= BITS_PER_LONG)
		fb_bitmap1x_imageblit(image, dst, fgcolor, bgcolor, bpp,
				     bits_per_line, reverse);
}

static inline void fb_imageblit(struct fb_info *p, const struct fb_image *image)
{
	int bpp = p->var.bits_per_pixel;
	unsigned int bits_per_line = BYTES_TO_BITS(p->fix.line_length);
	struct fb_address dst = fb_address_init(p);
	struct fb_reverse reverse = fb_reverse_init(p);
	const u32 *palette = fb_palette(p);

	fb_address_forward(&dst, image->dy * bits_per_line + image->dx * bpp);

	if (image->depth == 1)
		fb_bitmap_imageblit(image, &dst, bits_per_line, palette, bpp, reverse);
	else
		fb_color_imageblit(image, &dst, bits_per_line, palette, bpp, reverse);
}
