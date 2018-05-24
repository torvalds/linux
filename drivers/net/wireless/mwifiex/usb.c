/*
 * Marvell Wireless LAN device driver: USB specific handling
 *
 * Copyright (C) 2012-2014, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#include "main.h"
#include "usb.h"

#define USB_VERSION	"1.0"

static u8 user_rmmod;
static struct mwifiex_if_ops usb_ops;
static struct semaphore add_remove_card_sem;

static struct usb_device_id mwifiex_usb_table[] = {
	/* 8766 */
	{USB_DEVICE(USB8XXX_VID, USB8766_PID_1)},
	{USB_DEVICE_AND_INTERFACE_INFO(USB8XXX_VID, USB8766_PID_2,
				       USB_CLASS_VENDOR_SPEC,
				       USB_SUBCLASS_VENDOR_SPEC, 0xff)},
	/* 8797 */
	{USB_DEVICE(USB8XXX_VID, USB8797_PID_1)},
	{USB_DEVICE_AND_INTERFACE_INFO(USB8XXX_VID, USB8797_PID_2,
				       USB_CLASS_VENDOR_SPEC,
				       USB_SUBCLASS_VENDOR_SPEC, 0xff)},
	/* 8801 */
	{USB_DEVICE(USB8XXX_VID, USB8801_PID_1)},
	{USB_DEVICE_AND_INTERFACE_INFO(USB8XXX_VID, USB8801_PID_2,
				       USB_CLASS_VENDOR_SPEC,
				       USB_SUBCLASS_VENDOR_SPEC, 0xff)},
	/* 8997 */
	{USB_DEVICE(USB8XXX_VID, USB8997_PID_1)},
	{USB_DEVICE_AND_INTERFACE_INFO(USB8XXX_VID, USB8997_PID_2,
				       USB_CLASS_VENDOR_SPEC,
				       USB_SUBCLASS_VENDOR_SPEC, 0xff)},
	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, mwifiex_usb_table);

static int mwifiex_usb_submit_rx_urb(struct urb_context *ctx, int size);

/* This function handles received packet. Necessary action is taken based on
 * cmd/event/data.
 */
static int mwifiex_usb_recv(struct mwifiex_adapter *adapter,
			    struct sk_buff *skb, u8 ep)
{
	u32 recv_type;
	__le32 tmp;
	int ret;

	if (adapter->hs_activated)
		mwifiex_process_hs_config(adapter);

	if (skb->len < INTF_HEADER_LEN) {
		mwifiex_dbg(adapter, ERROR,
			    "%s: invalid skb->len\n", __func__);
		return -1;
	}

	switch (ep) {
	case MWIFIEX_USB_EP_CMD_EVENT:
		mwifiex_dbg(adapter, EVENT,
			    "%s: EP_CMD_EVENT\n", __func__);
		skb_copy_from_linear_data(skb, &tmp, INTF_HEADER_LEN);
		recv_type = le32_to_cpu(tmp);
		skb_pull(skb, INTF_HEADER_LEN);

		switch (recv_type) {
		case MWIFIEX_USB_TYPE_CMD:
			if (skb->len > MWIFIEX_SIZE_OF_CMD_BUFFER) {
				mwifiex_dbg(adapter, ERROR,
					    "CMD: skb->len too large\n");
				ret = -1;
				goto exit_restore_skb;
			} else if (!adapter->curr_cmd) {
				mwifiex_dbg(adapter, WARN, "CMD: no curr_cmd\n");
				if (adapter->ps_state == PS_STATE_SLEEP_CFM) {
					mwifiex_process_sleep_confirm_resp(
							adapter, skb->data,
							skb->len);
					ret = 0;
					goto exit_restore_skb;
				}
				ret = -1;
				goto exit_restore_skb;
			}

			adapter->curr_cmd->resp_skb = skb;
			adapter->cmd_resp_received = true;
			break;
		case MWIFIEX_USB_TYPE_EVENT:
			if (skb->len < sizeof(u32)) {
				mwifiex_dbg(adapter, ERROR,
					    "EVENT: skb->len too small\n");
				ret = -1;
				goto exit_restore_skb;
			}
			skb_copy_from_linear_data(skb, &tmp, sizeof(u32));
			adapter->event_cause = le32_to_cpu(tmp);
			mwifiex_dbg(adapter, EVENT,
				    "event_cause %#x\n", adapter->event_cause);

			if (skb->len > MAX_EVENT_SIZE) {
				mwifiex_dbg(adapter, ERROR,
					    "EVENT: event body too large\n");
				ret = -1;
				goto exit_restore_skb;
			}

			memcpy(adapter->event_body, skb->data +
			       MWIFIEX_EVENT_HEADER_LEN, skb->len);

			adapter->event_received = true;
			adapter->event_skb = skb;
			break;
		default:
			mwifiex_dbg(adapter, ERROR,
				    "unknown recv_type %#x\n", recv_type);
			return -1;
		}
		break;
	case MWIFIEX_USB_EP_DATA:
		mwifiex_dbg(adapter, DATA, "%s: EP_DATA\n", __func__);
		if (skb->len > MWIFIEX_RX_DATA_BUF_SIZE) {
			mwifiex_dbg(adapter, ERROR,
				    "DATA: skb->len too large\n");
			return -1;
		}

		skb_queue_tail(&adapter->rx_data_q, skb);
		adapter->data_received = true;
		atomic_inc(&adapter->rx_pending);
		break;
	default:
		mwifiex_dbg(adapter, ERROR,
			    "%s: unknown endport %#x\n", __func__, ep);
		return -1;
	}

