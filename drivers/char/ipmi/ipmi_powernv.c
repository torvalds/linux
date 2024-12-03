// SPDX-License-Identifier: GPL-2.0+
/*
 * PowerNV OPAL IPMI driver
 *
 * Copyright 2014 IBM Corp.
 */

#define pr_fmt(fmt)        "ipmi-powernv: " fmt

#include <linux/ipmi_smi.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>

#include <asm/opal.h>


struct ipmi_smi_powernv {
	u64			interface_id;
	struct ipmi_smi		*intf;
	unsigned int		irq;

	/**
	 * We assume that there can only be one outstanding request, so
	 * keep the pending message in cur_msg. We protect this from concurrent
	 * updates through send & recv calls, (and consequently opal_msg, which
	 * is in-use when cur_msg is set) with msg_lock
	 */
	spinlock_t		msg_lock;
	struct ipmi_smi_msg	*cur_msg;
	struct opal_ipmi_msg	*opal_msg;
};

static int ipmi_powernv_start_processing(void *send_info, struct ipmi_smi *intf)
{
	struct ipmi_smi_powernv *smi = send_info;

	smi->intf = intf;
	return 0;
}

static void send_error_reply(struct ipmi_smi_powernv *smi,
		struct ipmi_smi_msg *msg, u8 completion_code)
{
	msg->rsp[0] = msg->data[0] | 0x4;
	msg->rsp[1] = msg->data[1];
	msg->rsp[2] = completion_code;
	msg->rsp_size = 3;
	ipmi_smi_msg_received(smi->intf, msg);
}

static void ipmi_powernv_send(void *send_info, struct ipmi_smi_msg *msg)
{
	struct ipmi_smi_powernv *smi = send_info;
	struct opal_ipmi_msg *opal_msg;
	unsigned long flags;
	int comp, rc;
	size_t size;

	/* ensure data_len will fit in the opal_ipmi_msg buffer... */
	if (msg->data_size > IPMI_MAX_MSG_LENGTH) {
		comp = IPMI_REQ_LEN_EXCEEDED_ERR;
		goto err;
	}

	/* ... and that we at least have netfn and cmd bytes */
	if (msg->data_size < 2) {
		comp = IPMI_REQ_LEN_INVALID_ERR;
		goto err;
	}

	spin_lock_irqsave(&smi->msg_lock, flags);

	if (smi->cur_msg) {
		comp = IPMI_NODE_BUSY_ERR;
		goto err_unlock;
	}

	/* format our data for the OPAL API */
	opal_msg = smi->opal_msg;
	opal_msg->version = OPAL_IPMI_MSG_FORMAT_VERSION_1;
	opal_msg->netfn = msg->data[0];
	opal_msg->cmd = msg->data[1];
	if (msg->data_size > 2)
		memcpy(opal_msg->data, msg->data + 2, msg->data_size - 2);

	/* data_size already includes the netfn and cmd bytes */
	size = sizeof(*opal_msg) + msg->data_size - 2;

	pr_devel("%s: opal_ipmi_send(0x%llx, %p, %ld)\n", __func__,
			smi->interface_id, opal_msg, size);
	rc = opal_ipmi_send(smi->interface_id, opal_msg, size);
	pr_devel("%s:  -> %d\n", __func__, rc);

	if (!rc) {
		smi->cur_msg = msg;
		spin_unlock_irqrestore(&smi->msg_lock, flags);
		return;
	}

	comp = IPMI_ERR_UNSPECIFIED;
err_unlock:
	spin_unlock_irqrestore(&smi->msg_lock, flags);
err:
	send_error_reply(smi, msg, comp);
}

static int ipmi_powernv_recv(struct ipmi_smi_powernv *smi)
{
	struct opal_ipmi_msg *opal_msg;
	struct ipmi_smi_msg *msg;
	unsigned long flags;
	uint64_t size;
	int rc;

	pr_devel("%s: opal_ipmi_recv(%llx, msg, sz)\n", __func__,
			smi->interface_id);

	spin_lock_irqsave(&smi->msg_lock, flags);

	if (!smi->cur_msg) {
		spin_unlock_irqrestore(&smi->msg_lock, flags);
		pr_warn("no current message?\n");
		return 0;
	}

	msg = smi->cur_msg;
	opal_msg = smi->opal_msg;

	size = cpu_to_be64(sizeof(*opal_msg) + IPMI_MAX_MSG_LENGTH);

	rc = opal_ipmi_recv(smi->interface_id,
			opal_msg,
			&size);
	size = be64_to_cpu(size);
	pr_devel("%s:   -> %d (size %lld)\n", __func__,
			rc, rc == 0 ? size : 0);
	if (rc) {
		/* If came via the poll, and response was not yet ready */
		if (rc == OPAL_EMPTY) {
			spin_unlock_irqrestore(&smi->msg_lock, flags);
			return 0;
		}

		smi->cur_msg = NULL;
		spin_unlock_irqrestore(&smi->msg_lock, flags);
		send_error_reply(smi, msg, IPMI_ERR_UNSPECIFIED);
		return 0;
	}

	if (size < sizeof(*opal_msg)) {
		spin_unlock_irqrestore(&smi->msg_lock, flags);
		pr_warn("unexpected IPMI message size %lld\n", size);
		return 0;
	}

	if (opal_msg->version != OPAL_IPMI_MSG_FORMAT_VERSION_1) {
		spin_unlock_irqrestore(&smi->msg_lock, flags);
		pr_warn("unexpected IPMI message format (version %d)\n",
				opal_msg->version);
		return 0;
	}

	msg->rsp[0] = opal_msg->netfn;
	msg->rsp[1] = opal_msg->cmd;
	if (size > sizeof(*opal_msg))
		memcpy(&msg->rsp[2], opal_msg->data, size - sizeof(*opal_msg));
	msg->rsp_size = 2 + size - sizeof(*opal_msg);

	smi->cur_msg = NULL;
	spin_unlock_irqrestore(&smi->msg_lock, flags);
	ipmi_smi_msg_received(smi->intf, msg);
	return 0;
}

