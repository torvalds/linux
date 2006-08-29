/*
 * framebuffer driver for Intel Based Mac's
 *
 * (c) 2006 Edgar Hucek <gimli@dark-green.com>
 * Original imac driver written by Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/screen_info.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/dmi.h>
#include <linux/efi.h>

#include <asm/io.h>

#include <video/vga.h>

typedef enum _MAC_TYPE {
	M_I17,
	M_I20,
	M_MINI,
	M_MACBOOK,
	M_UNKNOWN
} MAC_TYPE;

/* --------------------------------------------------------------------- */

static struct fb_var_screeninfo imacfb_defined __initdata = {
	.activate		= FB_ACTIVATE_NOW,
	.height			= -1,
	.width			= -1,
	.right_margin		= 32,
	.upper_margin		= 16,
	.lower_margin		= 4,
	.vsync_len		= 4,
	.vmode			= FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo imacfb_fix __initdata = {
	.id			= "IMAC VGA",
	.type			= FB_TYPE_PACKED_PIXELS,
	.accel			= FB_ACCEL_NONE,
	.visual			= FB_VISUAL_TRUECOLOR,
};

static int inverse;
static int model		= M_UNKNOWN;
static int manual_height;
static int manual_width;

static int set_system(struct dmi_system_id *id)
{
	printk(KERN_INFO "imacfb: %s detected - set system to %ld\n",
		id->ident, (long)id->driver_data);

	model = (long)id->driver_data;

	return 0;
}

static struct dmi_system_id __initdata dmi_system_table[] = {
	{ set_system, "iMac4,1", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"Apple Computer, Inc."),
	  DMI_MATCH(DMI_PRODUCT_NAME,"iMac4,1") }, (void*)M_I17},
	{ set_system, "MacBookPro1,1", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"Apple Computer, Inc."),
	  DMI_MATCH(DMI_PRODUCT_NAME,"MacBookPro1,1") }, (void*)M_I17},
	{ set_system, "MacBook1,1", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"Apple Computer, Inc."),
	  DMI_MATCH(DMI_PRODUCT_NAME,"MacBook1,1")}, (void *)M_MACBOOK},
	{ set_system, "Macmini1,1", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"Apple Computer, Inc."),
	  DMI_MATCH(DMI_PRODUCT_NAME,"Macmini1,1")}, (void *)M_MINI},
	{},
};

#define	DEFAULT_FB_MEM	1024*1024*16

/* --------------------------------------------------------------------- */

static int imacfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			    unsigned blue, unsigned transp,
			    struct fb_info *info)
{
	/*
	 *  Set a single color register. The values supplied are
	 *  already rounded down to the hardware's capabilities
	 *  (according to the entries in the `var' structure). Return
	 *  != 0 for invalid regno.
	 */

	if (regno >= info->cmap.len)
		return 1;

	if (regno < 16) {
		red   >>= 8;
		green >>= 8;
		blue  >>= 8;
		((u32 *)(info->pseudo_palette))[regno] =
			(red   << info->var.red.offset)   |
			(green << info->var.green.offset) |
			(blue  << info->var.blue.offset);
	}
	return 0;
}

static struct fb_ops imacfb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= imacfb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static int __init imacfb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt) continue;

		if (!strcmp(this_opt, "inverse"))
			inverse = 1;
		else if (!strcmp(this_opt, "i17"))
			model = M_I17;
		else if (!strcmp(this_opt, "i20"))
			model = M_I20;
		else if (!strcmp(this_opt, "mini"))
			model = M_MINI;
		else if (!strcmp(this_opt, "macbook"))
			model = M_MACBOOK;
		else if (!strncmp(this_opt, "height:", 7))
			manual_height = simple_strtoul(this_opt+7, NULL, 0);
		else if (!strncmp(this_opt, "width:", 6))
			manual_width = simple_strtoul(this_opt+6, NULL, 0);
	}
	return 0;
}

