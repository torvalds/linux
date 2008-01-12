/*
	Copyright (C) 2004 - 2007 rt2x00 SourceForge Project
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

/*
 * Set enviroment defines for rt2x00.h
 */
#define DRV_NAME "rt2x00usb"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "rt2x00.h"
#include "rt2x00usb.h"

/*
 * Interfacing with the HW.
 */
int rt2x00usb_vendor_request(const struct rt2x00_dev *rt2x00dev,
			     const u8 request, const u8 requesttype,
			     const u16 offset, const u16 value,
			     void *buffer, const u16 buffer_length,
			     const int timeout)
{
	struct usb_device *usb_dev =
	    interface_to_usbdev(rt2x00dev_usb(rt2x00dev));
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

int rt2x00usb_vendor_request_buff(const struct rt2x00_dev *rt2x00dev,
				  const u8 request, const u8 requesttype,
				  const u16 offset, void *buffer,
				  const u16 buffer_length, const int timeout)
{
	int status;

	/*
	 * Check for Cache availability.
	 */
	if (unlikely(!rt2x00dev->csr_cache || buffer_length > CSR_CACHE_SIZE)) {
		ERROR(rt2x00dev, "CSR cache not available.\n");
		return -ENOMEM;
	}

	if (requesttype == USB_VENDOR_REQUEST_OUT)
		memcpy(rt2x00dev->csr_cache, buffer, buffer_length);

	status = rt2x00usb_vendor_request(rt2x00dev, request, requesttype,
					  offset, 0, rt2x00dev->csr_cache,
					  buffer_length, timeout);

	if (!status && requesttype == USB_VENDOR_REQUEST_IN)
		memcpy(buffer, rt2x00dev->csr_cache, buffer_length);

	return status;
}
EXPORT_SYMBOL_GPL(rt2x00usb_vendor_request_buff);

/*
 * TX data handlers.
 */
static void rt2x00usb_interrupt_txdone(struct urb *urb)
{
	struct data_entry *entry = (struct data_entry *)urb->context;
	struct data_ring *ring = entry->ring;
	struct rt2x00_dev *rt2x00dev = ring->rt2x00dev;
	struct data_desc *txd = (struct data_desc *)entry->skb->data;
	u32 word;
	int tx_status;

	if (!test_bit(DEVICE_ENABLED_RADIO, &rt2x00dev->flags) ||
	    !__test_and_clear_bit(ENTRY_OWNER_NIC, &entry->flags))
		return;

	rt2x00_desc_read(txd, 0, &word);

	/*
	 * Remove the descriptor data from the buffer.
	 */
	skb_pull(entry->skb, ring->desc_size);

	/*
	 * Obtain the status about this packet.
	 */
	tx_status = !urb->status ? TX_SUCCESS : TX_FAIL_RETRY;

	rt2x00lib_txdone(entry, tx_status, 0);

	/*
	 * Make this entry available for reuse.
	 */
	entry->flags = 0;
	rt2x00_ring_index_done_inc(entry->ring);

	/*
	 * If the data ring was full before the txdone handler
	 * we must make sure the packet queue in the mac80211 stack
	 * is reenabled when the txdone handler has finished.
	 */
	if (!rt2x00_ring_full(ring))
		ieee80211_wake_queue(rt2x00dev->hw,
				     entry->tx_status.control.queue);
}

int rt2x00usb_write_tx_data(struct rt2x00_dev *rt2x00dev,
			    struct data_ring *ring, struct sk_buff *skb,
			    struct ieee80211_tx_control *control)
{
	struct usb_device *usb_dev =
	    interface_to_usbdev(rt2x00dev_usb(rt2x00dev));
	struct data_entry *entry = rt2x00_get_data_entry(ring);
	int pipe = usb_sndbulkpipe(usb_dev, 1);
	u32 length;

	if (rt2x00_ring_full(ring)) {
		ieee80211_stop_queue(rt2x00dev->hw, control->queue);
		return -EINVAL;
	}

	if (test_bit(ENTRY_OWNER_NIC, &entry->flags)) {
		ERROR(rt2x00dev,
		      "Arrived at non-free entry in the non-full queue %d.\n"
		      "Please file bug report to %s.\n",
		      control->queue, DRV_PROJECT);
		ieee80211_stop_queue(rt2x00dev->hw, control->queue);
		return -EINVAL;
	}

	/*
	 * Add the descriptor in front of the skb.
	 */
	skb_push(skb, ring->desc_size);
	memset(skb->data, 0, ring->desc_size);

	rt2x00lib_write_tx_desc(rt2x00dev, (struct data_desc *)skb->data,
				(struct ieee80211_hdr *)(skb->data +
							 ring->desc_size),
				skb->len - ring->desc_size, control);
	memcpy(&entry->tx_status.control, control, sizeof(*control));
	entry->skb = skb;

	/*
	 * USB devices cannot blindly pass the skb->len as the
	 * length of the data to usb_fill_bulk_urb. Pass the skb
	 * to the driver to determine what the length should be.
	 */
	length = rt2x00dev->ops->lib->get_tx_data_len(rt2x00dev, skb);

	/*
	 * Initialize URB and send the frame to the device.
	 */
	__set_bit(ENTRY_OWNER_NIC, &entry->flags);
	usb_fill_bulk_urb(entry->priv, usb_dev, pipe,
			  skb->data, length, rt2x00usb_interrupt_txdone, entry);
	usb_submit_urb(entry->priv, GFP_ATOMIC);

	rt2x00_ring_index_inc(ring);

	if (rt2x00_ring_full(ring))
		ieee80211_stop_queue(rt2x00dev->hw, control->queue);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00usb_write_tx_data);

/*
 * RX data handlers.
 */
static void rt2x00usb_interrupt_rxdone(struct urb *urb)
{
	struct data_entry *entry = (struct data_entry *)urb->context;
	struct data_ring *ring = entry->ring;
	struct rt2x00_dev *rt2x00dev = ring->rt2x00dev;
	struct sk_buff *skb;
	struct ieee80211_hdr *hdr;
	struct rxdata_entry_desc desc;
	int header_size;
	int frame_size;

	if (!test_bit(DEVICE_ENABLED_RADIO, &rt2x00dev->flags) ||
	    !test_and_clear_bit(ENTRY_OWNER_NIC, &entry->flags))
		return;

	/*
	 * Check if the received data is simply too small
	 * to be actually valid, or if the urb is signaling
	 * a problem.
	 */
	if (urb->actual_length < entry->ring->desc_size || urb->status)
		goto skip_entry;

	memset(&desc, 0x00, sizeof(desc));
	rt2x00dev->ops->lib->fill_rxdone(entry, &desc);

	/*
	 * Allocate a new sk buffer to replace the current one.
	 * If allocation fails, we should drop the current frame
	 * so we can recycle the existing sk buffer for the new frame.
	 */
	frame_size = entry->ring->data_size + entry->ring->desc_size;
	skb = dev_alloc_skb(frame_size + NET_IP_ALIGN);
	if (!skb)
		goto skip_entry;

	skb_reserve(skb, NET_IP_ALIGN);
	skb_put(skb, frame_size);

	/*
	 * The data behind the ieee80211 header must be
	 * aligned on a 4 byte boundary.
	 * After that trim the entire buffer down to only
	 * contain the valid frame data excluding the device
	 * descriptor.
	 */
	hdr = (struct ieee80211_hdr *)entry->skb->data;
	header_size =
	    ieee80211_get_hdrlen(le16_to_cpu(hdr->frame_control));

	if (header_size % 4 == 0) {
		skb_push(entry->skb, 2);
		memmove(entry->skb->data, entry->skb->data + 2, skb->len - 2);
	}
	skb_trim(entry->skb, desc.size);

	/*
	 * Send the frame to rt2x00lib for further processing.
	 */
	rt2x00lib_rxdone(entry, entry->skb, &desc);

	/*
	 * Replace current entry's skb with the newly allocated one,
	 * and reinitialize the urb.
	 */
	entry->skb = skb;
	urb->transfer_buffer = entry->skb->data;
	urb->transfer_buffer_length = entry->skb->len;

skip_entry:
	if (test_bit(DEVICE_ENABLED_RADIO, &ring->rt2x00dev->flags)) {
		__set_bit(ENTRY_OWNER_NIC, &entry->flags);
		usb_submit_urb(urb, GFP_ATOMIC);
	}

	rt2x00_ring_index_inc(ring);
}

/*
 * Radio handlers
 */
void rt2x00usb_enable_radio(struct rt2x00_dev *rt2x00dev)
{
	struct usb_device *usb_dev =
	    interface_to_usbdev(rt2x00dev_usb(rt2x00dev));
	struct data_ring *ring;
	struct data_entry *entry;
	unsigned int i;

	/*
	 * Initialize the TX rings
	 */
	txringall_for_each(rt2x00dev, ring) {
		for (i = 0; i < ring->stats.limit; i++)
			ring->entry[i].flags = 0;

		rt2x00_ring_index_clear(ring);
	}

	/*
	 * Initialize and start the RX ring.
	 */
	rt2x00_ring_index_clear(rt2x00dev->rx);

	for (i = 0; i < rt2x00dev->rx->stats.limit; i++) {
		entry = &rt2x00dev->rx->entry[i];

		usb_fill_bulk_urb(entry->priv, usb_dev,
				  usb_rcvbulkpipe(usb_dev, 1),
				  entry->skb->data, entry->skb->len,
				  rt2x00usb_interrupt_rxdone, entry);

		__set_bit(ENTRY_OWNER_NIC, &entry->flags);
		usb_submit_urb(entry->priv, GFP_ATOMIC);
	}
}
EXPORT_SYMBOL_GPL(rt2x00usb_enable_radio);

void rt2x00usb_disable_radio(struct rt2x00_dev *rt2x00dev)
{
	struct data_ring *ring;
	unsigned int i;

	rt2x00usb_vendor_request_sw(rt2x00dev, USB_RX_CONTROL, 0x0000, 0x0000,
				    REGISTER_TIMEOUT);

	/*
	 * Cancel all rings.
	 */
	ring_for_each(rt2x00dev, ring) {
		for (i = 0; i < ring->stats.limit; i++)
			usb_kill_urb(ring->entry[i].priv);
	}
}
EXPORT_SYMBOL_GPL(rt2x00usb_disable_radio);

/*
 * Device initialization handlers.
 */
static int rt2x00usb_alloc_urb(struct rt2x00_dev *rt2x00dev,
			       struct data_ring *ring)
{
	unsigned int i;

	/*
	 * Allocate the URB's
	 */
	for (i = 0; i < ring->stats.limit; i++) {
		ring->entry[i].priv = usb_alloc_urb(0, GFP_KERNEL);
		if (!ring->entry[i].priv)
			return -ENOMEM;
	}

	return 0;
}

static void rt2x00usb_free_urb(struct rt2x00_dev *rt2x00dev,
			       struct data_ring *ring)
{
	unsigned int i;

	if (!ring->entry)
		return;

	for (i = 0; i < ring->stats.limit; i++) {
		usb_kill_urb(ring->entry[i].priv);
		usb_free_urb(ring->entry[i].priv);
		if (ring->entry[i].skb)
			kfree_skb(ring->entry[i].skb);
	}
}

int rt2x00usb_initialize(struct rt2x00_dev *rt2x00dev)
{
	struct data_ring *ring;
	struct sk_buff *skb;
	unsigned int entry_size;
	unsigned int i;
	int status;

	/*
	 * Allocate DMA
	 */
	ring_for_each(rt2x00dev, ring) {
		status = rt2x00usb_alloc_urb(rt2x00dev, ring);
		if (status)
			goto exit;
	}

	/*
	 * For the RX ring, skb's should be allocated.
	 */
	entry_size = rt2x00dev->rx->data_size + rt2x00dev->rx->desc_size;
	for (i = 0; i < rt2x00dev->rx->stats.limit; i++) {
		skb = dev_alloc_skb(NET_IP_ALIGN + entry_size);
		if (!skb)
			goto exit;

		skb_reserve(skb, NET_IP_ALIGN);
		skb_put(skb, entry_size);

		rt2x00dev->rx->entry[i].skb = skb;
	}

	return 0;

exit:
	rt2x00usb_uninitialize(rt2x00dev);

	return status;
}
EXPORT_SYMBOL_GPL(rt2x00usb_initialize);

void rt2x00usb_uninitialize(struct rt2x00_dev *rt2x00dev)
{
	struct data_ring *ring;

	ring_for_each(rt2x00dev, ring)
		rt2x00usb_free_urb(rt2x00dev, ring);
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

	kfree(rt2x00dev->csr_cache);
	rt2x00dev->csr_cache = NULL;
}

static int rt2x00usb_alloc_reg(struct rt2x00_dev *rt2x00dev)
{
	rt2x00dev->csr_cache = kzalloc(CSR_CACHE_SIZE, GFP_KERNEL);
	if (!rt2x00dev->csr_cache)
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
 * rt2x00pci module information.
 */
MODULE_AUTHOR(DRV_PROJECT);
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("rt2x00 library");
MODULE_LICENSE("GPL");
