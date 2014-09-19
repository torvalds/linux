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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __USB_OPS_LINUX_H__
#define __USB_OPS_LINUX_H__

#define VENDOR_CMD_MAX_DATA_LEN	254
#define FW_START_ADDRESS	0x1000

#define RTW_USB_CONTROL_MSG_TIMEOUT_TEST	10//ms
#define RTW_USB_CONTROL_MSG_TIMEOUT	500//ms

#define RECV_BULK_IN_ADDR		0x80//assign by drv,not real address 
#define RECV_INT_IN_ADDR		0x81//assign by drv,not real address 


#if defined(CONFIG_VENDOR_REQ_RETRY) && defined(CONFIG_USB_VENDOR_REQ_MUTEX)
/* vendor req retry should be in the situation when each vendor req is atomically submitted from others */
#define MAX_USBCTRL_VENDORREQ_TIMES	10
#else
#define MAX_USBCTRL_VENDORREQ_TIMES	1
#endif

#define RTW_USB_BULKOUT_TIMEOUT	5000//ms

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)) || (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18))
#define _usbctrl_vendorreq_async_callback(urb, regs)	_usbctrl_vendorreq_async_callback(urb)
#define usb_bulkout_zero_complete(purb, regs)	usb_bulkout_zero_complete(purb)
#define usb_write_mem_complete(purb, regs)	usb_write_mem_complete(purb)
#define usb_write_port_complete(purb, regs)	usb_write_port_complete(purb)
#define usb_read_port_complete(purb, regs)	usb_read_port_complete(purb)
#define usb_read_interrupt_complete(purb, regs)	usb_read_interrupt_complete(purb)
#endif

#ifdef CONFIG_USB_SUPPORT_ASYNC_VDN_REQ
int usb_async_write8(struct intf_hdl *pintfhdl, u32 addr, u8 val);
int usb_async_write16(struct intf_hdl *pintfhdl, u32 addr, u16 val);
int usb_async_write32(struct intf_hdl *pintfhdl, u32 addr, u32 val);
#endif /* CONFIG_USB_SUPPORT_ASYNC_VDN_REQ */

unsigned int ffaddr2pipehdl(struct dvobj_priv *pdvobj, u32 addr);

void usb_read_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem);
void usb_write_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem);

void usb_read_port_cancel(struct intf_hdl *pintfhdl);

u32 usb_write_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem);
void usb_write_port_cancel(struct intf_hdl *pintfhdl);

int usbctrl_vendorreq(struct intf_hdl *pintfhdl, u8 request, u16 value, u16 index, void *pdata, u16 len, u8 requesttype);
#ifdef CONFIG_USB_SUPPORT_ASYNC_VDN_REQ
int _usbctrl_vendorreq_async_write(struct usb_device *udev, u8 request,
	u16 value, u16 index, void *pdata, u16 len, u8 requesttype);
#endif /* CONFIG_USB_SUPPORT_ASYNC_VDN_REQ */

u8 usb_read8(struct intf_hdl *pintfhdl, u32 addr);
u16 usb_read16(struct intf_hdl *pintfhdl, u32 addr);
u32 usb_read32(struct intf_hdl *pintfhdl, u32 addr);
int usb_write8(struct intf_hdl *pintfhdl, u32 addr, u8 val);
int usb_write16(struct intf_hdl *pintfhdl, u32 addr, u16 val);
int usb_write32(struct intf_hdl *pintfhdl, u32 addr, u32 val);
int usb_writeN(struct intf_hdl *pintfhdl, u32 addr, u32 length, u8 *pdata);
u32 usb_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem);
void usb_recv_tasklet(void *priv);

#endif