	return -EINPROGRESS;

exit_restore_skb:
	/* The buffer will be reused for further cmds/events */
	skb_push(skb, INTF_HEADER_LEN);

	return ret;
}

static void mwifiex_usb_rx_complete(struct urb *urb)
{
	struct urb_context *context = (struct urb_context *)urb->context;
	struct mwifiex_adapter *adapter = context->adapter;
	struct sk_buff *skb = context->skb;
	struct usb_card_rec *card;
	int recv_length = urb->actual_length;
	int size, status;

	if (!adapter || !adapter->card) {
		pr_err("mwifiex adapter or card structure is not valid\n");
		return;
	}

	card = (struct usb_card_rec *)adapter->card;
	if (card->rx_cmd_ep == context->ep)
		atomic_dec(&card->rx_cmd_urb_pending);
	else
		atomic_dec(&card->rx_data_urb_pending);

	if (recv_length) {
		if (urb->status || (adapter->surprise_removed)) {
			mwifiex_dbg(adapter, ERROR,
				    "URB status is failed: %d\n", urb->status);
			/* Do not free skb in case of command ep */
			if (card->rx_cmd_ep != context->ep)
				dev_kfree_skb_any(skb);
			goto setup_for_next;
		}
		if (skb->len > recv_length)
			skb_trim(skb, recv_length);
		else
			skb_put(skb, recv_length - skb->len);

		status = mwifiex_usb_recv(adapter, skb, context->ep);

		mwifiex_dbg(adapter, INFO,
			    "info: recv_length=%d, status=%d\n",
			    recv_length, status);
		if (status == -EINPROGRESS) {
			mwifiex_queue_main_work(adapter);

			/* urb for data_ep is re-submitted now;
			 * urb for cmd_ep will be re-submitted in callback
			 * mwifiex_usb_recv_complete
			 */
			if (card->rx_cmd_ep == context->ep)
				return;
		} else {
			if (status == -1)
				mwifiex_dbg(adapter, ERROR,
					    "received data processing failed!\n");

			/* Do not free skb in case of command ep */
			if (card->rx_cmd_ep != context->ep)
				dev_kfree_skb_any(skb);
		}
	} else if (urb->status) {
		if (!adapter->is_suspended) {
			mwifiex_dbg(adapter, FATAL,
				    "Card is removed: %d\n", urb->status);
			adapter->surprise_removed = true;
		}
		dev_kfree_skb_any(skb);
		return;
	} else {
		/* Do not free skb in case of command ep */
		if (card->rx_cmd_ep != context->ep)
			dev_kfree_skb_any(skb);

		/* fall through setup_for_next */
	}

setup_for_next:
	if (card->rx_cmd_ep == context->ep)
		size = MWIFIEX_RX_CMD_BUF_SIZE;
	else
		size = MWIFIEX_RX_DATA_BUF_SIZE;

	if (card->rx_cmd_ep == context->ep) {
		mwifiex_usb_submit_rx_urb(context, size);
	} else {
		if (atomic_read(&adapter->rx_pending) <= HIGH_RX_PENDING){
			mwifiex_usb_submit_rx_urb(context, size);
		}else{
			context->skb = NULL;
		}
	}

	return;
}

static void mwifiex_usb_tx_complete(struct urb *urb)
{
	struct urb_context *context = (struct urb_context *)(urb->context);
	struct mwifiex_adapter *adapter = context->adapter;
	struct usb_card_rec *card = adapter->card;
	struct usb_tx_data_port *port;
	int i;

	mwifiex_dbg(adapter, INFO,
		    "%s: status: %d\n", __func__, urb->status);

	if (context->ep == card->tx_cmd_ep) {
		mwifiex_dbg(adapter, CMD,
			    "%s: CMD\n", __func__);
		atomic_dec(&card->tx_cmd_urb_pending);
		adapter->cmd_sent = false;
	} else {
		mwifiex_dbg(adapter, DATA,
			    "%s: DATA\n", __func__);
		for (i = 0; i < MWIFIEX_TX_DATA_PORT; i++) {
			port = &card->port[i];
			if (context->ep == port->tx_data_ep) {
				atomic_dec(&port->tx_data_urb_pending);
				port->block_status = false;
				break;
			}
		}
		adapter->data_sent = false;
		mwifiex_write_data_complete(adapter, context->skb, 0,
					    urb->status ? -1 : 0);
	}

	if (card->mc_resync_flag)
		mwifiex_multi_chan_resync(adapter);

	mwifiex_queue_main_work(adapter);

	return;
}

static int mwifiex_usb_submit_rx_urb(struct urb_context *ctx, int size)
{
	struct mwifiex_adapter *adapter = ctx->adapter;
	struct usb_card_rec *card = (struct usb_card_rec *)adapter->card;

	if (card->rx_cmd_ep != ctx->ep) {
		ctx->skb = dev_alloc_skb(size);
		if (!ctx->skb) {
			mwifiex_dbg(adapter, ERROR,
				    "%s: dev_alloc_skb failed\n", __func__);
			return -ENOMEM;
		}
	}

	usb_fill_bulk_urb(ctx->urb, card->udev,
			  usb_rcvbulkpipe(card->udev, ctx->ep), ctx->skb->data,
			  size, mwifiex_usb_rx_complete, (void *)ctx);

	if (card->rx_cmd_ep == ctx->ep)
		atomic_inc(&card->rx_cmd_urb_pending);
	else
		atomic_inc(&card->rx_data_urb_pending);

	if (usb_submit_urb(ctx->urb, GFP_ATOMIC)) {
		mwifiex_dbg(adapter, ERROR, "usb_submit_urb failed\n");
		dev_kfree_skb_any(ctx->skb);
		ctx->skb = NULL;

		if (card->rx_cmd_ep == ctx->ep)
			atomic_dec(&card->rx_cmd_urb_pending);
		else
			atomic_dec(&card->rx_data_urb_pending);

		return -1;
	}

	return 0;
}

