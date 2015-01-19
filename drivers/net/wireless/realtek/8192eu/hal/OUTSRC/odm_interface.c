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

//============================================================
// include files
//============================================================


#include "odm_precomp.h"

//
// ODM IO Relative API.
//

u1Byte
ODM_Read1Byte(
	IN 	PDM_ODM_T		pDM_Odm,
	IN	u4Byte			RegAddr
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	prtl8192cd_priv	priv	= pDM_Odm->priv;
	return	RTL_R8(RegAddr);
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	return rtw_read8(Adapter,RegAddr);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	return	PlatformEFIORead1Byte(Adapter, RegAddr);
#endif	

}


u2Byte
ODM_Read2Byte(
	IN 	PDM_ODM_T		pDM_Odm,
	IN	u4Byte			RegAddr
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	prtl8192cd_priv	priv	= pDM_Odm->priv;
	return	RTL_R16(RegAddr);
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	return rtw_read16(Adapter,RegAddr);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	return	PlatformEFIORead2Byte(Adapter, RegAddr);
#endif	

}


u4Byte
ODM_Read4Byte(
	IN 	PDM_ODM_T		pDM_Odm,
	IN	u4Byte			RegAddr
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	prtl8192cd_priv	priv	= pDM_Odm->priv;
	return	RTL_R32(RegAddr);
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	return rtw_read32(Adapter,RegAddr);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	return	PlatformEFIORead4Byte(Adapter, RegAddr);
#endif	

}


VOID
ODM_Write1Byte(
	IN 	PDM_ODM_T		pDM_Odm,
	IN	u4Byte			RegAddr,
	IN	u1Byte			Data
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	prtl8192cd_priv	priv	= pDM_Odm->priv;
	RTL_W8(RegAddr, Data);
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	rtw_write8(Adapter,RegAddr, Data);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	PlatformEFIOWrite1Byte(Adapter, RegAddr, Data);
#endif
	
}


VOID
ODM_Write2Byte(
	IN 	PDM_ODM_T		pDM_Odm,
	IN	u4Byte			RegAddr,
	IN	u2Byte			Data
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	prtl8192cd_priv	priv	= pDM_Odm->priv;
	RTL_W16(RegAddr, Data);
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	rtw_write16(Adapter,RegAddr, Data);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	PlatformEFIOWrite2Byte(Adapter, RegAddr, Data);
#endif	

}


VOID
ODM_Write4Byte(
	IN 	PDM_ODM_T		pDM_Odm,
	IN	u4Byte			RegAddr,
	IN	u4Byte			Data
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	prtl8192cd_priv	priv	= pDM_Odm->priv;
	RTL_W32(RegAddr, Data);
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	rtw_write32(Adapter,RegAddr, Data);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	PlatformEFIOWrite4Byte(Adapter, RegAddr, Data);
#endif	

}


VOID
ODM_SetMACReg(	
	IN 	PDM_ODM_T	pDM_Odm,
	IN	u4Byte		RegAddr,
	IN	u4Byte		BitMask,
	IN	u4Byte		Data
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	PHY_SetBBReg(pDM_Odm->priv, RegAddr, BitMask, Data);
#elif(DM_ODM_SUPPORT_TYPE & (ODM_CE|ODM_WIN))
	PADAPTER		Adapter = pDM_Odm->Adapter;
	PHY_SetBBReg(Adapter, RegAddr, BitMask, Data);
#endif	
}


u4Byte 
ODM_GetMACReg(	
	IN 	PDM_ODM_T	pDM_Odm,
	IN	u4Byte		RegAddr,
	IN	u4Byte		BitMask
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	return PHY_QueryMacReg(pDM_Odm->priv, RegAddr, BitMask);
#elif(DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	PADAPTER		Adapter = pDM_Odm->Adapter;
	return PHY_QueryMacReg(Adapter, RegAddr, BitMask);
#elif(DM_ODM_SUPPORT_TYPE & (ODM_CE))
	return PHY_QueryBBReg(pDM_Odm->Adapter, RegAddr, BitMask);
#endif	
}


