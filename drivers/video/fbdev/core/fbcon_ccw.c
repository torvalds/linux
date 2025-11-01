/*
 *  linux/drivers/video/console/fbcon_ccw.c -- Software Rotation - 270 degrees
 *
 *      Copyright (C) 2005 Antonino Daplas <adaplas @pol.net>
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
#include "fbcon_rotate.h"

/*
 * Rotation 270 degrees
 */

static void ccw_update_attr(u8 *dst, u8 *src, int attribute,
				  struct vc_data *vc)
{
	int i, j, offset = (vc->vc_font.height < 10) ? 1 : 2;
	int width = (vc->vc_font.height + 7) >> 3;
	int mod = vc->vc_font.height % 8;
	u8 c, msk = ~(0xff << offset), msk1 = 0;

	if (mod)
		msk <<= (8 - mod);

	if (offset > mod)
		msk1 |= 0x01;

	for (i = 0; i < vc->vc_font.width; i++) {
		for (j = 0; j < width; j++) {
			c = *src;

			if (attribute & FBCON_ATTRIBUTE_UNDERLINE) {
				if (j == width - 1)
					c |= msk;

				if (msk1 && j == width - 2)
					c |= msk1;
			}

			if (attribute & FBCON_ATTRIBUTE_BOLD && i)
				*(dst - width) |= c;

			if (attribute & FBCON_ATTRIBUTE_REVERSE)
				c = ~c;
			src++;
			*dst++ = c;
		}
	}
}


static void ccw_bmove(struct vc_data *vc, struct fb_info *info, int sy,
		     int sx, int dy, int dx, int height, int width)
{
	struct fbcon_par *par = info->fbcon_par;
	struct fb_copyarea area;
	u32 vyres = GETVYRES(par->p, info);

	area.sx = sy * vc->vc_font.height;
	area.sy = vyres - ((sx + width) * vc->vc_font.width);
	area.dx = dy * vc->vc_font.height;
	area.dy = vyres - ((dx + width) * vc->vc_font.width);
	area.width = height * vc->vc_font.height;
	area.height  = width * vc->vc_font.width;

	info->fbops->fb_copyarea(info, &area);
}

static void ccw_clear(struct vc_data *vc, struct fb_info *info, int sy,
		     int sx, int height, int width, int fg, int bg)
{
	struct fbcon_par *par = info->fbcon_par;
	struct fb_fillrect region;
	u32 vyres = GETVYRES(par->p, info);

	region.color = bg;
	region.dx = sy * vc->vc_font.height;
	region.dy = vyres - ((sx + width) * vc->vc_font.width);
	region.height = width * vc->vc_font.width;
	region.width = height * vc->vc_font.height;
	region.rop = ROP_COPY;

	info->fbops->fb_fillrect(info, &region);
}

static inline void ccw_putcs_aligned(struct vc_data *vc, struct fb_info *info,
				    const u16 *s, u32 attr, u32 cnt,
				    u32 d_pitch, u32 s_pitch, u32 cellsize,
				    struct fb_image *image, u8 *buf, u8 *dst)
{
	struct fbcon_par *par = info->fbcon_par;
	u16 charmask = vc->vc_hi_font_mask ? 0x1ff : 0xff;
	u32 idx = (vc->vc_font.height + 7) >> 3;
	u8 *src;

	while (cnt--) {
		src = par->fontbuffer + (scr_readw(s--) & charmask) * cellsize;

		if (attr) {
			ccw_update_attr(buf, src, attr, vc);
			src = buf;
		}

		if (likely(idx == 1))
			__fb_pad_aligned_buffer(dst, d_pitch, src, idx,
						vc->vc_font.width);
		else
			fb_pad_aligned_buffer(dst, d_pitch, src, idx,
					      vc->vc_font.width);

		dst += d_pitch * vc->vc_font.width;
	}

	info->fbops->fb_imageblit(info, image);
}

