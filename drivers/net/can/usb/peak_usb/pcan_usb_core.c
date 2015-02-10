/*
 * CAN driver for PEAK System USB adapters
 * Derived from the PCAN project file driver/src/pcan_usb_core.c
 *
 * Copyright (C) 2003-2010 PEAK System-Technik GmbH
 * Copyright (C) 2010-2012 Stephane Grosjean <s.grosjean@peak-system.com>
 *
 * Many thanks to Klaus Hitschler <klaus.hitschler@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include <linux/init.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/usb.h>

#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>

#include "pcan_usb_core.h"

MODULE_AUTHOR("Stephane Grosjean <s.grosjean@peak-system.com>");
MODULE_DESCRIPTION("CAN driver for PEAK-System USB adapters");
MODULE_LICENSE("GPL v2");

/* Table of devices that work with this driver */
static struct usb_device_id peak_usb_table[] = {
	{USB_DEVICE(PCAN_USB_VENDOR_ID, PCAN_USB_PRODUCT_ID)},
	{USB_DEVICE(PCAN_USB_VENDOR_ID, PCAN_USBPRO_PRODUCT_ID)},
	{} /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, peak_usb_table);

/* List of supported PCAN-USB adapters (NULL terminated list) */
static struct peak_usb_adapter *peak_usb_adapters_list[] = {
	&pcan_usb,
	&pcan_usb_pro,
	NULL,
};

/*
 * dump memory
 */
#define DUMP_WIDTH	16
void pcan_dump_mem(char *prompt, void *p, int l)
{
	pr_info("%s dumping %s (%d bytes):\n",
		PCAN_USB_DRIVER_NAME, prompt ? prompt : "memory", l);
	print_hex_dump(KERN_INFO, PCAN_USB_DRIVER_NAME " ", DUMP_PREFIX_NONE,
		       DUMP_WIDTH, 1, p, l, false);
}

/*
 * initialize a time_ref object with usb adapter own settings
 */
void peak_usb_init_time_ref(struct peak_time_ref *time_ref,
			    struct peak_usb_adapter *adapter)
{
	if (time_ref) {
		memset(time_ref, 0, sizeof(struct peak_time_ref));
		time_ref->adapter = adapter;
	}
}

static void peak_usb_add_us(struct timeval *tv, u32 delta_us)
{
	/* number of s. to add to final time */
	u32 delta_s = delta_us / 1000000;

	delta_us -= delta_s * 1000000;

	tv->tv_usec += delta_us;
	if (tv->tv_usec >= 1000000) {
		tv->tv_usec -= 1000000;
		delta_s++;
	}
	tv->tv_sec += delta_s;
}

/*
 * sometimes, another now may be  more recent than current one...
 */
void peak_usb_update_ts_now(struct peak_time_ref *time_ref, u32 ts_now)
{
	time_ref->ts_dev_2 = ts_now;

	/* should wait at least two passes before computing */
	if (time_ref->tv_host.tv_sec > 0) {
		u32 delta_ts = time_ref->ts_dev_2 - time_ref->ts_dev_1;

		if (time_ref->ts_dev_2 < time_ref->ts_dev_1)
			delta_ts &= (1 << time_ref->adapter->ts_used_bits) - 1;

		time_ref->ts_total += delta_ts;
	}
}

/*
 * register device timestamp as now
 */
void peak_usb_set_ts_now(struct peak_time_ref *time_ref, u32 ts_now)
{
	if (time_ref->tv_host_0.tv_sec == 0) {
		/* use monotonic clock to correctly compute further deltas */
		time_ref->tv_host_0 = ktime_to_timeval(ktime_get());
		time_ref->tv_host.tv_sec = 0;
	} else {
		/*
		 * delta_us should not be >= 2^32 => delta_s should be < 4294
		 * handle 32-bits wrapping here: if count of s. reaches 4200,
		 * reset counters and change time base
		 */
		if (time_ref->tv_host.tv_sec != 0) {
			u32 delta_s = time_ref->tv_host.tv_sec
						- time_ref->tv_host_0.tv_sec;
			if (delta_s > 4200) {
				time_ref->tv_host_0 = time_ref->tv_host;
				time_ref->ts_total = 0;
			}
		}

		time_ref->tv_host = ktime_to_timeval(ktime_get());
		time_ref->tick_count++;
	}

	time_ref->ts_dev_1 = time_ref->ts_dev_2;
	peak_usb_update_ts_now(time_ref, ts_now);
}

