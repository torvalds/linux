/*
 * Arasan Secure Digital Host Controller Interface.
 * Copyright (C) 2011 - 2012 Michal Simek <monstr@monstr.eu>
 * Copyright (c) 2012 Wind River Systems, Inc.
 * Copyright (C) 2013 Pengutronix e.K.
 * Copyright (C) 2013 Xilinx Inc.
 *
 * Based on sdhci-of-esdhc.c
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 * Copyright (c) 2009 MontaVista Software, Inc.
 *
 * Authors: Xiaobo Xie <X.Xie@freescale.com>
 *	    Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include "sdhci-pltfm.h"

#define SDHCI_ARASAN_CLK_CTRL_OFFSET	0x2c
#define SDHCI_ARASAN_VENDOR_REGISTER	0x78

#define VENDOR_ENHANCED_STROBE		BIT(0)
#define CLK_CTRL_TIMEOUT_SHIFT		16
#define CLK_CTRL_TIMEOUT_MASK		(0xf << CLK_CTRL_TIMEOUT_SHIFT)
#define CLK_CTRL_TIMEOUT_MIN_EXP	13

/*
 * On some SoCs the syscon area has a feature where the upper 16-bits of
 * each 32-bit register act as a write mask for the lower 16-bits.  This allows
 * atomic updates of the register without locking.  This macro is used on SoCs
 * that have that feature.
 */
#define HIWORD_UPDATE(val, mask, shift) \
		((val) << (shift) | (mask) << ((shift) + 16))

/**
 * struct sdhci_arasan_soc_ctl_field - Field used in sdhci_arasan_soc_ctl_map
 *
 * @reg:	Offset within the syscon of the register containing this field
 * @width:	Number of bits for this field
 * @shift:	Bit offset within @reg of this field (or -1 if not avail)
 */
struct sdhci_arasan_soc_ctl_field {
	u32 reg;
	u16 width;
	s16 shift;
};

/**
 * struct sdhci_arasan_soc_ctl_map - Map in syscon to corecfg registers
 *
 * It's up to the licensee of the Arsan IP block to make these available
 * somewhere if needed.  Presumably these will be scattered somewhere that's
 * accessible via the syscon API.
 *
 * @baseclkfreq:	Where to find corecfg_baseclkfreq
 * @hiword_update:	If true, use HIWORD_UPDATE to access the syscon
 */
struct sdhci_arasan_soc_ctl_map {
	struct sdhci_arasan_soc_ctl_field	baseclkfreq;
	bool					hiword_update;
};

/**
 * struct sdhci_arasan_data
 * @host:		Pointer to the main SDHCI host structure.
 * @clk_ahb:		Pointer to the AHB clock
 * @phy:		Pointer to the generic phy
 * @sdcardclk_hw:	Struct for the clock we might provide to a PHY.
 * @sdcardclk:		Pointer to normal 'struct clock' for sdcardclk_hw.
 * @soc_ctl_base:	Pointer to regmap for syscon for soc_ctl registers.
 * @soc_ctl_map:	Map to get offsets into soc_ctl registers.
 */
struct sdhci_arasan_data {
	struct sdhci_host *host;
	struct clk	*clk_ahb;
	struct phy	*phy;

	struct clk_hw	sdcardclk_hw;
	struct clk      *sdcardclk;

	struct regmap	*soc_ctl_base;
	const struct sdhci_arasan_soc_ctl_map *soc_ctl_map;
};

static const struct sdhci_arasan_soc_ctl_map rk3399_soc_ctl_map = {
	.baseclkfreq = { .reg = 0xf000, .width = 8, .shift = 8 },
	.hiword_update = true,
};

/**
 * sdhci_arasan_syscon_write - Write to a field in soc_ctl registers
 *
 * This function allows writing to fields in sdhci_arasan_soc_ctl_map.
 * Note that if a field is specified as not available (shift < 0) then
 * this function will silently return an error code.  It will be noisy
 * and print errors for any other (unexpected) errors.
 *
 * @host:	The sdhci_host
 * @fld:	The field to write to
 * @val:	The value to write
 */
