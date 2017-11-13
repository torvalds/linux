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

#define READ_AND_CONFIG_MP(ic, txt) (odm_read_and_config_mp_##ic##txt(p_dm))
#define READ_AND_CONFIG_TC(ic, txt) (odm_read_and_config_tc_##ic##txt(p_dm))


#if (PHYDM_TESTCHIP_SUPPORT == 1)
#define READ_AND_CONFIG(ic, txt) do {\
		if (p_dm->is_mp_chip)\
			READ_AND_CONFIG_MP(ic, txt);\
		else\
			READ_AND_CONFIG_TC(ic, txt);\
	} while (0)
#else
#define READ_AND_CONFIG     READ_AND_CONFIG_MP
#endif

#define GET_VERSION_MP(ic, txt)		(odm_get_version_mp_##ic##txt())
#define GET_VERSION_TC(ic, txt)		(odm_get_version_tc_##ic##txt())

#if (PHYDM_TESTCHIP_SUPPORT == 1)
	#define GET_VERSION(ic, txt) (p_dm->is_mp_chip ? GET_VERSION_MP(ic, txt) : GET_VERSION_TC(ic, txt))
#else
	#define GET_VERSION(ic, txt) GET_VERSION_MP(ic, txt)
#endif

enum hal_status
odm_config_rf_with_header_file(
	struct PHY_DM_STRUCT		*p_dm,
	enum odm_rf_config_type		config_type,
	u8			e_rf_path
)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	PMGNT_INFO		p_mgnt_info = &(adapter->MgntInfo);
#endif
	enum hal_status	result = HAL_STATUS_SUCCESS;

	PHYDM_DBG(p_dm, ODM_COMP_INIT,
		("===>odm_config_rf_with_header_file (%s)\n", (p_dm->is_mp_chip) ? "MPChip" : "TestChip"));
	PHYDM_DBG(p_dm, ODM_COMP_INIT,
		("support_platform: 0x%X, support_interface: 0x%X, board_type: 0x%X\n",
		p_dm->support_platform, p_dm->support_interface, p_dm->board_type));

	/* 1 AP doesn't use PHYDM power tracking table in these ICs */
#if (DM_ODM_SUPPORT_TYPE != ODM_AP)
#if (RTL8812A_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8812) {
		if (config_type == CONFIG_RF_RADIO) {
			if (e_rf_path == RF_PATH_A)
				READ_AND_CONFIG_MP(8812a, _radioa);
			else if (e_rf_path == RF_PATH_B)
				READ_AND_CONFIG_MP(8812a, _radiob);
		} else if (config_type == CONFIG_RF_TXPWR_LMT) {
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN) && (DEV_BUS_TYPE == RT_PCI_INTERFACE)
			HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
			if ((p_hal_data->EEPROMSVID == 0x17AA && p_hal_data->EEPROMSMID == 0xA811) ||
			    (p_hal_data->EEPROMSVID == 0x10EC && p_hal_data->EEPROMSMID == 0xA812) ||
			    (p_hal_data->EEPROMSVID == 0x10EC && p_hal_data->EEPROMSMID == 0x8812))
				READ_AND_CONFIG_MP(8812a, _txpwr_lmt_hm812a03);
			else
#endif
				READ_AND_CONFIG_MP(8812a, _txpwr_lmt);
		}
	}
#endif
#if (RTL8821A_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8821) {
		if (config_type == CONFIG_RF_RADIO) {
			if (e_rf_path == RF_PATH_A)
				READ_AND_CONFIG_MP(8821a, _radioa);
		} else if (config_type == CONFIG_RF_TXPWR_LMT) {
			if (p_dm->support_interface == ODM_ITRF_USB) {
				if (p_dm->ext_pa_5g || p_dm->ext_lna_5g)
					READ_AND_CONFIG_MP(8821a, _txpwr_lmt_8811a_u_fem);
				else
					READ_AND_CONFIG_MP(8821a, _txpwr_lmt_8811a_u_ipa);
			} else {
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
				if (p_mgnt_info->CustomerID == RT_CID_8821AE_ASUS_MB)
					READ_AND_CONFIG_MP(8821a, _txpwr_lmt_8821a_sar_8mm);
				else if (p_mgnt_info->CustomerID == RT_CID_ASUS_NB)
					READ_AND_CONFIG_MP(8821a, _txpwr_lmt_8821a_sar_5mm);
				else
#endif
					READ_AND_CONFIG_MP(8821a, _txpwr_lmt_8821a);
			}
		}
	}
#endif
#if (RTL8192E_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8192E) {
		if (config_type == CONFIG_RF_RADIO) {
			if (e_rf_path == RF_PATH_A)
				READ_AND_CONFIG_MP(8192e, _radioa);
			else if (e_rf_path == RF_PATH_B)
				READ_AND_CONFIG_MP(8192e, _radiob);
		} else if (config_type == CONFIG_RF_TXPWR_LMT) {
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN) && (DEV_BUS_TYPE == RT_PCI_INTERFACE)	/*Refine by Vincent Lan for 5mm SAR pwr limit*/
			HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);

			if ((p_hal_data->EEPROMSVID == 0x11AD && p_hal_data->EEPROMSMID == 0x8192) ||
			    (p_hal_data->EEPROMSVID == 0x11AD && p_hal_data->EEPROMSMID == 0x8193))
				READ_AND_CONFIG_MP(8192e, _txpwr_lmt_8192e_sar_5mm);
			else
