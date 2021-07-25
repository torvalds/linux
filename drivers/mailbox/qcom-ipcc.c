// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/bitfield.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <dt-bindings/mailbox/qcom-ipcc.h>

#define IPCC_MBOX_MAX_CHAN		48

/* IPCC Register offsets */
#define IPCC_REG_SEND_ID		0x0c
#define IPCC_REG_RECV_ID		0x10
#define IPCC_REG_RECV_SIGNAL_ENABLE	0x14
#define IPCC_REG_RECV_SIGNAL_DISABLE	0x18
#define IPCC_REG_RECV_SIGNAL_CLEAR	0x1c
#define IPCC_REG_CLIENT_CLEAR		0x38

#define IPCC_SIGNAL_ID_MASK		GENMASK(15, 0)
#define IPCC_CLIENT_ID_MASK		GENMASK(31, 16)

#define IPCC_NO_PENDING_IRQ		GENMASK(31, 0)

/**
 * struct qcom_ipcc_chan_info - Per-mailbox-channel info
 * @client_id:	The client-id to which the interrupt has to be triggered
 * @signal_id:	The signal-id to which the interrupt has to be triggered
 */
struct qcom_ipcc_chan_info {
	u16 client_id;
	u16 signal_id;
};

/**
 * struct qcom_ipcc - Holder for the mailbox driver
 * @dev:		Device associated with this instance
 * @base:		Base address of the IPCC frame associated to APSS
 * @irq_domain:		The irq_domain associated with this instance
 * @chan:		The mailbox channels array
 * @mchan:		The per-mailbox channel info array
 * @mbox:		The mailbox controller
 * @irq:		Summary irq
 */
struct qcom_ipcc {
	struct device *dev;
	void __iomem *base;
	struct irq_domain *irq_domain;
	struct mbox_chan chan[IPCC_MBOX_MAX_CHAN];
	struct qcom_ipcc_chan_info mchan[IPCC_MBOX_MAX_CHAN];
	struct mbox_controller mbox;
	int irq;
};

static inline struct qcom_ipcc *to_qcom_ipcc(struct mbox_controller *mbox)
{
	return container_of(mbox, struct qcom_ipcc, mbox);
}

static inline u32 qcom_ipcc_get_hwirq(u16 client_id, u16 signal_id)
{
	return FIELD_PREP(IPCC_CLIENT_ID_MASK, client_id) |
	       FIELD_PREP(IPCC_SIGNAL_ID_MASK, signal_id);
}

static irqreturn_t qcom_ipcc_irq_fn(int irq, void *data)
{
	struct qcom_ipcc *ipcc = data;
	u32 hwirq;
	int virq;

	for (;;) {
		hwirq = readl(ipcc->base + IPCC_REG_RECV_ID);
		if (hwirq == IPCC_NO_PENDING_IRQ)
			break;

		virq = irq_find_mapping(ipcc->irq_domain, hwirq);
		writel(hwirq, ipcc->base + IPCC_REG_RECV_SIGNAL_CLEAR);
		generic_handle_irq(virq);
	}

	return IRQ_HANDLED;
}

static void qcom_ipcc_mask_irq(struct irq_data *irqd)
{
	struct qcom_ipcc *ipcc = irq_data_get_irq_chip_data(irqd);
	irq_hw_number_t hwirq = irqd_to_hwirq(irqd);

	writel(hwirq, ipcc->base + IPCC_REG_RECV_SIGNAL_DISABLE);
}

static void qcom_ipcc_unmask_irq(struct irq_data *irqd)
{
	struct qcom_ipcc *ipcc = irq_data_get_irq_chip_data(irqd);
	irq_hw_number_t hwirq = irqd_to_hwirq(irqd);

	writel(hwirq, ipcc->base + IPCC_REG_RECV_SIGNAL_ENABLE);
}

static struct irq_chip qcom_ipcc_irq_chip = {
	.name = "ipcc",
	.irq_mask = qcom_ipcc_mask_irq,
	.irq_unmask = qcom_ipcc_unmask_irq,
	.flags = IRQCHIP_SKIP_SET_WAKE,
};

static int qcom_ipcc_domain_map(struct irq_domain *d, unsigned int irq,
				irq_hw_number_t hw)
{
	struct qcom_ipcc *ipcc = d->host_data;

	irq_set_chip_and_handler(irq, &qcom_ipcc_irq_chip, handle_level_irq);
	irq_set_chip_data(irq, ipcc);
	irq_set_noprobe(irq);

	return 0;
}

static int qcom_ipcc_domain_xlate(struct irq_domain *d,
				  struct device_node *node, const u32 *intspec,
				  unsigned int intsize,
				  unsigned long *out_hwirq,
				  unsigned int *out_type)
{
	if (intsize != 3)
		return -EINVAL;

	*out_hwirq = qcom_ipcc_get_hwirq(intspec[0], intspec[1]);
	*out_type = intspec[2] & IRQ_TYPE_SENSE_MASK;

	return 0;
}

