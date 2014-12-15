/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2004 Liu Peng Infineon IFAP DC COM CPE
 *  Copyright (C) 2010 John Crispin <blogic@openwrt.org>
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/cfi.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/of.h>

#include <lantiq_soc.h>

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

static const char ltq_map_name[] = "ltq_nor";
static const char * const ltq_probe_types[] = { "cmdlinepart", "ofpart", NULL };

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

static int
ltq_mtd_probe(struct platform_device *pdev)
{
	struct mtd_part_parser_data ppdata;
	struct ltq_mtd *ltq_mtd;
	struct cfi_private *cfi;
	int err;

	if (of_machine_is_compatible("lantiq,falcon") &&
			(ltq_boot_select() != BS_FLASH)) {
		dev_err(&pdev->dev, "invalid bootstrap options\n");
		return -ENODEV;
	}

	ltq_mtd = devm_kzalloc(&pdev->dev, sizeof(struct ltq_mtd), GFP_KERNEL);
	if (!ltq_mtd)
		return -ENOMEM;

	platform_set_drvdata(pdev, ltq_mtd);

	ltq_mtd->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!ltq_mtd->res) {
		dev_err(&pdev->dev, "failed to get memory resource\n");
		return -ENOENT;
	}

	ltq_mtd->map = devm_kzalloc(&pdev->dev, sizeof(struct map_info),
				    GFP_KERNEL);
	if (!ltq_mtd->map)
		return -ENOMEM;

	ltq_mtd->map->phys = ltq_mtd->res->start;
	ltq_mtd->map->size = resource_size(ltq_mtd->res);
	ltq_mtd->map->virt = devm_ioremap_resource(&pdev->dev, ltq_mtd->res);
	if (IS_ERR(ltq_mtd->map->virt))
		return PTR_ERR(ltq_mtd->map->virt);

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
		return -ENXIO;
	}

	ltq_mtd->mtd->owner = THIS_MODULE;

	cfi = ltq_mtd->map->fldrv_priv;
	cfi->addr_unlock1 ^= 1;
	cfi->addr_unlock2 ^= 1;

	ppdata.of_node = pdev->dev.of_node;
	err = mtd_device_parse_register(ltq_mtd->mtd, ltq_probe_types,
					&ppdata, NULL, 0);
	if (err) {
		dev_err(&pdev->dev, "failed to add partitions\n");
		goto err_destroy;
	}

	return 0;

err_destroy:
	map_destroy(ltq_mtd->mtd);
	return err;
}

static int
ltq_mtd_remove(struct platform_device *pdev)
{
	struct ltq_mtd *ltq_mtd = platform_get_drvdata(pdev);

	if (ltq_mtd && ltq_mtd->mtd) {
		mtd_device_unregister(ltq_mtd->mtd);
		map_destroy(ltq_mtd->mtd);
	}
	return 0;
}

static const struct of_device_id ltq_mtd_match[] = {
	{ .compatible = "lantiq,nor" },
	{},
};
MODULE_DEVICE_TABLE(of, ltq_mtd_match);

static struct platform_driver ltq_mtd_driver = {
	.probe = ltq_mtd_probe,
	.remove = ltq_mtd_remove,
	.driver = {
		.name = "ltq-nor",
		.of_match_table = ltq_mtd_match,
	},
};

module_platform_driver(ltq_mtd_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Crispin <blogic@openwrt.org>");
MODULE_DESCRIPTION("Lantiq SoC NOR");