#endif
				READ_AND_CONFIG_MP(8192e, _txpwr_lmt);
		}
	}
#endif
#if (RTL8723D_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8723D) {
		if (config_type == CONFIG_RF_RADIO) {
			if (e_rf_path == RF_PATH_A)
				READ_AND_CONFIG_MP(8723d, _radioa);
		} else if (config_type == CONFIG_RF_TXPWR_LMT)
			READ_AND_CONFIG_MP(8723d, _txpwr_lmt);
	}
#endif
/* JJ ADD 20161014 */
#if (RTL8710B_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8710B) {
		if (config_type == CONFIG_RF_RADIO) {
			if (e_rf_path == RF_PATH_A)
				READ_AND_CONFIG_MP(8710b, _radioa);
		} else if (config_type == CONFIG_RF_TXPWR_LMT)
			READ_AND_CONFIG_MP(8710b, _txpwr_lmt);
	}
#endif

#endif/* (DM_ODM_SUPPORT_TYPE !=  ODM_AP) */

	/* 1 All platforms support */
#if (RTL8188E_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8188E) {
		if (config_type == CONFIG_RF_RADIO) {
			if (e_rf_path == RF_PATH_A)
				READ_AND_CONFIG_MP(8188e, _radioa);
		} else if (config_type == CONFIG_RF_TXPWR_LMT)
			READ_AND_CONFIG_MP(8188e, _txpwr_lmt);
	}
#endif
#if (RTL8723B_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8723B) {
		if (config_type == CONFIG_RF_RADIO)
			READ_AND_CONFIG_MP(8723b, _radioa);
		else if (config_type == CONFIG_RF_TXPWR_LMT)
			READ_AND_CONFIG_MP(8723b, _txpwr_lmt);
	}
#endif
#if (RTL8814A_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8814A) {
		if (config_type == CONFIG_RF_RADIO) {
			if (e_rf_path == RF_PATH_A)
				READ_AND_CONFIG_MP(8814a, _radioa);
			else if (e_rf_path == RF_PATH_B)
				READ_AND_CONFIG_MP(8814a, _radiob);
			else if (e_rf_path == RF_PATH_C)
				READ_AND_CONFIG_MP(8814a, _radioc);
			else if (e_rf_path == RF_PATH_D)
				READ_AND_CONFIG_MP(8814a, _radiod);
		} else if (config_type == CONFIG_RF_TXPWR_LMT) {
			if (p_dm->rfe_type == 0)
				READ_AND_CONFIG_MP(8814a,_txpwr_lmt_type0);
			else if (p_dm->rfe_type == 1)
				READ_AND_CONFIG_MP(8814a,_txpwr_lmt_type1);				
			else if (p_dm->rfe_type == 2)
				READ_AND_CONFIG_MP(8814a,_txpwr_lmt_type2);		
			else if (p_dm->rfe_type == 3)
				READ_AND_CONFIG_MP(8814a,_txpwr_lmt_type3);		
			else if (p_dm->rfe_type == 5)
				READ_AND_CONFIG_MP(8814a,_txpwr_lmt_type5);		
			else if (p_dm->rfe_type == 7)
				READ_AND_CONFIG_MP(8814a,_txpwr_lmt_type7);
			else if (p_dm->rfe_type == 8)
				READ_AND_CONFIG_MP(8814a,_txpwr_lmt_type8);
			else
				READ_AND_CONFIG_MP(8814a,_txpwr_lmt);
		}
	}
#endif
#if (RTL8703B_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8703B) {
		if (config_type == CONFIG_RF_RADIO) {
			if (e_rf_path == RF_PATH_A)
				READ_AND_CONFIG_MP(8703b, _radioa);
		}
	}
#endif
#if (RTL8188F_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8188F) {
		if (config_type == CONFIG_RF_RADIO) {
			if (e_rf_path == RF_PATH_A)
				READ_AND_CONFIG_MP(8188f, _radioa);
		} else if (config_type == CONFIG_RF_TXPWR_LMT)
			READ_AND_CONFIG_MP(8188f, _txpwr_lmt);
	}
#endif
#if (RTL8822B_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8822B) {
		if (config_type == CONFIG_RF_RADIO) {
			if (e_rf_path == RF_PATH_A)
				READ_AND_CONFIG_MP(8822b, _radioa);
			else if (e_rf_path == RF_PATH_B)
				READ_AND_CONFIG_MP(8822b, _radiob);
		} else if (config_type == CONFIG_RF_TXPWR_LMT) {
			if (p_dm->rfe_type == 5)
				READ_AND_CONFIG_MP(8822b, _txpwr_lmt_type5);
			else if (p_dm->rfe_type == 2)
				READ_AND_CONFIG_MP(8822b, _txpwr_lmt_type2);
			else if (p_dm->rfe_type == 3)
				READ_AND_CONFIG_MP(8822b, _txpwr_lmt_type3);
			else if (p_dm->rfe_type == 4)
				READ_AND_CONFIG_MP(8822b, _txpwr_lmt_type4);
			else if (p_dm->rfe_type == 12)
				READ_AND_CONFIG_MP(8822b, _txpwr_lmt_type12);
			else if (p_dm->rfe_type == 15)
				READ_AND_CONFIG_MP(8822b, _txpwr_lmt_type15);
			else if (p_dm->rfe_type == 16)
				READ_AND_CONFIG_MP(8822b, _txpwr_lmt_type16);
			else if (p_dm->rfe_type == 17)
				READ_AND_CONFIG_MP(8822b, _txpwr_lmt_type17);
			else
				READ_AND_CONFIG_MP(8822b, _txpwr_lmt);
		}
	}
