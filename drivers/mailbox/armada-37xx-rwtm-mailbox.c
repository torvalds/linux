// SPDX-License-Identifier: GPL-2.0+
/*
 * rWTM BIU Mailbox driver for Armada 37xx
 *
 * Author: Marek Behun <marek.behun@nic.cz>
 */

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/armada-37xx-rwtm-mailbox.h>

#define DRIVER_NAME	"armada-37xx-rwtm-mailbox"

/* relative to rWTM BIU Mailbox Registers */
#define RWTM_MBOX_PARAM(i)		(0x0 + ((i) << 2))
#define RWTM_MBOX_COMMAND		0x40
#define RWTM_MBOX_RETURN_STATUS		0x80
#define RWTM_MBOX_STATUS(i)		(0x84 + ((i) << 2))
#define RWTM_MBOX_FIFO_STATUS		0xc4
#define FIFO_STS_RDY			0x100
#define FIFO_STS_CNTR_MASK		0x7
#define FIFO_STS_CNTR_MAX		4

#define RWTM_HOST_INT_RESET		0xc8
#define RWTM_HOST_INT_MASK		0xcc
#define SP_CMD_COMPLETE			BIT(0)
#define SP_CMD_QUEUE_FULL_ACCESS	BIT(17)
#define SP_CMD_QUEUE_FULL		BIT(18)

struct a37xx_mbox {
	struct device *dev;
	struct mbox_controller controller;
	void __iomem *base;
	int irq;
};

static void a37xx_mbox_receive(struct mbox_chan *chan)
{
	struct a37xx_mbox *mbox = chan->con_priv;
	struct armada_37xx_rwtm_rx_msg rx_msg;
	int i;

	rx_msg.retval = readl(mbox->base + RWTM_MBOX_RETURN_STATUS);
	for (i = 0; i < 16; ++i)
		rx_msg.status[i] = readl(mbox->base + RWTM_MBOX_STATUS(i));

	mbox_chan_received_data(chan, &rx_msg);
}

static irqreturn_t a37xx_mbox_irq_handler(int irq, void *data)
{
	struct mbox_chan *chan = data;
	struct a37xx_mbox *mbox = chan->con_priv;
	u32 reg;

	reg = readl(mbox->base + RWTM_HOST_INT_RESET);

	if (reg & SP_CMD_COMPLETE)
		a37xx_mbox_receive(chan);

	if (reg & (SP_CMD_QUEUE_FULL_ACCESS | SP_CMD_QUEUE_FULL))
		dev_err(mbox->dev, "Secure processor command queue full\n");

	writel(reg, mbox->base + RWTM_HOST_INT_RESET);
	if (reg)
		mbox_chan_txdone(chan, 0);

	return reg ? IRQ_HANDLED : IRQ_NONE;
}

static int a37xx_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct a37xx_mbox *mbox = chan->con_priv;
	struct armada_37xx_rwtm_tx_msg *msg = data;
	int i;
	u32 reg;

	if (!data)
		return -EINVAL;

	reg = readl(mbox->base + RWTM_MBOX_FIFO_STATUS);
	if (!(reg & FIFO_STS_RDY))
		dev_warn(mbox->dev, "Secure processor not ready\n");

	if ((reg & FIFO_STS_CNTR_MASK) >= FIFO_STS_CNTR_MAX) {
		dev_err(mbox->dev, "Secure processor command queue full\n");
		return -EBUSY;
	}

	for (i = 0; i < 16; ++i)
		writel(msg->args[i], mbox->base + RWTM_MBOX_PARAM(i));
	writel(msg->command, mbox->base + RWTM_MBOX_COMMAND);

	return 0;
}

static int a37xx_mbox_startup(struct mbox_chan *chan)
{
	struct a37xx_mbox *mbox = chan->con_priv;
	u32 reg;
	int ret;

	ret = devm_request_irq(mbox->dev, mbox->irq, a37xx_mbox_irq_handler, 0,
			       DRIVER_NAME, chan);
	if (ret < 0) {
		dev_err(mbox->dev, "Cannot request irq\n");
		return ret;
	}

	/* enable IRQ generation */
	reg = readl(mbox->base + RWTM_HOST_INT_MASK);
	reg &= ~(SP_CMD_COMPLETE | SP_CMD_QUEUE_FULL_ACCESS | SP_CMD_QUEUE_FULL);
	writel(reg, mbox->base + RWTM_HOST_INT_MASK);

	return 0;
}

static void a37xx_mbox_shutdown(struct mbox_chan *chan)
{
	u32 reg;
	struct a37xx_mbox *mbox = chan->con_priv;

	/* disable interrupt generation */
	reg = readl(mbox->base + RWTM_HOST_INT_MASK);
	reg |= SP_CMD_COMPLETE | SP_CMD_QUEUE_FULL_ACCESS | SP_CMD_QUEUE_FULL;
	writel(reg, mbox->base + RWTM_HOST_INT_MASK);

	devm_free_irq(mbox->dev, mbox->irq, chan);
}

static const struct mbox_chan_ops a37xx_mbox_ops = {
	.send_data	= a37xx_mbox_send_data,
	.startup	= a37xx_mbox_startup,
	.shutdown	= a37xx_mbox_shutdown,
};

static int armada_37xx_mbox_probe(struct platform_device *pdev)
{
	struct a37xx_mbox *mbox;
	struct mbox_chan *chans;
	int ret;

	mbox = devm_kzalloc(&pdev->dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	/* Allocated one channel */
	chans = devm_kzalloc(&pdev->dev, sizeof(*chans), GFP_KERNEL);
	if (!chans)
		return -ENOMEM;

	mbox->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mbox->base)) {
		dev_err(&pdev->dev, "ioremap failed\n");
		return PTR_ERR(mbox->base);
	}

	mbox->irq = platform_get_irq(pdev, 0);
	if (mbox->irq < 0) {
		dev_err(&pdev->dev, "Cannot get irq\n");
		return mbox->irq;
	}

	mbox->dev = &pdev->dev;

	/* Hardware supports only one channel. */
	chans[0].con_priv = mbox;
	mbox->controller.dev = mbox->dev;
	mbox->controller.num_chans = 1;
	mbox->controller.chans = chans;
	mbox->controller.ops = &a37xx_mbox_ops;
	mbox->controller.txdone_irq = true;

	ret = devm_mbox_controller_register(mbox->dev, &mbox->controller);
	if (ret) {
		dev_err(&pdev->dev, "Could not register mailbox controller\n");
		return ret;
	}

	platform_set_drvdata(pdev, mbox);
	return ret;
}


static const struct of_device_id armada_37xx_mbox_match[] = {
	{ .compatible = "marvell,armada-3700-rwtm-mailbox" },
	{ },
};

MODULE_DEVICE_TABLE(of, armada_37xx_mbox_match);

static struct platform_driver armada_37xx_mbox_driver = {
	.probe	= armada_37xx_mbox_probe,
	.driver	= {
		.name		= DRIVER_NAME,
		.of_match_table	= armada_37xx_mbox_match,
	},
};

module_platform_driver(armada_37xx_mbox_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("rWTM BIU Mailbox driver for Armada 37xx");
MODULE_AUTHOR("Marek Behun <marek.behun@nic.cz>");
