/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

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

#define _cat(_name, _ic_type, _func)					\
	(								\
		((_ic_type) & ODM_IC_11N_SERIES) ? _func##_11N(_name) :	\
		_func##_11AC(_name)					\
	)

/*  _name: name of register or bit. */
/*  Example: "ODM_REG(R_A_AGC_CORE1, pDM_Odm)" */
/*         gets "ODM_R_A_AGC_CORE1" or "ODM_R_A_AGC_CORE1_8192C",
 *	   depends on SupportICType. */
#define ODM_REG(_name, _pDM_Odm) _cat(_name, _pDM_Odm->SupportICType, _reg)
#define ODM_BIT(_name, _pDM_Odm) _cat(_name, _pDM_Odm->SupportICType, _bit)

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

u16 ODM_Read2Byte(struct odm_dm_struct *pDM_Odm, u32 RegAddr);

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
void ODM_AllocateMemory(struct odm_dm_struct *pDM_Odm, void **pPtr, u32 length);
void ODM_FreeMemory(struct odm_dm_struct *pDM_Odm, void *pPtr, u32 length);

s32 ODM_CompareMemory(struct odm_dm_struct *pDM_Odm, void *pBuf1, void *pBuf2,
		      u32 length);

/*  ODM MISC-spin lock relative API. */
void ODM_AcquireSpinLock(struct odm_dm_struct *pDM_Odm,
			 enum RT_SPINLOCK_TYPE type);

void ODM_ReleaseSpinLock(struct odm_dm_struct *pDM_Odm,
			 enum RT_SPINLOCK_TYPE type);

/*  ODM MISC-workitem relative API. */
void ODM_InitializeWorkItem(struct odm_dm_struct *pDM_Odm, void *pRtWorkItem,
			    RT_WORKITEM_CALL_BACK RtWorkItemCallback,
			    void *pContext, const char *szID);

void ODM_StartWorkItem(void *pRtWorkItem);

void ODM_StopWorkItem(void *pRtWorkItem);

void ODM_FreeWorkItem(void *pRtWorkItem);

void ODM_ScheduleWorkItem(void *pRtWorkItem);

void ODM_IsWorkItemScheduled(void *pRtWorkItem);

/*  ODM Timer relative API. */
void ODM_StallExecution(u32 usDelay);

void ODM_delay_ms(u32 ms);

void ODM_delay_us(u32 us);

void ODM_sleep_ms(u32 ms);

void ODM_sleep_us(u32 us);

void ODM_SetTimer(struct odm_dm_struct *pDM_Odm, struct timer_list *pTimer,
		  u32 msDelay);

void ODM_InitializeTimer(struct odm_dm_struct *pDM_Odm,
			 struct timer_list *pTimer, void *CallBackFunc,
			 void *pContext, const char *szID);

void ODM_CancelTimer(struct odm_dm_struct *pDM_Odm, struct timer_list *pTimer);

void ODM_ReleaseTimer(struct odm_dm_struct *pDM_Odm, struct timer_list *pTimer);

/*  ODM FW relative API. */
u32 ODM_FillH2CCmd(u8 *pH2CBuffer, u32 H2CBufferLen, u32 CmdNum,
		   u32 *pElementID, u32 *pCmdLen, u8 **pCmbBuffer,
		   u8 *CmdStartSeq);

#endif	/*  __ODM_INTERFACE_H__ */