static void ipmi_powernv_request_events(void *send_info)
{
}

static void ipmi_powernv_set_run_to_completion(void *send_info,
		bool run_to_completion)
{
}

static void ipmi_powernv_poll(void *send_info)
{
	struct ipmi_smi_powernv *smi = send_info;

	ipmi_powernv_recv(smi);
}

static const struct ipmi_smi_handlers ipmi_powernv_smi_handlers = {
	.owner			= THIS_MODULE,
	.start_processing	= ipmi_powernv_start_processing,
	.sender			= ipmi_powernv_send,
	.request_events		= ipmi_powernv_request_events,
	.set_run_to_completion	= ipmi_powernv_set_run_to_completion,
	.poll			= ipmi_powernv_poll,
};

static irqreturn_t ipmi_opal_event(int irq, void *data)
{
	struct ipmi_smi_powernv *smi = data;

	ipmi_powernv_recv(smi);
	return IRQ_HANDLED;
}

static int ipmi_powernv_probe(struct platform_device *pdev)
{
	struct ipmi_smi_powernv *ipmi;
	struct device *dev;
	u32 prop;
	int rc;

	if (!pdev || !pdev->dev.of_node)
		return -ENODEV;

	dev = &pdev->dev;

	ipmi = devm_kzalloc(dev, sizeof(*ipmi), GFP_KERNEL);
	if (!ipmi)
		return -ENOMEM;

	spin_lock_init(&ipmi->msg_lock);

	rc = of_property_read_u32(dev->of_node, "ibm,ipmi-interface-id",
			&prop);
	if (rc) {
		dev_warn(dev, "No interface ID property\n");
		goto err_free;
	}
	ipmi->interface_id = prop;

	rc = of_property_read_u32(dev->of_node, "interrupts", &prop);
	if (rc) {
		dev_warn(dev, "No interrupts property\n");
		goto err_free;
	}

	ipmi->irq = irq_of_parse_and_map(dev->of_node, 0);
	if (!ipmi->irq) {
		dev_info(dev, "Unable to map irq from device tree\n");
		ipmi->irq = opal_event_request(prop);
	}

	rc = request_irq(ipmi->irq, ipmi_opal_event, IRQ_TYPE_LEVEL_HIGH,
			 "opal-ipmi", ipmi);
	if (rc) {
		dev_warn(dev, "Unable to request irq\n");
		goto err_dispose;
	}

	ipmi->opal_msg = devm_kmalloc(dev,
			sizeof(*ipmi->opal_msg) + IPMI_MAX_MSG_LENGTH,
			GFP_KERNEL);
	if (!ipmi->opal_msg) {
		rc = -ENOMEM;
		goto err_unregister;
	}

	rc = ipmi_register_smi(&ipmi_powernv_smi_handlers, ipmi, dev, 0);
	if (rc) {
		dev_warn(dev, "IPMI SMI registration failed (%d)\n", rc);
		goto err_free_msg;
	}

	dev_set_drvdata(dev, ipmi);
	return 0;

err_free_msg:
	devm_kfree(dev, ipmi->opal_msg);
err_unregister:
	free_irq(ipmi->irq, ipmi);
err_dispose:
	irq_dispose_mapping(ipmi->irq);
err_free:
	devm_kfree(dev, ipmi);
	return rc;
}

static void ipmi_powernv_remove(struct platform_device *pdev)
{
	struct ipmi_smi_powernv *smi = dev_get_drvdata(&pdev->dev);

	ipmi_unregister_smi(smi->intf);
	free_irq(smi->irq, smi);
	irq_dispose_mapping(smi->irq);
}

static const struct of_device_id ipmi_powernv_match[] = {
	{ .compatible = "ibm,opal-ipmi" },
	{ },
};


static struct platform_driver powernv_ipmi_driver = {
	.driver = {
		.name		= "ipmi-powernv",
		.of_match_table	= ipmi_powernv_match,
	},
	.probe	= ipmi_powernv_probe,
	.remove = ipmi_powernv_remove,
};


module_platform_driver(powernv_ipmi_driver);

MODULE_DEVICE_TABLE(of, ipmi_powernv_match);
MODULE_DESCRIPTION("powernv IPMI driver");
MODULE_AUTHOR("Jeremy Kerr <jk@ozlabs.org>");
MODULE_LICENSE("GPL");
