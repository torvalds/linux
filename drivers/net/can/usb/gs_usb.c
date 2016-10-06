/* CAN driver for Geschwister Schneider USB/CAN devices
 * and bytewerk.org candleLight USB CAN interfaces.
 *
 * Copyright (C) 2013-2016 Geschwister Schneider Technologie-,
 * Entwicklungs- und Vertriebs UG (Haftungsbeschränkt).
 * Copyright (C) 2016 Hubert Denkmair
 *
 * Many thanks to all socketcan devs!
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */

#include <linux/init.h>
#include <linux/signal.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/usb.h>

#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>

/* Device specific constants */
#define USB_GSUSB_1_VENDOR_ID      0x1d50
#define USB_GSUSB_1_PRODUCT_ID     0x606f

#define USB_CANDLELIGHT_VENDOR_ID  0x1209
#define USB_CANDLELIGHT_PRODUCT_ID 0x2323

#define GSUSB_ENDPOINT_IN          1
#define GSUSB_ENDPOINT_OUT         2

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
struct gs_host_config {
	u32 byte_order;
} __packed;
/* All data exchanged between host and device is exchanged in host byte order,
 * thanks to the struct gs_host_config byte_order member, which is sent first
 * to indicate the desired byte order.
 */

struct gs_device_config {
	u8 reserved1;
	u8 reserved2;
	u8 reserved3;
	u8 icount;
	u32 sw_version;
	u32 hw_version;
} __packed;

#define GS_CAN_MODE_NORMAL               0
#define GS_CAN_MODE_LISTEN_ONLY          BIT(0)
#define GS_CAN_MODE_LOOP_BACK            BIT(1)
#define GS_CAN_MODE_TRIPLE_SAMPLE        BIT(2)
#define GS_CAN_MODE_ONE_SHOT             BIT(3)

struct gs_device_mode {
	u32 mode;
	u32 flags;
} __packed;

struct gs_device_state {
	u32 state;
	u32 rxerr;
	u32 txerr;
} __packed;

struct gs_device_bittiming {
	u32 prop_seg;
	u32 phase_seg1;
	u32 phase_seg2;
	u32 sjw;
	u32 brp;
} __packed;

struct gs_identify_mode {
	u32 mode;
} __packed;

#define GS_CAN_FEATURE_LISTEN_ONLY      BIT(0)
#define GS_CAN_FEATURE_LOOP_BACK        BIT(1)
#define GS_CAN_FEATURE_TRIPLE_SAMPLE    BIT(2)
#define GS_CAN_FEATURE_ONE_SHOT         BIT(3)
#define GS_CAN_FEATURE_HW_TIMESTAMP     BIT(4)
#define GS_CAN_FEATURE_IDENTIFY         BIT(5)

struct gs_device_bt_const {
	u32 feature;
	u32 fclk_can;
	u32 tseg1_min;
	u32 tseg1_max;
	u32 tseg2_min;
	u32 tseg2_max;
	u32 sjw_max;
	u32 brp_min;
	u32 brp_max;
	u32 brp_inc;
} __packed;

#define GS_CAN_FLAG_OVERFLOW 1

struct gs_host_frame {
	u32 echo_id;
	u32 can_id;

	u8 can_dlc;
	u8 channel;
	u8 flags;
	u8 reserved;

	u8 data[8];
} __packed;
/* The GS USB devices make use of the same flags and masks as in
 * linux/can.h and linux/can/error.h, and no additional mapping is necessary.
 */

/* Only send a max of GS_MAX_TX_URBS frames per channel at a time. */
#define GS_MAX_TX_URBS 10
/* Only launch a max of GS_MAX_RX_URBS usb requests at a time. */
#define GS_MAX_RX_URBS 30
/* Maximum number of interfaces the driver supports per device.
 * Current hardware only supports 2 interfaces. The future may vary.
 */
