// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/mod_devicetable.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

/**
 * struct sec_qfprom - structure holding secure qfprom attributes
 *
 * @base: starting physical address for secure qfprom corrected address space.
 * @dev: qfprom device structure.
 */
struct sec_qfprom {
	phys_addr_t base;
	struct device *dev;
};

static int sec_qfprom_reg_read(void *context, unsigned int reg, void *_val, size_t bytes)
{
	struct sec_qfprom *priv = context;
	unsigned int i;
	u8 *val = _val;
	u32 read_val;
	u8 *tmp;

	for (i = 0; i < bytes; i++, reg++) {
		if (i == 0 || reg % 4 == 0) {
			if (qcom_scm_io_readl(priv->base + (reg & ~3), &read_val)) {
				dev_err(priv->dev, "Couldn't access fuse register\n");
				return -EINVAL;
			}
			tmp = (u8 *)&read_val;
		}

		val[i] = tmp[reg & 3];
	}

	return 0;
}

static int sec_qfprom_probe(struct platform_device *pdev)
{
	struct nvmem_config econfig = {
		.name = "sec-qfprom",
		.add_legacy_fixed_of_cells = true,
		.stride = 1,
		.word_size = 1,
		.id = NVMEM_DEVID_AUTO,
		.reg_read = sec_qfprom_reg_read,
	};
	struct device *dev = &pdev->dev;
	struct nvmem_device *nvmem;
	struct sec_qfprom *priv;
	struct resource *res;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	priv->base = res->start;

	econfig.size = resource_size(res);
	econfig.dev = dev;
	econfig.priv = priv;

	priv->dev = dev;

	nvmem = devm_nvmem_register(dev, &econfig);

	return PTR_ERR_OR_ZERO(nvmem);
}

static const struct of_device_id sec_qfprom_of_match[] = {
	{ .compatible = "qcom,sec-qfprom" },
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, sec_qfprom_of_match);

static struct platform_driver qfprom_driver = {
	.probe = sec_qfprom_probe,
	.driver = {
		.name = "qcom_sec_qfprom",
		.of_match_table = sec_qfprom_of_match,
	},
};
module_platform_driver(qfprom_driver);
MODULE_DESCRIPTION("Qualcomm Secure QFPROM driver");
MODULE_LICENSE("GPL");
