/*
 * Freescale eSDHC i.MX controller driver for the platform bus.
 *
 * derived from the OF-version.
 *
 * Copyright (c) 2010 Pengutronix e.K.
 *   Author: Wolfram Sang <w.sang@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdhci-pltfm.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <mach/hardware.h>
#include <mach/esdhc.h>
#include "sdhci.h"
#include "sdhci-pltfm.h"
#include "sdhci-esdhc.h"

/* VENDOR SPEC register */
#define SDHCI_VENDOR_SPEC		0xC0
#define  SDHCI_VENDOR_SPEC_SDIO_QUIRK	0x00000002

#define ESDHC_FLAG_GPIO_FOR_CD_WP	(1 << 0)
/*
 * The CMDTYPE of the CMD register (offset 0xE) should be set to
 * "11" when the STOP CMD12 is issued on imx53 to abort one
 * open ended multi-blk IO. Otherwise the TC INT wouldn't
 * be generated.
 * In exact block transfer, the controller doesn't complete the
 * operations automatically as required at the end of the
 * transfer and remains on hold if the abort command is not sent.
 * As a result, the TC flag is not asserted and SW  received timeout
 * exeception. Bit1 of Vendor Spec registor is used to fix it.
 */
#define ESDHC_FLAG_MULTIBLK_NO_INT	(1 << 1)

struct pltfm_imx_data {
	int flags;
	u32 scratchpad;
};

static inline void esdhc_clrset_le(struct sdhci_host *host, u32 mask, u32 val, int reg)
{
	void __iomem *base = host->ioaddr + (reg & ~0x3);
	u32 shift = (reg & 0x3) * 8;

	writel(((readl(base) & ~(mask << shift)) | (val << shift)), base);
}

static u32 esdhc_readl_le(struct sdhci_host *host, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = pltfm_host->priv;

	/* fake CARD_PRESENT flag on mx25/35 */
	u32 val = readl(host->ioaddr + reg);

	if (unlikely((reg == SDHCI_PRESENT_STATE)
			&& (imx_data->flags & ESDHC_FLAG_GPIO_FOR_CD_WP))) {
		struct esdhc_platform_data *boarddata =
				host->mmc->parent->platform_data;

		if (boarddata && gpio_is_valid(boarddata->cd_gpio)
				&& gpio_get_value(boarddata->cd_gpio))
			/* no card, if a valid gpio says so... */
			val &= ~SDHCI_CARD_PRESENT;
		else
			/* ... in all other cases assume card is present */
			val |= SDHCI_CARD_PRESENT;
	}

	return val;
}

static void esdhc_writel_le(struct sdhci_host *host, u32 val, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = pltfm_host->priv;

	if (unlikely((reg == SDHCI_INT_ENABLE || reg == SDHCI_SIGNAL_ENABLE)
			&& (imx_data->flags & ESDHC_FLAG_GPIO_FOR_CD_WP)))
		/*
		 * these interrupts won't work with a custom card_detect gpio
		 * (only applied to mx25/35)
		 */
		val &= ~(SDHCI_INT_CARD_REMOVE | SDHCI_INT_CARD_INSERT);

	if (unlikely((imx_data->flags & ESDHC_FLAG_MULTIBLK_NO_INT)
				&& (reg == SDHCI_INT_STATUS)
				&& (val & SDHCI_INT_DATA_END))) {
			u32 v;
			v = readl(host->ioaddr + SDHCI_VENDOR_SPEC);
			v &= ~SDHCI_VENDOR_SPEC_SDIO_QUIRK;
			writel(v, host->ioaddr + SDHCI_VENDOR_SPEC);
	}

	writel(val, host->ioaddr + reg);
}

static u16 esdhc_readw_le(struct sdhci_host *host, int reg)
{
	if (unlikely(reg == SDHCI_HOST_VERSION))
		reg ^= 2;

	return readw(host->ioaddr + reg);
}

