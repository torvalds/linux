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
#ifndef __INC_PHYDM_API_H_8822B__
#define __INC_PHYDM_API_H_8822B__

#if (RTL8822B_SUPPORT == 1)

#define	PHY_CONFIG_VERSION_8822B			"27.5.31"	/*2016.08.01     (HW user guide version: R27, SW user guide version: R05, Modification: R31)*/

#define	INVALID_RF_DATA					0xffffffff
#define	INVALID_TXAGC_DATA				0xff

#define	config_phydm_read_rf_check_8822b(data)			(data != INVALID_RF_DATA)
#define	config_phydm_read_txagc_check_8822b(data)		(data != INVALID_TXAGC_DATA)

u4Byte
config_phydm_read_rf_reg_8822b(
	IN	PDM_ODM_T				pDM_Odm,
	IN	ODM_RF_RADIO_PATH_E		RFPath,
	IN	u4Byte					RegAddr,
	IN	u4Byte					BitMask
	);

BOOLEAN
config_phydm_write_rf_reg_8822b(
	IN	PDM_ODM_T				pDM_Odm,
	IN	ODM_RF_RADIO_PATH_E		RFPath,
	IN	u4Byte					RegAddr,
	IN	u4Byte					BitMask,
	IN	u4Byte					Data
	);

BOOLEAN
config_phydm_write_txagc_8822b(
	IN	PDM_ODM_T				pDM_Odm,
	IN	u4Byte					PowerIndex,
	IN	ODM_RF_RADIO_PATH_E		Path,	
	IN	u1Byte					HwRate
	);

u1Byte
config_phydm_read_txagc_8822b(
	IN	PDM_ODM_T				pDM_Odm,
	IN	ODM_RF_RADIO_PATH_E		Path,
	IN	u1Byte					HwRate
	);

BOOLEAN
config_phydm_switch_band_8822b(	
	IN	PDM_ODM_T				pDM_Odm,
	IN	u1Byte					central_ch
	);

BOOLEAN
config_phydm_switch_channel_8822b(	
	IN	PDM_ODM_T				pDM_Odm,
	IN	u1Byte					central_ch
	);

BOOLEAN
config_phydm_switch_bandwidth_8822b(	
	IN	PDM_ODM_T				pDM_Odm,
	IN	u1Byte					primary_ch_idx,
	IN	ODM_BW_E				bandwidth
	);

BOOLEAN
config_phydm_switch_channel_bw_8822b(	
	IN	PDM_ODM_T				pDM_Odm,
	IN	u1Byte					central_ch,
	IN	u1Byte					primary_ch_idx,
	IN	ODM_BW_E				bandwidth
	);

BOOLEAN
config_phydm_trx_mode_8822b(
	IN	PDM_ODM_T				pDM_Odm,
	IN	ODM_RF_PATH_E			TxPath,
	IN	ODM_RF_PATH_E			RxPath,
	IN	BOOLEAN					bTx2Path
	);

BOOLEAN
config_phydm_parameter_init(
	IN	PDM_ODM_T				pDM_Odm,
	IN	ODM_PARAMETER_INIT_E	type
	);


/* ======================================================================== */
/* These following functions can be used for PHY DM only*/

BOOLEAN
phydm_write_txagc_1byte_8822b(
	IN	PDM_ODM_T				pDM_Odm,
	IN	u4Byte					PowerIndex,
	IN	ODM_RF_RADIO_PATH_E		Path,	
	IN	u1Byte					HwRate
	);

VOID
phydm_init_hw_info_by_rfe_type_8822b(
	IN	PDM_ODM_T				pDM_Odm
	);

s4Byte
phydm_get_condition_number_8822B(
	IN	PDM_ODM_T				pDM_Odm
	);

/* ======================================================================== */

#endif	/* RTL8822B_SUPPORT == 1 */
#endif	/*  __INC_PHYDM_API_H_8822B__ */


