// SPDX-License-Identifier: GPL-2.0
/* Parts of this driver are based on the following:
 *  - Kvaser linux leaf driver (version 4.78)
 *  - CAN driver for esd CAN-USB/2
 *  - Kvaser linux usbcanII driver (version 5.3)
 *  - Kvaser linux mhydra driver (version 5.24)
 *
 * Copyright (C) 2002-2018 KVASER AB, Sweden. All rights reserved.
 * Copyright (C) 2010 Matthias Fuchs <matthias.fuchs@esd.eu>, esd gmbh
 * Copyright (C) 2012 Olivier Sobrie <olivier@sobrie.be>
 * Copyright (C) 2015 Valeo S.A.
 */

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/ethtool.h>
#include <linux/gfp.h>
#include <linux/if.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/usb.h>

#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/can/netlink.h>

#include "kvaser_usb.h"

/* Kvaser USB vendor id. */
#define KVASER_VENDOR_ID			0x0bfd

/* Kvaser Leaf USB devices product ids */
#define USB_LEAF_DEVEL_PRODUCT_ID		10
#define USB_LEAF_LITE_PRODUCT_ID		11
#define USB_LEAF_PRO_PRODUCT_ID			12
#define USB_LEAF_SPRO_PRODUCT_ID		14
#define USB_LEAF_PRO_LS_PRODUCT_ID		15
#define USB_LEAF_PRO_SWC_PRODUCT_ID		16
#define USB_LEAF_PRO_LIN_PRODUCT_ID		17
#define USB_LEAF_SPRO_LS_PRODUCT_ID		18
#define USB_LEAF_SPRO_SWC_PRODUCT_ID		19
#define USB_MEMO2_DEVEL_PRODUCT_ID		22
#define USB_MEMO2_HSHS_PRODUCT_ID		23
#define USB_UPRO_HSHS_PRODUCT_ID		24
#define USB_LEAF_LITE_GI_PRODUCT_ID		25
#define USB_LEAF_PRO_OBDII_PRODUCT_ID		26
#define USB_MEMO2_HSLS_PRODUCT_ID		27
#define USB_LEAF_LITE_CH_PRODUCT_ID		28
#define USB_BLACKBIRD_SPRO_PRODUCT_ID		29
#define USB_OEM_MERCURY_PRODUCT_ID		34
#define USB_OEM_LEAF_PRODUCT_ID			35
#define USB_CAN_R_PRODUCT_ID			39
#define USB_LEAF_LITE_V2_PRODUCT_ID		288
#define USB_MINI_PCIE_HS_PRODUCT_ID		289
#define USB_LEAF_LIGHT_HS_V2_OEM_PRODUCT_ID	290
#define USB_USBCAN_LIGHT_2HS_PRODUCT_ID		291
#define USB_MINI_PCIE_2HS_PRODUCT_ID		292
#define USB_USBCAN_R_V2_PRODUCT_ID		294
#define USB_LEAF_LIGHT_R_V2_PRODUCT_ID		295
#define USB_LEAF_LIGHT_HS_V2_OEM2_PRODUCT_ID	296

/* Kvaser USBCan-II devices product ids */
#define USB_USBCAN_REVB_PRODUCT_ID		2
#define USB_VCI2_PRODUCT_ID			3
#define USB_USBCAN2_PRODUCT_ID			4
#define USB_MEMORATOR_PRODUCT_ID		5

/* Kvaser Minihydra USB devices product ids */
#define USB_BLACKBIRD_V2_PRODUCT_ID		258
#define USB_MEMO_PRO_5HS_PRODUCT_ID		260
#define USB_USBCAN_PRO_5HS_PRODUCT_ID		261
#define USB_USBCAN_LIGHT_4HS_PRODUCT_ID		262
#define USB_LEAF_PRO_HS_V2_PRODUCT_ID		263
#define USB_USBCAN_PRO_2HS_V2_PRODUCT_ID	264
#define USB_MEMO_2HS_PRODUCT_ID			265
#define USB_MEMO_PRO_2HS_V2_PRODUCT_ID		266
#define USB_HYBRID_2CANLIN_PRODUCT_ID		267
#define USB_ATI_USBCAN_PRO_2HS_V2_PRODUCT_ID	268
#define USB_ATI_MEMO_PRO_2HS_V2_PRODUCT_ID	269
#define USB_HYBRID_PRO_2CANLIN_PRODUCT_ID	270
#define USB_U100_PRODUCT_ID			273
#define USB_U100P_PRODUCT_ID			274
#define USB_U100S_PRODUCT_ID			275
#define USB_USBCAN_PRO_4HS_PRODUCT_ID		276
#define USB_HYBRID_CANLIN_PRODUCT_ID		277
#define USB_HYBRID_PRO_CANLIN_PRODUCT_ID	278

