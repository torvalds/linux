// SPDX-License-Identifier: GPL-2.0
/*
 * DMA support for Internal DMAC with SDHI SD/SDIO controller
 *
 * Copyright (C) 2016-19 Renesas Electronics Corporation
 * Copyright (C) 2016-17 Horms Solutions, Simon Horman
 * Copyright (C) 2018-19 Sang Engineering, Wolfram Sang
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/io-64-nonatomic-hi-lo.h>
#include <linux/mmc/host.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pagemap.h>
#include <linux/platform_data/tmio.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/scatterlist.h>
#include <linux/sys_soc.h>

#include "renesas_sdhi.h"
#include "tmio_mmc.h"

#define DM_CM_DTRAN_MODE	0x820
#define DM_CM_DTRAN_CTRL	0x828
#define DM_CM_RST		0x830
#define DM_CM_INFO1		0x840
#define DM_CM_INFO1_MASK	0x848
#define DM_CM_INFO2		0x850
#define DM_CM_INFO2_MASK	0x858
#define DM_DTRAN_ADDR		0x880

/* DM_CM_DTRAN_MODE */
#define DTRAN_MODE_CH_NUM_CH0	0	/* "downstream" = for write commands */
#define DTRAN_MODE_CH_NUM_CH1	BIT(16)	/* "upstream" = for read commands */
#define DTRAN_MODE_BUS_WIDTH	(BIT(5) | BIT(4))
#define DTRAN_MODE_ADDR_MODE	BIT(0)	/* 1 = Increment address, 0 = Fixed */

/* DM_CM_DTRAN_CTRL */
#define DTRAN_CTRL_DM_START	BIT(0)

/* DM_CM_RST */
#define RST_DTRANRST1		BIT(9)
#define RST_DTRANRST0		BIT(8)
#define RST_RESERVED_BITS	GENMASK_ULL(31, 0)

/* DM_CM_INFO1 and DM_CM_INFO1_MASK */
#define INFO1_MASK_CLEAR	GENMASK_ULL(31, 0)
#define INFO1_DTRANEND1		BIT(20)
#define INFO1_DTRANEND1_OLD	BIT(17)
#define INFO1_DTRANEND0		BIT(16)

/* DM_CM_INFO2 and DM_CM_INFO2_MASK */
#define INFO2_MASK_CLEAR	GENMASK_ULL(31, 0)
#define INFO2_DTRANERR1		BIT(17)
#define INFO2_DTRANERR0		BIT(16)

enum renesas_sdhi_dma_cookie {
	COOKIE_UNMAPPED,
	COOKIE_PRE_MAPPED,
	COOKIE_MAPPED,
};

/*
 * Specification of this driver:
 * - host->chan_{rx,tx} will be used as a flag of enabling/disabling the dma
 * - Since this SDHI DMAC register set has 16 but 32-bit width, we
 *   need a custom accessor.
 */

static unsigned long global_flags;
/*
 * Workaround for avoiding to use RX DMAC by multiple channels. On R-Car M3-W
 * ES1.0, when multiple SDHI channels use RX DMAC simultaneously, sometimes
 * hundreds of data bytes are not stored into the system memory even if the
 * DMAC interrupt happened. So, this driver then uses one RX DMAC channel only.
 */
#define SDHI_INTERNAL_DMAC_RX_IN_USE	0

/* Definitions for sampling clocks */
static struct renesas_sdhi_scc rcar_gen3_scc_taps[] = {
	{
		.clk_rate = 0,
		.tap = 0x00000300,
		.tap_hs400_4tap = 0x00000100,
	},
};

