// SPDX-License-Identifier: GPL-2.0-only
/*
 * CAN driver for "8 devices" USB2CAN converter
 *
 * Copyright (C) 2012 Bernd Krumboeck (krumboeck@universalnet.at)
 *
 * This driver is inspired by the 3.2.0 version of drivers/net/can/usb/ems_usb.c
 * and drivers/net/can/usb/esd_usb2.c
 *
 * Many thanks to Gerhard Bertelsmann (info@gerhard-bertelsmann.de)
 * for testing and fixing this driver. Also many thanks to "8 devices",
 * who were very cooperative and answered my questions.
 */

#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/usb.h>

#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/can/led.h>

/* driver constants */
#define MAX_RX_URBS			20
#define MAX_TX_URBS			20
#define RX_BUFFER_SIZE			64

/* vendor and product id */
#define USB_8DEV_VENDOR_ID		0x0483
#define USB_8DEV_PRODUCT_ID		0x1234

/* endpoints */
enum usb_8dev_endpoint {
	USB_8DEV_ENDP_DATA_RX = 1,
	USB_8DEV_ENDP_DATA_TX,
	USB_8DEV_ENDP_CMD_RX,
	USB_8DEV_ENDP_CMD_TX
};

/* device CAN clock */
#define USB_8DEV_ABP_CLOCK		32000000

/* setup flags */
#define USB_8DEV_SILENT			0x01
#define USB_8DEV_LOOPBACK		0x02
#define USB_8DEV_DISABLE_AUTO_RESTRANS	0x04
#define USB_8DEV_STATUS_FRAME		0x08

/* commands */
enum usb_8dev_cmd {
	USB_8DEV_RESET = 1,
	USB_8DEV_OPEN,
	USB_8DEV_CLOSE,
	USB_8DEV_SET_SPEED,
	USB_8DEV_SET_MASK_FILTER,
	USB_8DEV_GET_STATUS,
	USB_8DEV_GET_STATISTICS,
	USB_8DEV_GET_SERIAL,
	USB_8DEV_GET_SOFTW_VER,
	USB_8DEV_GET_HARDW_VER,
	USB_8DEV_RESET_TIMESTAMP,
	USB_8DEV_GET_SOFTW_HARDW_VER
};

/* command options */
#define USB_8DEV_BAUD_MANUAL		0x09
#define USB_8DEV_CMD_START		0x11
#define USB_8DEV_CMD_END		0x22

#define USB_8DEV_CMD_SUCCESS		0
#define USB_8DEV_CMD_ERROR		255

#define USB_8DEV_CMD_TIMEOUT		1000

/* frames */
#define USB_8DEV_DATA_START		0x55
#define USB_8DEV_DATA_END		0xAA

#define USB_8DEV_TYPE_CAN_FRAME		0
#define USB_8DEV_TYPE_ERROR_FRAME	3

#define USB_8DEV_EXTID			0x01
#define USB_8DEV_RTR			0x02
#define USB_8DEV_ERR_FLAG		0x04

/* status */
#define USB_8DEV_STATUSMSG_OK		0x00  /* Normal condition. */
#define USB_8DEV_STATUSMSG_OVERRUN	0x01  /* Overrun occurred when sending */
#define USB_8DEV_STATUSMSG_BUSLIGHT	0x02  /* Error counter has reached 96 */
#define USB_8DEV_STATUSMSG_BUSHEAVY	0x03  /* Error count. has reached 128 */
#define USB_8DEV_STATUSMSG_BUSOFF	0x04  /* Device is in BUSOFF */
#define USB_8DEV_STATUSMSG_STUFF	0x20  /* Stuff Error */
#define USB_8DEV_STATUSMSG_FORM		0x21  /* Form Error */
#define USB_8DEV_STATUSMSG_ACK		0x23  /* Ack Error */
#define USB_8DEV_STATUSMSG_BIT0		0x24  /* Bit1 Error */
#define USB_8DEV_STATUSMSG_BIT1		0x25  /* Bit0 Error */
#define USB_8DEV_STATUSMSG_CRC		0x27  /* CRC Error */

#define USB_8DEV_RP_MASK		0x7F  /* Mask for Receive Error Bit */


