// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#include "../include/odm_precomp.h"
/*  ODM IO Relative API. */

u8 ODM_Read1Byte(struct odm_dm_struct *pDM_Odm, u32 RegAddr)
{
	struct adapter *Adapter = pDM_Odm->Adapter;
	return rtw_read8(Adapter, RegAddr);
}

u16 ODM_Read2Byte(struct odm_dm_struct *pDM_Odm, u32 RegAddr)
{
	struct adapter *Adapter = pDM_Odm->Adapter;
	return rtw_read16(Adapter, RegAddr);
}

u32 ODM_Read4Byte(struct odm_dm_struct *pDM_Odm, u32 RegAddr)
{
	struct adapter *Adapter = pDM_Odm->Adapter;
	return rtw_read32(Adapter, RegAddr);
}

void ODM_Write1Byte(struct odm_dm_struct *pDM_Odm, u32 RegAddr, u8 Data)
{
	struct adapter *Adapter = pDM_Odm->Adapter;
	rtw_write8(Adapter, RegAddr, Data);
}

void ODM_Write2Byte(struct odm_dm_struct *pDM_Odm, u32 RegAddr, u16 Data)
{
	struct adapter *Adapter = pDM_Odm->Adapter;
	rtw_write16(Adapter, RegAddr, Data);
}

void ODM_Write4Byte(struct odm_dm_struct *pDM_Odm, u32 RegAddr, u32 Data)
{
	struct adapter *Adapter = pDM_Odm->Adapter;
	rtw_write32(Adapter, RegAddr, Data);
}

void ODM_SetMACReg(struct odm_dm_struct *pDM_Odm, u32 RegAddr, u32 BitMask, u32 Data)
{
	struct adapter *Adapter = pDM_Odm->Adapter;
	PHY_SetBBReg(Adapter, RegAddr, BitMask, Data);
}

u32 ODM_GetMACReg(struct odm_dm_struct *pDM_Odm, u32 RegAddr, u32 BitMask)
{
	struct adapter *Adapter = pDM_Odm->Adapter;
	return PHY_QueryBBReg(Adapter, RegAddr, BitMask);
}

void ODM_SetBBReg(struct odm_dm_struct *pDM_Odm, u32 RegAddr, u32 BitMask, u32 Data)
{
	struct adapter *Adapter = pDM_Odm->Adapter;
	PHY_SetBBReg(Adapter, RegAddr, BitMask, Data);
}

u32 ODM_GetBBReg(struct odm_dm_struct *pDM_Odm, u32 RegAddr, u32 BitMask)
{
	struct adapter *Adapter = pDM_Odm->Adapter;
	return PHY_QueryBBReg(Adapter, RegAddr, BitMask);
}

void ODM_SetRFReg(struct odm_dm_struct *pDM_Odm, enum rf_radio_path	eRFPath, u32 RegAddr, u32 BitMask, u32 Data)
{
	struct adapter *Adapter = pDM_Odm->Adapter;
	PHY_SetRFReg(Adapter, (enum rf_radio_path)eRFPath, RegAddr, BitMask, Data);
}

u32 ODM_GetRFReg(struct odm_dm_struct *pDM_Odm, enum rf_radio_path	eRFPath, u32 RegAddr, u32 BitMask)
{
	struct adapter *Adapter = pDM_Odm->Adapter;
	return PHY_QueryRFReg(Adapter, (enum rf_radio_path)eRFPath, RegAddr, BitMask);
}

/*  ODM Memory relative API. */
void ODM_AllocateMemory(struct odm_dm_struct *pDM_Odm, void **pPtr, u32 length)
{
	*pPtr = vzalloc(length);
}

/*  length could be ignored, used to detect memory leakage. */
void ODM_FreeMemory(struct odm_dm_struct *pDM_Odm, void *pPtr, u32 length)
{
	vfree(pPtr);
}

s32 ODM_CompareMemory(struct odm_dm_struct *pDM_Odm, void *pBuf1, void *pBuf2, u32 length)
{
	return !memcmp(pBuf1, pBuf2, length);
}

/*  Work item relative API. FOr MP driver only~! */
void ODM_StopWorkItem(void *pRtWorkItem)
{
}

void ODM_FreeWorkItem(void *pRtWorkItem)
{
}

void ODM_ScheduleWorkItem(void *pRtWorkItem)
{
}

void ODM_IsWorkItemScheduled(void *pRtWorkItem)
{
}

/*  ODM Timer relative API. */
void ODM_StallExecution(u32 usDelay)
{
	udelay(usDelay);
}

void ODM_delay_ms(u32 ms)
{
	mdelay(ms);
}

void ODM_delay_us(u32 us)
{
	udelay(us);
}

void ODM_sleep_ms(u32 ms)
{
	msleep(ms);
}

void ODM_SetTimer(struct odm_dm_struct *pDM_Odm, struct timer_list *pTimer, u32 msDelay)
{
	_set_timer(pTimer, msDelay); /* ms */
}

void ODM_CancelTimer(struct odm_dm_struct *pDM_Odm, struct timer_list *pTimer)
{
	_cancel_timer_ex(pTimer);
}