static const struct kvaser_usb_driver_info kvaser_usb_driver_info_hydra = {
	.quirks = KVASER_USB_QUIRK_HAS_HARDWARE_TIMESTAMP,
	.ops = &kvaser_usb_hydra_dev_ops,
};

static const struct kvaser_usb_driver_info kvaser_usb_driver_info_usbcan = {
	.quirks = KVASER_USB_QUIRK_HAS_TXRX_ERRORS |
		  KVASER_USB_QUIRK_HAS_SILENT_MODE,
	.family = KVASER_USBCAN,
	.ops = &kvaser_usb_leaf_dev_ops,
};

static const struct kvaser_usb_driver_info kvaser_usb_driver_info_leaf = {
	.quirks = KVASER_USB_QUIRK_IGNORE_CLK_FREQ,
	.family = KVASER_LEAF,
	.ops = &kvaser_usb_leaf_dev_ops,
};

static const struct kvaser_usb_driver_info kvaser_usb_driver_info_leaf_err = {
	.quirks = KVASER_USB_QUIRK_HAS_TXRX_ERRORS |
		  KVASER_USB_QUIRK_IGNORE_CLK_FREQ,
	.family = KVASER_LEAF,
	.ops = &kvaser_usb_leaf_dev_ops,
};

static const struct kvaser_usb_driver_info kvaser_usb_driver_info_leaf_err_listen = {
	.quirks = KVASER_USB_QUIRK_HAS_TXRX_ERRORS |
		  KVASER_USB_QUIRK_HAS_SILENT_MODE |
		  KVASER_USB_QUIRK_IGNORE_CLK_FREQ,
	.family = KVASER_LEAF,
	.ops = &kvaser_usb_leaf_dev_ops,
};

static const struct kvaser_usb_driver_info kvaser_usb_driver_info_leafimx = {
	.quirks = 0,
	.ops = &kvaser_usb_leaf_dev_ops,
};

static const struct usb_device_id kvaser_usb_table[] = {
	/* Leaf M32C USB product IDs */
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_DEVEL_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leaf },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_LITE_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leaf },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_PRO_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leaf_err_listen },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_SPRO_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leaf_err_listen },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_PRO_LS_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leaf_err_listen },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_PRO_SWC_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leaf_err_listen },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_PRO_LIN_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leaf_err_listen },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_SPRO_LS_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leaf_err_listen },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_SPRO_SWC_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leaf_err_listen },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_MEMO2_DEVEL_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leaf_err_listen },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_MEMO2_HSHS_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leaf_err_listen },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_UPRO_HSHS_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leaf_err },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_LITE_GI_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leaf },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_PRO_OBDII_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leaf_err_listen },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_MEMO2_HSLS_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leaf_err },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_LITE_CH_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leaf_err },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_BLACKBIRD_SPRO_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leaf_err },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_OEM_MERCURY_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leaf_err },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_OEM_LEAF_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leaf_err },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_CAN_R_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leaf_err },

	/* Leaf i.MX28 USB product IDs */
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_LITE_V2_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leafimx },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_MINI_PCIE_HS_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leafimx },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_LIGHT_HS_V2_OEM_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leafimx },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_USBCAN_LIGHT_2HS_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leafimx },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_MINI_PCIE_2HS_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leafimx },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_USBCAN_R_V2_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leafimx },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_LIGHT_R_V2_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leafimx },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_LIGHT_HS_V2_OEM2_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_leafimx },

	/* USBCANII USB product IDs */
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_USBCAN2_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_usbcan },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_USBCAN_REVB_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_usbcan },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_MEMORATOR_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_usbcan },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_VCI2_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_usbcan },

	/* Minihydra USB product IDs */
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_BLACKBIRD_V2_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_hydra },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_MEMO_PRO_5HS_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_hydra },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_USBCAN_PRO_5HS_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_hydra },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_USBCAN_LIGHT_4HS_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_hydra },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_PRO_HS_V2_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_hydra },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_USBCAN_PRO_2HS_V2_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_hydra },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_MEMO_2HS_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_hydra },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_MEMO_PRO_2HS_V2_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_hydra },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_HYBRID_2CANLIN_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_hydra },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_ATI_USBCAN_PRO_2HS_V2_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_hydra },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_ATI_MEMO_PRO_2HS_V2_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_hydra },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_HYBRID_PRO_2CANLIN_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_hydra },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_U100_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_hydra },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_U100P_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_hydra },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_U100S_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_hydra },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_USBCAN_PRO_4HS_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_hydra },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_HYBRID_CANLIN_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_hydra },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_HYBRID_PRO_CANLIN_PRODUCT_ID),
		.driver_info = (kernel_ulong_t)&kvaser_usb_driver_info_hydra },
	{ }
};
MODULE_DEVICE_TABLE(usb, kvaser_usb_table);

