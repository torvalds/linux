// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/firmware/xlnx-zynqmp.h>

#include "cqhci.h"
#include "sdhci-cqhci.h"
#include "sdhci-pltfm.h"

#define SDHCI_ARASAN_VENDOR_REGISTER	0x78

#define SDHCI_ARASAN_ITAPDLY_REGISTER	0xF0F8
#define SDHCI_ARASAN_ITAPDLY_SEL_MASK	0xFF

#define SDHCI_ARASAN_OTAPDLY_REGISTER	0xF0FC
#define SDHCI_ARASAN_OTAPDLY_SEL_MASK	0x3F

#define SDHCI_ARASAN_CQE_BASE_ADDR	0x200
#define VENDOR_ENHANCED_STROBE		BIT(0)

#define PHY_CLK_TOO_SLOW_HZ		400000

#define SDHCI_ITAPDLY_CHGWIN		0x200
#define SDHCI_ITAPDLY_ENABLE		0x100
#define SDHCI_OTAPDLY_ENABLE		0x40

/* Default settings for ZynqMP Clock Phases */
#define ZYNQMP_ICLK_PHASE {0, 63, 63, 0, 63,  0,   0, 183, 54,  0, 0}
#define ZYNQMP_OCLK_PHASE {0, 72, 60, 0, 60, 72, 135, 48, 72, 135, 0}

#define VERSAL_ICLK_PHASE {0, 132, 132, 0, 132, 0, 0, 162, 90, 0, 0}
#define VERSAL_OCLK_PHASE {0,  60, 48, 0, 48, 72, 90, 36, 60, 90, 0}

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
 * @baseclkfreq:	Where to find corecfg_baseclkfreq
 * @clockmultiplier:	Where to find corecfg_clockmultiplier
 * @support64b:		Where to find SUPPORT64B bit
 * @hiword_update:	If true, use HIWORD_UPDATE to access the syscon
 *
 * It's up to the licensee of the Arsan IP block to make these available
 * somewhere if needed.  Presumably these will be scattered somewhere that's
 * accessible via the syscon API.
 */
struct sdhci_arasan_soc_ctl_map {
	struct sdhci_arasan_soc_ctl_field	baseclkfreq;
	struct sdhci_arasan_soc_ctl_field	clockmultiplier;
	struct sdhci_arasan_soc_ctl_field	support64b;
	bool					hiword_update;
};

/**
 * struct sdhci_arasan_clk_ops - Clock Operations for Arasan SD controller
 *
 * @sdcardclk_ops:	The output clock related operations
 * @sampleclk_ops:	The sample clock related operations
 */
struct sdhci_arasan_clk_ops {
	const struct clk_ops *sdcardclk_ops;
	const struct clk_ops *sampleclk_ops;
};

/**
 * struct sdhci_arasan_clk_data - Arasan Controller Clock Data.
 *
 * @sdcardclk_hw:	Struct for the clock we might provide to a PHY.
 * @sdcardclk:		Pointer to normal 'struct clock' for sdcardclk_hw.
 * @sampleclk_hw:	Struct for the clock we might provide to a PHY.
 * @sampleclk:		Pointer to normal 'struct clock' for sampleclk_hw.
 * @clk_phase_in:	Array of Input Clock Phase Delays for all speed modes
 * @clk_phase_out:	Array of Output Clock Phase Delays for all speed modes
 * @set_clk_delays:	Function pointer for setting Clock Delays
 * @clk_of_data:	Platform specific runtime clock data storage pointer
 */
struct sdhci_arasan_clk_data {
	struct clk_hw	sdcardclk_hw;
	struct clk      *sdcardclk;
	struct clk_hw	sampleclk_hw;
	struct clk      *sampleclk;
	int		clk_phase_in[MMC_TIMING_MMC_HS400 + 1];
	int		clk_phase_out[MMC_TIMING_MMC_HS400 + 1];
	void		(*set_clk_delays)(struct sdhci_host *host);
	void		*clk_of_data;
};

/**
 * struct sdhci_arasan_data - Arasan Controller Data
 *
 * @host:		Pointer to the main SDHCI host structure.
 * @clk_ahb:		Pointer to the AHB clock
 * @phy:		Pointer to the generic phy
 * @is_phy_on:		True if the PHY is on; false if not.
 * @has_cqe:		True if controller has command queuing engine.
 * @clk_data:		Struct for the Arasan Controller Clock Data.
 * @clk_ops:		Struct for the Arasan Controller Clock Operations.
 * @soc_ctl_base:	Pointer to regmap for syscon for soc_ctl registers.
 * @soc_ctl_map:	Map to get offsets into soc_ctl registers.
 * @quirks:		Arasan deviations from spec.
 */
struct sdhci_arasan_data {
	struct sdhci_host *host;
	struct clk	*clk_ahb;
	struct phy	*phy;
	bool		is_phy_on;

	bool		has_cqe;
	struct sdhci_arasan_clk_data clk_data;
	const struct sdhci_arasan_clk_ops *clk_ops;

	struct regmap	*soc_ctl_base;
	const struct sdhci_arasan_soc_ctl_map *soc_ctl_map;
	unsigned int	quirks;

/* Controller does not have CD wired and will not function normally without */
#define SDHCI_ARASAN_QUIRK_FORCE_CDTEST	BIT(0)
/* Controller immediately reports SDHCI_CLOCK_INT_STABLE after enabling the
 * internal clock even when the clock isn't stable */
#define SDHCI_ARASAN_QUIRK_CLOCK_UNSTABLE BIT(1)
/*
 * Some of the Arasan variations might not have timing requirements
 * met at 25MHz for Default Speed mode, those controllers work at
 * 19MHz instead
 */
#define SDHCI_ARASAN_QUIRK_CLOCK_25_BROKEN BIT(2)
};

struct sdhci_arasan_of_data {
	const struct sdhci_arasan_soc_ctl_map *soc_ctl_map;
	const struct sdhci_pltfm_data *pdata;
	const struct sdhci_arasan_clk_ops *clk_ops;
};

static const struct sdhci_arasan_soc_ctl_map rk3399_soc_ctl_map = {
	.baseclkfreq = { .reg = 0xf000, .width = 8, .shift = 8 },
	.clockmultiplier = { .reg = 0xf02c, .width = 8, .shift = 0},
	.hiword_update = true,
};

static const struct sdhci_arasan_soc_ctl_map intel_lgm_emmc_soc_ctl_map = {
	.baseclkfreq = { .reg = 0xa0, .width = 8, .shift = 2 },
	.clockmultiplier = { .reg = 0, .width = -1, .shift = -1 },
	.hiword_update = false,
};

static const struct sdhci_arasan_soc_ctl_map intel_lgm_sdxc_soc_ctl_map = {
	.baseclkfreq = { .reg = 0x80, .width = 8, .shift = 2 },
	.clockmultiplier = { .reg = 0, .width = -1, .shift = -1 },
	.hiword_update = false,
};

static const struct sdhci_arasan_soc_ctl_map thunderbay_soc_ctl_map = {
	.baseclkfreq = { .reg = 0x0, .width = 8, .shift = 14 },
	.clockmultiplier = { .reg = 0x4, .width = 8, .shift = 14 },
	.support64b = { .reg = 0x4, .width = 1, .shift = 24 },
	.hiword_update = false,
};

