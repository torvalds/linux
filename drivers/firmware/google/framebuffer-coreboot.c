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
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_data/simplefb.h>
#include <linux/platform_device.h>
#include <linux/sysfb.h>

#include "coreboot_table.h"

#if defined(CONFIG_PCI)
static bool framebuffer_pci_dev_is_enabled(struct pci_dev *pdev)
{
	/*
	 * TODO: Try to integrate this code into the PCI subsystem
	 */
	int ret;
	u16 command;

	ret = pci_read_config_word(pdev, PCI_COMMAND, &command);
	if (ret != PCIBIOS_SUCCESSFUL)
		return false;
	if (!(command & PCI_COMMAND_MEMORY))
		return false;
	return true;
}

static struct pci_dev *framebuffer_parent_pci_dev(struct resource *res)
{
	struct pci_dev *pdev = NULL;
	const struct resource *r = NULL;

	while (!r && (pdev = pci_get_base_class(PCI_BASE_CLASS_DISPLAY, pdev)))
		r = pci_find_resource(pdev, res);

	if (!r || !pdev)
		return NULL; /* not found; not an error */

	if (!framebuffer_pci_dev_is_enabled(pdev)) {
		pci_dev_put(pdev);
		return ERR_PTR(-ENODEV);
	}

	return pdev;
}
#else
static struct pci_dev *framebuffer_parent_pci_dev(struct resource *res)
{
	return NULL;
}
#endif

static struct device *framebuffer_parent_dev(struct resource *res)
{
	struct pci_dev *pdev;

	pdev = framebuffer_parent_pci_dev(res);
	if (IS_ERR(pdev))
		return ERR_CAST(pdev);
	else if (pdev)
		return &pdev->dev;

	return NULL;
}

static int framebuffer_probe(struct coreboot_device *dev)
{
	struct lb_framebuffer *fb = &dev->framebuffer;
	struct device *parent;
	struct platform_device *pdev;
	struct resource res;
	int ret;
#if !IS_ENABLED(CONFIG_DRM_COREBOOTDRM)
	struct simplefb_platform_data pdata = {
		.width = fb->x_resolution,
		.height = fb->y_resolution,
		.stride = fb->bytes_per_line,
		.format = NULL,
	};
	int i;
	static const struct simplefb_format formats[] = SIMPLEFB_FORMATS;
#endif

	/*
	 * On coreboot systems, the advertised LB_TAG_FRAMEBUFFER entry
	 * in the coreboot table should only be used if the payload did
	 * not pass a framebuffer information to the Linux kernel.
	 *
	 * If the global screen_info data has been filled, the Generic
	 * System Framebuffers (sysfb) will already register a platform
	 * device and pass that screen_info as platform_data to a driver
	 * that can scan-out using the system provided framebuffer.
	 */
	if (sysfb_handles_screen_info())
		return -ENODEV;

	if (!fb->physical_address)
		return -ENODEV;

	res = DEFINE_RES_MEM(fb->physical_address,
			     PAGE_ALIGN(fb->y_resolution * fb->bytes_per_line));
	if (res.end <= res.start)
		return -EINVAL;

	parent = framebuffer_parent_dev(&res);
	if (IS_ERR(parent))
		return PTR_ERR(parent);

#if IS_ENABLED(CONFIG_DRM_COREBOOTDRM)
	pdev = platform_device_register_resndata(parent, "coreboot-framebuffer", 0,
						 &res, 1, fb, fb->size);
	if (IS_ERR(pdev)) {
		pr_warn("coreboot: could not register framebuffer\n");
		ret = PTR_ERR(pdev);
		goto out_put_device_parent;
	}
#else
	/*
	 * FIXME: Coreboot systems should use a driver that binds to
	 *        coreboot-framebuffer devices. Remove support for
	 *        simple-framebuffer at some point.
	 */
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
	if (!pdata.format) {
		ret = -ENODEV;
		goto out_put_device_parent;
	}

	pdev = platform_device_register_resndata(parent,
						 "simple-framebuffer", 0,
						 &res, 1, &pdata,
						 sizeof(pdata));
	if (IS_ERR(pdev)) {
		ret = PTR_ERR(pdev);
		pr_warn("coreboot: could not register framebuffer\n");
		goto out_put_device_parent;
	}
#endif

	ret = 0;

out_put_device_parent:
	if (parent)
		put_device(parent);
	return ret;
}

static const struct coreboot_device_id framebuffer_ids[] = {
	{ .tag = CB_TAG_FRAMEBUFFER },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(coreboot, framebuffer_ids);

static struct coreboot_driver framebuffer_driver = {
	.probe = framebuffer_probe,
	.drv = {
		.name = "framebuffer",
	},
	.id_table = framebuffer_ids,
};
module_coreboot_driver(framebuffer_driver);

MODULE_AUTHOR("Samuel Holland <samuel@sholland.org>");
MODULE_DESCRIPTION("Memory based framebuffer accessed through coreboot table");
MODULE_LICENSE("GPL");
