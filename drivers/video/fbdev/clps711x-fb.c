// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cirrus Logic CLPS711X FB driver
 *
 * Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 * Based on driver by Russell King <rmk@arm.linux.org.uk>
 */

#include <linux/clk.h>
#include <linux/fb.h>
#include <linux/io.h>
#include <linux/lcd.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/clps711x.h>
#include <linux/regulator/consumer.h>
#include <video/of_display_timing.h>

#define CLPS711X_FB_NAME	"clps711x-fb"
#define CLPS711X_FB_BPP_MAX	(4)

/* Registers relative to LCDCON */
#define CLPS711X_LCDCON		(0x0000)
# define LCDCON_GSEN		BIT(30)
# define LCDCON_GSMD		BIT(31)
#define CLPS711X_PALLSW		(0x0280)
#define CLPS711X_PALMSW		(0x02c0)
#define CLPS711X_FBADDR		(0x0d40)

struct clps711x_fb_info {
	struct clk		*clk;
	void __iomem		*base;
	struct regmap		*syscon;
	resource_size_t		buffsize;
	struct fb_videomode	mode;
	struct regulator	*lcd_pwr;
	u32			ac_prescale;
	bool			cmap_invert;
};

static int clps711x_fb_setcolreg(u_int regno, u_int red, u_int green,
				 u_int blue, u_int transp, struct fb_info *info)
{
	struct clps711x_fb_info *cfb = info->par;
	u32 level, mask, shift;

	if (regno >= BIT(info->var.bits_per_pixel))
		return -EINVAL;

	shift = 4 * (regno & 7);
	mask  = 0xf << shift;
	/* gray = 0.30*R + 0.58*G + 0.11*B */
	level = (((red * 77 + green * 151 + blue * 28) >> 20) << shift) & mask;
	if (cfb->cmap_invert)
		level = 0xf - level;

	regno = (regno < 8) ? CLPS711X_PALLSW : CLPS711X_PALMSW;

	writel((readl(cfb->base + regno) & ~mask) | level, cfb->base + regno);

	return 0;
}

static int clps711x_fb_check_var(struct fb_var_screeninfo *var,
				 struct fb_info *info)
{
	u32 val;

	if (var->bits_per_pixel < 1 ||
	    var->bits_per_pixel > CLPS711X_FB_BPP_MAX)
		return -EINVAL;

	if (!var->pixclock)
		return -EINVAL;

	val = DIV_ROUND_UP(var->xres, 16) - 1;
	if (val < 0x01 || val > 0x3f)
		return -EINVAL;

	val = DIV_ROUND_UP(var->yres * var->xres * var->bits_per_pixel, 128);
	val--;
	if (val < 0x001 || val > 0x1fff)
		return -EINVAL;

	var->transp.msb_right	= 0;
	var->transp.offset	= 0;
	var->transp.length	= 0;
	var->red.msb_right	= 0;
	var->red.offset		= 0;
	var->red.length		= var->bits_per_pixel;
	var->green		= var->red;
	var->blue		= var->red;
	var->grayscale		= var->bits_per_pixel > 1;

	return 0;
}

static int clps711x_fb_set_par(struct fb_info *info)
{
	struct clps711x_fb_info *cfb = info->par;
	resource_size_t size;
	u32 lcdcon, pps;

	size = (info->var.xres * info->var.yres * info->var.bits_per_pixel) / 8;
	if (size > cfb->buffsize)
		return -EINVAL;

	switch (info->var.bits_per_pixel) {
	case 1:
		info->fix.visual = FB_VISUAL_MONO01;
		break;
	case 2:
	case 4:
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		break;
	default:
		return -EINVAL;
	}

	info->fix.line_length = info->var.xres * info->var.bits_per_pixel / 8;
	info->fix.smem_len = size;

	lcdcon = (info->var.xres * info->var.yres *
		  info->var.bits_per_pixel) / 128 - 1;
	lcdcon |= ((info->var.xres / 16) - 1) << 13;
	lcdcon |= (cfb->ac_prescale & 0x1f) << 25;

	pps = clk_get_rate(cfb->clk) / (PICOS2KHZ(info->var.pixclock) * 1000);
	if (pps)
		pps--;
	lcdcon |= (pps & 0x3f) << 19;

	if (info->var.bits_per_pixel == 4)
		lcdcon |= LCDCON_GSMD;
	if (info->var.bits_per_pixel >= 2)
		lcdcon |= LCDCON_GSEN;

	/* LCDCON must only be changed while the LCD is disabled */
	regmap_update_bits(cfb->syscon, SYSCON_OFFSET, SYSCON1_LCDEN, 0);
	writel(lcdcon, cfb->base + CLPS711X_LCDCON);
	regmap_update_bits(cfb->syscon, SYSCON_OFFSET,
			   SYSCON1_LCDEN, SYSCON1_LCDEN);

	return 0;
}