static const struct sdhci_arasan_soc_ctl_map intel_keembay_soc_ctl_map = {
	.baseclkfreq = { .reg = 0x0, .width = 8, .shift = 14 },
	.clockmultiplier = { .reg = 0x4, .width = 8, .shift = 14 },
	.support64b = { .reg = 0x4, .width = 1, .shift = 24 },
	.hiword_update = false,
};

/**
 * sdhci_arasan_syscon_write - Write to a field in soc_ctl registers
 *
 * @host:	The sdhci_host
 * @fld:	The field to write to
 * @val:	The value to write
 *
 * This function allows writing to fields in sdhci_arasan_soc_ctl_map.
 * Note that if a field is specified as not available (shift < 0) then
 * this function will silently return an error code.  It will be noisy
 * and print errors for any other (unexpected) errors.
 *
 * Return: 0 on success and error value on error
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

static void sdhci_arasan_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	struct sdhci_arasan_clk_data *clk_data = &sdhci_arasan->clk_data;
	bool ctrl_phy = false;

	if (!IS_ERR(sdhci_arasan->phy)) {
		if (!sdhci_arasan->is_phy_on && clock <= PHY_CLK_TOO_SLOW_HZ) {
			/*
			 * If PHY off, set clock to max speed and power PHY on.
			 *
			 * Although PHY docs apparently suggest power cycling
			 * when changing the clock the PHY doesn't like to be
			 * powered on while at low speeds like those used in ID
			 * mode.  Even worse is powering the PHY on while the
			 * clock is off.
			 *
			 * To workaround the PHY limitations, the best we can
			 * do is to power it on at a faster speed and then slam
			 * through low speeds without power cycling.
			 */
			sdhci_set_clock(host, host->max_clk);
			if (phy_power_on(sdhci_arasan->phy)) {
				pr_err("%s: Cannot power on phy.\n",
				       mmc_hostname(host->mmc));
				return;
			}

			sdhci_arasan->is_phy_on = true;

			/*
			 * We'll now fall through to the below case with
			 * ctrl_phy = false (so we won't turn off/on).  The
			 * sdhci_set_clock() will set the real clock.
			 */
		} else if (clock > PHY_CLK_TOO_SLOW_HZ) {
			/*
			 * At higher clock speeds the PHY is fine being power
			 * cycled and docs say you _should_ power cycle when
			 * changing clock speeds.
			 */
			ctrl_phy = true;
		}
	}

	if (ctrl_phy && sdhci_arasan->is_phy_on) {
		phy_power_off(sdhci_arasan->phy);
		sdhci_arasan->is_phy_on = false;
	}

	if (sdhci_arasan->quirks & SDHCI_ARASAN_QUIRK_CLOCK_25_BROKEN) {
		/*
		 * Some of the Arasan variations might not have timing
		 * requirements met at 25MHz for Default Speed mode,
		 * those controllers work at 19MHz instead.
		 */
		if (clock == DEFAULT_SPEED_MAX_DTR)
			clock = (DEFAULT_SPEED_MAX_DTR * 19) / 25;
	}

	/* Set the Input and Output Clock Phase Delays */
	if (clk_data->set_clk_delays)
		clk_data->set_clk_delays(host);

	sdhci_set_clock(host, clock);

	if (sdhci_arasan->quirks & SDHCI_ARASAN_QUIRK_CLOCK_UNSTABLE)
		/*
		 * Some controllers immediately report SDHCI_CLOCK_INT_STABLE
		 * after enabling the clock even though the clock is not
		 * stable. Trying to use a clock without waiting here results
		 * in EILSEQ while detecting some older/slower cards. The
		 * chosen delay is the maximum delay from sdhci_set_clock.
		 */
		msleep(20);

	if (ctrl_phy) {
		if (phy_power_on(sdhci_arasan->phy)) {
			pr_err("%s: Cannot power on phy.\n",
			       mmc_hostname(host->mmc));
			return;
		}

		sdhci_arasan->is_phy_on = true;
	}
}

static void sdhci_arasan_hs400_enhanced_strobe(struct mmc_host *mmc,
					struct mmc_ios *ios)
{
	u32 vendor;
	struct sdhci_host *host = mmc_priv(mmc);

	vendor = sdhci_readl(host, SDHCI_ARASAN_VENDOR_REGISTER);
	if (ios->enhanced_strobe)
		vendor |= VENDOR_ENHANCED_STROBE;
	else
		vendor &= ~VENDOR_ENHANCED_STROBE;

	sdhci_writel(host, vendor, SDHCI_ARASAN_VENDOR_REGISTER);
}

static void sdhci_arasan_reset(struct sdhci_host *host, u8 mask)
{
	u8 ctrl;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);

	sdhci_and_cqhci_reset(host, mask);

	if (sdhci_arasan->quirks & SDHCI_ARASAN_QUIRK_FORCE_CDTEST) {
		ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);
		ctrl |= SDHCI_CTRL_CDTEST_INS | SDHCI_CTRL_CDTEST_EN;
		sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);
	}
}

static int sdhci_arasan_voltage_switch(struct mmc_host *mmc,
				       struct mmc_ios *ios)
{
	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_180:
		/*
		 * Plese don't switch to 1V8 as arasan,5.1 doesn't
		 * actually refer to this setting to indicate the
		 * signal voltage and the state machine will be broken
		 * actually if we force to enable 1V8. That's something
		 * like broken quirk but we could work around here.
		 */
		return 0;
	case MMC_SIGNAL_VOLTAGE_330:
	case MMC_SIGNAL_VOLTAGE_120:
		/* We don't support 3V3 and 1V2 */
		break;
	}

	return -EINVAL;
}

static const struct sdhci_ops sdhci_arasan_ops = {
	.set_clock = sdhci_arasan_set_clock,
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.get_timeout_clock = sdhci_pltfm_clk_get_max_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_arasan_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
	.set_power = sdhci_set_power_and_bus_voltage,
};

static u32 sdhci_arasan_cqhci_irq(struct sdhci_host *host, u32 intmask)
{
	int cmd_error = 0;
	int data_error = 0;

	if (!sdhci_cqe_irq(host, intmask, &cmd_error, &data_error))
		return intmask;

	cqhci_irq(host->mmc, intmask, cmd_error, data_error);

	return 0;
}

static void sdhci_arasan_dumpregs(struct mmc_host *mmc)
{
	sdhci_dumpregs(mmc_priv(mmc));
}

static void sdhci_arasan_cqe_enable(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	u32 reg;

	reg = sdhci_readl(host, SDHCI_PRESENT_STATE);
	while (reg & SDHCI_DATA_AVAILABLE) {
		sdhci_readl(host, SDHCI_BUFFER);
		reg = sdhci_readl(host, SDHCI_PRESENT_STATE);
	}

	sdhci_cqe_enable(mmc);
}

static const struct cqhci_host_ops sdhci_arasan_cqhci_ops = {
	.enable         = sdhci_arasan_cqe_enable,
	.disable        = sdhci_cqe_disable,
	.dumpregs       = sdhci_arasan_dumpregs,
};

static const struct sdhci_ops sdhci_arasan_cqe_ops = {
	.set_clock = sdhci_arasan_set_clock,
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.get_timeout_clock = sdhci_pltfm_clk_get_max_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_arasan_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
	.set_power = sdhci_set_power_and_bus_voltage,
	.irq = sdhci_arasan_cqhci_irq,
};

static const struct sdhci_pltfm_data sdhci_arasan_cqe_pdata = {
	.ops = &sdhci_arasan_cqe_ops,
	.quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
			SDHCI_QUIRK2_CLOCK_DIV_ZERO_BROKEN,
};

