/*
 *
 * Copyright (C) STMicroelectronics SA 2017
 * Author(s): M'boumba Cedric Madianga <cedric.madianga@gmail.com>
 *            Pierre-Yves Mordret <pierre-yves.mordret@st.com>
 *
 * License terms: GPL V2.0.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * Driver for STM32 MDMA controller
 *
 * Inspired by stm32-dma.c and dma-jz4780.c
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include "virt-dma.h"

/*  MDMA Generic getter/setter */
#define STM32_MDMA_SHIFT(n)		(ffs(n) - 1)
#define STM32_MDMA_SET(n, mask)		(((n) << STM32_MDMA_SHIFT(mask)) & \
					 (mask))
#define STM32_MDMA_GET(n, mask)		(((n) & (mask)) >> \
					 STM32_MDMA_SHIFT(mask))

#define STM32_MDMA_GISR0		0x0000 /* MDMA Int Status Reg 1 */
#define STM32_MDMA_GISR1		0x0004 /* MDMA Int Status Reg 2 */

/* MDMA Channel x interrupt/status register */
#define STM32_MDMA_CISR(x)		(0x40 + 0x40 * (x)) /* x = 0..62 */
#define STM32_MDMA_CISR_CRQA		BIT(16)
#define STM32_MDMA_CISR_TCIF		BIT(4)
#define STM32_MDMA_CISR_BTIF		BIT(3)
#define STM32_MDMA_CISR_BRTIF		BIT(2)
#define STM32_MDMA_CISR_CTCIF		BIT(1)
#define STM32_MDMA_CISR_TEIF		BIT(0)

/* MDMA Channel x interrupt flag clear register */
#define STM32_MDMA_CIFCR(x)		(0x44 + 0x40 * (x))
#define STM32_MDMA_CIFCR_CLTCIF		BIT(4)
#define STM32_MDMA_CIFCR_CBTIF		BIT(3)
#define STM32_MDMA_CIFCR_CBRTIF		BIT(2)
#define STM32_MDMA_CIFCR_CCTCIF		BIT(1)
#define STM32_MDMA_CIFCR_CTEIF		BIT(0)
#define STM32_MDMA_CIFCR_CLEAR_ALL	(STM32_MDMA_CIFCR_CLTCIF \
					| STM32_MDMA_CIFCR_CBTIF \
					| STM32_MDMA_CIFCR_CBRTIF \
					| STM32_MDMA_CIFCR_CCTCIF \
					| STM32_MDMA_CIFCR_CTEIF)

/* MDMA Channel x error status register */
#define STM32_MDMA_CESR(x)		(0x48 + 0x40 * (x))
#define STM32_MDMA_CESR_BSE		BIT(11)
#define STM32_MDMA_CESR_ASR		BIT(10)
#define STM32_MDMA_CESR_TEMD		BIT(9)
#define STM32_MDMA_CESR_TELD		BIT(8)
#define STM32_MDMA_CESR_TED		BIT(7)
#define STM32_MDMA_CESR_TEA_MASK	GENMASK(6, 0)

/* MDMA Channel x control register */
#define STM32_MDMA_CCR(x)		(0x4C + 0x40 * (x))
#define STM32_MDMA_CCR_SWRQ		BIT(16)
#define STM32_MDMA_CCR_WEX		BIT(14)
#define STM32_MDMA_CCR_HEX		BIT(13)
#define STM32_MDMA_CCR_BEX		BIT(12)
#define STM32_MDMA_CCR_PL_MASK		GENMASK(7, 6)
#define STM32_MDMA_CCR_PL(n)		STM32_MDMA_SET(n, \
						       STM32_MDMA_CCR_PL_MASK)
#define STM32_MDMA_CCR_TCIE		BIT(5)
#define STM32_MDMA_CCR_BTIE		BIT(4)
#define STM32_MDMA_CCR_BRTIE		BIT(3)
#define STM32_MDMA_CCR_CTCIE		BIT(2)
#define STM32_MDMA_CCR_TEIE		BIT(1)
#define STM32_MDMA_CCR_EN		BIT(0)
#define STM32_MDMA_CCR_IRQ_MASK		(STM32_MDMA_CCR_TCIE \
					| STM32_MDMA_CCR_BTIE \
					| STM32_MDMA_CCR_BRTIE \
					| STM32_MDMA_CCR_CTCIE \
					| STM32_MDMA_CCR_TEIE)

/* MDMA Channel x transfer configuration register */
#define STM32_MDMA_CTCR(x)		(0x50 + 0x40 * (x))
#define STM32_MDMA_CTCR_BWM		BIT(31)
#define STM32_MDMA_CTCR_SWRM		BIT(30)
#define STM32_MDMA_CTCR_TRGM_MSK	GENMASK(29, 28)
#define STM32_MDMA_CTCR_TRGM(n)		STM32_MDMA_SET((n), \
						       STM32_MDMA_CTCR_TRGM_MSK)
#define STM32_MDMA_CTCR_TRGM_GET(n)	STM32_MDMA_GET((n), \
						       STM32_MDMA_CTCR_TRGM_MSK)
#define STM32_MDMA_CTCR_PAM_MASK	GENMASK(27, 26)
#define STM32_MDMA_CTCR_PAM(n)		STM32_MDMA_SET(n, \
						       STM32_MDMA_CTCR_PAM_MASK)
#define STM32_MDMA_CTCR_PKE		BIT(25)
#define STM32_MDMA_CTCR_TLEN_MSK	GENMASK(24, 18)
#define STM32_MDMA_CTCR_TLEN(n)		STM32_MDMA_SET((n), \
						       STM32_MDMA_CTCR_TLEN_MSK)
#define STM32_MDMA_CTCR_TLEN_GET(n)	STM32_MDMA_GET((n), \
						       STM32_MDMA_CTCR_TLEN_MSK)
#define STM32_MDMA_CTCR_LEN2_MSK	GENMASK(25, 18)
#define STM32_MDMA_CTCR_LEN2(n)		STM32_MDMA_SET((n), \
						       STM32_MDMA_CTCR_LEN2_MSK)
#define STM32_MDMA_CTCR_LEN2_GET(n)	STM32_MDMA_GET((n), \
						       STM32_MDMA_CTCR_LEN2_MSK)
#define STM32_MDMA_CTCR_DBURST_MASK	GENMASK(17, 15)
#define STM32_MDMA_CTCR_DBURST(n)	STM32_MDMA_SET(n, \
						    STM32_MDMA_CTCR_DBURST_MASK)
#define STM32_MDMA_CTCR_SBURST_MASK	GENMASK(14, 12)
#define STM32_MDMA_CTCR_SBURST(n)	STM32_MDMA_SET(n, \
						    STM32_MDMA_CTCR_SBURST_MASK)
#define STM32_MDMA_CTCR_DINCOS_MASK	GENMASK(11, 10)
#define STM32_MDMA_CTCR_DINCOS(n)	STM32_MDMA_SET((n), \
						    STM32_MDMA_CTCR_DINCOS_MASK)
#define STM32_MDMA_CTCR_SINCOS_MASK	GENMASK(9, 8)
#define STM32_MDMA_CTCR_SINCOS(n)	STM32_MDMA_SET((n), \
						    STM32_MDMA_CTCR_SINCOS_MASK)
#define STM32_MDMA_CTCR_DSIZE_MASK	GENMASK(7, 6)
#define STM32_MDMA_CTCR_DSIZE(n)	STM32_MDMA_SET(n, \
						     STM32_MDMA_CTCR_DSIZE_MASK)
#define STM32_MDMA_CTCR_SSIZE_MASK	GENMASK(5, 4)
#define STM32_MDMA_CTCR_SSIZE(n)	STM32_MDMA_SET(n, \
						     STM32_MDMA_CTCR_SSIZE_MASK)
#define STM32_MDMA_CTCR_DINC_MASK	GENMASK(3, 2)
#define STM32_MDMA_CTCR_DINC(n)		STM32_MDMA_SET((n), \
						      STM32_MDMA_CTCR_DINC_MASK)
#define STM32_MDMA_CTCR_SINC_MASK	GENMASK(1, 0)
#define STM32_MDMA_CTCR_SINC(n)		STM32_MDMA_SET((n), \
						      STM32_MDMA_CTCR_SINC_MASK)
#define STM32_MDMA_CTCR_CFG_MASK	(STM32_MDMA_CTCR_SINC_MASK \
					| STM32_MDMA_CTCR_DINC_MASK \
					| STM32_MDMA_CTCR_SINCOS_MASK \
					| STM32_MDMA_CTCR_DINCOS_MASK \
					| STM32_MDMA_CTCR_LEN2_MSK \
					| STM32_MDMA_CTCR_TRGM_MSK)

/* MDMA Channel x block number of data register */
#define STM32_MDMA_CBNDTR(x)		(0x54 + 0x40 * (x))
#define STM32_MDMA_CBNDTR_BRC_MK	GENMASK(31, 20)
#define STM32_MDMA_CBNDTR_BRC(n)	STM32_MDMA_SET(n, \
						       STM32_MDMA_CBNDTR_BRC_MK)
