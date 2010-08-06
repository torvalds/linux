/*
	Copyright (C) 2004 - 2009 Ivo van Doorn <IvDoorn@gmail.com>
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: rt2x00usb
	Abstract: rt2x00 generic usb device routines.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/bug.h>

#include "rt2x00.h"
#include "rt2x00usb.h"

/*
 * Interfacing with the HW.
 */
int rt2x00usb_vendor_request(struct rt2x00_dev *rt2x00dev,
			     const u8 request, const u8 requesttype,
			     const u16 offset, const u16 value,
			     void *buffer, const u16 buffer_length,
			     const int timeout)
{
	struct usb_device *usb_dev = to_usb_device_intf(rt2x00dev->dev);
	int status;
	unsigned int i;
	unsigned int pipe =
	    (requesttype == USB_VENDOR_REQUEST_IN) ?
	    usb_rcvctrlpipe(usb_dev, 0) : usb_sndctrlpipe(usb_dev, 0);

	if (!test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags))
		return -ENODEV;

	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		status = usb_control_msg(usb_dev, pipe, request, requesttype,
					 value, offset, buffer, buffer_length,
					 timeout);
		if (status >= 0)
			return 0;

		/*
		 * Check for errors
		 * -ENODEV: Device has disappeared, no point continuing.
		 * All other errors: Try again.
		 */
		else if (status == -ENODEV) {
			clear_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags);
			break;
		}
	}

	ERROR(rt2x00dev,
	      "Vendor Request 0x%02x failed for offset 0x%04x with error %d.\n",
	      request, offset, status);

	return status;
}
EXPORT_SYMBOL_GPL(rt2x00usb_vendor_request);

int rt2x00usb_vendor_req_buff_lock(struct rt2x00_dev *rt2x00dev,
				   const u8 request, const u8 requesttype,
				   const u16 offset, void *buffer,
				   const u16 buffer_length, const int timeout)
{
	int status;

	BUG_ON(!mutex_is_locked(&rt2x00dev->csr_mutex));

	/*
	 * Check for Cache availability.
	 */
	if (unlikely(!rt2x00dev->csr.cache || buffer_length > CSR_CACHE_SIZE)) {
		ERROR(rt2x00dev, "CSR cache not available.\n");
		return -ENOMEM;
	}

	if (requesttype == USB_VENDOR_REQUEST_OUT)
		memcpy(rt2x00dev->csr.cache, buffer, buffer_length);

	status = rt2x00usb_vendor_request(rt2x00dev, request, requesttype,
					  offset, 0, rt2x00dev->csr.cache,
					  buffer_length, timeout);

	if (!status && requesttype == USB_VENDOR_REQUEST_IN)
		memcpy(buffer, rt2x00dev->csr.cache, buffer_length);

	return status;
}
EXPORT_SYMBOL_GPL(rt2x00usb_vendor_req_buff_lock);

int rt2x00usb_vendor_request_buff(struct rt2x00_dev *rt2x00dev,
				  const u8 request, const u8 requesttype,
				  const u16 offset, void *buffer,
				  const u16 buffer_length, const int timeout)
{
	int status = 0;
	unsigned char *tb;
	u16 off, len, bsize;

	mutex_lock(&rt2x00dev->csr_mutex);

	tb  = (char *)buffer;
	off = offset;
	len = buffer_length;
	while (len && !status) {
		bsize = min_t(u16, CSR_CACHE_SIZE, len);
		status = rt2x00usb_vendor_req_buff_lock(rt2x00dev, request,
							requesttype, off, tb,
							bsize, timeout);

		tb  += bsize;
		len -= bsize;
		off += bsize;
	}

	mutex_unlock(&rt2x00dev->csr_mutex);

	return status;
}
EXPORT_SYMBOL_GPL(rt2x00usb_vendor_request_buff);

