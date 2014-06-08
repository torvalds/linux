/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/
#ifndef __USB_OPS_LINUX_H__
#define __USB_OPS_LINUX_H__

#define VENDOR_CMD_MAX_DATA_LEN	254

#define RTW_USB_CONTROL_MSG_TIMEOUT_TEST	10/* ms */
#define RTW_USB_CONTROL_MSG_TIMEOUT	500/* ms */

#define MAX_USBCTRL_VENDORREQ_TIMES	10

#define RTW_USB_BULKOUT_TIMEOUT	5000/* ms */

#define _usbctrl_vendorreq_async_callback(urb, regs)		\
	_usbctrl_vendorreq_async_callback(urb)
#define usb_write_mem23a_complete(purb, regs)	usb_write_mem23a_complete(purb)
#define usb_write_port23a_complete(purb, regs)	usb_write_port23a_complete(purb)
#define usb_read_port_complete(purb, regs)	usb_read_port_complete(purb)
#define usb_read_interrupt_complete(purb, regs)			\
	usb_read_interrupt_complete(purb)

unsigned int ffaddr2pipehdl23a(struct dvobj_priv *pdvobj, u32 addr);

void usb_read_mem23a(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem);
void usb_write_mem23a(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem);

void usb_read_port_cancel23a(struct intf_hdl *pintfhdl);

u32 usb_write_port23a(struct intf_hdl *pintfhdl, u32 addr, u32 cnt,
		   struct xmit_buf *wmem);
void usb_write_port23a_cancel(struct intf_hdl *pintfhdl);

#endif
