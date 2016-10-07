/*
 * Driver for STM32 DMA controller
 *
 * Inspired by dma-jz4740.c and tegra20-apb-dma.c
 *
 * Copyright (C) M'boumba Cedric Madianga 2015
 * Author: M'boumba Cedric Madianga <cedric.madianga@gmail.com>
 *
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "virt-dma.h"

#define STM32_DMA_LISR			0x0000 /* DMA Low Int Status Reg */
#define STM32_DMA_HISR			0x0004 /* DMA High Int Status Reg */
#define STM32_DMA_LIFCR			0x0008 /* DMA Low Int Flag Clear Reg */
#define STM32_DMA_HIFCR			0x000c /* DMA High Int Flag Clear Reg */
#define STM32_DMA_TCI			BIT(5) /* Transfer Complete Interrupt */
#define STM32_DMA_TEI			BIT(3) /* Transfer Error Interrupt */
#define STM32_DMA_DMEI			BIT(2) /* Direct Mode Error Interrupt */
#define STM32_DMA_FEI			BIT(0) /* FIFO Error Interrupt */

/* DMA Stream x Configuration Register */
#define STM32_DMA_SCR(x)		(0x0010 + 0x18 * (x)) /* x = 0..7 */
#define STM32_DMA_SCR_REQ(n)		((n & 0x7) << 25)
#define STM32_DMA_SCR_MBURST_MASK	GENMASK(24, 23)
#define STM32_DMA_SCR_MBURST(n)	        ((n & 0x3) << 23)
#define STM32_DMA_SCR_PBURST_MASK	GENMASK(22, 21)
#define STM32_DMA_SCR_PBURST(n)	        ((n & 0x3) << 21)
#define STM32_DMA_SCR_PL_MASK		GENMASK(17, 16)
#define STM32_DMA_SCR_PL(n)		((n & 0x3) << 16)
#define STM32_DMA_SCR_MSIZE_MASK	GENMASK(14, 13)
#define STM32_DMA_SCR_MSIZE(n)		((n & 0x3) << 13)
#define STM32_DMA_SCR_PSIZE_MASK	GENMASK(12, 11)
#define STM32_DMA_SCR_PSIZE(n)		((n & 0x3) << 11)
#define STM32_DMA_SCR_PSIZE_GET(n)	((n & STM32_DMA_SCR_PSIZE_MASK) >> 11)
#define STM32_DMA_SCR_DIR_MASK		GENMASK(7, 6)
#define STM32_DMA_SCR_DIR(n)		((n & 0x3) << 6)
#define STM32_DMA_SCR_CT		BIT(19) /* Target in double buffer */
#define STM32_DMA_SCR_DBM		BIT(18) /* Double Buffer Mode */
#define STM32_DMA_SCR_PINCOS		BIT(15) /* Peripheral inc offset size */
#define STM32_DMA_SCR_MINC		BIT(10) /* Memory increment mode */
#define STM32_DMA_SCR_PINC		BIT(9) /* Peripheral increment mode */
#define STM32_DMA_SCR_CIRC		BIT(8) /* Circular mode */
#define STM32_DMA_SCR_PFCTRL		BIT(5) /* Peripheral Flow Controller */
#define STM32_DMA_SCR_TCIE		BIT(4) /* Transfer Cplete Int Enable*/
#define STM32_DMA_SCR_TEIE		BIT(2) /* Transfer Error Int Enable */
#define STM32_DMA_SCR_DMEIE		BIT(1) /* Direct Mode Err Int Enable */
#define STM32_DMA_SCR_EN		BIT(0) /* Stream Enable */
#define STM32_DMA_SCR_CFG_MASK		(STM32_DMA_SCR_PINC \
					| STM32_DMA_SCR_MINC \
					| STM32_DMA_SCR_PINCOS \
					| STM32_DMA_SCR_PL_MASK)
#define STM32_DMA_SCR_IRQ_MASK		(STM32_DMA_SCR_TCIE \
					| STM32_DMA_SCR_TEIE \
					| STM32_DMA_SCR_DMEIE)

/* DMA Stream x number of data register */
#define STM32_DMA_SNDTR(x)		(0x0014 + 0x18 * (x))

/* DMA stream peripheral address register */
#define STM32_DMA_SPAR(x)		(0x0018 + 0x18 * (x))

/* DMA stream x memory 0 address register */
#define STM32_DMA_SM0AR(x)		(0x001c + 0x18 * (x))

/* DMA stream x memory 1 address register */
#define STM32_DMA_SM1AR(x)		(0x0020 + 0x18 * (x))

/* DMA stream x FIFO control register */
#define STM32_DMA_SFCR(x)		(0x0024 + 0x18 * (x))
#define STM32_DMA_SFCR_FTH_MASK		GENMASK(1, 0)
#define STM32_DMA_SFCR_FTH(n)		(n & STM32_DMA_SFCR_FTH_MASK)
#define STM32_DMA_SFCR_FEIE		BIT(7) /* FIFO error interrupt enable */
#define STM32_DMA_SFCR_DMDIS		BIT(2) /* Direct mode disable */
#define STM32_DMA_SFCR_MASK		(STM32_DMA_SFCR_FEIE \
					| STM32_DMA_SFCR_DMDIS)

/* DMA direction */
#define STM32_DMA_DEV_TO_MEM		0x00
#define	STM32_DMA_MEM_TO_DEV		0x01
#define	STM32_DMA_MEM_TO_MEM		0x02

