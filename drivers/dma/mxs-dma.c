// SPDX-License-Identifier: GPL-2.0
//
// Copyright 2011 Freescale Semiconductor, Inc. All Rights Reserved.
//
// Refer to drivers/dma/imx-sdma.c

#include <linux/init.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/dmaengine.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/stmp_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/list.h>
#include <linux/dma/mxs-dma.h>

#include <asm/irq.h>

#include "dmaengine.h"

/*
 * NOTE: The term "PIO" throughout the mxs-dma implementation means
 * PIO mode of mxs apbh-dma and apbx-dma.  With this working mode,
 * dma can program the controller registers of peripheral devices.
 */

#define dma_is_apbh(mxs_dma)	((mxs_dma)->type == MXS_DMA_APBH)
#define apbh_is_old(mxs_dma)	((mxs_dma)->dev_id == IMX23_DMA)

#define HW_APBHX_CTRL0				0x000
#define BM_APBH_CTRL0_APB_BURST8_EN		(1 << 29)
#define BM_APBH_CTRL0_APB_BURST_EN		(1 << 28)
#define BP_APBH_CTRL0_RESET_CHANNEL		16
#define HW_APBHX_CTRL1				0x010
#define HW_APBHX_CTRL2				0x020
#define HW_APBHX_CHANNEL_CTRL			0x030
#define BP_APBHX_CHANNEL_CTRL_RESET_CHANNEL	16
/*
 * The offset of NXTCMDAR register is different per both dma type and version,
 * while stride for each channel is all the same 0x70.
 */
#define HW_APBHX_CHn_NXTCMDAR(d, n) \
	(((dma_is_apbh(d) && apbh_is_old(d)) ? 0x050 : 0x110) + (n) * 0x70)
#define HW_APBHX_CHn_SEMA(d, n) \
	(((dma_is_apbh(d) && apbh_is_old(d)) ? 0x080 : 0x140) + (n) * 0x70)
#define HW_APBHX_CHn_BAR(d, n) \
	(((dma_is_apbh(d) && apbh_is_old(d)) ? 0x070 : 0x130) + (n) * 0x70)
#define HW_APBX_CHn_DEBUG1(d, n) (0x150 + (n) * 0x70)

/*
 * ccw bits definitions
 *
 * COMMAND:		0..1	(2)
 * CHAIN:		2	(1)
 * IRQ:			3	(1)
 * NAND_LOCK:		4	(1) - not implemented
 * NAND_WAIT4READY:	5	(1) - not implemented
 * DEC_SEM:		6	(1)
 * WAIT4END:		7	(1)
 * HALT_ON_TERMINATE:	8	(1)
 * TERMINATE_FLUSH:	9	(1)
 * RESERVED:		10..11	(2)
 * PIO_NUM:		12..15	(4)
 */
#define BP_CCW_COMMAND		0
#define BM_CCW_COMMAND		(3 << 0)
#define CCW_CHAIN		(1 << 2)
#define CCW_IRQ			(1 << 3)
#define CCW_WAIT4RDY		(1 << 5)
#define CCW_DEC_SEM		(1 << 6)
#define CCW_WAIT4END		(1 << 7)
#define CCW_HALT_ON_TERM	(1 << 8)
#define CCW_TERM_FLUSH		(1 << 9)
#define BP_CCW_PIO_NUM		12
#define BM_CCW_PIO_NUM		(0xf << 12)

#define BF_CCW(value, field)	(((value) << BP_CCW_##field) & BM_CCW_##field)

#define MXS_DMA_CMD_NO_XFER	0
#define MXS_DMA_CMD_WRITE	1
#define MXS_DMA_CMD_READ	2
#define MXS_DMA_CMD_DMA_SENSE	3	/* not implemented */

struct mxs_dma_ccw {
	u32		next;
	u16		bits;
	u16		xfer_bytes;
#define MAX_XFER_BYTES	0xff00
	u32		bufaddr;
#define MXS_PIO_WORDS	16
	u32		pio_words[MXS_PIO_WORDS];
};

#define CCW_BLOCK_SIZE	(4 * PAGE_SIZE)
#define NUM_CCW	(int)(CCW_BLOCK_SIZE / sizeof(struct mxs_dma_ccw))

