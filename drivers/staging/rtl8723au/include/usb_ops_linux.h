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

#define RTW_USB_CONTROL_MSG_TIMEOUT	500/* ms */

#define MAX_USBCTRL_VENDORREQ_TIMES	10

unsigned int ffaddr2pipehdl23a(struct dvobj_priv *pdvobj, u32 addr);

void usb_read_port_cancel23a(struct rtw_adapter *adapter);

int usb_write_port23a(struct rtw_adapter *adapter, u32 addr, u32 cnt,
		      struct xmit_buf *wmem);
void usb_write_port23a_cancel(struct rtw_adapter *adapter);

#endif
