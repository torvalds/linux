// SPDX-License-Identifier: GPL-2.0-only
/*
 * CAN driver for esd CAN-USB/2 and CAN-USB/Micro
 *
 * Copyright (C) 2010-2012 Matthias Fuchs <matthias.fuchs@esd.eu>, esd gmbh
 */
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/usb.h>

#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>

MODULE_AUTHOR("Matthias Fuchs <matthias.fuchs@esd.eu>");
MODULE_DESCRIPTION("CAN driver for esd CAN-USB/2 and CAN-USB/Micro interfaces");
MODULE_LICENSE("GPL v2");

/* Define these values to match your devices */
#define USB_ESDGMBH_VENDOR_ID	0x0ab4
#define USB_CANUSB2_PRODUCT_ID	0x0010
#define USB_CANUSBM_PRODUCT_ID	0x0011

#define ESD_USB2_CAN_CLOCK	60000000
#define ESD_USBM_CAN_CLOCK	36000000
#define ESD_USB2_MAX_NETS	2

/* USB2 commands */
#define CMD_VERSION		1 /* also used for VERSION_REPLY */
#define CMD_CAN_RX		2 /* device to host only */
#define CMD_CAN_TX		3 /* also used for TX_DONE */
#define CMD_SETBAUD		4 /* also used for SETBAUD_REPLY */
#define CMD_TS			5 /* also used for TS_REPLY */
#define CMD_IDADD		6 /* also used for IDADD_REPLY */

/* esd CAN message flags - dlc field */
#define ESD_RTR			0x10

/* esd CAN message flags - id field */
#define ESD_EXTID		0x20000000
#define ESD_EVENT		0x40000000
#define ESD_IDMASK		0x1fffffff

/* esd CAN event ids used by this driver */
#define ESD_EV_CAN_ERROR_EXT	2

/* baudrate message flags */
#define ESD_USB2_UBR		0x80000000
#define ESD_USB2_LOM		0x40000000
#define ESD_USB2_NO_BAUDRATE	0x7fffffff
#define ESD_USB2_TSEG1_MIN	1
#define ESD_USB2_TSEG1_MAX	16
#define ESD_USB2_TSEG1_SHIFT	16
#define ESD_USB2_TSEG2_MIN	1
#define ESD_USB2_TSEG2_MAX	8
#define ESD_USB2_TSEG2_SHIFT	20
#define ESD_USB2_SJW_MAX	4
#define ESD_USB2_SJW_SHIFT	14
#define ESD_USBM_SJW_SHIFT	24
#define ESD_USB2_BRP_MIN	1
#define ESD_USB2_BRP_MAX	1024
#define ESD_USB2_BRP_INC	1
#define ESD_USB2_3_SAMPLES	0x00800000

/* esd IDADD message */
#define ESD_ID_ENABLE		0x80
#define ESD_MAX_ID_SEGMENT	64

/* SJA1000 ECC register (emulated by usb2 firmware) */
#define SJA1000_ECC_SEG		0x1F
#define SJA1000_ECC_DIR		0x20
#define SJA1000_ECC_ERR		0x06
#define SJA1000_ECC_BIT		0x00
#define SJA1000_ECC_FORM	0x40
#define SJA1000_ECC_STUFF	0x80
#define SJA1000_ECC_MASK	0xc0

/* esd bus state event codes */
#define ESD_BUSSTATE_MASK	0xc0
#define ESD_BUSSTATE_WARN	0x40
#define ESD_BUSSTATE_ERRPASSIVE	0x80
#define ESD_BUSSTATE_BUSOFF	0xc0

#define RX_BUFFER_SIZE		1024
#define MAX_RX_URBS		4
#define MAX_TX_URBS		16 /* must be power of 2 */

struct header_msg {
	u8 len; /* len is always the total message length in 32bit words */
	u8 cmd;
	u8 rsvd[2];
};

struct version_msg {
	u8 len;
	u8 cmd;
	u8 rsvd;
	u8 flags;
	__le32 drv_version;
};

struct version_reply_msg {
	u8 len;
	u8 cmd;
	u8 nets;
	u8 features;
	__le32 version;
	u8 name[16];
	__le32 rsvd;
	__le32 ts;
};

struct rx_msg {
	u8 len;
	u8 cmd;
	u8 net;
	u8 dlc;
	__le32 ts;
	__le32 id; /* upper 3 bits contain flags */
	u8 data[8];
};

struct tx_msg {
	u8 len;
	u8 cmd;
	u8 net;
	u8 dlc;
	u32 hnd;	/* opaque handle, not used by device */
	__le32 id; /* upper 3 bits contain flags */
	u8 data[8];
};

