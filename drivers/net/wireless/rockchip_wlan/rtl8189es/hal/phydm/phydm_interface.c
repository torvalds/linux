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

#include "mp_precomp.h"
#include "phydm_precomp.h"

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
	return PHY_QueryBBReg(pDM_Odm->priv, RegAddr, BitMask);
#elif(DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	return PHY_QueryMacReg(pDM_Odm->Adapter, RegAddr, BitMask);
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
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	memcpy(pDest, pSrc, Length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE )	
	_rtw_memcpy(pDest, pSrc, Length);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformMoveMemory(pDest, pSrc, Length);
#endif	
}

void ODM_Memory_Set(
	IN	PDM_ODM_T	pDM_Odm,
	IN	PVOID		pbuf,
	IN	s1Byte		value,
	IN	u4Byte		length
)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	memset(pbuf, value, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE )	
	_rtw_memset(pbuf,value, length);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformFillMemory(pbuf,length,value);
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
	PADAPTER Adapter = pDM_Odm->Adapter;
	rtw_odm_acquirespinlock(Adapter, type);
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
	PADAPTER Adapter = pDM_Odm->Adapter;
	rtw_odm_releasespinlock(Adapter, type);
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
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	mod_timer(pTimer, jiffies + RTL_MILISECONDS_TO_JIFFIES(msDelay));
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
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	init_timer(pTimer);
	pTimer->function = CallBackFunc;
	pTimer->data = (unsigned long)pDM_Odm;
	mod_timer(pTimer, jiffies+RTL_MILISECONDS_TO_JIFFIES(10));	
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