/* table of devices that work with this driver */
static const struct usb_device_id usb_8dev_table[] = {
	{ USB_DEVICE(USB_8DEV_VENDOR_ID, USB_8DEV_PRODUCT_ID) },
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, usb_8dev_table);

struct usb_8dev_tx_urb_context {
	struct usb_8dev_priv *priv;

	u32 echo_index;
	u8 dlc;
};

/* Structure to hold all of our device specific stuff */
struct usb_8dev_priv {
	struct can_priv can; /* must be the first member */

	struct sk_buff *echo_skb[MAX_TX_URBS];

	struct usb_device *udev;
	struct net_device *netdev;

	atomic_t active_tx_urbs;
	struct usb_anchor tx_submitted;
	struct usb_8dev_tx_urb_context tx_contexts[MAX_TX_URBS];

	struct usb_anchor rx_submitted;

	struct can_berr_counter bec;

	u8 *cmd_msg_buffer;

	struct mutex usb_8dev_cmd_lock;
	void *rxbuf[MAX_RX_URBS];
	dma_addr_t rxbuf_dma[MAX_RX_URBS];
};

/* tx frame */
struct __packed usb_8dev_tx_msg {
	u8 begin;
	u8 flags;	/* RTR and EXT_ID flag */
	__be32 id;	/* upper 3 bits not used */
	u8 dlc;		/* data length code 0-8 bytes */
	u8 data[8];	/* 64-bit data */
	u8 end;
};

/* rx frame */
struct __packed usb_8dev_rx_msg {
	u8 begin;
	u8 type;		/* frame type */
	u8 flags;		/* RTR and EXT_ID flag */
	__be32 id;		/* upper 3 bits not used */
	u8 dlc;			/* data length code 0-8 bytes */
	u8 data[8];		/* 64-bit data */
	__be32 timestamp;	/* 32-bit timestamp */
	u8 end;
};

/* command frame */
struct __packed usb_8dev_cmd_msg {
	u8 begin;
	u8 channel;	/* unknown - always 0 */
	u8 command;	/* command to execute */
	u8 opt1;	/* optional parameter / return value */
	u8 opt2;	/* optional parameter 2 */
	u8 data[10];	/* optional parameter and data */
	u8 end;
};

static int usb_8dev_send_cmd_msg(struct usb_8dev_priv *priv, u8 *msg, int size)
{
	int actual_length;

	return usb_bulk_msg(priv->udev,
			    usb_sndbulkpipe(priv->udev, USB_8DEV_ENDP_CMD_TX),
			    msg, size, &actual_length, USB_8DEV_CMD_TIMEOUT);
}

static int usb_8dev_wait_cmd_msg(struct usb_8dev_priv *priv, u8 *msg, int size,
				int *actual_length)
{
	return usb_bulk_msg(priv->udev,
			    usb_rcvbulkpipe(priv->udev, USB_8DEV_ENDP_CMD_RX),
			    msg, size, actual_length, USB_8DEV_CMD_TIMEOUT);
}

/* Send command to device and receive result.
 * Command was successful when opt1 = 0.
 */
static int usb_8dev_send_cmd(struct usb_8dev_priv *priv,
			     struct usb_8dev_cmd_msg *out,
			     struct usb_8dev_cmd_msg *in)
{
	int err;
	int num_bytes_read;
	struct net_device *netdev;

	netdev = priv->netdev;

	out->begin = USB_8DEV_CMD_START;
	out->end = USB_8DEV_CMD_END;

	mutex_lock(&priv->usb_8dev_cmd_lock);

	memcpy(priv->cmd_msg_buffer, out,
		sizeof(struct usb_8dev_cmd_msg));

	err = usb_8dev_send_cmd_msg(priv, priv->cmd_msg_buffer,
				    sizeof(struct usb_8dev_cmd_msg));
	if (err < 0) {
		netdev_err(netdev, "sending command message failed\n");
		goto failed;
	}

	err = usb_8dev_wait_cmd_msg(priv, priv->cmd_msg_buffer,
				    sizeof(struct usb_8dev_cmd_msg),
				    &num_bytes_read);
	if (err < 0) {
		netdev_err(netdev, "no command message answer\n");
		goto failed;
	}

