/*
 * Support for SDHCI on STMicroelectronics SoCs
 *
 * Copyright (C) 2014 STMicroelectronics Ltd
 * Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
 * Contributors: Peter Griffin <peter.griffin@linaro.org>
 *
 * Based on sdhci-cns3xxx.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/mmc/host.h>
#include <linux/reset.h>
#include "sdhci-pltfm.h"

struct st_mmc_platform_data {
	struct  reset_control *rstc;
	struct  clk *icnclk;
	void __iomem *top_ioaddr;
};

/* MMCSS glue logic to setup the HC on some ST SoCs (e.g. STiH407 family) */

#define ST_MMC_CCONFIG_REG_1		0x400
#define ST_MMC_CCONFIG_TIMEOUT_CLK_UNIT	BIT(24)
#define ST_MMC_CCONFIG_TIMEOUT_CLK_FREQ	BIT(12)
#define ST_MMC_CCONFIG_TUNING_COUNT_DEFAULT	BIT(8)
#define ST_MMC_CCONFIG_ASYNC_WAKEUP	BIT(0)
#define ST_MMC_CCONFIG_1_DEFAULT	\
				((ST_MMC_CCONFIG_TIMEOUT_CLK_UNIT) | \
				 (ST_MMC_CCONFIG_TIMEOUT_CLK_FREQ) | \
				 (ST_MMC_CCONFIG_TUNING_COUNT_DEFAULT))

#define ST_MMC_CCONFIG_REG_2		0x404
#define ST_MMC_CCONFIG_HIGH_SPEED	BIT(28)
#define ST_MMC_CCONFIG_ADMA2		BIT(24)
#define ST_MMC_CCONFIG_8BIT		BIT(20)
#define ST_MMC_CCONFIG_MAX_BLK_LEN	16
#define  MAX_BLK_LEN_1024		1
#define  MAX_BLK_LEN_2048		2
#define BASE_CLK_FREQ_200		0xc8
#define BASE_CLK_FREQ_100		0x64
#define BASE_CLK_FREQ_50		0x32
#define ST_MMC_CCONFIG_2_DEFAULT \
	(ST_MMC_CCONFIG_HIGH_SPEED | ST_MMC_CCONFIG_ADMA2 | \
	 ST_MMC_CCONFIG_8BIT | \
	 (MAX_BLK_LEN_1024 << ST_MMC_CCONFIG_MAX_BLK_LEN))

#define ST_MMC_CCONFIG_REG_3			0x408
#define ST_MMC_CCONFIG_EMMC_SLOT_TYPE		BIT(28)
#define ST_MMC_CCONFIG_64BIT			BIT(24)
#define ST_MMC_CCONFIG_ASYNCH_INTR_SUPPORT	BIT(20)
#define ST_MMC_CCONFIG_1P8_VOLT			BIT(16)
#define ST_MMC_CCONFIG_3P0_VOLT			BIT(12)
#define ST_MMC_CCONFIG_3P3_VOLT			BIT(8)
#define ST_MMC_CCONFIG_SUSP_RES_SUPPORT		BIT(4)
#define ST_MMC_CCONFIG_SDMA			BIT(0)
#define ST_MMC_CCONFIG_3_DEFAULT	\
			 (ST_MMC_CCONFIG_ASYNCH_INTR_SUPPORT	| \
			  ST_MMC_CCONFIG_3P3_VOLT		| \
			  ST_MMC_CCONFIG_SUSP_RES_SUPPORT	| \
			  ST_MMC_CCONFIG_SDMA)

#define ST_MMC_CCONFIG_REG_4	0x40c
#define ST_MMC_CCONFIG_D_DRIVER	BIT(20)
#define ST_MMC_CCONFIG_C_DRIVER	BIT(16)
#define ST_MMC_CCONFIG_A_DRIVER	BIT(12)
#define ST_MMC_CCONFIG_DDR50	BIT(8)
#define ST_MMC_CCONFIG_SDR104	BIT(4)
#define ST_MMC_CCONFIG_SDR50	BIT(0)
#define ST_MMC_CCONFIG_4_DEFAULT	0