struct tx_done_msg {
	u8 len;
	u8 cmd;
	u8 net;
	u8 status;
	u32 hnd;	/* opaque handle, not used by device */
	__le32 ts;
};

struct id_filter_msg {
	u8 len;
	u8 cmd;
	u8 net;
	u8 option;
	__le32 mask[ESD_MAX_ID_SEGMENT + 1];
};

struct set_baudrate_msg {
	u8 len;
	u8 cmd;
	u8 net;
	u8 rsvd;
	__le32 baud;
};

/* Main message type used between library and application */
struct __attribute__ ((packed)) esd_usb2_msg {
	union {
		struct header_msg hdr;
		struct version_msg version;
		struct version_reply_msg version_reply;
		struct rx_msg rx;
		struct tx_msg tx;
		struct tx_done_msg txdone;
		struct set_baudrate_msg setbaud;
		struct id_filter_msg filter;
	} msg;
};

static struct usb_device_id esd_usb2_table[] = {
	{USB_DEVICE(USB_ESDGMBH_VENDOR_ID, USB_CANUSB2_PRODUCT_ID)},
	{USB_DEVICE(USB_ESDGMBH_VENDOR_ID, USB_CANUSBM_PRODUCT_ID)},
	{}
};
MODULE_DEVICE_TABLE(usb, esd_usb2_table);

struct esd_usb2_net_priv;

struct esd_tx_urb_context {
	struct esd_usb2_net_priv *priv;
	u32 echo_index;
	int dlc;
};

struct esd_usb2 {
	struct usb_device *udev;
	struct esd_usb2_net_priv *nets[ESD_USB2_MAX_NETS];

	struct usb_anchor rx_submitted;

	int net_count;
	u32 version;
	int rxinitdone;
	void *rxbuf[MAX_RX_URBS];
	dma_addr_t rxbuf_dma[MAX_RX_URBS];
};

struct esd_usb2_net_priv {
	struct can_priv can; /* must be the first member */

	atomic_t active_tx_jobs;
	struct usb_anchor tx_submitted;
	struct esd_tx_urb_context tx_contexts[MAX_TX_URBS];

	struct esd_usb2 *usb2;
	struct net_device *netdev;
	int index;
	u8 old_state;
	struct can_berr_counter bec;
};

static void esd_usb2_rx_event(struct esd_usb2_net_priv *priv,
			      struct esd_usb2_msg *msg)
{
	struct net_device_stats *stats = &priv->netdev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	u32 id = le32_to_cpu(msg->msg.rx.id) & ESD_IDMASK;

	if (id == ESD_EV_CAN_ERROR_EXT) {
		u8 state = msg->msg.rx.data[0];
		u8 ecc = msg->msg.rx.data[1];
		u8 rxerr = msg->msg.rx.data[2];
		u8 txerr = msg->msg.rx.data[3];

		netdev_dbg(priv->netdev,
			   "CAN_ERR_EV_EXT: dlc=%#02x state=%02x ecc=%02x rec=%02x tec=%02x\n",
			   msg->msg.rx.dlc, state, ecc, rxerr, txerr);

		skb = alloc_can_err_skb(priv->netdev, &cf);
		if (skb == NULL) {
			stats->rx_dropped++;
			return;
		}

		if (state != priv->old_state) {
			priv->old_state = state;

			switch (state & ESD_BUSSTATE_MASK) {
			case ESD_BUSSTATE_BUSOFF:
				priv->can.state = CAN_STATE_BUS_OFF;
				cf->can_id |= CAN_ERR_BUSOFF;
				priv->can.can_stats.bus_off++;
				can_bus_off(priv->netdev);
				break;
			case ESD_BUSSTATE_WARN:
				priv->can.state = CAN_STATE_ERROR_WARNING;
				priv->can.can_stats.error_warning++;
				break;
			case ESD_BUSSTATE_ERRPASSIVE:
				priv->can.state = CAN_STATE_ERROR_PASSIVE;
				priv->can.can_stats.error_passive++;
				break;
			default:
				priv->can.state = CAN_STATE_ERROR_ACTIVE;
				txerr = 0;
				rxerr = 0;
				break;
			}
		} else {
			priv->can.can_stats.bus_error++;
			stats->rx_errors++;

			cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;

			switch (ecc & SJA1000_ECC_MASK) {
			case SJA1000_ECC_BIT:
				cf->data[2] |= CAN_ERR_PROT_BIT;
				break;
			case SJA1000_ECC_FORM:
				cf->data[2] |= CAN_ERR_PROT_FORM;
				break;
			case SJA1000_ECC_STUFF:
				cf->data[2] |= CAN_ERR_PROT_STUFF;
				break;
			default:
				break;
			}

			/* Error occurred during transmission? */
			if (!(ecc & SJA1000_ECC_DIR))
				cf->data[2] |= CAN_ERR_PROT_TX;

			/* Bit stream position in CAN frame as the error was detected */
			cf->data[3] = ecc & SJA1000_ECC_SEG;

			if (priv->can.state == CAN_STATE_ERROR_WARNING ||
			    priv->can.state == CAN_STATE_ERROR_PASSIVE) {
				cf->data[1] = (txerr > rxerr) ?
					CAN_ERR_CRTL_TX_PASSIVE :
					CAN_ERR_CRTL_RX_PASSIVE;
			}
			cf->data[6] = txerr;
			cf->data[7] = rxerr;
		}

		priv->bec.txerr = txerr;
		priv->bec.rxerr = rxerr;

		stats->rx_packets++;
		stats->rx_bytes += cf->can_dlc;
		netif_rx(skb);
	}
}

