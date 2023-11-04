// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 MediaTek Inc.
 * Author: Andrew-CT Chen <andrew-ct.chen@mediatek.com>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/io.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>
#include <linux/property.h>

struct mtk_efuse_pdata {
	bool uses_post_processing;
};

struct mtk_efuse_priv {
	void __iomem *base;
};

static int mtk_reg_read(void *context,
			unsigned int reg, void *_val, size_t bytes)
{
	struct mtk_efuse_priv *priv = context;
	void __iomem *addr = priv->base + reg;
	u8 *val = _val;
	int i;

	for (i = 0; i < bytes; i++, val++)
		*val = readb(addr + i);

	return 0;
}

static int mtk_efuse_gpu_speedbin_pp(void *context, const char *id, int index,
				     unsigned int offset, void *data, size_t bytes)
{
	u8 *val = data;

	if (val[0] < 8)
		val[0] = BIT(val[0]);

	return 0;
}

static void mtk_efuse_fixup_cell_info(struct nvmem_device *nvmem,
				      struct nvmem_layout *layout,
				      struct nvmem_cell_info *cell)
{
	size_t sz = strlen(cell->name);

	/*
	 * On some SoCs, the GPU speedbin is not read as bitmask but as
	 * a number with range [0-7] (max 3 bits): post process to use
	 * it in OPP tables to describe supported-hw.
	 */
	if (cell->nbits <= 3 &&
	    strncmp(cell->name, "gpu-speedbin", min(sz, strlen("gpu-speedbin"))) == 0)
		cell->read_post_process = mtk_efuse_gpu_speedbin_pp;
}

static struct nvmem_layout mtk_efuse_layout = {
	.fixup_cell_info = mtk_efuse_fixup_cell_info,
};

static int mtk_efuse_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct nvmem_device *nvmem;
	struct nvmem_config econfig = {};
	struct mtk_efuse_priv *priv;
	const struct mtk_efuse_pdata *pdata;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	pdata = device_get_match_data(dev);
	econfig.add_legacy_fixed_of_cells = true;
	econfig.stride = 1;
	econfig.word_size = 1;
	econfig.reg_read = mtk_reg_read;
	econfig.size = resource_size(res);
	econfig.priv = priv;
	econfig.dev = dev;
	if (pdata->uses_post_processing)
		econfig.layout = &mtk_efuse_layout;
	nvmem = devm_nvmem_register(dev, &econfig);

	return PTR_ERR_OR_ZERO(nvmem);
}

static const struct mtk_efuse_pdata mtk_mt8186_efuse_pdata = {
	.uses_post_processing = true,
};

static const struct mtk_efuse_pdata mtk_efuse_pdata = {
	.uses_post_processing = false,
};

static const struct of_device_id mtk_efuse_of_match[] = {
	{ .compatible = "mediatek,mt8173-efuse", .data = &mtk_efuse_pdata },
	{ .compatible = "mediatek,mt8186-efuse", .data = &mtk_mt8186_efuse_pdata },
	{ .compatible = "mediatek,efuse", .data = &mtk_efuse_pdata },
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, mtk_efuse_of_match);

static struct platform_driver mtk_efuse_driver = {
	.probe = mtk_efuse_probe,
	.driver = {
		.name = "mediatek,efuse",
		.of_match_table = mtk_efuse_of_match,
	},
};

static int __init mtk_efuse_init(void)
{
	int ret;

	ret = platform_driver_register(&mtk_efuse_driver);
	if (ret) {
		pr_err("Failed to register efuse driver\n");
		return ret;
	}

	return 0;
}

static void __exit mtk_efuse_exit(void)
{
	return platform_driver_unregister(&mtk_efuse_driver);
}

subsys_initcall(mtk_efuse_init);
module_exit(mtk_efuse_exit);

MODULE_AUTHOR("Andrew-CT Chen <andrew-ct.chen@mediatek.com>");
MODULE_DESCRIPTION("Mediatek EFUSE driver");
MODULE_LICENSE("GPL v2");