/*
 * compute timeval according to current ts and time_ref data
 */
void peak_usb_get_ts_tv(struct peak_time_ref *time_ref, u32 ts,
			struct timeval *tv)
{
	/* protect from getting timeval before setting now */
	if (time_ref->tv_host.tv_sec > 0) {
		u64 delta_us;

		delta_us = ts - time_ref->ts_dev_2;
		if (ts < time_ref->ts_dev_2)
			delta_us &= (1 << time_ref->adapter->ts_used_bits) - 1;

		delta_us += time_ref->ts_total;

		delta_us *= time_ref->adapter->us_per_ts_scale;
		delta_us >>= time_ref->adapter->us_per_ts_shift;

		*tv = time_ref->tv_host_0;
		peak_usb_add_us(tv, (u32)delta_us);
	} else {
		*tv = ktime_to_timeval(ktime_get());
	}
}

/*
 * callback for bulk Rx urb
 */
static void peak_usb_read_bulk_callback(struct urb *urb)
{
	struct peak_usb_device *dev = urb->context;
	struct net_device *netdev;
	int err;

	netdev = dev->netdev;

	if (!netif_device_present(netdev))
		return;

	/* check reception status */
	switch (urb->status) {
	case 0:
		/* success */
		break;

	case -EILSEQ:
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		return;

	default:
		if (net_ratelimit())
			netdev_err(netdev,
				   "Rx urb aborted (%d)\n", urb->status);
		goto resubmit_urb;
	}

	/* protect from any incoming empty msgs */
	if ((urb->actual_length > 0) && (dev->adapter->dev_decode_buf)) {
		/* handle these kinds of msgs only if _start callback called */
		if (dev->state & PCAN_USB_STATE_STARTED) {
			err = dev->adapter->dev_decode_buf(dev, urb);
			if (err)
				pcan_dump_mem("received usb message",
					      urb->transfer_buffer,
					      urb->transfer_buffer_length);
		}
	}

resubmit_urb:
	usb_fill_bulk_urb(urb, dev->udev,
		usb_rcvbulkpipe(dev->udev, dev->ep_msg_in),
		urb->transfer_buffer, dev->adapter->rx_buffer_size,
		peak_usb_read_bulk_callback, dev);

	usb_anchor_urb(urb, &dev->rx_submitted);
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (!err)
		return;

	usb_unanchor_urb(urb);

	if (err == -ENODEV)
		netif_device_detach(netdev);
	else
		netdev_err(netdev, "failed resubmitting read bulk urb: %d\n",
			   err);
}

/*
 * callback for bulk Tx urb
 */
static void peak_usb_write_bulk_callback(struct urb *urb)
{
	struct peak_tx_urb_context *context = urb->context;
	struct peak_usb_device *dev;
	struct net_device *netdev;

	BUG_ON(!context);

	dev = context->dev;
	netdev = dev->netdev;

	atomic_dec(&dev->active_tx_urbs);

	if (!netif_device_present(netdev))
		return;

	/* check tx status */
	switch (urb->status) {
	case 0:
		/* transmission complete */
		netdev->stats.tx_packets++;
		netdev->stats.tx_bytes += context->dlc;

		/* prevent tx timeout */
		netdev->trans_start = jiffies;
		break;

	default:
		if (net_ratelimit())
			netdev_err(netdev, "Tx urb aborted (%d)\n",
				   urb->status);
	case -EPROTO:
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:

		break;
	}

	/* should always release echo skb and corresponding context */
	can_get_echo_skb(netdev, context->echo_index);
	context->echo_index = PCAN_USB_MAX_TX_URBS;

	/* do wakeup tx queue in case of success only */
	if (!urb->status)
		netif_wake_queue(netdev);
}