static void mwifiex_usb_free(struct usb_card_rec *card)
{
	struct usb_tx_data_port *port;
	int i, j;

	if (atomic_read(&card->rx_cmd_urb_pending) && card->rx_cmd.urb)
		usb_kill_urb(card->rx_cmd.urb);

	usb_free_urb(card->rx_cmd.urb);
	card->rx_cmd.urb = NULL;

	if (atomic_read(&card->rx_data_urb_pending))
		for (i = 0; i < MWIFIEX_RX_DATA_URB; i++)
			if (card->rx_data_list[i].urb)
				usb_kill_urb(card->rx_data_list[i].urb);

	for (i = 0; i < MWIFIEX_RX_DATA_URB; i++) {
		usb_free_urb(card->rx_data_list[i].urb);
		card->rx_data_list[i].urb = NULL;
	}

	for (i = 0; i < MWIFIEX_TX_DATA_PORT; i++) {
		port = &card->port[i];
		for (j = 0; j < MWIFIEX_TX_DATA_URB; j++) {
			usb_free_urb(port->tx_data_list[j].urb);
			port->tx_data_list[j].urb = NULL;
		}
	}

	usb_free_urb(card->tx_cmd.urb);
	card->tx_cmd.urb = NULL;

	return;
}

/* This function probes an mwifiex device and registers it. It allocates
 * the card structure, initiates the device registration and initialization
 * procedure by adding a logical interface.
 */
static int mwifiex_usb_probe(struct usb_interface *intf,
			     const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_host_interface *iface_desc = intf->cur_altsetting;
	struct usb_endpoint_descriptor *epd;
	int ret, i;
	struct usb_card_rec *card;
	u16 id_vendor, id_product, bcd_device, bcd_usb;

	card = kzalloc(sizeof(struct usb_card_rec), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	id_vendor = le16_to_cpu(udev->descriptor.idVendor);
	id_product = le16_to_cpu(udev->descriptor.idProduct);
	bcd_device = le16_to_cpu(udev->descriptor.bcdDevice);
	bcd_usb = le16_to_cpu(udev->descriptor.bcdUSB);
	pr_debug("info: VID/PID = %X/%X, Boot2 version = %X\n",
		 id_vendor, id_product, bcd_device);

	/* PID_1 is used for firmware downloading only */
	switch (id_product) {
	case USB8766_PID_1:
	case USB8797_PID_1:
	case USB8801_PID_1:
	case USB8997_PID_1:
		card->usb_boot_state = USB8XXX_FW_DNLD;
		break;
	case USB8766_PID_2:
	case USB8797_PID_2:
	case USB8801_PID_2:
	case USB8997_PID_2:
		card->usb_boot_state = USB8XXX_FW_READY;
		break;
	default:
		pr_warn("unknown id_product %#x\n", id_product);
		card->usb_boot_state = USB8XXX_FW_DNLD;
		break;
	}

	card->udev = udev;
	card->intf = intf;

	pr_debug("info: bcdUSB=%#x Device Class=%#x SubClass=%#x Protocol=%#x\n",
		 udev->descriptor.bcdUSB, udev->descriptor.bDeviceClass,
		 udev->descriptor.bDeviceSubClass,
		 udev->descriptor.bDeviceProtocol);

	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		epd = &iface_desc->endpoint[i].desc;
		if (usb_endpoint_dir_in(epd) &&
		    usb_endpoint_num(epd) == MWIFIEX_USB_EP_CMD_EVENT &&
		    usb_endpoint_xfer_bulk(epd)) {
			pr_debug("info: bulk IN: max pkt size: %d, addr: %d\n",
				 le16_to_cpu(epd->wMaxPacketSize),
				 epd->bEndpointAddress);
			card->rx_cmd_ep = usb_endpoint_num(epd);
			atomic_set(&card->rx_cmd_urb_pending, 0);
		}
		if (usb_endpoint_dir_in(epd) &&
		    usb_endpoint_num(epd) == MWIFIEX_USB_EP_DATA &&
		    usb_endpoint_xfer_bulk(epd)) {
			pr_debug("info: bulk IN: max pkt size: %d, addr: %d\n",
				 le16_to_cpu(epd->wMaxPacketSize),
				 epd->bEndpointAddress);
			card->rx_data_ep = usb_endpoint_num(epd);
			atomic_set(&card->rx_data_urb_pending, 0);
		}
		if (usb_endpoint_dir_out(epd) &&
		    usb_endpoint_num(epd) == MWIFIEX_USB_EP_DATA &&
		    usb_endpoint_xfer_bulk(epd)) {
			pr_debug("info: bulk OUT: max pkt size: %d, addr: %d\n",
				 le16_to_cpu(epd->wMaxPacketSize),
				 epd->bEndpointAddress);
			card->port[0].tx_data_ep = usb_endpoint_num(epd);
			atomic_set(&card->port[0].tx_data_urb_pending, 0);
		}
		if (usb_endpoint_dir_out(epd) &&
		    usb_endpoint_num(epd) == MWIFIEX_USB_EP_DATA_CH2 &&
		    usb_endpoint_xfer_bulk(epd)) {
			pr_debug("info: bulk OUT chan2:\t"
				 "max pkt size: %d, addr: %d\n",
				 le16_to_cpu(epd->wMaxPacketSize),
				 epd->bEndpointAddress);
			card->port[1].tx_data_ep = usb_endpoint_num(epd);
			atomic_set(&card->port[1].tx_data_urb_pending, 0);
		}
		if (usb_endpoint_dir_out(epd) &&
		    usb_endpoint_num(epd) == MWIFIEX_USB_EP_CMD_EVENT &&
		    usb_endpoint_xfer_bulk(epd)) {
			pr_debug("info: bulk OUT: max pkt size: %d, addr: %d\n",
				 le16_to_cpu(epd->wMaxPacketSize),
				 epd->bEndpointAddress);
			card->tx_cmd_ep = usb_endpoint_num(epd);
			atomic_set(&card->tx_cmd_urb_pending, 0);
			card->bulk_out_maxpktsize =
					le16_to_cpu(epd->wMaxPacketSize);
		}
	}

	usb_set_intfdata(intf, card);

	ret = mwifiex_add_card(card, &add_remove_card_sem, &usb_ops,
			       MWIFIEX_USB);
	if (ret) {
		pr_err("%s: mwifiex_add_card failed: %d\n", __func__, ret);
		usb_reset_device(udev);
		kfree(card);
		return ret;
	}

	usb_get_dev(udev);

	return 0;
}

