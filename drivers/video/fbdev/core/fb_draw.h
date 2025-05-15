/* SPDX-License-Identifier: GPL-2.0
 *
 *  Various common functions used by the framebuffer drawing code
 *
 *	Copyright (C)  2025 Zsolt Kajtar (soci@c64.rulez.org)
 */
#ifndef _FB_DRAW_H
#define _FB_DRAW_H

/* swap bytes in a long, independent of word size */
#define swab_long _swab_long(BITS_PER_LONG)
#define _swab_long(x) __swab_long(x)
#define __swab_long(x) swab##x

/* move the address pointer by the number of words */
static inline void fb_address_move_long(struct fb_address *adr, int offset)
{
	adr->address += offset * (BITS_PER_LONG / BITS_PER_BYTE);
}

/* move the address pointer forward with the number of bits */
static inline void fb_address_forward(struct fb_address *adr, unsigned int offset)
{
	unsigned int bits = (unsigned int)adr->bits + offset;

	adr->bits = bits & (BITS_PER_LONG - 1u);
	adr->address += (bits & ~(BITS_PER_LONG - 1u)) / BITS_PER_BYTE;
}

/* move the address pointer backwards with the number of bits */
static inline void fb_address_backward(struct fb_address *adr, unsigned int offset)
{
	int bits = adr->bits - (int)offset;

	adr->bits = bits & (BITS_PER_LONG - 1);
	if (bits < 0)
		adr->address -= (adr->bits - bits) / BITS_PER_BYTE;
	else
		adr->address += (bits - adr->bits) / BITS_PER_BYTE;
}

/* compose pixels based on mask */
static inline unsigned long fb_comp(unsigned long set, unsigned long unset, unsigned long mask)
{
	return ((set ^ unset) & mask) ^ unset;
}

/* framebuffer read-modify-write access for replacing bits in the mask */
static inline void fb_modify_offset(unsigned long val, unsigned long mask,
				    int offset, const struct fb_address *dst)
{
	fb_write_offset(fb_comp(val, fb_read_offset(offset, dst), mask), offset, dst);
}

/*
 * get current palette, if applicable for visual
 *
 * The pseudo color table entries (and colors) are right justified and in the
 * same byte order as it's expected to be placed into a native ordered
 * framebuffer memory. What that means:
 *
 * Expected bytes in framebuffer memory (in native order):
 * RR GG BB RR GG BB RR GG BB ...
 *
 * Pseudo palette entry on little endian arch:
 * RR | GG << 8 | BB << 16
 *
 * Pseudo palette entry on a big endian arch:
 * RR << 16 | GG << 8 | BB
 */
static inline const u32 *fb_palette(struct fb_info *info)
{
	return (info->fix.visual == FB_VISUAL_TRUECOLOR ||
		info->fix.visual == FB_VISUAL_DIRECTCOLOR) ? info->pseudo_palette : NULL;
}

/* move pixels right on screen when framebuffer is in native order */
static inline unsigned long fb_right(unsigned long value, int index)
{
#ifdef __LITTLE_ENDIAN
	return value << index;
#else
	return value >> index;
#endif
}

/* move pixels left on screen when framebuffer is in native order */
static inline unsigned long fb_left(unsigned long value, int index)
{
#ifdef __LITTLE_ENDIAN
	return value >> index;
#else
	return value << index;
#endif
}

/* reversal options */
struct fb_reverse {
	bool byte, pixel;
};

/* reverse bits of each byte in a long */
static inline unsigned long fb_reverse_bits_long(unsigned long val)
{
#if defined(CONFIG_HAVE_ARCH_BITREVERSE) && BITS_PER_LONG == 32
	return bitrev8x4(val);
#else
	val = fb_comp(val >> 1, val << 1, ~0UL / 3);
	val = fb_comp(val >> 2, val << 2, ~0UL / 5);
	return fb_comp(val >> 4, val << 4, ~0UL / 17);
#endif
}

/* apply byte and bit reversals as necessary */
static inline unsigned long fb_reverse_long(unsigned long val,
					    struct fb_reverse reverse)
{
	if (reverse.pixel)
		val = fb_reverse_bits_long(val);
	return reverse.byte ? swab_long(val) : val;
}

/* calculate a pixel mask for the given reversal */
static inline unsigned long fb_pixel_mask(int index, struct fb_reverse reverse)
{
#ifdef FB_REV_PIXELS_IN_BYTE
	if (reverse.byte)
		return reverse.pixel ? fb_left(~0UL, index) : swab_long(fb_right(~0UL, index));
	else
		return reverse.pixel ? swab_long(fb_left(~0UL, index)) : fb_right(~0UL, index);
#else
	return reverse.byte ? swab_long(fb_right(~0UL, index)) : fb_right(~0UL, index);
#endif
}


/*
 * initialise reversals based on info
 *
 * Normally the first byte is the low byte on little endian and in the high
 * on big endian. If it's the other way around then that's reverse byte order.
 *
 * Normally the first pixel is the LSB on little endian and the MSB on big
 * endian. If that's not the case that's reverse pixel order.
 */
static inline struct fb_reverse fb_reverse_init(struct fb_info *info)
{
	struct fb_reverse reverse;
#ifdef __LITTLE_ENDIAN
	reverse.byte = fb_be_math(info) != 0;
#else
	reverse.byte = fb_be_math(info) == 0;
#endif
#ifdef FB_REV_PIXELS_IN_BYTE
	reverse.pixel = info->var.bits_per_pixel < BITS_PER_BYTE
		&& (info->var.nonstd & FB_NONSTD_REV_PIX_IN_B);
#else
	reverse.pixel = false;
#endif
	return reverse;
}

#endif /* FB_DRAW_H */
