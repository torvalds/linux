/**
 * SDHCI Controller driver for TI's OMAP SoCs
 *
 * Copyright (C) 2017 Texas Instruments
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/delay.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/sys_soc.h>

#include "sdhci-pltfm.h"

#define SDHCI_OMAP_CON		0x12c
#define CON_DW8			BIT(5)
#define CON_DMA_MASTER		BIT(20)
#define CON_DDR			BIT(19)
#define CON_CLKEXTFREE		BIT(16)
#define CON_PADEN		BIT(15)
#define CON_CTPL		BIT(11)
#define CON_INIT		BIT(1)
#define CON_OD			BIT(0)

#define SDHCI_OMAP_DLL		0x0134
#define DLL_SWT			BIT(20)
#define DLL_FORCE_SR_C_SHIFT	13
#define DLL_FORCE_SR_C_MASK	(0x7f << DLL_FORCE_SR_C_SHIFT)
#define DLL_FORCE_VALUE		BIT(12)
#define DLL_CALIB		BIT(1)

#define SDHCI_OMAP_CMD		0x20c

#define SDHCI_OMAP_PSTATE	0x0224
#define PSTATE_DLEV_DAT0	BIT(20)
#define PSTATE_DATI		BIT(1)

#define SDHCI_OMAP_HCTL		0x228
#define HCTL_SDBP		BIT(8)
#define HCTL_SDVS_SHIFT		9
#define HCTL_SDVS_MASK		(0x7 << HCTL_SDVS_SHIFT)
#define HCTL_SDVS_33		(0x7 << HCTL_SDVS_SHIFT)
#define HCTL_SDVS_30		(0x6 << HCTL_SDVS_SHIFT)
#define HCTL_SDVS_18		(0x5 << HCTL_SDVS_SHIFT)

#define SDHCI_OMAP_SYSCTL	0x22c
#define SYSCTL_CEN		BIT(2)
#define SYSCTL_CLKD_SHIFT	6
#define SYSCTL_CLKD_MASK	0x3ff

#define SDHCI_OMAP_STAT		0x230

#define SDHCI_OMAP_IE		0x234
#define INT_CC_EN		BIT(0)

#define SDHCI_OMAP_AC12		0x23c
#define AC12_V1V8_SIGEN		BIT(19)
#define AC12_SCLK_SEL		BIT(23)

#define SDHCI_OMAP_CAPA		0x240
#define CAPA_VS33		BIT(24)
#define CAPA_VS30		BIT(25)
#define CAPA_VS18		BIT(26)

#define SDHCI_OMAP_CAPA2	0x0244
#define CAPA2_TSDR50		BIT(13)

#define SDHCI_OMAP_TIMEOUT	1		/* 1 msec */

#define SYSCTL_CLKD_MAX		0x3FF

#define IOV_1V8			1800000		/* 180000 uV */
#define IOV_3V0			3000000		/* 300000 uV */
#define IOV_3V3			3300000		/* 330000 uV */

#define MAX_PHASE_DELAY		0x7C

/* sdhci-omap controller flags */
#define SDHCI_OMAP_REQUIRE_IODELAY	BIT(0)

struct sdhci_omap_data {
	u32 offset;
	u8 flags;
};

struct sdhci_omap_host {
	char			*version;
	void __iomem		*base;
	struct device		*dev;
	struct	regulator	*pbias;
	bool			pbias_enabled;
	struct sdhci_host	*host;
	u8			bus_mode;
	u8			power_mode;
	u8			timing;
	u8			flags;

	struct pinctrl		*pinctrl;
	struct pinctrl_state	**pinctrl_state;
};

static void sdhci_omap_start_clock(struct sdhci_omap_host *omap_host);
static void sdhci_omap_stop_clock(struct sdhci_omap_host *omap_host);

static inline u32 sdhci_omap_readl(struct sdhci_omap_host *host,
				   unsigned int offset)
{
	return readl(host->base + offset);
}

static inline void sdhci_omap_writel(struct sdhci_omap_host *host,
				     unsigned int offset, u32 data)
{
	writel(data, host->base + offset);
}