int kvaser_usb_send_cmd(const struct kvaser_usb *dev, void *cmd, int len)
{
	return usb_bulk_msg(dev->udev,
			    usb_sndbulkpipe(dev->udev,
					    dev->bulk_out->bEndpointAddress),
			    cmd, len, NULL, KVASER_USB_TIMEOUT);
}

int kvaser_usb_recv_cmd(const struct kvaser_usb *dev, void *cmd, int len,
			int *actual_len)
{
	return usb_bulk_msg(dev->udev,
			    usb_rcvbulkpipe(dev->udev,
					    dev->bulk_in->bEndpointAddress),
			    cmd, len, actual_len, KVASER_USB_TIMEOUT);
}

static void kvaser_usb_send_cmd_callback(struct urb *urb)
{
	struct net_device *netdev = urb->context;

	kfree(urb->transfer_buffer);

	if (urb->status)
		netdev_warn(netdev, "urb status received: %d\n", urb->status);
}

int kvaser_usb_send_cmd_async(struct kvaser_usb_net_priv *priv, void *cmd,
			      int len)
{
	struct kvaser_usb *dev = priv->dev;
	struct net_device *netdev = priv->netdev;
	struct urb *urb;
	int err;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb)
		return -ENOMEM;

	usb_fill_bulk_urb(urb, dev->udev,
			  usb_sndbulkpipe(dev->udev,
					  dev->bulk_out->bEndpointAddress),
			  cmd, len, kvaser_usb_send_cmd_callback, netdev);
	usb_anchor_urb(urb, &priv->tx_submitted);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		netdev_err(netdev, "Error transmitting URB\n");
		usb_unanchor_urb(urb);
	}
	usb_free_urb(urb);

	return 0;
}

int kvaser_usb_can_rx_over_error(struct net_device *netdev)
{
	struct net_device_stats *stats = &netdev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;

	stats->rx_over_errors++;
	stats->rx_errors++;

	skb = alloc_can_err_skb(netdev, &cf);
	if (!skb) {
		stats->rx_dropped++;
		netdev_warn(netdev, "No memory left for err_skb\n");
		return -ENOMEM;
	}

	cf->can_id |= CAN_ERR_CRTL;
	cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;

	netif_rx(skb);

	return 0;
}

static void kvaser_usb_read_bulk_callback(struct urb *urb)
{
	struct kvaser_usb *dev = urb->context;
	const struct kvaser_usb_dev_ops *ops = dev->driver_info->ops;
	int err;
	unsigned int i;

	switch (urb->status) {
	case 0:
		break;
	case -ENOENT:
	case -EPIPE:
	case -EPROTO:
	case -ESHUTDOWN:
		return;
	default:
		dev_info(&dev->intf->dev, "Rx URB aborted (%d)\n", urb->status);
		goto resubmit_urb;
	}

	ops->dev_read_bulk_callback(dev, urb->transfer_buffer,
				    urb->actual_length);

resubmit_urb:
	usb_fill_bulk_urb(urb, dev->udev,
			  usb_rcvbulkpipe(dev->udev,
					  dev->bulk_in->bEndpointAddress),
			  urb->transfer_buffer, KVASER_USB_RX_BUFFER_SIZE,
			  kvaser_usb_read_bulk_callback, dev);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err == -ENODEV) {
		for (i = 0; i < dev->nchannels; i++) {
			if (!dev->nets[i])
				continue;

			netif_device_detach(dev->nets[i]->netdev);
		}
	} else if (err) {
		dev_err(&dev->intf->dev,
			"Failed resubmitting read bulk urb: %d\n", err);
	}
}

