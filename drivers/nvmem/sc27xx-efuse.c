// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Spreadtrum Communications Inc.

#include <linux/hwspinlock.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/nvmem-provider.h>

/* PMIC global registers definition */
#define SC27XX_MODULE_EN		0xc08
#define SC2730_MODULE_EN		0x1808
#define SC27XX_EFUSE_EN			BIT(6)

/* Efuse controller registers definition */
#define SC27XX_EFUSE_GLB_CTRL		0x0
#define SC27XX_EFUSE_DATA_RD		0x4
#define SC27XX_EFUSE_DATA_WR		0x8
#define SC27XX_EFUSE_BLOCK_INDEX	0xc
#define SC27XX_EFUSE_MODE_CTRL		0x10
#define SC27XX_EFUSE_STATUS		0x14
#define SC27XX_EFUSE_WR_TIMING_CTRL	0x20
#define SC27XX_EFUSE_RD_TIMING_CTRL	0x24
#define SC27XX_EFUSE_EFUSE_DEB_CTRL	0x28

/* Mask definition for SC27XX_EFUSE_BLOCK_INDEX register */
#define SC27XX_EFUSE_BLOCK_MASK		GENMASK(4, 0)

/* Bits definitions for SC27XX_EFUSE_MODE_CTRL register */
#define SC27XX_EFUSE_PG_START		BIT(0)
#define SC27XX_EFUSE_RD_START		BIT(1)
#define SC27XX_EFUSE_CLR_RDDONE		BIT(2)

/* Bits definitions for SC27XX_EFUSE_STATUS register */
#define SC27XX_EFUSE_PGM_BUSY		BIT(0)
#define SC27XX_EFUSE_READ_BUSY		BIT(1)
#define SC27XX_EFUSE_STANDBY		BIT(2)
#define SC27XX_EFUSE_GLOBAL_PROT	BIT(3)
#define SC27XX_EFUSE_RD_DONE		BIT(4)

/* Block number and block width (bytes) definitions */
#define SC27XX_EFUSE_BLOCK_MAX		32
#define SC27XX_EFUSE_BLOCK_WIDTH	2

/* Timeout (ms) for the trylock of hardware spinlocks */
#define SC27XX_EFUSE_HWLOCK_TIMEOUT	5000

/* Timeout (us) of polling the status */
#define SC27XX_EFUSE_POLL_TIMEOUT	3000000
#define SC27XX_EFUSE_POLL_DELAY_US	10000

/*
 * Since different PMICs of SC27xx series can have different
 * address , we should save address in the device data structure.
 */
struct sc27xx_efuse_variant_data {
	u32 module_en;
};

struct sc27xx_efuse {
	struct device *dev;
	struct regmap *regmap;
	struct hwspinlock *hwlock;
	struct mutex mutex;
	u32 base;
	const struct sc27xx_efuse_variant_data *var_data;
};

static const struct sc27xx_efuse_variant_data sc2731_edata = {
	.module_en = SC27XX_MODULE_EN,
};

static const struct sc27xx_efuse_variant_data sc2730_edata = {
	.module_en = SC2730_MODULE_EN,
};

/*
 * On Spreadtrum platform, we have multi-subsystems will access the unique
 * efuse controller, so we need one hardware spinlock to synchronize between
 * the multiple subsystems.
 */
static int sc27xx_efuse_lock(struct sc27xx_efuse *efuse)
{
	int ret;

	mutex_lock(&efuse->mutex);

	ret = hwspin_lock_timeout_raw(efuse->hwlock,
				      SC27XX_EFUSE_HWLOCK_TIMEOUT);
	if (ret) {
		dev_err(efuse->dev, "timeout to get the hwspinlock\n");
		mutex_unlock(&efuse->mutex);
		return ret;
	}

	return 0;
}

static void sc27xx_efuse_unlock(struct sc27xx_efuse *efuse)
{
	hwspin_unlock_raw(efuse->hwlock);
	mutex_unlock(&efuse->mutex);
}

static int sc27xx_efuse_poll_status(struct sc27xx_efuse *efuse, u32 bits)
{
	int ret;
	u32 val;

	ret = regmap_read_poll_timeout(efuse->regmap,
				       efuse->base + SC27XX_EFUSE_STATUS,
				       val, (val & bits),
				       SC27XX_EFUSE_POLL_DELAY_US,
				       SC27XX_EFUSE_POLL_TIMEOUT);
	if (ret) {
		dev_err(efuse->dev, "timeout to update the efuse status\n");
		return ret;
	}

	return 0;
}

