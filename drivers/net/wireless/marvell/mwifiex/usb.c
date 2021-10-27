/*
 * NXP Wireless LAN device driver: USB specific handling
 *
 * Copyright 2011-2020 NXP
 *
 * This software file (the "File") is distributed by NXP
 * under the terms of the GNU General Public License Version 2, June 1991
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

static struct mwifiex_if_ops usb_ops;

static const struct usb_device_id mwifiex_usb_table[] = {
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
		if (urb->status ||
		    test_bit(MWIFIEX_SURPRISE_REMOVED, &adapter->work_flags)) {
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
		if (!test_bit(MWIFIEX_IS_SUSPENDED, &adapter->work_flags)) {
			mwifiex_dbg(adapter, FATAL,
				    "Card is removed: %d\n", urb->status);
			set_bit(MWIFIEX_SURPRISE_REMOVED, &adapter->work_flags);
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
		if (atomic_read(&adapter->rx_pending) <= HIGH_RX_PENDING) {
			mwifiex_usb_submit_rx_urb(context, size);
		} else {
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
		mwifiex_write_data_complete(adapter, context->skb, 0,
					    urb->status ? -1 : 0);
		for (i = 0; i < MWIFIEX_TX_DATA_PORT; i++) {
			port = &card->port[i];
			if (context->ep == port->tx_data_ep) {
				atomic_dec(&port->tx_data_urb_pending);
				port->block_status = false;
				break;
			}
		}
		adapter->data_sent = false;
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

	if (test_bit(MWIFIEX_IS_SUSPENDED, &adapter->work_flags)) {
		if (card->rx_cmd_ep == ctx->ep) {
			mwifiex_dbg(adapter, INFO, "%s: free rx_cmd skb\n",
				    __func__);
			dev_kfree_skb_any(ctx->skb);
			ctx->skb = NULL;
		}
		mwifiex_dbg(adapter, ERROR,
			    "%s: card removed/suspended, EP %d rx_cmd URB submit skipped\n",
			    __func__, ctx->ep);
		return -1;
	}

	if (card->rx_cmd_ep != ctx->ep) {
		ctx->skb = dev_alloc_skb(size);
		if (!ctx->skb) {
			mwifiex_dbg(adapter, ERROR,
				    "%s: dev_alloc_skb failed\n", __func__);
			return -ENOMEM;
		}
	}

	if (card->rx_cmd_ep == ctx->ep &&
	    card->rx_cmd_ep_type == USB_ENDPOINT_XFER_INT)
		usb_fill_int_urb(ctx->urb, card->udev,
				 usb_rcvintpipe(card->udev, ctx->ep),
				 ctx->skb->data, size, mwifiex_usb_rx_complete,
				 (void *)ctx, card->rx_cmd_interval);
	else
		usb_fill_bulk_urb(ctx->urb, card->udev,
				  usb_rcvbulkpipe(card->udev, ctx->ep),
				  ctx->skb->data, size, mwifiex_usb_rx_complete,
				  (void *)ctx);

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
			usb_kill_urb(port->tx_data_list[j].urb);
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
	u16 id_vendor, id_product, bcd_device;

	card = devm_kzalloc(&intf->dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	init_completion(&card->fw_done);

	id_vendor = le16_to_cpu(udev->descriptor.idVendor);
	id_product = le16_to_cpu(udev->descriptor.idProduct);
	bcd_device = le16_to_cpu(udev->descriptor.bcdDevice);
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
		 le16_to_cpu(udev->descriptor.bcdUSB),
		 udev->descriptor.bDeviceClass,
		 udev->descriptor.bDeviceSubClass,
		 udev->descriptor.bDeviceProtocol);

	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		epd = &iface_desc->endpoint[i].desc;
		if (usb_endpoint_dir_in(epd) &&
		    usb_endpoint_num(epd) == MWIFIEX_USB_EP_CMD_EVENT &&
		    (usb_endpoint_xfer_bulk(epd) ||
		     usb_endpoint_xfer_int(epd))) {
			card->rx_cmd_ep_type = usb_endpoint_type(epd);
			card->rx_cmd_interval = epd->bInterval;
			pr_debug("info: Rx CMD/EVT:: max pkt size: %d, addr: %d, ep_type: %d\n",
				 le16_to_cpu(epd->wMaxPacketSize),
				 epd->bEndpointAddress, card->rx_cmd_ep_type);
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
		    (usb_endpoint_xfer_bulk(epd) ||
		     usb_endpoint_xfer_int(epd))) {
			card->tx_cmd_ep_type = usb_endpoint_type(epd);
			card->tx_cmd_interval = epd->bInterval;
			pr_debug("info: bulk OUT: max pkt size: %d, addr: %d\n",
				 le16_to_cpu(epd->wMaxPacketSize),
				 epd->bEndpointAddress);
			pr_debug("info: Tx CMD:: max pkt size: %d, addr: %d, ep_type: %d\n",
				 le16_to_cpu(epd->wMaxPacketSize),
				 epd->bEndpointAddress, card->tx_cmd_ep_type);
			card->tx_cmd_ep = usb_endpoint_num(epd);
			atomic_set(&card->tx_cmd_urb_pending, 0);
			card->bulk_out_maxpktsize =
					le16_to_cpu(epd->wMaxPacketSize);
		}
	}

	switch (card->usb_boot_state) {
	case USB8XXX_FW_DNLD:
		/* Reject broken descriptors. */
		if (!card->rx_cmd_ep || !card->tx_cmd_ep)
			return -ENODEV;
		if (card->bulk_out_maxpktsize == 0)
			return -ENODEV;
		break;
	case USB8XXX_FW_READY:
		/* Assume the driver can handle missing endpoints for now. */
		break;
	default:
		WARN_ON(1);
		return -ENODEV;
	}

	usb_set_intfdata(intf, card);

	ret = mwifiex_add_card(card, &card->fw_done, &usb_ops,
			       MWIFIEX_USB, &card->udev->dev);
	if (ret) {
		pr_err("%s: mwifiex_add_card failed: %d\n", __func__, ret);
		usb_reset_device(udev);
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

	/* Might still be loading firmware */
	wait_for_completion(&card->fw_done);

	adapter = card->adapter;
	if (!adapter) {
		dev_err(&intf->dev, "card is not valid\n");
		return 0;
	}

	if (unlikely(test_bit(MWIFIEX_IS_SUSPENDED, &adapter->work_flags)))
		mwifiex_dbg(adapter, WARN,
			    "Device already suspended\n");

	/* Enable the Host Sleep */
	if (!mwifiex_enable_hs(adapter)) {
		mwifiex_dbg(adapter, ERROR,
			    "cmd: failed to suspend\n");
		clear_bit(MWIFIEX_IS_HS_ENABLING, &adapter->work_flags);
		return -EFAULT;
	}


	/* 'MWIFIEX_IS_SUSPENDED' bit indicates device is suspended.
	 * It must be set here before the usb_kill_urb() calls. Reason
	 * is in the complete handlers, urb->status(= -ENOENT) and
	 * this flag is used in combination to distinguish between a
	 * 'suspended' state and a 'disconnect' one.
	 */
	set_bit(MWIFIEX_IS_SUSPENDED, &adapter->work_flags);
	clear_bit(MWIFIEX_IS_HS_ENABLING, &adapter->work_flags);

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

	if (!card->adapter) {
		dev_err(&intf->dev, "%s: card->adapter is NULL\n",
			__func__);
		return 0;
	}
	adapter = card->adapter;

	if (unlikely(!test_bit(MWIFIEX_IS_SUSPENDED, &adapter->work_flags))) {
		mwifiex_dbg(adapter, WARN,
			    "Device already resumed\n");
		return 0;
	}

	/* Indicate device resumed. The netdev queue will be resumed only
	 * after the urbs have been re-submitted
	 */
	clear_bit(MWIFIEX_IS_SUSPENDED, &adapter->work_flags);

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

	wait_for_completion(&card->fw_done);

	adapter = card->adapter;
	if (!adapter || !adapter->priv_num)
		return;

	if (card->udev->state != USB_STATE_NOTATTACHED && !adapter->mfg_mode) {
		mwifiex_deauthenticate_all(adapter);

		mwifiex_init_shutdown_fw(mwifiex_get_priv(adapter,
							  MWIFIEX_BSS_ROLE_ANY),
					 MWIFIEX_FUNC_SHUTDOWN);
	}

	mwifiex_dbg(adapter, FATAL,
		    "%s: removing card\n", __func__);
	mwifiex_remove_card(adapter);

	usb_put_dev(interface_to_usbdev(intf));
}

