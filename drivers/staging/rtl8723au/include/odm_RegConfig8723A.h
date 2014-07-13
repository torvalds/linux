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
 ******************************************************************************/
#ifndef __INC_ODM_REGCONFIG_H_8723A
#define __INC_ODM_REGCONFIG_H_8723A

void odm_ConfigRFReg_8723A(struct dm_odm_t *pDM_Odm, u32 Addr, u32 Data,
			   enum RF_RADIO_PATH RF_PATH, u32 RegAddr);

void odm_ConfigMAC_8723A(struct dm_odm_t *pDM_Odm, u32 Addr, u8 Data);

void odm_ConfigBB_AGC_8723A(struct dm_odm_t *pDM_Odm, u32 Addr,
			    u32 Bitmask, u32 Data);

void odm_ConfigBB_PHY_8723A(struct dm_odm_t *pDM_Odm, u32 Addr, u32 Bitmask, u32 Data);

#endif /*  end of SUPPORT */
