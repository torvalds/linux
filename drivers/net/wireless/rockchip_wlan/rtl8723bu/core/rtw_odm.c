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
#include <hal_data.h>

const char *odm_comp_str[] = {
	/* BIT0 */"ODM_COMP_DIG",
	/* BIT1 */"ODM_COMP_RA_MASK",
	/* BIT2 */"ODM_COMP_DYNAMIC_TXPWR",
	/* BIT3 */"ODM_COMP_FA_CNT",
	/* BIT4 */"ODM_COMP_RSSI_MONITOR",
	/* BIT5 */"ODM_COMP_CCK_PD",
	/* BIT6 */"ODM_COMP_ANT_DIV",
	/* BIT7 */"ODM_COMP_PWR_SAVE",
	/* BIT8 */"ODM_COMP_PWR_TRAIN",
	/* BIT9 */"ODM_COMP_RATE_ADAPTIVE",
	/* BIT10 */"ODM_COMP_PATH_DIV",
	/* BIT11 */"ODM_COMP_PSD",
	/* BIT12 */"ODM_COMP_DYNAMIC_PRICCA",
	/* BIT13 */"ODM_COMP_RXHP",
	/* BIT14 */"ODM_COMP_MP",
	/* BIT15 */"ODM_COMP_CFO_TRACKING",
	/* BIT16 */"ODM_COMP_ACS",
	/* BIT17 */"PHYDM_COMP_ADAPTIVITY",
	/* BIT18 */"PHYDM_COMP_RA_DBG",
	/* BIT19 */"PHYDM_COMP_TXBF",
	/* BIT20 */"ODM_COMP_EDCA_TURBO",
	/* BIT21 */"ODM_COMP_EARLY_MODE",
	/* BIT22 */"ODM_FW_DEBUG_TRACE",
	/* BIT23 */NULL,
	/* BIT24 */"ODM_COMP_TX_PWR_TRACK",
	/* BIT25 */"ODM_COMP_RX_GAIN_TRACK",
	/* BIT26 */"ODM_COMP_CALIBRATION",
	/* BIT27 */NULL,
	/* BIT28 */NULL,
	/* BIT29 */"BEAMFORMING_DEBUG",
	/* BIT30 */"ODM_COMP_COMMON",
	/* BIT31 */"ODM_COMP_INIT",
};

#define RTW_ODM_COMP_MAX 32

const char *odm_ability_str[] = {
	/* BIT0 */"ODM_BB_DIG",
	/* BIT1 */"ODM_BB_RA_MASK",
	/* BIT2 */"ODM_BB_DYNAMIC_TXPWR",
	/* BIT3 */"ODM_BB_FA_CNT",
	/* BIT4 */"ODM_BB_RSSI_MONITOR",
	/* BIT5 */"ODM_BB_CCK_PD",
	/* BIT6 */"ODM_BB_ANT_DIV",
	/* BIT7 */"ODM_BB_PWR_SAVE",
	/* BIT8 */"ODM_BB_PWR_TRAIN",
	/* BIT9 */"ODM_BB_RATE_ADAPTIVE",
	/* BIT10 */"ODM_BB_PATH_DIV",
	/* BIT11 */"ODM_BB_PSD",
	/* BIT12 */"ODM_BB_RXHP",
	/* BIT13 */"ODM_BB_ADAPTIVITY",
	/* BIT14 */"ODM_BB_CFO_TRACKING",
	/* BIT15 */"ODM_BB_NHM_CNT",
	/* BIT16 */"ODM_BB_PRIMARY_CCA",
	/* BIT17 */"ODM_BB_TXBF",
	/* BIT18 */NULL,
	/* BIT19 */NULL,
	/* BIT20 */"ODM_MAC_EDCA_TURBO",
	/* BIT21 */"ODM_MAC_EARLY_MODE",
	/* BIT22 */NULL,
	/* BIT23 */NULL,
	/* BIT24 */"ODM_RF_TX_PWR_TRACK",
	/* BIT25 */"ODM_RF_RX_GAIN_TRACK",
	/* BIT26 */"ODM_RF_CALIBRATION",
};

#define RTW_ODM_ABILITY_MAX 27

const char *odm_dbg_level_str[] = {
	NULL,
	"ODM_DBG_OFF",
	"ODM_DBG_SERIOUS",
	"ODM_DBG_WARNING",
	"ODM_DBG_LOUD",
	"ODM_DBG_TRACE",
};

#define RTW_ODM_DBG_LEVEL_NUM 6

