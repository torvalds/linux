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


#ifndef	__ODM_INTERFACE_H__
#define __ODM_INTERFACE_H__



//
// =========== Constant/Structure/Enum/... Define
//



//
// =========== Macro Define
//

#define _reg_all(_name)			ODM_##_name
#define _reg_ic(_name, _ic)		ODM_##_name##_ic
#define _bit_all(_name)			BIT_##_name
#define _bit_ic(_name, _ic)		BIT_##_name##_ic

// _cat: implemented by Token-Pasting Operator.
#if 0
#define _cat(_name, _ic_type, _func)								\
	( 															\
		_func##_all(_name)										\
	)
#endif

/*===================================

#define ODM_REG_DIG_11N		0xC50
#define ODM_REG_DIG_11AC	0xDDD

ODM_REG(DIG,_pDM_Odm)
=====================================*/

#define _reg_11N(_name)			ODM_REG_##_name##_11N 
#define _reg_11AC(_name)		ODM_REG_##_name##_11AC
#define _bit_11N(_name)			ODM_BIT_##_name##_11N 
#define _bit_11AC(_name)		ODM_BIT_##_name##_11AC

#if 1 //TODO: enable it if we need to support run-time to differentiate between 92C_SERIES and JAGUAR_SERIES.
#define _cat(_name, _ic_type, _func)									\
	( 															\
		((_ic_type) & ODM_IC_11N_SERIES)? _func##_11N(_name):		\
		_func##_11AC(_name)									\
	)
#endif
#if 0 // only sample code
#define _cat(_name, _ic_type, _func)									\
	( 															\
		((_ic_type) & ODM_RTL8192C)? _func##_ic(_name, _8192C):		\
		((_ic_type) & ODM_RTL8192D)? _func##_ic(_name, _8192D):		\
		((_ic_type) & ODM_RTL8192S)? _func##_ic(_name, _8192S):		\
		((_ic_type) & ODM_RTL8723A)? _func##_ic(_name, _8723A):		\
		((_ic_type) & ODM_RTL8188E)? _func##_ic(_name, _8188E):		\
		_func##_ic(_name, _8195)									\
	)
#endif

// _name: name of register or bit.
// Example: "ODM_REG(R_A_AGC_CORE1, pDM_Odm)" 
//        gets "ODM_R_A_AGC_CORE1" or "ODM_R_A_AGC_CORE1_8192C", depends on SupportICType.
#define ODM_REG(_name, _pDM_Odm)	_cat(_name, _pDM_Odm->SupportICType, _reg)
#define ODM_BIT(_name, _pDM_Odm)	_cat(_name, _pDM_Odm->SupportICType, _bit)

typedef enum _ODM_H2C_CMD 
{
	ODM_H2C_RSSI_REPORT = 0,
	ODM_H2C_PSD_RESULT=1,	
	ODM_H2C_PathDiv = 2,               
	ODM_MAX_H2CCMD
}ODM_H2C_CMD;


//
// 2012/02/17 MH For non-MP compile pass only. Linux does not support workitem.
// Suggest HW team to use thread instead of workitem. Windows also support the feature.
//
#if (DM_ODM_SUPPORT_TYPE != ODM_WIN)
typedef  void *PRT_WORK_ITEM ;
typedef  void RT_WORKITEM_HANDLE,*PRT_WORKITEM_HANDLE;
typedef VOID (*RT_WORKITEM_CALL_BACK)(PVOID pContext);

#if 0
typedef struct tasklet_struct RT_WORKITEM_HANDLE, *PRT_WORKITEM_HANDLE;

typedef struct _RT_WORK_ITEM
{
	
	RT_WORKITEM_HANDLE			Handle;			// Platform-dependent handle for this workitem, e.g. Ndis Workitem object.
	PVOID						Adapter;		// Pointer to Adapter object.
	PVOID						pContext;		// Parameter to passed to CallBackFunc(). 
	RT_WORKITEM_CALL_BACK		CallbackFunc;	// Callback function of the workitem.
	u1Byte						RefCount;		// 0: driver is going to unload, 1: No such workitem scheduled, 2: one workitem is schedueled. 
	PVOID						pPlatformExt;	// Pointer to platform-dependent extension.	
	BOOLEAN						bFree;
	char						szID[36];		// An identity string of this workitem.
}RT_WORK_ITEM, *PRT_WORK_ITEM;

#endif


#endif

//
// =========== Extern Variable ??? It should be forbidden.
//


//
// =========== EXtern Function Prototype
//


u1Byte
ODM_Read1Byte(
	IN 	PDM_ODM_T		pDM_Odm,
	IN	u4Byte			RegAddr
	);

u2Byte
ODM_Read2Byte(
	IN 	PDM_ODM_T		pDM_Odm,
	IN	u4Byte			RegAddr
	);

u4Byte
ODM_Read4Byte(
	IN 	PDM_ODM_T		pDM_Odm,
	IN	u4Byte			RegAddr
	);

VOID
ODM_Write1Byte(
	IN 	PDM_ODM_T		pDM_Odm,
	IN	u4Byte			RegAddr,
	IN	u1Byte			Data
	);

VOID
ODM_Write2Byte(
	IN 	PDM_ODM_T		pDM_Odm,
	IN	u4Byte			RegAddr,
	IN	u2Byte			Data
	);

VOID
ODM_Write4Byte(
	IN 	PDM_ODM_T		pDM_Odm,
	IN	u4Byte			RegAddr,
	IN	u4Byte			Data
	);

