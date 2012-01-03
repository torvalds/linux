/*
 *  linux/drivers/video/offb.c -- Open Firmware based frame buffer device
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This driver is partly based on the PowerMac console driver:
 *
 *	Copyright (C) 1996 Paul Mackerras
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <asm/io.h>

#ifdef CONFIG_PPC64
#include <asm/pci-bridge.h>
#endif

#ifdef CONFIG_PPC32
#include <asm/bootx.h>
#endif

#include "macmodes.h"

/* Supported palette hacks */
enum {
	cmap_unknown,
	cmap_m64,		/* ATI Mach64 */
	cmap_r128,		/* ATI Rage128 */
	cmap_M3A,		/* ATI Rage Mobility M3 Head A */
	cmap_M3B,		/* ATI Rage Mobility M3 Head B */
	cmap_radeon,		/* ATI Radeon */
	cmap_gxt2000,		/* IBM GXT2000 */
	cmap_avivo,		/* ATI R5xx */
};

struct offb_par {
	volatile void __iomem *cmap_adr;
	volatile void __iomem *cmap_data;
	int cmap_type;
	int blanked;
};

struct offb_par default_par;

#ifdef CONFIG_PPC32
extern boot_infos_t *boot_infos;
#endif

/* Definitions used by the Avivo palette hack */
#define AVIVO_DC_LUT_RW_SELECT                  0x6480
#define AVIVO_DC_LUT_RW_MODE                    0x6484
#define AVIVO_DC_LUT_RW_INDEX                   0x6488
#define AVIVO_DC_LUT_SEQ_COLOR                  0x648c
#define AVIVO_DC_LUT_PWL_DATA                   0x6490
#define AVIVO_DC_LUT_30_COLOR                   0x6494
#define AVIVO_DC_LUT_READ_PIPE_SELECT           0x6498
#define AVIVO_DC_LUT_WRITE_EN_MASK              0x649c
#define AVIVO_DC_LUT_AUTOFILL                   0x64a0

#define AVIVO_DC_LUTA_CONTROL                   0x64c0
#define AVIVO_DC_LUTA_BLACK_OFFSET_BLUE         0x64c4
#define AVIVO_DC_LUTA_BLACK_OFFSET_GREEN        0x64c8
#define AVIVO_DC_LUTA_BLACK_OFFSET_RED          0x64cc
#define AVIVO_DC_LUTA_WHITE_OFFSET_BLUE         0x64d0
#define AVIVO_DC_LUTA_WHITE_OFFSET_GREEN        0x64d4
#define AVIVO_DC_LUTA_WHITE_OFFSET_RED          0x64d8

#define AVIVO_DC_LUTB_CONTROL                   0x6cc0
#define AVIVO_DC_LUTB_BLACK_OFFSET_BLUE         0x6cc4
#define AVIVO_DC_LUTB_BLACK_OFFSET_GREEN        0x6cc8
#define AVIVO_DC_LUTB_BLACK_OFFSET_RED          0x6ccc
#define AVIVO_DC_LUTB_WHITE_OFFSET_BLUE         0x6cd0
#define AVIVO_DC_LUTB_WHITE_OFFSET_GREEN        0x6cd4
#define AVIVO_DC_LUTB_WHITE_OFFSET_RED          0x6cd8

    /*
     *  Set a single color register. The values supplied are already
     *  rounded down to the hardware's capabilities (according to the
     *  entries in the var structure). Return != 0 for invalid regno.
     */

