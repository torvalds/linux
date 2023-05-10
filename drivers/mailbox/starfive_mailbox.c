// SPDX-License-Identifier: GPL-2.0
/*
 * mailbox driver for StarFive JH7110 SoC
 *
 * Copyright (c) 2021 StarFive Technology Co., Ltd.
 * Author: Shanlong Li <shanlong.li@starfivetech.com>
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>

#include "mailbox.h"

#define MBOX_CHAN_MAX		4

#define MBOX_BASE(mbox, ch)	((mbox)->base + ((ch) * 0x10))
#define MBOX_IRQ_REG		0x00
#define MBOX_SET_REG		0x04
#define MBOX_CLR_REG		0x08
#define MBOX_CMD_REG		0x0c
#define MBC_PEND_SMRY		0x100

typedef enum {
	MAILBOX_CORE_U7 = 0,
	MAILBOX_CORE_HIFI4,
	MAILBOX_CORE_E2,
	MAILBOX_CORE_RSVD0,
	MAILBOX_CORE_NUM,
} mailbox_core_t;

struct mailbox_irq_name_c{
	int id;
	char name[16];
};

static const struct mailbox_irq_name_c irq_peer_name[MBOX_CHAN_MAX] = {
	{MAILBOX_CORE_U7,    "u74_core"},
	{MAILBOX_CORE_HIFI4, "hifi4_core"},
	{MAILBOX_CORE_E2,    "e24_core"},
	{MAILBOX_CORE_RSVD0, "" },
};

/**
 * starfive mailbox channel information
 *
 * A channel can be used for TX or RX, it can trigger remote
 * processor interrupt to notify remote processor and can receive
 * interrupt if has incoming message.
 *
 * @dst_irq:    Interrupt vector for remote processor
 * @core_id:    id for remote processor
 */
struct starfive_chan_info {
	unsigned int dst_irq;
	mailbox_core_t core_id;
};

/**
 * starfive mailbox controller data
 *
 * Mailbox controller includes 4 channels and can allocate
 * channel for message transferring.
 *
 * @dev:    Device to which it is attached
 * @base:    Base address of the register mapping region
 * @chan:    Representation of channels in mailbox controller
 * @mchan:    Representation of channel info
 * @controller:    Representation of a communication channel controller
 */
struct starfive_mbox {
	struct device *dev;
	void __iomem *base;
	struct mbox_chan chan[MBOX_CHAN_MAX];
	struct starfive_chan_info mchan[MBOX_CHAN_MAX];
	struct mbox_controller controller;
	struct clk *clk;
	struct reset_control *rst_rresetn;
};

static struct starfive_mbox *to_starfive_mbox(struct mbox_controller *mbox)
{
	return container_of(mbox, struct starfive_mbox, controller);
}

static struct mbox_chan *
starfive_of_mbox_index_xlate(struct mbox_controller *mbox,
			const struct of_phandle_args *sp)
{
	struct starfive_mbox *sbox;

	int ind = sp->args[0];
	int core_id = sp->args[1];

	if (ind >= mbox->num_chans || core_id >= MAILBOX_CORE_NUM)
		return ERR_PTR(-EINVAL);

	sbox = to_starfive_mbox(mbox);

	sbox->mchan[ind].core_id = core_id;

	return &mbox->chans[ind];
}

static irqreturn_t starfive_rx_irq_handler(int irq, void *p)
{
	struct mbox_chan *chan = p;
	unsigned long ch = (unsigned long)chan->con_priv;
	struct starfive_mbox *mbox = to_starfive_mbox(chan->mbox);
	void __iomem *base = MBOX_BASE(mbox, ch);
	u32 val;

	val = readl(base + MBOX_CMD_REG);
	if (!val)
		return IRQ_NONE;

	mbox_chan_received_data(chan, (void *)&val);
	writel(val, base + MBOX_CLR_REG);
	return IRQ_HANDLED;
}

static int starfive_mbox_check_state(struct mbox_chan *chan)
{
	unsigned long ch = (unsigned long)chan->con_priv;
	struct starfive_mbox *mbox = to_starfive_mbox(chan->mbox);
	unsigned long irq_flag = IRQF_SHARED;
	long ret = 0;

	pm_runtime_get_sync(mbox->dev);
	/* MAILBOX should be with IRQF_NO_SUSPEND set */
	if (!mbox->dev->pm_domain)
		irq_flag |= IRQF_NO_SUSPEND;

	/* Mailbox is idle so directly bail out */
	if (readl(mbox->base + MBC_PEND_SMRY) & BIT(ch))
		return -EBUSY;

	if (mbox->mchan[ch].dst_irq > 0) {
		dev_dbg(mbox->dev, "%s: host IRQ = %d, ch:%ld", __func__, mbox->mchan[ch].dst_irq, ch);
		ret = devm_request_irq(mbox->dev, mbox->mchan[ch].dst_irq, starfive_rx_irq_handler,
			irq_flag, irq_peer_name[ch].name, chan);
		if (ret < 0)
			dev_err(mbox->dev, "request_irq %d failed\n", mbox->mchan[ch].dst_irq);
	}

	return ret;
}

static int starfive_mbox_startup(struct mbox_chan *chan)
{
	return starfive_mbox_check_state(chan);
}

static void starfive_mbox_shutdown(struct mbox_chan *chan)
{
	struct starfive_mbox *mbox = to_starfive_mbox(chan->mbox);
	unsigned long ch = (unsigned long)chan->con_priv;
	void __iomem *base = MBOX_BASE(mbox, ch);

	writel(0x0, base + MBOX_IRQ_REG);
	writel(0x0, base + MBOX_CLR_REG);

	if (mbox->mchan[ch].dst_irq > 0)
		devm_free_irq(mbox->dev, mbox->mchan[ch].dst_irq, chan);
	pm_runtime_put_sync(mbox->dev);
}

