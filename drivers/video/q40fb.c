/*
 * linux/drivers/video/q40fb.c -- Q40 frame buffer device
 *
 * Copyright (C) 2001
 *
 *      Richard Zidlicky <rz@linux-m68k.org>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <asm/uaccess.h>
#include <asm/setup.h>
#include <asm/system.h>
#include <asm/q40_master.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <asm/pgtable.h>

#define Q40_PHYS_SCREEN_ADDR 0xFE800000

static struct fb_fix_screeninfo q40fb_fix __initdata = {
	.id		= "Q40",
	.smem_len	= 1024*1024,
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_TRUECOLOR,
	.line_length	= 1024*2,
	.accel		= FB_ACCEL_NONE,
};

static struct fb_var_screeninfo q40fb_var __initdata = {
	.xres		= 1024,
	.yres		= 512,
	.xres_virtual	= 1024,
	.yres_virtual	= 512,
	.bits_per_pixel	= 16,
    	.red		= {6, 5, 0},
	.green		= {11, 5, 0},
	.blue		= {0, 6, 0},
	.activate	= FB_ACTIVATE_NOW,
	.height		= 230,
	.width		= 300,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static int q40fb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp,
			   struct fb_info *info)
{
    /*
     *  Set a single color register. The values supplied have a 16 bit
     *  magnitude.
     *  Return != 0 for invalid regno.
     */

    if (regno > 255)
	    return 1;
    red>>=11;
    green>>=11;
    blue>>=10;

    if (regno < 16) {
	((u32 *)info->pseudo_palette)[regno] = ((red & 31) <<6) |
					       ((green & 31) << 11) |
					       (blue & 63);
    }
    return 0;
}

static struct fb_ops q40fb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= q40fb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static int __devinit q40fb_probe(struct platform_device *dev)
{
	struct fb_info *info;

	if (!MACH_IS_Q40)
		return -ENXIO;

	/* mapped in q40/config.c */
	q40fb_fix.smem_start = Q40_PHYS_SCREEN_ADDR;

	info = framebuffer_alloc(sizeof(u32) * 16, &dev->dev);
	if (!info)
		return -ENOMEM;

	info->var = q40fb_var;
	info->fix = q40fb_fix;
	info->fbops = &q40fb_ops;
	info->flags = FBINFO_DEFAULT;  /* not as module for now */
	info->pseudo_palette = info->par;
	info->par = NULL;
	info->screen_base = (char *) q40fb_fix.smem_start;

	if (fb_alloc_cmap(&info->cmap, 256, 0) < 0) {
		framebuffer_release(info);
		return -ENOMEM;
	}

	master_outb(3, DISPLAY_CONTROL_REG);

	if (register_framebuffer(info) < 0) {
		printk(KERN_ERR "Unable to register Q40 frame buffer\n");
		fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
		return -EINVAL;
	}

        printk(KERN_INFO "fb%d: Q40 frame buffer alive and kicking !\n",
	       info->node);
	return 0;
}

static struct platform_driver q40fb_driver = {
	.probe	= q40fb_probe,
	.driver	= {
		.name	= "q40fb",
	},
};

static struct platform_device q40fb_device = {
	.name	= "q40fb",
};

int __init q40fb_init(void)
{
	int ret = 0;

	if (fb_get_options("q40fb", NULL))
		return -ENODEV;

	ret = platform_driver_register(&q40fb_driver);

	if (!ret) {
		ret = platform_device_register(&q40fb_device);
		if (ret)
			platform_driver_unregister(&q40fb_driver);
	}
	return ret;
}

module_init(q40fb_init);
MODULE_LICENSE("GPL");
