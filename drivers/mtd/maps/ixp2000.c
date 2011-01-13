/*
 * drivers/mtd/maps/ixp2000.c
 *
 * Mapping for the Intel XScale IXP2000 based systems
 *
 * Copyright (C) 2002 Intel Corp.
 * Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 * Original Author: Naeem M Afzal <naeem.m.afzal@intel.com>
 * Maintainer: Deepak Saxena <dsaxena@plexity.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>
#include <mach/hardware.h>
#include <asm/mach/flash.h>

#include <linux/reboot.h>

struct ixp2000_flash_info {
	struct		mtd_info *mtd;
	struct		map_info map;
	struct		mtd_partition *partitions;
	struct		resource *res;
};

static inline unsigned long flash_bank_setup(struct map_info *map, unsigned long ofs)
{
	unsigned long (*set_bank)(unsigned long) =
		(unsigned long(*)(unsigned long))map->map_priv_2;

	return (set_bank ? set_bank(ofs) : ofs);
}

#ifdef __ARMEB__
/*
 * Rev A0 and A1 of IXP2400 silicon have a broken addressing unit which
 * causes the lower address bits to be XORed with 0x11 on 8 bit accesses
 * and XORed with 0x10 on 16 bit accesses. See the spec update, erratum 44.
 */
static int erratum44_workaround = 0;

static inline unsigned long address_fix8_write(unsigned long addr)
{
	if (erratum44_workaround) {
		return (addr ^ 3);
	}
	return addr;
}
#else

#define address_fix8_write(x)	(x)
#endif

static map_word ixp2000_flash_read8(struct map_info *map, unsigned long ofs)
{
	map_word val;

	val.x[0] =  *((u8 *)(map->map_priv_1 + flash_bank_setup(map, ofs)));
	return val;
}

/*
 * We can't use the standard memcpy due to the broken SlowPort
 * address translation on rev A0 and A1 silicon and the fact that
 * we have banked flash.
 */
static void ixp2000_flash_copy_from(struct map_info *map, void *to,
			      unsigned long from, ssize_t len)
{
	from = flash_bank_setup(map, from);
	while(len--)
		*(__u8 *) to++ = *(__u8 *)(map->map_priv_1 + from++);
}

static void ixp2000_flash_write8(struct map_info *map, map_word d, unsigned long ofs)
{
	*(__u8 *) (address_fix8_write(map->map_priv_1 +
				      flash_bank_setup(map, ofs))) = d.x[0];
}

static void ixp2000_flash_copy_to(struct map_info *map, unsigned long to,
			    const void *from, ssize_t len)
{
	to = flash_bank_setup(map, to);
	while(len--) {
		unsigned long tmp = address_fix8_write(map->map_priv_1 + to++);
		*(__u8 *)(tmp) = *(__u8 *)(from++);
	}
}


static int ixp2000_flash_remove(struct platform_device *dev)
{
	struct flash_platform_data *plat = dev->dev.platform_data;
	struct ixp2000_flash_info *info = platform_get_drvdata(dev);

	platform_set_drvdata(dev, NULL);

	if(!info)
		return 0;

	if (info->mtd) {
		del_mtd_partitions(info->mtd);
		map_destroy(info->mtd);
	}
	if (info->map.map_priv_1)
		iounmap((void *) info->map.map_priv_1);

	kfree(info->partitions);

	if (info->res) {
		release_resource(info->res);
		kfree(info->res);
	}

	if (plat->exit)
		plat->exit();

	return 0;
}


static int ixp2000_flash_probe(struct platform_device *dev)
{
	static const char *probes[] = { "RedBoot", "cmdlinepart", NULL };
	struct ixp2000_flash_data *ixp_data = dev->dev.platform_data;
	struct flash_platform_data *plat;
	struct ixp2000_flash_info *info;
	unsigned long window_size;
	int err = -1;

	if (!ixp_data)
		return -ENODEV;

	plat = ixp_data->platform_data;
	if (!plat)
		return -ENODEV;

	window_size = dev->resource->end - dev->resource->start + 1;
	dev_info(&dev->dev, "Probe of IXP2000 flash(%d banks x %dMiB)\n",
		 ixp_data->nr_banks, ((u32)window_size >> 20));

	if (plat->width != 1) {
		dev_err(&dev->dev, "IXP2000 MTD map only supports 8-bit mode, asking for %d\n",
			plat->width * 8);
		return -EIO;
	}

	info = kzalloc(sizeof(struct ixp2000_flash_info), GFP_KERNEL);
	if(!info) {
		err = -ENOMEM;
		goto Error;
	}

	platform_set_drvdata(dev, info);

	/*
	 * Tell the MTD layer we're not 1:1 mapped so that it does
	 * not attempt to do a direct access on us.
	 */
	info->map.phys = NO_XIP;

	info->map.size = ixp_data->nr_banks * window_size;
	info->map.bankwidth = 1;

	/*
 	 * map_priv_2 is used to store a ptr to the bank_setup routine
 	 */
	info->map.map_priv_2 = (unsigned long) ixp_data->bank_setup;

	info->map.name = dev_name(&dev->dev);
	info->map.read = ixp2000_flash_read8;
	info->map.write = ixp2000_flash_write8;
	info->map.copy_from = ixp2000_flash_copy_from;
	info->map.copy_to = ixp2000_flash_copy_to;

	info->res = request_mem_region(dev->resource->start,
			dev->resource->end - dev->resource->start + 1,
			dev_name(&dev->dev));
	if (!info->res) {
		dev_err(&dev->dev, "Could not reserve memory region\n");
		err = -ENOMEM;
		goto Error;
	}

	info->map.map_priv_1 = (unsigned long) ioremap(dev->resource->start,
			    	dev->resource->end - dev->resource->start + 1);
	if (!info->map.map_priv_1) {
		dev_err(&dev->dev, "Failed to ioremap flash region\n");
		err = -EIO;
		goto Error;
	}

#if defined(__ARMEB__)
	/*
	 * Enable erratum 44 workaround for NPUs with broken slowport
	 */

	erratum44_workaround = ixp2000_has_broken_slowport();
	dev_info(&dev->dev, "Erratum 44 workaround %s\n",
	       erratum44_workaround ? "enabled" : "disabled");
#endif

	info->mtd = do_map_probe(plat->map_name, &info->map);
	if (!info->mtd) {
		dev_err(&dev->dev, "map_probe failed\n");
		err = -ENXIO;
		goto Error;
	}
	info->mtd->owner = THIS_MODULE;

	err = parse_mtd_partitions(info->mtd, probes, &info->partitions, 0);
	if (err > 0) {
		err = add_mtd_partitions(info->mtd, info->partitions, err);
		if(err)
			dev_err(&dev->dev, "Could not parse partitions\n");
	}

	if (err)
		goto Error;

	return 0;

Error:
	ixp2000_flash_remove(dev);
	return err;
}

static struct platform_driver ixp2000_flash_driver = {
	.probe		= ixp2000_flash_probe,
	.remove		= ixp2000_flash_remove,
	.driver		= {
		.name	= "IXP2000-Flash",
		.owner	= THIS_MODULE,
	},
};

static int __init ixp2000_flash_init(void)
{
	return platform_driver_register(&ixp2000_flash_driver);
}

static void __exit ixp2000_flash_exit(void)
{
	platform_driver_unregister(&ixp2000_flash_driver);
}

module_init(ixp2000_flash_init);
module_exit(ixp2000_flash_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Deepak Saxena <dsaxena@plexity.net>");
MODULE_ALIAS("platform:IXP2000-Flash");
