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

Only sync read/rtw_write_mem operations are provided.

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
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &pio_priv->intf;

	r_val = pintfhdl->io_ops._read8(pintfhdl, addr);

	return r_val;
}

u16 _rtw_read1623a(struct rtw_adapter *adapter, u32 addr)
{
	u16 r_val;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &pio_priv->intf;

	r_val = pintfhdl->io_ops._read16(pintfhdl, addr);

	return le16_to_cpu(r_val);
}

u32 _rtw_read3223a(struct rtw_adapter *adapter, u32 addr)
{
	u32 r_val;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &pio_priv->intf;

	r_val = pintfhdl->io_ops._read32(pintfhdl, addr);

	return le32_to_cpu(r_val);
}

int _rtw_write823a(struct rtw_adapter *adapter, u32 addr, u8 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl		*pintfhdl = &pio_priv->intf;
	int ret;

	ret = pintfhdl->io_ops._write8(pintfhdl, addr, val);

	return RTW_STATUS_CODE23a(ret);
}

int _rtw_write1623a(struct rtw_adapter *adapter, u32 addr, u16 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl		*pintfhdl = &pio_priv->intf;
	int ret;

	val = cpu_to_le16(val);
	ret = pintfhdl->io_ops._write16(pintfhdl, addr, val);

	return RTW_STATUS_CODE23a(ret);
}
int _rtw_write3223a(struct rtw_adapter *adapter, u32 addr, u32 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &pio_priv->intf;
	int ret;

	val = cpu_to_le32(val);
	ret = pintfhdl->io_ops._write32(pintfhdl, addr, val);

	return RTW_STATUS_CODE23a(ret);
}

int _rtw_writeN23a(struct rtw_adapter *adapter, u32 addr , u32 length , u8 *pdata)
{
	struct io_priv *pio_priv = &adapter->iopriv;
        struct intf_hdl *pintfhdl = (struct intf_hdl*)&pio_priv->intf;
	int ret;

	ret = pintfhdl->io_ops._writeN(pintfhdl, addr, length, pdata);

	return RTW_STATUS_CODE23a(ret);
}
int _rtw_write823a_async23a(struct rtw_adapter *adapter, u32 addr, u8 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &pio_priv->intf;
	int ret;

	ret = pintfhdl->io_ops._write8_async(pintfhdl, addr, val);

	return RTW_STATUS_CODE23a(ret);
}
int _rtw_write1623a_async(struct rtw_adapter *adapter, u32 addr, u16 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &pio_priv->intf;
	int ret;

	val = cpu_to_le16(val);
	ret = pintfhdl->io_ops._write16_async(pintfhdl, addr, val);

	return RTW_STATUS_CODE23a(ret);
}
int _rtw_write3223a_async23a(struct rtw_adapter *adapter, u32 addr, u32 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &pio_priv->intf;
	int ret;

	val = cpu_to_le32(val);
	ret = pintfhdl->io_ops._write32_async(pintfhdl, addr, val);

	return RTW_STATUS_CODE23a(ret);
}

void _rtw_read_mem23a(struct rtw_adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &pio_priv->intf;

	if ((adapter->bDriverStopped == true) ||
	    (adapter->bSurpriseRemoved == true)) {
	     RT_TRACE(_module_rtl871x_io_c_, _drv_info_,
		      ("rtw_read_mem:bDriverStopped(%d) OR "
		       "bSurpriseRemoved(%d)", adapter->bDriverStopped,
		       adapter->bSurpriseRemoved));
	     return;
	}

	pintfhdl->io_ops._read_mem(pintfhdl, addr, cnt, pmem);
}

void _rtw_write_mem23a(struct rtw_adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &pio_priv->intf;

	pintfhdl->io_ops._write_mem(pintfhdl, addr, cnt, pmem);
}

void _rtw_read_port23a(struct rtw_adapter *adapter, u32 addr, u32 cnt,
		    struct recv_buf *rbuf)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &pio_priv->intf;

	if ((adapter->bDriverStopped == true) ||
	    (adapter->bSurpriseRemoved == true)) {
	     RT_TRACE(_module_rtl871x_io_c_, _drv_info_,
		      ("rtw_read_port:bDriverStopped(%d) OR "
		       "bSurpriseRemoved(%d)", adapter->bDriverStopped,
		       adapter->bSurpriseRemoved));
	     return;
	}

	pintfhdl->io_ops._read_port(pintfhdl, addr, cnt, rbuf);
}

void _rtw_read_port23a_cancel(struct rtw_adapter *adapter)
{
	void (*_read_port_cancel)(struct intf_hdl *pintfhdl);
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &pio_priv->intf;

	_read_port_cancel = pintfhdl->io_ops._read_port_cancel;

	if (_read_port_cancel)
		_read_port_cancel(pintfhdl);
}

u32 _rtw_write_port23a(struct rtw_adapter *adapter, u32 addr, u32 cnt,
		    struct xmit_buf *xbuf)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &pio_priv->intf;
	u32 ret = _SUCCESS;

	ret = pintfhdl->io_ops._write_port(pintfhdl, addr, cnt, xbuf);

	return ret;
}

u32 _rtw_write_port23a_and_wait23a(struct rtw_adapter *adapter, u32 addr, u32 cnt,
			     struct xmit_buf *pxmitbuf, int timeout_ms)
{
	int ret = _SUCCESS;
	struct submit_ctx sctx;

	rtw_sctx_init23a(&sctx, timeout_ms);
	pxmitbuf->sctx = &sctx;

	ret = _rtw_write_port23a(adapter, addr, cnt, pxmitbuf);

	if (ret == _SUCCESS)
		ret = rtw_sctx_wait23a(&sctx);

	return ret;
}

void _rtw_write_port23a_cancel(struct rtw_adapter *adapter)
{
	void (*_write_port_cancel)(struct intf_hdl *pintfhdl);
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &pio_priv->intf;

	_write_port_cancel = pintfhdl->io_ops._write_port_cancel;

	if (_write_port_cancel)
		_write_port_cancel(pintfhdl);
}

int rtw_init_io_priv23a(struct rtw_adapter *padapter,
		     void (*set_intf_ops)(struct _io_ops *pops))
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
