/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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

/* ************************************************************
 * include files
 * ************************************************************ */
#include "mp_precomp.h"
#include "phydm_precomp.h"

void
odm_dynamic_tx_power_init(
	void					*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER	*adapter = p_dm_odm->adapter;
	PMGNT_INFO			p_mgnt_info = &adapter->MgntInfo;
	HAL_DATA_TYPE		*p_hal_data = GET_HAL_DATA(adapter);

	/*if (!IS_HARDWARE_TYPE_8814A(adapter)) {*/
	/*	ODM_RT_TRACE(p_dm_odm,ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, */
	/*	("odm_dynamic_tx_power_init DynamicTxPowerEnable=%d\n", p_mgnt_info->is_dynamic_tx_power_enable));*/
	/*	return;*/
	/*} else*/
	{
		p_mgnt_info->bDynamicTxPowerEnable = true;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD,
			("odm_dynamic_tx_power_init DynamicTxPowerEnable=%d\n", p_mgnt_info->bDynamicTxPowerEnable));
	}

#if DEV_BUS_TYPE == RT_USB_INTERFACE
	if (RT_GetInterfaceSelection(adapter) == INTF_SEL1_USB_High_Power) {
		odm_dynamic_tx_power_save_power_index(p_dm_odm);
		p_mgnt_info->bDynamicTxPowerEnable = true;
	} else
#else
	/* so 92c pci do not need dynamic tx power? vivi check it later */
	p_mgnt_info->bDynamicTxPowerEnable = false;
#endif


		p_hal_data->LastDTPLvl = tx_high_pwr_level_normal;
	p_hal_data->DynamicTxHighPowerLvl = tx_high_pwr_level_normal;

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)

	p_dm_odm->last_dtp_lvl = tx_high_pwr_level_normal;
	p_dm_odm->dynamic_tx_high_power_lvl = tx_high_pwr_level_normal;
	p_dm_odm->tx_agc_ofdm_18_6 = odm_get_bb_reg(p_dm_odm, 0xC24, MASKDWORD); /*TXAGC {18M 12M 9M 6M}*/

#endif

}

void
odm_dynamic_tx_power_save_power_index(
	void					*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
#if (DM_ODM_SUPPORT_TYPE & (ODM_CE | ODM_WIN))
	u8		index;
	u32		power_index_reg[6] = {0xc90, 0xc91, 0xc92, 0xc98, 0xc99, 0xc9a};

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER	*adapter = p_dm_odm->adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
	for (index = 0; index < 6; index++)
		p_hal_data->PowerIndex_backup[index] = PlatformEFIORead1Byte(adapter, power_index_reg[index]);


#endif
#endif
}

void
odm_dynamic_tx_power_restore_power_index(
	void					*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
#if (DM_ODM_SUPPORT_TYPE & (ODM_CE | ODM_WIN))
	u8			index;
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
	u32			power_index_reg[6] = {0xc90, 0xc91, 0xc92, 0xc98, 0xc99, 0xc9a};
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	for (index = 0; index < 6; index++)
		PlatformEFIOWrite1Byte(adapter, power_index_reg[index], p_hal_data->PowerIndex_backup[index]);


#endif
#endif
}

void
odm_dynamic_tx_power_write_power_index(
	void					*p_dm_void,
	u8		value)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			index;
	u32			power_index_reg[6] = {0xc90, 0xc91, 0xc92, 0xc98, 0xc99, 0xc9a};

	for (index = 0; index < 6; index++)
		/* platform_efio_write_1byte(adapter, power_index_reg[index], value); */
		odm_write_1byte(p_dm_odm, power_index_reg[index], value);

}