static void mwifiex_usb_coredump(struct device *dev)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_card_rec *card = usb_get_intfdata(intf);

	mwifiex_fw_dump_event(mwifiex_get_priv(card->adapter,
					       MWIFIEX_BSS_ROLE_ANY));
}

static struct usb_driver mwifiex_usb_driver = {
	.name = "mwifiex_usb",
	.probe = mwifiex_usb_probe,
	.disconnect = mwifiex_usb_disconnect,
	.id_table = mwifiex_usb_table,
	.suspend = mwifiex_usb_suspend,
	.resume = mwifiex_usb_resume,
	.soft_unbind = 1,
	.drvwrap.driver = {
		.coredump = mwifiex_usb_coredump,
	},
};

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

static int mwifiex_usb_construct_send_urb(struct mwifiex_adapter *adapter,
					  struct usb_tx_data_port *port, u8 ep,
					  struct urb_context *context,
					  struct sk_buff *skb_send)
{
	struct usb_card_rec *card = adapter->card;
	int ret = -EINPROGRESS;
	struct urb *tx_urb;

	context->adapter = adapter;
	context->ep = ep;
	context->skb = skb_send;
	tx_urb = context->urb;

	if (ep == card->tx_cmd_ep &&
	    card->tx_cmd_ep_type == USB_ENDPOINT_XFER_INT)
		usb_fill_int_urb(tx_urb, card->udev,
				 usb_sndintpipe(card->udev, ep), skb_send->data,
				 skb_send->len, mwifiex_usb_tx_complete,
				 (void *)context, card->tx_cmd_interval);
	else
		usb_fill_bulk_urb(tx_urb, card->udev,
				  usb_sndbulkpipe(card->udev, ep),
				  skb_send->data, skb_send->len,
				  mwifiex_usb_tx_complete, (void *)context);

