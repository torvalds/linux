/*
 * Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/clk.h>

#include <soc/rockchip/rk3368-mailbox.h>
#include <soc/rockchip/scpi.h>

#define MAILBOX_VERSION			"V1.00"

#define MAILBOX_A2B_INTEN		0x00
#define MAILBOX_A2B_STATUS		0x04
#define MAILBOX_A2B_CMD(x)		(0x08 + (x) * 8)
#define MAILBOX_A2B_DAT(x)		(0x0c + (x) * 8)

#define MAILBOX_B2A_INTEN		0x28
#define MAILBOX_B2A_STATUS		0x2C
#define MAILBOX_B2A_CMD(x)		(0x30 + (x) * 8)
#define MAILBOX_B2A_DAT(x)		(0x34 + (x) * 8)

#define MAILBOX_ATOMIC_LOCK(x)		(0x100 + (x) * 8)

/* A2B: 0 - 2k */
#define A2B_BUF(size, idx)		((idx) * (size))
/* B2A: 2k - 4k */
#define B2A_BUF(size, idx)		(((idx) + 4) * (size))

struct rk3368_mbox_drv_data {
	int num_chans;
};

struct rk3368_mbox_chan {
	int idx;
	struct rk3368_mbox_msg *msg;
	struct rk3368_mbox *mb;
};

struct rk3368_mbox {
	struct mbox_controller mbox;
	struct clk *pclk;
	void __iomem *mbox_base;
	/* The base address of share memory to transfer data */
	void __iomem *buf_base;
	/* The maximum size of buf for each channel */
	u32 buf_size;
	struct rk3368_mbox_chan *chans;
};

#define MBOX_CHAN_NUMS	4
int idx_map_irq[MBOX_CHAN_NUMS] = {0, 0, 0, 0};

static inline int chan_to_idx(struct rk3368_mbox *mb,
			      struct mbox_chan *chan)
{
	return (chan - mb->mbox.chans);
}

static int rk3368_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct rk3368_mbox *mb = dev_get_drvdata(chan->mbox->dev);
	struct rk3368_mbox_msg *msg = data;
	int idx = chan_to_idx(mb, chan);

	if (!msg)
		return -EINVAL;

	if ((msg->tx_size > mb->buf_size) ||
	    (msg->rx_size > mb->buf_size)) {
		dev_err(mb->mbox.dev, "Transmit size over buf size(%d)\n",
			mb->buf_size);
		return -EINVAL;
	}

	dev_dbg(mb->mbox.dev, "Chan[%d]: A2B message, cmd 0x%08x\n",
		idx, msg->cmd);

	mb->chans[idx].msg = msg;

	if (msg->tx_buf)
		memcpy(mb->buf_base + A2B_BUF(mb->buf_size, idx),
		       msg->tx_buf, msg->tx_size);

	writel_relaxed(msg->cmd, mb->mbox_base + MAILBOX_A2B_CMD(idx));
	writel_relaxed(msg->rx_size, mb->mbox_base + MAILBOX_A2B_DAT(idx));

	return 0;
}

static int rk3368_mbox_startup(struct mbox_chan *chan)
{
	return 0;
}

static void rk3368_mbox_shutdown(struct mbox_chan *chan)
{
	struct rk3368_mbox *mb = dev_get_drvdata(chan->mbox->dev);
	int idx = chan_to_idx(mb, chan);

	mb->chans[idx].msg = NULL;
}

static struct mbox_chan_ops rk3368_mbox_chan_ops = {
	.send_data	= rk3368_mbox_send_data,
	.startup	= rk3368_mbox_startup,
	.shutdown	= rk3368_mbox_shutdown,
};

static irqreturn_t rk3368_mbox_irq(int irq, void *dev_id)
{
	int idx;
	struct rk3368_mbox *mb = (struct rk3368_mbox *)dev_id;
	u32 status = readl_relaxed(mb->mbox_base + MAILBOX_B2A_STATUS);

	for (idx = 0; idx < mb->mbox.num_chans; idx++) {
		if ((status & (1 << idx)) && (irq == idx_map_irq[idx])) {
			/* Clear mbox interrupt */
			writel_relaxed(1 << idx,
				       mb->mbox_base + MAILBOX_B2A_STATUS);
			return IRQ_WAKE_THREAD;
		}
	}

	return IRQ_NONE;
}