void
odm_dynamic_tx_power_nic_ce(
	void					*p_dm_void
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_CE))
#if (RTL8821A_SUPPORT == 1)
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			val;
	u8			rssi_tmp = p_dm_odm->rssi_min;

	if (!(p_dm_odm->support_ability & ODM_BB_DYNAMIC_TXPWR))
		return;

	if (rssi_tmp >= TX_POWER_NEAR_FIELD_THRESH_LVL2) {
		p_dm_odm->dynamic_tx_high_power_lvl = tx_high_pwr_level_level2;
		/**/
	} else if (rssi_tmp >= TX_POWER_NEAR_FIELD_THRESH_LVL1) {
		p_dm_odm->dynamic_tx_high_power_lvl = tx_high_pwr_level_level1;
		/**/
	} else if (rssi_tmp < (TX_POWER_NEAR_FIELD_THRESH_LVL1 - 5)) {
		p_dm_odm->dynamic_tx_high_power_lvl = tx_high_pwr_level_normal;
		/**/
	}

	if (p_dm_odm->last_dtp_lvl != p_dm_odm->dynamic_tx_high_power_lvl) {

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_TXPWR, ODM_DBG_LOUD, ("update_DTP_lv: ((%d)) -> ((%d))\n", p_dm_odm->last_dtp_lvl, p_dm_odm->dynamic_tx_high_power_lvl));

		p_dm_odm->last_dtp_lvl = p_dm_odm->dynamic_tx_high_power_lvl;

		if (p_dm_odm->support_ic_type & (ODM_RTL8821)) {

			if (p_dm_odm->dynamic_tx_high_power_lvl == tx_high_pwr_level_level2) {

				odm_set_mac_reg(p_dm_odm, 0x6D8, BIT(20) | BIT19 | BIT18, 1); /* Resp TXAGC offset = -3dB*/

				val = p_dm_odm->tx_agc_ofdm_18_6 & 0xff;
				if (val >= 0x20)
					val -= 0x16;

				odm_set_bb_reg(p_dm_odm, 0xC24, 0xff, val);
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_TXPWR, ODM_DBG_LOUD, ("Set TX power: level 2\n"));
			} else if (p_dm_odm->dynamic_tx_high_power_lvl == tx_high_pwr_level_level1) {

				odm_set_mac_reg(p_dm_odm, 0x6D8, BIT(20) | BIT19 | BIT18, 1); /* Resp TXAGC offset = -3dB*/

				val = p_dm_odm->tx_agc_ofdm_18_6 & 0xff;
				if (val >= 0x20)
					val -= 0x10;

				odm_set_bb_reg(p_dm_odm, 0xC24, 0xff, val);
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_TXPWR, ODM_DBG_LOUD, ("Set TX power: level 1\n"));
			} else if (p_dm_odm->dynamic_tx_high_power_lvl == tx_high_pwr_level_normal) {

				odm_set_mac_reg(p_dm_odm, 0x6D8, BIT(20) | BIT19 | BIT18, 0); /* Resp TXAGC offset = 0dB*/
				odm_set_bb_reg(p_dm_odm, 0xC24, MASKDWORD, p_dm_odm->tx_agc_ofdm_18_6);
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_TXPWR, ODM_DBG_LOUD, ("Set TX power: normal\n"));
			}
		}
	}

#endif
#endif
}


void
odm_dynamic_tx_power(
	void					*p_dm_void
)
{
	/*  */
	/* For AP/ADSL use struct rtl8192cd_priv* */
	/* For CE/NIC use struct _ADAPTER* */
	/*  */
	/* struct _ADAPTER*		p_adapter = p_dm_odm->adapter;
	*	struct rtl8192cd_priv*	priv		= p_dm_odm->priv; */
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	if (!(p_dm_odm->support_ability & ODM_BB_DYNAMIC_TXPWR))
		return;
	/*  */
	/* 2011/09/29 MH In HW integration first stage, we provide 4 different handle to operate */
	/* at the same time. In the stage2/3, we need to prive universal interface and merge all */
	/* HW dynamic mechanism. */
	/*  */
	switch	(p_dm_odm->support_platform) {
	case	ODM_WIN:
		odm_dynamic_tx_power_nic(p_dm_odm);
		break;
	case	ODM_CE:
		odm_dynamic_tx_power_nic_ce(p_dm_odm);
		break;
	case	ODM_AP:
		odm_dynamic_tx_power_ap(p_dm_odm);
		break;
	default:
		break;
	}


}


void
odm_dynamic_tx_power_nic(
	void					*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (!(p_dm_odm->support_ability & ODM_BB_DYNAMIC_TXPWR))
		return;

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)

	if (p_dm_odm->support_ic_type == ODM_RTL8814A)
		odm_dynamic_tx_power_8814a(p_dm_odm);
	else if (p_dm_odm->support_ic_type & ODM_RTL8821) {
		struct _ADAPTER		*adapter	 =  p_dm_odm->adapter;
		PMGNT_INFO		p_mgnt_info = GetDefaultMgntInfo(adapter);

		if (p_mgnt_info->RegRspPwr == 1)	{
			if (p_dm_odm->rssi_min > 60)
				odm_set_mac_reg(p_dm_odm, ODM_REG_RESP_TX_11AC, BIT(20) | BIT19 | BIT18, 1); /*Resp TXAGC offset = -3dB*/
			else if (p_dm_odm->rssi_min < 55)
				odm_set_mac_reg(p_dm_odm, ODM_REG_RESP_TX_11AC, BIT(20) | BIT19 | BIT18, 0); /*Resp TXAGC offset = 0dB*/
		}
	}
