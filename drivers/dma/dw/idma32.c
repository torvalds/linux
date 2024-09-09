// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2013,2018,2020-2021 Intel Corporation

#include <linux/bitops.h>
#include <linux/dmaengine.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "internal.h"

#define DMA_CTL_CH(x)			(0x1000 + (x) * 4)
#define DMA_SRC_ADDR_FILLIN(x)		(0x1100 + (x) * 4)
#define DMA_DST_ADDR_FILLIN(x)		(0x1200 + (x) * 4)
#define DMA_XBAR_SEL(x)			(0x1300 + (x) * 4)
#define DMA_REGACCESS_CHID_CFG		(0x1400)

#define CTL_CH_TRANSFER_MODE_MASK	GENMASK(1, 0)
#define CTL_CH_TRANSFER_MODE_S2S	0
#define CTL_CH_TRANSFER_MODE_S2D	1
#define CTL_CH_TRANSFER_MODE_D2S	2
#define CTL_CH_TRANSFER_MODE_D2D	3
#define CTL_CH_RD_RS_MASK		GENMASK(4, 3)
#define CTL_CH_WR_RS_MASK		GENMASK(6, 5)
#define CTL_CH_RD_NON_SNOOP_BIT		BIT(8)
#define CTL_CH_WR_NON_SNOOP_BIT		BIT(9)

#define XBAR_SEL_DEVID_MASK		GENMASK(15, 0)
#define XBAR_SEL_RX_TX_BIT		BIT(16)
#define XBAR_SEL_RX_TX_SHIFT		16

#define REGACCESS_CHID_MASK		GENMASK(2, 0)

static unsigned int idma32_get_slave_devfn(struct dw_dma_chan *dwc)
{
	struct device *slave = dwc->chan.slave;

	if (!slave || !dev_is_pci(slave))
		return 0;

	return to_pci_dev(slave)->devfn;
}

static void idma32_initialize_chan_xbar(struct dw_dma_chan *dwc)
{
	struct dw_dma *dw = to_dw_dma(dwc->chan.device);
	void __iomem *misc = __dw_regs(dw);
	u32 cfghi = 0, cfglo = 0;
	u8 dst_id, src_id;
	u32 value;

	/* DMA Channel ID Configuration register must be programmed first */
	value = readl(misc + DMA_REGACCESS_CHID_CFG);

	value &= ~REGACCESS_CHID_MASK;
	value |= dwc->chan.chan_id;

	writel(value, misc + DMA_REGACCESS_CHID_CFG);

	/* Configure channel attributes */
	value = readl(misc + DMA_CTL_CH(dwc->chan.chan_id));

	value &= ~(CTL_CH_RD_NON_SNOOP_BIT | CTL_CH_WR_NON_SNOOP_BIT);
	value &= ~(CTL_CH_RD_RS_MASK | CTL_CH_WR_RS_MASK);
	value &= ~CTL_CH_TRANSFER_MODE_MASK;

	switch (dwc->direction) {
	case DMA_MEM_TO_DEV:
		value |= CTL_CH_TRANSFER_MODE_D2S;
		value |= CTL_CH_WR_NON_SNOOP_BIT;
		break;
	case DMA_DEV_TO_MEM:
		value |= CTL_CH_TRANSFER_MODE_S2D;
		value |= CTL_CH_RD_NON_SNOOP_BIT;
		break;
	default:
		/*
		 * Memory-to-Memory and Device-to-Device are ignored for now.
		 *
		 * For Memory-to-Memory transfers we would need to set mode
		 * and disable snooping on both sides.
		 */
		return;
	}

	writel(value, misc + DMA_CTL_CH(dwc->chan.chan_id));

	/* Configure crossbar selection */
	value = readl(misc + DMA_XBAR_SEL(dwc->chan.chan_id));

	/* DEVFN selection */
	value &= ~XBAR_SEL_DEVID_MASK;
	value |= idma32_get_slave_devfn(dwc);

	switch (dwc->direction) {
	case DMA_MEM_TO_DEV:
		value |= XBAR_SEL_RX_TX_BIT;
		break;
	case DMA_DEV_TO_MEM:
		value &= ~XBAR_SEL_RX_TX_BIT;
		break;
	default:
		/* Memory-to-Memory and Device-to-Device are ignored for now */
		return;
	}

	writel(value, misc + DMA_XBAR_SEL(dwc->chan.chan_id));

	/* Configure DMA channel low and high registers */
	switch (dwc->direction) {
	case DMA_MEM_TO_DEV:
		dst_id = dwc->chan.chan_id;
		src_id = dwc->dws.src_id;
		break;
	case DMA_DEV_TO_MEM:
		dst_id = dwc->dws.dst_id;
		src_id = dwc->chan.chan_id;
		break;
	default:
		/* Memory-to-Memory and Device-to-Device are ignored for now */
		return;
	}

	/* Set default burst alignment */
	cfglo |= IDMA32C_CFGL_DST_BURST_ALIGN | IDMA32C_CFGL_SRC_BURST_ALIGN;

	/* Low 4 bits of the request lines */
	cfghi |= IDMA32C_CFGH_DST_PER(dst_id & 0xf);
	cfghi |= IDMA32C_CFGH_SRC_PER(src_id & 0xf);

	/* Request line extension (2 bits) */
	cfghi |= IDMA32C_CFGH_DST_PER_EXT(dst_id >> 4 & 0x3);
	cfghi |= IDMA32C_CFGH_SRC_PER_EXT(src_id >> 4 & 0x3);

	channel_writel(dwc, CFG_LO, cfglo);
	channel_writel(dwc, CFG_HI, cfghi);
}

static void idma32_initialize_chan_generic(struct dw_dma_chan *dwc)
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

static inline u8 idma32_encode_maxburst(u32 maxburst)
{
	return maxburst > 1 ? fls(maxburst) - 1 : 0;
}

static u32 idma32_prepare_ctllo(struct dw_dma_chan *dwc)
{
	struct dma_slave_config	*sconfig = &dwc->dma_sconfig;
	u8 smsize = 0, dmsize = 0;

	if (dwc->direction == DMA_MEM_TO_DEV)
		dmsize = idma32_encode_maxburst(sconfig->dst_maxburst);
	else if (dwc->direction == DMA_DEV_TO_MEM)
		smsize = idma32_encode_maxburst(sconfig->src_maxburst);

	return DWC_CTLL_LLP_D_EN | DWC_CTLL_LLP_S_EN |
	       DWC_CTLL_DST_MSIZE(dmsize) | DWC_CTLL_SRC_MSIZE(smsize);
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
	if (chip->pdata->quirks & DW_DMA_QUIRK_XBAR_PRESENT)
		dw->initialize_chan = idma32_initialize_chan_xbar;
	else
		dw->initialize_chan = idma32_initialize_chan_generic;
	dw->suspend_chan = idma32_suspend_chan;
	dw->resume_chan = idma32_resume_chan;
	dw->prepare_ctllo = idma32_prepare_ctllo;
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