/* Kernel needs to suspend all functions separately. Therefore all
 * registered functions must have drivers with suspend and resume
 * methods. Failing that the kernel simply removes the whole card.
 *
 * If already not suspended, this function allocates and sends a
 * 'host sleep activate' request to the firmware and turns off the traffic.
 */
static int mwifiex_usb_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usb_card_rec *card = usb_get_intfdata(intf);
	struct mwifiex_adapter *adapter;
	struct usb_tx_data_port *port;
	int i, j;

	if (!card || !card->adapter) {
		pr_err("%s: card or card->adapter is NULL\n", __func__);
		return 0;
	}
	adapter = card->adapter;

	if (unlikely(adapter->is_suspended))
		mwifiex_dbg(adapter, WARN,
			    "Device already suspended\n");

	mwifiex_enable_hs(adapter);

	/* 'is_suspended' flag indicates device is suspended.
	 * It must be set here before the usb_kill_urb() calls. Reason
	 * is in the complete handlers, urb->status(= -ENOENT) and
	 * this flag is used in combination to distinguish between a
	 * 'suspended' state and a 'disconnect' one.
	 */
	adapter->is_suspended = true;
	adapter->hs_enabling = false;

	if (atomic_read(&card->rx_cmd_urb_pending) && card->rx_cmd.urb)
		usb_kill_urb(card->rx_cmd.urb);

	if (atomic_read(&card->rx_data_urb_pending))
		for (i = 0; i < MWIFIEX_RX_DATA_URB; i++)
			if (card->rx_data_list[i].urb)
				usb_kill_urb(card->rx_data_list[i].urb);

	for (i = 0; i < MWIFIEX_TX_DATA_PORT; i++) {
		port = &card->port[i];
		for (j = 0; j < MWIFIEX_TX_DATA_URB; j++) {
			if (port->tx_data_list[j].urb)
				usb_kill_urb(port->tx_data_list[j].urb);
		}
	}

	if (card->tx_cmd.urb)
		usb_kill_urb(card->tx_cmd.urb);

	return 0;
}

/* Kernel needs to suspend all functions separately. Therefore all
 * registered functions must have drivers with suspend and resume
 * methods. Failing that the kernel simply removes the whole card.
 *
 * If already not resumed, this function turns on the traffic and
 * sends a 'host sleep cancel' request to the firmware.
 */
static int mwifiex_usb_resume(struct usb_interface *intf)
{
	struct usb_card_rec *card = usb_get_intfdata(intf);
	struct mwifiex_adapter *adapter;
	int i;

	if (!card || !card->adapter) {
		pr_err("%s: card or card->adapter is NULL\n", __func__);
		return 0;
	}
	adapter = card->adapter;

	if (unlikely(!adapter->is_suspended)) {
		mwifiex_dbg(adapter, WARN,
			    "Device already resumed\n");
		return 0;
	}

	/* Indicate device resumed. The netdev queue will be resumed only
	 * after the urbs have been re-submitted
	 */
	adapter->is_suspended = false;

	if (!atomic_read(&card->rx_data_urb_pending))
		for (i = 0; i < MWIFIEX_RX_DATA_URB; i++)
			mwifiex_usb_submit_rx_urb(&card->rx_data_list[i],
						  MWIFIEX_RX_DATA_BUF_SIZE);

	if (!atomic_read(&card->rx_cmd_urb_pending)) {
		card->rx_cmd.skb = dev_alloc_skb(MWIFIEX_RX_CMD_BUF_SIZE);
		if (card->rx_cmd.skb)
			mwifiex_usb_submit_rx_urb(&card->rx_cmd,
						  MWIFIEX_RX_CMD_BUF_SIZE);
	}

	/* Disable Host Sleep */
	if (adapter->hs_activated)
		mwifiex_cancel_hs(mwifiex_get_priv(adapter,
						   MWIFIEX_BSS_ROLE_ANY),
				  MWIFIEX_ASYNC_CMD);

	return 0;
}

