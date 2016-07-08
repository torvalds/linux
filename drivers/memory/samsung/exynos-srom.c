/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *	      http://www.samsung.com/
 *
 * EXYNOS - SROM Controller support
 * Author: Pankaj Dubey <pankaj.dubey@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "exynos-srom.h"

static const unsigned long exynos_srom_offsets[] = {
	/* SROM side */
	EXYNOS_SROM_BW,
	EXYNOS_SROM_BC0,
	EXYNOS_SROM_BC1,
	EXYNOS_SROM_BC2,
	EXYNOS_SROM_BC3,
};

/**
 * struct exynos_srom_reg_dump: register dump of SROM Controller registers.
 * @offset: srom register offset from the controller base address.
 * @value: the value of register under the offset.
 */
struct exynos_srom_reg_dump {
	u32     offset;
	u32     value;
};

/**
 * struct exynos_srom: platform data for exynos srom controller driver.
 * @dev: platform device pointer
 * @reg_base: srom base address
 * @reg_offset: exynos_srom_reg_dump pointer to hold offset and its value.
 */
struct exynos_srom {
	struct device *dev;
	void __iomem *reg_base;
	struct exynos_srom_reg_dump *reg_offset;
};

static struct exynos_srom_reg_dump *exynos_srom_alloc_reg_dump(
		const unsigned long *rdump,
		unsigned long nr_rdump)
{
	struct exynos_srom_reg_dump *rd;
	unsigned int i;

	rd = kcalloc(nr_rdump, sizeof(*rd), GFP_KERNEL);
	if (!rd)
		return NULL;

	for (i = 0; i < nr_rdump; ++i)
		rd[i].offset = rdump[i];

	return rd;
}

static int exynos_srom_configure_bank(struct exynos_srom *srom,
				      struct device_node *np)
{
	u32 bank, width, pmc = 0;
	u32 timing[6];
	u32 cs, bw;

	if (of_property_read_u32(np, "reg", &bank))
		return -EINVAL;
	if (of_property_read_u32(np, "reg-io-width", &width))
		width = 1;
	if (of_property_read_bool(np, "samsung,srom-page-mode"))
		pmc = 1 << EXYNOS_SROM_BCX__PMC__SHIFT;
	if (of_property_read_u32_array(np, "samsung,srom-timing", timing,
				       ARRAY_SIZE(timing)))
		return -EINVAL;

	bank *= 4; /* Convert bank into shift/offset */

	cs = 1 << EXYNOS_SROM_BW__BYTEENABLE__SHIFT;
	if (width == 2)
		cs |= 1 << EXYNOS_SROM_BW__DATAWIDTH__SHIFT;

	bw = __raw_readl(srom->reg_base + EXYNOS_SROM_BW);
	bw = (bw & ~(EXYNOS_SROM_BW__CS_MASK << bank)) | (cs << bank);
	__raw_writel(bw, srom->reg_base + EXYNOS_SROM_BW);

	__raw_writel(pmc | (timing[0] << EXYNOS_SROM_BCX__TACP__SHIFT) |
		    (timing[1] << EXYNOS_SROM_BCX__TCAH__SHIFT) |
		    (timing[2] << EXYNOS_SROM_BCX__TCOH__SHIFT) |
		    (timing[3] << EXYNOS_SROM_BCX__TACC__SHIFT) |
		    (timing[4] << EXYNOS_SROM_BCX__TCOS__SHIFT) |
		    (timing[5] << EXYNOS_SROM_BCX__TACS__SHIFT),
		    srom->reg_base + EXYNOS_SROM_BC0 + bank);

	return 0;
}

static int exynos_srom_probe(struct platform_device *pdev)
{
	struct device_node *np, *child;
	struct exynos_srom *srom;
	struct device *dev = &pdev->dev;
	bool bad_bank_config = false;

	np = dev->of_node;
	if (!np) {
		dev_err(&pdev->dev, "could not find device info\n");
		return -EINVAL;
	}

	srom = devm_kzalloc(&pdev->dev,
			sizeof(struct exynos_srom), GFP_KERNEL);
	if (!srom)
		return -ENOMEM;

	srom->dev = dev;
	srom->reg_base = of_iomap(np, 0);
	if (!srom->reg_base) {
		dev_err(&pdev->dev, "iomap of exynos srom controller failed\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, srom);

	srom->reg_offset = exynos_srom_alloc_reg_dump(exynos_srom_offsets,
			sizeof(exynos_srom_offsets));
	if (!srom->reg_offset) {
		iounmap(srom->reg_base);
		return -ENOMEM;
	}

	for_each_child_of_node(np, child) {
		if (exynos_srom_configure_bank(srom, child)) {
			dev_err(dev,
				"Could not decode bank configuration for %s\n",
				child->name);
			bad_bank_config = true;
		}
	}

	/*
	 * If any bank failed to configure, we still provide suspend/resume,
	 * but do not probe child devices
	 */
	if (bad_bank_config)
		return 0;

	return of_platform_populate(np, NULL, NULL, dev);
}

static int exynos_srom_remove(struct platform_device *pdev)
{
	struct exynos_srom *srom = platform_get_drvdata(pdev);

	kfree(srom->reg_offset);
	iounmap(srom->reg_base);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static void exynos_srom_save(void __iomem *base,
				    struct exynos_srom_reg_dump *rd,
				    unsigned int num_regs)
{
	for (; num_regs > 0; --num_regs, ++rd)
		rd->value = readl(base + rd->offset);
}

static void exynos_srom_restore(void __iomem *base,
				      const struct exynos_srom_reg_dump *rd,
				      unsigned int num_regs)
{
	for (; num_regs > 0; --num_regs, ++rd)
		writel(rd->value, base + rd->offset);
}

static int exynos_srom_suspend(struct device *dev)
{
	struct exynos_srom *srom = dev_get_drvdata(dev);

	exynos_srom_save(srom->reg_base, srom->reg_offset,
				ARRAY_SIZE(exynos_srom_offsets));
	return 0;
}

static int exynos_srom_resume(struct device *dev)
{
	struct exynos_srom *srom = dev_get_drvdata(dev);

	exynos_srom_restore(srom->reg_base, srom->reg_offset,
				ARRAY_SIZE(exynos_srom_offsets));
	return 0;
}
#endif

static const struct of_device_id of_exynos_srom_ids[] = {
	{
		.compatible	= "samsung,exynos4210-srom",
	},
	{},
};
MODULE_DEVICE_TABLE(of, of_exynos_srom_ids);

static SIMPLE_DEV_PM_OPS(exynos_srom_pm_ops, exynos_srom_suspend, exynos_srom_resume);

static struct platform_driver exynos_srom_driver = {
	.probe = exynos_srom_probe,
	.remove = exynos_srom_remove,
	.driver = {
		.name = "exynos-srom",
		.of_match_table = of_exynos_srom_ids,
		.pm = &exynos_srom_pm_ops,
	},
};
module_platform_driver(exynos_srom_driver);

MODULE_AUTHOR("Pankaj Dubey <pankaj.dubey@samsung.com>");
MODULE_DESCRIPTION("Exynos SROM Controller Driver");
MODULE_LICENSE("GPL");