/*
 * called by netdev to send one skb on the CAN interface.
 */
static netdev_tx_t peak_usb_ndo_start_xmit(struct sk_buff *skb,
					   struct net_device *netdev)
{
	struct peak_usb_device *dev = netdev_priv(netdev);
	struct peak_tx_urb_context *context = NULL;
	struct net_device_stats *stats = &netdev->stats;
	struct can_frame *cf = (struct can_frame *)skb->data;
	struct urb *urb;
	u8 *obuf;
	int i, err;
	size_t size = dev->adapter->tx_buffer_size;

	if (can_dropped_invalid_skb(netdev, skb))
		return NETDEV_TX_OK;

	for (i = 0; i < PCAN_USB_MAX_TX_URBS; i++)
		if (dev->tx_contexts[i].echo_index == PCAN_USB_MAX_TX_URBS) {
			context = dev->tx_contexts + i;
			break;
		}

	if (!context) {
		/* should not occur except during restart */
		return NETDEV_TX_BUSY;
	}

	urb = context->urb;
	obuf = urb->transfer_buffer;

	err = dev->adapter->dev_encode_msg(dev, skb, obuf, &size);
	if (err) {
		if (net_ratelimit())
			netdev_err(netdev, "packet dropped\n");
		dev_kfree_skb(skb);
		stats->tx_dropped++;
		return NETDEV_TX_OK;
	}

	context->echo_index = i;
	context->dlc = cf->can_dlc;

	usb_anchor_urb(urb, &dev->tx_submitted);

	can_put_echo_skb(skb, netdev, context->echo_index);

	atomic_inc(&dev->active_tx_urbs);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		can_free_echo_skb(netdev, context->echo_index);

		usb_unanchor_urb(urb);

		/* this context is not used in fact */
		context->echo_index = PCAN_USB_MAX_TX_URBS;

		atomic_dec(&dev->active_tx_urbs);

		switch (err) {
		case -ENODEV:
			netif_device_detach(netdev);
			break;
		default:
			netdev_warn(netdev, "tx urb submitting failed err=%d\n",
				    err);
		case -ENOENT:
			/* cable unplugged */
			stats->tx_dropped++;
		}
	} else {
		netdev->trans_start = jiffies;

		/* slow down tx path */
		if (atomic_read(&dev->active_tx_urbs) >= PCAN_USB_MAX_TX_URBS)
			netif_stop_queue(netdev);
	}

	return NETDEV_TX_OK;
}

/*
 * start the CAN interface.
 * Rx and Tx urbs are allocated here. Rx urbs are submitted here.
 */