	tx_urb->transfer_flags |= URB_ZERO_PACKET;

	if (ep == card->tx_cmd_ep)
		atomic_inc(&card->tx_cmd_urb_pending);
	else
		atomic_inc(&port->tx_data_urb_pending);

	if (ep != card->tx_cmd_ep &&
	    atomic_read(&port->tx_data_urb_pending) ==
					MWIFIEX_TX_DATA_URB) {
		port->block_status = true;
		adapter->data_sent = mwifiex_usb_data_sent(adapter);
		ret = -ENOSR;
	}

	if (usb_submit_urb(tx_urb, GFP_ATOMIC)) {
		mwifiex_dbg(adapter, ERROR,
			    "%s: usb_submit_urb failed\n", __func__);
		if (ep == card->tx_cmd_ep) {
			atomic_dec(&card->tx_cmd_urb_pending);
		} else {
			atomic_dec(&port->tx_data_urb_pending);
			port->block_status = false;
			adapter->data_sent = false;
			if (port->tx_data_ix)
				port->tx_data_ix--;
			else
				port->tx_data_ix = MWIFIEX_TX_DATA_URB;
		}
		ret = -1;
	}

	return ret;
}

static int mwifiex_usb_prepare_tx_aggr_skb(struct mwifiex_adapter *adapter,
					   struct usb_tx_data_port *port,
					   struct sk_buff **skb_send)
{
	struct sk_buff *skb_aggr, *skb_tmp;
	u8 *payload, pad;
	u16 align = adapter->bus_aggr.tx_aggr_align;
	struct mwifiex_txinfo *tx_info = NULL;
	bool is_txinfo_set = false;

