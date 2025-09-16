// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 pureLiFi
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/usb.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/module.h>
#include <net/mac80211.h>
#include <linux/unaligned.h>
#include <linux/sysfs.h>

#include "mac.h"
#include "usb.h"
#include "chip.h"

static const struct usb_device_id usb_ids[] = {
	{ USB_DEVICE(PURELIFI_X_VENDOR_ID_0, PURELIFI_X_PRODUCT_ID_0),
	  .driver_info = DEVICE_LIFI_X },
	{ USB_DEVICE(PURELIFI_XC_VENDOR_ID_0, PURELIFI_XC_PRODUCT_ID_0),
	  .driver_info = DEVICE_LIFI_XC },
	{ USB_DEVICE(PURELIFI_XL_VENDOR_ID_0, PURELIFI_XL_PRODUCT_ID_0),
	  .driver_info = DEVICE_LIFI_XL },
	{}
};

void plfxlc_send_packet_from_data_queue(struct plfxlc_usb *usb)
{
	struct plfxlc_usb_tx *tx = &usb->tx;
	struct sk_buff *skb = NULL;
	unsigned long flags;
	u8 last_served_sidx;

	spin_lock_irqsave(&tx->lock, flags);
	last_served_sidx = usb->sidx;
	do {
		usb->sidx = (usb->sidx + 1) % MAX_STA_NUM;
		if (!(tx->station[usb->sidx].flag & STATION_CONNECTED_FLAG))
			continue;
		if (!(tx->station[usb->sidx].flag & STATION_FIFO_FULL_FLAG))
			skb = skb_peek(&tx->station[usb->sidx].data_list);
	} while ((usb->sidx != last_served_sidx) && (!skb));

	if (skb) {
		skb = skb_dequeue(&tx->station[usb->sidx].data_list);
		plfxlc_usb_wreq_async(usb, skb->data, skb->len, USB_REQ_DATA_TX,
				      plfxlc_tx_urb_complete, skb);
		if (skb_queue_len(&tx->station[usb->sidx].data_list) <= 60)
			ieee80211_wake_queues(plfxlc_usb_to_hw(usb));
	}
	spin_unlock_irqrestore(&tx->lock, flags);
}

static void handle_rx_packet(struct plfxlc_usb *usb, const u8 *buffer,
			     unsigned int length)
{
	plfxlc_mac_rx(plfxlc_usb_to_hw(usb), buffer, length);
}

static void rx_urb_complete(struct urb *urb)
{
	struct plfxlc_usb_tx *tx;
	struct plfxlc_usb *usb;
	unsigned int length;
	const u8 *buffer;
	u16 status;
	u8 sidx;
	int r;

	if (!urb) {
		pr_err("urb is NULL\n");
		return;
	}
	if (!urb->context) {
		pr_err("urb ctx is NULL\n");
		return;
	}
	usb = urb->context;

	if (usb->initialized != 1) {
		pr_err("usb is not initialized\n");
		return;
	}

	tx = &usb->tx;
	switch (urb->status) {
	case 0:
		break;
	case -ESHUTDOWN:
	case -EINVAL:
	case -ENODEV:
	case -ENOENT:
	case -ECONNRESET:
	case -EPIPE:
		dev_dbg(plfxlc_urb_dev(urb), "urb %p error %d\n", urb, urb->status);
		return;
	default:
		dev_dbg(plfxlc_urb_dev(urb), "urb %p error %d\n", urb, urb->status);
		if (tx->submitted_urbs++ < PURELIFI_URB_RETRY_MAX) {
			dev_dbg(plfxlc_urb_dev(urb), "urb %p resubmit %d", urb,
				tx->submitted_urbs++);
			goto resubmit;
		} else {
			dev_dbg(plfxlc_urb_dev(urb), "urb %p  max resubmits reached", urb);
			tx->submitted_urbs = 0;
			return;
		}
	}

	buffer = urb->transfer_buffer;
	length = le32_to_cpu(*(__le32 *)(buffer + sizeof(struct rx_status)))
		 + sizeof(u32);

	if (urb->actual_length != (PLF_MSG_STATUS_OFFSET + 1)) {
		if (usb->initialized && usb->link_up)
			handle_rx_packet(usb, buffer, length);
		goto resubmit;
	}

	status = buffer[PLF_MSG_STATUS_OFFSET];

	switch (status) {
	case STATION_FIFO_ALMOST_FULL_NOT_MESSAGE:
		dev_dbg(&usb->intf->dev,
			"FIFO full not packet receipt\n");
		tx->mac_fifo_full = 1;
		for (sidx = 0; sidx < MAX_STA_NUM; sidx++)
			tx->station[sidx].flag |= STATION_FIFO_FULL_FLAG;
		break;
	case STATION_FIFO_ALMOST_FULL_MESSAGE:
		dev_dbg(&usb->intf->dev, "FIFO full packet receipt\n");

		for (sidx = 0; sidx < MAX_STA_NUM; sidx++)
			tx->station[sidx].flag &= STATION_ACTIVE_FLAG;

		plfxlc_send_packet_from_data_queue(usb);
		break;
	case STATION_CONNECT_MESSAGE:
		usb->link_up = 1;
		dev_dbg(&usb->intf->dev, "ST_CONNECT_MSG packet receipt\n");
		break;
	case STATION_DISCONNECT_MESSAGE:
		usb->link_up = 0;
		dev_dbg(&usb->intf->dev, "ST_DISCONN_MSG packet receipt\n");
		break;
	default:
		dev_dbg(&usb->intf->dev, "Unknown packet receipt\n");
		break;
	}

resubmit:
	r = usb_submit_urb(urb, GFP_ATOMIC);
	if (r)
		dev_dbg(plfxlc_urb_dev(urb), "urb %p resubmit fail (%d)\n", urb, r);
}

