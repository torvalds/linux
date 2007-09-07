/*
 * Normal mappings of chips in physical memory for OF devices
 *
 * Copyright (C) 2006 MontaVista Software Inc.
 * Author: Vitaly Wool <vwool@ru.mvista.com>
 *
 * Revised to handle newer style flash binding by:
 *   Copyright (C) 2007 David Gibson, IBM Corporation.
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
	struct mtd_partition	*parts;
#endif
};

#ifdef CONFIG_MTD_PARTITIONS
static int parse_obsolete_partitions(struct of_device *dev,
				     struct physmap_flash_info *info,
				     struct device_node *dp)
{
	int i, plen, nr_parts;
	const struct {
		u32 offset, len;
	} *part;
	const char *names;

	part = of_get_property(dp, "partitions", &plen);
	if (!part)
		return -ENOENT;

	dev_warn(&dev->dev, "Device tree uses obsolete partition map binding\n");

	nr_parts = plen / sizeof(part[0]);

	info->parts = kzalloc(nr_parts * sizeof(struct mtd_partition), GFP_KERNEL);
	if (!info->parts) {
		printk(KERN_ERR "Can't allocate the flash partition data!\n");
		return -ENOMEM;
	}

	names = of_get_property(dp, "partition-names", &plen);

	for (i = 0; i < nr_parts; i++) {
		info->parts[i].offset = part->offset;
		info->parts[i].size   = part->len & ~1;
		if (part->len & 1) /* bit 0 set signifies read only partition */
			info->parts[i].mask_flags = MTD_WRITEABLE;

		if (names && (plen > 0)) {
			int len = strlen(names) + 1;

			info->parts[i].name = (char *)names;
			plen -= len;
			names += len;
		} else {
			info->parts[i].name = "unnamed";
		}

		part++;
	}

	return nr_parts;
}

static int __devinit process_partitions(struct physmap_flash_info *info,
					struct of_device *dev)
{
	const char *partname;
	static const char *part_probe_types[]
		= { "cmdlinepart", "RedBoot", NULL };
	struct device_node *dp = dev->node, *pp;
	int nr_parts, i;

	/* First look for RedBoot table or partitions on the command
	 * line, these take precedence over device tree information */
	nr_parts = parse_mtd_partitions(info->mtd, part_probe_types,
					&info->parts, 0);
	if (nr_parts > 0) {
		add_mtd_partitions(info->mtd, info->parts, nr_parts);
		return 0;
	}

	/* First count the subnodes */
	nr_parts = 0;
	for (pp = dp->child; pp; pp = pp->sibling)
		nr_parts++;

	if (nr_parts) {
		info->parts = kzalloc(nr_parts * sizeof(struct mtd_partition),
				      GFP_KERNEL);
		if (!info->parts) {
			printk(KERN_ERR "Can't allocate the flash partition data!\n");
			return -ENOMEM;
		}

		for (pp = dp->child, i = 0 ; pp; pp = pp->sibling, i++) {
			const u32 *reg;
			int len;

			reg = of_get_property(pp, "reg", &len);
			if (!reg || (len != 2*sizeof(u32))) {
				dev_err(&dev->dev, "Invalid 'reg' on %s\n",
					dp->full_name);
				kfree(info->parts);
				info->parts = NULL;
				return -EINVAL;
			}
			info->parts[i].offset = reg[0];
			info->parts[i].size = reg[1];

			partname = of_get_property(pp, "label", &len);
			if (!partname)
				partname = of_get_property(pp, "name", &len);
			info->parts[i].name = (char *)partname;

			if (of_get_property(pp, "read-only", &len))
				info->parts[i].mask_flags = MTD_WRITEABLE;
		}
	} else {
		nr_parts = parse_obsolete_partitions(dev, info, dp);
	}

	if (nr_parts < 0)
		return nr_parts;

	if (nr_parts > 0)
		add_mtd_partitions(info->mtd, info->parts, nr_parts);
	else
		add_mtd_device(info->mtd);

	return 0;
}
#else /* MTD_PARTITIONS */
static int __devinit process_partitions(struct physmap_flash_info *info,
					struct device_node *dev)
{
	add_mtd_device(info->mtd);
	return 0;
}
#endif /* MTD_PARTITIONS */

static int of_physmap_remove(struct of_device *dev)
{
	struct physmap_flash_info *info;

	info = dev_get_drvdata(&dev->dev);
	if (info == NULL)
		return 0;
	dev_set_drvdata(&dev->dev, NULL);

	if (info->mtd != NULL) {
#ifdef CONFIG_MTD_PARTITIONS
		if (info->parts) {
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

/* Helper function to handle probing of the obsolete "direct-mapped"
 * compatible binding, which has an extra "probe-type" property
 * describing the type of flash probe necessary. */
static struct mtd_info * __devinit obsolete_probe(struct of_device *dev,
						  struct map_info *map)
{
	struct device_node *dp = dev->node;
	const char *of_probe;
	struct mtd_info *mtd;
	static const char *rom_probe_types[]
		= { "cfi_probe", "jedec_probe", "map_rom"};
	int i;

	dev_warn(&dev->dev, "Device tree uses obsolete \"direct-mapped\" "
		 "flash binding\n");

	of_probe = of_get_property(dp, "probe-type", NULL);
	if (!of_probe) {
		for (i = 0; i < ARRAY_SIZE(rom_probe_types); i++) {
			mtd = do_map_probe(rom_probe_types[i], map);
			if (mtd)
				return mtd;
		}
		return NULL;
	} else if (strcmp(of_probe, "CFI") == 0) {
		return do_map_probe("cfi_probe", map);
	} else if (strcmp(of_probe, "JEDEC") == 0) {
		return do_map_probe("jedec_probe", map);
	} else {
		if (strcmp(of_probe, "ROM") != 0)
			dev_dbg(&dev->dev, "obsolete_probe: don't know probe type "
				"'%s', mapping as rom\n", of_probe);
		return do_map_probe("mtd_rom", map);
	}
}

static int __devinit of_physmap_probe(struct of_device *dev, const struct of_device_id *match)
{
	struct device_node *dp = dev->node;
	struct resource res;
	struct physmap_flash_info *info;
	const char *probe_type = (const char *)match->data;
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

	if (probe_type)
		info->mtd = do_map_probe(probe_type, &info->map);
	else
		info->mtd = obsolete_probe(dev, &info->map);

	if (info->mtd == NULL) {
		dev_err(&dev->dev, "map_probe failed\n");
		err = -ENXIO;
		goto err_out;
	}
	info->mtd->owner = THIS_MODULE;

	return process_partitions(info, dev);

err_out:
	of_physmap_remove(dev);
	return err;

	return 0;


}

static struct of_device_id of_physmap_match[] = {
	{
		.compatible	= "cfi-flash",
		.data		= (void *)"cfi_probe",
	},
	{
		/* FIXME: JEDEC chips can't be safely and reliably
		 * probed, although the mtd code gets it right in
		 * practice most of the time.  We should use the
		 * vendor and device ids specified by the binding to
		 * bypass the heuristic probe code, but the mtd layer
		 * provides, at present, no interface for doing so
		 * :(. */
		.compatible	= "jedec-flash",
		.data		= (void *)"jedec_probe",
	},
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
