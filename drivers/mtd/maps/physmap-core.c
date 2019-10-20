// SPDX-License-Identifier: GPL-2.0+
/*
 * Normal mappings of chips in physical memory
 *
 * Copyright (C) 2003 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * 031022 - [jsun] add run-time configure and partition setup
 *
 * Device tree support:
 *    Copyright (C) 2006 MontaVista Software Inc.
 *    Author: Vitaly Wool <vwool@ru.mvista.com>
 *
 *    Revised to handle newer style flash binding by:
 *    Copyright (C) 2007 David Gibson, IBM Corporation.
 *
 * GPIO address extension:
 *    Handle the case where a flash device is mostly addressed using physical
 *    line and supplemented by GPIOs.  This way you can hook up say a 8MiB flash
 *    to a 2MiB memory range and use the GPIOs to select a particular range.
 *
 *    Copyright © 2000 Nicolas Pitre <nico@cam.org>
 *    Copyright © 2005-2009 Analog Devices Inc.
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
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/concat.h>
#include <linux/mtd/cfi_endian.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/gpio/consumer.h>

#include "physmap-gemini.h"
#include "physmap-ixp4xx.h"
#include "physmap-versatile.h"

struct physmap_flash_info {
	unsigned int		nmaps;
	struct mtd_info		**mtds;
	struct mtd_info		*cmtd;
	struct map_info		*maps;
	spinlock_t		vpp_lock;
	int			vpp_refcnt;
	const char		*probe_type;
	const char * const	*part_types;
	unsigned int		nparts;
	const struct mtd_partition *parts;
	struct gpio_descs	*gpios;
	unsigned int		gpio_values;
	unsigned int		win_order;
};

static int physmap_flash_remove(struct platform_device *dev)
{
	struct physmap_flash_info *info;
	struct physmap_flash_data *physmap_data;
	int i, err;

	info = platform_get_drvdata(dev);
	if (!info)
		return 0;

	if (info->cmtd) {
		err = mtd_device_unregister(info->cmtd);
		if (err)
			return err;

		if (info->cmtd != info->mtds[0])
			mtd_concat_destroy(info->cmtd);
	}

	for (i = 0; i < info->nmaps; i++) {
		if (info->mtds[i])
			map_destroy(info->mtds[i]);
	}

	physmap_data = dev_get_platdata(&dev->dev);
	if (physmap_data && physmap_data->exit)
		physmap_data->exit(dev);

	return 0;
}

static void physmap_set_vpp(struct map_info *map, int state)
{
	struct platform_device *pdev;
	struct physmap_flash_data *physmap_data;
	struct physmap_flash_info *info;
	unsigned long flags;

	pdev = (struct platform_device *)map->map_priv_1;
	physmap_data = dev_get_platdata(&pdev->dev);

	if (!physmap_data->set_vpp)
		return;

	info = platform_get_drvdata(pdev);

	spin_lock_irqsave(&info->vpp_lock, flags);
	if (state) {
		if (++info->vpp_refcnt == 1)    /* first nested 'on' */
			physmap_data->set_vpp(pdev, 1);
	} else {
		if (--info->vpp_refcnt == 0)    /* last nested 'off' */
			physmap_data->set_vpp(pdev, 0);
	}
	spin_unlock_irqrestore(&info->vpp_lock, flags);
}

#if IS_ENABLED(CONFIG_MTD_PHYSMAP_GPIO_ADDR)
static void physmap_set_addr_gpios(struct physmap_flash_info *info,
				   unsigned long ofs)
{
	unsigned int i;

	ofs >>= info->win_order;
	if (info->gpio_values == ofs)
		return;

	for (i = 0; i < info->gpios->ndescs; i++) {
		if ((BIT(i) & ofs) == (BIT(i) & info->gpio_values))
			continue;

		gpiod_set_value(info->gpios->desc[i], !!(BIT(i) & ofs));
	}

	info->gpio_values = ofs;
}

