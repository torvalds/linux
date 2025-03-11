// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Marvell Xenon SDHC as a platform device
 *
 * Copyright (C) 2016 Marvell, All Rights Reserved.
 *
 * Author:	Hu Ziji <huziji@marvell.com>
 * Date:	2016-8-24
 *
 * Inspired by Jisheng Zhang <jszhang@marvell.com>
 * Special thanks to Video BG4 project team.
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>

#include "sdhci-pltfm.h"
#include "sdhci-xenon.h"

static int xenon_enable_internal_clk(struct sdhci_host *host)
{
	u32 reg;
	ktime_t timeout;

	reg = sdhci_readl(host, SDHCI_CLOCK_CONTROL);
	reg |= SDHCI_CLOCK_INT_EN;
	sdhci_writel(host, reg, SDHCI_CLOCK_CONTROL);
	/* Wait max 20 ms */
	timeout = ktime_add_ms(ktime_get(), 20);
	while (1) {
		bool timedout = ktime_after(ktime_get(), timeout);

		reg = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
		if (reg & SDHCI_CLOCK_INT_STABLE)
			break;
		if (timedout) {
			dev_err(mmc_dev(host->mmc), "Internal clock never stabilised.\n");
			return -ETIMEDOUT;
		}
		usleep_range(900, 1100);
	}

	return 0;
}

/* Set SDCLK-off-while-idle */
static void xenon_set_sdclk_off_idle(struct sdhci_host *host,
				     unsigned char sdhc_id, bool enable)
{
	u32 reg;
	u32 mask;

	reg = sdhci_readl(host, XENON_SYS_OP_CTRL);
	/* Get the bit shift basing on the SDHC index */
	mask = (0x1 << (XENON_SDCLK_IDLEOFF_ENABLE_SHIFT + sdhc_id));
	if (enable)
		reg |= mask;
	else
		reg &= ~mask;

	sdhci_writel(host, reg, XENON_SYS_OP_CTRL);
}

/* Enable/Disable the Auto Clock Gating function */
static void xenon_set_acg(struct sdhci_host *host, bool enable)
{
	u32 reg;

	reg = sdhci_readl(host, XENON_SYS_OP_CTRL);
	if (enable)
		reg &= ~XENON_AUTO_CLKGATE_DISABLE_MASK;
	else
		reg |= XENON_AUTO_CLKGATE_DISABLE_MASK;
	sdhci_writel(host, reg, XENON_SYS_OP_CTRL);
}

/* Enable this SDHC */
static void xenon_enable_sdhc(struct sdhci_host *host,
			      unsigned char sdhc_id)
{
	u32 reg;

	reg = sdhci_readl(host, XENON_SYS_OP_CTRL);
	reg |= (BIT(sdhc_id) << XENON_SLOT_ENABLE_SHIFT);
	sdhci_writel(host, reg, XENON_SYS_OP_CTRL);

	host->mmc->caps |= MMC_CAP_WAIT_WHILE_BUSY;
	/*
	 * Force to clear BUS_TEST to
	 * skip bus_test_pre and bus_test_post
	 */
	host->mmc->caps &= ~MMC_CAP_BUS_WIDTH_TEST;
}

/* Disable this SDHC */
static void xenon_disable_sdhc(struct sdhci_host *host,
			       unsigned char sdhc_id)
{
	u32 reg;

	reg = sdhci_readl(host, XENON_SYS_OP_CTRL);
	reg &= ~(BIT(sdhc_id) << XENON_SLOT_ENABLE_SHIFT);
	sdhci_writel(host, reg, XENON_SYS_OP_CTRL);
}

/* Enable Parallel Transfer Mode */
static void xenon_enable_sdhc_parallel_tran(struct sdhci_host *host,
					    unsigned char sdhc_id)
{
	u32 reg;

	reg = sdhci_readl(host, XENON_SYS_EXT_OP_CTRL);
	reg |= BIT(sdhc_id);
	sdhci_writel(host, reg, XENON_SYS_EXT_OP_CTRL);
}

