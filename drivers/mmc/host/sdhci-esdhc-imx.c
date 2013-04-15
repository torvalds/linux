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
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_data/mmc-esdhc-imx.h>
#include "sdhci-pltfm.h"
#include "sdhci-esdhc.h"

#define	ESDHC_CTRL_D3CD			0x08
/* VENDOR SPEC register */
#define ESDHC_VENDOR_SPEC		0xc0
#define  ESDHC_VENDOR_SPEC_SDIO_QUIRK	(1 << 1)
#define ESDHC_WTMK_LVL			0x44
#define ESDHC_MIX_CTRL			0x48
#define  ESDHC_MIX_CTRL_AC23EN		(1 << 7)
/* Bits 3 and 6 are not SDHCI standard definitions */
#define  ESDHC_MIX_CTRL_SDHCI_MASK	0xb7

/*
 * Our interpretation of the SDHCI_HOST_CONTROL register
 */
#define ESDHC_CTRL_4BITBUS		(0x1 << 1)
#define ESDHC_CTRL_8BITBUS		(0x2 << 1)
#define ESDHC_CTRL_BUSWIDTH_MASK	(0x3 << 1)

/*
 * There is an INT DMA ERR mis-match between eSDHC and STD SDHC SPEC:
 * Bit25 is used in STD SPEC, and is reserved in fsl eSDHC design,
 * but bit28 is used as the INT DMA ERR in fsl eSDHC design.
 * Define this macro DMA error INT for fsl eSDHC
 */
#define ESDHC_INT_VENDOR_SPEC_DMA_ERR	(1 << 28)

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

enum imx_esdhc_type {
	IMX25_ESDHC,
	IMX35_ESDHC,
	IMX51_ESDHC,
	IMX53_ESDHC,
	IMX6Q_USDHC,
};

struct pltfm_imx_data {
	int flags;
	u32 scratchpad;
	enum imx_esdhc_type devtype;
	struct pinctrl *pinctrl;
	struct esdhc_platform_data boarddata;
	struct clk *clk_ipg;
	struct clk *clk_ahb;
	struct clk *clk_per;
};

