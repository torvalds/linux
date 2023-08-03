// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive Designware Mobile Storage Host Controller Driver
 *
 * Copyright (c) 2022 StarFive Technology Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>

#include "dw_mmc.h"
#include "dw_mmc-pltfm.h"

#define ALL_INT_CLR		0x1ffff
#define MAX_DELAY_CHAIN		32
struct starfive_priv {
	struct device *dev;
	struct regmap *reg_syscon;
	u32 syscon_offset;
	u32 syscon_shift;
	u32 syscon_mask;
};

static unsigned long dw_mci_starfive_caps[] = {
	MMC_CAP_CMD23,
	MMC_CAP_CMD23,
	MMC_CAP_CMD23
};

static void dw_mci_starfive_set_ios(struct dw_mci *host, struct mmc_ios *ios)
{
	int ret;
	unsigned int clock;

	if (ios->timing == MMC_TIMING_MMC_DDR52 || ios->timing == MMC_TIMING_UHS_DDR50) {
		clock = (ios->clock > 50000000 && ios->clock <= 52000000) ? 100000000 : ios->clock;
		ret = clk_set_rate(host->ciu_clk, clock);
		if (ret)
			dev_dbg(host->dev, "Use an external frequency divider %uHz\n", ios->clock);
		host->bus_hz = clk_get_rate(host->ciu_clk);
	} else {
		dev_dbg(host->dev, "Using the internal divider\n");
	}
}

static void dw_mci_starfive_hs_set_bits(struct dw_mci *host, u32 smpl_phase)
{
	/* change driver phase and sample phase */
	u32 mask = 0x1f;
	u32 reg_value;

	reg_value = mci_readl(host, UHS_REG_EXT);

	/* In UHS_REG_EXT, only 5 bits valid in DRV_PHASE and SMPL_PHASE */
	reg_value &= ~(mask << 16);
	reg_value |= (smpl_phase << 16);
	mci_writel(host, UHS_REG_EXT, reg_value);

	/* We should delay 1ms wait for timing setting finished. */
	udelay(1000);
}

static int dw_mci_starfive_execute_tuning(struct dw_mci_slot *slot,
					     u32 opcode)
{
	static const int grade  = MAX_DELAY_CHAIN;
	struct dw_mci *host = slot->host;
	int err = -1;
	int smpl_phase, smpl_raise = -1, smpl_fall = -1;
	int i;

	for (i = 0; i < grade; i++) {
		smpl_phase = i;
		dw_mci_starfive_hs_set_bits(host, smpl_phase);
		mci_writel(host, RINTSTS, ALL_INT_CLR);

		err = mmc_send_tuning(slot->mmc, opcode, NULL);

		if (!err && smpl_raise < 0)
			smpl_raise = i;
		else if (err && smpl_raise >= 0) {
			smpl_fall = i - 1;
			break;
		}
	}

	if (i >= grade && smpl_raise >= 0)
		smpl_fall = grade - 1 ;

	if (smpl_raise < 0) {
		dev_err(host->dev, "No valid delay chain! use default\n");
		dw_mci_starfive_hs_set_bits(host, 0);
		err = -EINVAL;
	}
	else {
		smpl_phase = (smpl_raise + smpl_fall) / 2;
		dw_mci_starfive_hs_set_bits(host, smpl_phase);
		dev_dbg(host->dev, "Found valid delay chain! use it [delay=%d]\n", smpl_phase);
		err = 0;
	}

	mci_writel(host, RINTSTS, ALL_INT_CLR);
	return err;
}

