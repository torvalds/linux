/*
 * DMA support use of SYS DMAC with SDHI SD/SDIO controller
 *
 * Copyright (C) 2016-17 Renesas Electronics Corporation
 * Copyright (C) 2016-17 Sang Engineering, Wolfram Sang
 * Copyright (C) 2017 Horms Solutions, Simon Horman
 * Copyright (C) 2010-2011 Guennadi Liakhovetski
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pagemap.h>
#include <linux/scatterlist.h>
#include <linux/sys_soc.h>

#include "renesas_sdhi.h"
#include "tmio_mmc.h"

#define TMIO_MMC_MIN_DMA_LEN 8

static const struct renesas_sdhi_of_data of_default_cfg = {
	.tmio_flags = TMIO_MMC_HAS_IDLE_WAIT,
};

static const struct renesas_sdhi_of_data of_rz_compatible = {
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT | TMIO_MMC_32BIT_DATA_PORT |
			  TMIO_MMC_HAVE_CBSY,
	.tmio_ocr_mask	= MMC_VDD_32_33,
	.capabilities	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ,
};

static const struct renesas_sdhi_of_data of_rcar_gen1_compatible = {
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT | TMIO_MMC_WRPROTECT_DISABLE |
			  TMIO_MMC_CLK_ACTUAL,
	.capabilities	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ,
};

/* Definitions for sampling clocks */
static struct renesas_sdhi_scc rcar_gen2_scc_taps[] = {
	{
		.clk_rate = 156000000,
		.tap = 0x00000703,
	},
	{
		.clk_rate = 0,
		.tap = 0x00000300,
	},
};

static const struct renesas_sdhi_of_data of_rcar_gen2_compatible = {
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT | TMIO_MMC_WRPROTECT_DISABLE |
			  TMIO_MMC_CLK_ACTUAL | TMIO_MMC_HAVE_CBSY |
			  TMIO_MMC_MIN_RCAR2,
	.capabilities	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ |
			  MMC_CAP_CMD23,
	.dma_buswidth	= DMA_SLAVE_BUSWIDTH_4_BYTES,
	.dma_rx_offset	= 0x2000,
	.scc_offset	= 0x0300,
	.taps		= rcar_gen2_scc_taps,
	.taps_num	= ARRAY_SIZE(rcar_gen2_scc_taps),
};

/* Definitions for sampling clocks */
static struct renesas_sdhi_scc rcar_gen3_scc_taps[] = {
	{
		.clk_rate = 0,
		.tap = 0x00000300,
	},
};

static const struct renesas_sdhi_of_data of_rcar_gen3_compatible = {
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT | TMIO_MMC_WRPROTECT_DISABLE |
			  TMIO_MMC_CLK_ACTUAL | TMIO_MMC_HAVE_CBSY |
			  TMIO_MMC_MIN_RCAR2,
	.capabilities	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ |
			  MMC_CAP_CMD23,
	.bus_shift	= 2,
	.scc_offset	= 0x1000,
	.taps		= rcar_gen3_scc_taps,
	.taps_num	= ARRAY_SIZE(rcar_gen3_scc_taps),
};

static const struct of_device_id renesas_sdhi_sys_dmac_of_match[] = {
	{ .compatible = "renesas,sdhi-shmobile" },
	{ .compatible = "renesas,sdhi-sh73a0", .data = &of_default_cfg, },
	{ .compatible = "renesas,sdhi-r8a73a4", .data = &of_default_cfg, },
	{ .compatible = "renesas,sdhi-r8a7740", .data = &of_default_cfg, },
	{ .compatible = "renesas,sdhi-r7s72100", .data = &of_rz_compatible, },
	{ .compatible = "renesas,sdhi-r8a7778", .data = &of_rcar_gen1_compatible, },
	{ .compatible = "renesas,sdhi-r8a7779", .data = &of_rcar_gen1_compatible, },
	{ .compatible = "renesas,sdhi-r8a7743", .data = &of_rcar_gen2_compatible, },
	{ .compatible = "renesas,sdhi-r8a7745", .data = &of_rcar_gen2_compatible, },
	{ .compatible = "renesas,sdhi-r8a7790", .data = &of_rcar_gen2_compatible, },
	{ .compatible = "renesas,sdhi-r8a7791", .data = &of_rcar_gen2_compatible, },
	{ .compatible = "renesas,sdhi-r8a7792", .data = &of_rcar_gen2_compatible, },
	{ .compatible = "renesas,sdhi-r8a7793", .data = &of_rcar_gen2_compatible, },
	{ .compatible = "renesas,sdhi-r8a7794", .data = &of_rcar_gen2_compatible, },
	{ .compatible = "renesas,sdhi-r8a7795", .data = &of_rcar_gen3_compatible, },
	{ .compatible = "renesas,sdhi-r8a7796", .data = &of_rcar_gen3_compatible, },
	{},
};
MODULE_DEVICE_TABLE(of, renesas_sdhi_sys_dmac_of_match);