/* DMA priority level */
#define STM32_DMA_PRIORITY_LOW		0x00
#define STM32_DMA_PRIORITY_MEDIUM	0x01
#define STM32_DMA_PRIORITY_HIGH		0x02
#define STM32_DMA_PRIORITY_VERY_HIGH	0x03

/* DMA FIFO threshold selection */
#define STM32_DMA_FIFO_THRESHOLD_1QUARTERFULL		0x00
#define STM32_DMA_FIFO_THRESHOLD_HALFFULL		0x01
#define STM32_DMA_FIFO_THRESHOLD_3QUARTERSFULL		0x02
#define STM32_DMA_FIFO_THRESHOLD_FULL			0x03

#define STM32_DMA_MAX_DATA_ITEMS	0xffff
#define STM32_DMA_MAX_CHANNELS		0x08
#define STM32_DMA_MAX_REQUEST_ID	0x08
#define STM32_DMA_MAX_DATA_PARAM	0x03

enum stm32_dma_width {
	STM32_DMA_BYTE,
	STM32_DMA_HALF_WORD,
	STM32_DMA_WORD,
};

enum stm32_dma_burst_size {
	STM32_DMA_BURST_SINGLE,
	STM32_DMA_BURST_INCR4,
	STM32_DMA_BURST_INCR8,
	STM32_DMA_BURST_INCR16,
};

struct stm32_dma_cfg {
	u32 channel_id;
	u32 request_line;
	u32 stream_config;
	u32 threshold;
};

struct stm32_dma_chan_reg {
	u32 dma_lisr;
	u32 dma_hisr;
	u32 dma_lifcr;
	u32 dma_hifcr;
	u32 dma_scr;
	u32 dma_sndtr;
	u32 dma_spar;
	u32 dma_sm0ar;
	u32 dma_sm1ar;
	u32 dma_sfcr;
};

struct stm32_dma_sg_req {
	u32 len;
	struct stm32_dma_chan_reg chan_reg;
};

struct stm32_dma_desc {
	struct virt_dma_desc vdesc;
	bool cyclic;
	u32 num_sgs;
	struct stm32_dma_sg_req sg_req[];
};

struct stm32_dma_chan {
	struct virt_dma_chan vchan;
	bool config_init;
	bool busy;
	u32 id;
	u32 irq;
	struct stm32_dma_desc *desc;
	u32 next_sg;
	struct dma_slave_config	dma_sconfig;
	struct stm32_dma_chan_reg chan_reg;
};

struct stm32_dma_device {
	struct dma_device ddev;
	void __iomem *base;
	struct clk *clk;
	struct reset_control *rst;
	bool mem2mem;
	struct stm32_dma_chan chan[STM32_DMA_MAX_CHANNELS];
};

static struct stm32_dma_device *stm32_dma_get_dev(struct stm32_dma_chan *chan)
{
	return container_of(chan->vchan.chan.device, struct stm32_dma_device,
			    ddev);
}

static struct stm32_dma_chan *to_stm32_dma_chan(struct dma_chan *c)
{
	return container_of(c, struct stm32_dma_chan, vchan.chan);
}

static struct stm32_dma_desc *to_stm32_dma_desc(struct virt_dma_desc *vdesc)
{
	return container_of(vdesc, struct stm32_dma_desc, vdesc);
}

static struct device *chan2dev(struct stm32_dma_chan *chan)
{
	return &chan->vchan.chan.dev->device;
}

static u32 stm32_dma_read(struct stm32_dma_device *dmadev, u32 reg)
{
	return readl_relaxed(dmadev->base + reg);
}

static void stm32_dma_write(struct stm32_dma_device *dmadev, u32 reg, u32 val)
{
	writel_relaxed(val, dmadev->base + reg);
}

static struct stm32_dma_desc *stm32_dma_alloc_desc(u32 num_sgs)
{
	return kzalloc(sizeof(struct stm32_dma_desc) +
		       sizeof(struct stm32_dma_sg_req) * num_sgs, GFP_NOWAIT);
}

static int stm32_dma_get_width(struct stm32_dma_chan *chan,
			       enum dma_slave_buswidth width)
{
	switch (width) {
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
		return STM32_DMA_BYTE;
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
		return STM32_DMA_HALF_WORD;
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		return STM32_DMA_WORD;
	default:
		dev_err(chan2dev(chan), "Dma bus width not supported\n");
		return -EINVAL;
	}
}

static int stm32_dma_get_burst(struct stm32_dma_chan *chan, u32 maxburst)
{
	switch (maxburst) {
	case 0:
	case 1:
		return STM32_DMA_BURST_SINGLE;
	case 4:
		return STM32_DMA_BURST_INCR4;
	case 8:
		return STM32_DMA_BURST_INCR8;
	case 16:
		return STM32_DMA_BURST_INCR16;
	default:
		dev_err(chan2dev(chan), "Dma burst size not supported\n");
		return -EINVAL;
	}
}

static void stm32_dma_set_fifo_config(struct stm32_dma_chan *chan,
				      u32 src_maxburst, u32 dst_maxburst)
{
	chan->chan_reg.dma_sfcr &= ~STM32_DMA_SFCR_MASK;
	chan->chan_reg.dma_scr &= ~STM32_DMA_SCR_DMEIE;

	if ((!src_maxburst) && (!dst_maxburst)) {
		/* Using direct mode */
		chan->chan_reg.dma_scr |= STM32_DMA_SCR_DMEIE;
	} else {
		/* Using FIFO mode */
		chan->chan_reg.dma_sfcr |= STM32_DMA_SFCR_MASK;
	}
}