static struct urb *alloc_rx_urb(struct plfxlc_usb *usb)
{
	struct usb_device *udev = plfxlc_usb_to_usbdev(usb);
	struct urb *urb;
	void *buffer;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return NULL;

	buffer = usb_alloc_coherent(udev, USB_MAX_RX_SIZE, GFP_KERNEL,
				    &urb->transfer_dma);
	if (!buffer) {
		usb_free_urb(urb);
		return NULL;
	}

	usb_fill_bulk_urb(urb, udev, usb_rcvbulkpipe(udev, EP_DATA_IN),
			  buffer, USB_MAX_RX_SIZE,
			  rx_urb_complete, usb);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	return urb;
}

static void free_rx_urb(struct urb *urb)
{
	if (!urb)
		return;
	usb_free_coherent(urb->dev, urb->transfer_buffer_length,
			  urb->transfer_buffer, urb->transfer_dma);
	usb_free_urb(urb);
}

static int __lf_x_usb_enable_rx(struct plfxlc_usb *usb)
{
	struct plfxlc_usb_rx *rx = &usb->rx;
	struct urb **urbs;
	int i, r;

	r = -ENOMEM;
	urbs = kcalloc(RX_URBS_COUNT, sizeof(struct urb *), GFP_KERNEL);
	if (!urbs)
		goto error;

	for (i = 0; i < RX_URBS_COUNT; i++) {
		urbs[i] = alloc_rx_urb(usb);
		if (!urbs[i])
			goto error;
	}

	spin_lock_irq(&rx->lock);

	dev_dbg(plfxlc_usb_dev(usb), "irq_disabled %d\n", irqs_disabled());

	if (rx->urbs) {
		spin_unlock_irq(&rx->lock);
		r = 0;
		goto error;
	}
	rx->urbs = urbs;
	rx->urbs_count = RX_URBS_COUNT;
	spin_unlock_irq(&rx->lock);

	for (i = 0; i < RX_URBS_COUNT; i++) {
		r = usb_submit_urb(urbs[i], GFP_KERNEL);
		if (r)
			goto error_submit;
	}

	return 0;

error_submit:
	for (i = 0; i < RX_URBS_COUNT; i++)
		usb_kill_urb(urbs[i]);
	spin_lock_irq(&rx->lock);
	rx->urbs = NULL;
	rx->urbs_count = 0;
	spin_unlock_irq(&rx->lock);
error:
	if (urbs) {
		for (i = 0; i < RX_URBS_COUNT; i++)
			free_rx_urb(urbs[i]);
	}
	kfree(urbs);
	return r;
}

int plfxlc_usb_enable_rx(struct plfxlc_usb *usb)
{
	struct plfxlc_usb_rx *rx = &usb->rx;
	int r;

	mutex_lock(&rx->setup_mutex);
	r = __lf_x_usb_enable_rx(usb);
	if (!r)
		usb->rx_usb_enabled = 1;

	mutex_unlock(&rx->setup_mutex);

	return r;
}

