// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define APSS_CPUCP_IPC_CHAN_SUPPORTED		3
#define APSS_CPUCP_MBOX_CMD_OFF			0x4

/* Tx Registers */
#define APSS_CPUCP_TX_MBOX_CMD(i)		(0x100 + ((i) * 8))

/* Rx Registers */
#define APSS_CPUCP_RX_MBOX_CMD(i)		(0x100 + ((i) * 8))
#define APSS_CPUCP_RX_MBOX_MAP			0x4000
#define APSS_CPUCP_RX_MBOX_STAT			0x4400
#define APSS_CPUCP_RX_MBOX_CLEAR		0x4800
#define APSS_CPUCP_RX_MBOX_EN			0x4c00
#define APSS_CPUCP_RX_MBOX_CMD_MASK		GENMASK_ULL(63, 0)

/**
 * struct qcom_cpucp_mbox - Holder for the mailbox driver
 * @chans:			The mailbox channel
 * @mbox:			The mailbox controller
 * @tx_base:			Base address of the CPUCP tx registers
 * @rx_base:			Base address of the CPUCP rx registers
 */
struct qcom_cpucp_mbox {
	struct mbox_chan chans[APSS_CPUCP_IPC_CHAN_SUPPORTED];
	struct mbox_controller mbox;
	void __iomem *tx_base;
	void __iomem *rx_base;
};

static inline int channel_number(struct mbox_chan *chan)
{
	return chan - chan->mbox->chans;
}

static irqreturn_t qcom_cpucp_mbox_irq_fn(int irq, void *data)
{
	struct qcom_cpucp_mbox *cpucp = data;
	u64 status;
	int i;

	status = readq(cpucp->rx_base + APSS_CPUCP_RX_MBOX_STAT);

	for_each_set_bit(i, (unsigned long *)&status, APSS_CPUCP_IPC_CHAN_SUPPORTED) {
		u32 val = readl(cpucp->rx_base + APSS_CPUCP_RX_MBOX_CMD(i) + APSS_CPUCP_MBOX_CMD_OFF);
		struct mbox_chan *chan = &cpucp->chans[i];
		unsigned long flags;

		/* Provide mutual exclusion with changes to chan->cl */
		spin_lock_irqsave(&chan->lock, flags);
		if (chan->cl)
			mbox_chan_received_data(chan, &val);
		writeq(BIT(i), cpucp->rx_base + APSS_CPUCP_RX_MBOX_CLEAR);
		spin_unlock_irqrestore(&chan->lock, flags);
	}

	return IRQ_HANDLED;
}

static int qcom_cpucp_mbox_startup(struct mbox_chan *chan)
{
	struct qcom_cpucp_mbox *cpucp = container_of(chan->mbox, struct qcom_cpucp_mbox, mbox);
	unsigned long chan_id = channel_number(chan);
	u64 val;

	val = readq(cpucp->rx_base + APSS_CPUCP_RX_MBOX_EN);
	val |= BIT(chan_id);
	writeq(val, cpucp->rx_base + APSS_CPUCP_RX_MBOX_EN);

	return 0;
}

static void qcom_cpucp_mbox_shutdown(struct mbox_chan *chan)
{
	struct qcom_cpucp_mbox *cpucp = container_of(chan->mbox, struct qcom_cpucp_mbox, mbox);
	unsigned long chan_id = channel_number(chan);
	u64 val;

	val = readq(cpucp->rx_base + APSS_CPUCP_RX_MBOX_EN);
	val &= ~BIT(chan_id);
	writeq(val, cpucp->rx_base + APSS_CPUCP_RX_MBOX_EN);
}

static int qcom_cpucp_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct qcom_cpucp_mbox *cpucp = container_of(chan->mbox, struct qcom_cpucp_mbox, mbox);
	unsigned long chan_id = channel_number(chan);
	u32 *val = data;

	writel(*val, cpucp->tx_base + APSS_CPUCP_TX_MBOX_CMD(chan_id) + APSS_CPUCP_MBOX_CMD_OFF);

	return 0;
}

static const struct mbox_chan_ops qcom_cpucp_mbox_chan_ops = {
	.startup = qcom_cpucp_mbox_startup,
	.send_data = qcom_cpucp_mbox_send_data,
	.shutdown = qcom_cpucp_mbox_shutdown
};

static int qcom_cpucp_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qcom_cpucp_mbox *cpucp;
	struct mbox_controller *mbox;
	int irq, ret;

	cpucp = devm_kzalloc(dev, sizeof(*cpucp), GFP_KERNEL);
	if (!cpucp)
		return -ENOMEM;

	cpucp->rx_base = devm_of_iomap(dev, dev->of_node, 0, NULL);
	if (IS_ERR(cpucp->rx_base))
		return PTR_ERR(cpucp->rx_base);

	cpucp->tx_base = devm_of_iomap(dev, dev->of_node, 1, NULL);
	if (IS_ERR(cpucp->tx_base))
		return PTR_ERR(cpucp->tx_base);

	writeq(0, cpucp->rx_base + APSS_CPUCP_RX_MBOX_EN);
	writeq(0, cpucp->rx_base + APSS_CPUCP_RX_MBOX_CLEAR);
	writeq(0, cpucp->rx_base + APSS_CPUCP_RX_MBOX_MAP);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, qcom_cpucp_mbox_irq_fn,
			       IRQF_TRIGGER_HIGH, "apss_cpucp_mbox", cpucp);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to register irq: %d\n", irq);

	writeq(APSS_CPUCP_RX_MBOX_CMD_MASK, cpucp->rx_base + APSS_CPUCP_RX_MBOX_MAP);

	mbox = &cpucp->mbox;
	mbox->dev = dev;
	mbox->num_chans = APSS_CPUCP_IPC_CHAN_SUPPORTED;
	mbox->chans = cpucp->chans;
	mbox->ops = &qcom_cpucp_mbox_chan_ops;

	ret = devm_mbox_controller_register(dev, mbox);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to create mailbox\n");

	return 0;
}

static const struct of_device_id qcom_cpucp_mbox_of_match[] = {
	{ .compatible = "qcom,x1e80100-cpucp-mbox" },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_cpucp_mbox_of_match);

static struct platform_driver qcom_cpucp_mbox_driver = {
	.probe = qcom_cpucp_mbox_probe,
	.driver = {
		.name = "qcom_cpucp_mbox",
		.of_match_table = qcom_cpucp_mbox_of_match,
	},
};

static int __init qcom_cpucp_mbox_init(void)
{
	return platform_driver_register(&qcom_cpucp_mbox_driver);
}
core_initcall(qcom_cpucp_mbox_init);

static void __exit qcom_cpucp_mbox_exit(void)
{
	platform_driver_unregister(&qcom_cpucp_mbox_driver);
}
module_exit(qcom_cpucp_mbox_exit);

MODULE_DESCRIPTION("QTI CPUCP MBOX Driver");
MODULE_LICENSE("GPL");
