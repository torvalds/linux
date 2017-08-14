/*
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/reset.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/gpio/consumer.h>

#include "sdhci-pltfm.h"

/* Tegra SDHOST controller vendor register definitions */
#define SDHCI_TEGRA_VENDOR_CLOCK_CTRL			0x100
#define SDHCI_CLOCK_CTRL_TAP_MASK			0x00ff0000
#define SDHCI_CLOCK_CTRL_TAP_SHIFT			16
#define SDHCI_CLOCK_CTRL_SDR50_TUNING_OVERRIDE		BIT(5)
#define SDHCI_CLOCK_CTRL_PADPIPE_CLKEN_OVERRIDE		BIT(3)
#define SDHCI_CLOCK_CTRL_SPI_MODE_CLKEN_OVERRIDE	BIT(2)

#define SDHCI_TEGRA_VENDOR_MISC_CTRL		0x120
#define SDHCI_MISC_CTRL_ENABLE_SDR104		0x8
#define SDHCI_MISC_CTRL_ENABLE_SDR50		0x10
#define SDHCI_MISC_CTRL_ENABLE_SDHCI_SPEC_300	0x20
#define SDHCI_MISC_CTRL_ENABLE_DDR50		0x200

#define SDHCI_TEGRA_AUTO_CAL_CONFIG		0x1e4
#define SDHCI_AUTO_CAL_START			BIT(31)
#define SDHCI_AUTO_CAL_ENABLE			BIT(29)

#define NVQUIRK_FORCE_SDHCI_SPEC_200	BIT(0)
#define NVQUIRK_ENABLE_BLOCK_GAP_DET	BIT(1)
#define NVQUIRK_ENABLE_SDHCI_SPEC_300	BIT(2)
#define NVQUIRK_ENABLE_SDR50		BIT(3)
#define NVQUIRK_ENABLE_SDR104		BIT(4)
#define NVQUIRK_ENABLE_DDR50		BIT(5)
#define NVQUIRK_HAS_PADCALIB		BIT(6)

struct sdhci_tegra_soc_data {
	const struct sdhci_pltfm_data *pdata;
	u32 nvquirks;
};

struct sdhci_tegra {
	const struct sdhci_tegra_soc_data *soc_data;
	struct gpio_desc *power_gpio;
	bool ddr_signaling;
	bool pad_calib_required;

	struct reset_control *rst;
};

static u16 tegra_sdhci_readw(struct sdhci_host *host, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_tegra_soc_data *soc_data = tegra_host->soc_data;

	if (unlikely((soc_data->nvquirks & NVQUIRK_FORCE_SDHCI_SPEC_200) &&
			(reg == SDHCI_HOST_VERSION))) {
		/* Erratum: Version register is invalid in HW. */
		return SDHCI_SPEC_200;
	}

	return readw(host->ioaddr + reg);
}

static void tegra_sdhci_writew(struct sdhci_host *host, u16 val, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	switch (reg) {
	case SDHCI_TRANSFER_MODE:
		/*
		 * Postpone this write, we must do it together with a
		 * command write that is down below.
		 */
		pltfm_host->xfer_mode_shadow = val;
		return;
	case SDHCI_COMMAND:
		writel((val << 16) | pltfm_host->xfer_mode_shadow,
			host->ioaddr + SDHCI_TRANSFER_MODE);
		return;
	}

	writew(val, host->ioaddr + reg);
}

static void tegra_sdhci_writel(struct sdhci_host *host, u32 val, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_tegra_soc_data *soc_data = tegra_host->soc_data;

	/* Seems like we're getting spurious timeout and crc errors, so
	 * disable signalling of them. In case of real errors software
	 * timers should take care of eventually detecting them.
	 */
	if (unlikely(reg == SDHCI_SIGNAL_ENABLE))
		val &= ~(SDHCI_INT_TIMEOUT|SDHCI_INT_CRC);

	writel(val, host->ioaddr + reg);

	if (unlikely((soc_data->nvquirks & NVQUIRK_ENABLE_BLOCK_GAP_DET) &&
			(reg == SDHCI_INT_ENABLE))) {
		/* Erratum: Must enable block gap interrupt detection */
		u8 gap_ctrl = readb(host->ioaddr + SDHCI_BLOCK_GAP_CONTROL);
		if (val & SDHCI_INT_CARD_INT)
			gap_ctrl |= 0x8;
		else
			gap_ctrl &= ~0x8;
		writeb(gap_ctrl, host->ioaddr + SDHCI_BLOCK_GAP_CONTROL);
	}
}

