/*
 *  linux/drivers/video/console/tileblit.c -- Tile Blitting Operation
 *
 *      Copyright (C) 2004 Antonino Daplas <adaplas @pol.net>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/fb.h>
#include <linux/vt_kern.h>
#include <linux/console.h>
#include <asm/types.h>
#include "fbcon.h"

static void tile_bmove(struct vc_data *vc, struct fb_info *info, int sy,
		       int sx, int dy, int dx, int height, int width)
{
	struct fb_tilearea area;

	area.sx = sx;
	area.sy = sy;
	area.dx = dx;
	area.dy = dy;
	area.height = height;
	area.width = width;

	info->tileops->fb_tilecopy(info, &area);
}

static void tile_clear(struct vc_data *vc, struct fb_info *info, int sy,
		       int sx, int height, int width, int fg, int bg)
{
	struct fb_tilerect rect;

	rect.index = vc->vc_video_erase_char &
		((vc->vc_hi_font_mask) ? 0x1ff : 0xff);
	rect.fg = fg;
	rect.bg = bg;
	rect.sx = sx;
	rect.sy = sy;
	rect.width = width;
	rect.height = height;
	rect.rop = ROP_COPY;

	info->tileops->fb_tilefill(info, &rect);
}

static void tile_putcs(struct vc_data *vc, struct fb_info *info,
		       const unsigned short *s, int count, int yy, int xx,
		       int fg, int bg)
{
	struct fb_tileblit blit;
	unsigned short charmask = vc->vc_hi_font_mask ? 0x1ff : 0xff;
	int size = sizeof(u32) * count, i;

	blit.sx = xx;
	blit.sy = yy;
	blit.width = count;
	blit.height = 1;
	blit.fg = fg;
	blit.bg = bg;
	blit.length = count;
	blit.indices = (u32 *) fb_get_buffer_offset(info, &info->pixmap, size);
	for (i = 0; i < count; i++)
		blit.indices[i] = (u32)(scr_readw(s++) & charmask);

	info->tileops->fb_tileblit(info, &blit);
}

static void tile_clear_margins(struct vc_data *vc, struct fb_info *info,
			       int color, int bottom_only)
{
	unsigned int cw = vc->vc_font.width;
	unsigned int ch = vc->vc_font.height;
	unsigned int rw = info->var.xres - (vc->vc_cols*cw);
	unsigned int bh = info->var.yres - (vc->vc_rows*ch);
	unsigned int rs = info->var.xres - rw;
	unsigned int bs = info->var.yres - bh;
	unsigned int vwt = info->var.xres_virtual / cw;
	unsigned int vht = info->var.yres_virtual / ch;
	struct fb_tilerect rect;

	rect.index = vc->vc_video_erase_char &
		((vc->vc_hi_font_mask) ? 0x1ff : 0xff);
	rect.fg = color;
	rect.bg = color;

	if ((int) rw > 0 && !bottom_only) {
		rect.sx = (info->var.xoffset + rs + cw - 1) / cw;
		rect.sy = 0;
		rect.width = (rw + cw - 1) / cw;
		rect.height = vht;
		if (rect.width + rect.sx > vwt)
			rect.width = vwt - rect.sx;
		if (rect.sx < vwt)
			info->tileops->fb_tilefill(info, &rect);
	}

	if ((int) bh > 0) {
		rect.sx = info->var.xoffset / cw;
		rect.sy = (info->var.yoffset + bs) / ch;
		rect.width = rs / cw;
		rect.height = (bh + ch - 1) / ch;
		if (rect.height + rect.sy > vht)
			rect.height = vht - rect.sy;
		if (rect.sy < vht)
			info->tileops->fb_tilefill(info, &rect);
	}
}

static void tile_cursor(struct vc_data *vc, struct fb_info *info, bool enable,
			int fg, int bg)
{
	struct fb_tilecursor cursor;
	int use_sw = vc->vc_cursor_type & CUR_SW;

	cursor.sx = vc->state.x;
	cursor.sy = vc->state.y;
	cursor.mode = enable && !use_sw;
	cursor.fg = fg;
	cursor.bg = bg;

	switch (vc->vc_cursor_type & 0x0f) {
	case CUR_NONE:
		cursor.shape = FB_TILE_CURSOR_NONE;
		break;
	case CUR_UNDERLINE:
		cursor.shape = FB_TILE_CURSOR_UNDERLINE;
		break;
	case CUR_LOWER_THIRD:
		cursor.shape = FB_TILE_CURSOR_LOWER_THIRD;
		break;
	case CUR_LOWER_HALF:
		cursor.shape = FB_TILE_CURSOR_LOWER_HALF;
		break;
	case CUR_TWO_THIRDS:
		cursor.shape = FB_TILE_CURSOR_TWO_THIRDS;
		break;
	case CUR_BLOCK:
	default:
		cursor.shape = FB_TILE_CURSOR_BLOCK;
		break;
	}

	info->tileops->fb_tilecursor(info, &cursor);
}

static int tile_update_start(struct fb_info *info)
{
	struct fbcon_ops *ops = info->fbcon_par;
	int err;

	err = fb_pan_display(info, &ops->var);
	ops->var.xoffset = info->var.xoffset;
	ops->var.yoffset = info->var.yoffset;
	ops->var.vmode = info->var.vmode;
	return err;
}

void fbcon_set_tileops(struct vc_data *vc, struct fb_info *info)
{
	struct fb_tilemap map;
	struct fbcon_ops *ops = info->fbcon_par;

	ops->bmove = tile_bmove;
	ops->clear = tile_clear;
	ops->putcs = tile_putcs;
	ops->clear_margins = tile_clear_margins;
	ops->cursor = tile_cursor;
	ops->update_start = tile_update_start;

	if (ops->p) {
		map.width = vc->vc_font.width;
		map.height = vc->vc_font.height;
		map.depth = 1;
		map.length = vc->vc_font.charcount;
		map.data = ops->p->fontdata;
		info->tileops->fb_settile(info, &map);
	}
}
