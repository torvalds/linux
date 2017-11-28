/******************************************************************************
 *
 * Copyright(c) 2015 - 2016 Realtek Corporation. All rights reserved.
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
 ******************************************************************************/
#define _RTW_SDIO_C_

#include <drv_types.h>		/* struct dvobj_priv and etc. */
#include <drv_types_sdio.h>	/* RTW_SDIO_ADDR_CMD52_GEN */

/*
 * Description:
 *	Use SDIO cmd52 or cmd53 to read/write data
 *
 * Parameters:
 *	d	pointer of device object(struct dvobj_priv)
 *	addr	SDIO address, 17 bits
 *	buf	buffer for I/O
 *	len	length
 *	write	0:read, 1:write
 *	cmd52	0:cmd52, 1:cmd53
 *
 * Return:
 *	_SUCCESS	I/O ok.
 *	_FAIL		I/O fail.
 */
static u8 sdio_io(struct dvobj_priv *d, u32 addr, void *buf, size_t len, u8 write, u8 cmd52)
{
	int err;


	if (cmd52)
		addr = RTW_SDIO_ADDR_CMD52_GEN(addr);

	if (write)
		err = d->intf_ops->write(d, addr, buf, len, 0);
	else
		err = d->intf_ops->read(d, addr, buf, len, 0);
	if (err) {
		RTW_INFO("%s: [ERROR] %s FAIL! error(%d)\n",
			 __FUNCTION__, write ? "write" : "read", err);
		return _FAIL;
	}

	return _SUCCESS;
}

u8 rtw_sdio_read_cmd52(struct dvobj_priv *d, u32 addr, void *buf, size_t len)
{
	return sdio_io(d, addr, buf, len, 0, 1);
}

u8 rtw_sdio_read_cmd53(struct dvobj_priv *d, u32 addr, void *buf, size_t len)
{
	return sdio_io(d, addr, buf, len, 0, 0);
}

u8 rtw_sdio_write_cmd52(struct dvobj_priv *d, u32 addr, void *buf, size_t len)
{
	return sdio_io(d, addr, buf, len, 1, 1);
}

u8 rtw_sdio_write_cmd53(struct dvobj_priv *d, u32 addr, void *buf, size_t len)
{
	return sdio_io(d, addr, buf, len, 1, 0);
}

u8 rtw_sdio_f0_read(struct dvobj_priv *d, u32 addr, void *buf, size_t len)
{
	int err;
	u8 ret;


	ret = _SUCCESS;
	addr = RTW_SDIO_ADDR_F0_GEN(addr);

	err = d->intf_ops->read(d, addr, buf, len, 0);
	if (err) {
		RTW_INFO("%s: [ERROR] Read f0 register FAIL!\n", __FUNCTION__);
		ret = _FAIL;
	}


	return ret;
}
