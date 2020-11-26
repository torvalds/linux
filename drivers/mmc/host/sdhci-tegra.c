// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 Google, Inc.
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/ktime.h>

#include "sdhci-pltfm.h"
#include "cqhci.h"

/* Tegra SDHOST controller vendor register definitions */
#define SDHCI_TEGRA_VENDOR_CLOCK_CTRL			0x100
#define SDHCI_CLOCK_CTRL_TAP_MASK			0x00ff0000
#define SDHCI_CLOCK_CTRL_TAP_SHIFT			16
#define SDHCI_CLOCK_CTRL_TRIM_MASK			0x1f000000
#define SDHCI_CLOCK_CTRL_TRIM_SHIFT			24
#define SDHCI_CLOCK_CTRL_SDR50_TUNING_OVERRIDE		BIT(5)
#define SDHCI_CLOCK_CTRL_PADPIPE_CLKEN_OVERRIDE		BIT(3)
#define SDHCI_CLOCK_CTRL_SPI_MODE_CLKEN_OVERRIDE	BIT(2)

#define SDHCI_TEGRA_VENDOR_SYS_SW_CTRL			0x104
#define SDHCI_TEGRA_SYS_SW_CTRL_ENHANCED_STROBE		BIT(31)

#define SDHCI_TEGRA_VENDOR_CAP_OVERRIDES		0x10c
#define SDHCI_TEGRA_CAP_OVERRIDES_DQS_TRIM_MASK		0x00003f00
#define SDHCI_TEGRA_CAP_OVERRIDES_DQS_TRIM_SHIFT	8

#define SDHCI_TEGRA_VENDOR_MISC_CTRL			0x120
#define SDHCI_MISC_CTRL_ERASE_TIMEOUT_LIMIT		BIT(0)
#define SDHCI_MISC_CTRL_ENABLE_SDR104			0x8
#define SDHCI_MISC_CTRL_ENABLE_SDR50			0x10
#define SDHCI_MISC_CTRL_ENABLE_SDHCI_SPEC_300		0x20
#define SDHCI_MISC_CTRL_ENABLE_DDR50			0x200

#define SDHCI_TEGRA_VENDOR_DLLCAL_CFG			0x1b0
#define SDHCI_TEGRA_DLLCAL_CALIBRATE			BIT(31)

#define SDHCI_TEGRA_VENDOR_DLLCAL_STA			0x1bc
#define SDHCI_TEGRA_DLLCAL_STA_ACTIVE			BIT(31)

#define SDHCI_VNDR_TUN_CTRL0_0				0x1c0
#define SDHCI_VNDR_TUN_CTRL0_TUN_HW_TAP			0x20000
#define SDHCI_VNDR_TUN_CTRL0_START_TAP_VAL_MASK		0x03fc0000
#define SDHCI_VNDR_TUN_CTRL0_START_TAP_VAL_SHIFT	18
#define SDHCI_VNDR_TUN_CTRL0_MUL_M_MASK			0x00001fc0
#define SDHCI_VNDR_TUN_CTRL0_MUL_M_SHIFT		6
#define SDHCI_VNDR_TUN_CTRL0_TUN_ITER_MASK		0x000e000
#define SDHCI_VNDR_TUN_CTRL0_TUN_ITER_SHIFT		13
#define TRIES_128					2
#define TRIES_256					4
#define SDHCI_VNDR_TUN_CTRL0_TUN_WORD_SEL_MASK		0x7

#define SDHCI_TEGRA_VNDR_TUN_CTRL1_0			0x1c4
#define SDHCI_TEGRA_VNDR_TUN_STATUS0			0x1C8
#define SDHCI_TEGRA_VNDR_TUN_STATUS1			0x1CC
#define SDHCI_TEGRA_VNDR_TUN_STATUS1_TAP_MASK		0xFF
#define SDHCI_TEGRA_VNDR_TUN_STATUS1_END_TAP_SHIFT	0x8
#define TUNING_WORD_BIT_SIZE				32

#define SDHCI_TEGRA_AUTO_CAL_CONFIG			0x1e4
#define SDHCI_AUTO_CAL_START				BIT(31)
#define SDHCI_AUTO_CAL_ENABLE				BIT(29)
#define SDHCI_AUTO_CAL_PDPU_OFFSET_MASK			0x0000ffff

#define SDHCI_TEGRA_SDMEM_COMP_PADCTRL			0x1e0
#define SDHCI_TEGRA_SDMEM_COMP_PADCTRL_VREF_SEL_MASK	0x0000000f
#define SDHCI_TEGRA_SDMEM_COMP_PADCTRL_VREF_SEL_VAL	0x7
#define SDHCI_TEGRA_SDMEM_COMP_PADCTRL_E_INPUT_E_PWRD	BIT(31)
#define SDHCI_COMP_PADCTRL_DRVUPDN_OFFSET_MASK		0x07FFF000

#define SDHCI_TEGRA_AUTO_CAL_STATUS			0x1ec
#define SDHCI_TEGRA_AUTO_CAL_ACTIVE			BIT(31)

#define NVQUIRK_FORCE_SDHCI_SPEC_200			BIT(0)
#define NVQUIRK_ENABLE_BLOCK_GAP_DET			BIT(1)
#define NVQUIRK_ENABLE_SDHCI_SPEC_300			BIT(2)
#define NVQUIRK_ENABLE_SDR50				BIT(3)
#define NVQUIRK_ENABLE_SDR104				BIT(4)
#define NVQUIRK_ENABLE_DDR50				BIT(5)
/*
 * HAS_PADCALIB NVQUIRK is for SoC's supporting auto calibration of pads
 * drive strength.
 */
#define NVQUIRK_HAS_PADCALIB				BIT(6)
/*
 * NEEDS_PAD_CONTROL NVQUIRK is for SoC's having separate 3V3 and 1V8 pads.
 * 3V3/1V8 pad selection happens through pinctrl state selection depending
 * on the signaling mode.
 */
#define NVQUIRK_NEEDS_PAD_CONTROL			BIT(7)
#define NVQUIRK_DIS_CARD_CLK_CONFIG_TAP			BIT(8)
#define NVQUIRK_CQHCI_DCMD_R1B_CMD_TIMING		BIT(9)

/*
 * NVQUIRK_HAS_TMCLK is for SoC's having separate timeout clock for Tegra
 * SDMMC hardware data timeout.
 */
#define NVQUIRK_HAS_TMCLK				BIT(10)

/* SDMMC CQE Base Address for Tegra Host Ver 4.1 and Higher */
#define SDHCI_TEGRA_CQE_BASE_ADDR			0xF000

struct sdhci_tegra_soc_data {
	const struct sdhci_pltfm_data *pdata;
	u64 dma_mask;
	u32 nvquirks;
	u8 min_tap_delay;
	u8 max_tap_delay;
};

/* Magic pull up and pull down pad calibration offsets */
struct sdhci_tegra_autocal_offsets {
	u32 pull_up_3v3;
	u32 pull_down_3v3;
	u32 pull_up_3v3_timeout;
	u32 pull_down_3v3_timeout;
	u32 pull_up_1v8;
	u32 pull_down_1v8;
	u32 pull_up_1v8_timeout;
	u32 pull_down_1v8_timeout;
	u32 pull_up_sdr104;
	u32 pull_down_sdr104;
	u32 pull_up_hs400;
	u32 pull_down_hs400;
};

struct sdhci_tegra {
	const struct sdhci_tegra_soc_data *soc_data;
	struct gpio_desc *power_gpio;
	struct clk *tmclk;
	bool ddr_signaling;
	bool pad_calib_required;
	bool pad_control_available;