	memcpy(in, priv->cmd_msg_buffer, sizeof(struct usb_8dev_cmd_msg));

	if (in->begin != USB_8DEV_CMD_START || in->end != USB_8DEV_CMD_END ||
			num_bytes_read != 16 || in->opt1 != 0)
		err = -EPROTO;

failed:
	mutex_unlock(&priv->usb_8dev_cmd_lock);
	return err;
}

/* Send open command to device */
static int usb_8dev_cmd_open(struct usb_8dev_priv *priv)
{
	struct can_bittiming *bt = &priv->can.bittiming;
	struct usb_8dev_cmd_msg outmsg;
	struct usb_8dev_cmd_msg inmsg;
	u32 ctrlmode = priv->can.ctrlmode;
	u32 flags = USB_8DEV_STATUS_FRAME;
	__be32 beflags;
	__be16 bebrp;

	memset(&outmsg, 0, sizeof(outmsg));
	outmsg.command = USB_8DEV_OPEN;
	outmsg.opt1 = USB_8DEV_BAUD_MANUAL;
	outmsg.data[0] = bt->prop_seg + bt->phase_seg1;
	outmsg.data[1] = bt->phase_seg2;
	outmsg.data[2] = bt->sjw;

	/* BRP */
	bebrp = cpu_to_be16((u16)bt->brp);
	memcpy(&outmsg.data[3], &bebrp, sizeof(bebrp));

	/* flags */
	if (ctrlmode & CAN_CTRLMODE_LOOPBACK)
		flags |= USB_8DEV_LOOPBACK;
	if (ctrlmode & CAN_CTRLMODE_LISTENONLY)
		flags |= USB_8DEV_SILENT;
	if (ctrlmode & CAN_CTRLMODE_ONE_SHOT)
		flags |= USB_8DEV_DISABLE_AUTO_RESTRANS;

	beflags = cpu_to_be32(flags);
	memcpy(&outmsg.data[5], &beflags, sizeof(beflags));

	return usb_8dev_send_cmd(priv, &outmsg, &inmsg);
}

/* Send close command to device */
static int usb_8dev_cmd_close(struct usb_8dev_priv *priv)
{
	struct usb_8dev_cmd_msg inmsg;
	struct usb_8dev_cmd_msg outmsg = {
		.channel = 0,
		.command = USB_8DEV_CLOSE,
		.opt1 = 0,
		.opt2 = 0
	};

	return usb_8dev_send_cmd(priv, &outmsg, &inmsg);
}

/* Get firmware and hardware version */
static int usb_8dev_cmd_version(struct usb_8dev_priv *priv, u32 *res)
{
	struct usb_8dev_cmd_msg	inmsg;
	struct usb_8dev_cmd_msg	outmsg = {
		.channel = 0,
		.command = USB_8DEV_GET_SOFTW_HARDW_VER,
		.opt1 = 0,
		.opt2 = 0
	};

	int err = usb_8dev_send_cmd(priv, &outmsg, &inmsg);
	if (err)
		return err;

	*res = be32_to_cpup((__be32 *)inmsg.data);

	return err;
}

/* Set network device mode
 *
 * Maybe we should leave this function empty, because the device
 * set mode variable with open command.
 */
static int usb_8dev_set_mode(struct net_device *netdev, enum can_mode mode)
{
	struct usb_8dev_priv *priv = netdev_priv(netdev);
	int err = 0;

	switch (mode) {
	case CAN_MODE_START:
		err = usb_8dev_cmd_open(priv);
		if (err)
			netdev_warn(netdev, "couldn't start device");
		break;

	default:
		return -EOPNOTSUPP;
	}

	return err;
}

/* Read error/status frames */
static void usb_8dev_rx_err_msg(struct usb_8dev_priv *priv,
				struct usb_8dev_rx_msg *msg)
{
	struct can_frame *cf;
	struct sk_buff *skb;
	struct net_device_stats *stats = &priv->netdev->stats;

	/* Error message:
	 * byte 0: Status
	 * byte 1: bit   7: Receive Passive
	 * byte 1: bit 0-6: Receive Error Counter
	 * byte 2: Transmit Error Counter
	 * byte 3: Always 0 (maybe reserved for future use)
	 */

