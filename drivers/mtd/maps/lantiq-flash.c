/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2004 Liu Peng Infineon IFAP DC COM CPE
 *  Copyright (C) 2010 John Crispin <blogic@openwrt.org>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/cfi.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>

#include <lantiq_soc.h>
#include <lantiq_platform.h>

/*
 * The NOR flash is connected to the same external bus unit (EBU) as PCI.
 * To make PCI work we need to enable the endianness swapping for the address
 * written to the EBU. This endianness swapping works for PCI correctly but
 * fails for attached NOR devices. To workaround this we need to use a complex
 * map. The workaround involves swapping all addresses whilst probing the chip.
 * Once probing is complete we stop swapping the addresses but swizzle the
 * unlock addresses to ensure that access to the NOR device works correctly.
 */

enum {
	LTQ_NOR_PROBING,
	LTQ_NOR_NORMAL
};

struct ltq_mtd {
	struct resource *res;
	struct mtd_info *mtd;
	struct map_info *map;
};

static char ltq_map_name[] = "ltq_nor";

static map_word
ltq_read16(struct map_info *map, unsigned long adr)
{
	unsigned long flags;
	map_word temp;

	if (map->map_priv_1 == LTQ_NOR_PROBING)
		adr ^= 2;
	spin_lock_irqsave(&ebu_lock, flags);
	temp.x[0] = *(u16 *)(map->virt + adr);
	spin_unlock_irqrestore(&ebu_lock, flags);
	return temp;
}

static void
ltq_write16(struct map_info *map, map_word d, unsigned long adr)
{
	unsigned long flags;

	if (map->map_priv_1 == LTQ_NOR_PROBING)
		adr ^= 2;
	spin_lock_irqsave(&ebu_lock, flags);
	*(u16 *)(map->virt + adr) = d.x[0];
	spin_unlock_irqrestore(&ebu_lock, flags);
}

/*
 * The following 2 functions copy data between iomem and a cached memory
 * section. As memcpy() makes use of pre-fetching we cannot use it here.
 * The normal alternative of using memcpy_{to,from}io also makes use of
 * memcpy() on MIPS so it is not applicable either. We are therefore stuck
 * with having to use our own loop.
 */
static void
ltq_copy_from(struct map_info *map, void *to,
	unsigned long from, ssize_t len)
{
	unsigned char *f = (unsigned char *)map->virt + from;
	unsigned char *t = (unsigned char *)to;
	unsigned long flags;

	spin_lock_irqsave(&ebu_lock, flags);
	while (len--)
		*t++ = *f++;
	spin_unlock_irqrestore(&ebu_lock, flags);
}

static void
ltq_copy_to(struct map_info *map, unsigned long to,
	const void *from, ssize_t len)
{
	unsigned char *f = (unsigned char *)from;
	unsigned char *t = (unsigned char *)map->virt + to;
	unsigned long flags;

	spin_lock_irqsave(&ebu_lock, flags);
	while (len--)
		*t++ = *f++;
	spin_unlock_irqrestore(&ebu_lock, flags);
}

static const char const *part_probe_types[] = { "cmdlinepart", NULL };