static unsigned int tegra_sdhci_get_ro(struct sdhci_host *host)
{
	return mmc_gpio_get_ro(host->mmc);
}

static void tegra_sdhci_reset(struct sdhci_host *host, u8 mask)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_tegra_soc_data *soc_data = tegra_host->soc_data;
	u32 misc_ctrl, clk_ctrl;

	sdhci_reset(host, mask);

	if (!(mask & SDHCI_RESET_ALL))
		return;

	misc_ctrl = sdhci_readl(host, SDHCI_TEGRA_VENDOR_MISC_CTRL);
	clk_ctrl = sdhci_readl(host, SDHCI_TEGRA_VENDOR_CLOCK_CTRL);

	misc_ctrl &= ~(SDHCI_MISC_CTRL_ENABLE_SDHCI_SPEC_300 |
		       SDHCI_MISC_CTRL_ENABLE_SDR50 |
		       SDHCI_MISC_CTRL_ENABLE_DDR50 |
		       SDHCI_MISC_CTRL_ENABLE_SDR104);

	clk_ctrl &= ~SDHCI_CLOCK_CTRL_SPI_MODE_CLKEN_OVERRIDE;

	/*
	 * If the board does not define a regulator for the SDHCI
	 * IO voltage, then don't advertise support for UHS modes
	 * even if the device supports it because the IO voltage
	 * cannot be configured.
	 */
	if (!IS_ERR(host->mmc->supply.vqmmc)) {
		/* Erratum: Enable SDHCI spec v3.00 support */
		if (soc_data->nvquirks & NVQUIRK_ENABLE_SDHCI_SPEC_300)
			misc_ctrl |= SDHCI_MISC_CTRL_ENABLE_SDHCI_SPEC_300;
		/* Advertise UHS modes as supported by host */
		if (soc_data->nvquirks & NVQUIRK_ENABLE_SDR50)
			misc_ctrl |= SDHCI_MISC_CTRL_ENABLE_SDR50;
		if (soc_data->nvquirks & NVQUIRK_ENABLE_DDR50)
			misc_ctrl |= SDHCI_MISC_CTRL_ENABLE_DDR50;
		if (soc_data->nvquirks & NVQUIRK_ENABLE_SDR104)
			misc_ctrl |= SDHCI_MISC_CTRL_ENABLE_SDR104;
		if (soc_data->nvquirks & SDHCI_MISC_CTRL_ENABLE_SDR50)
			clk_ctrl |= SDHCI_CLOCK_CTRL_SDR50_TUNING_OVERRIDE;
	}

	sdhci_writel(host, misc_ctrl, SDHCI_TEGRA_VENDOR_MISC_CTRL);
	sdhci_writel(host, clk_ctrl, SDHCI_TEGRA_VENDOR_CLOCK_CTRL);

	if (soc_data->nvquirks & NVQUIRK_HAS_PADCALIB)
		tegra_host->pad_calib_required = true;

	tegra_host->ddr_signaling = false;
}

static void tegra_sdhci_pad_autocalib(struct sdhci_host *host)
{
	u32 val;

	mdelay(1);

	val = sdhci_readl(host, SDHCI_TEGRA_AUTO_CAL_CONFIG);
	val |= SDHCI_AUTO_CAL_ENABLE | SDHCI_AUTO_CAL_START;
	sdhci_writel(host,val, SDHCI_TEGRA_AUTO_CAL_CONFIG);
}