#define STM32_MDMA_CBNDTR_BRC_GET(n)	STM32_MDMA_GET((n), \
						       STM32_MDMA_CBNDTR_BRC_MK)

#define STM32_MDMA_CBNDTR_BRDUM		BIT(19)
#define STM32_MDMA_CBNDTR_BRSUM		BIT(18)
#define STM32_MDMA_CBNDTR_BNDT_MASK	GENMASK(16, 0)
#define STM32_MDMA_CBNDTR_BNDT(n)	STM32_MDMA_SET(n, \
						    STM32_MDMA_CBNDTR_BNDT_MASK)

/* MDMA Channel x source address register */
#define STM32_MDMA_CSAR(x)		(0x58 + 0x40 * (x))

/* MDMA Channel x destination address register */
#define STM32_MDMA_CDAR(x)		(0x5C + 0x40 * (x))

/* MDMA Channel x block repeat address update register */
#define STM32_MDMA_CBRUR(x)		(0x60 + 0x40 * (x))
#define STM32_MDMA_CBRUR_DUV_MASK	GENMASK(31, 16)
#define STM32_MDMA_CBRUR_DUV(n)		STM32_MDMA_SET(n, \
						      STM32_MDMA_CBRUR_DUV_MASK)
#define STM32_MDMA_CBRUR_SUV_MASK	GENMASK(15, 0)
#define STM32_MDMA_CBRUR_SUV(n)		STM32_MDMA_SET(n, \
						      STM32_MDMA_CBRUR_SUV_MASK)

/* MDMA Channel x link address register */
#define STM32_MDMA_CLAR(x)		(0x64 + 0x40 * (x))

/* MDMA Channel x trigger and bus selection register */
#define STM32_MDMA_CTBR(x)		(0x68 + 0x40 * (x))
#define STM32_MDMA_CTBR_DBUS		BIT(17)
#define STM32_MDMA_CTBR_SBUS		BIT(16)
#define STM32_MDMA_CTBR_TSEL_MASK	GENMASK(7, 0)
#define STM32_MDMA_CTBR_TSEL(n)		STM32_MDMA_SET(n, \
						      STM32_MDMA_CTBR_TSEL_MASK)

/* MDMA Channel x mask address register */
#define STM32_MDMA_CMAR(x)		(0x70 + 0x40 * (x))

/* MDMA Channel x mask data register */
#define STM32_MDMA_CMDR(x)		(0x74 + 0x40 * (x))

#define STM32_MDMA_MAX_BUF_LEN		128
#define STM32_MDMA_MAX_BLOCK_LEN	65536
#define STM32_MDMA_MAX_CHANNELS		63
#define STM32_MDMA_MAX_REQUESTS		256
#define STM32_MDMA_MAX_BURST		128
#define STM32_MDMA_VERY_HIGH_PRIORITY	0x11

enum stm32_mdma_trigger_mode {
	STM32_MDMA_BUFFER,
	STM32_MDMA_BLOCK,
	STM32_MDMA_BLOCK_REP,
	STM32_MDMA_LINKED_LIST,
};

enum stm32_mdma_width {
	STM32_MDMA_BYTE,
	STM32_MDMA_HALF_WORD,
	STM32_MDMA_WORD,
	STM32_MDMA_DOUBLE_WORD,
};

enum stm32_mdma_inc_mode {
	STM32_MDMA_FIXED = 0,
	STM32_MDMA_INC = 2,
	STM32_MDMA_DEC = 3,
};

struct stm32_mdma_chan_config {
	u32 request;
	u32 priority_level;
	u32 transfer_config;
	u32 mask_addr;
	u32 mask_data;
};

struct stm32_mdma_hwdesc {
	u32 ctcr;
	u32 cbndtr;
	u32 csar;
	u32 cdar;
	u32 cbrur;
	u32 clar;
	u32 ctbr;
	u32 dummy;
	u32 cmar;
	u32 cmdr;
} __aligned(64);

struct stm32_mdma_desc_node {
	struct stm32_mdma_hwdesc *hwdesc;
	dma_addr_t hwdesc_phys;
};

struct stm32_mdma_desc {
	struct virt_dma_desc vdesc;
	u32 ccr;
	bool cyclic;
	u32 count;
	struct stm32_mdma_desc_node node[];
};

struct stm32_mdma_chan {
	struct virt_dma_chan vchan;
	struct dma_pool *desc_pool;
	u32 id;
	struct stm32_mdma_desc *desc;
	u32 curr_hwdesc;
	struct dma_slave_config dma_config;
	struct stm32_mdma_chan_config chan_config;
	bool busy;
	u32 mem_burst;
	u32 mem_width;
};

struct stm32_mdma_device {
	struct dma_device ddev;
	void __iomem *base;
	struct clk *clk;
	int irq;
	struct reset_control *rst;
	u32 nr_channels;
	u32 nr_requests;
	u32 nr_ahb_addr_masks;
	struct stm32_mdma_chan chan[STM32_MDMA_MAX_CHANNELS];
	u32 ahb_addr_masks[];
};

static struct stm32_mdma_device *stm32_mdma_get_dev(
	struct stm32_mdma_chan *chan)
{
	return container_of(chan->vchan.chan.device, struct stm32_mdma_device,
			    ddev);
}

static struct stm32_mdma_chan *to_stm32_mdma_chan(struct dma_chan *c)
{
	return container_of(c, struct stm32_mdma_chan, vchan.chan);
}

static struct stm32_mdma_desc *to_stm32_mdma_desc(struct virt_dma_desc *vdesc)
{
	return container_of(vdesc, struct stm32_mdma_desc, vdesc);
}

static struct device *chan2dev(struct stm32_mdma_chan *chan)
{
	return &chan->vchan.chan.dev->device;
}

static struct device *mdma2dev(struct stm32_mdma_device *mdma_dev)
{
	return mdma_dev->ddev.dev;
}

static u32 stm32_mdma_read(struct stm32_mdma_device *dmadev, u32 reg)
{
	return readl_relaxed(dmadev->base + reg);
}

static void stm32_mdma_write(struct stm32_mdma_device *dmadev, u32 reg, u32 val)
{
	writel_relaxed(val, dmadev->base + reg);
}

static void stm32_mdma_set_bits(struct stm32_mdma_device *dmadev, u32 reg,
				u32 mask)
{
	void __iomem *addr = dmadev->base + reg;

	writel_relaxed(readl_relaxed(addr) | mask, addr);
}

static void stm32_mdma_clr_bits(struct stm32_mdma_device *dmadev, u32 reg,
				u32 mask)
{
	void __iomem *addr = dmadev->base + reg;

	writel_relaxed(readl_relaxed(addr) & ~mask, addr);
}

static struct stm32_mdma_desc *stm32_mdma_alloc_desc(
		struct stm32_mdma_chan *chan, u32 count)
{
	struct stm32_mdma_desc *desc;
	int i;

	desc = kzalloc(offsetof(typeof(*desc), node[count]), GFP_NOWAIT);
	if (!desc)
		return NULL;

	for (i = 0; i < count; i++) {
		desc->node[i].hwdesc =
			dma_pool_alloc(chan->desc_pool, GFP_NOWAIT,
				       &desc->node[i].hwdesc_phys);
		if (!desc->node[i].hwdesc)
			goto err;
	}

	desc->count = count;

	return desc;

err:
	dev_err(chan2dev(chan), "Failed to allocate descriptor\n");
	while (--i >= 0)
		dma_pool_free(chan->desc_pool, desc->node[i].hwdesc,
			      desc->node[i].hwdesc_phys);
	kfree(desc);
	return NULL;
}

static void stm32_mdma_desc_free(struct virt_dma_desc *vdesc)
{
	struct stm32_mdma_desc *desc = to_stm32_mdma_desc(vdesc);
	struct stm32_mdma_chan *chan = to_stm32_mdma_chan(vdesc->tx.chan);
	int i;

	for (i = 0; i < desc->count; i++)
		dma_pool_free(chan->desc_pool, desc->node[i].hwdesc,
			      desc->node[i].hwdesc_phys);
	kfree(desc);
}

static int stm32_mdma_get_width(struct stm32_mdma_chan *chan,
				enum dma_slave_buswidth width)
{
	switch (width) {
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
	case DMA_SLAVE_BUSWIDTH_8_BYTES:
		return ffs(width) - 1;
	default:
		dev_err(chan2dev(chan), "Dma bus width %i not supported\n",
			width);
		return -EINVAL;
	}
}

static enum dma_slave_buswidth stm32_mdma_get_max_width(dma_addr_t addr,
							u32 buf_len, u32 tlen)
{
	enum dma_slave_buswidth max_width = DMA_SLAVE_BUSWIDTH_8_BYTES;

	for (max_width = DMA_SLAVE_BUSWIDTH_8_BYTES;
	     max_width > DMA_SLAVE_BUSWIDTH_1_BYTE;
	     max_width >>= 1) {
		/*
		 * Address and buffer length both have to be aligned on
		 * bus width
		 */
		if ((((buf_len | addr) & (max_width - 1)) == 0) &&
		    tlen >= max_width)
			break;
	}

	return max_width;
}

