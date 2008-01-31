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
	Abstract: Data structures for the rt2x00usb module.
 */

#ifndef RT2X00USB_H
#define RT2X00USB_H

/*
 * This variable should be used with the
 * usb_driver structure initialization.
 */
#define USB_DEVICE_DATA(__ops)	.driver_info = (kernel_ulong_t)(__ops)

/*
 * Register defines.
 * Some registers require multiple attempts before success,
 * in those cases REGISTER_BUSY_COUNT attempts should be
 * taken with a REGISTER_BUSY_DELAY interval.
 * For USB vendor requests we need to pass a timeout
 * time in ms, for this we use the REGISTER_TIMEOUT,
 * however when loading firmware a higher value is
 * required. In that case we use the REGISTER_TIMEOUT_FIRMWARE.
 */
#define REGISTER_BUSY_COUNT		5
#define REGISTER_BUSY_DELAY		100
#define REGISTER_TIMEOUT		500
#define REGISTER_TIMEOUT_FIRMWARE	1000

/*
 * Cache size
 */
#define CSR_CACHE_SIZE			8
#define CSR_CACHE_SIZE_FIRMWARE		64

/*
 * USB request types.
 */
#define USB_VENDOR_REQUEST	( USB_TYPE_VENDOR | USB_RECIP_DEVICE )
#define USB_VENDOR_REQUEST_IN	( USB_DIR_IN | USB_VENDOR_REQUEST )
#define USB_VENDOR_REQUEST_OUT	( USB_DIR_OUT | USB_VENDOR_REQUEST )

/*
 * USB vendor commands.
 */
#define USB_DEVICE_MODE		0x01
#define USB_SINGLE_WRITE	0x02
#define USB_SINGLE_READ		0x03
#define USB_MULTI_WRITE		0x06
#define USB_MULTI_READ		0x07
#define USB_EEPROM_WRITE	0x08
#define USB_EEPROM_READ		0x09
#define USB_LED_CONTROL		0x0a	/* RT73USB */
#define USB_RX_CONTROL		0x0c

/*
 * Device modes offset
 */
#define USB_MODE_RESET		0x01
#define USB_MODE_UNPLUG		0x02
#define USB_MODE_FUNCTION	0x03
#define USB_MODE_TEST		0x04
#define USB_MODE_SLEEP		0x07	/* RT73USB */
#define USB_MODE_FIRMWARE	0x08	/* RT73USB */
#define USB_MODE_WAKEUP		0x09	/* RT73USB */

/*
 * Used to read/write from/to the device.
 * This is the main function to communicate with the device,
 * the buffer argument _must_ either be NULL or point to
 * a buffer allocated by kmalloc. Failure to do so can lead
 * to unexpected behavior depending on the architecture.
 */
int rt2x00usb_vendor_request(struct rt2x00_dev *rt2x00dev,
			     const u8 request, const u8 requesttype,
			     const u16 offset, const u16 value,
			     void *buffer, const u16 buffer_length,
			     const int timeout);

/*
 * Used to read/write from/to the device.
 * This function will use a previously with kmalloc allocated cache
 * to communicate with the device. The contents of the buffer pointer
 * will be copied to this cache when writing, or read from the cache
 * when reading.
 * Buffers send to rt2x00usb_vendor_request _must_ be allocated with
 * kmalloc. Hence the reason for using a previously allocated cache
 * which has been allocated properly.
 */
int rt2x00usb_vendor_request_buff(struct rt2x00_dev *rt2x00dev,
				  const u8 request, const u8 requesttype,
				  const u16 offset, void *buffer,
				  const u16 buffer_length, const int timeout);

/*
 * A version of rt2x00usb_vendor_request_buff which must be called
 * if the usb_cache_mutex is already held. */
int rt2x00usb_vendor_req_buff_lock(struct rt2x00_dev *rt2x00dev,
				   const u8 request, const u8 requesttype,
				   const u16 offset, void *buffer,
				   const u16 buffer_length, const int timeout);

/*
 * Simple wrapper around rt2x00usb_vendor_request to write a single
 * command to the device. Since we don't use the buffer argument we
 * don't have to worry about kmalloc here.
 */
static inline int rt2x00usb_vendor_request_sw(struct rt2x00_dev *rt2x00dev,
					      const u8 request,
					      const u16 offset,
					      const u16 value,
					      const int timeout)
{
	return rt2x00usb_vendor_request(rt2x00dev, request,
					USB_VENDOR_REQUEST_OUT, offset,
					value, NULL, 0, timeout);
}

/*
 * Simple wrapper around rt2x00usb_vendor_request to read the eeprom
 * from the device. Note that the eeprom argument _must_ be allocated using
 * kmalloc for correct handling inside the kernel USB layer.
 */
static inline int rt2x00usb_eeprom_read(struct rt2x00_dev *rt2x00dev,
					__le16 *eeprom, const u16 lenght)
{
	int timeout = REGISTER_TIMEOUT * (lenght / sizeof(u16));

	return rt2x00usb_vendor_request(rt2x00dev, USB_EEPROM_READ,
					USB_VENDOR_REQUEST_IN, 0x0000,
					0x0000, eeprom, lenght, timeout);
}

/*
 * Radio handlers
 */
void rt2x00usb_disable_radio(struct rt2x00_dev *rt2x00dev);

/*
 * TX data handlers.
 */
int rt2x00usb_write_tx_data(struct rt2x00_dev *rt2x00dev,
			    struct data_ring *ring, struct sk_buff *skb,
			    struct ieee80211_tx_control *control);

/*
 * Device initialization handlers.
 */
void rt2x00usb_init_rxentry(struct rt2x00_dev *rt2x00dev,
			    struct data_entry *entry);
void rt2x00usb_init_txentry(struct rt2x00_dev *rt2x00dev,
			    struct data_entry *entry);
int rt2x00usb_initialize(struct rt2x00_dev *rt2x00dev);
void rt2x00usb_uninitialize(struct rt2x00_dev *rt2x00dev);

/*
 * USB driver handlers.
 */
int rt2x00usb_probe(struct usb_interface *usb_intf,
		    const struct usb_device_id *id);
void rt2x00usb_disconnect(struct usb_interface *usb_intf);
#ifdef CONFIG_PM
int rt2x00usb_suspend(struct usb_interface *usb_intf, pm_message_t state);
int rt2x00usb_resume(struct usb_interface *usb_intf);
#else
#define rt2x00usb_suspend	NULL
#define rt2x00usb_resume	NULL
#endif /* CONFIG_PM */

#endif /* RT2X00USB_H */