static int stm32_dma_slave_config(struct dma_chan *c,
				  struct dma_slave_config *config)
{
	struct stm32_dma_chan *chan = to_stm32_dma_chan(c);

	memcpy(&chan->dma_sconfig, config, sizeof(*config));

	chan->config_init = true;

	return 0;
}

static u32 stm32_dma_irq_status(struct stm32_dma_chan *chan)
{
	struct stm32_dma_device *dmadev = stm32_dma_get_dev(chan);
	u32 flags, dma_isr;

	/*
	 * Read "flags" from DMA_xISR register corresponding to the selected
	 * DMA channel at the correct bit offset inside that register.
	 *
	 * If (ch % 4) is 2 or 3, left shift the mask by 16 bits.
	 * If (ch % 4) is 1 or 3, additionally left shift the mask by 6 bits.
	 */

	if (chan->id & 4)
		dma_isr = stm32_dma_read(dmadev, STM32_DMA_HISR);
	else
		dma_isr = stm32_dma_read(dmadev, STM32_DMA_LISR);

	flags = dma_isr >> (((chan->id & 2) << 3) | ((chan->id & 1) * 6));

	return flags;
}

static void stm32_dma_irq_clear(struct stm32_dma_chan *chan, u32 flags)
{
	struct stm32_dma_device *dmadev = stm32_dma_get_dev(chan);
	u32 dma_ifcr;

	/*
	 * Write "flags" to the DMA_xIFCR register corresponding to the selected
	 * DMA channel at the correct bit offset inside that register.
	 *
	 * If (ch % 4) is 2 or 3, left shift the mask by 16 bits.
	 * If (ch % 4) is 1 or 3, additionally left shift the mask by 6 bits.
	 */
	dma_ifcr = flags << (((chan->id & 2) << 3) | ((chan->id & 1) * 6));

	if (chan->id & 4)
		stm32_dma_write(dmadev, STM32_DMA_HIFCR, dma_ifcr);
	else
		stm32_dma_write(dmadev, STM32_DMA_LIFCR, dma_ifcr);
}

static int stm32_dma_disable_chan(struct stm32_dma_chan *chan)
{
	struct stm32_dma_device *dmadev = stm32_dma_get_dev(chan);
	unsigned long timeout = jiffies + msecs_to_jiffies(5000);
	u32 dma_scr, id;

	id = chan->id;
	dma_scr = stm32_dma_read(dmadev, STM32_DMA_SCR(id));

	if (dma_scr & STM32_DMA_SCR_EN) {
		dma_scr &= ~STM32_DMA_SCR_EN;
		stm32_dma_write(dmadev, STM32_DMA_SCR(id), dma_scr);

		do {
			dma_scr = stm32_dma_read(dmadev, STM32_DMA_SCR(id));
			dma_scr &= STM32_DMA_SCR_EN;
			if (!dma_scr)
				break;

			if (time_after_eq(jiffies, timeout)) {
				dev_err(chan2dev(chan), "%s: timeout!\n",
					__func__);
				return -EBUSY;
			}
			cond_resched();
		} while (1);
	}

	return 0;
}

static void stm32_dma_stop(struct stm32_dma_chan *chan)
{
	struct stm32_dma_device *dmadev = stm32_dma_get_dev(chan);
	u32 dma_scr, dma_sfcr, status;
	int ret;

	/* Disable interrupts */
	dma_scr = stm32_dma_read(dmadev, STM32_DMA_SCR(chan->id));
	dma_scr &= ~STM32_DMA_SCR_IRQ_MASK;
	stm32_dma_write(dmadev, STM32_DMA_SCR(chan->id), dma_scr);
	dma_sfcr = stm32_dma_read(dmadev, STM32_DMA_SFCR(chan->id));
	dma_sfcr &= ~STM32_DMA_SFCR_FEIE;
	stm32_dma_write(dmadev, STM32_DMA_SFCR(chan->id), dma_sfcr);

	/* Disable DMA */
	ret = stm32_dma_disable_chan(chan);
	if (ret < 0)
		return;

	/* Clear interrupt status if it is there */
	status = stm32_dma_irq_status(chan);
	if (status) {
		dev_dbg(chan2dev(chan), "%s(): clearing interrupt: 0x%08x\n",
			__func__, status);
		stm32_dma_irq_clear(chan, status);
	}

	chan->busy = false;
}

static int stm32_dma_terminate_all(struct dma_chan *c)
{
	struct stm32_dma_chan *chan = to_stm32_dma_chan(c);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&chan->vchan.lock, flags);

	if (chan->busy) {
		stm32_dma_stop(chan);
		chan->desc = NULL;
	}

	vchan_get_all_descriptors(&chan->vchan, &head);
	spin_unlock_irqrestore(&chan->vchan.lock, flags);
	vchan_dma_desc_free_list(&chan->vchan, &head);

	return 0;
}

