// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#define MAILBOX_A2B_INTEN		0x00
#define MAILBOX_A2B_STATUS		0x04
#define MAILBOX_A2B_CMD(x)		(0x08 + (x) * 8)
#define MAILBOX_A2B_DAT(x)		(0x0c + (x) * 8)

#define MAILBOX_B2A_INTEN		0x28
#define MAILBOX_B2A_STATUS		0x2C
#define MAILBOX_B2A_CMD(x)		(0x30 + (x) * 8)
#define MAILBOX_B2A_DAT(x)		(0x34 + (x) * 8)

struct rockchip_mbox_msg {
	u32 cmd;
	int rx_size;
};

struct rockchip_mbox_data {
	int num_chans;
};

struct rockchip_mbox_chan {
	int idx;
	int irq;
	struct rockchip_mbox_msg *msg;
	struct rockchip_mbox *mb;
};

struct rockchip_mbox {
	struct mbox_controller mbox;
	struct clk *pclk;
	void __iomem *mbox_base;

	/* The maximum size of buf for each channel */
	u32 buf_size;

	struct rockchip_mbox_chan *chans;
};

static int rockchip_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct rockchip_mbox *mb = dev_get_drvdata(chan->mbox->dev);
	struct rockchip_mbox_msg *msg = data;
	struct rockchip_mbox_chan *chans = mb->chans;

	if (!msg)
		return -EINVAL;

	if (msg->rx_size > mb->buf_size) {
		dev_err(mb->mbox.dev, "Transmit size over buf size(%d)\n",
			mb->buf_size);
		return -EINVAL;
	}

	dev_dbg(mb->mbox.dev, "Chan[%d]: A2B message, cmd 0x%08x\n",
		chans->idx, msg->cmd);

	mb->chans[chans->idx].msg = msg;

	writel_relaxed(msg->cmd, mb->mbox_base + MAILBOX_A2B_CMD(chans->idx));
	writel_relaxed(msg->rx_size, mb->mbox_base +
		       MAILBOX_A2B_DAT(chans->idx));

	return 0;
}

static int rockchip_mbox_startup(struct mbox_chan *chan)
{
	struct rockchip_mbox *mb = dev_get_drvdata(chan->mbox->dev);

	/* Enable all B2A interrupts */
	writel_relaxed((1 << mb->mbox.num_chans) - 1,
		       mb->mbox_base + MAILBOX_B2A_INTEN);

	return 0;
}

static void rockchip_mbox_shutdown(struct mbox_chan *chan)
{
	struct rockchip_mbox *mb = dev_get_drvdata(chan->mbox->dev);
	struct rockchip_mbox_chan *chans = mb->chans;

	/* Disable all B2A interrupts */
	writel_relaxed(0, mb->mbox_base + MAILBOX_B2A_INTEN);

	mb->chans[chans->idx].msg = NULL;
}

static const struct mbox_chan_ops rockchip_mbox_chan_ops = {
	.send_data	= rockchip_mbox_send_data,
	.startup	= rockchip_mbox_startup,
	.shutdown	= rockchip_mbox_shutdown,
};

static irqreturn_t rockchip_mbox_irq(int irq, void *dev_id)
{
	int idx;
	struct rockchip_mbox *mb = (struct rockchip_mbox *)dev_id;
	u32 status = readl_relaxed(mb->mbox_base + MAILBOX_B2A_STATUS);

	for (idx = 0; idx < mb->mbox.num_chans; idx++) {
		if ((status & (1 << idx)) && (irq == mb->chans[idx].irq)) {
			/* Clear mbox interrupt */
			writel_relaxed(1 << idx,
				       mb->mbox_base + MAILBOX_B2A_STATUS);
			return IRQ_WAKE_THREAD;
		}
	}

	return IRQ_NONE;
}

