/******************************************************************************
 *
 * Copyright(c) 2007 - 2013 Realtek Corporation. All rights reserved.
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

#ifndef	__DM_H__
#define __DM_H__

#define	DYNAMIC_FUNC_DISABLE		(0x0)
#define	DYNAMIC_FUNC_DIG			BIT(0)
#define	DYNAMIC_FUNC_HP			BIT(1)
#define	DYNAMIC_FUNC_SS			BIT(2) //Tx Power Tracking
#define DYNAMIC_FUNC_BT			BIT(3)
#define DYNAMIC_FUNC_ANT_DIV		BIT(4)
#define DYNAMIC_FUNC_ADAPTIVITY	BIT(5)

void rtw_dm_ability_msg(void *sel, _adapter *adapter);
void rtw_dm_ability_set(_adapter *adapter, u8 ability);

void rtw_dm_check_rxfifo_full(_adapter *adapter);

void rtw_odm_dbg_comp_msg(void *sel, _adapter *adapter);
void rtw_odm_dbg_comp_set(_adapter *adapter, u64 comps);
void rtw_odm_dbg_level_msg(void *sel, _adapter *adapter);
void rtw_odm_dbg_level_set(_adapter *adapter, u32 level);

void rtw_odm_adaptivity_parm_msg(void *sel, _adapter *adapter);
void rtw_odm_adaptivity_parm_set(_adapter *pAdapter, s8 TH_L2H_ini, s8 TH_EDCCA_HL_diff,
	s8 IGI_Base, bool ForceEDCCA, u8 AdapEn_RSSI, u8 IGI_LowerBound);

void rtw_odm_init(_adapter *adapter);

typedef struct DM_Out_Source_Dynamic_Mechanism_Structure
{
	_adapter *Adapter;
	u32 SupportICType;
	u8 SupportInterface;

	u64 DebugComponents;
	u32 DebugLevel;

	void (*write_dig)(_adapter *adapter);

#ifdef CONFIG_ODM_ADAPTIVITY
	/* Ported from ODM, for ESTI Adaptivity test */
	s8 TH_L2H_ini;
	s8 TH_EDCCA_HL_diff;
	s8 IGI_Base;
	u8 IGI_target;
	bool ForceEDCCA;
	u8 AdapEn_RSSI;
	s8 Force_TH_H;
	s8 Force_TH_L;
	u8 IGI_LowerBound;

	bool	bPreEdccaEnable;

	// add by Yu Cehn for adaptivtiy
	bool adaptivity_flag;
	bool NHM_disable;
	bool TxHangFlg;
	u8 tolerance_cnt;
	u64 NHMCurTxOkcnt;
	u64 NHMCurRxOkcnt;
	u64 NHMLastTxOkcnt;
	u64 NHMLastRxOkcnt;
	u8 txEdcca1;
	u8 txEdcca0;
	s8 H2L_lb;
	s8 L2H_lb;
	u8 Adaptivity_IGI_upper;
#endif

} DM_ODM_T, *PDM_ODM_T; /* ODM structure for ease of partial porting */

void odm_AdaptivityInit(PDM_ODM_T pDM_Odm);
void odm_Adaptivity(PDM_ODM_T pDM_Odm);

#endif /* __DM_H__ */

