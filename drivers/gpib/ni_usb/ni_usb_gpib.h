/* SPDX-License-Identifier: GPL-2.0 */

/***************************************************************************
 *   copyright            : (C) 2004 by Frank Mori Hess
 ***************************************************************************/

#ifndef _NI_USB_GPIB_H
#define _NI_USB_GPIB_H

#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <linux/usb.h>
#include <linux/timer.h>
#include "gpibP.h"

enum {
	USB_VENDOR_ID_NI = 0x3923
};

enum {
	USB_DEVICE_ID_NI_USB_B = 0x702a,
	USB_DEVICE_ID_NI_USB_B_PREINIT = 0x702b,	// device id before firmware is loaded
	USB_DEVICE_ID_NI_USB_HS = 0x709b,
	USB_DEVICE_ID_NI_USB_HS_PLUS = 0x7618,
	USB_DEVICE_ID_KUSB_488A = 0x725c,
	USB_DEVICE_ID_MC_USB_488 = 0x725d
};

enum ni_usb_device {
	NIUSB_SUBDEV_TNT4882 = 1,
	NIUSB_SUBDEV_UNKNOWN2 = 2,
	NIUSB_SUBDEV_UNKNOWN3 = 3,
};

enum endpoint_addresses {
	NIUSB_B_BULK_OUT_ENDPOINT = 0x2,
	NIUSB_B_BULK_IN_ENDPOINT = 0x2,
	NIUSB_B_BULK_IN_ALT_ENDPOINT = 0x6,
	NIUSB_B_INTERRUPT_IN_ENDPOINT = 0x4,
};

enum hs_enpoint_addresses {
	NIUSB_HS_BULK_OUT_ENDPOINT = 0x2,
	NIUSB_HS_BULK_OUT_ALT_ENDPOINT = 0x6,
	NIUSB_HS_BULK_IN_ENDPOINT = 0x4,
	NIUSB_HS_BULK_IN_ALT_ENDPOINT = 0x8,
	NIUSB_HS_INTERRUPT_IN_ENDPOINT = 0x1,
};

enum hs_plus_endpoint_addresses {
	NIUSB_HS_PLUS_BULK_OUT_ENDPOINT = 0x1,
	NIUSB_HS_PLUS_BULK_OUT_ALT_ENDPOINT = 0x4,
	NIUSB_HS_PLUS_BULK_IN_ENDPOINT = 0x2,
	NIUSB_HS_PLUS_BULK_IN_ALT_ENDPOINT = 0x5,
	NIUSB_HS_PLUS_INTERRUPT_IN_ENDPOINT = 0x3,
};

struct ni_usb_urb_ctx {
	struct completion complete;
	unsigned timed_out : 1;
};

// struct which defines private_data for ni_usb devices
struct ni_usb_priv {
	struct usb_interface *bus_interface;
	int bulk_out_endpoint;
	int bulk_in_endpoint;
	int interrupt_in_endpoint;
	u8 eos_char;
	unsigned short eos_mode;
	unsigned int monitored_ibsta_bits;
	struct urb *bulk_urb;
	struct urb *interrupt_urb;
	u8 interrupt_buffer[0x11];
	struct mutex addressed_transfer_lock;	// protect transfer lock
	struct mutex bulk_transfer_lock;	// protect bulk message sends
	struct mutex control_transfer_lock;	// protect control messages
	struct mutex interrupt_transfer_lock;	//  protect interrupt messages
	struct timer_list bulk_timer;
	struct ni_usb_urb_ctx context;
	int product_id;
	unsigned short ren_state;
};

struct ni_usb_status_block {
	short id;
	unsigned short ibsta;
	short error_code;
	unsigned short count;
};

struct ni_usb_register {
	enum ni_usb_device device;
	short address;
	unsigned short value;
};

enum ni_usb_bulk_ids {
	NIUSB_IBCAC_ID = 0x1,
	NIUSB_UNKNOWN3_ID = 0x3, // device level function id?
	NIUSB_TERM_ID = 0x4,
	NIUSB_IBGTS_ID = 0x6,
	NIUSB_IBRPP_ID = 0x7,
	NIUSB_REG_READ_ID = 0x8,
	NIUSB_REG_WRITE_ID = 0x9,
	NIUSB_IBSIC_ID = 0xf,
	NIUSB_REGISTER_READ_DATA_START_ID = 0x34,
	NIUSB_REGISTER_READ_DATA_END_ID = 0x35,
	NIUSB_IBRD_DATA_ID = 0x36,
	NIUSB_IBRD_EXTENDED_DATA_ID = 0x37,
	NIUSB_IBRD_STATUS_ID = 0x38
};