	u8 state = msg->data[0];
	u8 rxerr = msg->data[1] & USB_8DEV_RP_MASK;
	u8 txerr = msg->data[2];
	int rx_errors = 0;
	int tx_errors = 0;

	skb = alloc_can_err_skb(priv->netdev, &cf);
	if (!skb)
		return;

	switch (state) {
	case USB_8DEV_STATUSMSG_OK:
		priv->can.state = CAN_STATE_ERROR_ACTIVE;
		cf->can_id |= CAN_ERR_PROT;
		cf->data[2] = CAN_ERR_PROT_ACTIVE;
		break;
	case USB_8DEV_STATUSMSG_BUSOFF:
		priv->can.state = CAN_STATE_BUS_OFF;
		cf->can_id |= CAN_ERR_BUSOFF;
		priv->can.can_stats.bus_off++;
		can_bus_off(priv->netdev);
		break;
	case USB_8DEV_STATUSMSG_OVERRUN:
	case USB_8DEV_STATUSMSG_BUSLIGHT:
	case USB_8DEV_STATUSMSG_BUSHEAVY:
		cf->can_id |= CAN_ERR_CRTL;
		break;
	default:
		priv->can.state = CAN_STATE_ERROR_WARNING;
		cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;
		priv->can.can_stats.bus_error++;
		break;
	}

	switch (state) {
	case USB_8DEV_STATUSMSG_OK:
	case USB_8DEV_STATUSMSG_BUSOFF:
		break;
	case USB_8DEV_STATUSMSG_ACK:
		cf->can_id |= CAN_ERR_ACK;
		tx_errors = 1;
		break;
	case USB_8DEV_STATUSMSG_CRC:
		cf->data[3] = CAN_ERR_PROT_LOC_CRC_SEQ;
		rx_errors = 1;
		break;
	case USB_8DEV_STATUSMSG_BIT0:
		cf->data[2] |= CAN_ERR_PROT_BIT0;
		tx_errors = 1;
		break;
	case USB_8DEV_STATUSMSG_BIT1:
		cf->data[2] |= CAN_ERR_PROT_BIT1;
		tx_errors = 1;
		break;
	case USB_8DEV_STATUSMSG_FORM:
		cf->data[2] |= CAN_ERR_PROT_FORM;
		rx_errors = 1;
		break;
	case USB_8DEV_STATUSMSG_STUFF:
		cf->data[2] |= CAN_ERR_PROT_STUFF;
		rx_errors = 1;
		break;
	case USB_8DEV_STATUSMSG_OVERRUN:
		cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
		stats->rx_over_errors++;
		rx_errors = 1;
		break;
	case USB_8DEV_STATUSMSG_BUSLIGHT:
		priv->can.state = CAN_STATE_ERROR_WARNING;
		cf->data[1] = (txerr > rxerr) ?
			CAN_ERR_CRTL_TX_WARNING :
			CAN_ERR_CRTL_RX_WARNING;
		priv->can.can_stats.error_warning++;
		break;
	case USB_8DEV_STATUSMSG_BUSHEAVY:
		priv->can.state = CAN_STATE_ERROR_PASSIVE;
		cf->data[1] = (txerr > rxerr) ?
			CAN_ERR_CRTL_TX_PASSIVE :
			CAN_ERR_CRTL_RX_PASSIVE;
		priv->can.can_stats.error_passive++;
		break;
	default:
		netdev_warn(priv->netdev,
			    "Unknown status/error message (%d)\n", state);
		break;
	}

	if (tx_errors) {
		cf->data[2] |= CAN_ERR_PROT_TX;
		stats->tx_errors++;
	}

	if (rx_errors)
		stats->rx_errors++;

	cf->data[6] = txerr;
	cf->data[7] = rxerr;

	priv->bec.txerr = txerr;
	priv->bec.rxerr = rxerr;

	stats->rx_packets++;
	stats->rx_bytes += cf->len;
	netif_rx(skb);
}

/* Read data and status frames */
static void usb_8dev_rx_can_msg(struct usb_8dev_priv *priv,
				struct usb_8dev_rx_msg *msg)
{
	struct can_frame *cf;
	struct sk_buff *skb;
	struct net_device_stats *stats = &priv->netdev->stats;

