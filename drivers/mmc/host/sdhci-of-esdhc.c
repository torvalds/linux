/*
 * Freescale eSDHC controller driver.
 *
 * Copyright (c) 2007, 2010, 2012 Freescale Semiconductor, Inc.
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

#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/sys_soc.h>
#include <linux/clk.h>
#include <linux/ktime.h>
#include <linux/dma-mapping.h>
#include <linux/mmc/host.h>
#include "sdhci-pltfm.h"
#include "sdhci-esdhc.h"

#define VENDOR_V_22	0x12
#define VENDOR_V_23	0x13

#define MMC_TIMING_NUM (MMC_TIMING_MMC_HS400 + 1)

struct esdhc_clk_fixup {
	const unsigned int sd_dflt_max_clk;
	const unsigned int max_clk[MMC_TIMING_NUM];
};

static const struct esdhc_clk_fixup ls1021a_esdhc_clk = {
	.sd_dflt_max_clk = 25000000,
	.max_clk[MMC_TIMING_MMC_HS] = 46500000,
	.max_clk[MMC_TIMING_SD_HS] = 46500000,
};

static const struct esdhc_clk_fixup ls1046a_esdhc_clk = {
	.sd_dflt_max_clk = 25000000,
	.max_clk[MMC_TIMING_UHS_SDR104] = 167000000,
	.max_clk[MMC_TIMING_MMC_HS200] = 167000000,
};

static const struct esdhc_clk_fixup ls1012a_esdhc_clk = {
	.sd_dflt_max_clk = 25000000,
	.max_clk[MMC_TIMING_UHS_SDR104] = 125000000,
	.max_clk[MMC_TIMING_MMC_HS200] = 125000000,
};

static const struct esdhc_clk_fixup p1010_esdhc_clk = {
	.sd_dflt_max_clk = 20000000,
	.max_clk[MMC_TIMING_LEGACY] = 20000000,
	.max_clk[MMC_TIMING_MMC_HS] = 42000000,
	.max_clk[MMC_TIMING_SD_HS] = 40000000,
};

static const struct of_device_id sdhci_esdhc_of_match[] = {
	{ .compatible = "fsl,ls1021a-esdhc", .data = &ls1021a_esdhc_clk},
	{ .compatible = "fsl,ls1046a-esdhc", .data = &ls1046a_esdhc_clk},
	{ .compatible = "fsl,ls1012a-esdhc", .data = &ls1012a_esdhc_clk},
	{ .compatible = "fsl,p1010-esdhc",   .data = &p1010_esdhc_clk},
	{ .compatible = "fsl,mpc8379-esdhc" },
	{ .compatible = "fsl,mpc8536-esdhc" },
	{ .compatible = "fsl,esdhc" },
	{ }
};
MODULE_DEVICE_TABLE(of, sdhci_esdhc_of_match);

struct sdhci_esdhc {
	u8 vendor_ver;
	u8 spec_ver;
	bool quirk_incorrect_hostver;
	unsigned int peripheral_clock;
	const struct esdhc_clk_fixup *clk_fixup;
};

/**
 * esdhc_read*_fixup - Fixup the value read from incompatible eSDHC register
 *		       to make it compatible with SD spec.
 *
 * @host: pointer to sdhci_host
 * @spec_reg: SD spec register address
 * @value: 32bit eSDHC register value on spec_reg address
 *
 * In SD spec, there are 8/16/32/64 bits registers, while all of eSDHC
 * registers are 32 bits. There are differences in register size, register
 * address, register function, bit position and function between eSDHC spec
 * and SD spec.
 *
 * Return a fixed up register value
 */
