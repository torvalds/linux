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
	Abstract: Data structures for the rt2x00usb module.
 */

#ifndef RT2X00USB_H
#define RT2X00USB_H

#include <linux/usb.h>

#define to_usb_device_intf(d) \
({ \
	struct usb_interface *intf = to_usb_interface(d); \
	interface_to_usbdev(intf); \
})

/*
 * This variable should be used with the
 * usb_driver structure initialization.
 */
#define USB_DEVICE_DATA(__ops)	.driver_info = (kernel_ulong_t)(__ops)

/*
 * For USB vendor requests we need to pass a timeout
 * time in ms, for this we use the REGISTER_TIMEOUT,
 * however when loading firmware a higher value is
 * required. In that case we use the REGISTER_TIMEOUT_FIRMWARE.
 */
#define REGISTER_TIMEOUT		500
#define REGISTER_TIMEOUT_FIRMWARE	1000

/**
 * REGISTER_TIMEOUT16 - Determine the timeout for 16bit register access
 * @__datalen: Data length
 */
#define REGISTER_TIMEOUT16(__datalen)	\
	( REGISTER_TIMEOUT * ((__datalen) / sizeof(u16)) )

/**
 * REGISTER_TIMEOUT32 - Determine the timeout for 32bit register access
 * @__datalen: Data length
 */
#define REGISTER_TIMEOUT32(__datalen)	\
	( REGISTER_TIMEOUT * ((__datalen) / sizeof(u32)) )

/*
 * Cache size
 */
#define CSR_CACHE_SIZE			64

/*
 * USB request types.
 */
#define USB_VENDOR_REQUEST	( USB_TYPE_VENDOR | USB_RECIP_DEVICE )
#define USB_VENDOR_REQUEST_IN	( USB_DIR_IN | USB_VENDOR_REQUEST )
#define USB_VENDOR_REQUEST_OUT	( USB_DIR_OUT | USB_VENDOR_REQUEST )

/**
 * enum rt2x00usb_vendor_request: USB vendor commands.
 */
enum rt2x00usb_vendor_request {
	USB_DEVICE_MODE = 1,
	USB_SINGLE_WRITE = 2,
	USB_SINGLE_READ = 3,
	USB_MULTI_WRITE = 6,
	USB_MULTI_READ = 7,
	USB_EEPROM_WRITE = 8,
	USB_EEPROM_READ = 9,
	USB_LED_CONTROL = 10, /* RT73USB */
	USB_RX_CONTROL = 12,
};

/**
 * enum rt2x00usb_mode_offset: Device modes offset.
 */
enum rt2x00usb_mode_offset {
	USB_MODE_RESET = 1,
	USB_MODE_UNPLUG = 2,
	USB_MODE_FUNCTION = 3,
	USB_MODE_TEST = 4,
	USB_MODE_SLEEP = 7,	/* RT73USB */
	USB_MODE_FIRMWARE = 8,	/* RT73USB */
	USB_MODE_WAKEUP = 9,	/* RT73USB */
};

/**
 * rt2x00usb_vendor_request - Send register command to device
 * @rt2x00dev: Pointer to &struct rt2x00_dev
 * @request: USB vendor command (See &enum rt2x00usb_vendor_request)
 * @requesttype: Request type &USB_VENDOR_REQUEST_*
 * @offset: Register offset to perform action on
 * @value: Value to write to device
 * @buffer: Buffer where information will be read/written to by device
 * @buffer_length: Size of &buffer
 * @timeout: Operation timeout
 *
 * This is the main function to communicate with the device,
 * the &buffer argument _must_ either be NULL or point to
 * a buffer allocated by kmalloc. Failure to do so can lead
 * to unexpected behavior depending on the architecture.
 */
int rt2x00usb_vendor_request(struct rt2x00_dev *rt2x00dev,
			     const u8 request, const u8 requesttype,
			     const u16 offset, const u16 value,
			     void *buffer, const u16 buffer_length,
			     const int timeout);

/**
 * rt2x00usb_vendor_request_buff - Send register command to device (buffered)
 * @rt2x00dev: Pointer to &struct rt2x00_dev
 * @request: USB vendor command (See &enum rt2x00usb_vendor_request)
 * @requesttype: Request type &USB_VENDOR_REQUEST_*
 * @offset: Register offset to perform action on
 * @buffer: Buffer where information will be read/written to by device
 * @buffer_length: Size of &buffer
 * @timeout: Operation timeout
 *
 * This function will use a previously with kmalloc allocated cache
 * to communicate with the device. The contents of the buffer pointer
 * will be copied to this cache when writing, or read from the cache
 * when reading.
 * Buffers send to &rt2x00usb_vendor_request _must_ be allocated with
 * kmalloc. Hence the reason for using a previously allocated cache
 * which has been allocated properly.
 */
