// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017-2018 HiSilicon Limited.
// Copyright (c) 2017-2018 Linaro Limited.

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "mailbox.h"

#define MBOX_CHAN_MAX			32

#define MBOX_RX				0x0
#define MBOX_TX				0x1

#define MBOX_BASE(mbox, ch)		((mbox)->base + ((ch) * 0x40))
#define MBOX_SRC_REG			0x00
#define MBOX_DST_REG			0x04
#define MBOX_DCLR_REG			0x08
#define MBOX_DSTAT_REG			0x0c
#define MBOX_MODE_REG			0x10
#define MBOX_IMASK_REG			0x14
#define MBOX_ICLR_REG			0x18
#define MBOX_SEND_REG			0x1c
#define MBOX_DATA_REG			0x20

#define MBOX_IPC_LOCK_REG		0xa00
#define MBOX_IPC_UNLOCK			0x1acce551

#define MBOX_AUTOMATIC_ACK		1

#define MBOX_STATE_IDLE			BIT(4)
#define MBOX_STATE_READY		BIT(5)
#define MBOX_STATE_ACK			BIT(7)

#define MBOX_MSG_LEN			8

/**
 * struct hi3660_chan_info - Hi3660 mailbox channel information
 * @dst_irq:	Interrupt vector for remote processor
 * @ack_irq:	Interrupt vector for local processor
 *
 * A channel can be used for TX or RX, it can trigger remote
 * processor interrupt to notify remote processor and can receive
 * interrupt if it has an incoming message.
 */
struct hi3660_chan_info {
	unsigned int dst_irq;
	unsigned int ack_irq;
};

/**
 * struct hi3660_mbox - Hi3660 mailbox controller data
 * @dev:	Device to which it is attached
 * @base:	Base address of the register mapping region
 * @chan:	Representation of channels in mailbox controller
 * @mchan:	Representation of channel info
 * @controller:	Representation of a communication channel controller
 *
 * Mailbox controller includes 32 channels and can allocate
 * channel for message transferring.
 */
struct hi3660_mbox {
	struct device *dev;
	void __iomem *base;
	struct mbox_chan chan[MBOX_CHAN_MAX];
	struct hi3660_chan_info mchan[MBOX_CHAN_MAX];
	struct mbox_controller controller;
};

static struct hi3660_mbox *to_hi3660_mbox(struct mbox_controller *mbox)
{
	return container_of(mbox, struct hi3660_mbox, controller);
}

static int hi3660_mbox_check_state(struct mbox_chan *chan)
{
	unsigned long ch = (unsigned long)chan->con_priv;
	struct hi3660_mbox *mbox = to_hi3660_mbox(chan->mbox);
	struct hi3660_chan_info *mchan = &mbox->mchan[ch];
	void __iomem *base = MBOX_BASE(mbox, ch);
	unsigned long val;
	unsigned int ret;

	/* Mailbox is ready to use */
	if (readl(base + MBOX_MODE_REG) & MBOX_STATE_READY)
		return 0;

	/* Wait for acknowledge from remote */
	ret = readx_poll_timeout_atomic(readl, base + MBOX_MODE_REG,
			val, (val & MBOX_STATE_ACK), 1000, 300000);
	if (ret) {
		dev_err(mbox->dev, "%s: timeout for receiving ack\n", __func__);
		return ret;
	}

	/* clear ack state, mailbox will get back to ready state */
	writel(BIT(mchan->ack_irq), base + MBOX_ICLR_REG);

	return 0;
}

static int hi3660_mbox_unlock(struct mbox_chan *chan)
{
	struct hi3660_mbox *mbox = to_hi3660_mbox(chan->mbox);
	unsigned int val, retry = 3;

	do {
		writel(MBOX_IPC_UNLOCK, mbox->base + MBOX_IPC_LOCK_REG);

		val = readl(mbox->base + MBOX_IPC_LOCK_REG);
		if (!val)
			break;

		udelay(10);
	} while (retry--);

	if (val)
		dev_err(mbox->dev, "%s: failed to unlock mailbox\n", __func__);

	return (!val) ? 0 : -ETIMEDOUT;
}

static int hi3660_mbox_acquire_channel(struct mbox_chan *chan)
{
	unsigned long ch = (unsigned long)chan->con_priv;
	struct hi3660_mbox *mbox = to_hi3660_mbox(chan->mbox);
	struct hi3660_chan_info *mchan = &mbox->mchan[ch];
	void __iomem *base = MBOX_BASE(mbox, ch);
	unsigned int val, retry;

	for (retry = 10; retry; retry--) {
		/* Check if channel is in idle state */
		if (readl(base + MBOX_MODE_REG) & MBOX_STATE_IDLE) {
			writel(BIT(mchan->ack_irq), base + MBOX_SRC_REG);

			/* Check ack bit has been set successfully */
			val = readl(base + MBOX_SRC_REG);
			if (val & BIT(mchan->ack_irq))
				break;
		}
	}

	if (!retry)
		dev_err(mbox->dev, "%s: failed to acquire channel\n", __func__);

	return retry ? 0 : -ETIMEDOUT;
}

