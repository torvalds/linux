/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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

#include <rtw_sreset.h>
#include <usb_ops_linux.h>

void sreset_init_value(struct adapter *padapter)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;

	psrtpriv->Wifi_Error_Status = WIFI_STATUS_SUCCESS;
}

u8 sreset_get_wifi_status(struct adapter *padapter)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;

	u8 status = WIFI_STATUS_SUCCESS;
	u32 val32 = 0;

	val32 = usb_read32(padapter, REG_TXDMA_STATUS);
	if (val32 == 0xeaeaeaea) {
		psrtpriv->Wifi_Error_Status = WIFI_IF_NOT_EXIST;
	} else if (val32 != 0) {
		DBG_88E("txdmastatu(%x)\n", val32);
		psrtpriv->Wifi_Error_Status = WIFI_MAC_TXDMA_ERROR;
	}

	if (WIFI_STATUS_SUCCESS != psrtpriv->Wifi_Error_Status) {
		DBG_88E("==>%s error_status(0x%x)\n", __func__, psrtpriv->Wifi_Error_Status);
		status = psrtpriv->Wifi_Error_Status & (~(USB_READ_PORT_FAIL|USB_WRITE_PORT_FAIL));
	}
	DBG_88E("==> %s wifi_status(0x%x)\n", __func__, status);

	/* status restore */
	psrtpriv->Wifi_Error_Status = WIFI_STATUS_SUCCESS;

	return status;
}

void sreset_set_wifi_error_status(struct adapter *padapter, u32 status)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(padapter);
	pHalData->srestpriv.Wifi_Error_Status = status;
}
