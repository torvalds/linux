/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __INC_ODM_REGCONFIG_H_8188E
#define __INC_ODM_REGCONFIG_H_8188E

void odm_ConfigRFReg_8188E(struct odm_dm_struct *pDM_Odm, u32 Addr, u32 Data,
			   enum rf_radio_path  RF_PATH, u32 RegAddr);

void odm_ConfigRF_RadioA_8188E(struct odm_dm_struct *pDM_Odm,
			       u32 Addr, u32 Data);

void odm_ConfigRF_RadioB_8188E(struct odm_dm_struct *pDM_Odm,
			       u32 Addr, u32 Data);

void odm_ConfigMAC_8188E(struct odm_dm_struct *pDM_Odm, u32 Addr, u8 Data);

void odm_ConfigBB_AGC_8188E(struct odm_dm_struct *pDM_Odm, u32 Addr,
			    u32 Bitmask, u32 Data);

void odm_ConfigBB_PHY_REG_PG_8188E(struct odm_dm_struct *pDM_Odm, u32 Addr,
				   u32 Bitmask, u32 Data);

void odm_ConfigBB_PHY_8188E(struct odm_dm_struct *pDM_Odm, u32 Addr,
			    u32 Bitmask, u32 Data);

#endif