static u32 stm32_mdma_get_best_burst(u32 buf_len, u32 tlen, u32 max_burst,
				     enum dma_slave_buswidth width)
{
	u32 best_burst;

	best_burst = min((u32)1 << __ffs(tlen | buf_len),
			 max_burst * width) / width;

	return (best_burst > 0) ? best_burst : 1;
}

static int stm32_mdma_disable_chan(struct stm32_mdma_chan *chan)
{
	struct stm32_mdma_device *dmadev = stm32_mdma_get_dev(chan);
	u32 ccr, cisr, id, reg;
	int ret;

	id = chan->id;
	reg = STM32_MDMA_CCR(id);

	/* Disable interrupts */
	stm32_mdma_clr_bits(dmadev, reg, STM32_MDMA_CCR_IRQ_MASK);

	ccr = stm32_mdma_read(dmadev, reg);
	if (ccr & STM32_MDMA_CCR_EN) {
		stm32_mdma_clr_bits(dmadev, reg, STM32_MDMA_CCR_EN);

		/* Ensure that any ongoing transfer has been completed */
		ret = readl_relaxed_poll_timeout_atomic(
				dmadev->base + STM32_MDMA_CISR(id), cisr,
				(cisr & STM32_MDMA_CISR_CTCIF), 10, 1000);
		if (ret) {
			dev_err(chan2dev(chan), "%s: timeout!\n", __func__);
			return -EBUSY;
		}
	}

	return 0;
}

static void stm32_mdma_stop(struct stm32_mdma_chan *chan)
{
	struct stm32_mdma_device *dmadev = stm32_mdma_get_dev(chan);
	u32 status;
	int ret;

	/* Disable DMA */
	ret = stm32_mdma_disable_chan(chan);
	if (ret < 0)
		return;

	/* Clear interrupt status if it is there */
	status = stm32_mdma_read(dmadev, STM32_MDMA_CISR(chan->id));
	if (status) {
		dev_dbg(chan2dev(chan), "%s(): clearing interrupt: 0x%08x\n",
			__func__, status);
		stm32_mdma_set_bits(dmadev, STM32_MDMA_CIFCR(chan->id), status);
	}

	chan->busy = false;
}

static void stm32_mdma_set_bus(struct stm32_mdma_device *dmadev, u32 *ctbr,
			       u32 ctbr_mask, u32 src_addr)
{
	u32 mask;
	int i;

	/* Check if memory device is on AHB or AXI */
	*ctbr &= ~ctbr_mask;
	mask = src_addr & 0xF0000000;
	for (i = 0; i < dmadev->nr_ahb_addr_masks; i++) {
		if (mask == dmadev->ahb_addr_masks[i]) {
			*ctbr |= ctbr_mask;
			break;
		}
	}
}

static int stm32_mdma_set_xfer_param(struct stm32_mdma_chan *chan,
				     enum dma_transfer_direction direction,
				     u32 *mdma_ccr, u32 *mdma_ctcr,
				     u32 *mdma_ctbr, dma_addr_t addr,
				     u32 buf_len)
{
	struct stm32_mdma_device *dmadev = stm32_mdma_get_dev(chan);
	struct stm32_mdma_chan_config *chan_config = &chan->chan_config;
	enum dma_slave_buswidth src_addr_width, dst_addr_width;
	phys_addr_t src_addr, dst_addr;
	int src_bus_width, dst_bus_width;
	u32 src_maxburst, dst_maxburst, src_best_burst, dst_best_burst;
	u32 ccr, ctcr, ctbr, tlen;

	src_addr_width = chan->dma_config.src_addr_width;
	dst_addr_width = chan->dma_config.dst_addr_width;
	src_maxburst = chan->dma_config.src_maxburst;
	dst_maxburst = chan->dma_config.dst_maxburst;

	ccr = stm32_mdma_read(dmadev, STM32_MDMA_CCR(chan->id));
	ctcr = stm32_mdma_read(dmadev, STM32_MDMA_CTCR(chan->id));
	ctbr = stm32_mdma_read(dmadev, STM32_MDMA_CTBR(chan->id));

	/* Enable HW request mode */
	ctcr &= ~STM32_MDMA_CTCR_SWRM;

	/* Set DINC, SINC, DINCOS, SINCOS, TRGM and TLEN retrieve from DT */
	ctcr &= ~STM32_MDMA_CTCR_CFG_MASK;
	ctcr |= chan_config->transfer_config & STM32_MDMA_CTCR_CFG_MASK;

	/*
	 * For buffer transfer length (TLEN) we have to set
	 * the number of bytes - 1 in CTCR register
	 */
	tlen = STM32_MDMA_CTCR_LEN2_GET(ctcr);
	ctcr &= ~STM32_MDMA_CTCR_LEN2_MSK;
	ctcr |= STM32_MDMA_CTCR_TLEN((tlen - 1));

	/* Disable Pack Enable */
	ctcr &= ~STM32_MDMA_CTCR_PKE;

	/* Check burst size constraints */
	if (src_maxburst * src_addr_width > STM32_MDMA_MAX_BURST ||
	    dst_maxburst * dst_addr_width > STM32_MDMA_MAX_BURST) {
		dev_err(chan2dev(chan),
			"burst size * bus width higher than %d bytes\n",
			STM32_MDMA_MAX_BURST);
		return -EINVAL;
	}

	if ((!is_power_of_2(src_maxburst) && src_maxburst > 0) ||
	    (!is_power_of_2(dst_maxburst) && dst_maxburst > 0)) {
		dev_err(chan2dev(chan), "burst size must be a power of 2\n");
		return -EINVAL;
	}

	/*
	 * Configure channel control:
	 * - Clear SW request as in this case this is a HW one
	 * - Clear WEX, HEX and BEX bits
	 * - Set priority level
	 */
	ccr &= ~(STM32_MDMA_CCR_SWRQ | STM32_MDMA_CCR_WEX | STM32_MDMA_CCR_HEX |
		 STM32_MDMA_CCR_BEX | STM32_MDMA_CCR_PL_MASK);
	ccr |= STM32_MDMA_CCR_PL(chan_config->priority_level);

	/* Configure Trigger selection */
	ctbr &= ~STM32_MDMA_CTBR_TSEL_MASK;
	ctbr |= STM32_MDMA_CTBR_TSEL(chan_config->request);

	switch (direction) {
	case DMA_MEM_TO_DEV:
		dst_addr = chan->dma_config.dst_addr;

		/* Set device data size */
		dst_bus_width = stm32_mdma_get_width(chan, dst_addr_width);
		if (dst_bus_width < 0)
			return dst_bus_width;
		ctcr &= ~STM32_MDMA_CTCR_DSIZE_MASK;
		ctcr |= STM32_MDMA_CTCR_DSIZE(dst_bus_width);

		/* Set device burst value */
		dst_best_burst = stm32_mdma_get_best_burst(buf_len, tlen,
							   dst_maxburst,
							   dst_addr_width);
		chan->mem_burst = dst_best_burst;
		ctcr &= ~STM32_MDMA_CTCR_DBURST_MASK;
		ctcr |= STM32_MDMA_CTCR_DBURST((ilog2(dst_best_burst)));

		/* Set memory data size */
		src_addr_width = stm32_mdma_get_max_width(addr, buf_len, tlen);
		chan->mem_width = src_addr_width;
		src_bus_width = stm32_mdma_get_width(chan, src_addr_width);
		if (src_bus_width < 0)
			return src_bus_width;
		ctcr &= ~STM32_MDMA_CTCR_SSIZE_MASK |
			STM32_MDMA_CTCR_SINCOS_MASK;
		ctcr |= STM32_MDMA_CTCR_SSIZE(src_bus_width) |
			STM32_MDMA_CTCR_SINCOS(src_bus_width);

		/* Set memory burst value */
		src_maxburst = STM32_MDMA_MAX_BUF_LEN / src_addr_width;
		src_best_burst = stm32_mdma_get_best_burst(buf_len, tlen,
							   src_maxburst,
							   src_addr_width);
		chan->mem_burst = src_best_burst;
		ctcr &= ~STM32_MDMA_CTCR_SBURST_MASK;
		ctcr |= STM32_MDMA_CTCR_SBURST((ilog2(src_best_burst)));

		/* Select bus */
		stm32_mdma_set_bus(dmadev, &ctbr, STM32_MDMA_CTBR_DBUS,
				   dst_addr);

		if (dst_bus_width != src_bus_width)
			ctcr |= STM32_MDMA_CTCR_PKE;

		/* Set destination address */
		stm32_mdma_write(dmadev, STM32_MDMA_CDAR(chan->id), dst_addr);
		break;

	case DMA_DEV_TO_MEM:
		src_addr = chan->dma_config.src_addr;

		/* Set device data size */
		src_bus_width = stm32_mdma_get_width(chan, src_addr_width);
		if (src_bus_width < 0)
			return src_bus_width;
		ctcr &= ~STM32_MDMA_CTCR_SSIZE_MASK;
		ctcr |= STM32_MDMA_CTCR_SSIZE(src_bus_width);

		/* Set device burst value */
		src_best_burst = stm32_mdma_get_best_burst(buf_len, tlen,
							   src_maxburst,
							   src_addr_width);
		ctcr &= ~STM32_MDMA_CTCR_SBURST_MASK;
		ctcr |= STM32_MDMA_CTCR_SBURST((ilog2(src_best_burst)));

		/* Set memory data size */
		dst_addr_width = stm32_mdma_get_max_width(addr, buf_len, tlen);
		chan->mem_width = dst_addr_width;
		dst_bus_width = stm32_mdma_get_width(chan, dst_addr_width);
		if (dst_bus_width < 0)
			return dst_bus_width;
		ctcr &= ~(STM32_MDMA_CTCR_DSIZE_MASK |
			STM32_MDMA_CTCR_DINCOS_MASK);
		ctcr |= STM32_MDMA_CTCR_DSIZE(dst_bus_width) |
			STM32_MDMA_CTCR_DINCOS(dst_bus_width);

		/* Set memory burst value */
		dst_maxburst = STM32_MDMA_MAX_BUF_LEN / dst_addr_width;
		dst_best_burst = stm32_mdma_get_best_burst(buf_len, tlen,
							   dst_maxburst,
							   dst_addr_width);
		ctcr &= ~STM32_MDMA_CTCR_DBURST_MASK;
		ctcr |= STM32_MDMA_CTCR_DBURST((ilog2(dst_best_burst)));

		/* Select bus */
		stm32_mdma_set_bus(dmadev, &ctbr, STM32_MDMA_CTBR_SBUS,
				   src_addr);

		if (dst_bus_width != src_bus_width)
			ctcr |= STM32_MDMA_CTCR_PKE;

		/* Set source address */
		stm32_mdma_write(dmadev, STM32_MDMA_CSAR(chan->id), src_addr);
		break;

	default:
		dev_err(chan2dev(chan), "Dma direction is not supported\n");
		return -EINVAL;
	}

	*mdma_ccr = ccr;
	*mdma_ctcr = ctcr;
	*mdma_ctbr = ctbr;

	return 0;
}