static void __lf_x_usb_disable_rx(struct plfxlc_usb *usb)
{
	struct plfxlc_usb_rx *rx = &usb->rx;
	unsigned long flags;
	unsigned int count;
	struct urb **urbs;
	int i;

	spin_lock_irqsave(&rx->lock, flags);
	urbs = rx->urbs;
	count = rx->urbs_count;
	spin_unlock_irqrestore(&rx->lock, flags);

	if (!urbs)
		return;

	for (i = 0; i < count; i++) {
		usb_kill_urb(urbs[i]);
		free_rx_urb(urbs[i]);
	}
	kfree(urbs);
	rx->urbs = NULL;
	rx->urbs_count = 0;
}

void plfxlc_usb_disable_rx(struct plfxlc_usb *usb)
{
	struct plfxlc_usb_rx *rx = &usb->rx;

	mutex_lock(&rx->setup_mutex);
	__lf_x_usb_disable_rx(usb);
	usb->rx_usb_enabled = 0;
	mutex_unlock(&rx->setup_mutex);
}

void plfxlc_usb_disable_tx(struct plfxlc_usb *usb)
{
	struct plfxlc_usb_tx *tx = &usb->tx;
	unsigned long flags;

	clear_bit(PLF_BIT_ENABLED, &tx->enabled);

	/* kill all submitted tx-urbs */
	usb_kill_anchored_urbs(&tx->submitted);

	spin_lock_irqsave(&tx->lock, flags);
	WARN_ON(!skb_queue_empty(&tx->submitted_skbs));
	WARN_ON(tx->submitted_urbs != 0);
	tx->submitted_urbs = 0;
	spin_unlock_irqrestore(&tx->lock, flags);

	/* The stopped state is ignored, relying on ieee80211_wake_queues()
	 * in a potentionally following plfxlc_usb_enable_tx().
	 */
}

void plfxlc_usb_enable_tx(struct plfxlc_usb *usb)
{
	struct plfxlc_usb_tx *tx = &usb->tx;
	unsigned long flags;

	spin_lock_irqsave(&tx->lock, flags);
	set_bit(PLF_BIT_ENABLED, &tx->enabled);
	tx->submitted_urbs = 0;
	ieee80211_wake_queues(plfxlc_usb_to_hw(usb));
	tx->stopped = 0;
	spin_unlock_irqrestore(&tx->lock, flags);
}

void plfxlc_tx_urb_complete(struct urb *urb)
{
	struct ieee80211_tx_info *info;
	struct plfxlc_usb *usb;
	struct sk_buff *skb;

	skb = urb->context;
	info = IEEE80211_SKB_CB(skb);
	/* grab 'usb' pointer before handing off the skb (since
	 * it might be freed by plfxlc_mac_tx_to_dev or mac80211)
	 */
	usb = &plfxlc_hw_mac(info->rate_driver_data[0])->chip.usb;

	switch (urb->status) {
	case 0:
		break;
	case -ESHUTDOWN:
	case -EINVAL:
	case -ENODEV:
	case -ENOENT:
	case -ECONNRESET:
	case -EPIPE:
		dev_dbg(plfxlc_urb_dev(urb), "urb %p error %d\n", urb, urb->status);
		break;
	default:
		dev_dbg(plfxlc_urb_dev(urb), "urb %p error %d\n", urb, urb->status);
		return;
	}

	plfxlc_mac_tx_to_dev(skb, urb->status);
	plfxlc_send_packet_from_data_queue(usb);
	usb_free_urb(urb);
}

static inline void init_usb_rx(struct plfxlc_usb *usb)
{
	struct plfxlc_usb_rx *rx = &usb->rx;

	spin_lock_init(&rx->lock);
	mutex_init(&rx->setup_mutex);

	if (interface_to_usbdev(usb->intf)->speed == USB_SPEED_HIGH)
		rx->usb_packet_size = 512;
	else
		rx->usb_packet_size = 64;

	if (rx->fragment_length != 0)
		dev_dbg(plfxlc_usb_dev(usb), "fragment_length error\n");
}

static inline void init_usb_tx(struct plfxlc_usb *usb)
{
	struct plfxlc_usb_tx *tx = &usb->tx;

	spin_lock_init(&tx->lock);
	clear_bit(PLF_BIT_ENABLED, &tx->enabled);
	tx->stopped = 0;
	skb_queue_head_init(&tx->submitted_skbs);
	init_usb_anchor(&tx->submitted);
}