VOID
ODM_SetBBReg(	
	IN 	PDM_ODM_T	pDM_Odm,
	IN	u4Byte		RegAddr,
	IN	u4Byte		BitMask,
	IN	u4Byte		Data
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	PHY_SetBBReg(pDM_Odm->priv, RegAddr, BitMask, Data);
#elif(DM_ODM_SUPPORT_TYPE & (ODM_CE|ODM_WIN))
	PADAPTER		Adapter = pDM_Odm->Adapter;
	PHY_SetBBReg(Adapter, RegAddr, BitMask, Data);
#endif	
}


u4Byte 
ODM_GetBBReg(	
	IN 	PDM_ODM_T	pDM_Odm,
	IN	u4Byte		RegAddr,
	IN	u4Byte		BitMask
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	return PHY_QueryBBReg(pDM_Odm->priv, RegAddr, BitMask);
#elif(DM_ODM_SUPPORT_TYPE & (ODM_CE|ODM_WIN))
	PADAPTER		Adapter = pDM_Odm->Adapter;
	return PHY_QueryBBReg(Adapter, RegAddr, BitMask);
#endif	
}


VOID
ODM_SetRFReg(	
	IN 	PDM_ODM_T			pDM_Odm,
	IN	ODM_RF_RADIO_PATH_E	eRFPath,
	IN	u4Byte				RegAddr,
	IN	u4Byte				BitMask,
	IN	u4Byte				Data
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	PHY_SetRFReg(pDM_Odm->priv, eRFPath, RegAddr, BitMask, Data);
#elif(DM_ODM_SUPPORT_TYPE & (ODM_CE|ODM_WIN))
	PADAPTER		Adapter = pDM_Odm->Adapter;
	PHY_SetRFReg(Adapter, eRFPath, RegAddr, BitMask, Data);
#endif	
}


u4Byte 
ODM_GetRFReg(	
	IN 	PDM_ODM_T			pDM_Odm,
	IN	ODM_RF_RADIO_PATH_E	eRFPath,
	IN	u4Byte				RegAddr,
	IN	u4Byte				BitMask
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	return PHY_QueryRFReg(pDM_Odm->priv, eRFPath, RegAddr, BitMask, 1);
#elif(DM_ODM_SUPPORT_TYPE & (ODM_CE|ODM_WIN))
	PADAPTER		Adapter = pDM_Odm->Adapter;
	return PHY_QueryRFReg(Adapter, eRFPath, RegAddr, BitMask);
#endif	
}




//
// ODM Memory relative API.
//
VOID
ODM_AllocateMemory(	
	IN 	PDM_ODM_T	pDM_Odm,
	OUT	PVOID		*pPtr,
	IN	u4Byte		length
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	*pPtr = kmalloc(length, GFP_ATOMIC);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE )
	*pPtr = rtw_zvmalloc(length);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	PlatformAllocateMemory(Adapter, pPtr, length);
#endif	
}

// length could be ignored, used to detect memory leakage.
VOID
ODM_FreeMemory(	
	IN 	PDM_ODM_T	pDM_Odm,
	OUT	PVOID		pPtr,
	IN	u4Byte		length
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	kfree(pPtr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE )	
	rtw_vmfree(pPtr, length);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	//PADAPTER    Adapter = pDM_Odm->Adapter;
	PlatformFreeMemory(pPtr, length);
#endif	
}

VOID
ODM_MoveMemory(	
	IN 	PDM_ODM_T	pDM_Odm,
	OUT PVOID		pDest,
	IN  PVOID		pSrc,
	IN  u4Byte		Length
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE )	
	_rtw_memcpy(pDest, pSrc, Length);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformMoveMemory(pDest, pSrc, Length);
#endif	
}

s4Byte ODM_CompareMemory(
	IN 	PDM_ODM_T		pDM_Odm,
	IN	PVOID           pBuf1,
	IN	PVOID           pBuf2,
	IN	u4Byte          length
       )
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	return memcmp(pBuf1,pBuf2,length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE )	
	return _rtw_memcmp(pBuf1,pBuf2,length);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)	
	return PlatformCompareMemory(pBuf1,pBuf2,length);