int rt2x00usb_regbusy_read(struct rt2x00_dev *rt2x00dev,
			   const unsigned int offset,
			   const struct rt2x00_field32 field,
			   u32 *reg)
{
	unsigned int i;

	if (!test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags))
		return -ENODEV;

	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2x00usb_register_read_lock(rt2x00dev, offset, reg);
		if (!rt2x00_get_field32(*reg, field))
			return 1;
		udelay(REGISTER_BUSY_DELAY);
	}

	ERROR(rt2x00dev, "Indirect register access failed: "
	      "offset=0x%.08x, value=0x%.08x\n", offset, *reg);
	*reg = ~0;

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00usb_regbusy_read);

/*
 * TX data handlers.
 */
static void rt2x00usb_interrupt_txdone(struct urb *urb)
{
	struct queue_entry *entry = (struct queue_entry *)urb->context;
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct txdone_entry_desc txdesc;

	if (!test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags) ||
	    !test_bit(ENTRY_OWNER_DEVICE_DATA, &entry->flags))
		return;

	/*
	 * Obtain the status about this packet.
	 * Note that when the status is 0 it does not mean the
	 * frame was send out correctly. It only means the frame
	 * was succesfully pushed to the hardware, we have no
	 * way to determine the transmission status right now.
	 * (Only indirectly by looking at the failed TX counters
	 * in the register).
	 */
	txdesc.flags = 0;
	if (!urb->status)
		__set_bit(TXDONE_UNKNOWN, &txdesc.flags);
	else
		__set_bit(TXDONE_FAILURE, &txdesc.flags);
	txdesc.retry = 0;

	rt2x00lib_txdone(entry, &txdesc);
}

static inline void rt2x00usb_kick_tx_entry(struct queue_entry *entry)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct usb_device *usb_dev = to_usb_device_intf(rt2x00dev->dev);
	struct queue_entry_priv_usb *entry_priv = entry->priv_data;
	u32 length;

	if (test_and_clear_bit(ENTRY_DATA_PENDING, &entry->flags)) {
		/*
		 * USB devices cannot blindly pass the skb->len as the
		 * length of the data to usb_fill_bulk_urb. Pass the skb
		 * to the driver to determine what the length should be.
		 */
		length = rt2x00dev->ops->lib->get_tx_data_len(entry);

		usb_fill_bulk_urb(entry_priv->urb, usb_dev,
				  usb_sndbulkpipe(usb_dev, entry->queue->usb_endpoint),
				  entry->skb->data, length,
				  rt2x00usb_interrupt_txdone, entry);

		usb_submit_urb(entry_priv->urb, GFP_ATOMIC);
	}
}

void rt2x00usb_kick_tx_queue(struct rt2x00_dev *rt2x00dev,
			     const enum data_queue_qid qid)
{
	struct data_queue *queue = rt2x00queue_get_queue(rt2x00dev, qid);
	unsigned long irqflags;
	unsigned int index;
	unsigned int index_done;
	unsigned int i;

	/*
	 * Only protect the range we are going to loop over,
	 * if during our loop a extra entry is set to pending
	 * it should not be kicked during this run, since it
	 * is part of another TX operation.
	 */
	spin_lock_irqsave(&queue->lock, irqflags);
	index = queue->index[Q_INDEX];
	index_done = queue->index[Q_INDEX_DONE];
	spin_unlock_irqrestore(&queue->lock, irqflags);

	/*
	 * Start from the TX done pointer, this guarentees that we will
	 * send out all frames in the correct order.
	 */
	if (index_done < index) {
		for (i = index_done; i < index; i++)
			rt2x00usb_kick_tx_entry(&queue->entries[i]);
	} else {
		for (i = index_done; i < queue->limit; i++)
			rt2x00usb_kick_tx_entry(&queue->entries[i]);

		for (i = 0; i < index; i++)
			rt2x00usb_kick_tx_entry(&queue->entries[i]);
	}
}
EXPORT_SYMBOL_GPL(rt2x00usb_kick_tx_queue);