static const struct renesas_sdhi_of_data of_data_rza2 = {
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT | TMIO_MMC_CLK_ACTUAL |
			  TMIO_MMC_HAVE_CBSY,
	.tmio_ocr_mask	= MMC_VDD_32_33,
	.capabilities	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ |
			  MMC_CAP_CMD23 | MMC_CAP_WAIT_WHILE_BUSY,
	.bus_shift	= 2,
	.scc_offset	= 0 - 0x1000,
	.taps		= rcar_gen3_scc_taps,
	.taps_num	= ARRAY_SIZE(rcar_gen3_scc_taps),
	/* DMAC can handle 32bit blk count but only 1 segment */
	.max_blk_count	= UINT_MAX / TMIO_MAX_BLK_SIZE,
	.max_segs	= 1,
};

static const struct renesas_sdhi_of_data of_data_rcar_gen3 = {
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT | TMIO_MMC_CLK_ACTUAL |
			  TMIO_MMC_HAVE_CBSY | TMIO_MMC_MIN_RCAR2,
	.capabilities	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ |
			  MMC_CAP_CMD23 | MMC_CAP_WAIT_WHILE_BUSY,
	.capabilities2	= MMC_CAP2_NO_WRITE_PROTECT | MMC_CAP2_MERGE_CAPABLE,
	.bus_shift	= 2,
	.scc_offset	= 0x1000,
	.taps		= rcar_gen3_scc_taps,
	.taps_num	= ARRAY_SIZE(rcar_gen3_scc_taps),
	/* DMAC can handle 32bit blk count but only 1 segment */
	.max_blk_count	= UINT_MAX / TMIO_MAX_BLK_SIZE,
	.max_segs	= 1,
	.sdhi_flags	= SDHI_FLAG_NEED_CLKH_FALLBACK,
};

static const struct renesas_sdhi_of_data of_data_rcar_gen3_no_sdh_fallback = {
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT | TMIO_MMC_CLK_ACTUAL |
			  TMIO_MMC_HAVE_CBSY | TMIO_MMC_MIN_RCAR2,
	.capabilities	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ |
			  MMC_CAP_CMD23 | MMC_CAP_WAIT_WHILE_BUSY,
	.capabilities2	= MMC_CAP2_NO_WRITE_PROTECT | MMC_CAP2_MERGE_CAPABLE,
	.bus_shift	= 2,
	.scc_offset	= 0x1000,
	.taps		= rcar_gen3_scc_taps,
	.taps_num	= ARRAY_SIZE(rcar_gen3_scc_taps),
	/* DMAC can handle 32bit blk count but only 1 segment */
	.max_blk_count	= UINT_MAX / TMIO_MAX_BLK_SIZE,
	.max_segs	= 1,
};

static const u8 r8a7796_es13_calib_table[2][SDHI_CALIB_TABLE_MAX] = {
	{ 3,  3,  3,  3,  3,  3,  3,  4,  4,  5,  6,  7,  8,  9, 10, 15,
	 16, 16, 16, 16, 16, 16, 17, 18, 18, 19, 20, 21, 22, 23, 24, 25 },
	{ 5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  6,  7,  8, 11,
	 12, 17, 18, 18, 18, 18, 18, 18, 18, 19, 20, 21, 22, 23, 25, 25 }
};

static const u8 r8a77965_calib_table[2][SDHI_CALIB_TABLE_MAX] = {
	{ 1,  2,  6,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 15, 15, 16,
	 17, 18, 19, 20, 21, 22, 23, 24, 25, 25, 26, 27, 28, 29, 30, 31 },
	{ 2,  3,  4,  4,  5,  6,  7,  9, 10, 11, 12, 13, 14, 15, 16, 17,
	 17, 17, 20, 21, 22, 23, 24, 25, 27, 28, 29, 30, 31, 31, 31, 31 }
};

