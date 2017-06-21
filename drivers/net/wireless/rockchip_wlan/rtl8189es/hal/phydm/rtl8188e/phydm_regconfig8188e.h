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
#ifndef __INC_ODM_REGCONFIG_H_8188E
#define __INC_ODM_REGCONFIG_H_8188E
 
#if (RTL8188E_SUPPORT == 1)

void
odm_ConfigRFReg_8188E(
	IN 	PDM_ODM_T 				pDM_Odm,
	IN 	u4Byte 					Addr,
	IN 	u4Byte 					Data,
	IN  ODM_RF_RADIO_PATH_E     RF_PATH,
	IN	u4Byte				    RegAddr
	);

void 
odm_ConfigRF_RadioA_8188E(
	IN 	PDM_ODM_T 				pDM_Odm,
	IN 	u4Byte 					Addr,
	IN 	u4Byte 					Data
	);

void 
odm_ConfigRF_RadioB_8188E(
	IN 	PDM_ODM_T 				pDM_Odm,
	IN 	u4Byte 					Addr,
	IN 	u4Byte 					Data
	);

void 
odm_ConfigMAC_8188E(
 	IN 	PDM_ODM_T 	pDM_Odm,
 	IN 	u4Byte 		Addr,
 	IN 	u1Byte 		Data
 	);

void 
odm_ConfigBB_AGC_8188E(
    IN 	PDM_ODM_T 	pDM_Odm,
    IN 	u4Byte 		Addr,
    IN 	u4Byte 		Bitmask,
    IN 	u4Byte 		Data
    );

void
odm_ConfigBB_PHY_REG_PG_8188E(
	IN 	PDM_ODM_T 	pDM_Odm,
	IN	u4Byte		Band,
	IN	u4Byte		RfPath,
	IN	u4Byte		TxNum,
    IN 	u4Byte 		Addr,
    IN 	u4Byte 		Bitmask,
    IN 	u4Byte 		Data
    );

void 
odm_ConfigBB_PHY_8188E(
	IN 	PDM_ODM_T 	pDM_Odm,
    IN 	u4Byte 		Addr,
    IN 	u4Byte 		Bitmask,
    IN 	u4Byte 		Data
    );

void
odm_ConfigBB_TXPWR_LMT_8188E(
	IN 	PDM_ODM_T 	pDM_Odm,
	IN	pu1Byte		Regulation,
	IN	pu1Byte		Band,
	IN	pu1Byte		Bandwidth,
	IN	pu1Byte		RateSection,
	IN	pu1Byte		RfPath,
	IN	pu1Byte 	Channel,
	IN	pu1Byte		PowerLimit
    );

#endif
#endif // end of SUPPORT