	struct reset_control *rst;
	struct pinctrl *pinctrl_sdmmc;
	struct pinctrl_state *pinctrl_state_3v3;
	struct pinctrl_state *pinctrl_state_1v8;
	struct pinctrl_state *pinctrl_state_3v3_drv;
	struct pinctrl_state *pinctrl_state_1v8_drv;

	struct sdhci_tegra_autocal_offsets autocal_offsets;
	ktime_t last_calib;

	u32 default_tap;
	u32 default_trim;
	u32 dqs_trim;
	bool enable_hwcq;
	unsigned long curr_clk_rate;
	u8 tuned_tap_delay;
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

static bool tegra_sdhci_configure_card_clk(struct sdhci_host *host, bool enable)
{
	bool status;
	u32 reg;

	reg = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	status = !!(reg & SDHCI_CLOCK_CARD_EN);

	if (status == enable)
		return status;

	if (enable)
		reg |= SDHCI_CLOCK_CARD_EN;
	else
		reg &= ~SDHCI_CLOCK_CARD_EN;

	sdhci_writew(host, reg, SDHCI_CLOCK_CONTROL);

	return status;
}

static void tegra210_sdhci_writew(struct sdhci_host *host, u16 val, int reg)
{
	bool is_tuning_cmd = 0;
	bool clk_enabled;
	u8 cmd;

	if (reg == SDHCI_COMMAND) {
		cmd = SDHCI_GET_CMD(val);
		is_tuning_cmd = cmd == MMC_SEND_TUNING_BLOCK ||
				cmd == MMC_SEND_TUNING_BLOCK_HS200;
	}

	if (is_tuning_cmd)
		clk_enabled = tegra_sdhci_configure_card_clk(host, 0);

	writew(val, host->ioaddr + reg);

	if (is_tuning_cmd) {
		udelay(1);
		sdhci_reset(host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
		tegra_sdhci_configure_card_clk(host, clk_enabled);
	}
}

static unsigned int tegra_sdhci_get_ro(struct sdhci_host *host)
{
	/*
	 * Write-enable shall be assumed if GPIO is missing in a board's
	 * device-tree because SDHCI's WRITE_PROTECT bit doesn't work on
	 * Tegra.
	 */
	return mmc_gpio_get_ro(host->mmc);
}

static bool tegra_sdhci_is_pad_and_regulator_valid(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	int has_1v8, has_3v3;

	/*
	 * The SoCs which have NVQUIRK_NEEDS_PAD_CONTROL require software pad
	 * voltage configuration in order to perform voltage switching. This
	 * means that valid pinctrl info is required on SDHCI instances capable
	 * of performing voltage switching. Whether or not an SDHCI instance is
	 * capable of voltage switching is determined based on the regulator.
	 */

	if (!(tegra_host->soc_data->nvquirks & NVQUIRK_NEEDS_PAD_CONTROL))
		return true;

	if (IS_ERR(host->mmc->supply.vqmmc))
		return false;

	has_1v8 = regulator_is_supported_voltage(host->mmc->supply.vqmmc,
						 1700000, 1950000);

	has_3v3 = regulator_is_supported_voltage(host->mmc->supply.vqmmc,
						 2700000, 3600000);

	if (has_1v8 == 1 && has_3v3 == 1)
		return tegra_host->pad_control_available;

	/* Fixed voltage, no pad control required. */
	return true;
}

static void tegra_sdhci_set_tap(struct sdhci_host *host, unsigned int tap)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_tegra_soc_data *soc_data = tegra_host->soc_data;
	bool card_clk_enabled = false;
	u32 reg;

	/*
	 * Touching the tap values is a bit tricky on some SoC generations.
	 * The quirk enables a workaround for a glitch that sometimes occurs if
	 * the tap values are changed.
	 */

	if (soc_data->nvquirks & NVQUIRK_DIS_CARD_CLK_CONFIG_TAP)
		card_clk_enabled = tegra_sdhci_configure_card_clk(host, false);

	reg = sdhci_readl(host, SDHCI_TEGRA_VENDOR_CLOCK_CTRL);
	reg &= ~SDHCI_CLOCK_CTRL_TAP_MASK;
	reg |= tap << SDHCI_CLOCK_CTRL_TAP_SHIFT;
	sdhci_writel(host, reg, SDHCI_TEGRA_VENDOR_CLOCK_CTRL);

	if (soc_data->nvquirks & NVQUIRK_DIS_CARD_CLK_CONFIG_TAP &&
	    card_clk_enabled) {
		udelay(1);
		sdhci_reset(host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
		tegra_sdhci_configure_card_clk(host, card_clk_enabled);
	}
}

static void tegra_sdhci_hs400_enhanced_strobe(struct mmc_host *mmc,
					      struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	u32 val;

	val = sdhci_readl(host, SDHCI_TEGRA_VENDOR_SYS_SW_CTRL);

	if (ios->enhanced_strobe)
		val |= SDHCI_TEGRA_SYS_SW_CTRL_ENHANCED_STROBE;
	else
		val &= ~SDHCI_TEGRA_SYS_SW_CTRL_ENHANCED_STROBE;

	sdhci_writel(host, val, SDHCI_TEGRA_VENDOR_SYS_SW_CTRL);

}

static void tegra_sdhci_reset(struct sdhci_host *host, u8 mask)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_tegra_soc_data *soc_data = tegra_host->soc_data;
	u32 misc_ctrl, clk_ctrl, pad_ctrl;

	sdhci_reset(host, mask);

	if (!(mask & SDHCI_RESET_ALL))
		return;

	tegra_sdhci_set_tap(host, tegra_host->default_tap);

	misc_ctrl = sdhci_readl(host, SDHCI_TEGRA_VENDOR_MISC_CTRL);
	clk_ctrl = sdhci_readl(host, SDHCI_TEGRA_VENDOR_CLOCK_CTRL);

	misc_ctrl &= ~(SDHCI_MISC_CTRL_ENABLE_SDHCI_SPEC_300 |
		       SDHCI_MISC_CTRL_ENABLE_SDR50 |
		       SDHCI_MISC_CTRL_ENABLE_DDR50 |
		       SDHCI_MISC_CTRL_ENABLE_SDR104);

	clk_ctrl &= ~(SDHCI_CLOCK_CTRL_TRIM_MASK |
		      SDHCI_CLOCK_CTRL_SPI_MODE_CLKEN_OVERRIDE);

	if (tegra_sdhci_is_pad_and_regulator_valid(host)) {
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
		if (soc_data->nvquirks & NVQUIRK_ENABLE_SDR50)
			clk_ctrl |= SDHCI_CLOCK_CTRL_SDR50_TUNING_OVERRIDE;
	}

	clk_ctrl |= tegra_host->default_trim << SDHCI_CLOCK_CTRL_TRIM_SHIFT;

	sdhci_writel(host, misc_ctrl, SDHCI_TEGRA_VENDOR_MISC_CTRL);
	sdhci_writel(host, clk_ctrl, SDHCI_TEGRA_VENDOR_CLOCK_CTRL);

	if (soc_data->nvquirks & NVQUIRK_HAS_PADCALIB) {
		pad_ctrl = sdhci_readl(host, SDHCI_TEGRA_SDMEM_COMP_PADCTRL);
		pad_ctrl &= ~SDHCI_TEGRA_SDMEM_COMP_PADCTRL_VREF_SEL_MASK;
		pad_ctrl |= SDHCI_TEGRA_SDMEM_COMP_PADCTRL_VREF_SEL_VAL;
		sdhci_writel(host, pad_ctrl, SDHCI_TEGRA_SDMEM_COMP_PADCTRL);

		tegra_host->pad_calib_required = true;
	}

	tegra_host->ddr_signaling = false;
}

static void tegra_sdhci_configure_cal_pad(struct sdhci_host *host, bool enable)
{
	u32 val;

	/*
	 * Enable or disable the additional I/O pad used by the drive strength
	 * calibration process.
	 */
	val = sdhci_readl(host, SDHCI_TEGRA_SDMEM_COMP_PADCTRL);

	if (enable)
		val |= SDHCI_TEGRA_SDMEM_COMP_PADCTRL_E_INPUT_E_PWRD;
	else
		val &= ~SDHCI_TEGRA_SDMEM_COMP_PADCTRL_E_INPUT_E_PWRD;

	sdhci_writel(host, val, SDHCI_TEGRA_SDMEM_COMP_PADCTRL);

	if (enable)
		usleep_range(1, 2);
}

static void tegra_sdhci_set_pad_autocal_offset(struct sdhci_host *host,
					       u16 pdpu)
{
	u32 reg;

	reg = sdhci_readl(host, SDHCI_TEGRA_AUTO_CAL_CONFIG);
	reg &= ~SDHCI_AUTO_CAL_PDPU_OFFSET_MASK;
	reg |= pdpu;
	sdhci_writel(host, reg, SDHCI_TEGRA_AUTO_CAL_CONFIG);
}

static int tegra_sdhci_set_padctrl(struct sdhci_host *host, int voltage,
				   bool state_drvupdn)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	struct sdhci_tegra_autocal_offsets *offsets =
						&tegra_host->autocal_offsets;
	struct pinctrl_state *pinctrl_drvupdn = NULL;
	int ret = 0;
	u8 drvup = 0, drvdn = 0;
	u32 reg;