static const u8 r8a77990_calib_table[2][SDHI_CALIB_TABLE_MAX] = {
	{ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
	{ 0,  0,  0,  1,  2,  3,  3,  4,  4,  4,  5,  5,  6,  8,  9, 10,
	 11, 12, 13, 15, 16, 17, 17, 18, 18, 19, 20, 22, 24, 25, 26, 26 }
};

static const struct renesas_sdhi_quirks sdhi_quirks_4tap_nohs400 = {
	.hs400_disabled = true,
	.hs400_4taps = true,
};

static const struct renesas_sdhi_quirks sdhi_quirks_4tap_nohs400_one_rx = {
	.hs400_disabled = true,
	.hs400_4taps = true,
	.dma_one_rx_only = true,
	.old_info1_layout = true,
};

static const struct renesas_sdhi_quirks sdhi_quirks_4tap = {
	.hs400_4taps = true,
	.hs400_bad_taps = BIT(2) | BIT(3) | BIT(6) | BIT(7),
	.manual_tap_correction = true,
};

static const struct renesas_sdhi_quirks sdhi_quirks_nohs400 = {
	.hs400_disabled = true,
};

static const struct renesas_sdhi_quirks sdhi_quirks_fixed_addr = {
	.fixed_addr_mode = true,
};

static const struct renesas_sdhi_quirks sdhi_quirks_bad_taps1357 = {
	.hs400_bad_taps = BIT(1) | BIT(3) | BIT(5) | BIT(7),
	.manual_tap_correction = true,
};

static const struct renesas_sdhi_quirks sdhi_quirks_bad_taps2367 = {
	.hs400_bad_taps = BIT(2) | BIT(3) | BIT(6) | BIT(7),
	.manual_tap_correction = true,
};

static const struct renesas_sdhi_quirks sdhi_quirks_r8a7796_es13 = {
	.hs400_4taps = true,
	.hs400_bad_taps = BIT(2) | BIT(3) | BIT(6) | BIT(7),
	.hs400_calib_table = r8a7796_es13_calib_table,
	.manual_tap_correction = true,
};

static const struct renesas_sdhi_quirks sdhi_quirks_r8a77965 = {
	.hs400_bad_taps = BIT(2) | BIT(3) | BIT(6) | BIT(7),
	.hs400_calib_table = r8a77965_calib_table,
	.manual_tap_correction = true,
};

static const struct renesas_sdhi_quirks sdhi_quirks_r8a77990 = {
	.hs400_calib_table = r8a77990_calib_table,
	.manual_tap_correction = true,
};

static const struct renesas_sdhi_quirks sdhi_quirks_rzg2l = {
	.fixed_addr_mode = true,
	.hs400_disabled = true,
};

/*
 * Note for r8a7796 / r8a774a1: we can't distinguish ES1.1 and 1.2 as of now.
 * So, we want to treat them equally and only have a match for ES1.2 to enforce
 * this if there ever will be a way to distinguish ES1.2.
 */
static const struct soc_device_attribute sdhi_quirks_match[]  = {
	{ .soc_id = "r8a774a1", .revision = "ES1.[012]", .data = &sdhi_quirks_4tap_nohs400 },
	{ .soc_id = "r8a7795", .revision = "ES2.0", .data = &sdhi_quirks_4tap },
	{ .soc_id = "r8a7796", .revision = "ES1.0", .data = &sdhi_quirks_4tap_nohs400_one_rx },
	{ .soc_id = "r8a7796", .revision = "ES1.[12]", .data = &sdhi_quirks_4tap_nohs400 },
	{ .soc_id = "r8a7796", .revision = "ES1.*", .data = &sdhi_quirks_r8a7796_es13 },
	{ .soc_id = "r8a77980", .revision = "ES1.*", .data = &sdhi_quirks_nohs400 },
	{ /* Sentinel. */ }
};

static const struct renesas_sdhi_of_data_with_quirks of_r8a7795_compatible = {
	.of_data = &of_data_rcar_gen3,
	.quirks = &sdhi_quirks_bad_taps2367,
};

static const struct renesas_sdhi_of_data_with_quirks of_r8a77961_compatible = {
	.of_data = &of_data_rcar_gen3,
	.quirks = &sdhi_quirks_bad_taps1357,
};

static const struct renesas_sdhi_of_data_with_quirks of_r8a77965_compatible = {
	.of_data = &of_data_rcar_gen3,
	.quirks = &sdhi_quirks_r8a77965,
};

static const struct renesas_sdhi_of_data_with_quirks of_r8a77970_compatible = {
	.of_data = &of_data_rcar_gen3_no_sdh_fallback,
	.quirks = &sdhi_quirks_nohs400,
};

static const struct renesas_sdhi_of_data_with_quirks of_r8a77990_compatible = {
	.of_data = &of_data_rcar_gen3,
	.quirks = &sdhi_quirks_r8a77990,
};

static const struct renesas_sdhi_of_data_with_quirks of_rzg2l_compatible = {
	.of_data = &of_data_rcar_gen3,
	.quirks = &sdhi_quirks_rzg2l,
};

static const struct renesas_sdhi_of_data_with_quirks of_rcar_gen3_compatible = {
	.of_data = &of_data_rcar_gen3,
};

static const struct renesas_sdhi_of_data_with_quirks of_rcar_gen3_nohs400_compatible = {
	.of_data = &of_data_rcar_gen3,
	.quirks = &sdhi_quirks_nohs400,
};

static const struct renesas_sdhi_of_data_with_quirks of_rza2_compatible = {
	.of_data	= &of_data_rza2,
	.quirks		= &sdhi_quirks_fixed_addr,
};

static const struct of_device_id renesas_sdhi_internal_dmac_of_match[] = {
	{ .compatible = "renesas,sdhi-r7s9210", .data = &of_rza2_compatible, },
	{ .compatible = "renesas,sdhi-mmc-r8a77470", .data = &of_rcar_gen3_compatible, },
	{ .compatible = "renesas,sdhi-r8a7795", .data = &of_r8a7795_compatible, },
	{ .compatible = "renesas,sdhi-r8a77961", .data = &of_r8a77961_compatible, },
	{ .compatible = "renesas,sdhi-r8a77965", .data = &of_r8a77965_compatible, },
	{ .compatible = "renesas,sdhi-r8a77970", .data = &of_r8a77970_compatible, },
	{ .compatible = "renesas,sdhi-r8a77990", .data = &of_r8a77990_compatible, },
	{ .compatible = "renesas,sdhi-r8a77995", .data = &of_rcar_gen3_nohs400_compatible, },
	{ .compatible = "renesas,sdhi-r9a09g011", .data = &of_rzg2l_compatible, },
	{ .compatible = "renesas,sdhi-r9a09g057", .data = &of_rzg2l_compatible, },
	{ .compatible = "renesas,rzg2l-sdhi", .data = &of_rzg2l_compatible, },
	{ .compatible = "renesas,rcar-gen3-sdhi", .data = &of_rcar_gen3_compatible, },
	{ .compatible = "renesas,rcar-gen4-sdhi", .data = &of_rcar_gen3_compatible, },
	{},
};
MODULE_DEVICE_TABLE(of, renesas_sdhi_internal_dmac_of_match);

static void
renesas_sdhi_internal_dmac_enable_dma(struct tmio_mmc_host *host, bool enable)
{
	struct renesas_sdhi *priv = host_to_priv(host);
	u32 dma_irqs = INFO1_DTRANEND0 |
			(sdhi_has_quirk(priv, old_info1_layout) ?
			INFO1_DTRANEND1_OLD : INFO1_DTRANEND1);

	if (!host->chan_tx || !host->chan_rx)
		return;

	writel(enable ? ~dma_irqs : INFO1_MASK_CLEAR, host->ctl + DM_CM_INFO1_MASK);

	if (priv->dma_priv.enable)
		priv->dma_priv.enable(host, enable);
}

static void
renesas_sdhi_internal_dmac_abort_dma(struct tmio_mmc_host *host)
{
	u64 val = RST_DTRANRST1 | RST_DTRANRST0;

	renesas_sdhi_internal_dmac_enable_dma(host, false);

	writel(RST_RESERVED_BITS & ~val, host->ctl + DM_CM_RST);
	writel(RST_RESERVED_BITS | val, host->ctl + DM_CM_RST);

	clear_bit(SDHI_INTERNAL_DMAC_RX_IN_USE, &global_flags);

	renesas_sdhi_internal_dmac_enable_dma(host, true);
}

static bool renesas_sdhi_internal_dmac_dma_irq(struct tmio_mmc_host *host)
{
	struct renesas_sdhi *priv = host_to_priv(host);
	struct renesas_sdhi_dma *dma_priv = &priv->dma_priv;

	u32 dma_irqs = INFO1_DTRANEND0 |
			(sdhi_has_quirk(priv, old_info1_layout) ?
			INFO1_DTRANEND1_OLD : INFO1_DTRANEND1);
	u32 status = readl(host->ctl + DM_CM_INFO1);

	if (status & dma_irqs) {
		writel(status ^ dma_irqs, host->ctl + DM_CM_INFO1);
		set_bit(SDHI_DMA_END_FLAG_DMA, &dma_priv->end_flags);
		if (test_bit(SDHI_DMA_END_FLAG_ACCESS, &dma_priv->end_flags))
			queue_work(system_bh_wq, &dma_priv->dma_complete);
	}

	return status & dma_irqs;
}

static void
renesas_sdhi_internal_dmac_dataend_dma(struct tmio_mmc_host *host)
{
	struct renesas_sdhi *priv = host_to_priv(host);
	struct renesas_sdhi_dma *dma_priv = &priv->dma_priv;

	set_bit(SDHI_DMA_END_FLAG_ACCESS, &dma_priv->end_flags);
	if (test_bit(SDHI_DMA_END_FLAG_DMA, &dma_priv->end_flags) ||
	    host->data->error)
		queue_work(system_bh_wq, &dma_priv->dma_complete);
}

/*
 * renesas_sdhi_internal_dmac_map() will be called with two different
 * sg pointers in two mmc_data by .pre_req(), but tmio host can have a single
 * sg_ptr only. So, renesas_sdhi_internal_dmac_{un}map() should use a sg
 * pointer in a mmc_data instead of host->sg_ptr.
 */
static void
renesas_sdhi_internal_dmac_unmap(struct tmio_mmc_host *host,
				 struct mmc_data *data,
				 enum renesas_sdhi_dma_cookie cookie)
{
	bool unmap = cookie == COOKIE_UNMAPPED ? (data->host_cookie != cookie) :
						 (data->host_cookie == cookie);

	if (unmap) {
		dma_unmap_sg(&host->pdev->dev, data->sg, data->sg_len,
			     mmc_get_dma_dir(data));
		data->host_cookie = COOKIE_UNMAPPED;
	}
}

static bool
renesas_sdhi_internal_dmac_map(struct tmio_mmc_host *host,
			       struct mmc_data *data,
			       enum renesas_sdhi_dma_cookie cookie)
{
	if (data->host_cookie == COOKIE_PRE_MAPPED)
		return true;

	if (!dma_map_sg(&host->pdev->dev, data->sg, data->sg_len,
			    mmc_get_dma_dir(data)))
		return false;

	data->host_cookie = cookie;

	/* This DMAC needs buffers to be 128-byte aligned */
	if (!IS_ALIGNED(sg_dma_address(data->sg), 128)) {
		renesas_sdhi_internal_dmac_unmap(host, data, cookie);
		return false;
	}

	return true;
}

static void
renesas_sdhi_internal_dmac_start_dma(struct tmio_mmc_host *host,
				     struct mmc_data *data)
{
	struct renesas_sdhi *priv = host_to_priv(host);
	struct scatterlist *sg = host->sg_ptr;
	u32 dtran_mode = DTRAN_MODE_BUS_WIDTH;

	if (!sdhi_has_quirk(priv, fixed_addr_mode))
		dtran_mode |= DTRAN_MODE_ADDR_MODE;

	if (!renesas_sdhi_internal_dmac_map(host, data, COOKIE_MAPPED))
		goto force_pio;

	if (data->flags & MMC_DATA_READ) {
		dtran_mode |= DTRAN_MODE_CH_NUM_CH1;
		if (sdhi_has_quirk(priv, dma_one_rx_only) &&
		    test_and_set_bit(SDHI_INTERNAL_DMAC_RX_IN_USE, &global_flags))
			goto force_pio_with_unmap;
	} else {
		dtran_mode |= DTRAN_MODE_CH_NUM_CH0;
	}

	priv->dma_priv.end_flags = 0;
	renesas_sdhi_internal_dmac_enable_dma(host, true);

	/* set dma parameters */
	writel(dtran_mode, host->ctl + DM_CM_DTRAN_MODE);
	writel(sg_dma_address(sg), host->ctl + DM_DTRAN_ADDR);

	host->dma_on = true;

	return;

force_pio_with_unmap:
	renesas_sdhi_internal_dmac_unmap(host, data, COOKIE_UNMAPPED);

force_pio:
	renesas_sdhi_internal_dmac_enable_dma(host, false);
}

static void renesas_sdhi_internal_dmac_issue_work_fn(struct work_struct *work)
{
	struct tmio_mmc_host *host = from_work(host, work, dma_issue);
	struct renesas_sdhi *priv = host_to_priv(host);

	tmio_mmc_enable_mmc_irqs(host, TMIO_STAT_DATAEND);

	if (!host->cmd->error) {
		/* start the DMAC */
		writel(DTRAN_CTRL_DM_START, host->ctl + DM_CM_DTRAN_CTRL);
	} else {
		/* on CMD errors, simulate DMA end immediately */
		set_bit(SDHI_DMA_END_FLAG_DMA, &priv->dma_priv.end_flags);
		if (test_bit(SDHI_DMA_END_FLAG_ACCESS, &priv->dma_priv.end_flags))
			queue_work(system_bh_wq, &priv->dma_priv.dma_complete);
	}
}

static bool renesas_sdhi_internal_dmac_complete(struct tmio_mmc_host *host)
{
	enum dma_data_direction dir;

	if (!host->dma_on)
		return false;

	if (!host->data)
		return false;

	if (host->data->flags & MMC_DATA_READ)
		dir = DMA_FROM_DEVICE;
	else
		dir = DMA_TO_DEVICE;

	renesas_sdhi_internal_dmac_enable_dma(host, false);
	renesas_sdhi_internal_dmac_unmap(host, host->data, COOKIE_MAPPED);

	if (dir == DMA_FROM_DEVICE)
		clear_bit(SDHI_INTERNAL_DMAC_RX_IN_USE, &global_flags);

	host->dma_on = false;

	return true;
}

static void renesas_sdhi_internal_dmac_complete_work_fn(struct work_struct *work)
{
	struct renesas_sdhi_dma *dma_priv = from_work(dma_priv, work, dma_complete);
	struct renesas_sdhi *priv = container_of(dma_priv, typeof(*priv), dma_priv);
	struct tmio_mmc_host *host = priv->host;

	spin_lock_irq(&host->lock);
	if (!renesas_sdhi_internal_dmac_complete(host))
		goto out;

	tmio_mmc_do_data_irq(host);
out:
	spin_unlock_irq(&host->lock);
}

static void renesas_sdhi_internal_dmac_end_dma(struct tmio_mmc_host *host)
{
	if (host->data)
		renesas_sdhi_internal_dmac_complete(host);
}

static void renesas_sdhi_internal_dmac_post_req(struct mmc_host *mmc,
						struct mmc_request *mrq,
						int err)
{
	struct tmio_mmc_host *host = mmc_priv(mmc);
	struct mmc_data *data = mrq->data;

	if (!data)
		return;

	renesas_sdhi_internal_dmac_unmap(host, data, COOKIE_UNMAPPED);
}

static void renesas_sdhi_internal_dmac_pre_req(struct mmc_host *mmc,
					       struct mmc_request *mrq)
{
	struct tmio_mmc_host *host = mmc_priv(mmc);
	struct mmc_data *data = mrq->data;

	if (!data)
		return;

	data->host_cookie = COOKIE_UNMAPPED;
	renesas_sdhi_internal_dmac_map(host, data, COOKIE_PRE_MAPPED);
}

static void
renesas_sdhi_internal_dmac_request_dma(struct tmio_mmc_host *host,
				       struct tmio_mmc_data *pdata)
{
	struct renesas_sdhi *priv = host_to_priv(host);

	/* Disable DMAC interrupts initially */
	writel(INFO1_MASK_CLEAR, host->ctl + DM_CM_INFO1_MASK);
	writel(INFO2_MASK_CLEAR, host->ctl + DM_CM_INFO2_MASK);
	writel(0, host->ctl + DM_CM_INFO1);
	writel(0, host->ctl + DM_CM_INFO2);

	/* Each value is set to non-zero to assume "enabling" each DMA */
	host->chan_rx = host->chan_tx = (void *)0xdeadbeaf;

	INIT_WORK(&priv->dma_priv.dma_complete,
		  renesas_sdhi_internal_dmac_complete_work_fn);
	INIT_WORK(&host->dma_issue,
		  renesas_sdhi_internal_dmac_issue_work_fn);

	/* Add pre_req and post_req */
	host->ops.pre_req = renesas_sdhi_internal_dmac_pre_req;
	host->ops.post_req = renesas_sdhi_internal_dmac_post_req;
}

static void
renesas_sdhi_internal_dmac_release_dma(struct tmio_mmc_host *host)
{
	/* Each value is set to zero to assume "disabling" each DMA */
	host->chan_rx = host->chan_tx = NULL;
}

static const struct tmio_mmc_dma_ops renesas_sdhi_internal_dmac_dma_ops = {
	.start = renesas_sdhi_internal_dmac_start_dma,
	.enable = renesas_sdhi_internal_dmac_enable_dma,
	.request = renesas_sdhi_internal_dmac_request_dma,
	.release = renesas_sdhi_internal_dmac_release_dma,
	.abort = renesas_sdhi_internal_dmac_abort_dma,
	.dataend = renesas_sdhi_internal_dmac_dataend_dma,
	.end = renesas_sdhi_internal_dmac_end_dma,
	.dma_irq = renesas_sdhi_internal_dmac_dma_irq,
};

static int renesas_sdhi_internal_dmac_probe(struct platform_device *pdev)
{
	const struct soc_device_attribute *attr;
	const struct renesas_sdhi_of_data_with_quirks *of_data_quirks;
	const struct renesas_sdhi_quirks *quirks;
	struct device *dev = &pdev->dev;

	of_data_quirks = of_device_get_match_data(&pdev->dev);
	quirks = of_data_quirks->quirks;

	attr = soc_device_match(sdhi_quirks_match);
	if (attr)
		quirks = attr->data;

	/* value is max of SD_SECCNT. Confirmed by HW engineers */
	dma_set_max_seg_size(dev, 0xffffffff);

	return renesas_sdhi_probe(pdev, &renesas_sdhi_internal_dmac_dma_ops,
				  of_data_quirks->of_data, quirks);
}

static const struct dev_pm_ops renesas_sdhi_internal_dmac_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(tmio_mmc_host_runtime_suspend,
			   tmio_mmc_host_runtime_resume,
			   NULL)
};

static struct platform_driver renesas_internal_dmac_sdhi_driver = {
	.driver		= {
		.name	= "renesas_sdhi_internal_dmac",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.pm	= &renesas_sdhi_internal_dmac_dev_pm_ops,
		.of_match_table = renesas_sdhi_internal_dmac_of_match,
	},
	.probe		= renesas_sdhi_internal_dmac_probe,
	.remove		= renesas_sdhi_remove,
};

module_platform_driver(renesas_internal_dmac_sdhi_driver);

MODULE_DESCRIPTION("Renesas SDHI driver for internal DMAC");
MODULE_AUTHOR("Yoshihiro Shimoda");
MODULE_LICENSE("GPL v2");