static void esd_usb2_rx_can_msg(struct esd_usb2_net_priv *priv,
				struct esd_usb2_msg *msg)
{
	struct net_device_stats *stats = &priv->netdev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	int i;
	u32 id;

	if (!netif_device_present(priv->netdev))
		return;

	id = le32_to_cpu(msg->msg.rx.id);

	if (id & ESD_EVENT) {
		esd_usb2_rx_event(priv, msg);
	} else {
		skb = alloc_can_skb(priv->netdev, &cf);
		if (skb == NULL) {
			stats->rx_dropped++;
			return;
		}

		cf->can_id = id & ESD_IDMASK;
		cf->can_dlc = get_can_dlc(msg->msg.rx.dlc & ~ESD_RTR);

		if (id & ESD_EXTID)
			cf->can_id |= CAN_EFF_FLAG;

		if (msg->msg.rx.dlc & ESD_RTR) {
			cf->can_id |= CAN_RTR_FLAG;
		} else {
			for (i = 0; i < cf->can_dlc; i++)
				cf->data[i] = msg->msg.rx.data[i];
		}

		stats->rx_packets++;
		stats->rx_bytes += cf->can_dlc;
		netif_rx(skb);
	}

	return;
}

static void esd_usb2_tx_done_msg(struct esd_usb2_net_priv *priv,
				 struct esd_usb2_msg *msg)
{
	struct net_device_stats *stats = &priv->netdev->stats;
	struct net_device *netdev = priv->netdev;
	struct esd_tx_urb_context *context;

	if (!netif_device_present(netdev))
		return;

	context = &priv->tx_contexts[msg->msg.txdone.hnd & (MAX_TX_URBS - 1)];

	if (!msg->msg.txdone.status) {
		stats->tx_packets++;
		stats->tx_bytes += context->dlc;
		can_get_echo_skb(netdev, context->echo_index);
	} else {
		stats->tx_errors++;
		can_free_echo_skb(netdev, context->echo_index);
	}

	/* Release context */
	context->echo_index = MAX_TX_URBS;
	atomic_dec(&priv->active_tx_jobs);

	netif_wake_queue(netdev);
}

static void esd_usb2_read_bulk_callback(struct urb *urb)
{
	struct esd_usb2 *dev = urb->context;
	int retval;
	int pos = 0;
	int i;

	switch (urb->status) {
	case 0: /* success */
		break;

	case -ENOENT:
	case -EPIPE:
	case -EPROTO:
	case -ESHUTDOWN:
		return;

	default:
		dev_info(dev->udev->dev.parent,
			 "Rx URB aborted (%d)\n", urb->status);
		goto resubmit_urb;
	}

	while (pos < urb->actual_length) {
		struct esd_usb2_msg *msg;

		msg = (struct esd_usb2_msg *)(urb->transfer_buffer + pos);

		switch (msg->msg.hdr.cmd) {
		case CMD_CAN_RX:
			if (msg->msg.rx.net >= dev->net_count) {
				dev_err(dev->udev->dev.parent, "format error\n");
				break;
			}

			esd_usb2_rx_can_msg(dev->nets[msg->msg.rx.net], msg);
			break;

		case CMD_CAN_TX:
			if (msg->msg.txdone.net >= dev->net_count) {
				dev_err(dev->udev->dev.parent, "format error\n");
				break;
			}

			esd_usb2_tx_done_msg(dev->nets[msg->msg.txdone.net],
					     msg);
			break;
		}

		pos += msg->msg.hdr.len << 2;

		if (pos > urb->actual_length) {
			dev_err(dev->udev->dev.parent, "format error\n");
			break;
		}
	}

resubmit_urb:
	usb_fill_bulk_urb(urb, dev->udev, usb_rcvbulkpipe(dev->udev, 1),
			  urb->transfer_buffer, RX_BUFFER_SIZE,
			  esd_usb2_read_bulk_callback, dev);

	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval == -ENODEV) {
		for (i = 0; i < dev->net_count; i++) {
			if (dev->nets[i])
				netif_device_detach(dev->nets[i]->netdev);
		}
	} else if (retval) {
		dev_err(dev->udev->dev.parent,
			"failed resubmitting read bulk urb: %d\n", retval);
	}

	return;
}

