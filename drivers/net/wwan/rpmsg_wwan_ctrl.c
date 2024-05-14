// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021, Stephan Gerhold <stephan@gerhold.net> */
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rpmsg.h>
#include <linux/wwan.h>

struct rpmsg_wwan_dev {
	/* Lower level is a rpmsg dev, upper level is a wwan port */
	struct rpmsg_device *rpdev;
	struct wwan_port *wwan_port;
	struct rpmsg_endpoint *ept;
};

static int rpmsg_wwan_ctrl_callback(struct rpmsg_device *rpdev,
				    void *buf, int len, void *priv, u32 src)
{
	struct rpmsg_wwan_dev *rpwwan = priv;
	struct sk_buff *skb;

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, buf, len);
	wwan_port_rx(rpwwan->wwan_port, skb);
	return 0;
}

static int rpmsg_wwan_ctrl_start(struct wwan_port *port)
{
	struct rpmsg_wwan_dev *rpwwan = wwan_port_get_drvdata(port);
	struct rpmsg_channel_info chinfo = {
		.src = rpwwan->rpdev->src,
		.dst = RPMSG_ADDR_ANY,
	};

	strncpy(chinfo.name, rpwwan->rpdev->id.name, RPMSG_NAME_SIZE);
	rpwwan->ept = rpmsg_create_ept(rpwwan->rpdev, rpmsg_wwan_ctrl_callback,
				       rpwwan, chinfo);
	if (!rpwwan->ept)
		return -EREMOTEIO;

	return 0;
}

static void rpmsg_wwan_ctrl_stop(struct wwan_port *port)
{
	struct rpmsg_wwan_dev *rpwwan = wwan_port_get_drvdata(port);

	rpmsg_destroy_ept(rpwwan->ept);
	rpwwan->ept = NULL;
}

static int rpmsg_wwan_ctrl_tx(struct wwan_port *port, struct sk_buff *skb)
{
	struct rpmsg_wwan_dev *rpwwan = wwan_port_get_drvdata(port);
	int ret;

	ret = rpmsg_trysend(rpwwan->ept, skb->data, skb->len);
	if (ret)
		return ret;

	consume_skb(skb);
	return 0;
}

static int rpmsg_wwan_ctrl_tx_blocking(struct wwan_port *port, struct sk_buff *skb)
{
	struct rpmsg_wwan_dev *rpwwan = wwan_port_get_drvdata(port);
	int ret;

	ret = rpmsg_send(rpwwan->ept, skb->data, skb->len);
	if (ret)
		return ret;

	consume_skb(skb);
	return 0;
}

static __poll_t rpmsg_wwan_ctrl_tx_poll(struct wwan_port *port,
					struct file *filp, poll_table *wait)
{
	struct rpmsg_wwan_dev *rpwwan = wwan_port_get_drvdata(port);

	return rpmsg_poll(rpwwan->ept, filp, wait);
}

static const struct wwan_port_ops rpmsg_wwan_pops = {
	.start = rpmsg_wwan_ctrl_start,
	.stop = rpmsg_wwan_ctrl_stop,
	.tx = rpmsg_wwan_ctrl_tx,
	.tx_blocking = rpmsg_wwan_ctrl_tx_blocking,
	.tx_poll = rpmsg_wwan_ctrl_tx_poll,
};

static struct device *rpmsg_wwan_find_parent(struct device *dev)
{
	/* Select first platform device as parent for the WWAN ports.
	 * On Qualcomm platforms this is usually the platform device that
	 * represents the modem remote processor. This might need to be
	 * adjusted when adding device IDs for other platforms.
	 */
	for (dev = dev->parent; dev; dev = dev->parent) {
		if (dev_is_platform(dev))
			return dev;
	}
	return NULL;
}

static int rpmsg_wwan_ctrl_probe(struct rpmsg_device *rpdev)
{
	struct rpmsg_wwan_dev *rpwwan;
	struct wwan_port *port;
	struct device *parent;

	parent = rpmsg_wwan_find_parent(&rpdev->dev);
	if (!parent)
		return -ENODEV;

	rpwwan = devm_kzalloc(&rpdev->dev, sizeof(*rpwwan), GFP_KERNEL);
	if (!rpwwan)
		return -ENOMEM;

	rpwwan->rpdev = rpdev;
	dev_set_drvdata(&rpdev->dev, rpwwan);

	/* Register as a wwan port, id.driver_data contains wwan port type */
	port = wwan_create_port(parent, rpdev->id.driver_data,
				&rpmsg_wwan_pops, rpwwan);
	if (IS_ERR(port))
		return PTR_ERR(port);

	rpwwan->wwan_port = port;

	return 0;
};

static void rpmsg_wwan_ctrl_remove(struct rpmsg_device *rpdev)
{
	struct rpmsg_wwan_dev *rpwwan = dev_get_drvdata(&rpdev->dev);

	wwan_remove_port(rpwwan->wwan_port);
}

static const struct rpmsg_device_id rpmsg_wwan_ctrl_id_table[] = {
	/* RPMSG channels for Qualcomm SoCs with integrated modem */
	{ .name = "DATA5_CNTL", .driver_data = WWAN_PORT_QMI },
	{ .name = "DATA4", .driver_data = WWAN_PORT_AT },
	{},
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_wwan_ctrl_id_table);

static struct rpmsg_driver rpmsg_wwan_ctrl_driver = {
	.drv.name = "rpmsg_wwan_ctrl",
	.id_table = rpmsg_wwan_ctrl_id_table,
	.probe = rpmsg_wwan_ctrl_probe,
	.remove = rpmsg_wwan_ctrl_remove,
};
module_rpmsg_driver(rpmsg_wwan_ctrl_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("RPMSG WWAN CTRL Driver");
MODULE_AUTHOR("Stephan Gerhold <stephan@gerhold.net>");
