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

======================================================================*/

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/io.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/concat.h>

#include <asm/mach/flash.h>
#include <mach/hardware.h>
#include <asm/system.h>

struct armflash_subdev_info {
	char			*name;
	struct mtd_info		*mtd;
	struct map_info		map;
	struct flash_platform_data *plat;
};

struct armflash_info {
	struct resource		*res;
	struct mtd_partition	*parts;
	struct mtd_info		*mtd;
	int			nr_subdev;
	struct armflash_subdev_info subdev[0];
};

static void armflash_set_vpp(struct map_info *map, int on)
{
	struct armflash_subdev_info *info =
		container_of(map, struct armflash_subdev_info, map);

	if (info->plat && info->plat->set_vpp)
		info->plat->set_vpp(on);
}

static const char *probes[] = { "cmdlinepart", "RedBoot", "afs", NULL };

static int armflash_subdev_probe(struct armflash_subdev_info *subdev,
				 struct resource *res)
{
	struct flash_platform_data *plat = subdev->plat;
	resource_size_t size = res->end - res->start + 1;
	void __iomem *base;
	int err = 0;

	if (!request_mem_region(res->start, size, subdev->name)) {
		err = -EBUSY;
		goto out;
	}

	base = ioremap(res->start, size);
	if (!base) {
		err = -ENOMEM;
		goto no_mem;
	}

	/*
	 * look for CFI based flash parts fitted to this board
	 */
	subdev->map.size	= size;
	subdev->map.bankwidth	= plat->width;
	subdev->map.phys	= res->start;
	subdev->map.virt	= base;
	subdev->map.name	= subdev->name;
	subdev->map.set_vpp	= armflash_set_vpp;

	simple_map_init(&subdev->map);

	/*
	 * Also, the CFI layer automatically works out what size
	 * of chips we have, and does the necessary identification
	 * for us automatically.
	 */
	subdev->mtd = do_map_probe(plat->map_name, &subdev->map);
	if (!subdev->mtd) {
		err = -ENXIO;
		goto no_device;
	}

	subdev->mtd->owner = THIS_MODULE;

	/* Successful? */
	if (err == 0)
		return err;

	if (subdev->mtd)
		map_destroy(subdev->mtd);
 no_device:
	iounmap(base);
 no_mem:
	release_mem_region(res->start, size);
 out:
	return err;
}

static void armflash_subdev_remove(struct armflash_subdev_info *subdev)
{
	if (subdev->mtd)
		map_destroy(subdev->mtd);
	if (subdev->map.virt)
		iounmap(subdev->map.virt);
	kfree(subdev->name);
	subdev->name = NULL;
	release_mem_region(subdev->map.phys, subdev->map.size);
}

static int armflash_probe(struct platform_device *dev)
{
	struct flash_platform_data *plat = dev->dev.platform_data;
	unsigned int size;
	struct armflash_info *info;
	int i, nr, err;

	/* Count the number of devices */
	for (nr = 0; ; nr++)
		if (!platform_get_resource(dev, IORESOURCE_MEM, nr))
			break;
	if (nr == 0) {
		err = -ENODEV;
		goto out;
	}

	size = sizeof(struct armflash_info) +
		sizeof(struct armflash_subdev_info) * nr;
	info = kzalloc(size, GFP_KERNEL);
	if (!info) {
		err = -ENOMEM;
		goto out;
	}

	if (plat && plat->init) {
		err = plat->init();
		if (err)
			goto no_resource;
	}

	for (i = 0; i < nr; i++) {
		struct armflash_subdev_info *subdev = &info->subdev[i];
		struct resource *res;

		res = platform_get_resource(dev, IORESOURCE_MEM, i);
		if (!res)
			break;

		if (nr == 1)
			/* No MTD concatenation, just use the default name */
			subdev->name = kstrdup(dev_name(&dev->dev), GFP_KERNEL);
		else
			subdev->name = kasprintf(GFP_KERNEL, "%s-%d",
						 dev_name(&dev->dev), i);
		if (!subdev->name) {
			err = -ENOMEM;
			break;
		}
		subdev->plat = plat;

		err = armflash_subdev_probe(subdev, res);
		if (err) {
			kfree(subdev->name);
			subdev->name = NULL;
			break;
		}
	}
	info->nr_subdev = i;

	if (err)
		goto subdev_err;

	if (info->nr_subdev == 1)
		info->mtd = info->subdev[0].mtd;
	else if (info->nr_subdev > 1) {
#ifdef CONFIG_MTD_CONCAT
		struct mtd_info *cdev[info->nr_subdev];

		/*
		 * We detected multiple devices.  Concatenate them together.
		 */
		for (i = 0; i < info->nr_subdev; i++)
			cdev[i] = info->subdev[i].mtd;

		info->mtd = mtd_concat_create(cdev, info->nr_subdev,
					      dev_name(&dev->dev));
		if (info->mtd == NULL)
			err = -ENXIO;
#else
		printk(KERN_ERR "armflash: multiple devices found but "
		       "MTD concat support disabled.\n");
		err = -ENXIO;
#endif
	}

	if (err < 0)
		goto cleanup;

	err = parse_mtd_partitions(info->mtd, probes, &info->parts, 0);
	if (err > 0) {
		err = add_mtd_partitions(info->mtd, info->parts, err);
		if (err)
			printk(KERN_ERR
			       "mtd partition registration failed: %d\n", err);
	}

	if (err == 0) {
		platform_set_drvdata(dev, info);
		return err;
	}

	/*
	 * We got an error, free all resources.
	 */
 cleanup:
	if (info->mtd) {
		del_mtd_partitions(info->mtd);
#ifdef CONFIG_MTD_CONCAT
		if (info->mtd != info->subdev[0].mtd)
			mtd_concat_destroy(info->mtd);
#endif
	}
	kfree(info->parts);
 subdev_err:
	for (i = info->nr_subdev - 1; i >= 0; i--)
		armflash_subdev_remove(&info->subdev[i]);
 no_resource:
	if (plat && plat->exit)
		plat->exit();
	kfree(info);
 out:
	return err;
}

static int armflash_remove(struct platform_device *dev)
{
	struct armflash_info *info = platform_get_drvdata(dev);
	struct flash_platform_data *plat = dev->dev.platform_data;
	int i;

	platform_set_drvdata(dev, NULL);

	if (info) {
		if (info->mtd) {
			del_mtd_partitions(info->mtd);
#ifdef CONFIG_MTD_CONCAT
			if (info->mtd != info->subdev[0].mtd)
				mtd_concat_destroy(info->mtd);
#endif
		}
		kfree(info->parts);

		for (i = info->nr_subdev - 1; i >= 0; i--)
			armflash_subdev_remove(&info->subdev[i]);

		if (plat && plat->exit)
			plat->exit();

		kfree(info);
	}

	return 0;
}

static struct platform_driver armflash_driver = {
	.probe		= armflash_probe,
	.remove		= armflash_remove,
	.driver		= {
		.name	= "armflash",
		.owner	= THIS_MODULE,
	},
};

static int __init armflash_init(void)
{
	return platform_driver_register(&armflash_driver);
}

static void __exit armflash_exit(void)
{
	platform_driver_unregister(&armflash_driver);
}

module_init(armflash_init);
module_exit(armflash_exit);

MODULE_AUTHOR("ARM Ltd");
MODULE_DESCRIPTION("ARM Integrator CFI map driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:armflash");
