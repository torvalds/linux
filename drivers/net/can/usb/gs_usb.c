// SPDX-License-Identifier: GPL-2.0-only
/* CAN driver for Geschwister Schneider USB/CAN devices
 * and bytewerk.org candleLight USB CAN interfaces.
 *
 * Copyright (C) 2013-2016 Geschwister Schneider Technologie-,
 * Entwicklungs- und Vertriebs UG (Haftungsbeschränkt).
 * Copyright (C) 2016 Hubert Denkmair
 *
 * Many thanks to all socketcan devs!
 */

#include <linux/bitfield.h>
#include <linux/ethtool.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/signal.h>
#include <linux/usb.h>

#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>

/* Device specific constants */
#define USB_GSUSB_1_VENDOR_ID 0x1d50
#define USB_GSUSB_1_PRODUCT_ID 0x606f

#define USB_CANDLELIGHT_VENDOR_ID 0x1209
#define USB_CANDLELIGHT_PRODUCT_ID 0x2323

#define USB_CES_CANEXT_FD_VENDOR_ID 0x1cd2
#define USB_CES_CANEXT_FD_PRODUCT_ID 0x606f

#define USB_ABE_CANDEBUGGER_FD_VENDOR_ID 0x16d0
#define USB_ABE_CANDEBUGGER_FD_PRODUCT_ID 0x10b8

#define GSUSB_ENDPOINT_IN 1
#define GSUSB_ENDPOINT_OUT 2

/* Device specific constants */
enum gs_usb_breq {
	GS_USB_BREQ_HOST_FORMAT = 0,
	GS_USB_BREQ_BITTIMING,
	GS_USB_BREQ_MODE,
	GS_USB_BREQ_BERR,
	GS_USB_BREQ_BT_CONST,
	GS_USB_BREQ_DEVICE_CONFIG,
	GS_USB_BREQ_TIMESTAMP,
	GS_USB_BREQ_IDENTIFY,
	GS_USB_BREQ_GET_USER_ID,
	GS_USB_BREQ_QUIRK_CANTACT_PRO_DATA_BITTIMING = GS_USB_BREQ_GET_USER_ID,
	GS_USB_BREQ_SET_USER_ID,
	GS_USB_BREQ_DATA_BITTIMING,
	GS_USB_BREQ_BT_CONST_EXT,
};

enum gs_can_mode {
	/* reset a channel. turns it off */
	GS_CAN_MODE_RESET = 0,
	/* starts a channel */
	GS_CAN_MODE_START
};

enum gs_can_state {
	GS_CAN_STATE_ERROR_ACTIVE = 0,
	GS_CAN_STATE_ERROR_WARNING,
	GS_CAN_STATE_ERROR_PASSIVE,
	GS_CAN_STATE_BUS_OFF,
	GS_CAN_STATE_STOPPED,
	GS_CAN_STATE_SLEEPING
};

enum gs_can_identify_mode {
	GS_CAN_IDENTIFY_OFF = 0,
	GS_CAN_IDENTIFY_ON
};

/* data types passed between host and device */

/* The firmware on the original USB2CAN by Geschwister Schneider
 * Technologie Entwicklungs- und Vertriebs UG exchanges all data
 * between the host and the device in host byte order. This is done
 * with the struct gs_host_config::byte_order member, which is sent
 * first to indicate the desired byte order.
 *
 * The widely used open source firmware candleLight doesn't support
 * this feature and exchanges the data in little endian byte order.
 */
struct gs_host_config {
	__le32 byte_order;
} __packed;

struct gs_device_config {
	u8 reserved1;
	u8 reserved2;
	u8 reserved3;
	u8 icount;
	__le32 sw_version;
	__le32 hw_version;
} __packed;

#define GS_CAN_MODE_NORMAL 0
#define GS_CAN_MODE_LISTEN_ONLY BIT(0)
#define GS_CAN_MODE_LOOP_BACK BIT(1)
#define GS_CAN_MODE_TRIPLE_SAMPLE BIT(2)
#define GS_CAN_MODE_ONE_SHOT BIT(3)
#define GS_CAN_MODE_HW_TIMESTAMP BIT(4)
/* GS_CAN_FEATURE_IDENTIFY BIT(5) */
/* GS_CAN_FEATURE_USER_ID BIT(6) */
#define GS_CAN_MODE_PAD_PKTS_TO_MAX_PKT_SIZE BIT(7)
#define GS_CAN_MODE_FD BIT(8)
/* GS_CAN_FEATURE_REQ_USB_QUIRK_LPC546XX BIT(9) */
/* GS_CAN_FEATURE_BT_CONST_EXT BIT(10) */

struct gs_device_mode {
	__le32 mode;
	__le32 flags;
} __packed;

struct gs_device_state {
	__le32 state;
	__le32 rxerr;
	__le32 txerr;
} __packed;

struct gs_device_bittiming {
	__le32 prop_seg;
	__le32 phase_seg1;
	__le32 phase_seg2;
	__le32 sjw;
	__le32 brp;
} __packed;

struct gs_identify_mode {
	__le32 mode;
} __packed;

#define GS_CAN_FEATURE_LISTEN_ONLY BIT(0)
#define GS_CAN_FEATURE_LOOP_BACK BIT(1)
#define GS_CAN_FEATURE_TRIPLE_SAMPLE BIT(2)
#define GS_CAN_FEATURE_ONE_SHOT BIT(3)
#define GS_CAN_FEATURE_HW_TIMESTAMP BIT(4)
#define GS_CAN_FEATURE_IDENTIFY BIT(5)
#define GS_CAN_FEATURE_USER_ID BIT(6)
#define GS_CAN_FEATURE_PAD_PKTS_TO_MAX_PKT_SIZE BIT(7)
#define GS_CAN_FEATURE_FD BIT(8)
#define GS_CAN_FEATURE_REQ_USB_QUIRK_LPC546XX BIT(9)
#define GS_CAN_FEATURE_BT_CONST_EXT BIT(10)
#define GS_CAN_FEATURE_MASK GENMASK(10, 0)