static void stm32_dma_dump_reg(struct stm32_dma_chan *chan)
{
	struct stm32_dma_device *dmadev = stm32_dma_get_dev(chan);
	u32 scr = stm32_dma_read(dmadev, STM32_DMA_SCR(chan->id));
	u32 ndtr = stm32_dma_read(dmadev, STM32_DMA_SNDTR(chan->id));
	u32 spar = stm32_dma_read(dmadev, STM32_DMA_SPAR(chan->id));
	u32 sm0ar = stm32_dma_read(dmadev, STM32_DMA_SM0AR(chan->id));
	u32 sm1ar = stm32_dma_read(dmadev, STM32_DMA_SM1AR(chan->id));
	u32 sfcr = stm32_dma_read(dmadev, STM32_DMA_SFCR(chan->id));

	dev_dbg(chan2dev(chan), "SCR:   0x%08x\n", scr);
	dev_dbg(chan2dev(chan), "NDTR:  0x%08x\n", ndtr);
	dev_dbg(chan2dev(chan), "SPAR:  0x%08x\n", spar);
	dev_dbg(chan2dev(chan), "SM0AR: 0x%08x\n", sm0ar);
	dev_dbg(chan2dev(chan), "SM1AR: 0x%08x\n", sm1ar);
	dev_dbg(chan2dev(chan), "SFCR:  0x%08x\n", sfcr);
}

static int stm32_dma_start_transfer(struct stm32_dma_chan *chan)
{
	struct stm32_dma_device *dmadev = stm32_dma_get_dev(chan);
	struct virt_dma_desc *vdesc;
	struct stm32_dma_sg_req *sg_req;
	struct stm32_dma_chan_reg *reg;
	u32 status;
	int ret;

	ret = stm32_dma_disable_chan(chan);
	if (ret < 0)
		return ret;

	if (!chan->desc) {
		vdesc = vchan_next_desc(&chan->vchan);
		if (!vdesc)
			return -EPERM;

		chan->desc = to_stm32_dma_desc(vdesc);
		chan->next_sg = 0;
	}

	if (chan->next_sg == chan->desc->num_sgs)
		chan->next_sg = 0;

	sg_req = &chan->desc->sg_req[chan->next_sg];
	reg = &sg_req->chan_reg;

	stm32_dma_write(dmadev, STM32_DMA_SCR(chan->id), reg->dma_scr);
	stm32_dma_write(dmadev, STM32_DMA_SPAR(chan->id), reg->dma_spar);
	stm32_dma_write(dmadev, STM32_DMA_SM0AR(chan->id), reg->dma_sm0ar);
	stm32_dma_write(dmadev, STM32_DMA_SFCR(chan->id), reg->dma_sfcr);
	stm32_dma_write(dmadev, STM32_DMA_SM1AR(chan->id), reg->dma_sm1ar);
	stm32_dma_write(dmadev, STM32_DMA_SNDTR(chan->id), reg->dma_sndtr);

	chan->next_sg++;

	/* Clear interrupt status if it is there */
	status = stm32_dma_irq_status(chan);
	if (status)
		stm32_dma_irq_clear(chan, status);

	stm32_dma_dump_reg(chan);

	/* Start DMA */
	reg->dma_scr |= STM32_DMA_SCR_EN;
	stm32_dma_write(dmadev, STM32_DMA_SCR(chan->id), reg->dma_scr);

	chan->busy = true;

	return 0;
}

static void stm32_dma_configure_next_sg(struct stm32_dma_chan *chan)
{
	struct stm32_dma_device *dmadev = stm32_dma_get_dev(chan);
	struct stm32_dma_sg_req *sg_req;
	u32 dma_scr, dma_sm0ar, dma_sm1ar, id;

	id = chan->id;
	dma_scr = stm32_dma_read(dmadev, STM32_DMA_SCR(id));

	if (dma_scr & STM32_DMA_SCR_DBM) {
		if (chan->next_sg == chan->desc->num_sgs)
			chan->next_sg = 0;

		sg_req = &chan->desc->sg_req[chan->next_sg];

		if (dma_scr & STM32_DMA_SCR_CT) {
			dma_sm0ar = sg_req->chan_reg.dma_sm0ar;
			stm32_dma_write(dmadev, STM32_DMA_SM0AR(id), dma_sm0ar);
			dev_dbg(chan2dev(chan), "CT=1 <=> SM0AR: 0x%08x\n",
				stm32_dma_read(dmadev, STM32_DMA_SM0AR(id)));
		} else {
			dma_sm1ar = sg_req->chan_reg.dma_sm1ar;
			stm32_dma_write(dmadev, STM32_DMA_SM1AR(id), dma_sm1ar);
			dev_dbg(chan2dev(chan), "CT=0 <=> SM1AR: 0x%08x\n",
				stm32_dma_read(dmadev, STM32_DMA_SM1AR(id)));
		}

		chan->next_sg++;
	}
}

static void stm32_dma_handle_chan_done(struct stm32_dma_chan *chan)
{
	if (chan->desc) {
		if (chan->desc->cyclic) {
			vchan_cyclic_callback(&chan->desc->vdesc);
			stm32_dma_configure_next_sg(chan);
		} else {
			chan->busy = false;
			if (chan->next_sg == chan->desc->num_sgs) {
				list_del(&chan->desc->vdesc.node);
				vchan_cookie_complete(&chan->desc->vdesc);
				chan->desc = NULL;
			}
			stm32_dma_start_transfer(chan);
		}
	}
}

static irqreturn_t stm32_dma_chan_irq(int irq, void *devid)
{
	struct stm32_dma_chan *chan = devid;
	struct stm32_dma_device *dmadev = stm32_dma_get_dev(chan);
	u32 status, scr, sfcr;

	spin_lock(&chan->vchan.lock);

	status = stm32_dma_irq_status(chan);
	scr = stm32_dma_read(dmadev, STM32_DMA_SCR(chan->id));
	sfcr = stm32_dma_read(dmadev, STM32_DMA_SFCR(chan->id));

	if ((status & STM32_DMA_TCI) && (scr & STM32_DMA_SCR_TCIE)) {
		stm32_dma_irq_clear(chan, STM32_DMA_TCI);
		stm32_dma_handle_chan_done(chan);

	} else {
		stm32_dma_irq_clear(chan, status);
		dev_err(chan2dev(chan), "DMA error: status=0x%08x\n", status);
	}

	spin_unlock(&chan->vchan.lock);

	return IRQ_HANDLED;
}