#define win_mask(order)		(BIT(order) - 1)

static map_word physmap_addr_gpios_read(struct map_info *map,
					unsigned long ofs)
{
	struct platform_device *pdev;
	struct physmap_flash_info *info;
	map_word mw;
	u16 word;

	pdev = (struct platform_device *)map->map_priv_1;
	info = platform_get_drvdata(pdev);
	physmap_set_addr_gpios(info, ofs);

	word = readw(map->virt + (ofs & win_mask(info->win_order)));
	mw.x[0] = word;
	return mw;
}

static void physmap_addr_gpios_copy_from(struct map_info *map, void *buf,
					 unsigned long ofs, ssize_t len)
{
	struct platform_device *pdev;
	struct physmap_flash_info *info;

	pdev = (struct platform_device *)map->map_priv_1;
	info = platform_get_drvdata(pdev);

	while (len) {
		unsigned int winofs = ofs & win_mask(info->win_order);
		unsigned int chunklen = min_t(unsigned int, len,
					      BIT(info->win_order) - winofs);

		physmap_set_addr_gpios(info, ofs);
		memcpy_fromio(buf, map->virt + winofs, chunklen);
		len -= chunklen;
		buf += chunklen;
		ofs += chunklen;
	}
}

static void physmap_addr_gpios_write(struct map_info *map, map_word mw,
				     unsigned long ofs)
{
	struct platform_device *pdev;
	struct physmap_flash_info *info;
	u16 word;

	pdev = (struct platform_device *)map->map_priv_1;
	info = platform_get_drvdata(pdev);
	physmap_set_addr_gpios(info, ofs);

	word = mw.x[0];
	writew(word, map->virt + (ofs & win_mask(info->win_order)));
}

static void physmap_addr_gpios_copy_to(struct map_info *map, unsigned long ofs,
				       const void *buf, ssize_t len)
{
	struct platform_device *pdev;
	struct physmap_flash_info *info;

	pdev = (struct platform_device *)map->map_priv_1;
	info = platform_get_drvdata(pdev);

	while (len) {
		unsigned int winofs = ofs & win_mask(info->win_order);
		unsigned int chunklen = min_t(unsigned int, len,
					      BIT(info->win_order) - winofs);

		physmap_set_addr_gpios(info, ofs);
		memcpy_toio(map->virt + winofs, buf, chunklen);
		len -= chunklen;
		buf += chunklen;
		ofs += chunklen;
	}
}

static int physmap_addr_gpios_map_init(struct map_info *map)
{
	map->phys = NO_XIP;
	map->read = physmap_addr_gpios_read;
	map->copy_from = physmap_addr_gpios_copy_from;
	map->write = physmap_addr_gpios_write;
	map->copy_to = physmap_addr_gpios_copy_to;

	return 0;
}
#else
static int physmap_addr_gpios_map_init(struct map_info *map)
{
	return -ENOTSUPP;
}
#endif

