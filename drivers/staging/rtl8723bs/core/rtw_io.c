// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
/*

The purpose of rtw_io.c

a. provides the API

b. provides the protocol engine

c. provides the software interface between caller and the hardware interface


Compiler Flag Option:

1. CONFIG_SDIO_HCI:
    a. USE_SYNC_IRP:  Only sync operations are provided.
    b. USE_ASYNC_IRP:Both sync/async operations are provided.

jackson@realtek.com.tw

*/

#include <drv_types.h>

u8 rtw_read8(struct adapter *adapter, u32 addr)
{
	/* struct	io_queue	*pio_queue = (struct io_queue *)adapter->pio_queue; */
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	u8 (*_read8)(struct intf_hdl *pintfhdl, u32 addr);

	_read8 = pintfhdl->io_ops._read8;

	return _read8(pintfhdl, addr);
}

u16 rtw_read16(struct adapter *adapter, u32 addr)
{
	/* struct	io_queue	*pio_queue = (struct io_queue *)adapter->pio_queue; */
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	u16 (*_read16)(struct intf_hdl *pintfhdl, u32 addr);

	_read16 = pintfhdl->io_ops._read16;

	return _read16(pintfhdl, addr);
}

u32 rtw_read32(struct adapter *adapter, u32 addr)
{
	/* struct	io_queue	*pio_queue = (struct io_queue *)adapter->pio_queue; */
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	u32 (*_read32)(struct intf_hdl *pintfhdl, u32 addr);

	_read32 = pintfhdl->io_ops._read32;

	return _read32(pintfhdl, addr);

}

int rtw_write8(struct adapter *adapter, u32 addr, u8 val)
{
	/* struct	io_queue	*pio_queue = (struct io_queue *)adapter->pio_queue; */
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	int (*_write8)(struct intf_hdl *pintfhdl, u32 addr, u8 val);
	int ret;

	_write8 = pintfhdl->io_ops._write8;

	ret = _write8(pintfhdl, addr, val);

	return RTW_STATUS_CODE(ret);
}
int rtw_write16(struct adapter *adapter, u32 addr, u16 val)
{
	/* struct	io_queue	*pio_queue = (struct io_queue *)adapter->pio_queue; */
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	int (*_write16)(struct intf_hdl *pintfhdl, u32 addr, u16 val);
	int ret;

	_write16 = pintfhdl->io_ops._write16;

	ret = _write16(pintfhdl, addr, val);
	return RTW_STATUS_CODE(ret);
}
int rtw_write32(struct adapter *adapter, u32 addr, u32 val)
{
	/* struct	io_queue	*pio_queue = (struct io_queue *)adapter->pio_queue; */
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	int (*_write32)(struct intf_hdl *pintfhdl, u32 addr, u32 val);
	int ret;

	_write32 = pintfhdl->io_ops._write32;

	ret = _write32(pintfhdl, addr, val);

	return RTW_STATUS_CODE(ret);
}

u32 rtw_write_port(struct adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{
	u32 (*_write_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);

	_write_port = pintfhdl->io_ops._write_port;

	return _write_port(pintfhdl, addr, cnt, pmem);
}

int rtw_init_io_priv(struct adapter *padapter, void (*set_intf_ops)(struct adapter *padapter, struct _io_ops *pops))
{
	struct io_priv *piopriv = &padapter->iopriv;
	struct intf_hdl *pintf = &piopriv->intf;

	if (!set_intf_ops)
		return _FAIL;

	piopriv->padapter = padapter;
	pintf->padapter = padapter;
	pintf->pintf_dev = adapter_to_dvobj(padapter);

	set_intf_ops(padapter, &pintf->io_ops);

	return _SUCCESS;
}

/*
* Increase and check if the continual_io_error of this @param dvobjprive is larger than MAX_CONTINUAL_IO_ERR
* @return true:
* @return false:
*/
int rtw_inc_and_chk_continual_io_error(struct dvobj_priv *dvobj)
{
	int error_count = atomic_inc_return(&dvobj->continual_io_error);

	if (error_count > MAX_CONTINUAL_IO_ERR)
		return true;

	return false;
}

/*
* Set the continual_io_error of this @param dvobjprive to 0
*/
void rtw_reset_continual_io_error(struct dvobj_priv *dvobj)
{
	atomic_set(&dvobj->continual_io_error, 0);
}
