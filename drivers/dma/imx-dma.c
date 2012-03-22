/*
 * drivers/dma/imx-dma.c
 *
 * This file contains a driver for the Freescale i.MX DMA engine
 * found on i.MX1/21/27
 *
 * Copyright 2010 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
 * Copyright 2012 Javier Martin, Vista Silicon <javier.martin@vista-silicon.com>
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/module.h>

#include <asm/irq.h>
#include <mach/dma.h>
#include <mach/hardware.h>

#include "dmaengine.h"
#define IMXDMA_MAX_CHAN_DESCRIPTORS	16
#define IMX_DMA_CHANNELS  16

#define DMA_MODE_READ		0
#define DMA_MODE_WRITE		1
#define DMA_MODE_MASK		1

#define IMX_DMA_LENGTH_LOOP	((unsigned int)-1)
#define IMX_DMA_MEMSIZE_32	(0 << 4)
#define IMX_DMA_MEMSIZE_8	(1 << 4)
#define IMX_DMA_MEMSIZE_16	(2 << 4)
#define IMX_DMA_TYPE_LINEAR	(0 << 10)
#define IMX_DMA_TYPE_2D		(1 << 10)
#define IMX_DMA_TYPE_FIFO	(2 << 10)

#define IMX_DMA_ERR_BURST     (1 << 0)
#define IMX_DMA_ERR_REQUEST   (1 << 1)
#define IMX_DMA_ERR_TRANSFER  (1 << 2)
#define IMX_DMA_ERR_BUFFER    (1 << 3)
#define IMX_DMA_ERR_TIMEOUT   (1 << 4)

#define DMA_DCR     0x00		/* Control Register */
#define DMA_DISR    0x04		/* Interrupt status Register */
#define DMA_DIMR    0x08		/* Interrupt mask Register */
#define DMA_DBTOSR  0x0c		/* Burst timeout status Register */
#define DMA_DRTOSR  0x10		/* Request timeout Register */
#define DMA_DSESR   0x14		/* Transfer Error Status Register */
#define DMA_DBOSR   0x18		/* Buffer overflow status Register */
#define DMA_DBTOCR  0x1c		/* Burst timeout control Register */
#define DMA_WSRA    0x40		/* W-Size Register A */
#define DMA_XSRA    0x44		/* X-Size Register A */
#define DMA_YSRA    0x48		/* Y-Size Register A */
#define DMA_WSRB    0x4c		/* W-Size Register B */
#define DMA_XSRB    0x50		/* X-Size Register B */
#define DMA_YSRB    0x54		/* Y-Size Register B */
#define DMA_SAR(x)  (0x80 + ((x) << 6))	/* Source Address Registers */
#define DMA_DAR(x)  (0x84 + ((x) << 6))	/* Destination Address Registers */
#define DMA_CNTR(x) (0x88 + ((x) << 6))	/* Count Registers */
#define DMA_CCR(x)  (0x8c + ((x) << 6))	/* Control Registers */
#define DMA_RSSR(x) (0x90 + ((x) << 6))	/* Request source select Registers */
#define DMA_BLR(x)  (0x94 + ((x) << 6))	/* Burst length Registers */
#define DMA_RTOR(x) (0x98 + ((x) << 6))	/* Request timeout Registers */
#define DMA_BUCR(x) (0x98 + ((x) << 6))	/* Bus Utilization Registers */
#define DMA_CCNR(x) (0x9C + ((x) << 6))	/* Channel counter Registers */

#define DCR_DRST           (1<<1)
#define DCR_DEN            (1<<0)
#define DBTOCR_EN          (1<<15)
#define DBTOCR_CNT(x)      ((x) & 0x7fff)
#define CNTR_CNT(x)        ((x) & 0xffffff)
#define CCR_ACRPT          (1<<14)
#define CCR_DMOD_LINEAR    (0x0 << 12)
#define CCR_DMOD_2D        (0x1 << 12)
#define CCR_DMOD_FIFO      (0x2 << 12)
#define CCR_DMOD_EOBFIFO   (0x3 << 12)
#define CCR_SMOD_LINEAR    (0x0 << 10)
#define CCR_SMOD_2D        (0x1 << 10)
#define CCR_SMOD_FIFO      (0x2 << 10)
#define CCR_SMOD_EOBFIFO   (0x3 << 10)
#define CCR_MDIR_DEC       (1<<9)
#define CCR_MSEL_B         (1<<8)
#define CCR_DSIZ_32        (0x0 << 6)
#define CCR_DSIZ_8         (0x1 << 6)
#define CCR_DSIZ_16        (0x2 << 6)
#define CCR_SSIZ_32        (0x0 << 4)
#define CCR_SSIZ_8         (0x1 << 4)
#define CCR_SSIZ_16        (0x2 << 4)
#define CCR_REN            (1<<3)
#define CCR_RPT            (1<<2)
#define CCR_FRC            (1<<1)
#define CCR_CEN            (1<<0)
#define RTOR_EN            (1<<15)
#define RTOR_CLK           (1<<14)
#define RTOR_PSC           (1<<13)

enum  imxdma_prep_type {
	IMXDMA_DESC_MEMCPY,
	IMXDMA_DESC_INTERLEAVED,
	IMXDMA_DESC_SLAVE_SG,
	IMXDMA_DESC_CYCLIC,
};