#endif

#if (RTL8197F_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8197F) {
		if (config_type == CONFIG_RF_RADIO) {
			if (e_rf_path == RF_PATH_A)
				READ_AND_CONFIG_MP(8197f, _radioa);
			else if (e_rf_path == RF_PATH_B)
				READ_AND_CONFIG_MP(8197f, _radiob);
		}
	}
#endif

#if (RTL8821C_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8821C) {
		if (config_type == CONFIG_RF_RADIO) {
			if (e_rf_path == RF_PATH_A)
				READ_AND_CONFIG(8821c, _radioa);
		} else if (config_type == CONFIG_RF_TXPWR_LMT)
			READ_AND_CONFIG(8821c, _txpwr_lmt);
	}
#endif

	if (config_type == CONFIG_RF_RADIO) {
		if (p_dm->fw_offload_ability & PHYDM_PHY_PARAM_OFFLOAD) {

			result = phydm_set_reg_by_fw(p_dm,
							PHYDM_HALMAC_CMD_END,
							0,
							0,
							0,
							(enum rf_path)0,
							0);
			PHYDM_DBG(p_dm, ODM_COMP_INIT,
				("rf param offload end!result = %d", result));
		}
	}

	return result;
}

enum hal_status
odm_config_rf_with_tx_pwr_track_header_file(
	struct PHY_DM_STRUCT		*p_dm
)
{
	PHYDM_DBG(p_dm, ODM_COMP_INIT,
		("===>odm_config_rf_with_tx_pwr_track_header_file (%s)\n", (p_dm->is_mp_chip) ? "MPChip" : "TestChip"));
	PHYDM_DBG(p_dm, ODM_COMP_INIT,
		("support_platform: 0x%X, support_interface: 0x%X, board_type: 0x%X\n",
		p_dm->support_platform, p_dm->support_interface, p_dm->board_type));


	/* 1 AP doesn't use PHYDM power tracking table in these ICs */
#if (DM_ODM_SUPPORT_TYPE != ODM_AP)
#if RTL8821A_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8821) {
		if (p_dm->support_interface == ODM_ITRF_PCIE)
			READ_AND_CONFIG_MP(8821a, _txpowertrack_pcie);
		else if (p_dm->support_interface == ODM_ITRF_USB)
			READ_AND_CONFIG_MP(8821a, _txpowertrack_usb);
		else if (p_dm->support_interface == ODM_ITRF_SDIO)
			READ_AND_CONFIG_MP(8821a, _txpowertrack_sdio);
	}
#endif
#if RTL8812A_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8812) {
		if (p_dm->support_interface == ODM_ITRF_PCIE)
			READ_AND_CONFIG_MP(8812a, _txpowertrack_pcie);
		else if (p_dm->support_interface == ODM_ITRF_USB) {
			if (p_dm->rfe_type == 3 && p_dm->is_mp_chip)
				READ_AND_CONFIG_MP(8812a, _txpowertrack_rfe3);
			else
				READ_AND_CONFIG_MP(8812a, _txpowertrack_usb);
		}

	}
#endif
#if RTL8192E_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8192E) {
		if (p_dm->support_interface == ODM_ITRF_PCIE)
			READ_AND_CONFIG_MP(8192e, _txpowertrack_pcie);
		else if (p_dm->support_interface == ODM_ITRF_USB)
			READ_AND_CONFIG_MP(8192e, _txpowertrack_usb);
		else if (p_dm->support_interface == ODM_ITRF_SDIO)
			READ_AND_CONFIG_MP(8192e, _txpowertrack_sdio);
	}
#endif
#if RTL8723D_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8723D) {
		if (p_dm->support_interface == ODM_ITRF_PCIE)
			READ_AND_CONFIG_MP(8723d, _txpowertrack_pcie);
		else if (p_dm->support_interface == ODM_ITRF_USB)
			READ_AND_CONFIG_MP(8723d, _txpowertrack_usb);
		else if (p_dm->support_interface == ODM_ITRF_SDIO)
			READ_AND_CONFIG_MP(8723d, _txpowertrack_sdio);

		READ_AND_CONFIG_MP(8723d, _txxtaltrack);
	}
#endif
/* JJ ADD 20161014 */
#if RTL8710B_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8710B) {
		if (p_dm->support_interface == ODM_ITRF_PCIE)
			READ_AND_CONFIG_MP(8710b, _txpowertrack_pcie);
		else if (p_dm->support_interface == ODM_ITRF_USB)
			READ_AND_CONFIG_MP(8710b, _txpowertrack_usb);
		else if (p_dm->support_interface == ODM_ITRF_SDIO)
			READ_AND_CONFIG_MP(8710b, _txpowertrack_sdio);

		READ_AND_CONFIG_MP(8710b, _txxtaltrack);
	}
