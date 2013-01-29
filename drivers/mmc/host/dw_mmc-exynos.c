/*
 * Exynos Specific Extensions for Synopsys DW Multimedia Card Interface driver
 *
 * Copyright (C) 2012, Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/mmc/host.h>
#include <linux/mmc/dw_mmc.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#include "dw_mmc.h"
#include "dw_mmc-pltfm.h"

#define NUM_PINS(x)			(x + 2)

#define SDMMC_CLKSEL			0x09C
#define SDMMC_CLKSEL_CCLK_SAMPLE(x)	(((x) & 7) << 0)
#define SDMMC_CLKSEL_CCLK_DRIVE(x)	(((x) & 7) << 16)
#define SDMMC_CLKSEL_CCLK_DIVIDER(x)	(((x) & 7) << 24)
#define SDMMC_CLKSEL_GET_DRV_WD3(x)	(((x) >> 16) & 0x7)
#define SDMMC_CLKSEL_TIMING(x, y, z)	(SDMMC_CLKSEL_CCLK_SAMPLE(x) |	\
					SDMMC_CLKSEL_CCLK_DRIVE(y) |	\
					SDMMC_CLKSEL_CCLK_DIVIDER(z))

#define SDMMC_CMD_USE_HOLD_REG		BIT(29)

#define EXYNOS4210_FIXED_CIU_CLK_DIV	2
#define EXYNOS4412_FIXED_CIU_CLK_DIV	4

/* Variations in Exynos specific dw-mshc controller */
enum dw_mci_exynos_type {
	DW_MCI_TYPE_EXYNOS4210,
	DW_MCI_TYPE_EXYNOS4412,
	DW_MCI_TYPE_EXYNOS5250,
};

/* Exynos implementation specific driver private data */
struct dw_mci_exynos_priv_data {
	enum dw_mci_exynos_type		ctrl_type;
	u8				ciu_div;
	u32				sdr_timing;
	u32				ddr_timing;
};

static struct dw_mci_exynos_compatible {
	char				*compatible;
	enum dw_mci_exynos_type		ctrl_type;
} exynos_compat[] = {
	{
		.compatible	= "samsung,exynos4210-dw-mshc",
		.ctrl_type	= DW_MCI_TYPE_EXYNOS4210,
	}, {
		.compatible	= "samsung,exynos4412-dw-mshc",
		.ctrl_type	= DW_MCI_TYPE_EXYNOS4412,
	}, {
		.compatible	= "samsung,exynos5250-dw-mshc",
		.ctrl_type	= DW_MCI_TYPE_EXYNOS5250,
	},
};

static int dw_mci_exynos_priv_init(struct dw_mci *host)
{
	struct dw_mci_exynos_priv_data *priv;
	int idx;

	priv = devm_kzalloc(host->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(host->dev, "mem alloc failed for private data\n");
		return -ENOMEM;
	}

	for (idx = 0; idx < ARRAY_SIZE(exynos_compat); idx++) {
		if (of_device_is_compatible(host->dev->of_node,
					exynos_compat[idx].compatible))
			priv->ctrl_type = exynos_compat[idx].ctrl_type;
	}

	host->priv = priv;
	return 0;
}

static int dw_mci_exynos_setup_clock(struct dw_mci *host)
{
	struct dw_mci_exynos_priv_data *priv = host->priv;

	if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS5250)
		host->bus_hz /= (priv->ciu_div + 1);
	else if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS4412)
		host->bus_hz /= EXYNOS4412_FIXED_CIU_CLK_DIV;
	else if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS4210)
		host->bus_hz /= EXYNOS4210_FIXED_CIU_CLK_DIV;

	return 0;
}

static void dw_mci_exynos_prepare_command(struct dw_mci *host, u32 *cmdr)
{
	/*
	 * Exynos4412 and Exynos5250 extends the use of CMD register with the
	 * use of bit 29 (which is reserved on standard MSHC controllers) for
	 * optionally bypassing the HOLD register for command and data. The
	 * HOLD register should be bypassed in case there is no phase shift
	 * applied on CMD/DATA that is sent to the card.
	 */
	if (SDMMC_CLKSEL_GET_DRV_WD3(mci_readl(host, CLKSEL)))
		*cmdr |= SDMMC_CMD_USE_HOLD_REG;
}

static void dw_mci_exynos_set_ios(struct dw_mci *host, struct mmc_ios *ios)
{
	struct dw_mci_exynos_priv_data *priv = host->priv;

	if (ios->timing == MMC_TIMING_UHS_DDR50)
		mci_writel(host, CLKSEL, priv->ddr_timing);
	else
		mci_writel(host, CLKSEL, priv->sdr_timing);
}