static int hi3660_mbox_startup(struct mbox_chan *chan)
{
	int ret;

	ret = hi3660_mbox_unlock(chan);
	if (ret)
		return ret;

	ret = hi3660_mbox_acquire_channel(chan);
	if (ret)
		return ret;

	return 0;
}

static int hi3660_mbox_send_data(struct mbox_chan *chan, void *msg)
{
	unsigned long ch = (unsigned long)chan->con_priv;
	struct hi3660_mbox *mbox = to_hi3660_mbox(chan->mbox);
	struct hi3660_chan_info *mchan = &mbox->mchan[ch];
	void __iomem *base = MBOX_BASE(mbox, ch);
	u32 *buf = msg;
	unsigned int i;
	int ret;

	ret = hi3660_mbox_check_state(chan);
	if (ret)
		return ret;

	/* Clear mask for destination interrupt */
	writel_relaxed(~BIT(mchan->dst_irq), base + MBOX_IMASK_REG);

	/* Config destination for interrupt vector */
	writel_relaxed(BIT(mchan->dst_irq), base + MBOX_DST_REG);

	/* Automatic acknowledge mode */
	writel_relaxed(MBOX_AUTOMATIC_ACK, base + MBOX_MODE_REG);

	/* Fill message data */
	for (i = 0; i < MBOX_MSG_LEN; i++)
		writel_relaxed(buf[i], base + MBOX_DATA_REG + i * 4);

	/* Trigger data transferring */
	writel(BIT(mchan->ack_irq), base + MBOX_SEND_REG);
	return 0;
}

static const struct mbox_chan_ops hi3660_mbox_ops = {
	.startup	= hi3660_mbox_startup,
	.send_data	= hi3660_mbox_send_data,
};

static struct mbox_chan *hi3660_mbox_xlate(struct mbox_controller *controller,
					   const struct of_phandle_args *spec)
{
	struct hi3660_mbox *mbox = to_hi3660_mbox(controller);
	struct hi3660_chan_info *mchan;
	unsigned int ch = spec->args[0];

	if (ch >= MBOX_CHAN_MAX) {
		dev_err(mbox->dev, "Invalid channel idx %d\n", ch);
		return ERR_PTR(-EINVAL);
	}

	mchan = &mbox->mchan[ch];
	mchan->dst_irq = spec->args[1];
	mchan->ack_irq = spec->args[2];

	return &mbox->chan[ch];
}

static const struct of_device_id hi3660_mbox_of_match[] = {
	{ .compatible = "hisilicon,hi3660-mbox", },
	{},
};

MODULE_DEVICE_TABLE(of, hi3660_mbox_of_match);

static int hi3660_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hi3660_mbox *mbox;
	struct mbox_chan *chan;
	unsigned long ch;
	int err;

	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	mbox->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mbox->base))
		return PTR_ERR(mbox->base);

	mbox->dev = dev;
	mbox->controller.dev = dev;
	mbox->controller.chans = mbox->chan;
	mbox->controller.num_chans = MBOX_CHAN_MAX;
	mbox->controller.ops = &hi3660_mbox_ops;
	mbox->controller.of_xlate = hi3660_mbox_xlate;

	/* Initialize mailbox channel data */
	chan = mbox->chan;
	for (ch = 0; ch < MBOX_CHAN_MAX; ch++)
		chan[ch].con_priv = (void *)ch;

	err = devm_mbox_controller_register(dev, &mbox->controller);
	if (err) {
		dev_err(dev, "Failed to register mailbox %d\n", err);
		return err;
	}

	platform_set_drvdata(pdev, mbox);
	dev_info(dev, "Mailbox enabled\n");
	return 0;
}

static struct platform_driver hi3660_mbox_driver = {
	.probe  = hi3660_mbox_probe,
	.driver = {
		.name = "hi3660-mbox",
		.of_match_table = hi3660_mbox_of_match,
	},
};

static int __init hi3660_mbox_init(void)
{
	return platform_driver_register(&hi3660_mbox_driver);
}
core_initcall(hi3660_mbox_init);

static void __exit hi3660_mbox_exit(void)
{
	platform_driver_unregister(&hi3660_mbox_driver);
}
module_exit(hi3660_mbox_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hisilicon Hi3660 Mailbox Controller");
MODULE_AUTHOR("Leo Yan <leo.yan@linaro.org>");