static void renesas_sdhi_sys_dmac_enable_dma(struct tmio_mmc_host *host,
					     bool enable)
{
	if (!host->chan_tx || !host->chan_rx)
		return;

	if (host->dma->enable)
		host->dma->enable(host, enable);
}

static void renesas_sdhi_sys_dmac_abort_dma(struct tmio_mmc_host *host)
{
	renesas_sdhi_sys_dmac_enable_dma(host, false);

	if (host->chan_rx)
		dmaengine_terminate_all(host->chan_rx);
	if (host->chan_tx)
		dmaengine_terminate_all(host->chan_tx);

	renesas_sdhi_sys_dmac_enable_dma(host, true);
}

static void renesas_sdhi_sys_dmac_dataend_dma(struct tmio_mmc_host *host)
{
	complete(&host->dma_dataend);
}

static void renesas_sdhi_sys_dmac_dma_callback(void *arg)
{
	struct tmio_mmc_host *host = arg;

	spin_lock_irq(&host->lock);

	if (!host->data)
		goto out;

	if (host->data->flags & MMC_DATA_READ)
		dma_unmap_sg(host->chan_rx->device->dev,
			     host->sg_ptr, host->sg_len,
			     DMA_FROM_DEVICE);
	else
		dma_unmap_sg(host->chan_tx->device->dev,
			     host->sg_ptr, host->sg_len,
			     DMA_TO_DEVICE);

	spin_unlock_irq(&host->lock);

	wait_for_completion(&host->dma_dataend);

	spin_lock_irq(&host->lock);
	tmio_mmc_do_data_irq(host);
out:
	spin_unlock_irq(&host->lock);
}

static void renesas_sdhi_sys_dmac_start_dma_rx(struct tmio_mmc_host *host)
{
	struct scatterlist *sg = host->sg_ptr, *sg_tmp;
	struct dma_async_tx_descriptor *desc = NULL;
	struct dma_chan *chan = host->chan_rx;
	dma_cookie_t cookie;
	int ret, i;
	bool aligned = true, multiple = true;
	unsigned int align = (1 << host->pdata->alignment_shift) - 1;

	for_each_sg(sg, sg_tmp, host->sg_len, i) {
		if (sg_tmp->offset & align)
			aligned = false;
		if (sg_tmp->length & align) {
			multiple = false;
			break;
		}
	}

	if ((!aligned && (host->sg_len > 1 || sg->length > PAGE_SIZE ||
			  (align & PAGE_MASK))) || !multiple) {
		ret = -EINVAL;
		goto pio;
	}

	if (sg->length < TMIO_MMC_MIN_DMA_LEN) {
		host->force_pio = true;
		return;
	}

	tmio_mmc_disable_mmc_irqs(host, TMIO_STAT_RXRDY);

	/* The only sg element can be unaligned, use our bounce buffer then */
	if (!aligned) {
		sg_init_one(&host->bounce_sg, host->bounce_buf, sg->length);
		host->sg_ptr = &host->bounce_sg;
		sg = host->sg_ptr;
	}

	ret = dma_map_sg(chan->device->dev, sg, host->sg_len, DMA_FROM_DEVICE);
	if (ret > 0)
		desc = dmaengine_prep_slave_sg(chan, sg, ret, DMA_DEV_TO_MEM,
					       DMA_CTRL_ACK);

	if (desc) {
		reinit_completion(&host->dma_dataend);
		desc->callback = renesas_sdhi_sys_dmac_dma_callback;
		desc->callback_param = host;

		cookie = dmaengine_submit(desc);
		if (cookie < 0) {
			desc = NULL;
			ret = cookie;
		}
	}
pio:
	if (!desc) {
		/* DMA failed, fall back to PIO */
		renesas_sdhi_sys_dmac_enable_dma(host, false);
		if (ret >= 0)
			ret = -EIO;
		host->chan_rx = NULL;
		dma_release_channel(chan);
		/* Free the Tx channel too */
		chan = host->chan_tx;
		if (chan) {
			host->chan_tx = NULL;
			dma_release_channel(chan);
		}
		dev_warn(&host->pdev->dev,
			 "DMA failed: %d, falling back to PIO\n", ret);
	}
}