static int sdhci_arasan_syscon_write(struct sdhci_host *host,
				   const struct sdhci_arasan_soc_ctl_field *fld,
				   u32 val)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	struct regmap *soc_ctl_base = sdhci_arasan->soc_ctl_base;
	u32 reg = fld->reg;
	u16 width = fld->width;
	s16 shift = fld->shift;
	int ret;

	/*
	 * Silently return errors for shift < 0 so caller doesn't have
	 * to check for fields which are optional.  For fields that
	 * are required then caller needs to do something special
	 * anyway.
	 */
	if (shift < 0)
		return -EINVAL;

	if (sdhci_arasan->soc_ctl_map->hiword_update)
		ret = regmap_write(soc_ctl_base, reg,
				   HIWORD_UPDATE(val, GENMASK(width, 0),
						 shift));
	else
		ret = regmap_update_bits(soc_ctl_base, reg,
					 GENMASK(shift + width, shift),
					 val << shift);

	/* Yell about (unexpected) regmap errors */
	if (ret)
		pr_warn("%s: Regmap write fail: %d\n",
			 mmc_hostname(host->mmc), ret);

	return ret;
}

static unsigned int sdhci_arasan_get_timeout_clock(struct sdhci_host *host)
{
	u32 div;
	unsigned long freq;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	div = readl(host->ioaddr + SDHCI_ARASAN_CLK_CTRL_OFFSET);
	div = (div & CLK_CTRL_TIMEOUT_MASK) >> CLK_CTRL_TIMEOUT_SHIFT;

	freq = clk_get_rate(pltfm_host->clk);
	freq /= 1 << (CLK_CTRL_TIMEOUT_MIN_EXP + div);

	return freq;
}

static void sdhci_arasan_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	bool ctrl_phy = false;

	if (clock > MMC_HIGH_52_MAX_DTR && (!IS_ERR(sdhci_arasan->phy)))
		ctrl_phy = true;

	if (ctrl_phy) {
		spin_unlock_irq(&host->lock);
		phy_power_off(sdhci_arasan->phy);
		spin_lock_irq(&host->lock);
	}

	sdhci_set_clock(host, clock);

	if (ctrl_phy) {
		spin_unlock_irq(&host->lock);
		phy_power_on(sdhci_arasan->phy);
		spin_lock_irq(&host->lock);
	}
}

static void sdhci_arasan_hs400_enhanced_strobe(struct mmc_host *mmc,
					struct mmc_ios *ios)
{
	u32 vendor;
	struct sdhci_host *host = mmc_priv(mmc);

	vendor = readl(host->ioaddr + SDHCI_ARASAN_VENDOR_REGISTER);
	if (ios->enhanced_strobe)
		vendor |= VENDOR_ENHANCED_STROBE;
	else
		vendor &= ~VENDOR_ENHANCED_STROBE;

	writel(vendor, host->ioaddr + SDHCI_ARASAN_VENDOR_REGISTER);
}

static struct sdhci_ops sdhci_arasan_ops = {
	.set_clock = sdhci_arasan_set_clock,
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.get_timeout_clock = sdhci_arasan_get_timeout_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static struct sdhci_pltfm_data sdhci_arasan_pdata = {
	.ops = &sdhci_arasan_ops,
	.quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
			SDHCI_QUIRK2_CLOCK_DIV_ZERO_BROKEN,
};

#ifdef CONFIG_PM_SLEEP
/**
 * sdhci_arasan_suspend - Suspend method for the driver
 * @dev:	Address of the device structure
 * Returns 0 on success and error value on error
 *
 * Put the device in a low power state.
 */
static int sdhci_arasan_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	int ret;

	ret = sdhci_suspend_host(host);
	if (ret)
		return ret;

	if (!IS_ERR(sdhci_arasan->phy)) {
		ret = phy_power_off(sdhci_arasan->phy);
		if (ret) {
			dev_err(dev, "Cannot power off phy.\n");
			sdhci_resume_host(host);
			return ret;
		}
	}

	clk_disable(pltfm_host->clk);
	clk_disable(sdhci_arasan->clk_ahb);