#define GS_MAX_INTF 2

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

	struct can_bittiming_const bt_const;
	unsigned int channel;	/* channel number */

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
	atomic_t active_channels;
	struct usb_device *udev;
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

static int gs_cmd_reset(struct gs_usb *gsusb, struct gs_can *gsdev)
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
			     USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
			     gsdev->channel,
			     0,
			     dm,
			     sizeof(*dm),
			     1000);

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
		goto resubmit_urb;

	dev = usbcan->canch[hf->channel];

	netdev = dev->netdev;
	stats = &netdev->stats;

	if (!netif_device_present(netdev))
		return;

	if (hf->echo_id == -1) { /* normal rx */
		skb = alloc_can_skb(dev->netdev, &cf);
		if (!skb)
			return;

		cf->can_id = hf->can_id;

		cf->can_dlc = get_can_dlc(hf->can_dlc);
		memcpy(cf->data, hf->data, 8);

		/* ERROR frames tell us information about the controller */
		if (hf->can_id & CAN_ERR_FLAG)
			gs_update_state(dev, cf);

		netdev->stats.rx_packets++;
		netdev->stats.rx_bytes += hf->can_dlc;

		netif_rx(skb);
	} else { /* echo_id == hf->echo_id */
		if (hf->echo_id >= GS_MAX_TX_URBS) {
			netdev_err(netdev,
				   "Unexpected out of range echo id %d\n",
				   hf->echo_id);
			goto resubmit_urb;
		}

		netdev->stats.tx_packets++;
		netdev->stats.tx_bytes += hf->can_dlc;

		txc = gs_get_tx_context(dev, hf->echo_id);

		/* bad devices send bad echo_ids. */
		if (!txc) {
			netdev_err(netdev,
				   "Unexpected unused echo id %d\n",
				   hf->echo_id);
			goto resubmit_urb;
		}

		can_get_echo_skb(netdev, hf->echo_id);

		gs_free_tx_context(txc);

		netif_wake_queue(netdev);
	}

	if (hf->flags & GS_CAN_FLAG_OVERFLOW) {
		skb = alloc_can_err_skb(netdev, &cf);
		if (!skb)
			goto resubmit_urb;

		cf->can_id |= CAN_ERR_CRTL;
		cf->can_dlc = CAN_ERR_DLC;
		cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
		stats->rx_over_errors++;
		stats->rx_errors++;
		netif_rx(skb);
	}

 resubmit_urb:
	usb_fill_bulk_urb(urb,
			  usbcan->udev,
			  usb_rcvbulkpipe(usbcan->udev, GSUSB_ENDPOINT_IN),
			  hf,
			  sizeof(struct gs_host_frame),
			  gs_usb_receive_bulk_callback,
			  usbcan
			  );

	rc = usb_submit_urb(urb, GFP_ATOMIC);

	/* USB failure take down all interfaces */
	if (rc == -ENODEV) {
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

	dbt->prop_seg = bt->prop_seg;
	dbt->phase_seg1 = bt->phase_seg1;
	dbt->phase_seg2 = bt->phase_seg2;
	dbt->sjw = bt->sjw;
	dbt->brp = bt->brp;

	/* request bit timings */
	rc = usb_control_msg(interface_to_usbdev(intf),
			     usb_sndctrlpipe(interface_to_usbdev(intf), 0),
			     GS_USB_BREQ_BITTIMING,
			     USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
			     dev->channel,
			     0,
			     dbt,
			     sizeof(*dbt),
			     1000);

	kfree(dbt);

	if (rc < 0)
		dev_err(netdev->dev.parent, "Couldn't set bittimings (err=%d)",
			rc);

	return rc;
}

static void gs_usb_xmit_callback(struct urb *urb)
{
	struct gs_tx_context *txc = urb->context;
	struct gs_can *dev = txc->dev;
	struct net_device *netdev = dev->netdev;

	if (urb->status)
		netdev_info(netdev, "usb xmit fail %d\n", txc->echo_id);

	usb_free_coherent(urb->dev,
			  urb->transfer_buffer_length,
			  urb->transfer_buffer,
			  urb->transfer_dma);

	atomic_dec(&dev->active_tx_urbs);

	if (!netif_device_present(netdev))
		return;

	if (netif_queue_stopped(netdev))
		netif_wake_queue(netdev);
}