static void renesas_sdhi_sys_dmac_start_dma_tx(struct tmio_mmc_host *host)
{
	struct scatterlist *sg = host->sg_ptr, *sg_tmp;
	struct dma_async_tx_descriptor *desc = NULL;
	struct dma_chan *chan = host->chan_tx;
	dma_cookie_t cookie;
	int ret, i;
	bool aligned = true, multiple = true;
	unsigned int align = (1 << host->pdata->alignment_shift) - 1;

	for_each_sg(sg, sg_tmp, host->sg_len, i) {
		if (sg_tmp->offset & align)
			aligned = false;
		if (sg_tmp->length & align) {
			multiple = false;
			break;
		}
	}

	if ((!aligned && (host->sg_len > 1 || sg->length > PAGE_SIZE ||
			  (align & PAGE_MASK))) || !multiple) {
		ret = -EINVAL;
		goto pio;
	}

	if (sg->length < TMIO_MMC_MIN_DMA_LEN) {
		host->force_pio = true;
		return;
	}

	tmio_mmc_disable_mmc_irqs(host, TMIO_STAT_TXRQ);

	/* The only sg element can be unaligned, use our bounce buffer then */
	if (!aligned) {
		unsigned long flags;
		void *sg_vaddr = tmio_mmc_kmap_atomic(sg, &flags);

		sg_init_one(&host->bounce_sg, host->bounce_buf, sg->length);
		memcpy(host->bounce_buf, sg_vaddr, host->bounce_sg.length);
		tmio_mmc_kunmap_atomic(sg, &flags, sg_vaddr);
		host->sg_ptr = &host->bounce_sg;
		sg = host->sg_ptr;
	}

	ret = dma_map_sg(chan->device->dev, sg, host->sg_len, DMA_TO_DEVICE);
	if (ret > 0)
		desc = dmaengine_prep_slave_sg(chan, sg, ret, DMA_MEM_TO_DEV,
					       DMA_CTRL_ACK);

	if (desc) {
		reinit_completion(&host->dma_dataend);
		desc->callback = renesas_sdhi_sys_dmac_dma_callback;
		desc->callback_param = host;

		cookie = dmaengine_submit(desc);
		if (cookie < 0) {
			desc = NULL;
			ret = cookie;
		}
	}
pio:
	if (!desc) {
		/* DMA failed, fall back to PIO */
		renesas_sdhi_sys_dmac_enable_dma(host, false);
		if (ret >= 0)
			ret = -EIO;
		host->chan_tx = NULL;
		dma_release_channel(chan);
		/* Free the Rx channel too */
		chan = host->chan_rx;
		if (chan) {
			host->chan_rx = NULL;
			dma_release_channel(chan);
		}
		dev_warn(&host->pdev->dev,
			 "DMA failed: %d, falling back to PIO\n", ret);
	}
}

static void renesas_sdhi_sys_dmac_start_dma(struct tmio_mmc_host *host,
					    struct mmc_data *data)
{
	if (data->flags & MMC_DATA_READ) {
		if (host->chan_rx)
			renesas_sdhi_sys_dmac_start_dma_rx(host);
	} else {
		if (host->chan_tx)
			renesas_sdhi_sys_dmac_start_dma_tx(host);
	}
}

static void renesas_sdhi_sys_dmac_issue_tasklet_fn(unsigned long priv)
{
	struct tmio_mmc_host *host = (struct tmio_mmc_host *)priv;
	struct dma_chan *chan = NULL;

	spin_lock_irq(&host->lock);

	if (host && host->data) {
		if (host->data->flags & MMC_DATA_READ)
			chan = host->chan_rx;
		else
			chan = host->chan_tx;
	}

	spin_unlock_irq(&host->lock);

	tmio_mmc_enable_mmc_irqs(host, TMIO_STAT_DATAEND);

	if (chan)
		dma_async_issue_pending(chan);
}

static void renesas_sdhi_sys_dmac_request_dma(struct tmio_mmc_host *host,
					      struct tmio_mmc_data *pdata)
{
	/* We can only either use DMA for both Tx and Rx or not use it at all */
	if (!host->dma || (!host->pdev->dev.of_node &&
			   (!pdata->chan_priv_tx || !pdata->chan_priv_rx)))
		return;