int rt2x00usb_vendor_request_buff(struct rt2x00_dev *rt2x00dev,
				  const u8 request, const u8 requesttype,
				  const u16 offset, void *buffer,
				  const u16 buffer_length, const int timeout);

/**
 * rt2x00usb_vendor_request_buff - Send register command to device (buffered)
 * @rt2x00dev: Pointer to &struct rt2x00_dev
 * @request: USB vendor command (See &enum rt2x00usb_vendor_request)
 * @requesttype: Request type &USB_VENDOR_REQUEST_*
 * @offset: Register offset to perform action on
 * @buffer: Buffer where information will be read/written to by device
 * @buffer_length: Size of &buffer
 * @timeout: Operation timeout
 *
 * A version of &rt2x00usb_vendor_request_buff which must be called
 * if the usb_cache_mutex is already held.
 */
int rt2x00usb_vendor_req_buff_lock(struct rt2x00_dev *rt2x00dev,
				   const u8 request, const u8 requesttype,
				   const u16 offset, void *buffer,
				   const u16 buffer_length, const int timeout);

/**
 * rt2x00usb_vendor_request_sw - Send single register command to device
 * @rt2x00dev: Pointer to &struct rt2x00_dev
 * @request: USB vendor command (See &enum rt2x00usb_vendor_request)
 * @offset: Register offset to perform action on
 * @value: Value to write to device
 * @timeout: Operation timeout
 *
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

/**
 * rt2x00usb_eeprom_read - Read eeprom from device
 * @rt2x00dev: Pointer to &struct rt2x00_dev
 * @eeprom: Pointer to eeprom array to store the information in
 * @length: Number of bytes to read from the eeprom
 *
 * Simple wrapper around rt2x00usb_vendor_request to read the eeprom
 * from the device. Note that the eeprom argument _must_ be allocated using
 * kmalloc for correct handling inside the kernel USB layer.
 */
static inline int rt2x00usb_eeprom_read(struct rt2x00_dev *rt2x00dev,
					__le16 *eeprom, const u16 length)
{
	return rt2x00usb_vendor_request(rt2x00dev, USB_EEPROM_READ,
					USB_VENDOR_REQUEST_IN, 0, 0,
					eeprom, length,
					REGISTER_TIMEOUT16(length));
}

/**
 * rt2x00usb_register_read - Read 32bit register word
 * @rt2x00dev: Device pointer, see &struct rt2x00_dev.
 * @offset: Register offset
 * @value: Pointer to where register contents should be stored
 *
 * This function is a simple wrapper for 32bit register access
 * through rt2x00usb_vendor_request_buff().
 */
static inline void rt2x00usb_register_read(struct rt2x00_dev *rt2x00dev,
					   const unsigned int offset,
					   u32 *value)
{
	__le32 reg;
	rt2x00usb_vendor_request_buff(rt2x00dev, USB_MULTI_READ,
				      USB_VENDOR_REQUEST_IN, offset,
				      &reg, sizeof(reg), REGISTER_TIMEOUT);
	*value = le32_to_cpu(reg);
}

/**
 * rt2x00usb_register_read_lock - Read 32bit register word
 * @rt2x00dev: Device pointer, see &struct rt2x00_dev.
 * @offset: Register offset
 * @value: Pointer to where register contents should be stored
 *
 * This function is a simple wrapper for 32bit register access
 * through rt2x00usb_vendor_req_buff_lock().
 */
static inline void rt2x00usb_register_read_lock(struct rt2x00_dev *rt2x00dev,
						const unsigned int offset,
						u32 *value)
{
	__le32 reg;
	rt2x00usb_vendor_req_buff_lock(rt2x00dev, USB_MULTI_READ,
				       USB_VENDOR_REQUEST_IN, offset,
				       &reg, sizeof(reg), REGISTER_TIMEOUT);
	*value = le32_to_cpu(reg);
}

/**
 * rt2x00usb_register_multiread - Read 32bit register words
 * @rt2x00dev: Device pointer, see &struct rt2x00_dev.
 * @offset: Register offset
 * @value: Pointer to where register contents should be stored
 * @length: Length of the data
 *
 * This function is a simple wrapper for 32bit register access
 * through rt2x00usb_vendor_request_buff().
 */
static inline void rt2x00usb_register_multiread(struct rt2x00_dev *rt2x00dev,
						const unsigned int offset,
						void *value, const u32 length)
{
	rt2x00usb_vendor_request_buff(rt2x00dev, USB_MULTI_READ,
				      USB_VENDOR_REQUEST_IN, offset,
				      value, length,
				      REGISTER_TIMEOUT32(length));
}