static int peak_usb_start(struct peak_usb_device *dev)
{
	struct net_device *netdev = dev->netdev;
	int err, i;

	for (i = 0; i < PCAN_USB_MAX_RX_URBS; i++) {
		struct urb *urb;
		u8 *buf;

		/* create a URB, and a buffer for it, to receive usb messages */
		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			netdev_err(netdev, "No memory left for URBs\n");
			err = -ENOMEM;
			break;
		}

		buf = kmalloc(dev->adapter->rx_buffer_size, GFP_KERNEL);
		if (!buf) {
			usb_free_urb(urb);
			err = -ENOMEM;
			break;
		}

		usb_fill_bulk_urb(urb, dev->udev,
			usb_rcvbulkpipe(dev->udev, dev->ep_msg_in),
			buf, dev->adapter->rx_buffer_size,
			peak_usb_read_bulk_callback, dev);

		/* ask last usb_free_urb() to also kfree() transfer_buffer */
		urb->transfer_flags |= URB_FREE_BUFFER;
		usb_anchor_urb(urb, &dev->rx_submitted);

		err = usb_submit_urb(urb, GFP_KERNEL);
		if (err) {
			if (err == -ENODEV)
				netif_device_detach(dev->netdev);

			usb_unanchor_urb(urb);
			kfree(buf);
			usb_free_urb(urb);
			break;
		}

		/* drop reference, USB core will take care of freeing it */
		usb_free_urb(urb);
	}

	/* did we submit any URBs? Warn if we was not able to submit all urbs */
	if (i < PCAN_USB_MAX_RX_URBS) {
		if (i == 0) {
			netdev_err(netdev, "couldn't setup any rx URB\n");
			return err;
		}

		netdev_warn(netdev, "rx performance may be slow\n");
	}

	/* pre-alloc tx buffers and corresponding urbs */
	for (i = 0; i < PCAN_USB_MAX_TX_URBS; i++) {
		struct peak_tx_urb_context *context;
		struct urb *urb;
		u8 *buf;

		/* create a URB and a buffer for it, to transmit usb messages */
		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			netdev_err(netdev, "No memory left for URBs\n");
			err = -ENOMEM;
			break;
		}

		buf = kmalloc(dev->adapter->tx_buffer_size, GFP_KERNEL);
		if (!buf) {
			usb_free_urb(urb);
			err = -ENOMEM;
			break;
		}

		context = dev->tx_contexts + i;
		context->dev = dev;
		context->urb = urb;

		usb_fill_bulk_urb(urb, dev->udev,
			usb_sndbulkpipe(dev->udev, dev->ep_msg_out),
			buf, dev->adapter->tx_buffer_size,
			peak_usb_write_bulk_callback, context);

		/* ask last usb_free_urb() to also kfree() transfer_buffer */
		urb->transfer_flags |= URB_FREE_BUFFER;
	}

	/* warn if we were not able to allocate enough tx contexts */
	if (i < PCAN_USB_MAX_TX_URBS) {
		if (i == 0) {
			netdev_err(netdev, "couldn't setup any tx URB\n");
			return err;
		}

		netdev_warn(netdev, "tx performance may be slow\n");
	}

	if (dev->adapter->dev_start) {
		err = dev->adapter->dev_start(dev);
		if (err)
			goto failed;
	}

	dev->state |= PCAN_USB_STATE_STARTED;

	/* can set bus on now */
	if (dev->adapter->dev_set_bus) {
		err = dev->adapter->dev_set_bus(dev, 1);
		if (err)
			goto failed;
	}

	dev->can.state = CAN_STATE_ERROR_ACTIVE;

	return 0;

failed:
	if (err == -ENODEV)
		netif_device_detach(dev->netdev);

	netdev_warn(netdev, "couldn't submit control: %d\n", err);

	return err;
}

/*
 * called by netdev to open the corresponding CAN interface.
 */
static int peak_usb_ndo_open(struct net_device *netdev)
{
	struct peak_usb_device *dev = netdev_priv(netdev);
	int err;

	/* common open */
	err = open_candev(netdev);
	if (err)
		return err;

	/* finally start device */
	err = peak_usb_start(dev);
	if (err) {
		netdev_err(netdev, "couldn't start device: %d\n", err);
		close_candev(netdev);
		return err;
	}

	netif_start_queue(netdev);

	return 0;
}

/*
 * unlink in-flight Rx and Tx urbs and free their memory.
 */
static void peak_usb_unlink_all_urbs(struct peak_usb_device *dev)
{
	int i;

	/* free all Rx (submitted) urbs */
	usb_kill_anchored_urbs(&dev->rx_submitted);

	/* free unsubmitted Tx urbs first */
	for (i = 0; i < PCAN_USB_MAX_TX_URBS; i++) {
		struct urb *urb = dev->tx_contexts[i].urb;

		if (!urb ||
		    dev->tx_contexts[i].echo_index != PCAN_USB_MAX_TX_URBS) {
			/*
			 * this urb is already released or always submitted,
			 * let usb core free by itself
			 */
			continue;
		}

		usb_free_urb(urb);
		dev->tx_contexts[i].urb = NULL;
	}

	/* then free all submitted Tx urbs */
	usb_kill_anchored_urbs(&dev->tx_submitted);
	atomic_set(&dev->active_tx_urbs, 0);
}