static int offb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			  u_int transp, struct fb_info *info)
{
	struct offb_par *par = (struct offb_par *) info->par;

	if (info->fix.visual == FB_VISUAL_TRUECOLOR) {
		u32 *pal = info->pseudo_palette;
		u32 cr = red >> (16 - info->var.red.length);
		u32 cg = green >> (16 - info->var.green.length);
		u32 cb = blue >> (16 - info->var.blue.length);
		u32 value;

		if (regno >= 16)
			return -EINVAL;

		value = (cr << info->var.red.offset) |
			(cg << info->var.green.offset) |
			(cb << info->var.blue.offset);
		if (info->var.transp.length > 0) {
			u32 mask = (1 << info->var.transp.length) - 1;
			mask <<= info->var.transp.offset;
			value |= mask;
		}
		pal[regno] = value;
		return 0;
	}

	if (regno > 255)
		return -EINVAL;

	red >>= 8;
	green >>= 8;
	blue >>= 8;

	if (!par->cmap_adr)
		return 0;

	switch (par->cmap_type) {
	case cmap_m64:
		writeb(regno, par->cmap_adr);
		writeb(red, par->cmap_data);
		writeb(green, par->cmap_data);
		writeb(blue, par->cmap_data);
		break;
	case cmap_M3A:
		/* Clear PALETTE_ACCESS_CNTL in DAC_CNTL */
		out_le32(par->cmap_adr + 0x58,
			 in_le32(par->cmap_adr + 0x58) & ~0x20);
	case cmap_r128:
		/* Set palette index & data */
		out_8(par->cmap_adr + 0xb0, regno);
		out_le32(par->cmap_adr + 0xb4,
			 (red << 16 | green << 8 | blue));
		break;
	case cmap_M3B:
		/* Set PALETTE_ACCESS_CNTL in DAC_CNTL */
		out_le32(par->cmap_adr + 0x58,
			 in_le32(par->cmap_adr + 0x58) | 0x20);
		/* Set palette index & data */
		out_8(par->cmap_adr + 0xb0, regno);
		out_le32(par->cmap_adr + 0xb4, (red << 16 | green << 8 | blue));
		break;
	case cmap_radeon:
		/* Set palette index & data (could be smarter) */
		out_8(par->cmap_adr + 0xb0, regno);
		out_le32(par->cmap_adr + 0xb4, (red << 16 | green << 8 | blue));
		break;
	case cmap_gxt2000:
		out_le32(((unsigned __iomem *) par->cmap_adr) + regno,
			 (red << 16 | green << 8 | blue));
		break;
	case cmap_avivo:
		/* Write to both LUTs for now */
		writel(1, par->cmap_adr + AVIVO_DC_LUT_RW_SELECT);
		writeb(regno, par->cmap_adr + AVIVO_DC_LUT_RW_INDEX);
		writel(((red) << 22) | ((green) << 12) | ((blue) << 2),
		       par->cmap_adr + AVIVO_DC_LUT_30_COLOR);
		writel(0, par->cmap_adr + AVIVO_DC_LUT_RW_SELECT);
		writeb(regno, par->cmap_adr + AVIVO_DC_LUT_RW_INDEX);
		writel(((red) << 22) | ((green) << 12) | ((blue) << 2),
		       par->cmap_adr + AVIVO_DC_LUT_30_COLOR);
		break;
	}

	return 0;
}

    /*
     *  Blank the display.
     */

static int offb_blank(int blank, struct fb_info *info)
{
	struct offb_par *par = (struct offb_par *) info->par;
	int i, j;

	if (!par->cmap_adr)
		return 0;

	if (!par->blanked)
		if (!blank)
			return 0;

	par->blanked = blank;

	if (blank)
		for (i = 0; i < 256; i++) {
			switch (par->cmap_type) {
			case cmap_m64:
				writeb(i, par->cmap_adr);
				for (j = 0; j < 3; j++)
					writeb(0, par->cmap_data);
				break;
			case cmap_M3A:
				/* Clear PALETTE_ACCESS_CNTL in DAC_CNTL */
				out_le32(par->cmap_adr + 0x58,
					 in_le32(par->cmap_adr + 0x58) & ~0x20);
			case cmap_r128:
				/* Set palette index & data */
				out_8(par->cmap_adr + 0xb0, i);
				out_le32(par->cmap_adr + 0xb4, 0);
				break;
			case cmap_M3B:
				/* Set PALETTE_ACCESS_CNTL in DAC_CNTL */
				out_le32(par->cmap_adr + 0x58,
					 in_le32(par->cmap_adr + 0x58) | 0x20);
				/* Set palette index & data */
				out_8(par->cmap_adr + 0xb0, i);
				out_le32(par->cmap_adr + 0xb4, 0);
				break;
			case cmap_radeon:
				out_8(par->cmap_adr + 0xb0, i);
				out_le32(par->cmap_adr + 0xb4, 0);
				break;
			case cmap_gxt2000:
				out_le32(((unsigned __iomem *) par->cmap_adr) + i,
					 0);
				break;
			case cmap_avivo:
				writel(1, par->cmap_adr + AVIVO_DC_LUT_RW_SELECT);
				writeb(i, par->cmap_adr + AVIVO_DC_LUT_RW_INDEX);
				writel(0, par->cmap_adr + AVIVO_DC_LUT_30_COLOR);
				writel(0, par->cmap_adr + AVIVO_DC_LUT_RW_SELECT);
				writeb(i, par->cmap_adr + AVIVO_DC_LUT_RW_INDEX);
				writel(0, par->cmap_adr + AVIVO_DC_LUT_30_COLOR);
				break;
			}
	} else
		fb_set_cmap(&info->cmap, info);
	return 0;
}