#endif

#if RTL8188E_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8188E) {
		if (odm_get_mac_reg(p_dm, 0xF0, 0xF000) >= 8) {		/*if 0xF0[15:12] >= 8, SMIC*/
			if (p_dm->support_interface == ODM_ITRF_PCIE)
				READ_AND_CONFIG_MP(8188e, _txpowertrack_pcie_icut);
			else if (p_dm->support_interface == ODM_ITRF_USB)
				READ_AND_CONFIG_MP(8188e, _txpowertrack_usb_icut);
			else if (p_dm->support_interface == ODM_ITRF_SDIO)
				READ_AND_CONFIG_MP(8188e, _txpowertrack_sdio_icut);
		} else {	/*else 0xF0[15:12] < 8, TSMC*/
			if (p_dm->support_interface == ODM_ITRF_PCIE)
				READ_AND_CONFIG_MP(8188e, _txpowertrack_pcie);
			else if (p_dm->support_interface == ODM_ITRF_USB)
				READ_AND_CONFIG_MP(8188e, _txpowertrack_usb);
			else if (p_dm->support_interface == ODM_ITRF_SDIO)
				READ_AND_CONFIG_MP(8188e, _txpowertrack_sdio);
		}

	}
#endif
#endif/* (DM_ODM_SUPPORT_TYPE !=  ODM_AP) */

	/* 1 All platforms support */
#if RTL8723B_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8723B) {
		if (p_dm->support_interface == ODM_ITRF_PCIE)
			READ_AND_CONFIG_MP(8723b, _txpowertrack_pcie);
		else if (p_dm->support_interface == ODM_ITRF_USB)
			READ_AND_CONFIG_MP(8723b, _txpowertrack_usb);
		else if (p_dm->support_interface == ODM_ITRF_SDIO)
			READ_AND_CONFIG_MP(8723b, _txpowertrack_sdio);
	}
#endif
#if RTL8814A_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8814A) {
		if (p_dm->rfe_type == 0)
			READ_AND_CONFIG_MP(8814a, _txpowertrack_type0);
		else if (p_dm->rfe_type == 2)
			READ_AND_CONFIG_MP(8814a, _txpowertrack_type2);
		else if (p_dm->rfe_type == 5)
			READ_AND_CONFIG_MP(8814a, _txpowertrack_type5);
		else if (p_dm->rfe_type == 7)
			READ_AND_CONFIG_MP(8814a, _txpowertrack_type7);
		else if (p_dm->rfe_type == 8)
			READ_AND_CONFIG_MP(8814a, _txpowertrack_type8);
		else
			READ_AND_CONFIG_MP(8814a, _txpowertrack);

		READ_AND_CONFIG_MP(8814a, _txpowertssi);
	}
#endif
#if RTL8703B_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8703B) {
		if (p_dm->support_interface == ODM_ITRF_USB)
			READ_AND_CONFIG_MP(8703b, _txpowertrack_usb);
		else if (p_dm->support_interface == ODM_ITRF_SDIO)
			READ_AND_CONFIG_MP(8703b, _txpowertrack_sdio);

		READ_AND_CONFIG_MP(8703b, _txxtaltrack);
	}
#endif

#if RTL8188F_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8188F) {
		if (p_dm->support_interface == ODM_ITRF_USB)
			READ_AND_CONFIG_MP(8188f, _txpowertrack_usb);
		else if (p_dm->support_interface == ODM_ITRF_SDIO)
			READ_AND_CONFIG_MP(8188f, _txpowertrack_sdio);
	}
#endif

#if RTL8822B_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8822B) {
		if (p_dm->rfe_type == 0)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type0);
		else if (p_dm->rfe_type == 1)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type1);
		else if (p_dm->rfe_type == 2)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type2);
		else if ((p_dm->rfe_type == 3) || (p_dm->rfe_type == 5))
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type3_type5);
		else if (p_dm->rfe_type == 4)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type4);
		else if (p_dm->rfe_type == 6)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type6);
		else if (p_dm->rfe_type == 7)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type7);
		else if (p_dm->rfe_type == 8)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type8);
		else if (p_dm->rfe_type == 9)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type9);
		else if (p_dm->rfe_type == 10)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type10);
		else if (p_dm->rfe_type == 11)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type11);
		else if (p_dm->rfe_type == 12)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type12);
		else if (p_dm->rfe_type == 13)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type13);
		else if (p_dm->rfe_type == 14)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type14);
		else if (p_dm->rfe_type == 15)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type15);
		else if (p_dm->rfe_type == 16)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type16);
		else if (p_dm->rfe_type == 17)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type17);
		else
			READ_AND_CONFIG_MP(8822b, _txpowertrack);
	}
#endif

#if RTL8197F_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8197F) {
		if (p_dm->rfe_type == 0)
			READ_AND_CONFIG_MP(8197f, _txpowertrack_type0);
		else if (p_dm->rfe_type == 1)
			READ_AND_CONFIG_MP(8197f, _txpowertrack_type1);
		else
			READ_AND_CONFIG_MP(8197f, _txpowertrack);
	}
