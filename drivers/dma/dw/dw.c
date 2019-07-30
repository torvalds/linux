// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2007-2008 Atmel Corporation
// Copyright (C) 2010-2011 ST Microelectronics
// Copyright (C) 2013,2018 Intel Corporation

#include <linux/bitops.h>
#include <linux/dmaengine.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "internal.h"

static void dw_dma_initialize_chan(struct dw_dma_chan *dwc)
{
	struct dw_dma *dw = to_dw_dma(dwc->chan.device);
	u32 cfghi = DWC_CFGH_FIFO_MODE;
	u32 cfglo = DWC_CFGL_CH_PRIOR(dwc->priority);
	bool hs_polarity = dwc->dws.hs_polarity;

	cfghi |= DWC_CFGH_DST_PER(dwc->dws.dst_id);
	cfghi |= DWC_CFGH_SRC_PER(dwc->dws.src_id);
	cfghi |= DWC_CFGH_PROTCTL(dw->pdata->protctl);

	/* Set polarity of handshake interface */
	cfglo |= hs_polarity ? DWC_CFGL_HS_DST_POL | DWC_CFGL_HS_SRC_POL : 0;

	channel_writel(dwc, CFG_LO, cfglo);
	channel_writel(dwc, CFG_HI, cfghi);
}

static void dw_dma_suspend_chan(struct dw_dma_chan *dwc, bool drain)
{
	u32 cfglo = channel_readl(dwc, CFG_LO);

	channel_writel(dwc, CFG_LO, cfglo | DWC_CFGL_CH_SUSP);
}

static void dw_dma_resume_chan(struct dw_dma_chan *dwc, bool drain)
{
	u32 cfglo = channel_readl(dwc, CFG_LO);

	channel_writel(dwc, CFG_LO, cfglo & ~DWC_CFGL_CH_SUSP);
}

static u32 dw_dma_bytes2block(struct dw_dma_chan *dwc,
			      size_t bytes, unsigned int width, size_t *len)
{
	u32 block;

	if ((bytes >> width) > dwc->block_size) {
		block = dwc->block_size;
		*len = dwc->block_size << width;
	} else {
		block = bytes >> width;
		*len = bytes;
	}

	return block;
}

static size_t dw_dma_block2bytes(struct dw_dma_chan *dwc, u32 block, u32 width)
{
	return DWC_CTLH_BLOCK_TS(block) << width;
}

static u32 dw_dma_prepare_ctllo(struct dw_dma_chan *dwc)
{
	struct dma_slave_config	*sconfig = &dwc->dma_sconfig;
	bool is_slave = is_slave_direction(dwc->direction);
	u8 smsize = is_slave ? sconfig->src_maxburst : DW_DMA_MSIZE_16;
	u8 dmsize = is_slave ? sconfig->dst_maxburst : DW_DMA_MSIZE_16;
	u8 p_master = dwc->dws.p_master;
	u8 m_master = dwc->dws.m_master;
	u8 dms = (dwc->direction == DMA_MEM_TO_DEV) ? p_master : m_master;
	u8 sms = (dwc->direction == DMA_DEV_TO_MEM) ? p_master : m_master;

	return DWC_CTLL_LLP_D_EN | DWC_CTLL_LLP_S_EN |
	       DWC_CTLL_DST_MSIZE(dmsize) | DWC_CTLL_SRC_MSIZE(smsize) |
	       DWC_CTLL_DMS(dms) | DWC_CTLL_SMS(sms);
}

static void dw_dma_encode_maxburst(struct dw_dma_chan *dwc, u32 *maxburst)
{
	/*
	 * Fix burst size according to dw_dmac. We need to convert them as:
	 * 1 -> 0, 4 -> 1, 8 -> 2, 16 -> 3.
	 */
	*maxburst = *maxburst > 1 ? fls(*maxburst) - 2 : 0;
}

static void dw_dma_set_device_name(struct dw_dma *dw, int id)
{
	snprintf(dw->name, sizeof(dw->name), "dw:dmac%d", id);
}

static void dw_dma_disable(struct dw_dma *dw)
{
	do_dw_dma_off(dw);
}

static void dw_dma_enable(struct dw_dma *dw)
{
	do_dw_dma_on(dw);
}

int dw_dma_probe(struct dw_dma_chip *chip)
{
	struct dw_dma *dw;

	dw = devm_kzalloc(chip->dev, sizeof(*dw), GFP_KERNEL);
	if (!dw)
		return -ENOMEM;

	/* Channel operations */
	dw->initialize_chan = dw_dma_initialize_chan;
	dw->suspend_chan = dw_dma_suspend_chan;
	dw->resume_chan = dw_dma_resume_chan;
	dw->prepare_ctllo = dw_dma_prepare_ctllo;
	dw->encode_maxburst = dw_dma_encode_maxburst;
	dw->bytes2block = dw_dma_bytes2block;
	dw->block2bytes = dw_dma_block2bytes;

	/* Device operations */
	dw->set_device_name = dw_dma_set_device_name;
	dw->disable = dw_dma_disable;
	dw->enable = dw_dma_enable;

	chip->dw = dw;
	return do_dma_probe(chip);
}
EXPORT_SYMBOL_GPL(dw_dma_probe);

int dw_dma_remove(struct dw_dma_chip *chip)
{
	return do_dma_remove(chip);
}
EXPORT_SYMBOL_GPL(dw_dma_remove);
