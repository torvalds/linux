/*
	Copyright (C) 2004 - 2008 rt2x00 SourceForge Project
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
	struct usb_device *usb_dev = rt2x00dev_usb_dev(rt2x00dev);
	int status;
	unsigned int i;
	unsigned int pipe =
	    (requesttype == USB_VENDOR_REQUEST_IN) ?
	    usb_rcvctrlpipe(usb_dev, 0) : usb_sndctrlpipe(usb_dev, 0);


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
		else if (status == -ENODEV)
			break;
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

	BUG_ON(!mutex_is_locked(&rt2x00dev->usb_cache_mutex));

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
	int status;

	mutex_lock(&rt2x00dev->usb_cache_mutex);

	status = rt2x00usb_vendor_req_buff_lock(rt2x00dev, request,
						requesttype, offset, buffer,
						buffer_length, timeout);

	mutex_unlock(&rt2x00dev->usb_cache_mutex);

	return status;
}
EXPORT_SYMBOL_GPL(rt2x00usb_vendor_request_buff);

/*
 * TX data handlers.
 */
static void rt2x00usb_interrupt_txdone(struct urb *urb)
{
	struct queue_entry *entry = (struct queue_entry *)urb->context;
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct txdone_entry_desc txdesc;
	__le32 *txd = (__le32 *)entry->skb->data;
	enum data_queue_qid qid = skb_get_queue_mapping(entry->skb);
	u32 word;

	if (!test_bit(DEVICE_ENABLED_RADIO, &rt2x00dev->flags) ||
	    !__test_and_clear_bit(ENTRY_OWNER_DEVICE_DATA, &entry->flags))
		return;

	rt2x00_desc_read(txd, 0, &word);

	/*
	 * Remove the descriptor data from the buffer.
	 */
	skb_pull(entry->skb, entry->queue->desc_size);

	/*
	 * Obtain the status about this packet.
	 * Note that when the status is 0 it does not mean the
	 * frame was send out correctly. It only means the frame
	 * was succesfully pushed to the hardware, we have no
	 * way to determine the transmission status right now.
	 * (Only indirectly by looking at the failed TX counters
	 * in the register).
	 */
	if (!urb->status)
		__set_bit(TXDONE_UNKNOWN, &txdesc.flags);
	else
		__set_bit(TXDONE_FAILURE, &txdesc.flags);
	txdesc.retry = 0;

	rt2x00lib_txdone(entry, &txdesc);

	/*
	 * Make this entry available for reuse.
	 */
	entry->flags = 0;
	rt2x00queue_index_inc(entry->queue, Q_INDEX_DONE);

	/*
	 * If the data queue was full before the txdone handler
	 * we must make sure the packet queue in the mac80211 stack
	 * is reenabled when the txdone handler has finished.
	 */
	if (!rt2x00queue_full(entry->queue))
		ieee80211_wake_queue(rt2x00dev->hw, qid);
}

int rt2x00usb_write_tx_data(struct rt2x00_dev *rt2x00dev,
			    struct data_queue *queue, struct sk_buff *skb)
{
	struct usb_device *usb_dev = rt2x00dev_usb_dev(rt2x00dev);
	struct queue_entry *entry = rt2x00queue_get_entry(queue, Q_INDEX);
	struct queue_entry_priv_usb *entry_priv = entry->priv_data;
	struct skb_frame_desc *skbdesc;
	struct txentry_desc txdesc;
	u32 length;

	if (rt2x00queue_full(queue))
		return -EINVAL;

	if (test_bit(ENTRY_OWNER_DEVICE_DATA, &entry->flags)) {
		ERROR(rt2x00dev,
		      "Arrived at non-free entry in the non-full queue %d.\n"
		      "Please file bug report to %s.\n",
		      entry->queue->qid, DRV_PROJECT);
		return -EINVAL;
	}

	/*
	 * Copy all TX descriptor information into txdesc,
	 * after that we are free to use the skb->cb array
	 * for our information.
	 */
	entry->skb = skb;
	rt2x00queue_create_tx_descriptor(entry, &txdesc);

	/*
	 * Add the descriptor in front of the skb.
	 */
	skb_push(skb, queue->desc_size);
	memset(skb->data, 0, queue->desc_size);

	/*
	 * Fill in skb descriptor
	 */
	skbdesc = get_skb_frame_desc(skb);
	memset(skbdesc, 0, sizeof(*skbdesc));
	skbdesc->data = skb->data + queue->desc_size;
	skbdesc->data_len = skb->len - queue->desc_size;
	skbdesc->desc = skb->data;
	skbdesc->desc_len = queue->desc_size;
	skbdesc->entry = entry;

