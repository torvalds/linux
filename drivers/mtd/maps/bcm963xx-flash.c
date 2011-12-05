/*
 * Copyright © 2006-2008  Florian Fainelli <florian@openwrt.org>
 *			  Mike Albon <malbon@openwrt.org>
 * Copyright © 2009-2010  Daniel Dickinson <openwrt@cshore.neomailbox.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mtd/map.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#define BCM63XX_BUSWIDTH	2		/* Buswidth */

static struct mtd_info *bcm963xx_mtd_info;

static struct map_info bcm963xx_map = {
	.name		= "bcm963xx",
	.bankwidth	= BCM63XX_BUSWIDTH,
};

static const char *part_types[] = { "bcm63xxpart", NULL };

static int bcm963xx_probe(struct platform_device *pdev)
{
	int err = 0;
	struct resource *r;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "no resource supplied\n");
		return -ENODEV;
	}

	bcm963xx_map.phys = r->start;
	bcm963xx_map.size = resource_size(r);
	bcm963xx_map.virt = ioremap(r->start, resource_size(r));
	if (!bcm963xx_map.virt) {
		dev_err(&pdev->dev, "failed to ioremap\n");
		return -EIO;
	}

	dev_info(&pdev->dev, "0x%08lx at 0x%08x\n",
					bcm963xx_map.size, bcm963xx_map.phys);

	simple_map_init(&bcm963xx_map);

	bcm963xx_mtd_info = do_map_probe("cfi_probe", &bcm963xx_map);
	if (!bcm963xx_mtd_info) {
		dev_err(&pdev->dev, "failed to probe using CFI\n");
		bcm963xx_mtd_info = do_map_probe("jedec_probe", &bcm963xx_map);
		if (bcm963xx_mtd_info)
			goto probe_ok;
		dev_err(&pdev->dev, "failed to probe using JEDEC\n");
		err = -EIO;
		goto err_probe;
	}

probe_ok:
	bcm963xx_mtd_info->owner = THIS_MODULE;

	return mtd_device_parse_register(bcm963xx_mtd_info, part_types, NULL,
					 NULL, 0);
err_probe:
	iounmap(bcm963xx_map.virt);
	return err;
}

static int bcm963xx_remove(struct platform_device *pdev)
{
	if (bcm963xx_mtd_info) {
		mtd_device_unregister(bcm963xx_mtd_info);
		map_destroy(bcm963xx_mtd_info);
	}

	if (bcm963xx_map.virt) {
		iounmap(bcm963xx_map.virt);
		bcm963xx_map.virt = 0;
	}

	return 0;
}

static struct platform_driver bcm63xx_mtd_dev = {
	.probe	= bcm963xx_probe,
	.remove = bcm963xx_remove,
	.driver = {
		.name	= "bcm963xx-flash",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(bcm63xx_mtd_dev);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Broadcom BCM63xx MTD driver for CFE and RedBoot");
MODULE_AUTHOR("Daniel Dickinson <openwrt@cshore.neomailbox.net>");
MODULE_AUTHOR("Florian Fainelli <florian@openwrt.org>");
MODULE_AUTHOR("Mike Albon <malbon@openwrt.org>");
