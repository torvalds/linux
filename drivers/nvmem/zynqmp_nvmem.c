// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Xilinx, Inc.
 */

#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/firmware/xlnx-zynqmp.h>

#define SILICON_REVISION_MASK 0xF

struct zynqmp_nvmem_data {
	struct device *dev;
	struct nvmem_device *nvmem;
};

static int zynqmp_nvmem_read(void *context, unsigned int offset,
			     void *val, size_t bytes)
{
	int ret;
	int idcode, version;
	struct zynqmp_nvmem_data *priv = context;

	ret = zynqmp_pm_get_chipid(&idcode, &version);
	if (ret < 0)
		return ret;

	dev_dbg(priv->dev, "Read chipid val %x %x\n", idcode, version);
	*(int *)val = version & SILICON_REVISION_MASK;

	return 0;
}

static struct nvmem_config econfig = {
	.name = "zynqmp-nvmem",
	.owner = THIS_MODULE,
	.word_size = 1,
	.size = 1,
	.read_only = true,
};

static const struct of_device_id zynqmp_nvmem_match[] = {
	{ .compatible = "xlnx,zynqmp-nvmem-fw", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, zynqmp_nvmem_match);

static int zynqmp_nvmem_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct zynqmp_nvmem_data *priv;

	priv = devm_kzalloc(dev, sizeof(struct zynqmp_nvmem_data), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	econfig.dev = dev;
	econfig.reg_read = zynqmp_nvmem_read;
	econfig.priv = priv;

	priv->nvmem = devm_nvmem_register(dev, &econfig);

	return PTR_ERR_OR_ZERO(priv->nvmem);
}

static struct platform_driver zynqmp_nvmem_driver = {
	.probe = zynqmp_nvmem_probe,
	.driver = {
		.name = "zynqmp-nvmem",
		.of_match_table = zynqmp_nvmem_match,
	},
};

module_platform_driver(zynqmp_nvmem_driver);

MODULE_AUTHOR("Michal Simek <michal.simek@amd.com>, Nava kishore Manne <nava.kishore.manne@amd.com>");
MODULE_DESCRIPTION("ZynqMP NVMEM driver");
MODULE_LICENSE("GPL");