/*
 * callback for bulk IN urb
 */
static void esd_usb2_write_bulk_callback(struct urb *urb)
{
	struct esd_tx_urb_context *context = urb->context;
	struct esd_usb2_net_priv *priv;
	struct net_device *netdev;
	size_t size = sizeof(struct esd_usb2_msg);

	WARN_ON(!context);

	priv = context->priv;
	netdev = priv->netdev;

	/* free up our allocated buffer */
	usb_free_coherent(urb->dev, size,
			  urb->transfer_buffer, urb->transfer_dma);

	if (!netif_device_present(netdev))
		return;

	if (urb->status)
		netdev_info(netdev, "Tx URB aborted (%d)\n", urb->status);

	netif_trans_update(netdev);
}

static ssize_t show_firmware(struct device *d,
			     struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(d);
	struct esd_usb2 *dev = usb_get_intfdata(intf);

	return sprintf(buf, "%d.%d.%d\n",
		       (dev->version >> 12) & 0xf,
		       (dev->version >> 8) & 0xf,
		       dev->version & 0xff);
}
static DEVICE_ATTR(firmware, 0444, show_firmware, NULL);

static ssize_t show_hardware(struct device *d,
			     struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(d);
	struct esd_usb2 *dev = usb_get_intfdata(intf);

	return sprintf(buf, "%d.%d.%d\n",
		       (dev->version >> 28) & 0xf,
		       (dev->version >> 24) & 0xf,
		       (dev->version >> 16) & 0xff);
}
static DEVICE_ATTR(hardware, 0444, show_hardware, NULL);

static ssize_t show_nets(struct device *d,
			 struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(d);
	struct esd_usb2 *dev = usb_get_intfdata(intf);

	return sprintf(buf, "%d", dev->net_count);
}
static DEVICE_ATTR(nets, 0444, show_nets, NULL);

static int esd_usb2_send_msg(struct esd_usb2 *dev, struct esd_usb2_msg *msg)
{
	int actual_length;

	return usb_bulk_msg(dev->udev,
			    usb_sndbulkpipe(dev->udev, 2),
			    msg,
			    msg->msg.hdr.len << 2,
			    &actual_length,
			    1000);
}

static int esd_usb2_wait_msg(struct esd_usb2 *dev,
			     struct esd_usb2_msg *msg)
{
	int actual_length;

	return usb_bulk_msg(dev->udev,
			    usb_rcvbulkpipe(dev->udev, 1),
			    msg,
			    sizeof(*msg),
			    &actual_length,
			    1000);
}

static int esd_usb2_setup_rx_urbs(struct esd_usb2 *dev)
{
	int i, err = 0;

	if (dev->rxinitdone)
		return 0;

	for (i = 0; i < MAX_RX_URBS; i++) {
		struct urb *urb = NULL;
		u8 *buf = NULL;
		dma_addr_t buf_dma;

		/* create a URB, and a buffer for it */
		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			err = -ENOMEM;
			break;
		}

		buf = usb_alloc_coherent(dev->udev, RX_BUFFER_SIZE, GFP_KERNEL,
					 &buf_dma);
		if (!buf) {
			dev_warn(dev->udev->dev.parent,
				 "No memory left for USB buffer\n");
			err = -ENOMEM;
			goto freeurb;
		}

		urb->transfer_dma = buf_dma;

		usb_fill_bulk_urb(urb, dev->udev,
				  usb_rcvbulkpipe(dev->udev, 1),
				  buf, RX_BUFFER_SIZE,
				  esd_usb2_read_bulk_callback, dev);
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		usb_anchor_urb(urb, &dev->rx_submitted);

		err = usb_submit_urb(urb, GFP_KERNEL);
		if (err) {
			usb_unanchor_urb(urb);
			usb_free_coherent(dev->udev, RX_BUFFER_SIZE, buf,
					  urb->transfer_dma);
			goto freeurb;
		}

		dev->rxbuf[i] = buf;
		dev->rxbuf_dma[i] = buf_dma;

freeurb:
		/* Drop reference, USB core will take care of freeing it */
		usb_free_urb(urb);
		if (err)
			break;
	}

	/* Did we submit any URBs */
	if (i == 0) {
		dev_err(dev->udev->dev.parent, "couldn't setup read URBs\n");
		return err;
	}

	/* Warn if we've couldn't transmit all the URBs */
	if (i < MAX_RX_URBS) {
		dev_warn(dev->udev->dev.parent,
			 "rx performance may be slow\n");
	}

	dev->rxinitdone = 1;
	return 0;
}