enum ni_usb_error_codes {
	NIUSB_NO_ERROR = 0,
	/*
	 * NIUSB_ABORTED_ERROR occurs when I/O is interrupted early by
	 * doing a NI_USB_STOP_REQUEST on the control endpoint.
	 */
	NIUSB_ABORTED_ERROR = 1,
	/*
	 * NIUSB_READ_ATN_ERROR occurs when you do a board read while
	 * ATN is set
	 */
	NIUSB_ATN_STATE_ERROR = 2,
	/*
	 * NIUSB_ADDRESSING_ERROR occurs when you do a board
	 * read/write as CIC but are not in LACS/TACS
	 */
	NIUSB_ADDRESSING_ERROR = 3,
	/*
	 * NIUSB_EOSMODE_ERROR occurs on reads if any eos mode or char
	 * bits are set when REOS is not set.
	 * Have also seen error 4 if you try to send more than 16
	 * command bytes at once on a usb-b.
	 */
	NIUSB_EOSMODE_ERROR = 4,
	/*
	 * NIUSB_NO_BUS_ERROR occurs when you try to write a command
	 * byte but there are no devices connected to the gpib bus
	 */
	NIUSB_NO_BUS_ERROR = 5,
	/*
	 * NIUSB_NO_LISTENER_ERROR occurs when you do a board write as
	 * CIC with no listener
	 */
	NIUSB_NO_LISTENER_ERROR = 8,
	/* get NIUSB_TIMEOUT_ERROR on board read/write timeout */
	NIUSB_TIMEOUT_ERROR = 10,
};

enum ni_usb_control_requests {
	NI_USB_STOP_REQUEST = 0x20,
	NI_USB_WAIT_REQUEST = 0x21,
	NI_USB_POLL_READY_REQUEST = 0x40,
	NI_USB_SERIAL_NUMBER_REQUEST = 0x41,
	NI_USB_HS_PLUS_0x48_REQUEST = 0x48,
	NI_USB_HS_PLUS_LED_REQUEST = 0x4b,
	NI_USB_HS_PLUS_0xf8_REQUEST = 0xf8
};

static const unsigned int ni_usb_ibsta_monitor_mask =
	SRQI | LOK | REM | CIC | ATN | TACS | LACS | DTAS | DCAS;

static inline int nec7210_to_tnt4882_offset(int offset)
{
	return 2 * offset;
};

static inline int ni_usb_bulk_termination(u8 *buffer)
{
	int i = 0;

	buffer[i++] = NIUSB_TERM_ID;
	buffer[i++] = 0x0;
	buffer[i++] = 0x0;
	buffer[i++] = 0x0;
	return i;
}

enum ni_usb_unknown3_register {
	SERIAL_NUMBER_4_REG = 0x8,
	SERIAL_NUMBER_3_REG = 0x9,
	SERIAL_NUMBER_2_REG = 0xa,
	SERIAL_NUMBER_1_REG = 0xb,
};

static inline int ni_usb_bulk_register_write_header(u8 *buffer, int num_writes)
{
	int i = 0;

	buffer[i++] = NIUSB_REG_WRITE_ID;
	buffer[i++] = num_writes;
	buffer[i++] = 0x0;
	return i;
}

static inline int ni_usb_bulk_register_write(u8 *buffer, struct ni_usb_register reg)
{
	int i = 0;

	buffer[i++] = reg.device;
	buffer[i++] = reg.address;
	buffer[i++] = reg.value;
	return i;
}

static inline int ni_usb_bulk_register_read_header(u8 *buffer, int num_reads)
{
	int i = 0;

	buffer[i++] = NIUSB_REG_READ_ID;
	buffer[i++] = num_reads;
	return i;
}

static inline int ni_usb_bulk_register_read(u8 *buffer, int device, int address)
{
	int i = 0;

	buffer[i++] = device;
	buffer[i++] = address;
	return i;
}

#endif	// _NI_USB_GPIB_H