#endif

#if RTL8821C_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8821C) {
		if (p_dm->rfe_type == 0x5)
			READ_AND_CONFIG(8821c, _txpowertrack_type0x28);
		else if (p_dm->rfe_type == 0x4)
			READ_AND_CONFIG(8821c, _txpowertrack_type0x20);
		else
			READ_AND_CONFIG(8821c, _txpowertrack);
	}
#endif

	return HAL_STATUS_SUCCESS;
}

enum hal_status
odm_config_bb_with_header_file(
	struct PHY_DM_STRUCT		*p_dm,
	enum odm_bb_config_type		config_type
)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	PMGNT_INFO		p_mgnt_info = &(adapter->MgntInfo);
#endif
	enum hal_status	result = HAL_STATUS_SUCCESS;

	/* 1 AP doesn't use PHYDM initialization in these ICs */
#if (DM_ODM_SUPPORT_TYPE != ODM_AP)
#if (RTL8812A_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8812) {
		if (config_type == CONFIG_BB_PHY_REG)
			READ_AND_CONFIG_MP(8812a, _phy_reg);
		else if (config_type == CONFIG_BB_AGC_TAB)
			READ_AND_CONFIG_MP(8812a, _agc_tab);
		else if (config_type == CONFIG_BB_PHY_REG_PG) {
			if (p_dm->rfe_type == 3 && p_dm->is_mp_chip)
				READ_AND_CONFIG_MP(8812a, _phy_reg_pg_asus);
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
			else if (p_mgnt_info->CustomerID == RT_CID_WNC_NEC && p_dm->is_mp_chip)
				READ_AND_CONFIG_MP(8812a, _phy_reg_pg_nec);
#if RT_PLATFORM == PLATFORM_MACOSX
			/*{1827}{1024} for BUFFALO power by rate table. Isaiah 2013-11-29*/
			else if (p_mgnt_info->CustomerID == RT_CID_DNI_BUFFALO)
				READ_AND_CONFIG_MP(8812a, _phy_reg_pg_dni);
			/* TP-Link T4UH, Isaiah 2015-03-16*/
			else if (p_mgnt_info->CustomerID == RT_CID_TPLINK_HPWR) {
				dbg_print("RT_CID_TPLINK_HPWR:: _PHY_REG_PG_TPLINK\n");
				READ_AND_CONFIG_MP(8812a, _phy_reg_pg_tplink);
			}
#endif
#endif
			else
				READ_AND_CONFIG_MP(8812a, _phy_reg_pg);
		} else if (config_type == CONFIG_BB_PHY_REG_MP)
			READ_AND_CONFIG_MP(8812a, _phy_reg_mp);
		else if (config_type == CONFIG_BB_AGC_TAB_DIFF) {
			p_dm->fw_offload_ability &= ~PHYDM_PHY_PARAM_OFFLOAD;
			/*AGC_TAB DIFF dont support FW offload*/
			if ((*p_dm->p_channel >= 36)  && (*p_dm->p_channel  <= 64))
				AGC_DIFF_CONFIG_MP(8812a, lb);
			else if (*p_dm->p_channel >= 100)
				AGC_DIFF_CONFIG_MP(8812a, hb);
		}
	}
#endif
#if (RTL8821A_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8821) {
		if (config_type == CONFIG_BB_PHY_REG)
			READ_AND_CONFIG_MP(8821a, _phy_reg);
		else if (config_type == CONFIG_BB_AGC_TAB)
			READ_AND_CONFIG_MP(8821a, _agc_tab);
		else if (config_type == CONFIG_BB_PHY_REG_PG) {
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
#if (DEV_BUS_TYPE == RT_PCI_INTERFACE)
			HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);

			if ((p_hal_data->EEPROMSVID == 0x1043 && p_hal_data->EEPROMSMID == 0x207F))
				READ_AND_CONFIG_MP(8821a, _phy_reg_pg_e202_sa);
			else
#endif
#if (RT_PLATFORM == PLATFORM_MACOSX)
				/*{1827}{1022} for BUFFALO power by rate table. Isaiah 2013-10-18*/
				if (p_mgnt_info->CustomerID == RT_CID_DNI_BUFFALO) {
					/*{1024} for BUFFALO power by rate table. (JP/US)*/
					if (p_mgnt_info->ChannelPlan == RT_CHANNEL_DOMAIN_US_2G_CANADA_5G)
						READ_AND_CONFIG_MP(8821a, _phy_reg_pg_dni_us);
					else
						READ_AND_CONFIG_MP(8821a, _phy_reg_pg_dni_jp);
				} else
#endif
#endif
					READ_AND_CONFIG_MP(8821a, _phy_reg_pg);
		}
	}
#endif
#if (RTL8192E_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8192E) {
		if (config_type == CONFIG_BB_PHY_REG)
			READ_AND_CONFIG_MP(8192e, _phy_reg);
		else if (config_type == CONFIG_BB_AGC_TAB)
			READ_AND_CONFIG_MP(8192e, _agc_tab);
		else if (config_type == CONFIG_BB_PHY_REG_PG)
			READ_AND_CONFIG_MP(8192e, _phy_reg_pg);
	}
