// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2012 Realtek Corporation. */

#define _HAL_INTF_C_
#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/hal_intf.h"

uint	 rtw_hal_init(struct adapter *adapt)
{
	uint	status = _SUCCESS;

	adapt->hw_init_completed = false;

	status = adapt->HalFunc.hal_init(adapt);

	if (status == _SUCCESS) {
		adapt->hw_init_completed = true;

		if (adapt->registrypriv.notch_filter == 1)
			hal_notch_filter_8188e(adapt, 1);
	} else {
		adapt->hw_init_completed = false;
		DBG_88E("rtw_hal_init: hal__init fail\n");
	}

	return status;
}

uint rtw_hal_deinit(struct adapter *adapt)
{
	uint	status = _SUCCESS;

	status = adapt->HalFunc.hal_deinit(adapt);

	if (status == _SUCCESS)
		adapt->hw_init_completed = false;
	else
		DBG_88E("\n rtw_hal_deinit: hal_init fail\n");

	return status;
}

void rtw_hal_set_hwreg(struct adapter *adapt, u8 variable, u8 *val)
{
	if (adapt->HalFunc.SetHwRegHandler)
		adapt->HalFunc.SetHwRegHandler(adapt, variable, val);
}

void rtw_hal_get_hwreg(struct adapter *adapt, u8 variable, u8 *val)
{
	if (adapt->HalFunc.GetHwRegHandler)
		adapt->HalFunc.GetHwRegHandler(adapt, variable, val);
}

u32 rtw_hal_inirp_deinit(struct adapter *adapt)
{
	if (adapt->HalFunc.inirp_deinit)
		return adapt->HalFunc.inirp_deinit(adapt);

	return _FAIL;
}

void rtw_hal_update_ra_mask(struct adapter *adapt, u32 mac_id, u8 rssi_level)
{
	struct mlme_priv *pmlmepriv = &adapt->mlmepriv;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		struct sta_info *psta = NULL;
		struct sta_priv *pstapriv = &adapt->stapriv;
		if ((mac_id - 1) > 0)
			psta = pstapriv->sta_aid[(mac_id - 1) - 1];
		if (psta)
			add_RATid(adapt, psta, 0);/* todo: based on rssi_level*/
	} else {
		UpdateHalRAMask8188EUsb(adapt, mac_id, rssi_level);
	}
}
