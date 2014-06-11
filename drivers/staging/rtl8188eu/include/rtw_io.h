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

#ifndef _RTW_IO_H_
#define _RTW_IO_H_

#include <osdep_service.h>
#include <osdep_intf.h>

#include <asm/byteorder.h>
#include <linux/semaphore.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>

#include <linux/usb.h>
#include <linux/usb/ch9.h>

#define rtw_usb_buffer_alloc(dev, size, dma)				\
	usb_alloc_coherent((dev), (size), (in_interrupt() ?		\
			   GFP_ATOMIC : GFP_KERNEL), (dma))
#define rtw_usb_buffer_free(dev, size, addr, dma)			\
	usb_free_coherent((dev), (size), (addr), (dma))


struct intf_priv;
struct intf_hdl;

struct _io_ops {
	u8 (*_read8)(struct adapter *pintfhdl, u32 addr);
};

struct io_req {
	struct list_head list;
	u32	addr;
	u32	val;
	u32	command;
	u32	status;
	u8	*pbuf;
	struct semaphore sema;

	void (*_async_io_callback)(struct adapter *padater,
				   struct io_req *pio_req, u8 *cnxt);
	u8 *cnxt;
};

struct	intf_hdl {
	struct adapter *padapter;
	struct dvobj_priv *pintf_dev;
	struct _io_ops	io_ops;
};

/*
Below is the data structure used by _io_handler
*/

struct io_priv {
	struct adapter *padapter;
	struct intf_hdl intf;
};

u8 _rtw_read8(struct adapter *adapter, u32 addr);
u16 usb_read16(struct adapter *adapter, u32 addr);
u32 usb_read32(struct adapter *adapter, u32 addr);
u32 usb_read_port(struct adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
void usb_read_port_cancel(struct adapter *adapter);

int usb_write8(struct adapter *adapter, u32 addr, u8 val);
int usb_write16(struct adapter *adapter, u32 addr, u16 val);
int usb_write32(struct adapter *adapter, u32 addr, u32 val);
int usb_writeN(struct adapter *adapter, u32 addr, u32 length, u8 *pdata);

u32 usb_write_port(struct adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
void usb_write_port_cancel(struct adapter *adapter);

#define rtw_read8(adapter, addr) _rtw_read8((adapter), (addr))

int rtw_init_io_priv(struct adapter *padapter,
		     void (*set_intf_ops)(struct _io_ops *pops));

#endif	/* _RTL8711_IO_H_ */
