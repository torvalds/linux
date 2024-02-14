// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 BayLibre SAS.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Synchronised with arm_mhu.c from :
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
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mailbox_controller.h>

#define INTR_SET_OFS	0x0
#define INTR_STAT_OFS	0x4
#define INTR_CLR_OFS	0x8

#define MHU_SEC_OFFSET	0x0
#define MHU_LP_OFFSET	0xc
#define MHU_HP_OFFSET	0x18
#define TX_REG_OFFSET	0x24

#define MHU_CHANS	3

struct platform_mhu_link {
	int irq;
	void __iomem *tx_reg;
	void __iomem *rx_reg;
};

struct platform_mhu {
	void __iomem *base;
	struct platform_mhu_link mlink[MHU_CHANS];
	struct mbox_chan chan[MHU_CHANS];
	struct mbox_controller mbox;
};

static irqreturn_t platform_mhu_rx_interrupt(int irq, void *p)
{
	struct mbox_chan *chan = p;
	struct platform_mhu_link *mlink = chan->con_priv;
	u32 val;

	val = readl_relaxed(mlink->rx_reg + INTR_STAT_OFS);
	if (!val)
		return IRQ_NONE;

	mbox_chan_received_data(chan, (void *)&val);

	writel_relaxed(val, mlink->rx_reg + INTR_CLR_OFS);

	return IRQ_HANDLED;
}

static bool platform_mhu_last_tx_done(struct mbox_chan *chan)
{
	struct platform_mhu_link *mlink = chan->con_priv;
	u32 val = readl_relaxed(mlink->tx_reg + INTR_STAT_OFS);

	return (val == 0);
}

static int platform_mhu_send_data(struct mbox_chan *chan, void *data)
{
	struct platform_mhu_link *mlink = chan->con_priv;
	u32 *arg = data;

	writel_relaxed(*arg, mlink->tx_reg + INTR_SET_OFS);

	return 0;
}

static int platform_mhu_startup(struct mbox_chan *chan)
{
	struct platform_mhu_link *mlink = chan->con_priv;
	u32 val;
	int ret;

	val = readl_relaxed(mlink->tx_reg + INTR_STAT_OFS);
	writel_relaxed(val, mlink->tx_reg + INTR_CLR_OFS);

	ret = request_irq(mlink->irq, platform_mhu_rx_interrupt,
			  IRQF_SHARED, "platform_mhu_link", chan);
	if (ret) {
		dev_err(chan->mbox->dev,
			"Unable to acquire IRQ %d\n", mlink->irq);
		return ret;
	}

	return 0;
}

static void platform_mhu_shutdown(struct mbox_chan *chan)
{
	struct platform_mhu_link *mlink = chan->con_priv;

	free_irq(mlink->irq, chan);
}

static const struct mbox_chan_ops platform_mhu_ops = {
	.send_data = platform_mhu_send_data,
	.startup = platform_mhu_startup,
	.shutdown = platform_mhu_shutdown,
	.last_tx_done = platform_mhu_last_tx_done,
};

static int platform_mhu_probe(struct platform_device *pdev)
{
	int i, err;
	struct platform_mhu *mhu;
	struct device *dev = &pdev->dev;
	int platform_mhu_reg[MHU_CHANS] = {
		MHU_SEC_OFFSET, MHU_LP_OFFSET, MHU_HP_OFFSET
	};

	/* Allocate memory for device */
	mhu = devm_kzalloc(dev, sizeof(*mhu), GFP_KERNEL);
	if (!mhu)
		return -ENOMEM;

	mhu->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mhu->base)) {
		dev_err(dev, "ioremap failed\n");
		return PTR_ERR(mhu->base);
	}

	for (i = 0; i < MHU_CHANS; i++) {
		mhu->chan[i].con_priv = &mhu->mlink[i];
		mhu->mlink[i].irq = platform_get_irq(pdev, i);
		if (mhu->mlink[i].irq < 0)
			return mhu->mlink[i].irq;
		mhu->mlink[i].rx_reg = mhu->base + platform_mhu_reg[i];
		mhu->mlink[i].tx_reg = mhu->mlink[i].rx_reg + TX_REG_OFFSET;
	}

	mhu->mbox.dev = dev;
	mhu->mbox.chans = &mhu->chan[0];
	mhu->mbox.num_chans = MHU_CHANS;
	mhu->mbox.ops = &platform_mhu_ops;
	mhu->mbox.txdone_irq = false;
	mhu->mbox.txdone_poll = true;
	mhu->mbox.txpoll_period = 1;

	platform_set_drvdata(pdev, mhu);

	err = devm_mbox_controller_register(dev, &mhu->mbox);
	if (err) {
		dev_err(dev, "Failed to register mailboxes %d\n", err);
		return err;
	}

	dev_info(dev, "Platform MHU Mailbox registered\n");
	return 0;
}

static const struct of_device_id platform_mhu_dt_ids[] = {
	{ .compatible = "amlogic,meson-gxbb-mhu", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, platform_mhu_dt_ids);

static struct platform_driver platform_mhu_driver = {
	.probe	= platform_mhu_probe,
	.driver = {
		.name = "platform-mhu",
		.of_match_table	= platform_mhu_dt_ids,
	},
};

module_platform_driver(platform_mhu_driver);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:platform-mhu");
MODULE_DESCRIPTION("Platform MHU Driver");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