struct mxs_dma_chan {
	struct mxs_dma_engine		*mxs_dma;
	struct dma_chan			chan;
	struct dma_async_tx_descriptor	desc;
	struct tasklet_struct		tasklet;
	unsigned int			chan_irq;
	struct mxs_dma_ccw		*ccw;
	dma_addr_t			ccw_phys;
	int				desc_count;
	enum dma_status			status;
	unsigned int			flags;
	bool				reset;
#define MXS_DMA_SG_LOOP			(1 << 0)
#define MXS_DMA_USE_SEMAPHORE		(1 << 1)
};

#define MXS_DMA_CHANNELS		16
#define MXS_DMA_CHANNELS_MASK		0xffff

enum mxs_dma_devtype {
	MXS_DMA_APBH,
	MXS_DMA_APBX,
};

enum mxs_dma_id {
	IMX23_DMA,
	IMX28_DMA,
};

struct mxs_dma_engine {
	enum mxs_dma_id			dev_id;
	enum mxs_dma_devtype		type;
	void __iomem			*base;
	struct clk			*clk;
	struct dma_device		dma_device;
	struct device_dma_parameters	dma_parms;
	struct mxs_dma_chan		mxs_chans[MXS_DMA_CHANNELS];
	struct platform_device		*pdev;
	unsigned int			nr_channels;
};

struct mxs_dma_type {
	enum mxs_dma_id id;
	enum mxs_dma_devtype type;
};

static struct mxs_dma_type mxs_dma_types[] = {
	{
		.id = IMX23_DMA,
		.type = MXS_DMA_APBH,
	}, {
		.id = IMX23_DMA,
		.type = MXS_DMA_APBX,
	}, {
		.id = IMX28_DMA,
		.type = MXS_DMA_APBH,
	}, {
		.id = IMX28_DMA,
		.type = MXS_DMA_APBX,
	}
};

static const struct platform_device_id mxs_dma_ids[] = {
	{
		.name = "imx23-dma-apbh",
		.driver_data = (kernel_ulong_t) &mxs_dma_types[0],
	}, {
		.name = "imx23-dma-apbx",
		.driver_data = (kernel_ulong_t) &mxs_dma_types[1],
	}, {
		.name = "imx28-dma-apbh",
		.driver_data = (kernel_ulong_t) &mxs_dma_types[2],
	}, {
		.name = "imx28-dma-apbx",
		.driver_data = (kernel_ulong_t) &mxs_dma_types[3],
	}, {
		/* end of list */
	}
};

static const struct of_device_id mxs_dma_dt_ids[] = {
	{ .compatible = "fsl,imx23-dma-apbh", .data = &mxs_dma_ids[0], },
	{ .compatible = "fsl,imx23-dma-apbx", .data = &mxs_dma_ids[1], },
	{ .compatible = "fsl,imx28-dma-apbh", .data = &mxs_dma_ids[2], },
	{ .compatible = "fsl,imx28-dma-apbx", .data = &mxs_dma_ids[3], },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_dma_dt_ids);

static struct mxs_dma_chan *to_mxs_dma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct mxs_dma_chan, chan);
}