/*
 * Start interface
 */
static int esd_usb2_start(struct esd_usb2_net_priv *priv)
{
	struct esd_usb2 *dev = priv->usb2;
	struct net_device *netdev = priv->netdev;
	struct esd_usb2_msg *msg;
	int err, i;

	msg = kmalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg) {
		err = -ENOMEM;
		goto out;
	}

	/*
	 * Enable all IDs
	 * The IDADD message takes up to 64 32 bit bitmasks (2048 bits).
	 * Each bit represents one 11 bit CAN identifier. A set bit
	 * enables reception of the corresponding CAN identifier. A cleared
	 * bit disabled this identifier. An additional bitmask value
	 * following the CAN 2.0A bits is used to enable reception of
	 * extended CAN frames. Only the LSB of this final mask is checked
	 * for the complete 29 bit ID range. The IDADD message also allows
	 * filter configuration for an ID subset. In this case you can add
	 * the number of the starting bitmask (0..64) to the filter.option
	 * field followed by only some bitmasks.
	 */
	msg->msg.hdr.cmd = CMD_IDADD;
	msg->msg.hdr.len = 2 + ESD_MAX_ID_SEGMENT;
	msg->msg.filter.net = priv->index;
	msg->msg.filter.option = ESD_ID_ENABLE; /* start with segment 0 */
	for (i = 0; i < ESD_MAX_ID_SEGMENT; i++)
		msg->msg.filter.mask[i] = cpu_to_le32(0xffffffff);
	/* enable 29bit extended IDs */
	msg->msg.filter.mask[ESD_MAX_ID_SEGMENT] = cpu_to_le32(0x00000001);

	err = esd_usb2_send_msg(dev, msg);
	if (err)
		goto out;

	err = esd_usb2_setup_rx_urbs(dev);
	if (err)
		goto out;

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

out:
	if (err == -ENODEV)
		netif_device_detach(netdev);
	if (err)
		netdev_err(netdev, "couldn't start device: %d\n", err);

	kfree(msg);
	return err;
}

static void unlink_all_urbs(struct esd_usb2 *dev)
{
	struct esd_usb2_net_priv *priv;
	int i, j;

	usb_kill_anchored_urbs(&dev->rx_submitted);

	for (i = 0; i < MAX_RX_URBS; ++i)
		usb_free_coherent(dev->udev, RX_BUFFER_SIZE,
				  dev->rxbuf[i], dev->rxbuf_dma[i]);

	for (i = 0; i < dev->net_count; i++) {
		priv = dev->nets[i];
		if (priv) {
			usb_kill_anchored_urbs(&priv->tx_submitted);
			atomic_set(&priv->active_tx_jobs, 0);

			for (j = 0; j < MAX_TX_URBS; j++)
				priv->tx_contexts[j].echo_index = MAX_TX_URBS;
		}
	}
}

static int esd_usb2_open(struct net_device *netdev)
{
	struct esd_usb2_net_priv *priv = netdev_priv(netdev);
	int err;

	/* common open */
	err = open_candev(netdev);
	if (err)
		return err;

	/* finally start device */
	err = esd_usb2_start(priv);
	if (err) {
		netdev_warn(netdev, "couldn't start device: %d\n", err);
		close_candev(netdev);
		return err;
	}

	netif_start_queue(netdev);

	return 0;
}

static netdev_tx_t esd_usb2_start_xmit(struct sk_buff *skb,
				      struct net_device *netdev)
{
	struct esd_usb2_net_priv *priv = netdev_priv(netdev);
	struct esd_usb2 *dev = priv->usb2;
	struct esd_tx_urb_context *context = NULL;
	struct net_device_stats *stats = &netdev->stats;
	struct can_frame *cf = (struct can_frame *)skb->data;
	struct esd_usb2_msg *msg;
	struct urb *urb;
	u8 *buf;
	int i, err;
	int ret = NETDEV_TX_OK;
	size_t size = sizeof(struct esd_usb2_msg);

