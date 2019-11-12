/******************************************************************************
 *
 * Copyright(c) 2015 - 2018 Realtek Corporation.
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
 *****************************************************************************/
#define _RTL8822BS_HALMAC_C_

#include <drv_types.h>		/* struct dvobj_priv and etc. */
#include <rtw_sdio.h>		/* rtw_sdio_write_cmd53() */
#include "../../hal_halmac.h"	/* struct halmac_adapter* and etc. */
#include "../rtl8822b.h"	/* rtl8822b_get_tx_desc_size() */
#include "rtl8822bs.h"		/* rtl8822bs_write_port() */


static u8 sdio_write_data_rsvd_page(void *d, u8 *pBuf, u32 size)
{
	struct dvobj_priv *drv;
	struct halmac_adapter *halmac;
	struct halmac_api *api;
	u32 desclen, len;
	u8 *buf;
	u8 ret;


	drv = (struct dvobj_priv *)d;
	halmac = dvobj_to_halmac(drv);
	api = HALMAC_GET_API(halmac);
	desclen = rtl8822b_get_tx_desc_size(dvobj_get_primary_adapter(drv));
	len = desclen + size;
	buf = rtw_zmalloc(len);
	if (!buf)
		return 0;
	_rtw_memcpy(buf + desclen, pBuf, size);

	SET_TX_DESC_TXPKTSIZE_8822B(buf, size);
	SET_TX_DESC_OFFSET_8822B(buf, desclen);
	SET_TX_DESC_QSEL_8822B(buf, HALMAC_TXDESC_QSEL_BEACON);
	api->halmac_fill_txdesc_checksum(halmac, buf);

	ret = rtl8822bs_write_port(drv, len, buf);
	if (_SUCCESS == ret)
		ret = 1;
	else
		ret = 0;

	rtw_mfree(buf, len);

	return ret;
}

static u8 sdio_write_data_h2c(void *d, u8 *pBuf, u32 size)
{
	struct dvobj_priv *drv;
	struct halmac_adapter *halmac;
	struct halmac_api *api;
	u32 addr, desclen, len;
	u8 *buf;
	u8 ret;


	drv = (struct dvobj_priv *)d;
	halmac = dvobj_to_halmac(drv);
	api = HALMAC_GET_API(halmac);
	desclen = rtl8822b_get_tx_desc_size(dvobj_get_primary_adapter(drv));
	len = desclen + size;
	buf = rtw_zmalloc(len);
	if (!buf)
		return 0;
	_rtw_memcpy(buf + desclen, pBuf, size);

	SET_TX_DESC_TXPKTSIZE_8822B(buf, size);
	SET_TX_DESC_QSEL_8822B(buf, HALMAC_TXDESC_QSEL_H2C_CMD);
	api->halmac_fill_txdesc_checksum(halmac, buf);

	ret = rtl8822bs_write_port(drv, len, buf);
	if (_SUCCESS == ret)
		ret = 1;
	else
		ret = 0;

	rtw_mfree(buf, len);

	return ret;
}

int rtl8822bs_halmac_init_adapter(PADAPTER adapter)
{
	struct dvobj_priv *d;
	struct halmac_platform_api *api;
	int err;


	d = adapter_to_dvobj(adapter);
	api = &rtw_halmac_platform_api;
	api->SEND_RSVD_PAGE = sdio_write_data_rsvd_page;
	api->SEND_H2C_PKT = sdio_write_data_h2c;

	err = rtw_halmac_init_adapter(d, api);

	return err;
}
