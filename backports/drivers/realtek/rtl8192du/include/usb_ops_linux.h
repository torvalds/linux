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
 *
 ******************************************************************************/
#ifndef __USB_OPS_LINUX_H__
#define __USB_OPS_LINUX_H__

#define VENDOR_CMD_MAX_DATA_LEN	254

#define RTW_USB_CONTROL_MSG_TIMEOUT_TEST	10/* ms */
#define RTW_USB_CONTROL_MSG_TIMEOUT	500/* ms */

#define MAX_USBCTRL_VENDORREQ_TIMES	1

#define RTW_USB_BULKOUT_TIMEOUT	5000/* ms */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)) || (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18))
#define _usbctrl_vendorreq_async_callback(urb, regs)	_usbctrl_vendorreq_async_callback(urb)
#define usb_bulkout_zero_complete(purb, regs)	usb_bulkout_zero_complete(purb)
#define usb_write_mem_complete(purb, regs)	usb_write_mem_complete(purb)
#define usb_write_port_complete(purb, regs)	usb_write_port_complete(purb)
#define usb_read_port_complete(purb, regs)	usb_read_port_complete(purb)
#define usb_read_interrupt_complete(purb, regs)	usb_read_interrupt_complete(purb)
#endif

unsigned int ffaddr2pipehdl(struct dvobj_priv *pdvobj, u32 addr);

void usb_read_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem);
void usb_write_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem);

void usb_read_port_cancel(struct intf_hdl *pintfhdl);

u32 usb_write_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem);
void usb_write_port_cancel(struct intf_hdl *pintfhdl);

#endif
