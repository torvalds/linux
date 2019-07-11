// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013-2015 Fujitsu Semiconductor Ltd.
 * Copyright (C) 2015 Linaro Ltd.
 * Author: Jassi Brar <jaswinder.singh@linaro.org>
 */

#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/amba/bus.h>
#include <linux/mailbox_controller.h>

#define INTR_STAT_OFS	0x0
#define INTR_SET_OFS	0x8
#define INTR_CLR_OFS	0x10

#define MHU_LP_OFFSET	0x0
#define MHU_HP_OFFSET	0x20
#define MHU_SEC_OFFSET	0x200
#define TX_REG_OFFSET	0x100

#define MHU_CHANS	3

struct mhu_link {
	unsigned irq;
	void __iomem *tx_reg;
	void __iomem *rx_reg;
};

struct arm_mhu {
	void __iomem *base;
	struct mhu_link mlink[MHU_CHANS];
	struct mbox_chan chan[MHU_CHANS];
	struct mbox_controller mbox;
};

static irqreturn_t mhu_rx_interrupt(int irq, void *p)
{
	struct mbox_chan *chan = p;
	struct mhu_link *mlink = chan->con_priv;
	u32 val;

	val = readl_relaxed(mlink->rx_reg + INTR_STAT_OFS);
	if (!val)
		return IRQ_NONE;

	mbox_chan_received_data(chan, (void *)&val);

	writel_relaxed(val, mlink->rx_reg + INTR_CLR_OFS);

	return IRQ_HANDLED;
}

static bool mhu_last_tx_done(struct mbox_chan *chan)
{
	struct mhu_link *mlink = chan->con_priv;
	u32 val = readl_relaxed(mlink->tx_reg + INTR_STAT_OFS);

	return (val == 0);
}

static int mhu_send_data(struct mbox_chan *chan, void *data)
{
	struct mhu_link *mlink = chan->con_priv;
	u32 *arg = data;

	writel_relaxed(*arg, mlink->tx_reg + INTR_SET_OFS);

	return 0;
}

static int mhu_startup(struct mbox_chan *chan)
{
	struct mhu_link *mlink = chan->con_priv;
	u32 val;
	int ret;

	val = readl_relaxed(mlink->tx_reg + INTR_STAT_OFS);
	writel_relaxed(val, mlink->tx_reg + INTR_CLR_OFS);

	ret = request_irq(mlink->irq, mhu_rx_interrupt,
			  IRQF_SHARED, "mhu_link", chan);
	if (ret) {
		dev_err(chan->mbox->dev,
			"Unable to acquire IRQ %d\n", mlink->irq);
		return ret;
	}

	return 0;
}

static void mhu_shutdown(struct mbox_chan *chan)
{
	struct mhu_link *mlink = chan->con_priv;

	free_irq(mlink->irq, chan);
}

static const struct mbox_chan_ops mhu_ops = {
	.send_data = mhu_send_data,
	.startup = mhu_startup,
	.shutdown = mhu_shutdown,
	.last_tx_done = mhu_last_tx_done,
};

static int mhu_probe(struct amba_device *adev, const struct amba_id *id)
{
	int i, err;
	struct arm_mhu *mhu;
	struct device *dev = &adev->dev;
	int mhu_reg[MHU_CHANS] = {MHU_LP_OFFSET, MHU_HP_OFFSET, MHU_SEC_OFFSET};

	/* Allocate memory for device */
	mhu = devm_kzalloc(dev, sizeof(*mhu), GFP_KERNEL);
	if (!mhu)
		return -ENOMEM;

	mhu->base = devm_ioremap_resource(dev, &adev->res);
	if (IS_ERR(mhu->base)) {
		dev_err(dev, "ioremap failed\n");
		return PTR_ERR(mhu->base);
	}

	for (i = 0; i < MHU_CHANS; i++) {
		mhu->chan[i].con_priv = &mhu->mlink[i];
		mhu->mlink[i].irq = adev->irq[i];
		mhu->mlink[i].rx_reg = mhu->base + mhu_reg[i];
		mhu->mlink[i].tx_reg = mhu->mlink[i].rx_reg + TX_REG_OFFSET;
	}

	mhu->mbox.dev = dev;
	mhu->mbox.chans = &mhu->chan[0];
	mhu->mbox.num_chans = MHU_CHANS;
	mhu->mbox.ops = &mhu_ops;
	mhu->mbox.txdone_irq = false;
	mhu->mbox.txdone_poll = true;
	mhu->mbox.txpoll_period = 1;

	amba_set_drvdata(adev, mhu);

	err = devm_mbox_controller_register(dev, &mhu->mbox);
	if (err) {
		dev_err(dev, "Failed to register mailboxes %d\n", err);
		return err;
	}

	dev_info(dev, "ARM MHU Mailbox registered\n");
	return 0;
}

static struct amba_id mhu_ids[] = {
	{
		.id	= 0x1bb098,
		.mask	= 0xffffff,
	},
	{ 0, 0 },
};
MODULE_DEVICE_TABLE(amba, mhu_ids);

static struct amba_driver arm_mhu_driver = {
	.drv = {
		.name	= "mhu",
	},
	.id_table	= mhu_ids,
	.probe		= mhu_probe,
};
module_amba_driver(arm_mhu_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ARM MHU Driver");
MODULE_AUTHOR("Jassi Brar <jassisinghbrar@gmail.com>");
