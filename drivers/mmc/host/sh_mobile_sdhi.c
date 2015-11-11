/*
 * SuperH Mobile SDHI
 *
 * Copyright (C) 2009 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on "Compaq ASIC3 support":
 *
 * Copyright 2001 Compaq Computer Corporation.
 * Copyright 2004-2005 Phil Blundell
 * Copyright 2007-2008 OpenedHand Ltd.
 *
 * Authors: Phil Blundell <pb@handhelds.org>,
 *	    Samuel Ortiz <sameo@openedhand.com>
 *
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <linux/mfd/tmio.h>
#include <linux/sh_dma.h>
#include <linux/delay.h>

#include "tmio_mmc.h"

#define EXT_ACC           0xe4

#define host_to_priv(host) container_of((host)->pdata, struct sh_mobile_sdhi, mmc_data)

struct sh_mobile_sdhi_of_data {
	unsigned long tmio_flags;
	unsigned long capabilities;
	unsigned long capabilities2;
	enum dma_slave_buswidth dma_buswidth;
	dma_addr_t dma_rx_offset;
};

static const struct sh_mobile_sdhi_of_data sh_mobile_sdhi_of_cfg[] = {
	{
		.tmio_flags = TMIO_MMC_HAS_IDLE_WAIT,
	},
};

static const struct sh_mobile_sdhi_of_data of_rcar_gen1_compatible = {
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT | TMIO_MMC_WRPROTECT_DISABLE |
			  TMIO_MMC_CLK_ACTUAL,
	.capabilities	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ,
};

static const struct sh_mobile_sdhi_of_data of_rcar_gen2_compatible = {
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT | TMIO_MMC_WRPROTECT_DISABLE |
			  TMIO_MMC_CLK_ACTUAL,
	.capabilities	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ,
	.dma_buswidth	= DMA_SLAVE_BUSWIDTH_4_BYTES,
	.dma_rx_offset	= 0x2000,
};

static const struct of_device_id sh_mobile_sdhi_of_match[] = {
	{ .compatible = "renesas,sdhi-shmobile" },
	{ .compatible = "renesas,sdhi-sh7372" },
	{ .compatible = "renesas,sdhi-sh73a0", .data = &sh_mobile_sdhi_of_cfg[0], },
	{ .compatible = "renesas,sdhi-r8a73a4", .data = &sh_mobile_sdhi_of_cfg[0], },
	{ .compatible = "renesas,sdhi-r8a7740", .data = &sh_mobile_sdhi_of_cfg[0], },
	{ .compatible = "renesas,sdhi-r8a7778", .data = &of_rcar_gen1_compatible, },
	{ .compatible = "renesas,sdhi-r8a7779", .data = &of_rcar_gen1_compatible, },
	{ .compatible = "renesas,sdhi-r8a7790", .data = &of_rcar_gen2_compatible, },
	{ .compatible = "renesas,sdhi-r8a7791", .data = &of_rcar_gen2_compatible, },
	{ .compatible = "renesas,sdhi-r8a7792", .data = &of_rcar_gen2_compatible, },
	{ .compatible = "renesas,sdhi-r8a7793", .data = &of_rcar_gen2_compatible, },
	{ .compatible = "renesas,sdhi-r8a7794", .data = &of_rcar_gen2_compatible, },
	{},
};
MODULE_DEVICE_TABLE(of, sh_mobile_sdhi_of_match);

struct sh_mobile_sdhi {
	struct clk *clk;
	struct tmio_mmc_data mmc_data;
	struct tmio_mmc_dma dma_priv;
};

static void sh_mobile_sdhi_sdbuf_width(struct tmio_mmc_host *host, int width)
{
	u32 val;

	/*
	 * see also
	 *	sh_mobile_sdhi_of_data :: dma_buswidth
	 */
	switch (sd_ctrl_read16(host, CTL_VERSION)) {
	case 0x490C:
		val = (width == 32) ? 0x0001 : 0x0000;
		break;
	case 0xCB0D:
		val = (width == 32) ? 0x0000 : 0x0001;
		break;
	default:
		/* nothing to do */
		return;
	}

	sd_ctrl_write16(host, EXT_ACC, val);
}

static int sh_mobile_sdhi_clk_enable(struct platform_device *pdev, unsigned int *f)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	struct tmio_mmc_host *host = mmc_priv(mmc);
	struct sh_mobile_sdhi *priv = host_to_priv(host);
	int ret = clk_prepare_enable(priv->clk);
	if (ret < 0)
		return ret;

	*f = clk_get_rate(priv->clk);

	/* enable 16bit data access on SDBUF as default */
	sh_mobile_sdhi_sdbuf_width(host, 16);

	return 0;
}

