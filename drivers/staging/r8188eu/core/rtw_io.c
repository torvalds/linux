// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

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
#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/rtw_io.h"
#include "../include/osdep_intf.h"
#include "../include/usb_ops.h"

#define rtw_le16_to_cpu(val)		le16_to_cpu(val)
#define rtw_le32_to_cpu(val)		le32_to_cpu(val)
#define rtw_cpu_to_le16(val)		cpu_to_le16(val)
#define rtw_cpu_to_le32(val)		cpu_to_le32(val)

int _rtw_write8_async(struct adapter *adapter, u32 addr, u8 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &pio_priv->intf;
	int (*_write8_async)(struct intf_hdl *pintfhdl, u32 addr, u8 val);
	int ret;

	_write8_async = pintfhdl->io_ops._write8_async;

	ret = _write8_async(pintfhdl, addr, val);


	return RTW_STATUS_CODE(ret);
}

int _rtw_write16_async(struct adapter *adapter, u32 addr, u16 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &pio_priv->intf;
	int (*_write16_async)(struct intf_hdl *pintfhdl, u32 addr, u16 val);
	int ret;

	_write16_async = pintfhdl->io_ops._write16_async;
	ret = _write16_async(pintfhdl, addr, val);

	return RTW_STATUS_CODE(ret);
}

int _rtw_write32_async(struct adapter *adapter, u32 addr, u32 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &pio_priv->intf;
	int (*_write32_async)(struct intf_hdl *pintfhdl, u32 addr, u32 val);
	int ret;

	_write32_async = pintfhdl->io_ops._write32_async;
	ret = _write32_async(pintfhdl, addr, val);

	return RTW_STATUS_CODE(ret);
}

void _rtw_read_port(struct adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{
	u32 (*_read_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &pio_priv->intf;



	if (adapter->bDriverStopped || adapter->bSurpriseRemoved)
	     return;

	_read_port = pintfhdl->io_ops._read_port;

	_read_port(pintfhdl, addr, cnt, pmem);


}

void _rtw_read_port_cancel(struct adapter *adapter)
{
	void (*_read_port_cancel)(struct intf_hdl *pintfhdl);
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &pio_priv->intf;

	_read_port_cancel = pintfhdl->io_ops._read_port_cancel;

	if (_read_port_cancel)
		_read_port_cancel(pintfhdl);
}

u32 _rtw_write_port(struct adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{
	u32 (*_write_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &pio_priv->intf;
	u32 ret = _SUCCESS;



	_write_port = pintfhdl->io_ops._write_port;

	ret = _write_port(pintfhdl, addr, cnt, pmem);



	return ret;
}

u32 _rtw_write_port_and_wait(struct adapter *adapter, u32 addr, u32 cnt, u8 *pmem, int timeout_ms)
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

void _rtw_write_port_cancel(struct adapter *adapter)
{
	void (*_write_port_cancel)(struct intf_hdl *pintfhdl);
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &pio_priv->intf;

	_write_port_cancel = pintfhdl->io_ops._write_port_cancel;

	if (_write_port_cancel)
		_write_port_cancel(pintfhdl);
}

int rtw_init_io_priv(struct adapter *padapter, void (*set_intf_ops)(struct _io_ops *pops))
{
	struct io_priv	*piopriv = &padapter->iopriv;
	struct intf_hdl *pintf = &piopriv->intf;

	if (!set_intf_ops)
		return _FAIL;

	piopriv->padapter = padapter;
	pintf->padapter = padapter;
	pintf->pintf_dev = adapter_to_dvobj(padapter);

	set_intf_ops(&pintf->io_ops);

	return _SUCCESS;
}