static u32 esdhc_readl_fixup(struct sdhci_host *host,
				     int spec_reg, u32 value)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_esdhc *esdhc = sdhci_pltfm_priv(pltfm_host);
	u32 ret;

	/*
	 * The bit of ADMA flag in eSDHC is not compatible with standard
	 * SDHC register, so set fake flag SDHCI_CAN_DO_ADMA2 when ADMA is
	 * supported by eSDHC.
	 * And for many FSL eSDHC controller, the reset value of field
	 * SDHCI_CAN_DO_ADMA1 is 1, but some of them can't support ADMA,
	 * only these vendor version is greater than 2.2/0x12 support ADMA.
	 */
	if ((spec_reg == SDHCI_CAPABILITIES) && (value & SDHCI_CAN_DO_ADMA1)) {
		if (esdhc->vendor_ver > VENDOR_V_22) {
			ret = value | SDHCI_CAN_DO_ADMA2;
			return ret;
		}
	}
	/*
	 * The DAT[3:0] line signal levels and the CMD line signal level are
	 * not compatible with standard SDHC register. The line signal levels
	 * DAT[7:0] are at bits 31:24 and the command line signal level is at
	 * bit 23. All other bits are the same as in the standard SDHC
	 * register.
	 */
	if (spec_reg == SDHCI_PRESENT_STATE) {
		ret = value & 0x000fffff;
		ret |= (value >> 4) & SDHCI_DATA_LVL_MASK;
		ret |= (value << 1) & SDHCI_CMD_LVL;
		return ret;
	}

	/*
	 * DTS properties of mmc host are used to enable each speed mode
	 * according to soc and board capability. So clean up
	 * SDR50/SDR104/DDR50 support bits here.
	 */
	if (spec_reg == SDHCI_CAPABILITIES_1) {
		ret = value & ~(SDHCI_SUPPORT_SDR50 | SDHCI_SUPPORT_SDR104 |
				SDHCI_SUPPORT_DDR50);
		return ret;
	}

	ret = value;
	return ret;
}

static u16 esdhc_readw_fixup(struct sdhci_host *host,
				     int spec_reg, u32 value)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_esdhc *esdhc = sdhci_pltfm_priv(pltfm_host);
	u16 ret;
	int shift = (spec_reg & 0x2) * 8;

	if (spec_reg == SDHCI_HOST_VERSION)
		ret = value & 0xffff;
	else
		ret = (value >> shift) & 0xffff;
	/* Workaround for T4240-R1.0-R2.0 eSDHC which has incorrect
	 * vendor version and spec version information.
	 */
	if ((spec_reg == SDHCI_HOST_VERSION) &&
	    (esdhc->quirk_incorrect_hostver))
		ret = (VENDOR_V_23 << SDHCI_VENDOR_VER_SHIFT) | SDHCI_SPEC_200;
	return ret;
}

static u8 esdhc_readb_fixup(struct sdhci_host *host,
				     int spec_reg, u32 value)
{
	u8 ret;
	u8 dma_bits;
	int shift = (spec_reg & 0x3) * 8;

	ret = (value >> shift) & 0xff;

	/*
	 * "DMA select" locates at offset 0x28 in SD specification, but on
	 * P5020 or P3041, it locates at 0x29.
	 */
	if (spec_reg == SDHCI_HOST_CONTROL) {
		/* DMA select is 22,23 bits in Protocol Control Register */
		dma_bits = (value >> 5) & SDHCI_CTRL_DMA_MASK;
		/* fixup the result */
		ret &= ~SDHCI_CTRL_DMA_MASK;
		ret |= dma_bits;
	}
	return ret;
}

/**
 * esdhc_write*_fixup - Fixup the SD spec register value so that it could be
 *			written into eSDHC register.
 *
 * @host: pointer to sdhci_host
 * @spec_reg: SD spec register address
 * @value: 8/16/32bit SD spec register value that would be written
 * @old_value: 32bit eSDHC register value on spec_reg address
 *
 * In SD spec, there are 8/16/32/64 bits registers, while all of eSDHC
 * registers are 32 bits. There are differences in register size, register
 * address, register function, bit position and function between eSDHC spec
 * and SD spec.
 *
 * Return a fixed up register value
 */
static u32 esdhc_writel_fixup(struct sdhci_host *host,
				     int spec_reg, u32 value, u32 old_value)
{
	u32 ret;

	/*
	 * Enabling IRQSTATEN[BGESEN] is just to set IRQSTAT[BGE]
	 * when SYSCTL[RSTD] is set for some special operations.
	 * No any impact on other operation.
	 */
	if (spec_reg == SDHCI_INT_ENABLE)
		ret = value | SDHCI_INT_BLK_GAP;
	else
		ret = value;

	return ret;
}

