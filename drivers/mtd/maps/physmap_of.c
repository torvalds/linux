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
#include <linux/mtd/concat.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/slab.h>

struct of_flash_list {
	struct mtd_info *mtd;
	struct map_info map;
	struct resource *res;
};

struct of_flash {
	struct mtd_info		*cmtd;
	int list_size; /* number of elements in of_flash_list */
	struct of_flash_list	list[0];
};

static int of_flash_remove(struct platform_device *dev)
{
	struct of_flash *info;
	int i;

	info = dev_get_drvdata(&dev->dev);
	if (!info)
		return 0;
	dev_set_drvdata(&dev->dev, NULL);

	if (info->cmtd != info->list[0].mtd) {
		mtd_device_unregister(info->cmtd);
		mtd_concat_destroy(info->cmtd);
	}

	if (info->cmtd)
		mtd_device_unregister(info->cmtd);

	for (i = 0; i < info->list_size; i++) {
		if (info->list[i].mtd)
			map_destroy(info->list[i].mtd);

		if (info->list[i].map.virt)
			iounmap(info->list[i].map.virt);

		if (info->list[i].res) {
			release_resource(info->list[i].res);
			kfree(info->list[i].res);
		}
	}
	return 0;
}

static const char * const rom_probe_types[] = {
	"cfi_probe", "jedec_probe", "map_rom" };

/* Helper function to handle probing of the obsolete "direct-mapped"
 * compatible binding, which has an extra "probe-type" property
 * describing the type of flash probe necessary. */
