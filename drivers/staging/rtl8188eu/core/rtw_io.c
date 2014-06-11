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
/*

The purpose of rtw_io.c

a. provides the API

b. provides the protocol engine

c. provides the software interface between caller and the hardware interface


Compiler Flag Option:

USB:
   a. USE_ASYNC_IRP: Both sync/async operations are provided.

Only sync read/rtw_write_mem operations are provided.

jackson@realtek.com.tw

*/

#define _RTW_IO_C_
#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_io.h>
#include <osdep_intf.h>
#include <usb_ops.h>

#define rtw_le16_to_cpu(val)		le16_to_cpu(val)
#define rtw_le32_to_cpu(val)		le32_to_cpu(val)
#define rtw_cpu_to_le16(val)		cpu_to_le16(val)
#define rtw_cpu_to_le32(val)		cpu_to_le32(val)


u8 _rtw_read8(struct adapter *adapter, u32 addr)
{
	u8 r_val;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl *pintfhdl = &(pio_priv->intf);
	u8 (*_read8)(struct adapter *pintfhdl, u32 addr);

	_read8 = pintfhdl->io_ops._read8;
	r_val = _read8(adapter, addr);
	return r_val;
}

u16 _rtw_read16(struct adapter *adapter, u32 addr)
{
	u16 r_val;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	u16 (*_read16)(struct adapter *pintfhdl, u32 addr);
	_read16 = pintfhdl->io_ops._read16;

	r_val = _read16(adapter, addr);
	return r_val;
}

u32 _rtw_read32(struct adapter *adapter, u32 addr)
{
	u32 r_val;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	u32	(*_read32)(struct adapter *pintfhdl, u32 addr);
	_read32 = pintfhdl->io_ops._read32;

	r_val = _read32(adapter, addr);
	return r_val;
}

int _rtw_write8(struct adapter *adapter, u32 addr, u8 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	int (*_write8)(struct adapter *pintfhdl, u32 addr, u8 val);
	int ret;
	_write8 = pintfhdl->io_ops._write8;

	ret = _write8(adapter, addr, val);

	return RTW_STATUS_CODE(ret);
}

int _rtw_write16(struct adapter *adapter, u32 addr, u16 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	int (*_write16)(struct adapter *pintfhdl, u32 addr, u16 val);
	int ret;
	_write16 = pintfhdl->io_ops._write16;

	ret = _write16(adapter, addr, val);

	return RTW_STATUS_CODE(ret);
}
int _rtw_write32(struct adapter *adapter, u32 addr, u32 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	int (*_write32)(struct adapter *pintfhdl, u32 addr, u32 val);
	int ret;
	_write32 = pintfhdl->io_ops._write32;

	ret = _write32(adapter, addr, val);

	return RTW_STATUS_CODE(ret);
}

int rtw_init_io_priv(struct adapter *padapter, void (*set_intf_ops)(struct _io_ops *pops))
{
	struct io_priv	*piopriv = &padapter->iopriv;
	struct intf_hdl *pintf = &piopriv->intf;

	if (set_intf_ops == NULL)
		return _FAIL;

	piopriv->padapter = padapter;
	pintf->padapter = padapter;
	pintf->pintf_dev = adapter_to_dvobj(padapter);

	set_intf_ops(&pintf->io_ops);

	return _SUCCESS;
}
