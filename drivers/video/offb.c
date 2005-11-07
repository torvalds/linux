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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <asm/prom.h>

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
};

struct offb_par {
	volatile void __iomem *cmap_adr;
	volatile void __iomem *cmap_data;
	int cmap_type;
	int blanked;
};

struct offb_par default_par;

    /*
     *  Interface used by the world
     */

int offb_init(void);

static int offb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			  u_int transp, struct fb_info *info);
static int offb_blank(int blank, struct fb_info *info);

#ifdef CONFIG_PPC32
extern boot_infos_t *boot_infos;
#endif

static void offb_init_nodriver(struct device_node *);
static void offb_init_fb(const char *name, const char *full_name,
			 int width, int height, int depth, int pitch,
			 unsigned long address, struct device_node *dp);

static struct fb_ops offb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= offb_setcolreg,
	.fb_blank	= offb_blank,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

    /*
     *  Set a single color register. The values supplied are already
     *  rounded down to the hardware's capabilities (according to the
     *  entries in the var structure). Return != 0 for invalid regno.
     */

static int offb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			  u_int transp, struct fb_info *info)
{
	struct offb_par *par = (struct offb_par *) info->par;

	if (!par->cmap_adr || regno > 255)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;

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
		out_le32((unsigned __iomem *) par->cmap_adr + regno,
			 (red << 16 | green << 8 | blue));
		break;
	}

	if (regno < 16)
		switch (info->var.bits_per_pixel) {
		case 16:
			((u16 *) (info->pseudo_palette))[regno] =
			    (regno << 10) | (regno << 5) | regno;
			break;
		case 32:
			{
				int i = (regno << 8) | regno;
				((u32 *) (info->pseudo_palette))[regno] =
				    (i << 16) | i;
				break;
			}
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
				out_le32((unsigned __iomem *) par->cmap_adr + i,
					 0);
				break;
			}
	} else
		fb_set_cmap(&info->cmap, info);
	return 0;
}

    /*
     *  Initialisation
     */

int __init offb_init(void)
{
	struct device_node *dp = NULL, *boot_disp = NULL;
#if defined(CONFIG_BOOTX_TEXT) && defined(CONFIG_PPC32)
	struct device_node *macos_display = NULL;
#endif
	if (fb_get_options("offb", NULL))
		return -ENODEV;

#if defined(CONFIG_BOOTX_TEXT) && defined(CONFIG_PPC32)
	/* If we're booted from BootX... */
	if (boot_infos != 0) {
		unsigned long addr =
		    (unsigned long) boot_infos->dispDeviceBase;
		/* find the device node corresponding to the macos display */
		while ((dp = of_find_node_by_type(dp, "display"))) {
			int i;
			/*
			 * Grrr...  It looks like the MacOS ATI driver
			 * munges the assigned-addresses property (but
			 * the AAPL,address value is OK).
			 */
			if (strncmp(dp->name, "ATY,", 4) == 0
			    && dp->n_addrs == 1) {
				unsigned int *ap =
				    (unsigned int *) get_property(dp,
								  "AAPL,address",
								  NULL);
				if (ap != NULL) {
					dp->addrs[0].address = *ap;
					dp->addrs[0].size = 0x01000000;
				}
			}

			/*
			 * The LTPro on the Lombard powerbook has no addresses
			 * on the display nodes, they are on their parent.
			 */
			if (dp->n_addrs == 0
			    && device_is_compatible(dp, "ATY,264LTPro")) {
				int na;
				unsigned int *ap = (unsigned int *)
				    get_property(dp, "AAPL,address", &na);
				if (ap != 0)
					for (na /= sizeof(unsigned int);
					     na > 0; --na, ++ap)
						if (*ap <= addr
						    && addr <
						    *ap + 0x1000000)
							goto foundit;
			}

			/*
			 * See if the display address is in one of the address
			 * ranges for this display.
			 */
			for (i = 0; i < dp->n_addrs; ++i) {
				if (dp->addrs[i].address <= addr
				    && addr <
				    dp->addrs[i].address +
				    dp->addrs[i].size)
					break;
			}
			if (i < dp->n_addrs) {
			      foundit:
				printk(KERN_INFO "MacOS display is %s\n",
				       dp->full_name);
				macos_display = dp;
				break;
			}
		}

		/* initialize it */
		offb_init_fb(macos_display ? macos_display->
			     name : "MacOS display",
			     macos_display ? macos_display->
			     full_name : "MacOS display",
			     boot_infos->dispDeviceRect[2],
			     boot_infos->dispDeviceRect[3],
			     boot_infos->dispDeviceDepth,
			     boot_infos->dispDeviceRowBytes, addr, NULL);
	}
#endif /* defined(CONFIG_BOOTX_TEXT) && defined(CONFIG_PPC32) */

	for (dp = NULL; (dp = of_find_node_by_type(dp, "display"));) {
		if (get_property(dp, "linux,opened", NULL) &&
		    get_property(dp, "linux,boot-display", NULL)) {
			boot_disp = dp;
			offb_init_nodriver(dp);
		}
	}
	for (dp = NULL; (dp = of_find_node_by_type(dp, "display"));) {
		if (get_property(dp, "linux,opened", NULL) &&
		    dp != boot_disp)
			offb_init_nodriver(dp);
	}

	return 0;
}