static struct platform_device_id imx_esdhc_devtype[] = {
	{
		.name = "sdhci-esdhc-imx25",
		.driver_data = IMX25_ESDHC,
	}, {
		.name = "sdhci-esdhc-imx35",
		.driver_data = IMX35_ESDHC,
	}, {
		.name = "sdhci-esdhc-imx51",
		.driver_data = IMX51_ESDHC,
	}, {
		.name = "sdhci-esdhc-imx53",
		.driver_data = IMX53_ESDHC,
	}, {
		.name = "sdhci-usdhc-imx6q",
		.driver_data = IMX6Q_USDHC,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, imx_esdhc_devtype);

static const struct of_device_id imx_esdhc_dt_ids[] = {
	{ .compatible = "fsl,imx25-esdhc", .data = &imx_esdhc_devtype[IMX25_ESDHC], },
	{ .compatible = "fsl,imx35-esdhc", .data = &imx_esdhc_devtype[IMX35_ESDHC], },
	{ .compatible = "fsl,imx51-esdhc", .data = &imx_esdhc_devtype[IMX51_ESDHC], },
	{ .compatible = "fsl,imx53-esdhc", .data = &imx_esdhc_devtype[IMX53_ESDHC], },
	{ .compatible = "fsl,imx6q-usdhc", .data = &imx_esdhc_devtype[IMX6Q_USDHC], },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_esdhc_dt_ids);

static inline int is_imx25_esdhc(struct pltfm_imx_data *data)
{
	return data->devtype == IMX25_ESDHC;
}

static inline int is_imx35_esdhc(struct pltfm_imx_data *data)
{
	return data->devtype == IMX35_ESDHC;
}

static inline int is_imx51_esdhc(struct pltfm_imx_data *data)
{
	return data->devtype == IMX51_ESDHC;
}

static inline int is_imx53_esdhc(struct pltfm_imx_data *data)
{
	return data->devtype == IMX53_ESDHC;
}

static inline int is_imx6q_usdhc(struct pltfm_imx_data *data)
{
	return data->devtype == IMX6Q_USDHC;
}

static inline void esdhc_clrset_le(struct sdhci_host *host, u32 mask, u32 val, int reg)
{
	void __iomem *base = host->ioaddr + (reg & ~0x3);
	u32 shift = (reg & 0x3) * 8;

	writel(((readl(base) & ~(mask << shift)) | (val << shift)), base);
}

static u32 esdhc_readl_le(struct sdhci_host *host, int reg)
{
	u32 val = readl(host->ioaddr + reg);

	if (unlikely(reg == SDHCI_CAPABILITIES)) {
		/* In FSL esdhc IC module, only bit20 is used to indicate the
		 * ADMA2 capability of esdhc, but this bit is messed up on
		 * some SOCs (e.g. on MX25, MX35 this bit is set, but they
		 * don't actually support ADMA2). So set the BROKEN_ADMA
		 * uirk on MX25/35 platforms.
		 */

		if (val & SDHCI_CAN_DO_ADMA1) {
			val &= ~SDHCI_CAN_DO_ADMA1;
			val |= SDHCI_CAN_DO_ADMA2;
		}
	}

	if (unlikely(reg == SDHCI_INT_STATUS)) {
		if (val & ESDHC_INT_VENDOR_SPEC_DMA_ERR) {
			val &= ~ESDHC_INT_VENDOR_SPEC_DMA_ERR;
			val |= SDHCI_INT_ADMA_ERROR;
		}
	}

	return val;
}

static void esdhc_writel_le(struct sdhci_host *host, u32 val, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = pltfm_host->priv;
	u32 data;

	if (unlikely(reg == SDHCI_INT_ENABLE || reg == SDHCI_SIGNAL_ENABLE)) {
		if (val & SDHCI_INT_CARD_INT) {
			/*
			 * Clear and then set D3CD bit to avoid missing the
			 * card interrupt.  This is a eSDHC controller problem
			 * so we need to apply the following workaround: clear
			 * and set D3CD bit will make eSDHC re-sample the card
			 * interrupt. In case a card interrupt was lost,
			 * re-sample it by the following steps.
			 */
			data = readl(host->ioaddr + SDHCI_HOST_CONTROL);
			data &= ~ESDHC_CTRL_D3CD;
			writel(data, host->ioaddr + SDHCI_HOST_CONTROL);
			data |= ESDHC_CTRL_D3CD;
			writel(data, host->ioaddr + SDHCI_HOST_CONTROL);
		}
	}

	if (unlikely((imx_data->flags & ESDHC_FLAG_MULTIBLK_NO_INT)
				&& (reg == SDHCI_INT_STATUS)
				&& (val & SDHCI_INT_DATA_END))) {
			u32 v;
			v = readl(host->ioaddr + ESDHC_VENDOR_SPEC);
			v &= ~ESDHC_VENDOR_SPEC_SDIO_QUIRK;
			writel(v, host->ioaddr + ESDHC_VENDOR_SPEC);
	}

	if (unlikely(reg == SDHCI_INT_ENABLE || reg == SDHCI_SIGNAL_ENABLE)) {
		if (val & SDHCI_INT_ADMA_ERROR) {
			val &= ~SDHCI_INT_ADMA_ERROR;
			val |= ESDHC_INT_VENDOR_SPEC_DMA_ERR;
		}
	}

	writel(val, host->ioaddr + reg);
}

static u16 esdhc_readw_le(struct sdhci_host *host, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = pltfm_host->priv;

	if (unlikely(reg == SDHCI_HOST_VERSION)) {
		reg ^= 2;
		if (is_imx6q_usdhc(imx_data)) {
			/*
			 * The usdhc register returns a wrong host version.
			 * Correct it here.
			 */
			return SDHCI_SPEC_300;
		}
	}

	return readw(host->ioaddr + reg);
}

static void esdhc_writew_le(struct sdhci_host *host, u16 val, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = pltfm_host->priv;

	switch (reg) {
	case SDHCI_TRANSFER_MODE:
		if ((imx_data->flags & ESDHC_FLAG_MULTIBLK_NO_INT)
				&& (host->cmd->opcode == SD_IO_RW_EXTENDED)
				&& (host->cmd->data->blocks > 1)
				&& (host->cmd->data->flags & MMC_DATA_READ)) {
			u32 v;
			v = readl(host->ioaddr + ESDHC_VENDOR_SPEC);
			v |= ESDHC_VENDOR_SPEC_SDIO_QUIRK;
			writel(v, host->ioaddr + ESDHC_VENDOR_SPEC);
		}

		if (is_imx6q_usdhc(imx_data)) {
			u32 m = readl(host->ioaddr + ESDHC_MIX_CTRL);
			/* Swap AC23 bit */
			if (val & SDHCI_TRNS_AUTO_CMD23) {
				val &= ~SDHCI_TRNS_AUTO_CMD23;
				val |= ESDHC_MIX_CTRL_AC23EN;
			}
			m = val | (m & ~ESDHC_MIX_CTRL_SDHCI_MASK);
			writel(m, host->ioaddr + ESDHC_MIX_CTRL);
		} else {
			/*
			 * Postpone this write, we must do it together with a
			 * command write that is down below.
			 */
			imx_data->scratchpad = val;
		}
		return;
	case SDHCI_COMMAND:
		if ((host->cmd->opcode == MMC_STOP_TRANSMISSION ||
		     host->cmd->opcode == MMC_SET_BLOCK_COUNT) &&
	            (imx_data->flags & ESDHC_FLAG_MULTIBLK_NO_INT))
			val |= SDHCI_CMD_ABORTCMD;

		if (is_imx6q_usdhc(imx_data))
			writel(val << 16,
			       host->ioaddr + SDHCI_TRANSFER_MODE);
		else
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
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = pltfm_host->priv;
	u32 new_val;
	u32 mask;

	switch (reg) {
	case SDHCI_POWER_CONTROL:
		/*
		 * FSL put some DMA bits here
		 * If your board has a regulator, code should be here
		 */
		return;
	case SDHCI_HOST_CONTROL:
		/* FSL messed up here, so we need to manually compose it. */
		new_val = val & SDHCI_CTRL_LED;
		/* ensure the endianness */
		new_val |= ESDHC_HOST_CONTROL_LE;
		/* bits 8&9 are reserved on mx25 */
		if (!is_imx25_esdhc(imx_data)) {
			/* DMA mode bits are shifted */
			new_val |= (val & SDHCI_CTRL_DMA_MASK) << 5;
		}

		/*
		 * Do not touch buswidth bits here. This is done in
		 * esdhc_pltfm_bus_width.
		 * Do not touch the D3CD bit either which is used for the
		 * SDIO interrupt errata workaround.
		 */
		mask = 0xffff & ~(ESDHC_CTRL_BUSWIDTH_MASK | ESDHC_CTRL_D3CD);

		esdhc_clrset_le(host, mask, new_val, reg);
		return;
	}
	esdhc_clrset_le(host, 0xff, val, reg);

	/*
	 * The esdhc has a design violation to SDHC spec which tells
	 * that software reset should not affect card detection circuit.
	 * But esdhc clears its SYSCTL register bits [0..2] during the
	 * software reset.  This will stop those clocks that card detection
	 * circuit relies on.  To work around it, we turn the clocks on back
	 * to keep card detection circuit functional.
	 */
	if ((reg == SDHCI_SOFTWARE_RESET) && (val & 1)) {
		esdhc_clrset_le(host, 0x7, 0x7, ESDHC_SYSTEM_CONTROL);
		/*
		 * The reset on usdhc fails to clear MIX_CTRL register.
		 * Do it manually here.
		 */
		if (is_imx6q_usdhc(imx_data))
			writel(0, host->ioaddr + ESDHC_MIX_CTRL);
	}
}

static unsigned int esdhc_pltfm_get_min_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return clk_get_rate(pltfm_host->clk) / 256 / 16;
}

static unsigned int esdhc_pltfm_get_ro(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = pltfm_host->priv;
	struct esdhc_platform_data *boarddata = &imx_data->boarddata;

	switch (boarddata->wp_type) {
	case ESDHC_WP_GPIO:
		return mmc_gpio_get_ro(host->mmc);
	case ESDHC_WP_CONTROLLER:
		return !(readl(host->ioaddr + SDHCI_PRESENT_STATE) &
			       SDHCI_WRITE_PROTECT);
	case ESDHC_WP_NONE:
		break;
	}

	return -ENOSYS;
}

static int esdhc_pltfm_bus_width(struct sdhci_host *host, int width)
{
	u32 ctrl;

	switch (width) {
	case MMC_BUS_WIDTH_8:
		ctrl = ESDHC_CTRL_8BITBUS;
		break;
	case MMC_BUS_WIDTH_4:
		ctrl = ESDHC_CTRL_4BITBUS;
		break;
	default:
		ctrl = 0;
		break;
	}

	esdhc_clrset_le(host, ESDHC_CTRL_BUSWIDTH_MASK, ctrl,
			SDHCI_HOST_CONTROL);

	return 0;
}

static const struct sdhci_ops sdhci_esdhc_ops = {
	.read_l = esdhc_readl_le,
	.read_w = esdhc_readw_le,
	.write_l = esdhc_writel_le,
	.write_w = esdhc_writew_le,
	.write_b = esdhc_writeb_le,
	.set_clock = esdhc_set_clock,
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.get_min_clock = esdhc_pltfm_get_min_clock,
	.get_ro = esdhc_pltfm_get_ro,
	.platform_bus_width = esdhc_pltfm_bus_width,
};

static const struct sdhci_pltfm_data sdhci_esdhc_imx_pdata = {
	.quirks = ESDHC_DEFAULT_QUIRKS | SDHCI_QUIRK_NO_HISPD_BIT
			| SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC
			| SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC
			| SDHCI_QUIRK_BROKEN_CARD_DETECTION,
	.ops = &sdhci_esdhc_ops,
};

#ifdef CONFIG_OF
static int
sdhci_esdhc_imx_probe_dt(struct platform_device *pdev,
			 struct esdhc_platform_data *boarddata)
{
	struct device_node *np = pdev->dev.of_node;