void rtw_odm_dbg_comp_msg(void *sel, _adapter *adapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &pHalData->odmpriv;
	int cnt = 0;
	u64 dbg_comp = 0;
	int i;

	rtw_hal_get_def_var(adapter, HW_DEF_ODM_DBG_FLAG, &dbg_comp);
	DBG_871X_SEL_NL(sel, "odm.DebugComponents = 0x%016llx \n", dbg_comp);
	for (i=0;i<RTW_ODM_COMP_MAX;i++) {
		if (odm_comp_str[i])
			DBG_871X_SEL_NL(sel, "%cBIT%-2d %s\n",
				(BIT0 << i) & dbg_comp ? '+' : ' ', i, odm_comp_str[i]);
	}
}

inline void rtw_odm_dbg_comp_set(_adapter *adapter, u64 comps)
{
	rtw_hal_set_def_var(adapter, HW_DEF_ODM_DBG_FLAG, &comps);
}

void rtw_odm_dbg_level_msg(void *sel, _adapter *adapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &pHalData->odmpriv;
	int cnt = 0;
	u32 dbg_level = 0;
	int i;

	rtw_hal_get_def_var(adapter, HW_DEF_ODM_DBG_LEVEL, &dbg_level);
	DBG_871X_SEL_NL(sel, "odm.DebugLevel = %u\n", dbg_level);
	for (i=0;i<RTW_ODM_DBG_LEVEL_NUM;i++) {
		if (odm_dbg_level_str[i])
			DBG_871X_SEL_NL(sel, "%u %s\n", i, odm_dbg_level_str[i]);
	}
}

inline void rtw_odm_dbg_level_set(_adapter *adapter, u32 level)
{
	rtw_hal_set_def_var(adapter, HW_DEF_ODM_DBG_LEVEL, &level);
}

void rtw_odm_ability_msg(void *sel, _adapter *adapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &pHalData->odmpriv;
	int cnt = 0;
	u32 ability = 0;
	int i;

	rtw_hal_get_hwreg(adapter, HW_VAR_DM_FLAG, (u8*)&ability);
	DBG_871X_SEL_NL(sel, "odm.SupportAbility = 0x%08x\n", ability);
	for (i=0;i<RTW_ODM_ABILITY_MAX;i++) {
		if (odm_ability_str[i])
			DBG_871X_SEL_NL(sel, "%cBIT%-2d %s\n",
				(BIT0 << i) & ability ? '+' : ' ', i, odm_ability_str[i]);
	}
}

inline void rtw_odm_ability_set(_adapter *adapter, u32 ability)
{
	rtw_hal_set_hwreg(adapter, HW_VAR_DM_FLAG, (u8*)&ability);
}

void rtw_odm_adaptivity_ver_msg(void *sel, _adapter *adapter)
{
	DBG_871X_SEL_NL(sel, "ADAPTIVITY_VERSION "ADAPTIVITY_VERSION"\n");
}

#define RTW_ADAPTIVITY_EN_DISABLE 0
#define RTW_ADAPTIVITY_EN_ENABLE 1

void rtw_odm_adaptivity_en_msg(void *sel, _adapter *adapter)
{
	struct registry_priv *regsty = &adapter->registrypriv;
	struct mlme_priv *mlme = &adapter->mlmepriv;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &hal_data->odmpriv;

	DBG_871X_SEL_NL(sel, "RTW_ADAPTIVITY_EN_");

	if (regsty->adaptivity_en == RTW_ADAPTIVITY_EN_DISABLE) {
		DBG_871X_SEL(sel, "DISABLE\n");
	} else if (regsty->adaptivity_en == RTW_ADAPTIVITY_EN_ENABLE) {
		DBG_871X_SEL(sel, "ENABLE\n");
	} else {
		DBG_871X_SEL(sel, "INVALID\n");
	}
}

#define RTW_ADAPTIVITY_MODE_NORMAL 0
#define RTW_ADAPTIVITY_MODE_CARRIER_SENSE 1

void rtw_odm_adaptivity_mode_msg(void *sel, _adapter *adapter)
{
	struct registry_priv *regsty = &adapter->registrypriv;

	DBG_871X_SEL_NL(sel, "RTW_ADAPTIVITY_MODE_");

	if (regsty->adaptivity_mode == RTW_ADAPTIVITY_MODE_NORMAL) {
		DBG_871X_SEL(sel, "NORMAL\n");
	} else if (regsty->adaptivity_mode == RTW_ADAPTIVITY_MODE_CARRIER_SENSE) {
		DBG_871X_SEL(sel, "CARRIER_SENSE\n");
	} else {
		DBG_871X_SEL(sel, "INVALID\n");
	}
}

