// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/platform_device.h>
#include <linux/mailbox_controller.h>
#include <dt-bindings/soc/qcom,ipcc.h>

/* IPCC Register offsets */
#define IPCC_REG_SEND_ID		0x0C
#define IPCC_REG_RECV_ID		0x10
#define IPCC_REG_RECV_SIGNAL_ENABLE	0x14
#define IPCC_REG_RECV_SIGNAL_DISABLE	0x18
#define IPCC_REG_RECV_SIGNAL_CLEAR	0x1c
#define IPCC_REG_CLIENT_CLEAR		0x38

#define IPCC_SIGNAL_ID_MASK		GENMASK(15, 0)
#define IPCC_CLIENT_ID_MASK		GENMASK(31, 16)
#define IPCC_CLIENT_ID_SHIFT		16

#define IPCC_NO_PENDING_IRQ		(~(u32)0)

/**
 * struct ipcc_protocol_data - Per-protocol data
 * @irq_domain:		irq_domain associated with this protocol-id
 * @mbox:		mailbox-controller interface
 * @chans:		The mailbox clients' channel array (created dynamically)
 * @base:		Base address of the IPCC frame associated to APPS
 * @dev:		Device associated with this instance
 * @irq:		Summary irq
 */
struct ipcc_protocol_data {
	struct irq_domain *irq_domain;
	struct mbox_controller mbox;
	struct mbox_chan *chans;
	void __iomem *base;
	struct device *dev;
	int num_chans;
	int irq;
};

/**
 * struct ipcc_mbox_chan - Per-mailbox-channel data. Associated to each channel
 *				requested by the clients
 * @client_id:	The client-id to which the interrupt has to be triggered
 * @signalid:	The signal-id to which the interrupt has to be triggered
 * @chan:	Points to this channel's array element for this protocol's
 *		ipcc_protocol_data->chans[]
 * @proto_data: The pointer to per-protocol data associated to this channel
 */
struct ipcc_mbox_chan {
	u16 client_id;
	u16 signal_id;
	struct mbox_chan *chan;
	struct ipcc_protocol_data *proto_data;
};

static inline u32 qcom_ipcc_get_packed_id(u16 client_id, u16 signal_id)
{
	return (client_id << IPCC_CLIENT_ID_SHIFT) | signal_id;
}

static inline u16 qcom_ipcc_get_client_id(u32 packed_id)
{
	return packed_id >> IPCC_CLIENT_ID_SHIFT;
}

static inline u16 qcom_ipcc_get_signal_id(u32 packed_id)
{
	return packed_id & IPCC_SIGNAL_ID_MASK;
}

static irqreturn_t qcom_ipcc_irq_fn(int irq, void *data)
{
	struct ipcc_protocol_data *proto_data = data;
	u32 packed_id;
	int virq;

	for (;;) {
		packed_id = readl(proto_data->base + IPCC_REG_RECV_ID);
		if (packed_id == IPCC_NO_PENDING_IRQ)
			break;

		virq = irq_find_mapping(proto_data->irq_domain, packed_id);

		dev_dbg(proto_data->dev,
			"IRQ for client_id: %u; signal_id: %u; virq: %d\n",
			qcom_ipcc_get_client_id(packed_id),
			qcom_ipcc_get_signal_id(packed_id), virq);

		writel(packed_id,
				proto_data->base + IPCC_REG_RECV_SIGNAL_CLEAR);

		generic_handle_irq(virq);
	}

	return IRQ_HANDLED;
}

static void qcom_ipcc_mask_irq(struct irq_data *irqd)
{
	struct ipcc_protocol_data *proto_data;
	irq_hw_number_t hwirq = irqd_to_hwirq(irqd);
	u16 sender_client_id = qcom_ipcc_get_client_id(hwirq);
	u16 sender_signal_id = qcom_ipcc_get_signal_id(hwirq);

	proto_data = irq_data_get_irq_chip_data(irqd);

	dev_dbg(proto_data->dev,
		"%s: Disabling interrupts for: client_id: %u; signal_id: %u\n",
		__func__, sender_client_id, sender_signal_id);

	writel(hwirq, proto_data->base + IPCC_REG_RECV_SIGNAL_DISABLE);
}

