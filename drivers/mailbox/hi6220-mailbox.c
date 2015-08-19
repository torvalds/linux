/*
 * Hisilicon's Hi6220 mailbox driver
 *
 * RX channel's message queue is based on the code written in
 * drivers/mailbox/omap-mailbox.c.
 *
 * Copyright (c) 2015 Hisilicon Limited.
 * Copyright (c) 2015 Linaro Limited.
 *
 * Author: Leo Yan <leo.yan@linaro.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kfifo.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

#define HI6220_MBOX_CHAN_MAX		32
#define HI6220_MBOX_CHAN_NUM		2
#define HI6220_MBOX_CHAN_SLOT_SIZE	64

#define HI6220_MBOX_RX			0x0
#define HI6220_MBOX_TX			0x1

/* Mailbox message length: 32 bytes */
#define HI6220_MBOX_MSG_LEN		32

/* Mailbox kfifo size */
#define HI6220_MBOX_MSG_FIFO_SIZE	512

/* Status & Mode Register */
#define HI6220_MBOX_MODE_REG		0x0

#define HI6220_MBOX_STATUS_MASK		(0xF << 4)
#define HI6220_MBOX_STATUS_IDLE		(0x1 << 4)
#define HI6220_MBOX_STATUS_TX		(0x2 << 4)
#define HI6220_MBOX_STATUS_RX		(0x4 << 4)
#define HI6220_MBOX_STATUS_ACK		(0x8 << 4)
#define HI6220_MBOX_ACK_CONFIG_MASK	(0x1 << 0)
#define HI6220_MBOX_ACK_AUTOMATIC	(0x1 << 0)
#define HI6220_MBOX_ACK_IRQ		(0x0 << 0)

/* Data Registers */
#define HI6220_MBOX_DATA_REG(i)		(0x4 + (i << 2))

/* ACPU Interrupt Register */
#define HI6220_MBOX_ACPU_INT_RAW_REG	0x400
#define HI6220_MBOX_ACPU_INT_MSK_REG	0x404
#define HI6220_MBOX_ACPU_INT_STAT_REG	0x408
#define HI6220_MBOX_ACPU_INT_CLR_REG	0x40c
#define HI6220_MBOX_ACPU_INT_ENA_REG	0x500
#define HI6220_MBOX_ACPU_INT_DIS_REG	0x504

/* MCU Interrupt Register */
#define HI6220_MBOX_MCU_INT_RAW_REG	0x420

/* Core Id */
#define HI6220_CORE_ACPU		0x0
#define HI6220_CORE_MCU			0x2

struct hi6220_mbox_queue {
	struct kfifo fifo;
	struct work_struct work;
	struct mbox_chan *chan;
	bool full;
};

struct hi6220_mbox_chan {

	/*
	 * Description for channel's hardware info:
	 *  - direction;
	 *  - peer core id for communication;
	 *  - local irq vector or number;
	 *  - remoted irq vector or number for peer core;
	 */
	unsigned int dir;
	unsigned int peer_core;
	unsigned int remote_irq;
	unsigned int local_irq;

	/*
	 * Slot address is cached value derived from index
	 * within buffer for every channel
	 */
	void __iomem *slot;

	/* For rx's fifo operations */
	struct hi6220_mbox_queue *mq;

	struct hi6220_mbox *parent;
};

struct hi6220_mbox {
	struct device *dev;

	spinlock_t lock;

	int irq;

	/* flag of enabling tx's irq mode */
	bool tx_irq_mode;

	/* region for ipc event */
	void __iomem *ipc;

	/* region for share mem */
	void __iomem *buf;

	unsigned int chan_num;
	struct hi6220_mbox_chan *mchan;

	void *irq_map_chan[HI6220_MBOX_CHAN_MAX];
	struct mbox_chan *chan;
	struct mbox_controller controller;
};

static void hi6220_mbox_set_status(struct hi6220_mbox_chan *mchan, u32 val)
{
	u32 status;

	status = readl(mchan->slot + HI6220_MBOX_MODE_REG);
	status &= ~HI6220_MBOX_STATUS_MASK;
	status |= val;
	writel(status, mchan->slot + HI6220_MBOX_MODE_REG);
}

static void hi6220_mbox_set_mode(struct hi6220_mbox_chan *mchan, u32 val)
{
	u32 mode;

	mode = readl(mchan->slot + HI6220_MBOX_MODE_REG);
	mode &= ~HI6220_MBOX_ACK_CONFIG_MASK;
	mode |= val;
	writel(mode, mchan->slot + HI6220_MBOX_MODE_REG);
}

