// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2023-2025 NXP
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

struct s32g_ocotp_priv {
	struct device *dev;
	void __iomem *base;
};

static int s32g_ocotp_read(void *context, unsigned int offset,
			    void *val, size_t bytes)
{
	struct s32g_ocotp_priv *s32g_data = context;
	u32 *dst = val;

	while (bytes >= sizeof(u32)) {
		*dst++ = ioread32(s32g_data->base + offset);

		bytes -= sizeof(u32);
		offset += sizeof(u32);
	}

	return 0;
}

static struct nvmem_keepout s32g_keepouts[] = {
	{ .start = 0,   .end = 520 },
	{ .start = 540, .end = 564 },
	{ .start = 596, .end = 664 },
	{ .start = 668, .end = 676 },
	{ .start = 684, .end = 732 },
	{ .start = 744, .end = 864 },
	{ .start = 908, .end = 924 },
	{ .start = 928, .end = 936 },
	{ .start = 948, .end = 964 },
	{ .start = 968, .end = 976 },
	{ .start = 984, .end = 1012 },
};

static struct nvmem_config s32g_ocotp_nvmem_config = {
	.name = "s32g-ocotp",
	.add_legacy_fixed_of_cells = true,
	.read_only = true,
	.word_size = 4,
	.reg_read = s32g_ocotp_read,
	.keepout = s32g_keepouts,
	.nkeepout = ARRAY_SIZE(s32g_keepouts),
};

static const struct of_device_id ocotp_of_match[] = {
	{ .compatible = "nxp,s32g2-ocotp" },
	{ /* sentinel */ }
};

static int s32g_ocotp_probe(struct platform_device *pdev)
{
	struct s32g_ocotp_priv *s32g_data;
	struct device *dev = &pdev->dev;
	struct nvmem_device *nvmem;
	struct resource *res;

	s32g_data = devm_kzalloc(dev, sizeof(*s32g_data), GFP_KERNEL);
	if (!s32g_data)
		return -ENOMEM;

	s32g_data->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(s32g_data->base))
		return dev_err_probe(dev, PTR_ERR(s32g_data->base),
				     "Cannot map OCOTP device.\n");

	s32g_data->dev = dev;
	s32g_ocotp_nvmem_config.dev = dev;
	s32g_ocotp_nvmem_config.priv = s32g_data;
	s32g_ocotp_nvmem_config.size = resource_size(res);

	nvmem = devm_nvmem_register(dev, &s32g_ocotp_nvmem_config);

	return PTR_ERR_OR_ZERO(nvmem);
}

static struct platform_driver s32g_ocotp_driver = {
	.probe = s32g_ocotp_probe,
	.driver = {
		.name = "s32g-ocotp",
		.of_match_table = ocotp_of_match,
	},
};
module_platform_driver(s32g_ocotp_driver);
MODULE_AUTHOR("NXP");
MODULE_DESCRIPTION("S32G OCOTP driver");
MODULE_LICENSE("GPL");