static int sc27xx_efuse_read(void *context, u32 offset, void *val, size_t bytes)
{
	struct sc27xx_efuse *efuse = context;
	u32 buf, blk_index = offset / SC27XX_EFUSE_BLOCK_WIDTH;
	u32 blk_offset = (offset % SC27XX_EFUSE_BLOCK_WIDTH) * BITS_PER_BYTE;
	int ret;

	if (blk_index > SC27XX_EFUSE_BLOCK_MAX ||
	    bytes > SC27XX_EFUSE_BLOCK_WIDTH)
		return -EINVAL;

	ret = sc27xx_efuse_lock(efuse);
	if (ret)
		return ret;

	/* Enable the efuse controller. */
	ret = regmap_update_bits(efuse->regmap, efuse->var_data->module_en,
				 SC27XX_EFUSE_EN, SC27XX_EFUSE_EN);
	if (ret)
		goto unlock_efuse;

	/*
	 * Before reading, we should ensure the efuse controller is in
	 * standby state.
	 */
	ret = sc27xx_efuse_poll_status(efuse, SC27XX_EFUSE_STANDBY);
	if (ret)
		goto disable_efuse;

	/* Set the block address to be read. */
	ret = regmap_write(efuse->regmap,
			   efuse->base + SC27XX_EFUSE_BLOCK_INDEX,
			   blk_index & SC27XX_EFUSE_BLOCK_MASK);
	if (ret)
		goto disable_efuse;

	/* Start reading process from efuse memory. */
	ret = regmap_update_bits(efuse->regmap,
				 efuse->base + SC27XX_EFUSE_MODE_CTRL,
				 SC27XX_EFUSE_RD_START,
				 SC27XX_EFUSE_RD_START);
	if (ret)
		goto disable_efuse;

	/*
	 * Polling the read done status to make sure the reading process
	 * is completed, that means the data can be read out now.
	 */
	ret = sc27xx_efuse_poll_status(efuse, SC27XX_EFUSE_RD_DONE);
	if (ret)
		goto disable_efuse;

	/* Read data from efuse memory. */
	ret = regmap_read(efuse->regmap, efuse->base + SC27XX_EFUSE_DATA_RD,
			  &buf);
	if (ret)
		goto disable_efuse;

	/* Clear the read done flag. */
	ret = regmap_update_bits(efuse->regmap,
				 efuse->base + SC27XX_EFUSE_MODE_CTRL,
				 SC27XX_EFUSE_CLR_RDDONE,
				 SC27XX_EFUSE_CLR_RDDONE);

disable_efuse:
	/* Disable the efuse controller after reading. */
	regmap_update_bits(efuse->regmap, efuse->var_data->module_en, SC27XX_EFUSE_EN, 0);
unlock_efuse:
	sc27xx_efuse_unlock(efuse);

	if (!ret) {
		buf >>= blk_offset;
		memcpy(val, &buf, bytes);
	}

	return ret;
}

static int sc27xx_efuse_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct nvmem_config econfig = { };
	struct nvmem_device *nvmem;
	struct sc27xx_efuse *efuse;
	int ret;

	efuse = devm_kzalloc(&pdev->dev, sizeof(*efuse), GFP_KERNEL);
	if (!efuse)
		return -ENOMEM;

	efuse->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!efuse->regmap) {
		dev_err(&pdev->dev, "failed to get efuse regmap\n");
		return -ENODEV;
	}

	ret = of_property_read_u32(np, "reg", &efuse->base);
	if (ret) {
		dev_err(&pdev->dev, "failed to get efuse base address\n");
		return ret;
	}

	ret = of_hwspin_lock_get_id(np, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get hwspinlock id\n");
		return ret;
	}

	efuse->hwlock = devm_hwspin_lock_request_specific(&pdev->dev, ret);
	if (!efuse->hwlock) {
		dev_err(&pdev->dev, "failed to request hwspinlock\n");
		return -ENXIO;
	}

	mutex_init(&efuse->mutex);
	efuse->dev = &pdev->dev;
	efuse->var_data = of_device_get_match_data(&pdev->dev);

	econfig.stride = 1;
	econfig.word_size = 1;
	econfig.read_only = true;
	econfig.name = "sc27xx-efuse";
	econfig.size = SC27XX_EFUSE_BLOCK_MAX * SC27XX_EFUSE_BLOCK_WIDTH;
	econfig.reg_read = sc27xx_efuse_read;
	econfig.priv = efuse;
	econfig.dev = &pdev->dev;
	nvmem = devm_nvmem_register(&pdev->dev, &econfig);
	if (IS_ERR(nvmem)) {
		dev_err(&pdev->dev, "failed to register nvmem config\n");
		return PTR_ERR(nvmem);
	}

	return 0;
}

static const struct of_device_id sc27xx_efuse_of_match[] = {
	{ .compatible = "sprd,sc2731-efuse", .data = &sc2731_edata},
	{ .compatible = "sprd,sc2730-efuse", .data = &sc2730_edata},
	{ }
};

static struct platform_driver sc27xx_efuse_driver = {
	.probe = sc27xx_efuse_probe,
	.driver = {
		.name = "sc27xx-efuse",
		.of_match_table = sc27xx_efuse_of_match,
	},
};

module_platform_driver(sc27xx_efuse_driver);

MODULE_AUTHOR("Freeman Liu <freeman.liu@spreadtrum.com>");
MODULE_DESCRIPTION("Spreadtrum SC27xx efuse driver");
MODULE_LICENSE("GPL v2");
