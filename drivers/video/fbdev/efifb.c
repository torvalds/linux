/*
 * Framebuffer driver for EFI/UEFI based system
 *
 * (c) 2006 Edgar Hucek <gimli@dark-green.com>
 * Original efi driver written by Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */

#include <linux/kernel.h>
#include <linux/efi.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/platform_device.h>
#include <linux/screen_info.h>
#include <video/vga.h>
#include <asm/efi.h>

static bool request_mem_succeeded = false;

static struct fb_var_screeninfo efifb_defined = {
	.activate		= FB_ACTIVATE_NOW,
	.height			= -1,
	.width			= -1,
	.right_margin		= 32,
	.upper_margin		= 16,
	.lower_margin		= 4,
	.vsync_len		= 4,
	.vmode			= FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo efifb_fix = {
	.id			= "EFI VGA",
	.type			= FB_TYPE_PACKED_PIXELS,
	.accel			= FB_ACCEL_NONE,
	.visual			= FB_VISUAL_TRUECOLOR,
};

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
	if (request_mem_succeeded)
		release_mem_region(info->apertures->ranges[0].base,
				   info->apertures->ranges[0].size);
	fb_dealloc_cmap(&info->cmap);
}

static struct fb_ops efifb_ops = {
	.owner		= THIS_MODULE,
	.fb_destroy	= efifb_destroy,
	.fb_setcolreg	= efifb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static int efifb_setup(char *options)
{
	char *this_opt;

	if (options && *options) {
		while ((this_opt = strsep(&options, ",")) != NULL) {
			if (!*this_opt) continue;

			efifb_setup_from_dmi(&screen_info, this_opt);

			if (!strncmp(this_opt, "base:", 5))
				screen_info.lfb_base = simple_strtoul(this_opt+5, NULL, 0);
			else if (!strncmp(this_opt, "stride:", 7))
				screen_info.lfb_linelength = simple_strtoul(this_opt+7, NULL, 0) * 4;
			else if (!strncmp(this_opt, "height:", 7))
				screen_info.lfb_height = simple_strtoul(this_opt+7, NULL, 0);
			else if (!strncmp(this_opt, "width:", 6))
				screen_info.lfb_width = simple_strtoul(this_opt+6, NULL, 0);
		}
	}

	return 0;
}

static inline bool fb_base_is_valid(void)
{
	if (screen_info.lfb_base)
		return true;

	if (!(screen_info.capabilities & VIDEO_CAPABILITY_64BIT_BASE))
		return false;

	if (screen_info.ext_lfb_base)
		return true;

	return false;
}

static int efifb_probe(struct platform_device *dev)
{
	struct fb_info *info;
	int err;
	unsigned int size_vmode;
	unsigned int size_remap;
	unsigned int size_total;
	char *option = NULL;

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

	if (!screen_info.lfb_depth)
		screen_info.lfb_depth = 32;
	if (!screen_info.pages)
		screen_info.pages = 1;
	if (!fb_base_is_valid()) {
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

	if (screen_info.capabilities & VIDEO_CAPABILITY_64BIT_BASE) {
		u64 ext_lfb_base;

		ext_lfb_base = (u64)(unsigned long)screen_info.ext_lfb_base << 32;
		efifb_fix.smem_start |= ext_lfb_base;
	}

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
		request_mem_succeeded = true;
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
	platform_set_drvdata(dev, info);
	info->pseudo_palette = info->par;
	info->par = NULL;

	info->apertures = alloc_apertures(1);
	if (!info->apertures) {
		err = -ENOMEM;
		goto err_release_fb;
	}
	info->apertures->ranges[0].base = efifb_fix.smem_start;
	info->apertures->ranges[0].size = size_remap;

	info->screen_base = ioremap_wc(efifb_fix.smem_start, efifb_fix.smem_len);
	if (!info->screen_base) {
		printk(KERN_ERR "efifb: abort, cannot ioremap video memory "
				"0x%x @ 0x%lx\n",
			efifb_fix.smem_len, efifb_fix.smem_start);
		err = -EIO;
		goto err_release_fb;
	}

	printk(KERN_INFO "efifb: framebuffer at 0x%lx, using %dk, total %dk\n",
	       efifb_fix.smem_start, size_remap/1024, size_total/1024);
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
	fb_info(info, "%s frame buffer device\n", info->fix.id);
	return 0;

err_fb_dealoc:
	fb_dealloc_cmap(&info->cmap);
err_unmap:
	iounmap(info->screen_base);
err_release_fb:
	framebuffer_release(info);
err_release_mem:
	if (request_mem_succeeded)
		release_mem_region(efifb_fix.smem_start, size_total);
	return err;
}

static int efifb_remove(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);

	unregister_framebuffer(info);
	framebuffer_release(info);

	return 0;
}

static struct platform_driver efifb_driver = {
	.driver = {
		.name = "efi-framebuffer",
	},
	.probe = efifb_probe,
	.remove = efifb_remove,
};

builtin_platform_driver(efifb_driver);