static u32 esdhc_writew_fixup(struct sdhci_host *host,
				     int spec_reg, u16 value, u32 old_value)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	int shift = (spec_reg & 0x2) * 8;
	u32 ret;

	switch (spec_reg) {
	case SDHCI_TRANSFER_MODE:
		/*
		 * Postpone this write, we must do it together with a
		 * command write that is down below. Return old value.
		 */
		pltfm_host->xfer_mode_shadow = value;
		return old_value;
	case SDHCI_COMMAND:
		ret = (value << 16) | pltfm_host->xfer_mode_shadow;
		return ret;
	}

	ret = old_value & (~(0xffff << shift));
	ret |= (value << shift);

	if (spec_reg == SDHCI_BLOCK_SIZE) {
		/*
		 * Two last DMA bits are reserved, and first one is used for
		 * non-standard blksz of 4096 bytes that we don't support
		 * yet. So clear the DMA boundary bits.
		 */
		ret &= (~SDHCI_MAKE_BLKSZ(0x7, 0));
	}
	return ret;
}

static u32 esdhc_writeb_fixup(struct sdhci_host *host,
				     int spec_reg, u8 value, u32 old_value)
{
	u32 ret;
	u32 dma_bits;
	u8 tmp;
	int shift = (spec_reg & 0x3) * 8;

	/*
	 * eSDHC doesn't have a standard power control register, so we do
	 * nothing here to avoid incorrect operation.
	 */
	if (spec_reg == SDHCI_POWER_CONTROL)
		return old_value;
	/*
	 * "DMA select" location is offset 0x28 in SD specification, but on
	 * P5020 or P3041, it's located at 0x29.
	 */
	if (spec_reg == SDHCI_HOST_CONTROL) {
		/*
		 * If host control register is not standard, exit
		 * this function
		 */
		if (host->quirks2 & SDHCI_QUIRK2_BROKEN_HOST_CONTROL)
			return old_value;

		/* DMA select is 22,23 bits in Protocol Control Register */
		dma_bits = (value & SDHCI_CTRL_DMA_MASK) << 5;
		ret = (old_value & (~(SDHCI_CTRL_DMA_MASK << 5))) | dma_bits;
		tmp = (value & (~SDHCI_CTRL_DMA_MASK)) |
		      (old_value & SDHCI_CTRL_DMA_MASK);
		ret = (ret & (~0xff)) | tmp;

		/* Prevent SDHCI core from writing reserved bits (e.g. HISPD) */
		ret &= ~ESDHC_HOST_CONTROL_RES;
		return ret;
	}

	ret = (old_value & (~(0xff << shift))) | (value << shift);
	return ret;
}

static u32 esdhc_be_readl(struct sdhci_host *host, int reg)
{
	u32 ret;
	u32 value;

	if (reg == SDHCI_CAPABILITIES_1)
		value = ioread32be(host->ioaddr + ESDHC_CAPABILITIES_1);
	else
		value = ioread32be(host->ioaddr + reg);

	ret = esdhc_readl_fixup(host, reg, value);

	return ret;
}

static u32 esdhc_le_readl(struct sdhci_host *host, int reg)
{
	u32 ret;
	u32 value;

	if (reg == SDHCI_CAPABILITIES_1)
		value = ioread32(host->ioaddr + ESDHC_CAPABILITIES_1);
	else
		value = ioread32(host->ioaddr + reg);

	ret = esdhc_readl_fixup(host, reg, value);

	return ret;
}

static u16 esdhc_be_readw(struct sdhci_host *host, int reg)
{
	u16 ret;
	u32 value;
	int base = reg & ~0x3;

	value = ioread32be(host->ioaddr + base);
	ret = esdhc_readw_fixup(host, reg, value);
	return ret;
}

static u16 esdhc_le_readw(struct sdhci_host *host, int reg)
{
	u16 ret;
	u32 value;
	int base = reg & ~0x3;

	value = ioread32(host->ioaddr + base);
	ret = esdhc_readw_fixup(host, reg, value);
	return ret;
}

static u8 esdhc_be_readb(struct sdhci_host *host, int reg)
{
	u8 ret;
	u32 value;
	int base = reg & ~0x3;

	value = ioread32be(host->ioaddr + base);
	ret = esdhc_readb_fixup(host, reg, value);
	return ret;
}

static u8 esdhc_le_readb(struct sdhci_host *host, int reg)
{
	u8 ret;
	u32 value;
	int base = reg & ~0x3;

	value = ioread32(host->ioaddr + base);
	ret = esdhc_readb_fixup(host, reg, value);
	return ret;
}