/* internal quirks - keep in GS_CAN_FEATURE space for now */

/* CANtact Pro original firmware:
 * BREQ DATA_BITTIMING overlaps with GET_USER_ID
 */
#define GS_CAN_FEATURE_QUIRK_BREQ_CANTACT_PRO BIT(31)

struct gs_device_bt_const {
	__le32 feature;
	__le32 fclk_can;
	__le32 tseg1_min;
	__le32 tseg1_max;
	__le32 tseg2_min;
	__le32 tseg2_max;
	__le32 sjw_max;
	__le32 brp_min;
	__le32 brp_max;
	__le32 brp_inc;
} __packed;

struct gs_device_bt_const_extended {
	__le32 feature;
	__le32 fclk_can;
	__le32 tseg1_min;
	__le32 tseg1_max;
	__le32 tseg2_min;
	__le32 tseg2_max;
	__le32 sjw_max;
	__le32 brp_min;
	__le32 brp_max;
	__le32 brp_inc;

	__le32 dtseg1_min;
	__le32 dtseg1_max;
	__le32 dtseg2_min;
	__le32 dtseg2_max;
	__le32 dsjw_max;
	__le32 dbrp_min;
	__le32 dbrp_max;
	__le32 dbrp_inc;
} __packed;

#define GS_CAN_FLAG_OVERFLOW BIT(0)
#define GS_CAN_FLAG_FD BIT(1)
#define GS_CAN_FLAG_BRS BIT(2)
#define GS_CAN_FLAG_ESI BIT(3)

struct classic_can {
	u8 data[8];
} __packed;

struct classic_can_quirk {
	u8 data[8];
	u8 quirk;
} __packed;

struct canfd {
	u8 data[64];
} __packed;

struct canfd_quirk {
	u8 data[64];
	u8 quirk;
} __packed;

struct gs_host_frame {
	u32 echo_id;
	__le32 can_id;

	u8 can_dlc;
	u8 channel;
	u8 flags;
	u8 reserved;

	union {
		DECLARE_FLEX_ARRAY(struct classic_can, classic_can);
		DECLARE_FLEX_ARRAY(struct classic_can_quirk, classic_can_quirk);
		DECLARE_FLEX_ARRAY(struct canfd, canfd);
		DECLARE_FLEX_ARRAY(struct canfd_quirk, canfd_quirk);
	};
} __packed;
/* The GS USB devices make use of the same flags and masks as in
 * linux/can.h and linux/can/error.h, and no additional mapping is necessary.
 */

/* Only send a max of GS_MAX_TX_URBS frames per channel at a time. */
#define GS_MAX_TX_URBS 10
/* Only launch a max of GS_MAX_RX_URBS usb requests at a time. */
#define GS_MAX_RX_URBS 30
/* Maximum number of interfaces the driver supports per device.
 * Current hardware only supports 3 interfaces. The future may vary.
 */
#define GS_MAX_INTF 3

struct gs_tx_context {
	struct gs_can *dev;
	unsigned int echo_id;
};

struct gs_can {
	struct can_priv can; /* must be the first member */

	struct gs_usb *parent;

	struct net_device *netdev;
	struct usb_device *udev;
	struct usb_interface *iface;

	struct can_bittiming_const bt_const, data_bt_const;
	unsigned int channel;	/* channel number */

	u32 feature;
	unsigned int hf_size_tx;

	/* This lock prevents a race condition between xmit and receive. */
	spinlock_t tx_ctx_lock;
	struct gs_tx_context tx_context[GS_MAX_TX_URBS];

	struct usb_anchor tx_submitted;
	atomic_t active_tx_urbs;
};

/* usb interface struct */
struct gs_usb {
	struct gs_can *canch[GS_MAX_INTF];
	struct usb_anchor rx_submitted;
	struct usb_device *udev;
	unsigned int hf_size_rx;
	u8 active_channels;
};

/* 'allocate' a tx context.
 * returns a valid tx context or NULL if there is no space.
 */
static struct gs_tx_context *gs_alloc_tx_context(struct gs_can *dev)
{
	int i = 0;
	unsigned long flags;

	spin_lock_irqsave(&dev->tx_ctx_lock, flags);

	for (; i < GS_MAX_TX_URBS; i++) {
		if (dev->tx_context[i].echo_id == GS_MAX_TX_URBS) {
			dev->tx_context[i].echo_id = i;
			spin_unlock_irqrestore(&dev->tx_ctx_lock, flags);
			return &dev->tx_context[i];
		}
	}

	spin_unlock_irqrestore(&dev->tx_ctx_lock, flags);
	return NULL;
}

/* releases a tx context
 */
static void gs_free_tx_context(struct gs_tx_context *txc)
{
	txc->echo_id = GS_MAX_TX_URBS;
}

/* Get a tx context by id.
 */
static struct gs_tx_context *gs_get_tx_context(struct gs_can *dev,
					       unsigned int id)
{
	unsigned long flags;

	if (id < GS_MAX_TX_URBS) {
		spin_lock_irqsave(&dev->tx_ctx_lock, flags);
		if (dev->tx_context[id].echo_id == id) {
			spin_unlock_irqrestore(&dev->tx_ctx_lock, flags);
			return &dev->tx_context[id];
		}
		spin_unlock_irqrestore(&dev->tx_ctx_lock, flags);
	}
	return NULL;
}

