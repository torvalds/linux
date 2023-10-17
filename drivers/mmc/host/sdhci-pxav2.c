// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 Marvell International Ltd.
 *		Zhangfei Gao <zhangfei.gao@marvell.com>
 *		Kevin Wang <dwang4@marvell.com>
 *		Jun Nie <njun@marvell.com>
 *		Qiming Wu <wuqm@marvell.com>
 *		Philip Rakity <prakity@marvell.com>
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/platform_data/pxa_sdhci.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/mmc.h>
#include <linux/pinctrl/consumer.h>

#include "sdhci.h"
#include "sdhci-pltfm.h"

#define SD_FIFO_PARAM		0xe0
#define DIS_PAD_SD_CLK_GATE	0x0400 /* Turn on/off Dynamic SD Clock Gating */
#define CLK_GATE_ON		0x0200 /* Disable/enable Clock Gate */
#define CLK_GATE_CTL		0x0100 /* Clock Gate Control */
#define CLK_GATE_SETTING_BITS	(DIS_PAD_SD_CLK_GATE | \
		CLK_GATE_ON | CLK_GATE_CTL)

#define SD_CLOCK_BURST_SIZE_SETUP	0xe6
#define SDCLK_SEL_SHIFT		8
#define SDCLK_SEL_MASK		0x3
#define SDCLK_DELAY_SHIFT	10
#define SDCLK_DELAY_MASK	0x3c

#define SD_CE_ATA_2		0xea
#define MMC_CARD		0x1000
#define MMC_WIDTH		0x0100

struct sdhci_pxav2_host {
	struct mmc_request *sdio_mrq;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;
	struct pinctrl_state *pins_cmd_gpio;
};

static void pxav2_reset(struct sdhci_host *host, u8 mask)
{
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;

	sdhci_reset(host, mask);

	if (mask == SDHCI_RESET_ALL) {
		u16 tmp = 0;

		/*
		 * tune timing of read data/command when crc error happen
		 * no performance impact
		 */
		if (pdata && pdata->clk_delay_sel == 1) {
			tmp = readw(host->ioaddr + SD_CLOCK_BURST_SIZE_SETUP);

			tmp &= ~(SDCLK_DELAY_MASK << SDCLK_DELAY_SHIFT);
			tmp |= (pdata->clk_delay_cycles & SDCLK_DELAY_MASK)
				<< SDCLK_DELAY_SHIFT;
			tmp &= ~(SDCLK_SEL_MASK << SDCLK_SEL_SHIFT);
			tmp |= (1 & SDCLK_SEL_MASK) << SDCLK_SEL_SHIFT;

			writew(tmp, host->ioaddr + SD_CLOCK_BURST_SIZE_SETUP);
		}

		if (pdata && (pdata->flags & PXA_FLAG_ENABLE_CLOCK_GATING)) {
			tmp = readw(host->ioaddr + SD_FIFO_PARAM);
			tmp &= ~CLK_GATE_SETTING_BITS;
			writew(tmp, host->ioaddr + SD_FIFO_PARAM);
		} else {
			tmp = readw(host->ioaddr + SD_FIFO_PARAM);
			tmp &= ~CLK_GATE_SETTING_BITS;
			tmp |= CLK_GATE_SETTING_BITS;
			writew(tmp, host->ioaddr + SD_FIFO_PARAM);
		}
	}
}

static u16 pxav1_readw(struct sdhci_host *host, int reg)
{
	/* Workaround for data abort exception on SDH2 and SDH4 on PXA168 */
	if (reg == SDHCI_HOST_VERSION)
		return readl(host->ioaddr + SDHCI_HOST_VERSION - 2) >> 16;

	return readw(host->ioaddr + reg);
}

static u32 pxav1_irq(struct sdhci_host *host, u32 intmask)
{
	struct sdhci_pxav2_host *pxav2_host = sdhci_pltfm_priv(sdhci_priv(host));
	struct mmc_request *sdio_mrq;

	if (pxav2_host->sdio_mrq && (intmask & SDHCI_INT_CMD_MASK)) {
		/* The dummy CMD0 for the SDIO workaround just completed */
		sdhci_writel(host, intmask & SDHCI_INT_CMD_MASK, SDHCI_INT_STATUS);
		intmask &= ~SDHCI_INT_CMD_MASK;

		/* Restore MMC function to CMD pin */
		if (pxav2_host->pinctrl && pxav2_host->pins_default)
			pinctrl_select_state(pxav2_host->pinctrl, pxav2_host->pins_default);

		sdio_mrq = pxav2_host->sdio_mrq;
		pxav2_host->sdio_mrq = NULL;
		mmc_request_done(host->mmc, sdio_mrq);
	}

	return intmask;
}