static int kvaser_usb_setup_rx_urbs(struct kvaser_usb *dev)
{
	int i, err = 0;

	if (dev->rxinitdone)
		return 0;

	for (i = 0; i < KVASER_USB_MAX_RX_URBS; i++) {
		struct urb *urb = NULL;
		u8 *buf = NULL;
		dma_addr_t buf_dma;

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			err = -ENOMEM;
			break;
		}

		buf = usb_alloc_coherent(dev->udev, KVASER_USB_RX_BUFFER_SIZE,
					 GFP_KERNEL, &buf_dma);
		if (!buf) {
			dev_warn(&dev->intf->dev,
				 "No memory left for USB buffer\n");
			usb_free_urb(urb);
			err = -ENOMEM;
			break;
		}

		usb_fill_bulk_urb(urb, dev->udev,
				  usb_rcvbulkpipe
					(dev->udev,
					 dev->bulk_in->bEndpointAddress),
				  buf, KVASER_USB_RX_BUFFER_SIZE,
				  kvaser_usb_read_bulk_callback, dev);
		urb->transfer_dma = buf_dma;
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		usb_anchor_urb(urb, &dev->rx_submitted);

		err = usb_submit_urb(urb, GFP_KERNEL);
		if (err) {
			usb_unanchor_urb(urb);
			usb_free_coherent(dev->udev,
					  KVASER_USB_RX_BUFFER_SIZE, buf,
					  buf_dma);
			usb_free_urb(urb);
			break;
		}

		dev->rxbuf[i] = buf;
		dev->rxbuf_dma[i] = buf_dma;

		usb_free_urb(urb);
	}

	if (i == 0) {
		dev_warn(&dev->intf->dev, "Cannot setup read URBs, error %d\n",
			 err);
		return err;
	} else if (i < KVASER_USB_MAX_RX_URBS) {
		dev_warn(&dev->intf->dev, "RX performances may be slow\n");
	}

	dev->rxinitdone = true;

	return 0;
}

static int kvaser_usb_open(struct net_device *netdev)
{
	struct kvaser_usb_net_priv *priv = netdev_priv(netdev);
	struct kvaser_usb *dev = priv->dev;
	const struct kvaser_usb_dev_ops *ops = dev->driver_info->ops;
	int err;

	err = open_candev(netdev);
	if (err)
		return err;

	err = kvaser_usb_setup_rx_urbs(dev);
	if (err)
		goto error;

	err = ops->dev_set_opt_mode(priv);
	if (err)
		goto error;

	err = ops->dev_start_chip(priv);
	if (err) {
		netdev_warn(netdev, "Cannot start device, error %d\n", err);
		goto error;
	}

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	return 0;

error:
	close_candev(netdev);
	return err;
}

static void kvaser_usb_reset_tx_urb_contexts(struct kvaser_usb_net_priv *priv)
{
	int i, max_tx_urbs;

	max_tx_urbs = priv->dev->max_tx_urbs;

	priv->active_tx_contexts = 0;
	for (i = 0; i < max_tx_urbs; i++)
		priv->tx_contexts[i].echo_index = max_tx_urbs;
}

/* This method might sleep. Do not call it in the atomic context
 * of URB completions.
 */
void kvaser_usb_unlink_tx_urbs(struct kvaser_usb_net_priv *priv)
{
	usb_kill_anchored_urbs(&priv->tx_submitted);
	kvaser_usb_reset_tx_urb_contexts(priv);
}

static void kvaser_usb_unlink_all_urbs(struct kvaser_usb *dev)
{
	int i;

	usb_kill_anchored_urbs(&dev->rx_submitted);

	for (i = 0; i < KVASER_USB_MAX_RX_URBS; i++)
		usb_free_coherent(dev->udev, KVASER_USB_RX_BUFFER_SIZE,
				  dev->rxbuf[i], dev->rxbuf_dma[i]);

	for (i = 0; i < dev->nchannels; i++) {
		struct kvaser_usb_net_priv *priv = dev->nets[i];

		if (priv)
			kvaser_usb_unlink_tx_urbs(priv);
	}
}