/* Mask command conflict error */
static void xenon_mask_cmd_conflict_err(struct sdhci_host *host)
{
	u32  reg;

	reg = sdhci_readl(host, XENON_SYS_EXT_OP_CTRL);
	reg |= XENON_MASK_CMD_CONFLICT_ERR;
	sdhci_writel(host, reg, XENON_SYS_EXT_OP_CTRL);
}

static void xenon_retune_setup(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xenon_priv *priv = sdhci_pltfm_priv(pltfm_host);
	u32 reg;

	/* Disable the Re-Tuning Request functionality */
	reg = sdhci_readl(host, XENON_SLOT_RETUNING_REQ_CTRL);
	reg &= ~XENON_RETUNING_COMPATIBLE;
	sdhci_writel(host, reg, XENON_SLOT_RETUNING_REQ_CTRL);

	/* Disable the Re-tuning Interrupt */
	reg = sdhci_readl(host, SDHCI_SIGNAL_ENABLE);
	reg &= ~SDHCI_INT_RETUNE;
	sdhci_writel(host, reg, SDHCI_SIGNAL_ENABLE);
	reg = sdhci_readl(host, SDHCI_INT_ENABLE);
	reg &= ~SDHCI_INT_RETUNE;
	sdhci_writel(host, reg, SDHCI_INT_ENABLE);

	/* Force to use Tuning Mode 1 */
	host->tuning_mode = SDHCI_TUNING_MODE_1;
	/* Set re-tuning period */
	host->tuning_count = 1 << (priv->tuning_count - 1);
}

/*
 * Operations inside struct sdhci_ops
 */
/* Recover the Register Setting cleared during SOFTWARE_RESET_ALL */
static void xenon_reset_exit(struct sdhci_host *host,
			     unsigned char sdhc_id, u8 mask)
{
	/* Only SOFTWARE RESET ALL will clear the register setting */
	if (!(mask & SDHCI_RESET_ALL))
		return;

	/* Disable tuning request and auto-retuning again */
	xenon_retune_setup(host);

	/*
	 * The ACG should be turned off at the early init time, in order
	 * to solve a possible issues with the 1.8V regulator stabilization.
	 * The feature is enabled in later stage.
	 */
	xenon_set_acg(host, false);

	xenon_set_sdclk_off_idle(host, sdhc_id, false);

	xenon_mask_cmd_conflict_err(host);
}

static void xenon_reset(struct sdhci_host *host, u8 mask)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xenon_priv *priv = sdhci_pltfm_priv(pltfm_host);

	sdhci_reset(host, mask);
	xenon_reset_exit(host, priv->sdhc_id, mask);
}

/*
 * Xenon defines different values for HS200 and HS400
 * in Host_Control_2
 */
static void xenon_set_uhs_signaling(struct sdhci_host *host,
				    unsigned int timing)
{
	u16 ctrl_2;

	ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	/* Select Bus Speed Mode for host */
	ctrl_2 &= ~SDHCI_CTRL_UHS_MASK;
	if (timing == MMC_TIMING_MMC_HS200)
		ctrl_2 |= XENON_CTRL_HS200;
	else if (timing == MMC_TIMING_UHS_SDR104)
		ctrl_2 |= SDHCI_CTRL_UHS_SDR104;
	else if (timing == MMC_TIMING_UHS_SDR12)
		ctrl_2 |= SDHCI_CTRL_UHS_SDR12;
	else if (timing == MMC_TIMING_UHS_SDR25)
		ctrl_2 |= SDHCI_CTRL_UHS_SDR25;
	else if (timing == MMC_TIMING_UHS_SDR50)
		ctrl_2 |= SDHCI_CTRL_UHS_SDR50;
	else if ((timing == MMC_TIMING_UHS_DDR50) ||
		 (timing == MMC_TIMING_MMC_DDR52))
		ctrl_2 |= SDHCI_CTRL_UHS_DDR50;
	else if (timing == MMC_TIMING_MMC_HS400)
		ctrl_2 |= XENON_CTRL_HS400;
	sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);
}

static void xenon_set_power(struct sdhci_host *host, unsigned char mode,
		unsigned short vdd)
{
	struct mmc_host *mmc = host->mmc;
	u8 pwr = host->pwr;

	sdhci_set_power_noreg(host, mode, vdd);

	if (host->pwr == pwr)
		return;

	if (host->pwr == 0)
		vdd = 0;

	if (!IS_ERR(mmc->supply.vmmc))
		mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, vdd);
}

