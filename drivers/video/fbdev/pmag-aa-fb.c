/*
 *	linux/drivers/video/pmag-aa-fb.c
 *	Copyright 2002 Karsten Merker <merker@debian.org>
 *
 *	PMAG-AA TurboChannel framebuffer card support ... derived from
 *	pmag-ba-fb.c, which is Copyright (C) 1999, 2000, 2001 by
 *	Michael Engel <engel@unix-ag.org>, Karsten Merker <merker@debian.org>
 *	and Harald Koerfgen <hkoerfg@web.de>, which itself is derived from
 *	"HP300 Topcat framebuffer support (derived from macfb of all things)
 *	Phil Blundell <philb@gnu.org> 1998"
 *	Copyright (c) 2016  Maciej W. Rozycki
 *
 *	This file is subject to the terms and conditions of the GNU General
 *	Public License.  See the file COPYING in the main directory of this
 *	archive for more details.
 *
 *	2002-09-28  Karsten Merker <merker@linuxtag.org>
 *		Version 0.01: First try to get a PMAG-AA running.
 *
 *	2003-02-24  Thiemo Seufer  <seufer@csv.ica.uni-stuttgart.de>
 *		Version 0.02: Major code cleanup.
 *
 *	2003-09-21  Thiemo Seufer  <seufer@csv.ica.uni-stuttgart.de>
 *		Hardware cursor support.
 *
 *	2016-02-21  Maciej W. Rozycki  <macro@linux-mips.org>
 *		Version 0.03: Rewritten for the new FB and TC APIs.
 */

#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/tc.h>
#include <linux/timer.h>

#include "bt455.h"
#include "bt431.h"

/* Version information */
#define DRIVER_VERSION "0.03"
#define DRIVER_AUTHOR "Karsten Merker <merker@linuxtag.org>"
#define DRIVER_DESCRIPTION "PMAG-AA Framebuffer Driver"

/*
 * Bt455 RAM DAC register base offset (rel. to TC slot base address).
 */
#define PMAG_AA_BT455_OFFSET		0x100000

/*
 * Bt431 cursor generator offset (rel. to TC slot base address).
 */
#define PMAG_AA_BT431_OFFSET		0x180000

/*
 * Begin of PMAG-AA framebuffer memory relative to TC slot address,
 * resolution is 1280x1024x1 (8 bits deep, but only LSB is used).
 */
#define PMAG_AA_ONBOARD_FBMEM_OFFSET	0x200000

struct aafb_par {
	void __iomem *mmio;
	struct bt455_regs __iomem *bt455;
	struct bt431_regs __iomem *bt431;
};

static const struct fb_var_screeninfo aafb_defined = {
	.xres		= 1280,
	.yres		= 1024,
	.xres_virtual	= 2048,
	.yres_virtual	= 1024,
	.bits_per_pixel	= 8,
	.grayscale	= 1,
	.red.length	= 0,
	.green.length	= 1,
	.blue.length	= 0,
	.activate	= FB_ACTIVATE_NOW,
	.accel_flags	= FB_ACCEL_NONE,
	.pixclock	= 7645,
	.left_margin	= 224,
	.right_margin	= 32,
	.upper_margin	= 33,
	.lower_margin	= 3,
	.hsync_len	= 160,
	.vsync_len	= 3,
	.sync		= FB_SYNC_ON_GREEN,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static const struct fb_fix_screeninfo aafb_fix = {
	.id		= "PMAG-AA",
	.smem_len	= (2048 * 1024),
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_MONO10,
	.ypanstep	= 1,
	.ywrapstep	= 1,
	.line_length	= 2048,
	.mmio_len	= PMAG_AA_ONBOARD_FBMEM_OFFSET - PMAG_AA_BT455_OFFSET,
};

static int aafb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	struct aafb_par *par = info->par;

	if (cursor->image.height > BT431_CURSOR_SIZE ||
	    cursor->image.width > BT431_CURSOR_SIZE) {
		bt431_erase_cursor(par->bt431);
		return -EINVAL;
	}

	if (!cursor->enable)
		bt431_erase_cursor(par->bt431);

	if (cursor->set & FB_CUR_SETPOS)
		bt431_position_cursor(par->bt431,
				      cursor->image.dx, cursor->image.dy);
	if (cursor->set & FB_CUR_SETCMAP) {
		u8 fg = cursor->image.fg_color ? 0xf : 0x0;
		u8 bg = cursor->image.bg_color ? 0xf : 0x0;

		bt455_write_cmap_entry(par->bt455, 8, bg);
		bt455_write_cmap_next(par->bt455, bg);
		bt455_write_ovly_next(par->bt455, fg);
	}
	if (cursor->set & (FB_CUR_SETSIZE | FB_CUR_SETSHAPE | FB_CUR_SETIMAGE))
		bt431_set_cursor(par->bt431,
				 cursor->image.data, cursor->mask, cursor->rop,
				 cursor->image.width, cursor->image.height);

	if (cursor->enable)
		bt431_enable_cursor(par->bt431);

	return 0;
}