static void esdhc_be_writel(struct sdhci_host *host, u32 val, int reg)
{
	u32 value;

	value = esdhc_writel_fixup(host, reg, val, 0);
	iowrite32be(value, host->ioaddr + reg);
}

static void esdhc_le_writel(struct sdhci_host *host, u32 val, int reg)
{
	u32 value;

	value = esdhc_writel_fixup(host, reg, val, 0);
	iowrite32(value, host->ioaddr + reg);
}

static void esdhc_be_writew(struct sdhci_host *host, u16 val, int reg)
{
	int base = reg & ~0x3;
	u32 value;
	u32 ret;

	value = ioread32be(host->ioaddr + base);
	ret = esdhc_writew_fixup(host, reg, val, value);
	if (reg != SDHCI_TRANSFER_MODE)
		iowrite32be(ret, host->ioaddr + base);
}

static void esdhc_le_writew(struct sdhci_host *host, u16 val, int reg)
{
	int base = reg & ~0x3;
	u32 value;
	u32 ret;

	value = ioread32(host->ioaddr + base);
	ret = esdhc_writew_fixup(host, reg, val, value);
	if (reg != SDHCI_TRANSFER_MODE)
		iowrite32(ret, host->ioaddr + base);
}

static void esdhc_be_writeb(struct sdhci_host *host, u8 val, int reg)
{
	int base = reg & ~0x3;
	u32 value;
	u32 ret;

	value = ioread32be(host->ioaddr + base);
	ret = esdhc_writeb_fixup(host, reg, val, value);
	iowrite32be(ret, host->ioaddr + base);
}

static void esdhc_le_writeb(struct sdhci_host *host, u8 val, int reg)
{
	int base = reg & ~0x3;
	u32 value;
	u32 ret;

	value = ioread32(host->ioaddr + base);
	ret = esdhc_writeb_fixup(host, reg, val, value);
	iowrite32(ret, host->ioaddr + base);
}

/*
 * For Abort or Suspend after Stop at Block Gap, ignore the ADMA
 * error(IRQSTAT[ADMAE]) if both Transfer Complete(IRQSTAT[TC])
 * and Block Gap Event(IRQSTAT[BGE]) are also set.
 * For Continue, apply soft reset for data(SYSCTL[RSTD]);
 * and re-issue the entire read transaction from beginning.
 */
static void esdhc_of_adma_workaround(struct sdhci_host *host, u32 intmask)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_esdhc *esdhc = sdhci_pltfm_priv(pltfm_host);
	bool applicable;
	dma_addr_t dmastart;
	dma_addr_t dmanow;

	applicable = (intmask & SDHCI_INT_DATA_END) &&
		     (intmask & SDHCI_INT_BLK_GAP) &&
		     (esdhc->vendor_ver == VENDOR_V_23);
	if (!applicable)
		return;

	host->data->error = 0;
	dmastart = sg_dma_address(host->data->sg);
	dmanow = dmastart + host->data->bytes_xfered;
	/*
	 * Force update to the next DMA block boundary.
	 */
	dmanow = (dmanow & ~(SDHCI_DEFAULT_BOUNDARY_SIZE - 1)) +
		SDHCI_DEFAULT_BOUNDARY_SIZE;
	host->data->bytes_xfered = dmanow - dmastart;
	sdhci_writel(host, dmanow, SDHCI_DMA_ADDRESS);
}

static int esdhc_of_enable_dma(struct sdhci_host *host)
{
	u32 value;
	struct device *dev = mmc_dev(host->mmc);

	if (of_device_is_compatible(dev->of_node, "fsl,ls1043a-esdhc") ||
	    of_device_is_compatible(dev->of_node, "fsl,ls1046a-esdhc"))
		dma_set_mask_and_coherent(dev, DMA_BIT_MASK(40));

	value = sdhci_readl(host, ESDHC_DMA_SYSCTL);
	value |= ESDHC_DMA_SNOOP;
	sdhci_writel(host, value, ESDHC_DMA_SYSCTL);
	return 0;
}

static unsigned int esdhc_of_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_esdhc *esdhc = sdhci_pltfm_priv(pltfm_host);

	if (esdhc->peripheral_clock)
		return esdhc->peripheral_clock;
	else
		return pltfm_host->clock;
}