/*
 * struct imxdma_channel_internal - i.MX specific DMA extension
 * @name: name specified by DMA client
 * @irq_handler: client callback for end of transfer
 * @err_handler: client callback for error condition
 * @data: clients context data for callbacks
 * @dma_mode: direction of the transfer %DMA_MODE_READ or %DMA_MODE_WRITE
 * @sg: pointer to the actual read/written chunk for scatter-gather emulation
 * @resbytes: total residual number of bytes to transfer
 *            (it can be lower or same as sum of SG mapped chunk sizes)
 * @sgcount: number of chunks to be read/written
 *
 * Structure is used for IMX DMA processing. It would be probably good
 * @struct dma_struct in the future for external interfacing and use
 * @struct imxdma_channel_internal only as extension to it.
 */

struct imxdma_channel_internal {
	void *data;
	unsigned int dma_mode;
	struct scatterlist *sg;
	unsigned int resbytes;

	int in_use;

	u32 ccr_from_device;
	u32 ccr_to_device;

	struct timer_list watchdog;

	int hw_chaining;
};

struct imxdma_desc {
	struct list_head		node;
	struct dma_async_tx_descriptor	desc;
	enum dma_status			status;
	dma_addr_t			src;
	dma_addr_t			dest;
	size_t				len;
	unsigned int			dmamode;
	enum imxdma_prep_type		type;
	/* For memcpy and interleaved */
	unsigned int			config_port;
	unsigned int			config_mem;
	/* For interleaved transfers */
	unsigned int			x;
	unsigned int			y;
	unsigned int			w;
	/* For slave sg and cyclic */
	struct scatterlist		*sg;
	unsigned int			sgcount;
};

struct imxdma_channel {
	struct imxdma_channel_internal	internal;
	struct imxdma_engine		*imxdma;
	unsigned int			channel;

	struct tasklet_struct		dma_tasklet;
	struct list_head		ld_free;
	struct list_head		ld_queue;
	struct list_head		ld_active;
	int				descs_allocated;
	enum dma_slave_buswidth		word_size;
	dma_addr_t			per_address;
	u32				watermark_level;
	struct dma_chan			chan;
	spinlock_t			lock;
	struct dma_async_tx_descriptor	desc;
	enum dma_status			status;
	int				dma_request;
	struct scatterlist		*sg_list;
};

struct imxdma_engine {
	struct device			*dev;
	struct device_dma_parameters	dma_parms;
	struct dma_device		dma_device;
	struct imxdma_channel		channel[IMX_DMA_CHANNELS];
};

static struct imxdma_channel *to_imxdma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct imxdma_channel, chan);
}

static inline bool imxdma_chan_is_doing_cyclic(struct imxdma_channel *imxdmac)
{
	struct imxdma_desc *desc;

	if (!list_empty(&imxdmac->ld_active)) {
		desc = list_first_entry(&imxdmac->ld_active, struct imxdma_desc,
					node);
		if (desc->type == IMXDMA_DESC_CYCLIC)
			return true;
	}
	return false;
}

/* TODO: put this inside any struct */
static void __iomem *imx_dmav1_baseaddr;
static struct clk *dma_clk;

static void imx_dmav1_writel(unsigned val, unsigned offset)
{
	__raw_writel(val, imx_dmav1_baseaddr + offset);
}

static unsigned imx_dmav1_readl(unsigned offset)
{
	return __raw_readl(imx_dmav1_baseaddr + offset);
}

static int imxdma_hw_chain(struct imxdma_channel_internal *imxdma)
{
	if (cpu_is_mx27())
		return imxdma->hw_chaining;
	else
		return 0;
}

/*
 * imxdma_sg_next - prepare next chunk for scatter-gather DMA emulation
 */
static inline int imxdma_sg_next(struct imxdma_channel *imxdmac, struct scatterlist *sg)
{
	struct imxdma_channel_internal *imxdma = &imxdmac->internal;
	unsigned long now;

	now = min(imxdma->resbytes, sg->length);
	if (imxdma->resbytes != IMX_DMA_LENGTH_LOOP)
		imxdma->resbytes -= now;

	if ((imxdma->dma_mode & DMA_MODE_MASK) == DMA_MODE_READ)
		imx_dmav1_writel(sg->dma_address, DMA_DAR(imxdmac->channel));
	else
		imx_dmav1_writel(sg->dma_address, DMA_SAR(imxdmac->channel));

	imx_dmav1_writel(now, DMA_CNTR(imxdmac->channel));

	pr_debug("imxdma%d: next sg chunk dst 0x%08x, src 0x%08x, "
		"size 0x%08x\n", imxdmac->channel,
		 imx_dmav1_readl(DMA_DAR(imxdmac->channel)),
		 imx_dmav1_readl(DMA_SAR(imxdmac->channel)),
		 imx_dmav1_readl(DMA_CNTR(imxdmac->channel)));

	return now;
}