static void stm32_dma_issue_pending(struct dma_chan *c)
{
	struct stm32_dma_chan *chan = to_stm32_dma_chan(c);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&chan->vchan.lock, flags);
	if (!chan->busy) {
		if (vchan_issue_pending(&chan->vchan) && !chan->desc) {
			ret = stm32_dma_start_transfer(chan);
			if ((!ret) && (chan->desc->cyclic))
				stm32_dma_configure_next_sg(chan);
		}
	}
	spin_unlock_irqrestore(&chan->vchan.lock, flags);
}

static int stm32_dma_set_xfer_param(struct stm32_dma_chan *chan,
				    enum dma_transfer_direction direction,
				    enum dma_slave_buswidth *buswidth)
{
	enum dma_slave_buswidth src_addr_width, dst_addr_width;
	int src_bus_width, dst_bus_width;
	int src_burst_size, dst_burst_size;
	u32 src_maxburst, dst_maxburst;
	dma_addr_t src_addr, dst_addr;
	u32 dma_scr = 0;

	src_addr_width = chan->dma_sconfig.src_addr_width;
	dst_addr_width = chan->dma_sconfig.dst_addr_width;
	src_maxburst = chan->dma_sconfig.src_maxburst;
	dst_maxburst = chan->dma_sconfig.dst_maxburst;
	src_addr = chan->dma_sconfig.src_addr;
	dst_addr = chan->dma_sconfig.dst_addr;

	switch (direction) {
	case DMA_MEM_TO_DEV:
		dst_bus_width = stm32_dma_get_width(chan, dst_addr_width);
		if (dst_bus_width < 0)
			return dst_bus_width;

		dst_burst_size = stm32_dma_get_burst(chan, dst_maxburst);
		if (dst_burst_size < 0)
			return dst_burst_size;

		if (!src_addr_width)
			src_addr_width = dst_addr_width;

		src_bus_width = stm32_dma_get_width(chan, src_addr_width);
		if (src_bus_width < 0)
			return src_bus_width;

		src_burst_size = stm32_dma_get_burst(chan, src_maxburst);
		if (src_burst_size < 0)
			return src_burst_size;

		dma_scr = STM32_DMA_SCR_DIR(STM32_DMA_MEM_TO_DEV) |
			STM32_DMA_SCR_PSIZE(dst_bus_width) |
			STM32_DMA_SCR_MSIZE(src_bus_width) |
			STM32_DMA_SCR_PBURST(dst_burst_size) |
			STM32_DMA_SCR_MBURST(src_burst_size);

		chan->chan_reg.dma_spar = chan->dma_sconfig.dst_addr;
		*buswidth = dst_addr_width;
		break;

	case DMA_DEV_TO_MEM:
		src_bus_width = stm32_dma_get_width(chan, src_addr_width);
		if (src_bus_width < 0)
			return src_bus_width;

		src_burst_size = stm32_dma_get_burst(chan, src_maxburst);
		if (src_burst_size < 0)
			return src_burst_size;

		if (!dst_addr_width)
			dst_addr_width = src_addr_width;

		dst_bus_width = stm32_dma_get_width(chan, dst_addr_width);
		if (dst_bus_width < 0)
			return dst_bus_width;

		dst_burst_size = stm32_dma_get_burst(chan, dst_maxburst);
		if (dst_burst_size < 0)
			return dst_burst_size;

		dma_scr = STM32_DMA_SCR_DIR(STM32_DMA_DEV_TO_MEM) |
			STM32_DMA_SCR_PSIZE(src_bus_width) |
			STM32_DMA_SCR_MSIZE(dst_bus_width) |
			STM32_DMA_SCR_PBURST(src_burst_size) |
			STM32_DMA_SCR_MBURST(dst_burst_size);

		chan->chan_reg.dma_spar = chan->dma_sconfig.src_addr;
		*buswidth = chan->dma_sconfig.src_addr_width;
		break;

	default:
		dev_err(chan2dev(chan), "Dma direction is not supported\n");
		return -EINVAL;
	}

	stm32_dma_set_fifo_config(chan, src_maxburst, dst_maxburst);

	chan->chan_reg.dma_scr &= ~(STM32_DMA_SCR_DIR_MASK |
			STM32_DMA_SCR_PSIZE_MASK | STM32_DMA_SCR_MSIZE_MASK |
			STM32_DMA_SCR_PBURST_MASK | STM32_DMA_SCR_MBURST_MASK);
	chan->chan_reg.dma_scr |= dma_scr;

	return 0;
}

static void stm32_dma_clear_reg(struct stm32_dma_chan_reg *regs)
{
	memset(regs, 0, sizeof(struct stm32_dma_chan_reg));
}

