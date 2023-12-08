// SPDX-License-Identifier: ISC
/* Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 *         Felix Fietkau <nbd@nbd.name>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include "mt7615.h"

int mt7622_wmac_init(struct mt7615_dev *dev)
{
	struct device_node *np = dev->mt76.dev->of_node;

	if (!is_mt7622(&dev->mt76))
		return 0;

	dev->infracfg = syscon_regmap_lookup_by_phandle(np, "mediatek,infracfg");
	if (IS_ERR(dev->infracfg)) {
		dev_err(dev->mt76.dev, "Cannot find infracfg controller\n");
		return PTR_ERR(dev->infracfg);
	}

	return 0;
}

static int mt7622_wmac_probe(struct platform_device *pdev)
{
	void __iomem *mem_base;
	int irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	mem_base = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(mem_base))
		return PTR_ERR(mem_base);

	return mt7615_mmio_probe(&pdev->dev, mem_base, irq, mt7615e_reg_map);
}

static int mt7622_wmac_remove(struct platform_device *pdev)
{
	struct mt7615_dev *dev = platform_get_drvdata(pdev);

	mt7615_unregister_device(dev);

	return 0;
}

static const struct of_device_id mt7622_wmac_of_match[] = {
	{ .compatible = "mediatek,mt7622-wmac" },
	{},
};

struct platform_driver mt7622_wmac_driver = {
	.driver = {
		.name = "mt7622-wmac",
		.of_match_table = mt7622_wmac_of_match,
	},
	.probe = mt7622_wmac_probe,
	.remove = mt7622_wmac_remove,
};

MODULE_FIRMWARE(MT7622_FIRMWARE_N9);
MODULE_FIRMWARE(MT7622_ROM_PATCH);
