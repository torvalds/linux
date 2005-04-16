/*
 *  linux/drivers/video/console/tileblit.c -- Tile Blitting Operation
 *
 *      Copyright (C) 2004 Antonino Daplas <adaplas @pol.net>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/config.h>
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
		       int sx, int height, int width)
{
	struct fb_tilerect rect;
	int bgshift = (vc->vc_hi_font_mask) ? 13 : 12;
	int fgshift = (vc->vc_hi_font_mask) ? 9 : 8;

	rect.index = vc->vc_video_erase_char &
		((vc->vc_hi_font_mask) ? 0x1ff : 0xff);
	rect.fg = attr_fgcol_ec(fgshift, vc);
	rect.bg = attr_bgcol_ec(bgshift, vc);
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
			       int bottom_only)
{
	return;
}

static void tile_cursor(struct vc_data *vc, struct fb_info *info,
			struct display *p, int mode, int softback_lines,
			int fg, int bg)
{
	struct fb_tilecursor cursor;
	int use_sw = (vc->vc_cursor_type & 0x01);

	cursor.sx = vc->vc_x;
	cursor.sy = vc->vc_y;
	cursor.mode = (mode == CM_ERASE || use_sw) ? 0 : 1;
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

void fbcon_set_tileops(struct vc_data *vc, struct fb_info *info,
		       struct display *p, struct fbcon_ops *ops)
{
	struct fb_tilemap map;

	ops->bmove = tile_bmove;
	ops->clear = tile_clear;
	ops->putcs = tile_putcs;
	ops->clear_margins = tile_clear_margins;
	ops->cursor = tile_cursor;

	if (p) {
		map.width = vc->vc_font.width;
		map.height = vc->vc_font.height;
		map.depth = 1;
		map.length = (p->userfont) ?
			FNTCHARCNT(p->fontdata) : 256;
		map.data = p->fontdata;
		info->tileops->fb_settile(info, &map);
	}
}

EXPORT_SYMBOL(fbcon_set_tileops);

MODULE_AUTHOR("Antonino Daplas <adaplas@pol.net>");
MODULE_DESCRIPTION("Tile Blitting Operation");
MODULE_LICENSE("GPL");

