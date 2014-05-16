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
/*

The purpose of rtw_io.c

a. provides the API

b. provides the protocol engine

c. provides the software interface between caller and the hardware interface

Compiler Flag Option:

1. For USB:
   a. USE_ASYNC_IRP: Both sync/async operations are provided.

jackson@realtek.com.tw

*/

#define _RTW_IO_C_
#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_io.h>
#include <osdep_intf.h>

#include <usb_ops.h>

u8 _rtw_read823a(struct rtw_adapter *adapter, u32 addr)
{
	u8 r_val;
	struct _io_ops *io_ops = &adapter->io_ops;

	r_val = io_ops->_read8(adapter, addr);

	return r_val;
}

u16 _rtw_read1623a(struct rtw_adapter *adapter, u32 addr)
{
	struct _io_ops *io_ops = &adapter->io_ops;

	return io_ops->_read16(adapter, addr);
}

u32 _rtw_read3223a(struct rtw_adapter *adapter, u32 addr)
{
	struct _io_ops *io_ops = &adapter->io_ops;

	return io_ops->_read32(adapter, addr);
}

int _rtw_write823a(struct rtw_adapter *adapter, u32 addr, u8 val)
{
	struct _io_ops *io_ops = &adapter->io_ops;
	int ret;

	ret = io_ops->_write8(adapter, addr, val);

	if (ret < 0)
		return _FAIL;
	else
		return _SUCCESS;
}

int _rtw_write1623a(struct rtw_adapter *adapter, u32 addr, u16 val)
{
	struct _io_ops *io_ops = &adapter->io_ops;
	int ret;

	ret = io_ops->_write16(adapter, addr, val);

	if (ret < 0)
		return _FAIL;
	else
		return _SUCCESS;
}

int _rtw_write3223a(struct rtw_adapter *adapter, u32 addr, u32 val)
{
	struct _io_ops *io_ops = &adapter->io_ops;
	int ret;

	ret = io_ops->_write32(adapter, addr, val);

	if (ret < 0)
		return _FAIL;
	else
		return _SUCCESS;
}

int _rtw_writeN23a(struct rtw_adapter *adapter, u32 addr , u32 length , u8 *pdata)
{
	struct _io_ops *io_ops = &adapter->io_ops;
	int ret;

	ret = io_ops->_writeN(adapter, addr, length, pdata);

	if (ret < 0)
		return _FAIL;
	else
		return _SUCCESS;
}

void _rtw_read_port23a_cancel(struct rtw_adapter *adapter)
{
	void (*_read_port_cancel)(struct rtw_adapter *adapter);
	struct _io_ops *io_ops = &adapter->io_ops;

	_read_port_cancel = io_ops->_read_port_cancel;

	if (_read_port_cancel)
		_read_port_cancel(adapter);
}

void _rtw_write_port23a_cancel(struct rtw_adapter *adapter)
{
	void (*_write_port_cancel)(struct rtw_adapter *adapter);
	struct _io_ops *io_ops = &adapter->io_ops;

	_write_port_cancel = io_ops->_write_port_cancel;

	if (_write_port_cancel)
		_write_port_cancel(adapter);
}