static int clps711x_fb_blank(int blank, struct fb_info *info)
{
	/* Return happy */
	return 0;
}

static const struct fb_ops clps711x_fb_ops = {
	.owner		= THIS_MODULE,
	FB_DEFAULT_IOMEM_OPS,
	.fb_setcolreg	= clps711x_fb_setcolreg,
	.fb_check_var	= clps711x_fb_check_var,
	.fb_set_par	= clps711x_fb_set_par,
	.fb_blank	= clps711x_fb_blank,
};

static int clps711x_lcd_check_fb(struct lcd_device *lcddev, struct fb_info *fi)
{
	struct clps711x_fb_info *cfb = dev_get_drvdata(&lcddev->dev);

	return (!fi || fi->par == cfb) ? 1 : 0;
}

static int clps711x_lcd_get_power(struct lcd_device *lcddev)
{
	struct clps711x_fb_info *cfb = dev_get_drvdata(&lcddev->dev);

	if (!IS_ERR_OR_NULL(cfb->lcd_pwr))
		if (!regulator_is_enabled(cfb->lcd_pwr))
			return FB_BLANK_NORMAL;

	return FB_BLANK_UNBLANK;
}

static int clps711x_lcd_set_power(struct lcd_device *lcddev, int blank)
{
	struct clps711x_fb_info *cfb = dev_get_drvdata(&lcddev->dev);

	if (!IS_ERR_OR_NULL(cfb->lcd_pwr)) {
		if (blank == FB_BLANK_UNBLANK) {
			if (!regulator_is_enabled(cfb->lcd_pwr))
				return regulator_enable(cfb->lcd_pwr);
		} else {
			if (regulator_is_enabled(cfb->lcd_pwr))
				return regulator_disable(cfb->lcd_pwr);
		}
	}

	return 0;
}

static struct lcd_ops clps711x_lcd_ops = {
	.check_fb	= clps711x_lcd_check_fb,
	.get_power	= clps711x_lcd_get_power,
	.set_power	= clps711x_lcd_set_power,
};

