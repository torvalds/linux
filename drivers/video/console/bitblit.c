/*
 *  linux/drivers/video/console/bitblit.c -- BitBlitting Operation
 *
 *  Originally from the 'accel_*' routines in drivers/video/console/fbcon.c
 *
 *      Copyright (C) 2004 Antonino Daplas <adaplas @pol.net>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/fb.h>
#include <linux/vt_kern.h>
#include <linux/console.h>
#include <asm/types.h>
#include "fbcon.h"

/*
 * Accelerated handlers.
 */
static void update_attr(u8 *dst, u8 *src, int attribute,
			       struct vc_data *vc)
{
	int i, offset = (vc->vc_font.height < 10) ? 1 : 2;
	int width = DIV_ROUND_UP(vc->vc_font.width, 8);
	unsigned int cellsize = vc->vc_font.height * width;
	u8 c;

	offset = cellsize - (offset * width);
	for (i = 0; i < cellsize; i++) {
		c = src[i];
		if (attribute & FBCON_ATTRIBUTE_UNDERLINE && i >= offset)
			c = 0xff;
		if (attribute & FBCON_ATTRIBUTE_BOLD)
			c |= c >> 1;
		if (attribute & FBCON_ATTRIBUTE_REVERSE)
			c = ~c;
		dst[i] = c;
	}
}

static void bit_bmove(struct vc_data *vc, struct fb_info *info, int sy,
		      int sx, int dy, int dx, int height, int width)
{
	struct fb_copyarea area;

	area.sx = sx * vc->vc_font.width;
	area.sy = sy * vc->vc_font.height;
	area.dx = dx * vc->vc_font.width;
	area.dy = dy * vc->vc_font.height;
	area.height = height * vc->vc_font.height;
	area.width = width * vc->vc_font.width;

	info->fbops->fb_copyarea(info, &area);
}

static void bit_clear(struct vc_data *vc, struct fb_info *info, int sy,
		      int sx, int height, int width)
{
	int bgshift = (vc->vc_hi_font_mask) ? 13 : 12;
	struct fb_fillrect region;

	region.color = attr_bgcol_ec(bgshift, vc, info);
	region.dx = sx * vc->vc_font.width;
	region.dy = sy * vc->vc_font.height;
	region.width = width * vc->vc_font.width;
	region.height = height * vc->vc_font.height;
	region.rop = ROP_COPY;

	info->fbops->fb_fillrect(info, &region);
}

static inline void bit_putcs_aligned(struct vc_data *vc, struct fb_info *info,
				     const u16 *s, u32 attr, u32 cnt,
				     u32 d_pitch, u32 s_pitch, u32 cellsize,
				     struct fb_image *image, u8 *buf, u8 *dst)
{
	u16 charmask = vc->vc_hi_font_mask ? 0x1ff : 0xff;
	u32 idx = vc->vc_font.width >> 3;
	u8 *src;

	while (cnt--) {
		src = vc->vc_font.data + (scr_readw(s++)&
					  charmask)*cellsize;

		if (attr) {
			update_attr(buf, src, attr, vc);
			src = buf;
		}

		if (likely(idx == 1))
			__fb_pad_aligned_buffer(dst, d_pitch, src, idx,
						image->height);
		else
			fb_pad_aligned_buffer(dst, d_pitch, src, idx,
					      image->height);

		dst += s_pitch;
	}

	info->fbops->fb_imageblit(info, image);
}

static inline void bit_putcs_unaligned(struct vc_data *vc,
				       struct fb_info *info, const u16 *s,
				       u32 attr, u32 cnt, u32 d_pitch,
				       u32 s_pitch, u32 cellsize,
				       struct fb_image *image, u8 *buf,
				       u8 *dst)
{
	u16 charmask = vc->vc_hi_font_mask ? 0x1ff : 0xff;
	u32 shift_low = 0, mod = vc->vc_font.width % 8;
	u32 shift_high = 8;
	u32 idx = vc->vc_font.width >> 3;
	u8 *src;

	while (cnt--) {
		src = vc->vc_font.data + (scr_readw(s++)&
					  charmask)*cellsize;

		if (attr) {
			update_attr(buf, src, attr, vc);
			src = buf;
		}

		fb_pad_unaligned_buffer(dst, d_pitch, src, idx,
					image->height, shift_high,
					shift_low, mod);
		shift_low += mod;
		dst += (shift_low >= 8) ? s_pitch : s_pitch - 1;
		shift_low &= 7;
		shift_high = 8 - shift_low;
	}

	info->fbops->fb_imageblit(info, image);

}