	/* Packets in aggr_list will be send in either skb_aggr or
	 * write complete, delete the tx_aggr timer
	 */
	if (port->tx_aggr.timer_cnxt.is_hold_timer_set) {
		del_timer(&port->tx_aggr.timer_cnxt.hold_timer);
		port->tx_aggr.timer_cnxt.is_hold_timer_set = false;
		port->tx_aggr.timer_cnxt.hold_tmo_msecs = 0;
	}

	skb_aggr = mwifiex_alloc_dma_align_buf(port->tx_aggr.aggr_len,
					       GFP_ATOMIC);
	if (!skb_aggr) {
		mwifiex_dbg(adapter, ERROR,
			    "%s: alloc skb_aggr failed\n", __func__);

		while ((skb_tmp = skb_dequeue(&port->tx_aggr.aggr_list)))
			mwifiex_write_data_complete(adapter, skb_tmp, 0, -1);

		port->tx_aggr.aggr_num = 0;
		port->tx_aggr.aggr_len = 0;
		return -EBUSY;
	}

	tx_info = MWIFIEX_SKB_TXCB(skb_aggr);
	memset(tx_info, 0, sizeof(*tx_info));

	while ((skb_tmp = skb_dequeue(&port->tx_aggr.aggr_list))) {
		/* padding for aligning next packet header*/
		pad = (align - (skb_tmp->len & (align - 1))) % align;
		payload = skb_put(skb_aggr, skb_tmp->len + pad);
		memcpy(payload, skb_tmp->data, skb_tmp->len);
		if (skb_queue_empty(&port->tx_aggr.aggr_list)) {
			/* do not padding for last packet*/
			*(u16 *)payload = cpu_to_le16(skb_tmp->len);
			*(u16 *)&payload[2] =
				cpu_to_le16(MWIFIEX_TYPE_AGGR_DATA_V2 | 0x80);
			skb_trim(skb_aggr, skb_aggr->len - pad);
		} else {
			/* add aggregation interface header */
			*(u16 *)payload = cpu_to_le16(skb_tmp->len + pad);
			*(u16 *)&payload[2] =
				cpu_to_le16(MWIFIEX_TYPE_AGGR_DATA_V2);
		}

		if (!is_txinfo_set) {
			tx_info->bss_num = MWIFIEX_SKB_TXCB(skb_tmp)->bss_num;
			tx_info->bss_type = MWIFIEX_SKB_TXCB(skb_tmp)->bss_type;
			is_txinfo_set = true;
		}

		port->tx_aggr.aggr_num--;
		port->tx_aggr.aggr_len -= (skb_tmp->len + pad);
		mwifiex_write_data_complete(adapter, skb_tmp, 0, 0);
	}

	tx_info->pkt_len = skb_aggr->len -
			(sizeof(struct txpd) + adapter->intf_hdr_len);
	tx_info->flags |= MWIFIEX_BUF_FLAG_AGGR_PKT;

	port->tx_aggr.aggr_num = 0;
	port->tx_aggr.aggr_len = 0;
	*skb_send = skb_aggr;

	return 0;
}

/* This function prepare data packet to be send under usb tx aggregation
 * protocol, check current usb aggregation status, link packet to aggrgation
 * list if possible, work flow as below:
 * (1) if only 1 packet available, add usb tx aggregation header and send.
 * (2) if packet is able to aggregated, link it to current aggregation list.
 * (3) if packet is not able to aggregated, aggregate and send exist packets
 *     in aggrgation list. Then, link packet in the list if there is more
 *     packet in transmit queue, otherwise try to transmit single packet.
 */