static int
imxdma_setup_single_hw(struct imxdma_channel *imxdmac, dma_addr_t dma_address,
		     unsigned int dma_length, unsigned int dev_addr,
		     unsigned int dmamode)
{
	int channel = imxdmac->channel;

	imxdmac->internal.sg = NULL;
	imxdmac->internal.dma_mode = dmamode;

	if (!dma_address) {
		printk(KERN_ERR "imxdma%d: imx_dma_setup_single null address\n",
		       channel);
		return -EINVAL;
	}

	if (!dma_length) {
		printk(KERN_ERR "imxdma%d: imx_dma_setup_single zero length\n",
		       channel);
		return -EINVAL;
	}

	if ((dmamode & DMA_MODE_MASK) == DMA_MODE_READ) {
		pr_debug("imxdma%d: %s dma_addressg=0x%08x dma_length=%d "
			"dev_addr=0x%08x for read\n",
			channel, __func__, (unsigned int)dma_address,
			dma_length, dev_addr);

		imx_dmav1_writel(dev_addr, DMA_SAR(channel));
		imx_dmav1_writel(dma_address, DMA_DAR(channel));
		imx_dmav1_writel(imxdmac->internal.ccr_from_device, DMA_CCR(channel));
	} else if ((dmamode & DMA_MODE_MASK) == DMA_MODE_WRITE) {
		pr_debug("imxdma%d: %s dma_addressg=0x%08x dma_length=%d "
			"dev_addr=0x%08x for write\n",
			channel, __func__, (unsigned int)dma_address,
			dma_length, dev_addr);

		imx_dmav1_writel(dma_address, DMA_SAR(channel));
		imx_dmav1_writel(dev_addr, DMA_DAR(channel));
		imx_dmav1_writel(imxdmac->internal.ccr_to_device,
				DMA_CCR(channel));
	} else {
		printk(KERN_ERR "imxdma%d: imx_dma_setup_single bad dmamode\n",
		       channel);
		return -EINVAL;
	}

	imx_dmav1_writel(dma_length, DMA_CNTR(channel));

	return 0;
}

static void imxdma_enable_hw(struct imxdma_channel *imxdmac)
{
	int channel = imxdmac->channel;
	unsigned long flags;

	pr_debug("imxdma%d: imx_dma_enable\n", channel);

	if (imxdmac->internal.in_use)
		return;

	local_irq_save(flags);

	imx_dmav1_writel(1 << channel, DMA_DISR);
	imx_dmav1_writel(imx_dmav1_readl(DMA_DIMR) & ~(1 << channel), DMA_DIMR);
	imx_dmav1_writel(imx_dmav1_readl(DMA_CCR(channel)) | CCR_CEN |
		CCR_ACRPT, DMA_CCR(channel));

	if ((cpu_is_mx21() || cpu_is_mx27()) &&
			imxdmac->internal.sg && imxdma_hw_chain(&imxdmac->internal)) {
		imxdmac->internal.sg = sg_next(imxdmac->internal.sg);
		if (imxdmac->internal.sg) {
			u32 tmp;
			imxdma_sg_next(imxdmac, imxdmac->internal.sg);
			tmp = imx_dmav1_readl(DMA_CCR(channel));
			imx_dmav1_writel(tmp | CCR_RPT | CCR_ACRPT,
				DMA_CCR(channel));
		}
	}
	imxdmac->internal.in_use = 1;

	local_irq_restore(flags);
}

static void imxdma_disable_hw(struct imxdma_channel *imxdmac)
{
	int channel = imxdmac->channel;
	unsigned long flags;

	pr_debug("imxdma%d: imx_dma_disable\n", channel);

	if (imxdma_hw_chain(&imxdmac->internal))
		del_timer(&imxdmac->internal.watchdog);

	local_irq_save(flags);
	imx_dmav1_writel(imx_dmav1_readl(DMA_DIMR) | (1 << channel), DMA_DIMR);
	imx_dmav1_writel(imx_dmav1_readl(DMA_CCR(channel)) & ~CCR_CEN,
			DMA_CCR(channel));
	imx_dmav1_writel(1 << channel, DMA_DISR);
	imxdmac->internal.in_use = 0;
	local_irq_restore(flags);
}

static int
imxdma_config_channel_hw(struct imxdma_channel *imxdmac, unsigned int config_port,
	unsigned int config_mem, unsigned int dmareq, int hw_chaining)
{
	int channel = imxdmac->channel;
	u32 dreq = 0;

	imxdmac->internal.hw_chaining = 0;

	if (hw_chaining) {
		imxdmac->internal.hw_chaining = 1;
		if (!imxdma_hw_chain(&imxdmac->internal))
			return -EINVAL;
	}

	if (dmareq)
		dreq = CCR_REN;

	imxdmac->internal.ccr_from_device = config_port | (config_mem << 2) | dreq;
	imxdmac->internal.ccr_to_device = config_mem | (config_port << 2) | dreq;

	imx_dmav1_writel(dmareq, DMA_RSSR(channel));

	return 0;
}

