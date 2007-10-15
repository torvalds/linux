/*
 * Map driver for Intel XScale PXA2xx platforms.
 *
 * Author:	Nicolas Pitre
 * Copyright:	(C) 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/cacheflush.h>

#include <asm/mach/flash.h>

static void pxa2xx_map_inval_cache(struct map_info *map, unsigned long from,
				      ssize_t len)
{
	flush_ioremap_region(map->phys, map->cached, from, len);
}

struct pxa2xx_flash_info {
	struct mtd_partition	*parts;
	int			nr_parts;
	struct mtd_info		*mtd;
	struct map_info		map;
};


static const char *probes[] = { "RedBoot", "cmdlinepart", NULL };


static int __init pxa2xx_flash_probe(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct flash_platform_data *flash = pdev->dev.platform_data;
	struct pxa2xx_flash_info *info;
	struct mtd_partition *parts;
	struct resource *res;
	int ret = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	info = kmalloc(sizeof(struct pxa2xx_flash_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	memset(info, 0, sizeof(struct pxa2xx_flash_info));
	info->map.name = (char *) flash->name;
	info->map.bankwidth = flash->width;
	info->map.phys = res->start;
	info->map.size = res->end - res->start + 1;
	info->parts = flash->parts;
	info->nr_parts = flash->nr_parts;

	info->map.virt = ioremap(info->map.phys, info->map.size);
	if (!info->map.virt) {
		printk(KERN_WARNING "Failed to ioremap %s\n",
		       info->map.name);
		return -ENOMEM;
	}
	info->map.cached =
		ioremap_cached(info->map.phys, info->map.size);
	if (!info->map.cached)
		printk(KERN_WARNING "Failed to ioremap cached %s\n",
		       info->map.name);
	info->map.inval_cache = pxa2xx_map_inval_cache;
	simple_map_init(&info->map);

	printk(KERN_NOTICE
	       "Probing %s at physical address 0x%08lx"
	       " (%d-bit bankwidth)\n",
	       info->map.name, (unsigned long)info->map.phys,
	       info->map.bankwidth * 8);

	info->mtd = do_map_probe(flash->map_name, &info->map);

	if (!info->mtd) {
		iounmap((void *)info->map.virt);
		if (info->map.cached)
			iounmap(info->map.cached);
		return -EIO;
	}
	info->mtd->owner = THIS_MODULE;

#ifdef CONFIG_MTD_PARTITIONS
	ret = parse_mtd_partitions(info->mtd, probes, &parts, 0);

	if (ret > 0) {
		info->nr_parts = ret;
		info->parts = parts;
	}
#endif

	if (info->nr_parts) {
		add_mtd_partitions(info->mtd, info->parts,
				   info->nr_parts);
	} else {
		printk("Registering %s as whole device\n",
		       info->map.name);
		add_mtd_device(info->mtd);
	}

	dev_set_drvdata(dev, info);
	return 0;
}

static int __exit pxa2xx_flash_remove(struct device *dev)
{
	struct pxa2xx_flash_info *info = dev_get_drvdata(dev);

	dev_set_drvdata(dev, NULL);

#ifdef CONFIG_MTD_PARTITIONS
	if (info->nr_parts)
		del_mtd_partitions(info->mtd);
	else
#endif
		del_mtd_device(info->mtd);

	map_destroy(info->mtd);
	iounmap(info->map.virt);
	if (info->map.cached)
		iounmap(info->map.cached);
	kfree(info->parts);
	kfree(info);
	return 0;
}

#ifdef CONFIG_PM
static int pxa2xx_flash_suspend(struct device *dev, pm_message_t state)
{
	struct pxa2xx_flash_info *info = dev_get_drvdata(dev);
	int ret = 0;

	if (info->mtd && info->mtd->suspend)
		ret = info->mtd->suspend(info->mtd);
	return ret;
}

static int pxa2xx_flash_resume(struct device *dev)
{
	struct pxa2xx_flash_info *info = dev_get_drvdata(dev);

	if (info->mtd && info->mtd->resume)
		info->mtd->resume(info->mtd);
	return 0;
}
static void pxa2xx_flash_shutdown(struct device *dev)
{
	struct pxa2xx_flash_info *info = dev_get_drvdata(dev);

	if (info && info->mtd->suspend(info->mtd) == 0)
		info->mtd->resume(info->mtd);
}
#else
#define pxa2xx_flash_suspend NULL
#define pxa2xx_flash_resume NULL
#define pxa2xx_flash_shutdown NULL
#endif

static struct device_driver pxa2xx_flash_driver = {
	.name		= "pxa2xx-flash",
	.bus		= &platform_bus_type,
	.probe		= pxa2xx_flash_probe,
	.remove		= __exit_p(pxa2xx_flash_remove),
	.suspend	= pxa2xx_flash_suspend,
	.resume		= pxa2xx_flash_resume,
	.shutdown	= pxa2xx_flash_shutdown,
};

static int __init init_pxa2xx_flash(void)
{
	return driver_register(&pxa2xx_flash_driver);
}

static void __exit cleanup_pxa2xx_flash(void)
{
	driver_unregister(&pxa2xx_flash_driver);
}

module_init(init_pxa2xx_flash);
module_exit(cleanup_pxa2xx_flash);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nicolas Pitre <nico@cam.org>");
MODULE_DESCRIPTION("MTD map driver for Intel XScale PXA2xx");