static const struct sdhci_pltfm_data sdhci_arasan_thunderbay_pdata = {
	.ops = &sdhci_arasan_cqe_ops,
	.quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN | SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
		SDHCI_QUIRK2_CLOCK_DIV_ZERO_BROKEN |
		SDHCI_QUIRK2_STOP_WITH_TC |
		SDHCI_QUIRK2_CAPS_BIT63_FOR_HS400,
};

#ifdef CONFIG_PM_SLEEP
/**
 * sdhci_arasan_suspend - Suspend method for the driver
 * @dev:	Address of the device structure
 *
 * Put the device in a low power state.
 *
 * Return: 0 on success and error value on error
 */
static int sdhci_arasan_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	int ret;

	if (host->tuning_mode != SDHCI_TUNING_MODE_3)
		mmc_retune_needed(host->mmc);

	if (sdhci_arasan->has_cqe) {
		ret = cqhci_suspend(host->mmc);
		if (ret)
			return ret;
	}

	ret = sdhci_suspend_host(host);
	if (ret)
		return ret;

	if (!IS_ERR(sdhci_arasan->phy) && sdhci_arasan->is_phy_on) {
		ret = phy_power_off(sdhci_arasan->phy);
		if (ret) {
			dev_err(dev, "Cannot power off phy.\n");
			if (sdhci_resume_host(host))
				dev_err(dev, "Cannot resume host.\n");

			return ret;
		}
		sdhci_arasan->is_phy_on = false;
	}

	clk_disable(pltfm_host->clk);
	clk_disable(sdhci_arasan->clk_ahb);

	return 0;
}

/**
 * sdhci_arasan_resume - Resume method for the driver
 * @dev:	Address of the device structure
 *
 * Resume operation after suspend
 *
 * Return: 0 on success and error value on error
 */
static int sdhci_arasan_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
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

	if (!IS_ERR(sdhci_arasan->phy) && host->mmc->actual_clock) {
		ret = phy_power_on(sdhci_arasan->phy);
		if (ret) {
			dev_err(dev, "Cannot power on phy.\n");
			return ret;
		}
		sdhci_arasan->is_phy_on = true;
	}

	ret = sdhci_resume_host(host);
	if (ret) {
		dev_err(dev, "Cannot resume host.\n");
		return ret;
	}

	if (sdhci_arasan->has_cqe)
		return cqhci_resume(host->mmc);

	return 0;
}
#endif /* ! CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(sdhci_arasan_dev_pm_ops, sdhci_arasan_suspend,
			 sdhci_arasan_resume);

/**
 * sdhci_arasan_sdcardclk_recalc_rate - Return the card clock rate
 *
 * @hw:			Pointer to the hardware clock structure.
 * @parent_rate:		The parent rate (should be rate of clk_xin).
 *
 * Return the current actual rate of the SD card clock.  This can be used
 * to communicate with out PHY.
 *
 * Return: The card clock rate.
 */
static unsigned long sdhci_arasan_sdcardclk_recalc_rate(struct clk_hw *hw,
						      unsigned long parent_rate)
{
	struct sdhci_arasan_clk_data *clk_data =
		container_of(hw, struct sdhci_arasan_clk_data, sdcardclk_hw);
	struct sdhci_arasan_data *sdhci_arasan =
		container_of(clk_data, struct sdhci_arasan_data, clk_data);
	struct sdhci_host *host = sdhci_arasan->host;

	return host->mmc->actual_clock;
}

static const struct clk_ops arasan_sdcardclk_ops = {
	.recalc_rate = sdhci_arasan_sdcardclk_recalc_rate,
};

/**
 * sdhci_arasan_sampleclk_recalc_rate - Return the sampling clock rate
 *
 * @hw:			Pointer to the hardware clock structure.
 * @parent_rate:		The parent rate (should be rate of clk_xin).
 *
 * Return the current actual rate of the sampling clock.  This can be used
 * to communicate with out PHY.
 *
 * Return: The sample clock rate.
 */
static unsigned long sdhci_arasan_sampleclk_recalc_rate(struct clk_hw *hw,
						      unsigned long parent_rate)
{
	struct sdhci_arasan_clk_data *clk_data =
		container_of(hw, struct sdhci_arasan_clk_data, sampleclk_hw);
	struct sdhci_arasan_data *sdhci_arasan =
		container_of(clk_data, struct sdhci_arasan_data, clk_data);
	struct sdhci_host *host = sdhci_arasan->host;

	return host->mmc->actual_clock;
}

static const struct clk_ops arasan_sampleclk_ops = {
	.recalc_rate = sdhci_arasan_sampleclk_recalc_rate,
};

/**
 * sdhci_zynqmp_sdcardclk_set_phase - Set the SD Output Clock Tap Delays
 *
 * @hw:			Pointer to the hardware clock structure.
 * @degrees:		The clock phase shift between 0 - 359.
 *
 * Set the SD Output Clock Tap Delays for Output path
 *
 * Return: 0 on success and error value on error
 */
static int sdhci_zynqmp_sdcardclk_set_phase(struct clk_hw *hw, int degrees)
{
	struct sdhci_arasan_clk_data *clk_data =
		container_of(hw, struct sdhci_arasan_clk_data, sdcardclk_hw);
	struct sdhci_arasan_data *sdhci_arasan =
		container_of(clk_data, struct sdhci_arasan_data, clk_data);
	struct sdhci_host *host = sdhci_arasan->host;
	const char *clk_name = clk_hw_get_name(hw);
	u32 node_id = !strcmp(clk_name, "clk_out_sd0") ? NODE_SD_0 : NODE_SD_1;
	u8 tap_delay, tap_max = 0;
	int ret;

	/* This is applicable for SDHCI_SPEC_300 and above */
	if (host->version < SDHCI_SPEC_300)
		return 0;

	switch (host->timing) {
	case MMC_TIMING_MMC_HS:
	case MMC_TIMING_SD_HS:
	case MMC_TIMING_UHS_SDR25:
	case MMC_TIMING_UHS_DDR50:
	case MMC_TIMING_MMC_DDR52:
		/* For 50MHz clock, 30 Taps are available */
		tap_max = 30;
		break;
	case MMC_TIMING_UHS_SDR50:
		/* For 100MHz clock, 15 Taps are available */
		tap_max = 15;
		break;
	case MMC_TIMING_UHS_SDR104:
	case MMC_TIMING_MMC_HS200:
		/* For 200MHz clock, 8 Taps are available */
		tap_max = 8;
		break;
	default:
		break;
	}

	tap_delay = (degrees * tap_max) / 360;

	/* Set the Clock Phase */
	ret = zynqmp_pm_set_sd_tapdelay(node_id, PM_TAPDELAY_OUTPUT, tap_delay);
	if (ret)
		pr_err("Error setting Output Tap Delay\n");

	/* Release DLL Reset */
	zynqmp_pm_sd_dll_reset(node_id, PM_DLL_RESET_RELEASE);

	return ret;
}

static const struct clk_ops zynqmp_sdcardclk_ops = {
	.recalc_rate = sdhci_arasan_sdcardclk_recalc_rate,
	.set_phase = sdhci_zynqmp_sdcardclk_set_phase,
};

