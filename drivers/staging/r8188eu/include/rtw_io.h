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

int __must_check rtw_read8(struct adapter *adapter, u32 addr, u8 *data);
int __must_check rtw_read16(struct adapter *adapter, u32 addr, u16 *data);
int __must_check rtw_read32(struct adapter *adapter, u32 addr, u32 *data);
int rtw_read_port(struct adapter *adapter, struct recv_buf *precvbuf);
void rtw_read_port_cancel(struct adapter *adapter);

int rtw_write8(struct adapter *adapter, u32 addr, u8 val);
int rtw_write16(struct adapter *adapter, u32 addr, u16 val);
int rtw_write32(struct adapter *adapter, u32 addr, u32 val);
int rtw_writeN(struct adapter *adapter, u32 addr, u32 length, u8 *pdata);

u32 rtw_write_port(struct adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
void rtw_write_port_cancel(struct adapter *adapter);

#endif	/* _RTL8711_IO_H_ */