static void mxs_dma_reset_chan(struct dma_chan *chan)
{
	struct mxs_dma_chan *mxs_chan = to_mxs_dma_chan(chan);
	struct mxs_dma_engine *mxs_dma = mxs_chan->mxs_dma;
	int chan_id = mxs_chan->chan.chan_id;

	/*
	 * mxs dma channel resets can cause a channel stall. To recover from a
	 * channel stall, we have to reset the whole DMA engine. To avoid this,
	 * we use cyclic DMA with semaphores, that are enhanced in
	 * mxs_dma_int_handler. To reset the channel, we can simply stop writing
	 * into the semaphore counter.
	 */
	if (mxs_chan->flags & MXS_DMA_USE_SEMAPHORE &&
			mxs_chan->flags & MXS_DMA_SG_LOOP) {
		mxs_chan->reset = true;
	} else if (dma_is_apbh(mxs_dma) && apbh_is_old(mxs_dma)) {
		writel(1 << (chan_id + BP_APBH_CTRL0_RESET_CHANNEL),
			mxs_dma->base + HW_APBHX_CTRL0 + STMP_OFFSET_REG_SET);
	} else {
		unsigned long elapsed = 0;
		const unsigned long max_wait = 50000; /* 50ms */
		void __iomem *reg_dbg1 = mxs_dma->base +
				HW_APBX_CHn_DEBUG1(mxs_dma, chan_id);

		/*
		 * On i.MX28 APBX, the DMA channel can stop working if we reset
		 * the channel while it is in READ_FLUSH (0x08) state.
		 * We wait here until we leave the state. Then we trigger the
		 * reset. Waiting a maximum of 50ms, the kernel shouldn't crash
		 * because of this.
		 */
		while ((readl(reg_dbg1) & 0xf) == 0x8 && elapsed < max_wait) {
			udelay(100);
			elapsed += 100;
		}

		if (elapsed >= max_wait)
			dev_err(&mxs_chan->mxs_dma->pdev->dev,
					"Failed waiting for the DMA channel %d to leave state READ_FLUSH, trying to reset channel in READ_FLUSH state now\n",
					chan_id);

		writel(1 << (chan_id + BP_APBHX_CHANNEL_CTRL_RESET_CHANNEL),
			mxs_dma->base + HW_APBHX_CHANNEL_CTRL + STMP_OFFSET_REG_SET);
	}

	mxs_chan->status = DMA_COMPLETE;
}

static void mxs_dma_enable_chan(struct dma_chan *chan)
{
	struct mxs_dma_chan *mxs_chan = to_mxs_dma_chan(chan);
	struct mxs_dma_engine *mxs_dma = mxs_chan->mxs_dma;
	int chan_id = mxs_chan->chan.chan_id;

	/* set cmd_addr up */
	writel(mxs_chan->ccw_phys,
		mxs_dma->base + HW_APBHX_CHn_NXTCMDAR(mxs_dma, chan_id));

	/* write 1 to SEMA to kick off the channel */
	if (mxs_chan->flags & MXS_DMA_USE_SEMAPHORE &&
			mxs_chan->flags & MXS_DMA_SG_LOOP) {
		/* A cyclic DMA consists of at least 2 segments, so initialize
		 * the semaphore with 2 so we have enough time to add 1 to the
		 * semaphore if we need to */
		writel(2, mxs_dma->base + HW_APBHX_CHn_SEMA(mxs_dma, chan_id));
	} else {
		writel(1, mxs_dma->base + HW_APBHX_CHn_SEMA(mxs_dma, chan_id));
	}
	mxs_chan->reset = false;
}

static void mxs_dma_disable_chan(struct dma_chan *chan)
{
	struct mxs_dma_chan *mxs_chan = to_mxs_dma_chan(chan);

	mxs_chan->status = DMA_COMPLETE;
}

static int mxs_dma_pause_chan(struct dma_chan *chan)
{
	struct mxs_dma_chan *mxs_chan = to_mxs_dma_chan(chan);
	struct mxs_dma_engine *mxs_dma = mxs_chan->mxs_dma;
	int chan_id = mxs_chan->chan.chan_id;

	/* freeze the channel */
	if (dma_is_apbh(mxs_dma) && apbh_is_old(mxs_dma))
		writel(1 << chan_id,
			mxs_dma->base + HW_APBHX_CTRL0 + STMP_OFFSET_REG_SET);
	else
		writel(1 << chan_id,
			mxs_dma->base + HW_APBHX_CHANNEL_CTRL + STMP_OFFSET_REG_SET);

	mxs_chan->status = DMA_PAUSED;
	return 0;
}

static int mxs_dma_resume_chan(struct dma_chan *chan)
{
	struct mxs_dma_chan *mxs_chan = to_mxs_dma_chan(chan);
	struct mxs_dma_engine *mxs_dma = mxs_chan->mxs_dma;
	int chan_id = mxs_chan->chan.chan_id;

	/* unfreeze the channel */
	if (dma_is_apbh(mxs_dma) && apbh_is_old(mxs_dma))
		writel(1 << chan_id,
			mxs_dma->base + HW_APBHX_CTRL0 + STMP_OFFSET_REG_CLR);
	else
		writel(1 << chan_id,
			mxs_dma->base + HW_APBHX_CHANNEL_CTRL + STMP_OFFSET_REG_CLR);

	mxs_chan->status = DMA_IN_PROGRESS;
	return 0;
}