static int mwifiex_usb_aggr_tx_data(struct mwifiex_adapter *adapter, u8 ep,
				    struct sk_buff *skb,
				    struct mwifiex_tx_param *tx_param,
				    struct usb_tx_data_port *port)
{
	u8 *payload, pad;
	u16 align = adapter->bus_aggr.tx_aggr_align;
	struct sk_buff *skb_send = NULL;
	struct urb_context *context = NULL;
	struct txpd *local_tx_pd =
		(struct txpd *)((u8 *)skb->data + adapter->intf_hdr_len);
	u8 f_send_aggr_buf = 0;
	u8 f_send_cur_buf = 0;
	u8 f_precopy_cur_buf = 0;
	u8 f_postcopy_cur_buf = 0;
	u32 timeout;
	int ret;

	/* padding to ensure each packet alginment */
	pad = (align - (skb->len & (align - 1))) % align;

	if (tx_param && tx_param->next_pkt_len) {
		/* next packet available in tx queue*/
		if (port->tx_aggr.aggr_len + skb->len + pad >
		    adapter->bus_aggr.tx_aggr_max_size) {
			f_send_aggr_buf = 1;
			f_postcopy_cur_buf = 1;
		} else {
			/* current packet could be aggregated*/
			f_precopy_cur_buf = 1;

			if (port->tx_aggr.aggr_len + skb->len + pad +
			    tx_param->next_pkt_len >
			    adapter->bus_aggr.tx_aggr_max_size ||
			    port->tx_aggr.aggr_num + 2 >
			    adapter->bus_aggr.tx_aggr_max_num) {
			    /* next packet could not be aggregated
			     * send current aggregation buffer
			     */
				f_send_aggr_buf = 1;
			}
		}
	} else {
		/* last packet in tx queue */
		if (port->tx_aggr.aggr_num > 0) {
			/* pending packets in aggregation buffer*/
			if (port->tx_aggr.aggr_len + skb->len + pad >
			    adapter->bus_aggr.tx_aggr_max_size) {
				/* current packet not be able to aggregated,
				 * send aggr buffer first, then send packet.
				 */
				f_send_cur_buf = 1;
			} else {
				/* last packet, Aggregation and send */
				f_precopy_cur_buf = 1;
			}

			f_send_aggr_buf = 1;
		} else {
			/* no pending packets in aggregation buffer,
			 * send current packet immediately
			 */
			 f_send_cur_buf = 1;
		}
	}

	if (local_tx_pd->flags & MWIFIEX_TxPD_POWER_MGMT_NULL_PACKET) {
		/* Send NULL packet immediately*/
		if (f_precopy_cur_buf) {
			if (skb_queue_empty(&port->tx_aggr.aggr_list)) {
				f_precopy_cur_buf = 0;
				f_send_aggr_buf = 0;
				f_send_cur_buf = 1;
			} else {
				f_send_aggr_buf = 1;
			}
		} else if (f_postcopy_cur_buf) {
			f_send_cur_buf = 1;
			f_postcopy_cur_buf = 0;
		}
	}

	if (f_precopy_cur_buf) {
		skb_queue_tail(&port->tx_aggr.aggr_list, skb);
		port->tx_aggr.aggr_len += (skb->len + pad);
		port->tx_aggr.aggr_num++;
		if (f_send_aggr_buf)
			goto send_aggr_buf;

		/* packet will not been send immediately,
		 * set a timer to make sure it will be sent under
		 * strict time limit. Dynamically fit the timeout
		 * value, according to packets number in aggr_list
		 */
		if (!port->tx_aggr.timer_cnxt.is_hold_timer_set) {
			port->tx_aggr.timer_cnxt.hold_tmo_msecs =
					MWIFIEX_USB_TX_AGGR_TMO_MIN;
			timeout =
				port->tx_aggr.timer_cnxt.hold_tmo_msecs;
			mod_timer(&port->tx_aggr.timer_cnxt.hold_timer,
				  jiffies + msecs_to_jiffies(timeout));
			port->tx_aggr.timer_cnxt.is_hold_timer_set = true;
		} else {
			if (port->tx_aggr.timer_cnxt.hold_tmo_msecs <
			    MWIFIEX_USB_TX_AGGR_TMO_MAX) {
				/* Dyanmic fit timeout */
				timeout =
				++port->tx_aggr.timer_cnxt.hold_tmo_msecs;
				mod_timer(&port->tx_aggr.timer_cnxt.hold_timer,
					  jiffies + msecs_to_jiffies(timeout));
			}
		}
	}

send_aggr_buf:
	if (f_send_aggr_buf) {
		ret = mwifiex_usb_prepare_tx_aggr_skb(adapter, port, &skb_send);
		if (!ret) {
			context = &port->tx_data_list[port->tx_data_ix++];
			ret = mwifiex_usb_construct_send_urb(adapter, port, ep,
							     context, skb_send);
			if (ret == -1)
				mwifiex_write_data_complete(adapter, skb_send,
							    0, -1);
		}
	}