static int clps711x_fb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *disp, *np = dev->of_node;
	struct clps711x_fb_info *cfb;
	struct lcd_device *lcd;
	struct fb_info *info;
	struct resource *res;
	int ret = -ENOENT;
	u32 val;

	if (fb_get_options(CLPS711X_FB_NAME, NULL))
		return -ENODEV;

	info = framebuffer_alloc(sizeof(*cfb), dev);
	if (!info)
		return -ENOMEM;

	cfb = info->par;
	platform_set_drvdata(pdev, info);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		goto out_fb_release;
	cfb->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!cfb->base) {
		ret = -ENOMEM;
		goto out_fb_release;
	}

	info->fix.mmio_start = res->start;
	info->fix.mmio_len = resource_size(res);

	info->screen_base = devm_platform_get_and_ioremap_resource(pdev, 1, &res);
	if (IS_ERR(info->screen_base)) {
		ret = PTR_ERR(info->screen_base);
		goto out_fb_release;
	}

	/* Physical address should be aligned to 256 MiB */
	if (res->start & 0x0fffffff) {
		ret = -EINVAL;
		goto out_fb_release;
	}

	cfb->buffsize = resource_size(res);
	info->fix.smem_start = res->start;

	cfb->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(cfb->clk)) {
		ret = PTR_ERR(cfb->clk);
		goto out_fb_release;
	}

	cfb->syscon = syscon_regmap_lookup_by_phandle(np, "syscon");
	if (IS_ERR(cfb->syscon)) {
		ret = PTR_ERR(cfb->syscon);
		goto out_fb_release;
	}

	disp = of_parse_phandle(np, "display", 0);
	if (!disp) {
		dev_err(&pdev->dev, "No display defined\n");
		ret = -ENODATA;
		goto out_fb_release;
	}

	ret = of_get_fb_videomode(disp, &cfb->mode, OF_USE_NATIVE_MODE);
	if (ret) {
		of_node_put(disp);
		goto out_fb_release;
	}

	of_property_read_u32(disp, "ac-prescale", &cfb->ac_prescale);
	cfb->cmap_invert = of_property_read_bool(disp, "cmap-invert");

	ret = of_property_read_u32(disp, "bits-per-pixel",
				   &info->var.bits_per_pixel);
	of_node_put(disp);
	if (ret)
		goto out_fb_release;

	/* Force disable LCD on any mismatch */
	if (info->fix.smem_start != (readb(cfb->base + CLPS711X_FBADDR) << 28))
		regmap_update_bits(cfb->syscon, SYSCON_OFFSET,
				   SYSCON1_LCDEN, 0);

	ret = regmap_read(cfb->syscon, SYSCON_OFFSET, &val);
	if (ret)
		goto out_fb_release;

	if (!(val & SYSCON1_LCDEN)) {
		/* Setup start FB address */
		writeb(info->fix.smem_start >> 28, cfb->base + CLPS711X_FBADDR);
		/* Clean FB memory */
		memset_io(info->screen_base, 0, cfb->buffsize);
	}

	cfb->lcd_pwr = devm_regulator_get(dev, "lcd");
	if (PTR_ERR(cfb->lcd_pwr) == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		goto out_fb_release;
	}

	info->fbops = &clps711x_fb_ops;
	info->var.activate = FB_ACTIVATE_FORCE | FB_ACTIVATE_NOW;
	info->var.height = -1;
	info->var.width = -1;
	info->var.vmode = FB_VMODE_NONINTERLACED;
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.accel = FB_ACCEL_NONE;
	strscpy(info->fix.id, CLPS711X_FB_NAME, sizeof(info->fix.id));
	fb_videomode_to_var(&info->var, &cfb->mode);

	ret = fb_alloc_cmap(&info->cmap, BIT(CLPS711X_FB_BPP_MAX), 0);
	if (ret)
		goto out_fb_release;

	ret = fb_set_var(info, &info->var);
	if (ret)
		goto out_fb_dealloc_cmap;

	ret = register_framebuffer(info);
	if (ret)
		goto out_fb_dealloc_cmap;

	lcd = devm_lcd_device_register(dev, "clps711x-lcd", dev, cfb,
				       &clps711x_lcd_ops);
	if (!IS_ERR(lcd))
		return 0;

	ret = PTR_ERR(lcd);
	unregister_framebuffer(info);

out_fb_dealloc_cmap:
	regmap_update_bits(cfb->syscon, SYSCON_OFFSET, SYSCON1_LCDEN, 0);
	fb_dealloc_cmap(&info->cmap);

out_fb_release:
	framebuffer_release(info);

	return ret;
}

static void clps711x_fb_remove(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);
	struct clps711x_fb_info *cfb = info->par;

	regmap_update_bits(cfb->syscon, SYSCON_OFFSET, SYSCON1_LCDEN, 0);

	unregister_framebuffer(info);
	fb_dealloc_cmap(&info->cmap);
	framebuffer_release(info);
}

static const struct of_device_id clps711x_fb_dt_ids[] = {
	{ .compatible = "cirrus,ep7209-fb", },
	{ }
};
MODULE_DEVICE_TABLE(of, clps711x_fb_dt_ids);

static struct platform_driver clps711x_fb_driver = {
	.driver	= {
		.name		= CLPS711X_FB_NAME,
		.of_match_table	= clps711x_fb_dt_ids,
	},
	.probe	= clps711x_fb_probe,
	.remove_new = clps711x_fb_remove,
};
module_platform_driver(clps711x_fb_driver);

MODULE_AUTHOR("Alexander Shiyan <shc_work@mail.ru>");
MODULE_DESCRIPTION("Cirrus Logic CLPS711X FB driver");
MODULE_LICENSE("GPL");
