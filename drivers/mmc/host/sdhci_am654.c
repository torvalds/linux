// SPDX-License-Identifier: GPL-2.0
/*
 * sdhci_am654.c - SDHCI driver for TI's AM654 SOCs
 *
 * Copyright (C) 2018 Texas Instruments Incorporated - http://www.ti.com
 *
 */
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regmap.h>

#include "sdhci-pltfm.h"

/* CTL_CFG Registers */
#define CTL_CFG_2		0x14

#define SLOTTYPE_MASK		GENMASK(31, 30)
#define SLOTTYPE_EMBEDDED	BIT(30)

/* PHY Registers */
#define PHY_CTRL1	0x100
#define PHY_CTRL2	0x104
#define PHY_CTRL3	0x108
#define PHY_CTRL4	0x10C
#define PHY_CTRL5	0x110
#define PHY_CTRL6	0x114
#define PHY_STAT1	0x130
#define PHY_STAT2	0x134

#define IOMUX_ENABLE_SHIFT	31
#define IOMUX_ENABLE_MASK	BIT(IOMUX_ENABLE_SHIFT)
#define OTAPDLYENA_SHIFT	20
#define OTAPDLYENA_MASK		BIT(OTAPDLYENA_SHIFT)
#define OTAPDLYSEL_SHIFT	12
#define OTAPDLYSEL_MASK		GENMASK(15, 12)
#define STRBSEL_SHIFT		24
#define STRBSEL_4BIT_MASK	GENMASK(27, 24)
#define STRBSEL_8BIT_MASK	GENMASK(31, 24)
#define SEL50_SHIFT		8
#define SEL50_MASK		BIT(SEL50_SHIFT)
#define SEL100_SHIFT		9
#define SEL100_MASK		BIT(SEL100_SHIFT)
#define FREQSEL_SHIFT		8
#define FREQSEL_MASK		GENMASK(10, 8)
#define DLL_TRIM_ICP_SHIFT	4
#define DLL_TRIM_ICP_MASK	GENMASK(7, 4)
#define DR_TY_SHIFT		20
#define DR_TY_MASK		GENMASK(22, 20)
#define ENDLL_SHIFT		1
#define ENDLL_MASK		BIT(ENDLL_SHIFT)
#define DLLRDY_SHIFT		0
#define DLLRDY_MASK		BIT(DLLRDY_SHIFT)
#define PDB_SHIFT		0
#define PDB_MASK		BIT(PDB_SHIFT)
#define CALDONE_SHIFT		1
#define CALDONE_MASK		BIT(CALDONE_SHIFT)
#define RETRIM_SHIFT		17
#define RETRIM_MASK		BIT(RETRIM_SHIFT)

#define DRIVER_STRENGTH_50_OHM	0x0
#define DRIVER_STRENGTH_33_OHM	0x1
#define DRIVER_STRENGTH_66_OHM	0x2
#define DRIVER_STRENGTH_100_OHM	0x3
#define DRIVER_STRENGTH_40_OHM	0x4

#define CLOCK_TOO_SLOW_HZ	400000

static struct regmap_config sdhci_am654_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.fast_io = true,
};

struct sdhci_am654_data {
	struct regmap *base;
	int otap_del_sel;
	int trm_icp;
	int drv_strength;
	bool dll_on;
	int strb_sel;
	u32 flags;
};

struct sdhci_am654_driver_data {
	const struct sdhci_pltfm_data *pdata;
	u32 flags;
#define IOMUX_PRESENT	(1 << 0)
#define FREQSEL_2_BIT	(1 << 1)
#define STRBSEL_4_BIT	(1 << 2)
#define DLL_PRESENT	(1 << 3)
};