static void xenon_voltage_switch(struct sdhci_host *host)
{
	/* Wait for 5ms after set 1.8V signal enable bit */
	usleep_range(5000, 5500);
}

static unsigned int xenon_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	if (pltfm_host->clk)
		return sdhci_pltfm_clk_get_max_clock(host);
	else
		return pltfm_host->clock;
}

static const struct sdhci_ops sdhci_xenon_ops = {
	.voltage_switch		= xenon_voltage_switch,
	.set_clock		= sdhci_set_clock,
	.set_power		= xenon_set_power,
	.set_bus_width		= sdhci_set_bus_width,
	.reset			= xenon_reset,
	.set_uhs_signaling	= xenon_set_uhs_signaling,
	.get_max_clock		= xenon_get_max_clock,
};

static const struct sdhci_pltfm_data sdhci_xenon_pdata = {
	.ops = &sdhci_xenon_ops,
	.quirks = SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
		  SDHCI_QUIRK_NO_SIMULT_VDD_AND_POWER |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
};

/*
 * Xenon Specific Operations in mmc_host_ops
 */
static void xenon_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xenon_priv *priv = sdhci_pltfm_priv(pltfm_host);
	u32 reg;

	/*
	 * HS400/HS200/eMMC HS doesn't have Preset Value register.
	 * However, sdhci_set_ios will read HS400/HS200 Preset register.
	 * Disable Preset Value register for HS400/HS200.
	 * eMMC HS with preset_enabled set will trigger a bug in
	 * get_preset_value().
	 */
	if ((ios->timing == MMC_TIMING_MMC_HS400) ||
	    (ios->timing == MMC_TIMING_MMC_HS200) ||
	    (ios->timing == MMC_TIMING_MMC_HS)) {
		host->preset_enabled = false;
		host->quirks2 |= SDHCI_QUIRK2_PRESET_VALUE_BROKEN;
		host->flags &= ~SDHCI_PV_ENABLED;

		reg = sdhci_readw(host, SDHCI_HOST_CONTROL2);
		reg &= ~SDHCI_CTRL_PRESET_VAL_ENABLE;
		sdhci_writew(host, reg, SDHCI_HOST_CONTROL2);
	} else {
		host->quirks2 &= ~SDHCI_QUIRK2_PRESET_VALUE_BROKEN;
	}

	sdhci_set_ios(mmc, ios);
	xenon_phy_adj(host, ios);

	if (host->clock > XENON_DEFAULT_SDCLK_FREQ)
		xenon_set_sdclk_off_idle(host, priv->sdhc_id, true);
}

static int xenon_start_signal_voltage_switch(struct mmc_host *mmc,
					     struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);

	/*
	 * Before SD/SDIO set signal voltage, SD bus clock should be
	 * disabled. However, sdhci_set_clock will also disable the Internal
	 * clock in mmc_set_signal_voltage().
	 * If Internal clock is disabled, the 3.3V/1.8V bit can not be updated.
	 * Thus here manually enable internal clock.
	 *
	 * After switch completes, it is unnecessary to disable internal clock,
	 * since keeping internal clock active obeys SD spec.
	 */
	xenon_enable_internal_clk(host);

	xenon_soc_pad_ctrl(host, ios->signal_voltage);

	/*
	 * If Vqmmc is fixed on platform, vqmmc regulator should be unavailable.
	 * Thus SDHCI_CTRL_VDD_180 bit might not work then.
	 * Skip the standard voltage switch to avoid any issue.
	 */
	if (PTR_ERR(mmc->supply.vqmmc) == -ENODEV)
		return 0;

	return sdhci_start_signal_voltage_switch(mmc, ios);
}

/*
 * Update card type.
 * priv->init_card_type will be used in PHY timing adjustment.
 */
static void xenon_init_card(struct mmc_host *mmc, struct mmc_card *card)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xenon_priv *priv = sdhci_pltfm_priv(pltfm_host);

	/* Update card type*/
	priv->init_card_type = card->type;
}