u1Byte
phydm_trans_h2c_id(
	IN	PDM_ODM_T	pDM_Odm,
	IN	u1Byte		phydm_h2c_id
)
{
	u1Byte platform_h2c_id=0xff;

	
	switch(phydm_h2c_id)
	{
		//1 [0]
		case ODM_H2C_RSSI_REPORT:

			#if(DM_ODM_SUPPORT_TYPE & ODM_WIN)
				if(pDM_Odm->SupportICType == ODM_RTL8188E)
				{
					platform_h2c_id = H2C_88E_RSSI_REPORT;
				}
				else if(pDM_Odm->SupportICType == ODM_RTL8814A)
				{
					platform_h2c_id =H2C_8814A_RSSI_REPORT;                            
				}
				else
				{
					platform_h2c_id = H2C_RSSI_REPORT;
				}
				
			#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
				#if((RTL8812A_SUPPORT==1) ||(RTL8821A_SUPPORT==1))
					platform_h2c_id = H2C_8812_RSSI_REPORT;
				#elif(RTL8814A_SUPPORT == 1)
					platform_h2c_id = H2C_RSSI_SETTING;
				#elif(RTL8192E_SUPPORT==1)
					platform_h2c_id =H2C_8192E_RSSI_REPORT;
				#elif(RTL8723B_SUPPORT==1)
					platform_h2c_id =H2C_8723B_RSSI_SETTING;
				#elif(RTL8188E_SUPPORT==1)
					platform_h2c_id =H2C_RSSI_REPORT;
				#elif(RTL8723A_SUPPORT==1)
					platform_h2c_id =RSSI_SETTING_EID;
				#elif(RTL8192D_SUPPORT==1)
					platform_h2c_id =H2C_RSSI_REPORT;
				#elif(RTL8192C_SUPPORT==1)
					platform_h2c_id =RSSI_SETTING_EID;
				#endif
				
			#elif(DM_ODM_SUPPORT_TYPE & ODM_AP)
				#if((RTL8881A_SUPPORT==1)||(RTL8192E_SUPPORT==1)||(RTL8814A_SUPPORT==1) )
					if(pDM_Odm->SupportICType == ODM_RTL8881A || pDM_Odm->SupportICType == ODM_RTL8192E|| pDM_Odm->SupportICType == ODM_RTL8814A) 
					{
						platform_h2c_id =H2C_88XX_RSSI_REPORT;				
						//ODM_RT_TRACE(pDM_Odm,PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("[H2C] H2C_88XX_RSSI_REPORT CMD_ID = (( %d )) \n", platform_h2c_id));
					} else
				#endif
				#if(RTL8812A_SUPPORT==1) 
					if(pDM_Odm->SupportICType == ODM_RTL8812)
					{
						platform_h2c_id = H2C_8812_RSSI_REPORT;
					} else
				#endif				
					{}
			#endif
			
				break;

		//1 [3]	
		case ODM_H2C_WIFI_CALIBRATION:
			#if(DM_ODM_SUPPORT_TYPE & ODM_WIN)
					platform_h2c_id =H2C_WIFI_CALIBRATION;
			
			#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
				#if(RTL8723B_SUPPORT==1) 
					platform_h2c_id = H2C_8723B_BT_WLAN_CALIBRATION;
				#endif
				
			#elif(DM_ODM_SUPPORT_TYPE & ODM_AP)

			
			#endif
			
				break;		
	
			
		//1 [4]
		case ODM_H2C_IQ_CALIBRATION:
			#if(DM_ODM_SUPPORT_TYPE & ODM_WIN)
				platform_h2c_id =H2C_IQ_CALIBRATION;
			
			#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
				#if((RTL8812A_SUPPORT==1) ||(RTL8821A_SUPPORT==1))
				platform_h2c_id = H2C_8812_IQ_CALIBRATION;
				#endif
			#elif(DM_ODM_SUPPORT_TYPE & ODM_AP)

			
			#endif
			
				break;
		//1 [5]
		case ODM_H2C_RA_PARA_ADJUST:

			#if(DM_ODM_SUPPORT_TYPE & ODM_WIN)
				if(pDM_Odm->SupportICType == ODM_RTL8814A)
				{
					platform_h2c_id =H2C_8814A_RA_PARA_ADJUST;                            
				}
				else
				{
				platform_h2c_id = H2C_RA_PARA_ADJUST;
				}
			#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
				#if((RTL8812A_SUPPORT==1) ||(RTL8821A_SUPPORT==1))
					platform_h2c_id = H2C_8812_RA_PARA_ADJUST;
				#elif(RTL8814A_SUPPORT == 1)
					platform_h2c_id = H2C_RA_PARA_ADJUST;
				#elif(RTL8192E_SUPPORT==1)
					platform_h2c_id =H2C_8192E_RA_PARA_ADJUST;
				#elif(RTL8723B_SUPPORT==1) 
					platform_h2c_id =H2C_8723B_RA_PARA_ADJUST;
				#endif
				
			#elif(DM_ODM_SUPPORT_TYPE & ODM_AP)
				#if((RTL8881A_SUPPORT==1)||(RTL8192E_SUPPORT==1)||(RTL8814A_SUPPORT==1)) 
					if (pDM_Odm->SupportICType == ODM_RTL8881A || pDM_Odm->SupportICType == ODM_RTL8192E || pDM_Odm->SupportICType == ODM_RTL8814A) 
					{
						platform_h2c_id =H2C_88XX_RA_PARA_ADJUST;				
						/*ODM_RT_TRACE(pDM_Odm,PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("[H2C] H2C_88XX_RA_PARA_ADJUST CMD_ID = (( %d ))\n", platform_h2c_id));*/
					} else
				#endif
				#if(RTL8812A_SUPPORT==1) 
					if(pDM_Odm->SupportICType == ODM_RTL8812)
					{
						platform_h2c_id = H2C_8812_RA_PARA_ADJUST;
					} else
				#endif
					{}
			#endif
			
				break;


		//1 [6]
		case PHYDM_H2C_DYNAMIC_TX_PATH:

			#if(DM_ODM_SUPPORT_TYPE & ODM_WIN)
				if(pDM_Odm->SupportICType == ODM_RTL8814A)
				{
					platform_h2c_id =H2C_8814A_DYNAMIC_TX_PATH;
				}
			#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
				#if (RTL8814A_SUPPORT == 1)
				if (pDM_Odm->SupportICType == ODM_RTL8814A)
					platform_h2c_id = H2C_DYNAMIC_TX_PATH;
				#endif
			#elif(DM_ODM_SUPPORT_TYPE & ODM_AP)
				#if(RTL8814A_SUPPORT==1)
					if( pDM_Odm->SupportICType == ODM_RTL8814A)
					{
						platform_h2c_id = H2C_88XX_DYNAMIC_TX_PATH;				
					} 
				#endif

			#endif
			
				break;

		/* [7]*/
		case PHYDM_H2C_FW_TRACE_EN:

			#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
				if (pDM_Odm->SupportICType == ODM_RTL8814A)
					platform_h2c_id = H2C_8814A_FW_TRACE_EN;
				else 
					platform_h2c_id = H2C_FW_TRACE_EN;
				
			#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)

				
			#elif(DM_ODM_SUPPORT_TYPE & ODM_AP)
				#if ((RTL8881A_SUPPORT == 1) || (RTL8192E_SUPPORT == 1) || (RTL8814A_SUPPORT == 1))
					if (pDM_Odm->SupportICType == ODM_RTL8881A || pDM_Odm->SupportICType == ODM_RTL8192E || pDM_Odm->SupportICType == ODM_RTL8814A) {
						platform_h2c_id  = H2C_88XX_FW_TRACE_EN;
					} else
				#endif
				#if (RTL8812A_SUPPORT == 1) 
					if (pDM_Odm->SupportICType == ODM_RTL8812) {
						platform_h2c_id = H2C_8812_FW_TRACE_EN;
					} else
				#endif
					{}

			#endif
			
				break;

		default:
			platform_h2c_id=0xff;
			break;	
	}	
	
	return platform_h2c_id;
	
}