	if (f_send_cur_buf) {
		if (f_send_aggr_buf) {
			if (atomic_read(&port->tx_data_urb_pending) >=
			    MWIFIEX_TX_DATA_URB) {
				port->block_status = true;
				adapter->data_sent =
					mwifiex_usb_data_sent(adapter);
				/* no available urb, postcopy packet*/
				f_postcopy_cur_buf = 1;
				goto postcopy_cur_buf;
			}

			if (port->tx_data_ix >= MWIFIEX_TX_DATA_URB)
				port->tx_data_ix = 0;
		}

		payload = skb->data;
		*(u16 *)&payload[2] =
			cpu_to_le16(MWIFIEX_TYPE_AGGR_DATA_V2 | 0x80);
		*(u16 *)payload = cpu_to_le16(skb->len);
		skb_send = skb;
		context = &port->tx_data_list[port->tx_data_ix++];
		return mwifiex_usb_construct_send_urb(adapter, port, ep,
						      context, skb_send);
	}

postcopy_cur_buf:
	if (f_postcopy_cur_buf) {
		skb_queue_tail(&port->tx_aggr.aggr_list, skb);
		port->tx_aggr.aggr_len += (skb->len + pad);
		port->tx_aggr.aggr_num++;
		/* New aggregation begin, start timer */
		if (!port->tx_aggr.timer_cnxt.is_hold_timer_set) {
			port->tx_aggr.timer_cnxt.hold_tmo_msecs =
					MWIFIEX_USB_TX_AGGR_TMO_MIN;
			timeout = port->tx_aggr.timer_cnxt.hold_tmo_msecs;
			mod_timer(&port->tx_aggr.timer_cnxt.hold_timer,
				  jiffies + msecs_to_jiffies(timeout));
			port->tx_aggr.timer_cnxt.is_hold_timer_set = true;
		}
	}

	return -EINPROGRESS;
}

static void mwifiex_usb_tx_aggr_tmo(struct timer_list *t)
{
	struct urb_context *urb_cnxt = NULL;
	struct sk_buff *skb_send = NULL;
	struct tx_aggr_tmr_cnxt *timer_context =
		from_timer(timer_context, t, hold_timer);
	struct mwifiex_adapter *adapter = timer_context->adapter;
	struct usb_tx_data_port *port = timer_context->port;
	int err = 0;

	spin_lock_bh(&port->tx_aggr_lock);
	err = mwifiex_usb_prepare_tx_aggr_skb(adapter, port, &skb_send);
	if (err) {
		mwifiex_dbg(adapter, ERROR,
			    "prepare tx aggr skb failed, err=%d\n", err);
		goto unlock;
	}

	if (atomic_read(&port->tx_data_urb_pending) >=
	    MWIFIEX_TX_DATA_URB) {
		port->block_status = true;
		adapter->data_sent =
			mwifiex_usb_data_sent(adapter);
		err = -1;
		goto done;
	}

	if (port->tx_data_ix >= MWIFIEX_TX_DATA_URB)
		port->tx_data_ix = 0;

	urb_cnxt = &port->tx_data_list[port->tx_data_ix++];
	err = mwifiex_usb_construct_send_urb(adapter, port, port->tx_data_ep,
					     urb_cnxt, skb_send);
done:
	if (err == -1)
		mwifiex_write_data_complete(adapter, skb_send, 0, -1);
unlock:
	spin_unlock_bh(&port->tx_aggr_lock);
}