static int
imxdma_setup_sg_hw(struct imxdma_channel *imxdmac,
		 struct scatterlist *sg, unsigned int sgcount,
		 unsigned int dma_length, unsigned int dev_addr,
		 unsigned int dmamode)
{
	int channel = imxdmac->channel;

	if (imxdmac->internal.in_use)
		return -EBUSY;

	imxdmac->internal.sg = sg;
	imxdmac->internal.dma_mode = dmamode;
	imxdmac->internal.resbytes = dma_length;

	if (!sg || !sgcount) {
		printk(KERN_ERR "imxdma%d: imx_dma_setup_sg empty sg list\n",
		       channel);
		return -EINVAL;
	}

	if (!sg->length) {
		printk(KERN_ERR "imxdma%d: imx_dma_setup_sg zero length\n",
		       channel);
		return -EINVAL;
	}

	if ((dmamode & DMA_MODE_MASK) == DMA_MODE_READ) {
		pr_debug("imxdma%d: %s sg=%p sgcount=%d total length=%d "
			"dev_addr=0x%08x for read\n",
			channel, __func__, sg, sgcount, dma_length, dev_addr);

		imx_dmav1_writel(dev_addr, DMA_SAR(channel));
		imx_dmav1_writel(imxdmac->internal.ccr_from_device, DMA_CCR(channel));
	} else if ((dmamode & DMA_MODE_MASK) == DMA_MODE_WRITE) {
		pr_debug("imxdma%d: %s sg=%p sgcount=%d total length=%d "
			"dev_addr=0x%08x for write\n",
			channel, __func__, sg, sgcount, dma_length, dev_addr);

		imx_dmav1_writel(dev_addr, DMA_DAR(channel));
		imx_dmav1_writel(imxdmac->internal.ccr_to_device, DMA_CCR(channel));
	} else {
		printk(KERN_ERR "imxdma%d: imx_dma_setup_sg bad dmamode\n",
		       channel);
		return -EINVAL;
	}

	imxdma_sg_next(imxdmac, sg);

	return 0;
}

static void imxdma_watchdog(unsigned long data)
{
	struct imxdma_channel *imxdmac = (struct imxdma_channel *)data;
	int channel = imxdmac->channel;

	imx_dmav1_writel(0, DMA_CCR(channel));
	imxdmac->internal.in_use = 0;
	imxdmac->internal.sg = NULL;

	/* Tasklet watchdog error handler */
	tasklet_schedule(&imxdmac->dma_tasklet);
	pr_debug("imxdma%d: watchdog timeout!\n", imxdmac->channel);
}

static irqreturn_t imxdma_err_handler(int irq, void *dev_id)
{
	struct imxdma_engine *imxdma = dev_id;
	struct imxdma_channel_internal *internal;
	unsigned int err_mask;
	int i, disr;
	int errcode;

	disr = imx_dmav1_readl(DMA_DISR);

	err_mask = imx_dmav1_readl(DMA_DBTOSR) |
		   imx_dmav1_readl(DMA_DRTOSR) |
		   imx_dmav1_readl(DMA_DSESR)  |
		   imx_dmav1_readl(DMA_DBOSR);

	if (!err_mask)
		return IRQ_HANDLED;

	imx_dmav1_writel(disr & err_mask, DMA_DISR);

	for (i = 0; i < IMX_DMA_CHANNELS; i++) {
		if (!(err_mask & (1 << i)))
			continue;
		internal = &imxdma->channel[i].internal;
		errcode = 0;

		if (imx_dmav1_readl(DMA_DBTOSR) & (1 << i)) {
			imx_dmav1_writel(1 << i, DMA_DBTOSR);
			errcode |= IMX_DMA_ERR_BURST;
		}
		if (imx_dmav1_readl(DMA_DRTOSR) & (1 << i)) {
			imx_dmav1_writel(1 << i, DMA_DRTOSR);
			errcode |= IMX_DMA_ERR_REQUEST;
		}
		if (imx_dmav1_readl(DMA_DSESR) & (1 << i)) {
			imx_dmav1_writel(1 << i, DMA_DSESR);
			errcode |= IMX_DMA_ERR_TRANSFER;
		}
		if (imx_dmav1_readl(DMA_DBOSR) & (1 << i)) {
			imx_dmav1_writel(1 << i, DMA_DBOSR);
			errcode |= IMX_DMA_ERR_BUFFER;
		}
		/* Tasklet error handler */
		tasklet_schedule(&imxdma->channel[i].dma_tasklet);

		printk(KERN_WARNING
		       "DMA timeout on channel %d -%s%s%s%s\n", i,
		       errcode & IMX_DMA_ERR_BURST ?    " burst" : "",
		       errcode & IMX_DMA_ERR_REQUEST ?  " request" : "",
		       errcode & IMX_DMA_ERR_TRANSFER ? " transfer" : "",
		       errcode & IMX_DMA_ERR_BUFFER ?   " buffer" : "");
	}
	return IRQ_HANDLED;
}

static void dma_irq_handle_channel(struct imxdma_channel *imxdmac)
{
	struct imxdma_channel_internal *imxdma = &imxdmac->internal;
	int chno = imxdmac->channel;

	if (imxdma->sg) {
		u32 tmp;
		imxdma->sg = sg_next(imxdma->sg);

		if (imxdma->sg) {
			imxdma_sg_next(imxdmac, imxdma->sg);

			tmp = imx_dmav1_readl(DMA_CCR(chno));

			if (imxdma_hw_chain(imxdma)) {
				/* FIXME: The timeout should probably be
				 * configurable
				 */
				mod_timer(&imxdma->watchdog,
					jiffies + msecs_to_jiffies(500));

				tmp |= CCR_CEN | CCR_RPT | CCR_ACRPT;
				imx_dmav1_writel(tmp, DMA_CCR(chno));
			} else {
				imx_dmav1_writel(tmp & ~CCR_CEN, DMA_CCR(chno));
				tmp |= CCR_CEN;
			}

			imx_dmav1_writel(tmp, DMA_CCR(chno));

			if (imxdma_chan_is_doing_cyclic(imxdmac))
				/* Tasklet progression */
				tasklet_schedule(&imxdmac->dma_tasklet);

			return;
		}

		if (imxdma_hw_chain(imxdma)) {
			del_timer(&imxdma->watchdog);
			return;
		}
	}

	imx_dmav1_writel(0, DMA_CCR(chno));
	imxdma->in_use = 0;
	/* Tasklet irq */
	tasklet_schedule(&imxdmac->dma_tasklet);
}