	if (can_dropped_invalid_skb(netdev, skb))
		return NETDEV_TX_OK;

	/* create a URB, and a buffer for it, and copy the data to the URB */
	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		stats->tx_dropped++;
		dev_kfree_skb(skb);
		goto nourbmem;
	}

	buf = usb_alloc_coherent(dev->udev, size, GFP_ATOMIC,
				 &urb->transfer_dma);
	if (!buf) {
		netdev_err(netdev, "No memory left for USB buffer\n");
		stats->tx_dropped++;
		dev_kfree_skb(skb);
		goto nobufmem;
	}

	msg = (struct esd_usb2_msg *)buf;

	msg->msg.hdr.len = 3; /* minimal length */
	msg->msg.hdr.cmd = CMD_CAN_TX;
	msg->msg.tx.net = priv->index;
	msg->msg.tx.dlc = cf->can_dlc;
	msg->msg.tx.id = cpu_to_le32(cf->can_id & CAN_ERR_MASK);

	if (cf->can_id & CAN_RTR_FLAG)
		msg->msg.tx.dlc |= ESD_RTR;

	if (cf->can_id & CAN_EFF_FLAG)
		msg->msg.tx.id |= cpu_to_le32(ESD_EXTID);

	for (i = 0; i < cf->can_dlc; i++)
		msg->msg.tx.data[i] = cf->data[i];

	msg->msg.hdr.len += (cf->can_dlc + 3) >> 2;

	for (i = 0; i < MAX_TX_URBS; i++) {
		if (priv->tx_contexts[i].echo_index == MAX_TX_URBS) {
			context = &priv->tx_contexts[i];
			break;
		}
	}

	/*
	 * This may never happen.
	 */
	if (!context) {
		netdev_warn(netdev, "couldn't find free context\n");
		ret = NETDEV_TX_BUSY;
		goto releasebuf;
	}

	context->priv = priv;
	context->echo_index = i;
	context->dlc = cf->can_dlc;

	/* hnd must not be 0 - MSB is stripped in txdone handling */
	msg->msg.tx.hnd = 0x80000000 | i; /* returned in TX done message */

	usb_fill_bulk_urb(urb, dev->udev, usb_sndbulkpipe(dev->udev, 2), buf,
			  msg->msg.hdr.len << 2,
			  esd_usb2_write_bulk_callback, context);

	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	usb_anchor_urb(urb, &priv->tx_submitted);

	can_put_echo_skb(skb, netdev, context->echo_index);

	atomic_inc(&priv->active_tx_jobs);

	/* Slow down tx path */
	if (atomic_read(&priv->active_tx_jobs) >= MAX_TX_URBS)
		netif_stop_queue(netdev);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		can_free_echo_skb(netdev, context->echo_index);

		atomic_dec(&priv->active_tx_jobs);
		usb_unanchor_urb(urb);

		stats->tx_dropped++;

		if (err == -ENODEV)
			netif_device_detach(netdev);
		else
			netdev_warn(netdev, "failed tx_urb %d\n", err);

		goto releasebuf;
	}

	netif_trans_update(netdev);

	/*
	 * Release our reference to this URB, the USB core will eventually free
	 * it entirely.
	 */
	usb_free_urb(urb);

	return NETDEV_TX_OK;

releasebuf:
	usb_free_coherent(dev->udev, size, buf, urb->transfer_dma);

nobufmem:
	usb_free_urb(urb);

nourbmem:
	return ret;
}

static int esd_usb2_close(struct net_device *netdev)
{
	struct esd_usb2_net_priv *priv = netdev_priv(netdev);
	struct esd_usb2_msg *msg;
	int i;

	msg = kmalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	/* Disable all IDs (see esd_usb2_start()) */
	msg->msg.hdr.cmd = CMD_IDADD;
	msg->msg.hdr.len = 2 + ESD_MAX_ID_SEGMENT;
	msg->msg.filter.net = priv->index;
	msg->msg.filter.option = ESD_ID_ENABLE; /* start with segment 0 */
	for (i = 0; i <= ESD_MAX_ID_SEGMENT; i++)
		msg->msg.filter.mask[i] = 0;
	if (esd_usb2_send_msg(priv->usb2, msg) < 0)
		netdev_err(netdev, "sending idadd message failed\n");

	/* set CAN controller to reset mode */
	msg->msg.hdr.len = 2;
	msg->msg.hdr.cmd = CMD_SETBAUD;
	msg->msg.setbaud.net = priv->index;
	msg->msg.setbaud.rsvd = 0;
	msg->msg.setbaud.baud = cpu_to_le32(ESD_USB2_NO_BAUDRATE);
	if (esd_usb2_send_msg(priv->usb2, msg) < 0)
		netdev_err(netdev, "sending setbaud message failed\n");

	priv->can.state = CAN_STATE_STOPPED;

	netif_stop_queue(netdev);

	close_candev(netdev);

	kfree(msg);

	return 0;
}