#define RTW_ADAPTIVITY_DML_DISABLE 0
#define RTW_ADAPTIVITY_DML_ENABLE 1

void rtw_odm_adaptivity_dml_msg(void *sel, _adapter *adapter)
{
	struct registry_priv *regsty = &adapter->registrypriv;

	DBG_871X_SEL_NL(sel, "RTW_ADAPTIVITY_DML_");

	if (regsty->adaptivity_dml == RTW_ADAPTIVITY_DML_DISABLE) {
		DBG_871X_SEL(sel, "DISABLE\n");
	} else if (regsty->adaptivity_dml == RTW_ADAPTIVITY_DML_ENABLE) {
		DBG_871X_SEL(sel, "ENABLE\n");
	} else {
		DBG_871X_SEL(sel, "INVALID\n");
	}
}

void rtw_odm_adaptivity_dc_backoff_msg(void *sel, _adapter *adapter)
{
	struct registry_priv *regsty = &adapter->registrypriv;

	DBG_871X_SEL_NL(sel, "RTW_ADAPTIVITY_DC_BACKOFF:%u\n", regsty->adaptivity_dc_backoff);
}

bool rtw_odm_adaptivity_needed(_adapter *adapter)
{
	struct registry_priv *regsty = &adapter->registrypriv;
	struct mlme_priv *mlme = &adapter->mlmepriv;
	bool ret = _FALSE;

	if (regsty->adaptivity_en == RTW_ADAPTIVITY_EN_ENABLE)
		ret = _TRUE;

	if (ret == _TRUE) {
		rtw_odm_adaptivity_ver_msg(RTW_DBGDUMP, adapter);
		rtw_odm_adaptivity_en_msg(RTW_DBGDUMP, adapter);
		rtw_odm_adaptivity_mode_msg(RTW_DBGDUMP, adapter);
		rtw_odm_adaptivity_dml_msg(RTW_DBGDUMP, adapter);
		rtw_odm_adaptivity_dc_backoff_msg(RTW_DBGDUMP, adapter);
	}

	return ret;
}

void rtw_odm_adaptivity_parm_msg(void *sel, _adapter *adapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &pHalData->odmpriv;

	rtw_odm_adaptivity_ver_msg(sel, adapter);
	rtw_odm_adaptivity_en_msg(sel, adapter);
	rtw_odm_adaptivity_mode_msg(sel, adapter);
	rtw_odm_adaptivity_dml_msg(sel, adapter);
	rtw_odm_adaptivity_dc_backoff_msg(sel, adapter);

	DBG_871X_SEL_NL(sel, "%10s %16s\n"
		, "TH_L2H_ini", "TH_EDCCA_HL_diff");
	DBG_871X_SEL_NL(sel, "0x%-8x %-16d\n"
		, (u8)odm->TH_L2H_ini
		, odm->TH_EDCCA_HL_diff
	);

	DBG_871X_SEL_NL(sel, "%15s %9s\n", "AdapEnableState","Adap_Flag");
	DBG_871X_SEL_NL(sel, "%-15x %-9x \n"
		, odm->Adaptivity_enable
		, odm->adaptivity_flag
	);
	
	
}

void rtw_odm_adaptivity_parm_set(_adapter *adapter, s8 TH_L2H_ini, s8 TH_EDCCA_HL_diff)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &pHalData->odmpriv;

	odm->TH_L2H_ini = TH_L2H_ini;
	odm->TH_EDCCA_HL_diff = TH_EDCCA_HL_diff;
}

void rtw_odm_get_perpkt_rssi(void *sel, _adapter *adapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &(hal_data->odmpriv);	
	
	DBG_871X_SEL_NL(sel,"RxRate = %s, RSSI_A = %d(%%), RSSI_B = %d(%%)\n", 
	HDATA_RATE(odm->RxRate), odm->RSSI_A, odm->RSSI_B);	
}


void rtw_odm_acquirespinlock(_adapter *adapter,	RT_SPINLOCK_TYPE type)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(adapter);
	_irqL irqL;

	switch(type)
	{
		case RT_IQK_SPINLOCK:
			_enter_critical_bh(&pHalData->IQKSpinLock, &irqL);
		default:
			break;
	}
}

void rtw_odm_releasespinlock(_adapter *adapter,	RT_SPINLOCK_TYPE type)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(adapter);
	_irqL irqL;

	switch(type)
	{
		case RT_IQK_SPINLOCK:
			_exit_critical_bh(&pHalData->IQKSpinLock, &irqL);
		default:
			break;
	}
}