static dma_cookie_t mxs_dma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	return dma_cookie_assign(tx);
}

static void mxs_dma_tasklet(unsigned long data)
{
	struct mxs_dma_chan *mxs_chan = (struct mxs_dma_chan *) data;

	dmaengine_desc_get_callback_invoke(&mxs_chan->desc, NULL);
}

static int mxs_dma_irq_to_chan(struct mxs_dma_engine *mxs_dma, int irq)
{
	int i;

	for (i = 0; i != mxs_dma->nr_channels; ++i)
		if (mxs_dma->mxs_chans[i].chan_irq == irq)
			return i;

	return -EINVAL;
}

static irqreturn_t mxs_dma_int_handler(int irq, void *dev_id)
{
	struct mxs_dma_engine *mxs_dma = dev_id;
	struct mxs_dma_chan *mxs_chan;
	u32 completed;
	u32 err;
	int chan = mxs_dma_irq_to_chan(mxs_dma, irq);

	if (chan < 0)
		return IRQ_NONE;

	/* completion status */
	completed = readl(mxs_dma->base + HW_APBHX_CTRL1);
	completed = (completed >> chan) & 0x1;

	/* Clear interrupt */
	writel((1 << chan),
			mxs_dma->base + HW_APBHX_CTRL1 + STMP_OFFSET_REG_CLR);

	/* error status */
	err = readl(mxs_dma->base + HW_APBHX_CTRL2);
	err &= (1 << (MXS_DMA_CHANNELS + chan)) | (1 << chan);

	/*
	 * error status bit is in the upper 16 bits, error irq bit in the lower
	 * 16 bits. We transform it into a simpler error code:
	 * err: 0x00 = no error, 0x01 = TERMINATION, 0x02 = BUS_ERROR
	 */
	err = (err >> (MXS_DMA_CHANNELS + chan)) + (err >> chan);

	/* Clear error irq */
	writel((1 << chan),
			mxs_dma->base + HW_APBHX_CTRL2 + STMP_OFFSET_REG_CLR);

	/*
	 * When both completion and error of termination bits set at the
	 * same time, we do not take it as an error.  IOW, it only becomes
	 * an error we need to handle here in case of either it's a bus
	 * error or a termination error with no completion. 0x01 is termination
	 * error, so we can subtract err & completed to get the real error case.
	 */
	err -= err & completed;

	mxs_chan = &mxs_dma->mxs_chans[chan];

	if (err) {
		dev_dbg(mxs_dma->dma_device.dev,
			"%s: error in channel %d\n", __func__,
			chan);
		mxs_chan->status = DMA_ERROR;
		mxs_dma_reset_chan(&mxs_chan->chan);
	} else if (mxs_chan->status != DMA_COMPLETE) {
		if (mxs_chan->flags & MXS_DMA_SG_LOOP) {
			mxs_chan->status = DMA_IN_PROGRESS;
			if (mxs_chan->flags & MXS_DMA_USE_SEMAPHORE)
				writel(1, mxs_dma->base +
					HW_APBHX_CHn_SEMA(mxs_dma, chan));
		} else {
			mxs_chan->status = DMA_COMPLETE;
		}
	}

	if (mxs_chan->status == DMA_COMPLETE) {
		if (mxs_chan->reset)
			return IRQ_HANDLED;
		dma_cookie_complete(&mxs_chan->desc);
	}

	/* schedule tasklet on this channel */
	tasklet_schedule(&mxs_chan->tasklet);

	return IRQ_HANDLED;
}