#define ST_MMC_CCONFIG_REG_5		0x410
#define ST_MMC_CCONFIG_TUNING_FOR_SDR50	BIT(8)
#define RETUNING_TIMER_CNT_MAX		0xf
#define ST_MMC_CCONFIG_5_DEFAULT	0

/* I/O configuration for Arasan IP */
#define ST_MMC_GP_OUTPUT	0x450
#define ST_MMC_GP_OUTPUT_CD	BIT(12)

#define ST_MMC_STATUS_R		0x460

#define ST_TOP_MMC_DLY_FIX_OFF(x)	(x - 0x8)

/* TOP config registers to manage static and dynamic delay */
#define ST_TOP_MMC_TX_CLK_DLY			ST_TOP_MMC_DLY_FIX_OFF(0x8)
#define ST_TOP_MMC_RX_CLK_DLY			ST_TOP_MMC_DLY_FIX_OFF(0xc)
/* MMC delay control register */
#define ST_TOP_MMC_DLY_CTRL			ST_TOP_MMC_DLY_FIX_OFF(0x18)
#define ST_TOP_MMC_DLY_CTRL_DLL_BYPASS_CMD	BIT(0)
#define ST_TOP_MMC_DLY_CTRL_DLL_BYPASS_PH_SEL	BIT(1)
#define ST_TOP_MMC_DLY_CTRL_TX_DLL_ENABLE	BIT(8)
#define ST_TOP_MMC_DLY_CTRL_RX_DLL_ENABLE	BIT(9)
#define ST_TOP_MMC_DLY_CTRL_ATUNE_NOT_CFG_DLY	BIT(10)
#define ST_TOP_MMC_START_DLL_LOCK		BIT(11)

/* register to provide the phase-shift value for DLL */
#define ST_TOP_MMC_TX_DLL_STEP_DLY		ST_TOP_MMC_DLY_FIX_OFF(0x1c)
#define ST_TOP_MMC_RX_DLL_STEP_DLY		ST_TOP_MMC_DLY_FIX_OFF(0x20)
#define ST_TOP_MMC_RX_CMD_STEP_DLY		ST_TOP_MMC_DLY_FIX_OFF(0x24)

/* phase shift delay on the tx clk 2.188ns */
#define ST_TOP_MMC_TX_DLL_STEP_DLY_VALID	0x6

#define ST_TOP_MMC_DLY_MAX			0xf

#define ST_TOP_MMC_DYN_DLY_CONF	\
		(ST_TOP_MMC_DLY_CTRL_TX_DLL_ENABLE | \
		 ST_TOP_MMC_DLY_CTRL_ATUNE_NOT_CFG_DLY | \
		 ST_TOP_MMC_START_DLL_LOCK)

/*
 * For clock speeds greater than 90MHz, we need to check that the
 * DLL procedure has finished before switching to ultra-speed modes.
 */
#define	CLK_TO_CHECK_DLL_LOCK	90000000

static inline void st_mmcss_set_static_delay(void __iomem *ioaddr)
{
	if (!ioaddr)
		return;

	writel_relaxed(0x0, ioaddr + ST_TOP_MMC_DLY_CTRL);
	writel_relaxed(ST_TOP_MMC_DLY_MAX,
			ioaddr + ST_TOP_MMC_TX_CLK_DLY);
}

/**
 * st_mmcss_cconfig: configure the Arasan HC inside the flashSS.
 * @np: dt device node.
 * @host: sdhci host
 * Description: this function is to configure the Arasan host controller.
 * On some ST SoCs, i.e. STiH407 family, the MMC devices inside a dedicated
 * flashSS sub-system which needs to be configured to be compliant to eMMC 4.5
 * or eMMC4.3.  This has to be done before registering the sdhci host.
 */