static irqreturn_t rockchip_mbox_isr(int irq, void *dev_id)
{
	int idx;
	struct rockchip_mbox_msg *msg = NULL;
	struct rockchip_mbox *mb = (struct rockchip_mbox *)dev_id;

	for (idx = 0; idx < mb->mbox.num_chans; idx++) {
		if (irq != mb->chans[idx].irq)
			continue;

		msg = mb->chans[idx].msg;
		if (!msg) {
			dev_err(mb->mbox.dev,
				"Chan[%d]: B2A message is NULL\n", idx);
			break; /* spurious */
		}

		mbox_chan_received_data(&mb->mbox.chans[idx], msg);
		mb->chans[idx].msg = NULL;

		dev_dbg(mb->mbox.dev, "Chan[%d]: B2A message, cmd 0x%08x\n",
			idx, msg->cmd);

		break;
	}

	return IRQ_HANDLED;
}

static const struct rockchip_mbox_data rk3368_drv_data = {
	.num_chans = 4,
};

static const struct of_device_id rockchip_mbox_of_match[] = {
	{ .compatible = "rockchip,rk3368-mailbox", .data = &rk3368_drv_data},
	{ },
};
MODULE_DEVICE_TABLE(of, rockchp_mbox_of_match);

static int rockchip_mbox_probe(struct platform_device *pdev)
{
	struct rockchip_mbox *mb;
	const struct rockchip_mbox_data *drv_data;
	struct resource *res;
	int ret, irq, i;

	if (!pdev->dev.of_node)
		return -ENODEV;

	drv_data = (const struct rockchip_mbox_data *) device_get_match_data(&pdev->dev);

	mb = devm_kzalloc(&pdev->dev, sizeof(*mb), GFP_KERNEL);
	if (!mb)
		return -ENOMEM;

	mb->chans = devm_kcalloc(&pdev->dev, drv_data->num_chans,
				 sizeof(*mb->chans), GFP_KERNEL);
	if (!mb->chans)
		return -ENOMEM;

	mb->mbox.chans = devm_kcalloc(&pdev->dev, drv_data->num_chans,
				      sizeof(*mb->mbox.chans), GFP_KERNEL);
	if (!mb->mbox.chans)
		return -ENOMEM;

	platform_set_drvdata(pdev, mb);

	mb->mbox.dev = &pdev->dev;
	mb->mbox.num_chans = drv_data->num_chans;
	mb->mbox.ops = &rockchip_mbox_chan_ops;
	mb->mbox.txdone_irq = true;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	mb->mbox_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mb->mbox_base))
		return PTR_ERR(mb->mbox_base);

	/* Each channel has two buffers for A2B and B2A */
	mb->buf_size = (size_t)resource_size(res) / (drv_data->num_chans * 2);

	mb->pclk = devm_clk_get(&pdev->dev, "pclk_mailbox");
	if (IS_ERR(mb->pclk)) {
		ret = PTR_ERR(mb->pclk);
		dev_err(&pdev->dev, "failed to get pclk_mailbox clock: %d\n",
			ret);
		return ret;
	}

	ret = clk_prepare_enable(mb->pclk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable pclk: %d\n", ret);
		return ret;
	}

	for (i = 0; i < mb->mbox.num_chans; i++) {
		irq = platform_get_irq(pdev, i);
		if (irq < 0)
			return irq;

		ret = devm_request_threaded_irq(&pdev->dev, irq,
						rockchip_mbox_irq,
						rockchip_mbox_isr, IRQF_ONESHOT,
						dev_name(&pdev->dev), mb);
		if (ret < 0)
			return ret;

		mb->chans[i].idx = i;
		mb->chans[i].irq = irq;
		mb->chans[i].mb = mb;
		mb->chans[i].msg = NULL;
	}

	ret = devm_mbox_controller_register(&pdev->dev, &mb->mbox);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to register mailbox: %d\n", ret);

	return ret;
}

static struct platform_driver rockchip_mbox_driver = {
	.probe	= rockchip_mbox_probe,
	.driver = {
		.name = "rockchip-mailbox",
		.of_match_table = of_match_ptr(rockchip_mbox_of_match),
	},
};

module_platform_driver(rockchip_mbox_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Rockchip mailbox: communicate between CPU cores and MCU");
MODULE_AUTHOR("Addy Ke <addy.ke@rock-chips.com>");
MODULE_AUTHOR("Caesar Wang <wxt@rock-chips.com>");