static struct dma_async_tx_descriptor *stm32_dma_prep_slave_sg(
	struct dma_chan *c, struct scatterlist *sgl,
	u32 sg_len, enum dma_transfer_direction direction,
	unsigned long flags, void *context)
{
	struct stm32_dma_chan *chan = to_stm32_dma_chan(c);
	struct stm32_dma_desc *desc;
	struct scatterlist *sg;
	enum dma_slave_buswidth buswidth;
	u32 nb_data_items;
	int i, ret;

	if (!chan->config_init) {
		dev_err(chan2dev(chan), "dma channel is not configured\n");
		return NULL;
	}

	if (sg_len < 1) {
		dev_err(chan2dev(chan), "Invalid segment length %d\n", sg_len);
		return NULL;
	}

	desc = stm32_dma_alloc_desc(sg_len);
	if (!desc)
		return NULL;

	ret = stm32_dma_set_xfer_param(chan, direction, &buswidth);
	if (ret < 0)
		goto err;

	/* Set peripheral flow controller */
	if (chan->dma_sconfig.device_fc)
		chan->chan_reg.dma_scr |= STM32_DMA_SCR_PFCTRL;
	else
		chan->chan_reg.dma_scr &= ~STM32_DMA_SCR_PFCTRL;

	for_each_sg(sgl, sg, sg_len, i) {
		desc->sg_req[i].len = sg_dma_len(sg);

		nb_data_items = desc->sg_req[i].len / buswidth;
		if (nb_data_items > STM32_DMA_MAX_DATA_ITEMS) {
			dev_err(chan2dev(chan), "nb items not supported\n");
			goto err;
		}

		stm32_dma_clear_reg(&desc->sg_req[i].chan_reg);
		desc->sg_req[i].chan_reg.dma_scr = chan->chan_reg.dma_scr;
		desc->sg_req[i].chan_reg.dma_sfcr = chan->chan_reg.dma_sfcr;
		desc->sg_req[i].chan_reg.dma_spar = chan->chan_reg.dma_spar;
		desc->sg_req[i].chan_reg.dma_sm0ar = sg_dma_address(sg);
		desc->sg_req[i].chan_reg.dma_sm1ar = sg_dma_address(sg);
		desc->sg_req[i].chan_reg.dma_sndtr = nb_data_items;
	}

	desc->num_sgs = sg_len;
	desc->cyclic = false;

	return vchan_tx_prep(&chan->vchan, &desc->vdesc, flags);

err:
	kfree(desc);
	return NULL;
}

static struct dma_async_tx_descriptor *stm32_dma_prep_dma_cyclic(
	struct dma_chan *c, dma_addr_t buf_addr, size_t buf_len,
	size_t period_len, enum dma_transfer_direction direction,
	unsigned long flags)
{
	struct stm32_dma_chan *chan = to_stm32_dma_chan(c);
	struct stm32_dma_desc *desc;
	enum dma_slave_buswidth buswidth;
	u32 num_periods, nb_data_items;
	int i, ret;

	if (!buf_len || !period_len) {
		dev_err(chan2dev(chan), "Invalid buffer/period len\n");
		return NULL;
	}

	if (!chan->config_init) {
		dev_err(chan2dev(chan), "dma channel is not configured\n");
		return NULL;
	}

	if (buf_len % period_len) {
		dev_err(chan2dev(chan), "buf_len not multiple of period_len\n");
		return NULL;
	}

	/*
	 * We allow to take more number of requests till DMA is
	 * not started. The driver will loop over all requests.
	 * Once DMA is started then new requests can be queued only after
	 * terminating the DMA.
	 */
	if (chan->busy) {
		dev_err(chan2dev(chan), "Request not allowed when dma busy\n");
		return NULL;
	}

	ret = stm32_dma_set_xfer_param(chan, direction, &buswidth);
	if (ret < 0)
		return NULL;

	nb_data_items = period_len / buswidth;
	if (nb_data_items > STM32_DMA_MAX_DATA_ITEMS) {
		dev_err(chan2dev(chan), "number of items not supported\n");
		return NULL;
	}

	/*  Enable Circular mode or double buffer mode */
	if (buf_len == period_len)
		chan->chan_reg.dma_scr |= STM32_DMA_SCR_CIRC;
	else
		chan->chan_reg.dma_scr |= STM32_DMA_SCR_DBM;

	/* Clear periph ctrl if client set it */
	chan->chan_reg.dma_scr &= ~STM32_DMA_SCR_PFCTRL;

	num_periods = buf_len / period_len;

	desc = stm32_dma_alloc_desc(num_periods);
	if (!desc)
		return NULL;

	for (i = 0; i < num_periods; i++) {
		desc->sg_req[i].len = period_len;

		stm32_dma_clear_reg(&desc->sg_req[i].chan_reg);
		desc->sg_req[i].chan_reg.dma_scr = chan->chan_reg.dma_scr;
		desc->sg_req[i].chan_reg.dma_sfcr = chan->chan_reg.dma_sfcr;
		desc->sg_req[i].chan_reg.dma_spar = chan->chan_reg.dma_spar;
		desc->sg_req[i].chan_reg.dma_sm0ar = buf_addr;
		desc->sg_req[i].chan_reg.dma_sm1ar = buf_addr;
		desc->sg_req[i].chan_reg.dma_sndtr = nb_data_items;
		buf_addr += period_len;
	}

	desc->num_sgs = num_periods;
	desc->cyclic = true;

	return vchan_tx_prep(&chan->vchan, &desc->vdesc, flags);
}