	if (!np)
		return -ENODEV;

	if (of_get_property(np, "non-removable", NULL))
		boarddata->cd_type = ESDHC_CD_PERMANENT;

	if (of_get_property(np, "fsl,cd-controller", NULL))
		boarddata->cd_type = ESDHC_CD_CONTROLLER;

	if (of_get_property(np, "fsl,wp-controller", NULL))
		boarddata->wp_type = ESDHC_WP_CONTROLLER;

	boarddata->cd_gpio = of_get_named_gpio(np, "cd-gpios", 0);
	if (gpio_is_valid(boarddata->cd_gpio))
		boarddata->cd_type = ESDHC_CD_GPIO;

	boarddata->wp_gpio = of_get_named_gpio(np, "wp-gpios", 0);
	if (gpio_is_valid(boarddata->wp_gpio))
		boarddata->wp_type = ESDHC_WP_GPIO;

	of_property_read_u32(np, "bus-width", &boarddata->max_bus_width);

	return 0;
}
#else
static inline int
sdhci_esdhc_imx_probe_dt(struct platform_device *pdev,
			 struct esdhc_platform_data *boarddata)
{
	return -ENODEV;
}
#endif

static int sdhci_esdhc_imx_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(imx_esdhc_dt_ids, &pdev->dev);
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_host *host;
	struct esdhc_platform_data *boarddata;
	int err;
	struct pltfm_imx_data *imx_data;