static unsigned int esdhc_of_get_min_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_esdhc *esdhc = sdhci_pltfm_priv(pltfm_host);
	unsigned int clock;

	if (esdhc->peripheral_clock)
		clock = esdhc->peripheral_clock;
	else
		clock = pltfm_host->clock;
	return clock / 256 / 16;
}

static void esdhc_clock_enable(struct sdhci_host *host, bool enable)
{
	u32 val;
	ktime_t timeout;

	val = sdhci_readl(host, ESDHC_SYSTEM_CONTROL);

	if (enable)
		val |= ESDHC_CLOCK_SDCLKEN;
	else
		val &= ~ESDHC_CLOCK_SDCLKEN;

	sdhci_writel(host, val, ESDHC_SYSTEM_CONTROL);

	/* Wait max 20 ms */
	timeout = ktime_add_ms(ktime_get(), 20);
	val = ESDHC_CLOCK_STABLE;
	while  (1) {
		bool timedout = ktime_after(ktime_get(), timeout);

		if (sdhci_readl(host, ESDHC_PRSSTAT) & val)
			break;
		if (timedout) {
			pr_err("%s: Internal clock never stabilised.\n",
				mmc_hostname(host->mmc));
			break;
		}
		udelay(10);
	}
}

static void esdhc_of_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_esdhc *esdhc = sdhci_pltfm_priv(pltfm_host);
	int pre_div = 1;
	int div = 1;
	ktime_t timeout;
	long fixup = 0;
	u32 temp;

	host->mmc->actual_clock = 0;

	if (clock == 0) {
		esdhc_clock_enable(host, false);
		return;
	}

	/* Workaround to start pre_div at 2 for VNN < VENDOR_V_23 */
	if (esdhc->vendor_ver < VENDOR_V_23)
		pre_div = 2;

	if (host->mmc->card && mmc_card_sd(host->mmc->card) &&
		esdhc->clk_fixup && host->mmc->ios.timing == MMC_TIMING_LEGACY)
		fixup = esdhc->clk_fixup->sd_dflt_max_clk;
	else if (esdhc->clk_fixup)
		fixup = esdhc->clk_fixup->max_clk[host->mmc->ios.timing];

	if (fixup && clock > fixup)
		clock = fixup;

	temp = sdhci_readl(host, ESDHC_SYSTEM_CONTROL);
	temp &= ~(ESDHC_CLOCK_SDCLKEN | ESDHC_CLOCK_IPGEN | ESDHC_CLOCK_HCKEN |
		  ESDHC_CLOCK_PEREN | ESDHC_CLOCK_MASK);
	sdhci_writel(host, temp, ESDHC_SYSTEM_CONTROL);

	while (host->max_clk / pre_div / 16 > clock && pre_div < 256)
		pre_div *= 2;

	while (host->max_clk / pre_div / div > clock && div < 16)
		div++;

	dev_dbg(mmc_dev(host->mmc), "desired SD clock: %d, actual: %d\n",
		clock, host->max_clk / pre_div / div);
	host->mmc->actual_clock = host->max_clk / pre_div / div;
	pre_div >>= 1;
	div--;

	temp = sdhci_readl(host, ESDHC_SYSTEM_CONTROL);
	temp |= (ESDHC_CLOCK_IPGEN | ESDHC_CLOCK_HCKEN | ESDHC_CLOCK_PEREN
		| (div << ESDHC_DIVIDER_SHIFT)
		| (pre_div << ESDHC_PREDIV_SHIFT));
	sdhci_writel(host, temp, ESDHC_SYSTEM_CONTROL);

	/* Wait max 20 ms */
	timeout = ktime_add_ms(ktime_get(), 20);
	while (1) {
		bool timedout = ktime_after(ktime_get(), timeout);

		if (sdhci_readl(host, ESDHC_PRSSTAT) & ESDHC_CLOCK_STABLE)
			break;
		if (timedout) {
			pr_err("%s: Internal clock never stabilised.\n",
				mmc_hostname(host->mmc));
			return;
		}
		udelay(10);
	}

	temp |= ESDHC_CLOCK_SDCLKEN;
	sdhci_writel(host, temp, ESDHC_SYSTEM_CONTROL);
}