static bool hi6220_mbox_last_tx_done(struct mbox_chan *chan)
{
	struct hi6220_mbox_chan *mchan = chan->con_priv;
	struct hi6220_mbox *mbox = mchan->parent;
	u32 status;

	/* Only set idle state for polling mode */
	BUG_ON(mbox->tx_irq_mode);

	status = readl(mchan->slot + HI6220_MBOX_MODE_REG);
	status = status & HI6220_MBOX_STATUS_MASK;
	return (status == HI6220_MBOX_STATUS_IDLE);
}

static int hi6220_mbox_send_data(struct mbox_chan *chan, void *msg)
{
	struct hi6220_mbox_chan *mchan = chan->con_priv;
	struct hi6220_mbox *mbox = mchan->parent;
	int irq = mchan->remote_irq;
	u32 *buf = msg;
	unsigned long flags;
	int i;

	hi6220_mbox_set_status(mchan, HI6220_MBOX_STATUS_TX);

	if (mbox->tx_irq_mode)
		hi6220_mbox_set_mode(mchan, HI6220_MBOX_ACK_IRQ);
	else
		hi6220_mbox_set_mode(mchan, HI6220_MBOX_ACK_AUTOMATIC);

	for (i = 0; i < (HI6220_MBOX_MSG_LEN >> 2); i++)
		writel(buf[i], mchan->slot + HI6220_MBOX_DATA_REG(i));

	/* trigger remote request */
	spin_lock_irqsave(&mbox->lock, flags);
	writel(1 << irq, mbox->ipc + HI6220_MBOX_MCU_INT_RAW_REG);
	spin_unlock_irqrestore(&mbox->lock, flags);
	return 0;
}

static void hi6220_mbox_rx_work(struct work_struct *work)
{
	struct hi6220_mbox_queue *mq =
			container_of(work, struct hi6220_mbox_queue, work);
	struct mbox_chan *chan = mq->chan;
	struct hi6220_mbox_chan *mchan = chan->con_priv;
	struct hi6220_mbox *mbox = mchan->parent;
	int irq = mchan->local_irq, len;
	u32 msg[HI6220_MBOX_MSG_LEN >> 2];

	while (kfifo_len(&mq->fifo) >= sizeof(msg)) {
		len = kfifo_out(&mq->fifo, (unsigned char *)&msg, sizeof(msg));
		WARN_ON(len != sizeof(msg));

		mbox_chan_received_data(chan, (void *)msg);
		spin_lock_irq(&mbox->lock);
		if (mq->full) {
			mq->full = false;
			writel(1 << irq,
				mbox->ipc + HI6220_MBOX_ACPU_INT_ENA_REG);
		}
		spin_unlock_irq(&mbox->lock);
	}
}

static void hi6220_mbox_tx_interrupt(struct mbox_chan *chan)
{
	struct hi6220_mbox_chan *mchan = chan->con_priv;
	struct hi6220_mbox *mbox = mchan->parent;
	int irq = mchan->local_irq;

	spin_lock(&mbox->lock);
	writel(1 << irq, mbox->ipc + HI6220_MBOX_ACPU_INT_CLR_REG);
	spin_unlock(&mbox->lock);

	hi6220_mbox_set_status(mchan, HI6220_MBOX_STATUS_IDLE);
	mbox_chan_txdone(chan, 0);
}

static void hi6220_mbox_rx_interrupt(struct mbox_chan *chan)
{
	struct hi6220_mbox_chan *mchan = chan->con_priv;
	struct hi6220_mbox_queue *mq = mchan->mq;
	struct hi6220_mbox *mbox = mchan->parent;
	int irq = mchan->local_irq;
	int msg[HI6220_MBOX_MSG_LEN >> 2];
	int i, len;

	if (unlikely(kfifo_avail(&mq->fifo) < sizeof(msg))) {
		spin_lock(&mbox->lock);
		writel(1 << irq, mbox->ipc + HI6220_MBOX_ACPU_INT_DIS_REG);
		mq->full = true;
		spin_unlock(&mbox->lock);
		goto nomem;
	}

	for (i = 0; i < (HI6220_MBOX_MSG_LEN >> 2); i++)
		msg[i] = readl(mchan->slot + HI6220_MBOX_DATA_REG(i));

	/* clear IRQ source */
	spin_lock(&mbox->lock);
	writel(1 << irq, mbox->ipc + HI6220_MBOX_ACPU_INT_CLR_REG);
	spin_unlock(&mbox->lock);

	hi6220_mbox_set_status(mchan, HI6220_MBOX_STATUS_IDLE);

	len = kfifo_in(&mq->fifo, (unsigned char *)&msg, sizeof(msg));
	WARN_ON(len != sizeof(msg));

nomem:
	schedule_work(&mq->work);
}