static irqreturn_t dma_irq_handler(int irq, void *dev_id)
{
	struct imxdma_engine *imxdma = dev_id;
	struct imxdma_channel_internal *internal;
	int i, disr;

	if (cpu_is_mx21() || cpu_is_mx27())
		imxdma_err_handler(irq, dev_id);

	disr = imx_dmav1_readl(DMA_DISR);

	pr_debug("imxdma: dma_irq_handler called, disr=0x%08x\n",
		     disr);

	imx_dmav1_writel(disr, DMA_DISR);
	for (i = 0; i < IMX_DMA_CHANNELS; i++) {
		if (disr & (1 << i)) {
			internal = &imxdma->channel[i].internal;
			dma_irq_handle_channel(&imxdma->channel[i]);
		}
	}

	return IRQ_HANDLED;
}

static int imxdma_xfer_desc(struct imxdma_desc *d)
{
	struct imxdma_channel *imxdmac = to_imxdma_chan(d->desc.chan);
	int ret;

	/* Configure and enable */
	switch (d->type) {
	case IMXDMA_DESC_MEMCPY:
		ret = imxdma_config_channel_hw(imxdmac,
					  d->config_port, d->config_mem, 0, 0);
		if (ret < 0)
			return ret;
		ret = imxdma_setup_single_hw(imxdmac, d->src,
					   d->len, d->dest, d->dmamode);
		if (ret < 0)
			return ret;
		break;

	/* Cyclic transfer is the same as slave_sg with special sg configuration. */
	case IMXDMA_DESC_CYCLIC:
	case IMXDMA_DESC_SLAVE_SG:
		if (d->dmamode == DMA_MODE_READ)
			ret = imxdma_setup_sg_hw(imxdmac, d->sg,
				       d->sgcount, d->len, d->src, d->dmamode);
		else
			ret = imxdma_setup_sg_hw(imxdmac, d->sg,
				      d->sgcount, d->len, d->dest, d->dmamode);
		if (ret < 0)
			return ret;
		break;
	default:
		return -EINVAL;
	}
	imxdma_enable_hw(imxdmac);
	return 0;
}

static void imxdma_tasklet(unsigned long data)
{
	struct imxdma_channel *imxdmac = (void *)data;
	struct imxdma_engine *imxdma = imxdmac->imxdma;
	struct imxdma_desc *desc;

	spin_lock(&imxdmac->lock);

	if (list_empty(&imxdmac->ld_active)) {
		/* Someone might have called terminate all */
		goto out;
	}
	desc = list_first_entry(&imxdmac->ld_active, struct imxdma_desc, node);

	if (desc->desc.callback)
		desc->desc.callback(desc->desc.callback_param);

	dma_cookie_complete(&desc->desc);

	/* If we are dealing with a cyclic descriptor keep it on ld_active */
	if (imxdma_chan_is_doing_cyclic(imxdmac))
		goto out;

	list_move_tail(imxdmac->ld_active.next, &imxdmac->ld_free);

	if (!list_empty(&imxdmac->ld_queue)) {
		desc = list_first_entry(&imxdmac->ld_queue, struct imxdma_desc,
					node);
		list_move_tail(imxdmac->ld_queue.next, &imxdmac->ld_active);
		if (imxdma_xfer_desc(desc) < 0)
			dev_warn(imxdma->dev, "%s: channel: %d couldn't xfer desc\n",
				 __func__, imxdmac->channel);
	}
out:
	spin_unlock(&imxdmac->lock);
}

static int imxdma_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd,
		unsigned long arg)
{
	struct imxdma_channel *imxdmac = to_imxdma_chan(chan);
	struct dma_slave_config *dmaengine_cfg = (void *)arg;
	int ret;
	unsigned long flags;
	unsigned int mode = 0;

	switch (cmd) {
	case DMA_TERMINATE_ALL:
		imxdma_disable_hw(imxdmac);

		spin_lock_irqsave(&imxdmac->lock, flags);
		list_splice_tail_init(&imxdmac->ld_active, &imxdmac->ld_free);
		list_splice_tail_init(&imxdmac->ld_queue, &imxdmac->ld_free);
		spin_unlock_irqrestore(&imxdmac->lock, flags);
		return 0;
	case DMA_SLAVE_CONFIG:
		if (dmaengine_cfg->direction == DMA_DEV_TO_MEM) {
			imxdmac->per_address = dmaengine_cfg->src_addr;
			imxdmac->watermark_level = dmaengine_cfg->src_maxburst;
			imxdmac->word_size = dmaengine_cfg->src_addr_width;
		} else {
			imxdmac->per_address = dmaengine_cfg->dst_addr;
			imxdmac->watermark_level = dmaengine_cfg->dst_maxburst;
			imxdmac->word_size = dmaengine_cfg->dst_addr_width;
		}

		switch (imxdmac->word_size) {
		case DMA_SLAVE_BUSWIDTH_1_BYTE:
			mode = IMX_DMA_MEMSIZE_8;
			break;
		case DMA_SLAVE_BUSWIDTH_2_BYTES:
			mode = IMX_DMA_MEMSIZE_16;
			break;
		default:
		case DMA_SLAVE_BUSWIDTH_4_BYTES:
			mode = IMX_DMA_MEMSIZE_32;
			break;
		}
		ret = imxdma_config_channel_hw(imxdmac,
				mode | IMX_DMA_TYPE_FIFO,
				IMX_DMA_MEMSIZE_32 | IMX_DMA_TYPE_LINEAR,
				imxdmac->dma_request, 1);

		if (ret)
			return ret;
		/* Set burst length */
		imx_dmav1_writel(imxdmac->watermark_level * imxdmac->word_size,
				 DMA_BLR(imxdmac->channel));

		return 0;
	default:
		return -ENOSYS;
	}

	return -EINVAL;
}