	if (!host->chan_tx && !host->chan_rx) {
		struct resource *res = platform_get_resource(host->pdev,
							     IORESOURCE_MEM, 0);
		struct dma_slave_config cfg = {};
		dma_cap_mask_t mask;
		int ret;

		if (!res)
			return;

		dma_cap_zero(mask);
		dma_cap_set(DMA_SLAVE, mask);

		host->chan_tx = dma_request_slave_channel_compat(mask,
					host->dma->filter, pdata->chan_priv_tx,
					&host->pdev->dev, "tx");
		dev_dbg(&host->pdev->dev, "%s: TX: got channel %p\n", __func__,
			host->chan_tx);

		if (!host->chan_tx)
			return;

		cfg.direction = DMA_MEM_TO_DEV;
		cfg.dst_addr = res->start +
			(CTL_SD_DATA_PORT << host->bus_shift);
		cfg.dst_addr_width = host->dma->dma_buswidth;
		if (!cfg.dst_addr_width)
			cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		cfg.src_addr = 0;
		ret = dmaengine_slave_config(host->chan_tx, &cfg);
		if (ret < 0)
			goto ecfgtx;

		host->chan_rx = dma_request_slave_channel_compat(mask,
					host->dma->filter, pdata->chan_priv_rx,
					&host->pdev->dev, "rx");
		dev_dbg(&host->pdev->dev, "%s: RX: got channel %p\n", __func__,
			host->chan_rx);

		if (!host->chan_rx)
			goto ereqrx;

		cfg.direction = DMA_DEV_TO_MEM;
		cfg.src_addr = cfg.dst_addr + host->pdata->dma_rx_offset;
		cfg.src_addr_width = host->dma->dma_buswidth;
		if (!cfg.src_addr_width)
			cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		cfg.dst_addr = 0;
		ret = dmaengine_slave_config(host->chan_rx, &cfg);
		if (ret < 0)
			goto ecfgrx;

		host->bounce_buf = (u8 *)__get_free_page(GFP_KERNEL | GFP_DMA);
		if (!host->bounce_buf)
			goto ebouncebuf;

		init_completion(&host->dma_dataend);
		tasklet_init(&host->dma_issue,
			     renesas_sdhi_sys_dmac_issue_tasklet_fn,
			     (unsigned long)host);
	}

	renesas_sdhi_sys_dmac_enable_dma(host, true);

	return;

ebouncebuf:
ecfgrx:
	dma_release_channel(host->chan_rx);
	host->chan_rx = NULL;
ereqrx:
ecfgtx:
	dma_release_channel(host->chan_tx);
	host->chan_tx = NULL;
}

static void renesas_sdhi_sys_dmac_release_dma(struct tmio_mmc_host *host)
{
	if (host->chan_tx) {
		struct dma_chan *chan = host->chan_tx;

		host->chan_tx = NULL;
		dma_release_channel(chan);
	}
	if (host->chan_rx) {
		struct dma_chan *chan = host->chan_rx;

		host->chan_rx = NULL;
		dma_release_channel(chan);
	}
	if (host->bounce_buf) {
		free_pages((unsigned long)host->bounce_buf, 0);
		host->bounce_buf = NULL;
	}
}

static const struct tmio_mmc_dma_ops renesas_sdhi_sys_dmac_dma_ops = {
	.start = renesas_sdhi_sys_dmac_start_dma,
	.enable = renesas_sdhi_sys_dmac_enable_dma,
	.request = renesas_sdhi_sys_dmac_request_dma,
	.release = renesas_sdhi_sys_dmac_release_dma,
	.abort = renesas_sdhi_sys_dmac_abort_dma,
	.dataend = renesas_sdhi_sys_dmac_dataend_dma,
};

/*
 * Whitelist of specific R-Car Gen3 SoC ES versions to use this DMAC
 * implementation. Currently empty as all supported ES versions use
 * the internal DMAC.
 */
static const struct soc_device_attribute gen3_soc_whitelist[] = {
        { /* sentinel */ }
};

static int renesas_sdhi_sys_dmac_probe(struct platform_device *pdev)
{
	if (of_device_get_match_data(&pdev->dev) == &of_rcar_gen3_compatible &&
	    !soc_device_match(gen3_soc_whitelist))
		return -ENODEV;

	return renesas_sdhi_probe(pdev, &renesas_sdhi_sys_dmac_dma_ops);
}

static const struct dev_pm_ops renesas_sdhi_sys_dmac_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(tmio_mmc_host_runtime_suspend,
			   tmio_mmc_host_runtime_resume,
			   NULL)
};

static struct platform_driver renesas_sys_dmac_sdhi_driver = {
	.driver		= {
		.name	= "sh_mobile_sdhi",
		.pm	= &renesas_sdhi_sys_dmac_dev_pm_ops,
		.of_match_table = renesas_sdhi_sys_dmac_of_match,
	},
	.probe		= renesas_sdhi_sys_dmac_probe,
	.remove		= renesas_sdhi_remove,
};

module_platform_driver(renesas_sys_dmac_sdhi_driver);

MODULE_DESCRIPTION("Renesas SDHI driver");
MODULE_AUTHOR("Magnus Damm");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sh_mobile_sdhi");