static int dw_mci_exynos_parse_dt(struct dw_mci *host)
{
	struct dw_mci_exynos_priv_data *priv = host->priv;
	struct device_node *np = host->dev->of_node;
	u32 timing[2];
	u32 div = 0;
	int ret;

	of_property_read_u32(np, "samsung,dw-mshc-ciu-div", &div);
	priv->ciu_div = div;

	ret = of_property_read_u32_array(np,
			"samsung,dw-mshc-sdr-timing", timing, 2);
	if (ret)
		return ret;

	priv->sdr_timing = SDMMC_CLKSEL_TIMING(timing[0], timing[1], div);

	ret = of_property_read_u32_array(np,
			"samsung,dw-mshc-ddr-timing", timing, 2);
	if (ret)
		return ret;

	priv->ddr_timing = SDMMC_CLKSEL_TIMING(timing[0], timing[1], div);
	return 0;
}

static int dw_mci_exynos_setup_bus(struct dw_mci *host,
				struct device_node *slot_np, u8 bus_width)
{
	int idx, gpio, ret;

	if (!slot_np)
		return -EINVAL;

	/* cmd + clock + bus-width pins */
	for (idx = 0; idx < NUM_PINS(bus_width); idx++) {
		gpio = of_get_gpio(slot_np, idx);
		if (!gpio_is_valid(gpio)) {
			dev_err(host->dev, "invalid gpio: %d\n", gpio);
			return -EINVAL;
		}

		ret = devm_gpio_request(host->dev, gpio, "dw-mci-bus");
		if (ret) {
			dev_err(host->dev, "gpio [%d] request failed\n", gpio);
			return -EBUSY;
		}
	}

	gpio = of_get_named_gpio(slot_np, "wp-gpios", 0);
	if (gpio_is_valid(gpio)) {
		if (devm_gpio_request(host->dev, gpio, "dw-mci-wp"))
			dev_info(host->dev, "gpio [%d] request failed\n",
						gpio);
	} else {
		dev_info(host->dev, "wp gpio not available");
		host->pdata->quirks |= DW_MCI_QUIRK_NO_WRITE_PROTECT;
	}

	if (host->pdata->quirks & DW_MCI_QUIRK_BROKEN_CARD_DETECTION)
		return 0;

	gpio = of_get_named_gpio(slot_np, "samsung,cd-pinmux-gpio", 0);
	if (gpio_is_valid(gpio)) {
		if (devm_gpio_request(host->dev, gpio, "dw-mci-cd"))
			dev_err(host->dev, "gpio [%d] request failed\n", gpio);
	} else {
		dev_info(host->dev, "cd gpio not available");
	}

	return 0;
}

/* Exynos5250 controller specific capabilities */
static unsigned long exynos5250_dwmmc_caps[4] = {
	MMC_CAP_UHS_DDR50 | MMC_CAP_1_8V_DDR |
		MMC_CAP_8_BIT_DATA | MMC_CAP_CMD23,
	MMC_CAP_CMD23,
	MMC_CAP_CMD23,
	MMC_CAP_CMD23,
};

static const struct dw_mci_drv_data exynos5250_drv_data = {
	.caps			= exynos5250_dwmmc_caps,
	.init			= dw_mci_exynos_priv_init,
	.setup_clock		= dw_mci_exynos_setup_clock,
	.prepare_command	= dw_mci_exynos_prepare_command,
	.set_ios		= dw_mci_exynos_set_ios,
	.parse_dt		= dw_mci_exynos_parse_dt,
	.setup_bus		= dw_mci_exynos_setup_bus,
};

static const struct of_device_id dw_mci_exynos_match[] = {
	{ .compatible = "samsung,exynos5250-dw-mshc",
			.data = &exynos5250_drv_data, },
	{},
};
MODULE_DEVICE_TABLE(of, dw_mci_exynos_match);

int dw_mci_exynos_probe(struct platform_device *pdev)
{
	const struct dw_mci_drv_data *drv_data;
	const struct of_device_id *match;

	match = of_match_node(dw_mci_exynos_match, pdev->dev.of_node);
	drv_data = match->data;
	return dw_mci_pltfm_register(pdev, drv_data);
}

static struct platform_driver dw_mci_exynos_pltfm_driver = {
	.probe		= dw_mci_exynos_probe,
	.remove		= __exit_p(dw_mci_pltfm_remove),
	.driver		= {
		.name		= "dwmmc_exynos",
		.of_match_table	= of_match_ptr(dw_mci_exynos_match),
		.pm		= &dw_mci_pltfm_pmops,
	},
};

module_platform_driver(dw_mci_exynos_pltfm_driver);

MODULE_DESCRIPTION("Samsung Specific DW-MSHC Driver Extension");
MODULE_AUTHOR("Thomas Abraham <thomas.ab@samsung.com");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dwmmc-exynos");