void plfxlc_usb_init(struct plfxlc_usb *usb, struct ieee80211_hw *hw,
		     struct usb_interface *intf)
{
	memset(usb, 0, sizeof(*usb));
	usb->intf = usb_get_intf(intf);
	usb_set_intfdata(usb->intf, hw);
	init_usb_tx(usb);
	init_usb_rx(usb);
}

void plfxlc_usb_release(struct plfxlc_usb *usb)
{
	plfxlc_op_stop(plfxlc_usb_to_hw(usb), false);
	plfxlc_usb_disable_tx(usb);
	plfxlc_usb_disable_rx(usb);
	usb_set_intfdata(usb->intf, NULL);
	usb_put_intf(usb->intf);
}

const char *plfxlc_speed(enum usb_device_speed speed)
{
	switch (speed) {
	case USB_SPEED_LOW:
		return "low";
	case USB_SPEED_FULL:
		return "full";
	case USB_SPEED_HIGH:
		return "high";
	default:
		return "unknown";
	}
}

int plfxlc_usb_init_hw(struct plfxlc_usb *usb)
{
	int r;

	r = usb_reset_configuration(plfxlc_usb_to_usbdev(usb));
	if (r) {
		dev_err(plfxlc_usb_dev(usb), "cfg reset failed (%d)\n", r);
		return r;
	}
	return 0;
}

static void get_usb_req(struct usb_device *udev, void *buffer,
			u32 buffer_len, enum plf_usb_req_enum usb_req_id,
			struct plf_usb_req *usb_req)
{
	__be32 payload_len_nw = cpu_to_be32(buffer_len + FCS_LEN);
	const u8 *buffer_src_p = buffer;
	u8 *buffer_dst = usb_req->buf;
	u32 temp_usb_len = 0;

	usb_req->id = cpu_to_be32(usb_req_id);
	usb_req->len  = cpu_to_be32(0);

	/* Copy buffer length into the transmitted buffer, as it is important
	 * for the Rx MAC to know its exact length.
	 */
	if (usb_req->id == cpu_to_be32(USB_REQ_BEACON_WR)) {
		memcpy(buffer_dst, &payload_len_nw, sizeof(payload_len_nw));
		buffer_dst += sizeof(payload_len_nw);
		temp_usb_len += sizeof(payload_len_nw);
	}

	memcpy(buffer_dst, buffer_src_p, buffer_len);
	buffer_dst += buffer_len;
	buffer_src_p += buffer_len;
	temp_usb_len +=  buffer_len;

	/* Set the FCS_LEN (4) bytes as 0 for CRC checking. */
	memset(buffer_dst, 0, FCS_LEN);
	buffer_dst += FCS_LEN;
	temp_usb_len += FCS_LEN;

	/* Round the packet to be transmitted to 4 bytes. */
	if (temp_usb_len % PURELIFI_BYTE_NUM_ALIGNMENT) {
		memset(buffer_dst, 0, PURELIFI_BYTE_NUM_ALIGNMENT -
		       (temp_usb_len %
			PURELIFI_BYTE_NUM_ALIGNMENT));
		buffer_dst += PURELIFI_BYTE_NUM_ALIGNMENT -
				(temp_usb_len %
				PURELIFI_BYTE_NUM_ALIGNMENT);
		temp_usb_len += PURELIFI_BYTE_NUM_ALIGNMENT -
				(temp_usb_len % PURELIFI_BYTE_NUM_ALIGNMENT);
	}

	usb_req->len = cpu_to_be32(temp_usb_len);
}

int plfxlc_usb_wreq_async(struct plfxlc_usb *usb, const u8 *buffer,
			  int buffer_len, enum plf_usb_req_enum usb_req_id,
			  usb_complete_t complete_fn,
			  void *context)
{
	struct usb_device *udev = interface_to_usbdev(usb->ez_usb);
	struct urb *urb;
	int r;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb)
		return -ENOMEM;
	usb_fill_bulk_urb(urb, udev, usb_sndbulkpipe(udev, EP_DATA_OUT),
			  (void *)buffer, buffer_len, complete_fn, context);

	r = usb_submit_urb(urb, GFP_ATOMIC);
	if (r) {
		usb_free_urb(urb);
		dev_err(&udev->dev, "Async write submit failed (%d)\n", r);
	}

	return r;
}