static int gs_cmd_reset(struct gs_can *gsdev)
{
	struct gs_device_mode *dm;
	struct usb_interface *intf = gsdev->iface;
	int rc;

	dm = kzalloc(sizeof(*dm), GFP_KERNEL);
	if (!dm)
		return -ENOMEM;

	dm->mode = GS_CAN_MODE_RESET;

	rc = usb_control_msg(interface_to_usbdev(intf),
			     usb_sndctrlpipe(interface_to_usbdev(intf), 0),
			     GS_USB_BREQ_MODE,
			     USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			     gsdev->channel, 0, dm, sizeof(*dm), 1000);

	kfree(dm);

	return rc;
}

static void gs_update_state(struct gs_can *dev, struct can_frame *cf)
{
	struct can_device_stats *can_stats = &dev->can.can_stats;

	if (cf->can_id & CAN_ERR_RESTARTED) {
		dev->can.state = CAN_STATE_ERROR_ACTIVE;
		can_stats->restarts++;
	} else if (cf->can_id & CAN_ERR_BUSOFF) {
		dev->can.state = CAN_STATE_BUS_OFF;
		can_stats->bus_off++;
	} else if (cf->can_id & CAN_ERR_CRTL) {
		if ((cf->data[1] & CAN_ERR_CRTL_TX_WARNING) ||
		    (cf->data[1] & CAN_ERR_CRTL_RX_WARNING)) {
			dev->can.state = CAN_STATE_ERROR_WARNING;
			can_stats->error_warning++;
		} else if ((cf->data[1] & CAN_ERR_CRTL_TX_PASSIVE) ||
			   (cf->data[1] & CAN_ERR_CRTL_RX_PASSIVE)) {
			dev->can.state = CAN_STATE_ERROR_PASSIVE;
			can_stats->error_passive++;
		} else {
			dev->can.state = CAN_STATE_ERROR_ACTIVE;
		}
	}
}

static void gs_usb_receive_bulk_callback(struct urb *urb)
{
	struct gs_usb *usbcan = urb->context;
	struct gs_can *dev;
	struct net_device *netdev;
	int rc;
	struct net_device_stats *stats;
	struct gs_host_frame *hf = urb->transfer_buffer;
	struct gs_tx_context *txc;
	struct can_frame *cf;
	struct canfd_frame *cfd;
	struct sk_buff *skb;

	BUG_ON(!usbcan);

	switch (urb->status) {
	case 0: /* success */
		break;
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	default:
		/* do not resubmit aborted urbs. eg: when device goes down */
		return;
	}

	/* device reports out of range channel id */
	if (hf->channel >= GS_MAX_INTF)
		goto device_detach;

	dev = usbcan->canch[hf->channel];

	netdev = dev->netdev;
	stats = &netdev->stats;

	if (!netif_device_present(netdev))
		return;

	if (hf->echo_id == -1) { /* normal rx */
		if (hf->flags & GS_CAN_FLAG_FD) {
			skb = alloc_canfd_skb(dev->netdev, &cfd);
			if (!skb)
				return;

			cfd->can_id = le32_to_cpu(hf->can_id);
			cfd->len = can_fd_dlc2len(hf->can_dlc);
			if (hf->flags & GS_CAN_FLAG_BRS)
				cfd->flags |= CANFD_BRS;
			if (hf->flags & GS_CAN_FLAG_ESI)
				cfd->flags |= CANFD_ESI;

			memcpy(cfd->data, hf->canfd->data, cfd->len);
		} else {
			skb = alloc_can_skb(dev->netdev, &cf);
			if (!skb)
				return;

			cf->can_id = le32_to_cpu(hf->can_id);
			can_frame_set_cc_len(cf, hf->can_dlc, dev->can.ctrlmode);

			memcpy(cf->data, hf->classic_can->data, 8);

			/* ERROR frames tell us information about the controller */
			if (le32_to_cpu(hf->can_id) & CAN_ERR_FLAG)
				gs_update_state(dev, cf);
		}

		netdev->stats.rx_packets++;
		netdev->stats.rx_bytes += hf->can_dlc;

		netif_rx(skb);
	} else { /* echo_id == hf->echo_id */
		if (hf->echo_id >= GS_MAX_TX_URBS) {
			netdev_err(netdev,
				   "Unexpected out of range echo id %u\n",
				   hf->echo_id);
			goto resubmit_urb;
		}

		txc = gs_get_tx_context(dev, hf->echo_id);

		/* bad devices send bad echo_ids. */
		if (!txc) {
			netdev_err(netdev,
				   "Unexpected unused echo id %u\n",
				   hf->echo_id);
			goto resubmit_urb;
		}

		netdev->stats.tx_packets++;
		netdev->stats.tx_bytes += can_get_echo_skb(netdev, hf->echo_id,
							   NULL);

		gs_free_tx_context(txc);

		atomic_dec(&dev->active_tx_urbs);

		netif_wake_queue(netdev);
	}

	if (hf->flags & GS_CAN_FLAG_OVERFLOW) {
		skb = alloc_can_err_skb(netdev, &cf);
		if (!skb)
			goto resubmit_urb;

		cf->can_id |= CAN_ERR_CRTL;
		cf->len = CAN_ERR_DLC;
		cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
		stats->rx_over_errors++;
		stats->rx_errors++;
		netif_rx(skb);
	}

 resubmit_urb:
	usb_fill_bulk_urb(urb, usbcan->udev,
			  usb_rcvbulkpipe(usbcan->udev, GSUSB_ENDPOINT_IN),
			  hf, dev->parent->hf_size_rx,
			  gs_usb_receive_bulk_callback, usbcan);

	rc = usb_submit_urb(urb, GFP_ATOMIC);

	/* USB failure take down all interfaces */
	if (rc == -ENODEV) {
 device_detach:
		for (rc = 0; rc < GS_MAX_INTF; rc++) {
			if (usbcan->canch[rc])
				netif_device_detach(usbcan->canch[rc]->netdev);
		}
	}
}