static int sdhci_omap_set_pbias(struct sdhci_omap_host *omap_host,
				bool power_on, unsigned int iov)
{
	int ret;
	struct device *dev = omap_host->dev;

	if (IS_ERR(omap_host->pbias))
		return 0;

	if (power_on) {
		ret = regulator_set_voltage(omap_host->pbias, iov, iov);
		if (ret) {
			dev_err(dev, "pbias set voltage failed\n");
			return ret;
		}

		if (omap_host->pbias_enabled)
			return 0;

		ret = regulator_enable(omap_host->pbias);
		if (ret) {
			dev_err(dev, "pbias reg enable fail\n");
			return ret;
		}

		omap_host->pbias_enabled = true;
	} else {
		if (!omap_host->pbias_enabled)
			return 0;

		ret = regulator_disable(omap_host->pbias);
		if (ret) {
			dev_err(dev, "pbias reg disable fail\n");
			return ret;
		}
		omap_host->pbias_enabled = false;
	}

	return 0;
}

static int sdhci_omap_enable_iov(struct sdhci_omap_host *omap_host,
				 unsigned int iov)
{
	int ret;
	struct sdhci_host *host = omap_host->host;
	struct mmc_host *mmc = host->mmc;

	ret = sdhci_omap_set_pbias(omap_host, false, 0);
	if (ret)
		return ret;

	if (!IS_ERR(mmc->supply.vqmmc)) {
		ret = regulator_set_voltage(mmc->supply.vqmmc, iov, iov);
		if (ret) {
			dev_err(mmc_dev(mmc), "vqmmc set voltage failed\n");
			return ret;
		}
	}

	ret = sdhci_omap_set_pbias(omap_host, true, iov);
	if (ret)
		return ret;

	return 0;
}

static void sdhci_omap_conf_bus_power(struct sdhci_omap_host *omap_host,
				      unsigned char signal_voltage)
{
	u32 reg;
	ktime_t timeout;

	reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_HCTL);
	reg &= ~HCTL_SDVS_MASK;

	if (signal_voltage == MMC_SIGNAL_VOLTAGE_330)
		reg |= HCTL_SDVS_33;
	else
		reg |= HCTL_SDVS_18;

	sdhci_omap_writel(omap_host, SDHCI_OMAP_HCTL, reg);

	reg |= HCTL_SDBP;
	sdhci_omap_writel(omap_host, SDHCI_OMAP_HCTL, reg);

	/* wait 1ms */
	timeout = ktime_add_ms(ktime_get(), SDHCI_OMAP_TIMEOUT);
	while (1) {
		bool timedout = ktime_after(ktime_get(), timeout);

		if (sdhci_omap_readl(omap_host, SDHCI_OMAP_HCTL) & HCTL_SDBP)
			break;
		if (WARN_ON(timedout))
			return;
		usleep_range(5, 10);
	}
}

static void sdhci_omap_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_omap_host *omap_host = sdhci_pltfm_priv(pltfm_host);
	u32 reg;

	reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_CON);
	if (enable)
		reg |= (CON_CTPL | CON_CLKEXTFREE);
	else
		reg &= ~(CON_CTPL | CON_CLKEXTFREE);
	sdhci_omap_writel(omap_host, SDHCI_OMAP_CON, reg);

	sdhci_enable_sdio_irq(mmc, enable);
}

static inline void sdhci_omap_set_dll(struct sdhci_omap_host *omap_host,
				      int count)
{
	int i;
	u32 reg;

	reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_DLL);
	reg |= DLL_FORCE_VALUE;
	reg &= ~DLL_FORCE_SR_C_MASK;
	reg |= (count << DLL_FORCE_SR_C_SHIFT);
	sdhci_omap_writel(omap_host, SDHCI_OMAP_DLL, reg);

	reg |= DLL_CALIB;
	sdhci_omap_writel(omap_host, SDHCI_OMAP_DLL, reg);
	for (i = 0; i < 1000; i++) {
		reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_DLL);
		if (reg & DLL_CALIB)
			break;
	}
	reg &= ~DLL_CALIB;
	sdhci_omap_writel(omap_host, SDHCI_OMAP_DLL, reg);
}

static void sdhci_omap_disable_tuning(struct sdhci_omap_host *omap_host)
{
	u32 reg;

	reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_AC12);
	reg &= ~AC12_SCLK_SEL;
	sdhci_omap_writel(omap_host, SDHCI_OMAP_AC12, reg);

	reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_DLL);
	reg &= ~(DLL_FORCE_VALUE | DLL_SWT);
	sdhci_omap_writel(omap_host, SDHCI_OMAP_DLL, reg);
}