	if (!state_drvupdn) {
		/* PADS Drive Strength */
		if (voltage == MMC_SIGNAL_VOLTAGE_180) {
			if (tegra_host->pinctrl_state_1v8_drv) {
				pinctrl_drvupdn =
					tegra_host->pinctrl_state_1v8_drv;
			} else {
				drvup = offsets->pull_up_1v8_timeout;
				drvdn = offsets->pull_down_1v8_timeout;
			}
		} else {
			if (tegra_host->pinctrl_state_3v3_drv) {
				pinctrl_drvupdn =
					tegra_host->pinctrl_state_3v3_drv;
			} else {
				drvup = offsets->pull_up_3v3_timeout;
				drvdn = offsets->pull_down_3v3_timeout;
			}
		}

		if (pinctrl_drvupdn != NULL) {
			ret = pinctrl_select_state(tegra_host->pinctrl_sdmmc,
							pinctrl_drvupdn);
			if (ret < 0)
				dev_err(mmc_dev(host->mmc),
					"failed pads drvupdn, ret: %d\n", ret);
		} else if ((drvup) || (drvdn)) {
			reg = sdhci_readl(host,
					SDHCI_TEGRA_SDMEM_COMP_PADCTRL);
			reg &= ~SDHCI_COMP_PADCTRL_DRVUPDN_OFFSET_MASK;
			reg |= (drvup << 20) | (drvdn << 12);
			sdhci_writel(host, reg,
					SDHCI_TEGRA_SDMEM_COMP_PADCTRL);
		}

	} else {
		/* Dual Voltage PADS Voltage selection */
		if (!tegra_host->pad_control_available)
			return 0;

		if (voltage == MMC_SIGNAL_VOLTAGE_180) {
			ret = pinctrl_select_state(tegra_host->pinctrl_sdmmc,
						tegra_host->pinctrl_state_1v8);
			if (ret < 0)
				dev_err(mmc_dev(host->mmc),
					"setting 1.8V failed, ret: %d\n", ret);
		} else {
			ret = pinctrl_select_state(tegra_host->pinctrl_sdmmc,
						tegra_host->pinctrl_state_3v3);
			if (ret < 0)
				dev_err(mmc_dev(host->mmc),
					"setting 3.3V failed, ret: %d\n", ret);
		}
	}

	return ret;
}

static void tegra_sdhci_pad_autocalib(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	struct sdhci_tegra_autocal_offsets offsets =
			tegra_host->autocal_offsets;
	struct mmc_ios *ios = &host->mmc->ios;
	bool card_clk_enabled;
	u16 pdpu;
	u32 reg;
	int ret;

	switch (ios->timing) {
	case MMC_TIMING_UHS_SDR104:
		pdpu = offsets.pull_down_sdr104 << 8 | offsets.pull_up_sdr104;
		break;
	case MMC_TIMING_MMC_HS400:
		pdpu = offsets.pull_down_hs400 << 8 | offsets.pull_up_hs400;
		break;
	default:
		if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_180)
			pdpu = offsets.pull_down_1v8 << 8 | offsets.pull_up_1v8;
		else
			pdpu = offsets.pull_down_3v3 << 8 | offsets.pull_up_3v3;
	}

	/* Set initial offset before auto-calibration */
	tegra_sdhci_set_pad_autocal_offset(host, pdpu);

	card_clk_enabled = tegra_sdhci_configure_card_clk(host, false);

	tegra_sdhci_configure_cal_pad(host, true);

	reg = sdhci_readl(host, SDHCI_TEGRA_AUTO_CAL_CONFIG);
	reg |= SDHCI_AUTO_CAL_ENABLE | SDHCI_AUTO_CAL_START;
	sdhci_writel(host, reg, SDHCI_TEGRA_AUTO_CAL_CONFIG);

	usleep_range(1, 2);
	/* 10 ms timeout */
	ret = readl_poll_timeout(host->ioaddr + SDHCI_TEGRA_AUTO_CAL_STATUS,
				 reg, !(reg & SDHCI_TEGRA_AUTO_CAL_ACTIVE),
				 1000, 10000);

	tegra_sdhci_configure_cal_pad(host, false);

	tegra_sdhci_configure_card_clk(host, card_clk_enabled);

	if (ret) {
		dev_err(mmc_dev(host->mmc), "Pad autocal timed out\n");

		/* Disable automatic cal and use fixed Drive Strengths */
		reg = sdhci_readl(host, SDHCI_TEGRA_AUTO_CAL_CONFIG);
		reg &= ~SDHCI_AUTO_CAL_ENABLE;
		sdhci_writel(host, reg, SDHCI_TEGRA_AUTO_CAL_CONFIG);

		ret = tegra_sdhci_set_padctrl(host, ios->signal_voltage, false);
		if (ret < 0)
			dev_err(mmc_dev(host->mmc),
				"Setting drive strengths failed: %d\n", ret);
	}
}