/*
 * called by netdev to close the corresponding CAN interface.
 */
static int peak_usb_ndo_stop(struct net_device *netdev)
{
	struct peak_usb_device *dev = netdev_priv(netdev);

	dev->state &= ~PCAN_USB_STATE_STARTED;
	netif_stop_queue(netdev);

	/* unlink all pending urbs and free used memory */
	peak_usb_unlink_all_urbs(dev);

	if (dev->adapter->dev_stop)
		dev->adapter->dev_stop(dev);

	close_candev(netdev);

	dev->can.state = CAN_STATE_STOPPED;

	/* can set bus off now */
	if (dev->adapter->dev_set_bus) {
		int err = dev->adapter->dev_set_bus(dev, 0);
		if (err)
			return err;
	}

	return 0;
}

/*
 * handle end of waiting for the device to reset
 */
void peak_usb_restart_complete(struct peak_usb_device *dev)
{
	/* finally MUST update can state */
	dev->can.state = CAN_STATE_ERROR_ACTIVE;

	/* netdev queue can be awaken now */
	netif_wake_queue(dev->netdev);
}

void peak_usb_async_complete(struct urb *urb)
{
	kfree(urb->transfer_buffer);
	usb_free_urb(urb);
}

/*
 * device (auto-)restart mechanism runs in a timer context =>
 * MUST handle restart with asynchronous usb transfers
 */
static int peak_usb_restart(struct peak_usb_device *dev)
{
	struct urb *urb;
	int err;
	u8 *buf;

	/*
	 * if device doesn't define any asynchronous restart handler, simply
	 * wake the netdev queue up
	 */
	if (!dev->adapter->dev_restart_async) {
		peak_usb_restart_complete(dev);
		return 0;
	}

	/* first allocate a urb to handle the asynchronous steps */
	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		netdev_err(dev->netdev, "no memory left for urb\n");
		return -ENOMEM;
	}

	/* also allocate enough space for the commands to send */
	buf = kmalloc(PCAN_USB_MAX_CMD_LEN, GFP_ATOMIC);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	/* call the device specific handler for the restart */
	err = dev->adapter->dev_restart_async(dev, urb, buf);
	if (!err)
		return 0;

	kfree(buf);
	usb_free_urb(urb);

	return err;
}

/*
 * candev callback used to change CAN mode.
 * Warning: this is called from a timer context!
 */
static int peak_usb_set_mode(struct net_device *netdev, enum can_mode mode)
{
	struct peak_usb_device *dev = netdev_priv(netdev);
	int err = 0;

	switch (mode) {
	case CAN_MODE_START:
		err = peak_usb_restart(dev);
		if (err)
			netdev_err(netdev, "couldn't start device (err %d)\n",
				   err);
		break;

	default:
		return -EOPNOTSUPP;
	}

	return err;
}

/*
 * candev callback used to set device bitrate.
 */
static int peak_usb_set_bittiming(struct net_device *netdev)
{
	struct peak_usb_device *dev = netdev_priv(netdev);
	struct can_bittiming *bt = &dev->can.bittiming;

	if (dev->adapter->dev_set_bittiming) {
		int err = dev->adapter->dev_set_bittiming(dev, bt);

		if (err)
			netdev_info(netdev, "couldn't set bitrate (err %d)\n",
				err);
		return err;
	}

	return 0;
}

static const struct net_device_ops peak_usb_netdev_ops = {
	.ndo_open = peak_usb_ndo_open,
	.ndo_stop = peak_usb_ndo_stop,
	.ndo_start_xmit = peak_usb_ndo_start_xmit,
};

/*
 * create one device which is attached to CAN controller #ctrl_idx of the
 * usb adapter.
 */