static void esdhc_pltfm_set_bus_width(struct sdhci_host *host, int width)
{
	u32 ctrl;

	ctrl = sdhci_readl(host, ESDHC_PROCTL);
	ctrl &= (~ESDHC_CTRL_BUSWIDTH_MASK);
	switch (width) {
	case MMC_BUS_WIDTH_8:
		ctrl |= ESDHC_CTRL_8BITBUS;
		break;

	case MMC_BUS_WIDTH_4:
		ctrl |= ESDHC_CTRL_4BITBUS;
		break;

	default:
		break;
	}

	sdhci_writel(host, ctrl, ESDHC_PROCTL);
}

static void esdhc_reset(struct sdhci_host *host, u8 mask)
{
	u32 val;

	sdhci_reset(host, mask);

	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);

	if (of_find_compatible_node(NULL, NULL, "fsl,p2020-esdhc"))
		mdelay(5);

	if (mask & SDHCI_RESET_ALL) {
		val = sdhci_readl(host, ESDHC_TBCTL);
		val &= ~ESDHC_TB_EN;
		sdhci_writel(host, val, ESDHC_TBCTL);
	}
}

/* The SCFG, Supplemental Configuration Unit, provides SoC specific
 * configuration and status registers for the device. There is a
 * SDHC IO VSEL control register on SCFG for some platforms. It's
 * used to support SDHC IO voltage switching.
 */
static const struct of_device_id scfg_device_ids[] = {
	{ .compatible = "fsl,t1040-scfg", },
	{ .compatible = "fsl,ls1012a-scfg", },
	{ .compatible = "fsl,ls1046a-scfg", },
	{}
};

/* SDHC IO VSEL control register definition */
#define SCFG_SDHCIOVSELCR	0x408
#define SDHCIOVSELCR_TGLEN	0x80000000
#define SDHCIOVSELCR_VSELVAL	0x60000000
#define SDHCIOVSELCR_SDHC_VS	0x00000001

static int esdhc_signal_voltage_switch(struct mmc_host *mmc,
				       struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct device_node *scfg_node;
	void __iomem *scfg_base = NULL;
	u32 sdhciovselcr;
	u32 val;

	/*
	 * Signal Voltage Switching is only applicable for Host Controllers
	 * v3.00 and above.
	 */
	if (host->version < SDHCI_SPEC_300)
		return 0;

	val = sdhci_readl(host, ESDHC_PROCTL);

	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_330:
		val &= ~ESDHC_VOLT_SEL;
		sdhci_writel(host, val, ESDHC_PROCTL);
		return 0;
	case MMC_SIGNAL_VOLTAGE_180:
		scfg_node = of_find_matching_node(NULL, scfg_device_ids);
		if (scfg_node)
			scfg_base = of_iomap(scfg_node, 0);
		if (scfg_base) {
			sdhciovselcr = SDHCIOVSELCR_TGLEN |
				       SDHCIOVSELCR_VSELVAL;
			iowrite32be(sdhciovselcr,
				scfg_base + SCFG_SDHCIOVSELCR);

			val |= ESDHC_VOLT_SEL;
			sdhci_writel(host, val, ESDHC_PROCTL);
			mdelay(5);

			sdhciovselcr = SDHCIOVSELCR_TGLEN |
				       SDHCIOVSELCR_SDHC_VS;
			iowrite32be(sdhciovselcr,
				scfg_base + SCFG_SDHCIOVSELCR);
			iounmap(scfg_base);
		} else {
			val |= ESDHC_VOLT_SEL;
			sdhci_writel(host, val, ESDHC_PROCTL);
		}
		return 0;
	default:
		return 0;
	}
}

static int esdhc_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct sdhci_host *host = mmc_priv(mmc);
	u32 val;

	/* Use tuning block for tuning procedure */
	esdhc_clock_enable(host, false);
	val = sdhci_readl(host, ESDHC_DMA_SYSCTL);
	val |= ESDHC_FLUSH_ASYNC_FIFO;
	sdhci_writel(host, val, ESDHC_DMA_SYSCTL);

	val = sdhci_readl(host, ESDHC_TBCTL);
	val |= ESDHC_TB_EN;
	sdhci_writel(host, val, ESDHC_TBCTL);
	esdhc_clock_enable(host, true);

	return sdhci_execute_tuning(mmc, opcode);
}

#ifdef CONFIG_PM_SLEEP
static u32 esdhc_proctl;
static int esdhc_of_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);

	esdhc_proctl = sdhci_readl(host, SDHCI_HOST_CONTROL);

	if (host->tuning_mode != SDHCI_TUNING_MODE_3)
		mmc_retune_needed(host->mmc);

	return sdhci_suspend_host(host);
}