static void esdhc_writew_le(struct sdhci_host *host, u16 val, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = pltfm_host->priv;

	switch (reg) {
	case SDHCI_TRANSFER_MODE:
		/*
		 * Postpone this write, we must do it together with a
		 * command write that is down below.
		 */
		if ((imx_data->flags & ESDHC_FLAG_MULTIBLK_NO_INT)
				&& (host->cmd->opcode == SD_IO_RW_EXTENDED)
				&& (host->cmd->data->blocks > 1)
				&& (host->cmd->data->flags & MMC_DATA_READ)) {
			u32 v;
			v = readl(host->ioaddr + SDHCI_VENDOR_SPEC);
			v |= SDHCI_VENDOR_SPEC_SDIO_QUIRK;
			writel(v, host->ioaddr + SDHCI_VENDOR_SPEC);
		}
		imx_data->scratchpad = val;
		return;
	case SDHCI_COMMAND:
		if ((host->cmd->opcode == MMC_STOP_TRANSMISSION ||
		     host->cmd->opcode == MMC_SET_BLOCK_COUNT) &&
	            (imx_data->flags & ESDHC_FLAG_MULTIBLK_NO_INT))
			val |= SDHCI_CMD_ABORTCMD;
		writel(val << 16 | imx_data->scratchpad,
			host->ioaddr + SDHCI_TRANSFER_MODE);
		return;
	case SDHCI_BLOCK_SIZE:
		val &= ~SDHCI_MAKE_BLKSZ(0x7, 0);
		break;
	}
	esdhc_clrset_le(host, 0xffff, val, reg);
}

static void esdhc_writeb_le(struct sdhci_host *host, u8 val, int reg)
{
	u32 new_val;

	switch (reg) {
	case SDHCI_POWER_CONTROL:
		/*
		 * FSL put some DMA bits here
		 * If your board has a regulator, code should be here
		 */
		return;
	case SDHCI_HOST_CONTROL:
		/* FSL messed up here, so we can just keep those two */
		new_val = val & (SDHCI_CTRL_LED | SDHCI_CTRL_4BITBUS);
		/* ensure the endianess */
		new_val |= ESDHC_HOST_CONTROL_LE;
		/* DMA mode bits are shifted */
		new_val |= (val & SDHCI_CTRL_DMA_MASK) << 5;

		esdhc_clrset_le(host, 0xffff, new_val, reg);
		return;
	}
	esdhc_clrset_le(host, 0xff, val, reg);
}

static unsigned int esdhc_pltfm_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return clk_get_rate(pltfm_host->clk);
}

static unsigned int esdhc_pltfm_get_min_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return clk_get_rate(pltfm_host->clk) / 256 / 16;
}

static unsigned int esdhc_pltfm_get_ro(struct sdhci_host *host)
{
	struct esdhc_platform_data *boarddata = host->mmc->parent->platform_data;

	if (boarddata && gpio_is_valid(boarddata->wp_gpio))
		return gpio_get_value(boarddata->wp_gpio);
	else
		return -ENOSYS;
}

static struct sdhci_ops sdhci_esdhc_ops = {
	.read_l = esdhc_readl_le,
	.read_w = esdhc_readw_le,
	.write_l = esdhc_writel_le,
	.write_w = esdhc_writew_le,
	.write_b = esdhc_writeb_le,
	.set_clock = esdhc_set_clock,
	.get_max_clock = esdhc_pltfm_get_max_clock,
	.get_min_clock = esdhc_pltfm_get_min_clock,
};

static irqreturn_t cd_irq(int irq, void *data)
{
	struct sdhci_host *sdhost = (struct sdhci_host *)data;

	tasklet_schedule(&sdhost->card_tasklet);
	return IRQ_HANDLED;
};