static void tegra_sdhci_parse_pad_autocal_dt(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	struct sdhci_tegra_autocal_offsets *autocal =
			&tegra_host->autocal_offsets;
	int err;

	err = device_property_read_u32(host->mmc->parent,
			"nvidia,pad-autocal-pull-up-offset-3v3",
			&autocal->pull_up_3v3);
	if (err)
		autocal->pull_up_3v3 = 0;

	err = device_property_read_u32(host->mmc->parent,
			"nvidia,pad-autocal-pull-down-offset-3v3",
			&autocal->pull_down_3v3);
	if (err)
		autocal->pull_down_3v3 = 0;

	err = device_property_read_u32(host->mmc->parent,
			"nvidia,pad-autocal-pull-up-offset-1v8",
			&autocal->pull_up_1v8);
	if (err)
		autocal->pull_up_1v8 = 0;

	err = device_property_read_u32(host->mmc->parent,
			"nvidia,pad-autocal-pull-down-offset-1v8",
			&autocal->pull_down_1v8);
	if (err)
		autocal->pull_down_1v8 = 0;

	err = device_property_read_u32(host->mmc->parent,
			"nvidia,pad-autocal-pull-up-offset-sdr104",
			&autocal->pull_up_sdr104);
	if (err)
		autocal->pull_up_sdr104 = autocal->pull_up_1v8;

	err = device_property_read_u32(host->mmc->parent,
			"nvidia,pad-autocal-pull-down-offset-sdr104",
			&autocal->pull_down_sdr104);
	if (err)
		autocal->pull_down_sdr104 = autocal->pull_down_1v8;

	err = device_property_read_u32(host->mmc->parent,
			"nvidia,pad-autocal-pull-up-offset-hs400",
			&autocal->pull_up_hs400);
	if (err)
		autocal->pull_up_hs400 = autocal->pull_up_1v8;

	err = device_property_read_u32(host->mmc->parent,
			"nvidia,pad-autocal-pull-down-offset-hs400",
			&autocal->pull_down_hs400);
	if (err)
		autocal->pull_down_hs400 = autocal->pull_down_1v8;

	/*
	 * Different fail-safe drive strength values based on the signaling
	 * voltage are applicable for SoCs supporting 3V3 and 1V8 pad controls.
	 * So, avoid reading below device tree properties for SoCs that don't
	 * have NVQUIRK_NEEDS_PAD_CONTROL.
	 */
	if (!(tegra_host->soc_data->nvquirks & NVQUIRK_NEEDS_PAD_CONTROL))
		return;

	err = device_property_read_u32(host->mmc->parent,
			"nvidia,pad-autocal-pull-up-offset-3v3-timeout",
			&autocal->pull_up_3v3_timeout);
	if (err) {
		if (!IS_ERR(tegra_host->pinctrl_state_3v3) &&
			(tegra_host->pinctrl_state_3v3_drv == NULL))
			pr_warn("%s: Missing autocal timeout 3v3-pad drvs\n",
				mmc_hostname(host->mmc));
		autocal->pull_up_3v3_timeout = 0;
	}

	err = device_property_read_u32(host->mmc->parent,
			"nvidia,pad-autocal-pull-down-offset-3v3-timeout",
			&autocal->pull_down_3v3_timeout);
	if (err) {
		if (!IS_ERR(tegra_host->pinctrl_state_3v3) &&
			(tegra_host->pinctrl_state_3v3_drv == NULL))
			pr_warn("%s: Missing autocal timeout 3v3-pad drvs\n",
				mmc_hostname(host->mmc));
		autocal->pull_down_3v3_timeout = 0;
	}

	err = device_property_read_u32(host->mmc->parent,
			"nvidia,pad-autocal-pull-up-offset-1v8-timeout",
			&autocal->pull_up_1v8_timeout);
	if (err) {
		if (!IS_ERR(tegra_host->pinctrl_state_1v8) &&
			(tegra_host->pinctrl_state_1v8_drv == NULL))
			pr_warn("%s: Missing autocal timeout 1v8-pad drvs\n",
				mmc_hostname(host->mmc));
		autocal->pull_up_1v8_timeout = 0;
	}

	err = device_property_read_u32(host->mmc->parent,
			"nvidia,pad-autocal-pull-down-offset-1v8-timeout",
			&autocal->pull_down_1v8_timeout);
	if (err) {
		if (!IS_ERR(tegra_host->pinctrl_state_1v8) &&
			(tegra_host->pinctrl_state_1v8_drv == NULL))
			pr_warn("%s: Missing autocal timeout 1v8-pad drvs\n",
				mmc_hostname(host->mmc));
		autocal->pull_down_1v8_timeout = 0;
	}
}

static void tegra_sdhci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	ktime_t since_calib = ktime_sub(ktime_get(), tegra_host->last_calib);

	/* 100 ms calibration interval is specified in the TRM */
	if (ktime_to_ms(since_calib) > 100) {
		tegra_sdhci_pad_autocalib(host);
		tegra_host->last_calib = ktime_get();
	}

	sdhci_request(mmc, mrq);
}

static void tegra_sdhci_parse_tap_and_trim(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	int err;

	err = device_property_read_u32(host->mmc->parent, "nvidia,default-tap",
				       &tegra_host->default_tap);
	if (err)
		tegra_host->default_tap = 0;

	err = device_property_read_u32(host->mmc->parent, "nvidia,default-trim",
				       &tegra_host->default_trim);
	if (err)
		tegra_host->default_trim = 0;

	err = device_property_read_u32(host->mmc->parent, "nvidia,dqs-trim",
				       &tegra_host->dqs_trim);
	if (err)
		tegra_host->dqs_trim = 0x11;
}

static void tegra_sdhci_parse_dt(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);

	if (device_property_read_bool(host->mmc->parent, "supports-cqe"))
		tegra_host->enable_hwcq = true;
	else
		tegra_host->enable_hwcq = false;

	tegra_sdhci_parse_pad_autocal_dt(host);
	tegra_sdhci_parse_tap_and_trim(host);
}

static void tegra_sdhci_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	unsigned long host_clk;

	if (!clock)
		return sdhci_set_clock(host, clock);

	/*
	 * In DDR50/52 modes the Tegra SDHCI controllers require the SDHCI
	 * divider to be configured to divided the host clock by two. The SDHCI
	 * clock divider is calculated as part of sdhci_set_clock() by
	 * sdhci_calc_clk(). The divider is calculated from host->max_clk and
	 * the requested clock rate.
	 *
	 * By setting the host->max_clk to clock * 2 the divider calculation
	 * will always result in the correct value for DDR50/52 modes,
	 * regardless of clock rate rounding, which may happen if the value
	 * from clk_get_rate() is used.
	 */
	host_clk = tegra_host->ddr_signaling ? clock * 2 : clock;
	clk_set_rate(pltfm_host->clk, host_clk);
	tegra_host->curr_clk_rate = host_clk;
	if (tegra_host->ddr_signaling)
		host->max_clk = host_clk;
	else
		host->max_clk = clk_get_rate(pltfm_host->clk);

	sdhci_set_clock(host, clock);

	if (tegra_host->pad_calib_required) {
		tegra_sdhci_pad_autocalib(host);
		tegra_host->pad_calib_required = false;
	}
}

static unsigned int tegra_sdhci_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return clk_round_rate(pltfm_host->clk, UINT_MAX);
}

static void tegra_sdhci_set_dqs_trim(struct sdhci_host *host, u8 trim)
{
	u32 val;

	val = sdhci_readl(host, SDHCI_TEGRA_VENDOR_CAP_OVERRIDES);
	val &= ~SDHCI_TEGRA_CAP_OVERRIDES_DQS_TRIM_MASK;
	val |= trim << SDHCI_TEGRA_CAP_OVERRIDES_DQS_TRIM_SHIFT;
	sdhci_writel(host, val, SDHCI_TEGRA_VENDOR_CAP_OVERRIDES);
}

static void tegra_sdhci_hs400_dll_cal(struct sdhci_host *host)
{
	u32 reg;
	int err;

	reg = sdhci_readl(host, SDHCI_TEGRA_VENDOR_DLLCAL_CFG);
	reg |= SDHCI_TEGRA_DLLCAL_CALIBRATE;
	sdhci_writel(host, reg, SDHCI_TEGRA_VENDOR_DLLCAL_CFG);

	/* 1 ms sleep, 5 ms timeout */
	err = readl_poll_timeout(host->ioaddr + SDHCI_TEGRA_VENDOR_DLLCAL_STA,
				 reg, !(reg & SDHCI_TEGRA_DLLCAL_STA_ACTIVE),
				 1000, 5000);
	if (err)
		dev_err(mmc_dev(host->mmc),
			"HS400 delay line calibration timed out\n");
}

