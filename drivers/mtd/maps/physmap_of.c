/*
 * Flash mappings described by the OF (or flattened) device tree
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
#include <linux/init.h>
#include <linux/device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/of.h>
#include <linux/of_platform.h>

struct of_flash {
	struct mtd_info		*mtd;
	struct map_info		map;
	struct resource		*res;
#ifdef CONFIG_MTD_PARTITIONS
	struct mtd_partition	*parts;
#endif
};

#ifdef CONFIG_MTD_PARTITIONS
#define OF_FLASH_PARTS(info)	((info)->parts)

static int parse_obsolete_partitions(struct of_device *dev,
				     struct of_flash *info,
				     struct device_node *dp)
{
	int i, plen, nr_parts;
	const struct {
		u32 offset, len;
	} *part;
	const char *names;

	part = of_get_property(dp, "partitions", &plen);
	if (!part)
		return 0; /* No partitions found */

	dev_warn(&dev->dev, "Device tree uses obsolete partition map binding\n");

	nr_parts = plen / sizeof(part[0]);

	info->parts = kzalloc(nr_parts * sizeof(*info->parts), GFP_KERNEL);
	if (!info->parts)
		return -ENOMEM;

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

static int __devinit parse_partitions(struct of_flash *info,
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

	if (nr_parts == 0)
		return parse_obsolete_partitions(dev, info, dp);

	info->parts = kzalloc(nr_parts * sizeof(*info->parts),
			      GFP_KERNEL);
	if (!info->parts)
		return -ENOMEM;

	for (pp = dp->child, i = 0; pp; pp = pp->sibling, i++) {
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

	return nr_parts;
}
#else /* MTD_PARTITIONS */
#define	OF_FLASH_PARTS(info)		(0)
#define parse_partitions(info, dev)	(0)
#endif /* MTD_PARTITIONS */

static int of_flash_remove(struct of_device *dev)
{
	struct of_flash *info;

	info = dev_get_drvdata(&dev->dev);
	if (!info)
		return 0;
	dev_set_drvdata(&dev->dev, NULL);

	if (info->mtd) {
		if (OF_FLASH_PARTS(info)) {
			del_mtd_partitions(info->mtd);
			kfree(OF_FLASH_PARTS(info));
		} else {
			del_mtd_device(info->mtd);
		}
		map_destroy(info->mtd);
	}

	if (info->map.virt)
		iounmap(info->map.virt);

	if (info->res) {
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
			dev_warn(&dev->dev, "obsolete_probe: don't know probe "
				 "type '%s', mapping as rom\n", of_probe);
		return do_map_probe("mtd_rom", map);
	}
}

static int __devinit of_flash_probe(struct of_device *dev,
				    const struct of_device_id *match)
{
	struct device_node *dp = dev->node;
	struct resource res;
	struct of_flash *info;
	const char *probe_type = match->data;
	const u32 *width;
	int err;

	err = -ENXIO;
	if (of_address_to_resource(dp, 0, &res)) {
		dev_err(&dev->dev, "Can't get IO address from device tree\n");
		goto err_out;
	}

       	dev_dbg(&dev->dev, "of_flash device: %.8llx-%.8llx\n",
		(unsigned long long)res.start, (unsigned long long)res.end);

	err = -ENOMEM;
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		goto err_out;
	memset(info, 0, sizeof(*info));

	dev_set_drvdata(&dev->dev, info);

	err = -EBUSY;
	info->res = request_mem_region(res.start, res.end - res.start + 1,
				       dev->dev.bus_id);
	if (!info->res)
		goto err_out;

	err = -ENXIO;
	width = of_get_property(dp, "bank-width", NULL);
	if (!width) {
		dev_err(&dev->dev, "Can't get bank width from device tree\n");
		goto err_out;
	}

	info->map.name = dev->dev.bus_id;
	info->map.phys = res.start;
	info->map.size = res.end - res.start + 1;
	info->map.bankwidth = *width;

	err = -ENOMEM;
	info->map.virt = ioremap(info->map.phys, info->map.size);
	if (!info->map.virt) {
		dev_err(&dev->dev, "Failed to ioremap() flash region\n");
		goto err_out;
	}

	simple_map_init(&info->map);

	if (probe_type)
		info->mtd = do_map_probe(probe_type, &info->map);
	else
		info->mtd = obsolete_probe(dev, &info->map);

	err = -ENXIO;
	if (!info->mtd) {
		dev_err(&dev->dev, "do_map_probe() failed\n");
		goto err_out;
	}
	info->mtd->owner = THIS_MODULE;

	err = parse_partitions(info, dev);
	if (err < 0)
		goto err_out;

	if (err > 0)
		add_mtd_partitions(info->mtd, OF_FLASH_PARTS(info), err);
	else
		add_mtd_device(info->mtd);

	return 0;

err_out:
	of_flash_remove(dev);
	return err;
}

static struct of_device_id of_flash_match[] = {
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
MODULE_DEVICE_TABLE(of, of_flash_match);

static struct of_platform_driver of_flash_driver = {
	.name		= "of-flash",
	.match_table	= of_flash_match,
	.probe		= of_flash_probe,
	.remove		= of_flash_remove,
};

static int __init of_flash_init(void)
{
	return of_register_platform_driver(&of_flash_driver);
}

static void __exit of_flash_exit(void)
{
	of_unregister_platform_driver(&of_flash_driver);
}

module_init(of_flash_init);
module_exit(of_flash_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vitaly Wool <vwool@ru.mvista.com>");
MODULE_DESCRIPTION("Device tree based MTD map driver");
