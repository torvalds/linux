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
 *
 ******************************************************************************/
/*

The purpose of rtw_io.c

a. provides the API

b. provides the protocol engine

c. provides the software interface between caller and the hardware interface

Compiler Flag Option:

1. default USB configuration
   a. USE_ASYNC_IRP: Both sync/async operations are provided.

Only sync read/rtw_write_mem operations are provided.

jackson@realtek.com.tw

*/

#define _RTW_IO_C_
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_io.h>
#include <osdep_intf.h>
#include <usb_ops.h>

u8 _rtw_read8(struct rtw_adapter *adapter, u32 addr)
{
	u8 r_val;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	u8 (*_read8)(struct intf_hdl *pintfhdl, u32 addr);

	_read8 = pintfhdl->io_ops._read8;

	r_val = _read8(pintfhdl, addr);

	return r_val;
}

u16 _rtw_read16(struct rtw_adapter *adapter, u32 addr)
{
	u16 r_val;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	u16	(*_read16)(struct intf_hdl *pintfhdl, u32 addr);

	_read16 = pintfhdl->io_ops._read16;

	r_val = _read16(pintfhdl, addr);

	return r_val;
}

u32 _rtw_read32(struct rtw_adapter *adapter, u32 addr)
{
	u32 r_val;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	u32	(*_read32)(struct intf_hdl *pintfhdl, u32 addr);

	_read32 = pintfhdl->io_ops._read32;

	r_val = _read32(pintfhdl, addr);

	return r_val;
}

int _rtw_write8(struct rtw_adapter *adapter, u32 addr, u8 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl *pintfhdl = &(pio_priv->intf);
	int (*_write8)(struct intf_hdl *pintfhdl, u32 addr, u8 val);
	int ret;

	_write8 = pintfhdl->io_ops._write8;

	ret = _write8(pintfhdl, addr, val);

	return RTW_STATUS_CODE(ret);
}

int _rtw_write16(struct rtw_adapter *adapter, u32 addr, u16 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	int (*_write16)(struct intf_hdl *pintfhdl, u32 addr, u16 val);
	int ret;

	_write16 = pintfhdl->io_ops._write16;

	ret = _write16(pintfhdl, addr, val);

	return RTW_STATUS_CODE(ret);
}
int _rtw_write32(struct rtw_adapter *adapter, u32 addr, u32 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	int (*_write32)(struct intf_hdl *pintfhdl, u32 addr, u32 val);
	int ret;

	_write32 = pintfhdl->io_ops._write32;

	ret = _write32(pintfhdl, addr, val);

	return RTW_STATUS_CODE(ret);
}

int _rtw_writeN(struct rtw_adapter *adapter, u32 addr, u32 length, u8 *pdata)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = (struct intf_hdl *)(&(pio_priv->intf));
	int (*_writeN)(struct intf_hdl *pintfhdl, u32 addr,
		       u32 length, u8 *pdata);
	int ret;

	_writeN = pintfhdl->io_ops._writeN;

	ret = _writeN(pintfhdl, addr, length, pdata);

	return RTW_STATUS_CODE(ret);
}
int _rtw_write8_async(struct rtw_adapter *adapter, u32 addr, u8 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	int (*_write8_async)(struct intf_hdl *pintfhdl, u32 addr, u8 val);
	int ret;

	_write8_async = pintfhdl->io_ops._write8_async;

	ret = _write8_async(pintfhdl, addr, val);

	return RTW_STATUS_CODE(ret);
}
int _rtw_write16_async(struct rtw_adapter *adapter, u32 addr, u16 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl *pintfhdl = &(pio_priv->intf);
	int (*_write16_async)(struct intf_hdl *pintfhdl, u32 addr, u16 val);
	int ret;

	_write16_async = pintfhdl->io_ops._write16_async;

	ret = _write16_async(pintfhdl, addr, val);

	return RTW_STATUS_CODE(ret);
}
int _rtw_write32_async(struct rtw_adapter *adapter, u32 addr, u32 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl *pintfhdl = &(pio_priv->intf);
	int (*_write32_async)(struct intf_hdl *pintfhdl, u32 addr, u32 val);
	int ret;

	_write32_async = pintfhdl->io_ops._write32_async;

	ret = _write32_async(pintfhdl, addr, val);

	return RTW_STATUS_CODE(ret);
}

