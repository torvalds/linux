/*
 *  linux/drivers/video/cfbcon_decor.c -- Framebuffer decor render functions
 *
 *  Copyright (C) 2004 Michal Januszewski <spock@gentoo.org>
 *
 *  Code based upon "Bootdecor" (C) 2001-2003
 *       Volker Poplawski <volker@poplawski.de>,
 *       Stefan Reinauer <stepan@suse.de>,
 *       Steffen Winterfeldt <snwint@suse.de>,
 *       Michael Schroeder <mls@suse.de>,
 *       Ken Wimer <wimer@suse.de>.
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fb.h>
#include <linux/selection.h>
#include <linux/slab.h>
#include <linux/vt_kern.h>
#include <asm/irq.h>

#include "fbcon.h"
#include "fbcondecor.h"

#define parse_pixel(shift,bpp,type)						\
	do {									\
		if (d & (0x80 >> (shift)))					\
			dd2[(shift)] = fgx;					\
		else								\
			dd2[(shift)] = transparent ? *(type *)decor_src : bgx;	\
		decor_src += (bpp);						\
	} while (0)								\

extern int get_color(struct vc_data *vc, struct fb_info *info,
		     u16 c, int is_fg);

void fbcon_decor_fix_pseudo_pal(struct fb_info *info, struct vc_data *vc)
{
	int i, j, k;
	int minlen = min(min(info->var.red.length, info->var.green.length),
			     info->var.blue.length);
	u32 col;

	for (j = i = 0; i < 16; i++) {
		k = color_table[i];

		col = ((vc->vc_palette[j++]  >> (8-minlen))
			<< info->var.red.offset);
		col |= ((vc->vc_palette[j++] >> (8-minlen))
			<< info->var.green.offset);
		col |= ((vc->vc_palette[j++] >> (8-minlen))
			<< info->var.blue.offset);
			((u32 *)info->pseudo_palette)[k] = col;
	}
}

void fbcon_decor_renderc(struct fb_info *info, int ypos, int xpos, int height,
		      int width, u8* src, u32 fgx, u32 bgx, u8 transparent)
{
	unsigned int x, y;
	u32 dd;
	int bytespp = ((info->var.bits_per_pixel + 7) >> 3);
	unsigned int d = ypos * info->fix.line_length + xpos * bytespp;
	unsigned int ds = (ypos * info->var.xres + xpos) * bytespp;
	u16 dd2[4];

	u8* decor_src = (u8 *)(info->bgdecor.data + ds);
	u8* dst = (u8 *)(info->screen_base + d);

	if ((ypos + height) > info->var.yres || (xpos + width) > info->var.xres)
		return;

	for (y = 0; y < height; y++) {
		switch (info->var.bits_per_pixel) {

		case 32:
			for (x = 0; x < width; x++) {

				if ((x & 7) == 0)
					d = *src++;
				if (d & 0x80)
					dd = fgx;
				else
					dd = transparent ?
					     *(u32 *)decor_src : bgx;

				d <<= 1;
				decor_src += 4;
				fb_writel(dd, dst);
				dst += 4;
			}
			break;
		case 24:
			for (x = 0; x < width; x++) {

				if ((x & 7) == 0)
					d = *src++;
				if (d & 0x80)
					dd = fgx;
				else
					dd = transparent ?
					     (*(u32 *)decor_src & 0xffffff) : bgx;

				d <<= 1;
				decor_src += 3;
#ifdef __LITTLE_ENDIAN
				fb_writew(dd & 0xffff, dst);
				dst += 2;
				fb_writeb((dd >> 16), dst);
#else
				fb_writew(dd >> 8, dst);
				dst += 2;
				fb_writeb(dd & 0xff, dst);
#endif
				dst++;
			}
			break;
		case 16:
			for (x = 0; x < width; x += 2) {
				if ((x & 7) == 0)
					d = *src++;

				parse_pixel(0, 2, u16);
				parse_pixel(1, 2, u16);
#ifdef __LITTLE_ENDIAN
				dd = dd2[0] | (dd2[1] << 16);
#else
				dd = dd2[1] | (dd2[0] << 16);
#endif
				d <<= 2;
				fb_writel(dd, dst);
				dst += 4;
			}
			break;

		case 8:
			for (x = 0; x < width; x += 4) {
				if ((x & 7) == 0)
					d = *src++;

				parse_pixel(0, 1, u8);
				parse_pixel(1, 1, u8);
				parse_pixel(2, 1, u8);
				parse_pixel(3, 1, u8);

#ifdef __LITTLE_ENDIAN
				dd = dd2[0] | (dd2[1] << 8) | (dd2[2] << 16) | (dd2[3] << 24);
#else
				dd = dd2[3] | (dd2[2] << 8) | (dd2[1] << 16) | (dd2[0] << 24);
#endif
				d <<= 4;
				fb_writel(dd, dst);
				dst += 4;
			}
		}

		dst += info->fix.line_length - width * bytespp;
		decor_src += (info->var.xres - width) * bytespp;
	}
}

#define cc2cx(a) 						\
	((info->fix.visual == FB_VISUAL_TRUECOLOR || 		\
	  info->fix.visual == FB_VISUAL_DIRECTCOLOR) ? 		\
	 ((u32*)info->pseudo_palette)[a] : a)

void fbcon_decor_putcs(struct vc_data *vc, struct fb_info *info,
		   const unsigned short *s, int count, int yy, int xx)
{
	unsigned short charmask = vc->vc_hi_font_mask ? 0x1ff : 0xff;
	struct fbcon_ops *ops = info->fbcon_par;
	int fg_color, bg_color, transparent;
	u8 *src;
	u32 bgx, fgx;
	u16 c = scr_readw(s);

	fg_color = get_color(vc, info, c, 1);
        bg_color = get_color(vc, info, c, 0);

	/* Don't paint the background image if console is blanked */
	transparent = ops->blank_state ? 0 :
		(vc->vc_decor.bg_color == bg_color);

	xx = xx * vc->vc_font.width + vc->vc_decor.tx;
	yy = yy * vc->vc_font.height + vc->vc_decor.ty;

	fgx = cc2cx(fg_color);
	bgx = cc2cx(bg_color);

	while (count--) {
		c = scr_readw(s++);
		src = vc->vc_font.data + (c & charmask) * vc->vc_font.height *
		      ((vc->vc_font.width + 7) >> 3);

		fbcon_decor_renderc(info, yy, xx, vc->vc_font.height,
			       vc->vc_font.width, src, fgx, bgx, transparent);
		xx += vc->vc_font.width;
	}
}

