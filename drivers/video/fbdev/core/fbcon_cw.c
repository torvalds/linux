/*
 *  linux/drivers/video/console/fbcon_ud.c -- Software Rotation - 90 degrees
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
 * Rotation 90 degrees
 */

static void cw_update_attr(u8 *dst, u8 *src, int attribute,
				  struct vc_data *vc)
{
	int i, j, offset = (vc->vc_font.height < 10) ? 1 : 2;
	int width = (vc->vc_font.height + 7) >> 3;
	u8 c, msk = ~(0xff >> offset);

	for (i = 0; i < vc->vc_font.width; i++) {
		for (j = 0; j < width; j++) {
			c = *src;
			if (attribute & FBCON_ATTRIBUTE_UNDERLINE && !j)
				c |= msk;
			if (attribute & FBCON_ATTRIBUTE_BOLD && i)
				c |= *(src-width);
			if (attribute & FBCON_ATTRIBUTE_REVERSE)
				c = ~c;
			src++;
			*dst++ = c;
		}
	}
}


static void cw_bmove(struct vc_data *vc, struct fb_info *info, int sy,
		     int sx, int dy, int dx, int height, int width)
{
	struct fbcon_ops *ops = info->fbcon_par;
	struct fb_copyarea area;
	u32 vxres = GETVXRES(ops->p, info);

	area.sx = vxres - ((sy + height) * vc->vc_font.height);
	area.sy = sx * vc->vc_font.width;
	area.dx = vxres - ((dy + height) * vc->vc_font.height);
	area.dy = dx * vc->vc_font.width;
	area.width = height * vc->vc_font.height;
	area.height  = width * vc->vc_font.width;

	info->fbops->fb_copyarea(info, &area);
}

static void cw_clear(struct vc_data *vc, struct fb_info *info, int sy,
		     int sx, int height, int width)
{
	struct fbcon_ops *ops = info->fbcon_par;
	struct fb_fillrect region;
	int bgshift = (vc->vc_hi_font_mask) ? 13 : 12;
	u32 vxres = GETVXRES(ops->p, info);

	region.color = attr_bgcol_ec(bgshift,vc,info);
	region.dx = vxres - ((sy + height) * vc->vc_font.height);
	region.dy = sx *  vc->vc_font.width;
	region.height = width * vc->vc_font.width;
	region.width = height * vc->vc_font.height;
	region.rop = ROP_COPY;

	info->fbops->fb_fillrect(info, &region);
}

static inline void cw_putcs_aligned(struct vc_data *vc, struct fb_info *info,
				    const u16 *s, u32 attr, u32 cnt,
				    u32 d_pitch, u32 s_pitch, u32 cellsize,
				    struct fb_image *image, u8 *buf, u8 *dst)
{
	struct fbcon_ops *ops = info->fbcon_par;
	u16 charmask = vc->vc_hi_font_mask ? 0x1ff : 0xff;
	u32 idx = (vc->vc_font.height + 7) >> 3;
	u8 *src;