static int __init
ltq_mtd_probe(struct platform_device *pdev)
{
	struct physmap_flash_data *ltq_mtd_data = dev_get_platdata(&pdev->dev);
	struct ltq_mtd *ltq_mtd;
	struct mtd_partition *parts;
	struct resource *res;
	int nr_parts = 0;
	struct cfi_private *cfi;
	int err;

	ltq_mtd = kzalloc(sizeof(struct ltq_mtd), GFP_KERNEL);
	platform_set_drvdata(pdev, ltq_mtd);

	ltq_mtd->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!ltq_mtd->res) {
		dev_err(&pdev->dev, "failed to get memory resource");
		err = -ENOENT;
		goto err_out;
	}

	res = devm_request_mem_region(&pdev->dev, ltq_mtd->res->start,
		resource_size(ltq_mtd->res), dev_name(&pdev->dev));
	if (!ltq_mtd->res) {
		dev_err(&pdev->dev, "failed to request mem resource");
		err = -EBUSY;
		goto err_out;
	}

	ltq_mtd->map = kzalloc(sizeof(struct map_info), GFP_KERNEL);
	ltq_mtd->map->phys = res->start;
	ltq_mtd->map->size = resource_size(res);
	ltq_mtd->map->virt = devm_ioremap_nocache(&pdev->dev,
				ltq_mtd->map->phys, ltq_mtd->map->size);
	if (!ltq_mtd->map->virt) {
		dev_err(&pdev->dev, "failed to ioremap!\n");
		err = -ENOMEM;
		goto err_free;
	}

	ltq_mtd->map->name = ltq_map_name;
	ltq_mtd->map->bankwidth = 2;
	ltq_mtd->map->read = ltq_read16;
	ltq_mtd->map->write = ltq_write16;
	ltq_mtd->map->copy_from = ltq_copy_from;
	ltq_mtd->map->copy_to = ltq_copy_to;

	ltq_mtd->map->map_priv_1 = LTQ_NOR_PROBING;
	ltq_mtd->mtd = do_map_probe("cfi_probe", ltq_mtd->map);
	ltq_mtd->map->map_priv_1 = LTQ_NOR_NORMAL;

	if (!ltq_mtd->mtd) {
		dev_err(&pdev->dev, "probing failed\n");
		err = -ENXIO;
		goto err_unmap;
	}

	ltq_mtd->mtd->owner = THIS_MODULE;

	cfi = ltq_mtd->map->fldrv_priv;
	cfi->addr_unlock1 ^= 1;
	cfi->addr_unlock2 ^= 1;

	nr_parts = parse_mtd_partitions(ltq_mtd->mtd,
				part_probe_types, &parts, 0);
	if (nr_parts > 0) {
		dev_info(&pdev->dev,
			"using %d partitions from cmdline", nr_parts);
	} else {
		nr_parts = ltq_mtd_data->nr_parts;
		parts = ltq_mtd_data->parts;
	}

	err = mtd_device_register(ltq_mtd->mtd, parts, nr_parts);
	if (err) {
		dev_err(&pdev->dev, "failed to add partitions\n");
		goto err_destroy;
	}

	return 0;

err_destroy:
	map_destroy(ltq_mtd->mtd);
err_unmap:
	iounmap(ltq_mtd->map->virt);
err_free:
	kfree(ltq_mtd->map);
err_out:
	kfree(ltq_mtd);
	return err;
}

static int __devexit
ltq_mtd_remove(struct platform_device *pdev)
{
	struct ltq_mtd *ltq_mtd = platform_get_drvdata(pdev);

	if (ltq_mtd) {
		if (ltq_mtd->mtd) {
			mtd_device_unregister(ltq_mtd->mtd);
			map_destroy(ltq_mtd->mtd);
		}
		if (ltq_mtd->map->virt)
			iounmap(ltq_mtd->map->virt);
		kfree(ltq_mtd->map);
		kfree(ltq_mtd);
	}
	return 0;
}

static struct platform_driver ltq_mtd_driver = {
	.remove = __devexit_p(ltq_mtd_remove),
	.driver = {
		.name = "ltq_nor",
		.owner = THIS_MODULE,
	},
};

static int __init
init_ltq_mtd(void)
{
	int ret = platform_driver_probe(&ltq_mtd_driver, ltq_mtd_probe);

	if (ret)
		pr_err("ltq_nor: error registering platform driver");
	return ret;
}

static void __exit
exit_ltq_mtd(void)
{
	platform_driver_unregister(&ltq_mtd_driver);
}

module_init(init_ltq_mtd);
module_exit(exit_ltq_mtd);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Crispin <blogic@openwrt.org>");
MODULE_DESCRIPTION("Lantiq SoC NOR");