static int sdhci_omap_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_omap_host *omap_host = sdhci_pltfm_priv(pltfm_host);
	struct device *dev = omap_host->dev;
	struct mmc_ios *ios = &mmc->ios;
	u32 start_window = 0, max_window = 0;
	bool dcrc_was_enabled = false;
	u8 cur_match, prev_match = 0;
	u32 length = 0, max_len = 0;
	u32 phase_delay = 0;
	int ret = 0;
	u32 reg;

	pltfm_host = sdhci_priv(host);
	omap_host = sdhci_pltfm_priv(pltfm_host);
	dev = omap_host->dev;

	/* clock tuning is not needed for upto 52MHz */
	if (ios->clock <= 52000000)
		return 0;

	reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_CAPA2);
	if (ios->timing == MMC_TIMING_UHS_SDR50 && !(reg & CAPA2_TSDR50))
		return 0;

	reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_DLL);
	reg |= DLL_SWT;
	sdhci_omap_writel(omap_host, SDHCI_OMAP_DLL, reg);

	/*
	 * OMAP5/DRA74X/DRA72x Errata i802:
	 * DCRC error interrupts (MMCHS_STAT[21] DCRC=0x1) can occur
	 * during the tuning procedure. So disable it during the
	 * tuning procedure.
	 */
	if (host->ier & SDHCI_INT_DATA_CRC) {
		host->ier &= ~SDHCI_INT_DATA_CRC;
		dcrc_was_enabled = true;
	}

	while (phase_delay <= MAX_PHASE_DELAY) {
		sdhci_omap_set_dll(omap_host, phase_delay);

		cur_match = !mmc_send_tuning(mmc, opcode, NULL);
		if (cur_match) {
			if (prev_match) {
				length++;
			} else {
				start_window = phase_delay;
				length = 1;
			}
		}

		if (length > max_len) {
			max_window = start_window;
			max_len = length;
		}

		prev_match = cur_match;
		phase_delay += 4;
	}

	if (!max_len) {
		dev_err(dev, "Unable to find match\n");
		ret = -EIO;
		goto tuning_error;
	}

	reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_AC12);
	if (!(reg & AC12_SCLK_SEL)) {
		ret = -EIO;
		goto tuning_error;
	}

	phase_delay = max_window + 4 * (max_len >> 1);
	sdhci_omap_set_dll(omap_host, phase_delay);

	goto ret;

tuning_error:
	dev_err(dev, "Tuning failed\n");
	sdhci_omap_disable_tuning(omap_host);

ret:
	sdhci_reset(host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
	/* Reenable forbidden interrupt */
	if (dcrc_was_enabled)
		host->ier |= SDHCI_INT_DATA_CRC;
	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);
	return ret;
}

static int sdhci_omap_card_busy(struct mmc_host *mmc)
{
	u32 reg, ac12;
	int ret = false;
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_omap_host *omap_host;
	u32 ier = host->ier;

	pltfm_host = sdhci_priv(host);
	omap_host = sdhci_pltfm_priv(pltfm_host);

	reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_CON);
	ac12 = sdhci_omap_readl(omap_host, SDHCI_OMAP_AC12);
	reg &= ~CON_CLKEXTFREE;
	if (ac12 & AC12_V1V8_SIGEN)
		reg |= CON_CLKEXTFREE;
	reg |= CON_PADEN;
	sdhci_omap_writel(omap_host, SDHCI_OMAP_CON, reg);

	disable_irq(host->irq);
	ier |= SDHCI_INT_CARD_INT;
	sdhci_writel(host, ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, ier, SDHCI_SIGNAL_ENABLE);

	/*
	 * Delay is required for PSTATE to correctly reflect
	 * DLEV/CLEV values after PADEN is set.
	 */
	usleep_range(50, 100);
	reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_PSTATE);
	if ((reg & PSTATE_DATI) || !(reg & PSTATE_DLEV_DAT0))
		ret = true;

	reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_CON);
	reg &= ~(CON_CLKEXTFREE | CON_PADEN);
	sdhci_omap_writel(omap_host, SDHCI_OMAP_CON, reg);

	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);
	enable_irq(host->irq);

	return ret;
}