static void tegra_sdhci_tap_correction(struct sdhci_host *host, u8 thd_up,
				       u8 thd_low, u8 fixed_tap)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	u32 val, tun_status;
	u8 word, bit, edge1, tap, window;
	bool tap_result;
	bool start_fail = false;
	bool start_pass = false;
	bool end_pass = false;
	bool first_fail = false;
	bool first_pass = false;
	u8 start_pass_tap = 0;
	u8 end_pass_tap = 0;
	u8 first_fail_tap = 0;
	u8 first_pass_tap = 0;
	u8 total_tuning_words = host->tuning_loop_count / TUNING_WORD_BIT_SIZE;

	/*
	 * Read auto-tuned results and extract good valid passing window by
	 * filtering out un-wanted bubble/partial/merged windows.
	 */
	for (word = 0; word < total_tuning_words; word++) {
		val = sdhci_readl(host, SDHCI_VNDR_TUN_CTRL0_0);
		val &= ~SDHCI_VNDR_TUN_CTRL0_TUN_WORD_SEL_MASK;
		val |= word;
		sdhci_writel(host, val, SDHCI_VNDR_TUN_CTRL0_0);
		tun_status = sdhci_readl(host, SDHCI_TEGRA_VNDR_TUN_STATUS0);
		bit = 0;
		while (bit < TUNING_WORD_BIT_SIZE) {
			tap = word * TUNING_WORD_BIT_SIZE + bit;
			tap_result = tun_status & (1 << bit);
			if (!tap_result && !start_fail) {
				start_fail = true;
				if (!first_fail) {
					first_fail_tap = tap;
					first_fail = true;
				}

			} else if (tap_result && start_fail && !start_pass) {
				start_pass_tap = tap;
				start_pass = true;
				if (!first_pass) {
					first_pass_tap = tap;
					first_pass = true;
				}

			} else if (!tap_result && start_fail && start_pass &&
				   !end_pass) {
				end_pass_tap = tap - 1;
				end_pass = true;
			} else if (tap_result && start_pass && start_fail &&
				   end_pass) {
				window = end_pass_tap - start_pass_tap;
				/* discard merged window and bubble window */
				if (window >= thd_up || window < thd_low) {
					start_pass_tap = tap;
					end_pass = false;
				} else {
					/* set tap at middle of valid window */
					tap = start_pass_tap + window / 2;
					tegra_host->tuned_tap_delay = tap;
					return;
				}
			}

			bit++;
		}
	}

	if (!first_fail) {
		WARN(1, "no edge detected, continue with hw tuned delay.\n");
	} else if (first_pass) {
		/* set tap location at fixed tap relative to the first edge */
		edge1 = first_fail_tap + (first_pass_tap - first_fail_tap) / 2;
		if (edge1 - 1 > fixed_tap)
			tegra_host->tuned_tap_delay = edge1 - fixed_tap;
		else
			tegra_host->tuned_tap_delay = edge1 + fixed_tap;
	}
}

static void tegra_sdhci_post_tuning(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_tegra_soc_data *soc_data = tegra_host->soc_data;
	u32 avg_tap_dly, val, min_tap_dly, max_tap_dly;
	u8 fixed_tap, start_tap, end_tap, window_width;
	u8 thdupper, thdlower;
	u8 num_iter;
	u32 clk_rate_mhz, period_ps, bestcase, worstcase;

	/* retain HW tuned tap to use incase if no correction is needed */
	val = sdhci_readl(host, SDHCI_TEGRA_VENDOR_CLOCK_CTRL);
	tegra_host->tuned_tap_delay = (val & SDHCI_CLOCK_CTRL_TAP_MASK) >>
				      SDHCI_CLOCK_CTRL_TAP_SHIFT;
	if (soc_data->min_tap_delay && soc_data->max_tap_delay) {
		min_tap_dly = soc_data->min_tap_delay;
		max_tap_dly = soc_data->max_tap_delay;
		clk_rate_mhz = tegra_host->curr_clk_rate / USEC_PER_SEC;
		period_ps = USEC_PER_SEC / clk_rate_mhz;
		bestcase = period_ps / min_tap_dly;
		worstcase = period_ps / max_tap_dly;
		/*
		 * Upper and Lower bound thresholds used to detect merged and
		 * bubble windows
		 */
		thdupper = (2 * worstcase + bestcase) / 2;
		thdlower = worstcase / 4;
		/*
		 * fixed tap is used when HW tuning result contains single edge
		 * and tap is set at fixed tap delay relative to the first edge
		 */
		avg_tap_dly = (period_ps * 2) / (min_tap_dly + max_tap_dly);
		fixed_tap = avg_tap_dly / 2;

		val = sdhci_readl(host, SDHCI_TEGRA_VNDR_TUN_STATUS1);
		start_tap = val & SDHCI_TEGRA_VNDR_TUN_STATUS1_TAP_MASK;
		end_tap = (val >> SDHCI_TEGRA_VNDR_TUN_STATUS1_END_TAP_SHIFT) &
			  SDHCI_TEGRA_VNDR_TUN_STATUS1_TAP_MASK;
		window_width = end_tap - start_tap;
		num_iter = host->tuning_loop_count;
		/*
		 * partial window includes edges of the tuning range.
		 * merged window includes more taps so window width is higher
		 * than upper threshold.
		 */
		if (start_tap == 0 || (end_tap == (num_iter - 1)) ||
		    (end_tap == num_iter - 2) || window_width >= thdupper) {
			pr_debug("%s: Apply tuning correction\n",
				 mmc_hostname(host->mmc));
			tegra_sdhci_tap_correction(host, thdupper, thdlower,
						   fixed_tap);
		}
	}

	tegra_sdhci_set_tap(host, tegra_host->tuned_tap_delay);
}

static int tegra_sdhci_execute_hw_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct sdhci_host *host = mmc_priv(mmc);
	int err;

	err = sdhci_execute_tuning(mmc, opcode);
	if (!err && !host->tuning_err)
		tegra_sdhci_post_tuning(host);

	return err;
}

static void tegra_sdhci_set_uhs_signaling(struct sdhci_host *host,
					  unsigned timing)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	bool set_default_tap = false;
	bool set_dqs_trim = false;
	bool do_hs400_dll_cal = false;
	u8 iter = TRIES_256;
	u32 val;

	tegra_host->ddr_signaling = false;
	switch (timing) {
	case MMC_TIMING_UHS_SDR50:
		break;
	case MMC_TIMING_UHS_SDR104:
	case MMC_TIMING_MMC_HS200:
		/* Don't set default tap on tunable modes. */
		iter = TRIES_128;
		break;
	case MMC_TIMING_MMC_HS400:
		set_dqs_trim = true;
		do_hs400_dll_cal = true;
		iter = TRIES_128;
		break;
	case MMC_TIMING_MMC_DDR52:
	case MMC_TIMING_UHS_DDR50:
		tegra_host->ddr_signaling = true;
		set_default_tap = true;
		break;
	default:
		set_default_tap = true;
		break;
	}

	val = sdhci_readl(host, SDHCI_VNDR_TUN_CTRL0_0);
	val &= ~(SDHCI_VNDR_TUN_CTRL0_TUN_ITER_MASK |
		 SDHCI_VNDR_TUN_CTRL0_START_TAP_VAL_MASK |
		 SDHCI_VNDR_TUN_CTRL0_MUL_M_MASK);
	val |= (iter << SDHCI_VNDR_TUN_CTRL0_TUN_ITER_SHIFT |
		0 << SDHCI_VNDR_TUN_CTRL0_START_TAP_VAL_SHIFT |
		1 << SDHCI_VNDR_TUN_CTRL0_MUL_M_SHIFT);
	sdhci_writel(host, val, SDHCI_VNDR_TUN_CTRL0_0);
	sdhci_writel(host, 0, SDHCI_TEGRA_VNDR_TUN_CTRL1_0);

	host->tuning_loop_count = (iter == TRIES_128) ? 128 : 256;

	sdhci_set_uhs_signaling(host, timing);

	tegra_sdhci_pad_autocalib(host);

	if (tegra_host->tuned_tap_delay && !set_default_tap)
		tegra_sdhci_set_tap(host, tegra_host->tuned_tap_delay);
	else
		tegra_sdhci_set_tap(host, tegra_host->default_tap);

	if (set_dqs_trim)
		tegra_sdhci_set_dqs_trim(host, tegra_host->dqs_trim);

	if (do_hs400_dll_cal)
		tegra_sdhci_hs400_dll_cal(host);
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