static void mwifiex_usb_disconnect(struct usb_interface *intf)
{
	struct usb_card_rec *card = usb_get_intfdata(intf);
	struct mwifiex_adapter *adapter;

	if (!card || !card->adapter) {
		pr_err("%s: card or card->adapter is NULL\n", __func__);
		return;
	}

	adapter = card->adapter;
	if (!adapter->priv_num)
		return;

	if (user_rmmod) {
#ifdef CONFIG_PM
		if (adapter->is_suspended)
			mwifiex_usb_resume(intf);
#endif

		mwifiex_deauthenticate_all(adapter);

		mwifiex_init_shutdown_fw(mwifiex_get_priv(adapter,
							  MWIFIEX_BSS_ROLE_ANY),
					 MWIFIEX_FUNC_SHUTDOWN);
	}

	if (adapter->workqueue)
		flush_workqueue(adapter->workqueue);

	mwifiex_usb_free(card);

	mwifiex_dbg(adapter, FATAL,
		    "%s: removing card\n", __func__);
	mwifiex_remove_card(adapter, &add_remove_card_sem);

	usb_set_intfdata(intf, NULL);
	usb_put_dev(interface_to_usbdev(intf));
	kfree(card);

	return;
}

static struct usb_driver mwifiex_usb_driver = {
	.name = "mwifiex_usb",
	.probe = mwifiex_usb_probe,
	.disconnect = mwifiex_usb_disconnect,
	.id_table = mwifiex_usb_table,
	.suspend = mwifiex_usb_suspend,
	.resume = mwifiex_usb_resume,
	.soft_unbind = 1,
};

static int mwifiex_usb_tx_init(struct mwifiex_adapter *adapter)
{
	struct usb_card_rec *card = (struct usb_card_rec *)adapter->card;
	struct usb_tx_data_port *port;
	int i, j;

	card->tx_cmd.adapter = adapter;
	card->tx_cmd.ep = card->tx_cmd_ep;

	card->tx_cmd.urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!card->tx_cmd.urb) {
		mwifiex_dbg(adapter, ERROR,
			    "tx_cmd.urb allocation failed\n");
		return -ENOMEM;
	}

	for (i = 0; i < MWIFIEX_TX_DATA_PORT; i++) {
		port = &card->port[i];
		if (!port->tx_data_ep)
			continue;
		port->tx_data_ix = 0;
		if (port->tx_data_ep == MWIFIEX_USB_EP_DATA)
			port->block_status = false;
		else
			port->block_status = true;
		for (j = 0; j < MWIFIEX_TX_DATA_URB; j++) {
			port->tx_data_list[j].adapter = adapter;
			port->tx_data_list[j].ep = port->tx_data_ep;
			port->tx_data_list[j].urb =
					usb_alloc_urb(0, GFP_KERNEL);
			if (!port->tx_data_list[j].urb) {
				mwifiex_dbg(adapter, ERROR,
					    "urb allocation failed\n");
				return -ENOMEM;
			}
		}
	}

	return 0;
}

static int mwifiex_usb_rx_init(struct mwifiex_adapter *adapter)
{
	struct usb_card_rec *card = (struct usb_card_rec *)adapter->card;
	int i;

	card->rx_cmd.adapter = adapter;
	card->rx_cmd.ep = card->rx_cmd_ep;

	card->rx_cmd.urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!card->rx_cmd.urb) {
		mwifiex_dbg(adapter, ERROR, "rx_cmd.urb allocation failed\n");
		return -ENOMEM;
	}

	card->rx_cmd.skb = dev_alloc_skb(MWIFIEX_RX_CMD_BUF_SIZE);
	if (!card->rx_cmd.skb)
		return -ENOMEM;

	if (mwifiex_usb_submit_rx_urb(&card->rx_cmd, MWIFIEX_RX_CMD_BUF_SIZE))
		return -1;

	for (i = 0; i < MWIFIEX_RX_DATA_URB; i++) {
		card->rx_data_list[i].adapter = adapter;
		card->rx_data_list[i].ep = card->rx_data_ep;

		card->rx_data_list[i].urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!card->rx_data_list[i].urb) {
			mwifiex_dbg(adapter, ERROR,
				    "rx_data_list[] urb allocation failed\n");
			return -1;
		}
		if (mwifiex_usb_submit_rx_urb(&card->rx_data_list[i],
					      MWIFIEX_RX_DATA_BUF_SIZE))
			return -1;
	}

	return 0;
}

static int mwifiex_write_data_sync(struct mwifiex_adapter *adapter, u8 *pbuf,
				   u32 *len, u8 ep, u32 timeout)
{
	struct usb_card_rec *card = adapter->card;
	int actual_length, ret;

	if (!(*len % card->bulk_out_maxpktsize))
		(*len)++;

	/* Send the data block */
	ret = usb_bulk_msg(card->udev, usb_sndbulkpipe(card->udev, ep), pbuf,
			   *len, &actual_length, timeout);
	if (ret) {
		mwifiex_dbg(adapter, ERROR,
			    "usb_bulk_msg for tx failed: %d\n", ret);
		return ret;
	}

	*len = actual_length;

	return ret;
}

static int mwifiex_read_data_sync(struct mwifiex_adapter *adapter, u8 *pbuf,
				  u32 *len, u8 ep, u32 timeout)
{
	struct usb_card_rec *card = adapter->card;
	int actual_length, ret;

	/* Receive the data response */
	ret = usb_bulk_msg(card->udev, usb_rcvbulkpipe(card->udev, ep), pbuf,
			   *len, &actual_length, timeout);
	if (ret) {
		mwifiex_dbg(adapter, ERROR,
			    "usb_bulk_msg for rx failed: %d\n", ret);
		return ret;
	}

	*len = actual_length;

	return ret;
}