	if (msg->type == USB_8DEV_TYPE_ERROR_FRAME &&
		   msg->flags == USB_8DEV_ERR_FLAG) {
		usb_8dev_rx_err_msg(priv, msg);
	} else if (msg->type == USB_8DEV_TYPE_CAN_FRAME) {
		skb = alloc_can_skb(priv->netdev, &cf);
		if (!skb)
			return;

		cf->can_id = be32_to_cpu(msg->id);
		can_frame_set_cc_len(cf, msg->dlc & 0xF, priv->can.ctrlmode);

		if (msg->flags & USB_8DEV_EXTID)
			cf->can_id |= CAN_EFF_FLAG;

		if (msg->flags & USB_8DEV_RTR)
			cf->can_id |= CAN_RTR_FLAG;
		else
			memcpy(cf->data, msg->data, cf->len);

		stats->rx_packets++;
		stats->rx_bytes += cf->len;
		netif_rx(skb);

		can_led_event(priv->netdev, CAN_LED_EVENT_RX);
	} else {
		netdev_warn(priv->netdev, "frame type %d unknown",
			 msg->type);
	}

}

/* Callback for reading data from device
 *
 * Check urb status, call read function and resubmit urb read operation.
 */
static void usb_8dev_read_bulk_callback(struct urb *urb)
{
	struct usb_8dev_priv *priv = urb->context;
	struct net_device *netdev;
	int retval;
	int pos = 0;

	netdev = priv->netdev;

	if (!netif_device_present(netdev))
		return;

	switch (urb->status) {
	case 0: /* success */
		break;

	case -ENOENT:
	case -EPIPE:
	case -EPROTO:
	case -ESHUTDOWN:
		return;

	default:
		netdev_info(netdev, "Rx URB aborted (%d)\n",
			 urb->status);
		goto resubmit_urb;
	}

	while (pos < urb->actual_length) {
		struct usb_8dev_rx_msg *msg;

		if (pos + sizeof(struct usb_8dev_rx_msg) > urb->actual_length) {
			netdev_err(priv->netdev, "format error\n");
			break;
		}

		msg = (struct usb_8dev_rx_msg *)(urb->transfer_buffer + pos);
		usb_8dev_rx_can_msg(priv, msg);

		pos += sizeof(struct usb_8dev_rx_msg);
	}

resubmit_urb:
	usb_fill_bulk_urb(urb, priv->udev,
			  usb_rcvbulkpipe(priv->udev, USB_8DEV_ENDP_DATA_RX),
			  urb->transfer_buffer, RX_BUFFER_SIZE,
			  usb_8dev_read_bulk_callback, priv);

	retval = usb_submit_urb(urb, GFP_ATOMIC);

	if (retval == -ENODEV)
		netif_device_detach(netdev);
	else if (retval)
		netdev_err(netdev,
			"failed resubmitting read bulk urb: %d\n", retval);
}

/* Callback handler for write operations
 *
 * Free allocated buffers, check transmit status and
 * calculate statistic.
 */
static void usb_8dev_write_bulk_callback(struct urb *urb)
{
	struct usb_8dev_tx_urb_context *context = urb->context;
	struct usb_8dev_priv *priv;
	struct net_device *netdev;

	BUG_ON(!context);

	priv = context->priv;
	netdev = priv->netdev;

	/* free up our allocated buffer */
	usb_free_coherent(urb->dev, urb->transfer_buffer_length,
			  urb->transfer_buffer, urb->transfer_dma);

	atomic_dec(&priv->active_tx_urbs);

	if (!netif_device_present(netdev))
		return;

	if (urb->status)
		netdev_info(netdev, "Tx URB aborted (%d)\n",
			 urb->status);

	netdev->stats.tx_packets++;
	netdev->stats.tx_bytes += context->dlc;

	can_get_echo_skb(netdev, context->echo_index, NULL);

	can_led_event(netdev, CAN_LED_EVENT_TX);

	/* Release context */
	context->echo_index = MAX_TX_URBS;

	netif_wake_queue(netdev);
}