static netdev_tx_t gs_can_start_xmit(struct sk_buff *skb,
				     struct net_device *netdev)
{
	struct gs_can *dev = netdev_priv(netdev);
	struct net_device_stats *stats = &dev->netdev->stats;
	struct urb *urb;
	struct gs_host_frame *hf;
	struct can_frame *cf;
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

	hf = usb_alloc_coherent(dev->udev, sizeof(*hf), GFP_ATOMIC,
				&urb->transfer_dma);
	if (!hf) {
		netdev_err(netdev, "No memory left for USB buffer\n");
		goto nomem_hf;
	}

	idx = txc->echo_id;

	if (idx >= GS_MAX_TX_URBS) {
		netdev_err(netdev, "Invalid tx context %d\n", idx);
		goto badidx;
	}

	hf->echo_id = idx;
	hf->channel = dev->channel;

	cf = (struct can_frame *)skb->data;

	hf->can_id = cf->can_id;
	hf->can_dlc = cf->can_dlc;
	memcpy(hf->data, cf->data, cf->can_dlc);

	usb_fill_bulk_urb(urb, dev->udev,
			  usb_sndbulkpipe(dev->udev, GSUSB_ENDPOINT_OUT),
			  hf,
			  sizeof(*hf),
			  gs_usb_xmit_callback,
			  txc);

	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	usb_anchor_urb(urb, &dev->tx_submitted);

	can_put_echo_skb(skb, netdev, idx);

	atomic_inc(&dev->active_tx_urbs);

