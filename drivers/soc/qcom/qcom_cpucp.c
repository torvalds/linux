// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/platform_device.h>
#include <linux/mailbox_controller.h>

/* CPUCP Register offsets */
#define CPUCP_IPC_CHAN_SUPPORTED	2
#define CPUCP_SEND_IRQ_OFFSET		0xC
#define CPUCP_SEND_IRQ_VAL		BIT(28)
#define CPUCP_CLEAR_IRQ_OFFSET		0x308
#define CPUCP_STATUS_IRQ_OFFSET		0x30C
#define CPUCP_CLEAR_IRQ_VAL		BIT(3)
#define CPUCP_STATUS_IRQ_VAL		BIT(3)
#define CPUCP_CLOCK_DOMAIN_OFFSET	0x1000

/**
 * struct cpucp_ipc     ipc per channel
 * @mbox:		mailbox-controller interface
 * @chans:		The mailbox clients' channel array
 * @tx_irq_base:	Memory address for sending irq
 * @rx_irq_base:	Memory address for receiving irq
 * @dev:		Device associated with this instance
 * @irq:		CPUCP to HLOS irq
 * @num_chan:		Number of ipc channels supported
 */
struct qcom_cpucp_ipc {
	struct mbox_controller mbox;
	struct mbox_chan chans[CPUCP_IPC_CHAN_SUPPORTED];
	void __iomem *tx_irq_base;
	void __iomem *rx_irq_base;
	struct device *dev;
	int irq;
	int num_chan;
};

static irqreturn_t qcom_cpucp_rx_interrupt(int irq, void *p)
{
	struct qcom_cpucp_ipc *cpucp_ipc;
	u32 val;
	int i;
	unsigned long flags;

	cpucp_ipc = p;

	for (i = 0; i < cpucp_ipc->num_chan; i++) {

		val = readl(cpucp_ipc->rx_irq_base +
		CPUCP_STATUS_IRQ_OFFSET + (i * CPUCP_CLOCK_DOMAIN_OFFSET));
		if (val & CPUCP_STATUS_IRQ_VAL) {

			val = CPUCP_CLEAR_IRQ_VAL;
			writel(val, cpucp_ipc->rx_irq_base +
			CPUCP_CLEAR_IRQ_OFFSET +
				(i * CPUCP_CLOCK_DOMAIN_OFFSET));
			/* Make sure reg write is complete before proceeding */
			mb();
			spin_lock_irqsave(&cpucp_ipc->chans[i].lock, flags);
			if (cpucp_ipc->chans[i].con_priv)
				mbox_chan_received_data(&cpucp_ipc->chans[i]
							, NULL);
			spin_unlock_irqrestore(&cpucp_ipc->chans[i].lock, flags);
		}
	}

	return IRQ_HANDLED;
}

static void qcom_cpucp_mbox_shutdown(struct mbox_chan *chan)
{
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	chan->con_priv = NULL;
	spin_unlock_irqrestore(&chan->lock, flags);
}

static int qcom_cpucp_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct qcom_cpucp_ipc *cpucp_ipc = container_of(chan->mbox,
						  struct qcom_cpucp_ipc, mbox);

	writel(CPUCP_SEND_IRQ_VAL,
			cpucp_ipc->tx_irq_base + CPUCP_SEND_IRQ_OFFSET);
	return 0;
}

static struct mbox_chan *qcom_cpucp_mbox_xlate(struct mbox_controller *mbox,
			const struct of_phandle_args *sp)
{
	struct qcom_cpucp_ipc *cpucp_ipc = container_of(mbox,
						  struct qcom_cpucp_ipc, mbox);
	unsigned long ind = sp->args[0];

	if (sp->args_count != 1)
		return ERR_PTR(-EINVAL);

	if (ind >= mbox->num_chans)
		return ERR_PTR(-EINVAL);

	if (mbox->chans[ind].con_priv)
		return ERR_PTR(-EBUSY);

	mbox->chans[ind].con_priv = cpucp_ipc;
	return &mbox->chans[ind];
}