static void tegra_sdhci_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	unsigned long host_clk;

	if (!clock)
		return sdhci_set_clock(host, clock);

	host_clk = tegra_host->ddr_signaling ? clock * 2 : clock;
	clk_set_rate(pltfm_host->clk, host_clk);
	host->max_clk = clk_get_rate(pltfm_host->clk);

	sdhci_set_clock(host, clock);

	if (tegra_host->pad_calib_required) {
		tegra_sdhci_pad_autocalib(host);
		tegra_host->pad_calib_required = false;
	}
}

static void tegra_sdhci_set_uhs_signaling(struct sdhci_host *host,
					  unsigned timing)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);

	if (timing == MMC_TIMING_UHS_DDR50)
		tegra_host->ddr_signaling = true;

	return sdhci_set_uhs_signaling(host, timing);
}

static unsigned int tegra_sdhci_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	/*
	 * DDR modes require the host to run at double the card frequency, so
	 * the maximum rate we can support is half of the module input clock.
	 */
	return clk_round_rate(pltfm_host->clk, UINT_MAX) / 2;
}

static void tegra_sdhci_set_tap(struct sdhci_host *host, unsigned int tap)
{
	u32 reg;

	reg = sdhci_readl(host, SDHCI_TEGRA_VENDOR_CLOCK_CTRL);
	reg &= ~SDHCI_CLOCK_CTRL_TAP_MASK;
	reg |= tap << SDHCI_CLOCK_CTRL_TAP_SHIFT;
	sdhci_writel(host, reg, SDHCI_TEGRA_VENDOR_CLOCK_CTRL);
}

static int tegra_sdhci_execute_tuning(struct sdhci_host *host, u32 opcode)
{
	unsigned int min, max;

	/*
	 * Start search for minimum tap value at 10, as smaller values are
	 * may wrongly be reported as working but fail at higher speeds,
	 * according to the TRM.
	 */
	min = 10;
	while (min < 255) {
		tegra_sdhci_set_tap(host, min);
		if (!mmc_send_tuning(host->mmc, opcode, NULL))
			break;
		min++;
	}

	/* Find the maximum tap value that still passes. */
	max = min + 1;
	while (max < 255) {
		tegra_sdhci_set_tap(host, max);
		if (mmc_send_tuning(host->mmc, opcode, NULL)) {
			max--;
			break;
		}
		max++;
	}

	/* The TRM states the ideal tap value is at 75% in the passing range. */
	tegra_sdhci_set_tap(host, min + ((max - min) * 3 / 4));

	return mmc_send_tuning(host->mmc, opcode, NULL);
}

static void tegra_sdhci_voltage_switch(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_tegra_soc_data *soc_data = tegra_host->soc_data;

	if (soc_data->nvquirks & NVQUIRK_HAS_PADCALIB)
		tegra_host->pad_calib_required = true;
}

static const struct sdhci_ops tegra_sdhci_ops = {
	.get_ro     = tegra_sdhci_get_ro,
	.read_w     = tegra_sdhci_readw,
	.write_l    = tegra_sdhci_writel,
	.set_clock  = tegra_sdhci_set_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset      = tegra_sdhci_reset,
	.platform_execute_tuning = tegra_sdhci_execute_tuning,
	.set_uhs_signaling = tegra_sdhci_set_uhs_signaling,
	.voltage_switch = tegra_sdhci_voltage_switch,
	.get_max_clock = tegra_sdhci_get_max_clock,
};

static const struct sdhci_pltfm_data sdhci_tegra20_pdata = {
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		  SDHCI_QUIRK_SINGLE_POWER_WRITE |
		  SDHCI_QUIRK_NO_HISPD_BIT |
		  SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.ops  = &tegra_sdhci_ops,
};

static const struct sdhci_tegra_soc_data soc_data_tegra20 = {
	.pdata = &sdhci_tegra20_pdata,
	.nvquirks = NVQUIRK_FORCE_SDHCI_SPEC_200 |
		    NVQUIRK_ENABLE_BLOCK_GAP_DET,
};

