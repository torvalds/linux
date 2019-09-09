// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 * rtl8712_io.c
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192SU
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>.
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#define _RTL8712_IO_C_

#include "osdep_service.h"
#include "drv_types.h"
#include "rtl871x_io.h"
#include "osdep_intf.h"
#include "usb_ops.h"

u8 r8712_read8(struct _adapter *adapter, u32 addr)
{
	struct intf_hdl *hdl = &adapter->pio_queue->intf;

	return hdl->io_ops._read8(hdl, addr);
}

u16 r8712_read16(struct _adapter *adapter, u32 addr)
{
	struct intf_hdl *hdl = &adapter->pio_queue->intf;

	return hdl->io_ops._read16(hdl, addr);
}

u32 r8712_read32(struct _adapter *adapter, u32 addr)
{
	struct intf_hdl *hdl = &adapter->pio_queue->intf;

	return hdl->io_ops._read32(hdl, addr);
}

void r8712_write8(struct _adapter *adapter, u32 addr, u8 val)
{
	struct intf_hdl *hdl = &adapter->pio_queue->intf;

	hdl->io_ops._write8(hdl, addr, val);
}

void r8712_write16(struct _adapter *adapter, u32 addr, u16 val)
{
	struct intf_hdl *hdl = &adapter->pio_queue->intf;

	hdl->io_ops._write16(hdl, addr, val);
}

void r8712_write32(struct _adapter *adapter, u32 addr, u32 val)
{
	struct intf_hdl *hdl = &adapter->pio_queue->intf;

	hdl->io_ops._write32(hdl, addr, val);
}

void r8712_read_mem(struct _adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{
	struct intf_hdl *hdl = &adapter->pio_queue->intf;

	if (adapter->driver_stopped || adapter->surprise_removed)
		return;

	hdl->io_ops._read_mem(hdl, addr, cnt, pmem);
}

void r8712_write_mem(struct _adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{
	struct intf_hdl *hdl = &adapter->pio_queue->intf;

	hdl->io_ops._write_mem(hdl, addr, cnt, pmem);
}

void r8712_read_port(struct _adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{
	struct intf_hdl *hdl = &adapter->pio_queue->intf;

	if (adapter->driver_stopped || adapter->surprise_removed)
		return;

	hdl->io_ops._read_port(hdl, addr, cnt, pmem);
}

void r8712_write_port(struct _adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{
	struct intf_hdl *hdl = &adapter->pio_queue->intf;

	hdl->io_ops._write_port(hdl, addr, cnt, pmem);
}