static void qcom_ipcc_unmask_irq(struct irq_data *irqd)
{
	struct ipcc_protocol_data *proto_data;
	irq_hw_number_t hwirq = irqd_to_hwirq(irqd);
	u16 sender_client_id = qcom_ipcc_get_client_id(hwirq);
	u16 sender_signal_id = qcom_ipcc_get_signal_id(hwirq);

	proto_data = irq_data_get_irq_chip_data(irqd);

	dev_dbg(proto_data->dev,
		"%s: Enabling interrupts for: client_id: %u; signal_id: %u\n",
		__func__, sender_client_id, sender_signal_id);

	writel(hwirq, proto_data->base + IPCC_REG_RECV_SIGNAL_ENABLE);
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
	struct ipcc_protocol_data *proto_data = d->host_data;

	irq_set_chip_and_handler(irq, &qcom_ipcc_irq_chip, handle_level_irq);
	irq_set_chip_data(irq, proto_data);
	irq_set_noprobe(irq);

	return 0;
}

static int
qcom_ipcc_domain_xlate(struct irq_domain *d, struct device_node *node,
			const u32 *intspec, unsigned int intsize,
			unsigned long *out_hwirq, unsigned int *out_type)
{
	struct ipcc_protocol_data *proto_data = d->host_data;
	struct device *dev = proto_data->dev;

	if (intsize != 3)
		return -EINVAL;

	*out_hwirq = qcom_ipcc_get_packed_id(intspec[0], intspec[1]);
	*out_type = intspec[2] & IRQ_TYPE_SENSE_MASK;

	dev_dbg(dev, "%s: hwirq: 0x%lx\n", __func__, *out_hwirq);

	return 0;
}

static const struct irq_domain_ops qcom_ipcc_irq_ops = {
	.map = qcom_ipcc_domain_map,
	.xlate = qcom_ipcc_domain_xlate,
};

static int qcom_ipcc_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct ipcc_mbox_chan *ipcc_mbox_chan = chan->con_priv;
	struct ipcc_protocol_data *proto_data = ipcc_mbox_chan->proto_data;
	u32 packed_id;

	dev_dbg(proto_data->dev,
		"%s: Generating IRQ for client_id: %u; signal_id: %u\n",
		__func__, ipcc_mbox_chan->client_id, ipcc_mbox_chan->signal_id);

	packed_id = qcom_ipcc_get_packed_id(ipcc_mbox_chan->client_id,
						ipcc_mbox_chan->signal_id);
	writel(packed_id, proto_data->base + IPCC_REG_SEND_ID);

	return 0;
}

static void qcom_ipcc_mbox_shutdown(struct mbox_chan *chan)
{
	chan->con_priv = NULL;
}

static struct mbox_chan *qcom_ipcc_mbox_xlate(struct mbox_controller *mbox,
					const struct of_phandle_args *ph)
{
	struct ipcc_protocol_data *proto_data;
	struct ipcc_mbox_chan *ipcc_mbox_chan;
	struct device *dev;
	int chan_id;

	proto_data = container_of(mbox, struct ipcc_protocol_data, mbox);
	if (WARN_ON(!proto_data))
		return ERR_PTR(-EINVAL);

	dev = proto_data->dev;

	if (ph->args_count != 2)
		return ERR_PTR(-EINVAL);

	for (chan_id = 0; chan_id < mbox->num_chans; chan_id++) {
		ipcc_mbox_chan = proto_data->chans[chan_id].con_priv;

		if (!ipcc_mbox_chan)
			break;
		else if (ipcc_mbox_chan->client_id == ph->args[0] &&
				ipcc_mbox_chan->signal_id == ph->args[1])
			return ERR_PTR(-EBUSY);
	}

	if (chan_id >= mbox->num_chans)
		return ERR_PTR(-EBUSY);

	ipcc_mbox_chan = devm_kzalloc(dev, sizeof(*ipcc_mbox_chan), GFP_KERNEL);
	if (!ipcc_mbox_chan)
		return ERR_PTR(-ENOMEM);

	ipcc_mbox_chan->client_id = ph->args[0];
	ipcc_mbox_chan->signal_id = ph->args[1];
	ipcc_mbox_chan->chan = &proto_data->chans[chan_id];
	ipcc_mbox_chan->proto_data = proto_data;
	ipcc_mbox_chan->chan->con_priv = ipcc_mbox_chan;

	dev_dbg(dev,
		"New mailbox channel: %u for client_id: %u; signal_id: %u\n",
		chan_id, ipcc_mbox_chan->client_id,
		ipcc_mbox_chan->signal_id);

	return ipcc_mbox_chan->chan;
}

static const struct mbox_chan_ops ipcc_mbox_chan_ops = {
	.send_data = qcom_ipcc_mbox_send_data,
	.shutdown = qcom_ipcc_mbox_shutdown
};