static void sdhci_am654_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_am654_data *sdhci_am654 = sdhci_pltfm_priv(pltfm_host);
	int sel50, sel100, freqsel;
	u32 mask, val;
	int ret;

	if (sdhci_am654->dll_on) {
		regmap_update_bits(sdhci_am654->base, PHY_CTRL1, ENDLL_MASK, 0);

		sdhci_am654->dll_on = false;
	}

	sdhci_set_clock(host, clock);

	if (clock > CLOCK_TOO_SLOW_HZ) {
		/* Setup DLL Output TAP delay */
		mask = OTAPDLYENA_MASK | OTAPDLYSEL_MASK;
		val = (1 << OTAPDLYENA_SHIFT) |
		      (sdhci_am654->otap_del_sel << OTAPDLYSEL_SHIFT);
		regmap_update_bits(sdhci_am654->base, PHY_CTRL4, mask, val);
		/* Write to STRBSEL for HS400 speed mode */
		if (host->mmc->ios.timing == MMC_TIMING_MMC_HS400) {
			if (sdhci_am654->flags & STRBSEL_4_BIT)
				mask = STRBSEL_4BIT_MASK;
			else
				mask = STRBSEL_8BIT_MASK;

			regmap_update_bits(sdhci_am654->base, PHY_CTRL4, mask,
					   sdhci_am654->strb_sel <<
					   STRBSEL_SHIFT);
		}

		if (sdhci_am654->flags & FREQSEL_2_BIT) {
			switch (clock) {
			case 200000000:
				sel50 = 0;
				sel100 = 0;
				break;
			case 100000000:
				sel50 = 0;
				sel100 = 1;
				break;
			default:
				sel50 = 1;
				sel100 = 0;
			}

			/* Configure PHY DLL frequency */
			mask = SEL50_MASK | SEL100_MASK;
			val = (sel50 << SEL50_SHIFT) | (sel100 << SEL100_SHIFT);
			regmap_update_bits(sdhci_am654->base, PHY_CTRL5, mask,
					   val);
		} else {
			switch (clock) {
			case 200000000:
				freqsel = 0x0;
				break;
			default:
				freqsel = 0x4;
			}

			regmap_update_bits(sdhci_am654->base, PHY_CTRL5,
					   FREQSEL_MASK,
					   freqsel << FREQSEL_SHIFT);
		}

		/* Configure DLL TRIM */
		mask = DLL_TRIM_ICP_MASK;
		val = sdhci_am654->trm_icp << DLL_TRIM_ICP_SHIFT;

		/* Configure DLL driver strength */
		mask |= DR_TY_MASK;
		val |= sdhci_am654->drv_strength << DR_TY_SHIFT;
		regmap_update_bits(sdhci_am654->base, PHY_CTRL1, mask, val);
		/* Enable DLL */
		regmap_update_bits(sdhci_am654->base, PHY_CTRL1, ENDLL_MASK,
				   0x1 << ENDLL_SHIFT);
		/*
		 * Poll for DLL ready. Use a one second timeout.
		 * Works in all experiments done so far
		 */
		ret = regmap_read_poll_timeout(sdhci_am654->base, PHY_STAT1,
					       val, val & DLLRDY_MASK, 1000,
					       1000000);
		if (ret) {
			dev_err(mmc_dev(host->mmc), "DLL failed to relock\n");
			return;
		}

		sdhci_am654->dll_on = true;
	}
}

void sdhci_j721e_4bit_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_am654_data *sdhci_am654 = sdhci_pltfm_priv(pltfm_host);
	int val, mask;

	mask = OTAPDLYENA_MASK | OTAPDLYSEL_MASK;
	val = (1 << OTAPDLYENA_SHIFT) |
	      (sdhci_am654->otap_del_sel << OTAPDLYSEL_SHIFT);
	regmap_update_bits(sdhci_am654->base, PHY_CTRL4, mask, val);

	sdhci_set_clock(host, clock);
}

static void sdhci_am654_set_power(struct sdhci_host *host, unsigned char mode,
				  unsigned short vdd)
{
	if (!IS_ERR(host->mmc->supply.vmmc)) {
		struct mmc_host *mmc = host->mmc;

		mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, vdd);
	}
	sdhci_set_power_noreg(host, mode, vdd);
}

static void sdhci_am654_write_b(struct sdhci_host *host, u8 val, int reg)
{
	unsigned char timing = host->mmc->ios.timing;

	if (reg == SDHCI_HOST_CONTROL) {
		switch (timing) {
		/*
		 * According to the data manual, HISPD bit
		 * should not be set in these speed modes.
		 */
		case MMC_TIMING_SD_HS:
		case MMC_TIMING_MMC_HS:
		case MMC_TIMING_UHS_SDR12:
		case MMC_TIMING_UHS_SDR25:
			val &= ~SDHCI_CTRL_HISPD;
		}
	}

	writeb(val, host->ioaddr + reg);
}

static struct sdhci_ops sdhci_am654_ops = {
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.get_timeout_clock = sdhci_pltfm_clk_get_max_clock,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
	.set_bus_width = sdhci_set_bus_width,
	.set_power = sdhci_am654_set_power,
	.set_clock = sdhci_am654_set_clock,
	.write_b = sdhci_am654_write_b,
	.reset = sdhci_reset,
};

