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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

#include <rtw_sreset.h>

void sreset_init_value(struct adapter *padapter)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;

	_rtw_mutex_init(&psrtpriv->silentreset_mutex);
	psrtpriv->silent_reset_inprogress = false;
	psrtpriv->Wifi_Error_Status = WIFI_STATUS_SUCCESS;
	psrtpriv->last_tx_time = 0;
	psrtpriv->last_tx_complete_time = 0;
}
void sreset_reset_value(struct adapter *padapter)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;

	psrtpriv->silent_reset_inprogress = false;
	psrtpriv->Wifi_Error_Status = WIFI_STATUS_SUCCESS;
	psrtpriv->last_tx_time = 0;
	psrtpriv->last_tx_complete_time = 0;
}

u8 sreset_get_wifi_status(struct adapter *padapter)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;

	u8 status = WIFI_STATUS_SUCCESS;
	u32 val32 = 0;

	if (psrtpriv->silent_reset_inprogress)
		return status;
	val32 = rtw_read32(padapter, REG_TXDMA_STATUS);
	if (val32 == 0xeaeaeaea) {
		psrtpriv->Wifi_Error_Status = WIFI_IF_NOT_EXIST;
	} else if (val32 != 0) {
		DBG_88E("txdmastatu(%x)\n", val32);
		psrtpriv->Wifi_Error_Status = WIFI_MAC_TXDMA_ERROR;
	}

	if (WIFI_STATUS_SUCCESS != psrtpriv->Wifi_Error_Status) {
		DBG_88E("==>%s error_status(0x%x)\n", __func__, psrtpriv->Wifi_Error_Status);
		status = (psrtpriv->Wifi_Error_Status & (~(USB_READ_PORT_FAIL|USB_WRITE_PORT_FAIL)));
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