static void bit_putcs(struct vc_data *vc, struct fb_info *info,
		      const unsigned short *s, int count, int yy, int xx,
		      int fg, int bg)
{
	struct fb_image image;
	u32 width = DIV_ROUND_UP(vc->vc_font.width, 8);
	u32 cellsize = width * vc->vc_font.height;
	u32 maxcnt = info->pixmap.size/cellsize;
	u32 scan_align = info->pixmap.scan_align - 1;
	u32 buf_align = info->pixmap.buf_align - 1;
	u32 mod = vc->vc_font.width % 8, cnt, pitch, size;
	u32 attribute = get_attribute(info, scr_readw(s));
	u8 *dst, *buf = NULL;

	image.fg_color = fg;
	image.bg_color = bg;
	image.dx = xx * vc->vc_font.width;
	image.dy = yy * vc->vc_font.height;
	image.height = vc->vc_font.height;
	image.depth = 1;

	if (attribute) {
		buf = kmalloc(cellsize, GFP_ATOMIC);
		if (!buf)
			return;
	}

	while (count) {
		if (count > maxcnt)
			cnt = maxcnt;
		else
			cnt = count;

		image.width = vc->vc_font.width * cnt;
		pitch = DIV_ROUND_UP(image.width, 8) + scan_align;
		pitch &= ~scan_align;
		size = pitch * image.height + buf_align;
		size &= ~buf_align;
		dst = fb_get_buffer_offset(info, &info->pixmap, size);
		image.data = dst;

		if (!mod)
			bit_putcs_aligned(vc, info, s, attribute, cnt, pitch,
					  width, cellsize, &image, buf, dst);
		else
			bit_putcs_unaligned(vc, info, s, attribute, cnt,
					    pitch, width, cellsize, &image,
					    buf, dst);

		image.dx += cnt * vc->vc_font.width;
		count -= cnt;
		s += cnt;
	}

	/* buf is always NULL except when in monochrome mode, so in this case
	   it's a gain to check buf against NULL even though kfree() handles
	   NULL pointers just fine */
	if (unlikely(buf))
		kfree(buf);

}

static void bit_clear_margins(struct vc_data *vc, struct fb_info *info,
			      int bottom_only)
{
	int bgshift = (vc->vc_hi_font_mask) ? 13 : 12;
	unsigned int cw = vc->vc_font.width;
	unsigned int ch = vc->vc_font.height;
	unsigned int rw = info->var.xres - (vc->vc_cols*cw);
	unsigned int bh = info->var.yres - (vc->vc_rows*ch);
	unsigned int rs = info->var.xres - rw;
	unsigned int bs = info->var.yres - bh;
	struct fb_fillrect region;

	region.color = attr_bgcol_ec(bgshift, vc, info);
	region.rop = ROP_COPY;

	if (rw && !bottom_only) {
		region.dx = info->var.xoffset + rs;
		region.dy = 0;
		region.width = rw;
		region.height = info->var.yres_virtual;
		info->fbops->fb_fillrect(info, &region);
	}

	if (bh) {
		region.dx = info->var.xoffset;
		region.dy = info->var.yoffset + bs;
		region.width = rs;
		region.height = bh;
		info->fbops->fb_fillrect(info, &region);
	}
}

