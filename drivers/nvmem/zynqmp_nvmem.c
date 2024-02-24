// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Xilinx, Inc.
 * Copyright (C) 2022 - 2023, Advanced Micro Devices, Inc.
 */

#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/firmware/xlnx-zynqmp.h>

#define SILICON_REVISION_MASK 0xF


static int zynqmp_nvmem_read(void *context, unsigned int offset,
			     void *val, size_t bytes)
{
	struct device *dev = context;
	int ret;
	int idcode;
	int version;

	ret = zynqmp_pm_get_chipid(&idcode, &version);
	if (ret < 0)
		return ret;

	dev_dbg(dev, "Read chipid val %x %x\n", idcode, version);
	*(int *)val = version & SILICON_REVISION_MASK;

	return 0;
}

static const struct of_device_id zynqmp_nvmem_match[] = {
	{ .compatible = "xlnx,zynqmp-nvmem-fw", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, zynqmp_nvmem_match);

static int zynqmp_nvmem_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nvmem_config econfig = {};

	econfig.name = "zynqmp-nvmem";
	econfig.owner = THIS_MODULE;
	econfig.word_size = 1;
	econfig.size = 1;
	econfig.dev = dev;
	econfig.add_legacy_fixed_of_cells = true;
	econfig.read_only = true;
	econfig.reg_read = zynqmp_nvmem_read;

	return PTR_ERR_OR_ZERO(devm_nvmem_register(dev, &econfig));
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