static int xenon_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct sdhci_host *host = mmc_priv(mmc);

	if (host->timing == MMC_TIMING_UHS_DDR50 ||
		host->timing == MMC_TIMING_MMC_DDR52)
		return 0;

	/*
	 * Currently force Xenon driver back to support mode 1 only,
	 * even though Xenon might claim to support mode 2 or mode 3.
	 * It requires more time to test mode 2/mode 3 on more platforms.
	 */
	if (host->tuning_mode != SDHCI_TUNING_MODE_1)
		xenon_retune_setup(host);

	return sdhci_execute_tuning(mmc, opcode);
}

static void xenon_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xenon_priv *priv = sdhci_pltfm_priv(pltfm_host);
	u32 reg;
	u8 sdhc_id = priv->sdhc_id;

	sdhci_enable_sdio_irq(mmc, enable);

	if (enable) {
		/*
		 * Set SDIO Card Inserted indication
		 * to enable detecting SDIO async irq.
		 */
		reg = sdhci_readl(host, XENON_SYS_CFG_INFO);
		reg |= (1 << (sdhc_id + XENON_SLOT_TYPE_SDIO_SHIFT));
		sdhci_writel(host, reg, XENON_SYS_CFG_INFO);
	} else {
		/* Clear SDIO Card Inserted indication */
		reg = sdhci_readl(host, XENON_SYS_CFG_INFO);
		reg &= ~(1 << (sdhc_id + XENON_SLOT_TYPE_SDIO_SHIFT));
		sdhci_writel(host, reg, XENON_SYS_CFG_INFO);
	}
}

static void xenon_replace_mmc_host_ops(struct sdhci_host *host)
{
	host->mmc_host_ops.set_ios = xenon_set_ios;
	host->mmc_host_ops.start_signal_voltage_switch =
			xenon_start_signal_voltage_switch;
	host->mmc_host_ops.init_card = xenon_init_card;
	host->mmc_host_ops.execute_tuning = xenon_execute_tuning;
	host->mmc_host_ops.enable_sdio_irq = xenon_enable_sdio_irq;
}

/*
 * Parse Xenon specific DT properties:
 * sdhc-id: the index of current SDHC.
 *	    Refer to XENON_SYS_CFG_INFO register
 * tun-count: the interval between re-tuning
 */
static int xenon_probe_params(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct mmc_host *mmc = host->mmc;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xenon_priv *priv = sdhci_pltfm_priv(pltfm_host);
	u32 sdhc_id, nr_sdhc;
	u32 tuning_count;
	struct sysinfo si;

	/* Disable HS200 on Armada AP806 */
	if (priv->hw_version == XENON_AP806)
		host->quirks2 |= SDHCI_QUIRK2_BROKEN_HS200;

	sdhc_id = 0x0;
	if (!device_property_read_u32(dev, "marvell,xenon-sdhc-id", &sdhc_id)) {
		nr_sdhc = sdhci_readl(host, XENON_SYS_CFG_INFO);
		nr_sdhc &= XENON_NR_SUPPORTED_SLOT_MASK;
		if (unlikely(sdhc_id > nr_sdhc)) {
			dev_err(mmc_dev(mmc), "SDHC Index %d exceeds Number of SDHCs %d\n",
				sdhc_id, nr_sdhc);
			return -EINVAL;
		}
	}
	priv->sdhc_id = sdhc_id;

	tuning_count = XENON_DEF_TUNING_COUNT;
	if (!device_property_read_u32(dev, "marvell,xenon-tun-count",
				      &tuning_count)) {
		if (unlikely(tuning_count >= XENON_TMR_RETUN_NO_PRESENT)) {
			dev_err(mmc_dev(mmc), "Wrong Re-tuning Count. Set default value %d\n",
				XENON_DEF_TUNING_COUNT);
			tuning_count = XENON_DEF_TUNING_COUNT;
		}
	}
	priv->tuning_count = tuning_count;

	/*
	 * AC5/X/IM HW has only 31-bits passed in the crossbar switch.
	 * If we have more than 2GB of memory, this means we might pass
	 * memory pointers which are above 2GB and which cannot be properly
	 * represented. In this case, disable ADMA, 64-bit DMA and allow only SDMA.
	 * This effectively will enable bounce buffer quirk in the
	 * generic SDHCI driver, which will make sure DMA is only done
	 * from supported memory regions:
	 */
	if (priv->hw_version == XENON_AC5) {
		si_meminfo(&si);
		if (si.totalram * si.mem_unit > SZ_2G) {
			host->quirks |= SDHCI_QUIRK_BROKEN_ADMA;
			host->quirks2 |= SDHCI_QUIRK2_BROKEN_64_BIT_DMA;
		}
	}

	return xenon_phy_parse_params(dev, host);
}

