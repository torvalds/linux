/*
 * drivers/mb862xx/mb862xxfb_accel.c
 *
 * Fujitsu Carmine/Coral-P(A)/Lime framebuffer driver acceleration support
 *
 * (C) 2007 Alexander Shishkin <virtuoso@slind.org>
 * (C) 2009 Valentin Sitdikov <v.sitdikov@gmail.com>
 * (C) 2009 Siemens AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#if defined(CONFIG_OF)
#include <linux/of_platform.h>
#endif
#include "mb862xxfb.h"
#include "mb862xx_reg.h"
#include "mb862xxfb_accel.h"

static void mb862xxfb_write_fifo(u32 count, u32 *data, struct fb_info *info)
{
	struct mb862xxfb_par *par = info->par;
	static u32 free;

	u32 total = 0;
	while (total < count) {
		if (free) {
			outreg(geo, GDC_GEO_REG_INPUT_FIFO, data[total]);
			total++;
			free--;
		} else {
			free = (u32) inreg(draw, GDC_REG_FIFO_COUNT);
		}
	}
}

static void mb86290fb_copyarea(struct fb_info *info,
			       const struct fb_copyarea *area)
{
	__u32 cmd[6];

	cmd[0] = (GDC_TYPE_SETREGISTER << 24) | (1 << 16) | GDC_REG_MODE_BITMAP;
	/* Set raster operation */
	cmd[1] = (2 << 7) | (GDC_ROP_COPY << 9);
	cmd[2] = GDC_TYPE_BLTCOPYP << 24;

	if (area->sx >= area->dx && area->sy >= area->dy)
		cmd[2] |= GDC_CMD_BLTCOPY_TOP_LEFT << 16;
	else if (area->sx >= area->dx && area->sy <= area->dy)
		cmd[2] |= GDC_CMD_BLTCOPY_BOTTOM_LEFT << 16;
	else if (area->sx <= area->dx && area->sy >= area->dy)
		cmd[2] |= GDC_CMD_BLTCOPY_TOP_RIGHT << 16;
	else
		cmd[2] |= GDC_CMD_BLTCOPY_BOTTOM_RIGHT << 16;

	cmd[3] = (area->sy << 16) | area->sx;
	cmd[4] = (area->dy << 16) | area->dx;
	cmd[5] = (area->height << 16) | area->width;
	mb862xxfb_write_fifo(6, cmd, info);
}

/*
 * Fill in the cmd array /GDC FIFO commands/ to draw a 1bit image.
 * Make sure cmd has enough room!
 */
static void mb86290fb_imageblit1(u32 *cmd, u16 step, u16 dx, u16 dy,
				 u16 width, u16 height, u32 fgcolor,
				 u32 bgcolor, const struct fb_image *image,
				 struct fb_info *info)
{
	int i;
	unsigned const char *line;
	u16 bytes;

	/* set colors and raster operation regs */
	cmd[0] = (GDC_TYPE_SETREGISTER << 24) | (1 << 16) | GDC_REG_MODE_BITMAP;
	/* Set raster operation */
	cmd[1] = (2 << 7) | (GDC_ROP_COPY << 9);
	cmd[2] =
	    (GDC_TYPE_SETCOLORREGISTER << 24) | (GDC_CMD_BODY_FORE_COLOR << 16);
	cmd[3] = fgcolor;
	cmd[4] =
	    (GDC_TYPE_SETCOLORREGISTER << 24) | (GDC_CMD_BODY_BACK_COLOR << 16);
	cmd[5] = bgcolor;

	i = 0;
	line = image->data;
	bytes = (image->width + 7) >> 3;

	/* and the image */
	cmd[6] = (GDC_TYPE_DRAWBITMAPP << 24) |
	    (GDC_CMD_BITMAP << 16) | (2 + (step * height));
	cmd[7] = (dy << 16) | dx;
	cmd[8] = (height << 16) | width;

