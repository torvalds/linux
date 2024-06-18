// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2019 Google Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Provides a simple driver to control the ASPEED P2A interface which allows
 * the host to read and write to various regions of the BMC's memory.
 */

#include <linux/fs.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <linux/aspeed-p2a-ctrl.h>

#define DEVICE_NAME	"aspeed-p2a-ctrl"

/* SCU2C is a Misc. Control Register. */
#define SCU2C 0x2c
/* SCU180 is the PCIe Configuration Setting Control Register. */
#define SCU180 0x180
/* Bit 1 controls the P2A bridge, while bit 0 controls the entire VGA device
 * on the PCI bus.
 */
#define SCU180_ENP2A BIT(1)

/* The ast2400/2500 both have six ranges. */
#define P2A_REGION_COUNT 6

struct region {
	u64 min;
	u64 max;
	u32 bit;
};

struct aspeed_p2a_model_data {
	/* min, max, bit */
	struct region regions[P2A_REGION_COUNT];
};

struct aspeed_p2a_ctrl {
	struct miscdevice miscdev;
	struct regmap *regmap;

	const struct aspeed_p2a_model_data *config;

	/* Access to these needs to be locked, held via probe, mapping ioctl,
	 * and release, remove.
	 */
	struct mutex tracking;
	u32 readers;
	u32 readerwriters[P2A_REGION_COUNT];

	phys_addr_t mem_base;
	resource_size_t mem_size;
};

struct aspeed_p2a_user {
	struct file *file;
	struct aspeed_p2a_ctrl *parent;

	/* The entire memory space is opened for reading once the bridge is
	 * enabled, therefore this needs only to be tracked once per user.
	 * If any user has it open for read, the bridge must stay enabled.
	 */
	u32 read;

	/* Each entry of the array corresponds to a P2A Region.  If the user
	 * opens for read or readwrite, the reference goes up here.  On
	 * release, this array is walked and references adjusted accordingly.
	 */
	u32 readwrite[P2A_REGION_COUNT];
};

static void aspeed_p2a_enable_bridge(struct aspeed_p2a_ctrl *p2a_ctrl)
{
	regmap_update_bits(p2a_ctrl->regmap,
		SCU180, SCU180_ENP2A, SCU180_ENP2A);
}

static void aspeed_p2a_disable_bridge(struct aspeed_p2a_ctrl *p2a_ctrl)
{
	regmap_update_bits(p2a_ctrl->regmap, SCU180, SCU180_ENP2A, 0);
}

static int aspeed_p2a_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long vsize;
	pgprot_t prot;
	struct aspeed_p2a_user *priv = file->private_data;
	struct aspeed_p2a_ctrl *ctrl = priv->parent;

	if (ctrl->mem_base == 0 && ctrl->mem_size == 0)
		return -EINVAL;

	vsize = vma->vm_end - vma->vm_start;
	prot = vma->vm_page_prot;

	if (vma->vm_pgoff + vma_pages(vma) > ctrl->mem_size >> PAGE_SHIFT)
		return -EINVAL;

	/* ast2400/2500 AHB accesses are not cache coherent */
	prot = pgprot_noncached(prot);

	if (remap_pfn_range(vma, vma->vm_start,
		(ctrl->mem_base >> PAGE_SHIFT) + vma->vm_pgoff,
		vsize, prot))
		return -EAGAIN;

	return 0;
}

static bool aspeed_p2a_region_acquire(struct aspeed_p2a_user *priv,
		struct aspeed_p2a_ctrl *ctrl,
		struct aspeed_p2a_ctrl_mapping *map)
{
	int i;
	u64 base, end;
	bool matched = false;

	base = map->addr;
	end = map->addr + (map->length - 1);

	/* If the value is a legal u32, it will find a match. */
	for (i = 0; i < P2A_REGION_COUNT; i++) {
		const struct region *curr = &ctrl->config->regions[i];

		/* If the top of this region is lower than your base, skip it.
		 */
		if (curr->max < base)
			continue;

		/* If the bottom of this region is higher than your end, bail.
		 */
		if (curr->min > end)
			break;

		/* Lock this and update it, therefore it someone else is
		 * closing their file out, this'll preserve the increment.
		 */
		mutex_lock(&ctrl->tracking);
		ctrl->readerwriters[i] += 1;
		mutex_unlock(&ctrl->tracking);

		/* Track with the user, so when they close their file, we can
		 * decrement properly.
		 */
		priv->readwrite[i] += 1;

		/* Enable the region as read-write. */
		regmap_update_bits(ctrl->regmap, SCU2C, curr->bit, 0);
		matched = true;
	}

	return matched;
}