static void ccw_putcs(struct vc_data *vc, struct fb_info *info,
		      const unsigned short *s, int count, int yy, int xx,
		      int fg, int bg)
{
	struct fb_image image;
	struct fbcon_par *par = info->fbcon_par;
	u32 width = (vc->vc_font.height + 7)/8;
	u32 cellsize = width * vc->vc_font.width;
	u32 maxcnt = info->pixmap.size/cellsize;
	u32 scan_align = info->pixmap.scan_align - 1;
	u32 buf_align = info->pixmap.buf_align - 1;
	u32 cnt, pitch, size;
	u32 attribute = get_attribute(info, scr_readw(s));
	u8 *dst, *buf = NULL;
	u32 vyres = GETVYRES(par->p, info);

	if (!par->fontbuffer)
		return;

	image.fg_color = fg;
	image.bg_color = bg;
	image.dx = yy * vc->vc_font.height;
	image.dy = vyres - ((xx + count) * vc->vc_font.width);
	image.width = vc->vc_font.height;
	image.depth = 1;

	if (attribute) {
		buf = kmalloc(cellsize, GFP_KERNEL);
		if (!buf)
			return;
	}

	s += count - 1;

	while (count) {
		if (count > maxcnt)
			cnt = maxcnt;
		else
			cnt = count;

		image.height = vc->vc_font.width * cnt;
		pitch = ((image.width + 7) >> 3) + scan_align;
		pitch &= ~scan_align;
		size = pitch * image.height + buf_align;
		size &= ~buf_align;
		dst = fb_get_buffer_offset(info, &info->pixmap, size);
		image.data = dst;
		ccw_putcs_aligned(vc, info, s, attribute, cnt, pitch,
				 width, cellsize, &image, buf, dst);
		image.dy += image.height;
		count -= cnt;
		s -= cnt;
	}

	/* buf is always NULL except when in monochrome mode, so in this case
	   it's a gain to check buf against NULL even though kfree() handles
	   NULL pointers just fine */
	if (unlikely(buf))
		kfree(buf);

}

static void ccw_clear_margins(struct vc_data *vc, struct fb_info *info,
			      int color, int bottom_only)
{
	unsigned int cw = vc->vc_font.width;
	unsigned int ch = vc->vc_font.height;
	unsigned int rw = info->var.yres - (vc->vc_cols*cw);
	unsigned int bh = info->var.xres - (vc->vc_rows*ch);
	unsigned int bs = vc->vc_rows*ch;
	struct fb_fillrect region;

	region.color = color;
	region.rop = ROP_COPY;

	if ((int) rw > 0 && !bottom_only) {
		region.dx = 0;
		region.dy = info->var.yoffset;
		region.height = rw;
		region.width = info->var.xres_virtual;
		info->fbops->fb_fillrect(info, &region);
	}

	if ((int) bh > 0) {
		region.dx = info->var.xoffset + bs;
		region.dy = 0;
                region.height = info->var.yres_virtual;
                region.width = bh;
		info->fbops->fb_fillrect(info, &region);
	}
}

