/*
 * Framebuffer driver for EFI/UEFI based system
 *
 * (c) 2006 Edgar Hucek <gimli@dark-green.com>
 * Original efi driver written by Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/platform_device.h>
#include <linux/screen_info.h>
#include <linux/dmi.h>
#include <linux/pci.h>
#include <video/vga.h>

static struct fb_var_screeninfo efifb_defined __initdata = {
	.activate		= FB_ACTIVATE_NOW,
	.height			= -1,
	.width			= -1,
	.right_margin		= 32,
	.upper_margin		= 16,
	.lower_margin		= 4,
	.vsync_len		= 4,
	.vmode			= FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo efifb_fix __initdata = {
	.id			= "EFI VGA",
	.type			= FB_TYPE_PACKED_PIXELS,
	.accel			= FB_ACCEL_NONE,
	.visual			= FB_VISUAL_TRUECOLOR,
};

enum {
	M_I17,		/* 17-Inch iMac */
	M_I20,		/* 20-Inch iMac */
	M_I20_SR,	/* 20-Inch iMac (Santa Rosa) */
	M_I24,		/* 24-Inch iMac */
	M_I24_8_1,	/* 24-Inch iMac, 8,1th gen */
	M_I24_10_1,	/* 24-Inch iMac, 10,1th gen */
	M_I27_11_1,	/* 27-Inch iMac, 11,1th gen */
	M_MINI,		/* Mac Mini */
	M_MINI_3_1,	/* Mac Mini, 3,1th gen */
	M_MINI_4_1,	/* Mac Mini, 4,1th gen */
	M_MB,		/* MacBook */
	M_MB_2,		/* MacBook, 2nd rev. */
	M_MB_3,		/* MacBook, 3rd rev. */
	M_MB_5_1,	/* MacBook, 5th rev. */
	M_MB_6_1,	/* MacBook, 6th rev. */
	M_MB_7_1,	/* MacBook, 7th rev. */
	M_MB_SR,	/* MacBook, 2nd gen, (Santa Rosa) */
	M_MBA,		/* MacBook Air */
	M_MBP,		/* MacBook Pro */
	M_MBP_2,	/* MacBook Pro 2nd gen */
	M_MBP_2_2,	/* MacBook Pro 2,2nd gen */
	M_MBP_SR,	/* MacBook Pro (Santa Rosa) */
	M_MBP_4,	/* MacBook Pro, 4th gen */
	M_MBP_5_1,    /* MacBook Pro, 5,1th gen */
	M_MBP_5_2,	/* MacBook Pro, 5,2th gen */
	M_MBP_5_3,	/* MacBook Pro, 5,3rd gen */
	M_MBP_6_1,	/* MacBook Pro, 6,1th gen */
	M_MBP_6_2,	/* MacBook Pro, 6,2th gen */
	M_MBP_7_1,	/* MacBook Pro, 7,1th gen */
	M_UNKNOWN	/* placeholder */
};

static struct efifb_dmi_info {
	char *optname;
	unsigned long base;
	int stride;
	int width;
	int height;
} dmi_list[] = {
	[M_I17] = { "i17", 0x80010000, 1472 * 4, 1440, 900 },
	[M_I20] = { "i20", 0x80010000, 1728 * 4, 1680, 1050 }, /* guess */
	[M_I20_SR] = { "imac7", 0x40010000, 1728 * 4, 1680, 1050 },
	[M_I24] = { "i24", 0x80010000, 2048 * 4, 1920, 1200 }, /* guess */
	[M_I24_8_1] = { "imac8", 0xc0060000, 2048 * 4, 1920, 1200 },
	[M_I24_10_1] = { "imac10", 0xc0010000, 2048 * 4, 1920, 1080 },
	[M_I27_11_1] = { "imac11", 0xc0010000, 2560 * 4, 2560, 1440 },
	[M_MINI]= { "mini", 0x80000000, 2048 * 4, 1024, 768 },
	[M_MINI_3_1] = { "mini31", 0x40010000, 1024 * 4, 1024, 768 },
	[M_MINI_4_1] = { "mini41", 0xc0010000, 2048 * 4, 1920, 1200 },
	[M_MB] = { "macbook", 0x80000000, 2048 * 4, 1280, 800 },
	[M_MB_5_1] = { "macbook51", 0x80010000, 2048 * 4, 1280, 800 },
	[M_MB_6_1] = { "macbook61", 0x80010000, 2048 * 4, 1280, 800 },
	[M_MB_7_1] = { "macbook71", 0x80010000, 2048 * 4, 1280, 800 },
	[M_MBA] = { "mba", 0x80000000, 2048 * 4, 1280, 800 },
	[M_MBP] = { "mbp", 0x80010000, 1472 * 4, 1440, 900 },
	[M_MBP_2] = { "mbp2", 0, 0, 0, 0 }, /* placeholder */
	[M_MBP_2_2] = { "mbp22", 0x80010000, 1472 * 4, 1440, 900 },
	[M_MBP_SR] = { "mbp3", 0x80030000, 2048 * 4, 1440, 900 },
	[M_MBP_4] = { "mbp4", 0xc0060000, 2048 * 4, 1920, 1200 },
	[M_MBP_5_1] = { "mbp51", 0xc0010000, 2048 * 4, 1440, 900 },
	[M_MBP_5_2] = { "mbp52", 0xc0010000, 2048 * 4, 1920, 1200 },
	[M_MBP_5_3] = { "mbp53", 0xd0010000, 2048 * 4, 1440, 900 },
	[M_MBP_6_1] = { "mbp61", 0x90030000, 2048 * 4, 1920, 1200 },
	[M_MBP_6_2] = { "mbp62", 0x90030000, 2048 * 4, 1680, 1050 },
	[M_MBP_7_1] = { "mbp71", 0xc0010000, 2048 * 4, 1280, 800 },
	[M_UNKNOWN] = { NULL, 0, 0, 0, 0 }
};