static int __init imacfb_probe(struct platform_device *dev)
{
	struct fb_info *info;
	int err;
	unsigned int size_vmode;
	unsigned int size_remap;
	unsigned int size_total;

	screen_info.lfb_depth = 32;
	screen_info.lfb_size = DEFAULT_FB_MEM / 0x10000;
	screen_info.pages=1;
	screen_info.blue_size = 8;
	screen_info.blue_pos = 0;
	screen_info.green_size = 8;
	screen_info.green_pos = 8;
	screen_info.red_size = 8;
	screen_info.red_pos = 16;
	screen_info.rsvd_size = 8;
	screen_info.rsvd_pos = 24;

	switch (model) {
	case M_I17:
		screen_info.lfb_width = 1440;
		screen_info.lfb_height = 900;
		screen_info.lfb_linelength = 1472 * 4;
		screen_info.lfb_base = 0x80010000;
		break;
	case M_I20:
		screen_info.lfb_width = 1680;
		screen_info.lfb_height = 1050;
		screen_info.lfb_linelength = 1728 * 4;
		screen_info.lfb_base = 0x80010000;
		break;
	case M_MINI:
		screen_info.lfb_width = 1024;
		screen_info.lfb_height = 768;
		screen_info.lfb_linelength = 2048 * 4;
		screen_info.lfb_base = 0x80000000;
		break;
	case M_MACBOOK:
		screen_info.lfb_width = 1280;
		screen_info.lfb_height = 800;
		screen_info.lfb_linelength = 2048 * 4;
		screen_info.lfb_base = 0x80000000;
		break;
 	}

	/* if the user wants to manually specify height/width,
	   we will override the defaults */
	/* TODO: eventually get auto-detection working */
	if (manual_height > 0)
		screen_info.lfb_height = manual_height;
	if (manual_width > 0)
		screen_info.lfb_width = manual_width;

	imacfb_fix.smem_start = screen_info.lfb_base;
	imacfb_defined.bits_per_pixel = screen_info.lfb_depth;
	imacfb_defined.xres = screen_info.lfb_width;
	imacfb_defined.yres = screen_info.lfb_height;
	imacfb_fix.line_length = screen_info.lfb_linelength;

	/*   size_vmode -- that is the amount of memory needed for the
	 *                 used video mode, i.e. the minimum amount of
	 *                 memory we need. */
	size_vmode = imacfb_defined.yres * imacfb_fix.line_length;

	/*   size_total -- all video memory we have. Used for
	 *                 entries, ressource allocation and bounds
	 *                 checking. */
	size_total = screen_info.lfb_size * 65536;
	if (size_total < size_vmode)
		size_total = size_vmode;

	/*   size_remap -- the amount of video memory we are going to
	 *                 use for imacfb.  With modern cards it is no
	 *                 option to simply use size_total as that
	 *                 wastes plenty of kernel address space. */
	size_remap  = size_vmode * 2;
	if (size_remap < size_vmode)
		size_remap = size_vmode;
	if (size_remap > size_total)
		size_remap = size_total;
	imacfb_fix.smem_len = size_remap;

#ifndef __i386__
	screen_info.imacpm_seg = 0;
#endif

	if (!request_mem_region(imacfb_fix.smem_start, size_total, "imacfb")) {
		printk(KERN_WARNING
		       "imacfb: cannot reserve video memory at 0x%lx\n",
			imacfb_fix.smem_start);
		/* We cannot make this fatal. Sometimes this comes from magic
		   spaces our resource handlers simply don't know about */
	}

	info = framebuffer_alloc(sizeof(u32) * 16, &dev->dev);
	if (!info) {
		err = -ENOMEM;
		goto err_release_mem;
	}
	info->pseudo_palette = info->par;
	info->par = NULL;

	info->screen_base = ioremap(imacfb_fix.smem_start, imacfb_fix.smem_len);
	if (!info->screen_base) {
		printk(KERN_ERR "imacfb: abort, cannot ioremap video memory "
				"0x%x @ 0x%lx\n",
			imacfb_fix.smem_len, imacfb_fix.smem_start);
		err = -EIO;
		goto err_unmap;
	}

	printk(KERN_INFO "imacfb: framebuffer at 0x%lx, mapped to 0x%p, "
	       "using %dk, total %dk\n",
	       imacfb_fix.smem_start, info->screen_base,
	       size_remap/1024, size_total/1024);
	printk(KERN_INFO "imacfb: mode is %dx%dx%d, linelength=%d, pages=%d\n",
	       imacfb_defined.xres, imacfb_defined.yres,
	       imacfb_defined.bits_per_pixel, imacfb_fix.line_length,
	       screen_info.pages);

	imacfb_defined.xres_virtual = imacfb_defined.xres;
	imacfb_defined.yres_virtual = imacfb_fix.smem_len /
					imacfb_fix.line_length;
	printk(KERN_INFO "imacfb: scrolling: redraw\n");
	imacfb_defined.yres_virtual = imacfb_defined.yres;

	/* some dummy values for timing to make fbset happy */
	imacfb_defined.pixclock     = 10000000 / imacfb_defined.xres *
					1000 / imacfb_defined.yres;
	imacfb_defined.left_margin  = (imacfb_defined.xres / 8) & 0xf8;
	imacfb_defined.hsync_len    = (imacfb_defined.xres / 8) & 0xf8;

	imacfb_defined.red.offset    = screen_info.red_pos;
	imacfb_defined.red.length    = screen_info.red_size;
	imacfb_defined.green.offset  = screen_info.green_pos;
	imacfb_defined.green.length  = screen_info.green_size;
	imacfb_defined.blue.offset   = screen_info.blue_pos;
	imacfb_defined.blue.length   = screen_info.blue_size;
	imacfb_defined.transp.offset = screen_info.rsvd_pos;
	imacfb_defined.transp.length = screen_info.rsvd_size;

	printk(KERN_INFO "imacfb: %s: "
	       "size=%d:%d:%d:%d, shift=%d:%d:%d:%d\n",
	       "Truecolor",
	       screen_info.rsvd_size,
	       screen_info.red_size,
	       screen_info.green_size,
	       screen_info.blue_size,
	       screen_info.rsvd_pos,
	       screen_info.red_pos,
	       screen_info.green_pos,
	       screen_info.blue_pos);

	imacfb_fix.ypanstep  = 0;
	imacfb_fix.ywrapstep = 0;

	/* request failure does not faze us, as vgacon probably has this
	 * region already (FIXME) */
	request_region(0x3c0, 32, "imacfb");

	info->fbops = &imacfb_ops;
	info->var = imacfb_defined;
	info->fix = imacfb_fix;
	info->flags = FBINFO_FLAG_DEFAULT;

	if (fb_alloc_cmap(&info->cmap, 256, 0) < 0) {
		err = -ENOMEM;
		goto err_unmap;
	}
	if (register_framebuffer(info)<0) {
		err = -EINVAL;
		goto err_fb_dealoc;
	}
	printk(KERN_INFO "fb%d: %s frame buffer device\n",
	       info->node, info->fix.id);
	return 0;

err_fb_dealoc:
	fb_dealloc_cmap(&info->cmap);
err_unmap:
	iounmap(info->screen_base);
	framebuffer_release(info);
err_release_mem:
	release_mem_region(imacfb_fix.smem_start, size_total);
	return err;
}

static struct platform_driver imacfb_driver = {
	.probe	= imacfb_probe,
	.driver	= {
		.name	= "imacfb",
	},
};

static struct platform_device imacfb_device = {
	.name	= "imacfb",
};

static int __init imacfb_init(void)
{
	int ret;
	char *option = NULL;

	if (!efi_enabled)
		return -ENODEV;
	if (!dmi_check_system(dmi_system_table))
		return -ENODEV;
	if (model == M_UNKNOWN)
		return -ENODEV;

	if (fb_get_options("imacfb", &option))
		return -ENODEV;

	imacfb_setup(option);
	ret = platform_driver_register(&imacfb_driver);

	if (!ret) {
		ret = platform_device_register(&imacfb_device);
		if (ret)
			platform_driver_unregister(&imacfb_driver);
	}
	return ret;
}
module_init(imacfb_init);

MODULE_LICENSE("GPL");