static void mwifiex_usb_port_resync(struct mwifiex_adapter *adapter)
{
	struct usb_card_rec *card = adapter->card;
	u8 active_port = MWIFIEX_USB_EP_DATA;
	struct mwifiex_private *priv = NULL;
	int i;

	if (adapter->usb_mc_status) {
		for (i = 0; i < adapter->priv_num; i++) {
			priv = adapter->priv[i];
			if (!priv)
				continue;
			if ((priv->bss_role == MWIFIEX_BSS_ROLE_UAP &&
			     !priv->bss_started) ||
			    (priv->bss_role == MWIFIEX_BSS_ROLE_STA &&
			     !priv->media_connected))
				priv->usb_port = MWIFIEX_USB_EP_DATA;
		}
		for (i = 0; i < MWIFIEX_TX_DATA_PORT; i++)
			card->port[i].block_status = false;
	} else {
		for (i = 0; i < adapter->priv_num; i++) {
			priv = adapter->priv[i];
			if (!priv)
				continue;
			if ((priv->bss_role == MWIFIEX_BSS_ROLE_UAP &&
			     priv->bss_started) ||
			    (priv->bss_role == MWIFIEX_BSS_ROLE_STA &&
			     priv->media_connected)) {
				active_port = priv->usb_port;
				break;
			}
		}
		for (i = 0; i < adapter->priv_num; i++) {
			priv = adapter->priv[i];
			if (priv)
				priv->usb_port = active_port;
		}
		for (i = 0; i < MWIFIEX_TX_DATA_PORT; i++) {
			if (active_port == card->port[i].tx_data_ep)
				card->port[i].block_status = false;
			else
				card->port[i].block_status = true;
		}
	}
}

static bool mwifiex_usb_is_port_ready(struct mwifiex_private *priv)
{
	struct usb_card_rec *card = priv->adapter->card;
	int idx;

	for (idx = 0; idx < MWIFIEX_TX_DATA_PORT; idx++) {
		if (priv->usb_port == card->port[idx].tx_data_ep)
			return !card->port[idx].block_status;
	}

	return false;
}

static inline u8 mwifiex_usb_data_sent(struct mwifiex_adapter *adapter)
{
	struct usb_card_rec *card = adapter->card;
	int i;

	for (i = 0; i < MWIFIEX_TX_DATA_PORT; i++)
		if (!card->port[i].block_status)
			return false;

	return true;
}

/* This function write a command/data packet to card. */
static int mwifiex_usb_host_to_card(struct mwifiex_adapter *adapter, u8 ep,
				    struct sk_buff *skb,
				    struct mwifiex_tx_param *tx_param)
{
	struct usb_card_rec *card = adapter->card;
	struct urb_context *context = NULL;
	struct usb_tx_data_port *port = NULL;
	u8 *data = (u8 *)skb->data;
	struct urb *tx_urb;
	int idx, ret;

	if (adapter->is_suspended) {
		mwifiex_dbg(adapter, ERROR,
			    "%s: not allowed while suspended\n", __func__);
		return -1;
	}

	if (adapter->surprise_removed) {
		mwifiex_dbg(adapter, ERROR, "%s: device removed\n", __func__);
		return -1;
	}

	mwifiex_dbg(adapter, INFO, "%s: ep=%d\n", __func__, ep);

	if (ep == card->tx_cmd_ep) {
		context = &card->tx_cmd;
	} else {
		for (idx = 0; idx < MWIFIEX_TX_DATA_PORT; idx++) {
			if (ep == card->port[idx].tx_data_ep) {
				port = &card->port[idx];
				if (atomic_read(&port->tx_data_urb_pending)
				    >= MWIFIEX_TX_DATA_URB) {
					port->block_status = true;
					ret = -EBUSY;
					goto done;
				}
				if (port->tx_data_ix >= MWIFIEX_TX_DATA_URB)
					port->tx_data_ix = 0;
				context =
					&port->tx_data_list[port->tx_data_ix++];
				break;
			}
		}
		if (!port) {
			mwifiex_dbg(adapter, ERROR, "Wrong usb tx data port\n");
			return -1;
		}
	}

	context->adapter = adapter;
	context->ep = ep;
	context->skb = skb;
	tx_urb = context->urb;

	usb_fill_bulk_urb(tx_urb, card->udev, usb_sndbulkpipe(card->udev, ep),
			  data, skb->len, mwifiex_usb_tx_complete,
			  (void *)context);

	tx_urb->transfer_flags |= URB_ZERO_PACKET;

	if (ep == card->tx_cmd_ep)
		atomic_inc(&card->tx_cmd_urb_pending);
	else
		atomic_inc(&port->tx_data_urb_pending);

	if (usb_submit_urb(tx_urb, GFP_ATOMIC)) {
		mwifiex_dbg(adapter, ERROR,
			    "%s: usb_submit_urb failed\n", __func__);
		if (ep == card->tx_cmd_ep) {
			atomic_dec(&card->tx_cmd_urb_pending);
		} else {
			atomic_dec(&port->tx_data_urb_pending);
			port->block_status = false;
			if (port->tx_data_ix)
				port->tx_data_ix--;
			else
				port->tx_data_ix = MWIFIEX_TX_DATA_URB;
		}

		return -1;
	} else {
		if (ep != card->tx_cmd_ep &&
		    atomic_read(&port->tx_data_urb_pending) ==
							MWIFIEX_TX_DATA_URB) {
			port->block_status = true;
			ret = -ENOSR;
			goto done;
		}
	}

	return -EINPROGRESS;

done:
	if (ep != card->tx_cmd_ep)
		adapter->data_sent = mwifiex_usb_data_sent(adapter);