static void st_mmcss_cconfig(struct device_node *np, struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct mmc_host *mhost = host->mmc;
	u32 cconf2, cconf3, cconf4, cconf5;

	if (!of_device_is_compatible(np, "st,sdhci-stih407"))
		return;

	cconf2 = ST_MMC_CCONFIG_2_DEFAULT;
	cconf3 = ST_MMC_CCONFIG_3_DEFAULT;
	cconf4 = ST_MMC_CCONFIG_4_DEFAULT;
	cconf5 = ST_MMC_CCONFIG_5_DEFAULT;

	writel_relaxed(ST_MMC_CCONFIG_1_DEFAULT,
			host->ioaddr + ST_MMC_CCONFIG_REG_1);

	/* Set clock frequency, default to 50MHz if max-frequency is not
	 * provided */

	switch (mhost->f_max) {
	case 200000000:
		clk_set_rate(pltfm_host->clk, mhost->f_max);
		cconf2 |= BASE_CLK_FREQ_200;
		break;
	case 100000000:
		clk_set_rate(pltfm_host->clk, mhost->f_max);
		cconf2 |= BASE_CLK_FREQ_100;
		break;
	default:
		clk_set_rate(pltfm_host->clk, 50000000);
		cconf2 |= BASE_CLK_FREQ_50;
	}

	writel_relaxed(cconf2, host->ioaddr + ST_MMC_CCONFIG_REG_2);

	if (!mmc_card_is_removable(mhost))
		cconf3 |= ST_MMC_CCONFIG_EMMC_SLOT_TYPE;
	else
		/* CARD _D ET_CTRL */
		writel_relaxed(ST_MMC_GP_OUTPUT_CD,
				host->ioaddr + ST_MMC_GP_OUTPUT);

	if (mhost->caps & MMC_CAP_UHS_SDR50) {
		/* use 1.8V */
		cconf3 |= ST_MMC_CCONFIG_1P8_VOLT;
		cconf4 |= ST_MMC_CCONFIG_SDR50;
		/* Use tuning */
		cconf5 |= ST_MMC_CCONFIG_TUNING_FOR_SDR50;
		/* Max timeout for retuning */
		cconf5 |= RETUNING_TIMER_CNT_MAX;
	}

	if (mhost->caps & MMC_CAP_UHS_SDR104) {
		/*
		 * SDR104 implies the HC can support HS200 mode, so
		 * it's mandatory to use 1.8V
		 */
		cconf3 |= ST_MMC_CCONFIG_1P8_VOLT;
		cconf4 |= ST_MMC_CCONFIG_SDR104;
		/* Max timeout for retuning */
		cconf5 |= RETUNING_TIMER_CNT_MAX;
	}

	if (mhost->caps & MMC_CAP_UHS_DDR50)
		cconf4 |= ST_MMC_CCONFIG_DDR50;

	writel_relaxed(cconf3, host->ioaddr + ST_MMC_CCONFIG_REG_3);
	writel_relaxed(cconf4, host->ioaddr + ST_MMC_CCONFIG_REG_4);
	writel_relaxed(cconf5, host->ioaddr + ST_MMC_CCONFIG_REG_5);
}

static inline void st_mmcss_set_dll(void __iomem *ioaddr)
{
	if (!ioaddr)
		return;

	writel_relaxed(ST_TOP_MMC_DYN_DLY_CONF,	ioaddr + ST_TOP_MMC_DLY_CTRL);
	writel_relaxed(ST_TOP_MMC_TX_DLL_STEP_DLY_VALID,
			ioaddr + ST_TOP_MMC_TX_DLL_STEP_DLY);
}

static int st_mmcss_lock_dll(void __iomem *ioaddr)
{
	unsigned long curr, value;
	unsigned long finish = jiffies + HZ;

	/* Checks if the DLL procedure is finished */
	do {
		curr = jiffies;
		value = readl(ioaddr + ST_MMC_STATUS_R);
		if (value & 0x1)
			return 0;

		cpu_relax();
	} while (!time_after_eq(curr, finish));

	return -EBUSY;
}

static int sdhci_st_set_dll_for_clock(struct sdhci_host *host)
{
	int ret = 0;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct st_mmc_platform_data *pdata = sdhci_pltfm_priv(pltfm_host);

	if (host->clock > CLK_TO_CHECK_DLL_LOCK) {
		st_mmcss_set_dll(pdata->top_ioaddr);
		ret = st_mmcss_lock_dll(host->ioaddr);
	}

	return ret;
}