static irqreturn_t hi6220_mbox_interrupt(int irq, void *p)
{
	struct hi6220_mbox *mbox = p;
	struct hi6220_mbox_chan *mchan;
	struct mbox_chan *chan;
	unsigned int state;
	unsigned int intr_bit;

	state = readl(mbox->ipc + HI6220_MBOX_ACPU_INT_STAT_REG);
	if (!state) {
		dev_warn(mbox->dev, "%s: spurious interrupt\n",
			 __func__);
		return IRQ_HANDLED;
	}

	while (state) {
		intr_bit = __ffs(state);
		state &= (state - 1);

		chan = mbox->irq_map_chan[intr_bit];
		if (!chan) {
			dev_warn(mbox->dev, "%s: unexpected irq vector %d\n",
				 __func__, intr_bit);
			continue;
		}

		mchan = chan->con_priv;
		if (mchan->dir == HI6220_MBOX_TX)
			hi6220_mbox_tx_interrupt(chan);
		else
			hi6220_mbox_rx_interrupt(chan);
	}

	return IRQ_HANDLED;
}

static struct hi6220_mbox_queue *hi6220_mbox_queue_alloc(
		struct mbox_chan *chan,
		void (*work)(struct work_struct *))
{
	struct hi6220_mbox_queue *mq;

	mq = kzalloc(sizeof(struct hi6220_mbox_queue), GFP_KERNEL);
	if (!mq)
		return NULL;

	if (kfifo_alloc(&mq->fifo, HI6220_MBOX_MSG_FIFO_SIZE, GFP_KERNEL))
		goto error;

	mq->chan = chan;
	INIT_WORK(&mq->work, work);
	return mq;

error:
	kfree(mq);
	return NULL;
}

static void hi6220_mbox_queue_free(struct hi6220_mbox_queue *mq)
{
	kfifo_free(&mq->fifo);
	kfree(mq);
}

static int hi6220_mbox_startup(struct mbox_chan *chan)
{
	struct hi6220_mbox_chan *mchan = chan->con_priv;
	struct hi6220_mbox *mbox = mchan->parent;
	unsigned int irq = mchan->local_irq;
	struct hi6220_mbox_queue *mq;
	unsigned long flags;

	mq = hi6220_mbox_queue_alloc(chan, hi6220_mbox_rx_work);
	if (!mq)
		return -ENOMEM;
	mchan->mq = mq;
	mbox->irq_map_chan[irq] = (void *)chan;

	/* enable interrupt */
	spin_lock_irqsave(&mbox->lock, flags);
	writel(1 << irq, mbox->ipc + HI6220_MBOX_ACPU_INT_ENA_REG);
	spin_unlock_irqrestore(&mbox->lock, flags);
	return 0;
}

static void hi6220_mbox_shutdown(struct mbox_chan *chan)
{
	struct hi6220_mbox_chan *mchan = chan->con_priv;
	struct hi6220_mbox *mbox = mchan->parent;
	unsigned int irq = mchan->local_irq;
	unsigned long flags;

	/* disable interrupt */
	spin_lock_irqsave(&mbox->lock, flags);
	writel(1 << irq, mbox->ipc + HI6220_MBOX_ACPU_INT_DIS_REG);
	spin_unlock_irqrestore(&mbox->lock, flags);

	mbox->irq_map_chan[irq] = NULL;
	flush_work(&mchan->mq->work);
	hi6220_mbox_queue_free(mchan->mq);
}

static struct mbox_chan_ops hi6220_mbox_chan_ops = {
	.send_data    = hi6220_mbox_send_data,
	.startup      = hi6220_mbox_startup,
	.shutdown     = hi6220_mbox_shutdown,
	.last_tx_done = hi6220_mbox_last_tx_done,
};