static void stm32_mdma_dump_hwdesc(struct stm32_mdma_chan *chan,
				   struct stm32_mdma_desc_node *node)
{
	dev_dbg(chan2dev(chan), "hwdesc:  %pad\n", &node->hwdesc_phys);
	dev_dbg(chan2dev(chan), "CTCR:    0x%08x\n", node->hwdesc->ctcr);
	dev_dbg(chan2dev(chan), "CBNDTR:  0x%08x\n", node->hwdesc->cbndtr);
	dev_dbg(chan2dev(chan), "CSAR:    0x%08x\n", node->hwdesc->csar);
	dev_dbg(chan2dev(chan), "CDAR:    0x%08x\n", node->hwdesc->cdar);
	dev_dbg(chan2dev(chan), "CBRUR:   0x%08x\n", node->hwdesc->cbrur);
	dev_dbg(chan2dev(chan), "CLAR:    0x%08x\n", node->hwdesc->clar);
	dev_dbg(chan2dev(chan), "CTBR:    0x%08x\n", node->hwdesc->ctbr);
	dev_dbg(chan2dev(chan), "CMAR:    0x%08x\n", node->hwdesc->cmar);
	dev_dbg(chan2dev(chan), "CMDR:    0x%08x\n\n", node->hwdesc->cmdr);
}

static void stm32_mdma_setup_hwdesc(struct stm32_mdma_chan *chan,
				    struct stm32_mdma_desc *desc,
				    enum dma_transfer_direction dir, u32 count,
				    dma_addr_t src_addr, dma_addr_t dst_addr,
				    u32 len, u32 ctcr, u32 ctbr, bool is_last,
				    bool is_first, bool is_cyclic)
{
	struct stm32_mdma_chan_config *config = &chan->chan_config;
	struct stm32_mdma_hwdesc *hwdesc;
	u32 next = count + 1;

	hwdesc = desc->node[count].hwdesc;
	hwdesc->ctcr = ctcr;
	hwdesc->cbndtr &= ~(STM32_MDMA_CBNDTR_BRC_MK |
			STM32_MDMA_CBNDTR_BRDUM |
			STM32_MDMA_CBNDTR_BRSUM |
			STM32_MDMA_CBNDTR_BNDT_MASK);
	hwdesc->cbndtr |= STM32_MDMA_CBNDTR_BNDT(len);
	hwdesc->csar = src_addr;
	hwdesc->cdar = dst_addr;
	hwdesc->cbrur = 0;
	hwdesc->ctbr = ctbr;
	hwdesc->cmar = config->mask_addr;
	hwdesc->cmdr = config->mask_data;

	if (is_last) {
		if (is_cyclic)
			hwdesc->clar = desc->node[0].hwdesc_phys;
		else
			hwdesc->clar = 0;
	} else {
		hwdesc->clar = desc->node[next].hwdesc_phys;
	}

	stm32_mdma_dump_hwdesc(chan, &desc->node[count]);
}

static int stm32_mdma_setup_xfer(struct stm32_mdma_chan *chan,
				 struct stm32_mdma_desc *desc,
				 struct scatterlist *sgl, u32 sg_len,
				 enum dma_transfer_direction direction)
{
	struct stm32_mdma_device *dmadev = stm32_mdma_get_dev(chan);
	struct dma_slave_config *dma_config = &chan->dma_config;
	struct scatterlist *sg;
	dma_addr_t src_addr, dst_addr;
	u32 ccr, ctcr, ctbr;
	int i, ret = 0;

	for_each_sg(sgl, sg, sg_len, i) {
		if (sg_dma_len(sg) > STM32_MDMA_MAX_BLOCK_LEN) {
			dev_err(chan2dev(chan), "Invalid block len\n");
			return -EINVAL;
		}

		if (direction == DMA_MEM_TO_DEV) {
			src_addr = sg_dma_address(sg);
			dst_addr = dma_config->dst_addr;
			ret = stm32_mdma_set_xfer_param(chan, direction, &ccr,
							&ctcr, &ctbr, src_addr,
							sg_dma_len(sg));
			stm32_mdma_set_bus(dmadev, &ctbr, STM32_MDMA_CTBR_SBUS,
					   src_addr);
		} else {
			src_addr = dma_config->src_addr;
			dst_addr = sg_dma_address(sg);
			ret = stm32_mdma_set_xfer_param(chan, direction, &ccr,
							&ctcr, &ctbr, dst_addr,
							sg_dma_len(sg));
			stm32_mdma_set_bus(dmadev, &ctbr, STM32_MDMA_CTBR_DBUS,
					   dst_addr);
		}

		if (ret < 0)
			return ret;

		stm32_mdma_setup_hwdesc(chan, desc, direction, i, src_addr,
					dst_addr, sg_dma_len(sg), ctcr, ctbr,
					i == sg_len - 1, i == 0, false);
	}

	/* Enable interrupts */
	ccr &= ~STM32_MDMA_CCR_IRQ_MASK;
	ccr |= STM32_MDMA_CCR_TEIE | STM32_MDMA_CCR_CTCIE;
	if (sg_len > 1)
		ccr |= STM32_MDMA_CCR_BTIE;
	desc->ccr = ccr;

	return 0;
}

static struct dma_async_tx_descriptor *
stm32_mdma_prep_slave_sg(struct dma_chan *c, struct scatterlist *sgl,
			 u32 sg_len, enum dma_transfer_direction direction,
			 unsigned long flags, void *context)
{
	struct stm32_mdma_chan *chan = to_stm32_mdma_chan(c);
	struct stm32_mdma_desc *desc;
	int i, ret;

	/*
	 * Once DMA is in setup cyclic mode the channel we cannot assign this
	 * channel anymore. The DMA channel needs to be aborted or terminated
	 * for allowing another request.
	 */
	if (chan->desc && chan->desc->cyclic) {
		dev_err(chan2dev(chan),
			"Request not allowed when dma in cyclic mode\n");
		return NULL;
	}

	desc = stm32_mdma_alloc_desc(chan, sg_len);
	if (!desc)
		return NULL;

	ret = stm32_mdma_setup_xfer(chan, desc, sgl, sg_len, direction);
	if (ret < 0)
		goto xfer_setup_err;

	desc->cyclic = false;

	return vchan_tx_prep(&chan->vchan, &desc->vdesc, flags);

xfer_setup_err:
	for (i = 0; i < desc->count; i++)
		dma_pool_free(chan->desc_pool, desc->node[i].hwdesc,
			      desc->node[i].hwdesc_phys);
	kfree(desc);
	return NULL;
}