static int mxs_dma_alloc_chan_resources(struct dma_chan *chan)
{
	struct mxs_dma_chan *mxs_chan = to_mxs_dma_chan(chan);
	struct mxs_dma_engine *mxs_dma = mxs_chan->mxs_dma;
	int ret;

	mxs_chan->ccw = dma_alloc_coherent(mxs_dma->dma_device.dev,
					   CCW_BLOCK_SIZE,
					   &mxs_chan->ccw_phys, GFP_KERNEL);
	if (!mxs_chan->ccw) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	ret = request_irq(mxs_chan->chan_irq, mxs_dma_int_handler,
			  0, "mxs-dma", mxs_dma);
	if (ret)
		goto err_irq;

	ret = clk_prepare_enable(mxs_dma->clk);
	if (ret)
		goto err_clk;

	mxs_dma_reset_chan(chan);

	dma_async_tx_descriptor_init(&mxs_chan->desc, chan);
	mxs_chan->desc.tx_submit = mxs_dma_tx_submit;

	/* the descriptor is ready */
	async_tx_ack(&mxs_chan->desc);

	return 0;

err_clk:
	free_irq(mxs_chan->chan_irq, mxs_dma);
err_irq:
	dma_free_coherent(mxs_dma->dma_device.dev, CCW_BLOCK_SIZE,
			mxs_chan->ccw, mxs_chan->ccw_phys);
err_alloc:
	return ret;
}

static void mxs_dma_free_chan_resources(struct dma_chan *chan)
{
	struct mxs_dma_chan *mxs_chan = to_mxs_dma_chan(chan);
	struct mxs_dma_engine *mxs_dma = mxs_chan->mxs_dma;

	mxs_dma_disable_chan(chan);

	free_irq(mxs_chan->chan_irq, mxs_dma);

	dma_free_coherent(mxs_dma->dma_device.dev, CCW_BLOCK_SIZE,
			mxs_chan->ccw, mxs_chan->ccw_phys);

	clk_disable_unprepare(mxs_dma->clk);
}

/*
 * How to use the flags for ->device_prep_slave_sg() :
 *    [1] If there is only one DMA command in the DMA chain, the code should be:
 *            ......
 *            ->device_prep_slave_sg(DMA_CTRL_ACK);
 *            ......
 *    [2] If there are two DMA commands in the DMA chain, the code should be
 *            ......
 *            ->device_prep_slave_sg(0);
 *            ......
 *            ->device_prep_slave_sg(DMA_CTRL_ACK);
 *            ......
 *    [3] If there are more than two DMA commands in the DMA chain, the code
 *        should be:
 *            ......
 *            ->device_prep_slave_sg(0);                                // First
 *            ......
 *            ->device_prep_slave_sg(DMA_CTRL_ACK]);
 *            ......
 *            ->device_prep_slave_sg(DMA_CTRL_ACK); // Last
 *            ......
 */
static struct dma_async_tx_descriptor *mxs_dma_prep_slave_sg(
		struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context)
{
	struct mxs_dma_chan *mxs_chan = to_mxs_dma_chan(chan);
	struct mxs_dma_engine *mxs_dma = mxs_chan->mxs_dma;
	struct mxs_dma_ccw *ccw;
	struct scatterlist *sg;
	u32 i, j;
	u32 *pio;
	int idx = 0;

	if (mxs_chan->status == DMA_IN_PROGRESS)
		idx = mxs_chan->desc_count;

	if (sg_len + idx > NUM_CCW) {
		dev_err(mxs_dma->dma_device.dev,
				"maximum number of sg exceeded: %d > %d\n",
				sg_len, NUM_CCW);
		goto err_out;
	}

	mxs_chan->status = DMA_IN_PROGRESS;
	mxs_chan->flags = 0;

	/*
	 * If the sg is prepared with append flag set, the sg
	 * will be appended to the last prepared sg.
	 */
	if (idx) {
		BUG_ON(idx < 1);
		ccw = &mxs_chan->ccw[idx - 1];
		ccw->next = mxs_chan->ccw_phys + sizeof(*ccw) * idx;
		ccw->bits |= CCW_CHAIN;
		ccw->bits &= ~CCW_IRQ;
		ccw->bits &= ~CCW_DEC_SEM;
	} else {
		idx = 0;
	}

	if (direction == DMA_TRANS_NONE) {
		ccw = &mxs_chan->ccw[idx++];
		pio = (u32 *) sgl;

		for (j = 0; j < sg_len;)
			ccw->pio_words[j++] = *pio++;

		ccw->bits = 0;
		ccw->bits |= CCW_IRQ;
		ccw->bits |= CCW_DEC_SEM;
		if (flags & MXS_DMA_CTRL_WAIT4END)
			ccw->bits |= CCW_WAIT4END;
		ccw->bits |= CCW_HALT_ON_TERM;
		ccw->bits |= CCW_TERM_FLUSH;
		ccw->bits |= BF_CCW(sg_len, PIO_NUM);
		ccw->bits |= BF_CCW(MXS_DMA_CMD_NO_XFER, COMMAND);
		if (flags & MXS_DMA_CTRL_WAIT4RDY)
			ccw->bits |= CCW_WAIT4RDY;
	} else {
		for_each_sg(sgl, sg, sg_len, i) {
			if (sg_dma_len(sg) > MAX_XFER_BYTES) {
				dev_err(mxs_dma->dma_device.dev, "maximum bytes for sg entry exceeded: %d > %d\n",
						sg_dma_len(sg), MAX_XFER_BYTES);
				goto err_out;
			}

			ccw = &mxs_chan->ccw[idx++];

			ccw->next = mxs_chan->ccw_phys + sizeof(*ccw) * idx;
			ccw->bufaddr = sg->dma_address;
			ccw->xfer_bytes = sg_dma_len(sg);

			ccw->bits = 0;
			ccw->bits |= CCW_CHAIN;
			ccw->bits |= CCW_HALT_ON_TERM;
			ccw->bits |= CCW_TERM_FLUSH;
			ccw->bits |= BF_CCW(direction == DMA_DEV_TO_MEM ?
					MXS_DMA_CMD_WRITE : MXS_DMA_CMD_READ,
					COMMAND);

			if (i + 1 == sg_len) {
				ccw->bits &= ~CCW_CHAIN;
				ccw->bits |= CCW_IRQ;
				ccw->bits |= CCW_DEC_SEM;
				if (flags & MXS_DMA_CTRL_WAIT4END)
					ccw->bits |= CCW_WAIT4END;
			}
		}
	}
	mxs_chan->desc_count = idx;

	return &mxs_chan->desc;

err_out:
	mxs_chan->status = DMA_ERROR;
	return NULL;
}

