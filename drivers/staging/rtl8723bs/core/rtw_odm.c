// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2013 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include <drv_types.h>
#include <rtw_debug.h>
#include <rtw_odm.h>
#include <hal_data.h>

#define RTW_ODM_COMP_MAX 32

static const char * const odm_ability_str[] = {
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
	/* BIT14 */"ODM_BB_DYNAMIC_ATC",
	/* BIT15 */NULL,
	/* BIT16 */"ODM_MAC_EDCA_TURBO",
	/* BIT17 */"ODM_MAC_EARLY_MODE",
	/* BIT18 */NULL,
	/* BIT19 */NULL,
	/* BIT20 */NULL,
	/* BIT21 */NULL,
	/* BIT22 */NULL,
	/* BIT23 */NULL,
	/* BIT24 */"ODM_RF_TX_PWR_TRACK",
	/* BIT25 */"ODM_RF_RX_GAIN_TRACK",
	/* BIT26 */"ODM_RF_CALIBRATION",
};

#define RTW_ODM_ABILITY_MAX 27

static const char * const odm_dbg_level_str[] = {
	NULL,
	"ODM_DBG_OFF",
	"ODM_DBG_SERIOUS",
	"ODM_DBG_WARNING",
	"ODM_DBG_LOUD",
	"ODM_DBG_TRACE",
};

#define RTW_ODM_DBG_LEVEL_NUM 6

void rtw_odm_dbg_level_msg(void *sel, struct adapter *adapter)
{
	u32 dbg_level;
	int i;

	rtw_hal_get_def_var(adapter, HW_DEF_ODM_DBG_LEVEL, &dbg_level);
	netdev_dbg(adapter->pnetdev, "odm.DebugLevel = %u\n", dbg_level);
	for (i = 0; i < RTW_ODM_DBG_LEVEL_NUM; i++) {
		if (odm_dbg_level_str[i])
			netdev_dbg(adapter->pnetdev, "%u %s\n", i,
				   odm_dbg_level_str[i]);
	}
}

inline void rtw_odm_dbg_level_set(struct adapter *adapter, u32 level)
{
	rtw_hal_set_def_var(adapter, HW_DEF_ODM_DBG_LEVEL, &level);
}

void rtw_odm_ability_msg(void *sel, struct adapter *adapter)
{
	u32 ability = 0;
	int i;

	rtw_hal_get_hwreg(adapter, HW_VAR_DM_FLAG, (u8 *)&ability);
	netdev_dbg(adapter->pnetdev, "odm.SupportAbility = 0x%08x\n", ability);
	for (i = 0; i < RTW_ODM_ABILITY_MAX; i++) {
		if (odm_ability_str[i])
			netdev_dbg(adapter->pnetdev, "%cBIT%-2d %s\n",
				   (BIT0 << i) & ability ? '+' : ' ', i,
				   odm_ability_str[i]);
	}
}

void rtw_odm_adaptivity_parm_set(struct adapter *adapter, s8 TH_L2H_ini,
				 s8 TH_EDCCA_HL_diff, s8 IGI_Base,
				 bool ForceEDCCA, u8 AdapEn_RSSI,
				 u8 IGI_LowerBound)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(adapter);
	struct dm_odm_t *odm = &pHalData->odmpriv;

	odm->TH_L2H_ini = TH_L2H_ini;
	odm->TH_EDCCA_HL_diff = TH_EDCCA_HL_diff;
	odm->IGI_Base = IGI_Base;
	odm->ForceEDCCA = ForceEDCCA;
	odm->AdapEn_RSSI = AdapEn_RSSI;
	odm->IGI_LowerBound = IGI_LowerBound;
}

void rtw_odm_get_perpkt_rssi(void *sel, struct adapter *adapter)
{
	struct hal_com_data *hal_data = GET_HAL_DATA(adapter);
	struct dm_odm_t *odm = &hal_data->odmpriv;

	netdev_dbg(adapter->pnetdev,
		   "RxRate = %s, RSSI_A = %d(%%), RSSI_B = %d(%%)\n",
		   HDATA_RATE(odm->RxRate), odm->RSSI_A, odm->RSSI_B);
}
