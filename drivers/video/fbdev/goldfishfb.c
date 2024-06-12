// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2007 Google, Inc.
 * Copyright (C) 2012 Intel, Inc.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>

enum {
	FB_GET_WIDTH        = 0x00,
	FB_GET_HEIGHT       = 0x04,
	FB_INT_STATUS       = 0x08,
	FB_INT_ENABLE       = 0x0c,
	FB_SET_BASE         = 0x10,
	FB_SET_ROTATION     = 0x14,
	FB_SET_BLANK        = 0x18,
	FB_GET_PHYS_WIDTH   = 0x1c,
	FB_GET_PHYS_HEIGHT  = 0x20,

	FB_INT_VSYNC             = 1U << 0,
	FB_INT_BASE_UPDATE_DONE  = 1U << 1
};

struct goldfish_fb {
	void __iomem *reg_base;
	int irq;
	spinlock_t lock;
	wait_queue_head_t wait;
	int base_update_count;
	int rotation;
	struct fb_info fb;
	u32 cmap[16];
};

static irqreturn_t goldfish_fb_interrupt(int irq, void *dev_id)
{
	unsigned long irq_flags;
	struct goldfish_fb *fb = dev_id;
	u32 status;

	spin_lock_irqsave(&fb->lock, irq_flags);
	status = readl(fb->reg_base + FB_INT_STATUS);
	if (status & FB_INT_BASE_UPDATE_DONE) {
		fb->base_update_count++;
		wake_up(&fb->wait);
	}
	spin_unlock_irqrestore(&fb->lock, irq_flags);
	return status ? IRQ_HANDLED : IRQ_NONE;
}

static inline u32 convert_bitfield(int val, struct fb_bitfield *bf)
{
	unsigned int mask = (1 << bf->length) - 1;

	return (val >> (16 - bf->length) & mask) << bf->offset;
}

static int
goldfish_fb_setcolreg(unsigned int regno, unsigned int red, unsigned int green,
		 unsigned int blue, unsigned int transp, struct fb_info *info)
{
	struct goldfish_fb *fb = container_of(info, struct goldfish_fb, fb);

	if (regno < 16) {
		fb->cmap[regno] = convert_bitfield(transp, &fb->fb.var.transp) |
				  convert_bitfield(blue, &fb->fb.var.blue) |
				  convert_bitfield(green, &fb->fb.var.green) |
				  convert_bitfield(red, &fb->fb.var.red);
		return 0;
	} else {
		return 1;
	}
}

static int goldfish_fb_check_var(struct fb_var_screeninfo *var,
							struct fb_info *info)
{
	if ((var->rotate & 1) != (info->var.rotate & 1)) {
		if ((var->xres != info->var.yres) ||
				(var->yres != info->var.xres) ||
				(var->xres_virtual != info->var.yres) ||
				(var->yres_virtual > info->var.xres * 2) ||
				(var->yres_virtual < info->var.xres)) {
			return -EINVAL;
		}
	} else {
		if ((var->xres != info->var.xres) ||
		   (var->yres != info->var.yres) ||
		   (var->xres_virtual != info->var.xres) ||
		   (var->yres_virtual > info->var.yres * 2) ||
		   (var->yres_virtual < info->var.yres)) {
			return -EINVAL;
		}
	}
	if ((var->xoffset != info->var.xoffset) ||
			(var->bits_per_pixel != info->var.bits_per_pixel) ||
			(var->grayscale != info->var.grayscale)) {
		return -EINVAL;
	}
	return 0;
}

static int goldfish_fb_set_par(struct fb_info *info)
{
	struct goldfish_fb *fb = container_of(info, struct goldfish_fb, fb);

	if (fb->rotation != fb->fb.var.rotate) {
		info->fix.line_length = info->var.xres * 2;
		fb->rotation = fb->fb.var.rotate;
		writel(fb->rotation, fb->reg_base + FB_SET_ROTATION);
	}
	return 0;
}


static int goldfish_fb_pan_display(struct fb_var_screeninfo *var,
							struct fb_info *info)
{
	unsigned long irq_flags;
	int base_update_count;
	struct goldfish_fb *fb = container_of(info, struct goldfish_fb, fb);

	spin_lock_irqsave(&fb->lock, irq_flags);
	base_update_count = fb->base_update_count;
	writel(fb->fb.fix.smem_start + fb->fb.var.xres * 2 * var->yoffset,
						fb->reg_base + FB_SET_BASE);
	spin_unlock_irqrestore(&fb->lock, irq_flags);
	wait_event_timeout(fb->wait,
			fb->base_update_count != base_update_count, HZ / 15);
	if (fb->base_update_count == base_update_count)
		pr_err("%s: timeout waiting for base update\n", __func__);
	return 0;
}

static int goldfish_fb_blank(int blank, struct fb_info *info)
{
	struct goldfish_fb *fb = container_of(info, struct goldfish_fb, fb);

	switch (blank) {
	case FB_BLANK_NORMAL:
		writel(1, fb->reg_base + FB_SET_BLANK);
		break;
	case FB_BLANK_UNBLANK:
		writel(0, fb->reg_base + FB_SET_BLANK);
		break;
	}
	return 0;
}

static const struct fb_ops goldfish_fb_ops = {
	.owner          = THIS_MODULE,
	FB_DEFAULT_IOMEM_OPS,
	.fb_check_var   = goldfish_fb_check_var,
	.fb_set_par     = goldfish_fb_set_par,
	.fb_setcolreg   = goldfish_fb_setcolreg,
	.fb_pan_display = goldfish_fb_pan_display,
	.fb_blank	= goldfish_fb_blank,
};