	host = sdhci_pltfm_init(pdev, &sdhci_esdhc_imx_pdata);
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);

	imx_data = devm_kzalloc(&pdev->dev, sizeof(*imx_data), GFP_KERNEL);
	if (!imx_data) {
		err = -ENOMEM;
		goto free_sdhci;
	}

	if (of_id)
		pdev->id_entry = of_id->data;
	imx_data->devtype = pdev->id_entry->driver_data;
	pltfm_host->priv = imx_data;

	imx_data->clk_ipg = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(imx_data->clk_ipg)) {
		err = PTR_ERR(imx_data->clk_ipg);
		goto free_sdhci;
	}

	imx_data->clk_ahb = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(imx_data->clk_ahb)) {
		err = PTR_ERR(imx_data->clk_ahb);
		goto free_sdhci;
	}

	imx_data->clk_per = devm_clk_get(&pdev->dev, "per");
	if (IS_ERR(imx_data->clk_per)) {
		err = PTR_ERR(imx_data->clk_per);
		goto free_sdhci;
	}

	pltfm_host->clk = imx_data->clk_per;

	clk_prepare_enable(imx_data->clk_per);
	clk_prepare_enable(imx_data->clk_ipg);
	clk_prepare_enable(imx_data->clk_ahb);

	imx_data->pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(imx_data->pinctrl)) {
		err = PTR_ERR(imx_data->pinctrl);
		goto disable_clk;
	}

	host->quirks |= SDHCI_QUIRK_BROKEN_TIMEOUT_VAL;

	if (is_imx25_esdhc(imx_data) || is_imx35_esdhc(imx_data))
		/* Fix errata ENGcm07207 present on i.MX25 and i.MX35 */
		host->quirks |= SDHCI_QUIRK_NO_MULTIBLOCK
			| SDHCI_QUIRK_BROKEN_ADMA;

	if (is_imx53_esdhc(imx_data))
		imx_data->flags |= ESDHC_FLAG_MULTIBLK_NO_INT;

	/*
	 * The imx6q ROM code will change the default watermark level setting
	 * to something insane.  Change it back here.
	 */
	if (is_imx6q_usdhc(imx_data))
		writel(0x08100810, host->ioaddr + ESDHC_WTMK_LVL);

	boarddata = &imx_data->boarddata;
	if (sdhci_esdhc_imx_probe_dt(pdev, boarddata) < 0) {
		if (!host->mmc->parent->platform_data) {
			dev_err(mmc_dev(host->mmc), "no board data!\n");
			err = -EINVAL;
			goto disable_clk;
		}
		imx_data->boarddata = *((struct esdhc_platform_data *)
					host->mmc->parent->platform_data);
	}

	/* write_protect */
	if (boarddata->wp_type == ESDHC_WP_GPIO) {
		err = mmc_gpio_request_ro(host->mmc, boarddata->wp_gpio);
		if (err) {
			dev_err(mmc_dev(host->mmc),
				"failed to request write-protect gpio!\n");
			goto disable_clk;
		}
		host->mmc->caps2 |= MMC_CAP2_RO_ACTIVE_HIGH;
	}

	/* card_detect */
	switch (boarddata->cd_type) {
	case ESDHC_CD_GPIO:
		err = mmc_gpio_request_cd(host->mmc, boarddata->cd_gpio);
		if (err) {
			dev_err(mmc_dev(host->mmc),
				"failed to request card-detect gpio!\n");
			goto disable_clk;
		}
		/* fall through */

	case ESDHC_CD_CONTROLLER:
		/* we have a working card_detect back */
		host->quirks &= ~SDHCI_QUIRK_BROKEN_CARD_DETECTION;
		break;

	case ESDHC_CD_PERMANENT:
		host->mmc->caps = MMC_CAP_NONREMOVABLE;
		break;

	case ESDHC_CD_NONE:
		break;
	}

	switch (boarddata->max_bus_width) {
	case 8:
		host->mmc->caps |= MMC_CAP_8_BIT_DATA | MMC_CAP_4_BIT_DATA;
		break;
	case 4:
		host->mmc->caps |= MMC_CAP_4_BIT_DATA;
		break;
	case 1:
	default:
		host->quirks |= SDHCI_QUIRK_FORCE_1_BIT_DATA;
		break;
	}

	err = sdhci_add_host(host);
	if (err)
		goto disable_clk;

	return 0;