/**
 * sdhci_zynqmp_sampleclk_set_phase - Set the SD Input Clock Tap Delays
 *
 * @hw:			Pointer to the hardware clock structure.
 * @degrees:		The clock phase shift between 0 - 359.
 *
 * Set the SD Input Clock Tap Delays for Input path
 *
 * Return: 0 on success and error value on error
 */
static int sdhci_zynqmp_sampleclk_set_phase(struct clk_hw *hw, int degrees)
{
	struct sdhci_arasan_clk_data *clk_data =
		container_of(hw, struct sdhci_arasan_clk_data, sampleclk_hw);
	struct sdhci_arasan_data *sdhci_arasan =
		container_of(clk_data, struct sdhci_arasan_data, clk_data);
	struct sdhci_host *host = sdhci_arasan->host;
	const char *clk_name = clk_hw_get_name(hw);
	u32 node_id = !strcmp(clk_name, "clk_in_sd0") ? NODE_SD_0 : NODE_SD_1;
	u8 tap_delay, tap_max = 0;
	int ret;

	/* This is applicable for SDHCI_SPEC_300 and above */
	if (host->version < SDHCI_SPEC_300)
		return 0;

	/* Assert DLL Reset */
	zynqmp_pm_sd_dll_reset(node_id, PM_DLL_RESET_ASSERT);

	switch (host->timing) {
	case MMC_TIMING_MMC_HS:
	case MMC_TIMING_SD_HS:
	case MMC_TIMING_UHS_SDR25:
	case MMC_TIMING_UHS_DDR50:
	case MMC_TIMING_MMC_DDR52:
		/* For 50MHz clock, 120 Taps are available */
		tap_max = 120;
		break;
	case MMC_TIMING_UHS_SDR50:
		/* For 100MHz clock, 60 Taps are available */
		tap_max = 60;
		break;
	case MMC_TIMING_UHS_SDR104:
	case MMC_TIMING_MMC_HS200:
		/* For 200MHz clock, 30 Taps are available */
		tap_max = 30;
		break;
	default:
		break;
	}

	tap_delay = (degrees * tap_max) / 360;

	/* Set the Clock Phase */
	ret = zynqmp_pm_set_sd_tapdelay(node_id, PM_TAPDELAY_INPUT, tap_delay);
	if (ret)
		pr_err("Error setting Input Tap Delay\n");

	return ret;
}

static const struct clk_ops zynqmp_sampleclk_ops = {
	.recalc_rate = sdhci_arasan_sampleclk_recalc_rate,
	.set_phase = sdhci_zynqmp_sampleclk_set_phase,
};

/**
 * sdhci_versal_sdcardclk_set_phase - Set the SD Output Clock Tap Delays
 *
 * @hw:			Pointer to the hardware clock structure.
 * @degrees:		The clock phase shift between 0 - 359.
 *
 * Set the SD Output Clock Tap Delays for Output path
 *
 * Return: 0 on success and error value on error
 */
static int sdhci_versal_sdcardclk_set_phase(struct clk_hw *hw, int degrees)
{
	struct sdhci_arasan_clk_data *clk_data =
		container_of(hw, struct sdhci_arasan_clk_data, sdcardclk_hw);
	struct sdhci_arasan_data *sdhci_arasan =
		container_of(clk_data, struct sdhci_arasan_data, clk_data);
	struct sdhci_host *host = sdhci_arasan->host;
	u8 tap_delay, tap_max = 0;

	/* This is applicable for SDHCI_SPEC_300 and above */
	if (host->version < SDHCI_SPEC_300)
		return 0;

	switch (host->timing) {
	case MMC_TIMING_MMC_HS:
	case MMC_TIMING_SD_HS:
	case MMC_TIMING_UHS_SDR25:
	case MMC_TIMING_UHS_DDR50:
	case MMC_TIMING_MMC_DDR52:
		/* For 50MHz clock, 30 Taps are available */
		tap_max = 30;
		break;
	case MMC_TIMING_UHS_SDR50:
		/* For 100MHz clock, 15 Taps are available */
		tap_max = 15;
		break;
	case MMC_TIMING_UHS_SDR104:
	case MMC_TIMING_MMC_HS200:
		/* For 200MHz clock, 8 Taps are available */
		tap_max = 8;
		break;
	default:
		break;
	}

	tap_delay = (degrees * tap_max) / 360;

	/* Set the Clock Phase */
	if (tap_delay) {
		u32 regval;

		regval = sdhci_readl(host, SDHCI_ARASAN_OTAPDLY_REGISTER);
		regval |= SDHCI_OTAPDLY_ENABLE;
		sdhci_writel(host, regval, SDHCI_ARASAN_OTAPDLY_REGISTER);
		regval &= ~SDHCI_ARASAN_OTAPDLY_SEL_MASK;
		regval |= tap_delay;
		sdhci_writel(host, regval, SDHCI_ARASAN_OTAPDLY_REGISTER);
	}

	return 0;
}

static const struct clk_ops versal_sdcardclk_ops = {
	.recalc_rate = sdhci_arasan_sdcardclk_recalc_rate,
	.set_phase = sdhci_versal_sdcardclk_set_phase,
};

/**
 * sdhci_versal_sampleclk_set_phase - Set the SD Input Clock Tap Delays
 *
 * @hw:			Pointer to the hardware clock structure.
 * @degrees:		The clock phase shift between 0 - 359.
 *
 * Set the SD Input Clock Tap Delays for Input path
 *
 * Return: 0 on success and error value on error
 */
static int sdhci_versal_sampleclk_set_phase(struct clk_hw *hw, int degrees)
{
	struct sdhci_arasan_clk_data *clk_data =
		container_of(hw, struct sdhci_arasan_clk_data, sampleclk_hw);
	struct sdhci_arasan_data *sdhci_arasan =
		container_of(clk_data, struct sdhci_arasan_data, clk_data);
	struct sdhci_host *host = sdhci_arasan->host;
	u8 tap_delay, tap_max = 0;

	/* This is applicable for SDHCI_SPEC_300 and above */
	if (host->version < SDHCI_SPEC_300)
		return 0;

	switch (host->timing) {
	case MMC_TIMING_MMC_HS:
	case MMC_TIMING_SD_HS:
	case MMC_TIMING_UHS_SDR25:
	case MMC_TIMING_UHS_DDR50:
	case MMC_TIMING_MMC_DDR52:
		/* For 50MHz clock, 120 Taps are available */
		tap_max = 120;
		break;
	case MMC_TIMING_UHS_SDR50:
		/* For 100MHz clock, 60 Taps are available */
		tap_max = 60;
		break;
	case MMC_TIMING_UHS_SDR104:
	case MMC_TIMING_MMC_HS200:
		/* For 200MHz clock, 30 Taps are available */
		tap_max = 30;
		break;
	default:
		break;
	}

	tap_delay = (degrees * tap_max) / 360;

	/* Set the Clock Phase */
	if (tap_delay) {
		u32 regval;

		regval = sdhci_readl(host, SDHCI_ARASAN_ITAPDLY_REGISTER);
		regval |= SDHCI_ITAPDLY_CHGWIN;
		sdhci_writel(host, regval, SDHCI_ARASAN_ITAPDLY_REGISTER);
		regval |= SDHCI_ITAPDLY_ENABLE;
		sdhci_writel(host, regval, SDHCI_ARASAN_ITAPDLY_REGISTER);
		regval &= ~SDHCI_ARASAN_ITAPDLY_SEL_MASK;
		regval |= tap_delay;
		sdhci_writel(host, regval, SDHCI_ARASAN_ITAPDLY_REGISTER);
		regval &= ~SDHCI_ITAPDLY_CHGWIN;
		sdhci_writel(host, regval, SDHCI_ARASAN_ITAPDLY_REGISTER);
	}

	return 0;
}