void rt2x00usb_kill_tx_queue(struct rt2x00_dev *rt2x00dev,
			     const enum data_queue_qid qid)
{
	struct data_queue *queue = rt2x00queue_get_queue(rt2x00dev, qid);
	struct queue_entry_priv_usb *entry_priv;
	struct queue_entry_priv_usb_bcn *bcn_priv;
	unsigned int i;
	bool kill_guard;

	/*
	 * When killing the beacon queue, we must also kill
	 * the beacon guard byte.
	 */
	kill_guard =
	    (qid == QID_BEACON) &&
	    (test_bit(DRIVER_REQUIRE_BEACON_GUARD, &rt2x00dev->flags));

	/*
	 * Cancel all entries.
	 */
	for (i = 0; i < queue->limit; i++) {
		entry_priv = queue->entries[i].priv_data;
		usb_kill_urb(entry_priv->urb);

		/*
		 * Kill guardian urb (if required by driver).
		 */
		if (kill_guard) {
			bcn_priv = queue->entries[i].priv_data;
			usb_kill_urb(bcn_priv->guardian_urb);
		}
	}
}
EXPORT_SYMBOL_GPL(rt2x00usb_kill_tx_queue);

static void rt2x00usb_watchdog_reset_tx(struct data_queue *queue)
{
	struct queue_entry_priv_usb *entry_priv;
	unsigned short threshold = queue->threshold;

	WARNING(queue->rt2x00dev, "TX queue %d timed out, invoke reset", queue->qid);

	/*
	 * Temporarily disable the TX queue, this will force mac80211
	 * to use the other queues until this queue has been restored.
	 *
	 * Set the queue threshold to the queue limit. This prevents the
	 * queue from being enabled during the txdone handler.
	 */
	queue->threshold = queue->limit;
	ieee80211_stop_queue(queue->rt2x00dev->hw, queue->qid);

	/*
	 * Reset all currently uploaded TX frames.
	 */
	while (!rt2x00queue_empty(queue)) {
		entry_priv = rt2x00queue_get_entry(queue, Q_INDEX_DONE)->priv_data;
		usb_kill_urb(entry_priv->urb);

		/*
		 * We need a short delay here to wait for
		 * the URB to be canceled and invoked the tx_done handler.
		 */
		udelay(200);
	}

	/*
	 * The queue has been reset, and mac80211 is allowed to use the
	 * queue again.
	 */
	queue->threshold = threshold;
	ieee80211_wake_queue(queue->rt2x00dev->hw, queue->qid);
}

void rt2x00usb_watchdog(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;

	tx_queue_for_each(rt2x00dev, queue) {
		if (rt2x00queue_timeout(queue))
			rt2x00usb_watchdog_reset_tx(queue);
	}
}
EXPORT_SYMBOL_GPL(rt2x00usb_watchdog);

/*
 * RX data handlers.
 */
static void rt2x00usb_interrupt_rxdone(struct urb *urb)
{
	struct queue_entry *entry = (struct queue_entry *)urb->context;
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(entry->skb);
	u8 rxd[32];

	if (!test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags) ||
	    !test_bit(ENTRY_OWNER_DEVICE_DATA, &entry->flags))
		return;

	/*
	 * Check if the received data is simply too small
	 * to be actually valid, or if the urb is signaling
	 * a problem.
	 */
	if (urb->actual_length < entry->queue->desc_size || urb->status) {
		set_bit(ENTRY_OWNER_DEVICE_DATA, &entry->flags);
		usb_submit_urb(urb, GFP_ATOMIC);
		return;
	}

	/*
	 * Fill in desc fields of the skb descriptor
	 */
	skbdesc->desc = rxd;
	skbdesc->desc_len = entry->queue->desc_size;

	/*
	 * Send the frame to rt2x00lib for further processing.
	 */
	rt2x00lib_rxdone(rt2x00dev, entry);
}

/*
 * Radio handlers
 */
