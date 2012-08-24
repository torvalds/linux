/*
 *  linux/arch/arm/mach-mmp/sram.c
 *
 *  based on mach-davinci/sram.c - DaVinci simple SRAM allocator
 *
 *  Copyright (c) 2011 Marvell Semiconductors Inc.
 *  All Rights Reserved
 *
 *  Add for mmp sram support - Leo Yan <leoy@marvell.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/genalloc.h>

#include <linux/platform_data/dma-mmp_tdma.h>

struct sram_bank_info {
	char *pool_name;
	struct gen_pool *gpool;
	int granularity;

	phys_addr_t sram_phys;
	void __iomem *sram_virt;
	u32 sram_size;

	struct list_head node;
};

static DEFINE_MUTEX(sram_lock);
static LIST_HEAD(sram_bank_list);

struct gen_pool *sram_get_gpool(char *pool_name)
{
	struct sram_bank_info *info = NULL;

	if (!pool_name)
		return NULL;

	mutex_lock(&sram_lock);

	list_for_each_entry(info, &sram_bank_list, node)
		if (!strcmp(pool_name, info->pool_name))
			break;

	mutex_unlock(&sram_lock);

	if (&info->node == &sram_bank_list)
		return NULL;

	return info->gpool;
}
EXPORT_SYMBOL(sram_get_gpool);

static int __devinit sram_probe(struct platform_device *pdev)
{
	struct sram_platdata *pdata = pdev->dev.platform_data;
	struct sram_bank_info *info;
	struct resource *res;
	int ret = 0;

	if (!pdata && !pdata->pool_name)
		return -ENODEV;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "no memory resource defined\n");
		ret = -ENODEV;
		goto out;
	}

	if (!resource_size(res))
		return 0;

	info->sram_phys   = (phys_addr_t)res->start;
	info->sram_size   = resource_size(res);
	info->sram_virt   = ioremap(info->sram_phys, info->sram_size);
	info->pool_name	  = kstrdup(pdata->pool_name, GFP_KERNEL);
	info->granularity = pdata->granularity;

	info->gpool = gen_pool_create(ilog2(info->granularity), -1);
	if (!info->gpool) {
		dev_err(&pdev->dev, "create pool failed\n");
		ret = -ENOMEM;
		goto create_pool_err;
	}

	ret = gen_pool_add_virt(info->gpool, (unsigned long)info->sram_virt,
				info->sram_phys, info->sram_size, -1);
	if (ret < 0) {
		dev_err(&pdev->dev, "add new chunk failed\n");
		ret = -ENOMEM;
		goto add_chunk_err;
	}

	mutex_lock(&sram_lock);
	list_add(&info->node, &sram_bank_list);
	mutex_unlock(&sram_lock);

	platform_set_drvdata(pdev, info);

	dev_info(&pdev->dev, "initialized\n");
	return 0;

add_chunk_err:
	gen_pool_destroy(info->gpool);
create_pool_err:
	iounmap(info->sram_virt);
	kfree(info->pool_name);
out:
	kfree(info);
	return ret;
}

static int __devexit sram_remove(struct platform_device *pdev)
{
	struct sram_bank_info *info;

	info = platform_get_drvdata(pdev);
	if (info == NULL)
		return -ENODEV;

	mutex_lock(&sram_lock);
	list_del(&info->node);
	mutex_unlock(&sram_lock);

	gen_pool_destroy(info->gpool);
	iounmap(info->sram_virt);
	kfree(info->pool_name);
	kfree(info);
	return 0;
}

static const struct platform_device_id sram_id_table[] = {
	{ "asram", MMP_ASRAM },
	{ "isram", MMP_ISRAM },
	{ }
};

static struct platform_driver sram_driver = {
	.probe		= sram_probe,
	.remove		= sram_remove,
	.driver		= {
		.name	= "mmp-sram",
	},
	.id_table	= sram_id_table,
};

static int __init sram_init(void)
{
	return platform_driver_register(&sram_driver);
}
core_initcall(sram_init);

MODULE_LICENSE("GPL");
