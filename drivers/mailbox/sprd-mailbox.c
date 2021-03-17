// SPDX-License-Identifier: GPL-2.0-only
/*
 * Spreadtrum mailbox driver
 *
 * Copyright (c) 2020 Spreadtrum Communications Inc.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#define SPRD_MBOX_ID		0x0
#define SPRD_MBOX_MSG_LOW	0x4
#define SPRD_MBOX_MSG_HIGH	0x8
#define SPRD_MBOX_TRIGGER	0xc
#define SPRD_MBOX_FIFO_RST	0x10
#define SPRD_MBOX_FIFO_STS	0x14
#define SPRD_MBOX_IRQ_STS	0x18
#define SPRD_MBOX_IRQ_MSK	0x1c
#define SPRD_MBOX_LOCK		0x20
#define SPRD_MBOX_FIFO_DEPTH	0x24

/* Bit and mask definiation for inbox's SPRD_MBOX_FIFO_STS register */
#define SPRD_INBOX_FIFO_DELIVER_MASK		GENMASK(23, 16)
#define SPRD_INBOX_FIFO_OVERLOW_MASK		GENMASK(15, 8)
#define SPRD_INBOX_FIFO_DELIVER_SHIFT		16
#define SPRD_INBOX_FIFO_BUSY_MASK		GENMASK(7, 0)

/* Bit and mask definiation for SPRD_MBOX_IRQ_STS register */
#define SPRD_MBOX_IRQ_CLR			BIT(0)

/* Bit and mask definiation for outbox's SPRD_MBOX_FIFO_STS register */
#define SPRD_OUTBOX_FIFO_FULL			BIT(0)
#define SPRD_OUTBOX_FIFO_WR_SHIFT		16
#define SPRD_OUTBOX_FIFO_RD_SHIFT		24
#define SPRD_OUTBOX_FIFO_POS_MASK		GENMASK(7, 0)

/* Bit and mask definiation for inbox's SPRD_MBOX_IRQ_MSK register */
#define SPRD_INBOX_FIFO_BLOCK_IRQ		BIT(0)
#define SPRD_INBOX_FIFO_OVERFLOW_IRQ		BIT(1)
#define SPRD_INBOX_FIFO_DELIVER_IRQ		BIT(2)
#define SPRD_INBOX_FIFO_IRQ_MASK		GENMASK(2, 0)

/* Bit and mask definiation for outbox's SPRD_MBOX_IRQ_MSK register */
#define SPRD_OUTBOX_FIFO_NOT_EMPTY_IRQ		BIT(0)
#define SPRD_OUTBOX_FIFO_IRQ_MASK		GENMASK(4, 0)

#define SPRD_MBOX_CHAN_MAX			8

struct sprd_mbox_priv {
	struct mbox_controller	mbox;
	struct device		*dev;
	void __iomem		*inbox_base;
	void __iomem		*outbox_base;
	struct clk		*clk;
	u32			outbox_fifo_depth;

	struct mbox_chan	chan[SPRD_MBOX_CHAN_MAX];
};

static struct sprd_mbox_priv *to_sprd_mbox_priv(struct mbox_controller *mbox)
{
	return container_of(mbox, struct sprd_mbox_priv, mbox);
}

static u32 sprd_mbox_get_fifo_len(struct sprd_mbox_priv *priv, u32 fifo_sts)
{
	u32 wr_pos = (fifo_sts >> SPRD_OUTBOX_FIFO_WR_SHIFT) &
		SPRD_OUTBOX_FIFO_POS_MASK;
	u32 rd_pos = (fifo_sts >> SPRD_OUTBOX_FIFO_RD_SHIFT) &
		SPRD_OUTBOX_FIFO_POS_MASK;
	u32 fifo_len;

	/*
	 * If the read pointer is equal with write pointer, which means the fifo
	 * is full or empty.
	 */
	if (wr_pos == rd_pos) {
		if (fifo_sts & SPRD_OUTBOX_FIFO_FULL)
			fifo_len = priv->outbox_fifo_depth;
		else
			fifo_len = 0;
	} else if (wr_pos > rd_pos) {
		fifo_len = wr_pos - rd_pos;
	} else {
		fifo_len = priv->outbox_fifo_depth - rd_pos + wr_pos;
	}

	return fifo_len;
}

