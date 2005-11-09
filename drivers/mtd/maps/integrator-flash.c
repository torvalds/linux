/*======================================================================

    drivers/mtd/maps/integrator-flash.c: ARM Integrator flash map driver

    Copyright (C) 2000 ARM Limited
    Copyright (C) 2003 Deep Blue Solutions Ltd.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

   This is access code for flashes using ARM's flash partitioning
   standards.

   $Id: integrator-flash.c,v 1.20 2005/11/07 11:14:27 gleixner Exp $

======================================================================*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/init.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/mach/flash.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/system.h>

#ifdef CONFIG_ARCH_P720T
#define FLASH_BASE		(0x04000000)
#define FLASH_SIZE		(64*1024*1024)
#endif

struct armflash_info {
	struct flash_platform_data *plat;
	struct resource		*res;
	struct mtd_partition	*parts;
	struct mtd_info		*mtd;
	struct map_info		map;
};

static void armflash_set_vpp(struct map_info *map, int on)
{
	struct armflash_info *info = container_of(map, struct armflash_info, map);

	if (info->plat && info->plat->set_vpp)
		info->plat->set_vpp(on);
}

static const char *probes[] = { "cmdlinepart", "RedBoot", "afs", NULL };

static int armflash_probe(struct device *_dev)
{
	struct platform_device *dev = to_platform_device(_dev);
	struct flash_platform_data *plat = dev->dev.platform_data;
	struct resource *res = dev->resource;
	unsigned int size = res->end - res->start + 1;
	struct armflash_info *info;
	int err;
	void __iomem *base;

	info = kmalloc(sizeof(struct armflash_info), GFP_KERNEL);
	if (!info) {
		err = -ENOMEM;
		goto out;
	}

	memset(info, 0, sizeof(struct armflash_info));

	info->plat = plat;
	if (plat && plat->init) {
		err = plat->init();
		if (err)
			goto no_resource;
	}

	info->res = request_mem_region(res->start, size, "armflash");
	if (!info->res) {
		err = -EBUSY;
		goto no_resource;
	}

	base = ioremap(res->start, size);
	if (!base) {
		err = -ENOMEM;
		goto no_mem;
	}

	/*
	 * look for CFI based flash parts fitted to this board
	 */
	info->map.size		= size;
	info->map.bankwidth	= plat->width;
	info->map.phys		= res->start;
	info->map.virt		= base;
	info->map.name		= dev->dev.bus_id;
	info->map.set_vpp	= armflash_set_vpp;

	simple_map_init(&info->map);

	/*
	 * Also, the CFI layer automatically works out what size
	 * of chips we have, and does the necessary identification
	 * for us automatically.
	 */
	info->mtd = do_map_probe(plat->map_name, &info->map);
	if (!info->mtd) {
		err = -ENXIO;
		goto no_device;
	}

	info->mtd->owner = THIS_MODULE;

	err = parse_mtd_partitions(info->mtd, probes, &info->parts, 0);
	if (err > 0) {
		err = add_mtd_partitions(info->mtd, info->parts, err);
		if (err)
			printk(KERN_ERR
			       "mtd partition registration failed: %d\n", err);
	}

	if (err == 0)
		dev_set_drvdata(&dev->dev, info);

	/*
	 * If we got an error, free all resources.
	 */
	if (err < 0) {
		if (info->mtd) {
			del_mtd_partitions(info->mtd);
			map_destroy(info->mtd);
		}
		kfree(info->parts);

 no_device:
		iounmap(base);
 no_mem:
		release_mem_region(res->start, size);
 no_resource:
		if (plat && plat->exit)
			plat->exit();
		kfree(info);
	}
 out:
	return err;
}

static int armflash_remove(struct device *_dev)
{
	struct platform_device *dev = to_platform_device(_dev);
	struct armflash_info *info = dev_get_drvdata(&dev->dev);

	dev_set_drvdata(&dev->dev, NULL);

	if (info) {
		if (info->mtd) {
			del_mtd_partitions(info->mtd);
			map_destroy(info->mtd);
		}
		kfree(info->parts);

		iounmap(info->map.virt);
		release_resource(info->res);
		kfree(info->res);

		if (info->plat && info->plat->exit)
			info->plat->exit();

		kfree(info);
	}

	return 0;
}

static struct device_driver armflash_driver = {
	.name		= "armflash",
	.bus		= &platform_bus_type,
	.probe		= armflash_probe,
	.remove		= armflash_remove,
};

static int __init armflash_init(void)
{
	return driver_register(&armflash_driver);
}

static void __exit armflash_exit(void)
{
	driver_unregister(&armflash_driver);
}

module_init(armflash_init);
module_exit(armflash_exit);

MODULE_AUTHOR("ARM Ltd");
MODULE_DESCRIPTION("ARM Integrator CFI map driver");
MODULE_LICENSE("GPL");