static int kvaser_usb_close(struct net_device *netdev)
{
	struct kvaser_usb_net_priv *priv = netdev_priv(netdev);
	struct kvaser_usb *dev = priv->dev;
	const struct kvaser_usb_dev_ops *ops = dev->driver_info->ops;
	int err;

	netif_stop_queue(netdev);

	err = ops->dev_flush_queue(priv);
	if (err)
		netdev_warn(netdev, "Cannot flush queue, error %d\n", err);

	if (ops->dev_reset_chip) {
		err = ops->dev_reset_chip(dev, priv->channel);
		if (err)
			netdev_warn(netdev, "Cannot reset card, error %d\n",
				    err);
	}

	err = ops->dev_stop_chip(priv);
	if (err)
		netdev_warn(netdev, "Cannot stop device, error %d\n", err);

	/* reset tx contexts */
	kvaser_usb_unlink_tx_urbs(priv);

	priv->can.state = CAN_STATE_STOPPED;
	close_candev(priv->netdev);

	return 0;
}

static void kvaser_usb_write_bulk_callback(struct urb *urb)
{
	struct kvaser_usb_tx_urb_context *context = urb->context;
	struct kvaser_usb_net_priv *priv;
	struct net_device *netdev;

	if (WARN_ON(!context))
		return;

	priv = context->priv;
	netdev = priv->netdev;

	kfree(urb->transfer_buffer);

	if (!netif_device_present(netdev))
		return;

	if (urb->status)
		netdev_info(netdev, "Tx URB aborted (%d)\n", urb->status);
}

static netdev_tx_t kvaser_usb_start_xmit(struct sk_buff *skb,
					 struct net_device *netdev)
{
	struct kvaser_usb_net_priv *priv = netdev_priv(netdev);
	struct kvaser_usb *dev = priv->dev;
	const struct kvaser_usb_dev_ops *ops = dev->driver_info->ops;
	struct net_device_stats *stats = &netdev->stats;
	struct kvaser_usb_tx_urb_context *context = NULL;
	struct urb *urb;
	void *buf;
	int cmd_len = 0;
	int err, ret = NETDEV_TX_OK;
	unsigned int i;
	unsigned long flags;

	if (can_dropped_invalid_skb(netdev, skb))
		return NETDEV_TX_OK;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		stats->tx_dropped++;
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	spin_lock_irqsave(&priv->tx_contexts_lock, flags);
	for (i = 0; i < dev->max_tx_urbs; i++) {
		if (priv->tx_contexts[i].echo_index == dev->max_tx_urbs) {
			context = &priv->tx_contexts[i];

			context->echo_index = i;
			++priv->active_tx_contexts;
			if (priv->active_tx_contexts >= (int)dev->max_tx_urbs)
				netif_stop_queue(netdev);

			break;
		}
	}
	spin_unlock_irqrestore(&priv->tx_contexts_lock, flags);

	/* This should never happen; it implies a flow control bug */
	if (!context) {
		netdev_warn(netdev, "cannot find free context\n");

		ret = NETDEV_TX_BUSY;
		goto freeurb;
	}

	buf = ops->dev_frame_to_cmd(priv, skb, &cmd_len, context->echo_index);
	if (!buf) {
		stats->tx_dropped++;
		dev_kfree_skb(skb);
		spin_lock_irqsave(&priv->tx_contexts_lock, flags);

		context->echo_index = dev->max_tx_urbs;
		--priv->active_tx_contexts;
		netif_wake_queue(netdev);

		spin_unlock_irqrestore(&priv->tx_contexts_lock, flags);
		goto freeurb;
	}

	context->priv = priv;

	can_put_echo_skb(skb, netdev, context->echo_index, 0);

	usb_fill_bulk_urb(urb, dev->udev,
			  usb_sndbulkpipe(dev->udev,
					  dev->bulk_out->bEndpointAddress),
			  buf, cmd_len, kvaser_usb_write_bulk_callback,
			  context);
	usb_anchor_urb(urb, &priv->tx_submitted);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (unlikely(err)) {
		spin_lock_irqsave(&priv->tx_contexts_lock, flags);

		can_free_echo_skb(netdev, context->echo_index, NULL);
		context->echo_index = dev->max_tx_urbs;
		--priv->active_tx_contexts;
		netif_wake_queue(netdev);

		spin_unlock_irqrestore(&priv->tx_contexts_lock, flags);

		usb_unanchor_urb(urb);
		kfree(buf);

		stats->tx_dropped++;

		if (err == -ENODEV)
			netif_device_detach(netdev);
		else
			netdev_warn(netdev, "Failed tx_urb %d\n", err);

		goto freeurb;
	}

