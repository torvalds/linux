// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2013,2018 Intel Corporation

#include <linux/bitops.h>
#include <linux/dmaengine.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "internal.h"

static void idma32_initialize_chan(struct dw_dma_chan *dwc)
{
	u32 cfghi = 0;
	u32 cfglo = 0;

	/* Set default burst alignment */
	cfglo |= IDMA32C_CFGL_DST_BURST_ALIGN | IDMA32C_CFGL_SRC_BURST_ALIGN;

	/* Low 4 bits of the request lines */
	cfghi |= IDMA32C_CFGH_DST_PER(dwc->dws.dst_id & 0xf);
	cfghi |= IDMA32C_CFGH_SRC_PER(dwc->dws.src_id & 0xf);

	/* Request line extension (2 bits) */
	cfghi |= IDMA32C_CFGH_DST_PER_EXT(dwc->dws.dst_id >> 4 & 0x3);
	cfghi |= IDMA32C_CFGH_SRC_PER_EXT(dwc->dws.src_id >> 4 & 0x3);

	channel_writel(dwc, CFG_LO, cfglo);
	channel_writel(dwc, CFG_HI, cfghi);
}

static void idma32_suspend_chan(struct dw_dma_chan *dwc, bool drain)
{
	u32 cfglo = channel_readl(dwc, CFG_LO);

	if (drain)
		cfglo |= IDMA32C_CFGL_CH_DRAIN;

	channel_writel(dwc, CFG_LO, cfglo | DWC_CFGL_CH_SUSP);
}

static void idma32_resume_chan(struct dw_dma_chan *dwc, bool drain)
{
	u32 cfglo = channel_readl(dwc, CFG_LO);

	if (drain)
		cfglo &= ~IDMA32C_CFGL_CH_DRAIN;

	channel_writel(dwc, CFG_LO, cfglo & ~DWC_CFGL_CH_SUSP);
}

static u32 idma32_bytes2block(struct dw_dma_chan *dwc,
			      size_t bytes, unsigned int width, size_t *len)
{
	u32 block;

	if (bytes > dwc->block_size) {
		block = dwc->block_size;
		*len = dwc->block_size;
	} else {
		block = bytes;
		*len = bytes;
	}

	return block;
}

static size_t idma32_block2bytes(struct dw_dma_chan *dwc, u32 block, u32 width)
{
	return IDMA32C_CTLH_BLOCK_TS(block);
}

static u32 idma32_prepare_ctllo(struct dw_dma_chan *dwc)
{
	struct dma_slave_config	*sconfig = &dwc->dma_sconfig;
	bool is_slave = is_slave_direction(dwc->direction);
	u8 smsize = is_slave ? sconfig->src_maxburst : IDMA32_MSIZE_8;
	u8 dmsize = is_slave ? sconfig->dst_maxburst : IDMA32_MSIZE_8;

	return DWC_CTLL_LLP_D_EN | DWC_CTLL_LLP_S_EN |
	       DWC_CTLL_DST_MSIZE(dmsize) | DWC_CTLL_SRC_MSIZE(smsize);
}

static void idma32_encode_maxburst(struct dw_dma_chan *dwc, u32 *maxburst)
{
	*maxburst = *maxburst > 1 ? fls(*maxburst) - 1 : 0;
}

static void idma32_set_device_name(struct dw_dma *dw, int id)
{
	snprintf(dw->name, sizeof(dw->name), "idma32:dmac%d", id);
}

/*
 * Program FIFO size of channels.
 *
 * By default full FIFO (512 bytes) is assigned to channel 0. Here we
 * slice FIFO on equal parts between channels.
 */
static void idma32_fifo_partition(struct dw_dma *dw)
{
	u64 value = IDMA32C_FP_PSIZE_CH0(64) | IDMA32C_FP_PSIZE_CH1(64) |
		    IDMA32C_FP_UPDATE;
	u64 fifo_partition = 0;

	/* Fill FIFO_PARTITION low bits (Channels 0..1, 4..5) */
	fifo_partition |= value << 0;

	/* Fill FIFO_PARTITION high bits (Channels 2..3, 6..7) */
	fifo_partition |= value << 32;

	/* Program FIFO Partition registers - 64 bytes per channel */
	idma32_writeq(dw, FIFO_PARTITION1, fifo_partition);
	idma32_writeq(dw, FIFO_PARTITION0, fifo_partition);
}

static void idma32_disable(struct dw_dma *dw)
{
	do_dw_dma_off(dw);
	idma32_fifo_partition(dw);
}

static void idma32_enable(struct dw_dma *dw)
{
	idma32_fifo_partition(dw);
	do_dw_dma_on(dw);
}

int idma32_dma_probe(struct dw_dma_chip *chip)
{
	struct dw_dma *dw;

	dw = devm_kzalloc(chip->dev, sizeof(*dw), GFP_KERNEL);
	if (!dw)
		return -ENOMEM;

	/* Channel operations */
	dw->initialize_chan = idma32_initialize_chan;
	dw->suspend_chan = idma32_suspend_chan;
	dw->resume_chan = idma32_resume_chan;
	dw->prepare_ctllo = idma32_prepare_ctllo;
	dw->encode_maxburst = idma32_encode_maxburst;
	dw->bytes2block = idma32_bytes2block;
	dw->block2bytes = idma32_block2bytes;

	/* Device operations */
	dw->set_device_name = idma32_set_device_name;
	dw->disable = idma32_disable;
	dw->enable = idma32_enable;

	chip->dw = dw;
	return do_dma_probe(chip);
}
EXPORT_SYMBOL_GPL(idma32_dma_probe);

int idma32_dma_remove(struct dw_dma_chip *chip)
{
	return do_dma_remove(chip);
}
EXPORT_SYMBOL_GPL(idma32_dma_remove);