static void sh_mobile_sdhi_clk_disable(struct platform_device *pdev)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	struct tmio_mmc_host *host = mmc_priv(mmc);
	struct sh_mobile_sdhi *priv = host_to_priv(host);
	clk_disable_unprepare(priv->clk);
}

static int sh_mobile_sdhi_wait_idle(struct tmio_mmc_host *host)
{
	int timeout = 1000;

	while (--timeout && !(sd_ctrl_read16(host, CTL_STATUS2) & (1 << 13)))
		udelay(1);

	if (!timeout) {
		dev_warn(&host->pdev->dev, "timeout waiting for SD bus idle\n");
		return -EBUSY;
	}

	return 0;
}

static int sh_mobile_sdhi_write16_hook(struct tmio_mmc_host *host, int addr)
{
	switch (addr)
	{
	case CTL_SD_CMD:
	case CTL_STOP_INTERNAL_ACTION:
	case CTL_XFER_BLK_COUNT:
	case CTL_SD_CARD_CLK_CTL:
	case CTL_SD_XFER_LEN:
	case CTL_SD_MEM_CARD_OPT:
	case CTL_TRANSACTION_CTL:
	case CTL_DMA_ENABLE:
		return sh_mobile_sdhi_wait_idle(host);
	}

	return 0;
}

static int sh_mobile_sdhi_multi_io_quirk(struct mmc_card *card,
					 unsigned int direction, int blk_size)
{
	/*
	 * In Renesas controllers, when performing a
	 * multiple block read of one or two blocks,
	 * depending on the timing with which the
	 * response register is read, the response
	 * value may not be read properly.
	 * Use single block read for this HW bug
	 */
	if ((direction == MMC_DATA_READ) &&
	    blk_size == 2)
		return 1;

	return blk_size;
}

static void sh_mobile_sdhi_enable_dma(struct tmio_mmc_host *host, bool enable)
{
	sd_ctrl_write16(host, CTL_DMA_ENABLE, enable ? 2 : 0);

	/* enable 32bit access if DMA mode if possibile */
	sh_mobile_sdhi_sdbuf_width(host, enable ? 32 : 16);
}