void rt2x00usb_disable_radio(struct rt2x00_dev *rt2x00dev)
{
	rt2x00usb_vendor_request_sw(rt2x00dev, USB_RX_CONTROL, 0, 0,
				    REGISTER_TIMEOUT);

	/*
	 * The USB version of kill_tx_queue also works
	 * on the RX queue.
	 */
	rt2x00dev->ops->lib->kill_tx_queue(rt2x00dev, QID_RX);
}
EXPORT_SYMBOL_GPL(rt2x00usb_disable_radio);

/*
 * Device initialization handlers.
 */
void rt2x00usb_clear_entry(struct queue_entry *entry)
{
	struct usb_device *usb_dev =
	    to_usb_device_intf(entry->queue->rt2x00dev->dev);
	struct queue_entry_priv_usb *entry_priv = entry->priv_data;
	int pipe;

	if (entry->queue->qid == QID_RX) {
		pipe = usb_rcvbulkpipe(usb_dev, entry->queue->usb_endpoint);
		usb_fill_bulk_urb(entry_priv->urb, usb_dev, pipe,
				entry->skb->data, entry->skb->len,
				rt2x00usb_interrupt_rxdone, entry);

		set_bit(ENTRY_OWNER_DEVICE_DATA, &entry->flags);
		usb_submit_urb(entry_priv->urb, GFP_ATOMIC);
	} else {
		entry->flags = 0;
	}
}
EXPORT_SYMBOL_GPL(rt2x00usb_clear_entry);

static void rt2x00usb_assign_endpoint(struct data_queue *queue,
				      struct usb_endpoint_descriptor *ep_desc)
{
	struct usb_device *usb_dev = to_usb_device_intf(queue->rt2x00dev->dev);
	int pipe;

	queue->usb_endpoint = usb_endpoint_num(ep_desc);

	if (queue->qid == QID_RX) {
		pipe = usb_rcvbulkpipe(usb_dev, queue->usb_endpoint);
		queue->usb_maxpacket = usb_maxpacket(usb_dev, pipe, 0);
	} else {
		pipe = usb_sndbulkpipe(usb_dev, queue->usb_endpoint);
		queue->usb_maxpacket = usb_maxpacket(usb_dev, pipe, 1);
	}

	if (!queue->usb_maxpacket)
		queue->usb_maxpacket = 1;
}

static int rt2x00usb_find_endpoints(struct rt2x00_dev *rt2x00dev)
{
	struct usb_interface *intf = to_usb_interface(rt2x00dev->dev);
	struct usb_host_interface *intf_desc = intf->cur_altsetting;
	struct usb_endpoint_descriptor *ep_desc;
	struct data_queue *queue = rt2x00dev->tx;
	struct usb_endpoint_descriptor *tx_ep_desc = NULL;
	unsigned int i;

	/*
	 * Walk through all available endpoints to search for "bulk in"
	 * and "bulk out" endpoints. When we find such endpoints collect
	 * the information we need from the descriptor and assign it
	 * to the queue.
	 */
	for (i = 0; i < intf_desc->desc.bNumEndpoints; i++) {
		ep_desc = &intf_desc->endpoint[i].desc;

		if (usb_endpoint_is_bulk_in(ep_desc)) {
			rt2x00usb_assign_endpoint(rt2x00dev->rx, ep_desc);
		} else if (usb_endpoint_is_bulk_out(ep_desc) &&
			   (queue != queue_end(rt2x00dev))) {
			rt2x00usb_assign_endpoint(queue, ep_desc);
			queue = queue_next(queue);

			tx_ep_desc = ep_desc;
		}
	}

	/*
	 * At least 1 endpoint for RX and 1 endpoint for TX must be available.
	 */
	if (!rt2x00dev->rx->usb_endpoint || !rt2x00dev->tx->usb_endpoint) {
		ERROR(rt2x00dev, "Bulk-in/Bulk-out endpoints not found\n");
		return -EPIPE;
	}

	/*
	 * It might be possible not all queues have a dedicated endpoint.
	 * Loop through all TX queues and copy the endpoint information
	 * which we have gathered from already assigned endpoints.
	 */
	txall_queue_for_each(rt2x00dev, queue) {
		if (!queue->usb_endpoint)
			rt2x00usb_assign_endpoint(queue, tx_ep_desc);
	}

	return 0;
}