static int sdhci_omap_start_signal_voltage_switch(struct mmc_host *mmc,
						  struct mmc_ios *ios)
{
	u32 reg;
	int ret;
	unsigned int iov;
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_omap_host *omap_host;
	struct device *dev;

	pltfm_host = sdhci_priv(host);
	omap_host = sdhci_pltfm_priv(pltfm_host);
	dev = omap_host->dev;

	if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_330) {
		reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_CAPA);
		if (!(reg & CAPA_VS33))
			return -EOPNOTSUPP;

		sdhci_omap_conf_bus_power(omap_host, ios->signal_voltage);

		reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_AC12);
		reg &= ~AC12_V1V8_SIGEN;
		sdhci_omap_writel(omap_host, SDHCI_OMAP_AC12, reg);

		iov = IOV_3V3;
	} else if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_180) {
		reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_CAPA);
		if (!(reg & CAPA_VS18))
			return -EOPNOTSUPP;

		sdhci_omap_conf_bus_power(omap_host, ios->signal_voltage);

		reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_AC12);
		reg |= AC12_V1V8_SIGEN;
		sdhci_omap_writel(omap_host, SDHCI_OMAP_AC12, reg);

		iov = IOV_1V8;
	} else {
		return -EOPNOTSUPP;
	}

	ret = sdhci_omap_enable_iov(omap_host, iov);
	if (ret) {
		dev_err(dev, "failed to switch IO voltage to %dmV\n", iov);
		return ret;
	}

	dev_dbg(dev, "IO voltage switched to %dmV\n", iov);
	return 0;
}

static void sdhci_omap_set_timing(struct sdhci_omap_host *omap_host, u8 timing)
{
	int ret;
	struct pinctrl_state *pinctrl_state;
	struct device *dev = omap_host->dev;

	if (!(omap_host->flags & SDHCI_OMAP_REQUIRE_IODELAY))
		return;

	if (omap_host->timing == timing)
		return;

	sdhci_omap_stop_clock(omap_host);

	pinctrl_state = omap_host->pinctrl_state[timing];
	ret = pinctrl_select_state(omap_host->pinctrl, pinctrl_state);
	if (ret) {
		dev_err(dev, "failed to select pinctrl state\n");
		return;
	}

	sdhci_omap_start_clock(omap_host);
	omap_host->timing = timing;
}

static void sdhci_omap_set_power_mode(struct sdhci_omap_host *omap_host,
				      u8 power_mode)
{
	if (omap_host->bus_mode == MMC_POWER_OFF)
		sdhci_omap_disable_tuning(omap_host);
	omap_host->power_mode = power_mode;
}

static void sdhci_omap_set_bus_mode(struct sdhci_omap_host *omap_host,
				    unsigned int mode)
{
	u32 reg;

	if (omap_host->bus_mode == mode)
		return;

	reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_CON);
	if (mode == MMC_BUSMODE_OPENDRAIN)
		reg |= CON_OD;
	else
		reg &= ~CON_OD;
	sdhci_omap_writel(omap_host, SDHCI_OMAP_CON, reg);

	omap_host->bus_mode = mode;
}

static void sdhci_omap_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_omap_host *omap_host;

	pltfm_host = sdhci_priv(host);
	omap_host = sdhci_pltfm_priv(pltfm_host);

	sdhci_omap_set_bus_mode(omap_host, ios->bus_mode);
	sdhci_omap_set_timing(omap_host, ios->timing);
	sdhci_set_ios(mmc, ios);
	sdhci_omap_set_power_mode(omap_host, ios->power_mode);
}

static u16 sdhci_omap_calc_divisor(struct sdhci_pltfm_host *host,
				   unsigned int clock)
{
	u16 dsor;

	dsor = DIV_ROUND_UP(clk_get_rate(host->clk), clock);
	if (dsor > SYSCTL_CLKD_MAX)
		dsor = SYSCTL_CLKD_MAX;

	return dsor;
}

static void sdhci_omap_start_clock(struct sdhci_omap_host *omap_host)
{
	u32 reg;

	reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_SYSCTL);
	reg |= SYSCTL_CEN;
	sdhci_omap_writel(omap_host, SDHCI_OMAP_SYSCTL, reg);
}