static int offb_set_par(struct fb_info *info)
{
	struct offb_par *par = (struct offb_par *) info->par;

	/* On avivo, initialize palette control */
	if (par->cmap_type == cmap_avivo) {
		writel(0, par->cmap_adr + AVIVO_DC_LUTA_CONTROL);
		writel(0, par->cmap_adr + AVIVO_DC_LUTA_BLACK_OFFSET_BLUE);
		writel(0, par->cmap_adr + AVIVO_DC_LUTA_BLACK_OFFSET_GREEN);
		writel(0, par->cmap_adr + AVIVO_DC_LUTA_BLACK_OFFSET_RED);
		writel(0x0000ffff, par->cmap_adr + AVIVO_DC_LUTA_WHITE_OFFSET_BLUE);
		writel(0x0000ffff, par->cmap_adr + AVIVO_DC_LUTA_WHITE_OFFSET_GREEN);
		writel(0x0000ffff, par->cmap_adr + AVIVO_DC_LUTA_WHITE_OFFSET_RED);
		writel(0, par->cmap_adr + AVIVO_DC_LUTB_CONTROL);
		writel(0, par->cmap_adr + AVIVO_DC_LUTB_BLACK_OFFSET_BLUE);
		writel(0, par->cmap_adr + AVIVO_DC_LUTB_BLACK_OFFSET_GREEN);
		writel(0, par->cmap_adr + AVIVO_DC_LUTB_BLACK_OFFSET_RED);
		writel(0x0000ffff, par->cmap_adr + AVIVO_DC_LUTB_WHITE_OFFSET_BLUE);
		writel(0x0000ffff, par->cmap_adr + AVIVO_DC_LUTB_WHITE_OFFSET_GREEN);
		writel(0x0000ffff, par->cmap_adr + AVIVO_DC_LUTB_WHITE_OFFSET_RED);
		writel(1, par->cmap_adr + AVIVO_DC_LUT_RW_SELECT);
		writel(0, par->cmap_adr + AVIVO_DC_LUT_RW_MODE);
		writel(0x0000003f, par->cmap_adr + AVIVO_DC_LUT_WRITE_EN_MASK);
		writel(0, par->cmap_adr + AVIVO_DC_LUT_RW_SELECT);
		writel(0, par->cmap_adr + AVIVO_DC_LUT_RW_MODE);
		writel(0x0000003f, par->cmap_adr + AVIVO_DC_LUT_WRITE_EN_MASK);
	}
	return 0;
}

static void offb_destroy(struct fb_info *info)
{
	if (info->screen_base)
		iounmap(info->screen_base);
	release_mem_region(info->apertures->ranges[0].base, info->apertures->ranges[0].size);
	framebuffer_release(info);
}