static int gs_usb_set_bittiming(struct net_device *netdev)
{
	struct gs_can *dev = netdev_priv(netdev);
	struct can_bittiming *bt = &dev->can.bittiming;
	struct usb_interface *intf = dev->iface;
	int rc;
	struct gs_device_bittiming *dbt;

	dbt = kmalloc(sizeof(*dbt), GFP_KERNEL);
	if (!dbt)
		return -ENOMEM;

	dbt->prop_seg = cpu_to_le32(bt->prop_seg);
	dbt->phase_seg1 = cpu_to_le32(bt->phase_seg1);
	dbt->phase_seg2 = cpu_to_le32(bt->phase_seg2);
	dbt->sjw = cpu_to_le32(bt->sjw);
	dbt->brp = cpu_to_le32(bt->brp);

	/* request bit timings */
	rc = usb_control_msg(interface_to_usbdev(intf),
			     usb_sndctrlpipe(interface_to_usbdev(intf), 0),
			     GS_USB_BREQ_BITTIMING,
			     USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			     dev->channel, 0, dbt, sizeof(*dbt), 1000);

	kfree(dbt);

	if (rc < 0)
		dev_err(netdev->dev.parent, "Couldn't set bittimings (err=%d)",
			rc);

	return (rc > 0) ? 0 : rc;
}

static int gs_usb_set_data_bittiming(struct net_device *netdev)
{
	struct gs_can *dev = netdev_priv(netdev);
	struct can_bittiming *bt = &dev->can.data_bittiming;
	struct usb_interface *intf = dev->iface;
	struct gs_device_bittiming *dbt;
	u8 request = GS_USB_BREQ_DATA_BITTIMING;
	int rc;

	dbt = kmalloc(sizeof(*dbt), GFP_KERNEL);
	if (!dbt)
		return -ENOMEM;

	dbt->prop_seg = cpu_to_le32(bt->prop_seg);
	dbt->phase_seg1 = cpu_to_le32(bt->phase_seg1);
	dbt->phase_seg2 = cpu_to_le32(bt->phase_seg2);
	dbt->sjw = cpu_to_le32(bt->sjw);
	dbt->brp = cpu_to_le32(bt->brp);

	if (dev->feature & GS_CAN_FEATURE_QUIRK_BREQ_CANTACT_PRO)
		request = GS_USB_BREQ_QUIRK_CANTACT_PRO_DATA_BITTIMING;

	/* request bit timings */
	rc = usb_control_msg(interface_to_usbdev(intf),
			     usb_sndctrlpipe(interface_to_usbdev(intf), 0),
			     request,
			     USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			     dev->channel, 0, dbt, sizeof(*dbt), 1000);

	kfree(dbt);

	if (rc < 0)
		dev_err(netdev->dev.parent,
			"Couldn't set data bittimings (err=%d)", rc);

	return (rc > 0) ? 0 : rc;
}

static void gs_usb_xmit_callback(struct urb *urb)
{
	struct gs_tx_context *txc = urb->context;
	struct gs_can *dev = txc->dev;
	struct net_device *netdev = dev->netdev;

	if (urb->status)
		netdev_info(netdev, "usb xmit fail %u\n", txc->echo_id);

	usb_free_coherent(urb->dev, urb->transfer_buffer_length,
			  urb->transfer_buffer, urb->transfer_dma);
}

static netdev_tx_t gs_can_start_xmit(struct sk_buff *skb,
				     struct net_device *netdev)
{
	struct gs_can *dev = netdev_priv(netdev);
	struct net_device_stats *stats = &dev->netdev->stats;
	struct urb *urb;
	struct gs_host_frame *hf;
	struct can_frame *cf;
	struct canfd_frame *cfd;
	int rc;
	unsigned int idx;
	struct gs_tx_context *txc;

	if (can_dropped_invalid_skb(netdev, skb))
		return NETDEV_TX_OK;

	/* find an empty context to keep track of transmission */
	txc = gs_alloc_tx_context(dev);
	if (!txc)
		return NETDEV_TX_BUSY;