#endif
#if (RTL8723D_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8723D) {
		if (config_type == CONFIG_BB_PHY_REG)
			READ_AND_CONFIG_MP(8723d, _phy_reg);
		else if (config_type == CONFIG_BB_AGC_TAB)
			READ_AND_CONFIG_MP(8723d, _agc_tab);
		else if (config_type == CONFIG_BB_PHY_REG_PG)
			READ_AND_CONFIG_MP(8723d, _phy_reg_pg);
	}
#endif
/* JJ ADD 20161014 */
#if (RTL8710B_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8710B) {
		if (config_type == CONFIG_BB_PHY_REG)
			READ_AND_CONFIG_MP(8710b, _phy_reg);
		else if (config_type == CONFIG_BB_AGC_TAB)
			READ_AND_CONFIG_MP(8710b, _agc_tab);
		else if (config_type == CONFIG_BB_PHY_REG_PG)
			READ_AND_CONFIG_MP(8710b, _phy_reg_pg);
	}
#endif

#endif/* (DM_ODM_SUPPORT_TYPE !=  ODM_AP) */


	/* 1 All platforms support */
#if (RTL8188E_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8188E) {
		if (config_type == CONFIG_BB_PHY_REG)
			READ_AND_CONFIG_MP(8188e, _phy_reg);
		else if (config_type == CONFIG_BB_AGC_TAB)
			READ_AND_CONFIG_MP(8188e, _agc_tab);
		else if (config_type == CONFIG_BB_PHY_REG_PG)
			READ_AND_CONFIG_MP(8188e, _phy_reg_pg);
	}
#endif
#if (RTL8723B_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8723B) {
		if (config_type == CONFIG_BB_PHY_REG)
			READ_AND_CONFIG_MP(8723b, _phy_reg);
		else if (config_type == CONFIG_BB_AGC_TAB)
			READ_AND_CONFIG_MP(8723b, _agc_tab);
		else if (config_type == CONFIG_BB_PHY_REG_PG)
			READ_AND_CONFIG_MP(8723b, _phy_reg_pg);
	}
#endif
#if (RTL8814A_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8814A) {
		if (config_type == CONFIG_BB_PHY_REG)
			READ_AND_CONFIG_MP(8814a, _phy_reg);
		else if (config_type == CONFIG_BB_AGC_TAB)
			READ_AND_CONFIG_MP(8814a, _agc_tab);
		else if (config_type == CONFIG_BB_PHY_REG_PG) {
			if (p_dm->rfe_type == 0)
				READ_AND_CONFIG_MP(8814a,_phy_reg_pg_type0);
			else if (p_dm->rfe_type == 2)
				READ_AND_CONFIG_MP(8814a,_phy_reg_pg_type2);				
			else if (p_dm->rfe_type == 3)
				READ_AND_CONFIG_MP(8814a,_phy_reg_pg_type3);		
			else if (p_dm->rfe_type == 4)
				READ_AND_CONFIG_MP(8814a,_phy_reg_pg_type4);		
			else if (p_dm->rfe_type == 5)
				READ_AND_CONFIG_MP(8814a,_phy_reg_pg_type5);		
			else if (p_dm->rfe_type == 7)
				READ_AND_CONFIG_MP(8814a,_phy_reg_pg_type7);
			else if (p_dm->rfe_type == 8)
				READ_AND_CONFIG_MP(8814a,_phy_reg_pg_type8);
			else
				READ_AND_CONFIG_MP(8814a,_phy_reg_pg);
		}
		else if (config_type == CONFIG_BB_PHY_REG_MP)
			READ_AND_CONFIG_MP(8814a, _phy_reg_mp);
	}
#endif
#if (RTL8703B_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8703B) {
		if (config_type == CONFIG_BB_PHY_REG)
			READ_AND_CONFIG_MP(8703b, _phy_reg);
		else if (config_type == CONFIG_BB_AGC_TAB)
			READ_AND_CONFIG_MP(8703b, _agc_tab);
		else if (config_type == CONFIG_BB_PHY_REG_PG)
			READ_AND_CONFIG_MP(8703b, _phy_reg_pg);
	}
#endif
#if (RTL8188F_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8188F) {
		if (config_type == CONFIG_BB_PHY_REG)
			READ_AND_CONFIG_MP(8188f, _phy_reg);
		else if (config_type == CONFIG_BB_AGC_TAB)
			READ_AND_CONFIG_MP(8188f, _agc_tab);
		else if (config_type == CONFIG_BB_PHY_REG_PG)
			READ_AND_CONFIG_MP(8188f, _phy_reg_pg);
	}