void fbcon_decor_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	int i;
	unsigned int dsize, s_pitch;
	struct fbcon_ops *ops = info->fbcon_par;
	struct vc_data* vc;
	u8 *src;

	/* we really don't need any cursors while the console is blanked */
	if (info->state != FBINFO_STATE_RUNNING || ops->blank_state)
		return;

	vc = vc_cons[ops->currcon].d;

	src = kmalloc(64 + sizeof(struct fb_image), GFP_ATOMIC);
	if (!src)
		return;

	s_pitch = (cursor->image.width + 7) >> 3;
	dsize = s_pitch * cursor->image.height;
	if (cursor->enable) {
		switch (cursor->rop) {
		case ROP_XOR:
			for (i = 0; i < dsize; i++)
				src[i] = cursor->image.data[i] ^ cursor->mask[i];
                        break;
		case ROP_COPY:
		default:
			for (i = 0; i < dsize; i++)
				src[i] = cursor->image.data[i] & cursor->mask[i];
			break;
		}
	} else
		memcpy(src, cursor->image.data, dsize);

	fbcon_decor_renderc(info,
			cursor->image.dy + vc->vc_decor.ty,
			cursor->image.dx + vc->vc_decor.tx,
			cursor->image.height,
			cursor->image.width,
			(u8*)src,
			cc2cx(cursor->image.fg_color),
			cc2cx(cursor->image.bg_color),
			cursor->image.bg_color == vc->vc_decor.bg_color);

	kfree(src);
}

static void decorset(u8 *dst, int height, int width, int dstbytes,
		        u32 bgx, int bpp)
{
	int i;

	if (bpp == 8)
		bgx |= bgx << 8;
	if (bpp == 16 || bpp == 8)
		bgx |= bgx << 16;

	while (height-- > 0) {
		u8 *p = dst;

		switch (bpp) {

		case 32:
			for (i=0; i < width; i++) {
				fb_writel(bgx, p); p += 4;
			}
			break;
		case 24:
			for (i=0; i < width; i++) {
#ifdef __LITTLE_ENDIAN
				fb_writew((bgx & 0xffff),(u16*)p); p += 2;
				fb_writeb((bgx >> 16),p++);
#else
				fb_writew((bgx >> 8),(u16*)p); p += 2;
				fb_writeb((bgx & 0xff),p++);
#endif
			}
		case 16:
			for (i=0; i < width/4; i++) {
				fb_writel(bgx,p); p += 4;
				fb_writel(bgx,p); p += 4;
			}
			if (width & 2) {
				fb_writel(bgx,p); p += 4;
			}
			if (width & 1)
				fb_writew(bgx,(u16*)p);
			break;
		case 8:
			for (i=0; i < width/4; i++) {
				fb_writel(bgx,p); p += 4;
			}

			if (width & 2) {
				fb_writew(bgx,p); p += 2;
			}
			if (width & 1)
				fb_writeb(bgx,(u8*)p);
			break;

		}
		dst += dstbytes;
	}
}