static struct mtd_info *obsolete_probe(struct platform_device *dev,
				       struct map_info *map)
{
	struct device_node *dp = dev->dev.of_node;
	const char *of_probe;
	struct mtd_info *mtd;
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

/* When partitions are set we look for a linux,part-probe property which
   specifies the list of partition probers to use. If none is given then the
   default is use. These take precedence over other device tree
   information. */
static const char * const part_probe_types_def[] = {
	"cmdlinepart", "RedBoot", "ofpart", "ofoldpart", NULL };

static const char * const *of_get_probes(struct device_node *dp)
{
	const char *cp;
	int cplen;
	unsigned int l;
	unsigned int count;
	const char **res;

	cp = of_get_property(dp, "linux,part-probe", &cplen);
	if (cp == NULL)
		return part_probe_types_def;

	count = 0;
	for (l = 0; l != cplen; l++)
		if (cp[l] == 0)
			count++;

	res = kzalloc((count + 1)*sizeof(*res), GFP_KERNEL);
	count = 0;
	while (cplen > 0) {
		res[count] = cp;
		l = strlen(cp) + 1;
		cp += l;
		cplen -= l;
		count++;
	}
	return res;
}

static void of_free_probes(const char * const *probes)
{
	if (probes != part_probe_types_def)
		kfree(probes);
}

static struct of_device_id of_flash_match[];
static int of_flash_probe(struct platform_device *dev)
{
	const char * const *part_probe_types;
	const struct of_device_id *match;
	struct device_node *dp = dev->dev.of_node;
	struct resource res;
	struct of_flash *info;
	const char *probe_type;
	const __be32 *width;
	int err;
	int i;
	int count;
	const __be32 *p;
	int reg_tuple_size;
	struct mtd_info **mtd_list = NULL;
	resource_size_t res_size;
	struct mtd_part_parser_data ppdata;
	bool map_indirect;
	const char *mtd_name = NULL;

	match = of_match_device(of_flash_match, &dev->dev);
	if (!match)
		return -EINVAL;
	probe_type = match->data;

	reg_tuple_size = (of_n_addr_cells(dp) + of_n_size_cells(dp)) * sizeof(u32);

	of_property_read_string(dp, "linux,mtd-name", &mtd_name);

	/*
	 * Get number of "reg" tuples. Scan for MTD devices on area's
	 * described by each "reg" region. This makes it possible (including
	 * the concat support) to support the Intel P30 48F4400 chips which
	 * consists internally of 2 non-identical NOR chips on one die.
	 */
	p = of_get_property(dp, "reg", &count);
	if (count % reg_tuple_size != 0) {
		dev_err(&dev->dev, "Malformed reg property on %s\n",
				dev->dev.of_node->full_name);
		err = -EINVAL;
		goto err_flash_remove;
	}
	count /= reg_tuple_size;

	map_indirect = of_property_read_bool(dp, "no-unaligned-direct-access");

	err = -ENOMEM;
	info = devm_kzalloc(&dev->dev,
			    sizeof(struct of_flash) +
			    sizeof(struct of_flash_list) * count, GFP_KERNEL);
	if (!info)
		goto err_flash_remove;

	dev_set_drvdata(&dev->dev, info);

	mtd_list = kzalloc(sizeof(*mtd_list) * count, GFP_KERNEL);
	if (!mtd_list)
		goto err_flash_remove;

	for (i = 0; i < count; i++) {
		err = -ENXIO;
		if (of_address_to_resource(dp, i, &res)) {
			/*
			 * Continue with next register tuple if this
			 * one is not mappable
			 */
			continue;
		}

		dev_dbg(&dev->dev, "of_flash device: %pR\n", &res);

		err = -EBUSY;
		res_size = resource_size(&res);
		info->list[i].res = request_mem_region(res.start, res_size,
						       dev_name(&dev->dev));
		if (!info->list[i].res)
			goto err_out;

		err = -ENXIO;
		width = of_get_property(dp, "bank-width", NULL);
		if (!width) {
			dev_err(&dev->dev, "Can't get bank width from device"
				" tree\n");
			goto err_out;
		}

		info->list[i].map.name = mtd_name ?: dev_name(&dev->dev);
		info->list[i].map.phys = res.start;
		info->list[i].map.size = res_size;
		info->list[i].map.bankwidth = be32_to_cpup(width);
		info->list[i].map.device_node = dp;

		err = -ENOMEM;
		info->list[i].map.virt = ioremap(info->list[i].map.phys,
						 info->list[i].map.size);
		if (!info->list[i].map.virt) {
			dev_err(&dev->dev, "Failed to ioremap() flash"
				" region\n");
			goto err_out;
		}

		simple_map_init(&info->list[i].map);

		/*
		 * On some platforms (e.g. MPC5200) a direct 1:1 mapping
		 * may cause problems with JFFS2 usage, as the local bus (LPB)
		 * doesn't support unaligned accesses as implemented in the
		 * JFFS2 code via memcpy(). By setting NO_XIP, the
		 * flash will not be exposed directly to the MTD users
		 * (e.g. JFFS2) any more.
		 */
		if (map_indirect)
			info->list[i].map.phys = NO_XIP;

		if (probe_type) {
			info->list[i].mtd = do_map_probe(probe_type,
							 &info->list[i].map);
		} else {
			info->list[i].mtd = obsolete_probe(dev,
							   &info->list[i].map);
		}
		mtd_list[i] = info->list[i].mtd;

		err = -ENXIO;
		if (!info->list[i].mtd) {
			dev_err(&dev->dev, "do_map_probe() failed\n");
			goto err_out;
		} else {
			info->list_size++;
		}
		info->list[i].mtd->owner = THIS_MODULE;
		info->list[i].mtd->dev.parent = &dev->dev;
	}

	err = 0;
	info->cmtd = NULL;
	if (info->list_size == 1) {
		info->cmtd = info->list[0].mtd;
	} else if (info->list_size > 1) {
		/*
		 * We detected multiple devices. Concatenate them together.
		 */
		info->cmtd = mtd_concat_create(mtd_list, info->list_size,
					       dev_name(&dev->dev));
	}
	if (info->cmtd == NULL)
		err = -ENXIO;

	if (err)
		goto err_out;

	ppdata.of_node = dp;
	part_probe_types = of_get_probes(dp);
	mtd_device_parse_register(info->cmtd, part_probe_types, &ppdata,
			NULL, 0);
	of_free_probes(part_probe_types);

	kfree(mtd_list);

	return 0;

err_out:
	kfree(mtd_list);
err_flash_remove:
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
		.compatible     = "mtd-ram",
		.data           = (void *)"map_ram",
	},
	{
		.type		= "rom",
		.compatible	= "direct-mapped"
	},
	{ },
};
MODULE_DEVICE_TABLE(of, of_flash_match);

static struct platform_driver of_flash_driver = {
	.driver = {
		.name = "of-flash",
		.owner = THIS_MODULE,
		.of_match_table = of_flash_match,
	},
	.probe		= of_flash_probe,
	.remove		= of_flash_remove,
};

module_platform_driver(of_flash_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vitaly Wool <vwool@ru.mvista.com>");
MODULE_DESCRIPTION("Device tree based MTD map driver");