#endif
#if (RTL8822B_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8822B) {
		if (config_type == CONFIG_BB_PHY_REG)
			READ_AND_CONFIG_MP(8822b, _phy_reg);
		else if (config_type == CONFIG_BB_AGC_TAB)
			READ_AND_CONFIG_MP(8822b, _agc_tab);
		else if (config_type == CONFIG_BB_PHY_REG_PG) {
			if (p_dm->rfe_type == 2)
				READ_AND_CONFIG_MP(8822b, _phy_reg_pg_type2);
			else if (p_dm->rfe_type == 3)
				READ_AND_CONFIG_MP(8822b, _phy_reg_pg_type3);
			else if (p_dm->rfe_type == 4)
				READ_AND_CONFIG_MP(8822b, _phy_reg_pg_type4);
			else if (p_dm->rfe_type == 5)
				READ_AND_CONFIG_MP(8822b, _phy_reg_pg_type5);
			else if (p_dm->rfe_type == 12)
				READ_AND_CONFIG_MP(8822b, _phy_reg_pg_type12);
			else if (p_dm->rfe_type == 15)
				READ_AND_CONFIG_MP(8822b, _phy_reg_pg_type15);
			else if (p_dm->rfe_type == 16)
				READ_AND_CONFIG_MP(8822b, _phy_reg_pg_type16);
			else if (p_dm->rfe_type == 17)
				READ_AND_CONFIG_MP(8822b, _phy_reg_pg_type17);
			else
				READ_AND_CONFIG_MP(8822b, _phy_reg_pg);
		}

	}
#endif

#if (RTL8197F_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8197F) {
		if (config_type == CONFIG_BB_PHY_REG) {
			READ_AND_CONFIG_MP(8197f, _phy_reg);
			if (p_dm->cut_version == ODM_CUT_A)
				phydm_phypara_a_cut(p_dm);
		} else if (config_type == CONFIG_BB_AGC_TAB)
			READ_AND_CONFIG_MP(8197f, _agc_tab);
	}
#endif

#if (RTL8821C_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8821C) {
		if (config_type == CONFIG_BB_PHY_REG)
			READ_AND_CONFIG(8821c, _phy_reg);
		else if (config_type == CONFIG_BB_AGC_TAB) {
			READ_AND_CONFIG(8821c, _agc_tab);
			/* According to RFEtype, choosing correct AGC table*/
			if (p_dm->default_rf_set_8821c == SWITCH_TO_BTG)
				AGC_DIFF_CONFIG_MP(8821c, btg);
		} else if (config_type == CONFIG_BB_PHY_REG_PG) {
			if (p_dm->rfe_type == 0x5)
				READ_AND_CONFIG(8821c, _phy_reg_pg_type0x28);
			else
				READ_AND_CONFIG(8821c, _phy_reg_pg);
		} else if (config_type == CONFIG_BB_AGC_TAB_DIFF) {
			p_dm->fw_offload_ability &= ~PHYDM_PHY_PARAM_OFFLOAD;
			/*AGC_TAB DIFF dont support FW offload*/
			if (p_dm->current_rf_set_8821c == SWITCH_TO_BTG)
				AGC_DIFF_CONFIG_MP(8821c, btg);
			else if (p_dm->current_rf_set_8821c == SWITCH_TO_WLG)
				AGC_DIFF_CONFIG_MP(8821c, wlg);
		} else if (config_type == CONFIG_BB_PHY_REG_MP)
			READ_AND_CONFIG(8821c, _phy_reg_mp);
	}
#endif

#if (RTL8195A_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8195A) {
		if (config_type == CONFIG_BB_PHY_REG)
			READ_AND_CONFIG(8195a, _phy_reg);
		else if (config_type == CONFIG_BB_AGC_TAB)
			READ_AND_CONFIG(8195a, _agc_tab);
		else if (config_type == CONFIG_BB_PHY_REG_PG)
			READ_AND_CONFIG(8195a, _phy_reg_pg);
	}
#endif

	if (config_type == CONFIG_BB_PHY_REG || config_type == CONFIG_BB_AGC_TAB)
		if (p_dm->fw_offload_ability & PHYDM_PHY_PARAM_OFFLOAD) {

			result = phydm_set_reg_by_fw(p_dm,
								PHYDM_HALMAC_CMD_END,
								0,
								0,
								0,
								(enum rf_path)0,
								0);
			PHYDM_DBG(p_dm, ODM_COMP_INIT,
				("phy param offload end!result = %d", result));
		}

	return result;
}