static struct dma_async_tx_descriptor *mxs_dma_prep_dma_cyclic(
		struct dma_chan *chan, dma_addr_t dma_addr, size_t buf_len,
		size_t period_len, enum dma_transfer_direction direction,
		unsigned long flags)
{
	struct mxs_dma_chan *mxs_chan = to_mxs_dma_chan(chan);
	struct mxs_dma_engine *mxs_dma = mxs_chan->mxs_dma;
	u32 num_periods = buf_len / period_len;
	u32 i = 0, buf = 0;

	if (mxs_chan->status == DMA_IN_PROGRESS)
		return NULL;

	mxs_chan->status = DMA_IN_PROGRESS;
	mxs_chan->flags |= MXS_DMA_SG_LOOP;
	mxs_chan->flags |= MXS_DMA_USE_SEMAPHORE;

	if (num_periods > NUM_CCW) {
		dev_err(mxs_dma->dma_device.dev,
				"maximum number of sg exceeded: %d > %d\n",
				num_periods, NUM_CCW);
		goto err_out;
	}

	if (period_len > MAX_XFER_BYTES) {
		dev_err(mxs_dma->dma_device.dev,
				"maximum period size exceeded: %zu > %d\n",
				period_len, MAX_XFER_BYTES);
		goto err_out;
	}

	while (buf < buf_len) {
		struct mxs_dma_ccw *ccw = &mxs_chan->ccw[i];

		if (i + 1 == num_periods)
			ccw->next = mxs_chan->ccw_phys;
		else
			ccw->next = mxs_chan->ccw_phys + sizeof(*ccw) * (i + 1);

		ccw->bufaddr = dma_addr;
		ccw->xfer_bytes = period_len;

		ccw->bits = 0;
		ccw->bits |= CCW_CHAIN;
		ccw->bits |= CCW_IRQ;
		ccw->bits |= CCW_HALT_ON_TERM;
		ccw->bits |= CCW_TERM_FLUSH;
		ccw->bits |= CCW_DEC_SEM;
		ccw->bits |= BF_CCW(direction == DMA_DEV_TO_MEM ?
				MXS_DMA_CMD_WRITE : MXS_DMA_CMD_READ, COMMAND);

		dma_addr += period_len;
		buf += period_len;

		i++;
	}
	mxs_chan->desc_count = i;

	return &mxs_chan->desc;

err_out:
	mxs_chan->status = DMA_ERROR;
	return NULL;
}