static const struct net_device_ops esd_usb2_netdev_ops = {
	.ndo_open = esd_usb2_open,
	.ndo_stop = esd_usb2_close,
	.ndo_start_xmit = esd_usb2_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static const struct can_bittiming_const esd_usb2_bittiming_const = {
	.name = "esd_usb2",
	.tseg1_min = ESD_USB2_TSEG1_MIN,
	.tseg1_max = ESD_USB2_TSEG1_MAX,
	.tseg2_min = ESD_USB2_TSEG2_MIN,
	.tseg2_max = ESD_USB2_TSEG2_MAX,
	.sjw_max = ESD_USB2_SJW_MAX,
	.brp_min = ESD_USB2_BRP_MIN,
	.brp_max = ESD_USB2_BRP_MAX,
	.brp_inc = ESD_USB2_BRP_INC,
};

static int esd_usb2_set_bittiming(struct net_device *netdev)
{
	struct esd_usb2_net_priv *priv = netdev_priv(netdev);
	struct can_bittiming *bt = &priv->can.bittiming;
	struct esd_usb2_msg *msg;
	int err;
	u32 canbtr;
	int sjw_shift;

	canbtr = ESD_USB2_UBR;
	if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		canbtr |= ESD_USB2_LOM;

	canbtr |= (bt->brp - 1) & (ESD_USB2_BRP_MAX - 1);

	if (le16_to_cpu(priv->usb2->udev->descriptor.idProduct) ==
	    USB_CANUSBM_PRODUCT_ID)
		sjw_shift = ESD_USBM_SJW_SHIFT;
	else
		sjw_shift = ESD_USB2_SJW_SHIFT;

	canbtr |= ((bt->sjw - 1) & (ESD_USB2_SJW_MAX - 1))
		<< sjw_shift;
	canbtr |= ((bt->prop_seg + bt->phase_seg1 - 1)
		   & (ESD_USB2_TSEG1_MAX - 1))
		<< ESD_USB2_TSEG1_SHIFT;
	canbtr |= ((bt->phase_seg2 - 1) & (ESD_USB2_TSEG2_MAX - 1))
		<< ESD_USB2_TSEG2_SHIFT;
	if (priv->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES)
		canbtr |= ESD_USB2_3_SAMPLES;

	msg = kmalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->msg.hdr.len = 2;
	msg->msg.hdr.cmd = CMD_SETBAUD;
	msg->msg.setbaud.net = priv->index;
	msg->msg.setbaud.rsvd = 0;
	msg->msg.setbaud.baud = cpu_to_le32(canbtr);

	netdev_info(netdev, "setting BTR=%#x\n", canbtr);

	err = esd_usb2_send_msg(priv->usb2, msg);

	kfree(msg);
	return err;
}

static int esd_usb2_get_berr_counter(const struct net_device *netdev,
				     struct can_berr_counter *bec)
{
	struct esd_usb2_net_priv *priv = netdev_priv(netdev);

	bec->txerr = priv->bec.txerr;
	bec->rxerr = priv->bec.rxerr;

	return 0;
}

static int esd_usb2_set_mode(struct net_device *netdev, enum can_mode mode)
{
	switch (mode) {
	case CAN_MODE_START:
		netif_wake_queue(netdev);
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int esd_usb2_probe_one_net(struct usb_interface *intf, int index)
{
	struct esd_usb2 *dev = usb_get_intfdata(intf);
	struct net_device *netdev;
	struct esd_usb2_net_priv *priv;
	int err = 0;
	int i;

	netdev = alloc_candev(sizeof(*priv), MAX_TX_URBS);
	if (!netdev) {
		dev_err(&intf->dev, "couldn't alloc candev\n");
		err = -ENOMEM;
		goto done;
	}

	priv = netdev_priv(netdev);

	init_usb_anchor(&priv->tx_submitted);
	atomic_set(&priv->active_tx_jobs, 0);

	for (i = 0; i < MAX_TX_URBS; i++)
		priv->tx_contexts[i].echo_index = MAX_TX_URBS;

	priv->usb2 = dev;
	priv->netdev = netdev;
	priv->index = index;

	priv->can.state = CAN_STATE_STOPPED;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_LISTENONLY;

	if (le16_to_cpu(dev->udev->descriptor.idProduct) ==
	    USB_CANUSBM_PRODUCT_ID)
		priv->can.clock.freq = ESD_USBM_CAN_CLOCK;
	else {
		priv->can.clock.freq = ESD_USB2_CAN_CLOCK;
		priv->can.ctrlmode_supported |= CAN_CTRLMODE_3_SAMPLES;
	}

	priv->can.bittiming_const = &esd_usb2_bittiming_const;
	priv->can.do_set_bittiming = esd_usb2_set_bittiming;
	priv->can.do_set_mode = esd_usb2_set_mode;
	priv->can.do_get_berr_counter = esd_usb2_get_berr_counter;

	netdev->flags |= IFF_ECHO; /* we support local echo */

	netdev->netdev_ops = &esd_usb2_netdev_ops;

	SET_NETDEV_DEV(netdev, &intf->dev);
	netdev->dev_id = index;

	err = register_candev(netdev);
	if (err) {
		dev_err(&intf->dev, "couldn't register CAN device: %d\n", err);
		free_candev(netdev);
		err = -ENOMEM;
		goto done;
	}

	dev->nets[index] = priv;
	netdev_info(netdev, "device %s registered\n", netdev->name);

done:
	return err;
}

/*
 * probe function for new USB2 devices
 *
 * check version information and number of available
 * CAN interfaces
 */
static int esd_usb2_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct esd_usb2 *dev;
	struct esd_usb2_msg *msg;
	int i, err;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		err = -ENOMEM;
		goto done;
	}

	dev->udev = interface_to_usbdev(intf);

	init_usb_anchor(&dev->rx_submitted);

	usb_set_intfdata(intf, dev);

	msg = kmalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg) {
		err = -ENOMEM;
		goto free_msg;
	}

	/* query number of CAN interfaces (nets) */
	msg->msg.hdr.cmd = CMD_VERSION;
	msg->msg.hdr.len = 2;
	msg->msg.version.rsvd = 0;
	msg->msg.version.flags = 0;
	msg->msg.version.drv_version = 0;

	err = esd_usb2_send_msg(dev, msg);
	if (err < 0) {
		dev_err(&intf->dev, "sending version message failed\n");
		goto free_msg;
	}

	err = esd_usb2_wait_msg(dev, msg);
	if (err < 0) {
		dev_err(&intf->dev, "no version message answer\n");
		goto free_msg;
	}

	dev->net_count = (int)msg->msg.version_reply.nets;
	dev->version = le32_to_cpu(msg->msg.version_reply.version);

	if (device_create_file(&intf->dev, &dev_attr_firmware))
		dev_err(&intf->dev,
			"Couldn't create device file for firmware\n");

	if (device_create_file(&intf->dev, &dev_attr_hardware))
		dev_err(&intf->dev,
			"Couldn't create device file for hardware\n");

	if (device_create_file(&intf->dev, &dev_attr_nets))
		dev_err(&intf->dev,
			"Couldn't create device file for nets\n");

	/* do per device probing */
	for (i = 0; i < dev->net_count; i++)
		esd_usb2_probe_one_net(intf, i);

free_msg:
	kfree(msg);
	if (err)
		kfree(dev);
done:
	return err;
}

/*
 * called by the usb core when the device is removed from the system
 */
static void esd_usb2_disconnect(struct usb_interface *intf)
{
	struct esd_usb2 *dev = usb_get_intfdata(intf);
	struct net_device *netdev;
	int i;

	device_remove_file(&intf->dev, &dev_attr_firmware);
	device_remove_file(&intf->dev, &dev_attr_hardware);
	device_remove_file(&intf->dev, &dev_attr_nets);

	usb_set_intfdata(intf, NULL);

	if (dev) {
		for (i = 0; i < dev->net_count; i++) {
			if (dev->nets[i]) {
				netdev = dev->nets[i]->netdev;
				unregister_netdev(netdev);
				free_candev(netdev);
			}
		}
		unlink_all_urbs(dev);
		kfree(dev);
	}
}

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver esd_usb2_driver = {
	.name = "esd_usb2",
	.probe = esd_usb2_probe,
	.disconnect = esd_usb2_disconnect,
	.id_table = esd_usb2_table,
};

module_usb_driver(esd_usb2_driver);