#endif
}

void
odm_dynamic_tx_power_ap(
	void					*p_dm_void

)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)

	/* #if ((RTL8192C_SUPPORT==1) || (RTL8192D_SUPPORT==1) || (RTL8188E_SUPPORT==1) || (RTL8812E_SUPPORT==1)) */


	struct rtl8192cd_priv	*priv		= p_dm_odm->priv;
	s32 i;
	s16 pwr_thd = 63;

	if (!priv->pshare->rf_ft_var.tx_pwr_ctrl)
		return;

#if ((RTL8812A_SUPPORT == 1) || (RTL8881A_SUPPORT == 1) || (RTL8814A_SUPPORT == 1))
	if (p_dm_odm->support_ic_type & (ODM_RTL8812 | ODM_RTL8881A | ODM_RTL8814A))
		pwr_thd = TX_POWER_NEAR_FIELD_THRESH_LVL1;
#endif

	/*
	 *	Check if station is near by to use lower tx power
	 */

	if ((priv->up_time % 3) == 0)  {
		int disable_pwr_ctrl = ((p_dm_odm->false_alm_cnt.cnt_all > 1000) || ((p_dm_odm->false_alm_cnt.cnt_all > 300) && ((RTL_R8(0xc50) & 0x7f) >= 0x32))) ? 1 : 0;

		for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
			struct sta_info *pstat = p_dm_odm->p_odm_sta_info[i];
			if (IS_STA_VALID(pstat)) {
				if (disable_pwr_ctrl)
					pstat->hp_level = 0;
				else if ((pstat->hp_level == 0) && (pstat->rssi > pwr_thd))
					pstat->hp_level = 1;
				else if ((pstat->hp_level == 1) && (pstat->rssi < (pwr_thd - 8)))
					pstat->hp_level = 0;
			}
		}

#if defined(CONFIG_WLAN_HAL_8192EE)
		if (GET_CHIP_VER(priv) == VERSION_8192E) {
			if (!disable_pwr_ctrl && (p_dm_odm->rssi_min != 0xff)) {
				if (p_dm_odm->rssi_min > pwr_thd)
					RRSR_power_control_11n(priv,  1);
				else if (p_dm_odm->rssi_min < (pwr_thd - 8))
					RRSR_power_control_11n(priv,  0);
			} else
				RRSR_power_control_11n(priv,  0);
		}
#endif

#ifdef CONFIG_WLAN_HAL_8814AE
		if (GET_CHIP_VER(priv) == VERSION_8814A) {
			if (!disable_pwr_ctrl && (p_dm_odm->rssi_min != 0xff)) {
				if (p_dm_odm->rssi_min > pwr_thd)
					RRSR_power_control_14(priv,  1);
				else if (p_dm_odm->rssi_min < (pwr_thd - 8))
					RRSR_power_control_14(priv,  0);
			} else
				RRSR_power_control_14(priv,  0);
		}
#endif

	}
	/* #endif */

#endif
}

void
odm_dynamic_tx_power_8821(
	void			*p_dm_void,
	u8			*p_desc,
	u8			mac_id
)
{
#if (RTL8821A_SUPPORT == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct sta_info		*p_entry;
	u8			reg0xc56_byte;
	u8			txpwr_offset = 0;

	p_entry = p_dm_odm->p_odm_sta_info[mac_id];

	reg0xc56_byte = odm_read_1byte(p_dm_odm, 0xc56);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("reg0xc56_byte=%d\n", reg0xc56_byte));

	if (p_entry[mac_id].rssi_stat.undecorated_smoothed_pwdb > 85) {

		/* Avoid TXAGC error after TX power offset is applied.
		For example: Reg0xc56=0x6, if txpwr_offset=3( reduce 11dB )
		Total power = 6-11= -5( overflow!! ), PA may be burned !
		so txpwr_offset should be adjusted by Reg0xc56*/

		if (reg0xc56_byte < 7)
			txpwr_offset = 1;
		else if (reg0xc56_byte < 11)
			txpwr_offset = 2;
		else
			txpwr_offset = 3;

		SET_TX_DESC_TX_POWER_OFFSET_8812(p_desc, txpwr_offset);
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("odm_dynamic_tx_power_8821: RSSI=%d, txpwr_offset=%d\n", p_entry[mac_id].rssi_stat.undecorated_smoothed_pwdb, txpwr_offset));

	} else {
		SET_TX_DESC_TX_POWER_OFFSET_8812(p_desc, txpwr_offset);
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("odm_dynamic_tx_power_8821: RSSI=%d, txpwr_offset=%d\n", p_entry[mac_id].rssi_stat.undecorated_smoothed_pwdb, txpwr_offset));

	}