static int mxs_dma_terminate_all(struct dma_chan *chan)
{
	mxs_dma_reset_chan(chan);
	mxs_dma_disable_chan(chan);

	return 0;
}

static enum dma_status mxs_dma_tx_status(struct dma_chan *chan,
			dma_cookie_t cookie, struct dma_tx_state *txstate)
{
	struct mxs_dma_chan *mxs_chan = to_mxs_dma_chan(chan);
	struct mxs_dma_engine *mxs_dma = mxs_chan->mxs_dma;
	u32 residue = 0;

	if (mxs_chan->status == DMA_IN_PROGRESS &&
			mxs_chan->flags & MXS_DMA_SG_LOOP) {
		struct mxs_dma_ccw *last_ccw;
		u32 bar;

		last_ccw = &mxs_chan->ccw[mxs_chan->desc_count - 1];
		residue = last_ccw->xfer_bytes + last_ccw->bufaddr;

		bar = readl(mxs_dma->base +
				HW_APBHX_CHn_BAR(mxs_dma, chan->chan_id));
		residue -= bar;
	}

	dma_set_tx_state(txstate, chan->completed_cookie, chan->cookie,
			residue);

	return mxs_chan->status;
}

static int __init mxs_dma_init(struct mxs_dma_engine *mxs_dma)
{
	int ret;

	ret = clk_prepare_enable(mxs_dma->clk);
	if (ret)
		return ret;

	ret = stmp_reset_block(mxs_dma->base);
	if (ret)
		goto err_out;

	/* enable apbh burst */
	if (dma_is_apbh(mxs_dma)) {
		writel(BM_APBH_CTRL0_APB_BURST_EN,
			mxs_dma->base + HW_APBHX_CTRL0 + STMP_OFFSET_REG_SET);
		writel(BM_APBH_CTRL0_APB_BURST8_EN,
			mxs_dma->base + HW_APBHX_CTRL0 + STMP_OFFSET_REG_SET);
	}

	/* enable irq for all the channels */
	writel(MXS_DMA_CHANNELS_MASK << MXS_DMA_CHANNELS,
		mxs_dma->base + HW_APBHX_CTRL1 + STMP_OFFSET_REG_SET);

err_out:
	clk_disable_unprepare(mxs_dma->clk);
	return ret;
}

struct mxs_dma_filter_param {
	struct device_node *of_node;
	unsigned int chan_id;
};

static bool mxs_dma_filter_fn(struct dma_chan *chan, void *fn_param)
{
	struct mxs_dma_filter_param *param = fn_param;
	struct mxs_dma_chan *mxs_chan = to_mxs_dma_chan(chan);
	struct mxs_dma_engine *mxs_dma = mxs_chan->mxs_dma;
	int chan_irq;

	if (mxs_dma->dma_device.dev->of_node != param->of_node)
		return false;

	if (chan->chan_id != param->chan_id)
		return false;

	chan_irq = platform_get_irq(mxs_dma->pdev, param->chan_id);
	if (chan_irq < 0)
		return false;

	mxs_chan->chan_irq = chan_irq;

	return true;
}

static struct dma_chan *mxs_dma_xlate(struct of_phandle_args *dma_spec,
			       struct of_dma *ofdma)
{
	struct mxs_dma_engine *mxs_dma = ofdma->of_dma_data;
	dma_cap_mask_t mask = mxs_dma->dma_device.cap_mask;
	struct mxs_dma_filter_param param;

	if (dma_spec->args_count != 1)
		return NULL;

	param.of_node = ofdma->of_node;
	param.chan_id = dma_spec->args[0];

	if (param.chan_id >= mxs_dma->nr_channels)
		return NULL;

	return dma_request_channel(mask, mxs_dma_filter_fn, &param);
}