	/* create a URB, and a buffer for it */
	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb)
		goto nomem_urb;

	hf = usb_alloc_coherent(dev->udev, dev->hf_size_tx, GFP_ATOMIC,
				&urb->transfer_dma);
	if (!hf) {
		netdev_err(netdev, "No memory left for USB buffer\n");
		goto nomem_hf;
	}

	idx = txc->echo_id;

	if (idx >= GS_MAX_TX_URBS) {
		netdev_err(netdev, "Invalid tx context %u\n", idx);
		goto badidx;
	}

	hf->echo_id = idx;
	hf->channel = dev->channel;
	hf->flags = 0;
	hf->reserved = 0;

	if (can_is_canfd_skb(skb)) {
		cfd = (struct canfd_frame *)skb->data;

		hf->can_id = cpu_to_le32(cfd->can_id);
		hf->can_dlc = can_fd_len2dlc(cfd->len);
		hf->flags |= GS_CAN_FLAG_FD;
		if (cfd->flags & CANFD_BRS)
			hf->flags |= GS_CAN_FLAG_BRS;
		if (cfd->flags & CANFD_ESI)
			hf->flags |= GS_CAN_FLAG_ESI;

		memcpy(hf->canfd->data, cfd->data, cfd->len);
	} else {
		cf = (struct can_frame *)skb->data;

		hf->can_id = cpu_to_le32(cf->can_id);
		hf->can_dlc = can_get_cc_dlc(cf, dev->can.ctrlmode);

		memcpy(hf->classic_can->data, cf->data, cf->len);
	}

	usb_fill_bulk_urb(urb, dev->udev,
			  usb_sndbulkpipe(dev->udev, GSUSB_ENDPOINT_OUT),
			  hf, dev->hf_size_tx,
			  gs_usb_xmit_callback, txc);

	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	usb_anchor_urb(urb, &dev->tx_submitted);

	can_put_echo_skb(skb, netdev, idx, 0);

	atomic_inc(&dev->active_tx_urbs);

	rc = usb_submit_urb(urb, GFP_ATOMIC);
	if (unlikely(rc)) {			/* usb send failed */
		atomic_dec(&dev->active_tx_urbs);

		can_free_echo_skb(netdev, idx, NULL);
		gs_free_tx_context(txc);

		usb_unanchor_urb(urb);
		usb_free_coherent(dev->udev, urb->transfer_buffer_length,
				  urb->transfer_buffer, urb->transfer_dma);

		if (rc == -ENODEV) {
			netif_device_detach(netdev);
		} else {
			netdev_err(netdev, "usb_submit failed (err=%d)\n", rc);
			stats->tx_dropped++;
		}
	} else {
		/* Slow down tx path */
		if (atomic_read(&dev->active_tx_urbs) >= GS_MAX_TX_URBS)
			netif_stop_queue(netdev);
	}

	/* let usb core take care of this urb */
	usb_free_urb(urb);

	return NETDEV_TX_OK;

 badidx:
	usb_free_coherent(dev->udev, urb->transfer_buffer_length,
			  urb->transfer_buffer, urb->transfer_dma);
 nomem_hf:
	usb_free_urb(urb);

 nomem_urb:
	gs_free_tx_context(txc);
	dev_kfree_skb(skb);
	stats->tx_dropped++;
	return NETDEV_TX_OK;
}

static int gs_can_open(struct net_device *netdev)
{
	struct gs_can *dev = netdev_priv(netdev);
	struct gs_usb *parent = dev->parent;
	int rc, i;
	struct gs_device_mode *dm;
	struct gs_host_frame *hf;
	u32 ctrlmode;
	u32 flags = 0;

	rc = open_candev(netdev);
	if (rc)
		return rc;

	ctrlmode = dev->can.ctrlmode;
	if (ctrlmode & CAN_CTRLMODE_FD) {
		flags |= GS_CAN_MODE_FD;

		if (dev->feature & GS_CAN_FEATURE_REQ_USB_QUIRK_LPC546XX)
			dev->hf_size_tx = struct_size(hf, canfd_quirk, 1);
		else
			dev->hf_size_tx = struct_size(hf, canfd, 1);
	} else {
		if (dev->feature & GS_CAN_FEATURE_REQ_USB_QUIRK_LPC546XX)
			dev->hf_size_tx = struct_size(hf, classic_can_quirk, 1);
		else
			dev->hf_size_tx = struct_size(hf, classic_can, 1);
	}

	if (!parent->active_channels) {
		for (i = 0; i < GS_MAX_RX_URBS; i++) {
			struct urb *urb;
			u8 *buf;

			/* alloc rx urb */
			urb = usb_alloc_urb(0, GFP_KERNEL);
			if (!urb)
				return -ENOMEM;

			/* alloc rx buffer */
			buf = usb_alloc_coherent(dev->udev,
						 dev->parent->hf_size_rx,
						 GFP_KERNEL,
						 &urb->transfer_dma);
			if (!buf) {
				netdev_err(netdev,
					   "No memory left for USB buffer\n");
				usb_free_urb(urb);
				return -ENOMEM;
			}

			/* fill, anchor, and submit rx urb */
			usb_fill_bulk_urb(urb,
					  dev->udev,
					  usb_rcvbulkpipe(dev->udev,
							  GSUSB_ENDPOINT_IN),
					  buf,
					  dev->parent->hf_size_rx,
					  gs_usb_receive_bulk_callback, parent);
			urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

			usb_anchor_urb(urb, &parent->rx_submitted);

			rc = usb_submit_urb(urb, GFP_KERNEL);
			if (rc) {
				if (rc == -ENODEV)
					netif_device_detach(dev->netdev);

				netdev_err(netdev,
					   "usb_submit failed (err=%d)\n", rc);

				usb_unanchor_urb(urb);
				usb_free_urb(urb);
				break;
			}

			/* Drop reference,
			 * USB core will take care of freeing it
			 */
			usb_free_urb(urb);
		}
	}

	dm = kmalloc(sizeof(*dm), GFP_KERNEL);
	if (!dm)
		return -ENOMEM;

	/* flags */
	if (ctrlmode & CAN_CTRLMODE_LOOPBACK)
		flags |= GS_CAN_MODE_LOOP_BACK;
	else if (ctrlmode & CAN_CTRLMODE_LISTENONLY)
		flags |= GS_CAN_MODE_LISTEN_ONLY;

	/* Controller is not allowed to retry TX
	 * this mode is unavailable on atmels uc3c hardware
	 */
	if (ctrlmode & CAN_CTRLMODE_ONE_SHOT)
		flags |= GS_CAN_MODE_ONE_SHOT;

	if (ctrlmode & CAN_CTRLMODE_3_SAMPLES)
		flags |= GS_CAN_MODE_TRIPLE_SAMPLE;

	/* finally start device */
	dm->mode = cpu_to_le32(GS_CAN_MODE_START);
	dm->flags = cpu_to_le32(flags);
	rc = usb_control_msg(interface_to_usbdev(dev->iface),
			     usb_sndctrlpipe(interface_to_usbdev(dev->iface), 0),
			     GS_USB_BREQ_MODE,
			     USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			     dev->channel, 0, dm, sizeof(*dm), 1000);

	if (rc < 0) {
		netdev_err(netdev, "Couldn't start device (err=%d)\n", rc);
		kfree(dm);
		return rc;
	}

	kfree(dm);

	dev->can.state = CAN_STATE_ERROR_ACTIVE;

	parent->active_channels++;
	if (!(dev->can.ctrlmode & CAN_CTRLMODE_LISTENONLY))
		netif_start_queue(netdev);

	return 0;
}