static int rt2x00usb_alloc_urb(struct rt2x00_dev *rt2x00dev,
			       struct data_queue *queue)
{
	struct queue_entry_priv_usb *entry_priv;
	struct queue_entry_priv_usb_bcn *bcn_priv;
	unsigned int i;

	for (i = 0; i < queue->limit; i++) {
		entry_priv = queue->entries[i].priv_data;
		entry_priv->urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!entry_priv->urb)
			return -ENOMEM;
	}

	/*
	 * If this is not the beacon queue or
	 * no guardian byte was required for the beacon,
	 * then we are done.
	 */
	if (rt2x00dev->bcn != queue ||
	    !test_bit(DRIVER_REQUIRE_BEACON_GUARD, &rt2x00dev->flags))
		return 0;

	for (i = 0; i < queue->limit; i++) {
		bcn_priv = queue->entries[i].priv_data;
		bcn_priv->guardian_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!bcn_priv->guardian_urb)
			return -ENOMEM;
	}

	return 0;
}

static void rt2x00usb_free_urb(struct rt2x00_dev *rt2x00dev,
			       struct data_queue *queue)
{
	struct queue_entry_priv_usb *entry_priv;
	struct queue_entry_priv_usb_bcn *bcn_priv;
	unsigned int i;

	if (!queue->entries)
		return;

	for (i = 0; i < queue->limit; i++) {
		entry_priv = queue->entries[i].priv_data;
		usb_kill_urb(entry_priv->urb);
		usb_free_urb(entry_priv->urb);
	}

	/*
	 * If this is not the beacon queue or
	 * no guardian byte was required for the beacon,
	 * then we are done.
	 */
	if (rt2x00dev->bcn != queue ||
	    !test_bit(DRIVER_REQUIRE_BEACON_GUARD, &rt2x00dev->flags))
		return;

	for (i = 0; i < queue->limit; i++) {
		bcn_priv = queue->entries[i].priv_data;
		usb_kill_urb(bcn_priv->guardian_urb);
		usb_free_urb(bcn_priv->guardian_urb);
	}
}

int rt2x00usb_initialize(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;
	int status;

	/*
	 * Find endpoints for each queue
	 */
	status = rt2x00usb_find_endpoints(rt2x00dev);
	if (status)
		goto exit;

	/*
	 * Allocate DMA
	 */
	queue_for_each(rt2x00dev, queue) {
		status = rt2x00usb_alloc_urb(rt2x00dev, queue);
		if (status)
			goto exit;
	}

	return 0;

exit:
	rt2x00usb_uninitialize(rt2x00dev);

	return status;
}
EXPORT_SYMBOL_GPL(rt2x00usb_initialize);

void rt2x00usb_uninitialize(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;

	queue_for_each(rt2x00dev, queue)
		rt2x00usb_free_urb(rt2x00dev, queue);
}
EXPORT_SYMBOL_GPL(rt2x00usb_uninitialize);

/*
 * USB driver handlers.
 */
static void rt2x00usb_free_reg(struct rt2x00_dev *rt2x00dev)
{
	kfree(rt2x00dev->rf);
	rt2x00dev->rf = NULL;

	kfree(rt2x00dev->eeprom);
	rt2x00dev->eeprom = NULL;

	kfree(rt2x00dev->csr.cache);
	rt2x00dev->csr.cache = NULL;
}