static struct dma_async_tx_descriptor *
stm32_mdma_prep_dma_cyclic(struct dma_chan *c, dma_addr_t buf_addr,
			   size_t buf_len, size_t period_len,
			   enum dma_transfer_direction direction,
			   unsigned long flags)
{
	struct stm32_mdma_chan *chan = to_stm32_mdma_chan(c);
	struct stm32_mdma_device *dmadev = stm32_mdma_get_dev(chan);
	struct dma_slave_config *dma_config = &chan->dma_config;
	struct stm32_mdma_desc *desc;
	dma_addr_t src_addr, dst_addr;
	u32 ccr, ctcr, ctbr, count;
	int i, ret;

	/*
	 * Once DMA is in setup cyclic mode the channel we cannot assign this
	 * channel anymore. The DMA channel needs to be aborted or terminated
	 * for allowing another request.
	 */
	if (chan->desc && chan->desc->cyclic) {
		dev_err(chan2dev(chan),
			"Request not allowed when dma in cyclic mode\n");
		return NULL;
	}

	if (!buf_len || !period_len || period_len > STM32_MDMA_MAX_BLOCK_LEN) {
		dev_err(chan2dev(chan), "Invalid buffer/period len\n");
		return NULL;
	}

	if (buf_len % period_len) {
		dev_err(chan2dev(chan), "buf_len not multiple of period_len\n");
		return NULL;
	}

	count = buf_len / period_len;

	desc = stm32_mdma_alloc_desc(chan, count);
	if (!desc)
		return NULL;

	/* Select bus */
	if (direction == DMA_MEM_TO_DEV) {
		src_addr = buf_addr;
		ret = stm32_mdma_set_xfer_param(chan, direction, &ccr, &ctcr,
						&ctbr, src_addr, period_len);
		stm32_mdma_set_bus(dmadev, &ctbr, STM32_MDMA_CTBR_SBUS,
				   src_addr);
	} else {
		dst_addr = buf_addr;
		ret = stm32_mdma_set_xfer_param(chan, direction, &ccr, &ctcr,
						&ctbr, dst_addr, period_len);
		stm32_mdma_set_bus(dmadev, &ctbr, STM32_MDMA_CTBR_DBUS,
				   dst_addr);
	}

	if (ret < 0)
		goto xfer_setup_err;

	/* Enable interrupts */
	ccr &= ~STM32_MDMA_CCR_IRQ_MASK;
	ccr |= STM32_MDMA_CCR_TEIE | STM32_MDMA_CCR_CTCIE | STM32_MDMA_CCR_BTIE;
	desc->ccr = ccr;

	/* Configure hwdesc list */
	for (i = 0; i < count; i++) {
		if (direction == DMA_MEM_TO_DEV) {
			src_addr = buf_addr + i * period_len;
			dst_addr = dma_config->dst_addr;
		} else {
			src_addr = dma_config->src_addr;
			dst_addr = buf_addr + i * period_len;
		}

		stm32_mdma_setup_hwdesc(chan, desc, direction, i, src_addr,
					dst_addr, period_len, ctcr, ctbr,
					i == count - 1, i == 0, true);
	}

	desc->cyclic = true;

	return vchan_tx_prep(&chan->vchan, &desc->vdesc, flags);

xfer_setup_err:
	for (i = 0; i < desc->count; i++)
		dma_pool_free(chan->desc_pool, desc->node[i].hwdesc,
			      desc->node[i].hwdesc_phys);
	kfree(desc);
	return NULL;
}

static struct dma_async_tx_descriptor *
stm32_mdma_prep_dma_memcpy(struct dma_chan *c, dma_addr_t dest, dma_addr_t src,
			   size_t len, unsigned long flags)
{
	struct stm32_mdma_chan *chan = to_stm32_mdma_chan(c);
	struct stm32_mdma_device *dmadev = stm32_mdma_get_dev(chan);
	enum dma_slave_buswidth max_width;
	struct stm32_mdma_desc *desc;
	struct stm32_mdma_hwdesc *hwdesc;
	u32 ccr, ctcr, ctbr, cbndtr, count, max_burst, mdma_burst;
	u32 best_burst, tlen;
	size_t xfer_count, offset;
	int src_bus_width, dst_bus_width;
	int i;

	/*
	 * Once DMA is in setup cyclic mode the channel we cannot assign this
	 * channel anymore. The DMA channel needs to be aborted or terminated
	 * to allow another request
	 */
	if (chan->desc && chan->desc->cyclic) {
		dev_err(chan2dev(chan),
			"Request not allowed when dma in cyclic mode\n");
		return NULL;
	}

	count = DIV_ROUND_UP(len, STM32_MDMA_MAX_BLOCK_LEN);
	desc = stm32_mdma_alloc_desc(chan, count);
	if (!desc)
		return NULL;

	ccr = stm32_mdma_read(dmadev, STM32_MDMA_CCR(chan->id));
	ctcr = stm32_mdma_read(dmadev, STM32_MDMA_CTCR(chan->id));
	ctbr = stm32_mdma_read(dmadev, STM32_MDMA_CTBR(chan->id));
	cbndtr = stm32_mdma_read(dmadev, STM32_MDMA_CBNDTR(chan->id));

	/* Enable sw req, some interrupts and clear other bits */
	ccr &= ~(STM32_MDMA_CCR_WEX | STM32_MDMA_CCR_HEX |
		 STM32_MDMA_CCR_BEX | STM32_MDMA_CCR_PL_MASK |
		 STM32_MDMA_CCR_IRQ_MASK);
	ccr |= STM32_MDMA_CCR_TEIE;

	/* Enable SW request mode, dest/src inc and clear other bits */
	ctcr &= ~(STM32_MDMA_CTCR_BWM | STM32_MDMA_CTCR_TRGM_MSK |
		  STM32_MDMA_CTCR_PAM_MASK | STM32_MDMA_CTCR_PKE |
		  STM32_MDMA_CTCR_TLEN_MSK | STM32_MDMA_CTCR_DBURST_MASK |
		  STM32_MDMA_CTCR_SBURST_MASK | STM32_MDMA_CTCR_DINCOS_MASK |
		  STM32_MDMA_CTCR_SINCOS_MASK | STM32_MDMA_CTCR_DSIZE_MASK |
		  STM32_MDMA_CTCR_SSIZE_MASK | STM32_MDMA_CTCR_DINC_MASK |
		  STM32_MDMA_CTCR_SINC_MASK);
	ctcr |= STM32_MDMA_CTCR_SWRM | STM32_MDMA_CTCR_SINC(STM32_MDMA_INC) |
		STM32_MDMA_CTCR_DINC(STM32_MDMA_INC);

	/* Reset HW request */
	ctbr &= ~STM32_MDMA_CTBR_TSEL_MASK;

	/* Select bus */
	stm32_mdma_set_bus(dmadev, &ctbr, STM32_MDMA_CTBR_SBUS, src);
	stm32_mdma_set_bus(dmadev, &ctbr, STM32_MDMA_CTBR_DBUS, dest);

	/* Clear CBNDTR registers */
	cbndtr &= ~(STM32_MDMA_CBNDTR_BRC_MK | STM32_MDMA_CBNDTR_BRDUM |
			STM32_MDMA_CBNDTR_BRSUM | STM32_MDMA_CBNDTR_BNDT_MASK);

	if (len <= STM32_MDMA_MAX_BLOCK_LEN) {
		cbndtr |= STM32_MDMA_CBNDTR_BNDT(len);
		if (len <= STM32_MDMA_MAX_BUF_LEN) {
			/* Setup a buffer transfer */
			ccr |= STM32_MDMA_CCR_TCIE | STM32_MDMA_CCR_CTCIE;
			ctcr |= STM32_MDMA_CTCR_TRGM(STM32_MDMA_BUFFER);
		} else {
			/* Setup a block transfer */
			ccr |= STM32_MDMA_CCR_BTIE | STM32_MDMA_CCR_CTCIE;
			ctcr |= STM32_MDMA_CTCR_TRGM(STM32_MDMA_BLOCK);
		}

		tlen = STM32_MDMA_MAX_BUF_LEN;
		ctcr |= STM32_MDMA_CTCR_TLEN((tlen - 1));

		/* Set source best burst size */
		max_width = stm32_mdma_get_max_width(src, len, tlen);
		src_bus_width = stm32_mdma_get_width(chan, max_width);

		max_burst = tlen / max_width;
		best_burst = stm32_mdma_get_best_burst(len, tlen, max_burst,
						       max_width);
		mdma_burst = ilog2(best_burst);

		ctcr |= STM32_MDMA_CTCR_SBURST(mdma_burst) |
			STM32_MDMA_CTCR_SSIZE(src_bus_width) |
			STM32_MDMA_CTCR_SINCOS(src_bus_width);

		/* Set destination best burst size */
		max_width = stm32_mdma_get_max_width(dest, len, tlen);
		dst_bus_width = stm32_mdma_get_width(chan, max_width);

		max_burst = tlen / max_width;
		best_burst = stm32_mdma_get_best_burst(len, tlen, max_burst,
						       max_width);
		mdma_burst = ilog2(best_burst);

		ctcr |= STM32_MDMA_CTCR_DBURST(mdma_burst) |
			STM32_MDMA_CTCR_DSIZE(dst_bus_width) |
			STM32_MDMA_CTCR_DINCOS(dst_bus_width);

		if (dst_bus_width != src_bus_width)
			ctcr |= STM32_MDMA_CTCR_PKE;

		/* Prepare hardware descriptor */
		hwdesc = desc->node[0].hwdesc;
		hwdesc->ctcr = ctcr;
		hwdesc->cbndtr = cbndtr;
		hwdesc->csar = src;
		hwdesc->cdar = dest;
		hwdesc->cbrur = 0;
		hwdesc->clar = 0;
		hwdesc->ctbr = ctbr;
		hwdesc->cmar = 0;
		hwdesc->cmdr = 0;

		stm32_mdma_dump_hwdesc(chan, &desc->node[0]);
	} else {
		/* Setup a LLI transfer */
		ctcr |= STM32_MDMA_CTCR_TRGM(STM32_MDMA_LINKED_LIST) |
			STM32_MDMA_CTCR_TLEN((STM32_MDMA_MAX_BUF_LEN - 1));
		ccr |= STM32_MDMA_CCR_BTIE | STM32_MDMA_CCR_CTCIE;
		tlen = STM32_MDMA_MAX_BUF_LEN;

		for (i = 0, offset = 0; offset < len;
		     i++, offset += xfer_count) {
			xfer_count = min_t(size_t, len - offset,
					   STM32_MDMA_MAX_BLOCK_LEN);

			/* Set source best burst size */
			max_width = stm32_mdma_get_max_width(src, len, tlen);
			src_bus_width = stm32_mdma_get_width(chan, max_width);

			max_burst = tlen / max_width;
			best_burst = stm32_mdma_get_best_burst(len, tlen,
							       max_burst,
							       max_width);
			mdma_burst = ilog2(best_burst);

			ctcr |= STM32_MDMA_CTCR_SBURST(mdma_burst) |
				STM32_MDMA_CTCR_SSIZE(src_bus_width) |
				STM32_MDMA_CTCR_SINCOS(src_bus_width);

			/* Set destination best burst size */
			max_width = stm32_mdma_get_max_width(dest, len, tlen);
			dst_bus_width = stm32_mdma_get_width(chan, max_width);

			max_burst = tlen / max_width;
			best_burst = stm32_mdma_get_best_burst(len, tlen,
							       max_burst,
							       max_width);
			mdma_burst = ilog2(best_burst);

			ctcr |= STM32_MDMA_CTCR_DBURST(mdma_burst) |
				STM32_MDMA_CTCR_DSIZE(dst_bus_width) |
				STM32_MDMA_CTCR_DINCOS(dst_bus_width);

			if (dst_bus_width != src_bus_width)
				ctcr |= STM32_MDMA_CTCR_PKE;

			/* Prepare hardware descriptor */
			stm32_mdma_setup_hwdesc(chan, desc, DMA_MEM_TO_MEM, i,
						src + offset, dest + offset,
						xfer_count, ctcr, ctbr,
						i == count - 1, i == 0, false);
		}
	}

	desc->ccr = ccr;

	desc->cyclic = false;

	return vchan_tx_prep(&chan->vchan, &desc->vdesc, flags);
}