static int starfive_mbox_send_data(struct mbox_chan *chan, void *msg)
{
	unsigned long ch = (unsigned long)chan->con_priv;
	struct starfive_mbox *mbox = to_starfive_mbox(chan->mbox);
	struct starfive_chan_info *mchan = &mbox->mchan[ch];
	void __iomem *base = MBOX_BASE(mbox, ch);
	u32 *buf = msg;

	/* Ensure channel is released */
	if (readl(mbox->base + MBC_PEND_SMRY) & BIT(ch)) {
		pr_debug("%s:%d. busy\n", __func__, __LINE__);
		return -EBUSY;
	}

	/* Clear mask for destination interrupt */
	writel(BIT(mchan->core_id), base + MBOX_IRQ_REG);

	/* Fill message data */
	writel(*buf, base + MBOX_SET_REG);
	return 0;
}

static struct mbox_chan_ops starfive_mbox_ops = {
	.startup = starfive_mbox_startup,
	.send_data = starfive_mbox_send_data,
	.shutdown = starfive_mbox_shutdown,
};

static const struct of_device_id starfive_mbox_of_match[] = {
	{ .compatible = "starfive,mail_box",},
	{},
};

MODULE_DEVICE_TABLE(of, starfive_mbox_of_match);

void starfive_mailbox_init(struct starfive_mbox *mbox)
{
	mbox->clk = devm_clk_get_optional(mbox->dev, "clk_apb");
	if (IS_ERR(mbox->clk)) {
		dev_err(mbox->dev, "failed to get mailbox\n");
		return;
	}

	mbox->rst_rresetn = devm_reset_control_get_exclusive(mbox->dev, "mbx_rre");
	if (IS_ERR(mbox->rst_rresetn)) {
		dev_err(mbox->dev, "failed to get mailbox reset\n");
		return;
	}

	clk_prepare_enable(mbox->clk);
	reset_control_deassert(mbox->rst_rresetn);
}

static int starfive_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct starfive_mbox *mbox;
	struct mbox_chan *chan;
	struct resource *res;
	unsigned long ch;
	int err;

	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mbox->base = devm_ioremap_resource(dev, res);
	mbox->dev = dev;

	if (IS_ERR(mbox->base))
		return PTR_ERR(mbox->base);

	starfive_mailbox_init(mbox);

	mbox->controller.dev = dev;
	mbox->controller.chans = mbox->chan;
	mbox->controller.num_chans = MBOX_CHAN_MAX;
	mbox->controller.ops = &starfive_mbox_ops;
	mbox->controller.of_xlate = starfive_of_mbox_index_xlate;
	mbox->controller.txdone_irq = true;
	mbox->controller.txdone_poll = false;

	/* Initialize mailbox channel data */
	chan = mbox->chan;
	for (ch = 0; ch < MBOX_CHAN_MAX; ch++) {
		mbox->mchan[ch].dst_irq = 0;
		mbox->mchan[ch].core_id = (mailbox_core_t)ch;
		chan[ch].con_priv = (void *)ch;
	}
	mbox->mchan[MAILBOX_CORE_HIFI4].dst_irq = platform_get_irq(pdev, 0);
	mbox->mchan[MAILBOX_CORE_E2].dst_irq = platform_get_irq(pdev, 1);

	err = mbox_controller_register(&mbox->controller);
	if (err) {
		dev_err(dev, "Failed to register mailbox %d\n", err);
		return err;
	}

	platform_set_drvdata(pdev, mbox);
	dev_info(dev, "Mailbox enabled\n");
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;
}

static int starfive_mbox_remove(struct platform_device *pdev)
{
	struct starfive_mbox *mbox = platform_get_drvdata(pdev);

	mbox_controller_unregister(&mbox->controller);
	devm_clk_put(mbox->dev, mbox->clk);
	pm_runtime_disable(mbox->dev);

	return 0;
}

static int __maybe_unused starfive_mbox_suspend(struct device *dev)
{
	struct starfive_mbox *mbox = dev_get_drvdata(dev);

	clk_disable_unprepare(mbox->clk);

	return 0;
}

static int __maybe_unused starfive_mbox_resume(struct device *dev)
{
	struct starfive_mbox *mbox = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(mbox->clk);
	if (ret)
		dev_err(dev, "failed to enable clock\n");

	return ret;
}

static const struct dev_pm_ops starfive_mbox_pm_ops = {
	.suspend = starfive_mbox_suspend,
	.resume = starfive_mbox_resume,
	SET_RUNTIME_PM_OPS(starfive_mbox_suspend, starfive_mbox_resume, NULL)
};
static struct platform_driver starfive_mbox_driver = {
	.probe  = starfive_mbox_probe,
	.remove = starfive_mbox_remove,
	.driver = {
	.name = "mailbox",
		.of_match_table = starfive_mbox_of_match,
		.pm = &starfive_mbox_pm_ops,
	},
};

static int __init starfive_mbox_init(void)
{
	return platform_driver_register(&starfive_mbox_driver);
}
core_initcall(starfive_mbox_init);

static void __exit starfive_mbox_exit(void)
{
	platform_driver_unregister(&starfive_mbox_driver);
}
module_exit(starfive_mbox_exit);

MODULE_DESCRIPTION("StarFive Mailbox Controller driver");
MODULE_AUTHOR("Shanlong Li <shanlong.li@starfivetech.com>");
MODULE_LICENSE("GPL");
