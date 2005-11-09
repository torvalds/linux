/*
 *  linux/drivers/video/console/fbcon_rotate.h -- Software Display Rotation
 *
 *	Copyright (C) 2005 Antonino Daplas <adaplas@pol.net>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */

#ifndef _FBCON_ROTATE_H
#define _FBCON_ROTATE_H

#define FNTCHARCNT(fd)	(((int *)(fd))[-3])

#define GETVYRES(s,i) ({                           \
        (s == SCROLL_REDRAW || s == SCROLL_MOVE) ? \
        (i)->var.yres : (i)->var.yres_virtual; })

#define GETVXRES(s,i) ({                           \
        (s == SCROLL_REDRAW || s == SCROLL_MOVE || !(i)->fix.xpanstep) ? \
        (i)->var.xres : (i)->var.xres_virtual; })

/*
 * The bitmap is always big endian
 */
#if defined(__LITTLE_ENDIAN)
#define FBCON_BIT(b) (7 - (b))
#else
#define FBCON_BIT(b) (b)
#endif

static inline int pattern_test_bit(u32 x, u32 y, u32 pitch, const char *pat)
{
	u32 tmp = (y * pitch) + x, index = tmp / 8,  bit = tmp % 8;

	pat +=index;
	return (test_bit(FBCON_BIT(bit), (void *)pat));
}

static inline void pattern_set_bit(u32 x, u32 y, u32 pitch, char *pat)
{
	u32 tmp = (y * pitch) + x, index = tmp / 8, bit = tmp % 8;

	pat += index;
	set_bit(FBCON_BIT(bit), (void *)pat);
}

static inline void rotate_ud(const char *in, char *out, u32 width, u32 height)
{
	int i, j;
	int shift = width % 8;

	width = (width + 7) & ~7;

	for (i = 0; i < height; i++) {
		for (j = 0; j < width; j++) {
			if (pattern_test_bit(j, i, width, in))
				pattern_set_bit(width - (1 + j + shift),
						height - (1 + i),
						width, out);
		}

	}
}

static inline void rotate_cw(const char *in, char *out, u32 width, u32 height)
{
	int i, j, h = height, w = width;
	int shift = (8 - (height % 8)) & 7;

	width = (width + 7) & ~7;
	height = (height + 7) & ~7;

	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			if (pattern_test_bit(j, i, width, in))
				pattern_set_bit(height - 1 - i - shift, j,
						height, out);

		}
	}
}

static inline void rotate_ccw(const char *in, char *out, u32 width, u32 height)
{
	int i, j, h = height, w = width;
	int shift = width % 8;

	width = (width + 7) & ~7;
	height = (height + 7) & ~7;

	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			if (pattern_test_bit(j, i, width, in))
				pattern_set_bit(i, width - 1 - j - shift,
						height, out);
		}
	}
}

extern void fbcon_rotate_cw(struct fbcon_ops *ops);
extern void fbcon_rotate_ud(struct fbcon_ops *ops);
extern void fbcon_rotate_ccw(struct fbcon_ops *ops);
#endif