static int set_system(const struct dmi_system_id *id);

#define EFIFB_DMI_SYSTEM_ID(vendor, name, enumid)		\
	{ set_system, name, {					\
		DMI_MATCH(DMI_BIOS_VENDOR, vendor),		\
		DMI_MATCH(DMI_PRODUCT_NAME, name) },		\
	  &dmi_list[enumid] }

static struct dmi_system_id __initdata dmi_system_table[] = {
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "iMac4,1", M_I17),
	/* At least one of these two will be right; maybe both? */
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "iMac5,1", M_I20),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "iMac5,1", M_I20),
	/* At least one of these two will be right; maybe both? */
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "iMac6,1", M_I24),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "iMac6,1", M_I24),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "iMac7,1", M_I20_SR),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "iMac8,1", M_I24_8_1),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "iMac10,1", M_I24_10_1),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "iMac11,1", M_I27_11_1),
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "Macmini1,1", M_MINI),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "Macmini3,1", M_MINI_3_1),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "Macmini4,1", M_MINI_4_1),
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "MacBook1,1", M_MB),
	/* At least one of these two will be right; maybe both? */
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "MacBook2,1", M_MB),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBook2,1", M_MB),
	/* At least one of these two will be right; maybe both? */
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "MacBook3,1", M_MB),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBook3,1", M_MB),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBook4,1", M_MB),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBook5,1", M_MB_5_1),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBook6,1", M_MB_6_1),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBook7,1", M_MB_7_1),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookAir1,1", M_MBA),
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "MacBookPro1,1", M_MBP),
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "MacBookPro2,1", M_MBP_2),
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "MacBookPro2,2", M_MBP_2_2),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookPro2,1", M_MBP_2),
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "MacBookPro3,1", M_MBP_SR),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookPro3,1", M_MBP_SR),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookPro4,1", M_MBP_4),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookPro5,1", M_MBP_5_1),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookPro5,2", M_MBP_5_2),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookPro5,3", M_MBP_5_3),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookPro6,1", M_MBP_6_1),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookPro6,2", M_MBP_6_2),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookPro7,1", M_MBP_7_1),
	{},
};

static int set_system(const struct dmi_system_id *id)
{
	struct efifb_dmi_info *info = id->driver_data;
	if (info->base == 0)
		return 0;

	printk(KERN_INFO "efifb: dmi detected %s - framebuffer at %p "
			 "(%dx%d, stride %d)\n", id->ident,
			 (void *)info->base, info->width, info->height,
			 info->stride);

	/* Trust the bootloader over the DMI tables */
	if (screen_info.lfb_base == 0) {
#if defined(CONFIG_PCI)
		struct pci_dev *dev = NULL;
		int found_bar = 0;
#endif
		screen_info.lfb_base = info->base;

#if defined(CONFIG_PCI)
		/* make sure that the address in the table is actually on a
		 * VGA device's PCI BAR */

		for_each_pci_dev(dev) {
			int i;
			if ((dev->class >> 8) != PCI_CLASS_DISPLAY_VGA)
				continue;
			for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
				resource_size_t start, end;

				start = pci_resource_start(dev, i);
				if (start == 0)
					break;
				end = pci_resource_end(dev, i);
				if (screen_info.lfb_base >= start &&
						screen_info.lfb_base < end) {
					found_bar = 1;
				}
			}
		}
		if (!found_bar)
			screen_info.lfb_base = 0;
#endif
	}
	if (screen_info.lfb_base) {
		if (screen_info.lfb_linelength == 0)
			screen_info.lfb_linelength = info->stride;
		if (screen_info.lfb_width == 0)
			screen_info.lfb_width = info->width;
		if (screen_info.lfb_height == 0)
			screen_info.lfb_height = info->height;
		if (screen_info.orig_video_isVGA == 0)
			screen_info.orig_video_isVGA = VIDEO_TYPE_EFI;
	} else {
		screen_info.lfb_linelength = 0;
		screen_info.lfb_width = 0;
		screen_info.lfb_height = 0;
		screen_info.orig_video_isVGA = 0;
		return 0;
	}
	return 1;
}