#endif	
}



//
// ODM MISC relative API.
//
VOID
ODM_AcquireSpinLock(	
	IN 	PDM_ODM_T			pDM_Odm,
	IN	RT_SPINLOCK_TYPE	type
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	PlatformAcquireSpinLock(Adapter, type);
#endif	
}
VOID
ODM_ReleaseSpinLock(	
	IN 	PDM_ODM_T			pDM_Odm,
	IN	RT_SPINLOCK_TYPE	type
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE )
	
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	PlatformReleaseSpinLock(Adapter, type);
#endif	
}

//
// Work item relative API. FOr MP driver only~!
//
VOID
ODM_InitializeWorkItem(	
	IN 	PDM_ODM_T					pDM_Odm,
	IN	PRT_WORK_ITEM				pRtWorkItem,
	IN	RT_WORKITEM_CALL_BACK		RtWorkItemCallback,
	IN	PVOID						pContext,
	IN	const char*					szID
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	PlatformInitializeWorkItem(Adapter, pRtWorkItem, RtWorkItemCallback, pContext, szID);
#endif	
}


VOID
ODM_StartWorkItem(	
	IN	PRT_WORK_ITEM	pRtWorkItem
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformStartWorkItem(pRtWorkItem);
#endif	
}


VOID
ODM_StopWorkItem(	
	IN	PRT_WORK_ITEM	pRtWorkItem
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformStopWorkItem(pRtWorkItem);
#endif	
}


VOID
ODM_FreeWorkItem(	
	IN	PRT_WORK_ITEM	pRtWorkItem
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformFreeWorkItem(pRtWorkItem);
#endif	
}


VOID
ODM_ScheduleWorkItem(	
	IN	PRT_WORK_ITEM	pRtWorkItem
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformScheduleWorkItem(pRtWorkItem);
#endif	
}


VOID
ODM_IsWorkItemScheduled(	
	IN	PRT_WORK_ITEM	pRtWorkItem
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformIsWorkItemScheduled(pRtWorkItem);
#endif	
}



//
// ODM Timer relative API.
//
VOID
ODM_StallExecution(	
	IN	u4Byte	usDelay
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	rtw_udelay_os(usDelay);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformStallExecution(usDelay);
#endif	
}

VOID
ODM_delay_ms(IN u4Byte	ms)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	delay_ms(ms);
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	rtw_mdelay_os(ms);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	delay_ms(ms);
#endif			
}

VOID
ODM_delay_us(IN u4Byte	us)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	delay_us(us);
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	rtw_udelay_os(us);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformStallExecution(us);
#endif			
}

VOID
ODM_sleep_ms(IN u4Byte	ms)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	rtw_msleep_os(ms);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)	
#endif		
}

VOID
ODM_sleep_us(IN u4Byte	us)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	rtw_usleep_os(us);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)	
#endif		
}

VOID
ODM_SetTimer(	
	IN 	PDM_ODM_T		pDM_Odm,
	IN	PRT_TIMER 		pTimer, 
	IN	u4Byte 			msDelay
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	mod_timer(pTimer, jiffies + (msDelay+9)/10);	
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	_set_timer(pTimer,msDelay ); //ms
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	PlatformSetTimer(Adapter, pTimer, msDelay);
#endif	

}

VOID
ODM_InitializeTimer(
	IN 	PDM_ODM_T			pDM_Odm,
	IN	PRT_TIMER 			pTimer, 
	IN	RT_TIMER_CALL_BACK	CallBackFunc, 
	IN	PVOID				pContext,
	IN	const char*			szID
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	pTimer->function = CallBackFunc;
	pTimer->data = (unsigned long)pDM_Odm;
	init_timer(pTimer);	
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	PADAPTER Adapter = pDM_Odm->Adapter;
	_init_timer(pTimer,Adapter->pnetdev,CallBackFunc,pDM_Odm);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PADAPTER Adapter = pDM_Odm->Adapter;
	PlatformInitializeTimer(Adapter, pTimer, CallBackFunc,pContext,szID);
#endif	
}