static int rt2x00usb_alloc_reg(struct rt2x00_dev *rt2x00dev)
{
	rt2x00dev->csr.cache = kzalloc(CSR_CACHE_SIZE, GFP_KERNEL);
	if (!rt2x00dev->csr.cache)
		goto exit;

	rt2x00dev->eeprom = kzalloc(rt2x00dev->ops->eeprom_size, GFP_KERNEL);
	if (!rt2x00dev->eeprom)
		goto exit;

	rt2x00dev->rf = kzalloc(rt2x00dev->ops->rf_size, GFP_KERNEL);
	if (!rt2x00dev->rf)
		goto exit;

	return 0;

exit:
	ERROR_PROBE("Failed to allocate registers.\n");

	rt2x00usb_free_reg(rt2x00dev);

	return -ENOMEM;
}

int rt2x00usb_probe(struct usb_interface *usb_intf,
		    const struct usb_device_id *id)
{
	struct usb_device *usb_dev = interface_to_usbdev(usb_intf);
	struct rt2x00_ops *ops = (struct rt2x00_ops *)id->driver_info;
	struct ieee80211_hw *hw;
	struct rt2x00_dev *rt2x00dev;
	int retval;

	usb_dev = usb_get_dev(usb_dev);

	hw = ieee80211_alloc_hw(sizeof(struct rt2x00_dev), ops->hw);
	if (!hw) {
		ERROR_PROBE("Failed to allocate hardware.\n");
		retval = -ENOMEM;
		goto exit_put_device;
	}

	usb_set_intfdata(usb_intf, hw);

	rt2x00dev = hw->priv;
	rt2x00dev->dev = &usb_intf->dev;
	rt2x00dev->ops = ops;
	rt2x00dev->hw = hw;

	rt2x00_set_chip_intf(rt2x00dev, RT2X00_CHIP_INTF_USB);

	retval = rt2x00usb_alloc_reg(rt2x00dev);
	if (retval)
		goto exit_free_device;

	retval = rt2x00lib_probe_dev(rt2x00dev);
	if (retval)
		goto exit_free_reg;

	return 0;

exit_free_reg:
	rt2x00usb_free_reg(rt2x00dev);

exit_free_device:
	ieee80211_free_hw(hw);

exit_put_device:
	usb_put_dev(usb_dev);

	usb_set_intfdata(usb_intf, NULL);

	return retval;
}
EXPORT_SYMBOL_GPL(rt2x00usb_probe);

void rt2x00usb_disconnect(struct usb_interface *usb_intf)
{
	struct ieee80211_hw *hw = usb_get_intfdata(usb_intf);
	struct rt2x00_dev *rt2x00dev = hw->priv;

	/*
	 * Free all allocated data.
	 */
	rt2x00lib_remove_dev(rt2x00dev);
	rt2x00usb_free_reg(rt2x00dev);
	ieee80211_free_hw(hw);

	/*
	 * Free the USB device data.
	 */
	usb_set_intfdata(usb_intf, NULL);
	usb_put_dev(interface_to_usbdev(usb_intf));
}
EXPORT_SYMBOL_GPL(rt2x00usb_disconnect);

#ifdef CONFIG_PM
int rt2x00usb_suspend(struct usb_interface *usb_intf, pm_message_t state)
{
	struct ieee80211_hw *hw = usb_get_intfdata(usb_intf);
	struct rt2x00_dev *rt2x00dev = hw->priv;
	int retval;

	retval = rt2x00lib_suspend(rt2x00dev, state);
	if (retval)
		return retval;

	/*
	 * Decrease usbdev refcount.
	 */
	usb_put_dev(interface_to_usbdev(usb_intf));

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00usb_suspend);

int rt2x00usb_resume(struct usb_interface *usb_intf)
{
	struct ieee80211_hw *hw = usb_get_intfdata(usb_intf);
	struct rt2x00_dev *rt2x00dev = hw->priv;

	usb_get_dev(interface_to_usbdev(usb_intf));

	return rt2x00lib_resume(rt2x00dev);
}
EXPORT_SYMBOL_GPL(rt2x00usb_resume);
#endif /* CONFIG_PM */

/*
 * rt2x00usb module information.
 */
MODULE_AUTHOR(DRV_PROJECT);
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("rt2x00 usb library");
MODULE_LICENSE("GPL");
