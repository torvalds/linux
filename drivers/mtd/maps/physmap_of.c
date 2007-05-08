/*
 * Normal mappings of chips in physical memory for OF devices
 *
 * Copyright (C) 2006 MontaVista Software Inc.
 * Author: Vitaly Wool <vwool@ru.mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/of_device.h>
#include <asm/of_platform.h>

struct physmap_flash_info {
	struct mtd_info		*mtd;
	struct map_info		map;
	struct resource		*res;
#ifdef CONFIG_MTD_PARTITIONS
	int			nr_parts;
	struct mtd_partition	*parts;
#endif
};

static const char *rom_probe_types[] = { "cfi_probe", "jedec_probe", "map_rom", NULL };
#ifdef CONFIG_MTD_PARTITIONS
static const char *part_probe_types[] = { "cmdlinepart", "RedBoot", NULL };
#endif

#ifdef CONFIG_MTD_PARTITIONS
static int parse_flash_partitions(struct device_node *node,
		struct mtd_partition **parts)
{
	int i, plen, retval = -ENOMEM;
	const  u32  *part;
	const  char *name;

	part = of_get_property(node, "partitions", &plen);
	if (part == NULL)
		goto err;

	retval = plen / (2 * sizeof(u32));
	*parts = kzalloc(retval * sizeof(struct mtd_partition), GFP_KERNEL);
	if (*parts == NULL) {
		printk(KERN_ERR "Can't allocate the flash partition data!\n");
		goto err;
	}

	name = of_get_property(node, "partition-names", &plen);

	for (i = 0; i < retval; i++) {
		(*parts)[i].offset = *part++;
		(*parts)[i].size   = *part & ~1;
		if (*part++ & 1) /* bit 0 set signifies read only partition */
			(*parts)[i].mask_flags = MTD_WRITEABLE;

		if (name != NULL && plen > 0) {
			int len = strlen(name) + 1;

			(*parts)[i].name = (char *)name;
			plen -= len;
			name += len;
		} else
			(*parts)[i].name = "unnamed";
	}
err:
	return retval;
}
#endif

static int of_physmap_remove(struct of_device *dev)
{
	struct physmap_flash_info *info;

	info = dev_get_drvdata(&dev->dev);
	if (info == NULL)
		return 0;
	dev_set_drvdata(&dev->dev, NULL);

	if (info->mtd != NULL) {
#ifdef CONFIG_MTD_PARTITIONS
		if (info->nr_parts) {
			del_mtd_partitions(info->mtd);
			kfree(info->parts);
		} else {
			del_mtd_device(info->mtd);
		}
#else
		del_mtd_device(info->mtd);
#endif
		map_destroy(info->mtd);
	}

	if (info->map.virt != NULL)
		iounmap(info->map.virt);

	if (info->res != NULL) {
		release_resource(info->res);
		kfree(info->res);
	}

	return 0;
}

static int __devinit of_physmap_probe(struct of_device *dev, const struct of_device_id *match)
{
	struct device_node *dp = dev->node;
	struct resource res;
	struct physmap_flash_info *info;
	const char **probe_type;
	const char *of_probe;
	const u32 *width;
	int err;


	if (of_address_to_resource(dp, 0, &res)) {
		dev_err(&dev->dev, "Can't get the flash mapping!\n");
		err = -EINVAL;
		goto err_out;
	}

       	dev_dbg(&dev->dev, "physmap flash device: %.8llx at %.8llx\n",
	    (unsigned long long)res.end - res.start + 1,
	    (unsigned long long)res.start);

	info = kzalloc(sizeof(struct physmap_flash_info), GFP_KERNEL);
	if (info == NULL) {
		err = -ENOMEM;
		goto err_out;
	}
	memset(info, 0, sizeof(*info));

	dev_set_drvdata(&dev->dev, info);

	info->res = request_mem_region(res.start, res.end - res.start + 1,
			dev->dev.bus_id);
	if (info->res == NULL) {
		dev_err(&dev->dev, "Could not reserve memory region\n");
		err = -ENOMEM;
		goto err_out;
	}

	width = of_get_property(dp, "bank-width", NULL);
	if (width == NULL) {
		dev_err(&dev->dev, "Can't get the flash bank width!\n");
		err = -EINVAL;
		goto err_out;
	}

	info->map.name = dev->dev.bus_id;
	info->map.phys = res.start;
	info->map.size = res.end - res.start + 1;
	info->map.bankwidth = *width;

	info->map.virt = ioremap(info->map.phys, info->map.size);
	if (info->map.virt == NULL) {
		dev_err(&dev->dev, "Failed to ioremap flash region\n");
		err = EIO;
		goto err_out;
	}

	simple_map_init(&info->map);

	of_probe = of_get_property(dp, "probe-type", NULL);
	if (of_probe == NULL) {
		probe_type = rom_probe_types;
		for (; info->mtd == NULL && *probe_type != NULL; probe_type++)
			info->mtd = do_map_probe(*probe_type, &info->map);
	} else if (!strcmp(of_probe, "CFI"))
		info->mtd = do_map_probe("cfi_probe", &info->map);
	else if (!strcmp(of_probe, "JEDEC"))
		info->mtd = do_map_probe("jedec_probe", &info->map);
	else {
 		if (strcmp(of_probe, "ROM"))
			dev_dbg(&dev->dev, "map_probe: don't know probe type "
			"'%s', mapping as rom\n");
		info->mtd = do_map_probe("mtd_rom", &info->map);
	}
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
	} else if ((err = parse_flash_partitions(dp, &info->parts)) > 0) {
		dev_info(&dev->dev, "Using OF partition information\n");
		add_mtd_partitions(info->mtd, info->parts, err);
		info->nr_parts = err;
	} else
#endif

	add_mtd_device(info->mtd);
	return 0;

err_out:
	of_physmap_remove(dev);
	return err;

	return 0;


}

static struct of_device_id of_physmap_match[] = {
	{
		.type		= "rom",
		.compatible	= "direct-mapped"
	},
	{ },
};

MODULE_DEVICE_TABLE(of, of_physmap_match);


static struct of_platform_driver of_physmap_flash_driver = {
	.name		= "physmap-flash",
	.match_table	= of_physmap_match,
	.probe		= of_physmap_probe,
	.remove		= of_physmap_remove,
};

static int __init of_physmap_init(void)
{
	return of_register_platform_driver(&of_physmap_flash_driver);
}

static void __exit of_physmap_exit(void)
{
	of_unregister_platform_driver(&of_physmap_flash_driver);
}

module_init(of_physmap_init);
module_exit(of_physmap_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vitaly Wool <vwool@ru.mvista.com>");
MODULE_DESCRIPTION("Configurable MTD map driver for OF");