static void pxav1_request_done(struct sdhci_host *host, struct mmc_request *mrq)
{
	u16 tmp;
	struct sdhci_pxav2_host *pxav2_host;

	/* If this is an SDIO command, perform errata workaround for silicon bug */
	if (mrq->cmd && !mrq->cmd->error &&
	    (mrq->cmd->opcode == SD_IO_RW_DIRECT ||
	     mrq->cmd->opcode == SD_IO_RW_EXTENDED)) {
		/* Reset data port */
		tmp = readw(host->ioaddr + SDHCI_TIMEOUT_CONTROL);
		tmp |= 0x400;
		writew(tmp, host->ioaddr + SDHCI_TIMEOUT_CONTROL);

		/* Clock is now stopped, so restart it by sending a dummy CMD0 */
		pxav2_host = sdhci_pltfm_priv(sdhci_priv(host));
		pxav2_host->sdio_mrq = mrq;

		/* Set CMD as high output rather than MMC function while we do CMD0 */
		if (pxav2_host->pinctrl && pxav2_host->pins_cmd_gpio)
			pinctrl_select_state(pxav2_host->pinctrl, pxav2_host->pins_cmd_gpio);

		sdhci_writel(host, 0, SDHCI_ARGUMENT);
		sdhci_writew(host, 0, SDHCI_TRANSFER_MODE);
		sdhci_writew(host, SDHCI_MAKE_CMD(MMC_GO_IDLE_STATE, SDHCI_CMD_RESP_NONE),
			     SDHCI_COMMAND);

		/* Don't finish this request until the dummy CMD0 finishes */
		return;
	}

	mmc_request_done(host->mmc, mrq);
}

static void pxav2_mmc_set_bus_width(struct sdhci_host *host, int width)
{
	u8 ctrl;
	u16 tmp;

	ctrl = readb(host->ioaddr + SDHCI_HOST_CONTROL);
	tmp = readw(host->ioaddr + SD_CE_ATA_2);
	if (width == MMC_BUS_WIDTH_8) {
		ctrl &= ~SDHCI_CTRL_4BITBUS;
		tmp |= MMC_CARD | MMC_WIDTH;
	} else {
		tmp &= ~(MMC_CARD | MMC_WIDTH);
		if (width == MMC_BUS_WIDTH_4)
			ctrl |= SDHCI_CTRL_4BITBUS;
		else
			ctrl &= ~SDHCI_CTRL_4BITBUS;
	}
	writew(tmp, host->ioaddr + SD_CE_ATA_2);
	writeb(ctrl, host->ioaddr + SDHCI_HOST_CONTROL);
}

struct sdhci_pxa_variant {
	const struct sdhci_ops *ops;
	unsigned int extra_quirks;
};

static const struct sdhci_ops pxav1_sdhci_ops = {
	.read_w        = pxav1_readw,
	.set_clock     = sdhci_set_clock,
	.irq           = pxav1_irq,
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.set_bus_width = pxav2_mmc_set_bus_width,
	.reset         = pxav2_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
	.request_done  = pxav1_request_done,
};

static const struct sdhci_pxa_variant __maybe_unused pxav1_variant = {
	.ops = &pxav1_sdhci_ops,
	.extra_quirks = SDHCI_QUIRK_NO_BUSY_IRQ | SDHCI_QUIRK_32BIT_DMA_SIZE,
};