enum hal_status
odm_config_mac_with_header_file(
	struct PHY_DM_STRUCT	*p_dm
)
{
	enum hal_status	result = HAL_STATUS_SUCCESS;
	PHYDM_DBG(p_dm, ODM_COMP_INIT,
		("===>odm_config_mac_with_header_file (%s)\n", (p_dm->is_mp_chip) ? "MPChip" : "TestChip"));
	PHYDM_DBG(p_dm, ODM_COMP_INIT,
		("support_platform: 0x%X, support_interface: 0x%X, board_type: 0x%X\n",
		p_dm->support_platform, p_dm->support_interface, p_dm->board_type));

	/* 1 AP doesn't use PHYDM initialization in these ICs */
#if (DM_ODM_SUPPORT_TYPE != ODM_AP)
#if (RTL8812A_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8812)
		READ_AND_CONFIG_MP(8812a, _mac_reg);
#endif
#if (RTL8821A_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8821)
		READ_AND_CONFIG_MP(8821a, _mac_reg);
#endif
#if (RTL8192E_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8192E)
		READ_AND_CONFIG_MP(8192e, _mac_reg);
#endif
#if (RTL8723D_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8723D)
		READ_AND_CONFIG_MP(8723d, _mac_reg);
#endif
/* JJ ADD 20161014 */
#if (RTL8710B_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8710B)
		READ_AND_CONFIG_MP(8710b, _mac_reg);
#endif

#endif/* (DM_ODM_SUPPORT_TYPE !=  ODM_AP) */

	/* 1 All platforms support */
#if (RTL8188E_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8188E)
		READ_AND_CONFIG_MP(8188e, _mac_reg);
#endif
#if (RTL8723B_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8723B)
		READ_AND_CONFIG_MP(8723b, _mac_reg);
#endif
#if (RTL8814A_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8814A)
		READ_AND_CONFIG_MP(8814a, _mac_reg);
#endif
#if (RTL8703B_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8703B)
		READ_AND_CONFIG_MP(8703b, _mac_reg);
#endif
#if (RTL8188F_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8188F)
		READ_AND_CONFIG_MP(8188f, _mac_reg);
#endif
#if (RTL8822B_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8822B)
		READ_AND_CONFIG_MP(8822b, _mac_reg);
#endif

#if (RTL8197F_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8197F)
		READ_AND_CONFIG_MP(8197f, _mac_reg);
#endif

#if (RTL8821C_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8821C)
		READ_AND_CONFIG(8821c, _mac_reg);
#endif

#if (RTL8195A_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8195A)
		READ_AND_CONFIG_MP(8195a, _mac_reg);
#endif

	if (p_dm->fw_offload_ability & PHYDM_PHY_PARAM_OFFLOAD) {

		result = phydm_set_reg_by_fw(p_dm,
							PHYDM_HALMAC_CMD_END,
							0,
							0,
							0,
							(enum rf_path)0,
							0);
		PHYDM_DBG(p_dm, ODM_COMP_INIT,
			("mac param offload end!result = %d", result));
	}

	return result;
}

u32
odm_get_hw_img_version(
	struct PHY_DM_STRUCT	*p_dm
)
{
	u32  version = 0;

	/* 1 AP doesn't use PHYDM initialization in these ICs */
#if (DM_ODM_SUPPORT_TYPE != ODM_AP)
#if (RTL8821A_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8821)
		version = GET_VERSION_MP(8821a, _mac_reg);
#endif
#if (RTL8192E_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8192E)
		version = GET_VERSION_MP(8192e, _mac_reg);
#endif
#if (RTL8812A_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8812)
		version = GET_VERSION_MP(8812a, _mac_reg);
#endif
#if (RTL8723D_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8723D)
		version = GET_VERSION_MP(8723d, _mac_reg);
#endif
/* JJ ADD 20161014 */
#if (RTL8710B_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8710B)
		version = GET_VERSION_MP(8710b, _mac_reg);
#endif

#endif /* (DM_ODM_SUPPORT_TYPE != ODM_AP) */

	/*1 All platforms support*/
#if (RTL8188E_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8188E)
		version = GET_VERSION_MP(8188e, _mac_reg);
#endif
#if (RTL8723B_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8723B)
		version = GET_VERSION_MP(8723b, _mac_reg);
#endif
#if (RTL8814A_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8814A)
		version = GET_VERSION_MP(8814a, _mac_reg);
#endif
#if (RTL8703B_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8703B)
		version = GET_VERSION_MP(8703b, _mac_reg);
#endif
#if (RTL8188F_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8188F)
		version = GET_VERSION_MP(8188f, _mac_reg);
#endif
#if (RTL8822B_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8822B)
		version = GET_VERSION_MP(8822b, _mac_reg);
#endif

#if (RTL8197F_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8197F)
		version = GET_VERSION_MP(8197f, _mac_reg);
#endif

#if (RTL8821C_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8821C)
		version = GET_VERSION(8821c, _mac_reg);
#endif

	return version;
}


u32
query_phydm_trx_capability(
	struct PHY_DM_STRUCT					*p_dm
)
{
	u32 value32 = 0xFFFFFFFF;

#if (RTL8821C_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8821C)
		value32 = query_phydm_trx_capability_8821c(p_dm);
#endif

	return value32;
}

u32
query_phydm_stbc_capability(
	struct PHY_DM_STRUCT					*p_dm
)
{
	u32 value32 = 0xFFFFFFFF;

#if (RTL8821C_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8821C)
		value32 = query_phydm_stbc_capability_8821c(p_dm);
#endif

	return value32;
}

u32
query_phydm_ldpc_capability(
	struct PHY_DM_STRUCT					*p_dm
)
{
	u32 value32 = 0xFFFFFFFF;

#if (RTL8821C_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8821C)
		value32 = query_phydm_ldpc_capability_8821c(p_dm);
#endif

	return value32;
}

u32
query_phydm_txbf_parameters(
	struct PHY_DM_STRUCT					*p_dm
)
{
	u32 value32 = 0xFFFFFFFF;

#if (RTL8821C_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8821C)
		value32 = query_phydm_txbf_parameters_8821c(p_dm);
#endif

	return value32;
}

u32
query_phydm_txbf_capability(
	struct PHY_DM_STRUCT					*p_dm
)
{
	u32 value32 = 0xFFFFFFFF;

#if (RTL8821C_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8821C)
		value32 = query_phydm_txbf_capability_8821c(p_dm);
#endif

	return value32;
}