int plfxlc_usb_wreq(struct usb_interface *ez_usb, void *buffer, int buffer_len,
		    enum plf_usb_req_enum usb_req_id)
{
	struct usb_device *udev = interface_to_usbdev(ez_usb);
	unsigned char *dma_buffer = NULL;
	struct plf_usb_req usb_req;
	int usb_bulk_msg_len;
	int actual_length;
	int r;

	get_usb_req(udev, buffer, buffer_len, usb_req_id, &usb_req);
	usb_bulk_msg_len = sizeof(__le32) + sizeof(__le32) +
			   be32_to_cpu(usb_req.len);

	dma_buffer = kmemdup(&usb_req, usb_bulk_msg_len, GFP_KERNEL);

	if (!dma_buffer) {
		r = -ENOMEM;
		goto error;
	}

	r = usb_bulk_msg(udev,
			 usb_sndbulkpipe(udev, EP_DATA_OUT),
			 dma_buffer, usb_bulk_msg_len,
			 &actual_length, USB_BULK_MSG_TIMEOUT_MS);
	kfree(dma_buffer);
error:
	if (r) {
		r = -ENOMEM;
		dev_err(&udev->dev, "usb_bulk_msg failed (%d)\n", r);
	}

	return r;
}

static void slif_data_plane_sap_timer_callb(struct timer_list *t)
{
	struct plfxlc_usb *usb = timer_container_of(usb, t, tx.tx_retry_timer);

	plfxlc_send_packet_from_data_queue(usb);
	timer_setup(&usb->tx.tx_retry_timer,
		    slif_data_plane_sap_timer_callb, 0);
	mod_timer(&usb->tx.tx_retry_timer, jiffies + TX_RETRY_BACKOFF_JIFF);
}

static void sta_queue_cleanup_timer_callb(struct timer_list *t)
{
	struct plfxlc_usb *usb = timer_container_of(usb, t, sta_queue_cleanup);
	struct plfxlc_usb_tx *tx = &usb->tx;
	int sidx;

	for (sidx = 0; sidx < MAX_STA_NUM - 1; sidx++) {
		if (!(tx->station[sidx].flag & STATION_CONNECTED_FLAG))
			continue;
		if (tx->station[sidx].flag & STATION_HEARTBEAT_FLAG) {
			tx->station[sidx].flag ^= STATION_HEARTBEAT_FLAG;
		} else {
			eth_zero_addr(tx->station[sidx].mac);
			tx->station[sidx].flag = 0;
		}
	}
	timer_setup(&usb->sta_queue_cleanup,
		    sta_queue_cleanup_timer_callb, 0);
	mod_timer(&usb->sta_queue_cleanup, jiffies + STA_QUEUE_CLEANUP_JIFF);
}