//
// ODM FW relative API.
//

VOID
ODM_FillH2CCmd(
	IN	PDM_ODM_T		pDM_Odm,
	IN	u1Byte 			phydm_h2c_id,
	IN	u4Byte 			CmdLen,
	IN	pu1Byte			pCmdBuffer
)
{
	PADAPTER 	Adapter = pDM_Odm->Adapter;
	u1Byte		platform_h2c_id;

	platform_h2c_id=phydm_trans_h2c_id(pDM_Odm, phydm_h2c_id);

	if(platform_h2c_id==0xff)
	{
		ODM_RT_TRACE(pDM_Odm,PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("[H2C] Wrong H2C CMD-ID !! platform_h2c_id==0xff ,  PHYDM_ElementID=((%d )) \n",phydm_h2c_id));
		return;
	}

	#if(DM_ODM_SUPPORT_TYPE & ODM_WIN)
		if(pDM_Odm->SupportICType == ODM_RTL8188E)
		{
			if(!pDM_Odm->RaSupport88E)
				FillH2CCmd88E(Adapter, platform_h2c_id, CmdLen, pCmdBuffer);
		}
		else if(pDM_Odm->SupportICType == ODM_RTL8192C)
		{
			FillH2CCmd92C(Adapter, platform_h2c_id, CmdLen, pCmdBuffer);
		}
		else if(pDM_Odm->SupportICType == ODM_RTL8814A)
		{
			FillH2CCmd8814A(Adapter, platform_h2c_id, CmdLen, pCmdBuffer);
		}
		else
		{		
			FillH2CCmd(Adapter, platform_h2c_id, CmdLen, pCmdBuffer);
		}
	#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
		rtw_hal_fill_h2c_cmd(Adapter, platform_h2c_id, CmdLen, pCmdBuffer);

	#elif(DM_ODM_SUPPORT_TYPE & ODM_AP)	
		#if((RTL8881A_SUPPORT==1)||(RTL8192E_SUPPORT==1)||(RTL8814A_SUPPORT==1)) 
			if(pDM_Odm->SupportICType == ODM_RTL8881A || pDM_Odm->SupportICType == ODM_RTL8192E|| pDM_Odm->SupportICType == ODM_RTL8814A) 
			{
				GET_HAL_INTERFACE(pDM_Odm->priv)->FillH2CCmdHandler(pDM_Odm->priv, platform_h2c_id, CmdLen, pCmdBuffer);
				//FillH2CCmd88XX(pDM_Odm->priv, platform_h2c_id, CmdLen, pCmdBuffer);				
			} else
		#endif
		#if(RTL8812A_SUPPORT==1) 
			if(pDM_Odm->SupportICType == ODM_RTL8812)
			{
				FillH2CCmd8812(pDM_Odm->priv, platform_h2c_id, CmdLen, pCmdBuffer);
			} else
		#endif
			{}
	#endif
}

u8Byte
ODM_GetCurrentTime(	
	IN 	PDM_ODM_T		pDM_Odm
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	return  0;
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	return (u8Byte)rtw_get_current_time();
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)	
	return  PlatformGetCurrentTime();
#endif
}

u8Byte
ODM_GetProgressingTime(	
	IN 	PDM_ODM_T		pDM_Odm,
	IN	u8Byte			Start_Time
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	return  0;
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	return rtw_get_passing_time_ms((u4Byte)Start_Time);
#elif(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	return   ((PlatformGetCurrentTime() - Start_Time)>>10);
#endif
}