static int sdhci_tegra_start_signal_voltage_switch(struct mmc_host *mmc,
						   struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	int ret = 0;

	if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_330) {
		ret = tegra_sdhci_set_padctrl(host, ios->signal_voltage, true);
		if (ret < 0)
			return ret;
		ret = sdhci_start_signal_voltage_switch(mmc, ios);
	} else if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_180) {
		ret = sdhci_start_signal_voltage_switch(mmc, ios);
		if (ret < 0)
			return ret;
		ret = tegra_sdhci_set_padctrl(host, ios->signal_voltage, true);
	}

	if (tegra_host->pad_calib_required)
		tegra_sdhci_pad_autocalib(host);

	return ret;
}

static int tegra_sdhci_init_pinctrl_info(struct device *dev,
					 struct sdhci_tegra *tegra_host)
{
	tegra_host->pinctrl_sdmmc = devm_pinctrl_get(dev);
	if (IS_ERR(tegra_host->pinctrl_sdmmc)) {
		dev_dbg(dev, "No pinctrl info, err: %ld\n",
			PTR_ERR(tegra_host->pinctrl_sdmmc));
		return -1;
	}

	tegra_host->pinctrl_state_1v8_drv = pinctrl_lookup_state(
				tegra_host->pinctrl_sdmmc, "sdmmc-1v8-drv");
	if (IS_ERR(tegra_host->pinctrl_state_1v8_drv)) {
		if (PTR_ERR(tegra_host->pinctrl_state_1v8_drv) == -ENODEV)
			tegra_host->pinctrl_state_1v8_drv = NULL;
	}

	tegra_host->pinctrl_state_3v3_drv = pinctrl_lookup_state(
				tegra_host->pinctrl_sdmmc, "sdmmc-3v3-drv");
	if (IS_ERR(tegra_host->pinctrl_state_3v3_drv)) {
		if (PTR_ERR(tegra_host->pinctrl_state_3v3_drv) == -ENODEV)
			tegra_host->pinctrl_state_3v3_drv = NULL;
	}

	tegra_host->pinctrl_state_3v3 =
		pinctrl_lookup_state(tegra_host->pinctrl_sdmmc, "sdmmc-3v3");
	if (IS_ERR(tegra_host->pinctrl_state_3v3)) {
		dev_warn(dev, "Missing 3.3V pad state, err: %ld\n",
			 PTR_ERR(tegra_host->pinctrl_state_3v3));
		return -1;
	}

	tegra_host->pinctrl_state_1v8 =
		pinctrl_lookup_state(tegra_host->pinctrl_sdmmc, "sdmmc-1v8");
	if (IS_ERR(tegra_host->pinctrl_state_1v8)) {
		dev_warn(dev, "Missing 1.8V pad state, err: %ld\n",
			 PTR_ERR(tegra_host->pinctrl_state_1v8));
		return -1;
	}

	tegra_host->pad_control_available = true;

	return 0;
}

static void tegra_sdhci_voltage_switch(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_tegra_soc_data *soc_data = tegra_host->soc_data;

	if (soc_data->nvquirks & NVQUIRK_HAS_PADCALIB)
		tegra_host->pad_calib_required = true;
}

static void tegra_cqhci_writel(struct cqhci_host *cq_host, u32 val, int reg)
{
	struct mmc_host *mmc = cq_host->mmc;
	u8 ctrl;
	ktime_t timeout;
	bool timed_out;

	/*
	 * During CQE resume/unhalt, CQHCI driver unhalts CQE prior to
	 * cqhci_host_ops enable where SDHCI DMA and BLOCK_SIZE registers need
	 * to be re-configured.
	 * Tegra CQHCI/SDHCI prevents write access to block size register when
	 * CQE is unhalted. So handling CQE resume sequence here to configure
	 * SDHCI block registers prior to exiting CQE halt state.
	 */
	if (reg == CQHCI_CTL && !(val & CQHCI_HALT) &&
	    cqhci_readl(cq_host, CQHCI_CTL) & CQHCI_HALT) {
		sdhci_cqe_enable(mmc);
		writel(val, cq_host->mmio + reg);
		timeout = ktime_add_us(ktime_get(), 50);
		while (1) {
			timed_out = ktime_compare(ktime_get(), timeout) > 0;
			ctrl = cqhci_readl(cq_host, CQHCI_CTL);
			if (!(ctrl & CQHCI_HALT) || timed_out)
				break;
		}
		/*
		 * CQE usually resumes very quick, but incase if Tegra CQE
		 * doesn't resume retry unhalt.
		 */
		if (timed_out)
			writel(val, cq_host->mmio + reg);
	} else {
		writel(val, cq_host->mmio + reg);
	}
}

static void sdhci_tegra_update_dcmd_desc(struct mmc_host *mmc,
					 struct mmc_request *mrq, u64 *data)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(mmc_priv(mmc));
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_tegra_soc_data *soc_data = tegra_host->soc_data;

	if (soc_data->nvquirks & NVQUIRK_CQHCI_DCMD_R1B_CMD_TIMING &&
	    mrq->cmd->flags & MMC_RSP_R1B)
		*data |= CQHCI_CMD_TIMING(1);
}

static void sdhci_tegra_cqe_enable(struct mmc_host *mmc)
{
	struct cqhci_host *cq_host = mmc->cqe_private;
	u32 val;

	/*
	 * Tegra CQHCI/SDMMC design prevents write access to sdhci block size
	 * register when CQE is enabled and unhalted.
	 * CQHCI driver enables CQE prior to activation, so disable CQE before
	 * programming block size in sdhci controller and enable it back.
	 */
	if (!cq_host->activated) {
		val = cqhci_readl(cq_host, CQHCI_CFG);
		if (val & CQHCI_ENABLE)
			cqhci_writel(cq_host, (val & ~CQHCI_ENABLE),
				     CQHCI_CFG);
		sdhci_cqe_enable(mmc);
		if (val & CQHCI_ENABLE)
			cqhci_writel(cq_host, val, CQHCI_CFG);
	}

	/*
	 * CMD CRC errors are seen sometimes with some eMMC devices when status
	 * command is sent during transfer of last data block which is the
	 * default case as send status command block counter (CBC) is 1.
	 * Recommended fix to set CBC to 0 allowing send status command only
	 * when data lines are idle.
	 */
	val = cqhci_readl(cq_host, CQHCI_SSC1);
	val &= ~CQHCI_SSC1_CBC_MASK;
	cqhci_writel(cq_host, val, CQHCI_SSC1);
}

static void sdhci_tegra_dumpregs(struct mmc_host *mmc)
{
	sdhci_dumpregs(mmc_priv(mmc));
}

static u32 sdhci_tegra_cqhci_irq(struct sdhci_host *host, u32 intmask)
{
	int cmd_error = 0;
	int data_error = 0;

	if (!sdhci_cqe_irq(host, intmask, &cmd_error, &data_error))
		return intmask;

	cqhci_irq(host->mmc, intmask, cmd_error, data_error);

	return 0;
}

static void tegra_sdhci_set_timeout(struct sdhci_host *host,
				    struct mmc_command *cmd)
{
	u32 val;