static struct fb_ops offb_ops = {
	.owner		= THIS_MODULE,
	.fb_destroy	= offb_destroy,
	.fb_setcolreg	= offb_setcolreg,
	.fb_set_par	= offb_set_par,
	.fb_blank	= offb_blank,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static void __iomem *offb_map_reg(struct device_node *np, int index,
				  unsigned long offset, unsigned long size)
{
	const u32 *addrp;
	u64 asize, taddr;
	unsigned int flags;

	addrp = of_get_pci_address(np, index, &asize, &flags);
	if (addrp == NULL)
		addrp = of_get_address(np, index, &asize, &flags);
	if (addrp == NULL)
		return NULL;
	if ((flags & (IORESOURCE_IO | IORESOURCE_MEM)) == 0)
		return NULL;
	if ((offset + size) > asize)
		return NULL;
	taddr = of_translate_address(np, addrp);
	if (taddr == OF_BAD_ADDR)
		return NULL;
	return ioremap(taddr + offset, size);
}

static void offb_init_palette_hacks(struct fb_info *info, struct device_node *dp,
				    const char *name, unsigned long address)
{
	struct offb_par *par = (struct offb_par *) info->par;

	if (dp && !strncmp(name, "ATY,Rage128", 11)) {
		par->cmap_adr = offb_map_reg(dp, 2, 0, 0x1fff);
		if (par->cmap_adr)
			par->cmap_type = cmap_r128;
	} else if (dp && (!strncmp(name, "ATY,RageM3pA", 12)
			  || !strncmp(name, "ATY,RageM3p12A", 14))) {
		par->cmap_adr = offb_map_reg(dp, 2, 0, 0x1fff);
		if (par->cmap_adr)
			par->cmap_type = cmap_M3A;
	} else if (dp && !strncmp(name, "ATY,RageM3pB", 12)) {
		par->cmap_adr = offb_map_reg(dp, 2, 0, 0x1fff);
		if (par->cmap_adr)
			par->cmap_type = cmap_M3B;
	} else if (dp && !strncmp(name, "ATY,Rage6", 9)) {
		par->cmap_adr = offb_map_reg(dp, 1, 0, 0x1fff);
		if (par->cmap_adr)
			par->cmap_type = cmap_radeon;
	} else if (!strncmp(name, "ATY,", 4)) {
		unsigned long base = address & 0xff000000UL;
		par->cmap_adr =
			ioremap(base + 0x7ff000, 0x1000) + 0xcc0;
		par->cmap_data = par->cmap_adr + 1;
		par->cmap_type = cmap_m64;
	} else if (dp && (of_device_is_compatible(dp, "pci1014,b7") ||
			  of_device_is_compatible(dp, "pci1014,21c"))) {
		par->cmap_adr = offb_map_reg(dp, 0, 0x6000, 0x1000);
		if (par->cmap_adr)
			par->cmap_type = cmap_gxt2000;
	} else if (dp && !strncmp(name, "vga,Display-", 12)) {
		/* Look for AVIVO initialized by SLOF */
		struct device_node *pciparent = of_get_parent(dp);
		const u32 *vid, *did;
		vid = of_get_property(pciparent, "vendor-id", NULL);
		did = of_get_property(pciparent, "device-id", NULL);
		/* This will match most R5xx */
		if (vid && did && *vid == 0x1002 &&
		    ((*did >= 0x7100 && *did < 0x7800) ||
		     (*did >= 0x9400))) {
			par->cmap_adr = offb_map_reg(pciparent, 2, 0, 0x10000);
			if (par->cmap_adr)
				par->cmap_type = cmap_avivo;
		}
		of_node_put(pciparent);
	}
	info->fix.visual = (par->cmap_type != cmap_unknown) ?
		FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_STATIC_PSEUDOCOLOR;
}

static void __init offb_init_fb(const char *name, const char *full_name,
				int width, int height, int depth,
				int pitch, unsigned long address,
				int foreign_endian, struct device_node *dp)
{
	unsigned long res_size = pitch * height;
	struct offb_par *par = &default_par;
	unsigned long res_start = address;
	struct fb_fix_screeninfo *fix;
	struct fb_var_screeninfo *var;
	struct fb_info *info;

	if (!request_mem_region(res_start, res_size, "offb"))
		return;

	printk(KERN_INFO
	       "Using unsupported %dx%d %s at %lx, depth=%d, pitch=%d\n",
	       width, height, name, address, depth, pitch);
	if (depth != 8 && depth != 15 && depth != 16 && depth != 32) {
		printk(KERN_ERR "%s: can't use depth = %d\n", full_name,
		       depth);
		release_mem_region(res_start, res_size);
		return;
	}

	info = framebuffer_alloc(sizeof(u32) * 16, NULL);
	
	if (info == 0) {
		release_mem_region(res_start, res_size);
		return;
	}

	fix = &info->fix;
	var = &info->var;
	info->par = par;

	strcpy(fix->id, "OFfb ");
	strncat(fix->id, name, sizeof(fix->id) - sizeof("OFfb "));
	fix->id[sizeof(fix->id) - 1] = '\0';

	var->xres = var->xres_virtual = width;
	var->yres = var->yres_virtual = height;
	fix->line_length = pitch;

	fix->smem_start = address;
	fix->smem_len = pitch * height;
	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->type_aux = 0;

	par->cmap_type = cmap_unknown;
	if (depth == 8)
		offb_init_palette_hacks(info, dp, name, address);
	else
		fix->visual = FB_VISUAL_TRUECOLOR;

	var->xoffset = var->yoffset = 0;
	switch (depth) {
	case 8:
		var->bits_per_pixel = 8;
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 15:		/* RGB 555 */
		var->bits_per_pixel = 16;
		var->red.offset = 10;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 5;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 16:		/* RGB 565 */
		var->bits_per_pixel = 16;
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 32:		/* RGB 888 */
		var->bits_per_pixel = 32;
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 24;
		var->transp.length = 8;
		break;
	}
	var->red.msb_right = var->green.msb_right = var->blue.msb_right =
	    var->transp.msb_right = 0;
	var->grayscale = 0;
	var->nonstd = 0;
	var->activate = 0;
	var->height = var->width = -1;
	var->pixclock = 10000;
	var->left_margin = var->right_margin = 16;
	var->upper_margin = var->lower_margin = 16;
	var->hsync_len = var->vsync_len = 8;
	var->sync = 0;
	var->vmode = FB_VMODE_NONINTERLACED;

	/* set offb aperture size for generic probing */
	info->apertures = alloc_apertures(1);
	if (!info->apertures)
		goto out_aper;
	info->apertures->ranges[0].base = address;
	info->apertures->ranges[0].size = fix->smem_len;

	info->fbops = &offb_ops;
	info->screen_base = ioremap(address, fix->smem_len);
	info->pseudo_palette = (void *) (info + 1);
	info->flags = FBINFO_DEFAULT | FBINFO_MISC_FIRMWARE | foreign_endian;

	fb_alloc_cmap(&info->cmap, 256, 0);

	if (register_framebuffer(info) < 0)
		goto out_err;

	printk(KERN_INFO "fb%d: Open Firmware frame buffer device on %s\n",
	       info->node, full_name);
	return;

out_err:
	iounmap(info->screen_base);
out_aper:
	iounmap(par->cmap_adr);
	par->cmap_adr = NULL;
	framebuffer_release(info);
	release_mem_region(res_start, res_size);
}


static void __init offb_init_nodriver(struct device_node *dp, int no_real_node)
{
	unsigned int len;
	int i, width = 640, height = 480, depth = 8, pitch = 640;
	unsigned int flags, rsize, addr_prop = 0;
	unsigned long max_size = 0;
	u64 rstart, address = OF_BAD_ADDR;
	const u32 *pp, *addrp, *up;
	u64 asize;
	int foreign_endian = 0;

#ifdef __BIG_ENDIAN
	if (of_get_property(dp, "little-endian", NULL))
		foreign_endian = FBINFO_FOREIGN_ENDIAN;
#else
	if (of_get_property(dp, "big-endian", NULL))
		foreign_endian = FBINFO_FOREIGN_ENDIAN;
#endif

	pp = of_get_property(dp, "linux,bootx-depth", &len);
	if (pp == NULL)
		pp = of_get_property(dp, "depth", &len);
	if (pp && len == sizeof(u32))
		depth = *pp;

	pp = of_get_property(dp, "linux,bootx-width", &len);
	if (pp == NULL)
		pp = of_get_property(dp, "width", &len);
	if (pp && len == sizeof(u32))
		width = *pp;

	pp = of_get_property(dp, "linux,bootx-height", &len);
	if (pp == NULL)
		pp = of_get_property(dp, "height", &len);
	if (pp && len == sizeof(u32))
		height = *pp;

	pp = of_get_property(dp, "linux,bootx-linebytes", &len);
	if (pp == NULL)
		pp = of_get_property(dp, "linebytes", &len);
	if (pp && len == sizeof(u32) && (*pp != 0xffffffffu))
		pitch = *pp;
	else
		pitch = width * ((depth + 7) / 8);

	rsize = (unsigned long)pitch * (unsigned long)height;

	/* Ok, now we try to figure out the address of the framebuffer.
	 *
	 * Unfortunately, Open Firmware doesn't provide a standard way to do
	 * so. All we can do is a dodgy heuristic that happens to work in
	 * practice. On most machines, the "address" property contains what
	 * we need, though not on Matrox cards found in IBM machines. What I've
	 * found that appears to give good results is to go through the PCI
	 * ranges and pick one that is both big enough and if possible encloses
	 * the "address" property. If none match, we pick the biggest
	 */
	up = of_get_property(dp, "linux,bootx-addr", &len);
	if (up == NULL)
		up = of_get_property(dp, "address", &len);
	if (up && len == sizeof(u32))
		addr_prop = *up;

	/* Hack for when BootX is passing us */
	if (no_real_node)
		goto skip_addr;

	for (i = 0; (addrp = of_get_address(dp, i, &asize, &flags))
		     != NULL; i++) {
		int match_addrp = 0;

		if (!(flags & IORESOURCE_MEM))
			continue;
		if (asize < rsize)
			continue;
		rstart = of_translate_address(dp, addrp);
		if (rstart == OF_BAD_ADDR)
			continue;
		if (addr_prop && (rstart <= addr_prop) &&
		    ((rstart + asize) >= (addr_prop + rsize)))
			match_addrp = 1;
		if (match_addrp) {
			address = addr_prop;
			break;
		}
		if (rsize > max_size) {
			max_size = rsize;
			address = OF_BAD_ADDR;
 		}

		if (address == OF_BAD_ADDR)
			address = rstart;
	}
 skip_addr:
	if (address == OF_BAD_ADDR && addr_prop)
		address = (u64)addr_prop;
	if (address != OF_BAD_ADDR) {
		/* kludge for valkyrie */
		if (strcmp(dp->name, "valkyrie") == 0)
			address += 0x1000;
		offb_init_fb(no_real_node ? "bootx" : dp->name,
			     no_real_node ? "display" : dp->full_name,
			     width, height, depth, pitch, address,
			     foreign_endian, no_real_node ? NULL : dp);
	}
}

static int __init offb_init(void)
{
	struct device_node *dp = NULL, *boot_disp = NULL;

	if (fb_get_options("offb", NULL))
		return -ENODEV;

	/* Check if we have a MacOS display without a node spec */
	if (of_get_property(of_chosen, "linux,bootx-noscreen", NULL) != NULL) {
		/* The old code tried to work out which node was the MacOS
		 * display based on the address. I'm dropping that since the
		 * lack of a node spec only happens with old BootX versions
		 * (users can update) and with this code, they'll still get
		 * a display (just not the palette hacks).
		 */
		offb_init_nodriver(of_chosen, 1);
	}

	for (dp = NULL; (dp = of_find_node_by_type(dp, "display"));) {
		if (of_get_property(dp, "linux,opened", NULL) &&
		    of_get_property(dp, "linux,boot-display", NULL)) {
			boot_disp = dp;
			offb_init_nodriver(dp, 0);
		}
	}
	for (dp = NULL; (dp = of_find_node_by_type(dp, "display"));) {
		if (of_get_property(dp, "linux,opened", NULL) &&
		    dp != boot_disp)
			offb_init_nodriver(dp, 0);
	}

	return 0;
}


module_init(offb_init);
MODULE_LICENSE("GPL");
