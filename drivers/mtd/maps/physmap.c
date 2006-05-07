/*
 * $Id: physmap.c,v 1.39 2005/11/29 14:49:36 gleixner Exp $
 *
 * Normal mappings of chips in physical memory
 *
 * Copyright (C) 2003 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * 031022 - [jsun] add run-time configure and partition setup
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/config.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <asm/io.h>
#include <asm/mach/flash.h>

struct physmap_flash_info {
	struct mtd_info		*mtd;
	struct map_info		map;
	struct resource		*res;
#ifdef CONFIG_MTD_PARTITIONS
	int			nr_parts;
	struct mtd_partition	*parts;
#endif
};


static int physmap_flash_remove(struct platform_device *dev)
{
	struct physmap_flash_info *info;
	struct physmap_flash_data *physmap_data;

	info = platform_get_drvdata(dev);
	if (info == NULL)
		return 0;
	platform_set_drvdata(dev, NULL);

	physmap_data = dev->dev.platform_data;

	if (info->mtd != NULL) {
#ifdef CONFIG_MTD_PARTITIONS
		if (info->nr_parts) {
			del_mtd_partitions(info->mtd);
			kfree(info->parts);
		} else if (physmap_data->nr_parts) {
			del_mtd_partitions(info->mtd);
		} else {
			del_mtd_device(info->mtd);
		}
#else
		del_mtd_device(info->mtd);
#endif
		map_destroy(info->mtd);
	}

	if (info->map.virt != NULL)
		iounmap((void *)info->map.virt);

	if (info->res != NULL) {
		release_resource(info->res);
		kfree(info->res);
	}

	return 0;
}

static const char *rom_probe_types[] = { "cfi_probe", "jedec_probe", "map_rom", NULL };
#ifdef CONFIG_MTD_PARTITIONS
static const char *part_probe_types[] = { "cmdlinepart", "RedBoot", NULL };
#endif

static int physmap_flash_probe(struct platform_device *dev)
{
	struct physmap_flash_data *physmap_data;
	struct physmap_flash_info *info;
	const char **probe_type;
	int err;

	physmap_data = dev->dev.platform_data;
	if (physmap_data == NULL)
		return -ENODEV;

       	printk(KERN_NOTICE "physmap platform flash device: %.8lx at %.8lx\n",
		dev->resource->end - dev->resource->start + 1,
		dev->resource->start);

	info = kmalloc(sizeof(struct physmap_flash_info), GFP_KERNEL);
	if (info == NULL) {
		err = -ENOMEM;
		goto err_out;
	}
	memset(info, 0, sizeof(*info));

	platform_set_drvdata(dev, info);

	info->res = request_mem_region(dev->resource->start,
			dev->resource->end - dev->resource->start + 1,
			dev->dev.bus_id);
	if (info->res == NULL) {
		dev_err(&dev->dev, "Could not reserve memory region\n");
		err = -ENOMEM;
		goto err_out;
	}

	info->map.name = dev->dev.bus_id;
	info->map.phys = dev->resource->start;
	info->map.size = dev->resource->end - dev->resource->start + 1;
	info->map.bankwidth = physmap_data->width;
	info->map.set_vpp = physmap_data->set_vpp;

	info->map.virt = ioremap(info->map.phys, info->map.size);
	if (info->map.virt == NULL) {
		dev_err(&dev->dev, "Failed to ioremap flash region\n");
		err = EIO;
		goto err_out;
	}

	simple_map_init(&info->map);

	probe_type = rom_probe_types;
	for (; info->mtd == NULL && *probe_type != NULL; probe_type++)
		info->mtd = do_map_probe(*probe_type, &info->map);
	if (info->mtd == NULL) {
		dev_err(&dev->dev, "map_probe failed\n");
		err = -ENXIO;
		goto err_out;
	}
	info->mtd->owner = THIS_MODULE;

#ifdef CONFIG_MTD_PARTITIONS
	err = parse_mtd_partitions(info->mtd, part_probe_types, &info->parts, 0);
	if (err > 0) {
		add_mtd_partitions(info->mtd, info->parts, err);
		return 0;
	}

	if (physmap_data->nr_parts) {
		printk(KERN_NOTICE "Using physmap partition information\n");
		add_mtd_partitions(info->mtd, physmap_data->parts,
						physmap_data->nr_parts);
		return 0;
	}
#endif

	add_mtd_device(info->mtd);
	return 0;

err_out:
	physmap_flash_remove(dev);
	return err;
}

static struct platform_driver physmap_flash_driver = {
	.probe		= physmap_flash_probe,
	.remove		= physmap_flash_remove,
	.driver		= {
		.name	= "physmap-flash",
	},
};


#ifdef CONFIG_MTD_PHYSMAP_LEN
#if CONFIG_MTD_PHYSMAP_LEN != 0
#warning using PHYSMAP compat code
#define PHYSMAP_COMPAT
#endif
#endif

#ifdef PHYSMAP_COMPAT
static struct physmap_flash_data physmap_flash_data = {
	.width		= CONFIG_MTD_PHYSMAP_BANKWIDTH,
};

static struct resource physmap_flash_resource = {
	.start		= CONFIG_MTD_PHYSMAP_START,
	.end		= CONFIG_MTD_PHYSMAP_START + CONFIG_MTD_PHYSMAP_LEN,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device physmap_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &physmap_flash_data,
	},
	.num_resources	= 1,
	.resource	= &physmap_flash_resource,
};

void physmap_configure(unsigned long addr, unsigned long size,
		int bankwidth, void (*set_vpp)(struct map_info *, int))
{
	physmap_flash_resource.start = addr;
	physmap_flash_resource.end = addr + size - 1;
	physmap_flash_data.width = bankwidth;
	physmap_flash_data.set_vpp = set_vpp;
}

#ifdef CONFIG_MTD_PARTITIONS
void physmap_set_partitions(struct mtd_partition *parts, int num_parts)
{
	physmap_flash_data.nr_parts = num_parts;
	physmap_flash_data.parts = parts;
}
#endif
#endif

static int __init physmap_init(void)
{
	int err;

	err = platform_driver_register(&physmap_flash_driver);
#ifdef PHYSMAP_COMPAT
	if (err == 0)
		platform_device_register(&physmap_flash);
#endif

	return err;
}

static void __exit physmap_exit(void)
{
#ifdef PHYSMAP_COMPAT
	platform_device_unregister(&physmap_flash);
#endif
	platform_driver_unregister(&physmap_flash_driver);
}

module_init(physmap_init);
module_exit(physmap_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("Generic configurable MTD map driver");