static const struct sdhci_ops pxav2_sdhci_ops = {
	.set_clock     = sdhci_set_clock,
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.set_bus_width = pxav2_mmc_set_bus_width,
	.reset         = pxav2_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static const struct sdhci_pxa_variant pxav2_variant = {
	.ops = &pxav2_sdhci_ops,
};

#ifdef CONFIG_OF
static const struct of_device_id sdhci_pxav2_of_match[] = {
	{ .compatible = "mrvl,pxav1-mmc", .data = &pxav1_variant, },
	{ .compatible = "mrvl,pxav2-mmc", .data = &pxav2_variant, },
	{},
};
MODULE_DEVICE_TABLE(of, sdhci_pxav2_of_match);

static struct sdhci_pxa_platdata *pxav2_get_mmc_pdata(struct device *dev)
{
	struct sdhci_pxa_platdata *pdata;
	struct device_node *np = dev->of_node;
	u32 bus_width;
	u32 clk_delay_cycles;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	if (of_property_read_bool(np, "non-removable"))
		pdata->flags |= PXA_FLAG_CARD_PERMANENT;

	of_property_read_u32(np, "bus-width", &bus_width);
	if (bus_width == 8)
		pdata->flags |= PXA_FLAG_SD_8_BIT_CAPABLE_SLOT;

	of_property_read_u32(np, "mrvl,clk-delay-cycles", &clk_delay_cycles);
	if (clk_delay_cycles > 0) {
		pdata->clk_delay_sel = 1;
		pdata->clk_delay_cycles = clk_delay_cycles;
	}

	return pdata;
}
#else
static inline struct sdhci_pxa_platdata *pxav2_get_mmc_pdata(struct device *dev)
{
	return NULL;
}
#endif

static int sdhci_pxav2_probe(struct platform_device *pdev)
{
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;
	struct sdhci_pxav2_host *pxav2_host;
	struct device *dev = &pdev->dev;
	struct sdhci_host *host = NULL;
	const struct sdhci_pxa_variant *variant;

	int ret;
	struct clk *clk, *clk_core;

	host = sdhci_pltfm_init(pdev, NULL, sizeof(*pxav2_host));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	pxav2_host = sdhci_pltfm_priv(pltfm_host);

	clk = devm_clk_get_optional_enabled(dev, "io");
	if (!clk)
		clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		dev_err_probe(dev, ret, "failed to get io clock\n");
		goto free;
	}
	pltfm_host->clk = clk;

	clk_core = devm_clk_get_optional_enabled(dev, "core");
	if (IS_ERR(clk_core)) {
		ret = PTR_ERR(clk_core);
		dev_err_probe(dev, ret, "failed to enable core clock\n");
		goto free;
	}

	host->quirks = SDHCI_QUIRK_BROKEN_ADMA
		| SDHCI_QUIRK_BROKEN_TIMEOUT_VAL
		| SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN;

	variant = of_device_get_match_data(dev);
	if (variant)
		pdata = pxav2_get_mmc_pdata(dev);
	else
		variant = &pxav2_variant;

	if (pdata) {
		if (pdata->flags & PXA_FLAG_CARD_PERMANENT) {
			/* on-chip device */
			host->quirks |= SDHCI_QUIRK_BROKEN_CARD_DETECTION;
			host->mmc->caps |= MMC_CAP_NONREMOVABLE;
		}

		/* If slot design supports 8 bit data, indicate this to MMC. */
		if (pdata->flags & PXA_FLAG_SD_8_BIT_CAPABLE_SLOT)
			host->mmc->caps |= MMC_CAP_8_BIT_DATA;

		if (pdata->quirks)
			host->quirks |= pdata->quirks;
		if (pdata->host_caps)
			host->mmc->caps |= pdata->host_caps;
		if (pdata->pm_caps)
			host->mmc->pm_caps |= pdata->pm_caps;
	}

	host->quirks |= variant->extra_quirks;
	host->ops = variant->ops;

	/* Set up optional pinctrl for PXA168 SDIO IRQ fix */
	pxav2_host->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(pxav2_host->pinctrl)) {
		pxav2_host->pins_cmd_gpio = pinctrl_lookup_state(pxav2_host->pinctrl,
								 "state_cmd_gpio");
		if (IS_ERR(pxav2_host->pins_cmd_gpio))
			pxav2_host->pins_cmd_gpio = NULL;
		pxav2_host->pins_default = pinctrl_lookup_state(pxav2_host->pinctrl,
								"default");
		if (IS_ERR(pxav2_host->pins_default))
			pxav2_host->pins_default = NULL;
	} else {
		pxav2_host->pinctrl = NULL;
	}

	ret = sdhci_add_host(host);
	if (ret)
		goto free;

	return 0;

free:
	sdhci_pltfm_free(pdev);
	return ret;
}

static struct platform_driver sdhci_pxav2_driver = {
	.driver		= {
		.name	= "sdhci-pxav2",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(sdhci_pxav2_of_match),
		.pm	= &sdhci_pltfm_pmops,
	},
	.probe		= sdhci_pxav2_probe,
	.remove_new	= sdhci_pltfm_remove,
};

module_platform_driver(sdhci_pxav2_driver);

MODULE_DESCRIPTION("SDHCI driver for pxav2");
MODULE_AUTHOR("Marvell International Ltd.");
MODULE_LICENSE("GPL v2");