#endif	/*#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)*/
#endif	/*#if (RTL8821A_SUPPORT==1)*/
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void
odm_dynamic_tx_power_8814a(
	void					*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER *adapter = p_dm_odm->adapter;
	PMGNT_INFO			p_mgnt_info = &adapter->MgntInfo;
	HAL_DATA_TYPE		*p_hal_data = GET_HAL_DATA(adapter);
	s32				undecorated_smoothed_pwdb;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD,
		("TxLevel=%d p_mgnt_info->iot_action=%x p_mgnt_info->is_dynamic_tx_power_enable=%d\n",
		p_hal_data->DynamicTxHighPowerLvl, p_mgnt_info->IOTAction, p_mgnt_info->bDynamicTxPowerEnable));

	/*STA not connected and AP not connected*/
	if ((!p_mgnt_info->bMediaConnect) && (p_hal_data->EntryMinUndecoratedSmoothedPWDB == 0)) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("Not connected to any reset power lvl\n"));
		p_hal_data->DynamicTxHighPowerLvl = tx_high_pwr_level_normal;
		return;
	}


	if ((p_mgnt_info->bDynamicTxPowerEnable != true) || p_mgnt_info->IOTAction & HT_IOT_ACT_DISABLE_HIGH_POWER)
		p_hal_data->DynamicTxHighPowerLvl = tx_high_pwr_level_normal;
	else {
		if (p_mgnt_info->bMediaConnect) {	/*Default port*/
			if (ACTING_AS_AP(adapter) || ACTING_AS_IBSS(adapter)) {
				undecorated_smoothed_pwdb = p_hal_data->EntryMinUndecoratedSmoothedPWDB;
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("AP Client PWDB = 0x%x\n", undecorated_smoothed_pwdb));
			} else {
				undecorated_smoothed_pwdb = p_hal_data->UndecoratedSmoothedPWDB;
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("STA Default Port PWDB = 0x%x\n", undecorated_smoothed_pwdb));
			}
		} else {/*associated entry pwdb*/
			undecorated_smoothed_pwdb = p_hal_data->EntryMinUndecoratedSmoothedPWDB;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("AP Ext Port PWDB = 0x%x\n", undecorated_smoothed_pwdb));
		}

		/*Should we separate as 2.4G/5G band?*/

		if (undecorated_smoothed_pwdb >= TX_POWER_NEAR_FIELD_THRESH_LVL2) {
			p_hal_data->DynamicTxHighPowerLvl = tx_high_pwr_level_level2;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("tx_high_pwr_level_level1 (TxPwr=0x0)\n"));
		} else if ((undecorated_smoothed_pwdb < (TX_POWER_NEAR_FIELD_THRESH_LVL2 - 3)) &&
			(undecorated_smoothed_pwdb >= TX_POWER_NEAR_FIELD_THRESH_LVL1)) {
			p_hal_data->DynamicTxHighPowerLvl = tx_high_pwr_level_level1;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("tx_high_pwr_level_level1 (TxPwr=0x10)\n"));
		} else if (undecorated_smoothed_pwdb < (TX_POWER_NEAR_FIELD_THRESH_LVL1 - 5)) {
			p_hal_data->DynamicTxHighPowerLvl = tx_high_pwr_level_normal;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("tx_high_pwr_level_normal\n"));
		}
	}


	if (p_hal_data->DynamicTxHighPowerLvl != p_hal_data->LastDTPLvl) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD, ("odm_dynamic_tx_power_8814a() channel = %d\n", p_hal_data->CurrentChannel));
		odm_set_tx_power_level8814(adapter, p_hal_data->CurrentChannel, p_hal_data->DynamicTxHighPowerLvl);
	}


	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_TXPWR, DBG_LOUD,
		("odm_dynamic_tx_power_8814a() channel = %d  TXpower lvl=%d/%d\n",
		p_hal_data->CurrentChannel, p_hal_data->LastDTPLvl, p_hal_data->DynamicTxHighPowerLvl));

	p_hal_data->LastDTPLvl = p_hal_data->DynamicTxHighPowerLvl;

}