static int esdhc_pltfm_init(struct sdhci_host *host, struct sdhci_pltfm_data *pdata)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct esdhc_platform_data *boarddata = host->mmc->parent->platform_data;
	struct clk *clk;
	int err;
	struct pltfm_imx_data *imx_data;

	clk = clk_get(mmc_dev(host->mmc), NULL);
	if (IS_ERR(clk)) {
		dev_err(mmc_dev(host->mmc), "clk err\n");
		return PTR_ERR(clk);
	}
	clk_enable(clk);
	pltfm_host->clk = clk;

	imx_data = kzalloc(sizeof(struct pltfm_imx_data), GFP_KERNEL);
	if (!imx_data) {
		clk_disable(pltfm_host->clk);
		clk_put(pltfm_host->clk);
		return -ENOMEM;
	}
	pltfm_host->priv = imx_data;

	host->quirks |= SDHCI_QUIRK_BROKEN_TIMEOUT_VAL;

	if (cpu_is_mx25() || cpu_is_mx35()) {
		/* Fix errata ENGcm07207 present on i.MX25 and i.MX35 */
		host->quirks |= SDHCI_QUIRK_NO_MULTIBLOCK;
		/* write_protect can't be routed to controller, use gpio */
		sdhci_esdhc_ops.get_ro = esdhc_pltfm_get_ro;
	}

	if (!(cpu_is_mx25() || cpu_is_mx35() || cpu_is_mx51()))
		imx_data->flags |= ESDHC_FLAG_MULTIBLK_NO_INT;

	if (boarddata) {
		err = gpio_request_one(boarddata->wp_gpio, GPIOF_IN, "ESDHC_WP");
		if (err) {
			dev_warn(mmc_dev(host->mmc),
				"no write-protect pin available!\n");
			boarddata->wp_gpio = err;
		}

		err = gpio_request_one(boarddata->cd_gpio, GPIOF_IN, "ESDHC_CD");
		if (err) {
			dev_warn(mmc_dev(host->mmc),
				"no card-detect pin available!\n");
			goto no_card_detect_pin;
		}

		/* i.MX5x has issues to be researched */
		if (!cpu_is_mx25() && !cpu_is_mx35())
			goto not_supported;

		err = request_irq(gpio_to_irq(boarddata->cd_gpio), cd_irq,
				 IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
				 mmc_hostname(host->mmc), host);
		if (err) {
			dev_warn(mmc_dev(host->mmc), "request irq error\n");
			goto no_card_detect_irq;
		}

		imx_data->flags |= ESDHC_FLAG_GPIO_FOR_CD_WP;
		/* Now we have a working card_detect again */
		host->quirks &= ~SDHCI_QUIRK_BROKEN_CARD_DETECTION;
	}

	return 0;

 no_card_detect_irq:
	gpio_free(boarddata->cd_gpio);
 no_card_detect_pin:
	boarddata->cd_gpio = err;
 not_supported:
	kfree(imx_data);
	return 0;
}

static void esdhc_pltfm_exit(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct esdhc_platform_data *boarddata = host->mmc->parent->platform_data;
	struct pltfm_imx_data *imx_data = pltfm_host->priv;

	if (boarddata && gpio_is_valid(boarddata->wp_gpio))
		gpio_free(boarddata->wp_gpio);

	if (boarddata && gpio_is_valid(boarddata->cd_gpio)) {
		gpio_free(boarddata->cd_gpio);

		if (!(host->quirks & SDHCI_QUIRK_BROKEN_CARD_DETECTION))
			free_irq(gpio_to_irq(boarddata->cd_gpio), host);
	}

	clk_disable(pltfm_host->clk);
	clk_put(pltfm_host->clk);
	kfree(imx_data);
}

struct sdhci_pltfm_data sdhci_esdhc_imx_pdata = {
	.quirks = ESDHC_DEFAULT_QUIRKS | SDHCI_QUIRK_BROKEN_ADMA
			| SDHCI_QUIRK_BROKEN_CARD_DETECTION,
	/* ADMA has issues. Might be fixable */
	.ops = &sdhci_esdhc_ops,
	.init = esdhc_pltfm_init,
	.exit = esdhc_pltfm_exit,
};