static void stm32_mdma_dump_reg(struct stm32_mdma_chan *chan)
{
	struct stm32_mdma_device *dmadev = stm32_mdma_get_dev(chan);

	dev_dbg(chan2dev(chan), "CCR:     0x%08x\n",
		stm32_mdma_read(dmadev, STM32_MDMA_CCR(chan->id)));
	dev_dbg(chan2dev(chan), "CTCR:    0x%08x\n",
		stm32_mdma_read(dmadev, STM32_MDMA_CTCR(chan->id)));
	dev_dbg(chan2dev(chan), "CBNDTR:  0x%08x\n",
		stm32_mdma_read(dmadev, STM32_MDMA_CBNDTR(chan->id)));
	dev_dbg(chan2dev(chan), "CSAR:    0x%08x\n",
		stm32_mdma_read(dmadev, STM32_MDMA_CSAR(chan->id)));
	dev_dbg(chan2dev(chan), "CDAR:    0x%08x\n",
		stm32_mdma_read(dmadev, STM32_MDMA_CDAR(chan->id)));
	dev_dbg(chan2dev(chan), "CBRUR:   0x%08x\n",
		stm32_mdma_read(dmadev, STM32_MDMA_CBRUR(chan->id)));
	dev_dbg(chan2dev(chan), "CLAR:    0x%08x\n",
		stm32_mdma_read(dmadev, STM32_MDMA_CLAR(chan->id)));
	dev_dbg(chan2dev(chan), "CTBR:    0x%08x\n",
		stm32_mdma_read(dmadev, STM32_MDMA_CTBR(chan->id)));
	dev_dbg(chan2dev(chan), "CMAR:    0x%08x\n",
		stm32_mdma_read(dmadev, STM32_MDMA_CMAR(chan->id)));
	dev_dbg(chan2dev(chan), "CMDR:    0x%08x\n",
		stm32_mdma_read(dmadev, STM32_MDMA_CMDR(chan->id)));
}

static void stm32_mdma_start_transfer(struct stm32_mdma_chan *chan)
{
	struct stm32_mdma_device *dmadev = stm32_mdma_get_dev(chan);
	struct virt_dma_desc *vdesc;
	struct stm32_mdma_hwdesc *hwdesc;
	u32 id = chan->id;
	u32 status, reg;

	vdesc = vchan_next_desc(&chan->vchan);
	if (!vdesc) {
		chan->desc = NULL;
		return;
	}

	chan->desc = to_stm32_mdma_desc(vdesc);
	hwdesc = chan->desc->node[0].hwdesc;
	chan->curr_hwdesc = 0;

	stm32_mdma_write(dmadev, STM32_MDMA_CCR(id), chan->desc->ccr);
	stm32_mdma_write(dmadev, STM32_MDMA_CTCR(id), hwdesc->ctcr);
	stm32_mdma_write(dmadev, STM32_MDMA_CBNDTR(id), hwdesc->cbndtr);
	stm32_mdma_write(dmadev, STM32_MDMA_CSAR(id), hwdesc->csar);
	stm32_mdma_write(dmadev, STM32_MDMA_CDAR(id), hwdesc->cdar);
	stm32_mdma_write(dmadev, STM32_MDMA_CBRUR(id), hwdesc->cbrur);
	stm32_mdma_write(dmadev, STM32_MDMA_CLAR(id), hwdesc->clar);
	stm32_mdma_write(dmadev, STM32_MDMA_CTBR(id), hwdesc->ctbr);
	stm32_mdma_write(dmadev, STM32_MDMA_CMAR(id), hwdesc->cmar);
	stm32_mdma_write(dmadev, STM32_MDMA_CMDR(id), hwdesc->cmdr);

	/* Clear interrupt status if it is there */
	status = stm32_mdma_read(dmadev, STM32_MDMA_CISR(id));
	if (status)
		stm32_mdma_set_bits(dmadev, STM32_MDMA_CIFCR(id), status);

	stm32_mdma_dump_reg(chan);

	/* Start DMA */
	stm32_mdma_set_bits(dmadev, STM32_MDMA_CCR(id), STM32_MDMA_CCR_EN);

	/* Set SW request in case of MEM2MEM transfer */
	if (hwdesc->ctcr & STM32_MDMA_CTCR_SWRM) {
		reg = STM32_MDMA_CCR(id);
		stm32_mdma_set_bits(dmadev, reg, STM32_MDMA_CCR_SWRQ);
	}

	chan->busy = true;

	dev_dbg(chan2dev(chan), "vchan %pK: started\n", &chan->vchan);
}

static void stm32_mdma_issue_pending(struct dma_chan *c)
{
	struct stm32_mdma_chan *chan = to_stm32_mdma_chan(c);
	unsigned long flags;

	spin_lock_irqsave(&chan->vchan.lock, flags);

	if (!vchan_issue_pending(&chan->vchan))
		goto end;

	dev_dbg(chan2dev(chan), "vchan %pK: issued\n", &chan->vchan);

	if (!chan->desc && !chan->busy)
		stm32_mdma_start_transfer(chan);

end:
	spin_unlock_irqrestore(&chan->vchan.lock, flags);
}

static int stm32_mdma_pause(struct dma_chan *c)
{
	struct stm32_mdma_chan *chan = to_stm32_mdma_chan(c);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&chan->vchan.lock, flags);
	ret = stm32_mdma_disable_chan(chan);
	spin_unlock_irqrestore(&chan->vchan.lock, flags);

	if (!ret)
		dev_dbg(chan2dev(chan), "vchan %pK: pause\n", &chan->vchan);

	return ret;
}

