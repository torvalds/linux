/*
 *    Filename: cfag12864bfb.c
 *     Version: 0.1.0
 * Description: cfag12864b LCD framebuffer driver
 *     License: GPLv2
 *     Depends: cfag12864b
 *
 *      Author: Copyright (C) Miguel Ojeda Sandonis <maxextreme@gmail.com>
 *        Date: 2006-10-31
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/cfag12864b.h>

#define CFAG12864BFB_NAME "cfag12864bfb"

static struct fb_fix_screeninfo cfag12864bfb_fix __initdata = {
	.id = "cfag12864b",
	.type = FB_TYPE_PACKED_PIXELS,
	.visual = FB_VISUAL_MONO10,
	.xpanstep = 0,
	.ypanstep = 0,
	.ywrapstep = 0,
	.line_length = CFAG12864B_WIDTH / 8,
	.accel = FB_ACCEL_NONE,
};

static struct fb_var_screeninfo cfag12864bfb_var __initdata = {
	.xres = CFAG12864B_WIDTH,
	.yres = CFAG12864B_HEIGHT,
	.xres_virtual = CFAG12864B_WIDTH,
	.yres_virtual = CFAG12864B_HEIGHT,
	.bits_per_pixel = 1,
	.red = { 0, 1, 0 },
      	.green = { 0, 1, 0 },
      	.blue = { 0, 1, 0 },
	.left_margin = 0,
	.right_margin = 0,
	.upper_margin = 0,
	.lower_margin = 0,
	.vmode = FB_VMODE_NONINTERLACED,
};

static int cfag12864bfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	return vm_insert_page(vma, vma->vm_start,
		virt_to_page(cfag12864b_buffer));
}

static struct fb_ops cfag12864bfb_ops = {
	.owner = THIS_MODULE,
	.fb_read = fb_sys_read,
	.fb_write = fb_sys_write,
	.fb_fillrect = sys_fillrect,
	.fb_copyarea = sys_copyarea,
	.fb_imageblit = sys_imageblit,
	.fb_mmap = cfag12864bfb_mmap,
};

static int __init cfag12864bfb_probe(struct platform_device *device)
{
	int ret = -EINVAL;
 	struct fb_info *info = framebuffer_alloc(0, &device->dev);

	if (!info)
		goto none;

	info->screen_base = (char __iomem *) cfag12864b_buffer;
	info->screen_size = CFAG12864B_SIZE;
	info->fbops = &cfag12864bfb_ops;
	info->fix = cfag12864bfb_fix;
	info->var = cfag12864bfb_var;
	info->pseudo_palette = NULL;
	info->par = NULL;
	info->flags = FBINFO_FLAG_DEFAULT;

	if (register_framebuffer(info) < 0)
		goto fballoced;

	platform_set_drvdata(device, info);

	printk(KERN_INFO "fb%d: %s frame buffer device\n", info->node,
		info->fix.id);

	return 0;

fballoced:
	framebuffer_release(info);

none:
	return ret;
}

static int cfag12864bfb_remove(struct platform_device *device)
{
	struct fb_info *info = platform_get_drvdata(device);

	if (info) {
		unregister_framebuffer(info);
		framebuffer_release(info);
	}

	return 0;
}

static struct platform_driver cfag12864bfb_driver = {
	.probe	= cfag12864bfb_probe,
	.remove = cfag12864bfb_remove,
	.driver = {
		.name	= CFAG12864BFB_NAME,
	},
};

static struct platform_device *cfag12864bfb_device;

static int __init cfag12864bfb_init(void)
{
	int ret = -EINVAL;

	/* cfag12864b_init() must be called first */
	if (!cfag12864b_isinited()) {
		printk(KERN_ERR CFAG12864BFB_NAME ": ERROR: "
			"cfag12864b is not initialized\n");
		goto none;
	}

	if (cfag12864b_enable()) {
		printk(KERN_ERR CFAG12864BFB_NAME ": ERROR: "
			"can't enable cfag12864b refreshing (being used)\n");
		return -ENODEV;
	}

	ret = platform_driver_register(&cfag12864bfb_driver);

	if (!ret) {
		cfag12864bfb_device =
			platform_device_alloc(CFAG12864BFB_NAME, 0);

		if (cfag12864bfb_device)
			ret = platform_device_add(cfag12864bfb_device);
		else
			ret = -ENOMEM;

		if (ret) {
			platform_device_put(cfag12864bfb_device);
			platform_driver_unregister(&cfag12864bfb_driver);
		}
	}

none:
	return ret;
}

static void __exit cfag12864bfb_exit(void)
{
	platform_device_unregister(cfag12864bfb_device);
	platform_driver_unregister(&cfag12864bfb_driver);
	cfag12864b_disable();
}

module_init(cfag12864bfb_init);
module_exit(cfag12864bfb_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Miguel Ojeda Sandonis <maxextreme@gmail.com>");
MODULE_DESCRIPTION("cfag12864b LCD framebuffer driver");