	while (i < height) {
		memcpy(&cmd[9 + i * step], line, step << 2);
#ifdef __LITTLE_ENDIAN
		{
			int k = 0;
			for (k = 0; k < step; k++)
				cmd[9 + i * step + k] =
				    cpu_to_be32(cmd[9 + i * step + k]);
		}
#endif
		line += bytes;
		i++;
	}
}

/*
 * Fill in the cmd array /GDC FIFO commands/ to draw a 8bit image.
 * Make sure cmd has enough room!
 */
static void mb86290fb_imageblit8(u32 *cmd, u16 step, u16 dx, u16 dy,
				 u16 width, u16 height, u32 fgcolor,
				 u32 bgcolor, const struct fb_image *image,
				 struct fb_info *info)
{
	int i, j;
	unsigned const char *line, *ptr;
	u16 bytes;

	cmd[0] = (GDC_TYPE_DRAWBITMAPP << 24) |
	    (GDC_CMD_BLT_DRAW << 16) | (2 + (height * step));
	cmd[1] = (dy << 16) | dx;
	cmd[2] = (height << 16) | width;

	i = 0;
	line = ptr = image->data;
	bytes = image->width;

	while (i < height) {
		ptr = line;
		for (j = 0; j < step; j++) {
			cmd[3 + i * step + j] =
			    (((u32 *) (info->pseudo_palette))[*ptr]) & 0xffff;
			ptr++;
			cmd[3 + i * step + j] |=
			    ((((u32 *) (info->
					pseudo_palette))[*ptr]) & 0xffff) << 16;
			ptr++;
		}

		line += bytes;
		i++;
	}
}

/*
 * Fill in the cmd array /GDC FIFO commands/ to draw a 16bit image.
 * Make sure cmd has enough room!
 */
static void mb86290fb_imageblit16(u32 *cmd, u16 step, u16 dx, u16 dy,
				  u16 width, u16 height, u32 fgcolor,
				  u32 bgcolor, const struct fb_image *image,
				  struct fb_info *info)
{
	int i;
	unsigned const char *line;
	u16 bytes;

	i = 0;
	line = image->data;
	bytes = image->width << 1;

	cmd[0] = (GDC_TYPE_DRAWBITMAPP << 24) |
	    (GDC_CMD_BLT_DRAW << 16) | (2 + step * height);
	cmd[1] = (dy << 16) | dx;
	cmd[2] = (height << 16) | width;

	while (i < height) {
		memcpy(&cmd[3 + i * step], line, step);
		line += bytes;
		i++;
	}
}

static void mb86290fb_imageblit(struct fb_info *info,
				const struct fb_image *image)
{
	int mdr;
	u32 *cmd = NULL;
	void (*cmdfn) (u32 *, u16, u16, u16, u16, u16, u32, u32,
		       const struct fb_image *, struct fb_info *) = NULL;
	u32 cmdlen;
	u32 fgcolor = 0, bgcolor = 0;
	u16 step;

	u16 width = image->width, height = image->height;
	u16 dx = image->dx, dy = image->dy;
	int x2, y2, vxres, vyres;

	mdr = (GDC_ROP_COPY << 9);
	x2 = image->dx + image->width;
	y2 = image->dy + image->height;
	vxres = info->var.xres_virtual;
	vyres = info->var.yres_virtual;
	x2 = min(x2, vxres);
	y2 = min(y2, vyres);
	width = x2 - dx;
	height = y2 - dy;

	switch (image->depth) {
	case 1:
		step = (width + 31) >> 5;
		cmdlen = 9 + height * step;
		cmdfn = mb86290fb_imageblit1;
		if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
		    info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
			fgcolor =
			    ((u32 *) (info->pseudo_palette))[image->fg_color];
			bgcolor =
			    ((u32 *) (info->pseudo_palette))[image->bg_color];
		} else {
			fgcolor = image->fg_color;
			bgcolor = image->bg_color;
		}

		break;

	case 8:
		step = (width + 1) >> 1;
		cmdlen = 3 + height * step;
		cmdfn = mb86290fb_imageblit8;
		break;

	case 16:
		step = (width + 1) >> 1;
		cmdlen = 3 + height * step;
		cmdfn = mb86290fb_imageblit16;
		break;

	default:
		cfb_imageblit(info, image);
		return;
	}

	cmd = kmalloc(cmdlen * 4, GFP_DMA);
	if (!cmd)
		return cfb_imageblit(info, image);
	cmdfn(cmd, step, dx, dy, width, height, fgcolor, bgcolor, image, info);
	mb862xxfb_write_fifo(cmdlen, cmd, info);
	kfree(cmd);
}