	rt2x00queue_write_tx_descriptor(entry, &txdesc);

	/*
	 * USB devices cannot blindly pass the skb->len as the
	 * length of the data to usb_fill_bulk_urb. Pass the skb
	 * to the driver to determine what the length should be.
	 */
	length = rt2x00dev->ops->lib->get_tx_data_len(rt2x00dev, skb);

	/*
	 * Initialize URB and send the frame to the device.
	 */
	__set_bit(ENTRY_OWNER_DEVICE_DATA, &entry->flags);
	usb_fill_bulk_urb(entry_priv->urb, usb_dev, usb_sndbulkpipe(usb_dev, 1),
			  skb->data, length, rt2x00usb_interrupt_txdone, entry);
	usb_submit_urb(entry_priv->urb, GFP_ATOMIC);

	rt2x00queue_index_inc(queue, Q_INDEX);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00usb_write_tx_data);

/*
 * RX data handlers.
 */
static struct sk_buff* rt2x00usb_alloc_rxskb(struct data_queue *queue)
{
	struct sk_buff *skb;
	unsigned int frame_size;
	unsigned int reserved_size;

	/*
	 * The frame size includes descriptor size, because the
	 * hardware directly receive the frame into the skbuffer.
	 */
	frame_size = queue->data_size + queue->desc_size;

	/*
	 * For the allocation we should keep a few things in mind:
	 * 1) 4byte alignment of 802.11 payload
	 *
	 * For (1) we need at most 4 bytes to guarentee the correct
	 * alignment. We are going to optimize the fact that the chance
	 * that the 802.11 header_size % 4 == 2 is much bigger then
	 * anything else. However since we need to move the frame up
	 * to 3 bytes to the front, which means we need to preallocate
	 * 6 bytes.
	 */
	reserved_size = 6;

	/*
	 * Allocate skbuffer.
	 */
	skb = dev_alloc_skb(frame_size + reserved_size);
	if (!skb)
		return NULL;

	skb_reserve(skb, reserved_size);
	skb_put(skb, frame_size);

	return skb;
}

static void rt2x00usb_interrupt_rxdone(struct urb *urb)
{
	struct queue_entry *entry = (struct queue_entry *)urb->context;
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct sk_buff *skb;
	struct skb_frame_desc *skbdesc;
	struct rxdone_entry_desc rxdesc;
	unsigned int header_size;
	unsigned int align;

	if (!test_bit(DEVICE_ENABLED_RADIO, &rt2x00dev->flags) ||
	    !test_and_clear_bit(ENTRY_OWNER_DEVICE_DATA, &entry->flags))
		return;

	/*
	 * Check if the received data is simply too small
	 * to be actually valid, or if the urb is signaling
	 * a problem.
	 */
	if (urb->actual_length < entry->queue->desc_size || urb->status)
		goto skip_entry;

	/*
	 * Fill in skb descriptor
	 */
	skbdesc = get_skb_frame_desc(entry->skb);
	memset(skbdesc, 0, sizeof(*skbdesc));
	skbdesc->entry = entry;

	memset(&rxdesc, 0, sizeof(rxdesc));
	rt2x00dev->ops->lib->fill_rxdone(entry, &rxdesc);

	header_size = ieee80211_get_hdrlen_from_skb(entry->skb);

	/*
	 * The data behind the ieee80211 header must be
	 * aligned on a 4 byte boundary. We already reserved
	 * 2 bytes for header_size % 4 == 2 optimization.
	 * To determine the number of bytes which the data
	 * should be moved to the left, we must add these
	 * 2 bytes to the header_size.
	 */
	align = (header_size + 2) % 4;

	if (align) {
		skb_push(entry->skb, align);
		/* Move entire frame in 1 command */
		memmove(entry->skb->data, entry->skb->data + align,
			rxdesc.size);
	}

	/* Update data pointers, trim buffer to correct size */
	skbdesc->data = entry->skb->data;
	skb_trim(entry->skb, rxdesc.size);

	/*
	 * Allocate a new sk buffer to replace the current one.
	 * If allocation fails, we should drop the current frame
	 * so we can recycle the existing sk buffer for the new frame.
	 */
	skb = rt2x00usb_alloc_rxskb(entry->queue);
	if (!skb)
		goto skip_entry;

	/*
	 * Send the frame to rt2x00lib for further processing.
	 */
	rt2x00lib_rxdone(entry, &rxdesc);

	/*
	 * Replace current entry's skb with the newly allocated one,
	 * and reinitialize the urb.
	 */
	entry->skb = skb;
	urb->transfer_buffer = entry->skb->data;
	urb->transfer_buffer_length = entry->skb->len;

skip_entry:
	if (test_bit(DEVICE_ENABLED_RADIO, &entry->queue->rt2x00dev->flags)) {
		__set_bit(ENTRY_OWNER_DEVICE_DATA, &entry->flags);
		usb_submit_urb(urb, GFP_ATOMIC);
	}

	rt2x00queue_index_inc(entry->queue, Q_INDEX);
}

