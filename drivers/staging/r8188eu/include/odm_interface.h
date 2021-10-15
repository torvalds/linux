/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef	__ODM_INTERFACE_H__
#define __ODM_INTERFACE_H__

enum odm_h2c_cmd {
	ODM_H2C_RSSI_REPORT = 0,
	ODM_H2C_PSD_RESULT= 1,
	ODM_H2C_PathDiv = 2,
	ODM_MAX_H2CCMD
};

/*  2012/02/17 MH For non-MP compile pass only. Linux does not support workitem. */
/*  Suggest HW team to use thread instead of workitem. Windows also support the feature. */
typedef void (*RT_WORKITEM_CALL_BACK)(void *pContext);

/*  =========== Extern Variable ??? It should be forbidden. */

/*  =========== EXtern Function Prototype */

u8 ODM_Read1Byte(struct odm_dm_struct *pDM_Odm, u32 RegAddr);

u32 ODM_Read4Byte(struct odm_dm_struct *pDM_Odm, u32 RegAddr);

void ODM_Write1Byte(struct odm_dm_struct *pDM_Odm, u32 RegAddr, u8 Data);

void ODM_Write2Byte(struct odm_dm_struct *pDM_Odm, u32 RegAddr, u16 Data);

void ODM_Write4Byte(struct odm_dm_struct *pDM_Odm, u32 RegAddr, u32 Data);

void ODM_SetMACReg(struct odm_dm_struct *pDM_Odm, u32 RegAddr,
		   u32 BitMask, u32 Data);

u32 ODM_GetMACReg(struct odm_dm_struct *pDM_Odm, u32 RegAddr, u32 BitMask);

void ODM_SetBBReg(struct odm_dm_struct *pDM_Odm, u32 RegAddr,
		  u32 BitMask, u32 Data);

u32 ODM_GetBBReg(struct odm_dm_struct *pDM_Odm, u32 RegAddr, u32 BitMask);

void ODM_SetRFReg(struct odm_dm_struct *pDM_Odm, enum rf_radio_path eRFPath,
		  u32 RegAddr, u32 BitMask, u32 Data);

u32 ODM_GetRFReg(struct odm_dm_struct *pDM_Odm, enum rf_radio_path eRFPath,
		 u32 RegAddr, u32 BitMask);

/*  Memory Relative Function. */
s32 ODM_CompareMemory(struct odm_dm_struct *pDM_Odm, void *pBuf1, void *pBuf2,
		      u32 length);

/*  ODM Timer relative API. */
void ODM_delay_ms(u32 ms);

void ODM_delay_us(u32 us);

void ODM_sleep_ms(u32 ms);

#endif	/*  __ODM_INTERFACE_H__ */