static void bit_cursor(struct vc_data *vc, struct fb_info *info, int mode,
		       int softback_lines, int fg, int bg)
{
	struct fb_cursor cursor;
	struct fbcon_ops *ops = info->fbcon_par;
	unsigned short charmask = vc->vc_hi_font_mask ? 0x1ff : 0xff;
	int w = DIV_ROUND_UP(vc->vc_font.width, 8), c;
	int y = real_y(ops->p, vc->vc_y);
	int attribute, use_sw = (vc->vc_cursor_type & 0x10);
	int err = 1;
	char *src;

	cursor.set = 0;

	if (softback_lines) {
		if (y + softback_lines >= vc->vc_rows) {
			mode = CM_ERASE;
			ops->cursor_flash = 0;
			return;
		} else
			y += softback_lines;
	}

 	c = scr_readw((u16 *) vc->vc_pos);
	attribute = get_attribute(info, c);
	src = vc->vc_font.data + ((c & charmask) * (w * vc->vc_font.height));

	if (ops->cursor_state.image.data != src ||
	    ops->cursor_reset) {
	    ops->cursor_state.image.data = src;
	    cursor.set |= FB_CUR_SETIMAGE;
	}

	if (attribute) {
		u8 *dst;

		dst = kmalloc(w * vc->vc_font.height, GFP_ATOMIC);
		if (!dst)
			return;
		kfree(ops->cursor_data);
		ops->cursor_data = dst;
		update_attr(dst, src, attribute, vc);
		src = dst;
	}

	if (ops->cursor_state.image.fg_color != fg ||
	    ops->cursor_state.image.bg_color != bg ||
	    ops->cursor_reset) {
		ops->cursor_state.image.fg_color = fg;
		ops->cursor_state.image.bg_color = bg;
		cursor.set |= FB_CUR_SETCMAP;
	}

	if ((ops->cursor_state.image.dx != (vc->vc_font.width * vc->vc_x)) ||
	    (ops->cursor_state.image.dy != (vc->vc_font.height * y)) ||
	    ops->cursor_reset) {
		ops->cursor_state.image.dx = vc->vc_font.width * vc->vc_x;
		ops->cursor_state.image.dy = vc->vc_font.height * y;
		cursor.set |= FB_CUR_SETPOS;
	}

	if (ops->cursor_state.image.height != vc->vc_font.height ||
	    ops->cursor_state.image.width != vc->vc_font.width ||
	    ops->cursor_reset) {
		ops->cursor_state.image.height = vc->vc_font.height;
		ops->cursor_state.image.width = vc->vc_font.width;
		cursor.set |= FB_CUR_SETSIZE;
	}

	if (ops->cursor_state.hot.x || ops->cursor_state.hot.y ||
	    ops->cursor_reset) {
		ops->cursor_state.hot.x = cursor.hot.y = 0;
		cursor.set |= FB_CUR_SETHOT;
	}

	if (cursor.set & FB_CUR_SETSIZE ||
	    vc->vc_cursor_type != ops->p->cursor_shape ||
	    ops->cursor_state.mask == NULL ||
	    ops->cursor_reset) {
		char *mask = kmalloc(w*vc->vc_font.height, GFP_ATOMIC);
		int cur_height, size, i = 0;
		u8 msk = 0xff;

		if (!mask)
			return;

		kfree(ops->cursor_state.mask);
		ops->cursor_state.mask = mask;

		ops->p->cursor_shape = vc->vc_cursor_type;
		cursor.set |= FB_CUR_SETSHAPE;

		switch (ops->p->cursor_shape & CUR_HWMASK) {
		case CUR_NONE:
			cur_height = 0;
			break;
		case CUR_UNDERLINE:
			cur_height = (vc->vc_font.height < 10) ? 1 : 2;
			break;
		case CUR_LOWER_THIRD:
			cur_height = vc->vc_font.height/3;
			break;
		case CUR_LOWER_HALF:
			cur_height = vc->vc_font.height >> 1;
			break;
		case CUR_TWO_THIRDS:
			cur_height = (vc->vc_font.height << 1)/3;
			break;
		case CUR_BLOCK:
		default:
			cur_height = vc->vc_font.height;
			break;
		}
		size = (vc->vc_font.height - cur_height) * w;
		while (size--)
			mask[i++] = ~msk;
		size = cur_height * w;
		while (size--)
			mask[i++] = msk;
	}

	switch (mode) {
	case CM_ERASE:
		ops->cursor_state.enable = 0;
		break;
	case CM_DRAW:
	case CM_MOVE:
	default:
		ops->cursor_state.enable = (use_sw) ? 0 : 1;
		break;
	}

	cursor.image.data = src;
	cursor.image.fg_color = ops->cursor_state.image.fg_color;
	cursor.image.bg_color = ops->cursor_state.image.bg_color;
	cursor.image.dx = ops->cursor_state.image.dx;
	cursor.image.dy = ops->cursor_state.image.dy;
	cursor.image.height = ops->cursor_state.image.height;
	cursor.image.width = ops->cursor_state.image.width;
	cursor.hot.x = ops->cursor_state.hot.x;
	cursor.hot.y = ops->cursor_state.hot.y;
	cursor.mask = ops->cursor_state.mask;
	cursor.enable = ops->cursor_state.enable;
	cursor.image.depth = 1;
	cursor.rop = ROP_XOR;

	if (info->fbops->fb_cursor)
		err = info->fbops->fb_cursor(info, &cursor);

	if (err)
		soft_cursor(info, &cursor);

	ops->cursor_reset = 0;
}

static int bit_update_start(struct fb_info *info)
{
	struct fbcon_ops *ops = info->fbcon_par;
	int err;

	err = fb_pan_display(info, &ops->var);
	ops->var.xoffset = info->var.xoffset;
	ops->var.yoffset = info->var.yoffset;
	ops->var.vmode = info->var.vmode;
	return err;
}

void fbcon_set_bitops(struct fbcon_ops *ops)
{
	ops->bmove = bit_bmove;
	ops->clear = bit_clear;
	ops->putcs = bit_putcs;
	ops->clear_margins = bit_clear_margins;
	ops->cursor = bit_cursor;
	ops->update_start = bit_update_start;
	ops->rotate_font = NULL;

	if (ops->rotate)
		fbcon_set_rotate(ops);
}

EXPORT_SYMBOL(fbcon_set_bitops);

MODULE_AUTHOR("Antonino Daplas <adaplas@pol.net>");
MODULE_DESCRIPTION("Bit Blitting Operation");
MODULE_LICENSE("GPL");