static int efifb_setcolreg(unsigned regno, unsigned red, unsigned green,
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

static void efifb_destroy(struct fb_info *info)
{
	if (info->screen_base)
		iounmap(info->screen_base);
	release_mem_region(info->aperture_base, info->aperture_size);
	framebuffer_release(info);
}

static struct fb_ops efifb_ops = {
	.owner		= THIS_MODULE,
	.fb_destroy	= efifb_destroy,
	.fb_setcolreg	= efifb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static int __init efifb_setup(char *options)
{
	char *this_opt;
	int i;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt) continue;

		for (i = 0; i < M_UNKNOWN; i++) {
			if (!strcmp(this_opt, dmi_list[i].optname) &&
					dmi_list[i].base != 0) {
				screen_info.lfb_base = dmi_list[i].base;
				screen_info.lfb_linelength = dmi_list[i].stride;
				screen_info.lfb_width = dmi_list[i].width;
				screen_info.lfb_height = dmi_list[i].height;
			}
		}
		if (!strncmp(this_opt, "base:", 5))
			screen_info.lfb_base = simple_strtoul(this_opt+5, NULL, 0);
		else if (!strncmp(this_opt, "stride:", 7))
			screen_info.lfb_linelength = simple_strtoul(this_opt+7, NULL, 0) * 4;
		else if (!strncmp(this_opt, "height:", 7))
			screen_info.lfb_height = simple_strtoul(this_opt+7, NULL, 0);
		else if (!strncmp(this_opt, "width:", 6))
			screen_info.lfb_width = simple_strtoul(this_opt+6, NULL, 0);
	}
	return 0;
}