static const struct clk_ops versal_sampleclk_ops = {
	.recalc_rate = sdhci_arasan_sampleclk_recalc_rate,
	.set_phase = sdhci_versal_sampleclk_set_phase,
};

static void arasan_zynqmp_dll_reset(struct sdhci_host *host, u32 deviceid)
{
	u16 clk;

	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk &= ~(SDHCI_CLOCK_CARD_EN | SDHCI_CLOCK_INT_EN);
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	/* Issue DLL Reset */
	zynqmp_pm_sd_dll_reset(deviceid, PM_DLL_RESET_PULSE);

	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);

	sdhci_enable_clk(host, clk);
}

static int arasan_zynqmp_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	struct clk_hw *hw = &sdhci_arasan->clk_data.sdcardclk_hw;
	const char *clk_name = clk_hw_get_name(hw);
	u32 device_id = !strcmp(clk_name, "clk_out_sd0") ? NODE_SD_0 :
							   NODE_SD_1;
	int err;

	/* ZynqMP SD controller does not perform auto tuning in DDR50 mode */
	if (mmc->ios.timing == MMC_TIMING_UHS_DDR50)
		return 0;

	arasan_zynqmp_dll_reset(host, device_id);

	err = sdhci_execute_tuning(mmc, opcode);
	if (err)
		return err;

	arasan_zynqmp_dll_reset(host, device_id);

	return 0;
}

/**
 * sdhci_arasan_update_clockmultiplier - Set corecfg_clockmultiplier
 *
 * @host:		The sdhci_host
 * @value:		The value to write
 *
 * The corecfg_clockmultiplier is supposed to contain clock multiplier
 * value of programmable clock generator.
 *
 * NOTES:
 * - Many existing devices don't seem to do this and work fine.  To keep
 *   compatibility for old hardware where the device tree doesn't provide a
 *   register map, this function is a noop if a soc_ctl_map hasn't been provided
 *   for this platform.
 * - The value of corecfg_clockmultiplier should sync with that of corresponding
 *   value reading from sdhci_capability_register. So this function is called
 *   once at probe time and never called again.
 */
static void sdhci_arasan_update_clockmultiplier(struct sdhci_host *host,
						u32 value)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_arasan_soc_ctl_map *soc_ctl_map =
		sdhci_arasan->soc_ctl_map;

	/* Having a map is optional */
	if (!soc_ctl_map)
		return;

	/* If we have a map, we expect to have a syscon */
	if (!sdhci_arasan->soc_ctl_base) {
		pr_warn("%s: Have regmap, but no soc-ctl-syscon\n",
			mmc_hostname(host->mmc));
		return;
	}

	sdhci_arasan_syscon_write(host, &soc_ctl_map->clockmultiplier, value);
}

/**
 * sdhci_arasan_update_baseclkfreq - Set corecfg_baseclkfreq
 *
 * @host:		The sdhci_host
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
 */