	return 0;
}

/**
 * sdhci_arasan_resume - Resume method for the driver
 * @dev:	Address of the device structure
 * Returns 0 on success and error value on error
 *
 * Resume operation after suspend
 */
static int sdhci_arasan_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	int ret;

	ret = clk_enable(sdhci_arasan->clk_ahb);
	if (ret) {
		dev_err(dev, "Cannot enable AHB clock.\n");
		return ret;
	}

	ret = clk_enable(pltfm_host->clk);
	if (ret) {
		dev_err(dev, "Cannot enable SD clock.\n");
		return ret;
	}

	if (!IS_ERR(sdhci_arasan->phy)) {
		ret = phy_power_on(sdhci_arasan->phy);
		if (ret) {
			dev_err(dev, "Cannot power on phy.\n");
			return ret;
		}
	}

	return sdhci_resume_host(host);
}
#endif /* ! CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(sdhci_arasan_dev_pm_ops, sdhci_arasan_suspend,
			 sdhci_arasan_resume);

static const struct of_device_id sdhci_arasan_of_match[] = {
	/* SoC-specific compatible strings w/ soc_ctl_map */
	{
		.compatible = "rockchip,rk3399-sdhci-5.1",
		.data = &rk3399_soc_ctl_map,
	},

	/* Generic compatible below here */
	{ .compatible = "arasan,sdhci-8.9a" },
	{ .compatible = "arasan,sdhci-5.1" },
	{ .compatible = "arasan,sdhci-4.9a" },

	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sdhci_arasan_of_match);

/**
 * sdhci_arasan_sdcardclk_recalc_rate - Return the card clock rate
 *
 * Return the current actual rate of the SD card clock.  This can be used
 * to communicate with out PHY.
 *
 * @hw:			Pointer to the hardware clock structure.
 * @parent_rate		The parent rate (should be rate of clk_xin).
 * Returns the card clock rate.
 */
static unsigned long sdhci_arasan_sdcardclk_recalc_rate(struct clk_hw *hw,
						      unsigned long parent_rate)

{
	struct sdhci_arasan_data *sdhci_arasan =
		container_of(hw, struct sdhci_arasan_data, sdcardclk_hw);
	struct sdhci_host *host = sdhci_arasan->host;

	return host->mmc->actual_clock;
}

static const struct clk_ops arasan_sdcardclk_ops = {
	.recalc_rate = sdhci_arasan_sdcardclk_recalc_rate,
};

/**
 * sdhci_arasan_update_baseclkfreq - Set corecfg_baseclkfreq
 *
 * The corecfg_baseclkfreq is supposed to contain the MHz of clk_xin.  This
 * function can be used to make that happen.
 *
 * NOTES:
 * - Many existing devices don't seem to do this and work fine.  To keep
 *   compatibility for old hardware where the device tree doesn't provide a
 *   register map, this function is a noop if a soc_ctl_map hasn't been provided
 *   for this platform.
 * - It's assumed that clk_xin is not dynamic and that we use the SDHCI divider
 *   to achieve lower clock rates.  That means that this function is called once
 *   at probe time and never called again.
 *
 * @host:		The sdhci_host
 */
static void sdhci_arasan_update_baseclkfreq(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_arasan_soc_ctl_map *soc_ctl_map =
		sdhci_arasan->soc_ctl_map;
	u32 mhz = DIV_ROUND_CLOSEST(clk_get_rate(pltfm_host->clk), 1000000);

	/* Having a map is optional */
	if (!soc_ctl_map)
		return;

	/* If we have a map, we expect to have a syscon */
	if (!sdhci_arasan->soc_ctl_base) {
		pr_warn("%s: Have regmap, but no soc-ctl-syscon\n",
			mmc_hostname(host->mmc));
		return;
	}

	sdhci_arasan_syscon_write(host, &soc_ctl_map->baseclkfreq, mhz);
}