static irqreturn_t sprd_mbox_outbox_isr(int irq, void *data)
{
	struct sprd_mbox_priv *priv = data;
	struct mbox_chan *chan;
	u32 fifo_sts, fifo_len, msg[2];
	int i, id;

	fifo_sts = readl(priv->outbox_base + SPRD_MBOX_FIFO_STS);

	fifo_len = sprd_mbox_get_fifo_len(priv, fifo_sts);
	if (!fifo_len) {
		dev_warn_ratelimited(priv->dev, "spurious outbox interrupt\n");
		return IRQ_NONE;
	}

	for (i = 0; i < fifo_len; i++) {
		msg[0] = readl(priv->outbox_base + SPRD_MBOX_MSG_LOW);
		msg[1] = readl(priv->outbox_base + SPRD_MBOX_MSG_HIGH);
		id = readl(priv->outbox_base + SPRD_MBOX_ID);

		chan = &priv->chan[id];
		mbox_chan_received_data(chan, (void *)msg);

		/* Trigger to update outbox FIFO pointer */
		writel(0x1, priv->outbox_base + SPRD_MBOX_TRIGGER);
	}

	/* Clear irq status after reading all message. */
	writel(SPRD_MBOX_IRQ_CLR, priv->outbox_base + SPRD_MBOX_IRQ_STS);

	return IRQ_HANDLED;
}

static irqreturn_t sprd_mbox_inbox_isr(int irq, void *data)
{
	struct sprd_mbox_priv *priv = data;
	struct mbox_chan *chan;
	u32 fifo_sts, send_sts, busy, id;

	fifo_sts = readl(priv->inbox_base + SPRD_MBOX_FIFO_STS);

	/* Get the inbox data delivery status */
	send_sts = (fifo_sts & SPRD_INBOX_FIFO_DELIVER_MASK) >>
		SPRD_INBOX_FIFO_DELIVER_SHIFT;
	if (!send_sts) {
		dev_warn_ratelimited(priv->dev, "spurious inbox interrupt\n");
		return IRQ_NONE;
	}

	while (send_sts) {
		id = __ffs(send_sts);
		send_sts &= (send_sts - 1);

		chan = &priv->chan[id];

		/*
		 * Check if the message was fetched by remote traget, if yes,
		 * that means the transmission has been completed.
		 */
		busy = fifo_sts & SPRD_INBOX_FIFO_BUSY_MASK;
		if (!(busy & BIT(id)))
			mbox_chan_txdone(chan, 0);
	}

	/* Clear FIFO delivery and overflow status */
	writel(fifo_sts &
	       (SPRD_INBOX_FIFO_DELIVER_MASK | SPRD_INBOX_FIFO_OVERLOW_MASK),
	       priv->inbox_base + SPRD_MBOX_FIFO_RST);

	/* Clear irq status */
	writel(SPRD_MBOX_IRQ_CLR, priv->inbox_base + SPRD_MBOX_IRQ_STS);

	return IRQ_HANDLED;
}

static int sprd_mbox_send_data(struct mbox_chan *chan, void *msg)
{
	struct sprd_mbox_priv *priv = to_sprd_mbox_priv(chan->mbox);
	unsigned long id = (unsigned long)chan->con_priv;
	u32 *data = msg;

	/* Write data into inbox FIFO, and only support 8 bytes every time */
	writel(data[0], priv->inbox_base + SPRD_MBOX_MSG_LOW);
	writel(data[1], priv->inbox_base + SPRD_MBOX_MSG_HIGH);

	/* Set target core id */
	writel(id, priv->inbox_base + SPRD_MBOX_ID);

	/* Trigger remote request */
	writel(0x1, priv->inbox_base + SPRD_MBOX_TRIGGER);

	return 0;
}

static int sprd_mbox_flush(struct mbox_chan *chan, unsigned long timeout)
{
	struct sprd_mbox_priv *priv = to_sprd_mbox_priv(chan->mbox);
	unsigned long id = (unsigned long)chan->con_priv;
	u32 busy;

	timeout = jiffies + msecs_to_jiffies(timeout);

	while (time_before(jiffies, timeout)) {
		busy = readl(priv->inbox_base + SPRD_MBOX_FIFO_STS) &
			SPRD_INBOX_FIFO_BUSY_MASK;
		if (!(busy & BIT(id))) {
			mbox_chan_txdone(chan, 0);
			return 0;
		}

		udelay(1);
	}

	return -ETIME;
}