static void sdhci_st_set_uhs_signaling(struct sdhci_host *host,
					unsigned int uhs)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct st_mmc_platform_data *pdata = sdhci_pltfm_priv(pltfm_host);
	u16 ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	int ret = 0;

	/* Select Bus Speed Mode for host */
	ctrl_2 &= ~SDHCI_CTRL_UHS_MASK;
	switch (uhs) {
	/*
	 * Set V18_EN -- UHS modes do not work without this.
	 * does not change signaling voltage
	 */

	case MMC_TIMING_UHS_SDR12:
		st_mmcss_set_static_delay(pdata->top_ioaddr);
		ctrl_2 |= SDHCI_CTRL_UHS_SDR12 | SDHCI_CTRL_VDD_180;
		break;
	case MMC_TIMING_UHS_SDR25:
		st_mmcss_set_static_delay(pdata->top_ioaddr);
		ctrl_2 |= SDHCI_CTRL_UHS_SDR25 | SDHCI_CTRL_VDD_180;
		break;
	case MMC_TIMING_UHS_SDR50:
		st_mmcss_set_static_delay(pdata->top_ioaddr);
		ctrl_2 |= SDHCI_CTRL_UHS_SDR50 | SDHCI_CTRL_VDD_180;
		ret = sdhci_st_set_dll_for_clock(host);
		break;
	case MMC_TIMING_UHS_SDR104:
	case MMC_TIMING_MMC_HS200:
		st_mmcss_set_static_delay(pdata->top_ioaddr);
		ctrl_2 |= SDHCI_CTRL_UHS_SDR104 | SDHCI_CTRL_VDD_180;
		ret =  sdhci_st_set_dll_for_clock(host);
		break;
	case MMC_TIMING_UHS_DDR50:
	case MMC_TIMING_MMC_DDR52:
		st_mmcss_set_static_delay(pdata->top_ioaddr);
		ctrl_2 |= SDHCI_CTRL_UHS_DDR50 | SDHCI_CTRL_VDD_180;
		break;
	}

	if (ret)
		dev_warn(mmc_dev(host->mmc), "Error setting dll for clock "
						"(uhs %d)\n", uhs);

	dev_dbg(mmc_dev(host->mmc), "uhs %d, ctrl_2 %04X\n", uhs, ctrl_2);

	sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);
}

static u32 sdhci_st_readl(struct sdhci_host *host, int reg)
{
	u32 ret;

	switch (reg) {
	case SDHCI_CAPABILITIES:
		ret = readl_relaxed(host->ioaddr + reg);
		/* Support 3.3V and 1.8V */
		ret &= ~SDHCI_CAN_VDD_300;
		break;
	default:
		ret = readl_relaxed(host->ioaddr + reg);
	}
	return ret;
}

static const struct sdhci_ops sdhci_st_ops = {
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.set_clock = sdhci_set_clock,
	.set_bus_width = sdhci_set_bus_width,
	.read_l = sdhci_st_readl,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_st_set_uhs_signaling,
};

static const struct sdhci_pltfm_data sdhci_st_pdata = {
	.ops = &sdhci_st_ops,
	.quirks = SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
		SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		SDHCI_QUIRK_NO_HISPD_BIT,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
		SDHCI_QUIRK2_STOP_WITH_TC,
};