static int gs_can_close(struct net_device *netdev)
{
	int rc;
	struct gs_can *dev = netdev_priv(netdev);
	struct gs_usb *parent = dev->parent;

	netif_stop_queue(netdev);

	/* Stop polling */
	parent->active_channels--;
	if (!parent->active_channels)
		usb_kill_anchored_urbs(&parent->rx_submitted);

	/* Stop sending URBs */
	usb_kill_anchored_urbs(&dev->tx_submitted);
	atomic_set(&dev->active_tx_urbs, 0);

	/* reset the device */
	rc = gs_cmd_reset(dev);
	if (rc < 0)
		netdev_warn(netdev, "Couldn't shutdown device (err=%d)", rc);

	/* reset tx contexts */
	for (rc = 0; rc < GS_MAX_TX_URBS; rc++) {
		dev->tx_context[rc].dev = dev;
		dev->tx_context[rc].echo_id = GS_MAX_TX_URBS;
	}

	/* close the netdev */
	close_candev(netdev);

	return 0;
}

static const struct net_device_ops gs_usb_netdev_ops = {
	.ndo_open = gs_can_open,
	.ndo_stop = gs_can_close,
	.ndo_start_xmit = gs_can_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static int gs_usb_set_identify(struct net_device *netdev, bool do_identify)
{
	struct gs_can *dev = netdev_priv(netdev);
	struct gs_identify_mode *imode;
	int rc;

	imode = kmalloc(sizeof(*imode), GFP_KERNEL);

	if (!imode)
		return -ENOMEM;

	if (do_identify)
		imode->mode = cpu_to_le32(GS_CAN_IDENTIFY_ON);
	else
		imode->mode = cpu_to_le32(GS_CAN_IDENTIFY_OFF);

	rc = usb_control_msg(interface_to_usbdev(dev->iface),
			     usb_sndctrlpipe(interface_to_usbdev(dev->iface), 0),
			     GS_USB_BREQ_IDENTIFY,
			     USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			     dev->channel, 0, imode, sizeof(*imode), 100);

	kfree(imode);

	return (rc > 0) ? 0 : rc;
}

/* blink LED's for finding the this interface */
static int gs_usb_set_phys_id(struct net_device *dev,
			      enum ethtool_phys_id_state state)
{
	int rc = 0;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		rc = gs_usb_set_identify(dev, GS_CAN_IDENTIFY_ON);
		break;
	case ETHTOOL_ID_INACTIVE:
		rc = gs_usb_set_identify(dev, GS_CAN_IDENTIFY_OFF);
		break;
	default:
		break;
	}

	return rc;
}

static const struct ethtool_ops gs_usb_ethtool_ops = {
	.set_phys_id = gs_usb_set_phys_id,
};

static struct gs_can *gs_make_candev(unsigned int channel,
				     struct usb_interface *intf,
				     struct gs_device_config *dconf)
{
	struct gs_can *dev;
	struct net_device *netdev;
	int rc;
	struct gs_device_bt_const *bt_const;
	struct gs_device_bt_const_extended *bt_const_extended;
	u32 feature;

	bt_const = kmalloc(sizeof(*bt_const), GFP_KERNEL);
	if (!bt_const)
		return ERR_PTR(-ENOMEM);

	/* fetch bit timing constants */
	rc = usb_control_msg(interface_to_usbdev(intf),
			     usb_rcvctrlpipe(interface_to_usbdev(intf), 0),
			     GS_USB_BREQ_BT_CONST,
			     USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			     channel, 0, bt_const, sizeof(*bt_const), 1000);

	if (rc < 0) {
		dev_err(&intf->dev,
			"Couldn't get bit timing const for channel (err=%d)\n",
			rc);
		kfree(bt_const);
		return ERR_PTR(rc);
	}

	/* create netdev */
	netdev = alloc_candev(sizeof(struct gs_can), GS_MAX_TX_URBS);
	if (!netdev) {
		dev_err(&intf->dev, "Couldn't allocate candev\n");
		kfree(bt_const);
		return ERR_PTR(-ENOMEM);
	}

	dev = netdev_priv(netdev);

	netdev->netdev_ops = &gs_usb_netdev_ops;

	netdev->flags |= IFF_ECHO; /* we support full roundtrip echo */

	/* dev setup */
	strcpy(dev->bt_const.name, "gs_usb");
	dev->bt_const.tseg1_min = le32_to_cpu(bt_const->tseg1_min);
	dev->bt_const.tseg1_max = le32_to_cpu(bt_const->tseg1_max);
	dev->bt_const.tseg2_min = le32_to_cpu(bt_const->tseg2_min);
	dev->bt_const.tseg2_max = le32_to_cpu(bt_const->tseg2_max);
	dev->bt_const.sjw_max = le32_to_cpu(bt_const->sjw_max);
	dev->bt_const.brp_min = le32_to_cpu(bt_const->brp_min);
	dev->bt_const.brp_max = le32_to_cpu(bt_const->brp_max);
	dev->bt_const.brp_inc = le32_to_cpu(bt_const->brp_inc);

	dev->udev = interface_to_usbdev(intf);
	dev->iface = intf;
	dev->netdev = netdev;
	dev->channel = channel;