static int probe(struct usb_interface *intf,
		 const struct usb_device_id *id)
{
	u8 serial_number[PURELIFI_SERIAL_LEN];
	struct ieee80211_hw *hw = NULL;
	struct plfxlc_usb_tx *tx;
	struct plfxlc_chip *chip;
	struct plfxlc_usb *usb;
	u8 hw_address[ETH_ALEN];
	unsigned int i;
	int r = 0;

	hw = plfxlc_mac_alloc_hw(intf);

	if (!hw) {
		r = -ENOMEM;
		goto error;
	}

	chip = &plfxlc_hw_mac(hw)->chip;
	usb = &chip->usb;
	usb->ez_usb = intf;
	tx = &usb->tx;

	r = plfxlc_upload_mac_and_serial(intf, hw_address, serial_number);
	if (r) {
		dev_err(&intf->dev, "MAC and Serial upload failed (%d)\n", r);
		goto error_free_hw;
	}

	chip->unit_type = STA;
	dev_err(&intf->dev, "Unit type is station");

	r = plfxlc_mac_preinit_hw(hw, hw_address);
	if (r) {
		dev_err(&intf->dev, "Init mac failed (%d)\n", r);
		goto error_free_hw;
	}

	r = ieee80211_register_hw(hw);
	if (r) {
		dev_err(&intf->dev, "Register device failed (%d)\n", r);
		goto error_free_hw;
	}

	if ((le16_to_cpu(interface_to_usbdev(intf)->descriptor.idVendor) ==
				PURELIFI_XL_VENDOR_ID_0) &&
	    (le16_to_cpu(interface_to_usbdev(intf)->descriptor.idProduct) ==
				PURELIFI_XL_PRODUCT_ID_0)) {
		r = plfxlc_download_xl_firmware(intf);
	} else {
		r = plfxlc_download_fpga(intf);
	}
	if (r != 0) {
		dev_err(&intf->dev, "FPGA download failed (%d)\n", r);
		goto error_unreg_hw;
	}

	tx->mac_fifo_full = 0;
	spin_lock_init(&tx->lock);

	msleep(PLF_MSLEEP_TIME);
	r = plfxlc_usb_init_hw(usb);
	if (r < 0) {
		dev_err(&intf->dev, "usb_init_hw failed (%d)\n", r);
		goto error_unreg_hw;
	}

	msleep(PLF_MSLEEP_TIME);
	r = plfxlc_chip_switch_radio(chip, PLFXLC_RADIO_ON);
	if (r < 0) {
		dev_dbg(&intf->dev, "chip_switch_radio_on failed (%d)\n", r);
		goto error_unreg_hw;
	}

	msleep(PLF_MSLEEP_TIME);
	r = plfxlc_chip_set_rate(chip, 8);
	if (r < 0) {
		dev_dbg(&intf->dev, "chip_set_rate failed (%d)\n", r);
		goto error_unreg_hw;
	}

	msleep(PLF_MSLEEP_TIME);
	r = plfxlc_usb_wreq(usb->ez_usb,
			    hw_address, ETH_ALEN, USB_REQ_MAC_WR);
	if (r < 0) {
		dev_dbg(&intf->dev, "MAC_WR failure (%d)\n", r);
		goto error_unreg_hw;
	}

	plfxlc_chip_enable_rxtx(chip);

	/* Initialise the data plane Tx queue */
	for (i = 0; i < MAX_STA_NUM; i++) {
		skb_queue_head_init(&tx->station[i].data_list);
		tx->station[i].flag = 0;
	}

	tx->station[STA_BROADCAST_INDEX].flag |= STATION_CONNECTED_FLAG;
	for (i = 0; i < ETH_ALEN; i++)
		tx->station[STA_BROADCAST_INDEX].mac[i] = 0xFF;

	timer_setup(&tx->tx_retry_timer, slif_data_plane_sap_timer_callb, 0);
	tx->tx_retry_timer.expires = jiffies + TX_RETRY_BACKOFF_JIFF;
	add_timer(&tx->tx_retry_timer);

	timer_setup(&usb->sta_queue_cleanup,
		    sta_queue_cleanup_timer_callb, 0);
	usb->sta_queue_cleanup.expires = jiffies + STA_QUEUE_CLEANUP_JIFF;
	add_timer(&usb->sta_queue_cleanup);

	plfxlc_mac_init_hw(hw);
	usb->initialized = true;
	return 0;

error_unreg_hw:
	ieee80211_unregister_hw(hw);
error_free_hw:
	plfxlc_mac_release_hw(hw);
error:
	dev_err(&intf->dev, "pureLifi:Device error");
	return r;
}

static void disconnect(struct usb_interface *intf)
{
	struct ieee80211_hw *hw = plfxlc_intf_to_hw(intf);
	struct plfxlc_mac *mac;
	struct plfxlc_usb *usb;

	/* Either something really bad happened, or
	 * we're just dealing with a DEVICE_INSTALLER.
	 */
	if (!hw)
		return;

	mac = plfxlc_hw_mac(hw);
	usb = &mac->chip.usb;

	timer_delete_sync(&usb->tx.tx_retry_timer);
	timer_delete_sync(&usb->sta_queue_cleanup);

	ieee80211_unregister_hw(hw);

	plfxlc_chip_disable_rxtx(&mac->chip);

	/* If the disconnect has been caused by a removal of the
	 * driver module, the reset allows reloading of the driver. If the
	 * reset will not be executed here, the upload of the firmware in the
	 * probe function caused by the reloading of the driver will fail.
	 */
	usb_reset_device(interface_to_usbdev(intf));

	plfxlc_mac_release_hw(hw);
}