/*
 * Radio handlers
 */
void rt2x00usb_disable_radio(struct rt2x00_dev *rt2x00dev)
{
	struct queue_entry_priv_usb *entry_priv;
	struct queue_entry_priv_usb_bcn *bcn_priv;
	unsigned int i;

	rt2x00usb_vendor_request_sw(rt2x00dev, USB_RX_CONTROL, 0, 0,
				    REGISTER_TIMEOUT);

	/*
	 * Cancel all queues.
	 */
	for (i = 0; i < rt2x00dev->rx->limit; i++) {
		entry_priv = rt2x00dev->rx->entries[i].priv_data;
		usb_kill_urb(entry_priv->urb);
	}

	/*
	 * Kill guardian urb.
	 */
	for (i = 0; i < rt2x00dev->bcn->limit; i++) {
		bcn_priv = rt2x00dev->bcn->entries[i].priv_data;
		if (bcn_priv->guardian_urb)
			usb_kill_urb(bcn_priv->guardian_urb);
	}
}
EXPORT_SYMBOL_GPL(rt2x00usb_disable_radio);

/*
 * Device initialization handlers.
 */
void rt2x00usb_init_rxentry(struct rt2x00_dev *rt2x00dev,
			    struct queue_entry *entry)
{
	struct usb_device *usb_dev = rt2x00dev_usb_dev(rt2x00dev);
	struct queue_entry_priv_usb *entry_priv = entry->priv_data;

	usb_fill_bulk_urb(entry_priv->urb, usb_dev,
			  usb_rcvbulkpipe(usb_dev, 1),
			  entry->skb->data, entry->skb->len,
			  rt2x00usb_interrupt_rxdone, entry);

	__set_bit(ENTRY_OWNER_DEVICE_DATA, &entry->flags);
	usb_submit_urb(entry_priv->urb, GFP_ATOMIC);
}
EXPORT_SYMBOL_GPL(rt2x00usb_init_rxentry);

void rt2x00usb_init_txentry(struct rt2x00_dev *rt2x00dev,
			    struct queue_entry *entry)
{
	entry->flags = 0;
}
EXPORT_SYMBOL_GPL(rt2x00usb_init_txentry);

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
		if (queue->entries[i].skb)
			kfree_skb(queue->entries[i].skb);
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
	struct sk_buff *skb;
	unsigned int entry_size;
	unsigned int i;
	int uninitialized_var(status);

	/*
	 * Allocate DMA
	 */
	queue_for_each(rt2x00dev, queue) {
		status = rt2x00usb_alloc_urb(rt2x00dev, queue);
		if (status)
			goto exit;
	}

	/*
	 * For the RX queue, skb's should be allocated.
	 */
	entry_size = rt2x00dev->rx->data_size + rt2x00dev->rx->desc_size;
	for (i = 0; i < rt2x00dev->rx->limit; i++) {
		skb = rt2x00usb_alloc_rxskb(rt2x00dev->rx);
		if (!skb)
			goto exit;

		rt2x00dev->rx->entries[i].skb = skb;
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
	rt2x00dev->dev = usb_intf;
	rt2x00dev->ops = ops;
	rt2x00dev->hw = hw;
	mutex_init(&rt2x00dev->usb_cache_mutex);

	rt2x00dev->usb_maxpacket =
	    usb_maxpacket(usb_dev, usb_sndbulkpipe(usb_dev, 1), 1);
	if (!rt2x00dev->usb_maxpacket)
		rt2x00dev->usb_maxpacket = 1;

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

	rt2x00usb_free_reg(rt2x00dev);

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
	int retval;

	usb_get_dev(interface_to_usbdev(usb_intf));

	retval = rt2x00usb_alloc_reg(rt2x00dev);
	if (retval)
		return retval;

	retval = rt2x00lib_resume(rt2x00dev);
	if (retval)
		goto exit_free_reg;

	return 0;

exit_free_reg:
	rt2x00usb_free_reg(rt2x00dev);

	return retval;
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
