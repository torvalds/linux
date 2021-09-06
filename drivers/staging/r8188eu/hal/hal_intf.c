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

u8 rtw_hal_set_def_var(struct adapter *adapt, enum hal_def_variable var,
		      void *val)
{
	if (adapt->HalFunc.SetHalDefVarHandler)
		return adapt->HalFunc.SetHalDefVarHandler(adapt, var, val);
	return _FAIL;
}

u8 rtw_hal_get_def_var(struct adapter *adapt,
		       enum hal_def_variable var, void *val)
{
	if (adapt->HalFunc.GetHalDefVarHandler)
		return adapt->HalFunc.GetHalDefVarHandler(adapt, var, val);
	return _FAIL;
}

u32 rtw_hal_inirp_init(struct adapter *adapt)
{
	u32 rst = _FAIL;

	if (adapt->HalFunc.inirp_init)
		rst = adapt->HalFunc.inirp_init(adapt);
	else
		DBG_88E(" %s HalFunc.inirp_init is NULL!!!\n", __func__);
	return rst;
}

u32 rtw_hal_inirp_deinit(struct adapter *adapt)
{
	if (adapt->HalFunc.inirp_deinit)
		return adapt->HalFunc.inirp_deinit(adapt);

	return _FAIL;
}

s32 rtw_hal_xmit(struct adapter *adapt, struct xmit_frame *pxmitframe)
{
	if (adapt->HalFunc.hal_xmit)
		return adapt->HalFunc.hal_xmit(adapt, pxmitframe);

	return false;
}

s32 rtw_hal_mgnt_xmit(struct adapter *adapt, struct xmit_frame *pmgntframe)
{
	s32 ret = _FAIL;
	if (adapt->HalFunc.mgnt_xmit)
		ret = adapt->HalFunc.mgnt_xmit(adapt, pmgntframe);
	return ret;
}

s32 rtw_hal_init_xmit_priv(struct adapter *adapt)
{
	if (adapt->HalFunc.init_xmit_priv)
		return adapt->HalFunc.init_xmit_priv(adapt);
	return _FAIL;
}

s32 rtw_hal_init_recv_priv(struct adapter *adapt)
{
	if (adapt->HalFunc.init_recv_priv)
		return adapt->HalFunc.init_recv_priv(adapt);

	return _FAIL;
}

void rtw_hal_free_recv_priv(struct adapter *adapt)
{
	if (adapt->HalFunc.free_recv_priv)
		adapt->HalFunc.free_recv_priv(adapt);
}

void rtw_hal_update_ra_mask(struct adapter *adapt, u32 mac_id, u8 rssi_level)
{
	struct mlme_priv *pmlmepriv = &adapt->mlmepriv;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
#ifdef CONFIG_88EU_AP_MODE
		struct sta_info *psta = NULL;
		struct sta_priv *pstapriv = &adapt->stapriv;
		if ((mac_id - 1) > 0)
			psta = pstapriv->sta_aid[(mac_id - 1) - 1];
		if (psta)
			add_RATid(adapt, psta, 0);/* todo: based on rssi_level*/
#endif
	} else {
		if (adapt->HalFunc.UpdateRAMaskHandler)
			adapt->HalFunc.UpdateRAMaskHandler(adapt, mac_id,
							      rssi_level);
	}
}

u32 rtw_hal_read_bbreg(struct adapter *adapt, u32 regaddr, u32 bitmask)
{
	u32 data = 0;

	if (adapt->HalFunc.read_bbreg)
		data = adapt->HalFunc.read_bbreg(adapt, regaddr, bitmask);
	return data;
}

void rtw_hal_write_bbreg(struct adapter *adapt, u32 regaddr, u32 bitmask,
			 u32 data)
{
	if (adapt->HalFunc.write_bbreg)
		adapt->HalFunc.write_bbreg(adapt, regaddr, bitmask, data);
}

u32 rtw_hal_read_rfreg(struct adapter *adapt, enum rf_radio_path rfpath,
		       u32 regaddr, u32 bitmask)
{
	u32 data = 0;

	if (adapt->HalFunc.read_rfreg)
		data = adapt->HalFunc.read_rfreg(adapt, rfpath, regaddr,
						    bitmask);
	return data;
}

void rtw_hal_write_rfreg(struct adapter *adapt, enum rf_radio_path rfpath,
			 u32 regaddr, u32 bitmask, u32 data)
{
	if (adapt->HalFunc.write_rfreg)
		adapt->HalFunc.write_rfreg(adapt, rfpath, regaddr,
					      bitmask, data);
}

int rtw_hal_iol_cmd(struct adapter  *adapter, struct xmit_frame *xmit_frame,
		    u32 max_wating_ms, u32 bndy_cnt)
{
	if (adapter->HalFunc.IOL_exec_cmds_sync)
		return adapter->HalFunc.IOL_exec_cmds_sync(adapter, xmit_frame,
							   max_wating_ms,
							   bndy_cnt);
	return _FAIL;
}