static struct dma_async_tx_descriptor *stm32_dma_prep_dma_memcpy(
	struct dma_chan *c, dma_addr_t dest,
	dma_addr_t src, size_t len, unsigned long flags)
{
	struct stm32_dma_chan *chan = to_stm32_dma_chan(c);
	u32 num_sgs;
	struct stm32_dma_desc *desc;
	size_t xfer_count, offset;
	int i;

	num_sgs = DIV_ROUND_UP(len, STM32_DMA_MAX_DATA_ITEMS);
	desc = stm32_dma_alloc_desc(num_sgs);
	if (!desc)
		return NULL;

	for (offset = 0, i = 0; offset < len; offset += xfer_count, i++) {
		xfer_count = min_t(size_t, len - offset,
				   STM32_DMA_MAX_DATA_ITEMS);

		desc->sg_req[i].len = xfer_count;

		stm32_dma_clear_reg(&desc->sg_req[i].chan_reg);
		desc->sg_req[i].chan_reg.dma_scr =
			STM32_DMA_SCR_DIR(STM32_DMA_MEM_TO_MEM) |
			STM32_DMA_SCR_MINC |
			STM32_DMA_SCR_PINC |
			STM32_DMA_SCR_TCIE |
			STM32_DMA_SCR_TEIE;
		desc->sg_req[i].chan_reg.dma_sfcr = STM32_DMA_SFCR_DMDIS |
			STM32_DMA_SFCR_FTH(STM32_DMA_FIFO_THRESHOLD_FULL) |
			STM32_DMA_SFCR_FEIE;
		desc->sg_req[i].chan_reg.dma_spar = src + offset;
		desc->sg_req[i].chan_reg.dma_sm0ar = dest + offset;
		desc->sg_req[i].chan_reg.dma_sndtr = xfer_count;
	}

	desc->num_sgs = num_sgs;
	desc->cyclic = false;

	return vchan_tx_prep(&chan->vchan, &desc->vdesc, flags);
}

static size_t stm32_dma_desc_residue(struct stm32_dma_chan *chan,
				     struct stm32_dma_desc *desc,
				     u32 next_sg)
{
	struct stm32_dma_device *dmadev = stm32_dma_get_dev(chan);
	u32 dma_scr, width, residue, count;
	int i;

	residue = 0;

	for (i = next_sg; i < desc->num_sgs; i++)
		residue += desc->sg_req[i].len;

	if (next_sg != 0) {
		dma_scr = stm32_dma_read(dmadev, STM32_DMA_SCR(chan->id));
		width = STM32_DMA_SCR_PSIZE_GET(dma_scr);
		count = stm32_dma_read(dmadev, STM32_DMA_SNDTR(chan->id));

		residue += count << width;
	}

	return residue;
}

static enum dma_status stm32_dma_tx_status(struct dma_chan *c,
					   dma_cookie_t cookie,
					   struct dma_tx_state *state)
{
	struct stm32_dma_chan *chan = to_stm32_dma_chan(c);
	struct virt_dma_desc *vdesc;
	enum dma_status status;
	unsigned long flags;
	u32 residue;

	status = dma_cookie_status(c, cookie, state);
	if ((status == DMA_COMPLETE) || (!state))
		return status;

	spin_lock_irqsave(&chan->vchan.lock, flags);
	vdesc = vchan_find_desc(&chan->vchan, cookie);
	if (cookie == chan->desc->vdesc.tx.cookie) {
		residue = stm32_dma_desc_residue(chan, chan->desc,
						 chan->next_sg);
	} else if (vdesc) {
		residue = stm32_dma_desc_residue(chan,
						 to_stm32_dma_desc(vdesc), 0);
	} else {
		residue = 0;
	}

	dma_set_residue(state, residue);

	spin_unlock_irqrestore(&chan->vchan.lock, flags);

	return status;
}

static int stm32_dma_alloc_chan_resources(struct dma_chan *c)
{
	struct stm32_dma_chan *chan = to_stm32_dma_chan(c);
	struct stm32_dma_device *dmadev = stm32_dma_get_dev(chan);
	int ret;

	chan->config_init = false;
	ret = clk_prepare_enable(dmadev->clk);
	if (ret < 0) {
		dev_err(chan2dev(chan), "clk_prepare_enable failed: %d\n", ret);
		return ret;
	}

	ret = stm32_dma_disable_chan(chan);
	if (ret < 0)
		clk_disable_unprepare(dmadev->clk);

	return ret;
}

static void stm32_dma_free_chan_resources(struct dma_chan *c)
{
	struct stm32_dma_chan *chan = to_stm32_dma_chan(c);
	struct stm32_dma_device *dmadev = stm32_dma_get_dev(chan);
	unsigned long flags;

	dev_dbg(chan2dev(chan), "Freeing channel %d\n", chan->id);

	if (chan->busy) {
		spin_lock_irqsave(&chan->vchan.lock, flags);
		stm32_dma_stop(chan);
		chan->desc = NULL;
		spin_unlock_irqrestore(&chan->vchan.lock, flags);
	}

	clk_disable_unprepare(dmadev->clk);

	vchan_free_chan_resources(to_virt_chan(c));
}

static void stm32_dma_desc_free(struct virt_dma_desc *vdesc)
{
	kfree(container_of(vdesc, struct stm32_dma_desc, vdesc));
}

static void stm32_dma_set_config(struct stm32_dma_chan *chan,
			  struct stm32_dma_cfg *cfg)
{
	stm32_dma_clear_reg(&chan->chan_reg);

	chan->chan_reg.dma_scr = cfg->stream_config & STM32_DMA_SCR_CFG_MASK;
	chan->chan_reg.dma_scr |= STM32_DMA_SCR_REQ(cfg->request_line);

	/* Enable Interrupts  */
	chan->chan_reg.dma_scr |= STM32_DMA_SCR_TEIE | STM32_DMA_SCR_TCIE;

	chan->chan_reg.dma_sfcr = cfg->threshold & STM32_DMA_SFCR_FTH_MASK;
}