/**
 * rt2x00usb_register_write - Write 32bit register word
 * @rt2x00dev: Device pointer, see &struct rt2x00_dev.
 * @offset: Register offset
 * @value: Data which should be written
 *
 * This function is a simple wrapper for 32bit register access
 * through rt2x00usb_vendor_request_buff().
 */
static inline void rt2x00usb_register_write(struct rt2x00_dev *rt2x00dev,
					    const unsigned int offset,
					    u32 value)
{
	__le32 reg = cpu_to_le32(value);
	rt2x00usb_vendor_request_buff(rt2x00dev, USB_MULTI_WRITE,
				      USB_VENDOR_REQUEST_OUT, offset,
				      &reg, sizeof(reg), REGISTER_TIMEOUT);
}

/**
 * rt2x00usb_register_write_lock - Write 32bit register word
 * @rt2x00dev: Device pointer, see &struct rt2x00_dev.
 * @offset: Register offset
 * @value: Data which should be written
 *
 * This function is a simple wrapper for 32bit register access
 * through rt2x00usb_vendor_req_buff_lock().
 */
static inline void rt2x00usb_register_write_lock(struct rt2x00_dev *rt2x00dev,
						 const unsigned int offset,
						 u32 value)
{
	__le32 reg = cpu_to_le32(value);
	rt2x00usb_vendor_req_buff_lock(rt2x00dev, USB_MULTI_WRITE,
				       USB_VENDOR_REQUEST_OUT, offset,
				       &reg, sizeof(reg), REGISTER_TIMEOUT);
}

/**
 * rt2x00usb_register_multiwrite - Write 32bit register words
 * @rt2x00dev: Device pointer, see &struct rt2x00_dev.
 * @offset: Register offset
 * @value: Data which should be written
 * @length: Length of the data
 *
 * This function is a simple wrapper for 32bit register access
 * through rt2x00usb_vendor_request_buff().
 */
static inline void rt2x00usb_register_multiwrite(struct rt2x00_dev *rt2x00dev,
						 const unsigned int offset,
						 const void *value,
						 const u32 length)
{
	rt2x00usb_vendor_request_buff(rt2x00dev, USB_MULTI_WRITE,
				      USB_VENDOR_REQUEST_OUT, offset,
				      (void *)value, length,
				      REGISTER_TIMEOUT32(length));
}

/**
 * rt2x00usb_regbusy_read - Read from register with busy check
 * @rt2x00dev: Device pointer, see &struct rt2x00_dev.
 * @offset: Register offset
 * @field: Field to check if register is busy
 * @reg: Pointer to where register contents should be stored
 *
 * This function will read the given register, and checks if the
 * register is busy. If it is, it will sleep for a couple of
 * microseconds before reading the register again. If the register
 * is not read after a certain timeout, this function will return
 * FALSE.
 */
int rt2x00usb_regbusy_read(struct rt2x00_dev *rt2x00dev,
			   const unsigned int offset,
			   const struct rt2x00_field32 field,
			   u32 *reg);

/*
 * Radio handlers
 */
void rt2x00usb_disable_radio(struct rt2x00_dev *rt2x00dev);

/**
 * struct queue_entry_priv_usb: Per entry USB specific information
 *
 * @urb: Urb structure used for device communication.
 */
struct queue_entry_priv_usb {
	struct urb *urb;
};

/**
 * struct queue_entry_priv_usb_bcn: Per TX entry USB specific information
 *
 * The first section should match &struct queue_entry_priv_usb exactly.
 * rt2500usb can use this structure to send a guardian byte when working
 * with beacons.
 *
 * @urb: Urb structure used for device communication.
 * @guardian_data: Set to 0, used for sending the guardian data.
 * @guardian_urb: Urb structure used to send the guardian data.
 */
struct queue_entry_priv_usb_bcn {
	struct urb *urb;

	unsigned int guardian_data;
	struct urb *guardian_urb;
};

/**
 * rt2x00usb_kick_queue - Kick data queue
 * @queue: Data queue to kick
 *
 * This will walk through all entries of the queue and push all pending
 * frames to the hardware as a single burst.
 */
void rt2x00usb_kick_queue(struct data_queue *queue);

/**
 * rt2x00usb_flush_queue - Flush data queue
 * @queue: Data queue to stop
 *
 * This will walk through all entries of the queue and kill all
 * URB's which were send to the device.
 */
void rt2x00usb_flush_queue(struct data_queue *queue);

/**
 * rt2x00usb_watchdog - Watchdog for USB communication
 * @rt2x00dev: Pointer to &struct rt2x00_dev
 *
 * Check the health of the USB communication and determine
 * if timeouts have occured. If this is the case, this function
 * will reset all communication to restore functionality again.
 */
void rt2x00usb_watchdog(struct rt2x00_dev *rt2x00dev);

/*
 * Device initialization handlers.
 */
void rt2x00usb_clear_entry(struct queue_entry *entry);
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