static int goldfish_fb_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *r;
	struct goldfish_fb *fb;
	size_t framesize;
	u32 width, height;
	dma_addr_t fbpaddr;

	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (fb == NULL) {
		ret = -ENOMEM;
		goto err_fb_alloc_failed;
	}
	spin_lock_init(&fb->lock);
	init_waitqueue_head(&fb->wait);
	platform_set_drvdata(pdev, fb);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		ret = -ENODEV;
		goto err_no_io_base;
	}
	fb->reg_base = ioremap(r->start, PAGE_SIZE);
	if (fb->reg_base == NULL) {
		ret = -ENOMEM;
		goto err_no_io_base;
	}

	fb->irq = platform_get_irq(pdev, 0);
	if (fb->irq < 0) {
		ret = fb->irq;
		goto err_no_irq;
	}

	width = readl(fb->reg_base + FB_GET_WIDTH);
	height = readl(fb->reg_base + FB_GET_HEIGHT);

	fb->fb.fbops		= &goldfish_fb_ops;
	fb->fb.pseudo_palette	= fb->cmap;
	fb->fb.fix.type		= FB_TYPE_PACKED_PIXELS;
	fb->fb.fix.visual = FB_VISUAL_TRUECOLOR;
	fb->fb.fix.line_length = width * 2;
	fb->fb.fix.accel	= FB_ACCEL_NONE;
	fb->fb.fix.ypanstep = 1;

	fb->fb.var.xres		= width;
	fb->fb.var.yres		= height;
	fb->fb.var.xres_virtual	= width;
	fb->fb.var.yres_virtual	= height * 2;
	fb->fb.var.bits_per_pixel = 16;
	fb->fb.var.activate	= FB_ACTIVATE_NOW;
	fb->fb.var.height	= readl(fb->reg_base + FB_GET_PHYS_HEIGHT);
	fb->fb.var.width	= readl(fb->reg_base + FB_GET_PHYS_WIDTH);
	fb->fb.var.pixclock	= 0;

	fb->fb.var.red.offset = 11;
	fb->fb.var.red.length = 5;
	fb->fb.var.green.offset = 5;
	fb->fb.var.green.length = 6;
	fb->fb.var.blue.offset = 0;
	fb->fb.var.blue.length = 5;

	framesize = width * height * 2 * 2;
	fb->fb.screen_base = (char __force __iomem *)dma_alloc_coherent(
						&pdev->dev, framesize,
						&fbpaddr, GFP_KERNEL);
	pr_debug("allocating frame buffer %d * %d, got %p\n",
					width, height, fb->fb.screen_base);
	if (fb->fb.screen_base == NULL) {
		ret = -ENOMEM;
		goto err_alloc_screen_base_failed;
	}
	fb->fb.fix.smem_start = fbpaddr;
	fb->fb.fix.smem_len = framesize;

	ret = fb_set_var(&fb->fb, &fb->fb.var);
	if (ret)
		goto err_fb_set_var_failed;

	ret = request_irq(fb->irq, goldfish_fb_interrupt, IRQF_SHARED,
							pdev->name, fb);
	if (ret)
		goto err_request_irq_failed;

	writel(FB_INT_BASE_UPDATE_DONE, fb->reg_base + FB_INT_ENABLE);
	goldfish_fb_pan_display(&fb->fb.var, &fb->fb); /* updates base */

	ret = register_framebuffer(&fb->fb);
	if (ret)
		goto err_register_framebuffer_failed;
	return 0;

err_register_framebuffer_failed:
	free_irq(fb->irq, fb);
err_request_irq_failed:
err_fb_set_var_failed:
	dma_free_coherent(&pdev->dev, framesize,
				(void *)fb->fb.screen_base,
				fb->fb.fix.smem_start);
err_alloc_screen_base_failed:
err_no_irq:
	iounmap(fb->reg_base);
err_no_io_base:
	kfree(fb);
err_fb_alloc_failed:
	return ret;
}

static void goldfish_fb_remove(struct platform_device *pdev)
{
	size_t framesize;
	struct goldfish_fb *fb = platform_get_drvdata(pdev);

	framesize = fb->fb.var.xres_virtual * fb->fb.var.yres_virtual * 2;
	unregister_framebuffer(&fb->fb);
	free_irq(fb->irq, fb);

	dma_free_coherent(&pdev->dev, framesize, (void *)fb->fb.screen_base,
						fb->fb.fix.smem_start);
	iounmap(fb->reg_base);
	kfree(fb);
}

static const struct of_device_id goldfish_fb_of_match[] = {
	{ .compatible = "google,goldfish-fb", },
	{},
};
MODULE_DEVICE_TABLE(of, goldfish_fb_of_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id goldfish_fb_acpi_match[] = {
	{ "GFSH0004", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, goldfish_fb_acpi_match);
#endif

static struct platform_driver goldfish_fb_driver = {
	.probe		= goldfish_fb_probe,
	.remove_new	= goldfish_fb_remove,
	.driver = {
		.name = "goldfish_fb",
		.of_match_table = goldfish_fb_of_match,
		.acpi_match_table = ACPI_PTR(goldfish_fb_acpi_match),
	}
};

module_platform_driver(goldfish_fb_driver);

MODULE_DESCRIPTION("Goldfish Virtual Platform Framebuffer driver");
MODULE_LICENSE("GPL v2");