	/*
	 * HW busy detection timeout is based on programmed data timeout
	 * counter and maximum supported timeout is 11s which may not be
	 * enough for long operations like cache flush, sleep awake, erase.
	 *
	 * ERASE_TIMEOUT_LIMIT bit of VENDOR_MISC_CTRL register allows
	 * host controller to wait for busy state until the card is busy
	 * without HW timeout.
	 *
	 * So, use infinite busy wait mode for operations that may take
	 * more than maximum HW busy timeout of 11s otherwise use finite
	 * busy wait mode.
	 */
	val = sdhci_readl(host, SDHCI_TEGRA_VENDOR_MISC_CTRL);
	if (cmd && cmd->busy_timeout >= 11 * HZ)
		val |= SDHCI_MISC_CTRL_ERASE_TIMEOUT_LIMIT;
	else
		val &= ~SDHCI_MISC_CTRL_ERASE_TIMEOUT_LIMIT;
	sdhci_writel(host, val, SDHCI_TEGRA_VENDOR_MISC_CTRL);

	__sdhci_set_timeout(host, cmd);
}

static const struct cqhci_host_ops sdhci_tegra_cqhci_ops = {
	.write_l    = tegra_cqhci_writel,
	.enable	= sdhci_tegra_cqe_enable,
	.disable = sdhci_cqe_disable,
	.dumpregs = sdhci_tegra_dumpregs,
	.update_dcmd_desc = sdhci_tegra_update_dcmd_desc,
};

static int tegra_sdhci_set_dma_mask(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *platform = sdhci_priv(host);
	struct sdhci_tegra *tegra = sdhci_pltfm_priv(platform);
	const struct sdhci_tegra_soc_data *soc = tegra->soc_data;
	struct device *dev = mmc_dev(host->mmc);

	if (soc->dma_mask)
		return dma_set_mask_and_coherent(dev, soc->dma_mask);

	return 0;
}

static const struct sdhci_ops tegra_sdhci_ops = {
	.get_ro     = tegra_sdhci_get_ro,
	.read_w     = tegra_sdhci_readw,
	.write_l    = tegra_sdhci_writel,
	.set_clock  = tegra_sdhci_set_clock,
	.set_dma_mask = tegra_sdhci_set_dma_mask,
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
	.dma_mask = DMA_BIT_MASK(32),
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
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
		   SDHCI_QUIRK2_BROKEN_HS200 |
		   /*
		    * Auto-CMD23 leads to "Got command interrupt 0x00010000 even
		    * though no command operation was in progress."
		    *
		    * The exact reason is unknown, as the same hardware seems
		    * to support Auto CMD23 on a downstream 3.1 kernel.
		    */
		   SDHCI_QUIRK2_ACMD23_BROKEN,
	.ops  = &tegra_sdhci_ops,
};

static const struct sdhci_tegra_soc_data soc_data_tegra30 = {
	.pdata = &sdhci_tegra30_pdata,
	.dma_mask = DMA_BIT_MASK(32),
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
	.set_dma_mask = tegra_sdhci_set_dma_mask,
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
	.dma_mask = DMA_BIT_MASK(32),
};

static const struct sdhci_pltfm_data sdhci_tegra124_pdata = {
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		  SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		  SDHCI_QUIRK_SINGLE_POWER_WRITE |
		  SDHCI_QUIRK_NO_HISPD_BIT |
		  SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
	.ops  = &tegra114_sdhci_ops,
};

static const struct sdhci_tegra_soc_data soc_data_tegra124 = {
	.pdata = &sdhci_tegra124_pdata,
	.dma_mask = DMA_BIT_MASK(34),
};

static const struct sdhci_ops tegra210_sdhci_ops = {
	.get_ro     = tegra_sdhci_get_ro,
	.read_w     = tegra_sdhci_readw,
	.write_w    = tegra210_sdhci_writew,
	.write_l    = tegra_sdhci_writel,
	.set_clock  = tegra_sdhci_set_clock,
	.set_dma_mask = tegra_sdhci_set_dma_mask,
	.set_bus_width = sdhci_set_bus_width,
	.reset      = tegra_sdhci_reset,
	.set_uhs_signaling = tegra_sdhci_set_uhs_signaling,
	.voltage_switch = tegra_sdhci_voltage_switch,
	.get_max_clock = tegra_sdhci_get_max_clock,
	.set_timeout = tegra_sdhci_set_timeout,
};

static const struct sdhci_pltfm_data sdhci_tegra210_pdata = {
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		  SDHCI_QUIRK_SINGLE_POWER_WRITE |
		  SDHCI_QUIRK_NO_HISPD_BIT |
		  SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
	.ops  = &tegra210_sdhci_ops,
};

static const struct sdhci_tegra_soc_data soc_data_tegra210 = {
	.pdata = &sdhci_tegra210_pdata,
	.dma_mask = DMA_BIT_MASK(34),
	.nvquirks = NVQUIRK_NEEDS_PAD_CONTROL |
		    NVQUIRK_HAS_PADCALIB |
		    NVQUIRK_DIS_CARD_CLK_CONFIG_TAP |
		    NVQUIRK_ENABLE_SDR50 |
		    NVQUIRK_ENABLE_SDR104 |
		    NVQUIRK_HAS_TMCLK,
	.min_tap_delay = 106,
	.max_tap_delay = 185,
};

static const struct sdhci_ops tegra186_sdhci_ops = {
	.get_ro     = tegra_sdhci_get_ro,
	.read_w     = tegra_sdhci_readw,
	.write_l    = tegra_sdhci_writel,
	.set_clock  = tegra_sdhci_set_clock,
	.set_dma_mask = tegra_sdhci_set_dma_mask,
	.set_bus_width = sdhci_set_bus_width,
	.reset      = tegra_sdhci_reset,
	.set_uhs_signaling = tegra_sdhci_set_uhs_signaling,
	.voltage_switch = tegra_sdhci_voltage_switch,
	.get_max_clock = tegra_sdhci_get_max_clock,
	.irq = sdhci_tegra_cqhci_irq,
	.set_timeout = tegra_sdhci_set_timeout,
};

static const struct sdhci_pltfm_data sdhci_tegra186_pdata = {
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		  SDHCI_QUIRK_SINGLE_POWER_WRITE |
		  SDHCI_QUIRK_NO_HISPD_BIT |
		  SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
	.ops  = &tegra186_sdhci_ops,
};

static const struct sdhci_tegra_soc_data soc_data_tegra186 = {
	.pdata = &sdhci_tegra186_pdata,
	.dma_mask = DMA_BIT_MASK(40),
	.nvquirks = NVQUIRK_NEEDS_PAD_CONTROL |
		    NVQUIRK_HAS_PADCALIB |
		    NVQUIRK_DIS_CARD_CLK_CONFIG_TAP |
		    NVQUIRK_ENABLE_SDR50 |
		    NVQUIRK_ENABLE_SDR104 |
		    NVQUIRK_HAS_TMCLK |
		    NVQUIRK_CQHCI_DCMD_R1B_CMD_TIMING,
	.min_tap_delay = 84,
	.max_tap_delay = 136,
};

static const struct sdhci_tegra_soc_data soc_data_tegra194 = {
	.pdata = &sdhci_tegra186_pdata,
	.dma_mask = DMA_BIT_MASK(39),
	.nvquirks = NVQUIRK_NEEDS_PAD_CONTROL |
		    NVQUIRK_HAS_PADCALIB |
		    NVQUIRK_DIS_CARD_CLK_CONFIG_TAP |
		    NVQUIRK_ENABLE_SDR50 |
		    NVQUIRK_ENABLE_SDR104 |
		    NVQUIRK_HAS_TMCLK,
	.min_tap_delay = 96,
	.max_tap_delay = 139,
};