	init_usb_anchor(&dev->tx_submitted);
	atomic_set(&dev->active_tx_urbs, 0);
	spin_lock_init(&dev->tx_ctx_lock);
	for (rc = 0; rc < GS_MAX_TX_URBS; rc++) {
		dev->tx_context[rc].dev = dev;
		dev->tx_context[rc].echo_id = GS_MAX_TX_URBS;
	}

	/* can setup */
	dev->can.state = CAN_STATE_STOPPED;
	dev->can.clock.freq = le32_to_cpu(bt_const->fclk_can);
	dev->can.bittiming_const = &dev->bt_const;
	dev->can.do_set_bittiming = gs_usb_set_bittiming;

	dev->can.ctrlmode_supported = CAN_CTRLMODE_CC_LEN8_DLC;

	feature = le32_to_cpu(bt_const->feature);
	dev->feature = FIELD_GET(GS_CAN_FEATURE_MASK, feature);
	if (feature & GS_CAN_FEATURE_LISTEN_ONLY)
		dev->can.ctrlmode_supported |= CAN_CTRLMODE_LISTENONLY;

	if (feature & GS_CAN_FEATURE_LOOP_BACK)
		dev->can.ctrlmode_supported |= CAN_CTRLMODE_LOOPBACK;

	if (feature & GS_CAN_FEATURE_TRIPLE_SAMPLE)
		dev->can.ctrlmode_supported |= CAN_CTRLMODE_3_SAMPLES;

	if (feature & GS_CAN_FEATURE_ONE_SHOT)
		dev->can.ctrlmode_supported |= CAN_CTRLMODE_ONE_SHOT;

	if (feature & GS_CAN_FEATURE_FD) {
		dev->can.ctrlmode_supported |= CAN_CTRLMODE_FD;
		/* The data bit timing will be overwritten, if
		 * GS_CAN_FEATURE_BT_CONST_EXT is set.
		 */
		dev->can.data_bittiming_const = &dev->bt_const;
		dev->can.do_set_data_bittiming = gs_usb_set_data_bittiming;
	}

	/* The CANtact Pro from LinkLayer Labs is based on the
	 * LPC54616 µC, which is affected by the NXP LPC USB transfer
	 * erratum. However, the current firmware (version 2) doesn't
	 * set the GS_CAN_FEATURE_REQ_USB_QUIRK_LPC546XX bit. Set the
	 * feature GS_CAN_FEATURE_REQ_USB_QUIRK_LPC546XX to workaround
	 * this issue.
	 *
	 * For the GS_USB_BREQ_DATA_BITTIMING USB control message the
	 * CANtact Pro firmware uses a request value, which is already
	 * used by the candleLight firmware for a different purpose
	 * (GS_USB_BREQ_GET_USER_ID). Set the feature
	 * GS_CAN_FEATURE_QUIRK_BREQ_CANTACT_PRO to workaround this
	 * issue.
	 */
	if (dev->udev->descriptor.idVendor == cpu_to_le16(USB_GSUSB_1_VENDOR_ID) &&
	    dev->udev->descriptor.idProduct == cpu_to_le16(USB_GSUSB_1_PRODUCT_ID) &&
	    dev->udev->manufacturer && dev->udev->product &&
	    !strcmp(dev->udev->manufacturer, "LinkLayer Labs") &&
	    !strcmp(dev->udev->product, "CANtact Pro") &&
	    (le32_to_cpu(dconf->sw_version) <= 2))
		dev->feature |= GS_CAN_FEATURE_REQ_USB_QUIRK_LPC546XX |
			GS_CAN_FEATURE_QUIRK_BREQ_CANTACT_PRO;

	if (le32_to_cpu(dconf->sw_version) > 1)
		if (feature & GS_CAN_FEATURE_IDENTIFY)
			netdev->ethtool_ops = &gs_usb_ethtool_ops;

	kfree(bt_const);

	/* fetch extended bit timing constants if device has feature
	 * GS_CAN_FEATURE_FD and GS_CAN_FEATURE_BT_CONST_EXT
	 */
	if (feature & GS_CAN_FEATURE_FD &&
	    feature & GS_CAN_FEATURE_BT_CONST_EXT) {
		bt_const_extended = kmalloc(sizeof(*bt_const_extended), GFP_KERNEL);
		if (!bt_const_extended)
			return ERR_PTR(-ENOMEM);

		rc = usb_control_msg(interface_to_usbdev(intf),
				     usb_rcvctrlpipe(interface_to_usbdev(intf), 0),
				     GS_USB_BREQ_BT_CONST_EXT,
				     USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
				     channel, 0, bt_const_extended,
				     sizeof(*bt_const_extended),
				     1000);
		if (rc < 0) {
			dev_err(&intf->dev,
				"Couldn't get extended bit timing const for channel (err=%d)\n",
				rc);
			kfree(bt_const_extended);
			return ERR_PTR(rc);
		}

		strcpy(dev->data_bt_const.name, "gs_usb");
		dev->data_bt_const.tseg1_min = le32_to_cpu(bt_const_extended->dtseg1_min);
		dev->data_bt_const.tseg1_max = le32_to_cpu(bt_const_extended->dtseg1_max);
		dev->data_bt_const.tseg2_min = le32_to_cpu(bt_const_extended->dtseg2_min);
		dev->data_bt_const.tseg2_max = le32_to_cpu(bt_const_extended->dtseg2_max);
		dev->data_bt_const.sjw_max = le32_to_cpu(bt_const_extended->dsjw_max);
		dev->data_bt_const.brp_min = le32_to_cpu(bt_const_extended->dbrp_min);
		dev->data_bt_const.brp_max = le32_to_cpu(bt_const_extended->dbrp_max);
		dev->data_bt_const.brp_inc = le32_to_cpu(bt_const_extended->dbrp_inc);

		dev->can.data_bittiming_const = &dev->data_bt_const;
	}