/* Send data to device */
static netdev_tx_t usb_8dev_start_xmit(struct sk_buff *skb,
				      struct net_device *netdev)
{
	struct usb_8dev_priv *priv = netdev_priv(netdev);
	struct net_device_stats *stats = &netdev->stats;
	struct can_frame *cf = (struct can_frame *) skb->data;
	struct usb_8dev_tx_msg *msg;
	struct urb *urb;
	struct usb_8dev_tx_urb_context *context = NULL;
	u8 *buf;
	int i, err;
	size_t size = sizeof(struct usb_8dev_tx_msg);

	if (can_dropped_invalid_skb(netdev, skb))
		return NETDEV_TX_OK;

	/* create a URB, and a buffer for it, and copy the data to the URB */
	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb)
		goto nomem;

	buf = usb_alloc_coherent(priv->udev, size, GFP_ATOMIC,
				 &urb->transfer_dma);
	if (!buf) {
		netdev_err(netdev, "No memory left for USB buffer\n");
		goto nomembuf;
	}

	memset(buf, 0, size);

	msg = (struct usb_8dev_tx_msg *)buf;
	msg->begin = USB_8DEV_DATA_START;
	msg->flags = 0x00;

	if (cf->can_id & CAN_RTR_FLAG)
		msg->flags |= USB_8DEV_RTR;

	if (cf->can_id & CAN_EFF_FLAG)
		msg->flags |= USB_8DEV_EXTID;

	msg->id = cpu_to_be32(cf->can_id & CAN_ERR_MASK);
	msg->dlc = can_get_cc_dlc(cf, priv->can.ctrlmode);
	memcpy(msg->data, cf->data, cf->len);
	msg->end = USB_8DEV_DATA_END;

	for (i = 0; i < MAX_TX_URBS; i++) {
		if (priv->tx_contexts[i].echo_index == MAX_TX_URBS) {
			context = &priv->tx_contexts[i];
			break;
		}
	}

	/* May never happen! When this happens we'd more URBs in flight as
	 * allowed (MAX_TX_URBS).
	 */
	if (!context)
		goto nofreecontext;

	context->priv = priv;
	context->echo_index = i;
	context->dlc = cf->len;

	usb_fill_bulk_urb(urb, priv->udev,
			  usb_sndbulkpipe(priv->udev, USB_8DEV_ENDP_DATA_TX),
			  buf, size, usb_8dev_write_bulk_callback, context);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	usb_anchor_urb(urb, &priv->tx_submitted);

	can_put_echo_skb(skb, netdev, context->echo_index, 0);

	atomic_inc(&priv->active_tx_urbs);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (unlikely(err))
		goto failed;
	else if (atomic_read(&priv->active_tx_urbs) >= MAX_TX_URBS)
		/* Slow down tx path */
		netif_stop_queue(netdev);

	/* Release our reference to this URB, the USB core will eventually free
	 * it entirely.
	 */
	usb_free_urb(urb);

	return NETDEV_TX_OK;

nofreecontext:
	usb_free_coherent(priv->udev, size, buf, urb->transfer_dma);
	usb_free_urb(urb);

	netdev_warn(netdev, "couldn't find free context");

	return NETDEV_TX_BUSY;

failed:
	can_free_echo_skb(netdev, context->echo_index, NULL);

	usb_unanchor_urb(urb);
	usb_free_coherent(priv->udev, size, buf, urb->transfer_dma);

	atomic_dec(&priv->active_tx_urbs);

	if (err == -ENODEV)
		netif_device_detach(netdev);
	else
		netdev_warn(netdev, "failed tx_urb %d\n", err);

nomembuf:
	usb_free_urb(urb);

nomem:
	dev_kfree_skb(skb);
	stats->tx_dropped++;

	return NETDEV_TX_OK;
}

static int usb_8dev_get_berr_counter(const struct net_device *netdev,
				     struct can_berr_counter *bec)
{
	struct usb_8dev_priv *priv = netdev_priv(netdev);

	bec->txerr = priv->bec.txerr;
	bec->rxerr = priv->bec.rxerr;

	return 0;
}

