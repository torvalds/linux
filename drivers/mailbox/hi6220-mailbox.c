// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hisilicon's Hi6220 mailbox driver
 *
 * Copyright (c) 2015 HiSilicon Limited.
 * Copyright (c) 2015 Linaro Limited.
 *
 * Author: Leo Yan <leo.yan@linaro.org>
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kfifo.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define MBOX_CHAN_MAX			32

#define MBOX_TX				0x1

/* Mailbox message length: 8 words */
#define MBOX_MSG_LEN			8

/* Mailbox Registers */
#define MBOX_OFF(m)			(0x40 * (m))
#define MBOX_MODE_REG(m)		(MBOX_OFF(m) + 0x0)
#define MBOX_DATA_REG(m)		(MBOX_OFF(m) + 0x4)

#define MBOX_STATE_MASK			(0xF << 4)
#define MBOX_STATE_IDLE			(0x1 << 4)
#define MBOX_STATE_TX			(0x2 << 4)
#define MBOX_STATE_RX			(0x4 << 4)
#define MBOX_STATE_ACK			(0x8 << 4)
#define MBOX_ACK_CONFIG_MASK		(0x1 << 0)
#define MBOX_ACK_AUTOMATIC		(0x1 << 0)
#define MBOX_ACK_IRQ			(0x0 << 0)

/* IPC registers */
#define ACK_INT_RAW_REG(i)		((i) + 0x400)
#define ACK_INT_MSK_REG(i)		((i) + 0x404)
#define ACK_INT_STAT_REG(i)		((i) + 0x408)
#define ACK_INT_CLR_REG(i)		((i) + 0x40c)
#define ACK_INT_ENA_REG(i)		((i) + 0x500)
#define ACK_INT_DIS_REG(i)		((i) + 0x504)
#define DST_INT_RAW_REG(i)		((i) + 0x420)


struct hi6220_mbox_chan {

	/*
	 * Description for channel's hardware info:
	 *  - direction: tx or rx
	 *  - dst irq: peer core's irq number
	 *  - ack irq: local irq number
	 *  - slot number
	 */
	unsigned int dir, dst_irq, ack_irq;
	unsigned int slot;

	struct hi6220_mbox *parent;
};

struct hi6220_mbox {
	struct device *dev;

	int irq;

	/* flag of enabling tx's irq mode */
	bool tx_irq_mode;

	/* region for ipc event */
	void __iomem *ipc;

	/* region for mailbox */
	void __iomem *base;

	unsigned int chan_num;
	struct hi6220_mbox_chan *mchan;

	void *irq_map_chan[MBOX_CHAN_MAX];
	struct mbox_chan *chan;
	struct mbox_controller controller;
};

static void mbox_set_state(struct hi6220_mbox *mbox,
			   unsigned int slot, u32 val)
{
	u32 status;

	status = readl(mbox->base + MBOX_MODE_REG(slot));
	status = (status & ~MBOX_STATE_MASK) | val;
	writel(status, mbox->base + MBOX_MODE_REG(slot));
}

static void mbox_set_mode(struct hi6220_mbox *mbox,
			  unsigned int slot, u32 val)
{
	u32 mode;

	mode = readl(mbox->base + MBOX_MODE_REG(slot));
	mode = (mode & ~MBOX_ACK_CONFIG_MASK) | val;
	writel(mode, mbox->base + MBOX_MODE_REG(slot));
}

static bool hi6220_mbox_last_tx_done(struct mbox_chan *chan)
{
	struct hi6220_mbox_chan *mchan = chan->con_priv;
	struct hi6220_mbox *mbox = mchan->parent;
	u32 state;

	/* Only set idle state for polling mode */
	BUG_ON(mbox->tx_irq_mode);

	state = readl(mbox->base + MBOX_MODE_REG(mchan->slot));
	return ((state & MBOX_STATE_MASK) == MBOX_STATE_IDLE);
}

static int hi6220_mbox_send_data(struct mbox_chan *chan, void *msg)
{
	struct hi6220_mbox_chan *mchan = chan->con_priv;
	struct hi6220_mbox *mbox = mchan->parent;
	unsigned int slot = mchan->slot;
	u32 *buf = msg;
	int i;

	/* indicate as a TX channel */
	mchan->dir = MBOX_TX;

	mbox_set_state(mbox, slot, MBOX_STATE_TX);

	if (mbox->tx_irq_mode)
		mbox_set_mode(mbox, slot, MBOX_ACK_IRQ);
	else
		mbox_set_mode(mbox, slot, MBOX_ACK_AUTOMATIC);

	for (i = 0; i < MBOX_MSG_LEN; i++)
		writel(buf[i], mbox->base + MBOX_DATA_REG(slot) + i * 4);

	/* trigger remote request */
	writel(BIT(mchan->dst_irq), DST_INT_RAW_REG(mbox->ipc));
	return 0;
}

static irqreturn_t hi6220_mbox_interrupt(int irq, void *p)
{
	struct hi6220_mbox *mbox = p;
	struct hi6220_mbox_chan *mchan;
	struct mbox_chan *chan;
	unsigned int state, intr_bit, i;
	u32 msg[MBOX_MSG_LEN];

	state = readl(ACK_INT_STAT_REG(mbox->ipc));
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
		if (mchan->dir == MBOX_TX)
			mbox_chan_txdone(chan, 0);
		else {
			for (i = 0; i < MBOX_MSG_LEN; i++)
				msg[i] = readl(mbox->base +
					MBOX_DATA_REG(mchan->slot) + i * 4);

			mbox_chan_received_data(chan, (void *)msg);
		}

		/* clear IRQ source */
		writel(BIT(mchan->ack_irq), ACK_INT_CLR_REG(mbox->ipc));
		mbox_set_state(mbox, mchan->slot, MBOX_STATE_IDLE);
	}

	return IRQ_HANDLED;
}