static long aspeed_p2a_ioctl(struct file *file, unsigned int cmd,
		unsigned long data)
{
	struct aspeed_p2a_user *priv = file->private_data;
	struct aspeed_p2a_ctrl *ctrl = priv->parent;
	void __user *arg = (void __user *)data;
	struct aspeed_p2a_ctrl_mapping map;

	if (copy_from_user(&map, arg, sizeof(map)))
		return -EFAULT;

	switch (cmd) {
	case ASPEED_P2A_CTRL_IOCTL_SET_WINDOW:
		/* If they want a region to be read-only, since the entire
		 * region is read-only once enabled, we just need to track this
		 * user wants to read from the bridge, and if it's not enabled.
		 * Enable it.
		 */
		if (map.flags == ASPEED_P2A_CTRL_READ_ONLY) {
			mutex_lock(&ctrl->tracking);
			ctrl->readers += 1;
			mutex_unlock(&ctrl->tracking);

			/* Track with the user, so when they close their file,
			 * we can decrement properly.
			 */
			priv->read += 1;
		} else if (map.flags == ASPEED_P2A_CTRL_READWRITE) {
			/* If we don't acquire any region return error. */
			if (!aspeed_p2a_region_acquire(priv, ctrl, &map)) {
				return -EINVAL;
			}
		} else {
			/* Invalid map flags. */
			return -EINVAL;
		}

		aspeed_p2a_enable_bridge(ctrl);
		return 0;
	case ASPEED_P2A_CTRL_IOCTL_GET_MEMORY_CONFIG:
		/* This is a request for the memory-region and corresponding
		 * length that is used by the driver for mmap.
		 */

		map.flags = 0;
		map.addr = ctrl->mem_base;
		map.length = ctrl->mem_size;

		return copy_to_user(arg, &map, sizeof(map)) ? -EFAULT : 0;
	}

	return -EINVAL;
}


/*
 * When a user opens this file, we create a structure to track their mappings.
 *
 * A user can map a region as read-only (bridge enabled), or read-write (bit
 * flipped, and bridge enabled).  Either way, this tracking is used, s.t. when
 * they release the device references are handled.
 *
 * The bridge is not enabled until a user calls an ioctl to map a region,
 * simply opening the device does not enable it.
 */
static int aspeed_p2a_open(struct inode *inode, struct file *file)
{
	struct aspeed_p2a_user *priv;

	priv = kmalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->file = file;
	priv->read = 0;
	memset(priv->readwrite, 0, sizeof(priv->readwrite));

	/* The file's private_data is initialized to the p2a_ctrl. */
	priv->parent = file->private_data;

	/* Set the file's private_data to the user's data. */
	file->private_data = priv;

	return 0;
}

/*
 * This will close the users mappings.  It will go through what they had opened
 * for readwrite, and decrement those counts.  If at the end, this is the last
 * user, it'll close the bridge.
 */
static int aspeed_p2a_release(struct inode *inode, struct file *file)
{
	int i;
	u32 bits = 0;
	bool open_regions = false;
	struct aspeed_p2a_user *priv = file->private_data;

	/* Lock others from changing these values until everything is updated
	 * in one pass.
	 */
	mutex_lock(&priv->parent->tracking);

	priv->parent->readers -= priv->read;

	for (i = 0; i < P2A_REGION_COUNT; i++) {
		priv->parent->readerwriters[i] -= priv->readwrite[i];

		if (priv->parent->readerwriters[i] > 0)
			open_regions = true;
		else
			bits |= priv->parent->config->regions[i].bit;
	}

	/* Setting a bit to 1 disables the region, so let's just OR with the
	 * above to disable any.
	 */

	/* Note, if another user is trying to ioctl, they can't grab tracking,
	 * and therefore can't grab either register mutex.
	 * If another user is trying to close, they can't grab tracking either.
	 */
	regmap_update_bits(priv->parent->regmap, SCU2C, bits, bits);

	/* If parent->readers is zero and open windows is 0, disable the
	 * bridge.
	 */
	if (!open_regions && priv->parent->readers == 0)
		aspeed_p2a_disable_bridge(priv->parent);

	mutex_unlock(&priv->parent->tracking);

	kfree(priv);

	return 0;
}

static const struct file_operations aspeed_p2a_ctrl_fops = {
	.owner = THIS_MODULE,
	.mmap = aspeed_p2a_mmap,
	.unlocked_ioctl = aspeed_p2a_ioctl,
	.open = aspeed_p2a_open,
	.release = aspeed_p2a_release,
};

