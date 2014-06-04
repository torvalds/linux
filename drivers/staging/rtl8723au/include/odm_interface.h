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


#ifndef	__ODM_INTERFACE_H__
#define __ODM_INTERFACE_H__



/*  */
/*  =========== Constant/Structure/Enum/... Define */
/*  */



/*  */
/*  =========== Macro Define */
/*  */

#define _reg_all(_name)			ODM_##_name
#define _reg_ic(_name, _ic)		ODM_##_name##_ic
#define _bit_all(_name)			BIT_##_name
#define _bit_ic(_name, _ic)		BIT_##_name##_ic

/*  _cat: implemented by Token-Pasting Operator. */

/*===================================

#define ODM_REG_DIG_11N		0xC50
#define ODM_REG_DIG_11AC	0xDDD

ODM_REG(DIG,_pDM_Odm)
=====================================*/

#define _reg_11N(_name)			ODM_REG_##_name##_11N
#define _reg_11AC(_name)		ODM_REG_##_name##_11AC
#define _bit_11N(_name)			ODM_BIT_##_name##_11N
#define _bit_11AC(_name)		ODM_BIT_##_name##_11AC

#define _cat(_name, _ic_type, _func)									\
	(															\
		((_ic_type) & ODM_IC_11N_SERIES)? _func##_11N(_name):		\
		_func##_11AC(_name)									\
	)

/*  _name: name of register or bit. */
/*  Example: "ODM_REG(R_A_AGC_CORE1, pDM_Odm)" */
/*         gets "ODM_R_A_AGC_CORE1" or "ODM_R_A_AGC_CORE1_8192C", depends on SupportICType. */
#define ODM_REG(_name, _pDM_Odm)	_cat(_name, _pDM_Odm->SupportICType, _reg)
#define ODM_BIT(_name, _pDM_Odm)	_cat(_name, _pDM_Odm->SupportICType, _bit)

/*  */
/*  2012/02/17 MH For non-MP compile pass only. Linux does not support workitem. */
/*  Suggest HW team to use thread instead of workitem. Windows also support the feature. */
/*  */
typedef void (*RT_WORKITEM_CALL_BACK)(struct work_struct *pContext);

/*  */
/*  =========== Extern Variable ??? It should be forbidden. */
/*  */


/*  */
/*  =========== EXtern Function Prototype */
/*  */


u8 ODM_Read1Byte(struct dm_odm_t *pDM_Odm, u32 RegAddr);

u16 ODM_Read2Byte(struct dm_odm_t *pDM_Odm, u32 RegAddr);

u32 ODM_Read4Byte(struct dm_odm_t *pDM_Odm, u32 RegAddr);

void ODM_Write1Byte(struct dm_odm_t *pDM_Odm, u32 RegAddr, u8 Data);

void ODM_Write2Byte(struct dm_odm_t *pDM_Odm, u32 RegAddr, u16 Data);

void ODM_Write4Byte(struct dm_odm_t *pDM_Odm, u32 RegAddr, u32 Data);

void ODM_SetMACReg(struct dm_odm_t *pDM_Odm, u32 RegAddr, u32 BitMask, u32 Data);

u32 ODM_GetMACReg(struct dm_odm_t *pDM_Odm, u32 RegAddr, u32 BitMask);

void ODM_SetBBReg(struct dm_odm_t *pDM_Odm, u32 RegAddr, u32 BitMask, u32 Data);

u32 ODM_GetBBReg(struct dm_odm_t *pDM_Odm, u32 RegAddr, u32 BitMask);

void ODM_SetRFReg(struct dm_odm_t *pDM_Odm, enum RF_RADIO_PATH eRFPath,
		  u32 RegAddr, u32 BitMask, u32 Data);

u32 ODM_GetRFReg(struct dm_odm_t *pDM_Odm, enum RF_RADIO_PATH eRFPath,
		 u32 RegAddr, u32 BitMask);

/*  Memory Relative Function. */
void ODM_AllocateMemory(struct dm_odm_t *pDM_Odm, void **pPtr, u32 length);
void ODM_FreeMemory(struct dm_odm_t *pDM_Odm, void *pPtr, u32 length);

s32 ODM_CompareMemory(struct dm_odm_t *pDM_Odm, void *pBuf1, void *pBuf2, u32 length);

/*  ODM MISC-spin lock relative API. */
void ODM_AcquireSpinLock(struct dm_odm_t *pDM_Odm, enum rt_spinlock_type type);

void ODM_ReleaseSpinLock(struct dm_odm_t *pDM_Odm, enum rt_spinlock_type type);

/*  ODM MISC-workitem relative API. */
void ODM_InitializeWorkItem(struct dm_odm_t *pDM_Odm, void *pRtWorkItem,
			    RT_WORKITEM_CALL_BACK RtWorkItemCallback, void *pContext, const char *szID);

/*  ODM Timer relative API. */
void ODM_SetTimer(struct dm_odm_t *pDM_Odm, struct timer_list *pTimer, u32 msDelay);

void ODM_ReleaseTimer(struct dm_odm_t *pDM_Odm, struct timer_list *pTimer);

/*  ODM FW relative API. */
u32 ODM_FillH2CCmd(u8 *pH2CBuffer, u32 H2CBufferLen, u32 CmdNum,
		   u32 *pElementID, u32 *pCmdLen, u8 **pCmbBuffer,
		   u8 *CmdStartSeq);

#endif	/*  __ODM_INTERFACE_H__ */
