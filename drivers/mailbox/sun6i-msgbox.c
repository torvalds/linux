// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2017-2019 Samuel Holland <samuel@sholland.org>

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/spinlock.h>

#define NUM_CHANS		8

#define CTRL_REG(n)		(0x0000 + 0x4 * ((n) / 4))
#define CTRL_RX(n)		BIT(0 + 8 * ((n) % 4))
#define CTRL_TX(n)		BIT(4 + 8 * ((n) % 4))

#define REMOTE_IRQ_EN_REG	0x0040
#define REMOTE_IRQ_STAT_REG	0x0050
#define LOCAL_IRQ_EN_REG	0x0060
#define LOCAL_IRQ_STAT_REG	0x0070

#define RX_IRQ(n)		BIT(0 + 2 * (n))
#define RX_IRQ_MASK		0x5555
#define TX_IRQ(n)		BIT(1 + 2 * (n))
#define TX_IRQ_MASK		0xaaaa

#define FIFO_STAT_REG(n)	(0x0100 + 0x4 * (n))
#define FIFO_STAT_MASK		GENMASK(0, 0)

#define MSG_STAT_REG(n)		(0x0140 + 0x4 * (n))
#define MSG_STAT_MASK		GENMASK(2, 0)

#define MSG_DATA_REG(n)		(0x0180 + 0x4 * (n))

#define mbox_dbg(mbox, ...)	dev_dbg((mbox)->controller.dev, __VA_ARGS__)

struct sun6i_msgbox {
	struct mbox_controller controller;
	struct clk *clk;
	spinlock_t lock;
	void __iomem *regs;
};

static bool sun6i_msgbox_last_tx_done(struct mbox_chan *chan);
static bool sun6i_msgbox_peek_data(struct mbox_chan *chan);

static inline int channel_number(struct mbox_chan *chan)
{
	return chan - chan->mbox->chans;
}

static inline struct sun6i_msgbox *to_sun6i_msgbox(struct mbox_chan *chan)
{
	return chan->con_priv;
}

static irqreturn_t sun6i_msgbox_irq(int irq, void *dev_id)
{
	struct sun6i_msgbox *mbox = dev_id;
	uint32_t status;
	int n;

	/* Only examine channels that are currently enabled. */
	status = readl(mbox->regs + LOCAL_IRQ_EN_REG) &
		 readl(mbox->regs + LOCAL_IRQ_STAT_REG);

	if (!(status & RX_IRQ_MASK))
		return IRQ_NONE;

	for (n = 0; n < NUM_CHANS; ++n) {
		struct mbox_chan *chan = &mbox->controller.chans[n];

		if (!(status & RX_IRQ(n)))
			continue;

		while (sun6i_msgbox_peek_data(chan)) {
			uint32_t msg = readl(mbox->regs + MSG_DATA_REG(n));

			mbox_dbg(mbox, "Channel %d received 0x%08x\n", n, msg);
			mbox_chan_received_data(chan, &msg);
		}

		/* The IRQ can be cleared only once the FIFO is empty. */
		writel(RX_IRQ(n), mbox->regs + LOCAL_IRQ_STAT_REG);
	}

	return IRQ_HANDLED;
}

static int sun6i_msgbox_send_data(struct mbox_chan *chan, void *data)
{
	struct sun6i_msgbox *mbox = to_sun6i_msgbox(chan);
	int n = channel_number(chan);
	uint32_t msg = *(uint32_t *)data;

	/* Using a channel backwards gets the hardware into a bad state. */
	if (WARN_ON_ONCE(!(readl(mbox->regs + CTRL_REG(n)) & CTRL_TX(n))))
		return 0;

	writel(msg, mbox->regs + MSG_DATA_REG(n));
	mbox_dbg(mbox, "Channel %d sent 0x%08x\n", n, msg);

	return 0;
}

static int sun6i_msgbox_startup(struct mbox_chan *chan)
{
	struct sun6i_msgbox *mbox = to_sun6i_msgbox(chan);
	int n = channel_number(chan);

	/* The coprocessor is responsible for setting channel directions. */
	if (readl(mbox->regs + CTRL_REG(n)) & CTRL_RX(n)) {
		/* Flush the receive FIFO. */
		while (sun6i_msgbox_peek_data(chan))
			readl(mbox->regs + MSG_DATA_REG(n));
		writel(RX_IRQ(n), mbox->regs + LOCAL_IRQ_STAT_REG);

		/* Enable the receive IRQ. */
		spin_lock(&mbox->lock);
		writel(readl(mbox->regs + LOCAL_IRQ_EN_REG) | RX_IRQ(n),
		       mbox->regs + LOCAL_IRQ_EN_REG);
		spin_unlock(&mbox->lock);
	}

	mbox_dbg(mbox, "Channel %d startup complete\n", n);

	return 0;
}

static void sun6i_msgbox_shutdown(struct mbox_chan *chan)
{
	struct sun6i_msgbox *mbox = to_sun6i_msgbox(chan);
	int n = channel_number(chan);

	if (readl(mbox->regs + CTRL_REG(n)) & CTRL_RX(n)) {
		/* Disable the receive IRQ. */
		spin_lock(&mbox->lock);
		writel(readl(mbox->regs + LOCAL_IRQ_EN_REG) & ~RX_IRQ(n),
		       mbox->regs + LOCAL_IRQ_EN_REG);
		spin_unlock(&mbox->lock);

		/* Attempt to flush the FIFO until the IRQ is cleared. */
		do {
			while (sun6i_msgbox_peek_data(chan))
				readl(mbox->regs + MSG_DATA_REG(n));
			writel(RX_IRQ(n), mbox->regs + LOCAL_IRQ_STAT_REG);
		} while (readl(mbox->regs + LOCAL_IRQ_STAT_REG) & RX_IRQ(n));
	}

	mbox_dbg(mbox, "Channel %d shutdown complete\n", n);
}