static void sdhci_omap_stop_clock(struct sdhci_omap_host *omap_host)
{
	u32 reg;

	reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_SYSCTL);
	reg &= ~SYSCTL_CEN;
	sdhci_omap_writel(omap_host, SDHCI_OMAP_SYSCTL, reg);
}

static void sdhci_omap_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_omap_host *omap_host = sdhci_pltfm_priv(pltfm_host);
	unsigned long clkdiv;

	sdhci_omap_stop_clock(omap_host);

	if (!clock)
		return;

	clkdiv = sdhci_omap_calc_divisor(pltfm_host, clock);
	clkdiv = (clkdiv & SYSCTL_CLKD_MASK) << SYSCTL_CLKD_SHIFT;
	sdhci_enable_clk(host, clkdiv);

	sdhci_omap_start_clock(omap_host);
}

static void sdhci_omap_set_power(struct sdhci_host *host, unsigned char mode,
			  unsigned short vdd)
{
	struct mmc_host *mmc = host->mmc;

	mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, vdd);
}

static int sdhci_omap_enable_dma(struct sdhci_host *host)
{
	u32 reg;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_omap_host *omap_host = sdhci_pltfm_priv(pltfm_host);

	reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_CON);
	reg |= CON_DMA_MASTER;
	sdhci_omap_writel(omap_host, SDHCI_OMAP_CON, reg);

	return 0;
}

static unsigned int sdhci_omap_get_min_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return clk_get_rate(pltfm_host->clk) / SYSCTL_CLKD_MAX;
}

static void sdhci_omap_set_bus_width(struct sdhci_host *host, int width)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_omap_host *omap_host = sdhci_pltfm_priv(pltfm_host);
	u32 reg;

	reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_CON);
	if (width == MMC_BUS_WIDTH_8)
		reg |= CON_DW8;
	else
		reg &= ~CON_DW8;
	sdhci_omap_writel(omap_host, SDHCI_OMAP_CON, reg);

	sdhci_set_bus_width(host, width);
}

static void sdhci_omap_init_74_clocks(struct sdhci_host *host, u8 power_mode)
{
	u32 reg;
	ktime_t timeout;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_omap_host *omap_host = sdhci_pltfm_priv(pltfm_host);

	if (omap_host->power_mode == power_mode)
		return;

	if (power_mode != MMC_POWER_ON)
		return;

	disable_irq(host->irq);

	reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_CON);
	reg |= CON_INIT;
	sdhci_omap_writel(omap_host, SDHCI_OMAP_CON, reg);
	sdhci_omap_writel(omap_host, SDHCI_OMAP_CMD, 0x0);

	/* wait 1ms */
	timeout = ktime_add_ms(ktime_get(), SDHCI_OMAP_TIMEOUT);
	while (1) {
		bool timedout = ktime_after(ktime_get(), timeout);

		if (sdhci_omap_readl(omap_host, SDHCI_OMAP_STAT) & INT_CC_EN)
			break;
		if (WARN_ON(timedout))
			return;
		usleep_range(5, 10);
	}

	reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_CON);
	reg &= ~CON_INIT;
	sdhci_omap_writel(omap_host, SDHCI_OMAP_CON, reg);
	sdhci_omap_writel(omap_host, SDHCI_OMAP_STAT, INT_CC_EN);

	enable_irq(host->irq);
}

static void sdhci_omap_set_uhs_signaling(struct sdhci_host *host,
					 unsigned int timing)
{
	u32 reg;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_omap_host *omap_host = sdhci_pltfm_priv(pltfm_host);

	sdhci_omap_stop_clock(omap_host);

	reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_CON);
	if (timing == MMC_TIMING_UHS_DDR50 || timing == MMC_TIMING_MMC_DDR52)
		reg |= CON_DDR;
	else
		reg &= ~CON_DDR;
	sdhci_omap_writel(omap_host, SDHCI_OMAP_CON, reg);

	sdhci_set_uhs_signaling(host, timing);
	sdhci_omap_start_clock(omap_host);
}