static const struct sdhci_pltfm_data sdhci_tegra30_pdata = {
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		  SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		  SDHCI_QUIRK_SINGLE_POWER_WRITE |
		  SDHCI_QUIRK_NO_HISPD_BIT |
		  SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
	.ops  = &tegra_sdhci_ops,
};

static const struct sdhci_tegra_soc_data soc_data_tegra30 = {
	.pdata = &sdhci_tegra30_pdata,
	.nvquirks = NVQUIRK_ENABLE_SDHCI_SPEC_300 |
		    NVQUIRK_ENABLE_SDR50 |
		    NVQUIRK_ENABLE_SDR104 |
		    NVQUIRK_HAS_PADCALIB,
};

static const struct sdhci_ops tegra114_sdhci_ops = {
	.get_ro     = tegra_sdhci_get_ro,
	.read_w     = tegra_sdhci_readw,
	.write_w    = tegra_sdhci_writew,
	.write_l    = tegra_sdhci_writel,
	.set_clock  = tegra_sdhci_set_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset      = tegra_sdhci_reset,
	.platform_execute_tuning = tegra_sdhci_execute_tuning,
	.set_uhs_signaling = tegra_sdhci_set_uhs_signaling,
	.voltage_switch = tegra_sdhci_voltage_switch,
	.get_max_clock = tegra_sdhci_get_max_clock,
};

static const struct sdhci_pltfm_data sdhci_tegra114_pdata = {
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		  SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		  SDHCI_QUIRK_SINGLE_POWER_WRITE |
		  SDHCI_QUIRK_NO_HISPD_BIT |
		  SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
	.ops  = &tegra114_sdhci_ops,
};

static const struct sdhci_tegra_soc_data soc_data_tegra114 = {
	.pdata = &sdhci_tegra114_pdata,
};

static const struct sdhci_pltfm_data sdhci_tegra124_pdata = {
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		  SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		  SDHCI_QUIRK_SINGLE_POWER_WRITE |
		  SDHCI_QUIRK_NO_HISPD_BIT |
		  SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
		   /*
		    * The TRM states that the SD/MMC controller found on
		    * Tegra124 can address 34 bits (the maximum supported by
		    * the Tegra memory controller), but tests show that DMA
		    * to or from above 4 GiB doesn't work. This is possibly
		    * caused by missing programming, though it's not obvious
		    * what sequence is required. Mark 64-bit DMA broken for
		    * now to fix this for existing users (e.g. Nyan boards).
		    */
		   SDHCI_QUIRK2_BROKEN_64_BIT_DMA,
	.ops  = &tegra114_sdhci_ops,
};

static const struct sdhci_tegra_soc_data soc_data_tegra124 = {
	.pdata = &sdhci_tegra124_pdata,
};

static const struct sdhci_pltfm_data sdhci_tegra210_pdata = {
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		  SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		  SDHCI_QUIRK_SINGLE_POWER_WRITE |
		  SDHCI_QUIRK_NO_HISPD_BIT |
		  SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
	.ops  = &tegra114_sdhci_ops,
};

static const struct sdhci_tegra_soc_data soc_data_tegra210 = {
	.pdata = &sdhci_tegra210_pdata,
};

static const struct sdhci_pltfm_data sdhci_tegra186_pdata = {
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		  SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		  SDHCI_QUIRK_SINGLE_POWER_WRITE |
		  SDHCI_QUIRK_NO_HISPD_BIT |
		  SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
	.ops  = &tegra114_sdhci_ops,
};

static const struct sdhci_tegra_soc_data soc_data_tegra186 = {
	.pdata = &sdhci_tegra186_pdata,
};

