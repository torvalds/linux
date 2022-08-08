/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2013 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __RTW_ODM_H__
#define __RTW_ODM_H__

#include <drv_types.h>

/*
* This file provides utilities/wrappers for rtw driver to use ODM
*/

void rtw_odm_dbg_comp_msg(struct adapter *adapter);
void rtw_odm_dbg_comp_set(struct adapter *adapter, u64 comps);
void rtw_odm_dbg_level_msg(void *sel, struct adapter *adapter);
void rtw_odm_dbg_level_set(struct adapter *adapter, u32 level);

void rtw_odm_ability_msg(void *sel, struct adapter *adapter);
void rtw_odm_ability_set(struct adapter *adapter, u32 ability);

void rtw_odm_adaptivity_parm_msg(void *sel, struct adapter *adapter);
void rtw_odm_adaptivity_parm_set(struct adapter *adapter, s8 TH_L2H_ini, s8 TH_EDCCA_HL_diff,
	s8 IGI_Base, bool ForceEDCCA, u8 AdapEn_RSSI, u8 IGI_LowerBound);
void rtw_odm_get_perpkt_rssi(void *sel, struct adapter *adapter);
#endif /*  __RTW_ODM_H__ */