static struct sdhci_ops sdhci_omap_ops = {
	.set_clock = sdhci_omap_set_clock,
	.set_power = sdhci_omap_set_power,
	.enable_dma = sdhci_omap_enable_dma,
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.get_min_clock = sdhci_omap_get_min_clock,
	.set_bus_width = sdhci_omap_set_bus_width,
	.platform_send_init_74_clocks = sdhci_omap_init_74_clocks,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_omap_set_uhs_signaling,
};

static int sdhci_omap_set_capabilities(struct sdhci_omap_host *omap_host)
{
	u32 reg;
	int ret = 0;
	struct device *dev = omap_host->dev;
	struct regulator *vqmmc;

	vqmmc = regulator_get(dev, "vqmmc");
	if (IS_ERR(vqmmc)) {
		ret = PTR_ERR(vqmmc);
		goto reg_put;
	}

	/* voltage capabilities might be set by boot loader, clear it */
	reg = sdhci_omap_readl(omap_host, SDHCI_OMAP_CAPA);
	reg &= ~(CAPA_VS18 | CAPA_VS30 | CAPA_VS33);

	if (regulator_is_supported_voltage(vqmmc, IOV_3V3, IOV_3V3))
		reg |= CAPA_VS33;
	if (regulator_is_supported_voltage(vqmmc, IOV_1V8, IOV_1V8))
		reg |= CAPA_VS18;

	sdhci_omap_writel(omap_host, SDHCI_OMAP_CAPA, reg);

reg_put:
	regulator_put(vqmmc);

	return ret;
}

static const struct sdhci_pltfm_data sdhci_omap_pdata = {
	.quirks = SDHCI_QUIRK_BROKEN_CARD_DETECTION |
		  SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		  SDHCI_QUIRK_NO_HISPD_BIT |
		  SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC,
	.quirks2 = SDHCI_QUIRK2_ACMD23_BROKEN |
		   SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
		   SDHCI_QUIRK2_RSP_136_HAS_CRC |
		   SDHCI_QUIRK2_DISABLE_HW_TIMEOUT,
	.ops = &sdhci_omap_ops,
};

static const struct sdhci_omap_data k2g_data = {
	.offset = 0x200,
};

static const struct sdhci_omap_data dra7_data = {
	.offset = 0x200,
	.flags	= SDHCI_OMAP_REQUIRE_IODELAY,
};

static const struct of_device_id omap_sdhci_match[] = {
	{ .compatible = "ti,dra7-sdhci", .data = &dra7_data },
	{ .compatible = "ti,k2g-sdhci", .data = &k2g_data },
	{},
};
MODULE_DEVICE_TABLE(of, omap_sdhci_match);

static struct pinctrl_state
*sdhci_omap_iodelay_pinctrl_state(struct sdhci_omap_host *omap_host, char *mode,
				  u32 *caps, u32 capmask)
{
	struct device *dev = omap_host->dev;
	char *version = omap_host->version;
	struct pinctrl_state *pinctrl_state = ERR_PTR(-ENODEV);
	char str[20];

	if (!(*caps & capmask))
		goto ret;

	if (version) {
		snprintf(str, 20, "%s-%s", mode, version);
		pinctrl_state = pinctrl_lookup_state(omap_host->pinctrl, str);
	}

	if (IS_ERR(pinctrl_state))
		pinctrl_state = pinctrl_lookup_state(omap_host->pinctrl, mode);

	if (IS_ERR(pinctrl_state)) {
		dev_err(dev, "no pinctrl state for %s mode", mode);
		*caps &= ~capmask;
	}

ret:
	return pinctrl_state;
}