#if IS_ENABLED(CONFIG_MTD_PHYSMAP_OF)
static const struct of_device_id of_flash_match[] = {
	{
		.compatible = "cfi-flash",
		.data = "cfi_probe",
	},
	{
		/*
		 * FIXME: JEDEC chips can't be safely and reliably
		 * probed, although the mtd code gets it right in
		 * practice most of the time.  We should use the
		 * vendor and device ids specified by the binding to
		 * bypass the heuristic probe code, but the mtd layer
		 * provides, at present, no interface for doing so
		 * :(.
		 */
		.compatible = "jedec-flash",
		.data = "jedec_probe",
	},
	{
		.compatible = "mtd-ram",
		.data = "map_ram",
	},
	{
		.compatible = "mtd-rom",
		.data = "map_rom",
	},
	{
		.type = "rom",
		.compatible = "direct-mapped"
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, of_flash_match);

static const char * const of_default_part_probes[] = {
	"cmdlinepart", "RedBoot", "ofpart", "ofoldpart", NULL
};

static const char * const *of_get_part_probes(struct platform_device *dev)
{
	struct device_node *dp = dev->dev.of_node;
	const char **res;
	int count;

	count = of_property_count_strings(dp, "linux,part-probe");
	if (count < 0)
		return of_default_part_probes;

	res = devm_kcalloc(&dev->dev, count + 1, sizeof(*res), GFP_KERNEL);
	if (!res)
		return NULL;

	count = of_property_read_string_array(dp, "linux,part-probe", res,
					      count);
	if (count < 0)
		return NULL;

	return res;
}

static const char *of_select_probe_type(struct platform_device *dev)
{
	struct device_node *dp = dev->dev.of_node;
	const struct of_device_id *match;
	const char *probe_type;

	match = of_match_device(of_flash_match, &dev->dev);
	probe_type = match->data;
	if (probe_type)
		return probe_type;

	dev_warn(&dev->dev,
		 "Device tree uses obsolete \"direct-mapped\" flash binding\n");

	of_property_read_string(dp, "probe-type", &probe_type);
	if (!probe_type)
		return NULL;

	if (!strcmp(probe_type, "CFI")) {
		probe_type = "cfi_probe";
	} else if (!strcmp(probe_type, "JEDEC")) {
		probe_type = "jedec_probe";
	} else if (!strcmp(probe_type, "ROM")) {
		probe_type = "map_rom";
	} else {
		dev_warn(&dev->dev,
			 "obsolete_probe: don't know probe type '%s', mapping as rom\n",
			 probe_type);
		probe_type = "map_rom";
	}

	return probe_type;
}

static int physmap_flash_of_init(struct platform_device *dev)
{
	struct physmap_flash_info *info = platform_get_drvdata(dev);
	struct device_node *dp = dev->dev.of_node;
	const char *mtd_name = NULL;
	int err, swap = 0;
	bool map_indirect;
	unsigned int i;
	u32 bankwidth;

	if (!dp)
		return -EINVAL;

	info->probe_type = of_select_probe_type(dev);

	info->part_types = of_get_part_probes(dev);
	if (!info->part_types)
		return -ENOMEM;

	of_property_read_string(dp, "linux,mtd-name", &mtd_name);

	map_indirect = of_property_read_bool(dp, "no-unaligned-direct-access");

	err = of_property_read_u32(dp, "bank-width", &bankwidth);
	if (err) {
		dev_err(&dev->dev, "Can't get bank width from device tree\n");
		return err;
	}

	if (of_property_read_bool(dp, "big-endian"))
		swap = CFI_BIG_ENDIAN;
	else if (of_property_read_bool(dp, "little-endian"))
		swap = CFI_LITTLE_ENDIAN;

	for (i = 0; i < info->nmaps; i++) {
		info->maps[i].name = mtd_name;
		info->maps[i].swap = swap;
		info->maps[i].bankwidth = bankwidth;
		info->maps[i].device_node = dp;

		err = of_flash_probe_gemini(dev, dp, &info->maps[i]);
		if (err)
			return err;

		err = of_flash_probe_ixp4xx(dev, dp, &info->maps[i]);
		if (err)
			return err;

		err = of_flash_probe_versatile(dev, dp, &info->maps[i]);
		if (err)
			return err;

		/*
		 * On some platforms (e.g. MPC5200) a direct 1:1 mapping
		 * may cause problems with JFFS2 usage, as the local bus (LPB)
		 * doesn't support unaligned accesses as implemented in the
		 * JFFS2 code via memcpy(). By setting NO_XIP, the
		 * flash will not be exposed directly to the MTD users
		 * (e.g. JFFS2) any more.
		 */
		if (map_indirect)
			info->maps[i].phys = NO_XIP;
	}

	return 0;
}
#else /* IS_ENABLED(CONFIG_MTD_PHYSMAP_OF) */
#define of_flash_match NULL

static int physmap_flash_of_init(struct platform_device *dev)
{
	return -ENOTSUPP;
}
#endif /* IS_ENABLED(CONFIG_MTD_PHYSMAP_OF) */

static const char * const rom_probe_types[] = {
	"cfi_probe", "jedec_probe", "qinfo_probe", "map_rom",
};

static const char * const part_probe_types[] = {
	"cmdlinepart", "RedBoot", "afs", NULL
};

static int physmap_flash_pdata_init(struct platform_device *dev)
{
	struct physmap_flash_info *info = platform_get_drvdata(dev);
	struct physmap_flash_data *physmap_data;
	unsigned int i;
	int err;

	physmap_data = dev_get_platdata(&dev->dev);
	if (!physmap_data)
		return -EINVAL;

	info->probe_type = physmap_data->probe_type;
	info->part_types = physmap_data->part_probe_types ? : part_probe_types;
	info->parts = physmap_data->parts;
	info->nparts = physmap_data->nr_parts;

	if (physmap_data->init) {
		err = physmap_data->init(dev);
		if (err)
			return err;
	}

	for (i = 0; i < info->nmaps; i++) {
		info->maps[i].bankwidth = physmap_data->width;
		info->maps[i].pfow_base = physmap_data->pfow_base;
		info->maps[i].set_vpp = physmap_set_vpp;
	}

	return 0;
}

static int physmap_flash_probe(struct platform_device *dev)
{
	struct physmap_flash_info *info;
	int err = 0;
	int i;

	if (!dev->dev.of_node && !dev_get_platdata(&dev->dev))
		return -EINVAL;

	info = devm_kzalloc(&dev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	while (platform_get_resource(dev, IORESOURCE_MEM, info->nmaps))
		info->nmaps++;

	if (!info->nmaps)
		return -ENODEV;

	info->maps = devm_kzalloc(&dev->dev,
				  sizeof(*info->maps) * info->nmaps,
				  GFP_KERNEL);
	if (!info->maps)
		return -ENOMEM;

	info->mtds = devm_kzalloc(&dev->dev,
				  sizeof(*info->mtds) * info->nmaps,
				  GFP_KERNEL);
	if (!info->mtds)
		return -ENOMEM;

	platform_set_drvdata(dev, info);

	info->gpios = devm_gpiod_get_array_optional(&dev->dev, "addr",
						    GPIOD_OUT_LOW);
	if (IS_ERR(info->gpios))
		return PTR_ERR(info->gpios);

	if (info->gpios && info->nmaps > 1) {
		dev_err(&dev->dev, "addr-gpios only supported for nmaps == 1\n");
		return -EINVAL;
	}

	if (dev->dev.of_node)
		err = physmap_flash_of_init(dev);
	else
		err = physmap_flash_pdata_init(dev);

	if (err)
		return err;

	for (i = 0; i < info->nmaps; i++) {
		struct resource *res;

		res = platform_get_resource(dev, IORESOURCE_MEM, i);
		info->maps[i].virt = devm_ioremap_resource(&dev->dev, res);
		if (IS_ERR(info->maps[i].virt)) {
			err = PTR_ERR(info->maps[i].virt);
			goto err_out;
		}

		dev_notice(&dev->dev, "physmap platform flash device: %pR\n",
			   res);

		info->maps[i].name = dev_name(&dev->dev);

		if (!info->maps[i].phys)
			info->maps[i].phys = res->start;

		info->win_order = get_bitmask_order(resource_size(res)) - 1;
		info->maps[i].size = BIT(info->win_order +
					 (info->gpios ?
					  info->gpios->ndescs : 0));

		info->maps[i].map_priv_1 = (unsigned long)dev;

		if (info->gpios) {
			err = physmap_addr_gpios_map_init(&info->maps[i]);
			if (err)
				goto err_out;
		}

#ifdef CONFIG_MTD_COMPLEX_MAPPINGS
		/*
		 * Only use the simple_map implementation if map hooks are not
		 * implemented. Since map->read() is mandatory checking for its
		 * presence is enough.
		 */
		if (!info->maps[i].read)
			simple_map_init(&info->maps[i]);
#else
		simple_map_init(&info->maps[i]);
#endif

		if (info->probe_type) {
			info->mtds[i] = do_map_probe(info->probe_type,
						     &info->maps[i]);
		} else {
			int j;

			for (j = 0; j < ARRAY_SIZE(rom_probe_types); j++) {
				info->mtds[i] = do_map_probe(rom_probe_types[j],
							     &info->maps[i]);
				if (info->mtds[i])
					break;
			}
		}

		if (!info->mtds[i]) {
			dev_err(&dev->dev, "map_probe failed\n");
			err = -ENXIO;
			goto err_out;
		}
		info->mtds[i]->dev.parent = &dev->dev;
	}

	if (info->nmaps == 1) {
		info->cmtd = info->mtds[0];
	} else {
		/*
		 * We detected multiple devices. Concatenate them together.
		 */
		info->cmtd = mtd_concat_create(info->mtds, info->nmaps,
					       dev_name(&dev->dev));
		if (!info->cmtd)
			err = -ENXIO;
	}
	if (err)
		goto err_out;

	spin_lock_init(&info->vpp_lock);

	mtd_set_of_node(info->cmtd, dev->dev.of_node);
	err = mtd_device_parse_register(info->cmtd, info->part_types, NULL,
					info->parts, info->nparts);
	if (err)
		goto err_out;

	return 0;

err_out:
	physmap_flash_remove(dev);
	return err;
}

#ifdef CONFIG_PM
static void physmap_flash_shutdown(struct platform_device *dev)
{
	struct physmap_flash_info *info = platform_get_drvdata(dev);
	int i;

	for (i = 0; i < info->nmaps && info->mtds[i]; i++)
		if (mtd_suspend(info->mtds[i]) == 0)
			mtd_resume(info->mtds[i]);
}
#else
#define physmap_flash_shutdown NULL
#endif

static struct platform_driver physmap_flash_driver = {
	.probe		= physmap_flash_probe,
	.remove		= physmap_flash_remove,
	.shutdown	= physmap_flash_shutdown,
	.driver		= {
		.name	= "physmap-flash",
		.of_match_table = of_flash_match,
	},
};

#ifdef CONFIG_MTD_PHYSMAP_COMPAT
static struct physmap_flash_data physmap_flash_data = {
	.width		= CONFIG_MTD_PHYSMAP_BANKWIDTH,
};

static struct resource physmap_flash_resource = {
	.start		= CONFIG_MTD_PHYSMAP_START,
	.end		= CONFIG_MTD_PHYSMAP_START + CONFIG_MTD_PHYSMAP_LEN - 1,
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
#endif

static int __init physmap_init(void)
{
	int err;

	err = platform_driver_register(&physmap_flash_driver);
#ifdef CONFIG_MTD_PHYSMAP_COMPAT
	if (err == 0) {
		err = platform_device_register(&physmap_flash);
		if (err)
			platform_driver_unregister(&physmap_flash_driver);
	}
#endif

	return err;
}

static void __exit physmap_exit(void)
{
#ifdef CONFIG_MTD_PHYSMAP_COMPAT
	platform_device_unregister(&physmap_flash);
#endif
	platform_driver_unregister(&physmap_flash_driver);
}

module_init(physmap_init);
module_exit(physmap_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_AUTHOR("Vitaly Wool <vwool@ru.mvista.com>");
MODULE_AUTHOR("Mike Frysinger <vapier@gentoo.org>");
MODULE_DESCRIPTION("Generic configurable MTD map driver");

/* legacy platform drivers can't hotplug or coldplg */
#ifndef CONFIG_MTD_PHYSMAP_COMPAT
/* work with hotplug and coldplug */
MODULE_ALIAS("platform:physmap-flash");
#endif