void fbcon_decor_copy(u8 *dst, u8 *src, int height, int width, int linebytes,
		   int srclinebytes, int bpp)
{
	int i;

	while (height-- > 0) {
		u32 *p = (u32 *)dst;
		u32 *q = (u32 *)src;

		switch (bpp) {

		case 32:
			for (i=0; i < width; i++)
				fb_writel(*q++, p++);
			break;
		case 24:
			for (i=0; i < (width*3/4); i++)
				fb_writel(*q++, p++);
			if ((width*3) % 4) {
				if (width & 2) {
					fb_writeb(*(u8*)q, (u8*)p);
				} else if (width & 1) {
					fb_writew(*(u16*)q, (u16*)p);
					fb_writeb(*(u8*)((u16*)q+1),(u8*)((u16*)p+2));
				}
			}
			break;
		case 16:
			for (i=0; i < width/4; i++) {
				fb_writel(*q++, p++);
				fb_writel(*q++, p++);
			}
			if (width & 2)
				fb_writel(*q++, p++);
			if (width & 1)
				fb_writew(*(u16*)q, (u16*)p);
			break;
		case 8:
			for (i=0; i < width/4; i++)
				fb_writel(*q++, p++);

			if (width & 2) {
				fb_writew(*(u16*)q, (u16*)p);
				q = (u32*) ((u16*)q + 1);
				p = (u32*) ((u16*)p + 1);
			}
			if (width & 1)
				fb_writeb(*(u8*)q, (u8*)p);
			break;
		}

		dst += linebytes;
		src += srclinebytes;
	}
}

static void decorfill(struct fb_info *info, int sy, int sx, int height,
		       int width)
{
	int bytespp = ((info->var.bits_per_pixel + 7) >> 3);
	int d  = sy * info->fix.line_length + sx * bytespp;
	int ds = (sy * info->var.xres + sx) * bytespp;

	fbcon_decor_copy((u8 *)(info->screen_base + d), (u8 *)(info->bgdecor.data + ds),
		    height, width, info->fix.line_length, info->var.xres * bytespp,
		    info->var.bits_per_pixel);
}

void fbcon_decor_clear(struct vc_data *vc, struct fb_info *info, int sy, int sx,
		    int height, int width)
{
	int bgshift = (vc->vc_hi_font_mask) ? 13 : 12;
	struct fbcon_ops *ops = info->fbcon_par;
	u8 *dst;
	int transparent, bg_color = attr_bgcol_ec(bgshift, vc, info);

	transparent = (vc->vc_decor.bg_color == bg_color);
	sy = sy * vc->vc_font.height + vc->vc_decor.ty;
	sx = sx * vc->vc_font.width + vc->vc_decor.tx;
	height *= vc->vc_font.height;
	width *= vc->vc_font.width;

	/* Don't paint the background image if console is blanked */
	if (transparent && !ops->blank_state) {
		decorfill(info, sy, sx, height, width);
	} else {
		dst = (u8 *)(info->screen_base + sy * info->fix.line_length +
			     sx * ((info->var.bits_per_pixel + 7) >> 3));
		decorset(dst, height, width, info->fix.line_length, cc2cx(bg_color),
			  info->var.bits_per_pixel);
	}
}

void fbcon_decor_clear_margins(struct vc_data *vc, struct fb_info *info,
			    int bottom_only)
{
	unsigned int tw = vc->vc_cols*vc->vc_font.width;
	unsigned int th = vc->vc_rows*vc->vc_font.height;

	if (!bottom_only) {
		/* top margin */
		decorfill(info, 0, 0, vc->vc_decor.ty, info->var.xres);
		/* left margin */
		decorfill(info, vc->vc_decor.ty, 0, th, vc->vc_decor.tx);
		/* right margin */
		decorfill(info, vc->vc_decor.ty, vc->vc_decor.tx + tw, th, 
			   info->var.xres - vc->vc_decor.tx - tw);
	}
	decorfill(info, vc->vc_decor.ty + th, 0, 
		   info->var.yres - vc->vc_decor.ty - th, info->var.xres);
}

void fbcon_decor_bmove_redraw(struct vc_data *vc, struct fb_info *info, int y, 
			   int sx, int dx, int width)
{
	u16 *d = (u16 *) (vc->vc_origin + vc->vc_size_row * y + dx * 2);
	u16 *s = d + (dx - sx);
	u16 *start = d;
	u16 *ls = d;
	u16 *le = d + width;
	u16 c;
	int x = dx;
	u16 attr = 1;

	do {
		c = scr_readw(d);
		if (attr != (c & 0xff00)) {
			attr = c & 0xff00;
			if (d > start) {
				fbcon_decor_putcs(vc, info, start, d - start, y, x);
				x += d - start;
				start = d;
			}
		}
		if (s >= ls && s < le && c == scr_readw(s)) {
			if (d > start) {
				fbcon_decor_putcs(vc, info, start, d - start, y, x);
				x += d - start + 1;
				start = d + 1;
			} else {
				x++;
				start++;
			}
		}
		s++;
		d++;
	} while (d < le);
	if (d > start)
		fbcon_decor_putcs(vc, info, start, d - start, y, x);
}

void fbcon_decor_blank(struct vc_data *vc, struct fb_info *info, int blank)
{
	if (blank) {
		decorset((u8 *)info->screen_base, info->var.yres, info->var.xres,
			  info->fix.line_length, 0, info->var.bits_per_pixel);
	} else {
		update_screen(vc);
		fbcon_decor_clear_margins(vc, info, 0);
	}
}