static int sdhci_omap_config_iodelay_pinctrl_state(struct sdhci_omap_host
						   *omap_host)
{
	struct device *dev = omap_host->dev;
	struct sdhci_host *host = omap_host->host;
	struct mmc_host *mmc = host->mmc;
	u32 *caps = &mmc->caps;
	u32 *caps2 = &mmc->caps2;
	struct pinctrl_state *state;
	struct pinctrl_state **pinctrl_state;

	if (!(omap_host->flags & SDHCI_OMAP_REQUIRE_IODELAY))
		return 0;

	pinctrl_state = devm_kcalloc(dev,
				     MMC_TIMING_MMC_HS200 + 1,
				     sizeof(*pinctrl_state),
				     GFP_KERNEL);
	if (!pinctrl_state)
		return -ENOMEM;

	omap_host->pinctrl = devm_pinctrl_get(omap_host->dev);
	if (IS_ERR(omap_host->pinctrl)) {
		dev_err(dev, "Cannot get pinctrl\n");
		return PTR_ERR(omap_host->pinctrl);
	}

	state = pinctrl_lookup_state(omap_host->pinctrl, "default");
	if (IS_ERR(state)) {
		dev_err(dev, "no pinctrl state for default mode\n");
		return PTR_ERR(state);
	}
	pinctrl_state[MMC_TIMING_LEGACY] = state;

	state = sdhci_omap_iodelay_pinctrl_state(omap_host, "sdr104", caps,
						 MMC_CAP_UHS_SDR104);
	if (!IS_ERR(state))
		pinctrl_state[MMC_TIMING_UHS_SDR104] = state;

	state = sdhci_omap_iodelay_pinctrl_state(omap_host, "ddr50", caps,
						 MMC_CAP_UHS_DDR50);
	if (!IS_ERR(state))
		pinctrl_state[MMC_TIMING_UHS_DDR50] = state;

	state = sdhci_omap_iodelay_pinctrl_state(omap_host, "sdr50", caps,
						 MMC_CAP_UHS_SDR50);
	if (!IS_ERR(state))
		pinctrl_state[MMC_TIMING_UHS_SDR50] = state;

	state = sdhci_omap_iodelay_pinctrl_state(omap_host, "sdr25", caps,
						 MMC_CAP_UHS_SDR25);
	if (!IS_ERR(state))
		pinctrl_state[MMC_TIMING_UHS_SDR25] = state;

	state = sdhci_omap_iodelay_pinctrl_state(omap_host, "sdr12", caps,
						 MMC_CAP_UHS_SDR12);
	if (!IS_ERR(state))
		pinctrl_state[MMC_TIMING_UHS_SDR12] = state;

	state = sdhci_omap_iodelay_pinctrl_state(omap_host, "ddr_1_8v", caps,
						 MMC_CAP_1_8V_DDR);
	if (!IS_ERR(state)) {
		pinctrl_state[MMC_TIMING_MMC_DDR52] = state;
	} else {
		state = sdhci_omap_iodelay_pinctrl_state(omap_host, "ddr_3_3v",
							 caps,
							 MMC_CAP_3_3V_DDR);
		if (!IS_ERR(state))
			pinctrl_state[MMC_TIMING_MMC_DDR52] = state;
	}

	state = sdhci_omap_iodelay_pinctrl_state(omap_host, "hs", caps,
						 MMC_CAP_SD_HIGHSPEED);
	if (!IS_ERR(state))
		pinctrl_state[MMC_TIMING_SD_HS] = state;

	state = sdhci_omap_iodelay_pinctrl_state(omap_host, "hs", caps,
						 MMC_CAP_MMC_HIGHSPEED);
	if (!IS_ERR(state))
		pinctrl_state[MMC_TIMING_MMC_HS] = state;

	state = sdhci_omap_iodelay_pinctrl_state(omap_host, "hs200_1_8v", caps2,
						 MMC_CAP2_HS200_1_8V_SDR);
	if (!IS_ERR(state))
		pinctrl_state[MMC_TIMING_MMC_HS200] = state;

	omap_host->pinctrl_state = pinctrl_state;

	return 0;
}

static const struct soc_device_attribute sdhci_omap_soc_devices[] = {
	{
		.machine = "DRA7[45]*",
		.revision = "ES1.[01]",
	},
	{
		/* sentinel */
	}
};