static bool sun6i_msgbox_last_tx_done(struct mbox_chan *chan)
{
	struct sun6i_msgbox *mbox = to_sun6i_msgbox(chan);
	int n = channel_number(chan);

	/*
	 * The hardware allows snooping on the remote user's IRQ statuses.
	 * We consider a message to be acknowledged only once the receive IRQ
	 * for that channel is cleared. Since the receive IRQ for a channel
	 * cannot be cleared until the FIFO for that channel is empty, this
	 * ensures that the message has actually been read. It also gives the
	 * recipient an opportunity to perform minimal processing before
	 * acknowledging the message.
	 */
	return !(readl(mbox->regs + REMOTE_IRQ_STAT_REG) & RX_IRQ(n));
}

static bool sun6i_msgbox_peek_data(struct mbox_chan *chan)
{
	struct sun6i_msgbox *mbox = to_sun6i_msgbox(chan);
	int n = channel_number(chan);

	return readl(mbox->regs + MSG_STAT_REG(n)) & MSG_STAT_MASK;
}

static const struct mbox_chan_ops sun6i_msgbox_chan_ops = {
	.send_data    = sun6i_msgbox_send_data,
	.startup      = sun6i_msgbox_startup,
	.shutdown     = sun6i_msgbox_shutdown,
	.last_tx_done = sun6i_msgbox_last_tx_done,
	.peek_data    = sun6i_msgbox_peek_data,
};

static int sun6i_msgbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mbox_chan *chans;
	struct reset_control *reset;
	struct resource *res;
	struct sun6i_msgbox *mbox;
	int i, ret;

	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	chans = devm_kcalloc(dev, NUM_CHANS, sizeof(*chans), GFP_KERNEL);
	if (!chans)
		return -ENOMEM;

	for (i = 0; i < NUM_CHANS; ++i)
		chans[i].con_priv = mbox;

	mbox->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(mbox->clk)) {
		ret = PTR_ERR(mbox->clk);
		dev_err(dev, "Failed to get clock: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(mbox->clk);
	if (ret) {
		dev_err(dev, "Failed to enable clock: %d\n", ret);
		return ret;
	}

	reset = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(reset)) {
		ret = PTR_ERR(reset);
		dev_err(dev, "Failed to get reset control: %d\n", ret);
		goto err_disable_unprepare;
	}

	/*
	 * NOTE: We rely on platform firmware to preconfigure the channel
	 * directions, and we share this hardware block with other firmware
	 * that runs concurrently with Linux (e.g. a trusted monitor).
	 *
	 * Therefore, we do *not* assert the reset line if probing fails or
	 * when removing the device.
	 */
	ret = reset_control_deassert(reset);
	if (ret) {
		dev_err(dev, "Failed to deassert reset: %d\n", ret);
		goto err_disable_unprepare;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENODEV;
		goto err_disable_unprepare;
	}

	mbox->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mbox->regs)) {
		ret = PTR_ERR(mbox->regs);
		dev_err(dev, "Failed to map MMIO resource: %d\n", ret);
		goto err_disable_unprepare;
	}

	/* Disable all IRQs for this end of the msgbox. */
	writel(0, mbox->regs + LOCAL_IRQ_EN_REG);

	ret = devm_request_irq(dev, irq_of_parse_and_map(dev->of_node, 0),
			       sun6i_msgbox_irq, 0, dev_name(dev), mbox);
	if (ret) {
		dev_err(dev, "Failed to register IRQ handler: %d\n", ret);
		goto err_disable_unprepare;
	}

	mbox->controller.dev           = dev;
	mbox->controller.ops           = &sun6i_msgbox_chan_ops;
	mbox->controller.chans         = chans;
	mbox->controller.num_chans     = NUM_CHANS;
	mbox->controller.txdone_irq    = false;
	mbox->controller.txdone_poll   = true;
	mbox->controller.txpoll_period = 5;

	spin_lock_init(&mbox->lock);
	platform_set_drvdata(pdev, mbox);

	ret = mbox_controller_register(&mbox->controller);
	if (ret) {
		dev_err(dev, "Failed to register controller: %d\n", ret);
		goto err_disable_unprepare;
	}

	return 0;

err_disable_unprepare:
	clk_disable_unprepare(mbox->clk);

	return ret;
}

static int sun6i_msgbox_remove(struct platform_device *pdev)
{
	struct sun6i_msgbox *mbox = platform_get_drvdata(pdev);

	mbox_controller_unregister(&mbox->controller);
	/* See the comment in sun6i_msgbox_probe about the reset line. */
	clk_disable_unprepare(mbox->clk);

	return 0;
}

static const struct of_device_id sun6i_msgbox_of_match[] = {
	{ .compatible = "allwinner,sun6i-a31-msgbox", },
	{},
};
MODULE_DEVICE_TABLE(of, sun6i_msgbox_of_match);

static struct platform_driver sun6i_msgbox_driver = {
	.driver = {
		.name = "sun6i-msgbox",
		.of_match_table = sun6i_msgbox_of_match,
	},
	.probe  = sun6i_msgbox_probe,
	.remove = sun6i_msgbox_remove,
};
module_platform_driver(sun6i_msgbox_driver);

MODULE_AUTHOR("Samuel Holland <samuel@sholland.org>");
MODULE_DESCRIPTION("Allwinner sun6i/sun8i/sun9i/sun50i Message Box");
MODULE_LICENSE("GPL v2");
