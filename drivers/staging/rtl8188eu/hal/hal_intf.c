// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#define _HAL_INTF_C_
#include <hal_intf.h>

uint rtw_hal_init(struct adapter *adapt)
{
	uint status = _SUCCESS;

	adapt->hw_init_completed = false;

	status = rtl8188eu_hal_init(adapt);

	if (status == _SUCCESS) {
		adapt->hw_init_completed = true;

		if (adapt->registrypriv.notch_filter == 1)
			rtw_hal_notch_filter(adapt, 1);
	} else {
		adapt->hw_init_completed = false;
	}

	return status;
}

uint rtw_hal_deinit(struct adapter *adapt)
{
	uint status = _SUCCESS;

	status = rtl8188eu_hal_deinit(adapt);

	if (status == _SUCCESS)
		adapt->hw_init_completed = false;

	return status;
}

void rtw_hal_update_ra_mask(struct adapter *adapt, u32 mac_id, u8 rssi_level)
{
	struct mlme_priv *pmlmepriv = &adapt->mlmepriv;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
#ifdef CONFIG_88EU_AP_MODE
		struct sta_info *psta = NULL;
		struct sta_priv *pstapriv = &adapt->stapriv;

		if (mac_id - 1 > 0)
			psta = pstapriv->sta_aid[mac_id - 2];
		if (psta)
			add_RATid(adapt, psta, 0);/* todo: based on rssi_level*/
#endif
	} else {
		UpdateHalRAMask8188EUsb(adapt, mac_id, rssi_level);
	}
}