static int stm32_mdma_resume(struct dma_chan *c)
{
	struct stm32_mdma_chan *chan = to_stm32_mdma_chan(c);
	struct stm32_mdma_device *dmadev = stm32_mdma_get_dev(chan);
	struct stm32_mdma_hwdesc *hwdesc;
	unsigned long flags;
	u32 status, reg;

	hwdesc = chan->desc->node[chan->curr_hwdesc].hwdesc;

	spin_lock_irqsave(&chan->vchan.lock, flags);

	/* Re-configure control register */
	stm32_mdma_write(dmadev, STM32_MDMA_CCR(chan->id), chan->desc->ccr);

	/* Clear interrupt status if it is there */
	status = stm32_mdma_read(dmadev, STM32_MDMA_CISR(chan->id));
	if (status)
		stm32_mdma_set_bits(dmadev, STM32_MDMA_CIFCR(chan->id), status);

	stm32_mdma_dump_reg(chan);

	/* Re-start DMA */
	reg = STM32_MDMA_CCR(chan->id);
	stm32_mdma_set_bits(dmadev, reg, STM32_MDMA_CCR_EN);

	/* Set SW request in case of MEM2MEM transfer */
	if (hwdesc->ctcr & STM32_MDMA_CTCR_SWRM)
		stm32_mdma_set_bits(dmadev, reg, STM32_MDMA_CCR_SWRQ);

	spin_unlock_irqrestore(&chan->vchan.lock, flags);

	dev_dbg(chan2dev(chan), "vchan %pK: resume\n", &chan->vchan);

	return 0;
}

static int stm32_mdma_terminate_all(struct dma_chan *c)
{
	struct stm32_mdma_chan *chan = to_stm32_mdma_chan(c);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&chan->vchan.lock, flags);
	if (chan->busy) {
		stm32_mdma_stop(chan);
		chan->desc = NULL;
	}
	vchan_get_all_descriptors(&chan->vchan, &head);
	spin_unlock_irqrestore(&chan->vchan.lock, flags);

	vchan_dma_desc_free_list(&chan->vchan, &head);

	return 0;
}

static void stm32_mdma_synchronize(struct dma_chan *c)
{
	struct stm32_mdma_chan *chan = to_stm32_mdma_chan(c);

	vchan_synchronize(&chan->vchan);
}

static int stm32_mdma_slave_config(struct dma_chan *c,
				   struct dma_slave_config *config)
{
	struct stm32_mdma_chan *chan = to_stm32_mdma_chan(c);

	memcpy(&chan->dma_config, config, sizeof(*config));

	return 0;
}

static size_t stm32_mdma_desc_residue(struct stm32_mdma_chan *chan,
				      struct stm32_mdma_desc *desc,
				      u32 curr_hwdesc)
{
	struct stm32_mdma_device *dmadev = stm32_mdma_get_dev(chan);
	struct stm32_mdma_hwdesc *hwdesc = desc->node[0].hwdesc;
	u32 cbndtr, residue, modulo, burst_size;
	int i;

	residue = 0;
	for (i = curr_hwdesc + 1; i < desc->count; i++) {
		hwdesc = desc->node[i].hwdesc;
		residue += STM32_MDMA_CBNDTR_BNDT(hwdesc->cbndtr);
	}
	cbndtr = stm32_mdma_read(dmadev, STM32_MDMA_CBNDTR(chan->id));
	residue += cbndtr & STM32_MDMA_CBNDTR_BNDT_MASK;

	if (!chan->mem_burst)
		return residue;

	burst_size = chan->mem_burst * chan->mem_width;
	modulo = residue % burst_size;
	if (modulo)
		residue = residue - modulo + burst_size;

	return residue;
}

static enum dma_status stm32_mdma_tx_status(struct dma_chan *c,
					    dma_cookie_t cookie,
					    struct dma_tx_state *state)
{
	struct stm32_mdma_chan *chan = to_stm32_mdma_chan(c);
	struct virt_dma_desc *vdesc;
	enum dma_status status;
	unsigned long flags;
	u32 residue = 0;

	status = dma_cookie_status(c, cookie, state);
	if ((status == DMA_COMPLETE) || (!state))
		return status;

	spin_lock_irqsave(&chan->vchan.lock, flags);

	vdesc = vchan_find_desc(&chan->vchan, cookie);
	if (chan->desc && cookie == chan->desc->vdesc.tx.cookie)
		residue = stm32_mdma_desc_residue(chan, chan->desc,
						  chan->curr_hwdesc);
	else if (vdesc)
		residue = stm32_mdma_desc_residue(chan,
						  to_stm32_mdma_desc(vdesc), 0);
	dma_set_residue(state, residue);

	spin_unlock_irqrestore(&chan->vchan.lock, flags);

	return status;
}

static void stm32_mdma_xfer_end(struct stm32_mdma_chan *chan)
{
	list_del(&chan->desc->vdesc.node);
	vchan_cookie_complete(&chan->desc->vdesc);
	chan->desc = NULL;
	chan->busy = false;

	/* Start the next transfer if this driver has a next desc */
	stm32_mdma_start_transfer(chan);
}

static irqreturn_t stm32_mdma_irq_handler(int irq, void *devid)
{
	struct stm32_mdma_device *dmadev = devid;
	struct stm32_mdma_chan *chan = devid;
	u32 reg, id, ien, status, flag;

	/* Find out which channel generates the interrupt */
	status = readl_relaxed(dmadev->base + STM32_MDMA_GISR0);
	if (status) {
		id = __ffs(status);
	} else {
		status = readl_relaxed(dmadev->base + STM32_MDMA_GISR1);
		if (!status) {
			dev_dbg(mdma2dev(dmadev), "spurious it\n");
			return IRQ_NONE;
		}
		id = __ffs(status);
		/*
		 * As GISR0 provides status for channel id from 0 to 31,
		 * so GISR1 provides status for channel id from 32 to 62
		 */
		id += 32;
	}

	chan = &dmadev->chan[id];
	if (!chan) {
		dev_err(chan2dev(chan), "MDMA channel not initialized\n");
		goto exit;
	}

	/* Handle interrupt for the channel */
	spin_lock(&chan->vchan.lock);
	status = stm32_mdma_read(dmadev, STM32_MDMA_CISR(chan->id));
	ien = stm32_mdma_read(dmadev, STM32_MDMA_CCR(chan->id));
	ien &= STM32_MDMA_CCR_IRQ_MASK;
	ien >>= 1;

	if (!(status & ien)) {
		spin_unlock(&chan->vchan.lock);
		dev_dbg(chan2dev(chan),
			"spurious it (status=0x%04x, ien=0x%04x)\n",
			status, ien);
		return IRQ_NONE;
	}

	flag = __ffs(status & ien);
	reg = STM32_MDMA_CIFCR(chan->id);

	switch (1 << flag) {
	case STM32_MDMA_CISR_TEIF:
		id = chan->id;
		status = readl_relaxed(dmadev->base + STM32_MDMA_CESR(id));
		dev_err(chan2dev(chan), "Transfer Err: stat=0x%08x\n", status);
		stm32_mdma_set_bits(dmadev, reg, STM32_MDMA_CIFCR_CTEIF);
		break;

	case STM32_MDMA_CISR_CTCIF:
		stm32_mdma_set_bits(dmadev, reg, STM32_MDMA_CIFCR_CCTCIF);
		stm32_mdma_xfer_end(chan);
		break;

	case STM32_MDMA_CISR_BRTIF:
		stm32_mdma_set_bits(dmadev, reg, STM32_MDMA_CIFCR_CBRTIF);
		break;

	case STM32_MDMA_CISR_BTIF:
		stm32_mdma_set_bits(dmadev, reg, STM32_MDMA_CIFCR_CBTIF);
		chan->curr_hwdesc++;
		if (chan->desc && chan->desc->cyclic) {
			if (chan->curr_hwdesc == chan->desc->count)
				chan->curr_hwdesc = 0;
			vchan_cyclic_callback(&chan->desc->vdesc);
		}
		break;

	case STM32_MDMA_CISR_TCIF:
		stm32_mdma_set_bits(dmadev, reg, STM32_MDMA_CIFCR_CLTCIF);
		break;

	default:
		dev_err(chan2dev(chan), "it %d unhandled (status=0x%04x)\n",
			1 << flag, status);
	}

	spin_unlock(&chan->vchan.lock);

exit:
	return IRQ_HANDLED;
}

static int stm32_mdma_alloc_chan_resources(struct dma_chan *c)
{
	struct stm32_mdma_chan *chan = to_stm32_mdma_chan(c);
	struct stm32_mdma_device *dmadev = stm32_mdma_get_dev(chan);
	int ret;

	chan->desc_pool = dmam_pool_create(dev_name(&c->dev->device),
					   c->device->dev,
					   sizeof(struct stm32_mdma_hwdesc),
					  __alignof__(struct stm32_mdma_hwdesc),
					   0);
	if (!chan->desc_pool) {
		dev_err(chan2dev(chan), "failed to allocate descriptor pool\n");
		return -ENOMEM;
	}

	ret = pm_runtime_get_sync(dmadev->ddev.dev);
	if (ret < 0)
		return ret;

	ret = stm32_mdma_disable_chan(chan);
	if (ret < 0)
		pm_runtime_put(dmadev->ddev.dev);

	return ret;
}