static int __init efifb_probe(struct platform_device *dev)
{
	struct fb_info *info;
	int err;
	unsigned int size_vmode;
	unsigned int size_remap;
	unsigned int size_total;
	int request_succeeded = 0;

	if (!screen_info.lfb_depth)
		screen_info.lfb_depth = 32;
	if (!screen_info.pages)
		screen_info.pages = 1;
	if (!screen_info.lfb_base) {
		printk(KERN_DEBUG "efifb: invalid framebuffer address\n");
		return -ENODEV;
	}
	printk(KERN_INFO "efifb: probing for efifb\n");

	/* just assume they're all unset if any are */
	if (!screen_info.blue_size) {
		screen_info.blue_size = 8;
		screen_info.blue_pos = 0;
		screen_info.green_size = 8;
		screen_info.green_pos = 8;
		screen_info.red_size = 8;
		screen_info.red_pos = 16;
		screen_info.rsvd_size = 8;
		screen_info.rsvd_pos = 24;
	}

	efifb_fix.smem_start = screen_info.lfb_base;
	efifb_defined.bits_per_pixel = screen_info.lfb_depth;
	efifb_defined.xres = screen_info.lfb_width;
	efifb_defined.yres = screen_info.lfb_height;
	efifb_fix.line_length = screen_info.lfb_linelength;

	/*   size_vmode -- that is the amount of memory needed for the
	 *                 used video mode, i.e. the minimum amount of
	 *                 memory we need. */
	size_vmode = efifb_defined.yres * efifb_fix.line_length;

	/*   size_total -- all video memory we have. Used for
	 *                 entries, ressource allocation and bounds
	 *                 checking. */
	size_total = screen_info.lfb_size;
	if (size_total < size_vmode)
		size_total = size_vmode;

	/*   size_remap -- the amount of video memory we are going to
	 *                 use for efifb.  With modern cards it is no
	 *                 option to simply use size_total as that
	 *                 wastes plenty of kernel address space. */
	size_remap  = size_vmode * 2;
	if (size_remap > size_total)
		size_remap = size_total;
	if (size_remap % PAGE_SIZE)
		size_remap += PAGE_SIZE - (size_remap % PAGE_SIZE);
	efifb_fix.smem_len = size_remap;

	if (request_mem_region(efifb_fix.smem_start, size_remap, "efifb")) {
		request_succeeded = 1;
	} else {
		/* We cannot make this fatal. Sometimes this comes from magic
		   spaces our resource handlers simply don't know about */
		printk(KERN_WARNING
		       "efifb: cannot reserve video memory at 0x%lx\n",
			efifb_fix.smem_start);
	}

	info = framebuffer_alloc(sizeof(u32) * 16, &dev->dev);
	if (!info) {
		printk(KERN_ERR "efifb: cannot allocate framebuffer\n");
		err = -ENOMEM;
		goto err_release_mem;
	}
	info->pseudo_palette = info->par;
	info->par = NULL;

	info->aperture_base = efifb_fix.smem_start;
	info->aperture_size = size_remap;

	info->screen_base = ioremap(efifb_fix.smem_start, efifb_fix.smem_len);
	if (!info->screen_base) {
		printk(KERN_ERR "efifb: abort, cannot ioremap video memory "
				"0x%x @ 0x%lx\n",
			efifb_fix.smem_len, efifb_fix.smem_start);
		err = -EIO;
		goto err_release_fb;
	}

	printk(KERN_INFO "efifb: framebuffer at 0x%lx, mapped to 0x%p, "
	       "using %dk, total %dk\n",
	       efifb_fix.smem_start, info->screen_base,
	       size_remap/1024, size_total/1024);
	printk(KERN_INFO "efifb: mode is %dx%dx%d, linelength=%d, pages=%d\n",
	       efifb_defined.xres, efifb_defined.yres,
	       efifb_defined.bits_per_pixel, efifb_fix.line_length,
	       screen_info.pages);

	efifb_defined.xres_virtual = efifb_defined.xres;
	efifb_defined.yres_virtual = efifb_fix.smem_len /
					efifb_fix.line_length;
	printk(KERN_INFO "efifb: scrolling: redraw\n");
	efifb_defined.yres_virtual = efifb_defined.yres;

	/* some dummy values for timing to make fbset happy */
	efifb_defined.pixclock     = 10000000 / efifb_defined.xres *
					1000 / efifb_defined.yres;
	efifb_defined.left_margin  = (efifb_defined.xres / 8) & 0xf8;
	efifb_defined.hsync_len    = (efifb_defined.xres / 8) & 0xf8;

	efifb_defined.red.offset    = screen_info.red_pos;
	efifb_defined.red.length    = screen_info.red_size;
	efifb_defined.green.offset  = screen_info.green_pos;
	efifb_defined.green.length  = screen_info.green_size;
	efifb_defined.blue.offset   = screen_info.blue_pos;
	efifb_defined.blue.length   = screen_info.blue_size;
	efifb_defined.transp.offset = screen_info.rsvd_pos;
	efifb_defined.transp.length = screen_info.rsvd_size;

	printk(KERN_INFO "efifb: %s: "
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

	efifb_fix.ypanstep  = 0;
	efifb_fix.ywrapstep = 0;

	info->fbops = &efifb_ops;
	info->var = efifb_defined;
	info->fix = efifb_fix;
	info->flags = FBINFO_FLAG_DEFAULT | FBINFO_MISC_FIRMWARE;

	if ((err = fb_alloc_cmap(&info->cmap, 256, 0)) < 0) {
		printk(KERN_ERR "efifb: cannot allocate colormap\n");
		goto err_unmap;
	}
	if ((err = register_framebuffer(info)) < 0) {
		printk(KERN_ERR "efifb: cannot register framebuffer\n");
		goto err_fb_dealoc;
	}
	printk(KERN_INFO "fb%d: %s frame buffer device\n",
		info->node, info->fix.id);
	return 0;

err_fb_dealoc:
	fb_dealloc_cmap(&info->cmap);
err_unmap:
	iounmap(info->screen_base);
err_release_fb:
	framebuffer_release(info);
err_release_mem:
	if (request_succeeded)
		release_mem_region(efifb_fix.smem_start, size_total);
	return err;
}

static struct platform_driver efifb_driver = {
	.probe	= efifb_probe,
	.driver	= {
		.name	= "efifb",
	},
};

static struct platform_device efifb_device = {
	.name	= "efifb",
};

static int __init efifb_init(void)
{
	int ret;
	char *option = NULL;

	dmi_check_system(dmi_system_table);

	if (screen_info.orig_video_isVGA != VIDEO_TYPE_EFI)
		return -ENODEV;

	if (fb_get_options("efifb", &option))
		return -ENODEV;
	efifb_setup(option);

	/* We don't get linelength from UGA Draw Protocol, only from
	 * EFI Graphics Protocol.  So if it's not in DMI, and it's not
	 * passed in from the user, we really can't use the framebuffer.
	 */
	if (!screen_info.lfb_linelength)
		return -ENODEV;

	ret = platform_driver_register(&efifb_driver);

	if (!ret) {
		ret = platform_device_register(&efifb_device);
		if (ret)
			platform_driver_unregister(&efifb_driver);
	}
	return ret;
}
module_init(efifb_init);

MODULE_LICENSE("GPL");