static const struct mbox_chan_ops cpucp_mbox_chan_ops = {
	.send_data = qcom_cpucp_mbox_send_data,
	.shutdown = qcom_cpucp_mbox_shutdown
};

static int qcom_cpucp_ipc_setup_mbox(struct qcom_cpucp_ipc *cpucp_ipc)
{
	struct mbox_controller *mbox;
	struct device *dev = cpucp_ipc->dev;
	unsigned long i;

	/* Initialize channel identifiers */
	for (i = 0; i < ARRAY_SIZE(cpucp_ipc->chans); i++)
		cpucp_ipc->chans[i].con_priv = NULL;

	mbox = &cpucp_ipc->mbox;
	mbox->dev = dev;
	mbox->num_chans = cpucp_ipc->num_chan;
	mbox->chans = cpucp_ipc->chans;
	mbox->ops = &cpucp_mbox_chan_ops;
	mbox->of_xlate = qcom_cpucp_mbox_xlate;
	mbox->txdone_irq = false;
	mbox->txdone_poll = false;

	return mbox_controller_register(mbox);
}

static int qcom_cpucp_probe(struct platform_device *pdev)
{
	struct qcom_cpucp_ipc *cpucp_ipc;
	struct resource *res;
	int ret;

	cpucp_ipc = devm_kzalloc(&pdev->dev, sizeof(*cpucp_ipc), GFP_KERNEL);
	if (!cpucp_ipc)
		return -ENOMEM;

	cpucp_ipc->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get the device base address\n");
		return -ENODEV;
	}

	cpucp_ipc->tx_irq_base = devm_ioremap(&pdev->dev, res->start,
			resource_size(res));
	if (!cpucp_ipc->tx_irq_base) {
		dev_err(&pdev->dev, "Failed to ioremap cpucp tx irq addr\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get the device base address\n");
		return -ENODEV;
	}

	cpucp_ipc->rx_irq_base = devm_ioremap(&pdev->dev, res->start,
			resource_size(res));
	if (!cpucp_ipc->rx_irq_base) {
		dev_err(&pdev->dev, "Failed to ioremap cpucp rx irq addr\n");
		return -ENOMEM;
	}

	cpucp_ipc->irq = platform_get_irq(pdev, 0);
	if (cpucp_ipc->irq < 0) {
		dev_err(&pdev->dev, "Failed to get the IRQ\n");
		return cpucp_ipc->irq;
	}

	cpucp_ipc->num_chan = CPUCP_IPC_CHAN_SUPPORTED;
	ret = qcom_cpucp_ipc_setup_mbox(cpucp_ipc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create mailbox\n");
		return ret;
	}

	ret = devm_request_irq(&pdev->dev, cpucp_ipc->irq,
		qcom_cpucp_rx_interrupt, IRQF_TRIGGER_HIGH | IRQF_NO_SUSPEND,
		"qcom_cpucp", cpucp_ipc);

	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register the irq: %d\n", ret);
		goto err_mbox;
	}
	platform_set_drvdata(pdev, cpucp_ipc);

	return 0;

err_mbox:
	mbox_controller_unregister(&cpucp_ipc->mbox);
	return ret;
}

static int qcom_cpucp_remove(struct platform_device *pdev)
{
	struct qcom_cpucp_ipc *cpucp_ipc = platform_get_drvdata(pdev);

	mbox_controller_unregister(&cpucp_ipc->mbox);

	return 0;
}

static const struct of_device_id qcom_cpucp_of_match[] = {
	{ .compatible = "qcom,cpucp"},
	{}
};
MODULE_DEVICE_TABLE(of, qcom_cpucp_of_match);

static struct platform_driver qcom_cpucp_driver = {
	.probe = qcom_cpucp_probe,
	.remove = qcom_cpucp_remove,
	.driver = {
		.name = "qcom_cpucp",
		.of_match_table = qcom_cpucp_of_match,
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(qcom_cpucp_driver);

MODULE_DESCRIPTION("QTI CPUCP Driver");
MODULE_LICENSE("GPL");