/* Start USB device */
static int usb_8dev_start(struct usb_8dev_priv *priv)
{
	struct net_device *netdev = priv->netdev;
	int err, i;

	for (i = 0; i < MAX_RX_URBS; i++) {
		struct urb *urb = NULL;
		u8 *buf;
		dma_addr_t buf_dma;

		/* create a URB, and a buffer for it */
		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			err = -ENOMEM;
			break;
		}

		buf = usb_alloc_coherent(priv->udev, RX_BUFFER_SIZE, GFP_KERNEL,
					 &buf_dma);
		if (!buf) {
			netdev_err(netdev, "No memory left for USB buffer\n");
			usb_free_urb(urb);
			err = -ENOMEM;
			break;
		}

		urb->transfer_dma = buf_dma;

		usb_fill_bulk_urb(urb, priv->udev,
				  usb_rcvbulkpipe(priv->udev,
						  USB_8DEV_ENDP_DATA_RX),
				  buf, RX_BUFFER_SIZE,
				  usb_8dev_read_bulk_callback, priv);
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		usb_anchor_urb(urb, &priv->rx_submitted);

		err = usb_submit_urb(urb, GFP_KERNEL);
		if (err) {
			usb_unanchor_urb(urb);
			usb_free_coherent(priv->udev, RX_BUFFER_SIZE, buf,
					  urb->transfer_dma);
			usb_free_urb(urb);
			break;
		}

		priv->rxbuf[i] = buf;
		priv->rxbuf_dma[i] = buf_dma;

		/* Drop reference, USB core will take care of freeing it */
		usb_free_urb(urb);
	}

	/* Did we submit any URBs */
	if (i == 0) {
		netdev_warn(netdev, "couldn't setup read URBs\n");
		return err;
	}

	/* Warn if we've couldn't transmit all the URBs */
	if (i < MAX_RX_URBS)
		netdev_warn(netdev, "rx performance may be slow\n");

	err = usb_8dev_cmd_open(priv);
	if (err)
		goto failed;

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	return 0;

failed:
	if (err == -ENODEV)
		netif_device_detach(priv->netdev);

	netdev_warn(netdev, "couldn't submit control: %d\n", err);

	return err;
}

/* Open USB device */
static int usb_8dev_open(struct net_device *netdev)
{
	struct usb_8dev_priv *priv = netdev_priv(netdev);
	int err;

	/* common open */
	err = open_candev(netdev);
	if (err)
		return err;

	can_led_event(netdev, CAN_LED_EVENT_OPEN);

	/* finally start device */
	err = usb_8dev_start(priv);
	if (err) {
		if (err == -ENODEV)
			netif_device_detach(priv->netdev);

		netdev_warn(netdev, "couldn't start device: %d\n",
			 err);

		close_candev(netdev);

		return err;
	}

	netif_start_queue(netdev);

	return 0;
}

static void unlink_all_urbs(struct usb_8dev_priv *priv)
{
	int i;

	usb_kill_anchored_urbs(&priv->rx_submitted);

	for (i = 0; i < MAX_RX_URBS; ++i)
		usb_free_coherent(priv->udev, RX_BUFFER_SIZE,
				  priv->rxbuf[i], priv->rxbuf_dma[i]);

	usb_kill_anchored_urbs(&priv->tx_submitted);
	atomic_set(&priv->active_tx_urbs, 0);

	for (i = 0; i < MAX_TX_URBS; i++)
		priv->tx_contexts[i].echo_index = MAX_TX_URBS;
}

/* Close USB device */
static int usb_8dev_close(struct net_device *netdev)
{
	struct usb_8dev_priv *priv = netdev_priv(netdev);
	int err = 0;

	/* Send CLOSE command to CAN controller */
	err = usb_8dev_cmd_close(priv);
	if (err)
		netdev_warn(netdev, "couldn't stop device");

	priv->can.state = CAN_STATE_STOPPED;

	netif_stop_queue(netdev);

	/* Stop polling */
	unlink_all_urbs(priv);

	close_candev(netdev);

	can_led_event(netdev, CAN_LED_EVENT_STOP);

	return err;
}