static enum dma_status imxdma_tx_status(struct dma_chan *chan,
					    dma_cookie_t cookie,
					    struct dma_tx_state *txstate)
{
	return dma_cookie_status(chan, cookie, txstate);
}

static dma_cookie_t imxdma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct imxdma_channel *imxdmac = to_imxdma_chan(tx->chan);
	dma_cookie_t cookie;
	unsigned long flags;

	spin_lock_irqsave(&imxdmac->lock, flags);
	cookie = dma_cookie_assign(tx);
	spin_unlock_irqrestore(&imxdmac->lock, flags);

	return cookie;
}

static int imxdma_alloc_chan_resources(struct dma_chan *chan)
{
	struct imxdma_channel *imxdmac = to_imxdma_chan(chan);
	struct imx_dma_data *data = chan->private;

	if (data != NULL)
		imxdmac->dma_request = data->dma_request;

	while (imxdmac->descs_allocated < IMXDMA_MAX_CHAN_DESCRIPTORS) {
		struct imxdma_desc *desc;

		desc = kzalloc(sizeof(*desc), GFP_KERNEL);
		if (!desc)
			break;
		__memzero(&desc->desc, sizeof(struct dma_async_tx_descriptor));
		dma_async_tx_descriptor_init(&desc->desc, chan);
		desc->desc.tx_submit = imxdma_tx_submit;
		/* txd.flags will be overwritten in prep funcs */
		desc->desc.flags = DMA_CTRL_ACK;
		desc->status = DMA_SUCCESS;

		list_add_tail(&desc->node, &imxdmac->ld_free);
		imxdmac->descs_allocated++;
	}

	if (!imxdmac->descs_allocated)
		return -ENOMEM;

	return imxdmac->descs_allocated;
}

static void imxdma_free_chan_resources(struct dma_chan *chan)
{
	struct imxdma_channel *imxdmac = to_imxdma_chan(chan);
	struct imxdma_desc *desc, *_desc;
	unsigned long flags;

	spin_lock_irqsave(&imxdmac->lock, flags);

	imxdma_disable_hw(imxdmac);
	list_splice_tail_init(&imxdmac->ld_active, &imxdmac->ld_free);
	list_splice_tail_init(&imxdmac->ld_queue, &imxdmac->ld_free);

	spin_unlock_irqrestore(&imxdmac->lock, flags);

	list_for_each_entry_safe(desc, _desc, &imxdmac->ld_free, node) {
		kfree(desc);
		imxdmac->descs_allocated--;
	}
	INIT_LIST_HEAD(&imxdmac->ld_free);

	if (imxdmac->sg_list) {
		kfree(imxdmac->sg_list);
		imxdmac->sg_list = NULL;
	}
}

static struct dma_async_tx_descriptor *imxdma_prep_slave_sg(
		struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context)
{
	struct imxdma_channel *imxdmac = to_imxdma_chan(chan);
	struct scatterlist *sg;
	int i, dma_length = 0;
	struct imxdma_desc *desc;

	if (list_empty(&imxdmac->ld_free) ||
	    imxdma_chan_is_doing_cyclic(imxdmac))
		return NULL;

	desc = list_first_entry(&imxdmac->ld_free, struct imxdma_desc, node);

	for_each_sg(sgl, sg, sg_len, i) {
		dma_length += sg->length;
	}

	switch (imxdmac->word_size) {
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		if (sgl->length & 3 || sgl->dma_address & 3)
			return NULL;
		break;
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
		if (sgl->length & 1 || sgl->dma_address & 1)
			return NULL;
		break;
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
		break;
	default:
		return NULL;
	}

	desc->type = IMXDMA_DESC_SLAVE_SG;
	desc->sg = sgl;
	desc->sgcount = sg_len;
	desc->len = dma_length;
	if (direction == DMA_DEV_TO_MEM) {
		desc->dmamode = DMA_MODE_READ;
		desc->src = imxdmac->per_address;
	} else {
		desc->dmamode = DMA_MODE_WRITE;
		desc->dest = imxdmac->per_address;
	}
	desc->desc.callback = NULL;
	desc->desc.callback_param = NULL;

	return &desc->desc;
}