static void sdhci_arasan_update_baseclkfreq(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_arasan_soc_ctl_map *soc_ctl_map =
		sdhci_arasan->soc_ctl_map;
	u32 mhz = DIV_ROUND_CLOSEST_ULL(clk_get_rate(pltfm_host->clk), 1000000);

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

static void sdhci_arasan_set_clk_delays(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	struct sdhci_arasan_clk_data *clk_data = &sdhci_arasan->clk_data;

	clk_set_phase(clk_data->sampleclk,
		      clk_data->clk_phase_in[host->timing]);
	clk_set_phase(clk_data->sdcardclk,
		      clk_data->clk_phase_out[host->timing]);
}

static void arasan_dt_read_clk_phase(struct device *dev,
				     struct sdhci_arasan_clk_data *clk_data,
				     unsigned int timing, const char *prop)
{
	struct device_node *np = dev->of_node;

	u32 clk_phase[2] = {0};
	int ret;

	/*
	 * Read Tap Delay values from DT, if the DT does not contain the
	 * Tap Values then use the pre-defined values.
	 */
	ret = of_property_read_variable_u32_array(np, prop, &clk_phase[0],
						  2, 0);
	if (ret < 0) {
		dev_dbg(dev, "Using predefined clock phase for %s = %d %d\n",
			prop, clk_data->clk_phase_in[timing],
			clk_data->clk_phase_out[timing]);
		return;
	}

	/* The values read are Input and Output Clock Delays in order */
	clk_data->clk_phase_in[timing] = clk_phase[0];
	clk_data->clk_phase_out[timing] = clk_phase[1];
}

/**
 * arasan_dt_parse_clk_phases - Read Clock Delay values from DT
 *
 * @dev:		Pointer to our struct device.
 * @clk_data:		Pointer to the Clock Data structure
 *
 * Called at initialization to parse the values of Clock Delays.
 */
static void arasan_dt_parse_clk_phases(struct device *dev,
				       struct sdhci_arasan_clk_data *clk_data)
{
	u32 mio_bank = 0;
	int i;

	/*
	 * This has been kept as a pointer and is assigned a function here.
	 * So that different controller variants can assign their own handling
	 * function.
	 */
	clk_data->set_clk_delays = sdhci_arasan_set_clk_delays;

	if (of_device_is_compatible(dev->of_node, "xlnx,zynqmp-8.9a")) {
		u32 zynqmp_iclk_phase[MMC_TIMING_MMC_HS400 + 1] =
			ZYNQMP_ICLK_PHASE;
		u32 zynqmp_oclk_phase[MMC_TIMING_MMC_HS400 + 1] =
			ZYNQMP_OCLK_PHASE;

		of_property_read_u32(dev->of_node, "xlnx,mio-bank", &mio_bank);
		if (mio_bank == 2) {
			zynqmp_oclk_phase[MMC_TIMING_UHS_SDR104] = 90;
			zynqmp_oclk_phase[MMC_TIMING_MMC_HS200] = 90;
		}

		for (i = 0; i <= MMC_TIMING_MMC_HS400; i++) {
			clk_data->clk_phase_in[i] = zynqmp_iclk_phase[i];
			clk_data->clk_phase_out[i] = zynqmp_oclk_phase[i];
		}
	}

	if (of_device_is_compatible(dev->of_node, "xlnx,versal-8.9a")) {
		u32 versal_iclk_phase[MMC_TIMING_MMC_HS400 + 1] =
			VERSAL_ICLK_PHASE;
		u32 versal_oclk_phase[MMC_TIMING_MMC_HS400 + 1] =
			VERSAL_OCLK_PHASE;

		for (i = 0; i <= MMC_TIMING_MMC_HS400; i++) {
			clk_data->clk_phase_in[i] = versal_iclk_phase[i];
			clk_data->clk_phase_out[i] = versal_oclk_phase[i];
		}
	}

	arasan_dt_read_clk_phase(dev, clk_data, MMC_TIMING_LEGACY,
				 "clk-phase-legacy");
	arasan_dt_read_clk_phase(dev, clk_data, MMC_TIMING_MMC_HS,
				 "clk-phase-mmc-hs");
	arasan_dt_read_clk_phase(dev, clk_data, MMC_TIMING_SD_HS,
				 "clk-phase-sd-hs");
	arasan_dt_read_clk_phase(dev, clk_data, MMC_TIMING_UHS_SDR12,
				 "clk-phase-uhs-sdr12");
	arasan_dt_read_clk_phase(dev, clk_data, MMC_TIMING_UHS_SDR25,
				 "clk-phase-uhs-sdr25");
	arasan_dt_read_clk_phase(dev, clk_data, MMC_TIMING_UHS_SDR50,
				 "clk-phase-uhs-sdr50");
	arasan_dt_read_clk_phase(dev, clk_data, MMC_TIMING_UHS_SDR104,
				 "clk-phase-uhs-sdr104");
	arasan_dt_read_clk_phase(dev, clk_data, MMC_TIMING_UHS_DDR50,
				 "clk-phase-uhs-ddr50");
	arasan_dt_read_clk_phase(dev, clk_data, MMC_TIMING_MMC_DDR52,
				 "clk-phase-mmc-ddr52");
	arasan_dt_read_clk_phase(dev, clk_data, MMC_TIMING_MMC_HS200,
				 "clk-phase-mmc-hs200");
	arasan_dt_read_clk_phase(dev, clk_data, MMC_TIMING_MMC_HS400,
				 "clk-phase-mmc-hs400");
}

static const struct sdhci_pltfm_data sdhci_arasan_pdata = {
	.ops = &sdhci_arasan_ops,
	.quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
			SDHCI_QUIRK2_CLOCK_DIV_ZERO_BROKEN |
			SDHCI_QUIRK2_STOP_WITH_TC,
};

static const struct sdhci_arasan_clk_ops arasan_clk_ops = {
	.sdcardclk_ops = &arasan_sdcardclk_ops,
	.sampleclk_ops = &arasan_sampleclk_ops,
};

static struct sdhci_arasan_of_data sdhci_arasan_generic_data = {
	.pdata = &sdhci_arasan_pdata,
	.clk_ops = &arasan_clk_ops,
};

static const struct sdhci_arasan_of_data sdhci_arasan_thunderbay_data = {
	.soc_ctl_map = &thunderbay_soc_ctl_map,
	.pdata = &sdhci_arasan_thunderbay_pdata,
	.clk_ops = &arasan_clk_ops,
};

static const struct sdhci_pltfm_data sdhci_keembay_emmc_pdata = {
	.ops = &sdhci_arasan_cqe_ops,
	.quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
		SDHCI_QUIRK_NO_LED |
		SDHCI_QUIRK_32BIT_DMA_ADDR |
		SDHCI_QUIRK_32BIT_DMA_SIZE |
		SDHCI_QUIRK_32BIT_ADMA_SIZE,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
		SDHCI_QUIRK2_CLOCK_DIV_ZERO_BROKEN |
		SDHCI_QUIRK2_CAPS_BIT63_FOR_HS400 |
		SDHCI_QUIRK2_STOP_WITH_TC |
		SDHCI_QUIRK2_BROKEN_64_BIT_DMA,
};

static const struct sdhci_pltfm_data sdhci_keembay_sd_pdata = {
	.ops = &sdhci_arasan_ops,
	.quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
		SDHCI_QUIRK_NO_LED |
		SDHCI_QUIRK_32BIT_DMA_ADDR |
		SDHCI_QUIRK_32BIT_DMA_SIZE |
		SDHCI_QUIRK_32BIT_ADMA_SIZE,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
		SDHCI_QUIRK2_CLOCK_DIV_ZERO_BROKEN |
		SDHCI_QUIRK2_CARD_ON_NEEDS_BUS_ON |
		SDHCI_QUIRK2_STOP_WITH_TC |
		SDHCI_QUIRK2_BROKEN_64_BIT_DMA,
};

static const struct sdhci_pltfm_data sdhci_keembay_sdio_pdata = {
	.ops = &sdhci_arasan_ops,
	.quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
		SDHCI_QUIRK_NO_LED |
		SDHCI_QUIRK_32BIT_DMA_ADDR |
		SDHCI_QUIRK_32BIT_DMA_SIZE |
		SDHCI_QUIRK_32BIT_ADMA_SIZE,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
		SDHCI_QUIRK2_CLOCK_DIV_ZERO_BROKEN |
		SDHCI_QUIRK2_HOST_OFF_CARD_ON |
		SDHCI_QUIRK2_BROKEN_64_BIT_DMA,
};

static struct sdhci_arasan_of_data sdhci_arasan_rk3399_data = {
	.soc_ctl_map = &rk3399_soc_ctl_map,
	.pdata = &sdhci_arasan_cqe_pdata,
	.clk_ops = &arasan_clk_ops,
};

static struct sdhci_arasan_of_data intel_lgm_emmc_data = {
	.soc_ctl_map = &intel_lgm_emmc_soc_ctl_map,
	.pdata = &sdhci_arasan_cqe_pdata,
	.clk_ops = &arasan_clk_ops,
};

static struct sdhci_arasan_of_data intel_lgm_sdxc_data = {
	.soc_ctl_map = &intel_lgm_sdxc_soc_ctl_map,
	.pdata = &sdhci_arasan_cqe_pdata,
	.clk_ops = &arasan_clk_ops,
};

static const struct sdhci_pltfm_data sdhci_arasan_zynqmp_pdata = {
	.ops = &sdhci_arasan_ops,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
			SDHCI_QUIRK2_CLOCK_DIV_ZERO_BROKEN |
			SDHCI_QUIRK2_STOP_WITH_TC,
};

static const struct sdhci_arasan_clk_ops zynqmp_clk_ops = {
	.sdcardclk_ops = &zynqmp_sdcardclk_ops,
	.sampleclk_ops = &zynqmp_sampleclk_ops,
};

static struct sdhci_arasan_of_data sdhci_arasan_zynqmp_data = {
	.pdata = &sdhci_arasan_zynqmp_pdata,
	.clk_ops = &zynqmp_clk_ops,
};

static const struct sdhci_arasan_clk_ops versal_clk_ops = {
	.sdcardclk_ops = &versal_sdcardclk_ops,
	.sampleclk_ops = &versal_sampleclk_ops,
};

static struct sdhci_arasan_of_data sdhci_arasan_versal_data = {
	.pdata = &sdhci_arasan_zynqmp_pdata,
	.clk_ops = &versal_clk_ops,
};

static struct sdhci_arasan_of_data intel_keembay_emmc_data = {
	.soc_ctl_map = &intel_keembay_soc_ctl_map,
	.pdata = &sdhci_keembay_emmc_pdata,
	.clk_ops = &arasan_clk_ops,
};

static struct sdhci_arasan_of_data intel_keembay_sd_data = {
	.soc_ctl_map = &intel_keembay_soc_ctl_map,
	.pdata = &sdhci_keembay_sd_pdata,
	.clk_ops = &arasan_clk_ops,
};

static struct sdhci_arasan_of_data intel_keembay_sdio_data = {
	.soc_ctl_map = &intel_keembay_soc_ctl_map,
	.pdata = &sdhci_keembay_sdio_pdata,
	.clk_ops = &arasan_clk_ops,
};

static const struct of_device_id sdhci_arasan_of_match[] = {
	/* SoC-specific compatible strings w/ soc_ctl_map */
	{
		.compatible = "rockchip,rk3399-sdhci-5.1",
		.data = &sdhci_arasan_rk3399_data,
	},
	{
		.compatible = "intel,lgm-sdhci-5.1-emmc",
		.data = &intel_lgm_emmc_data,
	},
	{
		.compatible = "intel,lgm-sdhci-5.1-sdxc",
		.data = &intel_lgm_sdxc_data,
	},
	{
		.compatible = "intel,keembay-sdhci-5.1-emmc",
		.data = &intel_keembay_emmc_data,
	},
	{
		.compatible = "intel,keembay-sdhci-5.1-sd",
		.data = &intel_keembay_sd_data,
	},
	{
		.compatible = "intel,keembay-sdhci-5.1-sdio",
		.data = &intel_keembay_sdio_data,
	},
	{
		.compatible = "intel,thunderbay-sdhci-5.1",
		.data = &sdhci_arasan_thunderbay_data,
	},
	/* Generic compatible below here */
	{
		.compatible = "arasan,sdhci-8.9a",
		.data = &sdhci_arasan_generic_data,
	},
	{
		.compatible = "arasan,sdhci-5.1",
		.data = &sdhci_arasan_generic_data,
	},
	{
		.compatible = "arasan,sdhci-4.9a",
		.data = &sdhci_arasan_generic_data,
	},
	{
		.compatible = "xlnx,zynqmp-8.9a",
		.data = &sdhci_arasan_zynqmp_data,
	},
	{
		.compatible = "xlnx,versal-8.9a",
		.data = &sdhci_arasan_versal_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sdhci_arasan_of_match);

/**
 * sdhci_arasan_register_sdcardclk - Register the sdcardclk for a PHY to use
 *
 * @sdhci_arasan:	Our private data structure.
 * @clk_xin:		Pointer to the functional clock
 * @dev:		Pointer to our struct device.
 *
 * Some PHY devices need to know what the actual card clock is.  In order for
 * them to find out, we'll provide a clock through the common clock framework
 * for them to query.
 *
 * Return: 0 on success and error value on error
 */
static int
sdhci_arasan_register_sdcardclk(struct sdhci_arasan_data *sdhci_arasan,
				struct clk *clk_xin,
				struct device *dev)
{
	struct sdhci_arasan_clk_data *clk_data = &sdhci_arasan->clk_data;
	struct device_node *np = dev->of_node;
	struct clk_init_data sdcardclk_init;
	const char *parent_clk_name;
	int ret;

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
	sdcardclk_init.ops = sdhci_arasan->clk_ops->sdcardclk_ops;

	clk_data->sdcardclk_hw.init = &sdcardclk_init;
	clk_data->sdcardclk =
		devm_clk_register(dev, &clk_data->sdcardclk_hw);
	if (IS_ERR(clk_data->sdcardclk))
		return PTR_ERR(clk_data->sdcardclk);
	clk_data->sdcardclk_hw.init = NULL;

	ret = of_clk_add_provider(np, of_clk_src_simple_get,
				  clk_data->sdcardclk);
	if (ret)
		dev_err(dev, "Failed to add sdcard clock provider\n");

	return ret;
}

/**
 * sdhci_arasan_register_sampleclk - Register the sampleclk for a PHY to use
 *
 * @sdhci_arasan:	Our private data structure.
 * @clk_xin:		Pointer to the functional clock
 * @dev:		Pointer to our struct device.
 *
 * Some PHY devices need to know what the actual card clock is.  In order for
 * them to find out, we'll provide a clock through the common clock framework
 * for them to query.
 *
 * Return: 0 on success and error value on error
 */
static int
sdhci_arasan_register_sampleclk(struct sdhci_arasan_data *sdhci_arasan,
				struct clk *clk_xin,
				struct device *dev)
{
	struct sdhci_arasan_clk_data *clk_data = &sdhci_arasan->clk_data;
	struct device_node *np = dev->of_node;
	struct clk_init_data sampleclk_init;
	const char *parent_clk_name;
	int ret;

	ret = of_property_read_string_index(np, "clock-output-names", 1,
					    &sampleclk_init.name);
	if (ret) {
		dev_err(dev, "DT has #clock-cells but no clock-output-names\n");
		return ret;
	}

	parent_clk_name = __clk_get_name(clk_xin);
	sampleclk_init.parent_names = &parent_clk_name;
	sampleclk_init.num_parents = 1;
	sampleclk_init.flags = CLK_GET_RATE_NOCACHE;
	sampleclk_init.ops = sdhci_arasan->clk_ops->sampleclk_ops;

	clk_data->sampleclk_hw.init = &sampleclk_init;
	clk_data->sampleclk =
		devm_clk_register(dev, &clk_data->sampleclk_hw);
	if (IS_ERR(clk_data->sampleclk))
		return PTR_ERR(clk_data->sampleclk);
	clk_data->sampleclk_hw.init = NULL;

	ret = of_clk_add_provider(np, of_clk_src_simple_get,
				  clk_data->sampleclk);
	if (ret)
		dev_err(dev, "Failed to add sample clock provider\n");

	return ret;
}

/**
 * sdhci_arasan_unregister_sdclk - Undoes sdhci_arasan_register_sdclk()
 *
 * @dev:		Pointer to our struct device.
 *
 * Should be called any time we're exiting and sdhci_arasan_register_sdclk()
 * returned success.
 */
static void sdhci_arasan_unregister_sdclk(struct device *dev)
{
	struct device_node *np = dev->of_node;

	if (!of_find_property(np, "#clock-cells", NULL))
		return;

	of_clk_del_provider(dev->of_node);
}

/**
 * sdhci_arasan_update_support64b - Set SUPPORT_64B (64-bit System Bus Support)
 * @host:		The sdhci_host
 * @value:		The value to write
 *
 * This should be set based on the System Address Bus.
 * 0: the Core supports only 32-bit System Address Bus.
 * 1: the Core supports 64-bit System Address Bus.
 *
 * NOTE:
 * For Keem Bay, it is required to clear this bit. Its default value is 1'b1.
 * Keem Bay does not support 64-bit access.
 */
static void sdhci_arasan_update_support64b(struct sdhci_host *host, u32 value)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_arasan_soc_ctl_map *soc_ctl_map;

	/* Having a map is optional */
	soc_ctl_map = sdhci_arasan->soc_ctl_map;
	if (!soc_ctl_map)
		return;

	/* If we have a map, we expect to have a syscon */
	if (!sdhci_arasan->soc_ctl_base) {
		pr_warn("%s: Have regmap, but no soc-ctl-syscon\n",
			mmc_hostname(host->mmc));
		return;
	}

	sdhci_arasan_syscon_write(host, &soc_ctl_map->support64b, value);
}

/**
 * sdhci_arasan_register_sdclk - Register the sdcardclk for a PHY to use
 *
 * @sdhci_arasan:	Our private data structure.
 * @clk_xin:		Pointer to the functional clock
 * @dev:		Pointer to our struct device.
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
 * Return: 0 on success and error value on error
 */
static int sdhci_arasan_register_sdclk(struct sdhci_arasan_data *sdhci_arasan,
				       struct clk *clk_xin,
				       struct device *dev)
{
	struct device_node *np = dev->of_node;
	u32 num_clks = 0;
	int ret;

	/* Providing a clock to the PHY is optional; no error if missing */
	if (of_property_read_u32(np, "#clock-cells", &num_clks) < 0)
		return 0;

	ret = sdhci_arasan_register_sdcardclk(sdhci_arasan, clk_xin, dev);
	if (ret)
		return ret;

	if (num_clks) {
		ret = sdhci_arasan_register_sampleclk(sdhci_arasan, clk_xin,
						      dev);
		if (ret) {
			sdhci_arasan_unregister_sdclk(dev);
			return ret;
		}
	}

	return 0;
}

static int sdhci_arasan_add_host(struct sdhci_arasan_data *sdhci_arasan)
{
	struct sdhci_host *host = sdhci_arasan->host;
	struct cqhci_host *cq_host;
	bool dma64;
	int ret;

	if (!sdhci_arasan->has_cqe)
		return sdhci_add_host(host);

	ret = sdhci_setup_host(host);
	if (ret)
		return ret;

	cq_host = devm_kzalloc(host->mmc->parent,
			       sizeof(*cq_host), GFP_KERNEL);
	if (!cq_host) {
		ret = -ENOMEM;
		goto cleanup;
	}

	cq_host->mmio = host->ioaddr + SDHCI_ARASAN_CQE_BASE_ADDR;
	cq_host->ops = &sdhci_arasan_cqhci_ops;

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

static int sdhci_arasan_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *node;
	struct clk *clk_xin;
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct sdhci_arasan_data *sdhci_arasan;
	const struct sdhci_arasan_of_data *data;

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	host = sdhci_pltfm_init(pdev, data->pdata, sizeof(*sdhci_arasan));

	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	sdhci_arasan->host = host;

	sdhci_arasan->soc_ctl_map = data->soc_ctl_map;
	sdhci_arasan->clk_ops = data->clk_ops;

	node = of_parse_phandle(np, "arasan,soc-ctl-syscon", 0);
	if (node) {
		sdhci_arasan->soc_ctl_base = syscon_node_to_regmap(node);
		of_node_put(node);

		if (IS_ERR(sdhci_arasan->soc_ctl_base)) {
			ret = dev_err_probe(dev,
					    PTR_ERR(sdhci_arasan->soc_ctl_base),
					    "Can't get syscon\n");
			goto err_pltfm_free;
		}
	}

	sdhci_get_of_property(pdev);

	sdhci_arasan->clk_ahb = devm_clk_get(dev, "clk_ahb");
	if (IS_ERR(sdhci_arasan->clk_ahb)) {
		ret = dev_err_probe(dev, PTR_ERR(sdhci_arasan->clk_ahb),
				    "clk_ahb clock not found.\n");
		goto err_pltfm_free;
	}

	clk_xin = devm_clk_get(dev, "clk_xin");
	if (IS_ERR(clk_xin)) {
		ret = dev_err_probe(dev, PTR_ERR(clk_xin), "clk_xin clock not found.\n");
		goto err_pltfm_free;
	}

	ret = clk_prepare_enable(sdhci_arasan->clk_ahb);
	if (ret) {
		dev_err(dev, "Unable to enable AHB clock.\n");
		goto err_pltfm_free;
	}

	/* If clock-frequency property is set, use the provided value */
	if (pltfm_host->clock &&
	    pltfm_host->clock != clk_get_rate(clk_xin)) {
		ret = clk_set_rate(clk_xin, pltfm_host->clock);
		if (ret) {
			dev_err(&pdev->dev, "Failed to set SD clock rate\n");
			goto clk_dis_ahb;
		}
	}

	ret = clk_prepare_enable(clk_xin);
	if (ret) {
		dev_err(dev, "Unable to enable SD clock.\n");
		goto clk_dis_ahb;
	}

	if (of_property_read_bool(np, "xlnx,fails-without-test-cd"))
		sdhci_arasan->quirks |= SDHCI_ARASAN_QUIRK_FORCE_CDTEST;

	if (of_property_read_bool(np, "xlnx,int-clock-stable-broken"))
		sdhci_arasan->quirks |= SDHCI_ARASAN_QUIRK_CLOCK_UNSTABLE;

	pltfm_host->clk = clk_xin;

	if (of_device_is_compatible(np, "rockchip,rk3399-sdhci-5.1"))
		sdhci_arasan_update_clockmultiplier(host, 0x0);

	if (of_device_is_compatible(np, "intel,keembay-sdhci-5.1-emmc") ||
	    of_device_is_compatible(np, "intel,keembay-sdhci-5.1-sd") ||
	    of_device_is_compatible(np, "intel,keembay-sdhci-5.1-sdio") ||
	    of_device_is_compatible(np, "intel,thunderbay-sdhci-5.1")) {
		sdhci_arasan_update_clockmultiplier(host, 0x0);
		sdhci_arasan_update_support64b(host, 0x0);

		host->mmc->caps |= MMC_CAP_WAIT_WHILE_BUSY;
	}

	sdhci_arasan_update_baseclkfreq(host);

	ret = sdhci_arasan_register_sdclk(sdhci_arasan, clk_xin, dev);
	if (ret)
		goto clk_disable_all;

	if (of_device_is_compatible(np, "xlnx,zynqmp-8.9a")) {
		host->mmc_host_ops.execute_tuning =
			arasan_zynqmp_execute_tuning;

		sdhci_arasan->quirks |= SDHCI_ARASAN_QUIRK_CLOCK_25_BROKEN;
		host->quirks |= SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12;
	}

	arasan_dt_parse_clk_phases(dev, &sdhci_arasan->clk_data);

	ret = mmc_of_parse(host->mmc);
	if (ret) {
		ret = dev_err_probe(dev, ret, "parsing dt failed.\n");
		goto unreg_clk;
	}

	sdhci_arasan->phy = ERR_PTR(-ENODEV);
	if (of_device_is_compatible(np, "arasan,sdhci-5.1")) {
		sdhci_arasan->phy = devm_phy_get(dev, "phy_arasan");
		if (IS_ERR(sdhci_arasan->phy)) {
			ret = dev_err_probe(dev, PTR_ERR(sdhci_arasan->phy),
					    "No phy for arasan,sdhci-5.1.\n");
			goto unreg_clk;
		}

		ret = phy_init(sdhci_arasan->phy);
		if (ret < 0) {
			dev_err(dev, "phy_init err.\n");
			goto unreg_clk;
		}

		host->mmc_host_ops.hs400_enhanced_strobe =
					sdhci_arasan_hs400_enhanced_strobe;
		host->mmc_host_ops.start_signal_voltage_switch =
					sdhci_arasan_voltage_switch;
		sdhci_arasan->has_cqe = true;
		host->mmc->caps2 |= MMC_CAP2_CQE;

		if (!of_property_read_bool(np, "disable-cqe-dcmd"))
			host->mmc->caps2 |= MMC_CAP2_CQE_DCMD;
	}

	ret = sdhci_arasan_add_host(sdhci_arasan);
	if (ret)
		goto err_add_host;

	return 0;

err_add_host:
	if (!IS_ERR(sdhci_arasan->phy))
		phy_exit(sdhci_arasan->phy);
unreg_clk:
	sdhci_arasan_unregister_sdclk(dev);
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
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	struct clk *clk_ahb = sdhci_arasan->clk_ahb;

	if (!IS_ERR(sdhci_arasan->phy)) {
		if (sdhci_arasan->is_phy_on)
			phy_power_off(sdhci_arasan->phy);
		phy_exit(sdhci_arasan->phy);
	}

	sdhci_arasan_unregister_sdclk(&pdev->dev);

	sdhci_pltfm_unregister(pdev);

	clk_disable_unprepare(clk_ahb);

	return 0;
}

static struct platform_driver sdhci_arasan_driver = {
	.driver = {
		.name = "sdhci-arasan",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
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