/* The regions are controlled by SCU2C */
static void aspeed_p2a_disable_all(struct aspeed_p2a_ctrl *p2a_ctrl)
{
	int i;
	u32 value = 0;

	for (i = 0; i < P2A_REGION_COUNT; i++)
		value |= p2a_ctrl->config->regions[i].bit;

	regmap_update_bits(p2a_ctrl->regmap, SCU2C, value, value);

	/* Disable the bridge. */
	aspeed_p2a_disable_bridge(p2a_ctrl);
}

static int aspeed_p2a_ctrl_probe(struct platform_device *pdev)
{
	struct aspeed_p2a_ctrl *misc_ctrl;
	struct device *dev;
	struct resource resm;
	struct device_node *node;
	int rc = 0;

	dev = &pdev->dev;

	misc_ctrl = devm_kzalloc(dev, sizeof(*misc_ctrl), GFP_KERNEL);
	if (!misc_ctrl)
		return -ENOMEM;

	mutex_init(&misc_ctrl->tracking);

	/* optional. */
	node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (node) {
		rc = of_address_to_resource(node, 0, &resm);
		of_node_put(node);
		if (rc) {
			dev_err(dev, "Couldn't address to resource for reserved memory\n");
			return -ENODEV;
		}

		misc_ctrl->mem_size = resource_size(&resm);
		misc_ctrl->mem_base = resm.start;
	}

	misc_ctrl->regmap = syscon_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(misc_ctrl->regmap)) {
		dev_err(dev, "Couldn't get regmap\n");
		return -ENODEV;
	}

	misc_ctrl->config = of_device_get_match_data(dev);

	dev_set_drvdata(&pdev->dev, misc_ctrl);

	aspeed_p2a_disable_all(misc_ctrl);

	misc_ctrl->miscdev.minor = MISC_DYNAMIC_MINOR;
	misc_ctrl->miscdev.name = DEVICE_NAME;
	misc_ctrl->miscdev.fops = &aspeed_p2a_ctrl_fops;
	misc_ctrl->miscdev.parent = dev;

	rc = misc_register(&misc_ctrl->miscdev);
	if (rc)
		dev_err(dev, "Unable to register device\n");

	return rc;
}

static void aspeed_p2a_ctrl_remove(struct platform_device *pdev)
{
	struct aspeed_p2a_ctrl *p2a_ctrl = dev_get_drvdata(&pdev->dev);

	misc_deregister(&p2a_ctrl->miscdev);
}

#define SCU2C_DRAM	BIT(25)
#define SCU2C_SPI	BIT(24)
#define SCU2C_SOC	BIT(23)
#define SCU2C_FLASH	BIT(22)

static const struct aspeed_p2a_model_data ast2400_model_data = {
	.regions = {
		{0x00000000, 0x17FFFFFF, SCU2C_FLASH},
		{0x18000000, 0x1FFFFFFF, SCU2C_SOC},
		{0x20000000, 0x2FFFFFFF, SCU2C_FLASH},
		{0x30000000, 0x3FFFFFFF, SCU2C_SPI},
		{0x40000000, 0x5FFFFFFF, SCU2C_DRAM},
		{0x60000000, 0xFFFFFFFF, SCU2C_SOC},
	}
};

static const struct aspeed_p2a_model_data ast2500_model_data = {
	.regions = {
		{0x00000000, 0x0FFFFFFF, SCU2C_FLASH},
		{0x10000000, 0x1FFFFFFF, SCU2C_SOC},
		{0x20000000, 0x3FFFFFFF, SCU2C_FLASH},
		{0x40000000, 0x5FFFFFFF, SCU2C_SOC},
		{0x60000000, 0x7FFFFFFF, SCU2C_SPI},
		{0x80000000, 0xFFFFFFFF, SCU2C_DRAM},
	}
};

static const struct of_device_id aspeed_p2a_ctrl_match[] = {
	{ .compatible = "aspeed,ast2400-p2a-ctrl",
	  .data = &ast2400_model_data },
	{ .compatible = "aspeed,ast2500-p2a-ctrl",
	  .data = &ast2500_model_data },
	{ },
};

static struct platform_driver aspeed_p2a_ctrl_driver = {
	.driver = {
		.name		= DEVICE_NAME,
		.of_match_table = aspeed_p2a_ctrl_match,
	},
	.probe = aspeed_p2a_ctrl_probe,
	.remove_new = aspeed_p2a_ctrl_remove,
};

module_platform_driver(aspeed_p2a_ctrl_driver);

MODULE_DEVICE_TABLE(of, aspeed_p2a_ctrl_match);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick Venture <venture@google.com>");
MODULE_DESCRIPTION("Control for aspeed 2400/2500 P2A VGA HOST to BMC mappings");