static int peak_usb_create_dev(struct peak_usb_adapter *peak_usb_adapter,
			       struct usb_interface *intf, int ctrl_idx)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	int sizeof_candev = peak_usb_adapter->sizeof_dev_private;
	struct peak_usb_device *dev;
	struct net_device *netdev;
	int i, err;
	u16 tmp16;

	if (sizeof_candev < sizeof(struct peak_usb_device))
		sizeof_candev = sizeof(struct peak_usb_device);

	netdev = alloc_candev(sizeof_candev, PCAN_USB_MAX_TX_URBS);
	if (!netdev) {
		dev_err(&intf->dev, "%s: couldn't alloc candev\n",
			PCAN_USB_DRIVER_NAME);
		return -ENOMEM;
	}

	dev = netdev_priv(netdev);

	/* allocate a buffer large enough to send commands */
	dev->cmd_buf = kmalloc(PCAN_USB_MAX_CMD_LEN, GFP_KERNEL);
	if (!dev->cmd_buf) {
		err = -ENOMEM;
		goto lbl_free_candev;
	}

	dev->udev = usb_dev;
	dev->netdev = netdev;
	dev->adapter = peak_usb_adapter;
	dev->ctrl_idx = ctrl_idx;
	dev->state = PCAN_USB_STATE_CONNECTED;

	dev->ep_msg_in = peak_usb_adapter->ep_msg_in;
	dev->ep_msg_out = peak_usb_adapter->ep_msg_out[ctrl_idx];

	dev->can.clock = peak_usb_adapter->clock;
	dev->can.bittiming_const = &peak_usb_adapter->bittiming_const;
	dev->can.do_set_bittiming = peak_usb_set_bittiming;
	dev->can.do_set_mode = peak_usb_set_mode;
	dev->can.ctrlmode_supported = CAN_CTRLMODE_3_SAMPLES |
				      CAN_CTRLMODE_LISTENONLY;

	netdev->netdev_ops = &peak_usb_netdev_ops;

	netdev->flags |= IFF_ECHO; /* we support local echo */

	init_usb_anchor(&dev->rx_submitted);

	init_usb_anchor(&dev->tx_submitted);
	atomic_set(&dev->active_tx_urbs, 0);

	for (i = 0; i < PCAN_USB_MAX_TX_URBS; i++)
		dev->tx_contexts[i].echo_index = PCAN_USB_MAX_TX_URBS;

	dev->prev_siblings = usb_get_intfdata(intf);
	usb_set_intfdata(intf, dev);

	SET_NETDEV_DEV(netdev, &intf->dev);

	err = register_candev(netdev);
	if (err) {
		dev_err(&intf->dev, "couldn't register CAN device: %d\n", err);
		goto lbl_restore_intf_data;
	}

	if (dev->prev_siblings)
		(dev->prev_siblings)->next_siblings = dev;

	/* keep hw revision into the netdevice */
	tmp16 = le16_to_cpu(usb_dev->descriptor.bcdDevice);
	dev->device_rev = tmp16 >> 8;

	if (dev->adapter->dev_init) {
		err = dev->adapter->dev_init(dev);
		if (err)
			goto lbl_unregister_candev;
	}

	/* set bus off */
	if (dev->adapter->dev_set_bus) {
		err = dev->adapter->dev_set_bus(dev, 0);
		if (err)
			goto lbl_unregister_candev;
	}

	/* get device number early */
	if (dev->adapter->dev_get_device_id)
		dev->adapter->dev_get_device_id(dev, &dev->device_number);

	netdev_info(netdev, "attached to %s channel %u (device %u)\n",
			peak_usb_adapter->name, ctrl_idx, dev->device_number);

	return 0;

lbl_unregister_candev:
	unregister_candev(netdev);

lbl_restore_intf_data:
	usb_set_intfdata(intf, dev->prev_siblings);
	kfree(dev->cmd_buf);

