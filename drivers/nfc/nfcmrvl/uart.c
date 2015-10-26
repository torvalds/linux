/**
 * Marvell NFC-over-UART driver
 *
 * Copyright (C) 2015, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available on the worldwide web at
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <net/nfc/nci.h>
#include <net/nfc/nci_core.h>
#include "nfcmrvl.h"

static unsigned int hci_muxed;
static unsigned int flow_control;
static unsigned int break_control;
static unsigned int reset_n_io;

/*
** NFCMRVL NCI OPS
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

static struct nfcmrvl_if_ops uart_ops = {
	.nci_open = nfcmrvl_uart_nci_open,
	.nci_close = nfcmrvl_uart_nci_close,
	.nci_send = nfcmrvl_uart_nci_send,
	.nci_update_config = nfcmrvl_uart_nci_update_config
};

#ifdef CONFIG_OF

static int nfcmrvl_uart_parse_dt(struct device_node *node,
				 struct nfcmrvl_platform_data *pdata)
{
	struct device_node *matched_node;
	int ret;

	matched_node = of_find_compatible_node(node, NULL, "marvell,nfc-uart");
	if (!matched_node) {
		matched_node = of_find_compatible_node(node, NULL,
						       "mrvl,nfc-uart");
		if (!matched_node)
			return -ENODEV;
	}

	ret = nfcmrvl_parse_dt(matched_node, pdata);
	if (ret < 0) {
		pr_err("Failed to get generic entries\n");
		return ret;
	}

	if (of_find_property(matched_node, "flow-control", NULL))
		pdata->flow_control = 1;
	else
		pdata->flow_control = 0;

	if (of_find_property(matched_node, "break-control", NULL))
		pdata->break_control = 1;
	else
		pdata->break_control = 0;

	return 0;
}

#else

static int nfcmrvl_uart_parse_dt(struct device_node *node,
				 struct nfcmrvl_platform_data *pdata)
{
	return -ENODEV;
}

#endif

/*
** NCI UART OPS
*/

static int nfcmrvl_nci_uart_open(struct nci_uart *nu)
{
	struct nfcmrvl_private *priv;
	struct nfcmrvl_platform_data *pdata = NULL;
	struct nfcmrvl_platform_data config;

	/*
	 * Platform data cannot be used here since usually it is already used
	 * by low level serial driver. We can try to retrieve serial device
	 * and check if DT entries were added.
	 */

	if (nu->tty->dev->parent && nu->tty->dev->parent->of_node)
		if (nfcmrvl_uart_parse_dt(nu->tty->dev->parent->of_node,
					  &config) == 0)
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
					nu->tty->dev, pdata);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	priv->support_fw_dnld = true;

	nu->drv_data = priv;
	nu->ndev = priv->ndev;

	/* Set BREAK */
	if (priv->config.break_control && nu->tty->ops->break_ctl)
		nu->tty->ops->break_ctl(nu->tty, -1);

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

	/* Remove BREAK to wake up the NFCC */
	if (priv->config.break_control && nu->tty->ops->break_ctl) {
		nu->tty->ops->break_ctl(nu->tty, 0);
		usleep_range(3000, 5000);
	}
}

static void nfcmrvl_nci_uart_tx_done(struct nci_uart *nu)
{
	struct nfcmrvl_private *priv = (struct nfcmrvl_private *)nu->drv_data;

	/*
	** To ensure that if the NFCC goes in DEEP SLEEP sate we can wake him
	** up. we set BREAK. Once we will be ready to send again we will remove
	** it.
	*/
	if (priv->config.break_control && nu->tty->ops->break_ctl)
		nu->tty->ops->break_ctl(nu->tty, -1);
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

/*
** Module init
*/

static int nfcmrvl_uart_init_module(void)
{
	return nci_uart_register(&nfcmrvl_nci_uart);
}

static void nfcmrvl_uart_exit_module(void)
{
	nci_uart_unregister(&nfcmrvl_nci_uart);
}

module_init(nfcmrvl_uart_init_module);
module_exit(nfcmrvl_uart_exit_module);

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION("Marvell NFC-over-UART");
MODULE_LICENSE("GPL v2");

module_param(flow_control, uint, 0);
MODULE_PARM_DESC(flow_control, "Tell if UART needs flow control at init.");

module_param(break_control, uint, 0);
MODULE_PARM_DESC(break_control, "Tell if UART driver must drive break signal.");

module_param(hci_muxed, uint, 0);
MODULE_PARM_DESC(hci_muxed, "Tell if transport is muxed in HCI one.");

module_param(reset_n_io, uint, 0);
MODULE_PARM_DESC(reset_n_io, "GPIO that is wired to RESET_N signal.");