/**
 * sdhci_arasan_register_sdclk - Register the sdclk for a PHY to use
 *
 * Some PHY devices need to know what the actual card clock is.  In order for
 * them to find out, we'll provide a clock through the common clock framework
 * for them to query.
 *
 * Note: without seriously re-architecting SDHCI's clock code and testing on
 * all platforms, there's no way to create a totally beautiful clock here
 * with all clock ops implemented.  Instead, we'll just create a clock that can
 * be queried and set the CLK_GET_RATE_NOCACHE attribute to tell common clock
 * framework that we're doing things behind its back.  This should be sufficient
 * to create nice clean device tree bindings and later (if needed) we can try
 * re-architecting SDHCI if we see some benefit to it.
 *
 * @sdhci_arasan:	Our private data structure.
 * @clk_xin:		Pointer to the functional clock
 * @dev:		Pointer to our struct device.
 * Returns 0 on success and error value on error
 */
static int sdhci_arasan_register_sdclk(struct sdhci_arasan_data *sdhci_arasan,
				       struct clk *clk_xin,
				       struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct clk_init_data sdcardclk_init;
	const char *parent_clk_name;
	int ret;

	/* Providing a clock to the PHY is optional; no error if missing */
	if (!of_find_property(np, "#clock-cells", NULL))
		return 0;

	ret = of_property_read_string_index(np, "clock-output-names", 0,
					    &sdcardclk_init.name);
	if (ret) {
		dev_err(dev, "DT has #clock-cells but no clock-output-names\n");
		return ret;
	}

	parent_clk_name = __clk_get_name(clk_xin);
	sdcardclk_init.parent_names = &parent_clk_name;
	sdcardclk_init.num_parents = 1;
	sdcardclk_init.flags = CLK_GET_RATE_NOCACHE;
	sdcardclk_init.ops = &arasan_sdcardclk_ops;

	sdhci_arasan->sdcardclk_hw.init = &sdcardclk_init;
	sdhci_arasan->sdcardclk =
		devm_clk_register(dev, &sdhci_arasan->sdcardclk_hw);
	sdhci_arasan->sdcardclk_hw.init = NULL;

	ret = of_clk_add_provider(np, of_clk_src_simple_get,
				  sdhci_arasan->sdcardclk);
	if (ret)
		dev_err(dev, "Failed to add clock provider\n");

	return ret;
}

/**
 * sdhci_arasan_unregister_sdclk - Undoes sdhci_arasan_register_sdclk()
 *
 * Should be called any time we're exiting and sdhci_arasan_register_sdclk()
 * returned success.
 *
 * @dev:		Pointer to our struct device.
 */
static void sdhci_arasan_unregister_sdclk(struct device *dev)
{
	struct device_node *np = dev->of_node;

	if (!of_find_property(np, "#clock-cells", NULL))
		return;

	of_clk_del_provider(dev->of_node);
}