/* 0 unblanks, any other blanks. */

static int aafb_blank(int blank, struct fb_info *info)
{
	struct aafb_par *par = info->par;
	u8 val = blank ? 0x00 : 0x0f;

	bt455_write_cmap_entry(par->bt455, 1, val);
	return 0;
}

static struct fb_ops aafb_ops = {
	.owner		= THIS_MODULE,
	.fb_blank	= aafb_blank,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_cursor	= aafb_cursor,
};

static int pmagaafb_probe(struct device *dev)
{
	struct tc_dev *tdev = to_tc_dev(dev);
	resource_size_t start, len;
	struct fb_info *info;
	struct aafb_par *par;
	int err;

	info = framebuffer_alloc(sizeof(struct aafb_par), dev);
	if (!info)
		return -ENOMEM;

	par = info->par;
	dev_set_drvdata(dev, info);

	info->fbops = &aafb_ops;
	info->fix = aafb_fix;
	info->var = aafb_defined;
	info->flags = FBINFO_DEFAULT;

	/* Request the I/O MEM resource. */
	start = tdev->resource.start;
	len = tdev->resource.end - start + 1;
	if (!request_mem_region(start, len, dev_name(dev))) {
		printk(KERN_ERR "%s: Cannot reserve FB region\n",
		       dev_name(dev));
		err = -EBUSY;
		goto err_alloc;
	}

	/* MMIO mapping setup. */
	info->fix.mmio_start = start + PMAG_AA_BT455_OFFSET;
	par->mmio = ioremap(info->fix.mmio_start, info->fix.mmio_len);
	if (!par->mmio) {
		printk(KERN_ERR "%s: Cannot map MMIO\n", dev_name(dev));
		err = -ENOMEM;
		goto err_resource;
	}
	par->bt455 = par->mmio - PMAG_AA_BT455_OFFSET + PMAG_AA_BT455_OFFSET;
	par->bt431 = par->mmio - PMAG_AA_BT455_OFFSET + PMAG_AA_BT431_OFFSET;

	/* Frame buffer mapping setup. */
	info->fix.smem_start = start + PMAG_AA_ONBOARD_FBMEM_OFFSET;
	info->screen_base = ioremap(info->fix.smem_start,
					    info->fix.smem_len);
	if (!info->screen_base) {
		printk(KERN_ERR "%s: Cannot map FB\n", dev_name(dev));
		err = -ENOMEM;
		goto err_mmio_map;
	}
	info->screen_size = info->fix.smem_len;

	/* Init colormap. */
	bt455_write_cmap_entry(par->bt455, 0, 0x0);
	bt455_write_cmap_next(par->bt455, 0xf);

	/* Init hardware cursor. */
	bt431_erase_cursor(par->bt431);
	bt431_init_cursor(par->bt431);

	err = register_framebuffer(info);
	if (err < 0) {
		printk(KERN_ERR "%s: Cannot register framebuffer\n",
		       dev_name(dev));
		goto err_smem_map;
	}

	get_device(dev);

	pr_info("fb%d: %s frame buffer device at %s\n",
		info->node, info->fix.id, dev_name(dev));

	return 0;


err_smem_map:
	iounmap(info->screen_base);

err_mmio_map:
	iounmap(par->mmio);

err_resource:
	release_mem_region(start, len);

err_alloc:
	framebuffer_release(info);
	return err;
}

static int pmagaafb_remove(struct device *dev)
{
	struct tc_dev *tdev = to_tc_dev(dev);
	struct fb_info *info = dev_get_drvdata(dev);
	struct aafb_par *par = info->par;
	resource_size_t start, len;

	put_device(dev);
	unregister_framebuffer(info);
	iounmap(info->screen_base);
	iounmap(par->mmio);
	start = tdev->resource.start;
	len = tdev->resource.end - start + 1;
	release_mem_region(start, len);
	framebuffer_release(info);
	return 0;
}

/*
 * Initialise the framebuffer.
 */
static const struct tc_device_id pmagaafb_tc_table[] = {
	{ "DEC     ", "PMAG-AA " },
	{ }
};
MODULE_DEVICE_TABLE(tc, pmagaafb_tc_table);

static struct tc_driver pmagaafb_driver = {
	.id_table	= pmagaafb_tc_table,
	.driver		= {
		.name	= "pmagaafb",
		.bus	= &tc_bus_type,
		.probe	= pmagaafb_probe,
		.remove	= pmagaafb_remove,
	},
};

static int __init pmagaafb_init(void)
{
#ifndef MODULE
	if (fb_get_options("pmagaafb", NULL))
		return -ENXIO;
#endif
	return tc_register_driver(&pmagaafb_driver);
}

static void __exit pmagaafb_exit(void)
{
	tc_unregister_driver(&pmagaafb_driver);
}

module_init(pmagaafb_init);
module_exit(pmagaafb_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_LICENSE("GPL");