static int sdhci_st_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct sdhci_host *host;
	struct st_mmc_platform_data *pdata;
	struct sdhci_pltfm_host *pltfm_host;
	struct clk *clk, *icnclk;
	int ret = 0;
	u16 host_version;
	struct resource *res;
	struct reset_control *rstc;

	clk =  devm_clk_get(&pdev->dev, "mmc");
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Peripheral clk not found\n");
		return PTR_ERR(clk);
	}

	/* ICN clock isn't compulsory, but use it if it's provided. */
	icnclk = devm_clk_get(&pdev->dev, "icn");
	if (IS_ERR(icnclk))
		icnclk = NULL;

	rstc = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(rstc))
		rstc = NULL;
	else
		reset_control_deassert(rstc);

	host = sdhci_pltfm_init(pdev, &sdhci_st_pdata, sizeof(*pdata));
	if (IS_ERR(host)) {
		dev_err(&pdev->dev, "Failed sdhci_pltfm_init\n");
		ret = PTR_ERR(host);
		goto err_pltfm_init;
	}

	pltfm_host = sdhci_priv(host);
	pdata = sdhci_pltfm_priv(pltfm_host);
	pdata->rstc = rstc;

	ret = mmc_of_parse(host->mmc);
	if (ret) {
		dev_err(&pdev->dev, "Failed mmc_of_parse\n");
		goto err_of;
	}

	clk_prepare_enable(clk);
	clk_prepare_enable(icnclk);

	/* Configure the FlashSS Top registers for setting eMMC TX/RX delay */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "top-mmc-delay");
	pdata->top_ioaddr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pdata->top_ioaddr)) {
		dev_warn(&pdev->dev, "FlashSS Top Dly registers not available");
		pdata->top_ioaddr = NULL;
	}

	pltfm_host->clk = clk;
	pdata->icnclk = icnclk;

	/* Configure the Arasan HC inside the flashSS */
	st_mmcss_cconfig(np, host);

	ret = sdhci_add_host(host);
	if (ret) {
		dev_err(&pdev->dev, "Failed sdhci_add_host\n");
		goto err_out;
	}

	host_version = readw_relaxed((host->ioaddr + SDHCI_HOST_VERSION));

	dev_info(&pdev->dev, "SDHCI ST Initialised: Host Version: 0x%x Vendor Version 0x%x\n",
		((host_version & SDHCI_SPEC_VER_MASK) >> SDHCI_SPEC_VER_SHIFT),
		((host_version & SDHCI_VENDOR_VER_MASK) >>
		SDHCI_VENDOR_VER_SHIFT));

	return 0;

err_out:
	clk_disable_unprepare(icnclk);
	clk_disable_unprepare(clk);
err_of:
	sdhci_pltfm_free(pdev);
err_pltfm_init:
	if (rstc)
		reset_control_assert(rstc);

	return ret;
}

static int sdhci_st_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct st_mmc_platform_data *pdata = sdhci_pltfm_priv(pltfm_host);
	struct reset_control *rstc = pdata->rstc;
	int ret;

	ret = sdhci_pltfm_unregister(pdev);

	clk_disable_unprepare(pdata->icnclk);

	if (rstc)
		reset_control_assert(rstc);

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int sdhci_st_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct st_mmc_platform_data *pdata = sdhci_pltfm_priv(pltfm_host);
	int ret;

	if (host->tuning_mode != SDHCI_TUNING_MODE_3)
		mmc_retune_needed(host->mmc);

	ret = sdhci_suspend_host(host);
	if (ret)
		goto out;

	if (pdata->rstc)
		reset_control_assert(pdata->rstc);

	clk_disable_unprepare(pdata->icnclk);
	clk_disable_unprepare(pltfm_host->clk);
out:
	return ret;
}

static int sdhci_st_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct st_mmc_platform_data *pdata = sdhci_pltfm_priv(pltfm_host);
	struct device_node *np = dev->of_node;

	clk_prepare_enable(pltfm_host->clk);
	clk_prepare_enable(pdata->icnclk);

	if (pdata->rstc)
		reset_control_deassert(pdata->rstc);

	st_mmcss_cconfig(np, host);

	return sdhci_resume_host(host);
}
#endif

static SIMPLE_DEV_PM_OPS(sdhci_st_pmops, sdhci_st_suspend, sdhci_st_resume);

static const struct of_device_id st_sdhci_match[] = {
	{ .compatible = "st,sdhci" },
	{},
};

MODULE_DEVICE_TABLE(of, st_sdhci_match);

static struct platform_driver sdhci_st_driver = {
	.probe = sdhci_st_probe,
	.remove = sdhci_st_remove,
	.driver = {
		   .name = "sdhci-st",
		   .pm = &sdhci_st_pmops,
		   .of_match_table = of_match_ptr(st_sdhci_match),
		  },
};

module_platform_driver(sdhci_st_driver);

MODULE_DESCRIPTION("SDHCI driver for STMicroelectronics SoCs");
MODULE_AUTHOR("Giuseppe Cavallaro <peppe.cavallaro@st.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sdhci-st");
