// SPDX-License-Identifier: GPL-2.0-only
/*
 * i.MX9 OCOTP fusebox driver
 *
 * Copyright 2023 NXP
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

enum fuse_type {
	FUSE_FSB = BIT(0),
	FUSE_ELE = BIT(1),
	FUSE_ECC = BIT(2),
	FUSE_INVALID = -1
};

struct ocotp_map_entry {
	u32 start; /* start word */
	u32 num; /* num words */
	enum fuse_type type;
};

struct ocotp_devtype_data {
	u32 reg_off;
	char *name;
	u32 size;
	u32 num_entry;
	u32 flag;
	nvmem_reg_read_t reg_read;
	struct ocotp_map_entry entry[];
};

struct imx_ocotp_priv {
	struct device *dev;
	void __iomem *base;
	struct nvmem_config config;
	struct mutex lock;
	const struct ocotp_devtype_data *data;
};

static enum fuse_type imx_ocotp_fuse_type(void *context, u32 index)
{
	struct imx_ocotp_priv *priv = context;
	const struct ocotp_devtype_data *data = priv->data;
	u32 start, end;
	int i;

	for (i = 0; i < data->num_entry; i++) {
		start = data->entry[i].start;
		end = data->entry[i].start + data->entry[i].num;

		if (index >= start && index < end)
			return data->entry[i].type;
	}

	return FUSE_INVALID;
}

static int imx_ocotp_reg_read(void *context, unsigned int offset, void *val, size_t bytes)
{
	struct imx_ocotp_priv *priv = context;
	void __iomem *reg = priv->base + priv->data->reg_off;
	u32 count, index, num_bytes;
	enum fuse_type type;
	u32 *buf;
	void *p;
	int i;

	index = offset;
	num_bytes = round_up(bytes, 4);
	count = num_bytes >> 2;

	if (count > ((priv->data->size >> 2) - index))
		count = (priv->data->size >> 2) - index;

	p = kzalloc(num_bytes, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	mutex_lock(&priv->lock);

	buf = p;

	for (i = index; i < (index + count); i++) {
		type = imx_ocotp_fuse_type(context, i);
		if (type == FUSE_INVALID || type == FUSE_ELE) {
			*buf++ = 0;
			continue;
		}

		if (type & FUSE_ECC)
			*buf++ = readl_relaxed(reg + (i << 2)) & GENMASK(15, 0);
		else
			*buf++ = readl_relaxed(reg + (i << 2));
	}

	memcpy(val, (u8 *)p, bytes);

	mutex_unlock(&priv->lock);

	kfree(p);

	return 0;
};

static int imx_ele_ocotp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct imx_ocotp_priv *priv;
	struct nvmem_device *nvmem;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->data = of_device_get_match_data(dev);

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->config.dev = dev;
	priv->config.name = "ELE-OCOTP";
	priv->config.id = NVMEM_DEVID_AUTO;
	priv->config.owner = THIS_MODULE;
	priv->config.size = priv->data->size;
	priv->config.reg_read = priv->data->reg_read;
	priv->config.word_size = 4;
	priv->config.stride = 1;
	priv->config.priv = priv;
	priv->config.read_only = true;
	mutex_init(&priv->lock);

	nvmem = devm_nvmem_register(dev, &priv->config);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	return 0;
}

static const struct ocotp_devtype_data imx93_ocotp_data = {
	.reg_off = 0x8000,
	.reg_read = imx_ocotp_reg_read,
	.size = 2048,
	.num_entry = 6,
	.entry = {
		{ 0, 52, FUSE_FSB },
		{ 63, 1, FUSE_ELE},
		{ 128, 16, FUSE_ELE },
		{ 182, 1, FUSE_ELE },
		{ 188, 1, FUSE_ELE },
		{ 312, 200, FUSE_FSB }
	},
};

static const struct ocotp_devtype_data imx95_ocotp_data = {
	.reg_off = 0x8000,
	.reg_read = imx_ocotp_reg_read,
	.size = 2048,
	.num_entry = 12,
	.entry = {
		{ 0, 1, FUSE_FSB | FUSE_ECC },
		{ 7, 1, FUSE_FSB | FUSE_ECC },
		{ 9, 3, FUSE_FSB | FUSE_ECC },
		{ 12, 24, FUSE_FSB },
		{ 36, 2, FUSE_FSB  | FUSE_ECC },
		{ 38, 14, FUSE_FSB },
		{ 63, 1, FUSE_ELE },
		{ 128, 16, FUSE_ELE },
		{ 188, 1, FUSE_ELE },
		{ 317, 2, FUSE_FSB | FUSE_ECC },
		{ 320, 7, FUSE_FSB },
		{ 328, 184, FUSE_FSB }
	},
};

static const struct of_device_id imx_ele_ocotp_dt_ids[] = {
	{ .compatible = "fsl,imx93-ocotp", .data = &imx93_ocotp_data, },
	{ .compatible = "fsl,imx95-ocotp", .data = &imx95_ocotp_data, },
	{},
};
MODULE_DEVICE_TABLE(of, imx_ele_ocotp_dt_ids);

static struct platform_driver imx_ele_ocotp_driver = {
	.driver = {
		.name = "imx_ele_ocotp",
		.of_match_table = imx_ele_ocotp_dt_ids,
	},
	.probe = imx_ele_ocotp_probe,
};
module_platform_driver(imx_ele_ocotp_driver);

MODULE_DESCRIPTION("i.MX OCOTP/ELE driver");
MODULE_AUTHOR("Peng Fan <peng.fan@nxp.com>");
MODULE_LICENSE("GPL");
