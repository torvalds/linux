// SPDX-License-Identifier: GPL-2.0-only
/*
 * framebuffer-coreboot.c
 *
 * Memory based framebuffer accessed through coreboot table.
 *
 * Copyright 2012-2013 David Herrmann <dh.herrmann@gmail.com>
 * Copyright 2017 Google Inc.
 * Copyright 2017 Samuel Holland <samuel@sholland.org>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_data/simplefb.h>
#include <linux/platform_device.h>

#include "coreboot_table.h"

#define CB_TAG_FRAMEBUFFER 0x12

static const struct simplefb_format formats[] = SIMPLEFB_FORMATS;

static int framebuffer_probe(struct coreboot_device *dev)
{
	int i;
	u32 length;
	struct lb_framebuffer *fb = &dev->framebuffer;
	struct platform_device *pdev;
	struct resource res;
	struct simplefb_platform_data pdata = {
		.width = fb->x_resolution,
		.height = fb->y_resolution,
		.stride = fb->bytes_per_line,
		.format = NULL,
	};

	if (!fb->physical_address)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(formats); ++i) {
		if (fb->bits_per_pixel     == formats[i].bits_per_pixel &&
		    fb->red_mask_pos       == formats[i].red.offset &&
		    fb->red_mask_size      == formats[i].red.length &&
		    fb->green_mask_pos     == formats[i].green.offset &&
		    fb->green_mask_size    == formats[i].green.length &&
		    fb->blue_mask_pos      == formats[i].blue.offset &&
		    fb->blue_mask_size     == formats[i].blue.length)
			pdata.format = formats[i].name;
	}
	if (!pdata.format)
		return -ENODEV;

	memset(&res, 0, sizeof(res));
	res.flags = IORESOURCE_MEM | IORESOURCE_BUSY;
	res.name = "Coreboot Framebuffer";
	res.start = fb->physical_address;
	length = PAGE_ALIGN(fb->y_resolution * fb->bytes_per_line);
	res.end = res.start + length - 1;
	if (res.end <= res.start)
		return -EINVAL;

	pdev = platform_device_register_resndata(&dev->dev,
						 "simple-framebuffer", 0,
						 &res, 1, &pdata,
						 sizeof(pdata));
	if (IS_ERR(pdev))
		pr_warn("coreboot: could not register framebuffer\n");
	else
		dev_set_drvdata(&dev->dev, pdev);

	return PTR_ERR_OR_ZERO(pdev);
}

static void framebuffer_remove(struct coreboot_device *dev)
{
	struct platform_device *pdev = dev_get_drvdata(&dev->dev);

	platform_device_unregister(pdev);
}

static const struct coreboot_device_id framebuffer_ids[] = {
	{ .tag = CB_TAG_FRAMEBUFFER },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(coreboot, framebuffer_ids);

static struct coreboot_driver framebuffer_driver = {
	.probe = framebuffer_probe,
	.remove = framebuffer_remove,
	.drv = {
		.name = "framebuffer",
	},
	.id_table = framebuffer_ids,
};
module_coreboot_driver(framebuffer_driver);

MODULE_AUTHOR("Samuel Holland <samuel@sholland.org>");
MODULE_LICENSE("GPL");