static int sprd_mbox_startup(struct mbox_chan *chan)
{
	struct sprd_mbox_priv *priv = to_sprd_mbox_priv(chan->mbox);
	u32 val;

	/* Select outbox FIFO mode and reset the outbox FIFO status */
	writel(0x0, priv->outbox_base + SPRD_MBOX_FIFO_RST);

	/* Enable inbox FIFO overflow and delivery interrupt */
	val = readl(priv->inbox_base + SPRD_MBOX_IRQ_MSK);
	val &= ~(SPRD_INBOX_FIFO_OVERFLOW_IRQ | SPRD_INBOX_FIFO_DELIVER_IRQ);
	writel(val, priv->inbox_base + SPRD_MBOX_IRQ_MSK);

	/* Enable outbox FIFO not empty interrupt */
	val = readl(priv->outbox_base + SPRD_MBOX_IRQ_MSK);
	val &= ~SPRD_OUTBOX_FIFO_NOT_EMPTY_IRQ;
	writel(val, priv->outbox_base + SPRD_MBOX_IRQ_MSK);

	return 0;
}

static void sprd_mbox_shutdown(struct mbox_chan *chan)
{
	struct sprd_mbox_priv *priv = to_sprd_mbox_priv(chan->mbox);

	/* Disable inbox & outbox interrupt */
	writel(SPRD_INBOX_FIFO_IRQ_MASK, priv->inbox_base + SPRD_MBOX_IRQ_MSK);
	writel(SPRD_OUTBOX_FIFO_IRQ_MASK, priv->outbox_base + SPRD_MBOX_IRQ_MSK);
}

static const struct mbox_chan_ops sprd_mbox_ops = {
	.send_data	= sprd_mbox_send_data,
	.flush		= sprd_mbox_flush,
	.startup	= sprd_mbox_startup,
	.shutdown	= sprd_mbox_shutdown,
};

static void sprd_mbox_disable(void *data)
{
	struct sprd_mbox_priv *priv = data;

	clk_disable_unprepare(priv->clk);
}

static int sprd_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sprd_mbox_priv *priv;
	int ret, inbox_irq, outbox_irq;
	unsigned long id;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	/*
	 * The Spreadtrum mailbox uses an inbox to send messages to the target
	 * core, and uses an outbox to receive messages from other cores.
	 *
	 * Thus the mailbox controller supplies 2 different register addresses
	 * and IRQ numbers for inbox and outbox.
	 */
	priv->inbox_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->inbox_base))
		return PTR_ERR(priv->inbox_base);

	priv->outbox_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(priv->outbox_base))
		return PTR_ERR(priv->outbox_base);

	priv->clk = devm_clk_get(dev, "enable");
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to get mailbox clock\n");
		return PTR_ERR(priv->clk);
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, sprd_mbox_disable, priv);
	if (ret) {
		dev_err(dev, "failed to add mailbox disable action\n");
		return ret;
	}

	inbox_irq = platform_get_irq(pdev, 0);
	if (inbox_irq < 0)
		return inbox_irq;

	ret = devm_request_irq(dev, inbox_irq, sprd_mbox_inbox_isr,
			       IRQF_NO_SUSPEND, dev_name(dev), priv);
	if (ret) {
		dev_err(dev, "failed to request inbox IRQ: %d\n", ret);
		return ret;
	}

	outbox_irq = platform_get_irq(pdev, 1);
	if (outbox_irq < 0)
		return outbox_irq;

	ret = devm_request_irq(dev, outbox_irq, sprd_mbox_outbox_isr,
			       IRQF_NO_SUSPEND, dev_name(dev), priv);
	if (ret) {
		dev_err(dev, "failed to request outbox IRQ: %d\n", ret);
		return ret;
	}

	/* Get the default outbox FIFO depth */
	priv->outbox_fifo_depth =
		readl(priv->outbox_base + SPRD_MBOX_FIFO_DEPTH) + 1;
	priv->mbox.dev = dev;
	priv->mbox.chans = &priv->chan[0];
	priv->mbox.num_chans = SPRD_MBOX_CHAN_MAX;
	priv->mbox.ops = &sprd_mbox_ops;
	priv->mbox.txdone_irq = true;

	for (id = 0; id < SPRD_MBOX_CHAN_MAX; id++)
		priv->chan[id].con_priv = (void *)id;

	ret = devm_mbox_controller_register(dev, &priv->mbox);
	if (ret) {
		dev_err(dev, "failed to register mailbox: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id sprd_mbox_of_match[] = {
	{ .compatible = "sprd,sc9860-mailbox", },
	{ },
};
MODULE_DEVICE_TABLE(of, sprd_mbox_of_match);

static struct platform_driver sprd_mbox_driver = {
	.driver = {
		.name = "sprd-mailbox",
		.of_match_table = sprd_mbox_of_match,
	},
	.probe	= sprd_mbox_probe,
};
module_platform_driver(sprd_mbox_driver);

MODULE_AUTHOR("Baolin Wang <baolin.wang@unisoc.com>");
MODULE_DESCRIPTION("Spreadtrum mailbox driver");
MODULE_LICENSE("GPL v2");
