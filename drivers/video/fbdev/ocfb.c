/*
 * OpenCores VGA/LCD 2.0 core frame buffer driver
 *
 * Copyright (C) 2013 Stefan Kristiansson, stefan.kristiansson@saunalahti.fi
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/slab.h>

/* OCFB register defines */
#define OCFB_CTRL	0x000
#define OCFB_STAT	0x004
#define OCFB_HTIM	0x008
#define OCFB_VTIM	0x00c
#define OCFB_HVLEN	0x010
#define OCFB_VBARA	0x014
#define OCFB_PALETTE	0x800

#define OCFB_CTRL_VEN	0x00000001 /* Video Enable */
#define OCFB_CTRL_HIE	0x00000002 /* HSync Interrupt Enable */
#define OCFB_CTRL_PC	0x00000800 /* 8-bit Pseudo Color Enable*/
#define OCFB_CTRL_CD8	0x00000000 /* Color Depth 8 */
#define OCFB_CTRL_CD16	0x00000200 /* Color Depth 16 */
#define OCFB_CTRL_CD24	0x00000400 /* Color Depth 24 */
#define OCFB_CTRL_CD32	0x00000600 /* Color Depth 32 */
#define OCFB_CTRL_VBL1	0x00000000 /* Burst Length 1 */
#define OCFB_CTRL_VBL2	0x00000080 /* Burst Length 2 */
#define OCFB_CTRL_VBL4	0x00000100 /* Burst Length 4 */
#define OCFB_CTRL_VBL8	0x00000180 /* Burst Length 8 */

#define PALETTE_SIZE	256

#define OCFB_NAME	"OC VGA/LCD"

static char *mode_option;

static const struct fb_videomode default_mode = {
	/* 640x480 @ 60 Hz, 31.5 kHz hsync */
	NULL, 60, 640, 480, 39721, 40, 24, 32, 11, 96, 2,
	0, FB_VMODE_NONINTERLACED
};

struct ocfb_dev {
	struct fb_info info;
	void __iomem *regs;
	/* flag indicating whether the regs are little endian accessed */
	int little_endian;
	/* Physical and virtual addresses of framebuffer */
	dma_addr_t fb_phys;
	void __iomem *fb_virt;
	u32 pseudo_palette[PALETTE_SIZE];
};

#ifndef MODULE
static int __init ocfb_setup(char *options)
{
	char *curr_opt;

	if (!options || !*options)
		return 0;

	while ((curr_opt = strsep(&options, ",")) != NULL) {
		if (!*curr_opt)
			continue;
		mode_option = curr_opt;
	}

	return 0;
}
#endif

static inline u32 ocfb_readreg(struct ocfb_dev *fbdev, loff_t offset)
{
	if (fbdev->little_endian)
		return ioread32(fbdev->regs + offset);
	else
		return ioread32be(fbdev->regs + offset);
}

static void ocfb_writereg(struct ocfb_dev *fbdev, loff_t offset, u32 data)
{
	if (fbdev->little_endian)
		iowrite32(data, fbdev->regs + offset);
	else
		iowrite32be(data, fbdev->regs + offset);
}