	return ret;
}

/* This function register usb device and initialize parameter. */
static int mwifiex_register_dev(struct mwifiex_adapter *adapter)
{
	struct usb_card_rec *card = (struct usb_card_rec *)adapter->card;

	card->adapter = adapter;
	adapter->dev = &card->udev->dev;

	switch (le16_to_cpu(card->udev->descriptor.idProduct)) {
	case USB8997_PID_1:
	case USB8997_PID_2:
		adapter->tx_buf_size = MWIFIEX_TX_DATA_BUF_SIZE_4K;
		strcpy(adapter->fw_name, USB8997_DEFAULT_FW_NAME);
		adapter->ext_scan = true;
		break;
	case USB8766_PID_1:
	case USB8766_PID_2:
		adapter->tx_buf_size = MWIFIEX_TX_DATA_BUF_SIZE_2K;
		strcpy(adapter->fw_name, USB8766_DEFAULT_FW_NAME);
		adapter->ext_scan = true;
		break;
	case USB8801_PID_1:
	case USB8801_PID_2:
		adapter->tx_buf_size = MWIFIEX_TX_DATA_BUF_SIZE_2K;
		strcpy(adapter->fw_name, USB8801_DEFAULT_FW_NAME);
		adapter->ext_scan = false;
		break;
	case USB8797_PID_1:
	case USB8797_PID_2:
	default:
		adapter->tx_buf_size = MWIFIEX_TX_DATA_BUF_SIZE_2K;
		strcpy(adapter->fw_name, USB8797_DEFAULT_FW_NAME);
		break;
	}

	adapter->usb_mc_status = false;
	adapter->usb_mc_setup = false;

	return 0;
}

static void mwifiex_unregister_dev(struct mwifiex_adapter *adapter)
{
	struct usb_card_rec *card = (struct usb_card_rec *)adapter->card;

	card->adapter = NULL;
}

static int mwifiex_prog_fw_w_helper(struct mwifiex_adapter *adapter,
				    struct mwifiex_fw_image *fw)
{
	int ret = 0;
	u8 *firmware = fw->fw_buf, *recv_buff;
	u32 retries = USB8XXX_FW_MAX_RETRY, dlen;
	u32 fw_seqnum = 0, tlen = 0, dnld_cmd = 0;
	struct fw_data *fwdata;
	struct fw_sync_header sync_fw;
	u8 check_winner = 1;

	if (!firmware) {
		mwifiex_dbg(adapter, ERROR,
			    "No firmware image found! Terminating download\n");
		ret = -1;
		goto fw_exit;
	}

	/* Allocate memory for transmit */
	fwdata = kzalloc(FW_DNLD_TX_BUF_SIZE, GFP_KERNEL);
	if (!fwdata) {
		ret = -ENOMEM;
		goto fw_exit;
	}

	/* Allocate memory for receive */
	recv_buff = kzalloc(FW_DNLD_RX_BUF_SIZE, GFP_KERNEL);
	if (!recv_buff)
		goto cleanup;

	do {
		/* Send pseudo data to check winner status first */
		if (check_winner) {
			memset(&fwdata->fw_hdr, 0, sizeof(struct fw_header));
			dlen = 0;
		} else {
			/* copy the header of the fw_data to get the length */
			memcpy(&fwdata->fw_hdr, &firmware[tlen],
			       sizeof(struct fw_header));

			dlen = le32_to_cpu(fwdata->fw_hdr.data_len);
			dnld_cmd = le32_to_cpu(fwdata->fw_hdr.dnld_cmd);
			tlen += sizeof(struct fw_header);

			memcpy(fwdata->data, &firmware[tlen], dlen);

			fwdata->seq_num = cpu_to_le32(fw_seqnum);
			tlen += dlen;
		}

		/* If the send/receive fails or CRC occurs then retry */
		while (retries--) {
			u8 *buf = (u8 *)fwdata;
			u32 len = FW_DATA_XMIT_SIZE;

			/* send the firmware block */
			ret = mwifiex_write_data_sync(adapter, buf, &len,
						MWIFIEX_USB_EP_CMD_EVENT,
						MWIFIEX_USB_TIMEOUT);
			if (ret) {
				mwifiex_dbg(adapter, ERROR,
					    "write_data_sync: failed: %d\n",
					    ret);
				continue;
			}

			buf = recv_buff;
			len = FW_DNLD_RX_BUF_SIZE;

			/* Receive the firmware block response */
			ret = mwifiex_read_data_sync(adapter, buf, &len,
						MWIFIEX_USB_EP_CMD_EVENT,
						MWIFIEX_USB_TIMEOUT);
			if (ret) {
				mwifiex_dbg(adapter, ERROR,
					    "read_data_sync: failed: %d\n",
					    ret);
				continue;
			}

			memcpy(&sync_fw, recv_buff,
			       sizeof(struct fw_sync_header));

			/* check 1st firmware block resp for highest bit set */
			if (check_winner) {
				if (le32_to_cpu(sync_fw.cmd) & 0x80000000) {
					mwifiex_dbg(adapter, WARN,
						    "USB is not the winner %#x\n",
						    sync_fw.cmd);

					/* returning success */
					ret = 0;
					goto cleanup;
				}

				mwifiex_dbg(adapter, MSG,
					    "start to download FW...\n");

				check_winner = 0;
				break;
			}

			/* check the firmware block response for CRC errors */
			if (sync_fw.cmd) {
				mwifiex_dbg(adapter, ERROR,
					    "FW received block with CRC %#x\n",
					    sync_fw.cmd);
				ret = -1;
				continue;
			}

			retries = USB8XXX_FW_MAX_RETRY;
			break;
		}
		fw_seqnum++;
	} while ((dnld_cmd != FW_HAS_LAST_BLOCK) && retries);

cleanup:
	mwifiex_dbg(adapter, MSG,
		    "info: FW download over, size %d bytes\n", tlen);

