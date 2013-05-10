/*
 *  altvipfb.c -- Altera Video and Image Processing(VIP) Frame Reader driver
 *
 *  This is based on a driver made by Thomas Chou <thomas@wytron.com.tw> and
 *  Walter Goossens <waltergoossens@home.nl> This driver supports the Altera VIP
 *  Frame Reader component.  More info on the hardware can be found in
 *  the Altera Video and Image Processing Suite User Guide at this address
 *  http://www.altera.com/literature/ug/ug_vip.pdf.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define PALETTE_SIZE	256
#define DRIVER_NAME	"altvipfb"

/* control registers */
#define ALTVIPFB_CONTROL		0
#define ALTVIPFB_FRAME_SELECT		12
#define ALTVIPFB_FRAME0_BASE_ADDRESS	16
#define ALTVIPFB_FRAME0_NUM_WORDS	20
#define ALTVIPFB_FRAME0_SAMPLES		24
#define ALTVIPFB_FRAME0_WIDTH		32
#define ALTVIPFB_FRAME0_HEIGHT		36
#define ALTVIPFB_FRAME0_INTERLACED	40

struct altvipfb_type;

struct altvipfb_dev {
	struct platform_device *pdev;
	struct fb_info info;
	struct resource *reg_res;
	void __iomem *base;
	int mem_word_width;
	u32 pseudo_palette[PALETTE_SIZE];
};

static int altvipfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp, struct fb_info *info)
{
	/*
	 *  Set a single color register. The values supplied have a 32 bit
	 *  magnitude.
	 *  Return != 0 for invalid regno.
	 */

	if (regno > 255)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;

	if (regno < 255) {
		((u32 *)info->pseudo_palette)[regno] =
		((red & 255) << 16) | ((green & 255) << 8) | (blue & 255);
	}

	return 0;
}

static struct fb_ops altvipfb_ops = {
	.owner = THIS_MODULE,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_setcolreg = altvipfb_setcolreg,
};

static int altvipfb_of_setup(struct altvipfb_dev *fbdev)
{
	struct device_node *np = fbdev->pdev->dev.of_node;
	int ret;
	u32 bits_per_color;

	ret = of_property_read_u32(np, "max-width", &fbdev->info.var.xres);
	if (ret) {
		dev_err(&fbdev->pdev->dev,
			"Missing required parameter 'max-width'");
		return ret;
	}
	fbdev->info.var.xres_virtual = fbdev->info.var.xres,

	ret = of_property_read_u32(np, "max-height", &fbdev->info.var.yres);
	if (ret) {
		dev_err(&fbdev->pdev->dev,
			"Missing required parameter 'max-height'");
		return ret;
	}
	fbdev->info.var.yres_virtual = fbdev->info.var.yres;

	ret = of_property_read_u32(np, "bits-per-color", &bits_per_color);
	if (ret) {
		dev_err(&fbdev->pdev->dev,
			"Missing required parameter 'bits-per-color'");
		return ret;
	}
	if (bits_per_color != 8) {
		dev_err(&fbdev->pdev->dev,
			"bits-per-color is set to %i.  Curently only 8 is supported.",
			bits_per_color);
		return -ENODEV;
	}
	fbdev->info.var.bits_per_pixel = 32;

	ret = of_property_read_u32(np, "mem-word-width",
				   &fbdev->mem_word_width);
	if (ret) {
		dev_err(&fbdev->pdev->dev,
			"Missing required parameter 'mem-word-width'");
		return ret;
	}
	if (!(fbdev->mem_word_width >= 32 && fbdev->mem_word_width % 32 == 0)) {
		dev_err(&fbdev->pdev->dev,
			"mem-word-width is set to %i.  must be >= 32 and multiple of 32.",
			fbdev->mem_word_width);
		return -ENODEV;
	}

	return 0;
}

static void altvipfb_start_hw(struct altvipfb_dev *fbdev)
{
	writel(fbdev->info.fix.smem_start, fbdev->base +
	       ALTVIPFB_FRAME0_BASE_ADDRESS);
	writel(fbdev->info.var.xres * fbdev->info.var.yres /
	       (fbdev->mem_word_width/32),
	       fbdev->base + ALTVIPFB_FRAME0_NUM_WORDS);
	writel(fbdev->info.var.xres * fbdev->info.var.yres,
	       fbdev->base + ALTVIPFB_FRAME0_SAMPLES);
	writel(fbdev->info.var.xres, fbdev->base + ALTVIPFB_FRAME0_WIDTH);
	writel(fbdev->info.var.yres, fbdev->base + ALTVIPFB_FRAME0_HEIGHT);
	writel(3, fbdev->base + ALTVIPFB_FRAME0_INTERLACED);
	writel(0, fbdev->base + ALTVIPFB_FRAME_SELECT);

	/* Finally set the control register to 1 to start streaming */
	writel(1, fbdev->base + ALTVIPFB_CONTROL);
}