static void __init offb_init_nodriver(struct device_node *dp)
{
	int *pp, i;
	unsigned int len;
	int width = 640, height = 480, depth = 8, pitch;
	unsigned *up;
	unsigned long address;

	if ((pp = (int *) get_property(dp, "depth", &len)) != NULL
	    && len == sizeof(int))
		depth = *pp;
	if ((pp = (int *) get_property(dp, "width", &len)) != NULL
	    && len == sizeof(int))
		width = *pp;
	if ((pp = (int *) get_property(dp, "height", &len)) != NULL
	    && len == sizeof(int))
		height = *pp;
	if ((pp = (int *) get_property(dp, "linebytes", &len)) != NULL
	    && len == sizeof(int)) {
		pitch = *pp;
		if (pitch == 1)
			pitch = 0x1000;
	} else
		pitch = width;
	if ((up = (unsigned *) get_property(dp, "address", &len)) != NULL
	    && len == sizeof(unsigned))
		address = (u_long) * up;
	else {
		for (i = 0; i < dp->n_addrs; ++i)
			if (dp->addrs[i].size >=
			    pitch * height * depth / 8)
				break;
		if (i >= dp->n_addrs) {
			printk(KERN_ERR
			       "no framebuffer address found for %s\n",
			       dp->full_name);
			return;
		}

		address = (u_long) dp->addrs[i].address;

#ifdef CONFIG_PPC64
		address += ((struct pci_dn *)dp->data)->phb->pci_mem_offset;
#endif

		/* kludge for valkyrie */
		if (strcmp(dp->name, "valkyrie") == 0)
			address += 0x1000;
	}
	offb_init_fb(dp->name, dp->full_name, width, height, depth,
		     pitch, address, dp);

}

static void __init offb_init_fb(const char *name, const char *full_name,
				int width, int height, int depth,
				int pitch, unsigned long address,
				struct device_node *dp)
{
	unsigned long res_size = pitch * height * depth / 8;
	struct offb_par *par = &default_par;
	unsigned long res_start = address;
	struct fb_fix_screeninfo *fix;
	struct fb_var_screeninfo *var;
	struct fb_info *info;
	int size;

	if (!request_mem_region(res_start, res_size, "offb"))
		return;

	printk(KERN_INFO
	       "Using unsupported %dx%d %s at %lx, depth=%d, pitch=%d\n",
	       width, height, name, address, depth, pitch);
	if (depth != 8 && depth != 16 && depth != 32) {
		printk(KERN_ERR "%s: can't use depth = %d\n", full_name,
		       depth);
		release_mem_region(res_start, res_size);
		return;
	}

	size = sizeof(struct fb_info) + sizeof(u32) * 17;

	info = kmalloc(size, GFP_ATOMIC);
	
	if (info == 0) {
		release_mem_region(res_start, res_size);
		return;
	}
	memset(info, 0, size);

	fix = &info->fix;
	var = &info->var;

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
	if (depth == 8) {
		/* XXX kludge for ati */
		if (dp && !strncmp(name, "ATY,Rage128", 11)) {
			unsigned long regbase = dp->addrs[2].address;
			par->cmap_adr = ioremap(regbase, 0x1FFF);
			par->cmap_type = cmap_r128;
		} else if (dp && (!strncmp(name, "ATY,RageM3pA", 12)
				  || !strncmp(name, "ATY,RageM3p12A", 14))) {
			unsigned long regbase =
			    dp->parent->addrs[2].address;
			par->cmap_adr = ioremap(regbase, 0x1FFF);
			par->cmap_type = cmap_M3A;
		} else if (dp && !strncmp(name, "ATY,RageM3pB", 12)) {
			unsigned long regbase =
			    dp->parent->addrs[2].address;
			par->cmap_adr = ioremap(regbase, 0x1FFF);
			par->cmap_type = cmap_M3B;
		} else if (dp && !strncmp(name, "ATY,Rage6", 9)) {
			unsigned long regbase = dp->addrs[1].address;
			par->cmap_adr = ioremap(regbase, 0x1FFF);
			par->cmap_type = cmap_radeon;
		} else if (!strncmp(name, "ATY,", 4)) {
			unsigned long base = address & 0xff000000UL;
			par->cmap_adr =
			    ioremap(base + 0x7ff000, 0x1000) + 0xcc0;
			par->cmap_data = par->cmap_adr + 1;
			par->cmap_type = cmap_m64;
		} else if (device_is_compatible(dp, "pci1014,b7")) {
			unsigned long regbase = dp->addrs[0].address;
			par->cmap_adr = ioremap(regbase + 0x6000, 0x1000);
			par->cmap_type = cmap_gxt2000;
		}
		fix->visual = par->cmap_adr ? FB_VISUAL_PSEUDOCOLOR
		    : FB_VISUAL_STATIC_PSEUDOCOLOR;
	} else
		fix->visual =	/* par->cmap_adr ? FB_VISUAL_DIRECTCOLOR
				   : */ FB_VISUAL_TRUECOLOR;

	var->xoffset = var->yoffset = 0;
	var->bits_per_pixel = depth;
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
	case 16:		/* RGB 555 */
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

	info->fbops = &offb_ops;
	info->screen_base = ioremap(address, fix->smem_len);
	info->par = par;
	info->pseudo_palette = (void *) (info + 1);
	info->flags = FBINFO_DEFAULT;

	fb_alloc_cmap(&info->cmap, 256, 0);

	if (register_framebuffer(info) < 0) {
		kfree(info);
		release_mem_region(res_start, res_size);
		return;
	}

	printk(KERN_INFO "fb%d: Open Firmware frame buffer device on %s\n",
	       info->node, full_name);
}

module_init(offb_init);
MODULE_LICENSE("GPL");