static const struct sdhci_pltfm_data sdhci_am654_pdata = {
	.ops = &sdhci_am654_ops,
	.quirks = SDHCI_QUIRK_INVERTED_WRITE_PROTECT |
		  SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
};

static const struct sdhci_am654_driver_data sdhci_am654_drvdata = {
	.pdata = &sdhci_am654_pdata,
	.flags = IOMUX_PRESENT | FREQSEL_2_BIT | STRBSEL_4_BIT | DLL_PRESENT,
};

struct sdhci_ops sdhci_j721e_8bit_ops = {
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.get_timeout_clock = sdhci_pltfm_clk_get_max_clock,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
	.set_bus_width = sdhci_set_bus_width,
	.set_power = sdhci_am654_set_power,
	.set_clock = sdhci_am654_set_clock,
	.write_b = sdhci_am654_write_b,
	.reset = sdhci_reset,
};

static const struct sdhci_pltfm_data sdhci_j721e_8bit_pdata = {
	.ops = &sdhci_j721e_8bit_ops,
	.quirks = SDHCI_QUIRK_INVERTED_WRITE_PROTECT |
		  SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
};

static const struct sdhci_am654_driver_data sdhci_j721e_8bit_drvdata = {
	.pdata = &sdhci_j721e_8bit_pdata,
	.flags = DLL_PRESENT,
};

struct sdhci_ops sdhci_j721e_4bit_ops = {
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.get_timeout_clock = sdhci_pltfm_clk_get_max_clock,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
	.set_bus_width = sdhci_set_bus_width,
	.set_power = sdhci_am654_set_power,
	.set_clock = sdhci_j721e_4bit_set_clock,
	.write_b = sdhci_am654_write_b,
	.reset = sdhci_reset,
};

static const struct sdhci_pltfm_data sdhci_j721e_4bit_pdata = {
	.ops = &sdhci_j721e_4bit_ops,
	.quirks = SDHCI_QUIRK_INVERTED_WRITE_PROTECT |
		  SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
};

static const struct sdhci_am654_driver_data sdhci_j721e_4bit_drvdata = {
	.pdata = &sdhci_j721e_4bit_pdata,
	.flags = IOMUX_PRESENT,
};
static int sdhci_am654_init(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_am654_data *sdhci_am654 = sdhci_pltfm_priv(pltfm_host);
	u32 ctl_cfg_2 = 0;
	u32 mask;
	u32 val;
	int ret;

	/* Reset OTAP to default value */
	mask = OTAPDLYENA_MASK | OTAPDLYSEL_MASK;
	regmap_update_bits(sdhci_am654->base, PHY_CTRL4, mask, 0x0);

	if (sdhci_am654->flags & DLL_PRESENT) {
		regmap_read(sdhci_am654->base, PHY_STAT1, &val);
		if (~val & CALDONE_MASK) {
			/* Calibrate IO lines */
			regmap_update_bits(sdhci_am654->base, PHY_CTRL1,
					   PDB_MASK, PDB_MASK);
			ret = regmap_read_poll_timeout(sdhci_am654->base,
						       PHY_STAT1, val,
						       val & CALDONE_MASK,
						       1, 20);
			if (ret)
				return ret;
		}
	}

	/* Enable pins by setting IO mux to 0 */
	if (sdhci_am654->flags & IOMUX_PRESENT)
		regmap_update_bits(sdhci_am654->base, PHY_CTRL1,
				   IOMUX_ENABLE_MASK, 0);

	/* Set slot type based on SD or eMMC */
	if (host->mmc->caps & MMC_CAP_NONREMOVABLE)
		ctl_cfg_2 = SLOTTYPE_EMBEDDED;

	regmap_update_bits(sdhci_am654->base, CTL_CFG_2, SLOTTYPE_MASK,
			   ctl_cfg_2);

	return sdhci_add_host(host);
}

static int sdhci_am654_get_of_property(struct platform_device *pdev,
					struct sdhci_am654_data *sdhci_am654)
{
	struct device *dev = &pdev->dev;
	int drv_strength;
	int ret;

	ret = device_property_read_u32(dev, "ti,otap-del-sel",
				       &sdhci_am654->otap_del_sel);
	if (ret)
		return ret;

