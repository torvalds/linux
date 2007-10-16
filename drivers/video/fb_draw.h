#ifndef _FB_DRAW_H
#define _FB_DRAW_H

#include <asm/types.h>
#include <linux/fb.h>

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

#ifdef CONFIG_FB_CFB_REV_PIXELS_IN_BYTE

static inline u32 fb_shifted_pixels_mask_u32(u32 index, u32 bswapmask)
{
	u32 mask;

	if (!bswapmask) {
		mask = FB_SHIFT_HIGH(~(u32)0, index);
	} else {
		mask = 0xff << FB_LEFT_POS(8);
		mask = FB_SHIFT_LOW(mask, index & (bswapmask)) & mask;
		mask = FB_SHIFT_HIGH(mask, index & ~(bswapmask));
#if defined(__i386__) || defined(__x86_64__)
		/* Shift argument is limited to 0 - 31 on x86 based CPU's */
		if(index + bswapmask < 32)
#endif
			mask |= FB_SHIFT_HIGH(~(u32)0,
					(index + bswapmask) & ~(bswapmask));
	}
	return mask;
}

static inline unsigned long fb_shifted_pixels_mask_long(u32 index, u32 bswapmask)
{
	unsigned long mask;

	if (!bswapmask) {
		mask = FB_SHIFT_HIGH(~0UL, index);
	} else {
		mask = 0xff << FB_LEFT_POS(8);
		mask = FB_SHIFT_LOW(mask, index & (bswapmask)) & mask;
		mask = FB_SHIFT_HIGH(mask, index & ~(bswapmask));
#if defined(__i386__) || defined(__x86_64__)
		/* Shift argument is limited to 0 - 31 on x86 based CPU's */
		if(index + bswapmask < BITS_PER_LONG)
#endif
			mask |= FB_SHIFT_HIGH(~0UL,
					(index + bswapmask) & ~(bswapmask));
	}
	return mask;
}


static inline u32 fb_compute_bswapmask(struct fb_info *info)
{
	u32 bswapmask = 0;
	unsigned bpp = info->var.bits_per_pixel;

	if ((bpp < 8) && (info->var.nonstd & FB_NONSTD_REV_PIX_IN_B)) {
		/*
		 * Reversed order of pixel layout in bytes
		 * works only for 1, 2 and 4 bpp
		 */
		bswapmask = 7 - bpp + 1;
	}
	return bswapmask;
}

#else /* CONFIG_FB_CFB_REV_PIXELS_IN_BYTE */

#define fb_shifted_pixels_mask_u32(i, b) FB_SHIFT_HIGH(~(u32)0, (i))
#define fb_shifted_pixels_mask_long(i, b) FB_SHIFT_HIGH(~0UL, (i))
#define fb_compute_bswapmask(...) 0

#endif  /* CONFIG_FB_CFB_REV_PIXELS_IN_BYTE */

#endif /* FB_DRAW_H */