static struct dma_async_tx_descriptor *imxdma_prep_dma_cyclic(
		struct dma_chan *chan, dma_addr_t dma_addr, size_t buf_len,
		size_t period_len, enum dma_transfer_direction direction,
		void *context)
{
	struct imxdma_channel *imxdmac = to_imxdma_chan(chan);
	struct imxdma_engine *imxdma = imxdmac->imxdma;
	struct imxdma_desc *desc;
	int i;
	unsigned int periods = buf_len / period_len;

	dev_dbg(imxdma->dev, "%s channel: %d buf_len=%d period_len=%d\n",
			__func__, imxdmac->channel, buf_len, period_len);

	if (list_empty(&imxdmac->ld_free) ||
	    imxdma_chan_is_doing_cyclic(imxdmac))
		return NULL;

	desc = list_first_entry(&imxdmac->ld_free, struct imxdma_desc, node);

	if (imxdmac->sg_list)
		kfree(imxdmac->sg_list);

	imxdmac->sg_list = kcalloc(periods + 1,
			sizeof(struct scatterlist), GFP_KERNEL);
	if (!imxdmac->sg_list)
		return NULL;

	sg_init_table(imxdmac->sg_list, periods);

	for (i = 0; i < periods; i++) {
		imxdmac->sg_list[i].page_link = 0;
		imxdmac->sg_list[i].offset = 0;
		imxdmac->sg_list[i].dma_address = dma_addr;
		imxdmac->sg_list[i].length = period_len;
		dma_addr += period_len;
	}

	/* close the loop */
	imxdmac->sg_list[periods].offset = 0;
	imxdmac->sg_list[periods].length = 0;
	imxdmac->sg_list[periods].page_link =
		((unsigned long)imxdmac->sg_list | 0x01) & ~0x02;

	desc->type = IMXDMA_DESC_CYCLIC;
	desc->sg = imxdmac->sg_list;
	desc->sgcount = periods;
	desc->len = IMX_DMA_LENGTH_LOOP;
	if (direction == DMA_DEV_TO_MEM) {
		desc->dmamode = DMA_MODE_READ;
		desc->src = imxdmac->per_address;
	} else {
		desc->dmamode = DMA_MODE_WRITE;
		desc->dest = imxdmac->per_address;
	}
	desc->desc.callback = NULL;
	desc->desc.callback_param = NULL;

	return &desc->desc;
}

static struct dma_async_tx_descriptor *imxdma_prep_dma_memcpy(
	struct dma_chan *chan, dma_addr_t dest,
	dma_addr_t src, size_t len, unsigned long flags)
{
	struct imxdma_channel *imxdmac = to_imxdma_chan(chan);
	struct imxdma_engine *imxdma = imxdmac->imxdma;
	struct imxdma_desc *desc;

	dev_dbg(imxdma->dev, "%s channel: %d src=0x%x dst=0x%x len=%d\n",
			__func__, imxdmac->channel, src, dest, len);

	if (list_empty(&imxdmac->ld_free) ||
	    imxdma_chan_is_doing_cyclic(imxdmac))
		return NULL;

	desc = list_first_entry(&imxdmac->ld_free, struct imxdma_desc, node);

	desc->type = IMXDMA_DESC_MEMCPY;
	desc->src = src;
	desc->dest = dest;
	desc->len = len;
	desc->dmamode = DMA_MODE_WRITE;
	desc->config_port = IMX_DMA_MEMSIZE_32 | IMX_DMA_TYPE_LINEAR;
	desc->config_mem = IMX_DMA_MEMSIZE_32 | IMX_DMA_TYPE_LINEAR;
	desc->desc.callback = NULL;
	desc->desc.callback_param = NULL;

	return &desc->desc;
}

static void imxdma_issue_pending(struct dma_chan *chan)
{
	struct imxdma_channel *imxdmac = to_imxdma_chan(chan);
	struct imxdma_engine *imxdma = imxdmac->imxdma;
	struct imxdma_desc *desc;
	unsigned long flags;

	spin_lock_irqsave(&imxdmac->lock, flags);
	if (list_empty(&imxdmac->ld_active) &&
	    !list_empty(&imxdmac->ld_queue)) {
		desc = list_first_entry(&imxdmac->ld_queue,
					struct imxdma_desc, node);

		if (imxdma_xfer_desc(desc) < 0) {
			dev_warn(imxdma->dev,
				 "%s: channel: %d couldn't issue DMA xfer\n",
				 __func__, imxdmac->channel);
		} else {
			list_move_tail(imxdmac->ld_queue.next,
				       &imxdmac->ld_active);
		}
	}
	spin_unlock_irqrestore(&imxdmac->lock, flags);
}