/* This function write a command/data packet to card. */
static int mwifiex_usb_host_to_card(struct mwifiex_adapter *adapter, u8 ep,
				    struct sk_buff *skb,
				    struct mwifiex_tx_param *tx_param)
{
	struct usb_card_rec *card = adapter->card;
	struct urb_context *context = NULL;
	struct usb_tx_data_port *port = NULL;
	int idx, ret;

	if (test_bit(MWIFIEX_IS_SUSPENDED, &adapter->work_flags)) {
		mwifiex_dbg(adapter, ERROR,
			    "%s: not allowed while suspended\n", __func__);
		return -1;
	}

	if (test_bit(MWIFIEX_SURPRISE_REMOVED, &adapter->work_flags)) {
		mwifiex_dbg(adapter, ERROR, "%s: device removed\n", __func__);
		return -1;
	}

	mwifiex_dbg(adapter, INFO, "%s: ep=%d\n", __func__, ep);

	if (ep == card->tx_cmd_ep) {
		context = &card->tx_cmd;
	} else {
		/* get the data port structure for endpoint */
		for (idx = 0; idx < MWIFIEX_TX_DATA_PORT; idx++) {
			if (ep == card->port[idx].tx_data_ep) {
				port = &card->port[idx];
				if (atomic_read(&port->tx_data_urb_pending)
				    >= MWIFIEX_TX_DATA_URB) {
					port->block_status = true;
					adapter->data_sent =
						mwifiex_usb_data_sent(adapter);
					return -EBUSY;
				}
				if (port->tx_data_ix >= MWIFIEX_TX_DATA_URB)
					port->tx_data_ix = 0;
				break;
			}
		}

		if (!port) {
			mwifiex_dbg(adapter, ERROR, "Wrong usb tx data port\n");
			return -1;
		}

		if (adapter->bus_aggr.enable) {
			spin_lock_bh(&port->tx_aggr_lock);
			ret =  mwifiex_usb_aggr_tx_data(adapter, ep, skb,
							tx_param, port);
			spin_unlock_bh(&port->tx_aggr_lock);
			return ret;
		}

		context = &port->tx_data_list[port->tx_data_ix++];
	}

	return mwifiex_usb_construct_send_urb(adapter, port, ep, context, skb);
}

static int mwifiex_usb_tx_init(struct mwifiex_adapter *adapter)
{
	struct usb_card_rec *card = (struct usb_card_rec *)adapter->card;
	struct usb_tx_data_port *port;
	int i, j;

	card->tx_cmd.adapter = adapter;
	card->tx_cmd.ep = card->tx_cmd_ep;

	card->tx_cmd.urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!card->tx_cmd.urb)
		return -ENOMEM;

	for (i = 0; i < MWIFIEX_TX_DATA_PORT; i++) {
		port = &card->port[i];
		if (!port->tx_data_ep)
			continue;
		port->tx_data_ix = 0;
		skb_queue_head_init(&port->tx_aggr.aggr_list);
		if (port->tx_data_ep == MWIFIEX_USB_EP_DATA)
			port->block_status = false;
		else
			port->block_status = true;
		for (j = 0; j < MWIFIEX_TX_DATA_URB; j++) {
			port->tx_data_list[j].adapter = adapter;
			port->tx_data_list[j].ep = port->tx_data_ep;
			port->tx_data_list[j].urb =
					usb_alloc_urb(0, GFP_KERNEL);
			if (!port->tx_data_list[j].urb)
				return -ENOMEM;
		}

		port->tx_aggr.timer_cnxt.adapter = adapter;
		port->tx_aggr.timer_cnxt.port = port;
		port->tx_aggr.timer_cnxt.is_hold_timer_set = false;
		port->tx_aggr.timer_cnxt.hold_tmo_msecs = 0;
		timer_setup(&port->tx_aggr.timer_cnxt.hold_timer,
			    mwifiex_usb_tx_aggr_tmo, 0);
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
	if (!card->rx_cmd.urb)
		return -ENOMEM;

	card->rx_cmd.skb = dev_alloc_skb(MWIFIEX_RX_CMD_BUF_SIZE);
	if (!card->rx_cmd.skb)
		return -ENOMEM;

	if (mwifiex_usb_submit_rx_urb(&card->rx_cmd, MWIFIEX_RX_CMD_BUF_SIZE))
		return -1;

	for (i = 0; i < MWIFIEX_RX_DATA_URB; i++) {
		card->rx_data_list[i].adapter = adapter;
		card->rx_data_list[i].ep = card->rx_data_ep;

		card->rx_data_list[i].urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!card->rx_data_list[i].urb)
			return -1;
		if (mwifiex_usb_submit_rx_urb(&card->rx_data_list[i],
					      MWIFIEX_RX_DATA_BUF_SIZE))
			return -1;
	}

	return 0;
}