static const struct of_device_id sdhci_tegra_dt_match[] = {
	{ .compatible = "nvidia,tegra194-sdhci", .data = &soc_data_tegra194 },
	{ .compatible = "nvidia,tegra186-sdhci", .data = &soc_data_tegra186 },
	{ .compatible = "nvidia,tegra210-sdhci", .data = &soc_data_tegra210 },
	{ .compatible = "nvidia,tegra124-sdhci", .data = &soc_data_tegra124 },
	{ .compatible = "nvidia,tegra114-sdhci", .data = &soc_data_tegra114 },
	{ .compatible = "nvidia,tegra30-sdhci", .data = &soc_data_tegra30 },
	{ .compatible = "nvidia,tegra20-sdhci", .data = &soc_data_tegra20 },
	{}
};
MODULE_DEVICE_TABLE(of, sdhci_tegra_dt_match);

static int sdhci_tegra_add_host(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_tegra *tegra_host = sdhci_pltfm_priv(pltfm_host);
	struct cqhci_host *cq_host;
	bool dma64;
	int ret;

	if (!tegra_host->enable_hwcq)
		return sdhci_add_host(host);

	sdhci_enable_v4_mode(host);

	ret = sdhci_setup_host(host);
	if (ret)
		return ret;

	host->mmc->caps2 |= MMC_CAP2_CQE | MMC_CAP2_CQE_DCMD;

	cq_host = devm_kzalloc(host->mmc->parent,
				sizeof(*cq_host), GFP_KERNEL);
	if (!cq_host) {
		ret = -ENOMEM;
		goto cleanup;
	}

	cq_host->mmio = host->ioaddr + SDHCI_TEGRA_CQE_BASE_ADDR;
	cq_host->ops = &sdhci_tegra_cqhci_ops;

	dma64 = host->flags & SDHCI_USE_64_BIT_DMA;
	if (dma64)
		cq_host->caps |= CQHCI_TASK_DESC_SZ_128;

	ret = cqhci_init(cq_host, host->mmc, dma64);
	if (ret)
		goto cleanup;

	ret = __sdhci_add_host(host);
	if (ret)
		goto cleanup;

	return 0;

cleanup:
	sdhci_cleanup_host(host);
	return ret;
}

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
	tegra_host->pad_control_available = false;
	tegra_host->soc_data = soc_data;

	if (soc_data->nvquirks & NVQUIRK_NEEDS_PAD_CONTROL) {
		rc = tegra_sdhci_init_pinctrl_info(&pdev->dev, tegra_host);
		if (rc == 0)
			host->mmc_host_ops.start_signal_voltage_switch =
				sdhci_tegra_start_signal_voltage_switch;
	}

	/* Hook to periodically rerun pad calibration */
	if (soc_data->nvquirks & NVQUIRK_HAS_PADCALIB)
		host->mmc_host_ops.request = tegra_sdhci_request;

	host->mmc_host_ops.hs400_enhanced_strobe =
			tegra_sdhci_hs400_enhanced_strobe;

	if (!host->ops->platform_execute_tuning)
		host->mmc_host_ops.execute_tuning =
				tegra_sdhci_execute_hw_tuning;

	rc = mmc_of_parse(host->mmc);
	if (rc)
		goto err_parse_dt;

	if (tegra_host->soc_data->nvquirks & NVQUIRK_ENABLE_DDR50)
		host->mmc->caps |= MMC_CAP_1_8V_DDR;

	/* HW busy detection is supported, but R1B responses are required. */
	host->mmc->caps |= MMC_CAP_WAIT_WHILE_BUSY | MMC_CAP_NEED_RSP_BUSY;

	tegra_sdhci_parse_dt(host);

	tegra_host->power_gpio = devm_gpiod_get_optional(&pdev->dev, "power",
							 GPIOD_OUT_HIGH);
	if (IS_ERR(tegra_host->power_gpio)) {
		rc = PTR_ERR(tegra_host->power_gpio);
		goto err_power_req;
	}

	/*
	 * Tegra210 has a separate SDMMC_LEGACY_TM clock used for host
	 * timeout clock and SW can choose TMCLK or SDCLK for hardware
	 * data timeout through the bit USE_TMCLK_FOR_DATA_TIMEOUT of
	 * the register SDHCI_TEGRA_VENDOR_SYS_SW_CTRL.
	 *
	 * USE_TMCLK_FOR_DATA_TIMEOUT bit default is set to 1 and SDMMC uses
	 * 12Mhz TMCLK which is advertised in host capability register.
	 * With TMCLK of 12Mhz provides maximum data timeout period that can
	 * be achieved is 11s better than using SDCLK for data timeout.
	 *
	 * So, TMCLK is set to 12Mhz and kept enabled all the time on SoC's
	 * supporting separate TMCLK.
	 */

	if (soc_data->nvquirks & NVQUIRK_HAS_TMCLK) {
		clk = devm_clk_get(&pdev->dev, "tmclk");
		if (IS_ERR(clk)) {
			rc = PTR_ERR(clk);
			if (rc == -EPROBE_DEFER)
				goto err_power_req;

			dev_warn(&pdev->dev, "failed to get tmclk: %d\n", rc);
			clk = NULL;
		}

		clk_set_rate(clk, 12000000);
		rc = clk_prepare_enable(clk);
		if (rc) {
			dev_err(&pdev->dev,
				"failed to enable tmclk: %d\n", rc);
			goto err_power_req;
		}

		tegra_host->tmclk = clk;
	}

	clk = devm_clk_get(mmc_dev(host->mmc), NULL);
	if (IS_ERR(clk)) {
		rc = dev_err_probe(&pdev->dev, PTR_ERR(clk),
				   "failed to get clock\n");
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

	rc = sdhci_tegra_add_host(host);
	if (rc)
		goto err_add_host;

	return 0;

err_add_host:
	reset_control_assert(tegra_host->rst);
err_rst_get:
	clk_disable_unprepare(pltfm_host->clk);
err_clk_get:
	clk_disable_unprepare(tegra_host->tmclk);
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
	clk_disable_unprepare(tegra_host->tmclk);

	sdhci_pltfm_free(pdev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int __maybe_unused sdhci_tegra_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	int ret;

	if (host->mmc->caps2 & MMC_CAP2_CQE) {
		ret = cqhci_suspend(host->mmc);
		if (ret)
			return ret;
	}

	ret = sdhci_suspend_host(host);
	if (ret) {
		cqhci_resume(host->mmc);
		return ret;
	}

	clk_disable_unprepare(pltfm_host->clk);
	return 0;
}

static int __maybe_unused sdhci_tegra_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	int ret;

	ret = clk_prepare_enable(pltfm_host->clk);
	if (ret)
		return ret;

	ret = sdhci_resume_host(host);
	if (ret)
		goto disable_clk;

	if (host->mmc->caps2 & MMC_CAP2_CQE) {
		ret = cqhci_resume(host->mmc);
		if (ret)
			goto suspend_host;
	}

	return 0;

suspend_host:
	sdhci_suspend_host(host);
disable_clk:
	clk_disable_unprepare(pltfm_host->clk);
	return ret;
}
#endif

static SIMPLE_DEV_PM_OPS(sdhci_tegra_dev_pm_ops, sdhci_tegra_suspend,
			 sdhci_tegra_resume);

static struct platform_driver sdhci_tegra_driver = {
	.driver		= {
		.name	= "sdhci-tegra",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = sdhci_tegra_dt_match,
		.pm	= &sdhci_tegra_dev_pm_ops,
	},
	.probe		= sdhci_tegra_probe,
	.remove		= sdhci_tegra_remove,
};

module_platform_driver(sdhci_tegra_driver);

MODULE_DESCRIPTION("SDHCI driver for Tegra");
MODULE_AUTHOR("Google, Inc.");
MODULE_LICENSE("GPL v2");