static int __init imxdma_probe(struct platform_device *pdev)
	{
	struct imxdma_engine *imxdma;
	int ret, i;

	if (cpu_is_mx1())
		imx_dmav1_baseaddr = MX1_IO_ADDRESS(MX1_DMA_BASE_ADDR);
	else if (cpu_is_mx21())
		imx_dmav1_baseaddr = MX21_IO_ADDRESS(MX21_DMA_BASE_ADDR);
	else if (cpu_is_mx27())
		imx_dmav1_baseaddr = MX27_IO_ADDRESS(MX27_DMA_BASE_ADDR);
	else
		return 0;

	dma_clk = clk_get(NULL, "dma");
	if (IS_ERR(dma_clk))
		return PTR_ERR(dma_clk);
	clk_enable(dma_clk);

	/* reset DMA module */
	imx_dmav1_writel(DCR_DRST, DMA_DCR);

	if (cpu_is_mx1()) {
		ret = request_irq(MX1_DMA_INT, dma_irq_handler, 0, "DMA", imxdma);
		if (ret) {
			pr_crit("Can't register IRQ for DMA\n");
			return ret;
		}

		ret = request_irq(MX1_DMA_ERR, imxdma_err_handler, 0, "DMA", imxdma);
		if (ret) {
			pr_crit("Can't register ERRIRQ for DMA\n");
			free_irq(MX1_DMA_INT, NULL);
			return ret;
		}
	}

	/* enable DMA module */
	imx_dmav1_writel(DCR_DEN, DMA_DCR);

	/* clear all interrupts */
	imx_dmav1_writel((1 << IMX_DMA_CHANNELS) - 1, DMA_DISR);

	/* disable interrupts */
	imx_dmav1_writel((1 << IMX_DMA_CHANNELS) - 1, DMA_DIMR);

	imxdma = kzalloc(sizeof(*imxdma), GFP_KERNEL);
	if (!imxdma)
		return -ENOMEM;

	INIT_LIST_HEAD(&imxdma->dma_device.channels);

	dma_cap_set(DMA_SLAVE, imxdma->dma_device.cap_mask);
	dma_cap_set(DMA_CYCLIC, imxdma->dma_device.cap_mask);
	dma_cap_set(DMA_MEMCPY, imxdma->dma_device.cap_mask);

	/* Initialize channel parameters */
	for (i = 0; i < IMX_DMA_CHANNELS; i++) {
		struct imxdma_channel *imxdmac = &imxdma->channel[i];
		memset(&imxdmac->internal, 0, sizeof(imxdmac->internal));
		if (cpu_is_mx21() || cpu_is_mx27()) {
			ret = request_irq(MX2x_INT_DMACH0 + i,
					dma_irq_handler, 0, "DMA", imxdma);
			if (ret) {
				pr_crit("Can't register IRQ %d for DMA channel %d\n",
						MX2x_INT_DMACH0 + i, i);
				goto err_init;
			}
			init_timer(&imxdmac->internal.watchdog);
			imxdmac->internal.watchdog.function = &imxdma_watchdog;
			imxdmac->internal.watchdog.data = (unsigned long)imxdmac;
		}

		imxdmac->imxdma = imxdma;
		spin_lock_init(&imxdmac->lock);

		INIT_LIST_HEAD(&imxdmac->ld_queue);
		INIT_LIST_HEAD(&imxdmac->ld_free);
		INIT_LIST_HEAD(&imxdmac->ld_active);

		tasklet_init(&imxdmac->dma_tasklet, imxdma_tasklet,
			     (unsigned long)imxdmac);
		imxdmac->chan.device = &imxdma->dma_device;
		dma_cookie_init(&imxdmac->chan);
		imxdmac->channel = i;

		/* Add the channel to the DMAC list */
		list_add_tail(&imxdmac->chan.device_node,
			      &imxdma->dma_device.channels);
	}

	imxdma->dev = &pdev->dev;
	imxdma->dma_device.dev = &pdev->dev;

	imxdma->dma_device.device_alloc_chan_resources = imxdma_alloc_chan_resources;
	imxdma->dma_device.device_free_chan_resources = imxdma_free_chan_resources;
	imxdma->dma_device.device_tx_status = imxdma_tx_status;
	imxdma->dma_device.device_prep_slave_sg = imxdma_prep_slave_sg;
	imxdma->dma_device.device_prep_dma_cyclic = imxdma_prep_dma_cyclic;
	imxdma->dma_device.device_prep_dma_memcpy = imxdma_prep_dma_memcpy;
	imxdma->dma_device.device_control = imxdma_control;
	imxdma->dma_device.device_issue_pending = imxdma_issue_pending;

	platform_set_drvdata(pdev, imxdma);

	imxdma->dma_device.copy_align = 2; /* 2^2 = 4 bytes alignment */
	imxdma->dma_device.dev->dma_parms = &imxdma->dma_parms;
	dma_set_max_seg_size(imxdma->dma_device.dev, 0xffffff);

	ret = dma_async_device_register(&imxdma->dma_device);
	if (ret) {
		dev_err(&pdev->dev, "unable to register\n");
		goto err_init;
	}

	return 0;

err_init:

	if (cpu_is_mx21() || cpu_is_mx27()) {
		while (--i >= 0)
			free_irq(MX2x_INT_DMACH0 + i, NULL);
	} else if cpu_is_mx1() {
		free_irq(MX1_DMA_INT, NULL);
		free_irq(MX1_DMA_ERR, NULL);
	}

	kfree(imxdma);
	return ret;
}

static int __exit imxdma_remove(struct platform_device *pdev)
{
	struct imxdma_engine *imxdma = platform_get_drvdata(pdev);
	int i;

        dma_async_device_unregister(&imxdma->dma_device);

	if (cpu_is_mx21() || cpu_is_mx27()) {
		for (i = 0; i < IMX_DMA_CHANNELS; i++)
			free_irq(MX2x_INT_DMACH0 + i, NULL);
	} else if cpu_is_mx1() {
		free_irq(MX1_DMA_INT, NULL);
		free_irq(MX1_DMA_ERR, NULL);
	}

        kfree(imxdma);

        return 0;
}

static struct platform_driver imxdma_driver = {
	.driver		= {
		.name	= "imx-dma",
	},
	.remove		= __exit_p(imxdma_remove),
};

static int __init imxdma_module_init(void)
{
	return platform_driver_probe(&imxdma_driver, imxdma_probe);
}
subsys_initcall(imxdma_module_init);

MODULE_AUTHOR("Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>");
MODULE_DESCRIPTION("i.MX dma driver");
MODULE_LICENSE("GPL");