static int qcom_ipcc_setup_mbox(struct ipcc_protocol_data *proto_data,
				struct device_node *controller_dn)
{
	struct of_phandle_args curr_ph;
	struct device_node *client_dn;
	struct mbox_controller *mbox;
	struct device *dev = proto_data->dev;
	int i, j, ret;

	/*
	 * Find out the number of clients interested in this mailbox
	 * and create channels accordingly.
	 */
	proto_data->num_chans = 0;
	for_each_node_with_property(client_dn, "mboxes") {
		if (!of_device_is_available(client_dn))
			continue;
		i = of_count_phandle_with_args(client_dn,
						"mboxes", "#mbox-cells");
		for (j = 0; j < i; j++) {
			ret = of_parse_phandle_with_args(client_dn, "mboxes",
						"#mbox-cells", j, &curr_ph);
			of_node_put(curr_ph.np);
			if (!ret && curr_ph.np == controller_dn) {
				proto_data->num_chans++;
				break;
			}
		}
	}

	/* If no clients are found, skip registering as a mbox controller */
	if (!proto_data->num_chans)
		return 0;

	proto_data->chans = devm_kcalloc(dev, proto_data->num_chans,
					sizeof(struct mbox_chan), GFP_KERNEL);
	if (!proto_data->chans)
		return -ENOMEM;

	mbox = &proto_data->mbox;
	mbox->dev = dev;
	mbox->num_chans = proto_data->num_chans;
	mbox->chans = proto_data->chans;
	mbox->ops = &ipcc_mbox_chan_ops;
	mbox->of_xlate = qcom_ipcc_mbox_xlate;
	mbox->txdone_irq = false;
	mbox->txdone_poll = false;

	return mbox_controller_register(mbox);
}

static int qcom_ipcc_probe(struct platform_device *pdev)
{
	struct ipcc_protocol_data *proto_data;
	struct resource *res;
	static int id;
	int ret;
	char *name;

	proto_data = devm_kzalloc(&pdev->dev, sizeof(*proto_data), GFP_KERNEL);
	if (!proto_data)
		return -ENOMEM;

	proto_data->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get the device base address\n");
		return -ENODEV;
	}

	proto_data->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(proto_data->base)) {
		dev_err(&pdev->dev, "Failed to ioremap the ipcc base addr\n");
		return PTR_ERR(proto_data->base);
	}

	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "ipcc_%d", id++);
	if (!name)
		return -ENOMEM;

	proto_data->irq = platform_get_irq(pdev, 0);
	if (proto_data->irq < 0) {
		dev_err(&pdev->dev, "Failed to get the IRQ\n");
		return proto_data->irq;
	}

	proto_data->irq_domain = irq_domain_add_tree(pdev->dev.of_node,
						&qcom_ipcc_irq_ops,
						proto_data);
	if (!proto_data->irq_domain) {
		dev_err(&pdev->dev, "Failed to add irq_domain\n");
		return -ENOMEM;
	}

	ret = qcom_ipcc_setup_mbox(proto_data, pdev->dev.of_node);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create mailbox\n");
		goto err_mbox;
	}

	ret = devm_request_irq(&pdev->dev, proto_data->irq, qcom_ipcc_irq_fn,
				IRQF_TRIGGER_HIGH | IRQF_NO_SUSPEND, name, proto_data);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register the irq: %d\n", ret);
		goto err_req_irq;
	}

	platform_set_drvdata(pdev, proto_data);

	return 0;

err_req_irq:
	if (proto_data->num_chans)
		mbox_controller_unregister(&proto_data->mbox);
err_mbox:
	irq_domain_remove(proto_data->irq_domain);
	return ret;
}

static int qcom_ipcc_remove(struct platform_device *pdev)
{
	struct ipcc_protocol_data *proto_data = platform_get_drvdata(pdev);

	disable_irq_wake(proto_data->irq);
	if (proto_data->num_chans)
		mbox_controller_unregister(&proto_data->mbox);
	irq_domain_remove(proto_data->irq_domain);

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
		.name = "qcom_ipcc",
		.of_match_table = qcom_ipcc_of_match,
	},
};

static int __init qcom_ipcc_init(void)
{
	int ret;

	ret = platform_driver_register(&qcom_ipcc_driver);
	if (ret)
		pr_err("%s: qcom_ipcc register failed %d\n", __func__, ret);
	return ret;
}
arch_initcall(qcom_ipcc_init);

static __exit void qcom_ipcc_exit(void)
{
	platform_driver_unregister(&qcom_ipcc_driver);
}
module_exit(qcom_ipcc_exit);

MODULE_DESCRIPTION("Qualcomm IPCC Driver");
MODULE_LICENSE("GPL v2");