static void altvipfb_disable_hw(struct altvipfb_dev *fbdev)
{
	/* set the control register to 0 to stop streaming */
	writel(0, fbdev->base + ALTVIPFB_CONTROL);
}


static int altvipfb_setup_fb_info(struct altvipfb_dev *fbdev)
{
	struct fb_info *info = &fbdev->info;
	int ret;

	strcpy(info->fix.id, DRIVER_NAME);
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.accel = FB_ACCEL_NONE;

	info->fbops = &altvipfb_ops;
	info->var.activate = FB_ACTIVATE_NOW;
	info->var.height = -1;
	info->var.width = -1;
	info->var.vmode = FB_VMODE_NONINTERLACED;

	ret = altvipfb_of_setup(fbdev);
	if (ret)
		return ret;

	/* settings for 32bit pixels */
	info->var.red.offset = 16;
	info->var.red.length = 8;
	info->var.red.msb_right = 0;
	info->var.green.offset = 8;
	info->var.green.length = 8;
	info->var.green.msb_right = 0;
	info->var.blue.offset = 0;
	info->var.blue.length = 8;
	info->var.blue.msb_right = 0;

	info->fix.line_length = (info->var.xres *
		(info->var.bits_per_pixel >> 3));
	info->fix.smem_len = info->fix.line_length * info->var.yres;

	info->pseudo_palette = fbdev->pseudo_palette;
	info->flags = FBINFO_FLAG_DEFAULT;

	return 0;
}

static int altvipfb_probe(struct platform_device *pdev)
{
	int retval;
	void *fbmem_virt;
	struct altvipfb_dev *fbdev;

	fbdev = devm_kzalloc(&pdev->dev, sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev)
		return -ENOMEM;

	fbdev->pdev = pdev;
	fbdev->reg_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!fbdev->reg_res)
		return -ENODEV;

	retval = altvipfb_setup_fb_info(fbdev);

	fbmem_virt = dma_alloc_coherent(NULL,
					fbdev->info.fix.smem_len,
					(void *)&(fbdev->info.fix.smem_start),
					GFP_KERNEL);
	if (!fbmem_virt) {
		dev_err(&pdev->dev,
			"altvipfb: unable to allocate %d Bytes fb memory\n",
			fbdev->info.fix.smem_len);
		return retval;
	}

	fbdev->info.screen_base = fbmem_virt;

	retval = fb_alloc_cmap(&fbdev->info.cmap, PALETTE_SIZE, 0);
	if (retval < 0)
		goto err_dma_free;

	platform_set_drvdata(pdev, fbdev);

	fbdev->base = devm_ioremap_resource(&pdev->dev, fbdev->reg_res);
	if (IS_ERR(fbdev->base)) {
		dev_err(&pdev->dev, "devm_ioremap_resource failed\n");
		retval = PTR_ERR(fbdev->base);
		goto err_dealloc_cmap;
	}

	altvipfb_start_hw(fbdev);

	retval = register_framebuffer(&fbdev->info);
	if (retval < 0)
		goto err_dealloc_cmap;

	dev_info(&pdev->dev, "fb%d: %s frame buffer device at 0x%x+0x%x\n",
		 fbdev->info.node, fbdev->info.fix.id,
		 (unsigned)fbdev->info.fix.smem_start,
		 fbdev->info.fix.smem_len);

	return 0;

err_dealloc_cmap:
	fb_dealloc_cmap(&fbdev->info.cmap);
err_dma_free:
	dma_free_coherent(NULL, fbdev->info.fix.smem_len, fbmem_virt,
			  fbdev->info.fix.smem_start);
	return retval;
}

static int altvipfb_remove(struct platform_device *dev)
{
	struct altvipfb_dev *fbdev = platform_get_drvdata(dev);

	if (fbdev) {
		unregister_framebuffer(&fbdev->info);
		fb_dealloc_cmap(&fbdev->info.cmap);
		dma_free_coherent(NULL, fbdev->info.fix.smem_len,
				  fbdev->info.screen_base,
				  fbdev->info.fix.smem_start);
		altvipfb_disable_hw(fbdev);
	}
	return 0;
}


static struct of_device_id altvipfb_match[] = {
	{ .compatible = "altr,vip-frame-reader-1.0" },
	{ .compatible = "altr,vip-frame-reader-9.1" },
	{},
};
MODULE_DEVICE_TABLE(of, altvipfb_match);

static struct platform_driver altvipfb_driver = {
	.probe = altvipfb_probe,
	.remove = altvipfb_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = DRIVER_NAME,
		.of_match_table = altvipfb_match,
	},
};
module_platform_driver(altvipfb_driver);

MODULE_DESCRIPTION("Altera VIP Frame Reader framebuffer driver");
MODULE_AUTHOR("Chris Rauer <crauer@altera.com>");
MODULE_LICENSE("GPL v2");