static const struct of_device_id sdhci_tegra_dt_match[] = {
	{ .compatible = "nvidia,tegra186-sdhci", .data = &soc_data_tegra186 },
	{ .compatible = "nvidia,tegra210-sdhci", .data = &soc_data_tegra210 },
	{ .compatible = "nvidia,tegra124-sdhci", .data = &soc_data_tegra124 },
	{ .compatible = "nvidia,tegra114-sdhci", .data = &soc_data_tegra114 },
	{ .compatible = "nvidia,tegra30-sdhci", .data = &soc_data_tegra30 },
	{ .compatible = "nvidia,tegra20-sdhci", .data = &soc_data_tegra20 },
	{}
};
MODULE_DEVICE_TABLE(of, sdhci_tegra_dt_match);

static int sdhci_tegra_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct sdhci_tegra_soc_data *soc_data;
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_tegra *tegra_host;
	struct clk *clk;
	int rc;

	match = of_match_device(sdhci_tegra_dt_match, &pdev->dev);
	if (!match)
		return -EINVAL;
	soc_data = match->data;

	host = sdhci_pltfm_init(pdev, soc_data->pdata, sizeof(*tegra_host));
	if (IS_ERR(host))
		return PTR_ERR(host);
	pltfm_host = sdhci_priv(host);

	tegra_host = sdhci_pltfm_priv(pltfm_host);
	tegra_host->ddr_signaling = false;
	tegra_host->pad_calib_required = false;
	tegra_host->soc_data = soc_data;

	rc = mmc_of_parse(host->mmc);
	if (rc)
		goto err_parse_dt;

	if (tegra_host->soc_data->nvquirks & NVQUIRK_ENABLE_DDR50)
		host->mmc->caps |= MMC_CAP_1_8V_DDR;

	tegra_host->power_gpio = devm_gpiod_get_optional(&pdev->dev, "power",
							 GPIOD_OUT_HIGH);
	if (IS_ERR(tegra_host->power_gpio)) {
		rc = PTR_ERR(tegra_host->power_gpio);
		goto err_power_req;
	}

	clk = devm_clk_get(mmc_dev(host->mmc), NULL);
	if (IS_ERR(clk)) {
		dev_err(mmc_dev(host->mmc), "clk err\n");
		rc = PTR_ERR(clk);
		goto err_clk_get;
	}
	clk_prepare_enable(clk);
	pltfm_host->clk = clk;

	tegra_host->rst = devm_reset_control_get_exclusive(&pdev->dev,
							   "sdhci");
	if (IS_ERR(tegra_host->rst)) {
		rc = PTR_ERR(tegra_host->rst);
		dev_err(&pdev->dev, "failed to get reset control: %d\n", rc);
		goto err_rst_get;
	}

	rc = reset_control_assert(tegra_host->rst);
	if (rc)
		goto err_rst_get;

	usleep_range(2000, 4000);

	rc = reset_control_deassert(tegra_host->rst);
	if (rc)
		goto err_rst_get;

	usleep_range(2000, 4000);

	rc = sdhci_add_host(host);
	if (rc)
		goto err_add_host;

	return 0;

err_add_host:
	reset_control_assert(tegra_host->rst);
err_rst_get:
	clk_disable_unprepare(pltfm_host->clk);
err_clk_get:
err_power_req:
err_parse_dt:
	sdhci_pltfm_free(pdev);
	return rc;
}

static int sdhci_tegra_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);

	sdhci_remove_host(host, 0);

	reset_control_assert(tegra_host->rst);
	usleep_range(2000, 4000);
	clk_disable_unprepare(pltfm_host->clk);

	sdhci_pltfm_free(pdev);

	return 0;
}

static struct platform_driver sdhci_tegra_driver = {
	.driver		= {
		.name	= "sdhci-tegra",
		.of_match_table = sdhci_tegra_dt_match,
		.pm	= &sdhci_pltfm_pmops,
	},
	.probe		= sdhci_tegra_probe,
	.remove		= sdhci_tegra_remove,
};

module_platform_driver(sdhci_tegra_driver);

MODULE_DESCRIPTION("SDHCI driver for Tegra");
MODULE_AUTHOR("Google, Inc.");
MODULE_LICENSE("GPL v2");
