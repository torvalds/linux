/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __USB_OPS_LINUX_H__
#define __USB_OPS_LINUX_H__

#define VENDOR_CMD_MAX_DATA_LEN	254

#define RTW_USB_CONTROL_MSG_TIMEOUT_TEST	10/* ms */
#define RTW_USB_CONTROL_MSG_TIMEOUT	500/* ms */

#define MAX_USBCTRL_VENDORREQ_TIMES	10

#define RTW_USB_BULKOUT_TIME	5000/* ms */

#define _usbctrl_vendorreq_async_callback(urb, regs)	\
	_usbctrl_vendorreq_async_callback(urb)
#define usb_bulkout_zero_complete(purb, regs)		\
	usb_bulkout_zero_complete(purb)
#define usb_write_mem_complete(purb, regs)		\
	usb_write_mem_complete(purb)
#define usb_write_port_complete(purb, regs)		\
	usb_write_port_complete(purb)
#define usb_read_port_complete(purb, regs)		\
	usb_read_port_complete(purb)
#define usb_read_interrupt_complete(purb, regs)		\
	usb_read_interrupt_complete(purb)

#endif