static int ocfb_setupfb(struct ocfb_dev *fbdev)
{
	unsigned long bpp_config;
	struct fb_var_screeninfo *var = &fbdev->info.var;
	struct device *dev = fbdev->info.device;
	u32 hlen;
	u32 vlen;

	/* Disable display */
	ocfb_writereg(fbdev, OCFB_CTRL, 0);

	/* Register framebuffer address */
	fbdev->little_endian = 0;
	ocfb_writereg(fbdev, OCFB_VBARA, fbdev->fb_phys);

	/* Detect endianess */
	if (ocfb_readreg(fbdev, OCFB_VBARA) != fbdev->fb_phys) {
		fbdev->little_endian = 1;
		ocfb_writereg(fbdev, OCFB_VBARA, fbdev->fb_phys);
	}

	/* Horizontal timings */
	ocfb_writereg(fbdev, OCFB_HTIM, (var->hsync_len - 1) << 24 |
		      (var->left_margin - 1) << 16 | (var->xres - 1));

	/* Vertical timings */
	ocfb_writereg(fbdev, OCFB_VTIM, (var->vsync_len - 1) << 24 |
		      (var->upper_margin - 1) << 16 | (var->yres - 1));

	/* Total length of frame */
	hlen = var->left_margin + var->right_margin + var->hsync_len +
		var->xres;

	vlen = var->upper_margin + var->lower_margin + var->vsync_len +
		var->yres;

	ocfb_writereg(fbdev, OCFB_HVLEN, (hlen - 1) << 16 | (vlen - 1));

	bpp_config = OCFB_CTRL_CD8;
	switch (var->bits_per_pixel) {
	case 8:
		if (!var->grayscale)
			bpp_config |= OCFB_CTRL_PC;  /* enable palette */
		break;

	case 16:
		bpp_config |= OCFB_CTRL_CD16;
		break;

	case 24:
		bpp_config |= OCFB_CTRL_CD24;
		break;

	case 32:
		bpp_config |= OCFB_CTRL_CD32;
		break;

	default:
		dev_err(dev, "no bpp specified\n");
		break;
	}

	/* maximum (8) VBL (video memory burst length) */
	bpp_config |= OCFB_CTRL_VBL8;

	/* Enable output */
	ocfb_writereg(fbdev, OCFB_CTRL, (OCFB_CTRL_VEN | bpp_config));

	return 0;
}

static int ocfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			  unsigned blue, unsigned transp,
			  struct fb_info *info)
{
	struct ocfb_dev *fbdev = (struct ocfb_dev *)info->par;
	u32 color;

	if (regno >= info->cmap.len) {
		dev_err(info->device, "regno >= cmap.len\n");
		return 1;
	}

	if (info->var.grayscale) {
		/* grayscale = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;
	}

	red >>= (16 - info->var.red.length);
	green >>= (16 - info->var.green.length);
	blue >>= (16 - info->var.blue.length);
	transp >>= (16 - info->var.transp.length);

	if (info->var.bits_per_pixel == 8 && !info->var.grayscale) {
		regno <<= 2;
		color = (red << 16) | (green << 8) | blue;
		ocfb_writereg(fbdev, OCFB_PALETTE + regno, color);
	} else {
		((u32 *)(info->pseudo_palette))[regno] =
			(red << info->var.red.offset) |
			(green << info->var.green.offset) |
			(blue << info->var.blue.offset) |
			(transp << info->var.transp.offset);
	}

	return 0;
}

static int ocfb_init_fix(struct ocfb_dev *fbdev)
{
	struct fb_var_screeninfo *var = &fbdev->info.var;
	struct fb_fix_screeninfo *fix = &fbdev->info.fix;

	strcpy(fix->id, OCFB_NAME);

	fix->line_length = var->xres * var->bits_per_pixel/8;
	fix->smem_len = fix->line_length * var->yres;
	fix->type = FB_TYPE_PACKED_PIXELS;

	if (var->bits_per_pixel == 8 && !var->grayscale)
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
	else
		fix->visual = FB_VISUAL_TRUECOLOR;

	return 0;
}

static int ocfb_init_var(struct ocfb_dev *fbdev)
{
	struct fb_var_screeninfo *var = &fbdev->info.var;

	var->accel_flags = FB_ACCEL_NONE;
	var->activate = FB_ACTIVATE_NOW;
	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;

	switch (var->bits_per_pixel) {
	case 8:
		var->transp.offset = 0;
		var->transp.length = 0;
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		break;

	case 16:
		var->transp.offset = 0;
		var->transp.length = 0;
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length  = 5;
		break;

	case 24:
		var->transp.offset = 0;
		var->transp.length = 0;
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		break;

	case 32:
		var->transp.offset = 24;
		var->transp.length = 8;
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		break;
	}

	return 0;
}

static const struct fb_ops ocfb_ops = {
	.owner		= THIS_MODULE,
	FB_DEFAULT_IOMEM_OPS,
	.fb_setcolreg	= ocfb_setcolreg,
};

static int ocfb_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct ocfb_dev *fbdev;
	int fbsize;

	fbdev = devm_kzalloc(&pdev->dev, sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev)
		return -ENOMEM;

	platform_set_drvdata(pdev, fbdev);

	fbdev->info.fbops = &ocfb_ops;
	fbdev->info.device = &pdev->dev;
	fbdev->info.par = fbdev;

	/* Video mode setup */
	if (!fb_find_mode(&fbdev->info.var, &fbdev->info, mode_option,
			  NULL, 0, &default_mode, 16)) {
		dev_err(&pdev->dev, "No valid video modes found\n");
		return -EINVAL;
	}
	ocfb_init_var(fbdev);
	ocfb_init_fix(fbdev);

	fbdev->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(fbdev->regs))
		return PTR_ERR(fbdev->regs);

