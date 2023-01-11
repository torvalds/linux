/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef _RTW_IO_H_
#define _RTW_IO_H_

#include "osdep_service.h"
#include "osdep_intf.h"

#include <asm/byteorder.h>
#include <linux/semaphore.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>

#include <linux/usb.h>
#include <linux/usb/ch9.h>

#define rtw_usb_buffer_alloc(dev, size, dma)				\
	usb_alloc_coherent((dev), (size), (in_interrupt() ?		\
			   GFP_ATOMIC : GFP_KERNEL), (dma))
#define rtw_usb_buffer_free(dev, size, addr, dma)			\
	usb_free_coherent((dev), (size), (addr), (dma))

struct intf_priv;
struct intf_hdl;

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
};

int __must_check rtw_read8(struct adapter *adapter, u32 addr, u8 *data);
int __must_check rtw_read16(struct adapter *adapter, u32 addr, u16 *data);
int __must_check rtw_read32(struct adapter *adapter, u32 addr, u32 *data);
u32 rtw_read_port(struct adapter *adapter, u8 *pmem);
void rtw_read_port_cancel(struct adapter *adapter);

int rtw_write8(struct adapter *adapter, u32 addr, u8 val);
int rtw_write16(struct adapter *adapter, u32 addr, u16 val);
int rtw_write32(struct adapter *adapter, u32 addr, u32 val);
int rtw_writeN(struct adapter *adapter, u32 addr, u32 length, u8 *pdata);

u32 rtw_write_port(struct adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
void rtw_write_port_cancel(struct adapter *adapter);

#endif	/* _RTL8711_IO_H_ */
