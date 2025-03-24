// SPDX-License-Identifier: GPL-2.0-only
/*
 * Marvell NFC-over-UART driver
 *
 * Copyright (C) 2015, Marvell International Ltd.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/printk.h>

#include <net/nfc/nci.h>
#include <net/nfc/nci_core.h>

#include "nfcmrvl.h"

static unsigned int hci_muxed;
static unsigned int flow_control;
static unsigned int break_control;
static int reset_n_io = -EINVAL;

/*
 * NFCMRVL NCI OPS
 */

static int nfcmrvl_uart_nci_open(struct nfcmrvl_private *priv)
{
	return 0;
}

static int nfcmrvl_uart_nci_close(struct nfcmrvl_private *priv)
{
	return 0;
}

static int nfcmrvl_uart_nci_send(struct nfcmrvl_private *priv,
				 struct sk_buff *skb)
{
	struct nci_uart *nu = priv->drv_data;

	return nu->ops.send(nu, skb);
}

static void nfcmrvl_uart_nci_update_config(struct nfcmrvl_private *priv,
					   const void *param)
{
	struct nci_uart *nu = priv->drv_data;
	const struct nfcmrvl_fw_uart_config *config = param;

	nci_uart_set_config(nu, le32_to_cpu(config->baudrate),
			    config->flow_control);
}

static const struct nfcmrvl_if_ops uart_ops = {
	.nci_open = nfcmrvl_uart_nci_open,
	.nci_close = nfcmrvl_uart_nci_close,
	.nci_send = nfcmrvl_uart_nci_send,
	.nci_update_config = nfcmrvl_uart_nci_update_config
};

static int nfcmrvl_uart_parse_dt(struct device_node *node,
				 struct nfcmrvl_platform_data *pdata)
{
	struct device_node *matched_node;
	int ret;

	matched_node = of_get_compatible_child(node, "marvell,nfc-uart");
	if (!matched_node) {
		matched_node = of_get_compatible_child(node, "mrvl,nfc-uart");
		if (!matched_node)
			return -ENODEV;
	}

	ret = nfcmrvl_parse_dt(matched_node, pdata);
	if (ret < 0) {
		pr_err("Failed to get generic entries\n");
		of_node_put(matched_node);
		return ret;
	}

	pdata->flow_control = of_property_read_bool(matched_node, "flow-control");
	pdata->break_control = of_property_read_bool(matched_node, "break-control");

	of_node_put(matched_node);

	return 0;
}

/*
 * NCI UART OPS
 */

static int nfcmrvl_nci_uart_open(struct nci_uart *nu)
{
	struct nfcmrvl_private *priv;
	struct nfcmrvl_platform_data config;
	const struct nfcmrvl_platform_data *pdata = NULL;
	struct device *dev = nu->tty->dev;

	/*
	 * Platform data cannot be used here since usually it is already used
	 * by low level serial driver. We can try to retrieve serial device
	 * and check if DT entries were added.
	 */

	if (dev && dev->parent && dev->parent->of_node)
		if (nfcmrvl_uart_parse_dt(dev->parent->of_node, &config) == 0)
			pdata = &config;

	if (!pdata) {
		pr_info("No platform data / DT -> fallback to module params\n");
		config.hci_muxed = hci_muxed;
		config.reset_n_io = reset_n_io;
		config.flow_control = flow_control;
		config.break_control = break_control;
		pdata = &config;
	}

	priv = nfcmrvl_nci_register_dev(NFCMRVL_PHY_UART, nu, &uart_ops,
					dev, pdata);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	priv->support_fw_dnld = true;

	nu->drv_data = priv;
	nu->ndev = priv->ndev;

	return 0;
}

static void nfcmrvl_nci_uart_close(struct nci_uart *nu)
{
	nfcmrvl_nci_unregister_dev((struct nfcmrvl_private *)nu->drv_data);
}

static int nfcmrvl_nci_uart_recv(struct nci_uart *nu, struct sk_buff *skb)
{
	return nfcmrvl_nci_recv_frame((struct nfcmrvl_private *)nu->drv_data,
				      skb);
}

static void nfcmrvl_nci_uart_tx_start(struct nci_uart *nu)
{
	struct nfcmrvl_private *priv = (struct nfcmrvl_private *)nu->drv_data;

	if (priv->ndev->nfc_dev->fw_download_in_progress)
		return;

	/* Remove BREAK to wake up the NFCC */
	if (priv->config.break_control && nu->tty->ops->break_ctl) {
		nu->tty->ops->break_ctl(nu->tty, 0);
		usleep_range(3000, 5000);
	}
}

static void nfcmrvl_nci_uart_tx_done(struct nci_uart *nu)
{
	struct nfcmrvl_private *priv = (struct nfcmrvl_private *)nu->drv_data;

	if (priv->ndev->nfc_dev->fw_download_in_progress)
		return;

	/*
	 * To ensure that if the NFCC goes in DEEP SLEEP sate we can wake him
	 * up. we set BREAK. Once we will be ready to send again we will remove
	 * it.
	 */
	if (priv->config.break_control && nu->tty->ops->break_ctl) {
		nu->tty->ops->break_ctl(nu->tty, -1);
		usleep_range(1000, 3000);
	}
}

static struct nci_uart nfcmrvl_nci_uart = {
	.owner  = THIS_MODULE,
	.name   = "nfcmrvl_uart",
	.driver = NCI_UART_DRIVER_MARVELL,
	.ops	= {
		.open		= nfcmrvl_nci_uart_open,
		.close		= nfcmrvl_nci_uart_close,
		.recv		= nfcmrvl_nci_uart_recv,
		.tx_start	= nfcmrvl_nci_uart_tx_start,
		.tx_done	= nfcmrvl_nci_uart_tx_done,
	}
};
module_driver(nfcmrvl_nci_uart, nci_uart_register, nci_uart_unregister);

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION("Marvell NFC-over-UART");
MODULE_LICENSE("GPL v2");

module_param(flow_control, uint, 0);
MODULE_PARM_DESC(flow_control, "Tell if UART needs flow control at init.");

module_param(break_control, uint, 0);
MODULE_PARM_DESC(break_control, "Tell if UART driver must drive break signal.");

module_param(hci_muxed, uint, 0);
MODULE_PARM_DESC(hci_muxed, "Tell if transport is muxed in HCI one.");

module_param(reset_n_io, int, 0);
MODULE_PARM_DESC(reset_n_io, "GPIO that is wired to RESET_N signal.");