/* This function register usb device and initialize parameter. */
static int mwifiex_register_dev(struct mwifiex_adapter *adapter)
{
	struct usb_card_rec *card = (struct usb_card_rec *)adapter->card;

	card->adapter = adapter;

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

static void mwifiex_usb_cleanup_tx_aggr(struct mwifiex_adapter *adapter)
{
	struct usb_card_rec *card = (struct usb_card_rec *)adapter->card;
	struct usb_tx_data_port *port;
	struct sk_buff *skb_tmp;
	int idx;

	for (idx = 0; idx < MWIFIEX_TX_DATA_PORT; idx++) {
		port = &card->port[idx];
		if (adapter->bus_aggr.enable)
			while ((skb_tmp =
				skb_dequeue(&port->tx_aggr.aggr_list)))
				mwifiex_write_data_complete(adapter, skb_tmp,
							    0, -1);
		if (port->tx_aggr.timer_cnxt.hold_timer.function)
			del_timer_sync(&port->tx_aggr.timer_cnxt.hold_timer);
		port->tx_aggr.timer_cnxt.is_hold_timer_set = false;
		port->tx_aggr.timer_cnxt.hold_tmo_msecs = 0;
	}
}

static void mwifiex_unregister_dev(struct mwifiex_adapter *adapter)
{
	struct usb_card_rec *card = (struct usb_card_rec *)adapter->card;

	mwifiex_usb_free(card);

	mwifiex_usb_cleanup_tx_aggr(adapter);

	card->adapter = NULL;
}

static int mwifiex_prog_fw_w_helper(struct mwifiex_adapter *adapter,
				    struct mwifiex_fw_image *fw)
{
	int ret = 0;
	u8 *firmware = fw->fw_buf, *recv_buff;
	u32 retries = USB8XXX_FW_MAX_RETRY + 1;
	u32 dlen;
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
	if (!recv_buff) {
		ret = -ENOMEM;
		goto cleanup;
	}

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

			/* Command 7 doesn't have data length field */
			if (dnld_cmd == FW_CMD_7)
				dlen = 0;

			memcpy(fwdata->data, &firmware[tlen], dlen);

			fwdata->seq_num = cpu_to_le32(fw_seqnum);
			tlen += dlen;
		}

		/* If the send/receive fails or CRC occurs then retry */
		while (--retries) {
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

			retries = USB8XXX_FW_MAX_RETRY + 1;
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

module_usb_driver(mwifiex_usb_driver);

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION("Marvell WiFi-Ex USB Driver version" USB_VERSION);
MODULE_VERSION(USB_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_FIRMWARE(USB8766_DEFAULT_FW_NAME);
MODULE_FIRMWARE(USB8797_DEFAULT_FW_NAME);
MODULE_FIRMWARE(USB8801_DEFAULT_FW_NAME);
MODULE_FIRMWARE(USB8997_DEFAULT_FW_NAME);