	ret = NETDEV_TX_OK;

freeurb:
	usb_free_urb(urb);
	return ret;
}

static const struct net_device_ops kvaser_usb_netdev_ops = {
	.ndo_open = kvaser_usb_open,
	.ndo_stop = kvaser_usb_close,
	.ndo_start_xmit = kvaser_usb_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static const struct net_device_ops kvaser_usb_netdev_ops_hwts = {
	.ndo_open = kvaser_usb_open,
	.ndo_stop = kvaser_usb_close,
	.ndo_eth_ioctl = can_eth_ioctl_hwts,
	.ndo_start_xmit = kvaser_usb_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static const struct ethtool_ops kvaser_usb_ethtool_ops = {
	.get_ts_info = ethtool_op_get_ts_info,
};

static const struct ethtool_ops kvaser_usb_ethtool_ops_hwts = {
	.get_ts_info = can_ethtool_op_get_ts_info_hwts,
};

static void kvaser_usb_remove_interfaces(struct kvaser_usb *dev)
{
	int i;

	for (i = 0; i < dev->nchannels; i++) {
		if (!dev->nets[i])
			continue;

		unregister_candev(dev->nets[i]->netdev);
	}

	kvaser_usb_unlink_all_urbs(dev);

	for (i = 0; i < dev->nchannels; i++) {
		if (!dev->nets[i])
			continue;

		free_candev(dev->nets[i]->netdev);
	}
}

static int kvaser_usb_init_one(struct kvaser_usb *dev, int channel)
{
	struct net_device *netdev;
	struct kvaser_usb_net_priv *priv;
	const struct kvaser_usb_driver_info *driver_info = dev->driver_info;
	const struct kvaser_usb_dev_ops *ops = driver_info->ops;
	int err;

	if (ops->dev_reset_chip) {
		err = ops->dev_reset_chip(dev, channel);
		if (err)
			return err;
	}

	netdev = alloc_candev(struct_size(priv, tx_contexts, dev->max_tx_urbs),
			      dev->max_tx_urbs);
	if (!netdev) {
		dev_err(&dev->intf->dev, "Cannot alloc candev\n");
		return -ENOMEM;
	}

	priv = netdev_priv(netdev);

	init_usb_anchor(&priv->tx_submitted);
	init_completion(&priv->start_comp);
	init_completion(&priv->stop_comp);
	init_completion(&priv->flush_comp);
	priv->can.ctrlmode_supported = 0;

	priv->dev = dev;
	priv->netdev = netdev;
	priv->channel = channel;

	spin_lock_init(&priv->tx_contexts_lock);
	kvaser_usb_reset_tx_urb_contexts(priv);

	priv->can.state = CAN_STATE_STOPPED;
	priv->can.clock.freq = dev->cfg->clock.freq;
	priv->can.bittiming_const = dev->cfg->bittiming_const;
	priv->can.do_set_bittiming = ops->dev_set_bittiming;
	priv->can.do_set_mode = ops->dev_set_mode;
	if ((driver_info->quirks & KVASER_USB_QUIRK_HAS_TXRX_ERRORS) ||
	    (priv->dev->card_data.capabilities & KVASER_USB_CAP_BERR_CAP))
		priv->can.do_get_berr_counter = ops->dev_get_berr_counter;
	if (driver_info->quirks & KVASER_USB_QUIRK_HAS_SILENT_MODE)
		priv->can.ctrlmode_supported |= CAN_CTRLMODE_LISTENONLY;

	priv->can.ctrlmode_supported |= dev->card_data.ctrlmode_supported;

	if (priv->can.ctrlmode_supported & CAN_CTRLMODE_FD) {
		priv->can.data_bittiming_const = dev->cfg->data_bittiming_const;
		priv->can.do_set_data_bittiming = ops->dev_set_data_bittiming;
	}

	netdev->flags |= IFF_ECHO;

	netdev->netdev_ops = &kvaser_usb_netdev_ops;
	if (driver_info->quirks & KVASER_USB_QUIRK_HAS_HARDWARE_TIMESTAMP) {
		netdev->netdev_ops = &kvaser_usb_netdev_ops_hwts;
		netdev->ethtool_ops = &kvaser_usb_ethtool_ops_hwts;
	} else {
		netdev->netdev_ops = &kvaser_usb_netdev_ops;
		netdev->ethtool_ops = &kvaser_usb_ethtool_ops;
	}
	SET_NETDEV_DEV(netdev, &dev->intf->dev);
	netdev->dev_id = channel;

	dev->nets[channel] = priv;

	err = register_candev(netdev);
	if (err) {
		dev_err(&dev->intf->dev, "Failed to register CAN device\n");
		free_candev(netdev);
		dev->nets[channel] = NULL;
		return err;
	}

	netdev_dbg(netdev, "device registered\n");

	return 0;
}

static int kvaser_usb_probe(struct usb_interface *intf,
			    const struct usb_device_id *id)
{
	struct kvaser_usb *dev;
	int err;
	int i;
	const struct kvaser_usb_driver_info *driver_info;
	const struct kvaser_usb_dev_ops *ops;

	driver_info = (const struct kvaser_usb_driver_info *)id->driver_info;
	if (!driver_info)
		return -ENODEV;

	dev = devm_kzalloc(&intf->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->intf = intf;
	dev->driver_info = driver_info;
	ops = driver_info->ops;

	err = ops->dev_setup_endpoints(dev);
	if (err) {
		dev_err(&intf->dev, "Cannot get usb endpoint(s)");
		return err;
	}

	dev->udev = interface_to_usbdev(intf);

	init_usb_anchor(&dev->rx_submitted);

	usb_set_intfdata(intf, dev);

	dev->card_data.ctrlmode_supported = 0;
	dev->card_data.capabilities = 0;
	err = ops->dev_init_card(dev);
	if (err) {
		dev_err(&intf->dev,
			"Failed to initialize card, error %d\n", err);
		return err;
	}

	err = ops->dev_get_software_info(dev);
	if (err) {
		dev_err(&intf->dev,
			"Cannot get software info, error %d\n", err);
		return err;
	}

	if (ops->dev_get_software_details) {
		err = ops->dev_get_software_details(dev);
		if (err) {
			dev_err(&intf->dev,
				"Cannot get software details, error %d\n", err);
			return err;
		}
	}

	if (WARN_ON(!dev->cfg))
		return -ENODEV;

	dev_dbg(&intf->dev, "Firmware version: %d.%d.%d\n",
		((dev->fw_version >> 24) & 0xff),
		((dev->fw_version >> 16) & 0xff),
		(dev->fw_version & 0xffff));

	dev_dbg(&intf->dev, "Max outstanding tx = %d URBs\n", dev->max_tx_urbs);

	err = ops->dev_get_card_info(dev);
	if (err) {
		dev_err(&intf->dev, "Cannot get card info, error %d\n", err);
		return err;
	}

	if (ops->dev_get_capabilities) {
		err = ops->dev_get_capabilities(dev);
		if (err) {
			dev_err(&intf->dev,
				"Cannot get capabilities, error %d\n", err);
			kvaser_usb_remove_interfaces(dev);
			return err;
		}
	}

	for (i = 0; i < dev->nchannels; i++) {
		err = kvaser_usb_init_one(dev, i);
		if (err) {
			kvaser_usb_remove_interfaces(dev);
			return err;
		}
	}

	return 0;
}

static void kvaser_usb_disconnect(struct usb_interface *intf)
{
	struct kvaser_usb *dev = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	if (!dev)
		return;

	kvaser_usb_remove_interfaces(dev);
}

static struct usb_driver kvaser_usb_driver = {
	.name = KBUILD_MODNAME,
	.probe = kvaser_usb_probe,
	.disconnect = kvaser_usb_disconnect,
	.id_table = kvaser_usb_table,
};

module_usb_driver(kvaser_usb_driver);

MODULE_AUTHOR("Olivier Sobrie <olivier@sobrie.be>");
MODULE_AUTHOR("Kvaser AB <support@kvaser.com>");
MODULE_DESCRIPTION("CAN driver for Kvaser CAN/USB devices");
MODULE_LICENSE("GPL v2");