void _rtw_read_mem(struct rtw_adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{
	void (*_read_mem)(struct intf_hdl *pintfhdl, u32 addr,
			  u32 cnt, u8 *pmem);
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);

	if ((adapter->bDriverStopped == true) ||
	    (adapter->bSurpriseRemoved == true)) {
		RT_TRACE(_module_rtl871x_io_c_, _drv_info_,
			 ("rtw_read_mem:bDriverStopped(%d) OR bSurpriseRemoved(%d)",
			 adapter->bDriverStopped, adapter->bSurpriseRemoved));
	     return;
	}

	_read_mem = pintfhdl->io_ops._read_mem;

	_read_mem(pintfhdl, addr, cnt, pmem);

}

void _rtw_write_mem(struct rtw_adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{
	void (*_write_mem)(struct intf_hdl *pintfhdl, u32 addr,
			   u32 cnt, u8 *pmem);
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl *pintfhdl = &(pio_priv->intf);

	_write_mem = pintfhdl->io_ops._write_mem;

	_write_mem(pintfhdl, addr, cnt, pmem);

}

void _rtw_read_port(struct rtw_adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{
	u32 (*_read_port)(struct intf_hdl *pintfhdl, u32 addr,
			  u32 cnt, u8 *pmem);
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl *pintfhdl = &(pio_priv->intf);

	if ((adapter->bDriverStopped == true) ||
	    (adapter->bSurpriseRemoved == true)) {
		RT_TRACE(_module_rtl871x_io_c_, _drv_info_,
			 ("rtw_read_port:bDriverStopped(%d) OR bSurpriseRemoved(%d)",
			 adapter->bDriverStopped, adapter->bSurpriseRemoved));
	     return;
	}

	_read_port = pintfhdl->io_ops._read_port;

	_read_port(pintfhdl, addr, cnt, pmem);

}

void _rtw_read_port_cancel(struct rtw_adapter *adapter)
{
	void (*_read_port_cancel)(struct intf_hdl *pintfhdl);
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &(pio_priv->intf);

	_read_port_cancel = pintfhdl->io_ops._read_port_cancel;

	if (_read_port_cancel)
		_read_port_cancel(pintfhdl);
}

u32 _rtw_write_port(struct rtw_adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{
	u32 (*_write_port)(struct intf_hdl *pintfhdl, u32 addr,
			   u32 cnt, u8 *pmem);
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	u32 ret = _SUCCESS;

	_write_port = pintfhdl->io_ops._write_port;

	ret = _write_port(pintfhdl, addr, cnt, pmem);

	return ret;
}

u32 _rtw_write_port_and_wait(struct rtw_adapter *adapter, u32 addr, u32 cnt,
			     u8 *pmem, int timeout_ms)
{
	int ret = _SUCCESS;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)pmem;
	struct submit_ctx sctx;

	rtw_sctx_init(&sctx, timeout_ms);
	pxmitbuf->sctx = &sctx;

	ret = _rtw_write_port(adapter, addr, cnt, pmem);

	if (ret == _SUCCESS)
		ret = rtw_sctx_wait(&sctx);

	 return ret;
}

void _rtw_write_port_cancel(struct rtw_adapter *adapter)
{
	void (*_write_port_cancel)(struct intf_hdl *pintfhdl);
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &(pio_priv->intf);

	_write_port_cancel = pintfhdl->io_ops._write_port_cancel;

	if (_write_port_cancel)
		_write_port_cancel(pintfhdl);
}

int rtw_init_io_priv(struct rtw_adapter *padapter,
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

#ifdef DBG_IO

u16 read_sniff_ranges[][2] = {
	/* 0x550, 0x551}, */
};

u16 write_sniff_ranges[][2] = {
	/* 0x550, 0x551}, */
	/* 0x4c, 0x4c}, */
};

int read_sniff_num = sizeof(read_sniff_ranges)/sizeof(u16)/2;
int write_sniff_num = sizeof(write_sniff_ranges)/sizeof(u16)/2;

bool match_read_sniff_ranges(u16 addr, u16 len)
{
	int i;
	for (i = 0; i < read_sniff_num; i++) {
		if (addr + len > read_sniff_ranges[i][0] &&
		    addr <= read_sniff_ranges[i][1])
			return true;
	}

	return false;
}

bool match_write_sniff_ranges(u16 addr, u16 len)
{
	int i;
	for (i = 0; i < write_sniff_num; i++) {
		if (addr + len > write_sniff_ranges[i][0] &&
		    addr <= write_sniff_ranges[i][1])
			return true;
	}

	return false;
}

u8 dbg_rtw_read8(struct rtw_adapter *adapter, u32 addr,
		 const char *caller, const int line)
{
	u8 val = _rtw_read8(adapter, addr);

	if (match_read_sniff_ranges(addr, 1))
		DBG_871X("DBG_IO %s:%d rtw_read8(0x%04x) return 0x%02x\n",
			 caller, line, addr, val);

	return val;
}

u16 dbg_rtw_read16(struct rtw_adapter *adapter, u32 addr,
		   const char *caller, const int line)
{
	u16 val = _rtw_read16(adapter, addr);

	if (match_read_sniff_ranges(addr, 2))
		DBG_871X("DBG_IO %s:%d rtw_read16(0x%04x) return 0x%04x\n",
			 caller, line, addr, val);

	return val;
}

u32 dbg_rtw_read32(struct rtw_adapter *adapter, u32 addr,
		   const char *caller, const int line)
{
	u32 val = _rtw_read32(adapter, addr);

	if (match_read_sniff_ranges(addr, 4))
		DBG_871X("DBG_IO %s:%d rtw_read32(0x%04x) return 0x%08x\n",
			 caller, line, addr, val);

	return val;
}

int dbg_rtw_write8(struct rtw_adapter *adapter, u32 addr, u8 val,
		   const char *caller, const int line)
{
	if (match_write_sniff_ranges(addr, 1))
		DBG_871X("DBG_IO %s:%d rtw_write8(0x%04x, 0x%02x)\n", caller,
			 line, addr, val);

	return _rtw_write8(adapter, addr, val);
}

int dbg_rtw_write16(struct rtw_adapter *adapter, u32 addr, u16 val,
		    const char *caller, const int line)
{
	if (match_write_sniff_ranges(addr, 2))
		DBG_871X("DBG_IO %s:%d rtw_write16(0x%04x, 0x%04x)\n", caller,
			 line, addr, val);

	return _rtw_write16(adapter, addr, val);
}

int dbg_rtw_write32(struct rtw_adapter *adapter, u32 addr, u32 val,
		    const char *caller, const int line)
{
	if (match_write_sniff_ranges(addr, 4))
		DBG_871X("DBG_IO %s:%d rtw_write32(0x%04x, 0x%08x)\n", caller,
			 line, addr, val);

	return _rtw_write32(adapter, addr, val);
}

int dbg_rtw_writeN(struct rtw_adapter *adapter, u32 addr, u32 length, u8 *data,
		   const char *caller, const int line)
{
	if (match_write_sniff_ranges(addr, length))
		DBG_871X("DBG_IO %s:%d rtw_writeN(0x%04x, %u)\n", caller,
			 line, addr, length);

	return _rtw_writeN(adapter, addr, length, data);
}
#endif