static const struct irq_domain_ops qcom_ipcc_irq_ops = {
	.map = qcom_ipcc_domain_map,
	.xlate = qcom_ipcc_domain_xlate,
};

static int qcom_ipcc_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct qcom_ipcc *ipcc = to_qcom_ipcc(chan->mbox);
	struct qcom_ipcc_chan_info *mchan = chan->con_priv;
	u32 hwirq;

	hwirq = qcom_ipcc_get_hwirq(mchan->client_id, mchan->signal_id);
	writel(hwirq, ipcc->base + IPCC_REG_SEND_ID);

	return 0;
}

static void qcom_ipcc_mbox_shutdown(struct mbox_chan *chan)
{
	chan->con_priv = NULL;
}

static struct mbox_chan *qcom_ipcc_mbox_xlate(struct mbox_controller *mbox,
					const struct of_phandle_args *ph)
{
	struct qcom_ipcc *ipcc = to_qcom_ipcc(mbox);
	struct qcom_ipcc_chan_info *mchan;
	struct mbox_chan *chan;
	unsigned int i;

	if (ph->args_count != 2)
		return ERR_PTR(-EINVAL);

	for (i = 0; i < IPCC_MBOX_MAX_CHAN; i++) {
		chan = &ipcc->chan[i];
		if (!chan->con_priv) {
			mchan = &ipcc->mchan[i];
			mchan->client_id = ph->args[0];
			mchan->signal_id = ph->args[1];
			chan->con_priv = mchan;
			break;
		}

		chan = NULL;
	}

	return chan ?: ERR_PTR(-EBUSY);
}

static const struct mbox_chan_ops ipcc_mbox_chan_ops = {
	.send_data = qcom_ipcc_mbox_send_data,
	.shutdown = qcom_ipcc_mbox_shutdown,
};

static int qcom_ipcc_setup_mbox(struct qcom_ipcc *ipcc)
{
	struct mbox_controller *mbox;
	struct device *dev = ipcc->dev;

	mbox = &ipcc->mbox;
	mbox->dev = dev;
	mbox->num_chans = IPCC_MBOX_MAX_CHAN;
	mbox->chans = ipcc->chan;
	mbox->ops = &ipcc_mbox_chan_ops;
	mbox->of_xlate = qcom_ipcc_mbox_xlate;
	mbox->txdone_irq = false;
	mbox->txdone_poll = false;

	return devm_mbox_controller_register(dev, mbox);
}

static int qcom_ipcc_probe(struct platform_device *pdev)
{
	struct qcom_ipcc *ipcc;
	int ret;

	ipcc = devm_kzalloc(&pdev->dev, sizeof(*ipcc), GFP_KERNEL);
	if (!ipcc)
		return -ENOMEM;

	ipcc->dev = &pdev->dev;

	ipcc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ipcc->base))
		return PTR_ERR(ipcc->base);

	ipcc->irq = platform_get_irq(pdev, 0);
	if (ipcc->irq < 0)
		return ipcc->irq;

	ipcc->irq_domain = irq_domain_add_tree(pdev->dev.of_node,
					       &qcom_ipcc_irq_ops, ipcc);
	if (!ipcc->irq_domain)
		return -ENOMEM;

	ret = qcom_ipcc_setup_mbox(ipcc);
	if (ret)
		goto err_mbox;

	ret = devm_request_irq(&pdev->dev, ipcc->irq, qcom_ipcc_irq_fn,
			       IRQF_TRIGGER_HIGH, "ipcc", ipcc);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register the irq: %d\n", ret);
		goto err_mbox;
	}

	enable_irq_wake(ipcc->irq);
	platform_set_drvdata(pdev, ipcc);

	return 0;

err_mbox:
	irq_domain_remove(ipcc->irq_domain);

	return ret;
}

static int qcom_ipcc_remove(struct platform_device *pdev)
{
	struct qcom_ipcc *ipcc = platform_get_drvdata(pdev);

	disable_irq_wake(ipcc->irq);
	irq_domain_remove(ipcc->irq_domain);

	return 0;
}

static const struct of_device_id qcom_ipcc_of_match[] = {
	{ .compatible = "qcom,ipcc"},
	{}
};
MODULE_DEVICE_TABLE(of, qcom_ipcc_of_match);

static struct platform_driver qcom_ipcc_driver = {
	.probe = qcom_ipcc_probe,
	.remove = qcom_ipcc_remove,
	.driver = {
		.name = "qcom-ipcc",
		.of_match_table = qcom_ipcc_of_match,
	},
};

static int __init qcom_ipcc_init(void)
{
	return platform_driver_register(&qcom_ipcc_driver);
}
arch_initcall(qcom_ipcc_init);

MODULE_AUTHOR("Venkata Narendra Kumar Gutta <vnkgutta@codeaurora.org>");
MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. IPCC driver");
MODULE_LICENSE("GPL v2");
