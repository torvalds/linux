// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include <rtw_sreset.h>
#include <usb_ops_linux.h>

void rtw_hal_sreset_init(struct adapter *padapter)
{
	struct sreset_priv *psrtpriv = &padapter->HalData->srestpriv;

	psrtpriv->Wifi_Error_Status = WIFI_STATUS_SUCCESS;
}

void sreset_set_wifi_error_status(struct adapter *padapter, u32 status)
{
	padapter->HalData->srestpriv.Wifi_Error_Status = status;
}
