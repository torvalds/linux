/******************************************************************************
 *
 * Copyright(c) 2013 Realtek Corporation. All rights reserved.
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

#include <rtw_odm.h>
#ifdef CONFIG_RTL8192C
#include <rtl8192c_hal.h>
#endif
#ifdef CONFIG_RTL8192D
#include <rtl8192d_hal.h>
#endif
#ifdef CONFIG_RTL8723A
#include <rtl8723a_hal.h>
#endif
#ifdef CONFIG_RTL8188E
#include <rtl8188e_hal.h>
#endif

const char *odm_comp_str[] = {
	"ODM_COMP_DIG",
	"ODM_COMP_RA_MASK",
	"ODM_COMP_DYNAMIC_TXPWR",
	"ODM_COMP_FA_CNT",
	"ODM_COMP_RSSI_MONITOR",
	"ODM_COMP_CCK_PD",
	"ODM_COMP_ANT_DIV",
	"ODM_COMP_PWR_SAVE",
	"ODM_COMP_PWR_TRAIN",
	"ODM_COMP_RATE_ADAPTIVE",
	"ODM_COMP_PATH_DIV",
	"ODM_COMP_PSD",
	"ODM_COMP_DYNAMIC_PRICCA",
	"ODM_COMP_RXHP",
	NULL,
	NULL,
	"ODM_COMP_EDCA_TURBO",
	"ODM_COMP_EARLY_MODE",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"ODM_COMP_TX_PWR_TRACK",
	"ODM_COMP_RX_GAIN_TRACK",
	"ODM_COMP_CALIBRATION",
	NULL,
	NULL,
	NULL,
	"ODM_COMP_COMMON",
	"ODM_COMP_INIT",
};

#define RTW_ODM_COMP_MAX 32

const char *odm_ability_str[] = {
	"ODM_BB_DIG",
	"ODM_BB_RA_MASK",
	"ODM_BB_DYNAMIC_TXPWR",
	"ODM_BB_FA_CNT",
	"ODM_BB_RSSI_MONITOR",
	"ODM_BB_CCK_PD	",
	"ODM_BB_ANT_DIV",
	"ODM_BB_PWR_SAVE",
	"ODM_BB_PWR_TRAIN",
	"ODM_BB_RATE_ADAPTIVE",
	"ODM_BB_PATH_DIV",
	"ODM_BB_PSD",
	"ODM_BB_RXHP",
	"ODM_BB_ADAPTIVITY",
	"ODM_BB_DYNAMIC_ATC",
	NULL,
	"ODM_MAC_EDCA_TURBO",
	"ODM_MAC_EARLY_MODE",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"ODM_RF_TX_PWR_TRACK",
	"ODM_RF_RX_GAIN_TRACK",
	"ODM_RF_CALIBRATION",
};

#define RTW_ODM_ABILITY_MAX 27

const char *odm_dbg_level_str[] = {
	NULL,
	"ODM_DBG_OFF",
	"ODM_DBG_SERIOUS",
	"ODM_DBG_WARNING",
	"ODM_DBG_LOUD",
	"ODM_DBG_TRACE	",
};

#define RTW_ODM_DBG_LEVEL_NUM 6

int _rtw_odm_dbg_comp_msg(_adapter *adapter, char *buf, int len)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &pHalData->odmpriv;
	int cnt = 0;
	u64 dbg_comp;
	int i;

	rtw_hal_get_def_var(adapter, HW_DEF_ODM_DBG_FLAG, &dbg_comp);
	cnt += snprintf(buf+cnt, len-cnt, "odm.DebugComponents = 0x%016llx \n", dbg_comp);
	for (i=0;i<RTW_ODM_COMP_MAX;i++) {
		if (odm_comp_str[i])
		cnt += snprintf(buf+cnt, len-cnt, "%cBIT%-2d %s\n",
			(BIT0 << i) & dbg_comp ? '+' : ' ', i, odm_comp_str[i]);
	}

	return cnt;
}

void rtw_odm_dbg_comp_msg(_adapter *adapter)
{
	char buf[768] = {0};

	_rtw_odm_dbg_comp_msg(adapter, buf, 768);
	DBG_871X_LEVEL(_drv_always_, "\n%s", buf);
}

inline void rtw_odm_dbg_comp_set(_adapter *adapter, u64 comps)
{
	rtw_hal_set_def_var(adapter, HW_DEF_ODM_DBG_FLAG, &comps);
}

int _rtw_odm_dbg_level_msg(_adapter *adapter, char *buf, int len)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &pHalData->odmpriv;
	int cnt = 0;
	u32 dbg_level;
	int i;

	rtw_hal_get_def_var(adapter, HW_DEF_ODM_DBG_LEVEL, &dbg_level);
	cnt += snprintf(buf+cnt, len-cnt, "odm.DebugDebugLevel = %u\n", dbg_level);
	for (i=0;i<RTW_ODM_DBG_LEVEL_NUM;i++) {
		if (odm_dbg_level_str[i])
			cnt += snprintf(buf+cnt, len-cnt, "%u %s\n", i, odm_dbg_level_str[i]);
	}

	return cnt;
}

void rtw_odm_dbg_level_msg(_adapter *adapter)
{
	char buf[100] = {0};

	_rtw_odm_dbg_comp_msg(adapter, buf, 100);
	DBG_871X_LEVEL(_drv_always_, "\n%s", buf);
}

inline void rtw_odm_dbg_level_set(_adapter *adapter, u32 level)
{
	rtw_hal_set_def_var(adapter, HW_DEF_ODM_DBG_LEVEL, &level);
}

int _rtw_odm_adaptivity_parm_msg(_adapter *adapter, char *buf, int len)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &pHalData->odmpriv;

	return snprintf(buf, len,
		"%10s %16s %8s %10s %11s %14s\n"
		"0x%-8x %-16d 0x%-6x %-10d %-11u %-14u\n",
		"TH_L2H_ini", "TH_EDCCA_HL_diff", "IGI_Base", "ForceEDCCA", "AdapEn_RSSI", "IGI_LowerBound",
		(u8)odm->TH_L2H_ini,
		odm->TH_EDCCA_HL_diff,
		odm->IGI_Base,
		odm->ForceEDCCA,
		odm->AdapEn_RSSI,
		odm->IGI_LowerBound
	);
}

void rtw_odm_adaptivity_parm_msg(_adapter *adapter)
{
	char buf[256] = {0};

	_rtw_odm_dbg_comp_msg(adapter, buf, 256);
	DBG_871X_LEVEL(_drv_always_, "\n%s", buf);
}

void rtw_odm_adaptivity_parm_set(_adapter *adapter, s8 TH_L2H_ini, s8 TH_EDCCA_HL_diff,
	s8 IGI_Base, bool ForceEDCCA, u8 AdapEn_RSSI, u8 IGI_LowerBound)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &pHalData->odmpriv;

	odm->TH_L2H_ini = TH_L2H_ini;
	odm->TH_EDCCA_HL_diff = TH_EDCCA_HL_diff;
	odm->IGI_Base = IGI_Base;
	odm->ForceEDCCA = ForceEDCCA;
	odm->AdapEn_RSSI = AdapEn_RSSI;
	odm->IGI_LowerBound = IGI_LowerBound;
}

