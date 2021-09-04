// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2012 Realtek Corporation. */

#include "../include/rtw_sreset.h"

void sreset_init_value(struct adapter *padapter)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;

	mutex_init(&psrtpriv->silentreset_mutex);
	psrtpriv->silent_reset_inprogress = false;
	psrtpriv->wifi_error_status = WIFI_STATUS_SUCCESS;
	psrtpriv->last_tx_time = 0;
	psrtpriv->last_tx_complete_time = 0;
}
void sreset_reset_value(struct adapter *padapter)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;

	psrtpriv->silent_reset_inprogress = false;
	psrtpriv->wifi_error_status = WIFI_STATUS_SUCCESS;
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
		psrtpriv->wifi_error_status = WIFI_IF_NOT_EXIST;
	} else if (val32 != 0) {
		DBG_88E("txdmastatu(%x)\n", val32);
		psrtpriv->wifi_error_status = WIFI_MAC_TXDMA_ERROR;
	}

	if (WIFI_STATUS_SUCCESS != psrtpriv->wifi_error_status) {
		DBG_88E("==>%s error_status(0x%x)\n", __func__, psrtpriv->wifi_error_status);
		status = (psrtpriv->wifi_error_status & (~(USB_READ_PORT_FAIL | USB_WRITE_PORT_FAIL)));
	}
	DBG_88E("==> %s wifi_status(0x%x)\n", __func__, status);

	/* status restore */
	psrtpriv->wifi_error_status = WIFI_STATUS_SUCCESS;

	return status;
}

void sreset_set_wifi_error_status(struct adapter *padapter, u32 status)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(padapter);
	pHalData->srestpriv.wifi_error_status = status;
}