static int xenon_sdhc_prepare(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xenon_priv *priv = sdhci_pltfm_priv(pltfm_host);
	u8 sdhc_id = priv->sdhc_id;

	/* Enable SDHC */
	xenon_enable_sdhc(host, sdhc_id);

	/* Enable ACG */
	xenon_set_acg(host, true);

	/* Enable Parallel Transfer Mode */
	xenon_enable_sdhc_parallel_tran(host, sdhc_id);

	/* Disable SDCLK-Off-While-Idle before card init */
	xenon_set_sdclk_off_idle(host, sdhc_id, false);

	xenon_mask_cmd_conflict_err(host);

	return 0;
}

static void xenon_sdhc_unprepare(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xenon_priv *priv = sdhci_pltfm_priv(pltfm_host);
	u8 sdhc_id = priv->sdhc_id;

	/* disable SDHC */
	xenon_disable_sdhc(host, sdhc_id);
}

static int xenon_probe(struct platform_device *pdev)
{
	struct sdhci_pltfm_host *pltfm_host;
	struct device *dev = &pdev->dev;
	struct sdhci_host *host;
	struct xenon_priv *priv;
	int err;

	host = sdhci_pltfm_init(pdev, &sdhci_xenon_pdata,
				sizeof(struct xenon_priv));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	priv = sdhci_pltfm_priv(pltfm_host);

	priv->hw_version = (unsigned long)device_get_match_data(&pdev->dev);

	/*
	 * Link Xenon specific mmc_host_ops function,
	 * to replace standard ones in sdhci_ops.
	 */
	xenon_replace_mmc_host_ops(host);

	if (dev->of_node) {
		pltfm_host->clk = devm_clk_get(&pdev->dev, "core");
		if (IS_ERR(pltfm_host->clk)) {
			err = PTR_ERR(pltfm_host->clk);
			dev_err(&pdev->dev, "Failed to setup input clk: %d\n", err);
			goto free_pltfm;
		}
		err = clk_prepare_enable(pltfm_host->clk);
		if (err)
			goto free_pltfm;

		priv->axi_clk = devm_clk_get(&pdev->dev, "axi");
		if (IS_ERR(priv->axi_clk)) {
			err = PTR_ERR(priv->axi_clk);
			if (err == -EPROBE_DEFER)
				goto err_clk;
		} else {
			err = clk_prepare_enable(priv->axi_clk);
			if (err)
				goto err_clk;
		}
	}

	err = mmc_of_parse(host->mmc);
	if (err)
		goto err_clk_axi;

	sdhci_get_property(pdev);

	xenon_set_acg(host, false);

	/* Xenon specific parameters parse */
	err = xenon_probe_params(pdev);
	if (err)
		goto err_clk_axi;

	err = xenon_sdhc_prepare(host);
	if (err)
		goto err_clk_axi;

	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, 50);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_suspend_ignore_children(&pdev->dev, 1);

	err = sdhci_add_host(host);
	if (err)
		goto remove_sdhc;

	pm_runtime_put_autosuspend(&pdev->dev);
	/*
	 * If we previously detected AC5 with over 2GB of memory,
	 * then we disable ADMA and 64-bit DMA.
	 * This means generic SDHCI driver has set the DMA mask to
	 * 32-bit. Since DDR starts at 0x2_0000_0000, we must use
	 * 34-bit DMA mask to access this DDR memory:
	 */
	if (priv->hw_version == XENON_AC5 &&
	    host->quirks2 & SDHCI_QUIRK2_BROKEN_64_BIT_DMA)
		dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(34));

	return 0;