	while (cnt--) {
		src = ops->fontbuffer + (scr_readw(s++) & charmask)*cellsize;

		if (attr) {
			cw_update_attr(buf, src, attr, vc);
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

static void cw_putcs(struct vc_data *vc, struct fb_info *info,
		      const unsigned short *s, int count, int yy, int xx,
		      int fg, int bg)
{
	struct fb_image image;
	struct fbcon_ops *ops = info->fbcon_par;
	u32 width = (vc->vc_font.height + 7)/8;
	u32 cellsize = width * vc->vc_font.width;
	u32 maxcnt = info->pixmap.size/cellsize;
	u32 scan_align = info->pixmap.scan_align - 1;
	u32 buf_align = info->pixmap.buf_align - 1;
	u32 cnt, pitch, size;
	u32 attribute = get_attribute(info, scr_readw(s));
	u8 *dst, *buf = NULL;
	u32 vxres = GETVXRES(ops->p, info);

	if (!ops->fontbuffer)
		return;

	image.fg_color = fg;
	image.bg_color = bg;
	image.dx = vxres - ((yy + 1) * vc->vc_font.height);
	image.dy = xx * vc->vc_font.width;
	image.width = vc->vc_font.height;
	image.depth = 1;

	if (attribute) {
		buf = kmalloc(cellsize, GFP_KERNEL);
		if (!buf)
			return;
	}

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
		cw_putcs_aligned(vc, info, s, attribute, cnt, pitch,
				 width, cellsize, &image, buf, dst);
		image.dy += image.height;
		count -= cnt;
		s += cnt;
	}

	/* buf is always NULL except when in monochrome mode, so in this case
	   it's a gain to check buf against NULL even though kfree() handles
	   NULL pointers just fine */
	if (unlikely(buf))
		kfree(buf);

}

static void cw_clear_margins(struct vc_data *vc, struct fb_info *info,
			     int color, int bottom_only)
{
	unsigned int cw = vc->vc_font.width;
	unsigned int ch = vc->vc_font.height;
	unsigned int rw = info->var.yres - (vc->vc_cols*cw);
	unsigned int bh = info->var.xres - (vc->vc_rows*ch);
	unsigned int rs = info->var.yres - rw;
	struct fb_fillrect region;

	region.color = color;
	region.rop = ROP_COPY;

	if ((int) rw > 0 && !bottom_only) {
		region.dx = 0;
		region.dy = info->var.yoffset + rs;
		region.height = rw;
		region.width = info->var.xres_virtual;
		info->fbops->fb_fillrect(info, &region);
	}

	if ((int) bh > 0) {
		region.dx = info->var.xoffset;
		region.dy = info->var.yoffset;
                region.height = info->var.yres;
                region.width = bh;
		info->fbops->fb_fillrect(info, &region);
	}
}

static void cw_cursor(struct vc_data *vc, struct fb_info *info, bool enable,
		      int fg, int bg)
{
	struct fb_cursor cursor;
	struct fbcon_ops *ops = info->fbcon_par;
	unsigned short charmask = vc->vc_hi_font_mask ? 0x1ff : 0xff;
	int w = (vc->vc_font.height + 7) >> 3, c;
	int y = real_y(ops->p, vc->state.y);
	int attribute, use_sw = vc->vc_cursor_type & CUR_SW;
	int err = 1, dx, dy;
	char *src;
	u32 vxres = GETVXRES(ops->p, info);

	if (!ops->fontbuffer)
		return;

	cursor.set = 0;

 	c = scr_readw((u16 *) vc->vc_pos);
	attribute = get_attribute(info, c);
	src = ops->fontbuffer + ((c & charmask) * (w * vc->vc_font.width));

	if (ops->cursor_state.image.data != src ||
	    ops->cursor_reset) {
	    ops->cursor_state.image.data = src;
	    cursor.set |= FB_CUR_SETIMAGE;
	}

	if (attribute) {
		u8 *dst;

		dst = kmalloc_array(w, vc->vc_font.width, GFP_ATOMIC);
		if (!dst)
			return;
		kfree(ops->cursor_data);
		ops->cursor_data = dst;
		cw_update_attr(dst, src, attribute, vc);
		src = dst;
	}

	if (ops->cursor_state.image.fg_color != fg ||
	    ops->cursor_state.image.bg_color != bg ||
	    ops->cursor_reset) {
		ops->cursor_state.image.fg_color = fg;
		ops->cursor_state.image.bg_color = bg;
		cursor.set |= FB_CUR_SETCMAP;
	}

	if (ops->cursor_state.image.height != vc->vc_font.width ||
	    ops->cursor_state.image.width != vc->vc_font.height ||
	    ops->cursor_reset) {
		ops->cursor_state.image.height = vc->vc_font.width;
		ops->cursor_state.image.width = vc->vc_font.height;
		cursor.set |= FB_CUR_SETSIZE;
	}

	dx = vxres - ((y * vc->vc_font.height) + vc->vc_font.height);
	dy = vc->state.x * vc->vc_font.width;

	if (ops->cursor_state.image.dx != dx ||
	    ops->cursor_state.image.dy != dy ||
	    ops->cursor_reset) {
		ops->cursor_state.image.dx = dx;
		ops->cursor_state.image.dy = dy;
		cursor.set |= FB_CUR_SETPOS;
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

		kfree(ops->cursor_state.mask);
		ops->cursor_state.mask = mask;

		ops->p->cursor_shape = vc->vc_cursor_type;
		cursor.set |= FB_CUR_SETSHAPE;

		switch (CUR_SIZE(ops->p->cursor_shape)) {
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
		rotate_cw(tmp, mask, vc->vc_font.width, vc->vc_font.height);
		kfree(tmp);
	}

	ops->cursor_state.enable = enable && !use_sw;

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

static int cw_update_start(struct fb_info *info)
{
	struct fbcon_ops *ops = info->fbcon_par;
	u32 vxres = GETVXRES(ops->p, info);
	u32 xoffset;
	int err;

	xoffset = vxres - (info->var.xres + ops->var.yoffset);
	ops->var.yoffset = ops->var.xoffset;
	ops->var.xoffset = xoffset;
	err = fb_pan_display(info, &ops->var);
	ops->var.xoffset = info->var.xoffset;
	ops->var.yoffset = info->var.yoffset;
	ops->var.vmode = info->var.vmode;
	return err;
}

void fbcon_rotate_cw(struct fbcon_ops *ops)
{
	ops->bmove = cw_bmove;
	ops->clear = cw_clear;
	ops->putcs = cw_putcs;
	ops->clear_margins = cw_clear_margins;
	ops->cursor = cw_cursor;
	ops->update_start = cw_update_start;
}