static int sdhci_omap_probe(struct platform_device *pdev)
{
	int ret;
	u32 offset;
	struct device *dev = &pdev->dev;
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_omap_host *omap_host;
	struct mmc_host *mmc;
	const struct of_device_id *match;
	struct sdhci_omap_data *data;
	const struct soc_device_attribute *soc;

	match = of_match_device(omap_sdhci_match, dev);
	if (!match)
		return -EINVAL;

	data = (struct sdhci_omap_data *)match->data;
	if (!data) {
		dev_err(dev, "no sdhci omap data\n");
		return -EINVAL;
	}
	offset = data->offset;

	host = sdhci_pltfm_init(pdev, &sdhci_omap_pdata,
				sizeof(*omap_host));
	if (IS_ERR(host)) {
		dev_err(dev, "Failed sdhci_pltfm_init\n");
		return PTR_ERR(host);
	}

	pltfm_host = sdhci_priv(host);
	omap_host = sdhci_pltfm_priv(pltfm_host);
	omap_host->host = host;
	omap_host->base = host->ioaddr;
	omap_host->dev = dev;
	omap_host->power_mode = MMC_POWER_UNDEFINED;
	omap_host->timing = MMC_TIMING_LEGACY;
	omap_host->flags = data->flags;
	host->ioaddr += offset;

	mmc = host->mmc;
	sdhci_get_of_property(pdev);
	ret = mmc_of_parse(mmc);
	if (ret)
		goto err_pltfm_free;

	soc = soc_device_match(sdhci_omap_soc_devices);
	if (soc) {
		omap_host->version = "rev11";
		if (!strcmp(dev_name(dev), "4809c000.mmc"))
			mmc->f_max = 96000000;
		if (!strcmp(dev_name(dev), "480b4000.mmc"))
			mmc->f_max = 48000000;
		if (!strcmp(dev_name(dev), "480ad000.mmc"))
			mmc->f_max = 48000000;
	}

	pltfm_host->clk = devm_clk_get(dev, "fck");
	if (IS_ERR(pltfm_host->clk)) {
		ret = PTR_ERR(pltfm_host->clk);
		goto err_pltfm_free;
	}

	ret = clk_set_rate(pltfm_host->clk, mmc->f_max);
	if (ret) {
		dev_err(dev, "failed to set clock to %d\n", mmc->f_max);
		goto err_pltfm_free;
	}

	omap_host->pbias = devm_regulator_get_optional(dev, "pbias");
	if (IS_ERR(omap_host->pbias)) {
		ret = PTR_ERR(omap_host->pbias);
		if (ret != -ENODEV)
			goto err_pltfm_free;
		dev_dbg(dev, "unable to get pbias regulator %d\n", ret);
	}
	omap_host->pbias_enabled = false;

	/*
	 * omap_device_pm_domain has callbacks to enable the main
	 * functional clock, interface clock and also configure the
	 * SYSCONFIG register of omap devices. The callback will be invoked
	 * as part of pm_runtime_get_sync.
	 */
	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "pm_runtime_get_sync failed\n");
		pm_runtime_put_noidle(dev);
		goto err_rpm_disable;
	}

	ret = sdhci_omap_set_capabilities(omap_host);
	if (ret) {
		dev_err(dev, "failed to set system capabilities\n");
		goto err_put_sync;
	}

	host->mmc_host_ops.get_ro = mmc_gpio_get_ro;
	host->mmc_host_ops.start_signal_voltage_switch =
					sdhci_omap_start_signal_voltage_switch;
	host->mmc_host_ops.set_ios = sdhci_omap_set_ios;
	host->mmc_host_ops.card_busy = sdhci_omap_card_busy;
	host->mmc_host_ops.execute_tuning = sdhci_omap_execute_tuning;
	host->mmc_host_ops.enable_sdio_irq = sdhci_omap_enable_sdio_irq;

	ret = sdhci_setup_host(host);
	if (ret)
		goto err_put_sync;

	ret = sdhci_omap_config_iodelay_pinctrl_state(omap_host);
	if (ret)
		goto err_cleanup_host;

	ret = __sdhci_add_host(host);
	if (ret)
		goto err_cleanup_host;

	return 0;

err_cleanup_host:
	sdhci_cleanup_host(host);

err_put_sync:
	pm_runtime_put_sync(dev);

err_rpm_disable:
	pm_runtime_disable(dev);

err_pltfm_free:
	sdhci_pltfm_free(pdev);
	return ret;
}

static int sdhci_omap_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sdhci_host *host = platform_get_drvdata(pdev);

	sdhci_remove_host(host, true);
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
	sdhci_pltfm_free(pdev);

	return 0;
}

static struct platform_driver sdhci_omap_driver = {
	.probe = sdhci_omap_probe,
	.remove = sdhci_omap_remove,
	.driver = {
		   .name = "sdhci-omap",
		   .of_match_table = omap_sdhci_match,
		  },
};

module_platform_driver(sdhci_omap_driver);

MODULE_DESCRIPTION("SDHCI driver for OMAP SoCs");
MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sdhci_omap");