static struct dma_chan *stm32_dma_of_xlate(struct of_phandle_args *dma_spec,
					   struct of_dma *ofdma)
{
	struct stm32_dma_device *dmadev = ofdma->of_dma_data;
	struct stm32_dma_cfg cfg;
	struct stm32_dma_chan *chan;
	struct dma_chan *c;

	if (dma_spec->args_count < 3)
		return NULL;

	cfg.channel_id = dma_spec->args[0];
	cfg.request_line = dma_spec->args[1];
	cfg.stream_config = dma_spec->args[2];
	cfg.threshold = 0;

	if ((cfg.channel_id >= STM32_DMA_MAX_CHANNELS) || (cfg.request_line >=
				STM32_DMA_MAX_REQUEST_ID))
		return NULL;

	if (dma_spec->args_count > 3)
		cfg.threshold = dma_spec->args[3];

	chan = &dmadev->chan[cfg.channel_id];

	c = dma_get_slave_channel(&chan->vchan.chan);
	if (c)
		stm32_dma_set_config(chan, &cfg);

	return c;
}

static const struct of_device_id stm32_dma_of_match[] = {
	{ .compatible = "st,stm32-dma", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, stm32_dma_of_match);

static int stm32_dma_probe(struct platform_device *pdev)
{
	struct stm32_dma_chan *chan;
	struct stm32_dma_device *dmadev;
	struct dma_device *dd;
	const struct of_device_id *match;
	struct resource *res;
	int i, ret;

	match = of_match_device(stm32_dma_of_match, &pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "Error: No device match found\n");
		return -ENODEV;
	}

	dmadev = devm_kzalloc(&pdev->dev, sizeof(*dmadev), GFP_KERNEL);
	if (!dmadev)
		return -ENOMEM;

	dd = &dmadev->ddev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dmadev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dmadev->base))
		return PTR_ERR(dmadev->base);

	dmadev->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dmadev->clk)) {
		dev_err(&pdev->dev, "Error: Missing controller clock\n");
		return PTR_ERR(dmadev->clk);
	}

	dmadev->mem2mem = of_property_read_bool(pdev->dev.of_node,
						"st,mem2mem");

	dmadev->rst = devm_reset_control_get(&pdev->dev, NULL);
	if (!IS_ERR(dmadev->rst)) {
		reset_control_assert(dmadev->rst);
		udelay(2);
		reset_control_deassert(dmadev->rst);
	}

	dma_cap_set(DMA_SLAVE, dd->cap_mask);
	dma_cap_set(DMA_PRIVATE, dd->cap_mask);
	dma_cap_set(DMA_CYCLIC, dd->cap_mask);
	dd->device_alloc_chan_resources = stm32_dma_alloc_chan_resources;
	dd->device_free_chan_resources = stm32_dma_free_chan_resources;
	dd->device_tx_status = stm32_dma_tx_status;
	dd->device_issue_pending = stm32_dma_issue_pending;
	dd->device_prep_slave_sg = stm32_dma_prep_slave_sg;
	dd->device_prep_dma_cyclic = stm32_dma_prep_dma_cyclic;
	dd->device_config = stm32_dma_slave_config;
	dd->device_terminate_all = stm32_dma_terminate_all;
	dd->src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
		BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
		BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	dd->dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
		BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
		BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	dd->directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	dd->residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;
	dd->dev = &pdev->dev;
	INIT_LIST_HEAD(&dd->channels);

	if (dmadev->mem2mem) {
		dma_cap_set(DMA_MEMCPY, dd->cap_mask);
		dd->device_prep_dma_memcpy = stm32_dma_prep_dma_memcpy;
		dd->directions |= BIT(DMA_MEM_TO_MEM);
	}

	for (i = 0; i < STM32_DMA_MAX_CHANNELS; i++) {
		chan = &dmadev->chan[i];
		chan->id = i;
		chan->vchan.desc_free = stm32_dma_desc_free;
		vchan_init(&chan->vchan, dd);
	}

	ret = dma_async_device_register(dd);
	if (ret)
		return ret;

	for (i = 0; i < STM32_DMA_MAX_CHANNELS; i++) {
		chan = &dmadev->chan[i];
		res = platform_get_resource(pdev, IORESOURCE_IRQ, i);
		if (!res) {
			ret = -EINVAL;
			dev_err(&pdev->dev, "No irq resource for chan %d\n", i);
			goto err_unregister;
		}
		chan->irq = res->start;
		ret = devm_request_irq(&pdev->dev, chan->irq,
				       stm32_dma_chan_irq, 0,
				       dev_name(chan2dev(chan)), chan);
		if (ret) {
			dev_err(&pdev->dev,
				"request_irq failed with err %d channel %d\n",
				ret, i);
			goto err_unregister;
		}
	}

	ret = of_dma_controller_register(pdev->dev.of_node,
					 stm32_dma_of_xlate, dmadev);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"STM32 DMA DMA OF registration failed %d\n", ret);
		goto err_unregister;
	}

	platform_set_drvdata(pdev, dmadev);

	dev_info(&pdev->dev, "STM32 DMA driver registered\n");

	return 0;

err_unregister:
	dma_async_device_unregister(dd);

	return ret;
}

static struct platform_driver stm32_dma_driver = {
	.driver = {
		.name = "stm32-dma",
		.of_match_table = stm32_dma_of_match,
	},
};

static int __init stm32_dma_init(void)
{
	return platform_driver_probe(&stm32_dma_driver, stm32_dma_probe);
}
subsys_initcall(stm32_dma_init);