static int esdhc_of_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	int ret = sdhci_resume_host(host);

	if (ret == 0) {
		/* Isn't this already done by sdhci_resume_host() ? --rmk */
		esdhc_of_enable_dma(host);
		sdhci_writel(host, esdhc_proctl, SDHCI_HOST_CONTROL);
	}
	return ret;
}
#endif

static SIMPLE_DEV_PM_OPS(esdhc_of_dev_pm_ops,
			esdhc_of_suspend,
			esdhc_of_resume);

static const struct sdhci_ops sdhci_esdhc_be_ops = {
	.read_l = esdhc_be_readl,
	.read_w = esdhc_be_readw,
	.read_b = esdhc_be_readb,
	.write_l = esdhc_be_writel,
	.write_w = esdhc_be_writew,
	.write_b = esdhc_be_writeb,
	.set_clock = esdhc_of_set_clock,
	.enable_dma = esdhc_of_enable_dma,
	.get_max_clock = esdhc_of_get_max_clock,
	.get_min_clock = esdhc_of_get_min_clock,
	.adma_workaround = esdhc_of_adma_workaround,
	.set_bus_width = esdhc_pltfm_set_bus_width,
	.reset = esdhc_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static const struct sdhci_ops sdhci_esdhc_le_ops = {
	.read_l = esdhc_le_readl,
	.read_w = esdhc_le_readw,
	.read_b = esdhc_le_readb,
	.write_l = esdhc_le_writel,
	.write_w = esdhc_le_writew,
	.write_b = esdhc_le_writeb,
	.set_clock = esdhc_of_set_clock,
	.enable_dma = esdhc_of_enable_dma,
	.get_max_clock = esdhc_of_get_max_clock,
	.get_min_clock = esdhc_of_get_min_clock,
	.adma_workaround = esdhc_of_adma_workaround,
	.set_bus_width = esdhc_pltfm_set_bus_width,
	.reset = esdhc_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static const struct sdhci_pltfm_data sdhci_esdhc_be_pdata = {
	.quirks = ESDHC_DEFAULT_QUIRKS |
#ifdef CONFIG_PPC
		  SDHCI_QUIRK_BROKEN_CARD_DETECTION |
#endif
		  SDHCI_QUIRK_NO_CARD_NO_RESET |
		  SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC,
	.ops = &sdhci_esdhc_be_ops,
};

static const struct sdhci_pltfm_data sdhci_esdhc_le_pdata = {
	.quirks = ESDHC_DEFAULT_QUIRKS |
		  SDHCI_QUIRK_NO_CARD_NO_RESET |
		  SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC,
	.ops = &sdhci_esdhc_le_ops,
};

static struct soc_device_attribute soc_incorrect_hostver[] = {
	{ .family = "QorIQ T4240", .revision = "1.0", },
	{ .family = "QorIQ T4240", .revision = "2.0", },
	{ },
};

static void esdhc_init(struct platform_device *pdev, struct sdhci_host *host)
{
	const struct of_device_id *match;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_esdhc *esdhc;
	struct device_node *np;
	struct clk *clk;
	u32 val;
	u16 host_ver;

	pltfm_host = sdhci_priv(host);
	esdhc = sdhci_pltfm_priv(pltfm_host);

	host_ver = sdhci_readw(host, SDHCI_HOST_VERSION);
	esdhc->vendor_ver = (host_ver & SDHCI_VENDOR_VER_MASK) >>
			     SDHCI_VENDOR_VER_SHIFT;
	esdhc->spec_ver = host_ver & SDHCI_SPEC_VER_MASK;
	if (soc_device_match(soc_incorrect_hostver))
		esdhc->quirk_incorrect_hostver = true;
	else
		esdhc->quirk_incorrect_hostver = false;

	match = of_match_node(sdhci_esdhc_of_match, pdev->dev.of_node);
	if (match)
		esdhc->clk_fixup = match->data;
	np = pdev->dev.of_node;
	clk = of_clk_get(np, 0);
	if (!IS_ERR(clk)) {
		/*
		 * esdhc->peripheral_clock would be assigned with a value
		 * which is eSDHC base clock when use periperal clock.
		 * For ls1046a, the clock value got by common clk API is
		 * peripheral clock while the eSDHC base clock is 1/2
		 * peripheral clock.
		 */
		if (of_device_is_compatible(np, "fsl,ls1046a-esdhc"))
			esdhc->peripheral_clock = clk_get_rate(clk) / 2;
		else
			esdhc->peripheral_clock = clk_get_rate(clk);

		clk_put(clk);
	}

	if (esdhc->peripheral_clock) {
		esdhc_clock_enable(host, false);
		val = sdhci_readl(host, ESDHC_DMA_SYSCTL);
		val |= ESDHC_PERIPHERAL_CLK_SEL;
		sdhci_writel(host, val, ESDHC_DMA_SYSCTL);
		esdhc_clock_enable(host, true);
	}
}

static int sdhci_esdhc_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct device_node *np;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_esdhc *esdhc;
	int ret;

	np = pdev->dev.of_node;

	if (of_property_read_bool(np, "little-endian"))
		host = sdhci_pltfm_init(pdev, &sdhci_esdhc_le_pdata,
					sizeof(struct sdhci_esdhc));
	else
		host = sdhci_pltfm_init(pdev, &sdhci_esdhc_be_pdata,
					sizeof(struct sdhci_esdhc));

	if (IS_ERR(host))
		return PTR_ERR(host);

	host->mmc_host_ops.start_signal_voltage_switch =
		esdhc_signal_voltage_switch;
	host->mmc_host_ops.execute_tuning = esdhc_execute_tuning;
	host->tuning_delay = 1;

	esdhc_init(pdev, host);

	sdhci_get_of_property(pdev);

	pltfm_host = sdhci_priv(host);
	esdhc = sdhci_pltfm_priv(pltfm_host);
	if (esdhc->vendor_ver == VENDOR_V_22)
		host->quirks2 |= SDHCI_QUIRK2_HOST_NO_CMD23;

	if (esdhc->vendor_ver > VENDOR_V_22)
		host->quirks &= ~SDHCI_QUIRK_NO_BUSY_IRQ;

	if (of_find_compatible_node(NULL, NULL, "fsl,p2020-esdhc")) {
		host->quirks2 |= SDHCI_QUIRK_RESET_AFTER_REQUEST;
		host->quirks2 |= SDHCI_QUIRK_BROKEN_TIMEOUT_VAL;
	}

	if (of_device_is_compatible(np, "fsl,p5040-esdhc") ||
	    of_device_is_compatible(np, "fsl,p5020-esdhc") ||
	    of_device_is_compatible(np, "fsl,p4080-esdhc") ||
	    of_device_is_compatible(np, "fsl,p1020-esdhc") ||
	    of_device_is_compatible(np, "fsl,t1040-esdhc"))
		host->quirks &= ~SDHCI_QUIRK_BROKEN_CARD_DETECTION;

	if (of_device_is_compatible(np, "fsl,ls1021a-esdhc"))
		host->quirks |= SDHCI_QUIRK_BROKEN_TIMEOUT_VAL;

	if (of_device_is_compatible(np, "fsl,p2020-esdhc")) {
		/*
		 * Freescale messed up with P2020 as it has a non-standard
		 * host control register
		 */
		host->quirks2 |= SDHCI_QUIRK2_BROKEN_HOST_CONTROL;
	}

	/* call to generic mmc_of_parse to support additional capabilities */
	ret = mmc_of_parse(host->mmc);
	if (ret)
		goto err;

	mmc_of_parse_voltage(np, &host->ocr_mask);

	ret = sdhci_add_host(host);
	if (ret)
		goto err;

	return 0;
 err:
	sdhci_pltfm_free(pdev);
	return ret;
}

static struct platform_driver sdhci_esdhc_driver = {
	.driver = {
		.name = "sdhci-esdhc",
		.of_match_table = sdhci_esdhc_of_match,
		.pm = &esdhc_of_dev_pm_ops,
	},
	.probe = sdhci_esdhc_probe,
	.remove = sdhci_pltfm_unregister,
};

module_platform_driver(sdhci_esdhc_driver);

MODULE_DESCRIPTION("SDHCI OF driver for Freescale MPC eSDHC");
MODULE_AUTHOR("Xiaobo Xie <X.Xie@freescale.com>, "
	      "Anton Vorontsov <avorontsov@ru.mvista.com>");
MODULE_LICENSE("GPL v2");