static void ccw_cursor(struct vc_data *vc, struct fb_info *info, bool enable,
		       int fg, int bg)
{
	struct fb_cursor cursor;
	struct fbcon_par *par = info->fbcon_par;
	unsigned short charmask = vc->vc_hi_font_mask ? 0x1ff : 0xff;
	int w = (vc->vc_font.height + 7) >> 3, c;
	int y = real_y(par->p, vc->state.y);
	int attribute, use_sw = vc->vc_cursor_type & CUR_SW;
	int err = 1, dx, dy;
	char *src;
	u32 vyres = GETVYRES(par->p, info);

	if (!par->fontbuffer)
		return;

	cursor.set = 0;

 	c = scr_readw((u16 *) vc->vc_pos);
	attribute = get_attribute(info, c);
	src = par->fontbuffer + ((c & charmask) * (w * vc->vc_font.width));

	if (par->cursor_state.image.data != src ||
	    par->cursor_reset) {
		par->cursor_state.image.data = src;
		cursor.set |= FB_CUR_SETIMAGE;
	}

	if (attribute) {
		u8 *dst;

		dst = kmalloc_array(w, vc->vc_font.width, GFP_ATOMIC);
		if (!dst)
			return;
		kfree(par->cursor_data);
		par->cursor_data = dst;
		ccw_update_attr(dst, src, attribute, vc);
		src = dst;
	}

	if (par->cursor_state.image.fg_color != fg ||
	    par->cursor_state.image.bg_color != bg ||
	    par->cursor_reset) {
		par->cursor_state.image.fg_color = fg;
		par->cursor_state.image.bg_color = bg;
		cursor.set |= FB_CUR_SETCMAP;
	}

	if (par->cursor_state.image.height != vc->vc_font.width ||
	    par->cursor_state.image.width != vc->vc_font.height ||
	    par->cursor_reset) {
		par->cursor_state.image.height = vc->vc_font.width;
		par->cursor_state.image.width = vc->vc_font.height;
		cursor.set |= FB_CUR_SETSIZE;
	}

	dx = y * vc->vc_font.height;
	dy = vyres - ((vc->state.x + 1) * vc->vc_font.width);

	if (par->cursor_state.image.dx != dx ||
	    par->cursor_state.image.dy != dy ||
	    par->cursor_reset) {
		par->cursor_state.image.dx = dx;
		par->cursor_state.image.dy = dy;
		cursor.set |= FB_CUR_SETPOS;
	}

	if (par->cursor_state.hot.x || par->cursor_state.hot.y ||
	    par->cursor_reset) {
		par->cursor_state.hot.x = cursor.hot.y = 0;
		cursor.set |= FB_CUR_SETHOT;
	}

	if (cursor.set & FB_CUR_SETSIZE ||
	    vc->vc_cursor_type != par->p->cursor_shape ||
	    par->cursor_state.mask == NULL ||
	    par->cursor_reset) {
		char *tmp, *mask = kmalloc_array(w, vc->vc_font.width,
						 GFP_ATOMIC);
		int cur_height, size, i = 0;
		int width = (vc->vc_font.width + 7)/8;

		if (!mask)
			return;

		tmp = kmalloc_array(width, vc->vc_font.height, GFP_ATOMIC);

		if (!tmp) {
			kfree(mask);
			return;
		}

		kfree(par->cursor_state.mask);
		par->cursor_state.mask = mask;

		par->p->cursor_shape = vc->vc_cursor_type;
		cursor.set |= FB_CUR_SETSHAPE;

		switch (CUR_SIZE(par->p->cursor_shape)) {
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

		size = (vc->vc_font.height - cur_height) * width;
		while (size--)
			tmp[i++] = 0;
		size = cur_height * width;
		while (size--)
			tmp[i++] = 0xff;
		memset(mask, 0, w * vc->vc_font.width);
		rotate_ccw(tmp, mask, vc->vc_font.width, vc->vc_font.height);
		kfree(tmp);
	}

	par->cursor_state.enable = enable && !use_sw;

	cursor.image.data = src;
	cursor.image.fg_color = par->cursor_state.image.fg_color;
	cursor.image.bg_color = par->cursor_state.image.bg_color;
	cursor.image.dx = par->cursor_state.image.dx;
	cursor.image.dy = par->cursor_state.image.dy;
	cursor.image.height = par->cursor_state.image.height;
	cursor.image.width = par->cursor_state.image.width;
	cursor.hot.x = par->cursor_state.hot.x;
	cursor.hot.y = par->cursor_state.hot.y;
	cursor.mask = par->cursor_state.mask;
	cursor.enable = par->cursor_state.enable;
	cursor.image.depth = 1;
	cursor.rop = ROP_XOR;

	if (info->fbops->fb_cursor)
		err = info->fbops->fb_cursor(info, &cursor);

	if (err)
		soft_cursor(info, &cursor);

	par->cursor_reset = 0;
}

static int ccw_update_start(struct fb_info *info)
{
	struct fbcon_par *par = info->fbcon_par;
	u32 yoffset;
	u32 vyres = GETVYRES(par->p, info);
	int err;

	yoffset = (vyres - info->var.yres) - par->var.xoffset;
	par->var.xoffset = par->var.yoffset;
	par->var.yoffset = yoffset;
	err = fb_pan_display(info, &par->var);
	par->var.xoffset = info->var.xoffset;
	par->var.yoffset = info->var.yoffset;
	par->var.vmode = info->var.vmode;
	return err;
}

static const struct fbcon_bitops ccw_fbcon_bitops = {
	.bmove = ccw_bmove,
	.clear = ccw_clear,
	.putcs = ccw_putcs,
	.clear_margins = ccw_clear_margins,
	.cursor = ccw_cursor,
	.update_start = ccw_update_start,
	.rotate_font = fbcon_rotate_font,
};

void fbcon_set_bitops_ccw(struct fbcon_par *par)
{
	par->bitops = &ccw_fbcon_bitops;
}