static void stm32_mdma_free_chan_resources(struct dma_chan *c)
{
	struct stm32_mdma_chan *chan = to_stm32_mdma_chan(c);
	struct stm32_mdma_device *dmadev = stm32_mdma_get_dev(chan);
	unsigned long flags;

	dev_dbg(chan2dev(chan), "Freeing channel %d\n", chan->id);

	if (chan->busy) {
		spin_lock_irqsave(&chan->vchan.lock, flags);
		stm32_mdma_stop(chan);
		chan->desc = NULL;
		spin_unlock_irqrestore(&chan->vchan.lock, flags);
	}

	pm_runtime_put(dmadev->ddev.dev);
	vchan_free_chan_resources(to_virt_chan(c));
	dmam_pool_destroy(chan->desc_pool);
	chan->desc_pool = NULL;
}

static struct dma_chan *stm32_mdma_of_xlate(struct of_phandle_args *dma_spec,
					    struct of_dma *ofdma)
{
	struct stm32_mdma_device *dmadev = ofdma->of_dma_data;
	struct stm32_mdma_chan *chan;
	struct dma_chan *c;
	struct stm32_mdma_chan_config config;

	if (dma_spec->args_count < 5) {
		dev_err(mdma2dev(dmadev), "Bad number of args\n");
		return NULL;
	}

	config.request = dma_spec->args[0];
	config.priority_level = dma_spec->args[1];
	config.transfer_config = dma_spec->args[2];
	config.mask_addr = dma_spec->args[3];
	config.mask_data = dma_spec->args[4];

	if (config.request >= dmadev->nr_requests) {
		dev_err(mdma2dev(dmadev), "Bad request line\n");
		return NULL;
	}

	if (config.priority_level > STM32_MDMA_VERY_HIGH_PRIORITY) {
		dev_err(mdma2dev(dmadev), "Priority level not supported\n");
		return NULL;
	}

	c = dma_get_any_slave_channel(&dmadev->ddev);
	if (!c) {
		dev_err(mdma2dev(dmadev), "No more channels available\n");
		return NULL;
	}

	chan = to_stm32_mdma_chan(c);
	chan->chan_config = config;

	return c;
}

static const struct of_device_id stm32_mdma_of_match[] = {
	{ .compatible = "st,stm32h7-mdma", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, stm32_mdma_of_match);

static int stm32_mdma_probe(struct platform_device *pdev)
{
	struct stm32_mdma_chan *chan;
	struct stm32_mdma_device *dmadev;
	struct dma_device *dd;
	struct device_node *of_node;
	struct resource *res;
	u32 nr_channels, nr_requests;
	int i, count, ret;

	of_node = pdev->dev.of_node;
	if (!of_node)
		return -ENODEV;

	ret = device_property_read_u32(&pdev->dev, "dma-channels",
				       &nr_channels);
	if (ret) {
		nr_channels = STM32_MDMA_MAX_CHANNELS;
		dev_warn(&pdev->dev, "MDMA defaulting on %i channels\n",
			 nr_channels);
	}

	ret = device_property_read_u32(&pdev->dev, "dma-requests",
				       &nr_requests);
	if (ret) {
		nr_requests = STM32_MDMA_MAX_REQUESTS;
		dev_warn(&pdev->dev, "MDMA defaulting on %i request lines\n",
			 nr_requests);
	}

	count = device_property_read_u32_array(&pdev->dev, "st,ahb-addr-masks",
					       NULL, 0);
	if (count < 0)
		count = 0;

	dmadev = devm_kzalloc(&pdev->dev, sizeof(*dmadev) + sizeof(u32) * count,
			      GFP_KERNEL);
	if (!dmadev)
		return -ENOMEM;

	dmadev->nr_channels = nr_channels;
	dmadev->nr_requests = nr_requests;
	ret = device_property_read_u32_array(&pdev->dev, "st,ahb-addr-masks",
				       dmadev->ahb_addr_masks,
				       count);
	if (ret)
		return ret;
	dmadev->nr_ahb_addr_masks = count;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dmadev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dmadev->base))
		return PTR_ERR(dmadev->base);

	dmadev->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dmadev->clk)) {
		ret = PTR_ERR(dmadev->clk);
		if (ret == -EPROBE_DEFER)
			dev_info(&pdev->dev, "Missing controller clock\n");
		return ret;
	}

	ret = clk_prepare_enable(dmadev->clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "clk_prep_enable error: %d\n", ret);
		return ret;
	}

	dmadev->rst = devm_reset_control_get(&pdev->dev, NULL);
	if (!IS_ERR(dmadev->rst)) {
		reset_control_assert(dmadev->rst);
		udelay(2);
		reset_control_deassert(dmadev->rst);
	}

	dd = &dmadev->ddev;
	dma_cap_set(DMA_SLAVE, dd->cap_mask);
	dma_cap_set(DMA_PRIVATE, dd->cap_mask);
	dma_cap_set(DMA_CYCLIC, dd->cap_mask);
	dma_cap_set(DMA_MEMCPY, dd->cap_mask);
	dd->device_alloc_chan_resources = stm32_mdma_alloc_chan_resources;
	dd->device_free_chan_resources = stm32_mdma_free_chan_resources;
	dd->device_tx_status = stm32_mdma_tx_status;
	dd->device_issue_pending = stm32_mdma_issue_pending;
	dd->device_prep_slave_sg = stm32_mdma_prep_slave_sg;
	dd->device_prep_dma_cyclic = stm32_mdma_prep_dma_cyclic;
	dd->device_prep_dma_memcpy = stm32_mdma_prep_dma_memcpy;
	dd->device_config = stm32_mdma_slave_config;
	dd->device_pause = stm32_mdma_pause;
	dd->device_resume = stm32_mdma_resume;
	dd->device_terminate_all = stm32_mdma_terminate_all;
	dd->device_synchronize = stm32_mdma_synchronize;
	dd->src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
		BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
		BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) |
		BIT(DMA_SLAVE_BUSWIDTH_8_BYTES);
	dd->dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
		BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
		BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) |
		BIT(DMA_SLAVE_BUSWIDTH_8_BYTES);
	dd->directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV) |
		BIT(DMA_MEM_TO_MEM);
	dd->residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;
	dd->max_burst = STM32_MDMA_MAX_BURST;
	dd->dev = &pdev->dev;
	INIT_LIST_HEAD(&dd->channels);

	for (i = 0; i < dmadev->nr_channels; i++) {
		chan = &dmadev->chan[i];
		chan->id = i;
		chan->vchan.desc_free = stm32_mdma_desc_free;
		vchan_init(&chan->vchan, dd);
	}

	dmadev->irq = platform_get_irq(pdev, 0);
	if (dmadev->irq < 0) {
		dev_err(&pdev->dev, "failed to get IRQ\n");
		return dmadev->irq;
	}

	ret = devm_request_irq(&pdev->dev, dmadev->irq, stm32_mdma_irq_handler,
			       0, dev_name(&pdev->dev), dmadev);
	if (ret) {
		dev_err(&pdev->dev, "failed to request IRQ\n");
		return ret;
	}

	ret = dmaenginem_async_device_register(dd);
	if (ret)
		return ret;

	ret = of_dma_controller_register(of_node, stm32_mdma_of_xlate, dmadev);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"STM32 MDMA DMA OF registration failed %d\n", ret);
		goto err_unregister;
	}

	platform_set_drvdata(pdev, dmadev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_put(&pdev->dev);

	dev_info(&pdev->dev, "STM32 MDMA driver registered\n");

	return 0;

err_unregister:
	return ret;
}

#ifdef CONFIG_PM
static int stm32_mdma_runtime_suspend(struct device *dev)
{
	struct stm32_mdma_device *dmadev = dev_get_drvdata(dev);

	clk_disable_unprepare(dmadev->clk);

	return 0;
}

static int stm32_mdma_runtime_resume(struct device *dev)
{
	struct stm32_mdma_device *dmadev = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(dmadev->clk);
	if (ret) {
		dev_err(dev, "failed to prepare_enable clock\n");
		return ret;
	}

	return 0;
}
#endif

static const struct dev_pm_ops stm32_mdma_pm_ops = {
	SET_RUNTIME_PM_OPS(stm32_mdma_runtime_suspend,
			   stm32_mdma_runtime_resume, NULL)
};

static struct platform_driver stm32_mdma_driver = {
	.probe = stm32_mdma_probe,
	.driver = {
		.name = "stm32-mdma",
		.of_match_table = stm32_mdma_of_match,
		.pm = &stm32_mdma_pm_ops,
	},
};

static int __init stm32_mdma_init(void)
{
	return platform_driver_register(&stm32_mdma_driver);
}

subsys_initcall(stm32_mdma_init);

MODULE_DESCRIPTION("Driver for STM32 MDMA controller");
MODULE_AUTHOR("M'boumba Cedric Madianga <cedric.madianga@gmail.com>");
MODULE_AUTHOR("Pierre-Yves Mordret <pierre-yves.mordret@st.com>");
MODULE_LICENSE("GPL v2");