remove_sdhc:
	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);
	xenon_sdhc_unprepare(host);
err_clk_axi:
	clk_disable_unprepare(priv->axi_clk);
err_clk:
	clk_disable_unprepare(pltfm_host->clk);
free_pltfm:
	sdhci_pltfm_free(pdev);
	return err;
}

static void xenon_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xenon_priv *priv = sdhci_pltfm_priv(pltfm_host);

	pm_runtime_get_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	sdhci_remove_host(host, 0);

	xenon_sdhc_unprepare(host);
	clk_disable_unprepare(priv->axi_clk);
	clk_disable_unprepare(pltfm_host->clk);

	sdhci_pltfm_free(pdev);
}

#ifdef CONFIG_PM_SLEEP
static int xenon_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xenon_priv *priv = sdhci_pltfm_priv(pltfm_host);
	int ret;

	ret = pm_runtime_force_suspend(dev);

	priv->restore_needed = true;
	return ret;
}
#endif

#ifdef CONFIG_PM
static int xenon_runtime_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xenon_priv *priv = sdhci_pltfm_priv(pltfm_host);
	int ret;

	ret = sdhci_runtime_suspend_host(host);
	if (ret)
		return ret;

	if (host->tuning_mode != SDHCI_TUNING_MODE_3)
		mmc_retune_needed(host->mmc);

	clk_disable_unprepare(pltfm_host->clk);
	/*
	 * Need to update the priv->clock here, or when runtime resume
	 * back, phy don't aware the clock change and won't adjust phy
	 * which will cause cmd err
	 */
	priv->clock = 0;
	return 0;
}

static int xenon_runtime_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xenon_priv *priv = sdhci_pltfm_priv(pltfm_host);
	int ret;

	ret = clk_prepare_enable(pltfm_host->clk);
	if (ret) {
		dev_err(dev, "can't enable mainck\n");
		return ret;
	}

	if (priv->restore_needed) {
		ret = xenon_sdhc_prepare(host);
		if (ret)
			goto out;
		priv->restore_needed = false;
	}

	ret = sdhci_runtime_resume_host(host, 0);
	if (ret)
		goto out;
	return 0;
out:
	clk_disable_unprepare(pltfm_host->clk);
	return ret;
}
#endif /* CONFIG_PM */

static const struct dev_pm_ops sdhci_xenon_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xenon_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(xenon_runtime_suspend,
			   xenon_runtime_resume,
			   NULL)
};

static const struct of_device_id sdhci_xenon_dt_ids[] = {
	{ .compatible = "marvell,armada-ap806-sdhci", .data = (void *)XENON_AP806},
	{ .compatible = "marvell,armada-ap807-sdhci", .data = (void *)XENON_AP807},
	{ .compatible = "marvell,armada-cp110-sdhci", .data =  (void *)XENON_CP110},
	{ .compatible = "marvell,armada-3700-sdhci", .data =  (void *)XENON_A3700},
	{ .compatible = "marvell,ac5-sdhci",	     .data =  (void *)XENON_AC5},
	{}
};
MODULE_DEVICE_TABLE(of, sdhci_xenon_dt_ids);

#ifdef CONFIG_ACPI
static const struct acpi_device_id sdhci_xenon_acpi_ids[] = {
	{ .id = "MRVL0002", XENON_AP806},
	{ .id = "MRVL0003", XENON_AP807},
	{ .id = "MRVL0004", XENON_CP110},
	{}
};
MODULE_DEVICE_TABLE(acpi, sdhci_xenon_acpi_ids);
#endif

static struct platform_driver sdhci_xenon_driver = {
	.driver	= {
		.name	= "xenon-sdhci",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = sdhci_xenon_dt_ids,
		.acpi_match_table = ACPI_PTR(sdhci_xenon_acpi_ids),
		.pm = &sdhci_xenon_dev_pm_ops,
	},
	.probe	= xenon_probe,
	.remove = xenon_remove,
};

module_platform_driver(sdhci_xenon_driver);

MODULE_DESCRIPTION("SDHCI platform driver for Marvell Xenon SDHC");
MODULE_AUTHOR("Hu Ziji <huziji@marvell.com>");
MODULE_LICENSE("GPL v2");
