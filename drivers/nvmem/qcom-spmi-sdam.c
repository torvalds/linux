// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017, 2020-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define SDAM_MEM_START			0x40
#define REGISTER_MAP_ID			0x40
#define REGISTER_MAP_VERSION		0x41
#define SDAM_SIZE			0x44
#define SDAM_PBS_TRIG_SET		0xE5
#define SDAM_PBS_TRIG_CLR		0xE6

struct sdam_chip {
	struct regmap			*regmap;
	struct nvmem_config		sdam_config;
	unsigned int			base;
	unsigned int			size;
};

/* read only register offsets */
static const u8 sdam_ro_map[] = {
	REGISTER_MAP_ID,
	REGISTER_MAP_VERSION,
	SDAM_SIZE
};

static bool sdam_is_valid(struct sdam_chip *sdam, unsigned int offset,
				size_t len)
{
	unsigned int sdam_mem_end = SDAM_MEM_START + sdam->size - 1;

	if (!len)
		return false;

	if (offset >= SDAM_MEM_START && offset <= sdam_mem_end
				&& (offset + len - 1) <= sdam_mem_end)
		return true;
	else if ((offset == SDAM_PBS_TRIG_SET || offset == SDAM_PBS_TRIG_CLR)
				&& (len == 1))
		return true;

	return false;
}

static bool sdam_is_ro(unsigned int offset, size_t len)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sdam_ro_map); i++)
		if (offset <= sdam_ro_map[i] && (offset + len) > sdam_ro_map[i])
			return true;

	return false;
}

static int sdam_read(void *priv, unsigned int offset, void *val,
				size_t bytes)
{
	struct sdam_chip *sdam = priv;
	struct device *dev = sdam->sdam_config.dev;
	int rc;

	if (!sdam_is_valid(sdam, offset, bytes)) {
		dev_err(dev, "Invalid SDAM offset %#x len=%zd\n",
			offset, bytes);
		return -EINVAL;
	}

	rc = regmap_bulk_read(sdam->regmap, sdam->base + offset, val, bytes);
	if (rc < 0)
		dev_err(dev, "Failed to read SDAM offset %#x len=%zd, rc=%d\n",
						offset, bytes, rc);

	return rc;
}

static int sdam_write(void *priv, unsigned int offset, void *val,
				size_t bytes)
{
	struct sdam_chip *sdam = priv;
	struct device *dev = sdam->sdam_config.dev;
	int rc;

	if (!sdam_is_valid(sdam, offset, bytes)) {
		dev_err(dev, "Invalid SDAM offset %#x len=%zd\n",
			offset, bytes);
		return -EINVAL;
	}

	if (sdam_is_ro(offset, bytes)) {
		dev_err(dev, "Invalid write offset %#x len=%zd\n",
			offset, bytes);
		return -EINVAL;
	}

	rc = regmap_bulk_write(sdam->regmap, sdam->base + offset, val, bytes);
	if (rc < 0)
		dev_err(dev, "Failed to write SDAM offset %#x len=%zd, rc=%d\n",
						offset, bytes, rc);

	return rc;
}

static int sdam_probe(struct platform_device *pdev)
{
	struct sdam_chip *sdam;
	struct nvmem_device *nvmem;
	unsigned int val;
	int rc;

	sdam = devm_kzalloc(&pdev->dev, sizeof(*sdam), GFP_KERNEL);
	if (!sdam)
		return -ENOMEM;

	sdam->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!sdam->regmap) {
		dev_err(&pdev->dev, "Failed to get regmap handle\n");
		return -ENXIO;
	}

	rc = of_property_read_u32(pdev->dev.of_node, "reg", &sdam->base);
	if (rc < 0) {
		dev_err(&pdev->dev, "Failed to get SDAM base, rc=%d\n", rc);
		return -EINVAL;
	}

	rc = regmap_read(sdam->regmap, sdam->base + SDAM_SIZE, &val);
	if (rc < 0) {
		dev_err(&pdev->dev, "Failed to read SDAM_SIZE rc=%d\n", rc);
		return -EINVAL;
	}
	sdam->size = val * 32;

	sdam->sdam_config.dev = &pdev->dev;
	sdam->sdam_config.name = "spmi_sdam";
	sdam->sdam_config.id = NVMEM_DEVID_AUTO;
	sdam->sdam_config.owner = THIS_MODULE;
	sdam->sdam_config.stride = 1;
	sdam->sdam_config.word_size = 1;
	sdam->sdam_config.reg_read = sdam_read;
	sdam->sdam_config.reg_write = sdam_write;
	sdam->sdam_config.priv = sdam;

	nvmem = devm_nvmem_register(&pdev->dev, &sdam->sdam_config);
	if (IS_ERR(nvmem)) {
		dev_err(&pdev->dev,
			"Failed to register SDAM nvmem device rc=%ld\n",
			PTR_ERR(nvmem));
		return -ENXIO;
	}
	dev_dbg(&pdev->dev,
		"SDAM base=%#x size=%u registered successfully\n",
		sdam->base, sdam->size);

	return 0;
}

static const struct of_device_id sdam_match_table[] = {
	{ .compatible = "qcom,spmi-sdam" },
	{},
};
MODULE_DEVICE_TABLE(of, sdam_match_table);

static struct platform_driver sdam_driver = {
	.driver = {
		.name = "qcom,spmi-sdam",
		.of_match_table = sdam_match_table,
	},
	.probe		= sdam_probe,
};
module_platform_driver(sdam_driver);

MODULE_DESCRIPTION("QCOM SPMI SDAM driver");
MODULE_LICENSE("GPL v2");
