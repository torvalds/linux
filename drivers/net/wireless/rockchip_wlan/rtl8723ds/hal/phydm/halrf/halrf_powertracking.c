/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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

/* ************************************************************
 * include files
 * ************************************************************ */
#include "mp_precomp.h"
#include "phydm_precomp.h"


boolean
odm_check_power_status(
	void		*p_dm_void
)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER		*p_adapter = p_dm->adapter;

	RT_RF_POWER_STATE	rt_state;
	PMGNT_INFO			p_mgnt_info	= &(p_adapter->MgntInfo);

	/* 2011/07/27 MH We are not testing ready~~!! We may fail to get correct value when init sequence. */
	if (p_mgnt_info->init_adpt_in_progress == true) {
		PHYDM_DBG(p_dm, ODM_COMP_INIT, ("check_pow_status Return true, due to initadapter\n"));
		return	true;
	}

	/*  */
	/*	2011/07/19 MH We can not execute tx pwoer tracking/ LLC calibrate or IQK. */
	/*  */
	p_adapter->HalFunc.GetHwRegHandler(p_adapter, HW_VAR_RF_STATE, (u8 *)(&rt_state));
	if (p_adapter->bDriverStopped || p_adapter->bDriverIsGoingToPnpSetPowerSleep || rt_state == eRfOff) {
		PHYDM_DBG(p_dm, ODM_COMP_INIT, ("check_pow_status Return false, due to %d/%d/%d\n",
			p_adapter->bDriverStopped, p_adapter->bDriverIsGoingToPnpSetPowerSleep, rt_state));
		return	false;
	}
#endif
	return	true;
	
}

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
void
halrf_update_pwr_track(
	void		*p_dm_void,
	u8		rate
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	u8			path_idx = 0;
#endif

	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("Pwr Track Get rate=0x%x\n", rate));

	p_dm->tx_rate = rate;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#if DEV_BUS_TYPE == RT_PCI_INTERFACE
#if USE_WORKITEM
	odm_schedule_work_item(&p_dm->ra_rpt_workitem);
#else
	if (p_dm->support_ic_type == ODM_RTL8821) {
#if (RTL8821A_SUPPORT == 1)
		odm_tx_pwr_track_set_pwr8821a(p_dm, MIX_MODE, RF_PATH_A, 0);
#endif
	} else if (p_dm->support_ic_type == ODM_RTL8812) {
		for (path_idx = RF_PATH_A; path_idx < MAX_PATH_NUM_8812A; path_idx++) {
#if (RTL8812A_SUPPORT == 1)
			odm_tx_pwr_track_set_pwr8812a(p_dm, MIX_MODE, path_idx, 0);
#endif
		}
	} else if (p_dm->support_ic_type == ODM_RTL8723B) {
#if (RTL8723B_SUPPORT == 1)
		odm_tx_pwr_track_set_pwr_8723b(p_dm, MIX_MODE, RF_PATH_A, 0);
#endif
	} else if (p_dm->support_ic_type == ODM_RTL8192E) {
		for (path_idx = RF_PATH_A; path_idx < MAX_PATH_NUM_8192E; path_idx++) {
#if (RTL8192E_SUPPORT == 1)
			odm_tx_pwr_track_set_pwr92_e(p_dm, MIX_MODE, path_idx, 0);
#endif
		}
	} else if (p_dm->support_ic_type == ODM_RTL8188E) {
#if (RTL8188E_SUPPORT == 1)
		odm_tx_pwr_track_set_pwr88_e(p_dm, MIX_MODE, RF_PATH_A, 0);
#endif
	}
#endif
#else
	odm_schedule_work_item(&p_dm->ra_rpt_workitem);
#endif
#endif

}

#endif



#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void
halrf_update_init_rate_work_item_callback(
	void	*p_context
)
{
	struct _ADAPTER	*adapter = (struct _ADAPTER *)p_context;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->DM_OutSrc;
	u8		p = 0;

	if (p_dm->support_ic_type == ODM_RTL8821) {
		odm_tx_pwr_track_set_pwr8821a(p_dm, MIX_MODE, RF_PATH_A, 0);
		/**/
	} else if (p_dm->support_ic_type == ODM_RTL8812) {
		for (p = RF_PATH_A; p < MAX_PATH_NUM_8812A; p++) {    /*DOn't know how to include &c*/

			odm_tx_pwr_track_set_pwr8812a(p_dm, MIX_MODE, p, 0);
			/**/
		}
	} else if (p_dm->support_ic_type == ODM_RTL8723B) {
		odm_tx_pwr_track_set_pwr_8723b(p_dm, MIX_MODE, RF_PATH_A, 0);
		/**/
	} else if (p_dm->support_ic_type == ODM_RTL8192E) {
		for (p = RF_PATH_A; p < MAX_PATH_NUM_8192E; p++) {   /*DOn't know how to include &c*/
			odm_tx_pwr_track_set_pwr92_e(p_dm, MIX_MODE, p, 0);
			/**/
		}
	} else if (p_dm->support_ic_type == ODM_RTL8188E) {
		odm_tx_pwr_track_set_pwr88_e(p_dm, MIX_MODE, RF_PATH_A, 0);
		/**/
	}
}
#endif