VOID
ODM_CancelTimer(
	IN 	PDM_ODM_T		pDM_Odm,
	IN	PRT_TIMER		pTimer
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	del_timer_sync(pTimer);
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	_cancel_timer_ex(pTimer);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PADAPTER Adapter = pDM_Odm->Adapter;
	PlatformCancelTimer(Adapter, pTimer);
#endif
}


VOID
ODM_ReleaseTimer(
	IN 	PDM_ODM_T		pDM_Odm,
	IN	PRT_TIMER		pTimer
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))

#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)

	PADAPTER Adapter = pDM_Odm->Adapter;

    // <20120301, Kordan> If the initilization fails, InitializeAdapterXxx will return regardless of InitHalDm. 
    // Hence, uninitialized timers cause BSOD when the driver releases resources since the init fail.
    if (pTimer == 0) 
    {
        ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_SERIOUS, ("=====>ODM_ReleaseTimer(), The timer is NULL! Please check it!\n"));
        return;
    }
        
	PlatformReleaseTimer(Adapter, pTimer);
#endif
}


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
)
{
	if(IS_HARDWARE_TYPE_JAGUAR(Adapter))
	{
		switch(ElementID)
		{
		case ODM_H2C_RSSI_REPORT:
			FillH2CCmd8812(Adapter, H2C_8812_RSSI_REPORT, CmdLen, pCmdBuffer);
			break;
		default:
			break;
		}

	}
	else if(IS_HARDWARE_TYPE_8192E(Adapter))
	{
		switch(ElementID)
		{
		case ODM_H2C_RSSI_REPORT:
			FillH2CCmd8812(Adapter, H2C_8812_RSSI_REPORT, CmdLen, pCmdBuffer);
			break;
		default:
			break;
		}	
	}
	else if(IS_HARDWARE_TYPE_8723B(Adapter))
	{
		//
		// <Roger_TODO> We should take RTL8723B into consideration, 2012.10.08
		//
		switch(ElementID)
		{
			case ODM_H2C_RSSI_REPORT:
			   FillH2CCmd8723B(Adapter, H2C_8723B_RSSI_REPORT, CmdLen, pCmdBuffer);
			   break;
			   
			default:
			   break;			   
		}

	}
	else if(IS_HARDWARE_TYPE_8188E(Adapter))
	{
		switch(ElementID)
		{
		case ODM_H2C_PSD_RESULT:
			FillH2CCmd88E(Adapter, H2C_88E_PSD_RESULT, CmdLen, pCmdBuffer);
			break;
		case ODM_H2C_RSSI_REPORT:
			if(IS_VENDOR_8188E_I_CUT_SERIES(Adapter))
				FillH2CCmd88E(Adapter, H2C_88E_RSSI_REPORT, CmdLen, pCmdBuffer);
			break;
		default:
			break;
		}
	}
	else
	{
		switch(ElementID)
		{
		case ODM_H2C_RSSI_REPORT:
			FillH2CCmd92C(Adapter, H2C_RSSI_REPORT, CmdLen, pCmdBuffer);
			break;
		case ODM_H2C_PSD_RESULT:
			FillH2CCmd92C(Adapter, H2C_92C_PSD_RESULT, CmdLen, pCmdBuffer);
			break;
		default:
			break;
		}
	}
}
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
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)	
	//FillH2CCmd(pH2CBuffer, H2CBufferLen, CmdNum, pElementID, pCmdLen, pCmbBuffer, CmdStartSeq);
	return	FALSE;
#endif

	return	TRUE;
}
#endif


u4Byte
ODM_GetCurrentTime(	
	IN 	PDM_ODM_T		pDM_Odm
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	return  0;
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	return rtw_get_current_time();
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)	
	return  0;
#endif
}

s4Byte
ODM_GetProgressingTime(	
	IN 	PDM_ODM_T		pDM_Odm,
	IN	u4Byte			Start_Time
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	return  0;
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	return rtw_get_passing_time_ms(Start_Time);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)	
	return  0;
#endif
}