static int dw_mci_starfive_switch_voltage(struct mmc_host *mmc, struct mmc_ios *ios)
{

	struct dw_mci_slot *slot = mmc_priv(mmc);
	struct dw_mci *host = slot->host;
	u32 ret;

	if (device_property_read_bool(host->dev, "board-is-evb")) {
		if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_330)
			ret = gpio_direction_output(25, 0);
		else if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_180)
			ret = gpio_direction_output(25, 1);
		if (ret)
			return ret;
	}

	if (!IS_ERR(mmc->supply.vqmmc)) {
		ret = mmc_regulator_set_vqmmc(mmc, ios);
		if (ret < 0) {
			dev_err(host->dev, "Regulator set error %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static const struct dw_mci_drv_data starfive_data = {
	.caps = dw_mci_starfive_caps,
	.num_caps = ARRAY_SIZE(dw_mci_starfive_caps),
	.set_ios = dw_mci_starfive_set_ios,
	.execute_tuning = dw_mci_starfive_execute_tuning,
	.switch_voltage = dw_mci_starfive_switch_voltage,
};

static const struct of_device_id dw_mci_starfive_match[] = {
	{ .compatible = "starfive,jh7110-sdio",
		.data = &starfive_data },
	{},
};
MODULE_DEVICE_TABLE(of, dw_mci_starfive_match);

static int dw_mci_starfive_probe(struct platform_device *pdev)
{
	const struct dw_mci_drv_data *drv_data;
	const struct of_device_id *match;
	struct gpio_desc *power_gpio;
	int gpio_wl_reg_on = -1;
	int ret;

	match = of_match_node(dw_mci_starfive_match, pdev->dev.of_node);
	drv_data = match->data;

	if (device_property_read_bool(&pdev->dev, "board-is-devkits")) {
		power_gpio = devm_gpiod_get_optional(&pdev->dev, "power", GPIOD_OUT_LOW);
		if (IS_ERR(power_gpio)) {
			dev_err(&pdev->dev, "Failed to get power-gpio\n");
			return -EINVAL;
		}

		gpiod_set_value_cansleep(power_gpio, 1);
	}

	gpio_wl_reg_on = of_get_named_gpio(pdev->dev.of_node, "gpio_wl_reg_on", 0);
	if (gpio_wl_reg_on >= 0) {
		ret = gpio_request(gpio_wl_reg_on, "WL_REG_ON");
		if (ret < 0) {
			dev_err(&pdev->dev, "gpio_request(%d) for WL_REG_ON failed %d\n",
				 gpio_wl_reg_on, ret);
			gpio_wl_reg_on = -1;
			return -EINVAL;
		}
		ret = gpio_direction_output(gpio_wl_reg_on, 0);
		if (ret) {
			dev_err(&pdev->dev, "WL_REG_ON didn't output high\n");
			return -EIO;
		}
		mdelay(10);
		ret = gpio_direction_output(gpio_wl_reg_on, 1);
		if (ret) {
			dev_err(&pdev->dev, "WL_REG_ON didn't output high\n");
			return -EIO;
		}
		mdelay(10);
	}

	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = dw_mci_pltfm_register(pdev, drv_data);
	if (ret) {
		pm_runtime_disable(&pdev->dev);
		pm_runtime_set_suspended(&pdev->dev);
		pm_runtime_put_noidle(&pdev->dev);

		return ret;
	}

	return 0;
}

static int dw_mci_starfive_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	return dw_mci_pltfm_remove(pdev);
}

static const struct dev_pm_ops dw_mci_starfive_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(dw_mci_runtime_suspend,
			   dw_mci_runtime_resume, NULL)
};

static struct platform_driver dw_mci_starfive_driver = {
	.probe = dw_mci_starfive_probe,
	.remove = dw_mci_starfive_remove,
	.driver = {
		.name = "dwmmc_starfive",
		.pm   = &dw_mci_starfive_pm_ops,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = dw_mci_starfive_match,
	},
};
module_platform_driver(dw_mci_starfive_driver);

MODULE_DESCRIPTION("StarFive JH7110 Specific DW-MSHC Driver Extension");
MODULE_AUTHOR("William Qiu <william.qiu@starfivetech.com");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:dwmmc_starfive");