/**/
/*For normal driver we always use the FW method to configure TX power index to reduce I/O transaction.*/
/**/
/**/
void
odm_set_tx_power_level8814(
	struct _ADAPTER		*adapter,
	u8			channel,
	u8			pwr_lvl
)
{
#if (DEV_BUS_TYPE == RT_USB_INTERFACE)
	u32			i, j, k = 0;
	u32			value[264] = {0};
	u32			path = 0, power_index, txagc_table_wd = 0x00801000;

	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);

	u8	jaguar2_rates[][4] = { {MGN_1M, MGN_2M, MGN_5_5M, MGN_11M},
		{MGN_6M, MGN_9M, MGN_12M, MGN_18M},
		{MGN_24M, MGN_36M, MGN_48M, MGN_54M},
		{MGN_MCS0, MGN_MCS1, MGN_MCS2, MGN_MCS3},
		{MGN_MCS4, MGN_MCS5, MGN_MCS6, MGN_MCS7},
		{MGN_MCS8, MGN_MCS9, MGN_MCS10, MGN_MCS11},
		{MGN_MCS12, MGN_MCS13, MGN_MCS14, MGN_MCS15},
		{MGN_MCS16, MGN_MCS17, MGN_MCS18, MGN_MCS19},
		{MGN_MCS20, MGN_MCS21, MGN_MCS22, MGN_MCS23},
		{MGN_VHT1SS_MCS0, MGN_VHT1SS_MCS1, MGN_VHT1SS_MCS2, MGN_VHT1SS_MCS3},
		{MGN_VHT1SS_MCS4, MGN_VHT1SS_MCS5, MGN_VHT1SS_MCS6, MGN_VHT1SS_MCS7},
		{MGN_VHT2SS_MCS8, MGN_VHT2SS_MCS9, MGN_VHT2SS_MCS0, MGN_VHT2SS_MCS1},
		{MGN_VHT2SS_MCS2, MGN_VHT2SS_MCS3, MGN_VHT2SS_MCS4, MGN_VHT2SS_MCS5},
		{MGN_VHT2SS_MCS6, MGN_VHT2SS_MCS7, MGN_VHT2SS_MCS8, MGN_VHT2SS_MCS9},
		{MGN_VHT3SS_MCS0, MGN_VHT3SS_MCS1, MGN_VHT3SS_MCS2, MGN_VHT3SS_MCS3},
		{MGN_VHT3SS_MCS4, MGN_VHT3SS_MCS5, MGN_VHT3SS_MCS6, MGN_VHT3SS_MCS7},
		{MGN_VHT3SS_MCS8, MGN_VHT3SS_MCS9, 0, 0}
	};

	for (path = ODM_RF_PATH_A; path <= ODM_RF_PATH_D; ++path) {

		u8	usb_host = UsbModeQueryHubUsbType(adapter);
		u8	usb_rfset = UsbModeQueryRfSet(adapter);
		u8	usb_rf_type = RT_GetRFType(adapter);

		for (i = 0; i <= 16; i++) {
			for (j = 0; j <= 3; j++) {
				if (jaguar2_rates[i][j] == 0)
					continue;

				txagc_table_wd =  0x00801000;
				power_index = (u32) PHY_GetTxPowerIndex(adapter, (u8)path, jaguar2_rates[i][j], p_hal_data->CurrentChannelBW, channel);

				/*for Query bus type to recude tx power.*/
				if (usb_host != USB_MODE_U3 && usb_rfset == 1 && IS_HARDWARE_TYPE_8814AU(adapter) && usb_rf_type == RF_3T3R) {
					if (channel <= 14) {
						if (power_index >= 16)
							power_index -= 16;
						else
							power_index = 0;
					} else
						power_index = 0;
				}

				if (pwr_lvl == tx_high_pwr_level_level1) {
					if (power_index >= 0x10)
						power_index -= 0x10;
					else
						power_index = 0;
				} else if (pwr_lvl == tx_high_pwr_level_level2)
					power_index = 0;

				txagc_table_wd |= (path << 8) | MRateToHwRate(jaguar2_rates[i][j]) | (power_index << 24);

				PHY_SetTxPowerIndexShadow(adapter, (u8)power_index, (u8)path, jaguar2_rates[i][j]);

				value[k++] = txagc_table_wd;
			}
		}
	}

	if (adapter->MgntInfo.bScanInProgress == false &&  adapter->MgntInfo.RegFWOffload == 2)
		HalDownloadTxPowerLevel8814(adapter, value);
#endif
}
#endif