static int sdhci_arasan_probe(struct platform_device *pdev)
{
	int ret;
	const struct of_device_id *match;
	struct device_node *node;
	struct clk *clk_xin;
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_arasan_data *sdhci_arasan;

	host = sdhci_pltfm_init(pdev, &sdhci_arasan_pdata,
				sizeof(*sdhci_arasan));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	sdhci_arasan->host = host;

	match = of_match_node(sdhci_arasan_of_match, pdev->dev.of_node);
	sdhci_arasan->soc_ctl_map = match->data;

	node = of_parse_phandle(pdev->dev.of_node, "arasan,soc-ctl-syscon", 0);
	if (node) {
		sdhci_arasan->soc_ctl_base = syscon_node_to_regmap(node);
		of_node_put(node);

		if (IS_ERR(sdhci_arasan->soc_ctl_base)) {
			ret = PTR_ERR(sdhci_arasan->soc_ctl_base);
			if (ret != -EPROBE_DEFER)
				dev_err(&pdev->dev, "Can't get syscon: %d\n",
					ret);
			goto err_pltfm_free;
		}
	}

	sdhci_arasan->clk_ahb = devm_clk_get(&pdev->dev, "clk_ahb");
	if (IS_ERR(sdhci_arasan->clk_ahb)) {
		dev_err(&pdev->dev, "clk_ahb clock not found.\n");
		ret = PTR_ERR(sdhci_arasan->clk_ahb);
		goto err_pltfm_free;
	}

	clk_xin = devm_clk_get(&pdev->dev, "clk_xin");
	if (IS_ERR(clk_xin)) {
		dev_err(&pdev->dev, "clk_xin clock not found.\n");
		ret = PTR_ERR(clk_xin);
		goto err_pltfm_free;
	}

	ret = clk_prepare_enable(sdhci_arasan->clk_ahb);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable AHB clock.\n");
		goto err_pltfm_free;
	}

	ret = clk_prepare_enable(clk_xin);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable SD clock.\n");
		goto clk_dis_ahb;
	}

	sdhci_get_of_property(pdev);
	pltfm_host->clk = clk_xin;

	sdhci_arasan_update_baseclkfreq(host);

	ret = sdhci_arasan_register_sdclk(sdhci_arasan, clk_xin, &pdev->dev);
	if (ret)
		goto clk_disable_all;

	ret = mmc_of_parse(host->mmc);
	if (ret) {
		dev_err(&pdev->dev, "parsing dt failed (%u)\n", ret);
		goto unreg_clk;
	}

	sdhci_arasan->phy = ERR_PTR(-ENODEV);
	if (of_device_is_compatible(pdev->dev.of_node,
				    "arasan,sdhci-5.1")) {
		sdhci_arasan->phy = devm_phy_get(&pdev->dev,
						 "phy_arasan");
		if (IS_ERR(sdhci_arasan->phy)) {
			ret = PTR_ERR(sdhci_arasan->phy);
			dev_err(&pdev->dev, "No phy for arasan,sdhci-5.1.\n");
			goto unreg_clk;
		}

		ret = phy_init(sdhci_arasan->phy);
		if (ret < 0) {
			dev_err(&pdev->dev, "phy_init err.\n");
			goto unreg_clk;
		}

		ret = phy_power_on(sdhci_arasan->phy);
		if (ret < 0) {
			dev_err(&pdev->dev, "phy_power_on err.\n");
			goto err_phy_power;
		}

		host->mmc_host_ops.hs400_enhanced_strobe =
					sdhci_arasan_hs400_enhanced_strobe;
	}

	ret = sdhci_add_host(host);
	if (ret)
		goto err_add_host;

	return 0;

err_add_host:
	if (!IS_ERR(sdhci_arasan->phy))
		phy_power_off(sdhci_arasan->phy);
err_phy_power:
	if (!IS_ERR(sdhci_arasan->phy))
		phy_exit(sdhci_arasan->phy);
unreg_clk:
	sdhci_arasan_unregister_sdclk(&pdev->dev);
clk_disable_all:
	clk_disable_unprepare(clk_xin);
clk_dis_ahb:
	clk_disable_unprepare(sdhci_arasan->clk_ahb);
err_pltfm_free:
	sdhci_pltfm_free(pdev);
	return ret;
}

static int sdhci_arasan_remove(struct platform_device *pdev)
{
	int ret;
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	struct clk *clk_ahb = sdhci_arasan->clk_ahb;

	if (!IS_ERR(sdhci_arasan->phy)) {
		phy_power_off(sdhci_arasan->phy);
		phy_exit(sdhci_arasan->phy);
	}

	sdhci_arasan_unregister_sdclk(&pdev->dev);

	ret = sdhci_pltfm_unregister(pdev);

	clk_disable_unprepare(clk_ahb);

	return ret;
}

static struct platform_driver sdhci_arasan_driver = {
	.driver = {
		.name = "sdhci-arasan",
		.of_match_table = sdhci_arasan_of_match,
		.pm = &sdhci_arasan_dev_pm_ops,
	},
	.probe = sdhci_arasan_probe,
	.remove = sdhci_arasan_remove,
};

module_platform_driver(sdhci_arasan_driver);

MODULE_DESCRIPTION("Driver for the Arasan SDHCI Controller");
MODULE_AUTHOR("Soeren Brinkmann <soren.brinkmann@xilinx.com>");
MODULE_LICENSE("GPL");