static void plfxlc_usb_resume(struct plfxlc_usb *usb)
{
	struct plfxlc_mac *mac = plfxlc_usb_to_mac(usb);
	int r;

	r = plfxlc_op_start(plfxlc_usb_to_hw(usb));
	if (r < 0) {
		dev_warn(plfxlc_usb_dev(usb),
			 "Device resume failed (%d)\n", r);

		if (usb->was_running)
			set_bit(PURELIFI_DEVICE_RUNNING, &mac->flags);

		usb_queue_reset_device(usb->intf);
		return;
	}

	if (mac->type != NL80211_IFTYPE_UNSPECIFIED) {
		r = plfxlc_restore_settings(mac);
		if (r < 0) {
			dev_dbg(plfxlc_usb_dev(usb),
				"Restore failed (%d)\n", r);
			return;
		}
	}
}

static void plfxlc_usb_stop(struct plfxlc_usb *usb)
{
	plfxlc_op_stop(plfxlc_usb_to_hw(usb), false);
	plfxlc_usb_disable_tx(usb);
	plfxlc_usb_disable_rx(usb);

	usb->initialized = false;
}

static int pre_reset(struct usb_interface *intf)
{
	struct ieee80211_hw *hw = usb_get_intfdata(intf);
	struct plfxlc_mac *mac;
	struct plfxlc_usb *usb;

	if (!hw || intf->condition != USB_INTERFACE_BOUND)
		return 0;

	mac = plfxlc_hw_mac(hw);
	usb = &mac->chip.usb;

	usb->was_running = test_bit(PURELIFI_DEVICE_RUNNING, &mac->flags);

	plfxlc_usb_stop(usb);

	return 0;
}

static int post_reset(struct usb_interface *intf)
{
	struct ieee80211_hw *hw = usb_get_intfdata(intf);
	struct plfxlc_mac *mac;
	struct plfxlc_usb *usb;

	if (!hw || intf->condition != USB_INTERFACE_BOUND)
		return 0;

	mac = plfxlc_hw_mac(hw);
	usb = &mac->chip.usb;

	if (usb->was_running)
		plfxlc_usb_resume(usb);

	return 0;
}

#ifdef CONFIG_PM

static struct plfxlc_usb *get_plfxlc_usb(struct usb_interface *intf)
{
	struct ieee80211_hw *hw = plfxlc_intf_to_hw(intf);
	struct plfxlc_mac *mac;

	/* Either something really bad happened, or
	 * we're just dealing with a DEVICE_INSTALLER.
	 */
	if (!hw)
		return NULL;

	mac = plfxlc_hw_mac(hw);
	return &mac->chip.usb;
}

static int suspend(struct usb_interface *interface,
		   pm_message_t message)
{
	struct plfxlc_usb *pl = get_plfxlc_usb(interface);
	struct plfxlc_mac *mac = plfxlc_usb_to_mac(pl);

	if (!pl)
		return -ENODEV;
	if (pl->initialized == 0)
		return 0;
	pl->was_running = test_bit(PURELIFI_DEVICE_RUNNING, &mac->flags);
	plfxlc_usb_stop(pl);
	return 0;
}

static int resume(struct usb_interface *interface)
{
	struct plfxlc_usb *pl = get_plfxlc_usb(interface);

	if (!pl)
		return -ENODEV;
	if (pl->was_running)
		plfxlc_usb_resume(pl);
	return 0;
}

#endif

static struct usb_driver driver = {
	.name = KBUILD_MODNAME,
	.id_table = usb_ids,
	.probe = probe,
	.disconnect = disconnect,
	.pre_reset = pre_reset,
	.post_reset = post_reset,
#ifdef CONFIG_PM
	.suspend = suspend,
	.resume = resume,
#endif
	.disable_hub_initiated_lpm = 1,
};

static int __init usb_init(void)
{
	int r;

	r = usb_register(&driver);
	if (r) {
		pr_err("%s usb_register() failed %d\n", driver.name, r);
		return r;
	}

	pr_debug("Driver initialized :%s\n", driver.name);
	return 0;
}

static void __exit usb_exit(void)
{
	usb_deregister(&driver);
	pr_debug("%s %s\n", driver.name, __func__);
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("USB driver for pureLiFi devices");
MODULE_AUTHOR("pureLiFi");
MODULE_VERSION("1.0");
MODULE_FIRMWARE("plfxlc/lifi-x.bin");
MODULE_DEVICE_TABLE(usb, usb_ids);

module_init(usb_init);
module_exit(usb_exit);