	rc = usb_submit_urb(urb, GFP_ATOMIC);
	if (unlikely(rc)) {			/* usb send failed */
		atomic_dec(&dev->active_tx_urbs);

		can_free_echo_skb(netdev, idx);
		gs_free_tx_context(txc);

		usb_unanchor_urb(urb);
		usb_free_coherent(dev->udev,
				  sizeof(*hf),
				  hf,
				  urb->transfer_dma);


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
	usb_free_coherent(dev->udev,
			  sizeof(*hf),
			  hf,
			  urb->transfer_dma);
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
	u32 ctrlmode;

	rc = open_candev(netdev);
	if (rc)
		return rc;

	if (atomic_add_return(1, &parent->active_channels) == 1) {
		for (i = 0; i < GS_MAX_RX_URBS; i++) {
			struct urb *urb;
			u8 *buf;

			/* alloc rx urb */
			urb = usb_alloc_urb(0, GFP_KERNEL);
			if (!urb)
				return -ENOMEM;

			/* alloc rx buffer */
			buf = usb_alloc_coherent(dev->udev,
						 sizeof(struct gs_host_frame),
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
					  sizeof(struct gs_host_frame),
					  gs_usb_receive_bulk_callback,
					  parent);
			urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

			usb_anchor_urb(urb, &parent->rx_submitted);

			rc = usb_submit_urb(urb, GFP_KERNEL);
			if (rc) {
				if (rc == -ENODEV)
					netif_device_detach(dev->netdev);

				netdev_err(netdev,
					   "usb_submit failed (err=%d)\n",
					   rc);

				usb_unanchor_urb(urb);
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
	ctrlmode = dev->can.ctrlmode;
	dm->flags = 0;

	if (ctrlmode & CAN_CTRLMODE_LOOPBACK)
		dm->flags |= GS_CAN_MODE_LOOP_BACK;
	else if (ctrlmode & CAN_CTRLMODE_LISTENONLY)
		dm->flags |= GS_CAN_MODE_LISTEN_ONLY;

	/* Controller is not allowed to retry TX
	 * this mode is unavailable on atmels uc3c hardware
	 */
	if (ctrlmode & CAN_CTRLMODE_ONE_SHOT)
		dm->flags |= GS_CAN_MODE_ONE_SHOT;

	if (ctrlmode & CAN_CTRLMODE_3_SAMPLES)
		dm->flags |= GS_CAN_MODE_TRIPLE_SAMPLE;

	/* finally start device */
	dm->mode = GS_CAN_MODE_START;
	rc = usb_control_msg(interface_to_usbdev(dev->iface),
			     usb_sndctrlpipe(interface_to_usbdev(dev->iface), 0),
			     GS_USB_BREQ_MODE,
			     USB_DIR_OUT | USB_TYPE_VENDOR |
			     USB_RECIP_INTERFACE,
			     dev->channel,
			     0,
			     dm,
			     sizeof(*dm),
			     1000);

	if (rc < 0) {
		netdev_err(netdev, "Couldn't start device (err=%d)\n", rc);
		kfree(dm);
		return rc;
	}

	kfree(dm);

	dev->can.state = CAN_STATE_ERROR_ACTIVE;

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
	if (atomic_dec_and_test(&parent->active_channels))
		usb_kill_anchored_urbs(&parent->rx_submitted);

	/* Stop sending URBs */
	usb_kill_anchored_urbs(&dev->tx_submitted);
	atomic_set(&dev->active_tx_urbs, 0);

	/* reset the device */
	rc = gs_cmd_reset(parent, dev);
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
	struct gs_identify_mode imode;
	int rc;

	if (do_identify)
		imode.mode = GS_CAN_IDENTIFY_ON;
	else
		imode.mode = GS_CAN_IDENTIFY_OFF;

	rc = usb_control_msg(interface_to_usbdev(dev->iface),
			     usb_sndctrlpipe(interface_to_usbdev(dev->iface),
					     0),
			     GS_USB_BREQ_IDENTIFY,
			     USB_DIR_OUT | USB_TYPE_VENDOR |
			     USB_RECIP_INTERFACE,
			     dev->channel,
			     0,
			     &imode,
			     sizeof(imode),
			     100);

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

	bt_const = kmalloc(sizeof(*bt_const), GFP_KERNEL);
	if (!bt_const)
		return ERR_PTR(-ENOMEM);

	/* fetch bit timing constants */
	rc = usb_control_msg(interface_to_usbdev(intf),
			     usb_rcvctrlpipe(interface_to_usbdev(intf), 0),
			     GS_USB_BREQ_BT_CONST,
			     USB_DIR_IN|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
			     channel,
			     0,
			     bt_const,
			     sizeof(*bt_const),
			     1000);

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

	/* dev settup */
	strcpy(dev->bt_const.name, "gs_usb");
	dev->bt_const.tseg1_min = bt_const->tseg1_min;
	dev->bt_const.tseg1_max = bt_const->tseg1_max;
	dev->bt_const.tseg2_min = bt_const->tseg2_min;
	dev->bt_const.tseg2_max = bt_const->tseg2_max;
	dev->bt_const.sjw_max = bt_const->sjw_max;
	dev->bt_const.brp_min = bt_const->brp_min;
	dev->bt_const.brp_max = bt_const->brp_max;
	dev->bt_const.brp_inc = bt_const->brp_inc;

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

	/* can settup */
	dev->can.state = CAN_STATE_STOPPED;
	dev->can.clock.freq = bt_const->fclk_can;
	dev->can.bittiming_const = &dev->bt_const;
	dev->can.do_set_bittiming = gs_usb_set_bittiming;

	dev->can.ctrlmode_supported = 0;

	if (bt_const->feature & GS_CAN_FEATURE_LISTEN_ONLY)
		dev->can.ctrlmode_supported |= CAN_CTRLMODE_LISTENONLY;

	if (bt_const->feature & GS_CAN_FEATURE_LOOP_BACK)
		dev->can.ctrlmode_supported |= CAN_CTRLMODE_LOOPBACK;

	if (bt_const->feature & GS_CAN_FEATURE_TRIPLE_SAMPLE)
		dev->can.ctrlmode_supported |= CAN_CTRLMODE_3_SAMPLES;

	if (bt_const->feature & GS_CAN_FEATURE_ONE_SHOT)
		dev->can.ctrlmode_supported |= CAN_CTRLMODE_ONE_SHOT;

	SET_NETDEV_DEV(netdev, &intf->dev);

	if (dconf->sw_version > 1)
		if (bt_const->feature & GS_CAN_FEATURE_IDENTIFY)
			netdev->ethtool_ops = &gs_usb_ethtool_ops;

	kfree(bt_const);

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
	struct gs_usb *dev;
	int rc = -ENOMEM;
	unsigned int icount, i;
	struct gs_host_config hconf = {
		.byte_order = 0x0000beef,
	};
	struct gs_device_config dconf;

	/* send host config */
	rc = usb_control_msg(interface_to_usbdev(intf),
			     usb_sndctrlpipe(interface_to_usbdev(intf), 0),
			     GS_USB_BREQ_HOST_FORMAT,
			     USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
			     1,
			     intf->altsetting[0].desc.bInterfaceNumber,
			     &hconf,
			     sizeof(hconf),
			     1000);

	if (rc < 0) {
		dev_err(&intf->dev, "Couldn't send data format (err=%d)\n",
			rc);
		return rc;
	}

	/* read device config */
	rc = usb_control_msg(interface_to_usbdev(intf),
			     usb_rcvctrlpipe(interface_to_usbdev(intf), 0),
			     GS_USB_BREQ_DEVICE_CONFIG,
			     USB_DIR_IN|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
			     1,
			     intf->altsetting[0].desc.bInterfaceNumber,
			     &dconf,
			     sizeof(dconf),
			     1000);
	if (rc < 0) {
		dev_err(&intf->dev, "Couldn't get device config: (err=%d)\n",
			rc);
		return rc;
	}

	icount = dconf.icount + 1;
	dev_info(&intf->dev, "Configuring for %d interfaces\n", icount);

	if (icount > GS_MAX_INTF) {
		dev_err(&intf->dev,
			"Driver cannot handle more that %d CAN interfaces\n",
			GS_MAX_INTF);
		return -EINVAL;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	init_usb_anchor(&dev->rx_submitted);

	atomic_set(&dev->active_channels, 0);

	usb_set_intfdata(intf, dev);
	dev->udev = interface_to_usbdev(intf);

	for (i = 0; i < icount; i++) {
		dev->canch[i] = gs_make_candev(i, intf, &dconf);
		if (IS_ERR_OR_NULL(dev->canch[i])) {
			/* save error code to return later */
			rc = PTR_ERR(dev->canch[i]);

			/* on failure destroy previously created candevs */
			icount = i;
			for (i = 0; i < icount; i++)
				gs_destroy_candev(dev->canch[i]);

			usb_kill_anchored_urbs(&dev->rx_submitted);
			kfree(dev);
			return rc;
		}
		dev->canch[i]->parent = dev;
	}

	return 0;
}

static void gs_usb_disconnect(struct usb_interface *intf)
{
	unsigned i;
	struct gs_usb *dev = usb_get_intfdata(intf);
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
	{} /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, gs_usb_table);

static struct usb_driver gs_usb_driver = {
	.name       = "gs_usb",
	.probe      = gs_usb_probe,
	.disconnect = gs_usb_disconnect,
	.id_table   = gs_usb_table,
};

module_usb_driver(gs_usb_driver);

MODULE_AUTHOR("Maximilian Schneider <mws@schneidersoft.net>");
MODULE_DESCRIPTION(
"Socket CAN device driver for Geschwister Schneider Technologie-, "
"Entwicklungs- und Vertriebs UG. USB2.0 to CAN interfaces\n"
"and bytewerk.org candleLight USB CAN interfaces.");
MODULE_LICENSE("GPL v2");
