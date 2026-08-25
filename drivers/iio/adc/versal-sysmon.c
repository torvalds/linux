// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Versal SysMon MMIO platform driver
 *
 * Copyright (C) 2019 - 2022, Xilinx, Inc.
 * Copyright (C) 2022 - 2026, Advanced Micro Devices, Inc.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include "versal-sysmon.h"

struct sysmon_mmio {
	void __iomem *base;
};

static int sysmon_mmio_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct sysmon_mmio *mmio = context;

	*val = readl(mmio->base + reg);
	return 0;
}

static int sysmon_mmio_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct sysmon_mmio *mmio = context;

	/* NPI must be unlocked before any register write except to NPI_LOCK */
	if (reg != SYSMON_NPI_LOCK)
		writel(SYSMON_NPI_UNLOCK_CODE, mmio->base + SYSMON_NPI_LOCK);
	writel(val, mmio->base + reg);

	return 0;
}

static const struct regmap_config sysmon_mmio_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = SYSMON_REG_STRIDE,
	.max_register = SYSMON_MAX_REG,
	.reg_read = sysmon_mmio_reg_read,
	.reg_write = sysmon_mmio_reg_write,
	.fast_io = true,
};

static int sysmon_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sysmon_mmio *mmio;
	struct regmap *regmap;

	mmio = devm_kzalloc(dev, sizeof(*mmio), GFP_KERNEL);
	if (!mmio)
		return -ENOMEM;

	mmio->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mmio->base))
		return PTR_ERR(mmio->base);

	regmap = devm_regmap_init(dev, NULL, mmio, &sysmon_mmio_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return devm_versal_sysmon_core_probe(dev, regmap);
}

static const struct of_device_id sysmon_of_match_table[] = {
	{ .compatible = "xlnx,versal-sysmon" },
	{ }
};
MODULE_DEVICE_TABLE(of, sysmon_of_match_table);

static struct platform_driver sysmon_platform_driver = {
	.probe = sysmon_platform_probe,
	.driver = {
		.name = "versal-sysmon",
		.of_match_table = sysmon_of_match_table,
	},
};
module_platform_driver(sysmon_platform_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AMD Versal SysMon Platform Driver");
MODULE_IMPORT_NS("VERSAL_SYSMON");
MODULE_AUTHOR("Salih Erim <salih.erim@amd.com>");
