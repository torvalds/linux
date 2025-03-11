// SPDX-License-Identifier: GPL-2.0-only
/*
 * Renesas R-Car E-FUSE/OTP Driver
 *
 * Copyright (C) 2024 Glider bv
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>

struct rcar_fuse {
	struct nvmem_keepout keepouts[2];
	struct nvmem_device *nvmem;
	struct device *dev;
	void __iomem *base;
};

struct rcar_fuse_data {
	unsigned int bank;	/* 0: PFC + E-FUSE, 1: OPT_MEM + E-FUSE */
	unsigned int start;	/* inclusive */
	unsigned int end;	/* exclusive */
};

static int rcar_fuse_reg_read(void *priv, unsigned int offset, void *val,
			      size_t bytes)
{
	struct rcar_fuse *fuse = priv;
	int ret;

	ret = pm_runtime_resume_and_get(fuse->dev);
	if (ret < 0)
		return ret;

	__ioread32_copy(val, fuse->base + offset, bytes / 4);

	pm_runtime_put(fuse->dev);

	return 0;
}

static int rcar_fuse_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct rcar_fuse_data *data = device_get_match_data(dev);
	struct nvmem_config config = {
		.dev = dev,
		.name = "rcar-fuse",
		.id = NVMEM_DEVID_NONE,
		.owner = THIS_MODULE,
		.type = NVMEM_TYPE_OTP,
		.read_only = true,
		.root_only = true,
		.reg_read = rcar_fuse_reg_read,
		.word_size = 4,
		.stride = 4,
	};
	struct rcar_fuse *fuse;
	struct resource *res;
	int ret;

	ret = devm_pm_runtime_enable(dev);
	if (ret < 0)
		return ret;

	fuse = devm_kzalloc(dev, sizeof(*fuse), GFP_KERNEL);
	if (!fuse)
		return -ENOMEM;

	fuse->base = devm_platform_get_and_ioremap_resource(pdev, data->bank,
							    &res);
	if (IS_ERR(fuse->base))
		return PTR_ERR(fuse->base);

	fuse->dev = dev;
	fuse->keepouts[0].start = 0;
	fuse->keepouts[0].end = data->start;
	fuse->keepouts[1].start = data->end;
	fuse->keepouts[1].end = resource_size(res);

	config.keepout = fuse->keepouts;
	config.nkeepout = ARRAY_SIZE(fuse->keepouts);
	config.size = resource_size(res);
	config.priv = fuse;

	fuse->nvmem = devm_nvmem_register(dev, &config);
	if (IS_ERR(fuse->nvmem))
		return dev_err_probe(dev, PTR_ERR(fuse->nvmem),
				     "Failed to register NVMEM device\n");

	return 0;
}

static const struct rcar_fuse_data rcar_fuse_v3u = {
	.bank = 0,
	.start = 0x0c0,
	.end = 0x0e8,
};

static const struct rcar_fuse_data rcar_fuse_s4 = {
	.bank = 0,
	.start = 0x0c0,
	.end = 0x14c,
};

static const struct rcar_fuse_data rcar_fuse_v4h = {
	.bank = 1,
	.start = 0x100,
	.end = 0x1a0,
};

static const struct rcar_fuse_data rcar_fuse_v4m = {
	.bank = 1,
	.start = 0x100,
	.end = 0x110,
};

static const struct of_device_id rcar_fuse_match[] = {
	{ .compatible = "renesas,r8a779a0-efuse", .data = &rcar_fuse_v3u },
	{ .compatible = "renesas,r8a779f0-efuse", .data = &rcar_fuse_s4 },
	{ .compatible = "renesas,r8a779g0-otp", .data = &rcar_fuse_v4h },
	{ .compatible = "renesas,r8a779h0-otp", .data = &rcar_fuse_v4m },
	{ /* sentinel */ }
};

static struct platform_driver rcar_fuse_driver = {
	.probe = rcar_fuse_probe,
	.driver = {
		.name = "rcar_fuse",
		.of_match_table = rcar_fuse_match,
	},
};
module_platform_driver(rcar_fuse_driver);

MODULE_DESCRIPTION("Renesas R-Car E-FUSE/OTP driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Geert Uytterhoeven");