	/* Allocate framebuffer memory */
	fbsize = fbdev->info.fix.smem_len;
	fbdev->fb_virt = dma_alloc_coherent(&pdev->dev, PAGE_ALIGN(fbsize),
					    &fbdev->fb_phys, GFP_KERNEL);
	if (!fbdev->fb_virt) {
		dev_err(&pdev->dev,
			"Frame buffer memory allocation failed\n");
		return -ENOMEM;
	}
	fbdev->info.fix.smem_start = fbdev->fb_phys;
	fbdev->info.screen_base = fbdev->fb_virt;
	fbdev->info.pseudo_palette = fbdev->pseudo_palette;

	/* Clear framebuffer */
	memset_io(fbdev->fb_virt, 0, fbsize);

	/* Setup and enable the framebuffer */
	ocfb_setupfb(fbdev);

	if (fbdev->little_endian)
		fbdev->info.flags |= FBINFO_FOREIGN_ENDIAN;

	/* Allocate color map */
	ret = fb_alloc_cmap(&fbdev->info.cmap, PALETTE_SIZE, 0);
	if (ret) {
		dev_err(&pdev->dev, "Color map allocation failed\n");
		goto err_dma_free;
	}

	/* Register framebuffer */
	ret = register_framebuffer(&fbdev->info);
	if (ret) {
		dev_err(&pdev->dev, "Framebuffer registration failed\n");
		goto err_dealloc_cmap;
	}

	return 0;

err_dealloc_cmap:
	fb_dealloc_cmap(&fbdev->info.cmap);

err_dma_free:
	dma_free_coherent(&pdev->dev, PAGE_ALIGN(fbsize), fbdev->fb_virt,
			  fbdev->fb_phys);

	return ret;
}

static void ocfb_remove(struct platform_device *pdev)
{
	struct ocfb_dev *fbdev = platform_get_drvdata(pdev);

	unregister_framebuffer(&fbdev->info);
	fb_dealloc_cmap(&fbdev->info.cmap);
	dma_free_coherent(&pdev->dev, PAGE_ALIGN(fbdev->info.fix.smem_len),
			  fbdev->fb_virt, fbdev->fb_phys);

	/* Disable display */
	ocfb_writereg(fbdev, OCFB_CTRL, 0);

	platform_set_drvdata(pdev, NULL);
}

static const struct of_device_id ocfb_match[] = {
	{ .compatible = "opencores,ocfb", },
	{},
};
MODULE_DEVICE_TABLE(of, ocfb_match);

static struct platform_driver ocfb_driver = {
	.probe  = ocfb_probe,
	.remove = ocfb_remove,
	.driver = {
		.name = "ocfb_fb",
		.of_match_table = ocfb_match,
	}
};

/*
 * Init and exit routines
 */
static int __init ocfb_init(void)
{
#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("ocfb", &option))
		return -ENODEV;
	ocfb_setup(option);
#endif
	return platform_driver_register(&ocfb_driver);
}

static void __exit ocfb_exit(void)
{
	platform_driver_unregister(&ocfb_driver);
}

module_init(ocfb_init);
module_exit(ocfb_exit);

MODULE_AUTHOR("Stefan Kristiansson <stefan.kristiansson@saunalahti.fi>");
MODULE_DESCRIPTION("OpenCores VGA/LCD 2.0 frame buffer driver");
MODULE_LICENSE("GPL v2");
module_param(mode_option, charp, 0);
MODULE_PARM_DESC(mode_option, "Video mode ('<xres>x<yres>[-<bpp>][@refresh]')");
