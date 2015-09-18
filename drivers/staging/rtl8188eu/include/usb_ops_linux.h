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

#define RTW_USB_CONTROL_MSG_TIMEOUT_TEST	10/* ms */
#define RTW_USB_CONTROL_MSG_TIMEOUT	500/* ms */

#define MAX_USBCTRL_VENDORREQ_TIMES	10

#define RTW_USB_BULKOUT_TIME	5000/* ms */

#define REALTEK_USB_VENQT_READ		0xC0
#define REALTEK_USB_VENQT_WRITE		0x40

#define ALIGNMENT_UNIT			16
#define MAX_VENDOR_REQ_CMD_SIZE	254	/* 8188cu SIE Support */
#define MAX_USB_IO_CTL_SIZE	(MAX_VENDOR_REQ_CMD_SIZE + ALIGNMENT_UNIT)

#define USB_HIGH_SPEED_BULK_SIZE	512
#define USB_FULL_SPEED_BULK_SIZE	64

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

static inline u8 rtw_usb_bulk_size_boundary(struct adapter *padapter,
					    int buf_len)
{
	u8 rst = true;
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);

	if (pdvobjpriv->ishighspeed)
		rst = (0 == (buf_len) % USB_HIGH_SPEED_BULK_SIZE) ?
		      true : false;
	else
		rst = (0 == (buf_len) % USB_FULL_SPEED_BULK_SIZE) ?
		      true : false;
	return rst;
}

unsigned int ffaddr2pipehdl(struct dvobj_priv *pdvobj, u32 addr);

u8 usb_read8(struct adapter *adapter, u32 addr);
u16 usb_read16(struct adapter *adapter, u32 addr);
u32 usb_read32(struct adapter *adapter, u32 addr);

u32 usb_read_port(struct adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
void usb_read_port_cancel(struct adapter *adapter);

int usb_write8(struct adapter *adapter, u32 addr, u8 val);
int usb_write16(struct adapter *adapter, u32 addr, u16 val);
int usb_write32(struct adapter *adapter, u32 addr, u32 val);

u32 usb_write_port(struct adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
void usb_write_port_cancel(struct adapter *adapter);

#endif