	if (sdhci_am654->flags & DLL_PRESENT) {
		ret = device_property_read_u32(dev, "ti,trm-icp",
					       &sdhci_am654->trm_icp);
		if (ret)
			return ret;

		ret = device_property_read_u32(dev, "ti,driver-strength-ohm",
					       &drv_strength);
		if (ret)
			return ret;

		switch (drv_strength) {
		case 50:
			sdhci_am654->drv_strength = DRIVER_STRENGTH_50_OHM;
			break;
		case 33:
			sdhci_am654->drv_strength = DRIVER_STRENGTH_33_OHM;
			break;
		case 66:
			sdhci_am654->drv_strength = DRIVER_STRENGTH_66_OHM;
			break;
		case 100:
			sdhci_am654->drv_strength = DRIVER_STRENGTH_100_OHM;
			break;
		case 40:
			sdhci_am654->drv_strength = DRIVER_STRENGTH_40_OHM;
			break;
		default:
			dev_err(dev, "Invalid driver strength\n");
			return -EINVAL;
		}
	}

	device_property_read_u32(dev, "ti,strobe-sel", &sdhci_am654->strb_sel);

	sdhci_get_of_property(pdev);

	return 0;
}

static const struct of_device_id sdhci_am654_of_match[] = {
	{
		.compatible = "ti,am654-sdhci-5.1",
		.data = &sdhci_am654_drvdata,
	},
	{
		.compatible = "ti,j721e-sdhci-8bit",
		.data = &sdhci_j721e_8bit_drvdata,
	},
	{
		.compatible = "ti,j721e-sdhci-4bit",
		.data = &sdhci_j721e_4bit_drvdata,
	},
	{ /* sentinel */ }
};

static int sdhci_am654_probe(struct platform_device *pdev)
{
	const struct sdhci_am654_driver_data *drvdata;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_am654_data *sdhci_am654;
	const struct of_device_id *match;
	struct sdhci_host *host;
	struct resource *res;
	struct clk *clk_xin;
	struct device *dev = &pdev->dev;
	void __iomem *base;
	int ret;

	match = of_match_node(sdhci_am654_of_match, pdev->dev.of_node);
	drvdata = match->data;
	host = sdhci_pltfm_init(pdev, drvdata->pdata, sizeof(*sdhci_am654));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	sdhci_am654 = sdhci_pltfm_priv(pltfm_host);
	sdhci_am654->flags = drvdata->flags;

	clk_xin = devm_clk_get(dev, "clk_xin");
	if (IS_ERR(clk_xin)) {
		dev_err(dev, "clk_xin clock not found.\n");
		ret = PTR_ERR(clk_xin);
		goto err_pltfm_free;
	}

	pltfm_host->clk = clk_xin;

	/* Clocks are enabled using pm_runtime */
	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		goto pm_runtime_disable;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		goto pm_runtime_put;
	}

	sdhci_am654->base = devm_regmap_init_mmio(dev, base,
						  &sdhci_am654_regmap_config);
	if (IS_ERR(sdhci_am654->base)) {
		dev_err(dev, "Failed to initialize regmap\n");
		ret = PTR_ERR(sdhci_am654->base);
		goto pm_runtime_put;
	}

	ret = sdhci_am654_get_of_property(pdev, sdhci_am654);
	if (ret)
		goto pm_runtime_put;

	ret = mmc_of_parse(host->mmc);
	if (ret) {
		dev_err(dev, "parsing dt failed (%d)\n", ret);
		goto pm_runtime_put;
	}

	ret = sdhci_am654_init(host);
	if (ret)
		goto pm_runtime_put;

	return 0;

pm_runtime_put:
	pm_runtime_put_sync(dev);
pm_runtime_disable:
	pm_runtime_disable(dev);
err_pltfm_free:
	sdhci_pltfm_free(pdev);
	return ret;
}

static int sdhci_am654_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	int ret;

	sdhci_remove_host(host, true);
	ret = pm_runtime_put_sync(&pdev->dev);
	if (ret < 0)
		return ret;

	pm_runtime_disable(&pdev->dev);
	sdhci_pltfm_free(pdev);

	return 0;
}

static struct platform_driver sdhci_am654_driver = {
	.driver = {
		.name = "sdhci-am654",
		.of_match_table = sdhci_am654_of_match,
	},
	.probe = sdhci_am654_probe,
	.remove = sdhci_am654_remove,
};

module_platform_driver(sdhci_am654_driver);

MODULE_DESCRIPTION("Driver for SDHCI Controller on TI's AM654 devices");
MODULE_AUTHOR("Faiz Abbas <faiz_abbas@ti.com>");
MODULE_LICENSE("GPL");