static void mb86290fb_fillrect(struct fb_info *info,
			       const struct fb_fillrect *rect)
{

	u32 x2, y2, vxres, vyres, height, width, fg;
	u32 cmd[7];

	vxres = info->var.xres_virtual;
	vyres = info->var.yres_virtual;

	if (!rect->width || !rect->height || rect->dx > vxres
	    || rect->dy > vyres)
		return;

	/* We could use hardware clipping but on many cards you get around
	 * hardware clipping by writing to framebuffer directly. */
	x2 = rect->dx + rect->width;
	y2 = rect->dy + rect->height;
	x2 = min(x2, vxres);
	y2 = min(y2, vyres);
	width = x2 - rect->dx;
	height = y2 - rect->dy;
	if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
	    info->fix.visual == FB_VISUAL_DIRECTCOLOR)
		fg = ((u32 *) (info->pseudo_palette))[rect->color];
	else
		fg = rect->color;

	switch (rect->rop) {

	case ROP_XOR:
		/* Set raster operation */
		cmd[1] = (2 << 7) | (GDC_ROP_XOR << 9);
		break;

	case ROP_COPY:
		/* Set raster operation */
		cmd[1] = (2 << 7) | (GDC_ROP_COPY << 9);
		break;

	}

	cmd[0] = (GDC_TYPE_SETREGISTER << 24) | (1 << 16) | GDC_REG_MODE_BITMAP;
	/* cmd[1] set earlier */
	cmd[2] =
	    (GDC_TYPE_SETCOLORREGISTER << 24) | (GDC_CMD_BODY_FORE_COLOR << 16);
	cmd[3] = fg;
	cmd[4] = (GDC_TYPE_DRAWRECTP << 24) | (GDC_CMD_BLT_FILL << 16);
	cmd[5] = (rect->dy << 16) | (rect->dx);
	cmd[6] = (height << 16) | width;

	mb862xxfb_write_fifo(7, cmd, info);
}

void mb862xxfb_init_accel(struct fb_info *info, int xres)
{
	struct mb862xxfb_par *par = info->par;

	if (info->var.bits_per_pixel == 32) {
		info->fbops->fb_fillrect = cfb_fillrect;
		info->fbops->fb_copyarea = cfb_copyarea;
		info->fbops->fb_imageblit = cfb_imageblit;
	} else {
		outreg(disp, GC_L0EM, 3);
		info->fbops->fb_fillrect = mb86290fb_fillrect;
		info->fbops->fb_copyarea = mb86290fb_copyarea;
		info->fbops->fb_imageblit = mb86290fb_imageblit;
	}
	outreg(draw, GDC_REG_DRAW_BASE, 0);
	outreg(draw, GDC_REG_MODE_MISC, 0x8000);
	outreg(draw, GDC_REG_X_RESOLUTION, xres);

	info->flags |=
	    FBINFO_HWACCEL_COPYAREA | FBINFO_HWACCEL_FILLRECT |
	    FBINFO_HWACCEL_IMAGEBLIT;
	info->fix.accel = 0xff;	/*FIXME: add right define */
}
EXPORT_SYMBOL(mb862xxfb_init_accel);

MODULE_LICENSE("GPL v2");