	kfree(recv_buff);
	kfree(fwdata);

	if (retries)
		ret = 0;
fw_exit:
	return ret;
}

static int mwifiex_usb_dnld_fw(struct mwifiex_adapter *adapter,
			struct mwifiex_fw_image *fw)
{
	int ret;
	struct usb_card_rec *card = (struct usb_card_rec *)adapter->card;

	if (card->usb_boot_state == USB8XXX_FW_DNLD) {
		ret = mwifiex_prog_fw_w_helper(adapter, fw);
		if (ret)
			return -1;

		/* Boot state changes after successful firmware download */
		if (card->usb_boot_state == USB8XXX_FW_DNLD)
			return -1;
	}

	ret = mwifiex_usb_rx_init(adapter);
	if (!ret)
		ret = mwifiex_usb_tx_init(adapter);

	return ret;
}

static void mwifiex_submit_rx_urb(struct mwifiex_adapter *adapter, u8 ep)
{
	struct usb_card_rec *card = (struct usb_card_rec *)adapter->card;

	skb_push(card->rx_cmd.skb, INTF_HEADER_LEN);
	if ((ep == card->rx_cmd_ep) &&
	    (!atomic_read(&card->rx_cmd_urb_pending)))
		mwifiex_usb_submit_rx_urb(&card->rx_cmd,
					  MWIFIEX_RX_CMD_BUF_SIZE);

	return;
}

static int mwifiex_usb_cmd_event_complete(struct mwifiex_adapter *adapter,
				       struct sk_buff *skb)
{
	mwifiex_submit_rx_urb(adapter, MWIFIEX_USB_EP_CMD_EVENT);

	return 0;
}

/* This function wakes up the card. */
static int mwifiex_pm_wakeup_card(struct mwifiex_adapter *adapter)
{
	/* Simulation of HS_AWAKE event */
	adapter->pm_wakeup_fw_try = false;
	del_timer(&adapter->wakeup_timer);
	adapter->pm_wakeup_card_req = false;
	adapter->ps_state = PS_STATE_AWAKE;

	return 0;
}

static void mwifiex_usb_submit_rem_rx_urbs(struct mwifiex_adapter *adapter)
{
	struct usb_card_rec *card = (struct usb_card_rec *)adapter->card;
	int i;
	struct urb_context *ctx;

	for (i = 0; i < MWIFIEX_RX_DATA_URB; i++) {
		if (card->rx_data_list[i].skb)
			continue;
		ctx = &card->rx_data_list[i];
		mwifiex_usb_submit_rx_urb(ctx, MWIFIEX_RX_DATA_BUF_SIZE);
	}
}

/* This function is called after the card has woken up. */
static inline int
mwifiex_pm_wakeup_card_complete(struct mwifiex_adapter *adapter)
{
	return 0;
}

static struct mwifiex_if_ops usb_ops = {
	.register_dev =		mwifiex_register_dev,
	.unregister_dev =	mwifiex_unregister_dev,
	.wakeup =		mwifiex_pm_wakeup_card,
	.wakeup_complete =	mwifiex_pm_wakeup_card_complete,

	/* USB specific */
	.dnld_fw =		mwifiex_usb_dnld_fw,
	.cmdrsp_complete =	mwifiex_usb_cmd_event_complete,
	.event_complete =	mwifiex_usb_cmd_event_complete,
	.host_to_card =		mwifiex_usb_host_to_card,
	.submit_rem_rx_urbs =	mwifiex_usb_submit_rem_rx_urbs,
	.multi_port_resync =	mwifiex_usb_port_resync,
	.is_port_ready =	mwifiex_usb_is_port_ready,
};

/* This function initializes the USB driver module.
 *
 * This initiates the semaphore and registers the device with
 * USB bus.
 */
static int mwifiex_usb_init_module(void)
{
	int ret;

	pr_debug("Marvell USB8797 Driver\n");

	sema_init(&add_remove_card_sem, 1);

	ret = usb_register(&mwifiex_usb_driver);
	if (ret)
		pr_err("Driver register failed!\n");
	else
		pr_debug("info: Driver registered successfully!\n");

	return ret;
}

/* This function cleans up the USB driver.
 *
 * The following major steps are followed in .disconnect for cleanup:
 *      - Resume the device if its suspended
 *      - Disconnect the device if connected
 *      - Shutdown the firmware
 *      - Unregister the device from USB bus.
 */
static void mwifiex_usb_cleanup_module(void)
{
	if (!down_interruptible(&add_remove_card_sem))
		up(&add_remove_card_sem);

	/* set the flag as user is removing this module */
	user_rmmod = 1;

	usb_deregister(&mwifiex_usb_driver);
}

module_init(mwifiex_usb_init_module);
module_exit(mwifiex_usb_cleanup_module);

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION("Marvell WiFi-Ex USB Driver version" USB_VERSION);
MODULE_VERSION(USB_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_FIRMWARE(USB8766_DEFAULT_FW_NAME);
MODULE_FIRMWARE(USB8797_DEFAULT_FW_NAME);
MODULE_FIRMWARE(USB8801_DEFAULT_FW_NAME);
MODULE_FIRMWARE(USB8997_DEFAULT_FW_NAME);