static void hi6220_mbox_init_hw(struct hi6220_mbox *mbox)
{
	struct hi6220_mbox_chan init_data[HI6220_MBOX_CHAN_NUM] = {
		{ HI6220_MBOX_RX, HI6220_CORE_MCU, 1, 10 },
		{ HI6220_MBOX_TX, HI6220_CORE_MCU, 0, 11 },
	};
	struct hi6220_mbox_chan *mchan = mbox->mchan;
	int i;

	for (i = 0; i < HI6220_MBOX_CHAN_NUM; i++) {
		memcpy(&mchan[i], &init_data[i], sizeof(*mchan));
		mchan[i].slot = mbox->buf + HI6220_MBOX_CHAN_SLOT_SIZE * i;
		mchan[i].parent = mbox;
	}

	/* mask and clear all interrupt vectors */
	writel(0x0,  mbox->ipc + HI6220_MBOX_ACPU_INT_MSK_REG);
	writel(~0x0, mbox->ipc + HI6220_MBOX_ACPU_INT_CLR_REG);

	/* use interrupt for tx's ack */
	mbox->tx_irq_mode = true;
}

static const struct of_device_id hi6220_mbox_of_match[] = {
	{ .compatible = "hisilicon,hi6220-mbox", },
	{},
};
MODULE_DEVICE_TABLE(of, hi6220_mbox_of_match);

static int hi6220_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hi6220_mbox *mbox;
	struct resource *res;
	int i, err;

	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	mbox->dev = dev;
	mbox->chan_num = HI6220_MBOX_CHAN_NUM;
	mbox->mchan = devm_kzalloc(dev,
		mbox->chan_num * sizeof(*mbox->mchan), GFP_KERNEL);
	if (!mbox->mchan)
		return -ENOMEM;

	mbox->chan = devm_kzalloc(dev,
		mbox->chan_num * sizeof(*mbox->chan), GFP_KERNEL);
	if (!mbox->chan)
		return -ENOMEM;

	mbox->irq = platform_get_irq(pdev, 0);
	if (mbox->irq < 0)
		return mbox->irq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mbox->ipc = devm_ioremap_resource(dev, res);
	if (IS_ERR(mbox->ipc)) {
		dev_err(dev, "ioremap ipc failed\n");
		return PTR_ERR(mbox->ipc);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	mbox->buf = devm_ioremap_resource(dev, res);
	if (IS_ERR(mbox->buf)) {
		dev_err(dev, "ioremap buffer failed\n");
		return PTR_ERR(mbox->buf);
	}

	err = devm_request_irq(dev, mbox->irq, hi6220_mbox_interrupt, 0,
			dev_name(dev), mbox);
	if (err) {
		dev_err(dev, "Failed to register a mailbox IRQ handler: %d\n",
			err);
		return -ENODEV;
	}

	/* init hardware parameters */
	hi6220_mbox_init_hw(mbox);

	spin_lock_init(&mbox->lock);

	for (i = 0; i < mbox->chan_num; i++) {
		mbox->chan[i].con_priv = &mbox->mchan[i];
		mbox->irq_map_chan[i] = NULL;
	}

	mbox->controller.dev = dev;
	mbox->controller.chans = &mbox->chan[0];
	mbox->controller.num_chans = mbox->chan_num;
	mbox->controller.ops = &hi6220_mbox_chan_ops;

	if (mbox->tx_irq_mode)
		mbox->controller.txdone_irq = true;
	else {
		mbox->controller.txdone_poll = true;
		mbox->controller.txpoll_period = 5;
	}

	err = mbox_controller_register(&mbox->controller);
	if (err) {
		dev_err(dev, "Failed to register mailbox %d\n", err);
		return err;
	}

	platform_set_drvdata(pdev, mbox);
	dev_info(dev, "Mailbox enabled\n");
	return 0;
}

static int hi6220_mbox_remove(struct platform_device *pdev)
{
	struct hi6220_mbox *mbox = platform_get_drvdata(pdev);

	mbox_controller_unregister(&mbox->controller);
	return 0;
}

static struct platform_driver hi6220_mbox_driver = {
	.driver = {
		.name = "hi6220-mbox",
		.owner = THIS_MODULE,
		.of_match_table = hi6220_mbox_of_match,
	},
	.probe	= hi6220_mbox_probe,
	.remove	= hi6220_mbox_remove,
};

static int __init hi6220_mbox_init(void)
{
	return platform_driver_register(&hi6220_mbox_driver);
}
core_initcall(hi6220_mbox_init);

static void __exit hi6220_mbox_exit(void)
{
	platform_driver_unregister(&hi6220_mbox_driver);
}
module_exit(hi6220_mbox_exit);

MODULE_AUTHOR("Leo Yan <leo.yan@linaro.org>");
MODULE_DESCRIPTION("Hi6220 mailbox driver");
MODULE_LICENSE("GPL v2");