disable_clk:
	clk_disable_unprepare(imx_data->clk_per);
	clk_disable_unprepare(imx_data->clk_ipg);
	clk_disable_unprepare(imx_data->clk_ahb);
free_sdhci:
	sdhci_pltfm_free(pdev);
	return err;
}

static int sdhci_esdhc_imx_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct pltfm_imx_data *imx_data = pltfm_host->priv;
	int dead = (readl(host->ioaddr + SDHCI_INT_STATUS) == 0xffffffff);

	sdhci_remove_host(host, dead);

	clk_disable_unprepare(imx_data->clk_per);
	clk_disable_unprepare(imx_data->clk_ipg);
	clk_disable_unprepare(imx_data->clk_ahb);

	sdhci_pltfm_free(pdev);

	return 0;
}

static struct platform_driver sdhci_esdhc_imx_driver = {
	.driver		= {
		.name	= "sdhci-esdhc-imx",
		.owner	= THIS_MODULE,
		.of_match_table = imx_esdhc_dt_ids,
		.pm	= SDHCI_PLTFM_PMOPS,
	},
	.id_table	= imx_esdhc_devtype,
	.probe		= sdhci_esdhc_imx_probe,
	.remove		= sdhci_esdhc_imx_remove,
};

module_platform_driver(sdhci_esdhc_imx_driver);

MODULE_DESCRIPTION("SDHCI driver for Freescale i.MX eSDHC");
MODULE_AUTHOR("Wolfram Sang <w.sang@pengutronix.de>");
MODULE_LICENSE("GPL v2");