static irqreturn_t rk3368_mbox_isr(int irq, void *dev_id)
{
	int idx;
	struct rk3368_mbox_msg *msg = NULL;
	struct rk3368_mbox *mb = (struct rk3368_mbox *)dev_id;

	for (idx = 0; idx < mb->mbox.num_chans; idx++) {
		if (irq != idx_map_irq[idx])
			continue;

		msg = mb->chans[idx].msg;
		if (!msg) {
			dev_err(mb->mbox.dev,
				"Chan[%d]: B2A message is NULL\n", idx);
			break; /* spurious */
		}

		if (msg->rx_buf)
			memcpy(msg->rx_buf,
			       mb->buf_base + B2A_BUF(mb->buf_size, idx),
			       msg->rx_size);

		mbox_chan_received_data(&mb->mbox.chans[idx], msg);

		dev_dbg(mb->mbox.dev, "Chan[%d]: B2A message, cmd 0x%08x\n",
			idx, msg->cmd);

		break;
	}

	return IRQ_HANDLED;
}

static const struct rk3368_mbox_drv_data rk3368_drv_data = {
	.num_chans = 4,
};

static const struct of_device_id rk3368_mbox_of_match[] = {
	{
		.compatible = "rockchip,rk3368-mbox-legacy",
		.data = &rk3368_drv_data,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, rockchp_mbox_of_match);

#ifdef CONFIG_PM
static int rk3368_mbox_suspend(struct platform_device *pdev,
			       pm_message_t state)
{
	struct rk3368_mbox *mb = platform_get_drvdata(pdev);

	if (scpi_sys_set_mcu_state_suspend())
		dev_err(mb->mbox.dev, "scpi_sys_set_mcu_state_suspend timeout.\n");
	return 0;
}

static int rk3368_mbox_resume(struct platform_device *pdev)
{
	struct rk3368_mbox *mb = platform_get_drvdata(pdev);

	writel_relaxed((1 << mb->mbox.num_chans) - 1,
		       mb->mbox_base + MAILBOX_B2A_INTEN);

	if (scpi_sys_set_mcu_state_resume())
		dev_err(mb->mbox.dev, "scpi_sys_set_mcu_state_resume timeout.\n");
	return 0;
}
#endif /* CONFIG_PM */

static int rk3368_mbox_probe(struct platform_device *pdev)
{
	struct rk3368_mbox *mb;
	const struct of_device_id *match;
	const struct rk3368_mbox_drv_data *drv_data;
	struct resource *res;
	int ret, irq, i;

	dev_info(&pdev->dev, "rk3368-mailbox initialized, version: %s\n",
		 MAILBOX_VERSION);

	if (!pdev->dev.of_node)
		return -ENODEV;

	match = of_match_node(rk3368_mbox_of_match, pdev->dev.of_node);
	drv_data = (const struct rk3368_mbox_drv_data *)match->data;

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
	mb->mbox.ops = &rk3368_mbox_chan_ops;
	mb->mbox.txdone_irq = true;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	mb->mbox_base = devm_ioremap_resource(&pdev->dev, res);
	if (!mb->mbox_base)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res)
		return -ENODEV;

	/* Each channel has two buffers for A2B and B2A */
	mb->buf_size = resource_size(res) / (drv_data->num_chans * 2);
	mb->buf_base = devm_ioremap_resource(&pdev->dev, res);
	if (!mb->buf_base)
		return -ENOMEM;

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
						rk3368_mbox_irq,
						rk3368_mbox_isr, IRQF_ONESHOT,
						dev_name(&pdev->dev), mb);
		if (ret < 0)
			return ret;

		mb->chans[i].idx = i;
		mb->chans[i].mb = mb;
		mb->chans[i].msg = NULL;
		idx_map_irq[i] = irq;
	}

	/* Enable all B2A interrupts */
	writel_relaxed((1 << mb->mbox.num_chans) - 1,
		       mb->mbox_base + MAILBOX_B2A_INTEN);

	ret = mbox_controller_register(&mb->mbox);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to register mailbox: %d\n", ret);

	return ret;
}

static int rk3368_mbox_remove(struct platform_device *pdev)
{
	struct rk3368_mbox *mb = platform_get_drvdata(pdev);

	mbox_controller_unregister(&mb->mbox);

	return 0;
}

static struct platform_driver rk3368_mbox_driver = {
	.probe	= rk3368_mbox_probe,
	.remove	= rk3368_mbox_remove,
#ifdef CONFIG_PM
	.suspend = rk3368_mbox_suspend,
	.resume	= rk3368_mbox_resume,
#endif /* CONFIG_PM */
	.driver = {
		.name = "rk3368-mailbox",
		.of_match_table = of_match_ptr(rk3368_mbox_of_match),
	},
};

static int __init rk3368_mbox_init(void)
{
	return platform_driver_register(&rk3368_mbox_driver);
}
subsys_initcall(rk3368_mbox_init);