	SET_NETDEV_DEV(netdev, &intf->dev);

	rc = register_candev(dev->netdev);
	if (rc) {
		free_candev(dev->netdev);
		dev_err(&intf->dev, "Couldn't register candev (err=%d)\n", rc);
		return ERR_PTR(rc);
	}

	return dev;
}

static void gs_destroy_candev(struct gs_can *dev)
{
	unregister_candev(dev->netdev);
	usb_kill_anchored_urbs(&dev->tx_submitted);
	free_candev(dev->netdev);
}

static int gs_usb_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct gs_host_frame *hf;
	struct gs_usb *dev;
	int rc = -ENOMEM;
	unsigned int icount, i;
	struct gs_host_config *hconf;
	struct gs_device_config *dconf;

	hconf = kmalloc(sizeof(*hconf), GFP_KERNEL);
	if (!hconf)
		return -ENOMEM;

	hconf->byte_order = cpu_to_le32(0x0000beef);

	/* send host config */
	rc = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			     GS_USB_BREQ_HOST_FORMAT,
			     USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			     1, intf->cur_altsetting->desc.bInterfaceNumber,
			     hconf, sizeof(*hconf), 1000);

	kfree(hconf);

	if (rc < 0) {
		dev_err(&intf->dev, "Couldn't send data format (err=%d)\n", rc);
		return rc;
	}

	dconf = kmalloc(sizeof(*dconf), GFP_KERNEL);
	if (!dconf)
		return -ENOMEM;

	/* read device config */
	rc = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			     GS_USB_BREQ_DEVICE_CONFIG,
			     USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			     1, intf->cur_altsetting->desc.bInterfaceNumber,
			     dconf, sizeof(*dconf), 1000);
	if (rc < 0) {
		dev_err(&intf->dev, "Couldn't get device config: (err=%d)\n",
			rc);
		kfree(dconf);
		return rc;
	}

	icount = dconf->icount + 1;
	dev_info(&intf->dev, "Configuring for %u interfaces\n", icount);

	if (icount > GS_MAX_INTF) {
		dev_err(&intf->dev,
			"Driver cannot handle more that %u CAN interfaces\n",
			GS_MAX_INTF);
		kfree(dconf);
		return -EINVAL;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		kfree(dconf);
		return -ENOMEM;
	}

	init_usb_anchor(&dev->rx_submitted);
	/* default to classic CAN, switch to CAN-FD if at least one of
	 * our channels support CAN-FD.
	 */
	dev->hf_size_rx = struct_size(hf, classic_can, 1);

	usb_set_intfdata(intf, dev);
	dev->udev = udev;

	for (i = 0; i < icount; i++) {
		dev->canch[i] = gs_make_candev(i, intf, dconf);
		if (IS_ERR_OR_NULL(dev->canch[i])) {
			/* save error code to return later */
			rc = PTR_ERR(dev->canch[i]);

			/* on failure destroy previously created candevs */
			icount = i;
			for (i = 0; i < icount; i++)
				gs_destroy_candev(dev->canch[i]);

			usb_kill_anchored_urbs(&dev->rx_submitted);
			kfree(dconf);
			kfree(dev);
			return rc;
		}
		dev->canch[i]->parent = dev;

		if (dev->canch[i]->can.ctrlmode_supported & CAN_CTRLMODE_FD)
			dev->hf_size_rx = struct_size(hf, canfd, 1);
	}

	kfree(dconf);

	return 0;
}

static void gs_usb_disconnect(struct usb_interface *intf)
{
	struct gs_usb *dev = usb_get_intfdata(intf);
	unsigned int i;

	usb_set_intfdata(intf, NULL);

	if (!dev) {
		dev_err(&intf->dev, "Disconnect (nodata)\n");
		return;
	}

	for (i = 0; i < GS_MAX_INTF; i++)
		if (dev->canch[i])
			gs_destroy_candev(dev->canch[i]);

	usb_kill_anchored_urbs(&dev->rx_submitted);
	kfree(dev);
}

static const struct usb_device_id gs_usb_table[] = {
	{ USB_DEVICE_INTERFACE_NUMBER(USB_GSUSB_1_VENDOR_ID,
				      USB_GSUSB_1_PRODUCT_ID, 0) },
	{ USB_DEVICE_INTERFACE_NUMBER(USB_CANDLELIGHT_VENDOR_ID,
				      USB_CANDLELIGHT_PRODUCT_ID, 0) },
	{ USB_DEVICE_INTERFACE_NUMBER(USB_CES_CANEXT_FD_VENDOR_ID,
				      USB_CES_CANEXT_FD_PRODUCT_ID, 0) },
	{ USB_DEVICE_INTERFACE_NUMBER(USB_ABE_CANDEBUGGER_FD_VENDOR_ID,
				      USB_ABE_CANDEBUGGER_FD_PRODUCT_ID, 0) },
	{} /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, gs_usb_table);

static struct usb_driver gs_usb_driver = {
	.name = "gs_usb",
	.probe = gs_usb_probe,
	.disconnect = gs_usb_disconnect,
	.id_table = gs_usb_table,
};

module_usb_driver(gs_usb_driver);

MODULE_AUTHOR("Maximilian Schneider <mws@schneidersoft.net>");
MODULE_DESCRIPTION(
"Socket CAN device driver for Geschwister Schneider Technologie-, "
"Entwicklungs- und Vertriebs UG. USB2.0 to CAN interfaces\n"
"and bytewerk.org candleLight USB CAN interfaces.");
MODULE_LICENSE("GPL v2");
