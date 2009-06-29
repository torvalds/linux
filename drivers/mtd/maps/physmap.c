/*
 * Normal mappings of chips in physical memory
 *
 * Copyright (C) 2003 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * 031022 - [jsun] add run-time configure and partition setup
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
#include <linux/io.h>

#define MAX_RESOURCES		4

struct physmap_flash_info {
	struct mtd_info		*mtd[MAX_RESOURCES];
	struct mtd_info		*cmtd;
	struct map_info		map[MAX_RESOURCES];
#ifdef CONFIG_MTD_PARTITIONS
	int			nr_parts;
	struct mtd_partition	*parts;
#endif
};

static int physmap_flash_remove(struct platform_device *dev)
{
	struct physmap_flash_info *info;
	struct physmap_flash_data *physmap_data;
	int i;

	info = platform_get_drvdata(dev);
	if (info == NULL)
		return 0;
	platform_set_drvdata(dev, NULL);

	physmap_data = dev->dev.platform_data;

	if (info->cmtd) {
#ifdef CONFIG_MTD_PARTITIONS
		if (info->nr_parts || physmap_data->nr_parts)
			del_mtd_partitions(info->cmtd);
		else
			del_mtd_device(info->cmtd);
#else
		del_mtd_device(info->cmtd);
#endif
	}
#ifdef CONFIG_MTD_PARTITIONS
	if (info->nr_parts)
		kfree(info->parts);
#endif

#ifdef CONFIG_MTD_CONCAT
	if (info->cmtd != info->mtd[0])
		mtd_concat_destroy(info->cmtd);
#endif

	for (i = 0; i < MAX_RESOURCES; i++) {
		if (info->mtd[i] != NULL)
			map_destroy(info->mtd[i]);
	}
	return 0;
}

static const char *rom_probe_types[] = {
					"cfi_probe",
					"jedec_probe",
					"qinfo_probe",
					"map_rom",
					NULL };
#ifdef CONFIG_MTD_PARTITIONS
static const char *part_probe_types[] = { "cmdlinepart", "RedBoot", NULL };
#endif

static int physmap_flash_probe(struct platform_device *dev)
{
	struct physmap_flash_data *physmap_data;
	struct physmap_flash_info *info;
	const char **probe_type;
	int err = 0;
	int i;
	int devices_found = 0;

	physmap_data = dev->dev.platform_data;
	if (physmap_data == NULL)
		return -ENODEV;

	info = devm_kzalloc(&dev->dev, sizeof(struct physmap_flash_info),
			    GFP_KERNEL);
	if (info == NULL) {
		err = -ENOMEM;
		goto err_out;
	}

	platform_set_drvdata(dev, info);

	for (i = 0; i < dev->num_resources; i++) {
		printk(KERN_NOTICE "physmap platform flash device: %.8llx at %.8llx\n",
		       (unsigned long long)(dev->resource[i].end - dev->resource[i].start + 1),
		       (unsigned long long)dev->resource[i].start);

		if (!devm_request_mem_region(&dev->dev,
			dev->resource[i].start,
			dev->resource[i].end - dev->resource[i].start + 1,
			dev_name(&dev->dev))) {
			dev_err(&dev->dev, "Could not reserve memory region\n");
			err = -ENOMEM;
			goto err_out;
		}

		info->map[i].name = dev_name(&dev->dev);
		info->map[i].phys = dev->resource[i].start;
		info->map[i].size = dev->resource[i].end - dev->resource[i].start + 1;
		info->map[i].bankwidth = physmap_data->width;
		info->map[i].set_vpp = physmap_data->set_vpp;
		info->map[i].pfow_base = physmap_data->pfow_base;

		info->map[i].virt = devm_ioremap(&dev->dev, info->map[i].phys,
						 info->map[i].size);
		if (info->map[i].virt == NULL) {
			dev_err(&dev->dev, "Failed to ioremap flash region\n");
			err = EIO;
			goto err_out;
		}

		simple_map_init(&info->map[i]);

		probe_type = rom_probe_types;
		for (; info->mtd[i] == NULL && *probe_type != NULL; probe_type++)
			info->mtd[i] = do_map_probe(*probe_type, &info->map[i]);
		if (info->mtd[i] == NULL) {
			dev_err(&dev->dev, "map_probe failed\n");
			err = -ENXIO;
			goto err_out;
		} else {
			devices_found++;
		}
		info->mtd[i]->owner = THIS_MODULE;
		info->mtd[i]->dev.parent = &dev->dev;
	}

	if (devices_found == 1) {
		info->cmtd = info->mtd[0];
	} else if (devices_found > 1) {
		/*
		 * We detected multiple devices. Concatenate them together.
		 */
#ifdef CONFIG_MTD_CONCAT
		info->cmtd = mtd_concat_create(info->mtd, devices_found, dev_name(&dev->dev));
		if (info->cmtd == NULL)
			err = -ENXIO;
#else
		printk(KERN_ERR "physmap-flash: multiple devices "
		       "found but MTD concat support disabled.\n");
		err = -ENXIO;
#endif
	}
	if (err)
		goto err_out;

#ifdef CONFIG_MTD_PARTITIONS
	err = parse_mtd_partitions(info->cmtd, part_probe_types,
				&info->parts, 0);
	if (err > 0) {
		add_mtd_partitions(info->cmtd, info->parts, err);
		info->nr_parts = err;
		return 0;
	}

	if (physmap_data->nr_parts) {
		printk(KERN_NOTICE "Using physmap partition information\n");
		add_mtd_partitions(info->cmtd, physmap_data->parts,
				   physmap_data->nr_parts);
		return 0;
	}
#endif

	add_mtd_device(info->cmtd);
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

	for (i = 0; i < MAX_RESOURCES && info->mtd[i]; i++)
		if (info->mtd[i]->suspend && info->mtd[i]->resume)
			if (info->mtd[i]->suspend(info->mtd[i]) == 0)
				info->mtd[i]->resume(info->mtd[i]);
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
		.owner	= THIS_MODULE,
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

void physmap_configure(unsigned long addr, unsigned long size,
		int bankwidth, void (*set_vpp)(struct map_info *, int))
{
	physmap_flash_resource.start = addr;
	physmap_flash_resource.end = addr + size - 1;
	physmap_flash_data.width = bankwidth;
	physmap_flash_data.set_vpp = set_vpp;
}

#ifdef CONFIG_MTD_PARTITIONS
void physmap_set_partitions(struct mtd_partition *parts, int num_parts)
{
	physmap_flash_data.nr_parts = num_parts;
	physmap_flash_data.parts = parts;
}
#endif
#endif

static int __init physmap_init(void)
{
	int err;

	err = platform_driver_register(&physmap_flash_driver);
#ifdef CONFIG_MTD_PHYSMAP_COMPAT
	if (err == 0)
		platform_device_register(&physmap_flash);
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
MODULE_DESCRIPTION("Generic configurable MTD map driver");

/* legacy platform drivers can't hotplug or coldplg */
#ifndef CONFIG_MTD_PHYSMAP_COMPAT
/* work with hotplug and coldplug */
MODULE_ALIAS("platform:physmap-flash");
#endif