static int hi6220_mbox_startup(struct mbox_chan *chan)
{
	struct hi6220_mbox_chan *mchan = chan->con_priv;
	struct hi6220_mbox *mbox = mchan->parent;

	mchan->dir = 0;

	/* enable interrupt */
	writel(BIT(mchan->ack_irq), ACK_INT_ENA_REG(mbox->ipc));
	return 0;
}

static void hi6220_mbox_shutdown(struct mbox_chan *chan)
{
	struct hi6220_mbox_chan *mchan = chan->con_priv;
	struct hi6220_mbox *mbox = mchan->parent;

	/* disable interrupt */
	writel(BIT(mchan->ack_irq), ACK_INT_DIS_REG(mbox->ipc));
	mbox->irq_map_chan[mchan->ack_irq] = NULL;
}

static const struct mbox_chan_ops hi6220_mbox_ops = {
	.send_data    = hi6220_mbox_send_data,
	.startup      = hi6220_mbox_startup,
	.shutdown     = hi6220_mbox_shutdown,
	.last_tx_done = hi6220_mbox_last_tx_done,
};

static struct mbox_chan *hi6220_mbox_xlate(struct mbox_controller *controller,
					   const struct of_phandle_args *spec)
{
	struct hi6220_mbox *mbox = dev_get_drvdata(controller->dev);
	struct hi6220_mbox_chan *mchan;
	struct mbox_chan *chan;
	unsigned int i = spec->args[0];
	unsigned int dst_irq = spec->args[1];
	unsigned int ack_irq = spec->args[2];

	/* Bounds checking */
	if (i >= mbox->chan_num || dst_irq >= mbox->chan_num ||
	    ack_irq >= mbox->chan_num) {
		dev_err(mbox->dev,
			"Invalid channel idx %d dst_irq %d ack_irq %d\n",
			i, dst_irq, ack_irq);
		return ERR_PTR(-EINVAL);
	}

	/* Is requested channel free? */
	chan = &mbox->chan[i];
	if (mbox->irq_map_chan[ack_irq] == (void *)chan) {
		dev_err(mbox->dev, "Channel in use\n");
		return ERR_PTR(-EBUSY);
	}

	mchan = chan->con_priv;
	mchan->dst_irq = dst_irq;
	mchan->ack_irq = ack_irq;

	mbox->irq_map_chan[ack_irq] = (void *)chan;
	return chan;
}

static const struct of_device_id hi6220_mbox_of_match[] = {
	{ .compatible = "hisilicon,hi6220-mbox", },
	{},
};
MODULE_DEVICE_TABLE(of, hi6220_mbox_of_match);

static int hi6220_mbox_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct hi6220_mbox *mbox;
	int i, err;

	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	mbox->dev = dev;
	mbox->chan_num = MBOX_CHAN_MAX;
	mbox->mchan = devm_kcalloc(dev,
		mbox->chan_num, sizeof(*mbox->mchan), GFP_KERNEL);
	if (!mbox->mchan)
		return -ENOMEM;

	mbox->chan = devm_kcalloc(dev,
		mbox->chan_num, sizeof(*mbox->chan), GFP_KERNEL);
	if (!mbox->chan)
		return -ENOMEM;

	mbox->irq = platform_get_irq(pdev, 0);
	if (mbox->irq < 0)
		return mbox->irq;

	mbox->ipc = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mbox->ipc)) {
		dev_err(dev, "ioremap ipc failed\n");
		return PTR_ERR(mbox->ipc);
	}

	mbox->base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(mbox->base)) {
		dev_err(dev, "ioremap buffer failed\n");
		return PTR_ERR(mbox->base);
	}

	err = devm_request_irq(dev, mbox->irq, hi6220_mbox_interrupt, 0,
			dev_name(dev), mbox);
	if (err) {
		dev_err(dev, "Failed to register a mailbox IRQ handler: %d\n",
			err);
		return -ENODEV;
	}

	mbox->controller.dev = dev;
	mbox->controller.chans = &mbox->chan[0];
	mbox->controller.num_chans = mbox->chan_num;
	mbox->controller.ops = &hi6220_mbox_ops;
	mbox->controller.of_xlate = hi6220_mbox_xlate;

	for (i = 0; i < mbox->chan_num; i++) {
		mbox->chan[i].con_priv = &mbox->mchan[i];
		mbox->irq_map_chan[i] = NULL;

		mbox->mchan[i].parent = mbox;
		mbox->mchan[i].slot   = i;
	}

	/* mask and clear all interrupt vectors */
	writel(0x0,  ACK_INT_MSK_REG(mbox->ipc));
	writel(~0x0, ACK_INT_CLR_REG(mbox->ipc));

	/* use interrupt for tx's ack */
	mbox->tx_irq_mode = !of_property_read_bool(node, "hi6220,mbox-tx-noirq");

	if (mbox->tx_irq_mode)
		mbox->controller.txdone_irq = true;
	else {
		mbox->controller.txdone_poll = true;
		mbox->controller.txpoll_period = 5;
	}

	err = devm_mbox_controller_register(dev, &mbox->controller);
	if (err) {
		dev_err(dev, "Failed to register mailbox %d\n", err);
		return err;
	}

	platform_set_drvdata(pdev, mbox);
	dev_info(dev, "Mailbox enabled\n");
	return 0;
}

static struct platform_driver hi6220_mbox_driver = {
	.driver = {
		.name = "hi6220-mbox",
		.of_match_table = hi6220_mbox_of_match,
	},
	.probe	= hi6220_mbox_probe,
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