static int __init mxs_dma_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct platform_device_id *id_entry;
	const struct of_device_id *of_id;
	const struct mxs_dma_type *dma_type;
	struct mxs_dma_engine *mxs_dma;
	struct resource *iores;
	int ret, i;

	mxs_dma = devm_kzalloc(&pdev->dev, sizeof(*mxs_dma), GFP_KERNEL);
	if (!mxs_dma)
		return -ENOMEM;

	ret = of_property_read_u32(np, "dma-channels", &mxs_dma->nr_channels);
	if (ret) {
		dev_err(&pdev->dev, "failed to read dma-channels\n");
		return ret;
	}

	of_id = of_match_device(mxs_dma_dt_ids, &pdev->dev);
	if (of_id)
		id_entry = of_id->data;
	else
		id_entry = platform_get_device_id(pdev);

	dma_type = (struct mxs_dma_type *)id_entry->driver_data;
	mxs_dma->type = dma_type->type;
	mxs_dma->dev_id = dma_type->id;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mxs_dma->base = devm_ioremap_resource(&pdev->dev, iores);
	if (IS_ERR(mxs_dma->base))
		return PTR_ERR(mxs_dma->base);

	mxs_dma->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(mxs_dma->clk))
		return PTR_ERR(mxs_dma->clk);

	dma_cap_set(DMA_SLAVE, mxs_dma->dma_device.cap_mask);
	dma_cap_set(DMA_CYCLIC, mxs_dma->dma_device.cap_mask);

	INIT_LIST_HEAD(&mxs_dma->dma_device.channels);

	/* Initialize channel parameters */
	for (i = 0; i < MXS_DMA_CHANNELS; i++) {
		struct mxs_dma_chan *mxs_chan = &mxs_dma->mxs_chans[i];

		mxs_chan->mxs_dma = mxs_dma;
		mxs_chan->chan.device = &mxs_dma->dma_device;
		dma_cookie_init(&mxs_chan->chan);

		tasklet_init(&mxs_chan->tasklet, mxs_dma_tasklet,
			     (unsigned long) mxs_chan);


		/* Add the channel to mxs_chan list */
		list_add_tail(&mxs_chan->chan.device_node,
			&mxs_dma->dma_device.channels);
	}

	ret = mxs_dma_init(mxs_dma);
	if (ret)
		return ret;

	mxs_dma->pdev = pdev;
	mxs_dma->dma_device.dev = &pdev->dev;

	/* mxs_dma gets 65535 bytes maximum sg size */
	mxs_dma->dma_device.dev->dma_parms = &mxs_dma->dma_parms;
	dma_set_max_seg_size(mxs_dma->dma_device.dev, MAX_XFER_BYTES);

	mxs_dma->dma_device.device_alloc_chan_resources = mxs_dma_alloc_chan_resources;
	mxs_dma->dma_device.device_free_chan_resources = mxs_dma_free_chan_resources;
	mxs_dma->dma_device.device_tx_status = mxs_dma_tx_status;
	mxs_dma->dma_device.device_prep_slave_sg = mxs_dma_prep_slave_sg;
	mxs_dma->dma_device.device_prep_dma_cyclic = mxs_dma_prep_dma_cyclic;
	mxs_dma->dma_device.device_pause = mxs_dma_pause_chan;
	mxs_dma->dma_device.device_resume = mxs_dma_resume_chan;
	mxs_dma->dma_device.device_terminate_all = mxs_dma_terminate_all;
	mxs_dma->dma_device.src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	mxs_dma->dma_device.dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	mxs_dma->dma_device.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	mxs_dma->dma_device.residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;
	mxs_dma->dma_device.device_issue_pending = mxs_dma_enable_chan;

	ret = dmaenginem_async_device_register(&mxs_dma->dma_device);
	if (ret) {
		dev_err(mxs_dma->dma_device.dev, "unable to register\n");
		return ret;
	}

	ret = of_dma_controller_register(np, mxs_dma_xlate, mxs_dma);
	if (ret) {
		dev_err(mxs_dma->dma_device.dev,
			"failed to register controller\n");
	}

	dev_info(mxs_dma->dma_device.dev, "initialized\n");

	return 0;
}

static struct platform_driver mxs_dma_driver = {
	.driver		= {
		.name	= "mxs-dma",
		.of_match_table = mxs_dma_dt_ids,
	},
	.id_table	= mxs_dma_ids,
};

static int __init mxs_dma_module_init(void)
{
	return platform_driver_probe(&mxs_dma_driver, mxs_dma_probe);
}
subsys_initcall(mxs_dma_module_init);