static int sh_mobile_sdhi_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
		of_match_device(sh_mobile_sdhi_of_match, &pdev->dev);
	struct sh_mobile_sdhi *priv;
	struct tmio_mmc_data *mmc_data;
	struct tmio_mmc_data *mmd = pdev->dev.platform_data;
	struct tmio_mmc_host *host;
	struct resource *res;
	int irq, ret, i = 0;
	bool multiplexed_isr = true;
	struct tmio_mmc_dma *dma_priv;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct sh_mobile_sdhi), GFP_KERNEL);
	if (priv == NULL) {
		dev_err(&pdev->dev, "kzalloc failed\n");
		return -ENOMEM;
	}

	mmc_data = &priv->mmc_data;
	dma_priv = &priv->dma_priv;

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk)) {
		ret = PTR_ERR(priv->clk);
		dev_err(&pdev->dev, "cannot get clock: %d\n", ret);
		goto eprobe;
	}

	host = tmio_mmc_host_alloc(pdev);
	if (!host) {
		ret = -ENOMEM;
		goto eprobe;
	}

	host->dma		= dma_priv;
	host->write16_hook	= sh_mobile_sdhi_write16_hook;
	host->clk_enable	= sh_mobile_sdhi_clk_enable;
	host->clk_disable	= sh_mobile_sdhi_clk_disable;
	host->multi_io_quirk	= sh_mobile_sdhi_multi_io_quirk;
	/* SD control register space size is 0x100, 0x200 for bus_shift=1 */
	if (resource_size(res) > 0x100)
		host->bus_shift = 1;
	else
		host->bus_shift = 0;

	if (mmd)
		*mmc_data = *mmd;

	dma_priv->filter = shdma_chan_filter;
	dma_priv->enable = sh_mobile_sdhi_enable_dma;

	mmc_data->alignment_shift = 1; /* 2-byte alignment */
	mmc_data->capabilities |= MMC_CAP_MMC_HIGHSPEED;

	/*
	 * All SDHI blocks support 2-byte and larger block sizes in 4-bit
	 * bus width mode.
	 */
	mmc_data->flags |= TMIO_MMC_BLKSZ_2BYTES;

	/*
	 * All SDHI blocks support SDIO IRQ signalling.
	 */
	mmc_data->flags |= TMIO_MMC_SDIO_IRQ;

	/*
	 * All SDHI have CMD12 controll bit
	 */
	mmc_data->flags |= TMIO_MMC_HAVE_CMD12_CTRL;

	/*
	 * All SDHI need SDIO_INFO1 reserved bit
	 */
	mmc_data->flags |= TMIO_MMC_SDIO_STATUS_QUIRK;

	if (of_id && of_id->data) {
		const struct sh_mobile_sdhi_of_data *of_data = of_id->data;
		mmc_data->flags |= of_data->tmio_flags;
		mmc_data->capabilities |= of_data->capabilities;
		mmc_data->capabilities2 |= of_data->capabilities2;
		mmc_data->dma_rx_offset = of_data->dma_rx_offset;
		dma_priv->dma_buswidth = of_data->dma_buswidth;
	}

	ret = tmio_mmc_host_probe(host, mmc_data);
	if (ret < 0)
		goto efree;

	/*
	 * Allow one or more specific (named) ISRs or
	 * one or more multiplexed (un-named) ISRs.
	 */

	irq = platform_get_irq_byname(pdev, SH_MOBILE_SDHI_IRQ_CARD_DETECT);
	if (irq >= 0) {
		multiplexed_isr = false;
		ret = devm_request_irq(&pdev->dev, irq, tmio_mmc_card_detect_irq, 0,
				  dev_name(&pdev->dev), host);
		if (ret)
			goto eirq;
	}

	irq = platform_get_irq_byname(pdev, SH_MOBILE_SDHI_IRQ_SDIO);
	if (irq >= 0) {
		multiplexed_isr = false;
		ret = devm_request_irq(&pdev->dev, irq, tmio_mmc_sdio_irq, 0,
				  dev_name(&pdev->dev), host);
		if (ret)
			goto eirq;
	}

	irq = platform_get_irq_byname(pdev, SH_MOBILE_SDHI_IRQ_SDCARD);
	if (irq >= 0) {
		multiplexed_isr = false;
		ret = devm_request_irq(&pdev->dev, irq, tmio_mmc_sdcard_irq, 0,
				  dev_name(&pdev->dev), host);
		if (ret)
			goto eirq;
	} else if (!multiplexed_isr) {
		dev_err(&pdev->dev,
			"Principal SD-card IRQ is missing among named interrupts\n");
		ret = irq;
		goto eirq;
	}

	if (multiplexed_isr) {
		while (1) {
			irq = platform_get_irq(pdev, i);
			if (irq < 0)
				break;
			i++;
			ret = devm_request_irq(&pdev->dev, irq, tmio_mmc_irq, 0,
					  dev_name(&pdev->dev), host);
			if (ret)
				goto eirq;
		}

		/* There must be at least one IRQ source */
		if (!i) {
			ret = irq;
			goto eirq;
		}
	}

	dev_info(&pdev->dev, "%s base at 0x%08lx clock rate %u MHz\n",
		 mmc_hostname(host->mmc), (unsigned long)
		 (platform_get_resource(pdev, IORESOURCE_MEM, 0)->start),
		 host->mmc->f_max / 1000000);

	return ret;

eirq:
	tmio_mmc_host_remove(host);
efree:
	tmio_mmc_host_free(host);
eprobe:
	return ret;
}

static int sh_mobile_sdhi_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	struct tmio_mmc_host *host = mmc_priv(mmc);

	tmio_mmc_host_remove(host);

	return 0;
}

static const struct dev_pm_ops tmio_mmc_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
			pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(tmio_mmc_host_runtime_suspend,
			tmio_mmc_host_runtime_resume,
			NULL)
};

static struct platform_driver sh_mobile_sdhi_driver = {
	.driver		= {
		.name	= "sh_mobile_sdhi",
		.pm	= &tmio_mmc_dev_pm_ops,
		.of_match_table = sh_mobile_sdhi_of_match,
	},
	.probe		= sh_mobile_sdhi_probe,
	.remove		= sh_mobile_sdhi_remove,
};

module_platform_driver(sh_mobile_sdhi_driver);

MODULE_DESCRIPTION("SuperH Mobile SDHI driver");
MODULE_AUTHOR("Magnus Damm");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sh_mobile_sdhi");