static const struct net_device_ops usb_8dev_netdev_ops = {
	.ndo_open = usb_8dev_open,
	.ndo_stop = usb_8dev_close,
	.ndo_start_xmit = usb_8dev_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static const struct can_bittiming_const usb_8dev_bittiming_const = {
	.name = "usb_8dev",
	.tseg1_min = 1,
	.tseg1_max = 16,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 1024,
	.brp_inc = 1,
};

/* Probe USB device
 *
 * Check device and firmware.
 * Set supported modes and bittiming constants.
 * Allocate some memory.
 */
static int usb_8dev_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct net_device *netdev;
	struct usb_8dev_priv *priv;
	int i, err = -ENOMEM;
	u32 version;
	char buf[18];
	struct usb_device *usbdev = interface_to_usbdev(intf);

	/* product id looks strange, better we also check iProduct string */
	if (usb_string(usbdev, usbdev->descriptor.iProduct, buf,
		       sizeof(buf)) > 0 && strcmp(buf, "USB2CAN converter")) {
		dev_info(&usbdev->dev, "ignoring: not an USB2CAN converter\n");
		return -ENODEV;
	}

	netdev = alloc_candev(sizeof(struct usb_8dev_priv), MAX_TX_URBS);
	if (!netdev) {
		dev_err(&intf->dev, "Couldn't alloc candev\n");
		return -ENOMEM;
	}

	priv = netdev_priv(netdev);

	priv->udev = usbdev;
	priv->netdev = netdev;

	priv->can.state = CAN_STATE_STOPPED;
	priv->can.clock.freq = USB_8DEV_ABP_CLOCK;
	priv->can.bittiming_const = &usb_8dev_bittiming_const;
	priv->can.do_set_mode = usb_8dev_set_mode;
	priv->can.do_get_berr_counter = usb_8dev_get_berr_counter;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK |
				      CAN_CTRLMODE_LISTENONLY |
				      CAN_CTRLMODE_ONE_SHOT |
				      CAN_CTRLMODE_CC_LEN8_DLC;

	netdev->netdev_ops = &usb_8dev_netdev_ops;

	netdev->flags |= IFF_ECHO; /* we support local echo */

	init_usb_anchor(&priv->rx_submitted);

	init_usb_anchor(&priv->tx_submitted);
	atomic_set(&priv->active_tx_urbs, 0);

	for (i = 0; i < MAX_TX_URBS; i++)
		priv->tx_contexts[i].echo_index = MAX_TX_URBS;

	priv->cmd_msg_buffer = devm_kzalloc(&intf->dev, sizeof(struct usb_8dev_cmd_msg),
					    GFP_KERNEL);
	if (!priv->cmd_msg_buffer)
		goto cleanup_candev;

	usb_set_intfdata(intf, priv);

	SET_NETDEV_DEV(netdev, &intf->dev);

	mutex_init(&priv->usb_8dev_cmd_lock);

	err = register_candev(netdev);
	if (err) {
		netdev_err(netdev,
			"couldn't register CAN device: %d\n", err);
		goto cleanup_candev;
	}

	err = usb_8dev_cmd_version(priv, &version);
	if (err) {
		netdev_err(netdev, "can't get firmware version\n");
		goto cleanup_unregister_candev;
	} else {
		netdev_info(netdev,
			 "firmware: %d.%d, hardware: %d.%d\n",
			 (version>>24) & 0xff, (version>>16) & 0xff,
			 (version>>8) & 0xff, version & 0xff);
	}

	devm_can_led_init(netdev);

	return 0;

cleanup_unregister_candev:
	unregister_netdev(priv->netdev);

cleanup_candev:
	free_candev(netdev);

	return err;

}

/* Called by the usb core when driver is unloaded or device is removed */
static void usb_8dev_disconnect(struct usb_interface *intf)
{
	struct usb_8dev_priv *priv = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	if (priv) {
		netdev_info(priv->netdev, "device disconnected\n");

		unregister_netdev(priv->netdev);
		unlink_all_urbs(priv);
		free_candev(priv->netdev);
	}

}

static struct usb_driver usb_8dev_driver = {
	.name =		"usb_8dev",
	.probe =	usb_8dev_probe,
	.disconnect =	usb_8dev_disconnect,
	.id_table =	usb_8dev_table,
};

module_usb_driver(usb_8dev_driver);

MODULE_AUTHOR("Bernd Krumboeck <krumboeck@universalnet.at>");
MODULE_DESCRIPTION("CAN driver for 8 devices USB2CAN interfaces");
MODULE_LICENSE("GPL v2");