VOID
ODM_SetMACReg(	
	IN 	PDM_ODM_T	pDM_Odm,
	IN	u4Byte		RegAddr,
	IN	u4Byte		BitMask,
	IN	u4Byte		Data
	);

u4Byte 
ODM_GetMACReg(	
	IN 	PDM_ODM_T	pDM_Odm,
	IN	u4Byte		RegAddr,
	IN	u4Byte		BitMask
	);

VOID
ODM_SetBBReg(	
	IN 	PDM_ODM_T	pDM_Odm,
	IN	u4Byte		RegAddr,
	IN	u4Byte		BitMask,
	IN	u4Byte		Data
	);

u4Byte 
ODM_GetBBReg(	
	IN 	PDM_ODM_T	pDM_Odm,
	IN	u4Byte		RegAddr,
	IN	u4Byte		BitMask
	);

VOID
ODM_SetRFReg(	
	IN 	PDM_ODM_T			pDM_Odm,
	IN	ODM_RF_RADIO_PATH_E	eRFPath,
	IN	u4Byte				RegAddr,
	IN	u4Byte				BitMask,
	IN	u4Byte				Data
	);

u4Byte 
ODM_GetRFReg(	
	IN 	PDM_ODM_T			pDM_Odm,
	IN	ODM_RF_RADIO_PATH_E	eRFPath,
	IN	u4Byte				RegAddr,
	IN	u4Byte				BitMask
	);


//
// Memory Relative Function.
//
VOID
ODM_AllocateMemory(	
	IN 	PDM_ODM_T	pDM_Odm,
	OUT	PVOID		*pPtr,
	IN	u4Byte		length
	);
VOID
ODM_FreeMemory(	
	IN 	PDM_ODM_T	pDM_Odm,
	OUT	PVOID		pPtr,
	IN	u4Byte		length
	);

VOID
ODM_MoveMemory(	
	IN 	PDM_ODM_T	pDM_Odm,
	OUT PVOID		pDest,
	IN  PVOID		pSrc,
	IN  u4Byte		Length
	);

s4Byte ODM_CompareMemory(
	IN 	PDM_ODM_T	pDM_Odm,
	IN	PVOID           pBuf1,
      IN	PVOID           pBuf2,
      IN	u4Byte          length
       );
	
//
// ODM MISC-spin lock relative API.
//
VOID
ODM_AcquireSpinLock(	
	IN 	PDM_ODM_T			pDM_Odm,
	IN	RT_SPINLOCK_TYPE	type
	);

VOID
ODM_ReleaseSpinLock(	
	IN 	PDM_ODM_T			pDM_Odm,
	IN	RT_SPINLOCK_TYPE	type
	);


//
// ODM MISC-workitem relative API.
//
VOID
ODM_InitializeWorkItem(	
	IN 	PDM_ODM_T					pDM_Odm,
	IN	PRT_WORK_ITEM				pRtWorkItem,
	IN	RT_WORKITEM_CALL_BACK		RtWorkItemCallback,
	IN	PVOID						pContext,
	IN	const char*					szID
	);

VOID
ODM_StartWorkItem(	
	IN	PRT_WORK_ITEM	pRtWorkItem
	);

VOID
ODM_StopWorkItem(	
	IN	PRT_WORK_ITEM	pRtWorkItem
	);

VOID
ODM_FreeWorkItem(	
	IN	PRT_WORK_ITEM	pRtWorkItem
	);

VOID
ODM_ScheduleWorkItem(	
	IN	PRT_WORK_ITEM	pRtWorkItem
	);

VOID
ODM_IsWorkItemScheduled(	
	IN	PRT_WORK_ITEM	pRtWorkItem
	);

//
// ODM Timer relative API.
//
VOID
ODM_StallExecution(	
	IN	u4Byte	usDelay
	);

VOID
ODM_delay_ms(IN u4Byte	ms);



VOID
ODM_delay_us(IN u4Byte	us);

VOID
ODM_sleep_ms(IN u4Byte	ms);

VOID
ODM_sleep_us(IN u4Byte	us);

VOID
ODM_SetTimer(	
	IN 	PDM_ODM_T		pDM_Odm,
	IN	PRT_TIMER 		pTimer, 
	IN	u4Byte 			msDelay
	);

VOID
ODM_InitializeTimer(
	IN 	PDM_ODM_T			pDM_Odm,
	IN	PRT_TIMER 			pTimer, 
	IN	RT_TIMER_CALL_BACK	CallBackFunc, 
	IN	PVOID				pContext,
	IN	const char*			szID
	);

VOID
ODM_CancelTimer(
	IN 	PDM_ODM_T		pDM_Odm,
	IN	PRT_TIMER		pTimer
	);

VOID
ODM_ReleaseTimer(
	IN 	PDM_ODM_T		pDM_Odm,
	IN	PRT_TIMER		pTimer
	);


//
// ODM FW relative API.
//
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
VOID
ODM_FillH2CCmd(
	IN	PADAPTER		Adapter,
	IN	u1Byte 	ElementID,
	IN	u4Byte 	CmdLen,
	IN	pu1Byte	pCmdBuffer
);
#else
u4Byte
ODM_FillH2CCmd(	
	IN	pu1Byte		pH2CBuffer,
	IN	u4Byte		H2CBufferLen,
	IN	u4Byte		CmdNum,
	IN	pu4Byte		pElementID,
	IN	pu4Byte		pCmdLen,
	IN	pu1Byte*		pCmbBuffer,
	IN	pu1Byte		CmdStartSeq
	);
#endif
#endif	// __ODM_INTERFACE_H__