lbl_free_candev:
	free_candev(netdev);

	return err;
}

/*
 * called by the usb core when the device is unplugged from the system
 */
static void peak_usb_disconnect(struct usb_interface *intf)
{
	struct peak_usb_device *dev;

	/* unregister as many netdev devices as siblings */
	for (dev = usb_get_intfdata(intf); dev; dev = dev->prev_siblings) {
		struct net_device *netdev = dev->netdev;
		char name[IFNAMSIZ];

		dev->state &= ~PCAN_USB_STATE_CONNECTED;
		strncpy(name, netdev->name, IFNAMSIZ);

		unregister_netdev(netdev);
		free_candev(netdev);

		kfree(dev->cmd_buf);
		dev->next_siblings = NULL;
		if (dev->adapter->dev_free)
			dev->adapter->dev_free(dev);

		dev_info(&intf->dev, "%s removed\n", name);
	}

	usb_set_intfdata(intf, NULL);
}

/*
 * probe function for new PEAK-System devices
 */
static int peak_usb_probe(struct usb_interface *intf,
			  const struct usb_device_id *id)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct peak_usb_adapter *peak_usb_adapter, **pp;
	int i, err = -ENOMEM;

	usb_dev = interface_to_usbdev(intf);

	/* get corresponding PCAN-USB adapter */
	for (pp = peak_usb_adapters_list; *pp; pp++)
		if ((*pp)->device_id == usb_dev->descriptor.idProduct)
			break;

	peak_usb_adapter = *pp;
	if (!peak_usb_adapter) {
		/* should never come except device_id bad usage in this file */
		pr_err("%s: didn't find device id. 0x%x in devices list\n",
			PCAN_USB_DRIVER_NAME, usb_dev->descriptor.idProduct);
		return -ENODEV;
	}

	/* got corresponding adapter: check if it handles current interface */
	if (peak_usb_adapter->intf_probe) {
		err = peak_usb_adapter->intf_probe(intf);
		if (err)
			return err;
	}

	for (i = 0; i < peak_usb_adapter->ctrl_count; i++) {
		err = peak_usb_create_dev(peak_usb_adapter, intf, i);
		if (err) {
			/* deregister already created devices */
			peak_usb_disconnect(intf);
			break;
		}
	}

	return err;
}

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver peak_usb_driver = {
	.name = PCAN_USB_DRIVER_NAME,
	.disconnect = peak_usb_disconnect,
	.probe = peak_usb_probe,
	.id_table = peak_usb_table,
};

static int __init peak_usb_init(void)
{
	int err;

	/* register this driver with the USB subsystem */
	err = usb_register(&peak_usb_driver);
	if (err)
		pr_err("%s: usb_register failed (err %d)\n",
			PCAN_USB_DRIVER_NAME, err);

	return err;
}

static int peak_usb_do_device_exit(struct device *d, void *arg)
{
	struct usb_interface *intf = to_usb_interface(d);
	struct peak_usb_device *dev;

	/* stop as many netdev devices as siblings */
	for (dev = usb_get_intfdata(intf); dev; dev = dev->prev_siblings) {
		struct net_device *netdev = dev->netdev;

		if (netif_device_present(netdev))
			if (dev->adapter->dev_exit)
				dev->adapter->dev_exit(dev);
	}

	return 0;
}

static void __exit peak_usb_exit(void)
{
	int err;

	/* last chance do send any synchronous commands here */
	err = driver_for_each_device(&peak_usb_driver.drvwrap.driver, NULL,
				     NULL, peak_usb_do_device_exit);
	if (err)
		pr_err("%s: failed to stop all can devices (err %d)\n",
			PCAN_USB_DRIVER_NAME, err);

	/* deregister this driver with the USB subsystem */
	usb_deregister(&peak_usb_driver);

	pr_info("%s: PCAN-USB interfaces driver unloaded\n",
		PCAN_USB_DRIVER_NAME);
}

module_init(peak_usb_init);
module_exit(peak_usb_exit);
