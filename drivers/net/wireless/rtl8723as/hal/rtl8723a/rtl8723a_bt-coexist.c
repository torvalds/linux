/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
#include <drv_types.h>
#include <rtl8723a_hal.h>


#ifdef bEnable
#undef bEnable
#endif

//#define BT_DEBUG

#define CHECK_BT_EXIST_FROM_REG

#ifdef BT_DEBUG

#ifdef PLATFORM_LINUX
#define RTPRINT(a,b,c) printk c
#define RTPRINT_ADDR(dbgtype, dbgflag, printstr, _Ptr)\
{\
	u32 __i;						\
	u8 *ptr = (u8*)_Ptr;	\
	printk printstr;				\
	printk(" ");					\
	for( __i=0; __i<6; __i++ )		\
		printk("%02X%s", ptr[__i], (__i==5)?"":"-");		\
	printk("\n");							\
}
#define RTPRINT_DATA(dbgtype, dbgflag, _TitleString, _HexData, _HexDataLen)\
{\
	u32 __i;									\
	u8 *ptr = (u8*)_HexData;			\
	printk(_TitleString);					\
	for( __i=0; __i<(u32)_HexDataLen; __i++ )	\
	{										\
		printk("%02X%s", ptr[__i], (((__i + 1) % 4) == 0)?"  ":" ");\
		if (((__i + 1) % 16) == 0)	printk("\n");\
	}										\
	printk("\n");							\
}
// Added by Annie, 2005-11-22.
#define MAX_STR_LEN	64
#define PRINTABLE(_ch)	(_ch>=' ' &&_ch<='~')	// I want to see ASCII 33 to 126 only. Otherwise, I print '?'. Annie, 2005-11-22.
#define RT_PRINT_STR(_Comp, _Level, _TitleString, _Ptr, _Len)					\
{\
/*	if (((_Comp) & GlobalDebugComponents) && (_Level <= GlobalDebugLevel))	*/\
	{									\
		u32 __i;						\
		u8 buffer[MAX_STR_LEN];					\
		u32	length = (_Len<MAX_STR_LEN)? _Len : (MAX_STR_LEN-1) ;	\
		_rtw_memset(buffer, 0, MAX_STR_LEN);			\
		_rtw_memcpy(buffer, (u8*)_Ptr, length);		\
		for (__i=0; __i<length; __i++)					\
		{								\
			if (!PRINTABLE(buffer[__i]))	buffer[__i] = '?';	\
		}								\
		buffer[length] = '\0';						\
		printk("Rtl819x: ");						\
		printk(_TitleString);						\
		printk(": %d, <%s>\n", _Len, buffer);				\
	}\
}
#endif // PLATFORM_LINUX

#else // !BT_DEBUG

#define RTPRINT(...)
#define RTPRINT_ADDR(...)
#define RTPRINT_DATA(...)
#define RT_PRINT_STR(...)

#endif // !BT_DEBUG

#define DCMD_Printf(...)
#define RT_ASSERT(...)

#ifdef PLATFORM_LINUX
#define rsprintf snprintf
#elif defined(PLATFORM_WINDOWS)
#define rsprintf sprintf_s
#endif

#define GET_BT_INFO(padapter)	(&GET_HAL_DATA(padapter)->BtInfo)

#define GetDefaultAdapter(padapter)	padapter

#define PlatformZeroMemory(ptr, sz)	_rtw_memset(ptr, 0, sz)

#ifdef PLATFORM_LINUX
#define PlatformProcessHCICommands(...)
#define PlatformTxBTQueuedPackets(...)
#define PlatformIndicateBTACLData(...)	(RT_STATUS_SUCCESS)
#endif
#define PlatformAcquireSpinLock(padapter, type)
#define PlatformReleaseSpinLock(padapter, type)

// timer
#define PlatformInitializeTimer(padapter, ptimer, pfunc, cntx, szID) \
	_init_timer(ptimer, padapter->pnetdev, pfunc, padapter)
#define PlatformSetTimer(a, ptimer, delay)	_set_timer(ptimer, delay)
static u8 PlatformCancelTimer(PADAPTER a, _timer *ptimer)
{
	u8 bcancelled;
	_cancel_timer(ptimer, &bcancelled);
	return bcancelled;
}
#define PlatformReleaseTimer(...)

// workitem
// already define in hal/OUTSRC/odm_interface.h
//typedef void (*RT_WORKITEM_CALL_BACK)(void *pContext);
#define PlatformInitializeWorkItem(padapter, pwi, pfunc, cntx, szID) \
	_init_workitem(pwi, pfunc, padapter)
#define PlatformFreeWorkItem(...)
#define PlatformScheduleWorkItem(pwork) _set_workitem(pwork)
#if 0
#define GET_UNDECORATED_AVERAGE_RSSI(padapter)	\
		(check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE|WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == _TRUE) ?		\
			(GET_HAL_DATA(padapter)->dmpriv.EntryMinUndecoratedSmoothedPWDB):	\
			(GET_HAL_DATA(padapter)->dmpriv.UndecoratedSmoothedPWDB)
#else
#define GET_UNDECORATED_AVERAGE_RSSI(padapter)	\
			(GET_HAL_DATA(padapter)->dmpriv.EntryMinUndecoratedSmoothedPWDB)
#endif
#define RT_RF_CHANGE_SOURCE u32

typedef enum _RT_JOIN_ACTION{
	RT_JOIN_INFRA   = 1,
	RT_JOIN_IBSS  = 2,
	RT_START_IBSS = 3,
	RT_NO_ACTION  = 4,
} RT_JOIN_ACTION;

// power saving
#ifdef CONFIG_IPS
#define IPSReturn(padapter, b)		ips_enter(padapter)
#define IPSDisable(padapter, b, c)	ips_leave(padapter)
#else
#define IPSReturn(...)
#define IPSDisable(...)
#endif
#ifdef CONFIG_LPS
#define LeisurePSLeave(padapter, b)	LPS_Leave(padapter)
#else
#define LeisurePSLeave(...)
#endif

#ifdef __BT_C__ // COMMOM/BT.c
// ===== Below this line is sync from SD7 driver COMMOM/BT.c =====

u8 BT_Operation(PADAPTER padapter)
{
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->BtOperationOn)
		return _TRUE;
	else
		return _FALSE;
}

u8 BT_IsLegalChannel(PADAPTER padapter, u8 channel)
{
	PRT_CHANNEL_INFO pChanneList = NULL;
	u8 channelLen, i;


	pChanneList = padapter->mlmeextpriv.channel_set;
	channelLen = padapter->mlmeextpriv.max_chan_nums;

	for (i = 0; i < channelLen; i++)
	{
		RTPRINT(FIOCTL, IOCTL_STATE, ("Check if chnl(%d) in channel plan contains bt target chnl(%d) for BT connection\n", pChanneList[i].ChannelNum, channel));
		if ((channel == pChanneList[i].ChannelNum) ||
			(channel == pChanneList[i].ChannelNum + 2))
		{
			return channel;
		}
	}
	return 0;
}

void BT_WifiScanNotify(PADAPTER padapter, u8 scanType)
{
	BTHCI_WifiScanNotify(padapter, scanType);
	BTDM_CheckAntSelMode(padapter);
	BTDM_WifiScanNotify(padapter, scanType);
}

void BT_WifiAssociateNotify(PADAPTER padapter, u8 action)
{
	// action :
	// TRUE = associate start
	// FALSE = associate finished
	if (action)
		BTDM_CheckAntSelMode(padapter);

	BTDM_WifiAssociateNotify(padapter, action);
}

// ===== End of sync from SD7 driver COMMOM/BT.c =====
#endif

#ifdef __BT_HANDLEPACKET_C__ // COMMOM/bt_handlepacket.c
// ===== Below this line is sync from SD7 driver COMMOM/bt_handlepacket.c =====

void btpkt_SendBeacon(PADAPTER padapter)
{
#if 0 // not implement yet
	PRT_TCB					pTcb;
	PRT_TX_LOCAL_BUFFER 	pBuf;

	PlatformAcquireSpinLock(padapter, RT_TX_SPINLOCK);

	if (MgntGetBuffer(padapter, &pTcb, &pBuf))
	{
		btpkt_ConstructBeaconFrame(
			padapter,
			pBuf->Buffer.VirtualAddress,
			&pTcb->PacketLength);

		MgntSendPacket(padapter, pTcb, pBuf, pTcb->PacketLength, NORMAL_QUEUE, MGN_1M);

	}

	PlatformReleaseSpinLock(padapter, RT_TX_SPINLOCK);
#endif
}

void BTPKT_WPAAuthINITIALIZE(PADAPTER padapter, u8 EntryNum)
{
#if 0
//	PMGNT_INFO 	pMgntInfo = &padapter->MgntInfo;
	PBT30Info			pBTinfo = GET_BT_INFO(padapter);
	PBT_SECURITY		pBtSec = &pBTinfo->BtSec;
	PBT_DBG				pBtDbg = &pBTinfo->BtDbg;
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(padapter);

	RTPRINT(FIOCTL, IOCTL_STATE, ("BTPKT_WPAAuthINITIALIZE() EntryNum = %d\n",EntryNum));

	if (pHalData->bBTMode)
	{
//		if (padapter->MgntInfo.OpMode == RT_OP_MODE_IBSS)
		if (check_fwstate(&padapter->mlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == _TRUE)
		{
			pBtSec->bUsedHwEncrypt = _FALSE;
		}
		else
		{
			pBtSec->bUsedHwEncrypt = _TRUE;
		}
	}
	else
		pBtSec->bUsedHwEncrypt = _FALSE;

	pBTinfo->BtAsocEntry[EntryNum].WPAAuthReplayCount = 0;

	if (pBTinfo->BtAsocEntry[EntryNum].AMPRole == AMP_BTAP_CREATOR)
	{
		u8			RdmBuf[20], NonceBuf[KEY_NONCE_LEN];
		u8			index;
		u64			KeyReplayCounter = 0;
		u8			temp[8] = {0};

		// Gene Creator Nonce
		GetRandomBuffer(RdmBuf);
		for (index = 0; index < 16; index++)
		{
			NonceBuf[index] = RdmBuf[index];
			NonceBuf[16+index] = RdmBuf[19-index];
		}
		_rtw_memcpy(pBTinfo->BtAsocEntry[EntryNum].ANonce, NonceBuf, KEY_NONCE_LEN);

		// Set ReplayCounter
		pBTinfo->BtAsocEntry[EntryNum].KeyReplayCounter ++;

		for( index = 0 ; index < 8 ; index++)
			temp[index] =  (u8)((pBTinfo->BtAsocEntry[EntryNum].KeyReplayCounter >>( (7-index) *8)) &0xff);

		_rtw_memcpy(&KeyReplayCounter, temp, 8);
		RTPRINT(FIOCTL, IOCTL_STATE, ("[BT pkt], 4-way packet, send 1st and wait for 2nd pkt\n"));

		pBtDbg->dbgBtPkt.btPktTx4way1st++;

		// Send 1st packet of 4-way
		btpkt_SendEapolKeyPacket(
						padapter,
						pBTinfo->BtAsocEntry[EntryNum].BTRemoteMACAddr, //Sta MAC address
						NULL, // Pointer to KCK (EAPOL-Key Confirmation Key).
						NULL, //
						type_Pairwise, // EAPOL-Key Information field: Key Type bit: type_Group or type_Pairwise.
						_FALSE, // EAPOL-Key Information field: Install Flag.
						_TRUE, // EAPOL-Key Information field: Key Ack bit.
						_FALSE, // EAPOL-Key Information field: Key MIC bit. If true, we will calculate EAPOL MIC and fill it into Key MIC field.
						_FALSE, // EAPOL-Key Information field: Secure bit.
						_FALSE, // EAPOL-Key Information field: Error bit. True for MIC failure report.
						_FALSE, // EAPOL-Key Information field: Requst bit.
						KeyReplayCounter, // EAPOL-KEY Replay Counter field.  //pSTA->perSTAKeyInfo.KeyReplayCounter
						pBTinfo->BtAsocEntry[EntryNum].ANonce, // EAPOL-Key Key Nonce field (32-byte).
						0, // EAPOL-Key Key RSC field (8-byte).
						NULL, // Key Data field: Pointer to RSN IE, NULL if
						NULL, // Key Data field: Pointer to GTK, NULL if Key Data Length = 0.
						EntryNum
						);
		pBTinfo->BtAsocEntry[EntryNum].BTWPAAuthState = STATE_WPA_AUTH_WAIT_PACKET_2;

		PlatformSetTimer(padapter, &pBtSec->BTWPAAuthTimer , BT_WPA_AUTH_TIMEOUT_PERIOD);
		// Set WPA Auth State

		RTPRINT(FIOCTL, IOCTL_STATE, ("Initial BT WPA Creat mode  successful !!\n"));
	}
	else if (pBTinfo->BtAsocEntry[EntryNum].AMPRole == AMP_BTAP_JOINER)
	{
		RTPRINT(FIOCTL, IOCTL_STATE, ("BT Joiner BTPKT_WPAAuthINITIALIZE\n"));
		// Set WPA Auth State
		pBTinfo->BtAsocEntry[EntryNum].BTWPAAuthState = STATE_WPA_AUTH_WAIT_PACKET_1;
		RTPRINT(FIOCTL, IOCTL_STATE, ("Initial BT WPA Joiner mode  successful !!\n"));
	}
	else
	{
		pBTinfo->BtAsocEntry[EntryNum].BTWPAAuthState = STATE_WPA_AUTH_UNINITIALIZED;
		RTPRINT(FIOCTL, IOCTL_STATE, ("=====> BT unknown mode\n"));
	}
#endif
}

void BTPKT_TimerCallbackWPAAuth(PRT_TIMER pTimer)
{
#if 0
//	PADAPTER		padapter = (PADAPTER)pTimer->padapter;
	PADAPTER		padapter = (PADAPTER)pTimer;
	PBT30Info		pBTinfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTinfo->BtMgnt;
	PBT_SECURITY		pBtSec = &pBTinfo->BtSec;
	u8			EntryNum = pBtMgnt->CurrentConnectEntryNum;
	u32			index;


	//
	//	Now we check all BT entry !!
	//
	for (index = 0; index < MAX_BT_ASOC_ENTRY_NUM; index++)
	{
		// Check bUsed
		if (!pBTinfo->BtAsocEntry[index].bUsed)
			continue;

		// Check state
		if (pBTinfo->BtAsocEntry[EntryNum].BTWPAAuthState == STATE_WPA_AUTH_SUCCESSED)
			continue;

		if (pBTinfo->BtAsocEntry[EntryNum].BTWPAAuthState == STATE_WPA_AUTH_UNINITIALIZED)
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("====> BTPKT_TimerCallbackWPAAuth(), BTPKT_WPAAuthINITIALIZE!!\n"));
			BTPKT_WPAAuthINITIALIZE(padapter,EntryNum);
			continue;
		}

		// Add Re-play counter !!
		pBTinfo->BtAsocEntry[EntryNum].WPAAuthReplayCount++;

		if (pBTinfo->BtAsocEntry[EntryNum].WPAAuthReplayCount > BTMaxWPAAuthReTransmitCoun)
		{
			BTHCI_SM_WITH_INFO(padapter,HCI_STATE_AUTHENTICATING,STATE_CMD_4WAY_FAILED,EntryNum);
			RTPRINT(FIOCTL, IOCTL_STATE, ("====> BTPKT_TimerCallbackWPAAuth(), Retry too much times !!\n"));
				continue;
		}
		else if (pBTinfo->BtAsocEntry[EntryNum].BTWPAAuthState == STATE_WPA_AUTH_WAIT_PACKET_1)
		{
			// We may be remove PlatformSetTimer , after check all station !!
			PlatformSetTimer(padapter, &pBtSec->BTWPAAuthTimer, BT_WPA_AUTH_TIMEOUT_PERIOD);
			RTPRINT(FIOCTL, IOCTL_STATE, ("====> Retry STATE_WPA_AUTH_WAIT_PACKET_1 !!\n"));
				continue;
		}
		else if (pBTinfo->BtAsocEntry[EntryNum].BTWPAAuthState == STATE_WPA_AUTH_WAIT_PACKET_2)
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("====> Re-Send 1st of 4-way, STATE_WPA_AUTH_WAIT_PACKET_2 !!\n"));
			// Re-Send 1st of 4-way !!
			{
				u64			KeyReplayCounter = 0;
				u8			temp[8] = {0};
				u8			indexi = 0;
				// Set ReplayCounter
				pBTinfo->BtAsocEntry[EntryNum].KeyReplayCounter ++;

				for (indexi = 0; indexi < 8; indexi++)
					temp[indexi] = (u8)((pBTinfo->BtAsocEntry[EntryNum].KeyReplayCounter >>((7-indexi)*8))&0xff);

				// Send 1st packet of 4-way
				btpkt_SendEapolKeyPacket(
								padapter,
								pBTinfo->BtAsocEntry[EntryNum].BTRemoteMACAddr, //Sta MAC address
								NULL, // Pointer to KCK (EAPOL-Key Confirmation Key).
								NULL, //
								type_Pairwise, // EAPOL-Key Information field: Key Type bit: type_Group or type_Pairwise.
								_FALSE, // EAPOL-Key Information field: Install Flag.
								_TRUE, // EAPOL-Key Information field: Key Ack bit.
								_FALSE, // EAPOL-Key Information field: Key MIC bit. If true, we will calculate EAPOL MIC and fill it into Key MIC field.
								_FALSE, // EAPOL-Key Information field: Secure bit.
								_FALSE, // EAPOL-Key Information field: Error bit. True for MIC failure report.
								_FALSE, // EAPOL-Key Information field: Requst bit.
								KeyReplayCounter, // EAPOL-KEY Replay Counter field.  //pSTA->perSTAKeyInfo.KeyReplayCounter
								pBTinfo->BtAsocEntry[EntryNum].ANonce, // EAPOL-Key Key Nonce field (32-byte).
								0, // EAPOL-Key Key RSC field (8-byte).
								NULL, // Key Data field: Pointer to RSN IE, NULL if
								NULL, // Key Data field: Pointer to GTK, NULL if Key Data Length = 0.
								EntryNum
								);
			}
			// We may be remove PlatformSetTimer BTWPAAuthTimer , after check all station !!
			PlatformSetTimer(padapter, &pBtSec->BTWPAAuthTimer , BT_WPA_AUTH_TIMEOUT_PERIOD);
			//RTPRINT(FIOCTL, IOCTL_STATE, ("====> Re-Send 1st of 4-way, STATE_WPA_AUTH_WAIT_PACKET_2 !!\n"));
			continue;
		}
		else if (pBTinfo->BtAsocEntry[EntryNum].BTWPAAuthState == STATE_WPA_AUTH_WAIT_PACKET_3)
		{
			// We may be remove PlatformSetTimer , after check all station !!
			PlatformSetTimer(padapter, &pBtSec->BTWPAAuthTimer , BT_WPA_AUTH_TIMEOUT_PERIOD);
			RTPRINT(FIOCTL, IOCTL_STATE, ("====> Re-Send 2nd of 4-way, STATE_WPA_AUTH_WAIT_PACKET_3 !!\n"));
			continue;
		}
		else if (pBTinfo->BtAsocEntry[EntryNum].BTWPAAuthState == STATE_WPA_AUTH_WAIT_PACKET_4)
		{
			// Re-Send 3th of 4-way !!
			{
				u64			KeyReplayCounter = 0;
				u8			temp[8] = {0};
				u8			indexi = 0;
				// Set ReplayCounter
				pBTinfo->BtAsocEntry[EntryNum].KeyReplayCounter ++;

				for (indexi = 0; indexi < 8; indexi++)
					temp[indexi] = (u8)((pBTinfo->BtAsocEntry[EntryNum].KeyReplayCounter >> ((7-indexi)*8))&0xff);

				btpkt_SendEapolKeyPacket(
						padapter,
						pBTinfo->BtAsocEntry[EntryNum].BTRemoteMACAddr,
						pBTinfo->BtAsocEntry[EntryNum].PTK, // Pointer to KCK (EAPOL-Key Confirmation Key).
						NULL,//pBTinfo->BtAsocEntry[EntryNum].PTK + 16,
						type_Pairwise, // EAPOL-Key Information field: Key Type bit: type_Group or type_Pairwise.
						_TRUE, // EAPOL-Key Information field: Install Flag.
						_TRUE, // EAPOL-Key Information field: Key Ack bit.
						_TRUE, // EAPOL-Key Information field: Key MIC bit. If true, we will calculate EAPOL MIC and fill it into Key MIC field.
						_TRUE, // EAPOL-Key Information field: Secure bit.
						_FALSE, // EAPOL-Key Information field: Error bit. True for MIC failure report.
						_FALSE, // EAPOL-Key Information field: Requst bit.
						KeyReplayCounter,//pSTA->perSTAKeyInfo.KeyReplayCounter, // EAPOL-KEY Replay Counter field.
						pBTinfo->BtAsocEntry[EntryNum].ANonce, // EAPOL-Key Key Nonce field (32-byte).
						0, // perSTA EAPOL-Key Key RSC field (8-byte).
						&(pBtSec->RSNIE), // Key Data field: Pointer to RSN IE, NULL if
						NULL,//pBTinfo->BtAsocEntry[EntryNum].GTK,  // Key Data field: Pointer to GTK, NULL if Key Data Length = 0.
						EntryNum
						);
			}
			// We may be remove PlatformSetTimer , after check all station !!
			PlatformSetTimer(padapter, &pBtSec->BTWPAAuthTimer, BT_WPA_AUTH_TIMEOUT_PERIOD);
			RTPRINT(FIOCTL, IOCTL_STATE, ("====> Re-Send 3th of 4-way, STATE_WPA_AUTH_WAIT_PACKET_4 !!\n"));
			continue;
		}
		else
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("====> BTPKT_TimerCallbackWPAAuth(), Error State !!%d\n",pBTinfo->BtAsocEntry[EntryNum].BTWPAAuthState ));
			continue;
		}
	}
#endif
}

void BTPKT_TimerCallbackBeacon(PRT_TIMER pTimer)
{
//	PADAPTER	padapter = (PADAPTER)pTimer->padapter;
	PADAPTER	padapter = (PADAPTER)pTimer;
//	PMGNT_INFO	pMgntInfo = &(padapter->MgntInfo);
	PBT30Info		pBTinfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTinfo->BtMgnt;

	RTPRINT(FIOCTL, IOCTL_CALLBACK_FUN, ("=====> BTPKT_TimerCallbackBeacon\n"));
//	if (RT_CANNOT_IO(padapter))
//		return;
	//pMgntInfo->BtInfo.BTBeaconTmrOn = _TRUE;

	if (!pBTinfo->BTBeaconTmrOn)
		return;

	if (pBtMgnt->BtOperationOn)
	{
		RTPRINT(FIOCTL, IOCTL_CALLBACK_FUN, ("btpkt_SendBeacon\n"));
		btpkt_SendBeacon(GetDefaultAdapter(padapter));
		PlatformSetTimer(padapter, &pBTinfo->BTBeaconTimer, 100);
	}
	else
	{
		RTPRINT(FIOCTL, IOCTL_CALLBACK_FUN, ("<===== BTPKT_TimerCallbackBeacon\n"));
	}
}


// ===== End of sync from SD7 driver COMMOM/bt_handlepacket.c =====
#endif

#ifdef __BT_HCI_C__ // COMMOM/bt_hci.c

#define i64fmt		"ll"
#define UINT64_C(v)  (v)

#define FillOctetString(_os,_octet,_len)		\
	(_os).Octet=(u8*)(_octet);			\
	(_os).Length=(_len);

static RT_STATUS PlatformIndicateBTEvent(
	PADAPTER 					padapter,
	void						*pEvntData,
	u32						dataLen
	)
{
	RT_STATUS	rt_status = RT_STATUS_FAILURE;
#ifdef PLATFORM_WINDOWS
	NTSTATUS	nt_status = STATUS_SUCCESS;
	PIRP		pIrp = NULL;
	u32			BytesTransferred = 0;
#endif

	RTPRINT(FIOCTL, IOCTL_BT_EVENT_DETAIL, ("BT event start, %d bytes data to Transferred!!\n", dataLen));
	RTPRINT_DATA(FIOCTL, IOCTL_BT_EVENT_DETAIL, "To transfer Hex Data :\n",
		pEvntData, dataLen);

//	if (pGBTDeviceExtension==NULL || pGBTDeviceExtension->padapter!=padapter)
//		return rt_status;

	BT_EventParse(padapter, pEvntData, dataLen);

#ifdef PLATFORM_LINUX

	printk(KERN_WARNING "%s: Linux has no way to report BT event!!\n", __FUNCTION__);

#elif defined(PLATFORM_WINDOWS)

	pIrp = IOCTL_BtIrpDequeue(pGBTDeviceExtension, IRP_HCI_EVENT_Q);

	if(pIrp)
	{
		PVOID	outbuf;
		ULONG	outlen;
		ULONG	offset;

		outbuf = MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, HighPagePriority);
		if(outbuf == NULL)
		{
			RTPRINT(FIOCTL, IOCTL_IRP, ("PlatformIndicateBTEvent(), error!! MdlAddress = NULL!!\n"));
			BytesTransferred = 0;
			nt_status = STATUS_UNSUCCESSFUL;
		}
		else
		{
			outlen = MmGetMdlByteCount(pIrp->MdlAddress);
			offset = MmGetMdlByteOffset(pIrp->MdlAddress);

			if(dataLen <= outlen)
				BytesTransferred = dataLen;
			else
				BytesTransferred = outlen;
			_rtw_memcpy(outbuf, pEvntData, BytesTransferred);
			nt_status = STATUS_SUCCESS;
		}
		RTPRINT(FIOCTL, IOCTL_BT_EVENT_DETAIL, ("BT event, %d bytes data Transferred!!\n", BytesTransferred));
		RTPRINT_DATA(FIOCTL, (IOCTL_BT_EVENT_DETAIL|IOCTL_BT_LOGO), "BT EVENT Hex Data :\n",
			outbuf, BytesTransferred);

		IOCTL_CompleteSingleIRP(pIrp, nt_status, BytesTransferred);
		if (nt_status == STATUS_SUCCESS)
			rt_status = RT_STATUS_SUCCESS;
	}

#endif // PLATFORM_WINDOWS

	RTPRINT(FIOCTL, IOCTL_BT_EVENT_DETAIL, ("BT event end, %s\n",
		(rt_status == RT_STATUS_SUCCESS)? "SUCCESS":"FAIL"));

	return rt_status;
}

// ===== Below this line is sync from SD7 driver COMMOM/bt_hci.c =====

u8	testPMK[PMK_LEN] = {2,2,3,3,4,4,5,5,6,6,
							7,7,8,8,9,9,2,2,3,3,
							4,4,2,2,8,8,9,9,2,2,
							5,5};

u8 bthci_GetLocalChannel(PADAPTER padapter)
{
	return padapter->mlmeextpriv.cur_channel;
}

u8 bthci_GetCurrentEntryNum(PADAPTER padapter, u8 PhyHandle)
{
	PBT30Info pBTInfo = GET_BT_INFO(padapter);
	u8 i;

	for (i = 0; i < MAX_BT_ASOC_ENTRY_NUM; i++)
	{
		if ((pBTInfo->BtAsocEntry[i].bUsed == _TRUE) && (pBTInfo->BtAsocEntry[i].PhyLinkCmdData.BtPhyLinkhandle == PhyHandle))
		{
			return i;
		}
	}

	return 0xFF;
}

void bthci_DecideBTChannel(PADAPTER padapter, u8 EntryNum)
{
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	struct mlme_priv *pmlmepriv;
	PBT30Info pBTInfo;
	PBT_MGNT pBtMgnt;
	PBT_HCI_INFO pBtHciInfo;
	PCHNL_TXPOWER_TRIPLE pTriple_subband = NULL;
	PCOMMON_TRIPLE pTriple;
	u8 i, j, localchnl, firstRemoteLegalChnlInTriplet=0, regulatory_skipLen=0;
	u8 subbandTripletCnt = 0;


	pmlmepriv = &padapter->mlmepriv;
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;
	pBtHciInfo = &pBTInfo->BtHciInfo;

	pBtMgnt->CheckChnlIsSuit = _TRUE;
	localchnl = bthci_GetLocalChannel(padapter);

	{
#if 0	// for debug only
		pTriple = (PCOMMON_TRIPLE)&(pBtHciInfo->BTPreChnllist[COUNTRY_STR_LEN]);

		// contains country string len is 3
		for (i=0; i<(pBtHciInfo->BtPreChnlListLen-COUNTRY_STR_LEN); i+=3, pTriple++)
		{
			DbgPrint("pTriple->byte_1st = %d, pTriple->byte_2nd = %d, pTriple->byte_3rd = %d\n",
				pTriple->byte_1st, pTriple->byte_2nd, pTriple->byte_3rd);
		}
#endif
		pTriple = (PCOMMON_TRIPLE)&(pBtHciInfo->BTPreChnllist[COUNTRY_STR_LEN]);

		// contains country string, len is 3
		for (i = 0; i < (pBtHciInfo->BtPreChnlListLen-COUNTRY_STR_LEN); i+=3, pTriple++)
		{
			//
			// check every triplet, an triplet may be
			// regulatory extension identifier or sub-band triplet
			//
			if (pTriple->byte_1st == 0xc9)	// Regulatory Extension Identifier, skip it
			{
				RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Find Regulatory ID, regulatory class = %d\n", pTriple->byte_2nd));
				regulatory_skipLen += 3;
				pTriple_subband = NULL;
				continue;
			}
			else							// Sub-band triplet
			{
				RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Find Sub-band triplet \n"));
				subbandTripletCnt++;
				pTriple_subband = (PCHNL_TXPOWER_TRIPLE)pTriple;
				//
				// if remote first legal channel not found, then find first remote channel
				// and it's legal for our channel plan.
				//

				// search the sub-band triplet and find if remote channel is legal to our channel plan.
				for (j = pTriple_subband->FirstChnl; j < (pTriple_subband->FirstChnl+pTriple_subband->NumChnls); j++)
				{
					RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), (" Check if chnl(%d) is legal\n", j));
					if (BT_IsLegalChannel(padapter, j))	// remote channel is legal for our channel plan.
					{
						firstRemoteLegalChnlInTriplet = j;
						RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Find first remote legal channel : %d\n", firstRemoteLegalChnlInTriplet));

						//
						// If we find a remote legal channel in the sub-band triplet
						// and only BT connection is established(local not connect to any AP or IBSS),
						// then we just switch channel to remote channel.
						//
					#if 0
						if (!MgntRoamingInProgress(pMgntInfo) &&
							!MgntIsLinkInProgress(pMgntInfo) &&
							!MgntScanInProgress(pMgntInfo))
					#endif
						{
#if 0
							if (!(pMgntInfo->mAssoc ||
								pMgntInfo->mIbss ||
								IsAPModeExist(padapter)||
								BTHCI_HsConnectionEstablished(padapter)))
#else
							if (!(check_fwstate(pmlmepriv, WIFI_ASOC_STATE|WIFI_ADHOC_STATE|WIFI_AP_STATE) == _TRUE ||
								BTHCI_HsConnectionEstablished(padapter)))
#endif
							{
								pBtMgnt->BTChannel = firstRemoteLegalChnlInTriplet;
								RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Remote legal channel (%d) is selected, Local not connect to any!!\n", pBtMgnt->BTChannel));
								return;
							}
							else
							{
								if ((localchnl >= firstRemoteLegalChnlInTriplet) &&
									(localchnl < (pTriple_subband->FirstChnl+pTriple_subband->NumChnls)))
								{
									pBtMgnt->BTChannel = localchnl;
									RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Local channel (%d) is selected, wifi or BT connection exists\n", pBtMgnt->BTChannel));
									return;
								}
							}
						}
						break;
					}
				}
			}
		}

		if (subbandTripletCnt)
		{
			//if any preferred channel triplet exists
			RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("There are %d sub band triplet exists, ", subbandTripletCnt));
		 	if (firstRemoteLegalChnlInTriplet == 0)
		 	{
		 		//no legal channel is found, reject the connection.
				RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("no legal channel is found!!\n"));
		 	}
			else
			{
				// Remote Legal channel is found but not match to local
				//(wifi connection exists), so reject the connection.
				RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Remote Legal channel is found but not match to local(wifi connection exists)!!\n"));
			}
			pBtMgnt->CheckChnlIsSuit = _FALSE;
		}
		else
		{
			// There are not any preferred channel triplet exists
			// Use current legal channel as the bt channel.
			RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("No sub band triplet exists!!\n"));
		}
		pBtMgnt->BTChannel = localchnl;
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Local channel (%d) is selected!!\n", pBtMgnt->BTChannel));
	}
}


//Success:return _TRUE
//Fail:return _FALSE
u8 bthci_GetAssocInfo(PADAPTER padapter, u8 EntryNum)
{
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo;
	PBT_HCI_INFO	pBtHciInfo;
	u8	tempBuf[256];
	u8	i = 0;
	u8	BaseMemoryShift = 0;
	u16	TotalLen = 0;

	PAMP_ASSOC_STRUCTURE	pAmpAsoc;


	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("GetAssocInfo start\n"));

	pBTInfo = GET_BT_INFO(padapter);
	pBtHciInfo = &pBTInfo->BtHciInfo;

	if (pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.LenSoFar == 0)
	{

		if (pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.AMPAssocRemLen < (MAX_AMP_ASSOC_FRAG_LEN))
			TotalLen = pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.AMPAssocRemLen;
		else if (pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.AMPAssocRemLen == (MAX_AMP_ASSOC_FRAG_LEN))
			TotalLen = MAX_AMP_ASSOC_FRAG_LEN;
	}
	else if (pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.LenSoFar > 0)
		TotalLen = pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.LenSoFar;

	while ((pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.LenSoFar >= BaseMemoryShift) || TotalLen > BaseMemoryShift)
	{
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD_DETAIL, ("GetAssocInfo, TotalLen=%d, BaseMemoryShift=%d\n",TotalLen,BaseMemoryShift));
		_rtw_memcpy(tempBuf,
			(u8*)pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.AMPAssocfragment+BaseMemoryShift,
			TotalLen-BaseMemoryShift);
		RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD_DETAIL, "GetAssocInfo :\n",
			tempBuf, TotalLen-BaseMemoryShift);

#if 0
		AmpAsoc[i].TypeID=*((u8 *)(tempBuf));
		AmpAsoc[i].Length=*((u16 *)(((u8 *)(tempBuf))+1));
		_rtw_memcpy(AmpAsoc[i].Data, ((u8 *)(tempBuf))+3, AmpAsoc[i].Length);
		BaseMemoryShift=BaseMemoryShift+3+AmpAsoc[i].Length;
#else
		pAmpAsoc = (PAMP_ASSOC_STRUCTURE)tempBuf;
		pAmpAsoc->Length = EF2Byte(pAmpAsoc->Length);
		BaseMemoryShift += 3 + pAmpAsoc->Length;
#endif

		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("TypeID = 0x%x, ", pAmpAsoc->TypeID));
		RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD, "Hex Data: \n", pAmpAsoc->Data, pAmpAsoc->Length);
		switch (pAmpAsoc->TypeID)
		{
			case AMP_MAC_ADDR:
				{
					RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("==> AMP_MAC_ADDR\n"));
					if (pAmpAsoc->Length > 6)
					{
						return _FALSE;
					}

					_rtw_memcpy(pBTInfo->BtAsocEntry[EntryNum].BTRemoteMACAddr, pAmpAsoc->Data,6);
					RTPRINT_ADDR(FIOCTL, IOCTL_BT_HCICMD, ("Remote Mac address \n"), pBTInfo->BtAsocEntry[EntryNum].BTRemoteMACAddr);
					break;
				}

			case AMP_PREFERRED_CHANNEL_LIST:
				{
					RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("==> AMP_PREFERRED_CHANNEL_LIST\n"));
					pBtHciInfo->BtPreChnlListLen=pAmpAsoc->Length;
					_rtw_memcpy(pBtHciInfo->BTPreChnllist,
						pAmpAsoc->Data,
						pBtHciInfo->BtPreChnlListLen);
					RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD, "Preferred channel list : \n", pBtHciInfo->BTPreChnllist, pBtHciInfo->BtPreChnlListLen);
					bthci_DecideBTChannel(padapter,EntryNum);
					break;
				}

			case AMP_CONNECTED_CHANNEL:
				{
					RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("==> AMP_CONNECTED_CHANNEL\n"));
					pBtHciInfo->BTConnectChnlListLen=pAmpAsoc->Length;
					_rtw_memcpy(pBtHciInfo->BTConnectChnllist,
						pAmpAsoc->Data,
						pBtHciInfo->BTConnectChnlListLen);
					break;

				}

			case AMP_80211_PAL_CAP_LIST:
				{

					RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("==> AMP_80211_PAL_CAP_LIST\n"));
					pBTInfo->BtAsocEntry[EntryNum].BTCapability=*(u32 *)(pAmpAsoc->Data);
					if (pBTInfo->BtAsocEntry[EntryNum].BTCapability && 0x00000001)
					{
						// TODO:

						//Signifies PAL capable of utilizing received activity reports.
					}
					if (pBTInfo->BtAsocEntry[EntryNum].BTCapability && 0x00000002)
					{
						// TODO:
						//Signifies PAL is capable of utilizing scheduling information received in an activity reports.
					}
					break;
				}

			case AMP_80211_PAL_VISION:
				{
					pBtHciInfo->BTPalVersion=*(u8 *)(pAmpAsoc->Data);
					pBtHciInfo->BTPalCompanyID=*(u16 *)(((u8 *)(pAmpAsoc->Data))+1);
					pBtHciInfo->BTPalsubversion=*(u16 *)(((u8 *)(pAmpAsoc->Data))+3);
					RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("==> AMP_80211_PAL_VISION PalVersion  0x%x, PalCompanyID  0x%x, Palsubversion 0x%x\n",
					pBtHciInfo->BTPalVersion,
					pBtHciInfo->BTPalCompanyID,
					pBtHciInfo->BTPalsubversion));
					break;
				}

			default:
					RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("==> Unsupport TypeID !!\n"));
				break;
		}
		i++;
	}
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("GetAssocInfo end\n"));

	return _TRUE;
}

u8 bthci_AddEntry(PADAPTER padapter)
{
	PBT30Info		pBTInfo;
	PBT_MGNT		pBtMgnt;
	u8			i;


	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;

	for (i = 0; i < MAX_BT_ASOC_ENTRY_NUM; i++)
	{
		if (pBTInfo->BtAsocEntry[i].bUsed == _FALSE)
		{
			pBTInfo->BtAsocEntry[i].bUsed = _TRUE;
			pBtMgnt->CurrentConnectEntryNum = i;
			break;
		}
	}

	if (i == MAX_BT_ASOC_ENTRY_NUM)
	{
		RTPRINT(FIOCTL, IOCTL_STATE, ("bthci_AddEntry(), Add entry fail!!\n"));
		return _FALSE;
	}
	return _TRUE;
}

u8 bthci_DiscardTxPackets(PADAPTER padapter, u16 LLH)
{
#if 0
	u8 flushOccured = _FALSE;
#if (SENDTXMEHTOD == 0 || SENDTXMEHTOD == 2)
//	PADAPTER		padapter = GetDefaultAdapter(padapter);
	PRT_TX_LOCAL_BUFFER	 pLocalBuffer;
	PPACKET_IRP_ACL_DATA pACLData;

	RTPRINT(FIOCTL, IOCTL_CALLBACK_FUN, ("bthci_DiscardTxPackets() ==>\n"));

	PlatformAcquireSpinLock(padapter, RT_BTData_SPINLOCK);
	while(!RTIsListEmpty(&padapter->BTDataTxQueue))
	{
		pLocalBuffer = (PRT_TX_LOCAL_BUFFER)RTRemoveHeadListWithCnt(&padapter->BTDataTxQueue, &padapter->NumTxBTDataBlock);
		if (pLocalBuffer)
		{
			pACLData = (PPACKET_IRP_ACL_DATA)pLocalBuffer->Buffer.VirtualAddress;
			if (pACLData->Handle == LLH)
				flushOccured = _TRUE;
			RTInsertTailListWithCnt(&padapter->BTDataIdleQueue, &pLocalBuffer->List, &padapter->NumIdleBTDataBlock);
		}
	}
	PlatformReleaseSpinLock(padapter, RT_BTData_SPINLOCK);
#endif
	RTPRINT(FIOCTL, IOCTL_CALLBACK_FUN, ("bthci_DiscardTxPackets() <==\n"));
	return flushOccured;
#else
	return _FALSE;
#endif
}

u8
bthci_CheckLogLinkBehavior(
	PADAPTER 					padapter,
	HCI_FLOW_SPEC			TxFlowSpec
	)
{
	u8	ID = TxFlowSpec.Identifier;
	u8	ServiceType = TxFlowSpec.ServiceType;
	u16	MaxSDUSize = TxFlowSpec.MaximumSDUSize;
	u32	SDUInterArrivatime = TxFlowSpec.SDUInterArrivalTime;
	u8	match = _FALSE;

	switch (ID)
	{
		case 1:
		{
			if (ServiceType == BT_LL_BE)
			{
				match = _TRUE;
				RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Logical Link Type =  TX best effort flowspec\n"));
			}
			else if ((ServiceType == BT_LL_GU) && (MaxSDUSize == 0xffff))
			{
				match = _TRUE;
				RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Logical Link Type =  RX guaranteed latency flowspec\n"));
			}
			else if ((ServiceType == BT_LL_GU) && (MaxSDUSize == 2500))
			{
				RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Logical Link Type =  RX guaranteed Large latency flowspec\n"));
			}
			break;
		}
		case 2:
		{
			if (ServiceType == BT_LL_BE)
			{
				match = _TRUE;
				RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Logical Link Type =  RX best effort flowspec\n"));

			}
			break;
		}
		case 3:
		{
			 if ((ServiceType == BT_LL_GU) && (MaxSDUSize == 1492))
			{
				match=_TRUE;
				RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Logical Link Type =  TX guaranteed latency flowspec\n"));
			}
			else if ((ServiceType==BT_LL_GU) && (MaxSDUSize==2500))
			{
				RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Logical Link Type =  TX guaranteed Large latency flowspec\n"));
			}
			break;
		}
		case 4:
		{
			if (ServiceType == BT_LL_BE)
			{
				if ((SDUInterArrivatime == 0xffffffff) && (ServiceType == BT_LL_BE) && (MaxSDUSize == 1492))
				{
					match = _TRUE;
					RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Logical Link Type =  TX/RX aggregated best effort flowspec\n"));
				}
			}
			else if (ServiceType == BT_LL_GU)
			{
				if ((SDUInterArrivatime == 100) && 10000)
				{
					match = _TRUE;
					RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Logical Link Type =  TX/RX guaranteed bandwidth flowspec\n"));
				}
			}
			break;
		}
		default:
		{
			RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Logical Link Type =  Unknow Type !!!!!!!!\n"));
			break;
		}
	}

#if 0
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("ID = 0x%x,	ServiceType = 0x%x, MaximumSDUSize = 0x%x, SDUInterArrivalTime  = 0x%lx, AccessLatency = 0x%lx, FlushTimeout = 0x%lx\n",
	TxFlowSpec.Identifier, TxFlowSpec.ServiceType, MaxSDUSize,SDUInterArrivatime, TxFlowSpec.AccessLatency, TxFlowSpec.FlushTimeout));
#else
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("ID=0x%x, ServiceType=0x%x, MaximumSDUSize=0x%x, SDUInterArrivalTime=0x%x, AccessLatency=0x%x, FlushTimeout=0x%x\n",
	TxFlowSpec.Identifier, TxFlowSpec.ServiceType, MaxSDUSize, SDUInterArrivatime, TxFlowSpec.AccessLatency, TxFlowSpec.FlushTimeout));
#endif
	return match;
}

void
bthci_SelectFlowType(
	PADAPTER 					padapter,
	BT_LL_FLOWSPEC			TxLLFlowSpec,
	BT_LL_FLOWSPEC			RxLLFlowSpec,
	PHCI_FLOW_SPEC		TxFlowSpec,
	PHCI_FLOW_SPEC		RxFlowSpec
	)
{
	switch (TxLLFlowSpec)
	{
		case BT_TX_BE_FS:
		{
			TxFlowSpec->Identifier = 0x1;
			TxFlowSpec->ServiceType = BT_LL_BE;
			TxFlowSpec->MaximumSDUSize = 0xffff;
			TxFlowSpec->SDUInterArrivalTime = 0xffffffff;
			TxFlowSpec->AccessLatency = 0xffffffff;
			TxFlowSpec->FlushTimeout = 0xffffffff;
			break;
		}
		case BT_RX_BE_FS:
		{
			RxFlowSpec->Identifier = 0x2;
			RxFlowSpec->ServiceType = BT_LL_BE;
			RxFlowSpec->MaximumSDUSize = 0xffff;
			RxFlowSpec->SDUInterArrivalTime = 0xffffffff;
			RxFlowSpec->AccessLatency = 0xffffffff;
			RxFlowSpec->FlushTimeout = 0xffffffff;
			break;
		}
		case BT_TX_GU_FS:
		{
			TxFlowSpec->Identifier = 0x3;
			TxFlowSpec->ServiceType = BT_LL_GU;
			TxFlowSpec->MaximumSDUSize = 1492;
			TxFlowSpec->SDUInterArrivalTime = 0xffffffff;
			TxFlowSpec->AccessLatency = 10000;
			TxFlowSpec->FlushTimeout = 10000;
			break;
		}
		case BT_RX_GU_FS:
		{
			RxFlowSpec->Identifier = 0x1;
			RxFlowSpec->ServiceType = BT_LL_GU;
			RxFlowSpec->MaximumSDUSize = 0xffff;
			RxFlowSpec->SDUInterArrivalTime = 0xffffffff;
			RxFlowSpec->AccessLatency = 10000;
			RxFlowSpec->FlushTimeout = 10000;
			break;
		}
		case BT_TX_BE_AGG_FS:
		{
			TxFlowSpec->Identifier = 0x4;
			TxFlowSpec->ServiceType = BT_LL_BE;
			TxFlowSpec->MaximumSDUSize = 1492;
			TxFlowSpec->SDUInterArrivalTime = 0xffffffff;
			TxFlowSpec->AccessLatency = 0xffffffff;
			TxFlowSpec->FlushTimeout = 0xffffffff;
			break;
		}
		case BT_RX_BE_AGG_FS:
		{
			RxFlowSpec->Identifier = 0x4;
			RxFlowSpec->ServiceType = BT_LL_BE;
			RxFlowSpec->MaximumSDUSize = 1492;
			RxFlowSpec->SDUInterArrivalTime = 0xffffffff;
			RxFlowSpec->AccessLatency = 0xffffffff;
			RxFlowSpec->FlushTimeout = 0xffffffff;
			break;
		}
		case BT_TX_GU_BW_FS:
		{
			TxFlowSpec->Identifier = 0x4;
			TxFlowSpec->ServiceType = BT_LL_GU;
			TxFlowSpec->MaximumSDUSize = 1492;
			TxFlowSpec->SDUInterArrivalTime = 100;
			TxFlowSpec->AccessLatency = 0xffffffff;
			TxFlowSpec->FlushTimeout = 0xffffffff;
			break;
		}
		case BT_RX_GU_BW_FS:
		{
			RxFlowSpec->Identifier = 0x4;
			RxFlowSpec->ServiceType = BT_LL_GU;
			RxFlowSpec->MaximumSDUSize = 1492;
			RxFlowSpec->SDUInterArrivalTime = 100;
			RxFlowSpec->AccessLatency = 0xffffffff;
			RxFlowSpec->FlushTimeout = 0xffffffff;
			break;
		}
		case BT_TX_GU_LARGE_FS:
		{
			TxFlowSpec->Identifier = 0x3;
			TxFlowSpec->ServiceType = BT_LL_GU;
			TxFlowSpec->MaximumSDUSize = 2500;
			TxFlowSpec->SDUInterArrivalTime = 0x1;
			TxFlowSpec->AccessLatency = 10000;
			TxFlowSpec->FlushTimeout = 10000;
			break;
		}
		case BT_RX_GU_LARGE_FS:
		{
			RxFlowSpec->Identifier = 0x1;
			RxFlowSpec->ServiceType = BT_LL_GU;
			RxFlowSpec->MaximumSDUSize = 2500;
			RxFlowSpec->SDUInterArrivalTime = 0x1;
			RxFlowSpec->AccessLatency = 10000;
			RxFlowSpec->FlushTimeout = 10000;
			break;
		}
		default:
			break;
	}

	switch (RxLLFlowSpec)
	{
		case BT_TX_BE_FS:
		{
			TxFlowSpec->Identifier = 0x1;
			TxFlowSpec->ServiceType = BT_LL_BE;
			TxFlowSpec->MaximumSDUSize = 0xffff;
			TxFlowSpec->SDUInterArrivalTime = 0xffffffff;
			TxFlowSpec->AccessLatency = 0xffffffff;
			TxFlowSpec->FlushTimeout = 0xffffffff;
			break;
		}
		case BT_RX_BE_FS:
		{
			RxFlowSpec->Identifier = 0x2;
			RxFlowSpec->ServiceType = BT_LL_BE;
			RxFlowSpec->MaximumSDUSize = 0xffff;
			RxFlowSpec->SDUInterArrivalTime = 0xffffffff;
			RxFlowSpec->AccessLatency = 0xffffffff;
			RxFlowSpec->FlushTimeout = 0xffffffff;
			break;
		}
		case BT_TX_GU_FS:
		{
			TxFlowSpec->Identifier = 0x3;
			TxFlowSpec->ServiceType = BT_LL_GU;
			TxFlowSpec->MaximumSDUSize = 1492;
			TxFlowSpec->SDUInterArrivalTime = 0xffffffff;
			TxFlowSpec->AccessLatency = 10000;
			TxFlowSpec->FlushTimeout = 10000;
			break;
		}
		case BT_RX_GU_FS:
		{
			RxFlowSpec->Identifier = 0x1;
			RxFlowSpec->ServiceType = BT_LL_GU;
			RxFlowSpec->MaximumSDUSize = 0xffff;
			RxFlowSpec->SDUInterArrivalTime = 0xffffffff;
			RxFlowSpec->AccessLatency = 10000;
			RxFlowSpec->FlushTimeout = 10000;
			break;
		}
		case BT_TX_BE_AGG_FS:
		{
			TxFlowSpec->Identifier = 0x4;
			TxFlowSpec->ServiceType = BT_LL_BE;
			TxFlowSpec->MaximumSDUSize = 1492;
			TxFlowSpec->SDUInterArrivalTime = 0xffffffff;
			TxFlowSpec->AccessLatency = 0xffffffff;
			TxFlowSpec->FlushTimeout = 0xffffffff;
			break;
		}
		case BT_RX_BE_AGG_FS:
		{
			RxFlowSpec->Identifier = 0x4;
			RxFlowSpec->ServiceType = BT_LL_BE;
			RxFlowSpec->MaximumSDUSize = 1492;
			RxFlowSpec->SDUInterArrivalTime = 0xffffffff;
			RxFlowSpec->AccessLatency = 0xffffffff;
			RxFlowSpec->FlushTimeout = 0xffffffff;
			break;
		}
		case BT_TX_GU_BW_FS:
		{
			TxFlowSpec->Identifier = 0x4;
			TxFlowSpec->ServiceType = BT_LL_GU;
			TxFlowSpec->MaximumSDUSize = 1492;
			TxFlowSpec->SDUInterArrivalTime = 100;
			TxFlowSpec->AccessLatency = 0xffffffff;
			TxFlowSpec->FlushTimeout = 0xffffffff;
			break;
		}
		case BT_RX_GU_BW_FS:
		{
			RxFlowSpec->Identifier = 0x4;
			RxFlowSpec->ServiceType = BT_LL_GU;
			RxFlowSpec->MaximumSDUSize = 1492;
			RxFlowSpec->SDUInterArrivalTime = 100;
			RxFlowSpec->AccessLatency = 0xffffffff;
			RxFlowSpec->FlushTimeout = 0xffffffff;
			break;
		}
		case BT_TX_GU_LARGE_FS:
		{
			TxFlowSpec->Identifier = 0x3;
			TxFlowSpec->ServiceType = BT_LL_GU;
			TxFlowSpec->MaximumSDUSize = 2500;
			TxFlowSpec->SDUInterArrivalTime = 0x1;
			TxFlowSpec->AccessLatency = 10000;
			TxFlowSpec->FlushTimeout = 10000;
			break;
		}
		case BT_RX_GU_LARGE_FS:
		{
			RxFlowSpec->Identifier = 0x1;
			RxFlowSpec->ServiceType = BT_LL_GU;
			RxFlowSpec->MaximumSDUSize = 2500;
			RxFlowSpec->SDUInterArrivalTime = 0x1;
			RxFlowSpec->AccessLatency = 10000;
			RxFlowSpec->FlushTimeout = 10000;
			break;
		}
		default:
			break;
	}
}

u16
bthci_AssocMACAddr(
	PADAPTER	padapter,
	void	*pbuf
	)
{
	PAMP_ASSOC_STRUCTURE pAssoStrc = (PAMP_ASSOC_STRUCTURE)pbuf;
/*
	u8	FakeAddress[6],i;

	for (i=0;i<6;i++)
	{
		FakeAddress[i]=i;
	}
*/
	pAssoStrc->TypeID = AMP_MAC_ADDR;
	pAssoStrc->Length = 0x06;
	//	_rtw_memcpy(&pAssoStrc->Data[0], Adapter->CurrentAddress, 6);
	_rtw_memcpy(&pAssoStrc->Data[0], padapter->eeprompriv.mac_addr, 6);
	//_rtw_memcpy(&pAssoStrc->Data[0], FakeAddress, 6);
	RTPRINT_DATA(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("AssocMACAddr : \n"), pAssoStrc, pAssoStrc->Length+3);

	return (pAssoStrc->Length+3);
}

u16
bthci_PALCapabilities(
	PADAPTER	padapter,
	void	*pbuf
	)
{
	PAMP_ASSOC_STRUCTURE pAssoStrc = (PAMP_ASSOC_STRUCTURE)pbuf;

	pAssoStrc->TypeID = AMP_80211_PAL_CAP_LIST;
	pAssoStrc->Length = 0x04;

	pAssoStrc->Data[0] = 0x00;
	pAssoStrc->Data[1] = 0x00;

	RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD_DETAIL, ("PALCapabilities : \n"), pAssoStrc, pAssoStrc->Length+3);
	RTPRINT(FIOCTL, IOCTL_BT_LOGO, ("PALCapabilities \n"));

	RTPRINT(FIOCTL, IOCTL_BT_LOGO, (" TypeID = 0x%x,\n Length = 0x%x,\n Content =0x0000\n",
		pAssoStrc->TypeID,
		pAssoStrc->Length));

	return (pAssoStrc->Length+3);
}

u16
bthci_AssocPreferredChannelList(
	PADAPTER		padapter,
	void	*pbuf,
	u8		EntryNum
	)
{
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info				pBTInfo;
//	PRT_DOT11D_INFO pDot11dInfo;
	PAMP_ASSOC_STRUCTURE	pAssoStrc;
	PAMP_PREF_CHNL_REGULATORY pReg;
	PCHNL_TXPOWER_TRIPLE pTripleIE, pTriple;
	char	ctrString[3] = {'X', 'X', 'X'};
	u32	len = 0;
	u8	i=0, NumTriples=0, preferredChnl;


	pBTInfo = GET_BT_INFO(padapter);
//	pDot11dInfo = GET_DOT11D_INFO(pMgntInfo);
	pAssoStrc = (PAMP_ASSOC_STRUCTURE)pbuf;
	pReg = (PAMP_PREF_CHNL_REGULATORY)&pAssoStrc->Data[3];

	preferredChnl = bthci_GetLocalChannel(padapter);
	pAssoStrc->TypeID = AMP_PREFERRED_CHANNEL_LIST;
#if 0//cosa temp remove
	// When 802.11d is enabled and learned from beacon
	if (	(pDot11dInfo->bEnabled) &&
		(pDot11dInfo->State == DOT11D_STATE_LEARNED)	)
	{
		//Country String
		_rtw_memcpy(&pAssoStrc->Data[0], &pDot11dInfo->CountryIeBuf[0], 3);
		pReg->reXId = 201;
		pReg->regulatoryClass = 254;	// should parse beacon frame
		pReg->coverageClass = 0;
		len += 6;
		pTriple=(PCHNL_TXPOWER_TRIPLE)&pAssoStrc->Data[len];
		pTripleIE = (PCHNL_TXPOWER_TRIPLE)(&pDot11dInfo->CountryIeBuf[3]);

		NumTriples = (pDot11dInfo->CountryIeLen-3)/3;// skip 3-byte country string.
		for (i=0; i<NumTriples; i++)
		{
			if (	(preferredChnl > pTripleIE->FirstChnl) &&
				(preferredChnl <= (pTripleIE->FirstChnl+pTripleIE->NumChnls-1)))
			{
				// ex: preferred=10, first=3, num=9, from ch3~ch11
				// that should be divided to 2~3 groups
				// (1) first=10, num=1, ch10
				// (2) first=3, num=7, from ch3~ch9
				// (3) first=11, num=1, ch11

				// (1) group 1, preferred channel
				pTriple->FirstChnl = preferredChnl;
				pTriple->NumChnls = 1;
				pTriple->MaxTxPowerInDbm = pTripleIE->MaxTxPowerInDbm;
				len += 3;
				pTriple = (PCHNL_TXPOWER_TRIPLE)((u8*)pTriple + 3);

				// (2) group 2, first chnl~preferred-1
				pTriple->FirstChnl = pTripleIE->FirstChnl;
				pTriple->NumChnls = preferredChnl-pTriple->FirstChnl;
				pTriple->MaxTxPowerInDbm = pTripleIE->MaxTxPowerInDbm;
				len += 3;
				pTriple = (PCHNL_TXPOWER_TRIPLE)((u8*)pTriple + 3);

				if (preferredChnl < (pTripleIE->FirstChnl+pTripleIE->NumChnls-1))
				{
					// (3) group 3, preferred+1~last
					pTriple->FirstChnl = preferredChnl+1;
					pTriple->NumChnls = pTripleIE->FirstChnl+pTripleIE->NumChnls-1-preferredChnl;
					pTriple->MaxTxPowerInDbm = pTripleIE->MaxTxPowerInDbm;
					len += 3;
					pTriple = (PCHNL_TXPOWER_TRIPLE)((u8*)pTriple + 3);
				}
			}
			else
			{
				pTriple->FirstChnl = pTripleIE->FirstChnl;
				pTriple->NumChnls = pTripleIE->NumChnls;
				pTriple->MaxTxPowerInDbm = pTripleIE->MaxTxPowerInDbm;
				len += 3;
				pTriple = (PCHNL_TXPOWER_TRIPLE)((u8*)pTriple + 3);
			}
			pTripleIE = (PCHNL_TXPOWER_TRIPLE)((u8*)pTripleIE + 3);
		}
	}
	else
#endif
	{
		// locale unknown
		_rtw_memcpy(&pAssoStrc->Data[0], &ctrString[0], 3);
		pReg->reXId = 201;
		pReg->regulatoryClass = 254;
		pReg->coverageClass = 0;
		len += 6;
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD | IOCTL_BT_LOGO), ("PREFERRED_CHNL_LIST\n"));
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD | IOCTL_BT_LOGO), ("XXX, 201,254,0\n"));
		// at the following, chnl 1~11 should be contained
		pTriple = (PCHNL_TXPOWER_TRIPLE)&pAssoStrc->Data[len];

		// (1) if any wifi or bt HS connection exists
		if ((pBTInfo->BtAsocEntry[EntryNum].AMPRole == AMP_BTAP_CREATOR) ||
#if 0
			pMgntInfo->mAssoc ||
			pMgntInfo->mIbss ||
			IsExtAPModeExist(padapter)) ||
#else
			(check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE|WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE|WIFI_AP_STATE) == _TRUE) ||
#endif
			BTHCI_HsConnectionEstablished(padapter))
		{
			pTriple->FirstChnl = preferredChnl;
			pTriple->NumChnls = 1;
			pTriple->MaxTxPowerInDbm = 20;
			len += 3;
			RTPRINT(FIOCTL, (IOCTL_BT_HCICMD | IOCTL_BT_LOGO), ("First Channel = %d, Channel Num = %d, MaxDbm = %d\n",
				pTriple->FirstChnl,
				pTriple->NumChnls,
				pTriple->MaxTxPowerInDbm));

			//pTriple = (PCHNL_TXPOWER_TRIPLE)((u8*)pTriple + 3);
		}
#if 0
		// If we are responder, we can fill all the channel list.
		if (pBTInfo->BtAsocEntry[EntryNum].AMPRole!=AMP_BTAP_CREATOR)
		{
			//
			// When Wifi connection exists, channel should be choosed to the current one.
			// 1. Infra, connect to an AP
			// 2. IBSS, fixed channel
			//
			if (!pMgntInfo->mAssoc &&
				(padapter->MgntInfo.Regdot11networktype != RT_JOIN_NETWORKTYPE_ADHOC ))
			{
				// (2) group 2, chnl 1~preferred-1
				if (preferredChnl > 1 && preferredChnl<15)
				{
					pTriple->FirstChnl = 1;
					pTriple->NumChnls = preferredChnl-1;
					pTriple->MaxTxPowerInDbm = 20;
					len += 3;
					RTPRINT(FIOCTL, (IOCTL_BT_HCICMD | IOCTL_BT_LOGO), ("First Channel = %d, Channel Num = %d, MaxDbm = %d\n",
						pTriple->FirstChnl,
						pTriple->NumChnls,
						pTriple->MaxTxPowerInDbm));
					pTriple = (PCHNL_TXPOWER_TRIPLE)((u8*)pTriple + 3);

				}
				// (3) group 3, preferred+1~chnl 11
				if (preferredChnl < 11)
				{
					pTriple->FirstChnl = preferredChnl+1;
					pTriple->NumChnls = 11-preferredChnl;
					pTriple->MaxTxPowerInDbm = 20;
					len += 3;
					//pTriple = (PCHNL_TXPOWER_TRIPLE)((u8*)pTriple + 3);

					RTPRINT(FIOCTL, (IOCTL_BT_HCICMD | IOCTL_BT_LOGO), ("First Channel = %d, Channel Num = %d, MaxDbm = %d\n",
						pTriple->FirstChnl,
						pTriple->NumChnls,
						pTriple->MaxTxPowerInDbm));
				}
			}
		}
#endif
	}
	pAssoStrc->Length = (u16)len;
	RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD, ("AssocPreferredChannelList : \n"), pAssoStrc, pAssoStrc->Length+3);

	return (pAssoStrc->Length+3);
}

u16 bthci_AssocPALVer(PADAPTER padapter, void *pbuf)
{
	PAMP_ASSOC_STRUCTURE pAssoStrc = (PAMP_ASSOC_STRUCTURE)pbuf;
	u8 *pu1Tmp;
	u16	*pu2Tmp;

	pAssoStrc->TypeID = AMP_80211_PAL_VISION;
	pAssoStrc->Length = 0x5;
	pu1Tmp = &pAssoStrc->Data[0];
	*pu1Tmp = 0x1;	// PAL Version
	pu2Tmp = (u16*)&pAssoStrc->Data[1];
	*pu2Tmp = 0x5D;	// SIG Company identifier of 802.11 PAL vendor
	pu2Tmp = (u16*)&pAssoStrc->Data[3];
	*pu2Tmp = 0x1;	// PAL Sub-version specifier

	RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD_DETAIL, ("AssocPALVer : \n"), pAssoStrc, pAssoStrc->Length+3);
	RTPRINT(FIOCTL, IOCTL_BT_LOGO, ("AssocPALVer \n"));

	RTPRINT(FIOCTL, IOCTL_BT_LOGO, (" TypeID = 0x%x,\n Length = 0x%x,\n PAL Version = 0x01,\n PAL vendor = 0x01,\n PAL Sub-version specifier = 0x01\n",
		pAssoStrc->TypeID,
		pAssoStrc->Length));
	return (pAssoStrc->Length+3);
}

u16
bthci_ReservedForTestingPLV(
	PADAPTER	padapter,
	void	*pbuf
	)
{
	PAMP_ASSOC_STRUCTURE pAssoStrc = (PAMP_ASSOC_STRUCTURE)pbuf;

	pAssoStrc->TypeID = AMP_RESERVED_FOR_TESTING;
	pAssoStrc->Length = 0x10;

	pAssoStrc->Data[0] = 0x00;
	pAssoStrc->Data[1] = 0x01;
	pAssoStrc->Data[2] = 0x02;
	pAssoStrc->Data[3] = 0x03;
	pAssoStrc->Data[4] = 0x04;
	pAssoStrc->Data[5] = 0x05;
	pAssoStrc->Data[6] = 0x06;
	pAssoStrc->Data[7] = 0x07;
	pAssoStrc->Data[8] = 0x08;
	pAssoStrc->Data[9] = 0x09;
	pAssoStrc->Data[10] = 0x0a;
	pAssoStrc->Data[11] = 0x0b;
	pAssoStrc->Data[12] = 0x0c;
	pAssoStrc->Data[13] = 0x0d;
	pAssoStrc->Data[14] = 0x0e;
	pAssoStrc->Data[15] = 0x0f;

	return (pAssoStrc->Length+3);
}

u8 bthci_CheckRfStateBeforeConnect(PADAPTER padapter)
{
	PBT30Info				pBTInfo;
	rt_rf_power_state 		RfState;


	pBTInfo = GET_BT_INFO(padapter);

//	padapter->HalFunc.GetHwRegHandler(padapter, HW_VAR_RF_STATE, (u8*)(&RfState));
	RfState = padapter->pwrctrlpriv.rf_pwrstate;

	if (RfState != rf_on)
	{
		PlatformSetTimer(padapter, &pBTInfo->BTPsDisableTimer, 50);
		return _FALSE;
	}

	return _TRUE;
}

u8
bthci_ConstructScanList(
	PBT30Info		pBTInfo,
	u8			*pChannels,
	u8			*pNChannels,
	PRT_SCAN_TYPE	pScanType,
	u16			*pDuration
	)
{
	PADAPTER				padapter;
	PBT_HCI_INFO				pBtHciInfo;
	PCHNL_TXPOWER_TRIPLE	pTriple_subband;
	PCOMMON_TRIPLE			pTriple;
	u8					chnl, i, j, tripleLetsCnt=0;


	padapter = pBTInfo->padapter;
	pBtHciInfo = &pBTInfo->BtHciInfo;
	*pNChannels = 0;
	*pScanType = SCAN_ACTIVE;
	*pDuration = 200;

	pTriple = (PCOMMON_TRIPLE)&(pBtHciInfo->BTPreChnllist[COUNTRY_STR_LEN]);

	// contains country string, len is 3
	for (i = 0; i < (pBtHciInfo->BtPreChnlListLen-COUNTRY_STR_LEN); i+=3, pTriple++)
	{
		if (pTriple->byte_1st == 0xc9)	// Regulatory Extension Identifier, skip it
			continue;
		else							// Sub-band triplet
		{
			tripleLetsCnt++;
			pTriple_subband = (PCHNL_TXPOWER_TRIPLE)pTriple;

			// search the sub-band triplet and find if remote channel is legal to our channel plan.
			for (chnl = pTriple_subband->FirstChnl; chnl < (pTriple_subband->FirstChnl+pTriple_subband->NumChnls); chnl++)
			{
				if (BT_IsLegalChannel(padapter, chnl))	// remote channel is legal for our channel plan.
				{
					//DbgPrint("cosa insert chnl(%d) into scan list\n", chnl);
					pChannels[*pNChannels] = chnl;
					(*pNChannels)++;
				}
			}
		}
	}

	if (tripleLetsCnt == 0)
	{
		// Fill chnl 1~ chnl 11
		for (chnl=1; chnl<12; chnl++)
		{
			//DbgPrint("cosa insert chnl(%d) into scan list\n", chnl);
			pChannels[*pNChannels] = chnl;
			(*pNChannels)++;
		}
	}

	if (*pNChannels == 0)
		return _FALSE;
	else
		return _TRUE;
}

void bthci_ResponderStartToScan(PADAPTER padapter)
{
#if 0
	static u8		Buf[512];
	PMGNT_INFO		pMgntInfo = &(padapter->MgntInfo);
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	u8			*pProbeReq = Buf + FIELD_OFFSET(CUSTOMIZED_SCAN_REQUEST, ProbeReqBuf);
	u16			*pProbeReqLen = (u16*)(Buf + FIELD_OFFSET(CUSTOMIZED_SCAN_REQUEST, ProbeReqLen));
	PCUSTOMIZED_SCAN_REQUEST pScanReq = (PCUSTOMIZED_SCAN_REQUEST)Buf;
	u8			i;

	pBtMgnt->JoinerNeedSendAuth=_TRUE;
	pMgntInfo->SettingBeforeScan.WirelessMode = pMgntInfo->dot11CurrentWirelessMode;
	pMgntInfo->SettingBeforeScan.ChannelNumber = pMgntInfo->dot11CurrentChannelNumber;
	pMgntInfo->SettingBeforeScan.ChannelBandwidth = (HT_CHANNEL_WIDTH)pMgntInfo->pHTInfo->bCurBW40MHz;
	pMgntInfo->SettingBeforeScan.ExtChnlOffset = pMgntInfo->pHTInfo->CurSTAExtChnlOffset;
	RTPRINT(FIOCTL, IOCTL_STATE, ("[Bt scan], responder start the scan process!!\n"));

	pScanReq->bEnabled = _TRUE;
	pScanReq->DataRate = MGN_6M;

	BTPKT_ConstructProbeRequest(
			padapter,
			pProbeReq,
			pProbeReqLen);

	bthci_ConstructScanList(pBTInfo,
		pScanReq->Channels,
		&pScanReq->nChannels,
		&pScanReq->ScanType,
		&pScanReq->Duration);

	RTPRINT(FIOCTL, IOCTL_STATE, ("[Bt scan], scan channel list =["));
	for (i=0; i<pScanReq->nChannels; i++)
	{
		if (i == pScanReq->nChannels-1)
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("%d", pScanReq->Channels[i]));
		}
		else
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("%d, \n", pScanReq->Channels[i]));
		}
	}
	RTPRINT(FIOCTL, IOCTL_STATE, ("]\n"));

	RTPRINT(FIOCTL, IOCTL_STATE, ("[Bt scan], customized scan started!!\n"));
	pBtMgnt->bBtScan = _TRUE;
	MgntActSet_802_11_CustomizedScanRequest((GetDefaultAdapter(padapter)), pScanReq);
#endif
}

u8
bthci_PhyLinkConnectionInProgress(
	PADAPTER	padapter,
	u8		PhyLinkHandle
	)
{
	PBT30Info	pBTInfo;
	PBT_MGNT	pBtMgnt;


	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->bPhyLinkInProgress &&
		(pBtMgnt->BtCurrentPhyLinkhandle == PhyLinkHandle))
	{
		return _TRUE;
	}
	return _FALSE;
}

void
bthci_ResetFlowSpec(
	PADAPTER	padapter,
	u8	EntryNum,
	u8	index
	)
{
	PBT30Info	pBTinfo;


	pBTinfo = GET_BT_INFO(padapter);

	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].BtLogLinkhandle = 0;
	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].BtPhyLinkhandle = 0;
	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].bLLCompleteEventIsSet = _FALSE;
	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].bLLCancelCMDIsSetandComplete = _FALSE;
	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].BtTxFlowSpecID = 0;
	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].TxPacketCount = 0;

	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].Tx_Flow_Spec.Identifier = 0x01;
	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].Tx_Flow_Spec.ServiceType = SERVICE_BEST_EFFORT;
	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].Tx_Flow_Spec.MaximumSDUSize = 0xffff;
	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].Tx_Flow_Spec.SDUInterArrivalTime = 0xffffffff;
	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].Tx_Flow_Spec.AccessLatency = 0xffffffff;
	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].Tx_Flow_Spec.FlushTimeout = 0xffffffff;

	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].Rx_Flow_Spec.Identifier = 0x01;
	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].Rx_Flow_Spec.ServiceType = SERVICE_BEST_EFFORT;
	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].Rx_Flow_Spec.MaximumSDUSize = 0xffff;
	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].Rx_Flow_Spec.SDUInterArrivalTime = 0xffffffff;
	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].Rx_Flow_Spec.AccessLatency = 0xffffffff;
	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].Rx_Flow_Spec.FlushTimeout = 0xffffffff;
}

void bthci_ResetEntry(PADAPTER padapter, u8 EntryNum)
{
	PBT30Info		pBTinfo;
	PBT_MGNT		pBtMgnt;
	u8	j;


	pBTinfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTinfo->BtMgnt;

	pBTinfo->BtAsocEntry[EntryNum].bUsed=_FALSE;
	pBTinfo->BtAsocEntry[EntryNum].BtCurrentState=HCI_STATE_DISCONNECTED;
	pBTinfo->BtAsocEntry[EntryNum].BtNextState=HCI_STATE_DISCONNECTED;

	pBTinfo->BtAsocEntry[EntryNum].AmpAsocCmdData.AMPAssocRemLen=0;
	pBTinfo->BtAsocEntry[EntryNum].AmpAsocCmdData.BtPhyLinkhandle = 0;
	if (pBTinfo->BtAsocEntry[EntryNum].AmpAsocCmdData.AMPAssocfragment != NULL)
	{
		_rtw_memset(pBTinfo->BtAsocEntry[EntryNum].AmpAsocCmdData.AMPAssocfragment, 0, TOTAL_ALLOCIATE_ASSOC_LEN);
	}
	pBTinfo->BtAsocEntry[EntryNum].AmpAsocCmdData.LenSoFar=0;

	pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKeyType = 0;
	pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle = 0;
	_rtw_memset(pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKey, 0, pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKeyLen);
	pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKeyLen=0;

	pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.LinkSuperversionTimeout=0x3e80;//0x640; //0.625ms*1600=1000ms, 0.625ms*16000=10000ms

	pBTinfo->BtAsocEntry[EntryNum].AMPRole = AMP_BTAP_NONE;

	pBTinfo->BtAsocEntry[EntryNum].mAssoc=_FALSE;
	pBTinfo->BtAsocEntry[EntryNum].b4waySuccess = _FALSE;

	// Reset BT WPA
	pBTinfo->BtAsocEntry[EntryNum].KeyReplayCounter = 0;
	pBTinfo->BtAsocEntry[EntryNum].BTWPAAuthState = STATE_WPA_AUTH_UNINITIALIZED;

	pBTinfo->BtAsocEntry[EntryNum].bSendSupervisionPacket=_FALSE;
	pBTinfo->BtAsocEntry[EntryNum].NoRxPktCnt=0;
	pBTinfo->BtAsocEntry[EntryNum].ShortRangeMode = 0;
	pBTinfo->BtAsocEntry[EntryNum].rxSuvpPktCnt = 0;

	for (j=0; j<MAX_LOGICAL_LINK_NUM; j++)
	{
		bthci_ResetFlowSpec(padapter, EntryNum, j);
	}

	pBtMgnt->BTAuthCount = 0;
	pBtMgnt->BTAsocCount = 0;
	pBtMgnt->BTCurrentConnectType = BT_DISCONNECT;
	pBtMgnt->BTReceiveConnectPkt = BT_DISCONNECT;

	HALBT_RemoveKey(padapter, EntryNum);
}

void
bthci_RemoveEntryByEntryNum(
	PADAPTER 	padapter,
	u8		EntryNum
	)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	bthci_ResetEntry(padapter, EntryNum);

	if (pBtMgnt->CurrentBTConnectionCnt>0)
		pBtMgnt->CurrentBTConnectionCnt--;

	RTPRINT(FIOCTL, IOCTL_STATE, ("[BT Flag], CurrentBTConnectionCnt = %d!!\n",
		pBtMgnt->CurrentBTConnectionCnt));

	if (pBtMgnt->CurrentBTConnectionCnt > 0)
		pBtMgnt->BtOperationOn = _TRUE;
	else
	{
		pBtMgnt->BtOperationOn = _FALSE;
		RTPRINT(FIOCTL, IOCTL_STATE, ("[BT Flag], Bt Operation OFF!!\n"));
	}

	if (pBtMgnt->BtOperationOn == _FALSE)
	{
		PlatformCancelTimer(padapter, &pBTInfo->BTSupervisionPktTimer);
#if (SENDTXMEHTOD == 0)
		PlatformCancelTimer(padapter, &pBTInfo->BTHCISendAclDataTimer);
#endif
		PlatformCancelTimer(padapter, &pBTInfo->BTHCIDiscardAclDataTimer);
		PlatformCancelTimer(padapter, &pBTInfo->BTBeaconTimer);
		pBtMgnt->bStartSendSupervisionPkt = _FALSE;
#if (RTS_CTS_NO_LEN_LIMIT == 1)
		rtw_write32(padapter, 0x4c8, 0xc140402);
#endif
	}
}

u8
bthci_CommandCompleteHeader(
	u8		*pbuf,
	u16		OGF,
	u16		OCF,
	HCI_STATUS	status
	)
{
	PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)pbuf;
	u8	NumHCI_Comm = 0x1;


	PPacketIrpEvent->EventCode = HCI_EVENT_COMMAND_COMPLETE;
	PPacketIrpEvent->Data[0] = NumHCI_Comm;	//packet #
	PPacketIrpEvent->Data[1] = HCIOPCODELOW(OCF, OGF);
	PPacketIrpEvent->Data[2] = HCIOPCODEHIGHT(OCF, OGF);

	if (OGF == OGF_EXTENSION)
	{
		if (OCF == HCI_SET_RSSI_VALUE)
		{
			RTPRINT(FIOCTL,(IOCTL_BT_EVENT_PERIODICAL), ("[BT event], CommandComplete, Num_HCI_Comm = 0x%x, Opcode = 0x%02x%02x, status = 0x%x, OGF = 0x%x, OCF = 0x%x\n",
				NumHCI_Comm,(HCIOPCODEHIGHT(OCF, OGF)), (HCIOPCODELOW(OCF, OGF)), status, OGF, OCF));
		}
		else
		{
			RTPRINT(FIOCTL,(IOCTL_BT_HCICMD_EXT), ("[BT event], CommandComplete, Num_HCI_Comm = 0x%x, Opcode = 0x%02x%02x, status = 0x%x, OGF = 0x%x, OCF = 0x%x\n",
				NumHCI_Comm,(HCIOPCODEHIGHT(OCF, OGF)), (HCIOPCODELOW(OCF, OGF)), status, OGF, OCF));
		}
	}
	else
	{
		RTPRINT(FIOCTL,(IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("[BT event], CommandComplete, Num_HCI_Comm = 0x%x, Opcode = 0x%02x%02x, status = 0x%x, OGF = 0x%x, OCF = 0x%x\n",
			NumHCI_Comm,(HCIOPCODEHIGHT(OCF, OGF)), (HCIOPCODELOW(OCF, OGF)), status, OGF, OCF));
	}
	return 3;
}

u8 bthci_ExtensionEventHeader(u8 *pbuf, u8 extensionEvent)
{
	PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)pbuf;
	PPacketIrpEvent->EventCode = HCI_EVENT_EXTENSION_MOTO;
	PPacketIrpEvent->Data[0] = extensionEvent;	//extension event code

	return 1;
}

u8 bthci_ExtensionEventHeaderRtk(u8 *pbuf, u8 extensionEvent)
{
	PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)pbuf;
	PPacketIrpEvent->EventCode = HCI_EVENT_EXTENSION_RTK;
	PPacketIrpEvent->Data[0] = extensionEvent;	//extension event code

	return 1;
}

RT_STATUS
bthci_IndicateEvent(
	PADAPTER 	padapter,
	void		*pEvntData,
	u32		dataLen
	)
{
	RT_STATUS	rt_status;

	rt_status = PlatformIndicateBTEvent(padapter, pEvntData, dataLen);

	return rt_status;
}

void
bthci_EventWriteRemoteAmpAssoc(
	PADAPTER	padapter,
	HCI_STATUS	status,
	u8		PLHandle
	)
{
	u8 localBuf[TmpLocalBufSize] = "";
	u8 *pRetPar;
	u8 len = 0;
	PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

	PlatformZeroMemory(&localBuf[0], TmpLocalBufSize);
	PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_STATUS_PARAMETERS,
		HCI_WRITE_REMOTE_AMP_ASSOC,
		status);
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("PhyLinkHandle = 0x%x, status = %d\n", PLHandle, status));
	// Return parameters starts from here
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;		//status
	pRetPar[1] = PLHandle;
	len += 2;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
}

void
bthci_EventEnhancedFlushComplete(
	PADAPTER 					padapter,
	u16					LLH
	)
{
	u8	localBuf[4] = "";
	PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;
	RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("EventEnhancedFlushComplete, LLH = 0x%x\n", LLH));

	PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);
	PPacketIrpEvent->EventCode=HCI_EVENT_ENHANCED_FLUSH_COMPLETE;
	PPacketIrpEvent->Length=2;
	//Logical link handle
	PPacketIrpEvent->Data[0] = TWOBYTE_LOWBYTE(LLH);
	PPacketIrpEvent->Data[1] = TWOBYTE_HIGHTBYTE(LLH);

	bthci_IndicateEvent(padapter, PPacketIrpEvent, 4);
}

void
bthci_EventShortRangeModeChangeComplete(
	PADAPTER 					padapter,
	HCI_STATUS				HciStatus,
	u8					ShortRangeState,
	u8					EntryNum
	)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO		pBtHciInfo = &pBTInfo->BtHciInfo;
	u8	localBuf[5] = "";
	PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

	if (!(pBtHciInfo->BTEventMaskPage2 & EMP2_HCI_EVENT_SHORT_RANGE_MODE_CHANGE_COMPLETE))
	{
		RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], Short Range Mode Change Complete, Ignore to send this event due to event mask page 2\n"));
		return;
	}
	RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], Short Range Mode Change Complete, Status = %d\n , PLH = 0x%x\n, Short_Range_Mode_State = 0x%x\n",
		HciStatus, pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle, ShortRangeState));

	PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);
	PPacketIrpEvent->EventCode=HCI_EVENT_SHORT_RANGE_MODE_CHANGE_COMPLETE;
	PPacketIrpEvent->Length=3;
	PPacketIrpEvent->Data[0] = HciStatus;
	PPacketIrpEvent->Data[1] = pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle;
	PPacketIrpEvent->Data[2] = ShortRangeState;
	bthci_IndicateEvent(padapter, PPacketIrpEvent, 5);
}

void
bthci_EventSendFlowSpecModifyComplete(
	PADAPTER 					padapter,
	HCI_STATUS				HciStatus,
	u16					logicHandle
	)
{
	u8	localBuf[5] = "";
	PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO		pBtHciInfo = &pBTInfo->BtHciInfo;

	if (!(pBtHciInfo->BTEventMaskPage2 & EMP2_HCI_EVENT_FLOW_SPEC_MODIFY_COMPLETE))
	{
		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("[BT event], Flow Spec Modify Complete, Ignore to send this event due to event mask page 2\n"));
		return;
	}
	RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("[BT event], Flow Spec Modify Complete, status = 0x%x, LLH = 0x%x\n",HciStatus,logicHandle));
	PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);
	PPacketIrpEvent->EventCode=HCI_EVENT_FLOW_SPEC_MODIFY_COMPLETE;
	PPacketIrpEvent->Length=3;

	PPacketIrpEvent->Data[0] = HciStatus;
	//Logical link handle
	PPacketIrpEvent->Data[1] = TWOBYTE_LOWBYTE(logicHandle);
	PPacketIrpEvent->Data[2] = TWOBYTE_HIGHTBYTE(logicHandle);

	bthci_IndicateEvent(padapter, PPacketIrpEvent, 5);
}

void
bthci_EventExtGetBTRSSI(
	PADAPTER 					padapter,
	u16						ConnectionHandle
	)
{
	u8 len = 0;
	u8 	localBuf[7] = "";
	u8 *pRetPar;
	u16	*pu2Temp;
	PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

	PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

	len += bthci_ExtensionEventHeader(&localBuf[0],
			HCI_EVENT_GET_BT_RSSI);

	// Return parameters starts from here
	pRetPar = &PPacketIrpEvent->Data[len];
	pu2Temp = (u16*)&pRetPar[0];
	*pu2Temp = ConnectionHandle;
	len += 2;

	PPacketIrpEvent->Length = len;
	if (bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2) == RT_STATUS_SUCCESS)
	{
		RTPRINT(FIOCTL, IOCTL_BT_EVENT_PERIODICAL, ("[BT event], Get BT RSSI, Connection Handle = 0x%x, Extension event code = 0x%x\n",
			ConnectionHandle, HCI_EVENT_GET_BT_RSSI));
	}
}

void
bthci_EventExtWifiScanNotify(
	PADAPTER 					padapter,
	u8						scanType
	)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	u8 len = 0;
	u8 localBuf[7] = "";
	u8 *pRetPar;
	u8 *pu1Temp;
	PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

	if (!pBtMgnt->BtOperationOn)
		return;

	PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

	len += bthci_ExtensionEventHeaderRtk(&localBuf[0], HCI_EVENT_EXT_WIFI_SCAN_NOTIFY);

	// Return parameters starts from here
	pRetPar = &PPacketIrpEvent->Data[len];
	pu1Temp = (u8*)&pRetPar[0];
	*pu1Temp = scanType;
	len += 1;

	PPacketIrpEvent->Length = len;

	if (bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2) == RT_STATUS_SUCCESS)
	{
		RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], Wifi scan notify, scan type = %d\n",
			scanType));
	}
}


void
bthci_EventAMPReceiverReport(
	PADAPTER 	padapter,
	u8		Reason
	)
{
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO	pBtHciInfo = &pBTInfo->BtHciInfo;

	if (pBtHciInfo->bTestNeedReport)
	{
		u8 localBuf[20] = "";
		u32	*pu4Temp;
		u16	*pu2Temp;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;


		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), (" HCI_EVENT_AMP_RECEIVER_REPORT \n"));
		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);
		PPacketIrpEvent->EventCode = HCI_EVENT_AMP_RECEIVER_REPORT;
		PPacketIrpEvent->Length = 2;

		PPacketIrpEvent->Data[0] = pBtHciInfo->TestCtrType;

		PPacketIrpEvent->Data[1] =Reason;

		pu4Temp = (u32*)&PPacketIrpEvent->Data[2];
		*pu4Temp = pBtHciInfo->TestEventType;

		pu2Temp = (u16*)&PPacketIrpEvent->Data[6];
		*pu2Temp = pBtHciInfo->TestNumOfFrame;

		pu2Temp = (u16*)&PPacketIrpEvent->Data[8];
		*pu2Temp = pBtHciInfo->TestNumOfErrFrame;

		pu4Temp = (u32*)&PPacketIrpEvent->Data[10];
		*pu4Temp = pBtHciInfo->TestNumOfBits;

		pu4Temp = (u32*)&PPacketIrpEvent->Data[14];
		*pu4Temp = pBtHciInfo->TestNumOfErrBits;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, 20);

		//Return to Idel state with RX and TX off.

	}

	pBtHciInfo->TestNumOfFrame = 0x00;
}

void
bthci_EventChannelSelected(
	PADAPTER				padapter,
	u8				EntryNum
	)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO		pBtHciInfo = &pBTInfo->BtHciInfo;
	u8	localBuf[3] = "";
	PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

	if (!(pBtHciInfo->BTEventMaskPage2 & EMP2_HCI_EVENT_CHANNEL_SELECT))
	{
		RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], Channel Selected, Ignore to send this event due to event mask page 2\n"));
		return;
	}

	RTPRINT(FIOCTL, IOCTL_BT_EVENT|IOCTL_STATE, ("[BT event], Channel Selected, PhyLinkHandle %d\n",
		pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle));

	PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);
	PPacketIrpEvent->EventCode=HCI_EVENT_CHANNEL_SELECT;
	PPacketIrpEvent->Length=1;
	PPacketIrpEvent->Data[0] = pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle;
	bthci_IndicateEvent(padapter, PPacketIrpEvent, 3);
}

void
bthci_EventDisconnectPhyLinkComplete(
	PADAPTER 					padapter,
	HCI_STATUS				HciStatus,
	HCI_STATUS				Reason,
	u8					EntryNum
	)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO		pBtHciInfo = &pBTInfo->BtHciInfo;
	u8	localBuf[5] = "";
	PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

	if (!(pBtHciInfo->BTEventMaskPage2 & EMP2_HCI_EVENT_DISCONNECT_PHY_LINK_COMPLETE))
	{
		RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], Disconnect Physical Link Complete, Ignore to send this event due to event mask page 2\n"));
		return;
	}
	RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], Disconnect Physical Link Complete, Status = 0x%x, PLH = 0x%x Reason =0x%x\n",
		HciStatus,pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle,Reason));
	PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);
	PPacketIrpEvent->EventCode=HCI_EVENT_DISCONNECT_PHY_LINK_COMPLETE;
	PPacketIrpEvent->Length=3;
	PPacketIrpEvent->Data[0] = HciStatus;
	PPacketIrpEvent->Data[1] = pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle;
	PPacketIrpEvent->Data[2] = Reason;
	bthci_IndicateEvent(padapter, PPacketIrpEvent, 5);
}

void
bthci_EventPhysicalLinkComplete(
	PADAPTER 					padapter,
	HCI_STATUS				HciStatus,
	u8					EntryNum,
	u8					PLHandle
	)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PBT_HCI_INFO		pBtHciInfo = &pBTInfo->BtHciInfo;
	PBT_DBG			pBtDbg=&pBTInfo->BtDbg;
	u8			localBuf[4] = "";
	PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;
	u8			PL_handle;

	pBtMgnt->bPhyLinkInProgress = _FALSE;
	pBtDbg->dbgHciInfo.hciCmdPhyLinkStatus = HciStatus;
	if (!(pBtHciInfo->BTEventMaskPage2 & EMP2_HCI_EVENT_PHY_LINK_COMPLETE))
	{
		RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], Physical Link Complete, Ignore to send this event due to event mask page 2\n"));
		return;
	}

	if (EntryNum == 0xff)
	{
		// connection not started yet, just use the input physical link handle to response.
		PL_handle = PLHandle;
	}
	else
	{
		// connection is under progress, use the phy link handle we recorded.
		PL_handle  = pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle;
		pBTInfo->BtAsocEntry[EntryNum].bNeedPhysLinkCompleteEvent=_FALSE;
	}

	RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], Physical Link Complete, Status = 0x%x PhyLinkHandle = 0x%x\n",HciStatus,
		PL_handle));

	PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);
	PPacketIrpEvent->EventCode=HCI_EVENT_PHY_LINK_COMPLETE;
	PPacketIrpEvent->Length=2;

	PPacketIrpEvent->Data[0] = HciStatus;
	PPacketIrpEvent->Data[1] = PL_handle;
	bthci_IndicateEvent(padapter, PPacketIrpEvent, 4);

}

void
bthci_EventCommandStatus(
	PADAPTER 					padapter,
	u8					OGF,
	u16					OCF,
	HCI_STATUS				HciStatus
	)
{

	u8 localBuf[6] = "";
	PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;
	u8	Num_Hci_Comm = 0x1;
	RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], CommandStatus, Opcode = 0x%02x%02x, OGF=0x%x,  OCF=0x%x, Status = 0x%x, Num_HCI_COMM = 0x%x\n",
		(HCIOPCODEHIGHT(OCF, OGF)), (HCIOPCODELOW(OCF, OGF)), OGF, OCF, HciStatus,Num_Hci_Comm));

	PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);
	PPacketIrpEvent->EventCode=HCI_EVENT_COMMAND_STATUS;
	PPacketIrpEvent->Length=4;
	PPacketIrpEvent->Data[0] = HciStatus;	//current pending
	PPacketIrpEvent->Data[1] = Num_Hci_Comm;	//packet #
	PPacketIrpEvent->Data[2] = HCIOPCODELOW(OCF, OGF);
	PPacketIrpEvent->Data[3] = HCIOPCODEHIGHT(OCF, OGF);

	bthci_IndicateEvent(padapter, PPacketIrpEvent, 6);

}

void
bthci_EventLogicalLinkComplete(
	PADAPTER 					padapter,
	HCI_STATUS				HciStatus,
	u8					PhyLinkHandle,
	u16					LogLinkHandle,
	u8					LogLinkIndex,
	u8					EntryNum
	)
{
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO		pBtHciInfo = &pBTInfo->BtHciInfo;
	u8	localBuf[7] = "";
	PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

	if (!(pBtHciInfo->BTEventMaskPage2 & EMP2_HCI_EVENT_LOGICAL_LINK_COMPLETE))
	{
		RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], Logical Link Complete, Ignore to send this event due to event mask page 2\n"));
		return;
	}
	RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], Logical Link Complete, PhyLinkHandle = 0x%x,  LogLinkHandle = 0x%x, Status= 0x%x\n",
		PhyLinkHandle, LogLinkHandle, HciStatus));


	PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);
	PPacketIrpEvent->EventCode = HCI_EVENT_LOGICAL_LINK_COMPLETE;
	PPacketIrpEvent->Length = 5;

	PPacketIrpEvent->Data[0] = HciStatus;//status code
	//Logical link handle
	PPacketIrpEvent->Data[1] = TWOBYTE_LOWBYTE(LogLinkHandle);
	PPacketIrpEvent->Data[2] = TWOBYTE_HIGHTBYTE(LogLinkHandle);
	//Physical link handle
	PPacketIrpEvent->Data[3] = TWOBYTE_LOWBYTE(PhyLinkHandle);
	//corresponding Tx flow spec ID
	if (HciStatus == HCI_STATUS_SUCCESS)
	{
		PPacketIrpEvent->Data[4] =
			pBTInfo->BtAsocEntry[EntryNum].LogLinkCmdData[LogLinkIndex].Tx_Flow_Spec.Identifier;
	}
	else
		PPacketIrpEvent->Data[4] = 0x0;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, 7);
}

void
bthci_EventDisconnectLogicalLinkComplete(
	PADAPTER 					padapter,
	HCI_STATUS				HciStatus,
	u16					LogLinkHandle,
	HCI_STATUS				Reason
	)
{
	u8 localBuf[6] = "";
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO		pBtHciInfo = &pBTInfo->BtHciInfo;
	PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

	if (!(pBtHciInfo->BTEventMaskPage2 & EMP2_HCI_EVENT_DISCONNECT_LOGICAL_LINK_COMPLETE))
	{
		RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], Disconnect Logical Link Complete, Ignore to send this event due to event mask page 2\n"));
		return;
	}
	RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], Disconnect Logical Link Complete, Status = 0x%x,LLH = 0x%x Reason =0x%x\n",HciStatus,LogLinkHandle,Reason));

	PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);
	PPacketIrpEvent->EventCode=HCI_EVENT_DISCONNECT_LOGICAL_LINK_COMPLETE;
	PPacketIrpEvent->Length=4;

	PPacketIrpEvent->Data[0] = HciStatus;
	//Logical link handle
	PPacketIrpEvent->Data[1] = TWOBYTE_LOWBYTE(LogLinkHandle);
	PPacketIrpEvent->Data[2] = TWOBYTE_HIGHTBYTE(LogLinkHandle);
	//Disconnect reason
	PPacketIrpEvent->Data[3] = Reason;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, 6);
}

void
bthci_EventFlushOccurred(
	PADAPTER 					padapter,
	u16					LogLinkHandle
	)
{
	u8	localBuf[4] = "";
	PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;
	RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("bthci_EventFlushOccurred(), LLH = 0x%x\n", LogLinkHandle));

	PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);
	PPacketIrpEvent->EventCode = HCI_EVENT_FLUSH_OCCRUED;
	PPacketIrpEvent->Length = 2;
	//Logical link handle
	PPacketIrpEvent->Data[0] = TWOBYTE_LOWBYTE(LogLinkHandle);
	PPacketIrpEvent->Data[1] = TWOBYTE_HIGHTBYTE(LogLinkHandle);

	bthci_IndicateEvent(padapter, PPacketIrpEvent, 4);
}

HCI_STATUS
bthci_BuildPhysicalLink(
	PADAPTER						padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd,
	u16							OCF
)
{
	HCI_STATUS 		status = HCI_STATUS_SUCCESS;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	u8			EntryNum, PLH;

	//Send HCI Command status event to AMP.
	bthci_EventCommandStatus(padapter,
			OGF_LINK_CONTROL_COMMANDS,
			OCF,
			HCI_STATUS_SUCCESS);

	PLH = *((u8*)pHciCmd->Data);

	// Check if resource or bt connection is under progress, if yes, reject the link creation.
	if (bthci_AddEntry(padapter) == _FALSE)
	{
		status = HCI_STATUS_CONNECT_RJT_LIMIT_RESOURCE;
		bthci_EventPhysicalLinkComplete(padapter, status, INVALID_ENTRY_NUM, PLH);
		return status;
	}

	EntryNum=pBtMgnt->CurrentConnectEntryNum;
	pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle = PLH;
	pBtMgnt->BtCurrentPhyLinkhandle = PLH;

	if (pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.AMPAssocfragment == NULL)
	{
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Create/Accept PhysicalLink, AMP controller is busy\n"));
		status = HCI_STATUS_CONTROLLER_BUSY;
		bthci_EventPhysicalLinkComplete(padapter, status, INVALID_ENTRY_NUM, PLH);
		return status;
	}

	// Record Key and the info
	pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKeyLen=(*((u8*)pHciCmd->Data+1));
	pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKeyType=(*((u8*)pHciCmd->Data+2));
	_rtw_memcpy(pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKey,
		(((u8*)pHciCmd->Data+3)), pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKeyLen);
#if (LOCAL_PMK == 1)
	_rtw_memcpy(pBTInfo->BtAsocEntry[EntryNum].PMK, testPMK, PMK_LEN);
#else
	_rtw_memcpy(pBTInfo->BtAsocEntry[EntryNum].PMK, pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKey, PMK_LEN);
#endif
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("BuildPhysicalLink, EntryNum = %d, PLH = 0x%x  KeyLen = 0x%x, KeyType =0x%x\n",
		EntryNum, pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle,
		pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKeyLen,
		pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKeyType));
	RTPRINT_DATA(FIOCTL, (IOCTL_BT_LOGO|IOCTL_BT_HCICMD), ("BtAMPKey\n"), pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKey,
		pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKeyLen);
	RTPRINT_DATA(FIOCTL, (IOCTL_BT_LOGO|IOCTL_BT_HCICMD), ("PMK\n"), pBTInfo->BtAsocEntry[EntryNum].PMK,
		PMK_LEN);

	if (OCF == HCI_CREATE_PHYSICAL_LINK)
	{
		BTHCI_SM_WITH_INFO(padapter, HCI_STATE_DISCONNECTED, STATE_CMD_CREATE_PHY_LINK, EntryNum);
	}
	else if (OCF == HCI_ACCEPT_PHYSICAL_LINK)
	{
		BTHCI_SM_WITH_INFO(padapter, HCI_STATE_DISCONNECTED, STATE_CMD_ACCEPT_PHY_LINK, EntryNum);
	}

	return status;
}

void
bthci_BuildLogicalLink(
	PADAPTER						padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd,
	u16						OCF
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info	pBTinfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTinfo->BtMgnt;
	u8	PhyLinkHandle, EntryNum;
	static u16 AssignLogHandle = 1;

	HCI_FLOW_SPEC	TxFlowSpec;
	HCI_FLOW_SPEC	RxFlowSpec;
	u32	MaxSDUSize, ArriveTime, Bandwidth;

	PhyLinkHandle = *((u8*)pHciCmd->Data);

	EntryNum = bthci_GetCurrentEntryNum(padapter, PhyLinkHandle);

	_rtw_memcpy(&TxFlowSpec,
		&pHciCmd->Data[1], sizeof(HCI_FLOW_SPEC));
	_rtw_memcpy(&RxFlowSpec,
		&pHciCmd->Data[17], sizeof(HCI_FLOW_SPEC));

#if 0	//for logo special test case only
		if (i==0)
		{
			bthci_SelectFlowType(padapter,BT_TX_BE_FS,BT_RX_BE_FS,&TxFlowSpec,&RxFlowSpec);
			i=1;
		}
		else if (i==1)
		{
			bthci_SelectFlowType(padapter,BT_TX_GU_FS,BT_RX_GU_FS,&TxFlowSpec,&RxFlowSpec);
			i=0;
		}
#endif

	MaxSDUSize = TxFlowSpec.MaximumSDUSize;
	ArriveTime = TxFlowSpec.SDUInterArrivalTime;

	if (bthci_CheckLogLinkBehavior(padapter, TxFlowSpec)&& bthci_CheckLogLinkBehavior(padapter, RxFlowSpec))
	{
		Bandwidth = BTTOTALBANDWIDTH;
	}
	else if (MaxSDUSize==0xffff && ArriveTime==0xffffffff)
	{
		Bandwidth = BTTOTALBANDWIDTH;
	}
	else
	{
		Bandwidth = MaxSDUSize*8*1000/(ArriveTime+244);
	}

#if 0
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("BuildLogicalLink, PhyLinkHandle = 0x%x, MaximumSDUSize = 0x%lx, SDUInterArrivalTime = 0x%lx, Bandwidth=0x%lx\n",
		PhyLinkHandle, MaxSDUSize,ArriveTime, Bandwidth));
#else
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("BuildLogicalLink, PhyLinkHandle=0x%x, MaximumSDUSize=0x%x, SDUInterArrivalTime=0x%x, Bandwidth=0x%x\n",
		PhyLinkHandle, MaxSDUSize, ArriveTime, Bandwidth));
#endif

	if (EntryNum == 0xff)
	{
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Invalid Physical Link handle = 0x%x, status=HCI_STATUS_UNKNOW_CONNECT_ID, return\n", PhyLinkHandle));
		status = HCI_STATUS_UNKNOW_CONNECT_ID;

		//When we receive Create/Accept logical link command, we should send command status event first.
		bthci_EventCommandStatus(padapter,
			OGF_LINK_CONTROL_COMMANDS,
			OCF,
			status);
		return;
	}

	if (pBtMgnt->bLogLinkInProgress == _FALSE)
	{
		if (bthci_PhyLinkConnectionInProgress(padapter, PhyLinkHandle))
		{
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Physical link connection in progress, status=HCI_STATUS_CMD_DISALLOW, return\n"));
			status = HCI_STATUS_CMD_DISALLOW;

			pBtMgnt->bPhyLinkInProgressStartLL = _TRUE;
			//When we receive Create/Accept logical link command, we should send command status event first.
			bthci_EventCommandStatus(padapter,
				OGF_LINK_CONTROL_COMMANDS,
				OCF,
				status);

			return;
		}

		if (Bandwidth > BTTOTALBANDWIDTH)//BTTOTALBANDWIDTH
		{
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("status=HCI_STATUS_QOS_REJECT, Bandwidth=0x%x, return\n", Bandwidth));
			status = HCI_STATUS_QOS_REJECT;

			//When we receive Create/Accept logical link command, we should send command status event first.
			bthci_EventCommandStatus(padapter,
				OGF_LINK_CONTROL_COMMANDS,
				OCF,
				status);
		}
		else
		{
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("status=HCI_STATUS_SUCCESS\n"));
			status = HCI_STATUS_SUCCESS;

			//When we receive Create/Accept logical link command, we should send command status event first.
			bthci_EventCommandStatus(padapter,
				OGF_LINK_CONTROL_COMMANDS,
				OCF,
				status);

#if 0// special logo test case only
			bthci_FakeCommand(padapter, OGF_LINK_CONTROL_COMMANDS, HCI_LOGICAL_LINK_CANCEL);
#endif
		}

		if (pBTinfo->BtAsocEntry[EntryNum].BtCurrentState != HCI_STATE_CONNECTED)
		{
			bthci_EventLogicalLinkComplete(padapter,
				HCI_STATUS_CMD_DISALLOW, 0, 0, 0,EntryNum);
		}
		else
		{
			u8 i, find=0;

			pBtMgnt->bLogLinkInProgress = _TRUE;

			// find an unused logical link index and copy the data
			for (i=0; i<MAX_LOGICAL_LINK_NUM; i++)
			{
				if (pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].BtLogLinkhandle == 0)
				{
					HCI_STATUS LogCompEventstatus = HCI_STATUS_SUCCESS;

					pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].BtPhyLinkhandle = *((u8*)pHciCmd->Data);
					pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].BtLogLinkhandle = AssignLogHandle;
					RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("BuildLogicalLink, EntryNum = %d, physical link handle = 0x%x, logical link handle = 0x%x\n",
						EntryNum, pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle,
								  pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].BtLogLinkhandle));
					_rtw_memcpy(&pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].Tx_Flow_Spec,
						&TxFlowSpec, sizeof(HCI_FLOW_SPEC));
					_rtw_memcpy(&pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].Rx_Flow_Spec,
						&RxFlowSpec, sizeof(HCI_FLOW_SPEC));

					pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].bLLCompleteEventIsSet=_FALSE;

					if (pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].bLLCancelCMDIsSetandComplete)
					{
						LogCompEventstatus = HCI_STATUS_UNKNOW_CONNECT_ID;
					}
					bthci_EventLogicalLinkComplete(padapter,
						LogCompEventstatus,
						pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].BtPhyLinkhandle,
						pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].BtLogLinkhandle, i,EntryNum);

					pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].bLLCompleteEventIsSet = _TRUE;

					find = 1;
					pBtMgnt->BtCurrentLogLinkhandle = AssignLogHandle;
					AssignLogHandle++;
					break;
				}
			}

			if (!find)
			{
				bthci_EventLogicalLinkComplete(padapter,
					HCI_STATUS_CONNECT_RJT_LIMIT_RESOURCE, 0, 0, 0,EntryNum);
			}
			pBtMgnt->bLogLinkInProgress = _FALSE;
		}
	}
	else
	{
		bthci_EventLogicalLinkComplete(padapter,
			HCI_STATUS_CONTROLLER_BUSY, 0, 0, 0,EntryNum);
	}

#if 0// special logo test case only
	bthci_FakeCommand(padapter, OGF_LINK_CONTROL_COMMANDS, HCI_LOGICAL_LINK_CANCEL);
#endif
}

void
bthci_StartBeaconAndConnect(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd,
	u8		CurrentAssocNum
	)
{
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("StartBeaconAndConnect, CurrentAssocNum=%d, AMPRole=%d\n",
		CurrentAssocNum,
		pBTInfo->BtAsocEntry[CurrentAssocNum].AMPRole));

	if (pBtMgnt->CheckChnlIsSuit == _FALSE)
	{
		bthci_EventPhysicalLinkComplete(padapter, HCI_STATUS_CONNECT_REJ_NOT_SUIT_CHNL_FOUND, CurrentAssocNum, INVALID_PL_HANDLE);
		bthci_RemoveEntryByEntryNum(padapter, CurrentAssocNum);
		return;
	}

	{
		if (pBTInfo->BtAsocEntry[CurrentAssocNum].AMPRole == AMP_BTAP_CREATOR)
		{
			rsprintf((char*)pBTInfo->BtAsocEntry[CurrentAssocNum].BTSsidBuf,32,"AMP-%02x-%02x-%02x-%02x-%02x-%02x",
#if 0
			padapter->PermanentAddress[0],
			padapter->PermanentAddress[1],
			padapter->PermanentAddress[2],
			padapter->PermanentAddress[3],
			padapter->PermanentAddress[4],
			padapter->PermanentAddress[5]);
#else
			padapter->eeprompriv.mac_addr[0],
			padapter->eeprompriv.mac_addr[1],
			padapter->eeprompriv.mac_addr[2],
			padapter->eeprompriv.mac_addr[3],
			padapter->eeprompriv.mac_addr[4],
			padapter->eeprompriv.mac_addr[5]);
#endif
		}
		else if (pBTInfo->BtAsocEntry[CurrentAssocNum].AMPRole == AMP_BTAP_JOINER)
		{
			rsprintf((char*)pBTInfo->BtAsocEntry[CurrentAssocNum].BTSsidBuf,32,"AMP-%02x-%02x-%02x-%02x-%02x-%02x",
			pBTInfo->BtAsocEntry[CurrentAssocNum].BTRemoteMACAddr[0],
			pBTInfo->BtAsocEntry[CurrentAssocNum].BTRemoteMACAddr[1],
			pBTInfo->BtAsocEntry[CurrentAssocNum].BTRemoteMACAddr[2],
			pBTInfo->BtAsocEntry[CurrentAssocNum].BTRemoteMACAddr[3],
			pBTInfo->BtAsocEntry[CurrentAssocNum].BTRemoteMACAddr[4],
			pBTInfo->BtAsocEntry[CurrentAssocNum].BTRemoteMACAddr[5]);
		}

		FillOctetString(pBTInfo->BtAsocEntry[CurrentAssocNum].BTSsid, pBTInfo->BtAsocEntry[CurrentAssocNum].BTSsidBuf, 21);
		pBTInfo->BtAsocEntry[CurrentAssocNum].BTSsid.Length = 21;

		//To avoid set the start ap or connect twice, or the original connection will be disconnected.
		if (!pBtMgnt->bBTConnectInProgress)
		{
			pBtMgnt->bBTConnectInProgress=_TRUE;
			RTPRINT(FIOCTL, IOCTL_STATE, ("[BT Flag], BT Connect in progress ON!!\n"));
			BTHCI_SM_WITH_INFO(padapter,HCI_STATE_STARTING,STATE_CMD_MAC_START_COMPLETE,CurrentAssocNum);

#if 0	//for logo special test case only
			bthci_BuildLogicalLink(padapter, pHciCmd, HCI_CREATE_LOGICAL_LINK);
#endif

			// 20100325 Joseph: Check RF ON/OFF.
			// If RF OFF, it reschedule connecting operation after 50ms.
			if (!bthci_CheckRfStateBeforeConnect(padapter))
				return;

			if (pBTInfo->BtAsocEntry[CurrentAssocNum].AMPRole == AMP_BTAP_CREATOR)
			{
//				BTPKT_StartBeacon(padapter, CurrentAssocNum); // not implement yet
				BTHCI_SM_WITH_INFO(padapter,HCI_STATE_CONNECTING,STATE_CMD_MAC_CONNECT_COMPLETE,CurrentAssocNum);
			}
			else if (pBTInfo->BtAsocEntry[CurrentAssocNum].AMPRole == AMP_BTAP_JOINER)
			{
				bthci_ResponderStartToScan(padapter);
 			}
		}
		RT_PRINT_STR(_module_rtl871x_mlme_c_, _drv_notice_, "StartBeaconAndConnect, SSID:\n", pBTInfo->BtAsocEntry[pBtMgnt->CurrentConnectEntryNum].BTSsid.Octet, pBTInfo->BtAsocEntry[pBtMgnt->CurrentConnectEntryNum].BTSsid.Length);
	}
}

void bthci_ResetBtMgnt(PBT_MGNT pBtMgnt)
{
	pBtMgnt->BtOperationOn = _FALSE;
	pBtMgnt->bBTConnectInProgress = _FALSE;
	pBtMgnt->bLogLinkInProgress = _FALSE;
	pBtMgnt->bPhyLinkInProgress = _FALSE;
	pBtMgnt->bPhyLinkInProgressStartLL = _FALSE;
	pBtMgnt->DisconnectEntryNum = 0xff;
	pBtMgnt->bStartSendSupervisionPkt = _FALSE;
	pBtMgnt->JoinerNeedSendAuth = _FALSE;
	pBtMgnt->CurrentBTConnectionCnt = 0;
	pBtMgnt->BTCurrentConnectType = BT_DISCONNECT;
	pBtMgnt->BTReceiveConnectPkt = BT_DISCONNECT;
	pBtMgnt->BTAuthCount = 0;
	pBtMgnt->btLogoTest = 0;
}

void bthci_ResetBtHciInfo(PBT_HCI_INFO pBtHciInfo)
{
	pBtHciInfo->BTEventMask = 0;
	pBtHciInfo->BTEventMaskPage2 = 0;
	pBtHciInfo->ConnAcceptTimeout =  10000;
	pBtHciInfo->PageTimeout  =  0x30;
	pBtHciInfo->LocationDomainAware = 0x0;
	pBtHciInfo->LocationDomain = 0x5858;
	pBtHciInfo->LocationDomainOptions = 0x58;
	pBtHciInfo->LocationOptions = 0x0;
	pBtHciInfo->FlowControlMode = 0x1;	// 0:Packet based data flow control mode(BR/EDR), 1: Data block based data flow control mode(AMP).

	pBtHciInfo->enFlush_LLH = 0;
	pBtHciInfo->FLTO_LLH = 0;

	//Test command only
	pBtHciInfo->bTestIsEnd = _TRUE;
	pBtHciInfo->bInTestMode = _FALSE;
	pBtHciInfo->bTestNeedReport = _FALSE;
	pBtHciInfo->TestScenario = 0xff;
	pBtHciInfo->TestReportInterval = 0x01;
	pBtHciInfo->TestCtrType = 0x5d;
	pBtHciInfo->TestEventType = 0x00;
	pBtHciInfo->TestNumOfFrame = 0;
	pBtHciInfo->TestNumOfErrFrame = 0;
	pBtHciInfo->TestNumOfBits = 0;
	pBtHciInfo->TestNumOfErrBits = 0;
}

void bthci_ResetBtSec(PADAPTER padapter, PBT_SECURITY pBtSec)
{
//	PMGNT_INFO	pMgntInfo = &padapter->MgntInfo;

	// Set BT used HW or SW encrypt !!
	if (GET_HAL_DATA(padapter)->bBTMode)
		pBtSec->bUsedHwEncrypt = _TRUE;
	else
		pBtSec->bUsedHwEncrypt = _FALSE;
	RT_TRACE(_module_rtl871x_security_c_, _drv_info_, ("%s: bUsedHwEncrypt=%d\n", __FUNCTION__, pBtSec->bUsedHwEncrypt));

	pBtSec->RSNIE.Octet = pBtSec->RSNIEBuf;
}

void bthci_ResetBtExtInfo(PBT_MGNT pBtMgnt)
{
	u8	i;

	for (i=0; i<MAX_BT_ASOC_ENTRY_NUM; i++)
	{
		pBtMgnt->ExtConfig.linkInfo[i].ConnectHandle = 0;
		pBtMgnt->ExtConfig.linkInfo[i].IncomingTrafficMode = 0;
		pBtMgnt->ExtConfig.linkInfo[i].OutgoingTrafficMode = 0;
		pBtMgnt->ExtConfig.linkInfo[i].BTProfile = BT_PROFILE_NONE;
		pBtMgnt->ExtConfig.linkInfo[i].BTCoreSpec = BT_SPEC_2_1_EDR;
		pBtMgnt->ExtConfig.linkInfo[i].BT_RSSI = 0;
		pBtMgnt->ExtConfig.linkInfo[i].TrafficProfile = BT_PROFILE_NONE;
		pBtMgnt->ExtConfig.linkInfo[i].linkRole = BT_LINK_MASTER;
	}

	pBtMgnt->ExtConfig.CurrentConnectHandle = 0;
	pBtMgnt->ExtConfig.CurrentIncomingTrafficMode = 0;
	pBtMgnt->ExtConfig.CurrentOutgoingTrafficMode = 0;
	pBtMgnt->ExtConfig.MIN_BT_RSSI = 0;
	pBtMgnt->ExtConfig.NumberOfHandle = 0;
	pBtMgnt->ExtConfig.NumberOfSCO = 0;
	pBtMgnt->ExtConfig.CurrentBTStatus = 0;
	pBtMgnt->ExtConfig.HCIExtensionVer = 0;

	pBtMgnt->ExtConfig.bManualControl = _FALSE;
	pBtMgnt->ExtConfig.bBTBusy = _FALSE;
	pBtMgnt->ExtConfig.bBTA2DPBusy = _FALSE;
}

HCI_STATUS bthci_CmdReset(PADAPTER _padapter, u8 bNeedSendEvent)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
	PADAPTER	padapter;
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo;
	PBT_MGNT		pBtMgnt;
	PBT_HCI_INFO		pBtHciInfo;
	PBT_SECURITY		pBtSec;
	PBT_DBG			pBtDbg;
	u8	i;


	RTPRINT(FIOCTL,IOCTL_BT_HCICMD, ("bthci_CmdReset()\n"));

	padapter = GetDefaultAdapter(_padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;
	pBtHciInfo = &pBTInfo->BtHciInfo;
	pBtSec = &pBTInfo->BtSec;
	pBtDbg = &pBTInfo->BtDbg;

	pBTInfo->padapter = padapter;

	for (i=0; i<MAX_BT_ASOC_ENTRY_NUM; i++)
	{
		bthci_ResetEntry(padapter, i);
	}

	bthci_ResetBtMgnt(pBtMgnt);
	bthci_ResetBtHciInfo(pBtHciInfo);
	bthci_ResetBtSec(padapter, pBtSec);

	pBtMgnt->BTChannel = BT_Default_Chnl;
	pBtMgnt->CheckChnlIsSuit = _TRUE;

	pBTInfo->BTBeaconTmrOn = _FALSE;
//	QosInitializeBssDesc(&pBtMgnt->bssDesc.BssQos); // not implement yet

	pBtMgnt->bCreateSpportQos=_TRUE;

	PlatformCancelTimer(padapter, &pBTInfo->BTSupervisionPktTimer);
#if (SENDTXMEHTOD == 0)
	PlatformCancelTimer(padapter, &pBTInfo->BTHCISendAclDataTimer);
#endif
	PlatformCancelTimer(padapter, &pBTInfo->BTHCIDiscardAclDataTimer);
	PlatformCancelTimer(padapter, &pBTInfo->BTBeaconTimer);

	HALBT_SetRtsCtsNoLenLimit(padapter);
	//
	// Maybe we need to take care Group != AES case !!
	// now we Pairwise and Group all used AES !!
//	BTPKT_ConstructRSNIE(padapter); // not implement yet

	bthci_ResetBtExtInfo(pBtMgnt);

	//send command complete event here when all data are received.
	if (bNeedSendEvent)
	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_RESET,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdWriteRemoteAMPAssoc(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_DBG			pBtDbg = &pBTInfo->BtDbg;
	u8			CurrentAssocNum;
	u8			PhyLinkHandle;

	pBtDbg->dbgHciInfo.hciCmdCntWriteRemoteAmpAssoc++;
	PhyLinkHandle = *((u8*)pHciCmd->Data);
	CurrentAssocNum = bthci_GetCurrentEntryNum(padapter, PhyLinkHandle);

	if (CurrentAssocNum == 0xff)
	{
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("WriteRemoteAMPAssoc, No such Handle in the Entry\n"));
		status = HCI_STATUS_UNKNOW_CONNECT_ID;
		bthci_EventWriteRemoteAmpAssoc(padapter, status, PhyLinkHandle);
		return status;
	}

	if (pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.AMPAssocfragment == NULL)
	{
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("WriteRemoteAMPAssoc, AMP controller is busy\n"));
		status = HCI_STATUS_CONTROLLER_BUSY;
		bthci_EventWriteRemoteAmpAssoc(padapter, status, PhyLinkHandle);
		return status;
	}

	pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.BtPhyLinkhandle = PhyLinkHandle;//*((u8*)pHciCmd->Data);
	pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.LenSoFar = *((u16*)((u8*)pHciCmd->Data+1));
	pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.AMPAssocRemLen = *((u16*)((u8*)pHciCmd->Data+3));

	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("WriteRemoteAMPAssoc, LenSoFar= 0x%x, AssocRemLen= 0x%x\n",
		pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.LenSoFar,pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.AMPAssocRemLen));

	RTPRINT_DATA(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("WriteRemoteAMPAssoc fragment \n"), pHciCmd->Data,pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.AMPAssocRemLen+5);
	if ((pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.AMPAssocRemLen) > MAX_AMP_ASSOC_FRAG_LEN)
	{
		_rtw_memcpy(((u8*)pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.AMPAssocfragment+(pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.LenSoFar*(sizeof(u8)))),
			(u8*)pHciCmd->Data+5,
			MAX_AMP_ASSOC_FRAG_LEN);
	}
	else
	{
		_rtw_memcpy((u8*)(pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.AMPAssocfragment)+(pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.LenSoFar*(sizeof(u8))),
			((u8*)pHciCmd->Data+5),
			(pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.AMPAssocRemLen));

		RTPRINT_DATA(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), "WriteRemoteAMPAssoc :\n",
			pHciCmd->Data+5, pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.AMPAssocRemLen);

		if (!bthci_GetAssocInfo(padapter, CurrentAssocNum))
			status=HCI_STATUS_INVALID_HCI_CMD_PARA_VALUE;

		bthci_EventWriteRemoteAmpAssoc(padapter, status, PhyLinkHandle);

		bthci_StartBeaconAndConnect(padapter,pHciCmd,CurrentAssocNum);
	}

	return status;
}

//7.3.13
HCI_STATUS bthci_CmdReadConnectionAcceptTimeout(PADAPTER padapter)
{
	HCI_STATUS		status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO	pBtHciInfo = &pBTInfo->BtHciInfo;

	{
		u8 localBuf[8] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;
		u16 *pu2Temp;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_READ_CONNECTION_ACCEPT_TIMEOUT,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		pu2Temp = (u16*)&pRetPar[1];		// Conn_Accept_Timeout
		*pu2Temp = pBtHciInfo->ConnAcceptTimeout;
		len += 3;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

//7.3.3
HCI_STATUS
bthci_CmdSetEventFilter(
	PADAPTER	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;

	return status;
}

//7.3.14
HCI_STATUS
bthci_CmdWriteConnectionAcceptTimeout(
	PADAPTER	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS		status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO	pBtHciInfo = &pBTInfo->BtHciInfo;
	u16	*pu2Temp;

	pu2Temp = (u16*)&pHciCmd->Data[0];
	pBtHciInfo->ConnAcceptTimeout = *pu2Temp;
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_DETAIL, ("ConnAcceptTimeout = 0x%x",
		pBtHciInfo->ConnAcceptTimeout));

	//send command complete event here when all data are received.
	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_WRITE_CONNECTION_ACCEPT_TIMEOUT,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdReadPageTimeout(
	PADAPTER	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS		status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO	pBtHciInfo = &pBTInfo->BtHciInfo;

	{
		u8 localBuf[8] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;
		u16 *pu2Temp;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_READ_PAGE_TIMEOUT,
			status);

		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Read PageTimeout = 0x%x\n", pBtHciInfo->PageTimeout));
		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		pu2Temp = (u16*)&pRetPar[1];		// Page_Timeout
		*pu2Temp = pBtHciInfo->PageTimeout;
		len+=3;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdWritePageTimeout(
	PADAPTER	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS		status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO	pBtHciInfo = &pBTInfo->BtHciInfo;
	u16	*pu2Temp;

	pu2Temp = (u16*)&pHciCmd->Data[0];
	pBtHciInfo->PageTimeout = *pu2Temp;
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Write PageTimeout = 0x%x\n",
		pBtHciInfo->PageTimeout));

	//send command complete event here when all data are received.
	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_WRITE_PAGE_TIMEOUT,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdReadLinkSupervisionTimeout(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS	status = HCI_STATUS_SUCCESS;
	PBT30Info	pBTinfo = GET_BT_INFO(padapter);
	u8			physicalLinkHandle, EntryNum;

	physicalLinkHandle = *((u8*)pHciCmd->Data);

	EntryNum = bthci_GetCurrentEntryNum(padapter, physicalLinkHandle);

	if (EntryNum == 0xff)
	{
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("ReadLinkSupervisionTimeout, No such Handle in the Entry\n"));
		status = HCI_STATUS_UNKNOW_CONNECT_ID;
		return status;
	}

	if (pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle != physicalLinkHandle)
		status = HCI_STATUS_UNKNOW_CONNECT_ID;

	{
		u8 localBuf[10] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;
		u16 *pu2Temp;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_READ_LINK_SUPERVISION_TIMEOUT,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;
		pRetPar[1] = pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle;
		pRetPar[2] = 0;
		pu2Temp = (u16*)&pRetPar[3];		// Conn_Accept_Timeout
		*pu2Temp = pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.LinkSuperversionTimeout;
		len += 5;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdWriteLinkSupervisionTimeout(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS	status = HCI_STATUS_SUCCESS;
	PBT30Info	pBTinfo = GET_BT_INFO(padapter);
	u8			physicalLinkHandle, EntryNum;

	physicalLinkHandle = *((u8*)pHciCmd->Data);

	EntryNum = bthci_GetCurrentEntryNum(padapter, physicalLinkHandle);

	if (EntryNum == 0xff)
	{
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("WriteLinkSupervisionTimeout, No such Handle in the Entry\n"));
		status=HCI_STATUS_UNKNOW_CONNECT_ID;
	}
	else
	{
		if (pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle != physicalLinkHandle)
			status = HCI_STATUS_UNKNOW_CONNECT_ID;
		else
		{
			pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.LinkSuperversionTimeout=*((u16 *)(((u8*)pHciCmd->Data)+2));
			RTPRINT(FIOCTL, IOCTL_STATE, ("BT Write LinkSuperversionTimeout[%d] = 0x%x\n",
				EntryNum, pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.LinkSuperversionTimeout));
		}
	}

	{
		u8 localBuf[8] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_WRITE_LINK_SUPERVISION_TIMEOUT,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;
		pRetPar[1] = pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle;
		pRetPar[2] = 0;
		len += 3;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdEnhancedFlush(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS		status = HCI_STATUS_SUCCESS;
	PBT30Info		pBTinfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO	pBtHciInfo = &pBTinfo->BtHciInfo;
	u16		logicHandle;
	u8		Packet_Type;

	logicHandle = *((u16*)&pHciCmd->Data[0]);
	Packet_Type = pHciCmd->Data[2];

	if (Packet_Type != 0)
	{
		status = HCI_STATUS_INVALID_HCI_CMD_PARA_VALUE;
	}
 	else
		pBtHciInfo->enFlush_LLH = logicHandle;

	if (bthci_DiscardTxPackets(padapter, pBtHciInfo->enFlush_LLH))
	{
		bthci_EventFlushOccurred(padapter, pBtHciInfo->enFlush_LLH);
	}

	// should send command status event
	bthci_EventCommandStatus(padapter,
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_ENHANCED_FLUSH,
			status);

	if (pBtHciInfo->enFlush_LLH)
	{
		bthci_EventEnhancedFlushComplete(padapter, pBtHciInfo->enFlush_LLH);
		pBtHciInfo->enFlush_LLH = 0;
	}

	return status;
}

HCI_STATUS
bthci_CmdReadLogicalLinkAcceptTimeout(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS		status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO	pBtHciInfo = &pBTInfo->BtHciInfo;

	{
		u8 localBuf[8] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;
		u16 *pu2Temp;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_READ_LOGICAL_LINK_ACCEPT_TIMEOUT,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;

		pu2Temp = (u16*)&pRetPar[1];		// Conn_Accept_Timeout
		*pu2Temp = pBtHciInfo->LogicalAcceptTimeout;
		len += 3;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdWriteLogicalLinkAcceptTimeout(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS		status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO	pBtHciInfo = &pBTInfo->BtHciInfo;

	pBtHciInfo->LogicalAcceptTimeout = *((u16*)pHciCmd->Data);

	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_WRITE_LOGICAL_LINK_ACCEPT_TIMEOUT,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;

		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}
	return status;
}

HCI_STATUS
bthci_CmdSetEventMask(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS		status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO	pBtHciInfo = &pBTInfo->BtHciInfo;
	u8 *pu8Temp;

	pu8Temp = (u8*)&pHciCmd->Data[0];
	pBtHciInfo->BTEventMask = *pu8Temp;
#if 0
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_DETAIL, ("BTEventMask = 0x%"i64fmt"x\n",
		((pBtHciInfo->BTEventMask & UINT64_C(0xffffffff00000000))>>32)));
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_DETAIL, ("%"i64fmt"x\n",
		(pBtHciInfo->BTEventMask & 0xffffffff)));
#else
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_DETAIL, ("BTEventMask = 0x%"i64fmt"x\n",
		pBtHciInfo->BTEventMask));
#endif

	//send command complete event here when all data are received.
	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_SET_EVENT_MASK,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

// 7.3.69
HCI_STATUS
bthci_CmdSetEventMaskPage2(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS		status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO	pBtHciInfo = &pBTInfo->BtHciInfo;
	u8	*pu8Temp;

	pu8Temp = (u8*)&pHciCmd->Data[0];
	pBtHciInfo->BTEventMaskPage2 = *pu8Temp;
#if 0
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("BTEventMaskPage2 = 0x%"i64fmt"x\n",
		((pBtHciInfo->BTEventMaskPage2& UINT64_C(0xffffffff00000000))>>32)));
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("%"i64fmt"x\n",
		(pBtHciInfo->BTEventMaskPage2&0xffffffff)));
#else
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("BTEventMaskPage2 = 0x%"i64fmt"x\n",
		pBtHciInfo->BTEventMaskPage2));
#endif

	//send command complete event here when all data are received.
	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_SET_EVENT_MASK_PAGE_2,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdReadLocationData(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS		status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO	pBtHciInfo = &pBTInfo->BtHciInfo;

	{
		u8 localBuf[12] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;
		u16 *pu2Temp;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_READ_LOCATION_DATA,
			status);
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("DomainAware = 0x%x\n", pBtHciInfo->LocationDomainAware));
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Domain = 0x%x\n", pBtHciInfo->LocationDomain));
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("DomainOptions = 0x%x\n", pBtHciInfo->LocationDomainOptions));
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Options = 0x%x\n", pBtHciInfo->LocationOptions));

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;

		pRetPar[1] = pBtHciInfo->LocationDomainAware;	//0x0;	// Location_Domain_Aware
		pu2Temp = (u16*)&pRetPar[2];					// Location_Domain
		*pu2Temp = pBtHciInfo->LocationDomain;		//0x5858;
		pRetPar[4] = pBtHciInfo->LocationDomainOptions;	//0x58;	//Location_Domain_Options
		pRetPar[5] = pBtHciInfo->LocationOptions;		//0x0;	//Location_Options
		len+=6;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}
	return status;
}

HCI_STATUS
bthci_CmdWriteLocationData(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO	pBtHciInfo = &pBTInfo->BtHciInfo;
	u16	*pu2Temp;

	pBtHciInfo->LocationDomainAware = pHciCmd->Data[0];
	pu2Temp = (u16*)&pHciCmd->Data[1];
	pBtHciInfo->LocationDomain = *pu2Temp;
	pBtHciInfo->LocationDomainOptions = pHciCmd->Data[3];
	pBtHciInfo->LocationOptions = pHciCmd->Data[4];
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("DomainAware = 0x%x\n", pBtHciInfo->LocationDomainAware));
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Domain = 0x%x\n", pBtHciInfo->LocationDomain));
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("DomainOptions = 0x%x\n", pBtHciInfo->LocationDomainOptions));
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Options = 0x%x\n", pBtHciInfo->LocationOptions));

	//send command complete event here when all data are received.
	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_WRITE_LOCATION_DATA,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdReadFlowControlMode(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO	pBtHciInfo = &pBTInfo->BtHciInfo;

	{
		u8 localBuf[7] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_READ_FLOW_CONTROL_MODE,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;
		pRetPar[1] = pBtHciInfo->FlowControlMode;	// Flow Control Mode
		len += 2;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}
	return status;
}

HCI_STATUS
bthci_CmdWriteFlowControlMode(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO	pBtHciInfo = &pBTInfo->BtHciInfo;

	pBtHciInfo->FlowControlMode = pHciCmd->Data[0];

	//send command complete event here when all data are received.
	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_WRITE_FLOW_CONTROL_MODE,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdReadBestEffortFlushTimeout(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info pBTinfo = GET_BT_INFO(padapter);
	u16		i, j, logicHandle;
	u32		BestEffortFlushTimeout = 0xffffffff;
	u8		find = 0;

	logicHandle = *((u16*)pHciCmd->Data);
	// find an matched logical link index and copy the data
	for (j=0; j<MAX_BT_ASOC_ENTRY_NUM; j++)
	{
		for (i=0; i<MAX_LOGICAL_LINK_NUM; i++)
		{
			if (pBTinfo->BtAsocEntry[j].LogLinkCmdData[i].BtLogLinkhandle == logicHandle)
			{
				BestEffortFlushTimeout = pBTinfo->BtAsocEntry[j].LogLinkCmdData[i].BestEffortFlushTimeout;
				find = 1;
				break;
			}
		}
	}

	if (!find)
		status = HCI_STATUS_UNKNOW_CONNECT_ID;

	{
		u8 localBuf[10] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;
		u32 *pu4Temp;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_READ_BEST_EFFORT_FLUSH_TIMEOUT,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;
		pu4Temp = (u32*)&pRetPar[1];				// Best_Effort_Flush_Timeout
		*pu4Temp = BestEffortFlushTimeout;
		len += 5;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}
	return status;
}

HCI_STATUS
bthci_CmdWriteBestEffortFlushTimeout(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info pBTinfo = GET_BT_INFO(padapter);
	u16		i, j, logicHandle;
	u32		BestEffortFlushTimeout = 0xffffffff;
	u8		find = 0;

	logicHandle = *((u16*)pHciCmd->Data);
	BestEffortFlushTimeout = *((u32 *)(pHciCmd->Data+1));

	// find an matched logical link index and copy the data
	for (j=0; j<MAX_BT_ASOC_ENTRY_NUM; j++)
	{
		for (i=0; i<MAX_LOGICAL_LINK_NUM; i++)
		{
			if (pBTinfo->BtAsocEntry[j].LogLinkCmdData[i].BtLogLinkhandle == logicHandle)
			{
				pBTinfo->BtAsocEntry[j].LogLinkCmdData[i].BestEffortFlushTimeout = BestEffortFlushTimeout;
				find = 1;
				break;
			}
		}
	}

	if (!find)
		status = HCI_STATUS_UNKNOW_CONNECT_ID;

	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_WRITE_BEST_EFFORT_FLUSH_TIMEOUT,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;
		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}
	return status;
}

HCI_STATUS
bthci_CmdShortRangeMode(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	u8			PhyLinkHandle, EntryNum, ShortRangeMode;

	PhyLinkHandle = pHciCmd->Data[0];
	ShortRangeMode = pHciCmd->Data[1];
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("PLH = 0x%x, Short_Range_Mode = 0x%x\n", PhyLinkHandle, ShortRangeMode));

	EntryNum = bthci_GetCurrentEntryNum(padapter, PhyLinkHandle);
	if (EntryNum != 0xff)
	{
		pBTInfo->BtAsocEntry[EntryNum].ShortRangeMode = ShortRangeMode;
	}
	else
	{
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("No such PLH(0x%x)\n", PhyLinkHandle));
		status = HCI_STATUS_UNKNOW_CONNECT_ID;
	}

	bthci_EventCommandStatus(padapter,
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_SHORT_RANGE_MODE,
			status);

	bthci_EventShortRangeModeChangeComplete(padapter, status, ShortRangeMode, EntryNum);

	return status;
}

HCI_STATUS bthci_CmdReadLocalSupportedCommands(PADAPTER padapter)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;

	// send command complete event here when all data are received.
	{
		//PVOID buffer = padapter->IrpHCILocalbuf.Ptr;
		u8 localBuf[TmpLocalBufSize] = "";
		u8 *pRetPar, *pSupportedCmds;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PlatformZeroMemory(&localBuf[0], TmpLocalBufSize);
		//PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(buffer);
		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_INFORMATIONAL_PARAMETERS,
			HCI_READ_LOCAL_SUPPORTED_COMMANDS,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		len += 1;
		pSupportedCmds = &pRetPar[1];
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[5]=0xc0\nBit [6]=Set Event Mask, [7]=Reset\n"));
		pSupportedCmds[5] = 0xc0;
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[6]=0x01\nBit [0]=Set Event Filter\n"));
		pSupportedCmds[6] = 0x01;
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[7]=0x0c\nBit [2]=Read Connection Accept Timeout, [3]=Write Connection Accept Timeout\n"));
		pSupportedCmds[7] = 0x0c;
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[10]=0x80\nBit [7]=Host Number Of Completed Packets\n"));
		pSupportedCmds[10] = 0x80;
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[11]=0x03\nBit [0]=Read Link Supervision Timeout, [1]=Write Link Supervision Timeout\n"));
		pSupportedCmds[11] = 0x03;
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[14]=0xa8\nBit [3]=Read Local Version Information, [5]=Read Local Supported Features, [7]=Read Buffer Size\n"));
		pSupportedCmds[14] = 0xa8;
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[15]=0x1c\nBit [2]=Read Failed Contact Count, [3]=Reset Failed Contact Count, [4]=Get Link Quality\n"));
		pSupportedCmds[15] = 0x1c;
		//pSupportedCmds[16] = 0x04;
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[19]=0x40\nBit [6]=Enhanced Flush\n"));
		pSupportedCmds[19] = 0x40;
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[21]=0xff\nBit [0]=Create Physical Link, [1]=Accept Physical Link, [2]=Disconnect Physical Link, [3]=Create Logical Link\n"));
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("	[4]=Accept Logical Link, [5]=Disconnect Logical Link, [6]=Logical Link Cancel, [7]=Flow Spec Modify\n"));
		pSupportedCmds[21] = 0xff;
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[22]=0xff\nBit [0]=Read Logical Link Accept Timeout, [1]=Write Logical Link Accept Timeout, [2]=Set Event Mask Page 2, [3]=Read Location Data\n"));
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("	[4]=Write Location Data, [5]=Read Local AMP Info, [6]=Read Local AMP_ASSOC, [7]=Write Remote AMP_ASSOC\n"));
		pSupportedCmds[22] = 0xff;
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[23]=0x07\nBit [0]=Read Flow Control Mode, [1]=Write Flow Control Mode, [2]=Read Data Block Size\n"));
		pSupportedCmds[23] = 0x07;
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[24]=0x1c\nBit [2]=Read Best Effort Flush Timeout, [3]=Write Best Effort Flush Timeout, [4]=Short Range Mode\n"));
		pSupportedCmds[24] = 0x1c;
		len += 64;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS bthci_CmdReadLocalSupportedFeatures(PADAPTER padapter)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;

	//send command complete event here when all data are received.
	{
		//PVOID buffer = padapter->IrpHCILocalbuf.Ptr;
		u8 localBuf[TmpLocalBufSize] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PlatformZeroMemory(&localBuf[0], TmpLocalBufSize);
		//PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(buffer);
		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_INFORMATIONAL_PARAMETERS,
			HCI_READ_LOCAL_SUPPORTED_FEATURES,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		len += 9;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}
	return status;
}

HCI_STATUS
bthci_CmdReadLocalAMPAssoc(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTInfo->BtMgnt;
	PBT_DBG		pBtDbg = &pBTInfo->BtDbg;
	u8			PhyLinkHandle, EntryNum;

	pBtDbg->dbgHciInfo.hciCmdCntReadLocalAmpAssoc++;
	PhyLinkHandle = *((u8*)pHciCmd->Data);
	EntryNum = bthci_GetCurrentEntryNum(padapter, PhyLinkHandle);

	if ((EntryNum==0xff) && PhyLinkHandle != 0)
	{
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("ReadLocalAMPAssoc, EntryNum = %d  !!!!!, physical link handle = 0x%x\n",
		EntryNum, PhyLinkHandle));
		status = HCI_STATUS_UNKNOW_CONNECT_ID;
	}
	else if (pBtMgnt->bPhyLinkInProgressStartLL)
	{
		status = HCI_STATUS_UNKNOW_CONNECT_ID;
		pBtMgnt->bPhyLinkInProgressStartLL = _FALSE;
	}
	else
	{
		pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.BtPhyLinkhandle = *((u8*)pHciCmd->Data);
		pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.LenSoFar = *((u16*)((u8*)pHciCmd->Data+1));
		pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.MaxRemoteASSOCLen = *((u16*)((u8*)pHciCmd->Data+3));
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD_DETAIL, ("ReadLocalAMPAssoc, LenSoFar=%d, MaxRemoteASSOCLen=%d\n",
			pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.LenSoFar,
			pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.MaxRemoteASSOCLen));
	}

	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("ReadLocalAMPAssoc, EntryNum = %d  !!!!!, physical link handle = 0x%x, LengthSoFar = %x  \n",
		EntryNum, PhyLinkHandle, pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.LenSoFar));

	//send command complete event here when all data are received.
	{
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		//PVOID buffer = padapter->IrpHCILocalbuf.Ptr;
		u8 localBuf[TmpLocalBufSize] = "";
		u16	*pRemainLen;
		u32	totalLen = 0;
		u16	typeLen=0, remainLen=0, ret_index=0;
		u8 *pRetPar;

		PlatformZeroMemory(&localBuf[0], TmpLocalBufSize);
		//PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(buffer);
		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		totalLen += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_STATUS_PARAMETERS,
			HCI_READ_LOCAL_AMP_ASSOC,
			status);
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("ReadLocalAMPAssoc, Remaining_Len=%d  \n", remainLen));
		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[totalLen];
		pRetPar[0] = status;		//status
		pRetPar[1] = *((u8*)pHciCmd->Data);
		pRemainLen = (u16*)&pRetPar[2];		// AMP_ASSOC_Remaining_Length
		totalLen += 4;	//[0]~[3]
		ret_index = 4;

		typeLen = bthci_AssocMACAddr(padapter, &pRetPar[ret_index]);
		totalLen += typeLen;
		remainLen += typeLen;
		ret_index += typeLen;
		typeLen = bthci_AssocPreferredChannelList(padapter, &pRetPar[ret_index], EntryNum);
		totalLen += typeLen;
		remainLen += typeLen;
		ret_index += typeLen;
		typeLen = bthci_PALCapabilities(padapter, &pRetPar[ret_index]);
		totalLen += typeLen;
		remainLen += typeLen;
		ret_index += typeLen;
		typeLen = bthci_AssocPALVer(padapter, &pRetPar[ret_index]);
		totalLen += typeLen;
		remainLen += typeLen;
#if 0//for logo special test case only
		ret_index += typeLen;
		typeLen = bthci_ReservedForTestingPLV(padapter, &pRetPar[ret_index]);
		totalLen += typeLen;
		remainLen += typeLen;
#endif
		PPacketIrpEvent->Length = (UCHAR)totalLen;
		*pRemainLen = remainLen;	// AMP_ASSOC_Remaining_Length
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("ReadLocalAMPAssoc, Remaining_Len=%d  \n", remainLen));
		RTPRINT_DATA(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("AMP_ASSOC_fragment : \n"), PPacketIrpEvent->Data, totalLen);

		bthci_IndicateEvent(padapter, PPacketIrpEvent, totalLen+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdReadFailedContactCounter(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{

	HCI_STATUS		status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO	pBtHciInfo = &pBTInfo->BtHciInfo;
	u16		handle;

	handle=*((u16*)pHciCmd->Data);
	//send command complete event here when all data are received.
	{
		u8 localBuf[TmpLocalBufSize] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PlatformZeroMemory(&localBuf[0], TmpLocalBufSize);
		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_STATUS_PARAMETERS,
			HCI_READ_FAILED_CONTACT_COUNTER,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		pRetPar[1] = TWOBYTE_LOWBYTE(handle);
		pRetPar[2] = TWOBYTE_HIGHTBYTE(handle);
		pRetPar[3] = TWOBYTE_LOWBYTE(pBtHciInfo->FailContactCount);
		pRetPar[4] = TWOBYTE_HIGHTBYTE(pBtHciInfo->FailContactCount);
		len += 5;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdResetFailedContactCounter(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS		status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO	pBtHciInfo = &pBTInfo->BtHciInfo;
	u16		handle;

	handle=*((u16*)pHciCmd->Data);
	pBtHciInfo->FailContactCount=0;

	//send command complete event here when all data are received.
	{
		//PVOID buffer = padapter->IrpHCILocalbuf.Ptr;
		u8 localBuf[TmpLocalBufSize] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PlatformZeroMemory(&localBuf[0], TmpLocalBufSize);
		//PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(buffer);
		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_STATUS_PARAMETERS,
			HCI_RESET_FAILED_CONTACT_COUNTER,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		pRetPar[1] = TWOBYTE_LOWBYTE(handle);
		pRetPar[2] = TWOBYTE_HIGHTBYTE(handle);
		len+=3;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}
	return status;
}

//
// BT 3.0+HS [Vol 2] 7.4.1
//
HCI_STATUS
bthci_CmdReadLocalVersionInformation(
	PADAPTER 	padapter
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;

	//send command complete event here when all data are received.
	{
		//PVOID buffer = padapter->IrpHCILocalbuf.Ptr;
		u8 localBuf[TmpLocalBufSize] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;
		u16 *pu2Temp;

		PlatformZeroMemory(&localBuf[0], TmpLocalBufSize);
		//PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(buffer);
		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_INFORMATIONAL_PARAMETERS,
			HCI_READ_LOCAL_VERSION_INFORMATION,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		pRetPar[1] = 0x05;					// HCI_Version
		pu2Temp = (u16*)&pRetPar[2];		// HCI_Revision
		*pu2Temp = 0x0001;
		pRetPar[4] = 0x05;					// LMP/PAL_Version
		pu2Temp = (u16*)&pRetPar[5];		// Manufacturer_Name
		*pu2Temp = 0x005d;
		pu2Temp = (u16*)&pRetPar[7];		// LMP/PAL_Subversion
		*pu2Temp = 0x0001;
		len += 9;
		PPacketIrpEvent->Length = len;

		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("LOCAL_VERSION_INFORMATION\n"));
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("Status  %x\n",status));
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("HCI_Version = 0x05\n"));
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("HCI_Revision = 0x0001\n"));
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("LMP/PAL_Version = 0x05\n"));
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("Manufacturer_Name = 0x0001\n"));
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("LMP/PAL_Subversion = 0x0001\n"));

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

//7.4.7
HCI_STATUS bthci_CmdReadDataBlockSize(PADAPTER padapter)
{
	HCI_STATUS			status = HCI_STATUS_SUCCESS;

	{
		//PVOID buffer = padapter->IrpHCILocalbuf.Ptr;
		u8 localBuf[TmpLocalBufSize] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;
		u16 *pu2Temp;

		PlatformZeroMemory(&localBuf[0], TmpLocalBufSize);
		//PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(buffer);
		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_INFORMATIONAL_PARAMETERS,
			HCI_READ_DATA_BLOCK_SIZE,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = HCI_STATUS_SUCCESS;		//status
		pu2Temp = (u16*)&pRetPar[1];		// Max_ACL_Data_Packet_Length
		*pu2Temp = Max80211PALPDUSize;

		pu2Temp = (u16*)&pRetPar[3];		// Data_Block_Length
		*pu2Temp = Max80211PALPDUSize;
		pu2Temp = (u16*)&pRetPar[5];		// Total_Num_Data_Blocks
		*pu2Temp = BTTotalDataBlockNum;
		len += 7;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

// 7.4.5
HCI_STATUS
bthci_CmdReadBufferSize(
	PADAPTER 					padapter
	)
{
	HCI_STATUS			status = HCI_STATUS_SUCCESS;

	{
		//PVOID buffer = padapter->IrpHCILocalbuf.Ptr;
		u8 localBuf[TmpLocalBufSize] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;
		u16 *pu2Temp;

		PlatformZeroMemory(&localBuf[0], TmpLocalBufSize);
		//PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(buffer);
		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_INFORMATIONAL_PARAMETERS,
			HCI_READ_BUFFER_SIZE,
			status);
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Synchronous_Data_Packet_Length = 0x%x\n", BTSynDataPacketLength));
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Total_Num_ACL_Data_Packets = 0x%x\n", BTTotalDataBlockNum));
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Total_Num_Synchronous_Data_Packets = 0x%x\n", BTTotalDataBlockNum));
		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		pu2Temp = (u16*)&pRetPar[1];		// HC_ACL_Data_Packet_Length
		*pu2Temp = Max80211PALPDUSize;

		pRetPar[3] = BTSynDataPacketLength;	// HC_Synchronous_Data_Packet_Length
		pu2Temp = (u16*)&pRetPar[4];		// HC_Total_Num_ACL_Data_Packets
		*pu2Temp = BTTotalDataBlockNum;
		pu2Temp = (u16*)&pRetPar[6];		// HC_Total_Num_Synchronous_Data_Packets
		*pu2Temp = BTTotalDataBlockNum;
		len += 8;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdReadLocalAMPInfo(
	PADAPTER 					padapter
	)
{
	HCI_STATUS			status = HCI_STATUS_SUCCESS;

	{
//		PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
		struct pwrctrl_priv *ppwrctrl = &padapter->pwrctrlpriv;
		u8 localBuf[TmpLocalBufSize] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;
		u16 *pu2Temp;
		u32 *pu4Temp;
		u32	TotalBandwidth=BTTOTALBANDWIDTH, MaxBandGUBandwidth=BTMAXBANDGUBANDWIDTH;
		u8	ControlType=0x01, AmpStatus=0x01;
		u32	MaxFlushTimeout=10000, BestEffortFlushTimeout=5000;
		u16 MaxPDUSize=Max80211PALPDUSize, PalCap=0x1, AmpAssocLen=Max80211AMPASSOCLen, MinLatency=20;

		if ((ppwrctrl->rfoff_reason & RF_CHANGE_BY_HW) ||
			(ppwrctrl->rfoff_reason & RF_CHANGE_BY_SW))
		{
			AmpStatus = AMP_STATUS_NO_CAPACITY_FOR_BT;
		}

		PlatformZeroMemory(&localBuf[0], TmpLocalBufSize);
		//PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(buffer);
		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_STATUS_PARAMETERS,
			HCI_READ_LOCAL_AMP_INFO,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		pRetPar[1] = AmpStatus;						// AMP_Status
		pu4Temp = (u32*)&pRetPar[2];		// Total_Bandwidth
		*pu4Temp = TotalBandwidth;//0x19bfcc00;//0x7530;
		pu4Temp = (u32*)&pRetPar[6];		// Max_Guaranteed_Bandwidth
		*pu4Temp = MaxBandGUBandwidth;//0x19bfcc00;//0x4e20;
		pu4Temp = (u32*)&pRetPar[10];		// Min_Latency
		*pu4Temp = MinLatency;//150;
		pu4Temp = (u32*)&pRetPar[14];		// Max_PDU_Size
		*pu4Temp = MaxPDUSize;
		pRetPar[18] = ControlType;					// Controller_Type
		pu2Temp = (u16*)&pRetPar[19];		// PAL_Capabilities
		*pu2Temp = PalCap;
		pu2Temp = (u16*)&pRetPar[21];		// AMP_ASSOC_Length
		*pu2Temp = AmpAssocLen;
		pu4Temp = (u32*)&pRetPar[23];		// Max_Flush_Timeout
		*pu4Temp = MaxFlushTimeout;
		pu4Temp = (u32*)&pRetPar[27];		// Best_Effort_Flush_Timeout
		*pu4Temp = BestEffortFlushTimeout;
		len += 31;
		PPacketIrpEvent->Length = len;
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("AmpStatus = 0x%x\n",
			AmpStatus));
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("TotalBandwidth = 0x%x, MaxBandGUBandwidth = 0x%x, MinLatency = 0x%x, \n MaxPDUSize = 0x%x, ControlType = 0x%x\n",
			TotalBandwidth,MaxBandGUBandwidth,MinLatency,MaxPDUSize,ControlType));
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("PalCap = 0x%x, AmpAssocLen = 0x%x, MaxFlushTimeout = 0x%x, BestEffortFlushTimeout = 0x%x\n",
			PalCap,AmpAssocLen,MaxFlushTimeout,BestEffortFlushTimeout));
		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdCreatePhysicalLink(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS	status;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_DBG		pBtDbg = &pBTInfo->BtDbg;

	pBtDbg->dbgHciInfo.hciCmdCntCreatePhyLink++;

	status = bthci_BuildPhysicalLink(padapter,
		pHciCmd, HCI_CREATE_PHYSICAL_LINK);

	return status;
}

HCI_STATUS
bthci_CmdReadLinkQuality(
	PADAPTER 					padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS			status = HCI_STATUS_SUCCESS;
	PBT30Info			pBTInfo = GET_BT_INFO(padapter);
	u16 				PLH;
	u8				EntryNum, LinkQuality=0x55;

	PLH = *((u16*)&pHciCmd->Data[0]);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("PLH = 0x%x\n", PLH));

	EntryNum = bthci_GetCurrentEntryNum(padapter, (u8)PLH);
	if (EntryNum == 0xff)
	{
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("No such PLH(0x%x)\n", PLH));
		status=HCI_STATUS_UNKNOW_CONNECT_ID;
	}

	{
		u8 localBuf[11] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_STATUS_PARAMETERS,
			HCI_READ_LINK_QUALITY,
			status);

		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, (" PLH = 0x%x\n Link Quality = 0x%x\n", PLH, LinkQuality));

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;			//status
		*((u16*)&(pRetPar[1])) = pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle;	// Handle
		pRetPar[3] = 0x55;	//Link Quailty
		len += 4;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS bthci_CmdReadRSSI(PADAPTER padapter)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
	return status;
}

HCI_STATUS
bthci_CmdCreateLogicalLink(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_DBG		pBtDbg = &pBTInfo->BtDbg;

	pBtDbg->dbgHciInfo.hciCmdCntCreateLogLink++;

	bthci_BuildLogicalLink(padapter, pHciCmd,
		HCI_CREATE_LOGICAL_LINK);

	return status;
}

HCI_STATUS
bthci_CmdAcceptLogicalLink(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_DBG		pBtDbg = &pBTInfo->BtDbg;

	pBtDbg->dbgHciInfo.hciCmdCntAcceptLogLink++;

	bthci_BuildLogicalLink(padapter, pHciCmd,
		HCI_ACCEPT_LOGICAL_LINK);

	return status;
}

HCI_STATUS
bthci_CmdDisconnectLogicalLink(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info	pBTinfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTinfo->BtMgnt;
	PBT_DBG		pBtDbg = &pBTinfo->BtDbg;
	u16	logicHandle;
	u8 i, j, find=0, LogLinkCount=0;

	pBtDbg->dbgHciInfo.hciCmdCntDisconnectLogLink++;

	logicHandle = *((u16*)pHciCmd->Data);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("DisconnectLogicalLink, logicHandle = 0x%x\n", logicHandle));

	// find an created logical link index and clear the data
	for (j=0; j<MAX_BT_ASOC_ENTRY_NUM;j++)
	{
		for (i=0; i<MAX_LOGICAL_LINK_NUM; i++)
		{
			if (pBTinfo->BtAsocEntry[j].LogLinkCmdData[i].BtLogLinkhandle == logicHandle)
			{
				RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("DisconnectLogicalLink, logicHandle is matched  0x%x\n", logicHandle));
				bthci_ResetFlowSpec(padapter, j, i);
				find = 1;
				pBtMgnt->DisconnectEntryNum = j;
				break;
			}
		}
	}

	if (!find)
		status = HCI_STATUS_UNKNOW_CONNECT_ID;

	// To check each
	for (i=0; i<MAX_LOGICAL_LINK_NUM; i++)
	{
		if (pBTinfo->BtAsocEntry[pBtMgnt->DisconnectEntryNum].LogLinkCmdData[i].BtLogLinkhandle !=0)
		{
			LogLinkCount++;
		}
	}

	//When we receive Create logical link command, we should send command status event first.
	bthci_EventCommandStatus(padapter,
			OGF_LINK_CONTROL_COMMANDS,
			HCI_DISCONNECT_LOGICAL_LINK,
			status);
	//
	//When we determines the logical link is established, we should send command complete event.
	//
	if (status == HCI_STATUS_SUCCESS)
	{
		bthci_EventDisconnectLogicalLinkComplete(padapter, status,
			logicHandle, HCI_STATUS_CONNECT_TERMINATE_LOCAL_HOST);
	}

	if (LogLinkCount == 0)
		PlatformSetTimer(padapter, &pBTinfo->BTDisconnectPhyLinkTimer, 100);

	return status;
}

HCI_STATUS
bthci_CmdLogicalLinkCancel(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
	PBT30Info	pBTinfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTinfo->BtMgnt;
	u8	CurrentEntryNum, CurrentLogEntryNum;

	u8	physicalLinkHandle, TxFlowSpecID,i;
	u16	CurrentLogicalHandle;

	physicalLinkHandle = *((u8*)pHciCmd->Data);
	TxFlowSpecID = *(((u8*)pHciCmd->Data)+1);

	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("LogicalLinkCancel, physicalLinkHandle = 0x%x, TxFlowSpecID = 0x%x\n",
		physicalLinkHandle, TxFlowSpecID));

	CurrentEntryNum=pBtMgnt->CurrentConnectEntryNum;
	CurrentLogicalHandle = pBtMgnt->BtCurrentLogLinkhandle;

	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("CurrentEntryNum=0x%x, CurrentLogicalHandle = 0x%x\n",
		CurrentEntryNum, CurrentLogicalHandle));

	CurrentLogEntryNum = 0xff;
	for (i=0; i<MAX_LOGICAL_LINK_NUM; i++)
	{
		if ((CurrentLogicalHandle == pBTinfo->BtAsocEntry[CurrentEntryNum].LogLinkCmdData[i].BtLogLinkhandle) &&
			(physicalLinkHandle == pBTinfo->BtAsocEntry[CurrentEntryNum].LogLinkCmdData[i].BtPhyLinkhandle))
		{
			CurrentLogEntryNum = i;
			break;
		}
	}

	if (CurrentLogEntryNum == 0xff)
	{
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("LogicalLinkCancel, CurrentLogEntryNum==0xff !!!!\n"));
		status=HCI_STATUS_UNKNOW_CONNECT_ID;
	}
	else
	{
		if (pBTinfo->BtAsocEntry[CurrentEntryNum].LogLinkCmdData[CurrentLogEntryNum].bLLCompleteEventIsSet)
		{
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("LogicalLinkCancel, LLCompleteEventIsSet!!!!\n"));
			status=HCI_STATUS_ACL_CONNECT_EXISTS;
		}
	}

	{
		u8 localBuf[8] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_LINK_CONTROL_COMMANDS,
			HCI_LOGICAL_LINK_CANCEL,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		pRetPar[1] = pBTinfo->BtAsocEntry[CurrentEntryNum].LogLinkCmdData[CurrentLogEntryNum].BtPhyLinkhandle;
		pRetPar[2] = pBTinfo->BtAsocEntry[CurrentEntryNum].LogLinkCmdData[CurrentLogEntryNum].BtTxFlowSpecID;
		len += 3;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	pBTinfo->BtAsocEntry[CurrentEntryNum].LogLinkCmdData[CurrentLogEntryNum].bLLCancelCMDIsSetandComplete=_TRUE;

	return status;
}

HCI_STATUS
bthci_CmdFlowSpecModify(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info pBTinfo = GET_BT_INFO(padapter);
	u8 i, j, find=0;
	u16 logicHandle;

	logicHandle = *((u16*)pHciCmd->Data);
	// find an matched logical link index and copy the data
	for (j=0;j<MAX_BT_ASOC_ENTRY_NUM;j++)
	{
		for (i=0; i<MAX_LOGICAL_LINK_NUM; i++)
		{
			if (pBTinfo->BtAsocEntry[j].LogLinkCmdData[i].BtLogLinkhandle == logicHandle)
			{
				_rtw_memcpy(&pBTinfo->BtAsocEntry[j].LogLinkCmdData[i].Tx_Flow_Spec,
					&pHciCmd->Data[2], sizeof(HCI_FLOW_SPEC));
				_rtw_memcpy(&pBTinfo->BtAsocEntry[j].LogLinkCmdData[i].Rx_Flow_Spec,
					&pHciCmd->Data[18], sizeof(HCI_FLOW_SPEC));

				bthci_CheckLogLinkBehavior(padapter, pBTinfo->BtAsocEntry[j].LogLinkCmdData[i].Tx_Flow_Spec);
				find = 1;
				break;
			}
		}
	}
	RTPRINT(FIOCTL, IOCTL_BT_LOGO, ("FlowSpecModify, LLH = 0x%x, \n",logicHandle));

	//When we receive Flow Spec Modify command, we should send command status event first.
	bthci_EventCommandStatus(padapter,
		OGF_LINK_CONTROL_COMMANDS,
		HCI_FLOW_SPEC_MODIFY,
		HCI_STATUS_SUCCESS);

	if (!find)
		status = HCI_STATUS_UNKNOW_CONNECT_ID;

	bthci_EventSendFlowSpecModifyComplete(padapter, status, logicHandle);

	return status;
}

HCI_STATUS
bthci_CmdAcceptPhysicalLink(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS	status;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_DBG		pBtDbg = &pBTInfo->BtDbg;

	pBtDbg->dbgHciInfo.hciCmdCntAcceptPhyLink++;

	status = bthci_BuildPhysicalLink(padapter,
		pHciCmd, HCI_ACCEPT_PHYSICAL_LINK);

	return status;
}

HCI_STATUS
bthci_CmdDisconnectPhysicalLink(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{

	HCI_STATUS	status = HCI_STATUS_SUCCESS;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_DBG		pBtDbg = &pBTInfo->BtDbg;
	u8		PLH, CurrentEntryNum, PhysLinkDisconnectReason;

	pBtDbg->dbgHciInfo.hciCmdCntDisconnectPhyLink++;

	PLH = *((u8*)pHciCmd->Data);
	PhysLinkDisconnectReason = (*((u8*)pHciCmd->Data+1));
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_DISCONNECT_PHYSICAL_LINK  PhyHandle = 0x%x, Reason=0x%x\n",
		PLH, PhysLinkDisconnectReason));

	CurrentEntryNum = bthci_GetCurrentEntryNum(padapter, PLH);

	if (CurrentEntryNum == 0xff)
	{
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("DisconnectPhysicalLink, No such Handle in the Entry\n"));
		status=HCI_STATUS_UNKNOW_CONNECT_ID;
		//return status;
	}

	pBTInfo->BtAsocEntry[CurrentEntryNum].PhyLinkDisconnectReason=(HCI_STATUS)PhysLinkDisconnectReason;
	//Send HCI Command status event to AMP.
	bthci_EventCommandStatus(padapter,
	OGF_LINK_CONTROL_COMMANDS,
	HCI_DISCONNECT_PHYSICAL_LINK,
	status);

	if (status != HCI_STATUS_SUCCESS)
		return status;

	if (pBTInfo->BtAsocEntry[CurrentEntryNum].BtCurrentState == HCI_STATE_DISCONNECTED)
	{
		BTHCI_SM_WITH_INFO(padapter, HCI_STATE_DISCONNECTED, STATE_CMD_DISCONNECT_PHY_LINK, CurrentEntryNum);
	}
	else
	{
		BTHCI_SM_WITH_INFO(padapter, HCI_STATE_DISCONNECTING, STATE_CMD_DISCONNECT_PHY_LINK, CurrentEntryNum);
	}

	return status;
}

HCI_STATUS
bthci_CmdSetACLLinkDataFlowMode(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTInfo->BtMgnt;

	pBtMgnt->ExtConfig.CurrentConnectHandle = *((u16*)pHciCmd->Data);
	pBtMgnt->ExtConfig.CurrentIncomingTrafficMode = *((u8*)pHciCmd->Data)+2;
	pBtMgnt->ExtConfig.CurrentOutgoingTrafficMode = *((u8*)pHciCmd->Data)+3;
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("Connection Handle = 0x%x, Incoming Traffic mode = 0x%x, Outgoing Traffic mode = 0x%x",
		pBtMgnt->ExtConfig.CurrentConnectHandle,
		pBtMgnt->ExtConfig.CurrentIncomingTrafficMode,
		pBtMgnt->ExtConfig.CurrentOutgoingTrafficMode));

	{
		u8 localBuf[8] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;
		u16 *pu2Temp;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_SET_ACL_LINK_DATA_FLOW_MODE,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status

		pu2Temp = (u16*)&pRetPar[1];
		*pu2Temp = pBtMgnt->ExtConfig.CurrentConnectHandle;
		len += 3;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdSetACLLinkStatus(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTInfo->BtMgnt;
	PBT_DBG		pBtDbg = &pBTInfo->BtDbg;
	u8		i;
	u8		*pTriple;

	pBtDbg->dbgHciInfo.hciCmdCntSetAclLinkStatus++;
	RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD_EXT, "SetACLLinkStatus, Hex Data :\n",
			&pHciCmd->Data[0], pHciCmd->Length);

	// Only Core Stack v251 and later version support this command.
	pBtMgnt->bSupportProfile = _TRUE;

	pBtMgnt->ExtConfig.NumberOfHandle= *((u8*)pHciCmd->Data);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("NumberOfHandle = 0x%x\n", pBtMgnt->ExtConfig.NumberOfHandle));

	pTriple = &pHciCmd->Data[1];
	for (i=0; i<pBtMgnt->ExtConfig.NumberOfHandle; i++)
	{
		pBtMgnt->ExtConfig.linkInfo[i].ConnectHandle = *((u16*)&pTriple[0]);
		pBtMgnt->ExtConfig.linkInfo[i].IncomingTrafficMode = pTriple[2];
		pBtMgnt->ExtConfig.linkInfo[i].OutgoingTrafficMode = pTriple[3];
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT,
			("Connection_Handle = 0x%x, Incoming Traffic mode = 0x%x, Outgoing Traffic Mode = 0x%x\n",
			pBtMgnt->ExtConfig.linkInfo[i].ConnectHandle,
			pBtMgnt->ExtConfig.linkInfo[i].IncomingTrafficMode,
			pBtMgnt->ExtConfig.linkInfo[i].OutgoingTrafficMode));
		pTriple += 4;
	}

	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_SET_ACL_LINK_STATUS,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status

		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdSetSCOLinkStatus(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTInfo->BtMgnt;
	PBT_DBG		pBtDbg = &pBTInfo->BtDbg;

	pBtDbg->dbgHciInfo.hciCmdCntSetScoLinkStatus++;
	pBtMgnt->ExtConfig.NumberOfSCO= *((u8*)pHciCmd->Data);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("NumberOfSCO = 0x%x\n",
		pBtMgnt->ExtConfig.NumberOfSCO));

	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_SET_SCO_LINK_STATUS,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status

		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdSetRSSIValue(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTInfo->BtMgnt;
	s8		min_bt_rssi = 0;
	u8		i;
#if 0
	if (pHciCmd->Length)
	{
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("pHciCmd->Length = 0x%x\n", pHciCmd->Length));
		RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD_EXT, "SetRSSIValue(), Hex Data :\n",
			&pHciCmd->Data[0], pHciCmd->Length);
	}
#endif
	for (i=0; i<pBtMgnt->ExtConfig.NumberOfHandle; i++)
	{
		if (pBtMgnt->ExtConfig.linkInfo[i].ConnectHandle == *((u16*)&pHciCmd->Data[0]))
		{
			pBtMgnt->ExtConfig.linkInfo[i].BT_RSSI = (s8)(pHciCmd->Data[2]);
			RTPRINT(FIOCTL, IOCTL_BT_EVENT_PERIODICAL,
			("Connection_Handle = 0x%x, RSSI = %d \n",
			pBtMgnt->ExtConfig.linkInfo[i].ConnectHandle,
			pBtMgnt->ExtConfig.linkInfo[i].BT_RSSI));
		}
		// get the minimum bt rssi value
		if (pBtMgnt->ExtConfig.linkInfo[i].BT_RSSI <= min_bt_rssi)
		{
			min_bt_rssi = pBtMgnt->ExtConfig.linkInfo[i].BT_RSSI;
		}
	}

	{
		pBtMgnt->ExtConfig.MIN_BT_RSSI = min_bt_rssi;
		RTPRINT(FBT, BT_TRACE, ("[bt rssi], the min rssi is %d\n", min_bt_rssi));
	}

	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_SET_RSSI_VALUE,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status

		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdSetCurrentBluetoothStatus(
	PADAPTER	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS	status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO	pMgntInfo = &padapter->MgntInfo;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTInfo->BtMgnt;

	pBtMgnt->ExtConfig.CurrentBTStatus = *((u8*)&pHciCmd->Data[0]);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("SetCurrentBluetoothStatus, CurrentBTStatus = 0x%x\n",
		pBtMgnt->ExtConfig.CurrentBTStatus));

	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_SET_CURRENT_BLUETOOTH_STATUS,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		len += 1;

		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdExtensionVersionNotify(
	PADAPTER 						padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS	status = HCI_STATUS_SUCCESS;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTInfo->BtMgnt;
	PBT_DBG		pBtDbg = &pBTInfo->BtDbg;

	pBtDbg->dbgHciInfo.hciCmdCntExtensionVersionNotify++;
	RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD_EXT, "ExtensionVersionNotify, Hex Data :\n",
			&pHciCmd->Data[0], pHciCmd->Length);

	pBtMgnt->ExtConfig.HCIExtensionVer = *((u16*)&pHciCmd->Data[0]);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCIExtensionVer = 0x%x\n", pBtMgnt->ExtConfig.HCIExtensionVer));

	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_EXTENSION_VERSION_NOTIFY,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status

		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdLinkStatusNotify(
	PADAPTER 						padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS	status = HCI_STATUS_SUCCESS;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTInfo->BtMgnt;
	PBT_DBG		pBtDbg = &pBTInfo->BtDbg;
	u8		i;
	u8		*pTriple;

	pBtDbg->dbgHciInfo.hciCmdCntLinkStatusNotify++;
	RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD_EXT, "LinkStatusNotify, Hex Data :\n",
			&pHciCmd->Data[0], pHciCmd->Length);

	// Current only RTL8723 support this command.
	pBtMgnt->bSupportProfile = _TRUE;

	pBtMgnt->ExtConfig.NumberOfHandle= *((u8*)pHciCmd->Data);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("NumberOfHandle = 0x%x\n", pBtMgnt->ExtConfig.NumberOfHandle));
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCIExtensionVer = %d\n", pBtMgnt->ExtConfig.HCIExtensionVer));

	pTriple = &pHciCmd->Data[1];
	for (i=0; i<pBtMgnt->ExtConfig.NumberOfHandle; i++)
	{
		if (pBtMgnt->ExtConfig.HCIExtensionVer < 1)
		{
			pBtMgnt->ExtConfig.linkInfo[i].ConnectHandle = *((u16*)&pTriple[0]);
			pBtMgnt->ExtConfig.linkInfo[i].BTProfile = pTriple[2];
			pBtMgnt->ExtConfig.linkInfo[i].BTCoreSpec = pTriple[3];
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT,
			("Connection_Handle = 0x%x, BTProfile=%d, BTSpec=%d\n",
				pBtMgnt->ExtConfig.linkInfo[i].ConnectHandle,
				pBtMgnt->ExtConfig.linkInfo[i].BTProfile,
				pBtMgnt->ExtConfig.linkInfo[i].BTCoreSpec));
		pTriple += 4;
	}
		else if (pBtMgnt->ExtConfig.HCIExtensionVer >= 1)
		{
			pBtMgnt->ExtConfig.linkInfo[i].ConnectHandle = *((u16*)&pTriple[0]);
			pBtMgnt->ExtConfig.linkInfo[i].BTProfile = pTriple[2];
			pBtMgnt->ExtConfig.linkInfo[i].BTCoreSpec = pTriple[3];
			pBtMgnt->ExtConfig.linkInfo[i].linkRole = pTriple[4];
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT,
				("Connection_Handle = 0x%x, BTProfile=%d, BTSpec=%d, LinkRole=%d\n",
				pBtMgnt->ExtConfig.linkInfo[i].ConnectHandle,
				pBtMgnt->ExtConfig.linkInfo[i].BTProfile,
				pBtMgnt->ExtConfig.linkInfo[i].BTCoreSpec,
				pBtMgnt->ExtConfig.linkInfo[i].linkRole));
			pTriple += 5;
		}

	}
	BTHCI_UpdateBTProfileRTKToMoto(padapter);
	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_LINK_STATUS_NOTIFY,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status

		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdBtOperationNotify(
	PADAPTER	padapter,
	PPACKET_IRP_HCICMD_DATA 	pHciCmd
	)
{
	HCI_STATUS	status = HCI_STATUS_SUCCESS;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTInfo->BtMgnt;

	RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD_EXT, "Bt Operation notify, Hex Data :\n",
			&pHciCmd->Data[0], pHciCmd->Length);

	pBtMgnt->ExtConfig.btOperationCode = *((u8*)pHciCmd->Data);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("btOperationCode = 0x%x\n", pBtMgnt->ExtConfig.btOperationCode));
	switch (pBtMgnt->ExtConfig.btOperationCode)
	{
		case HCI_BT_OP_NONE:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[bt operation] : Operation None!!\n"));
			break;
		case HCI_BT_OP_INQUIRY_START:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[bt operation] : Inquire start!!\n"));
			break;
		case HCI_BT_OP_INQUIRY_FINISH:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[bt operation] : Inquire finished!!\n"));
			break;
		case HCI_BT_OP_PAGING_START:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[bt operation] : Paging is started!!\n"));
			break;
		case HCI_BT_OP_PAGING_SUCCESS:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[bt operation] : Paging complete successfully!!\n"));
			break;
		case HCI_BT_OP_PAGING_UNSUCCESS:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[bt operation] : Paging complete unsuccessfully!!\n"));
			break;
		case HCI_BT_OP_PAIRING_START:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[bt operation] : Pairing start!!\n"));
			break;
		case HCI_BT_OP_PAIRING_FINISH:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[bt operation] : Pairing finished!!\n"));
			break;
		case HCI_BT_OP_BT_DEV_ENABLE:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[bt operation] : BT Device is enabled!!\n"));
			break;
		case HCI_BT_OP_BT_DEV_DISABLE:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[bt operation] : BT Device is disabled!!\n"));
			break;
		default:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[bt operation] : Unknown, error!!\n"));
			break;
	}
	BTDM_AdjustForBtOperation(padapter);
	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_BT_OPERATION_NOTIFY,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status

		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdEnableWifiScanNotify(
	PADAPTER	padapter,
	PPACKET_IRP_HCICMD_DATA 	pHciCmd
	)
{
	HCI_STATUS	status = HCI_STATUS_SUCCESS;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTInfo->BtMgnt;

	RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD_EXT, "Enable Wifi scan notify, Hex Data :\n",
			&pHciCmd->Data[0], pHciCmd->Length);

	pBtMgnt->ExtConfig.bEnableWifiScanNotify = *((u8*)pHciCmd->Data);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("bEnableWifiScanNotify = %d\n", pBtMgnt->ExtConfig.bEnableWifiScanNotify));

	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_ENABLE_WIFI_SCAN_NOTIFY,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status

		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdWIFICurrentChannel(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
//	u8		chnl = pMgntInfo->dot11CurrentChannelNumber;
	u8		chnl = pmlmeext->cur_channel;

//	if (pMgntInfo->pHTInfo->bCurBW40MHz == HT_CHANNEL_WIDTH_20_40)
	if (pmlmeext->cur_bwmode == HT_CHANNEL_WIDTH_40)
	{
//		if (pMgntInfo->pHTInfo->CurSTAExtChnlOffset == HT_EXTCHNL_OFFSET_UPPER)
		if (pmlmeext->cur_ch_offset == HAL_PRIME_CHNL_OFFSET_UPPER)
		{
			chnl += 2;
		}
//		else if (pMgntInfo->pHTInfo->CurSTAExtChnlOffset == HT_EXTCHNL_OFFSET_LOWER)
		else if (pmlmeext->cur_ch_offset == HAL_PRIME_CHNL_OFFSET_LOWER)
		{
			chnl -= 2;
		}
	}

	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("Current Channel  = 0x%x\n", chnl));

	{
		u8 localBuf[8] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_WIFI_CURRENT_CHANNEL,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		pRetPar[1] = chnl;			//current channel
		len += 2;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdWIFICurrentBandwidth(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
	HT_CHANNEL_WIDTH bw;
	u8	CurrentBW = 0;


//	padapter->HalFunc.GetHwRegHandler(padapter, HW_VAR_BW_MODE, (u8*)(&bw));
	bw = padapter->mlmeextpriv.cur_bwmode;

	if (bw == HT_CHANNEL_WIDTH_20)
	{
		CurrentBW = 0;
	}
	else if (bw == HT_CHANNEL_WIDTH_40)
	{
		CurrentBW = 1;
	}

	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("Current BW = 0x%x\n",
		CurrentBW));

	{
		u8 localBuf[8] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_WIFI_CURRENT_BANDWIDTH,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		pRetPar[1] = CurrentBW;		//current BW
		len += 2;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdWIFIConnectionStatus(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PADAPTER pDefaultAdapter = GetDefaultAdapter(padapter);
//	PADAPTER pExtAdapter = NULL;
//	PMGNT_INFO pExtMgntInfo = NULL;
	u8		connectStatus = HCI_WIFI_NOT_CONNECTED;

#if 0
	// Default port, connect to any
	if (pMgntInfo->bMediaConnect)
		connectStatus = HCI_WIFI_CONNECTED;
	if (pMgntInfo->mIbss)
		connectStatus = HCI_WIFI_CONNECTED;

	// AP mode, if any station associated
	if (padapter->MgntInfo.NdisVersion >= RT_NDIS_VERSION_6_20)
	{
		if (IsAPModeExist(padapter))
		{
			pExtAdapter = GetFirstExtAdapter(padapter);
			if (pExtAdapter == NULL) pExtAdapter = pDefaultAdapter;

			pExtMgntInfo = &pExtAdapter->MgntInfo;
			if (AsocEntry_AnyStationAssociated(pExtMgntInfo))
				connectStatus = HCI_WIFI_CONNECTED;
		}
		else
		{
			if (AsocEntry_AnyStationAssociated(pMgntInfo))
				connectStatus = HCI_WIFI_CONNECTED;
		}
	}
	else
	{
		if (AsocEntry_AnyStationAssociated(pMgntInfo))
			connectStatus = HCI_WIFI_CONNECTED;
	}

	if (connectStatus == HCI_WIFI_NOT_CONNECTED)
	{
		if (!MgntRoamingInProgress(pMgntInfo) &&
			!MgntIsLinkInProgress(pMgntInfo) &&
			!MgntScanInProgress(pMgntInfo))
		{
			connectStatus = HCI_WIFI_CONNECT_IN_PROGRESS;
		}
	}
#else
	if (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == _TRUE) {
		if (padapter->stapriv.asoc_sta_count >= 3)
			connectStatus = HCI_WIFI_CONNECTED;
		else
			connectStatus = HCI_WIFI_NOT_CONNECTED;
	}
	else if (check_fwstate(&padapter->mlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE|WIFI_ASOC_STATE) == _TRUE)
		connectStatus = HCI_WIFI_CONNECTED;
	else if (check_fwstate(&padapter->mlmepriv, WIFI_UNDER_LINKING) == _TRUE)
		connectStatus = HCI_WIFI_CONNECT_IN_PROGRESS;
	else
		connectStatus = HCI_WIFI_NOT_CONNECTED;
#endif

	{
		u8 localBuf[8] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_WIFI_CONNECTION_STATUS,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;			//status
		pRetPar[1] = connectStatus;	//connect status
		len += 2;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdEnableDeviceUnderTestMode(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO		pBtHciInfo = &pBTInfo->BtHciInfo;

	pBtHciInfo->bInTestMode = _TRUE;
	pBtHciInfo->bTestIsEnd = _FALSE;

	//send command complete event here when all data are received.
	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_TESTING_COMMANDS,
			HCI_ENABLE_DEVICE_UNDER_TEST_MODE,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdAMPTestEnd(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO		pBtHciInfo = &pBTInfo->BtHciInfo;
	u8		bFilterOutNonAssociatedBSSID = _TRUE;

	if (!pBtHciInfo->bInTestMode)
	{
		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Not in Test mode, return status=HCI_STATUS_CMD_DISALLOW\n"));
		status = HCI_STATUS_CMD_DISALLOW;
		return status;
	}

	pBtHciInfo->bTestIsEnd=_TRUE;

	PlatformCancelTimer(padapter,&pBTInfo->BTTestSendPacketTimer);

	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_CHECK_BSSID, (u8*)(&bFilterOutNonAssociatedBSSID));


	//send command complete event here when all data are received.
	{
		u8	localBuf[4] = "";
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("AMP Test End Event \n"));
		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);
		PPacketIrpEvent->EventCode=HCI_EVENT_AMP_TEST_END;
		PPacketIrpEvent->Length=2;

		PPacketIrpEvent->Data[0] = status;
		PPacketIrpEvent->Data[1] = pBtHciInfo->TestScenario;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, 4);
	}

	bthci_EventAMPReceiverReport(padapter,0x01);

	return status;
}

HCI_STATUS
bthci_CmdAMPTestCommand(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO		pBtHciInfo = &pBTInfo->BtHciInfo;

	if (!pBtHciInfo->bInTestMode)
	{
		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Not in Test mode, return status=HCI_STATUS_CMD_DISALLOW\n"));
		status = HCI_STATUS_CMD_DISALLOW;
		return status;
	}


	pBtHciInfo->TestScenario=*((u8*)pHciCmd->Data);

	if (pBtHciInfo->TestScenario == 0x01)
	{
		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("TX Single Test \n"));
	}
	else if (pBtHciInfo->TestScenario == 0x02)
	{
		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Receive Frame Test \n"));
	}
	else
	{
		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("No Such Test !!!!!!!!!!!!!!!!!! \n"));
	}


	if (pBtHciInfo->bTestIsEnd)
	{
		u8	localBuf[5] = "";
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;


		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("AMP Test End Event \n"));
		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);
		PPacketIrpEvent->EventCode=HCI_EVENT_AMP_TEST_END;
		PPacketIrpEvent->Length=2;

		PPacketIrpEvent->Data[0] = status;
		PPacketIrpEvent->Data[1] = pBtHciInfo->TestScenario ;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, 4);

		//Return to Idel state with RX and TX off.

		return status;
	}

	// should send command status event
	bthci_EventCommandStatus(padapter,
			OGF_TESTING_COMMANDS,
			HCI_AMP_TEST_COMMAND,
			status);

	//The HCI_AMP_Start Test Event shall be generated when the
	//HCI_AMP_Test_Command has completed and the first data is ready to be sent
	//or received.

	{
		u8	localBuf[5] = "";
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), (" HCI_AMP_Start Test Event \n"));
		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);
		PPacketIrpEvent->EventCode=HCI_EVENT_AMP_START_TEST;
		PPacketIrpEvent->Length=2;

		PPacketIrpEvent->Data[0] = status;
		PPacketIrpEvent->Data[1] = pBtHciInfo->TestScenario ;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, 4);

		//Return to Idel state with RX and TX off.
	}

	if (pBtHciInfo->TestScenario == 0x01)
	{
		/*
			When in a transmitter test scenario and the frames/bursts count have been
			transmitted the HCI_AMP_Test_End event shall be sent.
		*/
		PlatformSetTimer(padapter, &pBTInfo->BTTestSendPacketTimer, 50);
		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("TX Single Test \n"));
	}
	else if (pBtHciInfo->TestScenario == 0x02)
	{
		u8		bFilterOutNonAssociatedBSSID=_FALSE;
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_CHECK_BSSID, (u8*)(&bFilterOutNonAssociatedBSSID));
		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Receive Frame Test \n"));
	}

	return status;
}


HCI_STATUS
bthci_CmdEnableAMPReceiverReports(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO		pBtHciInfo = &pBTInfo->BtHciInfo;

	if (!pBtHciInfo->bInTestMode)
	{
		status = HCI_STATUS_CMD_DISALLOW;
		//send command complete event here when all data are received.
		{
			u8 localBuf[6] = "";
			u8 *pRetPar;
			u8 len = 0;
			PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

			PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

			len += bthci_CommandCompleteHeader(&localBuf[0],
				OGF_TESTING_COMMANDS,
				HCI_ENABLE_AMP_RECEIVER_REPORTS,
				status);

			// Return parameters starts from here
			pRetPar = &PPacketIrpEvent->Data[len];
			pRetPar[0] = status;		//status
			len += 1;
			PPacketIrpEvent->Length = len;

			bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
		}
		return status;
	}

	pBtHciInfo->bTestNeedReport= *((u8*)pHciCmd->Data);
	pBtHciInfo->TestReportInterval= (*((u8*)pHciCmd->Data+2));

	bthci_EventAMPReceiverReport(padapter,0x00);

	//send command complete event here when all data are received.
	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_TESTING_COMMANDS,
			HCI_ENABLE_AMP_RECEIVER_REPORTS,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdHostBufferSize(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	pBTInfo->BtAsocEntry[pBtMgnt->CurrentConnectEntryNum].ACLPacketsData.ACLDataPacketLen= *((u16*)pHciCmd->Data);
	pBTInfo->BtAsocEntry[pBtMgnt->CurrentConnectEntryNum].SyncDataPacketLen= *((u8 *)(pHciCmd->Data+2));
	pBTInfo->BtAsocEntry[pBtMgnt->CurrentConnectEntryNum].TotalNumACLDataPackets= *((u16 *)(pHciCmd->Data+3));
	pBTInfo->BtAsocEntry[pBtMgnt->CurrentConnectEntryNum].TotalSyncNumDataPackets= *((u16 *)(pHciCmd->Data+5));

	//send command complete event here when all data are received.
	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

		PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_HOST_BUFFER_SIZE,
			status);

		// Return parameters starts from here
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		//status
		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

HCI_STATUS
bthci_CmdHostNumberOfCompletedPackets(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;

	return status;
}

HCI_STATUS
bthci_UnknownCMD(
	PADAPTER	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_UNKNOW_HCI_CMD;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_DBG		pBtDbg = &pBTInfo->BtDbg;

	pBtDbg->dbgHciInfo.hciCmdCntUnknown++;
	bthci_EventCommandStatus(padapter,
			(u8)pHciCmd->OGF,
			pHciCmd->OCF,
			status);

	return status;
}

HCI_STATUS
bthci_HandleOGFInformationalParameters(
	PADAPTER 					padapter,
	PPACKET_IRP_HCICMD_DATA	pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;

	switch (pHciCmd->OCF)
	{
	case HCI_READ_LOCAL_VERSION_INFORMATION:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_LOCAL_VERSION_INFORMATION\n"));
		status = bthci_CmdReadLocalVersionInformation(padapter);
		break;
	case HCI_READ_LOCAL_SUPPORTED_COMMANDS:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_LOCAL_SUPPORTED_COMMANDS\n"));
		status = bthci_CmdReadLocalSupportedCommands(padapter);
		break;
	case HCI_READ_LOCAL_SUPPORTED_FEATURES:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_LOCAL_SUPPORTED_FEATURES\n"));
		status = bthci_CmdReadLocalSupportedFeatures(padapter);
		break;
	case HCI_READ_BUFFER_SIZE:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_BUFFER_SIZE\n"));
		status = bthci_CmdReadBufferSize(padapter);
		break;
	case HCI_READ_DATA_BLOCK_SIZE:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_DATA_BLOCK_SIZE\n"));
		status = bthci_CmdReadDataBlockSize(padapter);
		break;
	default:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("bthci_HandleOGFInformationalParameters(), Unknown case = 0x%x\n", pHciCmd->OCF));
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_UNKNOWN_COMMAND\n"));
		status = bthci_UnknownCMD(padapter, pHciCmd);
		break;
	}
	return status;
}

HCI_STATUS
bthci_HandleOGFSetEventMaskCMD(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;

	switch (pHciCmd->OCF)
	{
	case HCI_SET_EVENT_MASK:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_SET_EVENT_MASK\n"));
		status = bthci_CmdSetEventMask(padapter, pHciCmd);
		break;
	case HCI_RESET:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_RESET\n"));
		status = bthci_CmdReset(padapter, _TRUE);
		break;
	case HCI_READ_CONNECTION_ACCEPT_TIMEOUT:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_CONNECTION_ACCEPT_TIMEOUT\n"));
		status = bthci_CmdReadConnectionAcceptTimeout(padapter);
		break;
	case HCI_SET_EVENT_FILTER:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_SET_EVENT_FILTER\n"));
		status = bthci_CmdSetEventFilter(padapter, pHciCmd);
		break;
	case HCI_WRITE_CONNECTION_ACCEPT_TIMEOUT:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_WRITE_CONNECTION_ACCEPT_TIMEOUT\n"));
		status = bthci_CmdWriteConnectionAcceptTimeout(padapter, pHciCmd);
		break;
	case HCI_READ_PAGE_TIMEOUT:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_PAGE_TIMEOUT\n"));
		status = bthci_CmdReadPageTimeout(padapter, pHciCmd);
		break;
	case HCI_WRITE_PAGE_TIMEOUT:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_WRITE_PAGE_TIMEOUT\n"));
		status = bthci_CmdWritePageTimeout(padapter, pHciCmd);
		break;
	case HCI_HOST_NUMBER_OF_COMPLETED_PACKETS:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_HOST_NUMBER_OF_COMPLETED_PACKETS\n"));
		status = bthci_CmdHostNumberOfCompletedPackets(padapter, pHciCmd);
		break;
	case HCI_READ_LINK_SUPERVISION_TIMEOUT:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_LINK_SUPERVISION_TIMEOUT\n"));
		status = bthci_CmdReadLinkSupervisionTimeout(padapter, pHciCmd);
		break;
	case HCI_WRITE_LINK_SUPERVISION_TIMEOUT:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_WRITE_LINK_SUPERVISION_TIMEOUT\n"));
		status = bthci_CmdWriteLinkSupervisionTimeout(padapter, pHciCmd);
		break;
	case HCI_ENHANCED_FLUSH:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_ENHANCED_FLUSH\n"));
		status = bthci_CmdEnhancedFlush(padapter, pHciCmd);
		break;
	case HCI_READ_LOGICAL_LINK_ACCEPT_TIMEOUT:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_LOGICAL_LINK_ACCEPT_TIMEOUT\n"));
		status = bthci_CmdReadLogicalLinkAcceptTimeout(padapter, pHciCmd);
		break;
	case HCI_WRITE_LOGICAL_LINK_ACCEPT_TIMEOUT:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_WRITE_LOGICAL_LINK_ACCEPT_TIMEOUT\n"));
		status = bthci_CmdWriteLogicalLinkAcceptTimeout(padapter, pHciCmd);
		break;
	case HCI_SET_EVENT_MASK_PAGE_2:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_SET_EVENT_MASK_PAGE_2\n"));
		status = bthci_CmdSetEventMaskPage2(padapter, pHciCmd);
		break;
	case HCI_READ_LOCATION_DATA:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_LOCATION_DATA\n"));
		status = bthci_CmdReadLocationData(padapter, pHciCmd);
		break;
	case HCI_WRITE_LOCATION_DATA:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_WRITE_LOCATION_DATA\n"));
		status = bthci_CmdWriteLocationData(padapter, pHciCmd);
		break;
	case HCI_READ_FLOW_CONTROL_MODE:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_FLOW_CONTROL_MODE\n"));
		status = bthci_CmdReadFlowControlMode(padapter, pHciCmd);
		break;
	case HCI_WRITE_FLOW_CONTROL_MODE:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_WRITE_FLOW_CONTROL_MODE\n"));
		status = bthci_CmdWriteFlowControlMode(padapter, pHciCmd);
		break;
	case HCI_READ_BEST_EFFORT_FLUSH_TIMEOUT:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_BEST_EFFORT_FLUSH_TIMEOUT\n"));
		status = bthci_CmdReadBestEffortFlushTimeout(padapter, pHciCmd);
		break;
	case HCI_WRITE_BEST_EFFORT_FLUSH_TIMEOUT:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_WRITE_BEST_EFFORT_FLUSH_TIMEOUT\n"));
		status = bthci_CmdWriteBestEffortFlushTimeout(padapter, pHciCmd);
		break;
	case HCI_SHORT_RANGE_MODE:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_SHORT_RANGE_MODE\n"));
		status = bthci_CmdShortRangeMode(padapter, pHciCmd);
		break;
	case HCI_HOST_BUFFER_SIZE:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_HOST_BUFFER_SIZE\n"));
		status = bthci_CmdHostBufferSize(padapter,pHciCmd);
		break;
 	default:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("bthci_HandleOGFSetEventMaskCMD(), Unknown case = 0x%x\n", pHciCmd->OCF));
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_UNKNOWN_COMMAND\n"));
		status = bthci_UnknownCMD(padapter, pHciCmd);
		break;
	}
	return status;
}

HCI_STATUS
bthci_HandleOGFStatusParameters(
	PADAPTER 						padapter,
	PPACKET_IRP_HCICMD_DATA	pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;

	switch (pHciCmd->OCF)
	{
	case HCI_READ_FAILED_CONTACT_COUNTER:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_FAILED_CONTACT_COUNTER\n"));
		status = bthci_CmdReadFailedContactCounter(padapter,pHciCmd);
		break;
	case HCI_RESET_FAILED_CONTACT_COUNTER:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_RESET_FAILED_CONTACT_COUNTER\n"));
		status = bthci_CmdResetFailedContactCounter(padapter,pHciCmd);
		break;
	case HCI_READ_LINK_QUALITY:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_LINK_QUALITY\n"));
		status = bthci_CmdReadLinkQuality(padapter, pHciCmd);
		break;
	case HCI_READ_RSSI:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_RSSI\n"));
		status = bthci_CmdReadRSSI(padapter);
		break;
	case HCI_READ_LOCAL_AMP_INFO:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_LOCAL_AMP_INFO\n"));
		status = bthci_CmdReadLocalAMPInfo(padapter);
		break;
	case HCI_READ_LOCAL_AMP_ASSOC:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_LOCAL_AMP_ASSOC\n"));
                status = bthci_CmdReadLocalAMPAssoc(padapter,pHciCmd);
		break;
	case HCI_WRITE_REMOTE_AMP_ASSOC:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_WRITE_REMOTE_AMP_ASSOC\n"));
		status = bthci_CmdWriteRemoteAMPAssoc(padapter,pHciCmd);
		break;
	default:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("bthci_HandleOGFStatusParameters(), Unknown case = 0x%x\n", pHciCmd->OCF));
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_UNKNOWN_COMMAND\n"));
		status = bthci_UnknownCMD(padapter, pHciCmd);
		break;
	}
	return status;
}


HCI_STATUS
bthci_HandleOGFLinkControlCMD(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;

	switch (pHciCmd->OCF)
	{
		case HCI_CREATE_PHYSICAL_LINK:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_CREATE_PHYSICAL_LINK\n"));
			status = bthci_CmdCreatePhysicalLink(padapter,pHciCmd);
			break;
		case HCI_ACCEPT_PHYSICAL_LINK:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_ACCEPT_PHYSICAL_LINK\n"));
			status = bthci_CmdAcceptPhysicalLink(padapter,pHciCmd);
			break;
		case HCI_DISCONNECT_PHYSICAL_LINK:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_DISCONNECT_PHYSICAL_LINK\n"));
			status = bthci_CmdDisconnectPhysicalLink(padapter,pHciCmd);
			break;
		case HCI_CREATE_LOGICAL_LINK:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_CREATE_LOGICAL_LINK\n"));
			status = bthci_CmdCreateLogicalLink(padapter,pHciCmd);
			break;
		case HCI_ACCEPT_LOGICAL_LINK:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_ACCEPT_LOGICAL_LINK\n"));
			status = bthci_CmdAcceptLogicalLink(padapter,pHciCmd);
			break;
		case HCI_DISCONNECT_LOGICAL_LINK:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_DISCONNECT_LOGICAL_LINK\n"));
			status = bthci_CmdDisconnectLogicalLink(padapter,pHciCmd);
			break;
		case HCI_LOGICAL_LINK_CANCEL:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_LOGICAL_LINK_CANCEL\n"));
			status = bthci_CmdLogicalLinkCancel(padapter,pHciCmd);
			break;
		case HCI_FLOW_SPEC_MODIFY:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_FLOW_SPEC_MODIFY\n"));
			status = bthci_CmdFlowSpecModify(padapter,pHciCmd);
			break;

		default:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("bthci_HandleOGFLinkControlCMD(), Unknown case = 0x%x\n", pHciCmd->OCF));
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_UNKNOWN_COMMAND\n"));
			status = bthci_UnknownCMD(padapter, pHciCmd);
			break;
	}
	return status;
}

HCI_STATUS
bthci_HandleOGFTestingCMD(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
	switch (pHciCmd->OCF)
	{
		case HCI_ENABLE_DEVICE_UNDER_TEST_MODE:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_ENABLE_DEVICE_UNDER_TEST_MODE\n"));
			bthci_CmdEnableDeviceUnderTestMode(padapter,pHciCmd);
			break;
		case HCI_AMP_TEST_END:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_AMP_TEST_END\n"));
			bthci_CmdAMPTestEnd(padapter,pHciCmd);
			break;
		case HCI_AMP_TEST_COMMAND:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_AMP_TEST_COMMAND\n"));
			bthci_CmdAMPTestCommand(padapter,pHciCmd);
			break;
		case HCI_ENABLE_AMP_RECEIVER_REPORTS:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_ENABLE_AMP_RECEIVER_REPORTS\n"));
			bthci_CmdEnableAMPReceiverReports(padapter,pHciCmd);
			break;

		default:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_UNKNOWN_COMMAND\n"));
			status = bthci_UnknownCMD(padapter, pHciCmd);
			break;
	}
	return status;
}

HCI_STATUS
bthci_HandleOGFExtension(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	HCI_STATUS status = HCI_STATUS_SUCCESS;
	switch (pHciCmd->OCF)
	{
		case HCI_SET_ACL_LINK_DATA_FLOW_MODE:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_SET_ACL_LINK_DATA_FLOW_MODE\n"));
			status = bthci_CmdSetACLLinkDataFlowMode(padapter,pHciCmd);
			break;
		case HCI_SET_ACL_LINK_STATUS:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_SET_ACL_LINK_STATUS\n"));
			status = bthci_CmdSetACLLinkStatus(padapter,pHciCmd);
			break;
		case HCI_SET_SCO_LINK_STATUS:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_SET_SCO_LINK_STATUS\n"));
			status = bthci_CmdSetSCOLinkStatus(padapter,pHciCmd);
			break;
		case HCI_SET_RSSI_VALUE:
			RTPRINT(FIOCTL, IOCTL_BT_EVENT_PERIODICAL, ("HCI_SET_RSSI_VALUE\n"));
			status = bthci_CmdSetRSSIValue(padapter,pHciCmd);
			break;
		case HCI_SET_CURRENT_BLUETOOTH_STATUS:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_SET_CURRENT_BLUETOOTH_STATUS\n"));
			status = bthci_CmdSetCurrentBluetoothStatus(padapter,pHciCmd);
			break;
		//The following is for RTK8723

		case HCI_EXTENSION_VERSION_NOTIFY:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_EXTENSION_VERSION_NOTIFY\n"));
			status = bthci_CmdExtensionVersionNotify(padapter,pHciCmd);
			break;
		case HCI_LINK_STATUS_NOTIFY:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_LINK_STATUS_NOTIFY\n"));
			status = bthci_CmdLinkStatusNotify(padapter,pHciCmd);
			break;
		case HCI_BT_OPERATION_NOTIFY:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_BT_OPERATION_NOTIFY\n"));
			status = bthci_CmdBtOperationNotify(padapter,pHciCmd);
			break;
		case HCI_ENABLE_WIFI_SCAN_NOTIFY:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_ENABLE_WIFI_SCAN_NOTIFY\n"));
			status = bthci_CmdEnableWifiScanNotify(padapter,pHciCmd);
			break;

		//The following is for IVT
		case HCI_WIFI_CURRENT_CHANNEL:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_WIFI_CURRENT_CHANNEL\n"));
			status = bthci_CmdWIFICurrentChannel(padapter,pHciCmd);
			break;
		case HCI_WIFI_CURRENT_BANDWIDTH:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_WIFI_CURRENT_BANDWIDTH\n"));
			status = bthci_CmdWIFICurrentBandwidth(padapter,pHciCmd);
			break;
		case HCI_WIFI_CONNECTION_STATUS:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_WIFI_CONNECTION_STATUS\n"));
			status = bthci_CmdWIFIConnectionStatus(padapter,pHciCmd);
			break;

		default:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_UNKNOWN_COMMAND\n"));
			status = bthci_UnknownCMD(padapter, pHciCmd);
			break;
	}
	return status;
}

void
bthci_FakeCommand(
	PADAPTER 	padapter,
	u16	OGF,
	u16	OCF
	)
{
#define MAX_TMP_BUF_SIZE	200
	u8	buffer[MAX_TMP_BUF_SIZE];
	PPACKET_IRP_HCICMD_DATA pCmd=(PPACKET_IRP_HCICMD_DATA)buffer;

	PlatformZeroMemory(buffer, MAX_TMP_BUF_SIZE);

	pCmd->OGF = OGF;
	pCmd->OCF = OCF;

	if (OGF == OGF_LINK_CONTROL_COMMANDS && OCF == HCI_LOGICAL_LINK_CANCEL)
	{
		pCmd->Length = 2;
		pCmd->Data[0] = 1;		//physical link handle
		pCmd->Data[1] = 0x16;		//Tx_Flow_Spec_ID
		BTHCI_HandleHCICMD(padapter, (PPACKET_IRP_HCICMD_DATA)buffer);
	}
}

void
bthci_StateStarting(
	PADAPTER 					padapter,
	HCI_STATE_WITH_CMD		StateCmd,
	u8					EntryNum
	)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	RTPRINT(FIOCTL, IOCTL_STATE, ("[BT state], [Starting], "));
	switch (StateCmd)
	{
		case STATE_CMD_CONNECT_ACCEPT_TIMEOUT:
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_CONNECT_ACCEPT_TIMEOUT\n"));
			pBTInfo->BtAsocEntry[EntryNum].PhysLinkCompleteStatus=HCI_STATUS_CONNECT_ACCEPT_TIMEOUT;
			pBtMgnt->bNeedNotifyAMPNoCap = _TRUE;
			BTHCI_DisconnectPeer(padapter,EntryNum);
			break;
		}

		case STATE_CMD_DISCONNECT_PHY_LINK:
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_DISCONNECT_PHY_LINK\n"));

			bthci_EventDisconnectPhyLinkComplete(padapter,
			HCI_STATUS_SUCCESS,
			pBTInfo->BtAsocEntry[EntryNum].PhyLinkDisconnectReason,
			EntryNum);

			PlatformCancelTimer(padapter,&pBTInfo->BTHCIJoinTimeoutTimer);

			pBTInfo->BtAsocEntry[EntryNum].PhysLinkCompleteStatus=HCI_STATUS_UNKNOW_CONNECT_ID;

			BTHCI_DisconnectPeer(padapter, EntryNum);
			break;
		}

		case STATE_CMD_MAC_START_COMPLETE:
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_MAC_START_COMPLETE\n"));
			if (pBTInfo->BtAsocEntry[EntryNum].AMPRole == AMP_BTAP_JOINER)
			{
	 		}
			else if (pBTInfo->BtAsocEntry[EntryNum].AMPRole == AMP_BTAP_CREATOR)
			{
				bthci_EventChannelSelected(padapter,EntryNum);
			}

			break;
		}

		default:
			RTPRINT(FIOCTL, IOCTL_STATE, ("State command(%d) is Wrong !!!\n", StateCmd));
			break;
	}
}

void
bthci_StateConnecting(
	PADAPTER 					padapter,
	HCI_STATE_WITH_CMD		StateCmd,
	u8					EntryNum
	)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	RTPRINT(FIOCTL, IOCTL_STATE, ("[BT state], [Connecting], "));
	switch (StateCmd)
	{
		case STATE_CMD_CONNECT_ACCEPT_TIMEOUT:
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_CONNECT_ACCEPT_TIMEOUT\n"));
			pBTInfo->BtAsocEntry[EntryNum].PhysLinkCompleteStatus=HCI_STATUS_CONNECT_ACCEPT_TIMEOUT;
			pBtMgnt->bNeedNotifyAMPNoCap = _TRUE;
			BTHCI_DisconnectPeer(padapter, EntryNum);
			break;
		}

		case STATE_CMD_MAC_CONNECT_COMPLETE:
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_MAC_CONNECT_COMPLETE\n"));

			if (pBTInfo->BtAsocEntry[EntryNum].AMPRole == AMP_BTAP_JOINER)
			{
				RT_TRACE(COMP_TEST, DBG_LOUD , ("StateConnecting \n"));
//				BTPKT_WPAAuthINITIALIZE(padapter,EntryNum); // Not implement yet
			}
			break;
		}

		case STATE_CMD_DISCONNECT_PHY_LINK:
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_DISCONNECT_PHY_LINK\n"));

			bthci_EventDisconnectPhyLinkComplete(padapter,
			HCI_STATUS_SUCCESS,
			pBTInfo->BtAsocEntry[EntryNum].PhyLinkDisconnectReason,
			EntryNum);

			pBTInfo->BtAsocEntry[EntryNum].PhysLinkCompleteStatus=HCI_STATUS_UNKNOW_CONNECT_ID;

			PlatformCancelTimer(padapter,&pBTInfo->BTHCIJoinTimeoutTimer);

			BTHCI_DisconnectPeer(padapter, EntryNum);

			break;
		}

		case STATE_CMD_MAC_CONNECT_CANCEL_INDICATE:
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_MAC_CONNECT_CANCEL_INDICATE\n"));
			pBTInfo->BtAsocEntry[EntryNum].PhysLinkCompleteStatus=HCI_STATUS_CONTROLLER_BUSY;
			// Because this state cmd is caused by the BTHCI_EventAMPStatusChange(),
			// we don't need to send event in the following BTHCI_DisconnectPeer() again.
			pBtMgnt->bNeedNotifyAMPNoCap = _FALSE;
			BTHCI_DisconnectPeer(padapter, EntryNum);
			break;
		}

		default:
			RTPRINT(FIOCTL, IOCTL_STATE, ("State command(%d) is Wrong !!!\n", StateCmd));
			break;
	}
}

void
bthci_StateConnected(
	PADAPTER 					padapter,
	HCI_STATE_WITH_CMD		StateCmd,
	u8					EntryNum
	)
{
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	RTPRINT(FIOCTL, IOCTL_STATE, ("[BT state], [Connected], "));
	switch (StateCmd)
	{
		case STATE_CMD_DISCONNECT_PHY_LINK:
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_DISCONNECT_PHY_LINK\n"));

			//
			//When we are trying to disconnect the phy link, we should disconnect log link first,
			//
			{
				u8 i;
				u16 logicHandle = 0;
				for (i=0; i<MAX_LOGICAL_LINK_NUM; i++)
				{
					if (pBTInfo->BtAsocEntry[EntryNum].LogLinkCmdData->BtLogLinkhandle != 0)
					{
						logicHandle=pBTInfo->BtAsocEntry[EntryNum].LogLinkCmdData->BtLogLinkhandle;

						bthci_EventDisconnectLogicalLinkComplete(padapter, HCI_STATUS_SUCCESS,
							logicHandle, pBTInfo->BtAsocEntry[EntryNum].PhyLinkDisconnectReason);

						pBTInfo->BtAsocEntry[EntryNum].LogLinkCmdData->BtLogLinkhandle = 0;
					}
				}
			}

			bthci_EventDisconnectPhyLinkComplete(padapter,
			HCI_STATUS_SUCCESS,
			pBTInfo->BtAsocEntry[EntryNum].PhyLinkDisconnectReason,
			EntryNum);

			PlatformCancelTimer(padapter, &pBTInfo->BTHCIJoinTimeoutTimer);

			BTHCI_DisconnectPeer(padapter, EntryNum);
			break;
		}

		case STATE_CMD_MAC_DISCONNECT_INDICATE:
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_MAC_DISCONNECT_INDICATE\n"));

			bthci_EventDisconnectPhyLinkComplete(padapter,
			HCI_STATUS_SUCCESS,
			// TODO: Remote Host not local host
			HCI_STATUS_CONNECT_TERMINATE_LOCAL_HOST,
			EntryNum);
			BTHCI_DisconnectPeer(padapter, EntryNum);

			break;
		}

		case STATE_CMD_ENTER_STATE:
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_ENTER_STATE\n"));

			if (pBtMgnt->bBTConnectInProgress)
			{
				pBtMgnt->bBTConnectInProgress = _FALSE;
				RTPRINT(FIOCTL, IOCTL_STATE, ("[BT Flag], BT Connect in progress OFF!!\n"));
			}
			pBTInfo->BtAsocEntry[EntryNum].BtCurrentState = HCI_STATE_CONNECTED;
			pBTInfo->BtAsocEntry[EntryNum].b4waySuccess = _TRUE;
			pBtMgnt->bStartSendSupervisionPkt = _TRUE;

			PlatformSetTimer(padapter, &pBTInfo->BTSupervisionPktTimer, 10000);
			// for rate adaptive
#if 0
			padapter->HalFunc.UpdateHalRAMaskHandler(
									padapter,
									_FALSE,
									MAX_FW_SUPPORT_MACID_NUM-1-EntryNum,
									&pBTInfo->BtAsocEntry[EntryNum].BTRemoteMACAddr[0],
									NULL,
									0,
									RAMask_BT);
			padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_BASIC_RATE, (u8*)(&pMgntInfo->mBrates));
#else
			Update_RA_Entry(padapter, MAX_FW_SUPPORT_MACID_NUM-1-EntryNum);
			padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_BASIC_RATE, padapter->mlmepriv.cur_network.network.SupportedRates);
#endif
			BTDM_SetFwChnlInfo(padapter, RT_MEDIA_CONNECT);

			//padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_MEDIA_STATUS, (u8*)(&opMode));
			//padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_CHECK_BSSID, (u8*)(&bFilterOutNonAssociatedBSSID));
			break;
		}

		default:
			RTPRINT(FIOCTL, IOCTL_STATE, ("State command(%d) is Wrong !!!\n", StateCmd));
			break;
	}
}

void
bthci_StateAuth(
	PADAPTER 					padapter,
	HCI_STATE_WITH_CMD		StateCmd,
	u8					EntryNum
	)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	RTPRINT(FIOCTL, IOCTL_STATE, ("[BT state], [Authenticating], "));
	switch (StateCmd)
	{
		case STATE_CMD_CONNECT_ACCEPT_TIMEOUT:
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_CONNECT_ACCEPT_TIMEOUT\n"));
			pBTInfo->BtAsocEntry[EntryNum].PhysLinkCompleteStatus=HCI_STATUS_CONNECT_ACCEPT_TIMEOUT;
			pBtMgnt->bNeedNotifyAMPNoCap = _TRUE;
			BTHCI_DisconnectPeer(padapter, EntryNum);
			break;
		}

		case STATE_CMD_DISCONNECT_PHY_LINK:
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_DISCONNECT_PHY_LINK\n"));
			bthci_EventDisconnectPhyLinkComplete(padapter,
			HCI_STATUS_SUCCESS,
			pBTInfo->BtAsocEntry[EntryNum].PhyLinkDisconnectReason,
			EntryNum);

			pBTInfo->BtAsocEntry[EntryNum].PhysLinkCompleteStatus=HCI_STATUS_UNKNOW_CONNECT_ID;

			PlatformCancelTimer(padapter,&pBTInfo->BTHCIJoinTimeoutTimer);

			BTHCI_DisconnectPeer(padapter,EntryNum);
			break;
		}

		case STATE_CMD_4WAY_FAILED:
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_4WAY_FAILED\n"));

			pBTInfo->BtAsocEntry[EntryNum].PhysLinkCompleteStatus=HCI_STATUS_AUTH_FAIL;
			pBtMgnt->bNeedNotifyAMPNoCap = _TRUE;

			BTHCI_DisconnectPeer(padapter,EntryNum);

			PlatformCancelTimer(padapter,&pBTInfo->BTHCIJoinTimeoutTimer);
			break;
		}

		case STATE_CMD_4WAY_SUCCESSED:
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_4WAY_SUCCESSED\n"));

			bthci_EventPhysicalLinkComplete(padapter, HCI_STATUS_SUCCESS, EntryNum, INVALID_PL_HANDLE);

			PlatformCancelTimer(padapter, &pBTInfo->BTHCIJoinTimeoutTimer);

			BTHCI_SM_WITH_INFO(padapter, HCI_STATE_CONNECTED, STATE_CMD_ENTER_STATE, EntryNum);
			break;
		}

		default:
			RTPRINT(FIOCTL, IOCTL_STATE, ("State command(%d) is Wrong !!!\n", StateCmd));
			break;
	}
}

void
bthci_StateDisconnecting(
	PADAPTER 					padapter,
	HCI_STATE_WITH_CMD		StateCmd,
	u8					EntryNum
	)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	RTPRINT(FIOCTL, IOCTL_STATE, ("[BT state], [Disconnecting], "));
	switch (StateCmd)
	{
		case STATE_CMD_MAC_CONNECT_CANCEL_INDICATE:
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_MAC_CONNECT_CANCEL_INDICATE\n"));
			if (pBTInfo->BtAsocEntry[EntryNum].bNeedPhysLinkCompleteEvent)
			{
				bthci_EventPhysicalLinkComplete(padapter,
					pBTInfo->BtAsocEntry[EntryNum].PhysLinkCompleteStatus,
					EntryNum, INVALID_PL_HANDLE);
			}

			if (pBtMgnt->bBTConnectInProgress)
			{
				pBtMgnt->bBTConnectInProgress = _FALSE;
				RTPRINT(FIOCTL, IOCTL_STATE, ("[BT Flag], BT Connect in progress OFF!!\n"));
			}

			BTHCI_SM_WITH_INFO(padapter, HCI_STATE_DISCONNECTED, STATE_CMD_ENTER_STATE, EntryNum);
			break;
		}

		case STATE_CMD_DISCONNECT_PHY_LINK:
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_DISCONNECT_PHY_LINK\n"));

			bthci_EventDisconnectPhyLinkComplete(padapter,
			HCI_STATUS_SUCCESS,
			pBTInfo->BtAsocEntry[EntryNum].PhyLinkDisconnectReason,
			EntryNum);

			PlatformCancelTimer(padapter,&pBTInfo->BTHCIJoinTimeoutTimer);

			BTHCI_DisconnectPeer(padapter, EntryNum);
			break;
		}

		default:
			RTPRINT(FIOCTL, IOCTL_STATE, ("State command(%d) is Wrong !!!\n", StateCmd));
			break;
	}
}

void
bthci_StateDisconnected(
	PADAPTER 					padapter,
	HCI_STATE_WITH_CMD		StateCmd,
	u8					EntryNum
	)
{
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO		pBtHciInfo = &pBTInfo->BtHciInfo;
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	RTPRINT(FIOCTL, IOCTL_STATE, ("[BT state], [Disconnected], "));
	switch (StateCmd)
	{
		case STATE_CMD_CREATE_PHY_LINK:
		case STATE_CMD_ACCEPT_PHY_LINK:
		{
			if (StateCmd == STATE_CMD_CREATE_PHY_LINK)
			{
				RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_CREATE_PHY_LINK\n"));
			}
			else
			{
				RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_ACCEPT_PHY_LINK\n"));
			}

			RTPRINT(FIOCTL, IOCTL_STATE, ("[BT PS], Disable IPS and LPS\n"));
			IPSDisable(padapter, _FALSE, IPS_DISABLE_BT_ON);
			LeisurePSLeave(padapter, LPS_DISABLE_BT_HS_CONNECTION);

			pBtMgnt->bPhyLinkInProgress =_TRUE;
			pBtMgnt->BTCurrentConnectType=BT_DISCONNECT;
			if (!pBtMgnt->BtOperationOn)
			{
#if (SENDTXMEHTOD == 0)
				PlatformSetTimer(padapter, &pBTInfo->BTHCISendAclDataTimer, 1);
#endif
#if (RTS_CTS_NO_LEN_LIMIT == 1)
				rtw_write32(padapter, 0x4c8, 0xc140400);
#endif
			}
			pBtMgnt->CurrentBTConnectionCnt++;
			RTPRINT(FIOCTL, IOCTL_STATE, ("[BT Flag], CurrentBTConnectionCnt = %d\n",
				pBtMgnt->CurrentBTConnectionCnt));
			pBtMgnt->BtOperationOn = _TRUE;
			RTPRINT(FIOCTL, IOCTL_STATE, ("[BT Flag], Bt Operation ON!! CurrentConnectEntryNum = %d\n",
				pBtMgnt->CurrentConnectEntryNum));

			if (pBtMgnt->bBTConnectInProgress)
			{
				bthci_EventPhysicalLinkComplete(padapter, HCI_STATUS_CONTROLLER_BUSY, INVALID_ENTRY_NUM, pBtMgnt->BtCurrentPhyLinkhandle);
				bthci_RemoveEntryByEntryNum(padapter, EntryNum);
				return;
			}

			if (StateCmd == STATE_CMD_CREATE_PHY_LINK)
			{
				pBTInfo->BtAsocEntry[EntryNum].AMPRole = AMP_BTAP_CREATOR;
			}
			else
			{
				pBTInfo->BtAsocEntry[EntryNum].AMPRole = AMP_BTAP_JOINER;
			}

			// 1. MAC not yet in selected channel
#if 0
			while ((MgntRoamingInProgress(pMgntInfo)) ||
				(MgntIsLinkInProgress(pMgntInfo))||
				(MgntScanInProgress(pMgntInfo)))
#else
			while (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE|WIFI_SITE_MONITOR) == _TRUE)
#endif
			{
				RTPRINT(FIOCTL, IOCTL_STATE, ("Scan/Roaming/Wifi Link is in Progress, wait 200 ms\n"));
				rtw_mdelay_os(200);
			}
			// 2. MAC already in selected channel
			{
				RTPRINT(FIOCTL, IOCTL_STATE, ("Channel is Ready\n"));
				PlatformSetTimer(padapter, &pBTInfo->BTHCIJoinTimeoutTimer, pBtHciInfo->ConnAcceptTimeout);

				pBTInfo->BtAsocEntry[EntryNum].bNeedPhysLinkCompleteEvent = _TRUE;
			}
			break;
		}

		case STATE_CMD_DISCONNECT_PHY_LINK:
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_DISCONNECT_PHY_LINK\n"));

			PlatformCancelTimer(padapter,&pBTInfo->BTHCIJoinTimeoutTimer);

			bthci_EventDisconnectPhyLinkComplete(padapter,
			HCI_STATUS_SUCCESS,
			pBTInfo->BtAsocEntry[EntryNum].PhyLinkDisconnectReason,
			EntryNum);

			if (pBTInfo->BtAsocEntry[EntryNum].bNeedPhysLinkCompleteEvent)
			{
				bthci_EventPhysicalLinkComplete(padapter,
					HCI_STATUS_UNKNOW_CONNECT_ID,
					EntryNum, INVALID_PL_HANDLE);
			}

			if (pBtMgnt->bBTConnectInProgress)
			{
				pBtMgnt->bBTConnectInProgress = _FALSE;
				RTPRINT(FIOCTL, IOCTL_STATE, ("[BT Flag], BT Connect in progress OFF!!\n"));
			}
			BTHCI_SM_WITH_INFO(padapter, HCI_STATE_DISCONNECTED, STATE_CMD_ENTER_STATE, EntryNum);
			bthci_RemoveEntryByEntryNum(padapter,EntryNum);
			break;
		}

		case STATE_CMD_ENTER_STATE:
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_ENTER_STATE\n"));
			break;
		}

		default:
			RTPRINT(FIOCTL, IOCTL_STATE, ("State command(%d) is Wrong !!!\n", StateCmd));
			break;
	}
}

void
bthci_UseFakeData(
	PADAPTER 	padapter,
	PPACKET_IRP_HCICMD_DATA		pHciCmd
	)
{
	if (pHciCmd->OGF == OGF_LINK_CONTROL_COMMANDS && pHciCmd->OCF == HCI_CREATE_LOGICAL_LINK)
	{
		PHCI_FLOW_SPEC	pTxFlowSpec = (PHCI_FLOW_SPEC)&pHciCmd->Data[1];
		PHCI_FLOW_SPEC	pRxFlowSpec = (PHCI_FLOW_SPEC)&pHciCmd->Data[17];
		bthci_SelectFlowType(padapter, BT_TX_BE_FS, BT_RX_BE_FS, pTxFlowSpec, pRxFlowSpec);
		//bthci_SelectFlowType(padapter, BT_TX_be_FS, BT_RX_GU_FS, pTxFlowSpec, pRxFlowSpec);
	}
	else if (pHciCmd->OGF == OGF_LINK_CONTROL_COMMANDS && pHciCmd->OCF == HCI_FLOW_SPEC_MODIFY)
	{
		PHCI_FLOW_SPEC	pTxFlowSpec = (PHCI_FLOW_SPEC)&pHciCmd->Data[2];
		PHCI_FLOW_SPEC	pRxFlowSpec = (PHCI_FLOW_SPEC)&pHciCmd->Data[18];
		//bthci_SelectFlowType(padapter, BT_TX_BE_FS, BT_RX_BE_FS, pTxFlowSpec, pRxFlowSpec);
		bthci_SelectFlowType(padapter, BT_TX_BE_AGG_FS, BT_RX_BE_AGG_FS, pTxFlowSpec, pRxFlowSpec);
	}
}

void bthci_TimerCallbackHCICmd(PRT_TIMER pTimer)
{
#if (BT_THREAD == 0)
//	PADAPTER		padapter = (PADAPTER)pTimer->padapter;
	PADAPTER		padapter = (PADAPTER)pTimer;
	PBT30Info		pBTinfo = GET_BT_INFO(padapter);

	RTPRINT(FIOCTL, IOCTL_CALLBACK_FUN, ("bthci_TimerCallbackHCICmd() ==>\n"));

	PlatformScheduleWorkItem(&pBTinfo->HCICmdWorkItem);

	RTPRINT(FIOCTL, IOCTL_CALLBACK_FUN, ("bthci_TimerCallbackHCICmd() <==\n"));
#endif
}

void bthci_TimerCallbackSendAclData(PRT_TIMER pTimer)
{
#if (SENDTXMEHTOD == 0)
//	PADAPTER		padapter = (PADAPTER)pTimer->padapter;
	PADAPTER		padapter = (PADAPTER)pTimer;
	PBT30Info		pBTinfo = GET_BT_INFO(padapter);

	RTPRINT(FIOCTL, IOCTL_CALLBACK_FUN, ("HCIAclDataTimerCallback() ==>\n"));
	if (padapter->bDriverIsGoingToUnload)
	{
		return;
	}
	PlatformScheduleWorkItem(&pBTinfo->HCISendACLDataWorkItem);
	RTPRINT(FIOCTL, IOCTL_CALLBACK_FUN, ("HCIAclDataTimerCallback() <==\n"));
#endif
}

void bthci_TimerCallbackDiscardAclData(PRT_TIMER pTimer)
{
//	PADAPTER		padapter = (PADAPTER)pTimer->padapter;
	PADAPTER		padapter = (PADAPTER)pTimer;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO		pBtHciInfo = &pBTInfo->BtHciInfo;

	RTPRINT(FIOCTL, IOCTL_CALLBACK_FUN, ("bthci_TimerCallbackDiscardAclData() ==>\n"));

	RTPRINT(FIOCTL, (IOCTL_CALLBACK_FUN|IOCTL_BT_LOGO), ("Flush Timeout ==>\n"));
	if (bthci_DiscardTxPackets(padapter, pBtHciInfo->FLTO_LLH))
	{
		bthci_EventFlushOccurred(padapter, pBtHciInfo->FLTO_LLH);
	}
	RTPRINT(FIOCTL, IOCTL_CALLBACK_FUN, ("bthci_TimerCallbackDiscardAclData() <==\n"));
}

void bthci_TimerCallbackPsDisable(PRT_TIMER pTimer)
{
//	PADAPTER		padapter = (PADAPTER)pTimer->padapter;
	PADAPTER		padapter = (PADAPTER)pTimer;
	PBT30Info		pBTinfo = GET_BT_INFO(padapter);

	RTPRINT(FIOCTL, IOCTL_CALLBACK_FUN, ("bthci_TimerCallbackPsDisable() ==>\n"));

	PlatformScheduleWorkItem(&(pBTinfo->BTPsDisableWorkItem));

	RTPRINT(FIOCTL, IOCTL_CALLBACK_FUN, ("bthci_TimerCallbackPsDisable() <==\n"));
}

void bthci_TimerCallbackJoinTimeout(PRT_TIMER pTimer)
{
//	PADAPTER		padapter = (PADAPTER)pTimer->padapter;
	PADAPTER		padapter = (PADAPTER)pTimer;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	u8			CurrentEntry = pBtMgnt->CurrentConnectEntryNum;

	RTPRINT(FIOCTL, IOCTL_CALLBACK_FUN, ("bthci_TimerCallbackJoinTimeout() ==> Current State %x\n",pBTInfo->BtAsocEntry[CurrentEntry].BtCurrentState));

	if (pBTInfo->BtAsocEntry[CurrentEntry].BtCurrentState == HCI_STATE_STARTING)
	{
		bthci_StateStarting(padapter, STATE_CMD_CONNECT_ACCEPT_TIMEOUT,CurrentEntry);
	}
	else if (pBTInfo->BtAsocEntry[CurrentEntry].BtCurrentState == HCI_STATE_CONNECTING)
	{
		bthci_StateConnecting(padapter, STATE_CMD_CONNECT_ACCEPT_TIMEOUT,CurrentEntry);
	}
	else if (pBTInfo->BtAsocEntry[CurrentEntry].BtCurrentState == HCI_STATE_AUTHENTICATING)
	{
		bthci_StateAuth(padapter, STATE_CMD_CONNECT_ACCEPT_TIMEOUT,CurrentEntry);
	}
	else
	{
		RTPRINT(FIOCTL, IOCTL_CALLBACK_FUN, ("bthci_TimerCallbackJoinTimeout() <== No Such state!!!\n"));
	}

	RTPRINT(FIOCTL, IOCTL_CALLBACK_FUN, ("bthci_TimerCallbackJoinTimeout() <==\n"));
}

void bthci_TimerCallbackSendTestPacket(PRT_TIMER pTimer)
{
//	PADAPTER		padapter = (PADAPTER)pTimer->padapter;
	PADAPTER		padapter = (PADAPTER)pTimer;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO		pBtHciInfo = &pBTInfo->BtHciInfo;

	RTPRINT(FIOCTL, IOCTL_CALLBACK_FUN, ("bthci_TimerCallbackSendTestPacket() \n"));
	if (pBtHciInfo->bTestIsEnd || !pBtHciInfo->bInTestMode)
	{
		RTPRINT(FIOCTL, IOCTL_CALLBACK_FUN, ("bthci_TimerCallbackSendTestPacket() <==bTestIsEnd\n"));
		return;
	}

//	BTPKT_SendTestPacket(padapter); // not porting yet
	PlatformSetTimer(padapter, &pBTInfo->BTTestSendPacketTimer, 50);
	RTPRINT(FIOCTL, IOCTL_CALLBACK_FUN, ("bthci_TimerCallbackSendTestPacket() <==\n"));
}

void bthci_TimerCallbackBTSupervisionPacket(PRT_TIMER pTimer)
{
#if 0 // not porting yet
//	PADAPTER		padapter = (PADAPTER)pTimer->padapter;
	PADAPTER		padapter = (PADAPTER)pTimer;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	u8			i;
	u8			isQosdata = _TRUE;
	int				EntryTimer=5000;
	u32			EntryTOCnt = 0;
	int				callBackTimer=1000;


	if (pBTInfo->BTBeaconTmrOn)
	{
		RTPRINT(FIOCTL, IOCTL_STATE, ("[BT] stop beacon timer\n"));
		pBTInfo->BTBeaconTmrOn = _FALSE;
		PlatformCancelTimer(padapter, &(pBTInfo->BTBeaconTimer));
	}

	for (i=0; i<MAX_BT_ASOC_ENTRY_NUM; i++)
	{
		if (pBTInfo->BtAsocEntry[i].b4waySuccess)
		{
			if (pBTInfo->BtAsocEntry[i].NoRxPktCnt)
			{
				RTPRINT(FIOCTL, IOCTL_STATE, ("BtAsocEntry[%d].NoRxPktCnt = %ld\n", i, pBTInfo->BtAsocEntry[i].NoRxPktCnt));
			}
			EntryTimer = ((pBTInfo->BtAsocEntry[i].PhyLinkCmdData.LinkSuperversionTimeout*625)/1000);
			EntryTOCnt = (EntryTimer/callBackTimer);
			{
				if (pBtMgnt->bStartSendSupervisionPkt)
				{
					if (pBTInfo->BtAsocEntry[i].BtCurrentState == HCI_STATE_CONNECTED)
					{

						if (pBTInfo->BtAsocEntry[i].NoRxPktCnt >= 4)	// start to send supervision packet
						{
							isQosdata = _FALSE;
							if (pBTInfo->BtAsocEntry[i].AMPRole == AMP_BTAP_JOINER)
							{
								if (pBtMgnt->bssDesc.BssQos.bdQoSMode > QOS_DISABLE)
									isQosdata = _TRUE;
							}
							else if (pBTInfo->BtAsocEntry[i].AMPRole == AMP_BTAP_CREATOR)
							{
								if (pBTInfo->BtAsocEntry[i].bPeerQosSta)
									isQosdata = _TRUE;
							}
							BTPKT_SendLinkSupervisionPacket(padapter, _TRUE, i, isQosdata);
						}

						if (pBTInfo->BtAsocEntry[i].bSendSupervisionPacket)
						{
							if (pBTInfo->BtAsocEntry[i].NoRxPktCnt >= EntryTOCnt)
							{
								pBTInfo->BtAsocEntry[i].PhyLinkDisconnectReason=HCI_STATUS_CONNECT_TIMEOUT;
									RTPRINT(FIOCTL, (IOCTL_STATE|IOCTL_BT_LOGO), ("No Link supervision Packet received within %d Sec !!!!\n",(EntryTimer/1000)));
									RTPRINT(FIOCTL, IOCTL_STATE, ("[BT AMPStatus], set to invalid in bthci_TimerCallbackBTSupervisionPacket()\n"));
									BTHCI_EventAMPStatusChange(padapter, AMP_STATUS_NO_CAPACITY_FOR_BT);
							}
						}
						else
						{
							pBTInfo->BtAsocEntry[i].bSendSupervisionPacket = _TRUE;
						}
					}
				}
			}
			pBTInfo->BtAsocEntry[i].NoRxPktCnt++;
		}
	}

	PlatformSetTimer(padapter, &pBTInfo->BTSupervisionPktTimer, callBackTimer);
#endif
}

void bthci_TimerCallbackBTAuthTimeout(PRT_TIMER pTimer)
{
#if 0 // not porting yet
//	PADAPTER		padapter = (PADAPTER)pTimer->padapter;
	PADAPTER		padapter = (PADAPTER)pTimer;
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info	pBtInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBtInfo->BtMgnt;
	u8	arSeq, arAlg, AuthStatusCode;
	OCTET_STRING	OurCText;
	u8	arChalng[128];

	switch (pBtMgnt->BTCurrentConnectType)
	{
		case BT_CONNECT_AUTH_REQ:
		{
			RTPRINT(FIOCTL, (IOCTL_STATE|IOCTL_BT_LOGO), ("bthci_TimerCallbackBTAuthTimeout==> BT_CONNECT_AUTH_REQ\n"));
			if (pBtMgnt->BTReceiveConnectPkt! = BT_CONNECT_AUTH_RSP)
			{
				if (pBtMgnt->BTAuthCount < BTMaxAuthCount)
				{
					FillOctetString(OurCText, pMgntInfo->arChalng, 0);

					arAlg = OPEN_SYSTEM;
					AuthStatusCode = StatusCode_success;
					arSeq = 1;
					RTPRINT(FIOCTL, (IOCTL_STATE|IOCTL_BT_LOGO), ("bthci_TimerCallbackBTAuthTimeout==> Re Send Auth Req %d\n", pBtMgnt->BTAuthCount));
					BTPKT_SendAuthenticatePacket(
						padapter,
						pBtInfo->BtAsocEntry[pBtMgnt->CurrentConnectEntryNum].BTRemoteMACAddr, // auStaAddr,
						arAlg, // AuthAlg,
						arSeq, // AuthSeq,
						AuthStatusCode, // AuthStatusCode
						OurCText // AuthChallengetext
						);

					pBtMgnt->BTAuthCount++;

					PlatformSetTimer(padapter, &pBtInfo->BTAuthTimeoutTimer, 200);
				}
				else
				{
					RTPRINT(FIOCTL, (IOCTL_STATE|IOCTL_BT_LOGO), ("bthci_TimerCallbackBTAuthTimeout==> Reach  BTMaxAuthCount\n"));
				}
			}

			break;
		}
		case BT_CONNECT_AUTH_RSP:
		{
			RTPRINT(FIOCTL, (IOCTL_STATE|IOCTL_BT_LOGO), ("bthci_TimerCallbackBTAuthTimeout==> BT_CONNECT_AUTH_RSP\n"));
			if (pBtMgnt->BTReceiveConnectPkt != BT_CONNECT_ASOC_REQ)
			{
				if (pBtMgnt->BTAuthCount < BTMaxAuthCount)
				{
					OurCText.Length = 0;
					OurCText.Octet = arChalng;

					arAlg = OPEN_SYSTEM;

					AuthStatusCode = StatusCode_success;
					// Send auth frame.
					BTPKT_SendAuthenticatePacket(
						padapter,
						pBtInfo->BtAsocEntry[pBtMgnt->CurrentConnectEntryNum].BTRemoteMACAddr, // auStaAddr,
						arAlg, // AuthAlg,
						2, // AuthSeq,
						AuthStatusCode, // AuthStatusCode
						OurCText // AuthChallengetext
						);

					pBtMgnt->BTAuthCount++;
				}
				else
				{
					RTPRINT(FIOCTL, (IOCTL_STATE|IOCTL_BT_LOGO), ("bthci_TimerCallbackBTAuthTimeout==> Reach  BTMaxAuthCount\n"));
				}
			}

			break;
		}
		default:
		{
			RTPRINT(FIOCTL, (IOCTL_STATE|IOCTL_BT_LOGO), ("bthci_TimerCallbackBTAuthTimeout==> No Such Connect Type %d  !!!!!!\n", pBtMgnt->BTCurrentConnectType));
			break;
		}
	}
#endif
}

void bthci_TimerCallbackAsocTimeout(PRT_TIMER pTimer)
{
#if 0 // not porting yet
//	PADAPTER		padapter = (PADAPTER)pTimer->padapter;
	PADAPTER		padapter = (PADAPTER)pTimer;
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info	pBtInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBtInfo->BtMgnt;

	switch (pBtMgnt->BTCurrentConnectType)
	{
		case BT_CONNECT_ASOC_REQ:
		{
			RTPRINT(FIOCTL, (IOCTL_STATE|IOCTL_BT_LOGO), ("bthci_TimerCallbackAsocTimeout==> BT_CONNECT_ASOC_REQ\n"));
			if (pBtMgnt->BTReceiveConnectPkt! = BT_CONNECT_ASOC_RSP)
			{
				if (pBtMgnt->BTAsocCount < BTMaxAsocCount)
				{
					RTPRINT(FIOCTL, (IOCTL_STATE|IOCTL_BT_LOGO), ("bthci_TimerCallbackAsocTimeout==> Re Send Asoc Req %d\n", pBtMgnt->BTAsocCount));
					BTPKT_SendAssociateReq(
						padapter,
						pBtInfo->BtAsocEntry[pBtMgnt->CurrentConnectEntryNum].BTRemoteMACAddr,
						pMgntInfo->mCap,
						ASSOC_REQ_TIMEOUT,
						pBtMgnt->CurrentConnectEntryNum);

					pBtMgnt->BTAsocCount++;

					PlatformSetTimer(padapter, &pBtInfo->BTAsocTimeoutTimer, 200);
				}
				else
				{
					RTPRINT(FIOCTL, (IOCTL_STATE|IOCTL_BT_LOGO), ("bthci_TimerCallbackAsocTimeout==> Reach  BTMaxAuthCount\n"));
				}
			}
			break;
		}
		case BT_CONNECT_ASOC_RSP:
		{
			RTPRINT(FIOCTL, (IOCTL_STATE|IOCTL_BT_LOGO), ("bthci_TimerCallbackAsocTimeout==> BT_CONNECT_ASOC_RSP\n"));
			break;
		}
		default:
		{
			RTPRINT(FIOCTL, (IOCTL_STATE|IOCTL_BT_LOGO), ("bthci_TimerCallbackAsocTimeout==> No Such Connect Type %d  !!!!!!\n", pBtMgnt->BTCurrentConnectType));
			break;
		}
	}
#endif
}

void bthci_TimerCallbackDisconnectPhysicalLink(PRT_TIMER pTimer)
{
//	PADAPTER		padapter = (PADAPTER)pTimer->padapter;
	PADAPTER		padapter = (PADAPTER)pTimer;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	RT_TRACE(COMP_MLME, DBG_WARNING, ("===>bthci_TimerCallbackDisconnectPhysicalLink\n"));

	if (pBTInfo->BtAsocEntry[pBtMgnt->DisconnectEntryNum].BtCurrentState == HCI_STATE_CONNECTED)
	{
		BTHCI_SM_WITH_INFO(padapter,HCI_STATE_CONNECTED,STATE_CMD_DISCONNECT_PHY_LINK, pBtMgnt->DisconnectEntryNum);
	}

	BTHCI_EventNumOfCompletedDataBlocks(padapter);
	pBtMgnt->DisconnectEntryNum = 0xff;
	RT_TRACE(COMP_MLME, DBG_WARNING, ("<===bthci_TimerCallbackDisconnectPhysicalLink\n"));

}

u8 bthci_WaitForRfReady(PADAPTER padapter)
{
	u8 bRet = _FALSE;

//	PRT_POWER_SAVE_CONTROL	pPSC = GET_POWER_SAVE_CONTROL(&(padapter->MgntInfo));
	struct pwrctrl_priv *ppwrctrl = &padapter->pwrctrlpriv;
	rt_rf_power_state 			RfState;
	u32						waitcnt = 0;

	while(1)
	{
//		padapter->HalFunc.GetHwRegHandler(padapter, HW_VAR_RF_STATE, (u8*)(&RfState));
		RfState = ppwrctrl->rf_pwrstate;

//		if ((RfState != eRfOn) || (pPSC->bSwRfProcessing))
		if ((RfState != rf_on) || (ppwrctrl->bips_processing))
		{
			rtw_mdelay_os(10);
			if (waitcnt++ >= 200)
			{
//				RT_ASSERT(_FALSE, ("bthci_WaitForRfReady(), wait for RF ON timeout\n"));
				bRet = _FALSE;
				break;
			}
		}
		else
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("bthci_WaitForRfReady(), Rf is on, wait %d times\n", waitcnt));
			bRet = _TRUE;
			break;
		}
	}

	return bRet;
}

void bthci_WorkItemCallbackPsDisable(void *pContext)
{
	PADAPTER		padapter = (PADAPTER)pContext;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	u8			CurrentAssocNum;

	for (CurrentAssocNum=0; CurrentAssocNum<MAX_BT_ASOC_ENTRY_NUM; CurrentAssocNum++)
	{
		if (pBTInfo->BtAsocEntry[CurrentAssocNum].bUsed == _TRUE)
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("WorkItemCallbackPsDisable(): Handle Associate Entry %d\n", CurrentAssocNum));

			if (pBTInfo->BtAsocEntry[CurrentAssocNum].AMPRole == AMP_BTAP_CREATOR)
			{
//				BTPKT_StartBeacon(padapter, CurrentAssocNum); // not porting yet
				BTHCI_SM_WITH_INFO(padapter, HCI_STATE_CONNECTING, STATE_CMD_MAC_CONNECT_COMPLETE, CurrentAssocNum);
			}
			else if (pBTInfo->BtAsocEntry[CurrentAssocNum].AMPRole == AMP_BTAP_JOINER)
			{
				bthci_WaitForRfReady(padapter);
				bthci_ResponderStartToScan(padapter);
			}
		}
	}
}

void bthci_WorkItemCallbackHCICmd(void *pContext)
{
	PlatformProcessHCICommands(pContext);
}

void bthci_WorkItemCallbackSendACLData(void *pContext)
{
#if (SENDTXMEHTOD == 0)
	PADAPTER		padapter = (PADAPTER)pContext;
#if 0 //cosa for special logo test case
	if (acldata_cnt >= 2)
#endif
	PlatformTxBTQueuedPackets(padapter);
#endif
}

void bthci_WorkItemCallbackConnect(void *pContext)
{
	PADAPTER		padapter = (PADAPTER)pContext;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

//	BTPKT_JoinerConnectProcess(padapter, pBtMgnt->CurrentConnectEntryNum); // not porting yet
}

u8 BTHCI_GetConnectEntryNum(PADAPTER padapter)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	return bthci_GetCurrentEntryNum(padapter, pBtMgnt->BtCurrentPhyLinkhandle);
}

u8 BTHCI_GetCurrentEntryNumByMAC(PADAPTER padapter, u8 *SA)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	u8	i;

	for (i=0; i<MAX_BT_ASOC_ENTRY_NUM; i++)
	{
		if (pBTInfo->BtAsocEntry[i].bUsed == _TRUE)
		{
			if (_rtw_memcmp(pBTInfo->BtAsocEntry[i].BTRemoteMACAddr, SA, 6) == _TRUE)
			{
				return i;
			}
		}
	}
	return 0xFF;
}

void BTHCI_StatusWatchdog(PADAPTER padapter)
{
//	PMGNT_INFO			pMgntInfo = &padapter->MgntInfo;
	struct pwrctrl_priv *ppwrctrl = &padapter->pwrctrlpriv;
	PBT30Info			pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT			pBtMgnt = &pBTInfo->BtMgnt;
	PBT_TRAFFIC			pBtTraffic = &pBTInfo->BtTraffic;
	u8				bRfOff=_FALSE, bTxBusy = _FALSE, bRxBusy = _FALSE;

#if 0	// test only
	u8						testbuf[100]={0};
	PRTK_DBG_CTRL_OIDS			pDbgCtrl;
	PPACKET_IRP_HCICMD_DATA	pHCICMD;
	u32						a,b;
	u16	*					pu2Tmp;

	pDbgCtrl = (PRTK_DBG_CTRL_OIDS)testbuf;
	pDbgCtrl->ctrlType = 0;
	pHCICMD = (PPACKET_IRP_HCICMD_DATA)&pDbgCtrl->CtrlData;
	pHCICMD->OCF = OGF_EXTENSION;
	pHCICMD->OGF = HCI_LINK_STATUS_NOTIFY;
	pHCICMD->Length = 5;
	pHCICMD->Data[0] = 0x1;
	//pu2Tmp =
	*((u16*)&pHCICMD->Data[1]) = 0x0205;
	//*pu2Tmp = 0x0205;
	pHCICMD->Data[3] = 0x3;
	pHCICMD->Data[4] = 0x2;
	pDbgCtrl->ctrlDataLen = pHCICMD->Length+3;
	OIDS_RTKDbgControl(padapter, testbuf, pDbgCtrl->ctrlDataLen+4, &a, &b);
#endif

#if 0
	if ((pMgntInfo->RfOffReason & RF_CHANGE_BY_HW) ||
		(pMgntInfo->RfOffReason & RF_CHANGE_BY_SW))
#else
	if ((ppwrctrl->rfoff_reason & RF_CHANGE_BY_HW) ||
		(ppwrctrl->rfoff_reason & RF_CHANGE_BY_SW))
#endif
		bRfOff = _TRUE;

#if 0
	if (!MgntRoamingInProgress(pMgntInfo) &&
		!MgntIsLinkInProgress(pMgntInfo) &&
		!MgntScanInProgress(pMgntInfo) &&
#else
	if ((check_fwstate(&padapter->mlmepriv, WIFI_REASOC_STATE|WIFI_UNDER_LINKING|WIFI_SITE_MONITOR) == _FALSE) &&
#endif
		!bRfOff)
	{
		static u8 BTwaitcnt=0;
		if (pBtMgnt->BTNeedAMPStatusChg)
		{
			BTwaitcnt++;
			if (BTwaitcnt >= 2)
			{
				BTHCI_EventAMPStatusChange(padapter, AMP_STATUS_FULL_CAPACITY_FOR_BT);
				BTwaitcnt = 0;
			}
		}
	}

	RTPRINT(FIOCTL, IOCTL_BT_TP, ("[BT traffic], TxPktCntInPeriod=%d, TxPktLenInPeriod=%"i64fmt"d\n",
		pBtTraffic->Bt30TrafficStatistics.TxPktCntInPeriod,
		pBtTraffic->Bt30TrafficStatistics.TxPktLenInPeriod));
	RTPRINT(FIOCTL, IOCTL_BT_TP, ("[BT traffic], RxPktCntInPeriod=%d, RxPktLenInPeriod=%"i64fmt"d\n",
		pBtTraffic->Bt30TrafficStatistics.RxPktCntInPeriod,
		pBtTraffic->Bt30TrafficStatistics.RxPktLenInPeriod));
	if (pBtTraffic->Bt30TrafficStatistics.TxPktCntInPeriod > 100 ||
		pBtTraffic->Bt30TrafficStatistics.RxPktCntInPeriod > 100 )
	{
		if (pBtTraffic->Bt30TrafficStatistics.RxPktLenInPeriod > pBtTraffic->Bt30TrafficStatistics.TxPktLenInPeriod)
			bRxBusy = _TRUE;
		else if (pBtTraffic->Bt30TrafficStatistics.TxPktLenInPeriod > pBtTraffic->Bt30TrafficStatistics.RxPktLenInPeriod)
			bTxBusy = _TRUE;
	}

	pBtTraffic->Bt30TrafficStatistics.TxPktCntInPeriod = 0;
	pBtTraffic->Bt30TrafficStatistics.RxPktCntInPeriod = 0;
	pBtTraffic->Bt30TrafficStatistics.TxPktLenInPeriod = 0;
	pBtTraffic->Bt30TrafficStatistics.RxPktLenInPeriod = 0;
	pBtTraffic->Bt30TrafficStatistics.bTxBusyTraffic = bTxBusy;
	pBtTraffic->Bt30TrafficStatistics.bRxBusyTraffic = bRxBusy;
	RTPRINT(FIOCTL, IOCTL_BT_TP, ("[BT traffic], bTxBusyTraffic=%d, bRxBusyTraffic=%d\n",
		pBtTraffic->Bt30TrafficStatistics.bTxBusyTraffic,
		pBtTraffic->Bt30TrafficStatistics.bRxBusyTraffic));
}

void
BTHCI_NotifyRFState(
	PADAPTER				padapter,
	rt_rf_power_state		StateToSet,
	RT_RF_CHANGE_SOURCE	ChangeSource
	)
{
#if 0
	PMGNT_INFO				pMgntInfo = &padapter->MgntInfo;
	RT_RF_CHANGE_SOURCE	RfOffReason = pMgntInfo->RfOffReason;
#else
	struct pwrctrl_priv *ppwrctrl = &padapter->pwrctrlpriv;
	RT_RF_CHANGE_SOURCE	RfOffReason = ppwrctrl->rfoff_reason;
#endif

	RTPRINT(FIOCTL, IOCTL_STATE, ("BTHCI_NotifyRFState(), Old RfOffReason = 0x%x, ChangeSource = 0x%x\n", RfOffReason, ChangeSource));
	if (ChangeSource < RF_CHANGE_BY_HW)
	{
		RTPRINT(FIOCTL, IOCTL_STATE, ("BTHCI_NotifyRFState(), ChangeSource < RF_CHANGE_BY_HW\n"));
		return;
	}

	//
	// When RF is on/off by HW/SW(IPS/LPS not included), we have to notify
	// core stack the AMP_Status
	//

	// We only have to check RF On/Off by HW/SW
	RfOffReason &= (RF_CHANGE_BY_HW|RF_CHANGE_BY_SW);

	switch (StateToSet)
	{
		case rf_on:
			if (RfOffReason)
			{
				//
				// Previously, HW or SW Rf state is OFF, check if it is turned on by HW/SW
				//
				RfOffReason &= ~ChangeSource;
				if (!RfOffReason)
				{
					// Both HW/SW Rf is turned on
					RTPRINT(FIOCTL, IOCTL_STATE, ("BTHCI_NotifyRFState(), Rf is turned On!\n"));
					BTHCI_EventAMPStatusChange(padapter, AMP_STATUS_FULL_CAPACITY_FOR_BT);
				}
			}
			break;

		case rf_off:
			if (!RfOffReason)
			{
				//
				// Previously, both HW/SW Rf state is ON, check if it is turned off by HW/SW
				//
				RTPRINT(FIOCTL, IOCTL_STATE, ("BTHCI_NotifyRFState(), Rf is turned Off!\n"));
				RTPRINT(FIOCTL, IOCTL_STATE, ("[BT AMPStatus], set to invalid in BTHCI_NotifyRFState()\n"));
				BTHCI_EventAMPStatusChange(padapter, AMP_STATUS_NO_CAPACITY_FOR_BT);
			}
			break;

		default:
			RTPRINT(FIOCTL, IOCTL_STATE, ("Unknown case!! \n"));
			break;
	}
}

void
BTHCI_IndicateAMPStatus(
	PADAPTER			padapter,
	u8				JoinAction,
	u8				channel
	)
{
//	PMGNT_INFO	pMgntInfo = &(padapter->MgntInfo);
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTInfo->BtMgnt;
	u8		bNeedIndicate = _FALSE;

	RTPRINT(FIOCTL, IOCTL_STATE, ("JoinAction=%d, bssDesc->bdDsParms.ChannelNumber=%d\n",
		JoinAction, channel));

	switch (JoinAction)
	{
		case RT_JOIN_INFRA:
		case RT_JOIN_IBSS:
			//
			// When join infra or ibss, check if bt channel is the current channel,
			// if not, we need to indicate AMPStatus=2
			//
			if (channel != pBtMgnt->BTChannel)
				bNeedIndicate = _TRUE;
			break;
		case RT_START_IBSS:
			//
			// when start IBSS, we need to indicate AMPStatus=2 to
			// reset be hw security
			//
			bNeedIndicate = _TRUE;
			break;
		case RT_NO_ACTION:
			break;
		default:
			break;
	}

	if (bNeedIndicate)
	{
		RTPRINT(FIOCTL, IOCTL_STATE, ("BTHCI_IndicateAMPStatus(), BT channel=%d, bssDesc->bdDsParms.ChannelNumber=%d\n",
			pBtMgnt->BTChannel, channel));

		if (pBtMgnt->BtOperationOn)
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("[BT AMPStatus], set to invalid in JoinRequest()\n"));
			BTHCI_EventAMPStatusChange(padapter, AMP_STATUS_NO_CAPACITY_FOR_BT);
		}
	}
}

void
BTHCI_EventParse(
	PADAPTER 					padapter,
	void						*pEvntData,
	u32						dataLen
	)
{
	PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)pEvntData;
	return;

	RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("BT Event Code = 0x%x\n", PPacketIrpEvent->EventCode));
	RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("BT Event Length = 0x%x\n", PPacketIrpEvent->Length));

	switch (PPacketIrpEvent->EventCode)
	{
		case HCI_EVENT_COMMAND_COMPLETE:
			RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("HCI_EVENT_COMMAND_COMPLETE\n"));
			break;
		case HCI_EVENT_COMMAND_STATUS:
			RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("HCI_EVENT_COMMAND_STATUS\n"));
			break;
		default:
			break;
	}
}

u16
BTHCI_GetPhysicalLinkHandle(
	PADAPTER	padapter,
	u8		EntryNum
	)
{
	PBT30Info		pBTinfo = GET_BT_INFO(padapter);
	u16 handle;

	handle = pBTinfo->BtAsocEntry[EntryNum].AmpAsocCmdData.BtPhyLinkhandle;

	return handle;
}

RT_STATUS
BTHCI_IndicateRxData(
	PADAPTER 					padapter,
	void						*pData,
	u32						dataLen,
	u8						EntryNum
	)
{
	RT_STATUS	rt_status;

	rt_status = PlatformIndicateBTACLData(padapter, pData, dataLen, EntryNum);

	return rt_status;
}

void BTHCI_InitializeAllTimer(PADAPTER padapter)
{
	PBT30Info		pBTinfo = GET_BT_INFO(padapter);
	PBT_SECURITY		pBtSec = &pBTinfo->BtSec;

#if (BT_THREAD == 0)
	PlatformInitializeTimer(padapter, &pBTinfo->BTHCICmdTimer, (RT_TIMER_CALL_BACK)bthci_TimerCallbackHCICmd, NULL, "BTHCICmdTimer");
#endif
#if (SENDTXMEHTOD == 0)
	PlatformInitializeTimer(padapter, &pBTinfo->BTHCISendAclDataTimer, (RT_TIMER_CALL_BACK)bthci_TimerCallbackSendAclData, NULL, "BTHCISendAclDataTimer");
#endif
	PlatformInitializeTimer(padapter, &pBTinfo->BTHCIDiscardAclDataTimer, (RT_TIMER_CALL_BACK)bthci_TimerCallbackDiscardAclData, NULL, "BTHCIDiscardAclDataTimer");
	PlatformInitializeTimer(padapter, &pBTinfo->BTHCIJoinTimeoutTimer, (RT_TIMER_CALL_BACK)bthci_TimerCallbackJoinTimeout, NULL, "BTHCIJoinTimeoutTimer");
	PlatformInitializeTimer(padapter, &pBTinfo->BTTestSendPacketTimer, (RT_TIMER_CALL_BACK)bthci_TimerCallbackSendTestPacket, NULL, "BTTestSendPacketTimer");

	PlatformInitializeTimer(padapter, &pBTinfo->BTBeaconTimer, (RT_TIMER_CALL_BACK)BTPKT_TimerCallbackBeacon, NULL, "BTBeaconTimer");
	PlatformInitializeTimer(padapter, &pBtSec->BTWPAAuthTimer, (RT_TIMER_CALL_BACK)BTPKT_TimerCallbackWPAAuth, NULL, "BTWPAAuthTimer");
	PlatformInitializeTimer(padapter, &pBTinfo->BTSupervisionPktTimer, (RT_TIMER_CALL_BACK)bthci_TimerCallbackBTSupervisionPacket, NULL, "BTGeneralPurposeTimer");
	PlatformInitializeTimer(padapter, &pBTinfo->BTDisconnectPhyLinkTimer, (RT_TIMER_CALL_BACK)bthci_TimerCallbackDisconnectPhysicalLink, NULL, "BTDisconnectPhyLinkTimer");
	PlatformInitializeTimer(padapter, &pBTinfo->BTPsDisableTimer, (RT_TIMER_CALL_BACK)bthci_TimerCallbackPsDisable, NULL, "BTPsDisableTimer");
	PlatformInitializeTimer(padapter, &pBTinfo->BTAuthTimeoutTimer, (RT_TIMER_CALL_BACK)bthci_TimerCallbackBTAuthTimeout, NULL, "BTAuthTimeoutTimer");
	PlatformInitializeTimer(padapter, &pBTinfo->BTAsocTimeoutTimer, (RT_TIMER_CALL_BACK)bthci_TimerCallbackAsocTimeout, NULL, "BTAsocTimeoutTimer");
}

void BTHCI_CancelAllTimer(PADAPTER padapter)
{
	PBT30Info		pBTinfo = GET_BT_INFO(padapter);
	PBT_SECURITY		pBtSec = &pBTinfo->BtSec;

	// Note: don't cancel BTHCICmdTimer, if you cancel this timer, there will
	// have posibility to cause irp not completed.
#if (SENDTXMEHTOD == 0)
	PlatformCancelTimer(padapter, &pBTinfo->BTHCISendAclDataTimer);
#endif
	PlatformCancelTimer(padapter, &pBTinfo->BTHCIDiscardAclDataTimer);
	PlatformCancelTimer(padapter, &pBTinfo->BTHCIJoinTimeoutTimer);
	PlatformCancelTimer(padapter, &pBTinfo->BTTestSendPacketTimer);

	PlatformCancelTimer(padapter, &pBTinfo->BTBeaconTimer);
	PlatformCancelTimer(padapter, &pBtSec->BTWPAAuthTimer);
	PlatformCancelTimer(padapter, &pBTinfo->BTSupervisionPktTimer);
	PlatformCancelTimer(padapter, &pBTinfo->BTDisconnectPhyLinkTimer);
	PlatformCancelTimer(padapter, &pBTinfo->BTPsDisableTimer);
	PlatformCancelTimer(padapter, &pBTinfo->BTAuthTimeoutTimer);
	PlatformCancelTimer(padapter, &pBTinfo->BTAsocTimeoutTimer);
}

void BTHCI_ReleaseAllTimer(PADAPTER padapter)
{
	PBT30Info		pBTinfo = GET_BT_INFO(padapter);
	PBT_SECURITY		pBtSec = &pBTinfo->BtSec;

#if (BT_THREAD == 0)
	PlatformReleaseTimer(padapter, &pBTinfo->BTHCICmdTimer);
#endif
#if (SENDTXMEHTOD == 0)
	PlatformReleaseTimer(padapter, &pBTinfo->BTHCISendAclDataTimer);
#endif
	PlatformReleaseTimer(padapter, &pBTinfo->BTHCIDiscardAclDataTimer);
	PlatformReleaseTimer(padapter, &pBTinfo->BTHCIJoinTimeoutTimer);
	PlatformReleaseTimer(padapter, &pBTinfo->BTTestSendPacketTimer);

	PlatformReleaseTimer(padapter, &pBTinfo->BTBeaconTimer);
	PlatformReleaseTimer(padapter, &pBtSec->BTWPAAuthTimer);
	PlatformReleaseTimer(padapter, &pBTinfo->BTSupervisionPktTimer);
	PlatformReleaseTimer(padapter, &pBTinfo->BTDisconnectPhyLinkTimer);
	PlatformReleaseTimer(padapter, &pBTinfo->BTAuthTimeoutTimer);
	PlatformReleaseTimer(padapter, &pBTinfo->BTAsocTimeoutTimer);
	PlatformReleaseTimer(padapter, &pBTinfo->BTPsDisableTimer);
}

void BTHCI_InitializeAllWorkItem(PADAPTER padapter)
{
	PBT30Info		pBTinfo = GET_BT_INFO(padapter);
#if (BT_THREAD == 0)
	PlatformInitializeWorkItem(
		padapter,
		&(pBTinfo->HCICmdWorkItem),
		(RT_WORKITEM_CALL_BACK)bthci_WorkItemCallbackHCICmd,
		(PVOID)padapter,
		"HCICmdWorkItem");
#endif
#if (SENDTXMEHTOD == 0)
	PlatformInitializeWorkItem(
		padapter,
		&(pBTinfo->HCISendACLDataWorkItem),
		(RT_WORKITEM_CALL_BACK)bthci_WorkItemCallbackSendACLData,
		(PVOID)padapter,
		"HCISendACLDataWorkItem");
#endif
	PlatformInitializeWorkItem(
		padapter,
		&(pBTinfo->BTPsDisableWorkItem),
		(RT_WORKITEM_CALL_BACK)bthci_WorkItemCallbackPsDisable,
		(PVOID)padapter,
		"BTPsDisableWorkItem");

	PlatformInitializeWorkItem(
		padapter,
		&(pBTinfo->BTConnectWorkItem),
		(RT_WORKITEM_CALL_BACK)bthci_WorkItemCallbackConnect,
		(PVOID)padapter,
		"BTConnectWorkItem");
}

void BTHCI_FreeAllWorkItem(PADAPTER padapter)
{
	PBT30Info		pBTinfo = GET_BT_INFO(padapter);
#if (BT_THREAD == 0)
	PlatformFreeWorkItem(&(pBTinfo->HCICmdWorkItem));
#endif
#if (SENDTXMEHTOD == 0)
	PlatformFreeWorkItem(&(pBTinfo->HCISendACLDataWorkItem));
#endif
	PlatformFreeWorkItem(&(pBTinfo->BTPsDisableWorkItem));
	PlatformFreeWorkItem(&(pBTinfo->BTConnectWorkItem));
}

void BTHCI_Reset(PADAPTER padapter)
{
	bthci_CmdReset(padapter, _FALSE);
}

u8 BTHCI_HsConnectionEstablished(PADAPTER padapter)
{
	u8			bBtConnectionExist = _FALSE;
	PBT30Info	pBtinfo = GET_BT_INFO(padapter);
	u8 i;

	for (i=0; i<MAX_BT_ASOC_ENTRY_NUM; i++)
	{
		if (pBtinfo->BtAsocEntry[i].b4waySuccess == _TRUE)
		{
			bBtConnectionExist = _TRUE;
			break;
		}
	}

	RTPRINT(FIOCTL, IOCTL_STATE, (" BTHCI_HsConnectionEstablished(), connection exist = %d\n", bBtConnectionExist));

	return bBtConnectionExist;
}

u8
BTHCI_CheckProfileExist(
	PADAPTER 	padapter,
	BT_TRAFFIC_MODE_PROFILE	Profile
	)
{
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTInfo->BtMgnt;
	u8		IsPRofile = _FALSE;
	u8		i=0;

	for (i=0; i<pBtMgnt->ExtConfig.NumberOfHandle; i++)
	{
		if (pBtMgnt->ExtConfig.linkInfo[i].TrafficProfile == Profile)
		{
			IsPRofile=_TRUE;
			break;
		}
	}

	return IsPRofile;
}

u8 BTHCI_GetBTCoreSpecByProf(PADAPTER padapter, u8 profile)
{
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTInfo->BtMgnt;
	u8		btSpec = BT_SPEC_1_2;
	u8		i = 0;

	for (i=0; i<pBtMgnt->ExtConfig.NumberOfHandle; i++)
	{
		if (pBtMgnt->ExtConfig.linkInfo[i].TrafficProfile == profile)
		{
			btSpec = pBtMgnt->ExtConfig.linkInfo[i].BTCoreSpec;
			break;
		}
	}

	return btSpec;
}


void BTHCI_GetProfileNameMoto(PADAPTER padapter)
{
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTInfo->BtMgnt;
	u8		i = 0;
	u8		InCommingMode = 0,OutGoingMode = 0,ScoMode = 0;


	ScoMode = pBtMgnt->ExtConfig.NumberOfSCO;

	RTPRINT(FBT, BT_TRACE, ("[DM][BT], NumberOfHandle = %d, NumberOfSCO = %d\n",
		pBtMgnt->ExtConfig.NumberOfHandle, pBtMgnt->ExtConfig.NumberOfSCO));

	for (i=0; i<pBtMgnt->ExtConfig.NumberOfHandle; i++)
	{
		InCommingMode=pBtMgnt->ExtConfig.linkInfo[i].IncomingTrafficMode;
		OutGoingMode=pBtMgnt->ExtConfig.linkInfo[i].OutgoingTrafficMode;

		if (ScoMode)
		{
			pBtMgnt->ExtConfig.linkInfo[i].TrafficProfile = BT_PROFILE_SCO;
		}
		else if ((InCommingMode == BT_MOTOR_EXT_BE) && (OutGoingMode == BT_MOTOR_EXT_BE))
		{
			pBtMgnt->ExtConfig.linkInfo[i].TrafficProfile = BT_PROFILE_PAN;
		}
		else if ((InCommingMode == BT_MOTOR_EXT_GULB) && (OutGoingMode == BT_MOTOR_EXT_GULB))
		{
			pBtMgnt->ExtConfig.linkInfo[i].TrafficProfile = BT_PROFILE_A2DP;
		}
		else if ((InCommingMode == BT_MOTOR_EXT_GUL) && (OutGoingMode == BT_MOTOR_EXT_BE))
		{
			pBtMgnt->ExtConfig.linkInfo[i].TrafficProfile = BT_PROFILE_HID;
		}
		else
		{
			pBtMgnt->ExtConfig.linkInfo[i].TrafficProfile = BT_PROFILE_NONE;
		}
	}
}

void BTHCI_UpdateBTProfileRTKToMoto(PADAPTER padapter)
{
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTInfo->BtMgnt;
	u8		i = 0;

	pBtMgnt->ExtConfig.NumberOfSCO = 0;

	for (i=0; i<pBtMgnt->ExtConfig.NumberOfHandle; i++)
	{
		pBtMgnt->ExtConfig.linkInfo[i].TrafficProfile = BT_PROFILE_NONE;

		if (pBtMgnt->ExtConfig.linkInfo[i].BTProfile == BT_PROFILE_SCO)
		{
			pBtMgnt->ExtConfig.NumberOfSCO++;
		}

		pBtMgnt->ExtConfig.linkInfo[i].TrafficProfile = pBtMgnt->ExtConfig.linkInfo[i].BTProfile;
		switch (pBtMgnt->ExtConfig.linkInfo[i].TrafficProfile)
		{
			case BT_PROFILE_SCO:
				break;
			case BT_PROFILE_PAN:
				pBtMgnt->ExtConfig.linkInfo[i].IncomingTrafficMode = BT_MOTOR_EXT_BE;
				pBtMgnt->ExtConfig.linkInfo[i].OutgoingTrafficMode = BT_MOTOR_EXT_BE;
				break;
			case BT_PROFILE_A2DP:
				pBtMgnt->ExtConfig.linkInfo[i].IncomingTrafficMode = BT_MOTOR_EXT_GULB;
				pBtMgnt->ExtConfig.linkInfo[i].OutgoingTrafficMode = BT_MOTOR_EXT_GULB;
				break;
			case BT_PROFILE_HID:
				pBtMgnt->ExtConfig.linkInfo[i].IncomingTrafficMode = BT_MOTOR_EXT_GUL;
				pBtMgnt->ExtConfig.linkInfo[i].OutgoingTrafficMode = BT_MOTOR_EXT_BE;
				break;
			default:
				break;
		}
	}

	RTPRINT(FBT, BT_TRACE, ("[DM][BT], RTK, NumberOfHandle = %d, NumberOfSCO = %d\n",
		pBtMgnt->ExtConfig.NumberOfHandle, pBtMgnt->ExtConfig.NumberOfSCO));
}

void BTHCI_GetBTRSSI(PADAPTER padapter)
{
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTInfo->BtMgnt;
	u8		i = 0;

	//return;

	for (i=0; i<pBtMgnt->ExtConfig.NumberOfHandle; i++)
	{
		bthci_EventExtGetBTRSSI(padapter, pBtMgnt->ExtConfig.linkInfo[i].ConnectHandle);
	}
}

void BTHCI_WifiScanNotify(PADAPTER padapter, u8 scanType)
{
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bEnableWifiScanNotify)
		bthci_EventExtWifiScanNotify(padapter, scanType);
}

void
BTHCI_StateMachine(
	PADAPTER 					padapter,
	u8					StateToEnter,
	HCI_STATE_WITH_CMD		StateCmd,
	u8					EntryNum
	)
{
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTInfo->BtMgnt;

	if (EntryNum == 0xff)
	{
		RTPRINT(FIOCTL, IOCTL_STATE, (" StateMachine, error EntryNum=0x%x \n",EntryNum));
		return;
	}
	RTPRINT(FIOCTL, IOCTL_STATE, (" StateMachine, EntryNum = 0x%x, CurrentState = 0x%x, BtNextState = 0x%x,  StateCmd = 0x%x ,StateToEnter = 0x%x\n",
		EntryNum,pBTInfo->BtAsocEntry[EntryNum].BtCurrentState,pBTInfo->BtAsocEntry[EntryNum].BtNextState,StateCmd,StateToEnter));

	if (pBTInfo->BtAsocEntry[EntryNum].BtNextState & StateToEnter)
	{
		pBTInfo->BtAsocEntry[EntryNum].BtCurrentState = StateToEnter;

		switch (StateToEnter)
		{
			case HCI_STATE_STARTING:
				{
					pBTInfo->BtAsocEntry[EntryNum].BtNextState = HCI_STATE_DISCONNECTING | HCI_STATE_CONNECTING;
					bthci_StateStarting(padapter,StateCmd,EntryNum);
					break;
				}
			case HCI_STATE_CONNECTING:
				{
					pBTInfo->BtAsocEntry[EntryNum].BtNextState = HCI_STATE_CONNECTING | HCI_STATE_DISCONNECTING | HCI_STATE_AUTHENTICATING;
					bthci_StateConnecting(padapter,StateCmd,EntryNum);
					break;
				}

			case HCI_STATE_AUTHENTICATING:
				{
					pBTInfo->BtAsocEntry[EntryNum].BtNextState = HCI_STATE_DISCONNECTING | HCI_STATE_CONNECTED;
					bthci_StateAuth(padapter,StateCmd,EntryNum);
					break;
				}

			case HCI_STATE_CONNECTED:
				{
					pBTInfo->BtAsocEntry[EntryNum].BtNextState = HCI_STATE_CONNECTED | HCI_STATE_DISCONNECTING;
					bthci_StateConnected(padapter,StateCmd,EntryNum);
					break;
				}

			case HCI_STATE_DISCONNECTING:
				{
					pBTInfo->BtAsocEntry[EntryNum].BtNextState = HCI_STATE_DISCONNECTED | HCI_STATE_DISCONNECTING;
					bthci_StateDisconnecting(padapter,StateCmd,EntryNum);
					break;
				}

			case HCI_STATE_DISCONNECTED:
				{
					pBTInfo->BtAsocEntry[EntryNum].BtNextState = HCI_STATE_DISCONNECTED | HCI_STATE_STARTING | HCI_STATE_CONNECTING;
					bthci_StateDisconnected(padapter,StateCmd,EntryNum);
					break;
				}

			default:
				RTPRINT(FIOCTL, IOCTL_STATE, (" StateMachine, Unknown state to enter!!!\n"));
				break;
		}
	}
	else
	{
		RTPRINT(FIOCTL, IOCTL_STATE, (" StateMachine, Wrong state to enter\n"));
	}

	// 20100325 Joseph: Disable/Enable IPS/LPS according to BT status.
	if (!pBtMgnt->bBTConnectInProgress && !pBtMgnt->BtOperationOn)
	{
		RTPRINT(FIOCTL, IOCTL_STATE, ("[BT PS], IPSReturn()\n"));
		IPSReturn(padapter, IPS_DISABLE_BT_ON);
	}
}

void BTHCI_DisconnectPeer(PADAPTER padapter, u8 EntryNum)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, (" BTHCI_DisconnectPeer()\n"));

	BTHCI_SM_WITH_INFO(padapter,HCI_STATE_DISCONNECTING,STATE_CMD_MAC_CONNECT_CANCEL_INDICATE,EntryNum);

	if (pBTInfo->BtAsocEntry[EntryNum].bUsed)
	{
//		BTPKT_SendDeauthentication(padapter, pBTInfo->BtAsocEntry[EntryNum].BTRemoteMACAddr, unspec_reason); // not porting yet
	}

	if (pBtMgnt->bBTConnectInProgress)
	{
		pBtMgnt->bBTConnectInProgress = _FALSE;
		RTPRINT(FIOCTL, IOCTL_STATE, ("[BT Flag], BT Connect in progress OFF!!\n"));
	}

	bthci_RemoveEntryByEntryNum(padapter,EntryNum);

	if (pBtMgnt->bNeedNotifyAMPNoCap)
	{
		RTPRINT(FIOCTL, IOCTL_STATE, ("[BT AMPStatus], set to invalid in BTHCI_DisconnectPeer()\n"));
		BTHCI_EventAMPStatusChange(padapter, AMP_STATUS_NO_CAPACITY_FOR_BT);
	}
}

void BTHCI_EventNumOfCompletedDataBlocks(PADAPTER padapter)
{
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_HCI_INFO		pBtHciInfo = &pBTInfo->BtHciInfo;
	u8 localBuf[TmpLocalBufSize] = "";
	u8 *pRetPar, *pTriple;
	u8 len=0, i, j, handleNum=0;
	PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;
	u16 *pu2Temp, *pPackets, *pHandle, *pDblocks;
	u8 sent = 0;

#if 0
	PlatformZeroMemory(padapter->IrpHCILocalbuf.Ptr, padapter->IrpHCILocalbuf.Length);
	PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(buffer);
#else
	PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);
#endif

	if (!(pBtHciInfo->BTEventMaskPage2 & EMP2_HCI_EVENT_NUM_OF_COMPLETE_DATA_BLOCKS))
	{
		RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], Num Of Completed DataBlocks, Ignore to send NumOfCompletedDataBlocksEvent due to event mask page 2\n"));
		return;
	}

	// Return parameters starts from here
	pRetPar = &PPacketIrpEvent->Data[0];
	pTriple = &pRetPar[3];
	for (j=0; j<MAX_BT_ASOC_ENTRY_NUM; j++)
	{

		for (i=0; i<MAX_LOGICAL_LINK_NUM; i++)
		{
			if (pBTInfo->BtAsocEntry[j].LogLinkCmdData[i].BtLogLinkhandle)
			{
				handleNum++;
				pHandle = (u16*)&pTriple[0];	// Handle[i]
				pPackets = (u16*)&pTriple[2];	// Num_Of_Completed_Packets[i]
				pDblocks = (u16*)&pTriple[4];	// Num_Of_Completed_Blocks[i]
#if (SENDTXMEHTOD == 0 || SENDTXMEHTOD == 2)
				PlatformAcquireSpinLock(padapter, RT_TX_SPINLOCK);
#endif
				*pHandle = pBTInfo->BtAsocEntry[j].LogLinkCmdData[i].BtLogLinkhandle;
				*pPackets = (u16)pBTInfo->BtAsocEntry[j].LogLinkCmdData[i].TxPacketCount;
				*pDblocks = (u16)pBTInfo->BtAsocEntry[j].LogLinkCmdData[i].TxPacketCount;
				if (pBTInfo->BtAsocEntry[j].LogLinkCmdData[i].TxPacketCount)
				{
					sent = 1;
					RTPRINT(FIOCTL, IOCTL_BT_EVENT_DETAIL, ("[BT event], Num Of Completed DataBlocks, Handle = 0x%x, Num_Of_Completed_Packets = 0x%x, Num_Of_Completed_Blocks = 0x%x\n",
					*pHandle, *pPackets, *pDblocks));
				}
				pBTInfo->BtAsocEntry[j].LogLinkCmdData[i].TxPacketCount = 0;
#if (SENDTXMEHTOD == 0 || SENDTXMEHTOD == 2)
				PlatformReleaseSpinLock(padapter, RT_TX_SPINLOCK);
#endif
				len += 6;
				pTriple += len;
			}
		}
	}

	pRetPar[2] = handleNum;				// Number_of_Handles
	len += 1;
	pu2Temp = (u16*)&pRetPar[0];
	*pu2Temp = BTTotalDataBlockNum;
	len += 2;

	PPacketIrpEvent->EventCode = HCI_EVENT_NUM_OF_COMPLETE_DATA_BLOCKS;
	PPacketIrpEvent->Length = len;
	if (handleNum && sent)
	{
		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}
}

void BTHCI_EventNumOfCompletedPackets(PADAPTER padapter)
{
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	u8 localBuf[TmpLocalBufSize] = "";
	u8 *pRetPar, *pDouble;
	u8 len=0, i, j, handleNum=0;
	PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;
	u16 *pPackets, *pHandle;
	u8 sent = 0;

	PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);
	// Return parameters starts from here
	pRetPar = &PPacketIrpEvent->Data[0];
	pDouble = &pRetPar[1];
	for (j=0; j<MAX_BT_ASOC_ENTRY_NUM; j++)
	{
		for (i=0; i<MAX_LOGICAL_LINK_NUM; i++)
		{
			if (pBTInfo->BtAsocEntry[j].LogLinkCmdData[i].BtLogLinkhandle)
			{
				handleNum++;
				pHandle = (u16*)&pDouble[0];	// Handle[i]
				pPackets = (u16*)&pDouble[2];	// Num_Of_Completed_Packets[i]
				PlatformAcquireSpinLock(padapter, RT_TX_SPINLOCK);
				*pHandle = pBTInfo->BtAsocEntry[j].LogLinkCmdData[i].BtLogLinkhandle;
				*pPackets = (u16)pBTInfo->BtAsocEntry[j].LogLinkCmdData[i].TxPacketCount;
				if (pBTInfo->BtAsocEntry[j].LogLinkCmdData[i].TxPacketCount)
					sent = 1;
				pBTInfo->BtAsocEntry[j].LogLinkCmdData[i].TxPacketCount = 0;
				PlatformReleaseSpinLock(padapter, RT_TX_SPINLOCK);
				len += 4;
				pDouble += len;
			}
		}
	}

	pRetPar[0] = handleNum;				// Number_of_Handles
	len += 1;

	PPacketIrpEvent->EventCode = HCI_EVENT_NUMBER_OF_COMPLETE_PACKETS;
	PPacketIrpEvent->Length = len;
	if (handleNum && sent)
	{
		RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("BTHCI_EventNumOfCompletedPackets \n"));
		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}
}

void
BTHCI_EventAMPStatusChange(
	PADAPTER 					padapter,
	u8						AMP_Status
	)
{
//	PMGNT_INFO pMgntInfo = &padapter->MgntInfo;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	u8 len = 0;
	u8 localBuf[7] = "";
	u8 *pRetPar;
	PPACKET_IRP_HCIEVENT_DATA PPacketIrpEvent;

#if 0
	if (!(pBtHciInfo->BTEventMaskPage2 & EMP2_HCI_EVENT_AMP_STATUS_CHANGE))
	{
		RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("Ignore to send this event due to event mask page 2\n"));
		return;
	}
#endif

	if (AMP_Status==AMP_STATUS_NO_CAPACITY_FOR_BT)
	{
		pBtMgnt->BTNeedAMPStatusChg = _TRUE;
		pBtMgnt->bNeedNotifyAMPNoCap = _FALSE;

		BTHCI_DisconnectAll(padapter);
	}
	else if (AMP_Status == AMP_STATUS_FULL_CAPACITY_FOR_BT)
	{
		pBtMgnt->BTNeedAMPStatusChg = _FALSE;
	}

	PPacketIrpEvent = (PPACKET_IRP_HCIEVENT_DATA)(&localBuf[0]);
	// Return parameters starts from here
	pRetPar = &PPacketIrpEvent->Data[0];

	pRetPar[0] = 0;	// Status
	len += 1;
	pRetPar[1] = AMP_Status;	// AMP_Status
	len += 1;

	PPacketIrpEvent->EventCode = HCI_EVENT_AMP_STATUS_CHANGE;
	PPacketIrpEvent->Length = len;
	if (bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2) == RT_STATUS_SUCCESS)
	{
		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_STATE), ("[BT event], AMP Status Change, AMP_Status = %d\n", AMP_Status));
	}
}

void BTHCI_DisconnectAll(PADAPTER padapter)
{
	PADAPTER		pDefaultAdapter = GetDefaultAdapter(padapter);
//	PMGNT_INFO		pMgntInfo = &(pDefaultAdapter->MgntInfo);
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);

	u8 i;

	RTPRINT(FIOCTL, IOCTL_STATE, (" DisconnectALL()\n"));

	for (i=0; i<MAX_BT_ASOC_ENTRY_NUM; i++)
	{
		if (pBTInfo->BtAsocEntry[i].b4waySuccess == _TRUE)
		{
			BTHCI_SM_WITH_INFO(padapter, HCI_STATE_CONNECTED, STATE_CMD_DISCONNECT_PHY_LINK, i);
		}
		else if (pBTInfo->BtAsocEntry[i].bUsed == _TRUE)
		{
			if (pBTInfo->BtAsocEntry[i].BtCurrentState == HCI_STATE_CONNECTING)
			{
				BTHCI_SM_WITH_INFO(padapter, HCI_STATE_CONNECTING, STATE_CMD_MAC_CONNECT_CANCEL_INDICATE, i);
			}
			else if (pBTInfo->BtAsocEntry[i].BtCurrentState == HCI_STATE_DISCONNECTING)
			{
				BTHCI_SM_WITH_INFO(padapter, HCI_STATE_DISCONNECTING, STATE_CMD_MAC_CONNECT_CANCEL_INDICATE, i);
			}
		}
	}
}

HCI_STATUS
BTHCI_HandleHCICMD(
	PADAPTER 					padapter,
	PPACKET_IRP_HCICMD_DATA	pHciCmd
	)
{
	HCI_STATUS	status = HCI_STATUS_SUCCESS;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_DBG		pBtDbg = &pBTInfo->BtDbg;

	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("\n"));
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("HCI Command start, OGF=0x%x, OCF=0x%x, Length=0x%x\n",
		pHciCmd->OGF, pHciCmd->OCF, pHciCmd->Length));
#if 0 //for logo special test case only
	bthci_UseFakeData(padapter, pHciCmd);
#endif
	if (pHciCmd->Length)
	{
		RTPRINT_DATA(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), "HCI Command, Hex Data :\n",
			&pHciCmd->Data[0], pHciCmd->Length);
	}
	if (pHciCmd->OGF == OGF_EXTENSION)
	{
		if (pHciCmd->OCF == HCI_SET_RSSI_VALUE)
		{
			RTPRINT(FIOCTL, IOCTL_BT_EVENT_PERIODICAL, ("[BT cmd], "));
		}
		else
		{
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[BT cmd], "));
		}
	}
	else
	{
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("[BT cmd], "));
	}

	pBtDbg->dbgHciInfo.hciCmdCnt++;

	switch (pHciCmd->OGF)
	{
		case OGF_LINK_CONTROL_COMMANDS:
			status = bthci_HandleOGFLinkControlCMD(padapter, pHciCmd);
			break;
		case OGF_HOLD_MODE_COMMAND:
			break;
		case OGF_SET_EVENT_MASK_COMMAND:
			status = bthci_HandleOGFSetEventMaskCMD(padapter, pHciCmd);
			break;
		case OGF_INFORMATIONAL_PARAMETERS:
			status = bthci_HandleOGFInformationalParameters(padapter, pHciCmd);
			break;
		case OGF_STATUS_PARAMETERS:
			status = bthci_HandleOGFStatusParameters(padapter, pHciCmd);
			break;
		case OGF_TESTING_COMMANDS:
			status = bthci_HandleOGFTestingCMD(padapter, pHciCmd);
			break;
		case OGF_EXTENSION:
			status = bthci_HandleOGFExtension(padapter,pHciCmd);
			break;
		default:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI Command(), Unknown OGF = 0x%x\n", pHciCmd->OGF));
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_UNKNOWN_COMMAND\n"));
			status = bthci_UnknownCMD(padapter, pHciCmd);
			break;
	}
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_DETAIL, ("HCI Command execution end!!\n"));

	return status;
}

void
BTHCI_SetLinkStatusNotify(
	PADAPTER 					padapter,
	PPACKET_IRP_HCICMD_DATA	pHciCmd
	)
{
	bthci_CmdLinkStatusNotify(padapter, pHciCmd);
}

// ===== End of sync from SD7 driver COMMOM/bt_hci.c =====
#endif

#ifdef __HALBTC87231ANT_C__ // HAL/BTCoexist/HalBtc87231Ant.c

const char *const BtStateString[] =
{
	"BT_DISABLED",
	"BT_NO_CONNECTION",
	"BT_CONNECT_IDLE",
	"BT_INQ_OR_PAG",
	"BT_ACL_ONLY_BUSY",
	"BT_SCO_ONLY_BUSY",
	"BT_ACL_SCO_BUSY",
	"BT_STATE_NOT_DEFINED"
};

extern s32 FillH2CCmd(PADAPTER padapter, u8 ElementID, u32 CmdLen, u8 *pCmdBuffer);

// ===== Below this line is sync from SD7 driver HAL/BTCoexist/HalBtc87231Ant.c =====

void
btdm_SetFw50(
	PADAPTER	padapter,
	u8		byte1,
	u8		byte2,
	u8		byte3
	)
{
	u8			H2C_Parameter[3] = {0};

	H2C_Parameter[0] = byte1;
	H2C_Parameter[1] = byte2;
	H2C_Parameter[2] = byte3;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], FW write 0x50=0x%06x\n",
		H2C_Parameter[0]<<16|H2C_Parameter[1]<<8|H2C_Parameter[2]));

	FillH2CCmd(padapter, 0x50, 3, H2C_Parameter);
}

void btdm_SetFwIgnoreWlanAct(PADAPTER padapter, u8 bEnable)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			H2C_Parameter[1] = {0};

	if (bEnable)
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT Ignore Wlan_Act !!\n"));
		H2C_Parameter[0] |= BIT(0);		// function enable
		pHalData->bt_coexist.bFWCoexistAllOff = _FALSE;
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT don't ignore Wlan_Act !!\n"));
	}

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], set FW for BT Ignore Wlan_Act, write 0x25=0x%02x\n",
		H2C_Parameter[0]));

	FillH2CCmd(padapter, BT_IGNORE_WLAN_ACT_EID, 1, H2C_Parameter);
}

void btdm_NotifyFwScan(PADAPTER padapter, u8 scanType)
{
	u8			H2C_Parameter[1] = {0};

	if (scanType == _TRUE)
		H2C_Parameter[0] = 0x1;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], Notify FW for wifi scan, write 0x3b=0x%02x\n",
		H2C_Parameter[0]));

	FillH2CCmd(padapter, 0x3b, 1, H2C_Parameter);
}

void btdm_1AntSetPSMode(PADAPTER padapter, u8 enable, u8 mode)
{
	struct pwrctrl_priv *pwrctrl;


	RTPRINT(FBT, BT_TRACE, ("[BTCoex], PS %s option=%d\n", enable==_TRUE?"ENABLE":"DISABLE", mode));

	pwrctrl = &padapter->pwrctrlpriv;

	if (enable == _TRUE) {
		if (GET_HAL_DATA(padapter)->bt_coexist.halCoex8723.btdm1Ant.bWiFiHalt == _FALSE)
			rtw_set_ps_mode(padapter, PS_MODE_MIN, 0, mode);
	} else {
		rtw_set_ps_mode(padapter, PS_MODE_ACTIVE, 0, 0);
	}
}

void btdm_1AntTSFSwitch(PADAPTER padapter, u8 enable)
{
	u8 oldVal, newVal;


	oldVal = rtw_read8(padapter, 0x550);

	if (enable)
		newVal = oldVal | EN_BCN_FUNCTION;
	else
		newVal = oldVal & ~EN_BCN_FUNCTION;

	if (oldVal != newVal)
		rtw_write8(padapter, 0x550, newVal);
}

u8 btdm_Is1AntPsTdmaStateChange(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBTDM_8723A_1ANT	pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm1Ant;


	if ((pBtdm8723->bPrePsTdmaOn != pBtdm8723->bCurPsTdmaOn) ||
		(pBtdm8723->prePsTdma != pBtdm8723->curPsTdma))
	{
		return _TRUE;
	}
	else
	{
		return _FALSE;
	}
}

// Before enter TDMA, make sure Power Saving is enable!
void
btdm_1AntPsTdma(
	PADAPTER	padapter,
	u8		bTurnOn,
	u8		type
	)
{
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(padapter);
	PBTDM_8723A_1ANT	pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm1Ant;


	RTPRINT(FBT, BT_TRACE, ("[BTCoex], turn %s PS TDMA, type=%d\n", (bTurnOn? "ON":"OFF"), type));
	pBtdm8723->bCurPsTdmaOn = bTurnOn;
	pBtdm8723->curPsTdma = type;
	if (bTurnOn)
	{
		switch (type)
		{
			case 1:	// ACL low-retry type
			default:
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					// wide duration for WiFi
					BTDM_SetFw3a(padapter, 0x13, 0x1a, 0x1a, 0x0, 0x40);
				}
				break;
			case 2:	// ACL high-retry type - 1
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					// normal duration for WiFi
					BTDM_SetFw3a(padapter, 0x13, 0xf, 0xf, 0x0, 0x40);
				}
				break;
			case 3:	// for WiFi connected-busy & BT SCO busy
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					// protect 3 beacons in 3-beacon period & Tx pause at BT slot
					BTDM_SetFw3a(padapter, 0x93, 0x3f, 0x03, 0x10, 0x40);
				}
				break;
			case 4:	// for wifi scan & BT is connected
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					//protect 3 beacons in 3-beacon period & no Tx pause at BT slot
					BTDM_SetFw3a(padapter, 0x93, 0x15, 0x03, 0x10, 0x0);
				}
				break;
			case 5:	// for WiFi connected-busy & BT is Non-Connected-Idle
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					// SCO mode, Ant fixed at WiFi, WLAN_Act toggle
					BTDM_SetFw3a(padapter, 0xa9, 0x15, 0x03, 0x15, 0xc0);
				}
				break;
			case 6:	// for WiFi is connect idle & BT is not SCO
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					BTDM_SetFw3a(padapter, 0x13, 0xa, 0x3, 0x0, 0x0);
				}
				break;
			case 7: // for WiFi is at low RSSI case
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					BTDM_SetFw3a(padapter, 0x13, 0xc, 0x5, 0x0, 0x0);
				}
				break;
			case 8: // for WiFi Association, DHCP & BT is connected
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					// protect 3 beacons in 3-beacon period & Tx pause at BT slot
					BTDM_SetFw3a(padapter, 0x93, 0x25, 0x03, 0x10, 0x0);
				}
				break;
			case 9:	// ACL high-retry type - 2
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					// narrow duration for WiFi
					BTDM_SetFw3a(padapter, 0x13, 0xa, 0xa, 0x0, 0x40);
				}
				break;
			case 10: // for WiFi connect idle & BT ACL busy or WiFi Connected-Busy & BT is Inquiry
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					BTDM_SetFw3a(padapter, 0x13, 0xa, 0xa, 0x0, 0x40);
				}
				break;
			case 11: // ACL high-retry type - 3
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					// narrow duration for WiFi
					BTDM_SetFw3a(padapter, 0x13, 0x05, 0x05, 0x0, 0x0);
				}
				break;
			case 12: // for WiFi Connected-Busy & BT is Connected-Idle
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					// Allow High-Pri BT
					BTDM_SetFw3a(padapter, 0xa9, 0x0a, 0x03, 0x15, 0xc0);
				}
				break;
			case 15: // for WiFi connected-Idle vs BT-idle, BT-connectedIdle
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					// protect 1 beacons in 9-beacon period & Tx pause at BT slot
					BTDM_SetFw3a(padapter, 0x13, 0x0a, 0x03, 0x08, 0x00);
				}
				break;
			case 16: // for WiFi busy DHCP + BT ACL busy
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					BTDM_SetFw3a(padapter, 0x13, 0x15, 0x03, 0x00, 0x00);
				}
				break;
			case 18: // Re-DHCP
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					// protect 3 beacons in 3-beacon period & Tx pause at BT slot
					BTDM_SetFw3a(padapter, 0x93, 0x25, 0x03, 0x10, 0x00);
					// also send extend duration H2C cmd for WiFi
				}
				break;
			case 20: // WiFi only busy ,TDMA mode for power saving
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					BTDM_SetFw3a(padapter, 0x13, 0x25, 0x25, 0x00, 0x00);
				}
				break;
			case 21: // WiFi busy (BW 40) & BT SCO busy
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					BTDM_SetFw3a(padapter, 0x93, 0x35, 0x03, 0x10, 0x40);
				}
				break;
			case 22: // for WiFi busy\idle + BT ACL (A2DP¡ÏFTP) busy
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					BTDM_SetFw3a(padapter, 0x13, 0x08, 0x08, 0x00, 0x40);
				}
				break;
		}
	}
	else
	{
		// disable PS-TDMA
		switch (type)
		{
			case 8:
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					// Antenna control by PTA, 0x870 = 0x310
					BTDM_SetFw3a(padapter, 0x8, 0x0, 0x0, 0x0, 0x0);
				}
				break;
			case 0:
			default:
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					// Antenna control by PTA, 0x870 = 0x310
					BTDM_SetFw3a(padapter, 0x0, 0x0, 0x0, 0x0, 0x0);
				}
				rtw_write16(padapter, 0x860, 0x210); // Switch Antenna to BT
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], 0x860=0x210, Switch Antenna to BT\n"));
				break;
			case 9:
				if (btdm_Is1AntPsTdmaStateChange(padapter))
				{
					// Antenna control by PTA, 0x870 = 0x310
					BTDM_SetFw3a(padapter, 0x0, 0x0, 0x0, 0x0, 0x0);
				}
				rtw_write16(padapter, 0x860, 0x110); // Switch Antenna to WiFi
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], 0x860=0x110, Switch Antenna to WiFi\n"));
				break;
		}
	}

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], bPrePsTdmaOn = %d, bCurPsTdmaOn = %d!!\n",
		pBtdm8723->bPrePsTdmaOn, pBtdm8723->bCurPsTdmaOn));
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], prePsTdma = %d, curPsTdma = %d!!\n",
		pBtdm8723->prePsTdma, pBtdm8723->curPsTdma));

	// update pre state
	pBtdm8723->bPrePsTdmaOn = pBtdm8723->bCurPsTdmaOn;
	pBtdm8723->prePsTdma = pBtdm8723->curPsTdma;
}

void btdm_1AntSetPSTDMA(PADAPTER padapter, u8 bPSEn, u8 psOption, u8 bTDMAOn, u8 tdmaType)
{
	struct pwrctrl_priv *pwrctrl;
	PHAL_DATA_TYPE pHalData;
	PBTDM_8723A_1ANT pBtdm8723;
	u8 psMode;


	if ((check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE) == _FALSE) &&
		(get_fwstate(&padapter->mlmepriv) != WIFI_NULL_STATE))
	{
		btdm_1AntPsTdma(padapter, bTDMAOn, tdmaType);
		return;
	}

	RTPRINT(FBT, BT_TRACE,
			("[BTCoex], PS %s Option=%d, TDMA %s type=%d\n",
			 bPSEn==_TRUE?"ENABLE":"DISABLE", psOption,
			 bTDMAOn==_TRUE?"ENABLE":"DISABLE", tdmaType));

	pwrctrl = &padapter->pwrctrlpriv;
	pHalData = GET_HAL_DATA(padapter);
	pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm1Ant;

	if (bPSEn == _TRUE)
		psMode = PS_MODE_MIN;
	else
	{
		psMode = PS_MODE_ACTIVE;
		psOption = 0;
	}

	if ((psMode != pwrctrl->pwr_mode) ||
		((psMode != PS_MODE_ACTIVE) && (psOption != pwrctrl->bcn_ant_mode)))
	{
		// disable TDMA
		if (pBtdm8723->bCurPsTdmaOn == _TRUE)
		{
			if (bTDMAOn == _FALSE)
				btdm_1AntPsTdma(padapter, _FALSE, tdmaType);
			else
				btdm_1AntPsTdma(padapter, _FALSE, 9);
		}

		// change Power Save State
		btdm_1AntSetPSMode(padapter, bPSEn, psOption);
	}

	btdm_1AntPsTdma(padapter, bTDMAOn, tdmaType);
}

void btdm_1AntWifiParaAdjust(PADAPTER padapter, u8 bEnable)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBTDM_8723A_1ANT	pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm1Ant;

	if (bEnable)
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], wifi para adjust enable!!\n"));
		pBtdm8723->curWifiPara = 1;
		if (pBtdm8723->preWifiPara != pBtdm8723->curWifiPara)
		{
			BTDM_SetSwPenaltyTxRateAdaptive(padapter, BT_TX_RATE_ADAPTIVE_LOW_PENALTY);
		}
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], wifi para adjust disable!!\n"));
		pBtdm8723->curWifiPara = 2;
		if (pBtdm8723->preWifiPara != pBtdm8723->curWifiPara)
		{
			BTDM_SetSwPenaltyTxRateAdaptive(padapter, BT_TX_RATE_ADAPTIVE_NORMAL);
		}
	}

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], preWifiPara = %d, curWifiPara = %d!!\n",
		pBtdm8723->preWifiPara, pBtdm8723->curWifiPara));
	pBtdm8723->preWifiPara = pBtdm8723->curWifiPara;
}

void btdm_1AntPtaParaReload(PADAPTER padapter)
{
	// PTA parameter
	rtw_write8(padapter, 0x6cc, 0x0);			// 1-Ant coex
	rtw_write32(padapter, 0x6c8, 0xffff);		// wifi break table
	rtw_write32(padapter, 0x6c4, 0x55555555);	// coex table

	// Antenna switch control parameter
	rtw_write32(padapter, 0x858, 0xaaaaaaaa);
	if (IS_8723A_A_CUT(GET_HAL_DATA(padapter)->VersionID))
	{
		rtw_write32(padapter, 0x870, 0x0);	// SPDT(connected with TRSW) control by hardware PTA
		rtw_write8(padapter, 0x40, 0x24);
	}
	else
	{
		rtw_write8(padapter, 0x40, 0x20);
		rtw_write16(padapter, 0x860, 0x210);	// set antenna at bt side if ANTSW is software control
		rtw_write32(padapter, 0x870, 0x300);	// SPDT(connected with TRSW) control by hardware PTA
		rtw_write32(padapter, 0x874, 0x22804000);	// ANTSW keep by GNT_BT
	}

	// coexistence parameters
	rtw_write8(padapter, 0x778, 0x1);	// enable RTK mode PTA

	// BT don't ignore WLAN_Act
	btdm_SetFwIgnoreWlanAct(padapter, _FALSE);
}

/*
 * Return
 *	1: upgrade (add WiFi duration time)
 *	0: keep
 *	-1: downgrade (add BT duration time)
 */
s8 btdm_1AntTdmaJudgement(PADAPTER padapter, u8 retry)
{
	PHAL_DATA_TYPE		pHalData;
	PBTDM_8723A_1ANT	pBtdm8723;
	static s8 up = 0, dn = 0, m = 1, n = 3, WaitCount= 0;
	s8 ret;


	pHalData = GET_HAL_DATA(padapter);
	pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm1Ant;
	ret = 0;

	if (pBtdm8723->psTdmaMonitorCnt == 0)
	{
		up = 0;
		dn = 0;
		m = 1;
		n = 3;
		WaitCount = 0;
	}
	else
	{
		WaitCount++;
	}

	if (retry == 0)  // no retry in the last 2-second duration
	{
		up++;
		dn--;
		if (dn < 0) dn = 0;

		if (up >= 3*m)
		{
			// retry=0 in consecutive 3m*(2s), add WiFi duration
			ret = 1;

			n = 3;
			up = 0;
			dn = 0;
			WaitCount = 0;
		}
	}
	else if (retry <= 3)  // retry<=3 in the last 2-second duration
	{
		up--;
		dn++;
		if (up < 0) up = 0;

		if (dn == 2)
		{
			// retry<=3 in consecutive 2*(2s), minus WiFi duration (add BT duration)
			ret = -1;

			// record how many time downgrad WiFi duration
			if (WaitCount <= 2)
				m++;
			else
				m = 1;
			// the max number of m is 20
			// the longest time of upgrade WiFi duration is 20*3*2s = 120s
			if (m >= 20) m = 20;

			up = 0;
			dn = 0;
			WaitCount = 0;
		}
	}
	else  // retry count > 3
	{
		// retry>3, minus WiFi duration (add BT duration)
		ret = -1;

		// record how many time downgrad WiFi duration
		if (WaitCount == 1)
			m++;
		else
			m = 1;
		if (m >= 20) m = 20;

		up = 0;
		dn = 0;
		WaitCount = 0;
	}

	return ret;
}

void btdm_1AntTdmaDurationAdjust(PADAPTER padapter)
{
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(padapter);
	PBTDM_8723A_1ANT	pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm1Ant;


	RTPRINT(FBT, BT_TRACE, ("[BTCoex], TdmaDurationAdjust\n"));
	if (pBtdm8723->psTdmaGlobalCnt != pBtdm8723->psTdmaMonitorCnt)
	{
		pBtdm8723->psTdmaMonitorCnt = 0;
		pBtdm8723->psTdmaGlobalCnt = 0;
	}
	if (pBtdm8723->psTdmaMonitorCnt == 0)
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], TdmaDurationAdjust, first time execute!!\n"));
		btdm_1AntSetPSTDMA(padapter, _TRUE, 1, _TRUE, 2);
		pBtdm8723->psTdmaDuAdjType = 2;
	}
	else
	{
		// Now we only have 4 level Ps Tdma,
		// if that's not the following 4 level(will changed by wifi scan, dhcp...),
		// then we have to adjust it back to the previous record one.
		if ((pBtdm8723->curPsTdma != 1) &&
			(pBtdm8723->curPsTdma != 2) &&
			(pBtdm8723->curPsTdma != 9) &&
			(pBtdm8723->curPsTdma != 11))
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], tdma adjust type can only be 1/2/9/11 !!!\n"));
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], the latest adjust type = %d\n", pBtdm8723->psTdmaDuAdjType));

			btdm_1AntSetPSTDMA(padapter, _TRUE, 1, _TRUE, pBtdm8723->psTdmaDuAdjType);
		}
		else
		{
			s32 judge = 0;

			judge = btdm_1AntTdmaJudgement(padapter, pHalData->bt_coexist.halCoex8723.btRetryCnt);
			if (judge == -1)
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], TdmaDurationAdjust, Upgrade WiFi duration\n"));
				if (pBtdm8723->curPsTdma == 1)
				{
					// Decrease WiFi duration for high BT retry
					btdm_1AntSetPSTDMA(padapter, _TRUE, 1, _TRUE, 2);
					pBtdm8723->psTdmaDuAdjType = 2;
				}
				else if (pBtdm8723->curPsTdma == 2)
				{
					btdm_1AntSetPSTDMA(padapter, _TRUE, 1, _TRUE, 9);
					pBtdm8723->psTdmaDuAdjType = 9;
				}
				else if (pBtdm8723->curPsTdma == 9)
				{
					btdm_1AntSetPSTDMA(padapter, _TRUE, 1, _TRUE, 11);
					pBtdm8723->psTdmaDuAdjType = 11;
				}
			}
			else if (judge == 1)
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], TdmaDurationAdjust, Downgrade WiFi duration!!\n"));

				if (pBtdm8723->curPsTdma == 11)
				{
					btdm_1AntSetPSTDMA(padapter, _TRUE, 1, _TRUE, 9);
					pBtdm8723->psTdmaDuAdjType = 9;
				}
				else if (pBtdm8723->curPsTdma == 9)
				{
					btdm_1AntSetPSTDMA(padapter, _TRUE, 1, _TRUE, 2);
					pBtdm8723->psTdmaDuAdjType = 2;
				}
				else if (pBtdm8723->curPsTdma == 2)
				{
					btdm_1AntSetPSTDMA(padapter, _TRUE, 1, _TRUE, 1);
					pBtdm8723->psTdmaDuAdjType = 1;
				}
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], TdmaDurationAdjust, no need to change\n"));
			}
		}

		RTPRINT(FBT, BT_TRACE, ("[BTCoex], current PS TDMA is %s, type=%d\n",
			(pBtdm8723->bCurPsTdmaOn? "ON":"OFF"), pBtdm8723->curPsTdma));
	}

	pBtdm8723->psTdmaMonitorCnt++;
}

void btdm_1AntCoexProcessForWifiConnect(PADAPTER padapter)
{
	struct mlme_priv *pmlmepriv;
	PHAL_DATA_TYPE pHalData;
	PBT_COEXIST_8723A pBtCoex;
	PBTDM_8723A_1ANT pBtdm8723;
	u8 BtState;


	RTPRINT(FBT, BT_TRACE, ("[BTCoex], 1AntCoexProcessForWifiConnect!!\n"));

	pmlmepriv = &padapter->mlmepriv;
	pHalData = GET_HAL_DATA(padapter);
	pBtCoex = &pHalData->bt_coexist.halCoex8723;
	pBtdm8723 = &pBtCoex->btdm1Ant;
	BtState = pBtCoex->c2hBtInfo;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], WiFi is %s\n", BTDM_IsWifiBusy(padapter)?"Busy":"IDLE"));
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT is %s\n", BtStateString[BtState]));

	if ((!BTDM_IsWifiBusy(padapter)) &&
		((BtState == BT_INFO_STATE_NO_CONNECTION) || (BtState == BT_INFO_STATE_CONNECT_IDLE)))
	{
		switch (BtState)
		{
			case BT_INFO_STATE_NO_CONNECTION:
				btdm_1AntSetPSTDMA(padapter, _TRUE, 3, _FALSE, 9);
				break;
			case BT_INFO_STATE_CONNECT_IDLE:
				btdm_1AntSetPSTDMA(padapter, _TRUE, 7, _FALSE, 0);
				break;
		}
	}
	else
	{
		u8 val8;

		val8 = rtw_read8(padapter, 0x883);
		val8 &= 0x03;
		if ((BtState == BT_INFO_STATE_SCO_ONLY_BUSY) ||
			(BtState == BT_INFO_STATE_ACL_SCO_BUSY))
		{
			val8 |= 0x60; // 0x880[31:26]=011000
		}
		else
		{
			val8 |= 0xC0; // 0x880[31:26]=110000
		}
		rtw_write8(padapter, 0x883, val8);

		switch (BtState)
		{
			case BT_INFO_STATE_NO_CONNECTION:
				// WiFi is Busy
				btdm_1AntSetPSTDMA(padapter, _FALSE, 0, _TRUE, 5);
				break;
			case BT_INFO_STATE_CONNECT_IDLE:
				// WiFi is Busy
				btdm_1AntSetPSTDMA(padapter, _FALSE, 0, _TRUE, 12);
				break;
			case BT_INFO_STATE_INQ_OR_PAG:
				if (BTDM_IsWifiBusy(padapter) == _TRUE)
				{
					btdm_1AntSetPSTDMA(padapter, _TRUE, 1, _TRUE, 10);
				}
				else
				{
					btdm_1AntSetPSTDMA(padapter, _TRUE, 3, _FALSE, 9);
				}
				break;
			case BT_INFO_STATE_SCO_ONLY_BUSY:
			case BT_INFO_STATE_ACL_SCO_BUSY:
				if (BTDM_IsHT40(padapter) == _FALSE) // 20 MHz
					btdm_1AntSetPSTDMA(padapter, _TRUE, 1, _TRUE, 3);
				else
					btdm_1AntSetPSTDMA(padapter, _TRUE, 1, _TRUE, 21);
				break;
			case BT_INFO_STATE_ACL_ONLY_BUSY:
				if (pBtCoex->c2hBtProfile == BTINFO_B_FTP)
				{
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT PROFILE is FTP\n"));
					btdm_1AntSetPSTDMA(padapter, _TRUE, 1, _TRUE, 1);
				}
				else if (pBtCoex->c2hBtProfile == (BTINFO_B_A2DP|BTINFO_B_FTP))
				{
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT PROFILE is A2DP_FTP\n"));
					btdm_1AntSetPSTDMA(padapter, _TRUE, 1, _TRUE, 22);
				}
				else
				{
					if (pBtCoex->c2hBtProfile == BTINFO_B_A2DP)
					{
						RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT PROFILE is A2DP\n"));
					}
					else
					{
						RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT PROFILE is UNKNOWN(0x%02X)! Use A2DP Profile\n", pBtCoex->c2hBtProfile));
					}
					btdm_1AntTdmaDurationAdjust(padapter);
				}
				break;
		}
	}

	pBtdm8723->psTdmaGlobalCnt++;
}

void btdm_1AntBTStateChangeHandler(PADAPTER padapter, BT_STATE_1ANT oldState, BT_STATE_1ANT newState)
{
	if (oldState == newState)
		return;


	RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT state change, %s => %s\n", BtStateString[oldState], BtStateString[newState]));

	if ((oldState <= BT_INFO_STATE_NO_CONNECTION) &&
		(newState > BT_INFO_STATE_NO_CONNECTION))
	{
		btdm_SetFwIgnoreWlanAct(padapter, _FALSE);
	}

	if (oldState == BT_INFO_STATE_ACL_ONLY_BUSY)
	{
		PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
		pHalData->bt_coexist.halCoex8723.btdm1Ant.psTdmaMonitorCnt = 0;
	}
}

void btdm_1AntBtCoexistHandler(PADAPTER padapter)
{
	PHAL_DATA_TYPE		pHalData;
	PBT_COEXIST_8723A	pBtCoex8723;
	PBTDM_8723A_1ANT	pBtdm8723;
	u8			u1tmp;


	pHalData = GET_HAL_DATA(padapter);
	pBtCoex8723 = &pHalData->bt_coexist.halCoex8723;
	pBtdm8723 = &pBtCoex8723->btdm1Ant;

	if (BT_IsBtDisabled(padapter) == _TRUE)
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT is disabled\n"));

		if (BTDM_IsWifiConnectionExist(padapter) == _TRUE)
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], wifi is connected\n"));

			if (BTDM_IsWifiBusy(padapter) == _TRUE)
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], Wifi is busy\n"));
				btdm_1AntSetPSTDMA(padapter, _FALSE, 0, _FALSE, 9);
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], Wifi is idle\n"));
				btdm_1AntSetPSTDMA(padapter, _TRUE, 1, _FALSE, 9);
			}
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], wifi is disconnected\n"));

			btdm_1AntSetPSTDMA(padapter, _FALSE, 0, _FALSE, 9);
		}
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT is enabled\n"));

		if (BTDM_IsWifiConnectionExist(padapter) == _TRUE)
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], wifi is connected\n"));

			btdm_1AntWifiParaAdjust(padapter, _TRUE);
			btdm_1AntCoexProcessForWifiConnect(padapter);
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], wifi is disconnected\n"));

			// Antenna switch at BT side(0x870 = 0x300, 0x860 = 0x210) after PSTDMA off
			btdm_1AntWifiParaAdjust(padapter, _FALSE);
			btdm_1AntSetPSTDMA(padapter, _FALSE, 0, _FALSE, 0);
		}
	}

	btdm_1AntBTStateChangeHandler(padapter, pBtCoex8723->prec2hBtInfo, pBtCoex8723->c2hBtInfo);
	pBtCoex8723->prec2hBtInfo = pBtCoex8723->c2hBtInfo;
}

void
BTDM_1AntSetWifiRssiThresh(
	PADAPTER	padapter,
	u8		rssiThresh
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBTDM_8723A_1ANT pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm1Ant;

	pBtdm8723->wifiRssiThresh = rssiThresh;
	DbgPrint("cosa set rssi thresh = %d\n", pBtdm8723->wifiRssiThresh);
}

void BTDM_1AntParaInit(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBTDM_8723A_1ANT	pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm1Ant;

	// Enable counter statistics
	rtw_write8(padapter, 0x76e, 0x4);
	btdm_1AntPtaParaReload(padapter);

	pBtdm8723->wifiRssiThresh = 48;

	pBtdm8723->bWiFiHalt = _FALSE;
}

void BTDM_1AntForHalt(PADAPTER padapter)
{
	RTPRINT(FBT, BT_TRACE, ("\n[BTCoex], 1Ant for halt\n"));

	GET_HAL_DATA(padapter)->bt_coexist.halCoex8723.btdm1Ant.bWiFiHalt == _TRUE;

	btdm_1AntWifiParaAdjust(padapter, _FALSE);
	btdm_1AntSetPSTDMA(padapter, _FALSE, 0, _FALSE, 0);
}

void BTDM_1AntLpsLeave(PADAPTER padapter)
{
	btdm_1AntPsTdma(padapter, _FALSE, 8);
}

void BTDM_1AntWifiAssociateNotify(PADAPTER padapter, u8 type)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);


	RTPRINT(FBT, BT_TRACE, ("\n[BTCoex], 1Ant for associate, type=%d\n", type));

	if (type)
	{
		if (BT_IsBtDisabled(padapter) == _TRUE)
		{
			btdm_1AntPsTdma(padapter, _FALSE, 9);
		}
		else
		{
			btdm_1AntTSFSwitch(padapter, _TRUE);
			btdm_1AntPsTdma(padapter, _TRUE, 8);	// extend wifi slot
		}
	}
	else
	{
		if (BT_IsBtDisabled(padapter) == _FALSE)
		{
			btdm_1AntPsTdma(padapter, _FALSE, 9);

			if (BTDM_IsWifiConnectionExist(padapter) == _FALSE)
			{
				btdm_1AntTSFSwitch(padapter, _FALSE);
			}
		}

		btdm_1AntBtCoexistHandler(padapter);
	}
}

void BTDM_1AntMediaStatusNotify(PADAPTER padapter, RT_MEDIA_STATUS mstatus)
{
	RTPRINT(FBT, BT_TRACE, ("\n[BTCoex], wifi status change to %s(%s)\n",
			mstatus==RT_MEDIA_CONNECT?"connect":"disconnect",
			BTDM_IsWifiConnectionExist(padapter)==_TRUE?"connect":"disconnect"));

	btdm_1AntBtCoexistHandler(padapter);
}

void BTDM_1AntForDhcp(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;
	u8 u1tmp;


	RTPRINT(FBT, BT_TRACE, ("\n[BTCoex], 1Ant for DHCP\n"));

	pHalData = GET_HAL_DATA(padapter);

	if (BT_IsBtDisabled(padapter) == _TRUE)
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], 1Ant for DHCP, BT is disabled\n"));
		btdm_1AntSetPSTDMA(padapter, _FALSE, 0, _FALSE, 9);
	}
	else
	{
		if (BTDM_IsWifiBusy(padapter) == _TRUE)
		{
			u8 BtState;
			PBTDM_8723A_1ANT pBtdm8723;

			BtState = pHalData->bt_coexist.halCoex8723.c2hBtInfo;
			pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm1Ant;

			RTPRINT(FBT, BT_TRACE, ("[BTCoex], 1Ant for DHCP, WiFi is Busy\n"));
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], %s\n", BtStateString[BtState]));
			switch (BtState)
			{
				case BT_INFO_STATE_NO_CONNECTION:
				case BT_INFO_STATE_CONNECT_IDLE:
					btdm_1AntBtCoexistHandler(padapter);
					break;

				case BT_INFO_STATE_ACL_ONLY_BUSY:
					switch (pBtdm8723->curPsTdma)
					{
						case 1:
						case 2:
							RTPRINT(FBT, BT_TRACE, ("[BTCoex], 1Ant for DHCP, Keep PSTDMA type=%d\n", pBtdm8723->curPsTdma));
							break;
						default:
							btdm_1AntSetPSTDMA(padapter, _TRUE, 1, _TRUE, 16);
							break;
					}
					break;

				default:
					btdm_1AntSetPSTDMA(padapter, _TRUE, 1, _TRUE, 18); // extend wifi slot
					break;
			}
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], 1Ant for DHCP, WiFi is Idle\n"));
			btdm_1AntSetPSTDMA(padapter, _TRUE, 1, _TRUE, 18); // extend wifi slot
		}
	}
}

void BTDM_1AntWifiScanNotify(PADAPTER padapter, u8 scanType)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8 u1tmp;


	RTPRINT(FBT, BT_TRACE, ("\n[BTCoex], 1Ant for wifi scan=%d!!\n", scanType));

	if (scanType)
	{
		if (BT_IsBtDisabled(padapter) == _TRUE)
		{
			btdm_1AntSetPSTDMA(padapter, _FALSE, 0, _FALSE, 9);
		}
		else
		{
			if (BTDM_IsWifiConnectionExist(padapter) == _FALSE)
			{
				btdm_1AntTSFSwitch(padapter, _TRUE);
			}

			btdm_1AntPsTdma(padapter, _TRUE, 4); // Antenna control by TDMA
		}

		btdm_NotifyFwScan(padapter, 1);
	}
	else // WiFi_Finish_Scan
	{
		btdm_NotifyFwScan(padapter, 0);

		if (BT_IsBtDisabled(padapter) == _FALSE)
		{
			if (BTDM_IsWifiConnectionExist(padapter) == _FALSE)
			{
				btdm_1AntPsTdma(padapter, _FALSE, 9);
				btdm_1AntTSFSwitch(padapter, _FALSE);
			}
		}

		btdm_1AntBtCoexistHandler(padapter);
	}
}

void BTDM_1AntFwC2hBtInfo8723A(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData;
	PBT30Info		pBTInfo;
	PBT_MGNT		pBtMgnt;
	PBT_COEXIST_8723A pBtCoex;
	u8	u1tmp, btState;


	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;
	pBtCoex = &pHalData->bt_coexist.halCoex8723;

	u1tmp = pBtCoex->c2hBtInfoOriginal;
	// sco BUSY bit is not used on voice over PCM platform
	btState = u1tmp & 0xF;
	pBtCoex->c2hBtProfile = u1tmp & 0xE0;

	// default set bt to idle state.
	pBtMgnt->ExtConfig.bBTBusy = _FALSE;
	pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_IDLE;

	// check BIT2 first ==> check if bt is under inquiry or page scan
	if (btState & BIT(2))
	{
		pBtCoex->bC2hBtInquiryPage = _TRUE;
	}
	else
	{
		pBtCoex->bC2hBtInquiryPage = _FALSE;
	}
	btState &= ~BIT(2);

	if (!(btState & BIT(0)))
	{
		pBtCoex->c2hBtInfo = BT_INFO_STATE_NO_CONNECTION;
	}
	else
	{
		if (btState == 0x1)
		{
			pBtCoex->c2hBtInfo = BT_INFO_STATE_CONNECT_IDLE;
		}
		else if (btState == 0x9)
		{
			pBtCoex->c2hBtInfo = BT_INFO_STATE_ACL_ONLY_BUSY;
			pBtMgnt->ExtConfig.bBTBusy = _TRUE;
		}
		else if (btState == 0x3)
		{
			pBtCoex->c2hBtInfo = BT_INFO_STATE_SCO_ONLY_BUSY;
			pBtMgnt->ExtConfig.bBTBusy = _TRUE;
		}
		else if (btState == 0xb)
		{
			pBtCoex->c2hBtInfo = BT_INFO_STATE_ACL_SCO_BUSY;
			pBtMgnt->ExtConfig.bBTBusy = _TRUE;
		}
		else
		{
			pBtCoex->c2hBtInfo = BT_INFO_STATE_MAX;
		}
		if (_TRUE == pBtMgnt->ExtConfig.bBTBusy)
			pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_IDLE;
	}

	if ((BT_INFO_STATE_NO_CONNECTION == pBtCoex->c2hBtInfo) ||
		(BT_INFO_STATE_CONNECT_IDLE == pBtCoex->c2hBtInfo))
	{
		if (pHalData->bt_coexist.halCoex8723.bC2hBtInquiryPage)
			pHalData->bt_coexist.halCoex8723.c2hBtInfo = BT_INFO_STATE_INQ_OR_PAG;
	}

	RTPRINT(FBT, BT_TRACE, ("[BTC2H], Bt state=%d\n",
		pHalData->bt_coexist.halCoex8723.c2hBtInfo));

	switch(pHalData->bt_coexist.halCoex8723.c2hBtInfo)
	{
		case BT_INFO_STATE_DISABLED:
			RTPRINT(FBT, BT_TRACE, ("Bt is disabled!!\n"));
			break;
		case BT_INFO_STATE_NO_CONNECTION:
			RTPRINT(FBT, BT_TRACE, ("Bt is disconnected!!\n"));
			break;
		case BT_INFO_STATE_CONNECT_IDLE:
			RTPRINT(FBT, BT_TRACE, ("Bt is connected & idle!!\n"));
			break;
		case BT_INFO_STATE_INQ_OR_PAG:
			RTPRINT(FBT, BT_TRACE, ("Bt is inquirying or paging!!\n"));
			break;
		case BT_INFO_STATE_ACL_ONLY_BUSY:
			RTPRINT(FBT, BT_TRACE, ("Bt is ACL only busy!!\n"));
			break;
		case BT_INFO_STATE_SCO_ONLY_BUSY:
			RTPRINT(FBT, BT_TRACE, ("Bt is SCO only busy!!\n"));
			break;
		case BT_INFO_STATE_ACL_SCO_BUSY:
			RTPRINT(FBT, BT_TRACE, ("Bt is ACL+SCO busy!!\n"));
			break;
		default:
			RTPRINT(FBT, BT_TRACE, ("Undefined!!\n"));
			break;
	}
}

void BTDM_1AntBtCoexist8723A(PADAPTER padapter)
{
	struct mlme_priv *pmlmepriv;
	PHAL_DATA_TYPE	pHalData;
	u32 curr_time, delta_time;


	pmlmepriv = &padapter->mlmepriv;
	pHalData = GET_HAL_DATA(padapter);

	if (check_fwstate(pmlmepriv, WIFI_SITE_MONITOR) == _TRUE)
	{
		// already done in BTDM_1AntForScan()
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], wifi is under scan progress!!\n"));
		return;
	}

	if (check_fwstate(pmlmepriv, WIFI_UNDER_LINKING) == _TRUE)
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], wifi is under link progress!!\n"));
		return;
	}

	// under DHCP(Special packet)
	curr_time = rtw_get_current_time();
	delta_time = curr_time - padapter->pwrctrlpriv.DelayLPSLastTimeStamp;
	delta_time = rtw_systime_to_ms(delta_time);
	if (delta_time < 500) // 500ms
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], wifi is under DHCP progress(%d ms)!!\n", delta_time));
		return;
	}

	BTDM_CheckWiFiState(padapter);

	btdm_1AntBtCoexistHandler(padapter);
}

// ===== End of sync from SD7 driver HAL/BTCoexist/HalBtc87231Ant.c =====
#endif

#ifdef __HALBTC87232ANT_C__ // HAL/BTCoexist/HalBtc87232Ant.c
// ===== Below this line is sync from SD7 driver HAL/BTCoexist/HalBtc87232Ant.c =====

//============================================================
// local function proto type if needed
//============================================================
void btdm_SetBtdm(PADAPTER padapter, PBTDM_8723A_2ANT pBtdm);

//============================================================
// local function start with btdm_
//============================================================
void btdm_BtInqPageMonitor(PADAPTER	padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if(pHalData->bt_coexist.halCoex8723.bC2hBtInquiryPage)
	{

		// bt inquiry or page is started.
		if(pHalData->bt_coexist.halCoex8723.btInqPageStartTime == 0)
		{
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_INQ_PAGE;
			pHalData->bt_coexist.halCoex8723.btInqPageStartTime = rtw_get_current_time();
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT Inquiry/page is started at time : 0x%"i64fmt"x \n", 
			pHalData->bt_coexist.halCoex8723.btInqPageStartTime));
		}	
	}
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT Inquiry/page started time : 0x%"i64fmt"x, curTime : 0x%x \n", 
		pHalData->bt_coexist.halCoex8723.btInqPageStartTime,  rtw_get_current_time()));

	if(pHalData->bt_coexist.halCoex8723.btInqPageStartTime)
	{
		if((rtw_get_passing_time_ms(pHalData->bt_coexist.halCoex8723.btInqPageStartTime)/1000) >= 10)
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT Inquiry/page >= 10sec!!!"));
			pHalData->bt_coexist.halCoex8723.btInqPageStartTime = 0;
			pHalData->bt_coexist.CurrentState &=~ BT_COEX_STATE_BT_INQ_PAGE;
		}
	}
}

u8 btdm_NeedToDecBtPwr(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PADAPTER	pDefaultAdapter = GetDefaultAdapter(padapter);


//	if (MgntLinkStatusQuery(pDefaultAdapter) == RT_MEDIA_CONNECT)
	if (check_fwstate(&pDefaultAdapter->mlmepriv, WIFI_ASOC_STATE) == _TRUE)
	{
		RTPRINT(FBT, BT_TRACE, ("Need to decrease bt power\n"));
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_DEC_BT_POWER;
		return _TRUE;
	}
	pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_DEC_BT_POWER;
	return _FALSE;
}

u8 btdm_IsBadIsolation(PADAPTER padapter)
{
	return _FALSE;
}

void
btdm_SetCoexTable(
	PADAPTER	padapter,
	u32		val0x6c0,
	u32		val0x6c8,
	u8		val0x6cc
	)
{
	RTPRINT(FBT, BT_TRACE, ("set coex table, set 0x6c0=0x%x\n", val0x6c0));
	rtw_write32(padapter, 0x6c0, val0x6c0);

	RTPRINT(FBT, BT_TRACE, ("set coex table, set 0x6c8=0x%x\n", val0x6c8));
	rtw_write32(padapter, 0x6c8, val0x6c8);

	RTPRINT(FBT, BT_TRACE, ("set coex table, set 0x6cc=0x%x\n", val0x6cc));
	rtw_write8(padapter, 0x6cc, val0x6cc);
}

void btdm_SetHwPtaMode(PADAPTER padapter, u8 bMode)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (BT_PTA_MODE_ON == bMode)
	{
		RTPRINT(FBT, BT_TRACE, ("PTA mode on\n"));
		// Enable GPIO 0/1/2/3/8 pins for bt
		rtw_write8(padapter, 0x40, 0x20);
		pHalData->bt_coexist.bHWCoexistAllOff = _FALSE;
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("PTA mode off\n"));
		rtw_write8(padapter, 0x40, 0x0);
	}
}

void
btdm_BtdmStrctureReload(
	PADAPTER		padapter,
	PBTDM_8723A_2ANT	pBtdm
	)
{
	pBtdm->bAllOff = _FALSE;
	pBtdm->bAgcTableEn = _FALSE;
	pBtdm->bAdcBackOffOn = _FALSE;
	pBtdm->b2AntHidEn = _FALSE;
	pBtdm->bLowPenaltyRateAdaptive = _FALSE;
	pBtdm->bRfRxLpfShrink = _FALSE;
	pBtdm->bRejectAggrePkt= _FALSE;

	pBtdm->bTdmaOn = _FALSE;
	pBtdm->tdmaAnt = TDMA_2ANT;
	pBtdm->tdmaNav = TDMA_NAV_OFF;
	pBtdm->tdmaDacSwing = TDMA_DAC_SWING_OFF;
	pBtdm->fwDacSwingLvl = 0x20;

	pBtdm->bTraTdmaOn = _FALSE;
	pBtdm->traTdmaAnt = TDMA_2ANT;
	pBtdm->traTdmaNav = TDMA_NAV_OFF;
	pBtdm->bIgnoreWlanAct = _FALSE;

	pBtdm->bPsTdmaOn = _FALSE;
	pBtdm->psTdmaByte[0] = 0x0;
	pBtdm->psTdmaByte[1] = 0x0;
	pBtdm->psTdmaByte[2] = 0x0;
	pBtdm->psTdmaByte[3] = 0x8;

	pBtdm->bPtaOn = _TRUE;
	pBtdm->val0x6c0 = 0x5a5aaaaa;
	pBtdm->val0x6c8 = 0xcc;
	pBtdm->val0x6cc = 0x3;

	pBtdm->bSwDacSwingOn = _FALSE;
	pBtdm->swDacSwingLvl = 0xc0;
	pBtdm->wlanActHi = 0x20;
	pBtdm->wlanActLo = 0x10;
	pBtdm->btRetryIndex = 2;

	pBtdm->bDecBtPwr = _FALSE;
}

void
btdm_BtdmStrctureReloadAllOff(
	PADAPTER			padapter,
	PBTDM_8723A_2ANT		pBtdm
	)
{
	btdm_BtdmStrctureReload(padapter, pBtdm);
	pBtdm->bAllOff = _TRUE;
	pBtdm->bPtaOn = _FALSE;
	pBtdm->wlanActHi = 0x10;
}

u8 btdm_Is2Ant8723ACommonAction(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	BTDM_8723A_2ANT	btdm8723;
	u8			bCommon = _FALSE;

	btdm_BtdmStrctureReload(padapter, &btdm8723);
	if (btdm_IsBadIsolation(padapter))
	{
		RTPRINT(FBT, BT_TRACE, ("Bad isolation, bt coex mechanism always off!!\n"));
		btdm_BtdmStrctureReloadAllOff(padapter, &btdm8723);
		bCommon = _TRUE;
	}
	else if (!BTDM_IsWifiBusy(padapter) && !BTDM_IsBTBusy(padapter))
	{
		RTPRINT(FBT, BT_TRACE, ("Wifi idle + Bt idle, bt coex mechanism always off!!\n"));
		btdm_BtdmStrctureReloadAllOff(padapter, &btdm8723);
		bCommon = _TRUE;
	}
	else if (BTDM_IsWifiBusy(padapter) && !BTDM_IsBTBusy(padapter))
	{
		RTPRINT(FBT, BT_TRACE, ("Wifi non-idle + Bt disabled/idle!!\n"));
		btdm8723.bLowPenaltyRateAdaptive = _TRUE;
		btdm8723.bRfRxLpfShrink = _FALSE;
		btdm8723.bRejectAggrePkt = _FALSE;

		// sw mechanism
		btdm8723.bAgcTableEn = _FALSE;
		btdm8723.bAdcBackOffOn = _FALSE;
		btdm8723.bSwDacSwingOn = _FALSE;

		btdm8723.bPtaOn = _TRUE;
		btdm8723.val0x6c0 = 0x5a5aaaaa;
		btdm8723.val0x6c8 = 0xcccc;
		btdm8723.val0x6cc = 0x3;

		btdm8723.bTdmaOn = _FALSE;
		btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
		btdm8723.b2AntHidEn = _FALSE;

		bCommon = _TRUE;
	}
	else if (BTDM_IsBTBusy(padapter))
	{
		RTPRINT(FBT, BT_TRACE, ("Bt non-idle!!\n"));
		if (BTDM_IsWifiConnectionExist(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi connection exists!!\n"));
			bCommon = _FALSE;
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("No wifi connection!!\n"));

			btdm8723.bRfRxLpfShrink = _TRUE;
			btdm8723.bLowPenaltyRateAdaptive = _FALSE;
			btdm8723.bRejectAggrePkt = _FALSE;

			// sw mechanism
			btdm8723.bAgcTableEn = _FALSE;
			btdm8723.bAdcBackOffOn = _FALSE;
			btdm8723.bSwDacSwingOn = _FALSE;

			btdm8723.bPtaOn = _TRUE;
			btdm8723.val0x6c0 = 0xaaaaaaaa;
			btdm8723.val0x6c8 = 0xff000000;
			btdm8723.val0x6cc = 0x3;

			btdm8723.bTdmaOn = _FALSE;
			btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
			btdm8723.b2AntHidEn = _FALSE;

			bCommon = _TRUE;
		}
	}

	if (btdm_NeedToDecBtPwr(padapter))
	{
		btdm8723.bDecBtPwr = _TRUE;
	}
	if (pHalData->bt_coexist.halCoex8723.btInqPageStartTime)
	{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT btInqPageStartTime = 0x%"i64fmt"x,\n", 
			pHalData->bt_coexist.halCoex8723.btInqPageStartTime));
			btdm8723.bIgnoreWlanAct = _TRUE;

	}

	if (bCommon && BTDM_IsCoexistStateChanged(padapter))
	{
		btdm_SetBtdm(padapter, &btdm8723);
	}
	return bCommon;
}

void
btdm_SetSwFullTimeDacSwing(
	PADAPTER	padapter,
	u8		bSwDacSwingOn,
	u32		swDacSwingLvl
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (bSwDacSwingOn)
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], SwDacSwing = 0x%x\n", swDacSwingLvl));
		PHY_SetBBReg(padapter, 0x880, 0xff000000, swDacSwingLvl);
		pHalData->bt_coexist.bSWCoexistAllOff = _FALSE;
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], SwDacSwing Off!\n"));
		PHY_SetBBReg(padapter, 0x880, 0xff000000, 0xc0);
	}
}

void
btdm_SetFw2AntHID(
	PADAPTER	padapter,
	u8		bEnable,
	u8		bDACSwingOn
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			H2C_Parameter[1] = {0};

	H2C_Parameter[0] = 0;

	if (bEnable)
	{
		H2C_Parameter[0] |= BIT(0);
		pHalData->bt_coexist.bFWCoexistAllOff = _FALSE;
	}
	if (bDACSwingOn)
	{
		H2C_Parameter[0] |= BIT(1);// Dac Swing default enable
	}
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], turn 2-Ant+HID mode %s, DACSwing:%s, write 0x15=0x%x\n",
		(bEnable ? "ON!!":"OFF!!"), (bDACSwingOn ? "ON":"OFF"), H2C_Parameter[0]));

	FillH2CCmd(padapter, BT_2ANT_HID_EID, 1, H2C_Parameter);
}

void
btdm_SetFwTdmaCtrl(
	PADAPTER	padapter,
	u8		bEnable,
	u8		antNum,
	u8		navEn,
	u8		dacSwingEn)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			H2C_Parameter[1] = {0};
	u8			H2C_Parameter1[1] = {0};

	H2C_Parameter[0] = 0;
	H2C_Parameter1[0] = 0;

	if (bEnable)
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], set BT PTA update manager to trigger update!!\n"));
		H2C_Parameter1[0] |= BIT(0);

		RTPRINT(FBT, BT_TRACE, ("[BTCoex], turn TDMA mode ON!!\n"));
		H2C_Parameter[0] |= BIT(0);		// function enable
		if (TDMA_1ANT == antNum)
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], TDMA_1ANT\n"));
			H2C_Parameter[0] |= BIT(1);
		}
		else if (TDMA_2ANT == antNum)
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], TDMA_2ANT\n"));
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], Unknown Ant\n"));
		}

		if (TDMA_NAV_OFF == navEn)
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], TDMA_NAV_OFF\n"));
		}
		else if (TDMA_NAV_ON == navEn)
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], TDMA_NAV_ON\n"));
			H2C_Parameter[0] |= BIT(2);
		}

		if (TDMA_DAC_SWING_OFF == dacSwingEn)
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], TDMA_DAC_SWING_OFF\n"));
		}
		else if (TDMA_DAC_SWING_ON == dacSwingEn)
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], TDMA_DAC_SWING_ON\n"));
			H2C_Parameter[0] |= BIT(4);
		}
		pHalData->bt_coexist.bFWCoexistAllOff = _FALSE;
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], set BT PTA update manager to no update!!\n"));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], turn TDMA mode OFF!!\n"));
	}

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], FW2AntTDMA, write 0x26=0x%x\n",
		H2C_Parameter1[0]));
	FillH2CCmd(padapter, BT_PTA_MANAGER_UPDATE_ENABLE_EID, 1, H2C_Parameter1);

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], FW2AntTDMA, write 0x14=0x%x\n",
		H2C_Parameter[0]));
	FillH2CCmd(padapter, BT_ANT_TDMA_EID, 1, H2C_Parameter);

	if (!bEnable)
	{
		//rtw_mdelay_os(2);
		//rtw_write8(padapter, 0x778, 0x1);
	}
}

void
btdm_SetFwTraTdmaCtrl(
	PADAPTER	padapter,
	u8		bEnable,
	u8		antNum,
	u8		navEn
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			H2C_Parameter[2] = {0};

	// Only 8723 B cut should do this
	if (IS_8723A_A_CUT(pHalData->VersionID))
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], not 8723B cut, don't set Traditional TDMA!!\n"));
		return;
	}

	if (bEnable)
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], turn TTDMA mode ON!!\n"));
		H2C_Parameter[0] |= BIT(0);		// function enable
		if (TDMA_1ANT == antNum)
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], TTDMA_1ANT\n"));
			H2C_Parameter[0] |= BIT(1);
		}
		else if (TDMA_2ANT == antNum)
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], TTDMA_2ANT\n"));
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], Unknown Ant\n"));
		}

		if (TDMA_NAV_OFF == navEn)
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], TTDMA_NAV_OFF\n"));
		}
		else if (TDMA_NAV_ON == navEn)
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], TTDMA_NAV_ON\n"));
			H2C_Parameter[1] |= BIT(0);
		}

		pHalData->bt_coexist.bFWCoexistAllOff = _FALSE;
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], turn TTDMA mode OFF!!\n"));
	}

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], FW Traditional TDMA, write 0x33=0x%x\n",
		H2C_Parameter[0]<<8|H2C_Parameter[1]));

	FillH2CCmd(padapter, TRADITIONAL_TDMA_EN_EID, 2, H2C_Parameter);
}

void btdm_SetFwDacSwingLevel(PADAPTER padapter, u8 dacSwingLvl)
{
	u8			H2C_Parameter[1] = {0};

	H2C_Parameter[0] = dacSwingLvl;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], Set Dac Swing Level=0x%x\n", dacSwingLvl));
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], write 0x29=0x%x\n", H2C_Parameter[0]));

	FillH2CCmd(padapter, DAC_SWING_VALUE_EID, 1, H2C_Parameter);
}

void btdm_SetFwBtHidInfo(PADAPTER padapter, u8 bEnable)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			H2C_Parameter[1] = {0};

	H2C_Parameter[0] = 0;

	if (bEnable)
	{
		H2C_Parameter[0] |= BIT(0);
		pHalData->bt_coexist.bFWCoexistAllOff = _FALSE;
	}
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], Set BT HID information=0x%x\n", bEnable));
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], write 0x24=0x%x\n", H2C_Parameter[0]));

	FillH2CCmd(padapter, HID_PROFILE_ENABLE_EID, 1, H2C_Parameter);
}

void btdm_SetFwBtRetryIndex(PADAPTER padapter, u8 retryIndex)
{
	u8			H2C_Parameter[1] = {0};

	H2C_Parameter[0] = retryIndex;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], Set BT Retry Index=%d\n", retryIndex));
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], write 0x23=0x%x\n", H2C_Parameter[0]));

	FillH2CCmd(padapter, SET_BT_TX_RETRY_INDEX_EID, 1, H2C_Parameter);
}


void
btdm_SetFwWlanAct(
	PADAPTER	padapter,
	u8		wlanActHi,
	u8		wlanActLo
	)
{
	u8			H2C_ParameterHi[1] = {0};
	u8			H2C_ParameterLo[1] = {0};

	H2C_ParameterHi[0] = wlanActHi;
	H2C_ParameterLo[0] = wlanActLo;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], Set WLAN_ACT Hi:Lo=0x%x/0x%x\n", wlanActHi, wlanActLo));
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], write 0x22=0x%x\n", H2C_ParameterHi[0]));
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], write 0x11=0x%x\n", H2C_ParameterLo[0]));

	FillH2CCmd(padapter, SET_TDMA_WLAN_ACT_TIME_EID, 1, H2C_ParameterHi);	// WLAN_ACT = High duration, unit:ms
	FillH2CCmd(padapter, BT_QUEUE_PKT_EID, 1, H2C_ParameterLo);	// WLAN_ACT = Low duration, unit:3*625us
}

void
btdm_SetBtdm(
	PADAPTER			padapter,
	PBTDM_8723A_2ANT		pBtdm
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBTDM_8723A_2ANT	pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm2Ant;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	u8 i;
	//
	// check new setting is different with the old one,
	// if all the same, don't do the setting again.
	//
	if (_rtw_memcmp(pBtdm8723, pBtdm, sizeof(BTDM_8723A_2ANT)) == _TRUE)
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], the same coexist setting, return!!\n"));
		return;
	}
	else
	{	//save the new coexist setting
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], save new coexist setting and execute!!\n"));

		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new bAllOff=0x%x/ 0x%x \n", pBtdm8723->bAllOff, pBtdm->bAllOff));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new bAgcTableEn=0x%x/ 0x%x \n", pBtdm8723->bAgcTableEn, pBtdm->bAgcTableEn));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new bAdcBackOffOn=0x%x/ 0x%x \n", pBtdm8723->bAdcBackOffOn, pBtdm->bAdcBackOffOn));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new b2AntHidEn=0x%x/ 0x%x \n", pBtdm8723->b2AntHidEn, pBtdm->b2AntHidEn));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new bLowPenaltyRateAdaptive = 0x%x/ 0x%x \n", pBtdm8723->bLowPenaltyRateAdaptive, pBtdm->bLowPenaltyRateAdaptive));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new bRfRxLpfShrink=0x%x/ 0x%x \n", pBtdm8723->bRfRxLpfShrink, pBtdm->bRfRxLpfShrink));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new bRejectAggrePkt=0x%x/ 0x%x \n", pBtdm8723->bRejectAggrePkt, pBtdm->bRejectAggrePkt));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new bTdmaOn=0x%x/ 0x%x \n", pBtdm8723->bTdmaOn, pBtdm->bTdmaOn));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new tdmaAnt=0x%x/ 0x%x \n", pBtdm8723->tdmaAnt, pBtdm->tdmaAnt));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new tdmaNav=0x%x/ 0x%x \n", pBtdm8723->tdmaNav, pBtdm->tdmaNav));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new tdmaDacSwing=0x%x/ 0x%x \n", pBtdm8723->tdmaDacSwing, pBtdm->tdmaDacSwing));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new fwDacSwingLvl=0x%x/ 0x%x \n", pBtdm8723->fwDacSwingLvl, pBtdm->fwDacSwingLvl));

		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new bTraTdmaOn=0x%x/ 0x%x \n", pBtdm8723->bTraTdmaOn, pBtdm->bTraTdmaOn));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new traTdmaAnt=0x%x/ 0x%x \n", pBtdm8723->traTdmaAnt, pBtdm->traTdmaAnt));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new traTdmaNav=0x%x/ 0x%x \n", pBtdm8723->traTdmaNav, pBtdm->traTdmaNav));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new bPsTdmaOn=0x%x/ 0x%x \n", pBtdm8723->bPsTdmaOn, pBtdm->bPsTdmaOn));
		for(i=0; i<5; i++)
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new psTdmaByte[i]=0x%x/ 0x%x \n", pBtdm8723->psTdmaByte[i], pBtdm->psTdmaByte[i]));
		}
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new bIgnoreWlanAct=0x%x/ 0x%x \n", pBtdm8723->bIgnoreWlanAct, pBtdm->bIgnoreWlanAct));
		
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new bPtaOn=0x%x/ 0x%x \n", pBtdm8723->bPtaOn, pBtdm->bPtaOn));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new val0x6c0=0x%x/ 0x%x \n", pBtdm8723->val0x6c0, pBtdm->val0x6c0));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new val0x6c8=0x%x/ 0x%x \n", pBtdm8723->val0x6c8, pBtdm->val0x6c8));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new val0x6cc=0x%x/ 0x%x \n", pBtdm8723->val0x6cc, pBtdm->val0x6cc));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new bSwDacSwingOn=0x%x/ 0x%x \n", pBtdm8723->bSwDacSwingOn, pBtdm->bSwDacSwingOn));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new swDacSwingLvl=0x%x/ 0x%x \n", pBtdm8723->swDacSwingLvl, pBtdm->swDacSwingLvl));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new wlanActHi=0x%x/ 0x%x \n", pBtdm8723->wlanActHi, pBtdm->wlanActHi));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new wlanActLo=0x%x/ 0x%x \n", pBtdm8723->wlanActLo, pBtdm->wlanActLo));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new btRetryIndex=0x%x/ 0x%x \n", pBtdm8723->btRetryIndex, pBtdm->btRetryIndex));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new bDecBtPwr=0x%x/ 0x%x \n", pBtdm8723->bDecBtPwr, pBtdm->bDecBtPwr));
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], original/new bInqCnt=0x%x/ 0x%x \n", pBtdm8723->bInqCnt, pBtdm->bInqCnt));

		pBtdm->bInqCnt=pBtdm8723->bInqCnt;
		_rtw_memcpy(pBtdm8723, pBtdm, sizeof(BTDM_8723A_2ANT));
	}

	//
	// Here we only consider when Bt Operation
	// inquiry/paging/pairing is ON
	// we only need to turn off TDMA
	//
	if (pBtMgnt->ExtConfig.bHoldForBtOperation)
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], set to ignore wlanAct for BT OP!!\n"));
		btdm_SetFwIgnoreWlanAct(padapter, _TRUE);
		return;
	}

	if (pBtdm->bAllOff)
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], disable all coexist mechanism !!\n"));
		BTDM_CoexAllOff(padapter);
		return;
	}

	BTDM_RejectAPAggregatedPacket(padapter, pBtdm->bRejectAggrePkt);

	if (pBtdm->bLowPenaltyRateAdaptive)
		BTDM_SetSwPenaltyTxRateAdaptive(padapter, BT_TX_RATE_ADAPTIVE_LOW_PENALTY);
	else
		BTDM_SetSwPenaltyTxRateAdaptive(padapter, BT_TX_RATE_ADAPTIVE_NORMAL);

	if (pBtdm->bRfRxLpfShrink)
		BTDM_SetSwRfRxLpfCorner(padapter, BT_RF_RX_LPF_CORNER_SHRINK);
	else
		BTDM_SetSwRfRxLpfCorner(padapter, BT_RF_RX_LPF_CORNER_RESUME);

	if (pBtdm->bAgcTableEn)
		BTDM_AGCTable(padapter, BT_AGCTABLE_ON);
	else
		BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);

	if (pBtdm->bAdcBackOffOn)
		BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
	else
		BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_OFF);

	btdm_SetFwBtRetryIndex(padapter, pBtdm->btRetryIndex);

	btdm_SetFwDacSwingLevel(padapter, pBtdm->fwDacSwingLvl);
	btdm_SetFwWlanAct(padapter, pBtdm->wlanActHi, pBtdm->wlanActLo);

	btdm_SetCoexTable(padapter, pBtdm->val0x6c0, pBtdm->val0x6c8, pBtdm->val0x6cc);
	btdm_SetHwPtaMode(padapter, pBtdm->bPtaOn);

	// NOTE1: Only one of the following mechanism can be Turn ON!!!
	// 1)PsTDMA 2)old TDMA 3)2AntHid
	// NOTE2: When turn on, should turn off other mechanisms.
	//

#if 1
	if(pBtdm->b2AntHidEn)
	{
		// turn off tdma
		btdm_SetFwTraTdmaCtrl(padapter, pBtdm->bTraTdmaOn, pBtdm->traTdmaAnt, pBtdm->traTdmaNav);
		btdm_SetFwTdmaCtrl(padapter, _FALSE, pBtdm->tdmaAnt, pBtdm->tdmaNav, pBtdm->tdmaDacSwing);

		// turn off Pstdma
		btdm_SetFwIgnoreWlanAct(padapter, pBtdm->bIgnoreWlanAct);
		BTDM_SetFw3a(padapter, 0x0, 0x0, 0x0, 0x8, 0x0); 	// Antenna control by PTA, 0x870 = 0x300.

		// turn on 2AntHid
		btdm_SetFwBtHidInfo(padapter, _TRUE);
		btdm_SetFw2AntHID(padapter, _TRUE, _TRUE);
	}
	else if(pBtdm->bTdmaOn)
	{
		// turn off 2AntHid
		btdm_SetFwBtHidInfo(padapter, _FALSE);
		btdm_SetFw2AntHID(padapter, _FALSE, _FALSE);

		// turn off pstdma
		btdm_SetFwIgnoreWlanAct(padapter, pBtdm->bIgnoreWlanAct);
		BTDM_SetFw3a(padapter, 0x0, 0x0, 0x0, 0x8, 0x0); 	// Antenna control by PTA, 0x870 = 0x300.

		// turn on tdma
		btdm_SetFwTraTdmaCtrl(padapter, pBtdm->bTraTdmaOn, pBtdm->traTdmaAnt, pBtdm->traTdmaNav);
		btdm_SetFwTdmaCtrl(padapter, _TRUE, pBtdm->tdmaAnt, pBtdm->tdmaNav, pBtdm->tdmaDacSwing);
	}
	else if(pBtdm->bPsTdmaOn)
	{
		// turn off 2AntHid
		btdm_SetFwBtHidInfo(padapter, _FALSE);
		btdm_SetFw2AntHID(padapter, _FALSE, _FALSE);

		// turn off tdma
		btdm_SetFwTraTdmaCtrl(padapter, pBtdm->bTraTdmaOn, pBtdm->traTdmaAnt, pBtdm->traTdmaNav);
		btdm_SetFwTdmaCtrl(padapter, _FALSE, pBtdm->tdmaAnt, pBtdm->tdmaNav, pBtdm->tdmaDacSwing);

		// turn on pstdma
		btdm_SetFwIgnoreWlanAct(padapter, pBtdm->bIgnoreWlanAct);
		BTDM_SetFw3a(padapter, 
			pBtdm->psTdmaByte[0], 
			pBtdm->psTdmaByte[1], 
			pBtdm->psTdmaByte[2],
			pBtdm->psTdmaByte[3],
			pBtdm->psTdmaByte[4]);
	}
	else
	{
		// turn off 2AntHid
		btdm_SetFwBtHidInfo(padapter, _FALSE);
		btdm_SetFw2AntHID(padapter, _FALSE, _FALSE);

		// turn off tdma
		btdm_SetFwTraTdmaCtrl(padapter, pBtdm->bTraTdmaOn, pBtdm->traTdmaAnt, pBtdm->traTdmaNav);
		btdm_SetFwTdmaCtrl(padapter, _FALSE, pBtdm->tdmaAnt, pBtdm->tdmaNav, pBtdm->tdmaDacSwing);

		// turn off pstdma
		btdm_SetFwIgnoreWlanAct(padapter, pBtdm->bIgnoreWlanAct);
		BTDM_SetFw3a(padapter, 0x0, 0x0, 0x0, 0x8, 0x0); 	// Antenna control by PTA, 0x870 = 0x300.
	}
#else
	if (pBtdm->bTdmaOn)
	{
		if (pBtdm->bPsTdmaOn)
		{
			// we should not reach this case
			//DbgPrint("cosa error case, PsTDMA CANNOT ON when turn old TDMA ON!!!\n");
		}
		else
		{
			// NOTE: When turn on old TDMA, we should turn OFF PsTDMA first
			BTDM_SetFwIgnoreWlanAct(padapter, pBtdm->bIgnoreWlanAct);
			BTDM_SetFw3a(padapter, 0x0, 0x0, 0x0, 0x8, 0x0); 	// Antenna control by PTA, 0x870 = 0x300.
		}

		// Turn off 2AntHID first then turn tdma ON
		btdm_SetFwBtHidInfo(padapter, _FALSE);
		btdm_SetFw2AntHID(padapter, _FALSE, _FALSE);
		btdm_SetFwTraTdmaCtrl(padapter, pBtdm->bTraTdmaOn, pBtdm->traTdmaAnt, pBtdm->traTdmaNav);
		btdm_SetFwTdmaCtrl(padapter, _TRUE, pBtdm->tdmaAnt, pBtdm->tdmaNav, pBtdm->tdmaDacSwing);
	}
	else
	{
		// Turn off tdma first then turn 2AntHID ON if need
		btdm_SetFwTraTdmaCtrl(padapter, pBtdm->bTraTdmaOn, pBtdm->traTdmaAnt, pBtdm->traTdmaNav);
		btdm_SetFwTdmaCtrl(padapter, _FALSE, pBtdm->tdmaAnt, pBtdm->tdmaNav, pBtdm->tdmaDacSwing);
		if (pBtdm->b2AntHidEn)
		{
			btdm_SetFwBtHidInfo(padapter, _TRUE);
			btdm_SetFw2AntHID(padapter, _TRUE, _TRUE);
		}
		else
		{
			btdm_SetFwBtHidInfo(padapter, _FALSE);
			btdm_SetFw2AntHID(padapter, _FALSE, _FALSE);
		}

		// NOTE: When turn on PsTDMA, we should turn OFF old TDMA first
		if (pBtdm->bPsTdmaOn)
		{
			BTDM_SetFwIgnoreWlanAct(padapter, pBtdm->bIgnoreWlanAct);
			BTDM_SetFw3a(padapter,
				pBtdm->psTdmaByte[0],
				pBtdm->psTdmaByte[1],
				pBtdm->psTdmaByte[2],
				pBtdm->psTdmaByte[3],
				pBtdm->psTdmaByte[4]);
		}
		else
		{
			BTDM_SetFwIgnoreWlanAct(padapter, pBtdm->bIgnoreWlanAct);
			BTDM_SetFw3a(padapter, 0x0, 0x0, 0x0, 0x8, 0x0); 	// Antenna control by PTA, 0x870 = 0x300.
		}
	}
#endif
	//
	// Note:
	// We should add delay for making sure sw DacSwing can be set sucessfully.
	// because of that btdm_SetFw2AntHID() and btdm_SetFwTdmaCtrl()
	// will overwrite the reg 0x880.
	//
	rtw_mdelay_os(5);
	btdm_SetSwFullTimeDacSwing(padapter, pBtdm->bSwDacSwingOn, pBtdm->swDacSwingLvl);

	BTDM_SetFwDecBtPwr(padapter, pBtdm->bDecBtPwr);
}

void btdm_BtTrafficCheck(PADAPTER padapter)
{
	PBT30Info		pBTInfo;
	PBT_MGNT		pBtMgnt;
	PHAL_DATA_TYPE	pHalData;


	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;

	// Check if BT PAN (under BT 2.1) is uplink or downlink
	pHalData->bt_coexist.BT21TrafficStatistics.bTxBusyTraffic = _FALSE;
	pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic = _FALSE;

	if (0)
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], Now we always force BT as Uplink!!\n"));
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_UPLINK;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_DOWNLINK;
		pHalData->bt_coexist.BT21TrafficStatistics.bTxBusyTraffic = _TRUE;
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], Now we always force BT as Downlink!!\n"));
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_UPLINK;
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_DOWNLINK;
		pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic = _TRUE;
	}
}

void btdm_BtStateUpdate2Ant8723ASco(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT_MGNT		pBtMgnt = &pHalData->BtInfo.BtMgnt;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], SCO busy!!\n"));
	pBtMgnt->ExtConfig.bBTBusy = _TRUE;
	pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_IDLE;
}

void btdm_BtStateUpdate2Ant8723AHid(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT_MGNT		pBtMgnt = &pHalData->BtInfo.BtMgnt;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], HID busy!!\n"));
	pBtMgnt->ExtConfig.bBTBusy = _TRUE;
	pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_IDLE;
}

void btdm_BtStateUpdate2Ant8723AA2dp(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT_MGNT		pBtMgnt = &pHalData->BtInfo.BtMgnt;

	if (pHalData->bt_coexist.halCoex8723.lowPriorityTx > 10 ||
		pHalData->bt_coexist.halCoex8723.lowPriorityRx > 10)
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], A2DP busy!!\n"));
		pBtMgnt->ExtConfig.bBTBusy = _TRUE;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_IDLE;
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], A2DP idle!!\n"));
	}
}

void btdm_BtStateUpdate2Ant8723APan(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT_MGNT		pBtMgnt = &pHalData->BtInfo.BtMgnt;
	u8			bIdle = _FALSE;

	// note: pay attension that don'e divide by zero.
	if (pHalData->bt_coexist.halCoex8723.lowPriorityTx >=
		pHalData->bt_coexist.halCoex8723.lowPriorityRx)
	{
		if (pHalData->bt_coexist.halCoex8723.lowPriorityRx == 0)
		{
			if (pHalData->bt_coexist.halCoex8723.lowPriorityTx > 10)
				bIdle = _TRUE;
		}
		else
		{
			if ((pHalData->bt_coexist.halCoex8723.lowPriorityTx/
				pHalData->bt_coexist.halCoex8723.lowPriorityRx) > 10)
			{
				bIdle = _TRUE;
			}
		}
	}
	else
	{
		if (pHalData->bt_coexist.halCoex8723.lowPriorityTx == 0)
		{
			if (pHalData->bt_coexist.halCoex8723.lowPriorityRx > 10)
				bIdle = _TRUE;
		}
		else
		{
			if ((pHalData->bt_coexist.halCoex8723.lowPriorityRx/
				pHalData->bt_coexist.halCoex8723.lowPriorityTx) > 10)
			{
				bIdle = _TRUE;
			}
		}
	}

	if (!bIdle)
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], PAN busy!!\n"));
		pBtMgnt->ExtConfig.bBTBusy = _TRUE;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_IDLE;
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], PAN idle!!\n"));
	}
}

void btdm_BtStateUpdate2Ant8723AHidA2dp(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT_MGNT		pBtMgnt = &pHalData->BtInfo.BtMgnt;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], HID+A2DP busy!!\n"));
	pBtMgnt->ExtConfig.bBTBusy = _TRUE;
	pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_IDLE;
}

void btdm_BtStateUpdate2Ant8723AHidPan(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT_MGNT		pBtMgnt = &pHalData->BtInfo.BtMgnt;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], HID+PAN busy!!\n"));
	pBtMgnt->ExtConfig.bBTBusy = _TRUE;
	pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_IDLE;
}

void btdm_BtStateUpdate2Ant8723APanA2dp(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT_MGNT		pBtMgnt = &pHalData->BtInfo.BtMgnt;

	if (pHalData->bt_coexist.halCoex8723.lowPriorityTx > 10 ||
		pHalData->bt_coexist.halCoex8723.lowPriorityRx > 10)
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], PAN+A2DP busy!!\n"));
		pBtMgnt->ExtConfig.bBTBusy = _TRUE;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_IDLE;
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], PAN+A2DP idle!!\n"));
	}
}

u8 btdm_BtTxRxCounterLevel(PADAPTER	padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u32	btTxRxCnt=0;
	u8	btTxRxCntLvl=0;

	btTxRxCnt = BTDM_BtTxRxCounterH(padapter)+BTDM_BtTxRxCounterL(padapter);
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters = %d\n", btTxRxCnt));

	pHalData->bt_coexist.CurrentState &= ~\
		(BT_COEX_STATE_BT_CNT_LEVEL_0|BT_COEX_STATE_BT_CNT_LEVEL_1|
		BT_COEX_STATE_BT_CNT_LEVEL_2);

	if(btTxRxCnt >= BT_TXRX_CNT_THRES_3)
	{
		btTxRxCntLvl = BT_TXRX_CNT_LEVEL_3;
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_CNT_LEVEL_3;
	}
	else if(btTxRxCnt >= BT_TXRX_CNT_THRES_2)
	{
		btTxRxCntLvl = BT_TXRX_CNT_LEVEL_2;
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_CNT_LEVEL_2;
	}
	else if(btTxRxCnt >= BT_TXRX_CNT_THRES_1)
	{
		btTxRxCntLvl = BT_TXRX_CNT_LEVEL_1;
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_CNT_LEVEL_1;
	}
	else
	{
		btTxRxCntLvl = BT_TXRX_CNT_LEVEL_0;
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_CNT_LEVEL_0;
	}
	return btTxRxCntLvl;
}

void btdm_BtCommonStateUpdate2Ant8723A(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT_MGNT		pBtMgnt = &pHalData->BtInfo.BtMgnt;

	// Bt uplink/downlink
	btdm_BtTrafficCheck(padapter);

	// Default set bt as idle state, we will define busy in each case
	pBtMgnt->ExtConfig.bBTBusy = _FALSE;
	pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_IDLE;
}

void btdm_2Ant8723ASCOAction(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	BTDM_8723A_2ANT	btdm8723;
	u8			btRssiState;

	btdm_BtdmStrctureReload(padapter, &btdm8723);

	btdm8723.bRfRxLpfShrink = _TRUE;
	btdm8723.bLowPenaltyRateAdaptive = _TRUE;
	btdm8723.bRejectAggrePkt = _FALSE;

	if (BTDM_IsHT40(padapter))
	{
		RTPRINT(FBT, BT_TRACE, ("HT40\n"));
		// coex table
		btdm8723.val0x6c0 = 0x5a5aaaaa;
		btdm8723.val0x6c8 = 0xcc;
		btdm8723.val0x6cc = 0x3;
		btdm8723.bIgnoreWlanAct = _TRUE;
		// sw mechanism
		btdm8723.bAgcTableEn = _FALSE;
		btdm8723.bAdcBackOffOn = _TRUE;
		btdm8723.bSwDacSwingOn = _FALSE;
		// fw mechanism
		btdm8723.bPsTdmaOn = _FALSE;
		btdm8723.bTraTdmaOn = _FALSE;
		btdm8723.bTdmaOn = _FALSE;
		btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
		btdm8723.b2AntHidEn = _FALSE;
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_47, 0);

		// coex table
		btdm8723.val0x6c0 = 0x5a5aaaaa;
		btdm8723.val0x6c8 = 0xcc;
		btdm8723.val0x6cc = 0x3;
		btdm8723.bIgnoreWlanAct = _TRUE;
		// sw mechanism
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
			(btRssiState == BT_RSSI_STATE_STAY_HIGH))
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			btdm8723.bAgcTableEn = _TRUE;
			btdm8723.bAdcBackOffOn = _TRUE;
			btdm8723.bSwDacSwingOn = _FALSE;
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			btdm8723.bAgcTableEn = _FALSE;
			btdm8723.bAdcBackOffOn = _FALSE;
			btdm8723.bSwDacSwingOn = _FALSE;
		}
		// fw mechanism
		btdm8723.bPsTdmaOn = _FALSE;
		btdm8723.bTraTdmaOn = _FALSE;
		btdm8723.bTdmaOn = _FALSE;
		btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
		btdm8723.b2AntHidEn = _FALSE;
	}

	if (btdm_NeedToDecBtPwr(padapter))
	{
		btdm8723.bDecBtPwr = _TRUE;
	}

	if (BTDM_IsCoexistStateChanged(padapter))
	{
		btdm_SetBtdm(padapter, &btdm8723);
	}
}

void btdm_2Ant8723AHIDAction(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	BTDM_8723A_2ANT	btdm8723;
	u8	btRssiState;

	btdm_BtdmStrctureReload(padapter, &btdm8723);

	btdm8723.bRfRxLpfShrink = _TRUE;
	btdm8723.bLowPenaltyRateAdaptive = _TRUE;
	btdm8723.bRejectAggrePkt = _FALSE;

//	btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_50, 0);

	// coex table
	btdm8723.val0x6c0 = 0x55555555;
	btdm8723.val0x6c8 = 0xffff;
	btdm8723.val0x6cc = 0x3;
	btdm8723.bIgnoreWlanAct = _TRUE;

	if(BTDM_IsHT40(padapter))
	{
		RTPRINT(FBT, BT_TRACE, ("HT40\n"));		
		// sw mechanism
		btdm8723.bAgcTableEn = _FALSE;
		btdm8723.bAdcBackOffOn = _FALSE;
		btdm8723.bSwDacSwingOn = _FALSE;

		// fw mechanism
		btdm8723.bPsTdmaOn = _TRUE;
		btdm8723.psTdmaByte[0] = 0xa3;
		btdm8723.psTdmaByte[1] = 0xf;
		btdm8723.psTdmaByte[2] = 0xf;
		btdm8723.psTdmaByte[3] = 0x0;
		btdm8723.psTdmaByte[4] = 0x80;
		
		btdm8723.bTraTdmaOn = _FALSE;
		btdm8723.bTdmaOn = _FALSE;
		btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
		btdm8723.b2AntHidEn = _FALSE;
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 47, 0);

		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
			(btRssiState == BT_RSSI_STATE_STAY_HIGH))
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			// sw mechanism
			btdm8723.bAgcTableEn = _FALSE;
			btdm8723.bAdcBackOffOn = _FALSE;
			btdm8723.bSwDacSwingOn = _TRUE;
			btdm8723.swDacSwingLvl = 0x20;
				
			// fw mechanism
			btdm8723.bPsTdmaOn = _FALSE;
			btdm8723.bTdmaOn = _FALSE;
			btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
			btdm8723.b2AntHidEn = _FALSE;
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			// sw mechanism
			btdm8723.bAgcTableEn = _FALSE;
			btdm8723.bAdcBackOffOn = _FALSE;
			btdm8723.bSwDacSwingOn = _FALSE;

			// fw mechanism
			btdm8723.bPsTdmaOn = _FALSE;
			btdm8723.bTdmaOn = _FALSE;
			btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
			btdm8723.b2AntHidEn = _TRUE;
			btdm8723.fwDacSwingLvl = 0x20;
		}
	}

	if (btdm_NeedToDecBtPwr(padapter))
	{
		btdm8723.bDecBtPwr = _TRUE;
	}

	if (BTDM_IsCoexistStateChanged(padapter))
	{
		btdm_SetBtdm(padapter, &btdm8723);
	}
}

void btdm_2Ant8723AA2DPAction(PADAPTER padapter)
{
	BTDM_8723A_2ANT		btdm8723;
	u8	btRssiState, btSpec;

	btdm_BtdmStrctureReload(padapter, &btdm8723);

	btdm8723.bRfRxLpfShrink = _TRUE;
	btdm8723.bLowPenaltyRateAdaptive = _TRUE;
	btdm8723.bRejectAggrePkt = _FALSE;

	if (BTDM_IsHT40(padapter))
	{
		RTPRINT(FBT, BT_TRACE, ("HT40\n"));
		if (BTDM_IsWifiUplink(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi Uplink\n"));
			// coex table
			btdm8723.val0x6c0 = 0x5a5a5a5a;
			btdm8723.val0x6c8 = 0xcc;
			btdm8723.val0x6cc = 0x3;
			// sw mechanism
			btdm8723.bAgcTableEn = _FALSE;
			btdm8723.bAdcBackOffOn = _TRUE;
			btdm8723.bSwDacSwingOn = _FALSE;
			// fw mechanism
			btdm8723.bTdmaOn = _TRUE;
			btdm8723.tdmaDacSwing = TDMA_DAC_SWING_ON;
			btSpec = BTHCI_GetBTCoreSpecByProf(padapter, BT_PROFILE_A2DP);
			if (btSpec >= BT_SPEC_2_1_EDR)
			{
				btdm8723.wlanActHi = 0x20;
				btdm8723.wlanActLo = 0x18;
			}
			else
			{
				btdm8723.wlanActHi = 0x20;
				btdm8723.wlanActLo = 0x20;
			}
			btdm8723.btRetryIndex = 2;
			btdm8723.fwDacSwingLvl = 0x20;
		}
		else
		{
			RTPRINT (FBT, BT_TRACE, ("Wifi Downlink\n"));
			// coex table
			btdm8723.val0x6c0 = 0x5a5a5a5a;
			btdm8723.val0x6c8 = 0xcc;
			btdm8723.val0x6cc = 0x3;
			// sw mechanism
			btdm8723.bAgcTableEn = _FALSE;
			btdm8723.bAdcBackOffOn = _TRUE;
			btdm8723.bSwDacSwingOn = _FALSE;
			// fw mechanism
			btdm8723.bTdmaOn = _TRUE;
			btdm8723.tdmaDacSwing = TDMA_DAC_SWING_ON;
			btSpec = BTHCI_GetBTCoreSpecByProf(padapter, BT_PROFILE_A2DP);
			if (btSpec >= BT_SPEC_2_1_EDR)
			{
				btdm8723.wlanActHi = 0x20;
				btdm8723.wlanActLo = 0x18;
			}
			else
			{
				btdm8723.wlanActHi = 0x20;
				btdm8723.wlanActLo = 0x20;
			}
			btdm8723.btRetryIndex = 2;
			btdm8723.fwDacSwingLvl = 0xc0;
		}
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_47, 0);

		if (BTDM_IsWifiUplink(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi Uplink\n"));
			// coex table
			btdm8723.val0x6c0 = 0x5a5a5a5a;
			btdm8723.val0x6c8 = 0xcc;
			btdm8723.val0x6cc = 0x3;
			// sw mechanism
			if ((btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH))
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
				btdm8723.bAgcTableEn = _TRUE;
				btdm8723.bAdcBackOffOn = _TRUE;
				btdm8723.bSwDacSwingOn = _FALSE;
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
				btdm8723.bAgcTableEn = _FALSE;
				btdm8723.bAdcBackOffOn = _FALSE;
				btdm8723.bSwDacSwingOn = _FALSE;
			}
			// fw mechanism
			btdm8723.bTdmaOn = _TRUE;
			btdm8723.tdmaDacSwing = TDMA_DAC_SWING_ON;
			btSpec = BTHCI_GetBTCoreSpecByProf(padapter, BT_PROFILE_A2DP);
			if (btSpec >= BT_SPEC_2_1_EDR)
			{
				btdm8723.wlanActHi = 0x20;
				btdm8723.wlanActLo = 0x18;
			}
			else
			{
				btdm8723.wlanActHi = 0x20;
				btdm8723.wlanActLo = 0x20;
			}
			btdm8723.btRetryIndex = 2;
			btdm8723.fwDacSwingLvl = 0x20;
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi Downlink\n"));
			// coex table
			btdm8723.val0x6c0 = 0x5a5a5a5a;
			btdm8723.val0x6c8 = 0xcc;
			btdm8723.val0x6cc = 0x3;
			// sw mechanism
			if ((btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH))
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
				btdm8723.bAgcTableEn = _TRUE;
				btdm8723.bAdcBackOffOn = _TRUE;
				btdm8723.bSwDacSwingOn = _FALSE;
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
				btdm8723.bAgcTableEn = _FALSE;
				btdm8723.bAdcBackOffOn = _FALSE;
				btdm8723.bSwDacSwingOn = _FALSE;
			}
			// fw mechanism
			btdm8723.bTdmaOn = _TRUE;
			btdm8723.tdmaDacSwing = TDMA_DAC_SWING_ON;
			btSpec = BTHCI_GetBTCoreSpecByProf(padapter, BT_PROFILE_A2DP);
			if (btSpec >= BT_SPEC_2_1_EDR)
			{
				btdm8723.wlanActHi = 0x20;
				btdm8723.wlanActLo = 0x18;
			}
			else
			{
				btdm8723.wlanActHi = 0x20;
				btdm8723.wlanActLo = 0x20;
			}
			btdm8723.btRetryIndex = 2;
			btdm8723.fwDacSwingLvl = 0xc0;
		}
	}

	if (btdm_NeedToDecBtPwr(padapter))
	{
		btdm8723.bDecBtPwr = _TRUE;
	}

	if (BTDM_IsCoexistStateChanged(padapter))
	{
		btdm_SetBtdm(padapter, &btdm8723);
	}
}

void btdm_2Ant8723AAclOnlyBusy(PADAPTER padapter)
{
	BTDM_8723A_2ANT		btdm8723;
	u8	btRssiState, btRssiState1, btSpec;


	btdm_BtdmStrctureReload(padapter, &btdm8723);

	btdm8723.bRfRxLpfShrink = _TRUE;
	btdm8723.bLowPenaltyRateAdaptive = _TRUE;
	btdm8723.bRejectAggrePkt = _FALSE;

	if (BTDM_IsHT40(padapter))
	{
		RTPRINT(FBT, BT_TRACE, ("HT40\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 37, 0);

		// coex table
		btdm8723.val0x6c0 = 0x55555555;
		btdm8723.val0x6c8 = 0xffff;
		btdm8723.val0x6cc = 0x3;
		btdm8723.bIgnoreWlanAct = _FALSE;
		// sw mechanism
		btdm8723.bAgcTableEn = _FALSE;
		btdm8723.bAdcBackOffOn = _TRUE;
		btdm8723.bSwDacSwingOn = _FALSE;
		// fw mechanism
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH))
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			// only rssi high we need to do this,
			// when rssi low, the value will modified by fw
			rtw_write8(padapter, 0x883, 0x40);

			btdm8723.bPsTdmaOn = _TRUE;
			btdm8723.psTdmaByte[0] = 0xa3;
			btdm8723.psTdmaByte[1] = 0xf;
			btdm8723.psTdmaByte[2] = 0xf;
			btdm8723.psTdmaByte[3] = 0x81;
			btdm8723.psTdmaByte[4] = 0x80;

			btdm8723.bTraTdmaOn = _FALSE;
			btdm8723.bTdmaOn = _FALSE;
			btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
			btdm8723.b2AntHidEn = _FALSE;
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			btdm8723.bPsTdmaOn = _TRUE;
			btdm8723.psTdmaByte[0] = 0xa3;
			btdm8723.psTdmaByte[1] = 0xf;
			btdm8723.psTdmaByte[2] = 0xf;
			btdm8723.psTdmaByte[3] = 0x0;
			btdm8723.psTdmaByte[4] = 0x80;
	
			btdm8723.bTraTdmaOn = _FALSE;
			btdm8723.bTdmaOn = _FALSE;
			btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
			btdm8723.b2AntHidEn = _FALSE;
		}
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 47, 0);
		btRssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, 27, 0);

		// coex table
		btdm8723.val0x6c0 = 0x55555555;
		btdm8723.val0x6c8 = 0xffff;
		btdm8723.val0x6cc = 0x3;
		btdm8723.bIgnoreWlanAct = _FALSE;
		// sw mechanism
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
			(btRssiState == BT_RSSI_STATE_STAY_HIGH))
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 high \n"));
			btdm8723.bAgcTableEn = _TRUE;
			btdm8723.bAdcBackOffOn = _TRUE;
			btdm8723.bSwDacSwingOn = _FALSE;
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 low \n"));
			btdm8723.bAgcTableEn = _FALSE;
			btdm8723.bAdcBackOffOn = _FALSE;
			btdm8723.bSwDacSwingOn = _FALSE;
		}
		// fw mechanism
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
			(btRssiState == BT_RSSI_STATE_STAY_HIGH))
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			// only rssi high we need to do this,
			// when rssi low, the value will modified by fw
			rtw_write8(padapter, 0x883, 0x40);

			btdm8723.bPsTdmaOn = _TRUE;
			btdm8723.psTdmaByte[0] = 0xa3;
			btdm8723.psTdmaByte[1] = 0xf;
			btdm8723.psTdmaByte[2] = 0xf;
			btdm8723.psTdmaByte[3] = 0x81;
			btdm8723.psTdmaByte[4] = 0x80;

			btdm8723.bTraTdmaOn = _FALSE;
			btdm8723.bTdmaOn = _FALSE;
			btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
			btdm8723.b2AntHidEn = _FALSE;
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			btdm8723.bPsTdmaOn = _TRUE;
			btdm8723.psTdmaByte[0] = 0xa3;
			btdm8723.psTdmaByte[1] = 0xf;
			btdm8723.psTdmaByte[2] = 0xf;
			btdm8723.psTdmaByte[3] = 0x0;
			btdm8723.psTdmaByte[4] = 0x80;

			btdm8723.bTraTdmaOn = _FALSE;
			btdm8723.bTdmaOn = _FALSE;
			btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
			btdm8723.b2AntHidEn = _FALSE;
		}
	}

	if (btdm_NeedToDecBtPwr(padapter))
	{
		btdm8723.bDecBtPwr = _TRUE;
	}

	if (BTDM_IsCoexistStateChanged(padapter))
	{
		btdm_SetBtdm(padapter, &btdm8723);
	}
}

void btdm_2Ant8723AA2dpSinkActionNoProfile(PADAPTER padapter)
{
	BTDM_8723A_2ANT		btdm8723;
	u8	btRssiState, btSpec;

	btdm_BtdmStrctureReload(padapter, &btdm8723);

	btdm8723.bRfRxLpfShrink = _TRUE;
	btdm8723.bLowPenaltyRateAdaptive = _TRUE;
	btdm8723.bRejectAggrePkt = _FALSE;

	if (BTDM_IsHT40(padapter))
	{
		RTPRINT(FBT, BT_TRACE, ("HT40\n"));
		if (BTDM_IsWifiUplink(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi Uplink\n"));
			// coex table
			btdm8723.val0x6c0 = 0x5a5a5a5a;
			btdm8723.val0x6c8 = 0xcccc;
			btdm8723.val0x6cc = 0x3;
			// sw mechanism
			btdm8723.bAgcTableEn = _FALSE;
			btdm8723.bAdcBackOffOn = _TRUE;
			btdm8723.bSwDacSwingOn = _FALSE;
			// fw mechanism
			btdm8723.bTraTdmaOn = _FALSE;
			btdm8723.bTdmaOn = _FALSE;
			btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
			btdm8723.b2AntHidEn = _FALSE;

			btdm8723.bPsTdmaOn = _TRUE;
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi Downlink\n"));
			// coex table
			btdm8723.val0x6c0 = 0x5a5a5a5a;
			btdm8723.val0x6c8 = 0xcc;
			btdm8723.val0x6cc = 0x3;
			// sw mechanism
			btdm8723.bAgcTableEn = _FALSE;
			btdm8723.bAdcBackOffOn = _TRUE;
			btdm8723.bSwDacSwingOn = _FALSE;
			// fw mechanism
			btdm8723.bTraTdmaOn = _FALSE;
			btdm8723.bTdmaOn = _FALSE;
			btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
			btdm8723.b2AntHidEn = _FALSE;

			btdm8723.bPsTdmaOn = _TRUE;
		}
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_47, 0);

		if (BTDM_IsWifiUplink(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi Uplink\n"));
			// coex table
			btdm8723.val0x6c0 = 0x5a5a5a5a;
			btdm8723.val0x6c8 = 0xcccc;
			btdm8723.val0x6cc = 0x3;
			// sw mechanism
			if ((btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH))
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
				btdm8723.bAgcTableEn = _TRUE;
				btdm8723.bAdcBackOffOn = _TRUE;
				btdm8723.bSwDacSwingOn = _FALSE;
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
				btdm8723.bAgcTableEn = _FALSE;
				btdm8723.bAdcBackOffOn = _FALSE;
				btdm8723.bSwDacSwingOn = _FALSE;
			}
			// fw mechanism
			btdm8723.bTraTdmaOn = _FALSE;
			btdm8723.bTdmaOn = _FALSE;
			btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
			btdm8723.b2AntHidEn = _FALSE;

			btdm8723.bPsTdmaOn = _TRUE;
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi Downlink\n"));
			// coex table
			btdm8723.val0x6c0 = 0x5a5a5a5a;
			btdm8723.val0x6c8 = 0xcc;
			btdm8723.val0x6cc = 0x3;
			// sw mechanism
			if ((btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH))
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
				btdm8723.bAgcTableEn = _TRUE;
				btdm8723.bAdcBackOffOn = _TRUE;
				btdm8723.bSwDacSwingOn = _FALSE;
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
				btdm8723.bAgcTableEn = _FALSE;
				btdm8723.bAdcBackOffOn = _FALSE;
				btdm8723.bSwDacSwingOn = _FALSE;
			}
			// fw mechanism
			btdm8723.bTraTdmaOn = _FALSE;
			btdm8723.bTdmaOn = _FALSE;
			btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
			btdm8723.b2AntHidEn = _FALSE;

			btdm8723.bPsTdmaOn = _TRUE;
		}
	}

	if (btdm_NeedToDecBtPwr(padapter))
	{
		btdm8723.bDecBtPwr = _TRUE;
	}

	if (BTDM_IsCoexistStateChanged(padapter))
	{
		btdm_SetBtdm(padapter, &btdm8723);
	}
}

void btdm_2Ant8723APANAction(PADAPTER padapter)
{
	BTDM_8723A_2ANT		btdm8723;
	u8			btRssiState, btRssiState1, btSpec;

	btdm_BtdmStrctureReload(padapter, &btdm8723);

	btdm8723.bRfRxLpfShrink = _TRUE;
	btdm8723.bLowPenaltyRateAdaptive = _TRUE;
	btdm8723.bRejectAggrePkt = _FALSE;

	if(BTDM_IsBTHSMode(padapter))
	{
		RTPRINT(FBT, BT_TRACE, ("[BT 3.0]\n"));
		btdm_BtdmStrctureReloadAllOff(padapter, &btdm8723);
	}
	else
	{
		if(BTDM_IsHT40(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("HT40\n"));
			btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 37, 0);

			// coex table
			btdm8723.val0x6c0 = 0x55555555;
			btdm8723.val0x6c8 = 0xffff;
			btdm8723.val0x6cc = 0x3;
			btdm8723.bIgnoreWlanAct = _FALSE;
			// sw mechanism
			btdm8723.bAgcTableEn = _FALSE;
			btdm8723.bAdcBackOffOn = _TRUE;
			btdm8723.bSwDacSwingOn = _FALSE;
			// fw mechanism
			if( (btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH) )
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
				// only rssi high we need to do this, 
				// when rssi low, the value will modified by fw
				rtw_write8(padapter, 0x883, 0x40);

				btdm8723.bPsTdmaOn = _TRUE;
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0xf;
				btdm8723.psTdmaByte[2] = 0xf;
				btdm8723.psTdmaByte[3] = 0x81;
				btdm8723.psTdmaByte[4] = 0x80;
				
				btdm8723.bTraTdmaOn = _FALSE;
				btdm8723.bTdmaOn = _FALSE;
				btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
				btdm8723.b2AntHidEn = _FALSE;
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
				btdm8723.bPsTdmaOn = _TRUE;
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0xf;
				btdm8723.psTdmaByte[2] = 0xf;
				btdm8723.psTdmaByte[3] = 0x0;
				btdm8723.psTdmaByte[4] = 0x80;
				
				btdm8723.bTraTdmaOn = _FALSE;
				btdm8723.bTdmaOn = _FALSE;
				btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
				btdm8723.b2AntHidEn = _FALSE;
			}
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
			btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 47, 0);
			btRssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, 27, 0);
			
			// coex table
			btdm8723.val0x6c0 = 0x55555555;
			btdm8723.val0x6c8 = 0xffff;
			btdm8723.val0x6cc = 0x3;
			btdm8723.bIgnoreWlanAct = _FALSE;
			// sw mechanism
			if( (btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH) )
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 high \n"));
				btdm8723.bAgcTableEn = _TRUE;
				btdm8723.bAdcBackOffOn = _TRUE;
				btdm8723.bSwDacSwingOn = _FALSE;
			}
			else
{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 low \n"));
				btdm8723.bAgcTableEn = _FALSE;
				btdm8723.bAdcBackOffOn = _FALSE;
				btdm8723.bSwDacSwingOn = _FALSE;
			}
			// fw mechanism
			if( (btRssiState1 == BT_RSSI_STATE_HIGH) ||
				(btRssiState1 == BT_RSSI_STATE_STAY_HIGH) )
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
				// only rssi high we need to do this, 
				// when rssi low, the value will modified by fw
				rtw_write8(padapter, 0x883, 0x40);

				btdm8723.bPsTdmaOn = _TRUE;
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0xf;
				btdm8723.psTdmaByte[2] = 0xf;
				btdm8723.psTdmaByte[3] = 0x81;
				btdm8723.psTdmaByte[4] = 0x80;
				
				btdm8723.bTraTdmaOn = _FALSE;
				btdm8723.bTdmaOn = _FALSE;
				btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
				btdm8723.b2AntHidEn = _FALSE;
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
				btdm8723.bPsTdmaOn = _TRUE;
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0xf;
				btdm8723.psTdmaByte[2] = 0xf;
				btdm8723.psTdmaByte[3] = 0x0;
				btdm8723.psTdmaByte[4] = 0x80;

				btdm8723.bTraTdmaOn = _FALSE;
				btdm8723.bTdmaOn = _FALSE;
				btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
				btdm8723.b2AntHidEn = _FALSE;
			}
		}
	}

	if(btdm_NeedToDecBtPwr(padapter))
	{
		btdm8723.bDecBtPwr = _TRUE;
	}

	if(BTDM_IsCoexistStateChanged(padapter))
	{
		btdm_SetBtdm(padapter, &btdm8723);
	}
}

void btdm_2Ant8723AHIDA2DPAction(PADAPTER	padapter)
{
	BTDM_8723A_2ANT		btdm8723;
	u8			btRssiState;

	btdm_BtdmStrctureReload(padapter, &btdm8723);

	btdm8723.bRfRxLpfShrink = _TRUE;
	btdm8723.bLowPenaltyRateAdaptive = _TRUE;
	btdm8723.bRejectAggrePkt = _FALSE;

	if (BTDM_IsHT40(padapter))
	{
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_40, 0);
		RTPRINT(FBT, BT_TRACE, ("HT40\n"));
		// coex table
		btdm8723.val0x6c0 = 0x55555555;
		btdm8723.val0x6c8 = 0xffff;
		btdm8723.val0x6cc = 0x3;
		btdm8723.bIgnoreWlanAct = _TRUE;
		// sw mechanism
		btdm8723.bAgcTableEn = _FALSE;
		btdm8723.bAdcBackOffOn = _FALSE;
		btdm8723.bSwDacSwingOn = _FALSE;
		// fw mechanism
		btdm8723.bPsTdmaOn = _TRUE;
		btdm8723.psTdmaByte[0] = 0xa3;
		btdm8723.psTdmaByte[1] = 0xf;
		btdm8723.psTdmaByte[2] = 0xf;
		btdm8723.psTdmaByte[3] = 0x0;
		btdm8723.psTdmaByte[4] = 0x80;
		
		btdm8723.bTraTdmaOn = _FALSE;
		btdm8723.bTdmaOn = _FALSE;
		btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
		btdm8723.b2AntHidEn = _FALSE;
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_47, 0);
		// coex table
		btdm8723.val0x6c0 = 0x5a5a5a5a;
		btdm8723.val0x6c8 = 0xcccc;
		btdm8723.val0x6cc = 0x3;
		btdm8723.bIgnoreWlanAct = _TRUE;
		// sw mechanism
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
			(btRssiState == BT_RSSI_STATE_STAY_HIGH))
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high\n"));
			btdm8723.bAgcTableEn = _TRUE;
			btdm8723.bAdcBackOffOn = _TRUE;
			btdm8723.bSwDacSwingOn = _TRUE;
			btdm8723.swDacSwingLvl = 0x20;
			// fw mechanism
			btdm8723.bPsTdmaOn = _FALSE;
			btdm8723.bTraTdmaOn = _FALSE;
			btdm8723.bTdmaOn = _FALSE;
			btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
			btdm8723.b2AntHidEn = _FALSE;
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low\n"));
			btdm8723.bAgcTableEn = _FALSE;
			btdm8723.bAdcBackOffOn = _FALSE;
			btdm8723.bSwDacSwingOn = _FALSE;
			// fw mechanism
			btdm8723.bTdmaOn = _FALSE;
			btdm8723.bPsTdmaOn = _FALSE;
			btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
			btdm8723.b2AntHidEn = _TRUE;
			btdm8723.fwDacSwingLvl = 0x20;
		}
	}

	if (btdm_NeedToDecBtPwr(padapter))
	{
		btdm8723.bDecBtPwr = _TRUE;
	}

	if (BTDM_IsCoexistStateChanged(padapter))
	{
		btdm_SetBtdm(padapter, &btdm8723);
	}
}

void btdm_2Ant8723AHIDPANAction(	PADAPTER	padapter)
{
	BTDM_8723A_2ANT		btdm8723;
	u8			btSpec, btRssiState;

	btdm_BtdmStrctureReload(padapter, &btdm8723);

	btdm8723.bRfRxLpfShrink = _TRUE;
	btdm8723.bLowPenaltyRateAdaptive = _TRUE;
	btdm8723.bRejectAggrePkt = _FALSE;

	if (BTDM_IsBTHSMode(padapter))
	{
		RTPRINT(FBT, BT_TRACE, ("[BT 3.0]\n"));
		if(BTDM_IsHT40(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("HT40\n"));
			// coex table
			btdm8723.val0x6c0 = 0x55555555;
			btdm8723.val0x6c8 = 0xffff;
			btdm8723.val0x6cc = 0x3;
			btdm8723.bIgnoreWlanAct = _TRUE;
			// sw mechanism
			btdm8723.bAgcTableEn = _FALSE;
			btdm8723.bAdcBackOffOn = _FALSE;
			btdm8723.bSwDacSwingOn = _FALSE;
			// fw mechanism
			btdm8723.bPsTdmaOn = _TRUE;
			btdm8723.psTdmaByte[0] = 0xa3;
			btdm8723.psTdmaByte[1] = 0xf;
			btdm8723.psTdmaByte[2] = 0xf;
			btdm8723.psTdmaByte[3] = 0x0;
			btdm8723.psTdmaByte[4] = 0x80;
		
			btdm8723.bTraTdmaOn = _FALSE;
			btdm8723.bTdmaOn = _FALSE;
			btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
			btdm8723.b2AntHidEn = _FALSE;
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
			// coex table
			btdm8723.val0x6c0 = 0x55555555;
			btdm8723.val0x6c8 = 0xffff;
			btdm8723.val0x6cc = 0x3;
			btdm8723.bIgnoreWlanAct = _TRUE;

			btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 47, 0);
			
			if( (btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH) )
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
				// sw mechanism
				btdm8723.bAgcTableEn = _TRUE;
				btdm8723.bAdcBackOffOn = _TRUE;
				btdm8723.bSwDacSwingOn = _TRUE;
				btdm8723.swDacSwingLvl = 0x20;
				// fw mechanism
				btdm8723.bPsTdmaOn = _FALSE;
				btdm8723.bTraTdmaOn = _FALSE;
				btdm8723.bTdmaOn = _FALSE;
				btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
				btdm8723.b2AntHidEn = _FALSE;
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
				// sw mechanism
				btdm8723.bAgcTableEn = _FALSE;
				btdm8723.bAdcBackOffOn = _FALSE;
				btdm8723.bSwDacSwingOn = _FALSE;
				// fw mechanism
				btdm8723.bTdmaOn = _FALSE;
				btdm8723.bPsTdmaOn = _FALSE;
				btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
				btdm8723.b2AntHidEn = _TRUE;
				btdm8723.fwDacSwingLvl = 0x20;
			}
		}
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BT 2.1]\n"));
		if (BTDM_IsHT40(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("HT40\n"));
			// coex table
			btdm8723.val0x6c0 = 0x55555555;
			btdm8723.val0x6c8 = 0xffff;
			btdm8723.val0x6cc = 0x3;
			btdm8723.bIgnoreWlanAct = _TRUE;
			// sw mechanism
			btdm8723.bAgcTableEn = _FALSE;
			btdm8723.bAdcBackOffOn = _FALSE;
			btdm8723.bSwDacSwingOn = _FALSE;
			// fw mechanism
			btdm8723.bPsTdmaOn = _TRUE;
			btdm8723.psTdmaByte[0] = 0xa3;
			btdm8723.psTdmaByte[1] = 0xa;
			btdm8723.psTdmaByte[2] = 0xa;
			btdm8723.psTdmaByte[3] = 0x0;
			btdm8723.psTdmaByte[4] = 0x80;
		
			btdm8723.bTraTdmaOn = _FALSE;
			btdm8723.bTdmaOn = _FALSE;
			btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
			btdm8723.b2AntHidEn = _FALSE;
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
			// coex table
			btdm8723.val0x6c0 = 0x5a5a5a5a;
			btdm8723.val0x6c8 = 0xcccc;
			btdm8723.val0x6cc = 0x3;
			btdm8723.bIgnoreWlanAct = _TRUE;

			btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 47, 0);

			if ((btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH))
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
				// sw mechanism
				btdm8723.bAgcTableEn = _FALSE;
				btdm8723.bAdcBackOffOn = _FALSE;
				btdm8723.bSwDacSwingOn = _TRUE;
				btdm8723.swDacSwingLvl = 0x20;
				// fw mechanism
				btdm8723.bPsTdmaOn = _FALSE;
				btdm8723.bTraTdmaOn = _FALSE;
				btdm8723.bTdmaOn = _FALSE;
				btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
				btdm8723.b2AntHidEn = _FALSE;
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
				// sw mechanism
				btdm8723.bAgcTableEn = _FALSE;
				btdm8723.bAdcBackOffOn = _FALSE;
				btdm8723.bSwDacSwingOn = _FALSE;
				// fw mechanism
				btdm8723.bTdmaOn = _FALSE;
				btdm8723.bPsTdmaOn = _FALSE;
				btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
				btdm8723.b2AntHidEn = _TRUE;
				btdm8723.fwDacSwingLvl = 0x20;
			}
		}
	}

	if (btdm_NeedToDecBtPwr(padapter))
	{
		btdm8723.bDecBtPwr = _TRUE;
	}

	if (BTDM_IsCoexistStateChanged(padapter))
	{
		btdm_SetBtdm(padapter, &btdm8723);
	}
}

void
btdm_2Ant8723APANA2DPAction(
	PADAPTER	padapter
	)
{
	BTDM_8723A_2ANT		btdm8723;
	u8			btSpec, btRssiState, btRssiState1;

	btdm_BtdmStrctureReload(padapter, &btdm8723);

	btdm8723.bRfRxLpfShrink = _TRUE;
	btdm8723.bLowPenaltyRateAdaptive = _TRUE;
	btdm8723.bRejectAggrePkt = _FALSE;

	if (BTDM_IsBTHSMode(padapter))
	{
		RTPRINT(FBT, BT_TRACE, ("[BT 3.0]\n"));
		if(BTDM_IsHT40(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("HT40\n"));
			btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 37, 0);
			// coex table
			btdm8723.val0x6c0 = 0x55555555;
			btdm8723.val0x6c8 = 0xffff;
			btdm8723.val0x6cc = 0x3;
			btdm8723.bIgnoreWlanAct = _FALSE;
			// sw mechanism
			btdm8723.bAgcTableEn = _FALSE;
			btdm8723.bAdcBackOffOn = _TRUE;
			btdm8723.bSwDacSwingOn = _FALSE;			
			if( (btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH) )
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
				rtw_write8(padapter, 0x883, 0x40);

				// fw mechanism
				btdm8723.bPsTdmaOn = _TRUE;
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0xf;
				btdm8723.psTdmaByte[2] = 0xf;
				btdm8723.psTdmaByte[3] = 0x81;
				btdm8723.psTdmaByte[4] = 0x80;
			
				btdm8723.bTraTdmaOn = _FALSE;
				btdm8723.bTdmaOn = _FALSE;
				btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
				btdm8723.b2AntHidEn = _FALSE;
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
				// fw mechanism
				btdm8723.bPsTdmaOn = _TRUE;
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0xf;
				btdm8723.psTdmaByte[2] = 0xf;
				btdm8723.psTdmaByte[3] = 0x0;
				btdm8723.psTdmaByte[4] = 0x80;
			
				btdm8723.bTraTdmaOn = _FALSE;
				btdm8723.bTdmaOn = _FALSE;
				btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
				btdm8723.b2AntHidEn = _FALSE;
			}
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
			// coex table
			btdm8723.val0x6c0 = 0x55555555;
			btdm8723.val0x6c8 = 0xffff;
			btdm8723.val0x6cc = 0x3;
			btdm8723.bIgnoreWlanAct = _FALSE;

			// sw mechanism
			btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 47, 0);
			if( (btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH) )
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
				btdm8723.bAgcTableEn = _TRUE;
				btdm8723.bAdcBackOffOn = _TRUE;
				btdm8723.bSwDacSwingOn = _FALSE;
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
				btdm8723.bAgcTableEn = _FALSE;
				btdm8723.bAdcBackOffOn = _FALSE;
				btdm8723.bSwDacSwingOn = _FALSE;
			}
			
			// fw mechanism
			btRssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, 27, 0);
			if( (btRssiState1 == BT_RSSI_STATE_HIGH) ||
				(btRssiState1 == BT_RSSI_STATE_STAY_HIGH) )
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
				btdm8723.bPsTdmaOn = _TRUE;
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0xf;
				btdm8723.psTdmaByte[2] = 0xf;
				btdm8723.psTdmaByte[3] = 0x81;
				btdm8723.psTdmaByte[4] = 0x80;
				
				btdm8723.bTraTdmaOn = _FALSE;
				btdm8723.bTdmaOn = _FALSE;
				btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
				btdm8723.b2AntHidEn = _FALSE;
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
				btdm8723.bPsTdmaOn = _TRUE;
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0xf;
				btdm8723.psTdmaByte[2] = 0xf;
				btdm8723.psTdmaByte[3] = 0x0;
				btdm8723.psTdmaByte[4] = 0x80;
				
				btdm8723.bTraTdmaOn = _FALSE;
				btdm8723.bTdmaOn = _FALSE;
				btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
				btdm8723.b2AntHidEn = _FALSE;
			}
		}
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BT 2.1]\n"));
		if (BTDM_IsHT40(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("HT40\n"));
			btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 27, 0);
			// coex table
			btdm8723.val0x6c0 = 0x55555555;
			btdm8723.val0x6c8 = 0xffff;
			btdm8723.val0x6cc = 0x3;
			btdm8723.bIgnoreWlanAct = _TRUE;
			
			if( (btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH) )
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
				// sw mechanism
				btdm8723.bAgcTableEn = _FALSE;
				btdm8723.bAdcBackOffOn = _TRUE;
				btdm8723.bSwDacSwingOn = _TRUE;
				btdm8723.swDacSwingLvl = 0x20;
				// fw mechanism
				btdm8723.bPsTdmaOn = _FALSE;
				btdm8723.bTraTdmaOn = _FALSE;
				btdm8723.bTdmaOn = _FALSE;
				btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
				btdm8723.b2AntHidEn = _FALSE;
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
				// sw mechanism
				btdm8723.bAgcTableEn = _FALSE;
				btdm8723.bAdcBackOffOn = _TRUE;
				btdm8723.bSwDacSwingOn = _FALSE;	
				
				// fw mechanism
				btdm8723.bPsTdmaOn = _TRUE;
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0x5;
				btdm8723.psTdmaByte[2] = 0x5;
				btdm8723.psTdmaByte[3] = 0x0;
				btdm8723.psTdmaByte[4] = 0x80;
			
				btdm8723.bTraTdmaOn = _FALSE;
				btdm8723.bTdmaOn = _FALSE;
				btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
				btdm8723.b2AntHidEn = _FALSE;
			}
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
			// coex table
			btdm8723.val0x6c0 = 0x55555555;
			btdm8723.val0x6c8 = 0xffff;
			btdm8723.val0x6cc = 0x3;
			btdm8723.bIgnoreWlanAct = _TRUE;

			btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 47, 0);
			if( (btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH) )
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
				// sw mechanism
				btdm8723.bAgcTableEn = _TRUE;
				btdm8723.bAdcBackOffOn = _TRUE;
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
				// sw mechanism
				btdm8723.bAgcTableEn = _FALSE;
				btdm8723.bAdcBackOffOn = _FALSE;
			}

			btRssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, 27, 0);
			
			if( (btRssiState1 == BT_RSSI_STATE_HIGH) ||
				(btRssiState1 == BT_RSSI_STATE_STAY_HIGH) )
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
				// sw mechanism
				btdm8723.bSwDacSwingOn = _TRUE;
				btdm8723.swDacSwingLvl = 0x20;

				// fw mechanism
				btdm8723.bPsTdmaOn = _FALSE;
				btdm8723.bTraTdmaOn = _FALSE;
				btdm8723.bTdmaOn = _FALSE;
				btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
				btdm8723.b2AntHidEn = _FALSE;
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
				// sw mechanism
				btdm8723.bSwDacSwingOn = _FALSE;

				// fw mechanism
				btdm8723.bPsTdmaOn = _TRUE;
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0x5;
				btdm8723.psTdmaByte[2] = 0x5;
				btdm8723.psTdmaByte[3] = 0x0;
				btdm8723.psTdmaByte[4] = 0x80;
				
				btdm8723.bTraTdmaOn = _FALSE;
				btdm8723.bTdmaOn = _FALSE;
				btdm8723.tdmaDacSwing = TDMA_DAC_SWING_OFF;
				btdm8723.b2AntHidEn = _FALSE;
			}
		}
	}

	if (btdm_NeedToDecBtPwr(padapter))
	{
		btdm8723.bDecBtPwr = _TRUE;
	}

	if (BTDM_IsCoexistStateChanged(padapter))
	{
		btdm_SetBtdm(padapter, &btdm8723);
	}
}



void btdm_2Ant8723AHidScoEsco(PADAPTER	padapter	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	BTDM_8723A_2ANT		btdm8723;
	u8			btSpec, btRssiState, btRssiState1;
	u8			btTxRxCntLvl=0;

	btdm_BtdmStrctureReload(padapter, &btdm8723);

	btdm8723.bRfRxLpfShrink = _TRUE;
	btdm8723.bLowPenaltyRateAdaptive = _TRUE;
	btdm8723.bRejectAggrePkt = _FALSE;

	btTxRxCntLvl = btdm_BtTxRxCounterLevel(padapter);
	
	if(BTDM_IsHT40(padapter))
	{
		RTPRINT(FBT, BT_TRACE, ("HT40\n"));
		// coex table
		btdm8723.val0x6c0 = 0x55555555;
		btdm8723.val0x6c8 = 0xffff;
		btdm8723.val0x6cc = 0x3;

		// sw mechanism
		btdm8723.bAgcTableEn = _FALSE;
		btdm8723.bAdcBackOffOn = _FALSE;
		btdm8723.bSwDacSwingOn = _FALSE;

		// fw mechanism
		btdm8723.bPsTdmaOn = _TRUE;
		if(BT_TXRX_CNT_LEVEL_2 == btTxRxCntLvl)
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters >= 1400\n"));
			btdm8723.psTdmaByte[0] = 0xa3;
			btdm8723.psTdmaByte[1] = 0x5;
			btdm8723.psTdmaByte[2] = 0x5;
			btdm8723.psTdmaByte[3] = 0x2;
			btdm8723.psTdmaByte[4] = 0x80;
		}
		else if(BT_TXRX_CNT_LEVEL_1 == btTxRxCntLvl)
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters >= 1200 && < 1400\n"));
			btdm8723.psTdmaByte[0] = 0xa3;
			btdm8723.psTdmaByte[1] = 0xa;
			btdm8723.psTdmaByte[2] = 0xa;
			btdm8723.psTdmaByte[3] = 0x2;
			btdm8723.psTdmaByte[4] = 0x80;
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters < 1200\n"));
			btdm8723.psTdmaByte[0] = 0xa3;
			btdm8723.psTdmaByte[1] = 0xf;
			btdm8723.psTdmaByte[2] = 0xf;
			btdm8723.psTdmaByte[3] = 0x2;
			btdm8723.psTdmaByte[4] = 0x80;
		}
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 47, 0);
		btRssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, 27, 0);
		
		// coex table
		btdm8723.val0x6c0 = 0x55555555;
		btdm8723.val0x6c8 = 0xffff;
		btdm8723.val0x6cc = 0x3;

		// sw mechanism
		if( (btRssiState == BT_RSSI_STATE_HIGH) ||
			(btRssiState == BT_RSSI_STATE_STAY_HIGH) )
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			btdm8723.bAgcTableEn = _TRUE;
			btdm8723.bAdcBackOffOn = _TRUE;
			btdm8723.bSwDacSwingOn = _FALSE;
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			btdm8723.bAgcTableEn = _FALSE;
			btdm8723.bAdcBackOffOn = _FALSE;
			btdm8723.bSwDacSwingOn = _FALSE;
		}
		
		// fw mechanism
		btdm8723.bPsTdmaOn = _TRUE;
		if( (btRssiState1 == BT_RSSI_STATE_HIGH) ||
			(btRssiState1 == BT_RSSI_STATE_STAY_HIGH) )
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 high \n"));
			// only rssi high we need to do this, 
			// when rssi low, the value will modified by fw
			rtw_write8(padapter, 0x883, 0x40);
			if(BT_TXRX_CNT_LEVEL_2 == btTxRxCntLvl)
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters >= 1400\n"));
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0x5;
				btdm8723.psTdmaByte[2] = 0x5;
				btdm8723.psTdmaByte[3] = 0x83;
				btdm8723.psTdmaByte[4] = 0x80;
			}
			else if(BT_TXRX_CNT_LEVEL_1 == btTxRxCntLvl)
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters >= 1200 && < 1400\n"));
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0xa;
				btdm8723.psTdmaByte[2] = 0xa;
				btdm8723.psTdmaByte[3] = 0x83;
				btdm8723.psTdmaByte[4] = 0x80;
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters < 1200\n"));
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0xf;
				btdm8723.psTdmaByte[2] = 0xf;
				btdm8723.psTdmaByte[3] = 0x83;
				btdm8723.psTdmaByte[4] = 0x80;
			}
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 low \n"));
			if(BT_TXRX_CNT_LEVEL_2 == btTxRxCntLvl)
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters >= 1400\n"));
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0x5;
				btdm8723.psTdmaByte[2] = 0x5;
				btdm8723.psTdmaByte[3] = 0x2;
				btdm8723.psTdmaByte[4] = 0x80;
			}
			else if(BT_TXRX_CNT_LEVEL_1 == btTxRxCntLvl)
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters >= 1200 && < 1400\n"));
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0xa;
				btdm8723.psTdmaByte[2] = 0xa;
				btdm8723.psTdmaByte[3] = 0x2;
				btdm8723.psTdmaByte[4] = 0x80;
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters < 1200\n"));
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0xf;
				btdm8723.psTdmaByte[2] = 0xf;
				btdm8723.psTdmaByte[3] = 0x2;
				btdm8723.psTdmaByte[4] = 0x80;
			}
		}
	}

	if(btdm_NeedToDecBtPwr(padapter))
	{
		btdm8723.bDecBtPwr = _TRUE;
	}

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT btInqPageStartTime = 0x%"i64fmt"x, btTxRxCntLvl = %d\n", 
			pHalData->bt_coexist.halCoex8723.btInqPageStartTime, btTxRxCntLvl));
	if( (pHalData->bt_coexist.halCoex8723.btInqPageStartTime) ||
		(BT_TXRX_CNT_LEVEL_3 == btTxRxCntLvl) )
	{
		btdm8723.bPsTdmaOn = _TRUE;
		btdm8723.psTdmaByte[0] = 0xa3;
		btdm8723.psTdmaByte[1] = 0x5;
		btdm8723.psTdmaByte[2] = 0x5;
		btdm8723.psTdmaByte[3] = 0x2;
		btdm8723.psTdmaByte[4] = 0x80;
		btdm8723.bIgnoreWlanAct = _TRUE;

	}

	if(BTDM_IsCoexistStateChanged(padapter))
	{
		btdm_SetBtdm(padapter, &btdm8723);
	}
}
void btdm_2Ant8723AFtpA2dp(	PADAPTER	padapter	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	BTDM_8723A_2ANT		btdm8723;
	u1Byte			btSpec, btRssiState, btRssiState1;
	u1Byte			btTxRxCntLvl=0;

	btdm_BtdmStrctureReload(padapter, &btdm8723);

	btdm8723.bRfRxLpfShrink = _TRUE;
	btdm8723.bLowPenaltyRateAdaptive = _TRUE;
	btdm8723.bRejectAggrePkt = _FALSE;

	btTxRxCntLvl = btdm_BtTxRxCounterLevel(padapter);
	
	if(BTDM_IsHT40(padapter))
	{
		RTPRINT(FBT, BT_TRACE, ("HT40\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 37, 0);

		// coex table
		btdm8723.val0x6c0 = 0x55555555;
		btdm8723.val0x6c8 = 0xffff;
		btdm8723.val0x6cc = 0x3;

		// sw mechanism
		btdm8723.bAgcTableEn = _FALSE;
		btdm8723.bAdcBackOffOn = _TRUE;
		btdm8723.bSwDacSwingOn = _FALSE;

		// fw mechanism
		btdm8723.bPsTdmaOn = _TRUE;
		if( (btRssiState == BT_RSSI_STATE_HIGH) ||
			(btRssiState == BT_RSSI_STATE_STAY_HIGH) )
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			if(BT_TXRX_CNT_LEVEL_2 == btTxRxCntLvl)
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters >= 1400\n"));
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0x5;
				btdm8723.psTdmaByte[2] = 0x5;
				btdm8723.psTdmaByte[3] = 0x81;
				btdm8723.psTdmaByte[4] = 0x80;
			}
			else if(BT_TXRX_CNT_LEVEL_1 == btTxRxCntLvl)
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters >= 1200 && < 1400\n"));
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0xa;
				btdm8723.psTdmaByte[2] = 0xa;
				btdm8723.psTdmaByte[3] = 0x81;
				btdm8723.psTdmaByte[4] = 0x80;
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters < 1200\n"));
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0xf;
				btdm8723.psTdmaByte[2] = 0xf;
				btdm8723.psTdmaByte[3] = 0x81;
				btdm8723.psTdmaByte[4] = 0x80;
			}
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			if(BT_TXRX_CNT_LEVEL_2 == btTxRxCntLvl)
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters >= 1400\n"));
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0x5;
				btdm8723.psTdmaByte[2] = 0x5;
				btdm8723.psTdmaByte[3] = 0x0;
				btdm8723.psTdmaByte[4] = 0x80;
			}
			else if(BT_TXRX_CNT_LEVEL_1 == btTxRxCntLvl)
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters >= 1200 && < 1400\n"));
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0xa;
				btdm8723.psTdmaByte[2] = 0xa;
				btdm8723.psTdmaByte[3] = 0x0;
				btdm8723.psTdmaByte[4] = 0x80;
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters < 1200\n"));
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0xf;
				btdm8723.psTdmaByte[2] = 0xf;
				btdm8723.psTdmaByte[3] = 0x0;
				btdm8723.psTdmaByte[4] = 0x80;
			}
		}
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 47, 0);
		btRssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, 27, 0);
		
		// coex table
		btdm8723.val0x6c0 = 0x55555555;
		btdm8723.val0x6c8 = 0xffff;
		btdm8723.val0x6cc = 0x3;

		// sw mechanism
		if( (btRssiState == BT_RSSI_STATE_HIGH) ||
			(btRssiState == BT_RSSI_STATE_STAY_HIGH) )
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			btdm8723.bAgcTableEn = _TRUE;
			btdm8723.bAdcBackOffOn = _TRUE;
			btdm8723.bSwDacSwingOn = _FALSE;
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			btdm8723.bAgcTableEn = _FALSE;
			btdm8723.bAdcBackOffOn = _FALSE;
			btdm8723.bSwDacSwingOn = _FALSE;
		}
		
		// fw mechanism
		btdm8723.bPsTdmaOn = _TRUE;
		if( (btRssiState1 == BT_RSSI_STATE_HIGH) ||
			(btRssiState1 == BT_RSSI_STATE_STAY_HIGH) )
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 high \n"));
			// only rssi high we need to do this, 
			// when rssi low, the value will modified by fw
			rtw_write8(padapter, 0x883, 0x40);
			if(BT_TXRX_CNT_LEVEL_2 == btTxRxCntLvl)
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters >= 1400\n"));
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0x5;
				btdm8723.psTdmaByte[2] = 0x5;
				btdm8723.psTdmaByte[3] = 0x81;
				btdm8723.psTdmaByte[4] = 0x80;
			}
			else if(BT_TXRX_CNT_LEVEL_1 == btTxRxCntLvl)
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters >= 1200 && < 1400\n"));
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0xa;
				btdm8723.psTdmaByte[2] = 0xa;
				btdm8723.psTdmaByte[3] = 0x81;
				btdm8723.psTdmaByte[4] = 0x80;
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters < 1200\n"));
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0xf;
				btdm8723.psTdmaByte[2] = 0xf;
				btdm8723.psTdmaByte[3] = 0x81;
				btdm8723.psTdmaByte[4] = 0x80;
			}
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 low \n"));
			if(BT_TXRX_CNT_LEVEL_2 == btTxRxCntLvl)
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters >= 1400\n"));
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0x5;
				btdm8723.psTdmaByte[2] = 0x5;
				btdm8723.psTdmaByte[3] = 0x0;
				btdm8723.psTdmaByte[4] = 0x80;
			}
			else if(BT_TXRX_CNT_LEVEL_1 == btTxRxCntLvl)
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters >= 1200 && < 1400\n"));
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0xa;
				btdm8723.psTdmaByte[2] = 0xa;
				btdm8723.psTdmaByte[3] = 0x0;
				btdm8723.psTdmaByte[4] = 0x80;
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters < 1200\n"));
				btdm8723.psTdmaByte[0] = 0xa3;
				btdm8723.psTdmaByte[1] = 0xf;
				btdm8723.psTdmaByte[2] = 0xf;
				btdm8723.psTdmaByte[3] = 0x0;
				btdm8723.psTdmaByte[4] = 0x80;
			}
		}
	}

	if(btdm_NeedToDecBtPwr(padapter))
	{
		btdm8723.bDecBtPwr = _TRUE;
	}

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT btInqPageStartTime = 0x%"i64fmt"x, btTxRxCntLvl = %d\n", 
			pHalData->bt_coexist.halCoex8723.btInqPageStartTime, btTxRxCntLvl));
	if( (pHalData->bt_coexist.halCoex8723.btInqPageStartTime) ||
		(BT_TXRX_CNT_LEVEL_3 == btTxRxCntLvl) )
	{
		btdm8723.bPsTdmaOn = _TRUE;
		btdm8723.psTdmaByte[0] = 0xa3;
		btdm8723.psTdmaByte[1] = 0x5;
		btdm8723.psTdmaByte[2] = 0x5;
		btdm8723.psTdmaByte[3] = 0x83;
		btdm8723.psTdmaByte[4] = 0x80;
		btdm8723.bIgnoreWlanAct = _TRUE;
	}

	if(BTDM_IsCoexistStateChanged(padapter))
	{
		btdm_SetBtdm(padapter, &btdm8723);
	}
}


//============================================================
// extern function start with BTDM_
//============================================================
void BTDM_ForceA2dpSink(PADAPTER padapter, u8 type)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	pHalData->bt_coexist.halCoex8723.bForceA2dpSink = (u8)type;

	DbgPrint("cosa force bt A2dp sink = %d\n",
		pHalData->bt_coexist.halCoex8723.bForceA2dpSink);
}

void BTDM_2AntParaInit(PADAPTER padapter)
{
	// Enable counter statistics
	rtw_write8(padapter, 0x76e, 0x4);
	rtw_write8(padapter, 0x778, 0x3);
	rtw_write8(padapter, 0x40, 0x20);
}

void BTDM_2AntHwCoexAllOff8723A(PADAPTER padapter)
{
	btdm_SetCoexTable(padapter, 0x5a5aaaaa, 0xcc, 0x3);
	btdm_SetHwPtaMode(padapter, _FALSE);
}

void BTDM_2AntFwCoexAllOff8723A(PADAPTER padapter)
{
	BTDM_SetFw3a(padapter, 0x0, 0x0, 0x0, 0x8, 0x0); 	// Antenna control by PTA, 0x870 = 0x300.
	btdm_SetFw2AntHID(padapter, _FALSE, _FALSE);
	btdm_SetFwTraTdmaCtrl(padapter, _FALSE, TDMA_2ANT, TDMA_NAV_OFF);
	btdm_SetFwTdmaCtrl(padapter, _FALSE, TDMA_2ANT, TDMA_NAV_OFF, TDMA_DAC_SWING_OFF);
	btdm_SetFwDacSwingLevel(padapter, BT_DACSWING_OFF);
	btdm_SetFwBtHidInfo(padapter, _FALSE);
	btdm_SetFwBtRetryIndex(padapter, 2);
	btdm_SetFwWlanAct(padapter, 0x10, 0x10);
	BTDM_SetFwDecBtPwr(padapter, _FALSE);
}

void BTDM_2AntSwCoexAllOff8723A(PADAPTER padapter)
{
	BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
	BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_OFF);
	BTDM_RejectAPAggregatedPacket(padapter, _FALSE);

	BTDM_SetSwPenaltyTxRateAdaptive(padapter, BT_TX_RATE_ADAPTIVE_NORMAL);
	BTDM_SetSwRfRxLpfCorner(padapter, BT_RF_RX_LPF_CORNER_RESUME);
	btdm_SetSwFullTimeDacSwing(padapter, _FALSE, 0xc0);
}

void BTDM_2AntAdjustForBtOperation8723(PADAPTER padapter)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	PBTDM_8723A_2ANT	pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm2Ant;
	BTDM_8723A_2ANT	btdmAdjust;

	_rtw_memcpy(&btdmAdjust, pBtdm8723, sizeof(BTDM_8723A_2ANT));
	switch (pBtMgnt->ExtConfig.btOperationCode)
	{
		case HCI_BT_OP_NONE:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[BT OP] : Adjust for operation None!!\n"));
			break;
		case HCI_BT_OP_INQUIRY_START:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[BT OP] : Adjust for Inquiry start!!\n"));
			pBtMgnt->ExtConfig.bHoldForBtOperation = _TRUE;
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_INQ_PAGE;
			if(!pBtdm8723->bIgnoreWlanAct)
			{
				RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[BT OP] : need to ignore wlanAct!!\n"));
				btdmAdjust.bIgnoreWlanAct = _TRUE;
				btdm_SetBtdm(padapter, &btdmAdjust);
			}
			break;
		case HCI_BT_OP_INQUIRY_FINISH:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[BT OP] : Adjust for Inquiry finished!!\n"));
			pBtMgnt->ExtConfig.bHoldForBtOperation = _FALSE;
			pHalData->bt_coexist.CurrentState &=~ BT_COEX_STATE_BT_INQ_PAGE;
			break;
		case HCI_BT_OP_PAGING_START:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[BT OP] : Adjust for paging start!!\n"));
			pBtMgnt->ExtConfig.bHoldForBtOperation = _TRUE;
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_INQ_PAGE;
			if(!pBtdm8723->bIgnoreWlanAct)
			{
				RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[BT OP] : need to ignore wlanAct!!\n"));
				btdmAdjust.bIgnoreWlanAct = _TRUE;
				btdm_SetBtdm(padapter, &btdmAdjust);
			}
			break;
		case HCI_BT_OP_PAGING_SUCCESS:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[BT OP] : Adjust for paging successfully!!\n"));
			pBtMgnt->ExtConfig.bHoldForBtOperation = _FALSE;
			pHalData->bt_coexist.CurrentState &=~ BT_COEX_STATE_BT_INQ_PAGE;			
			break;
		case HCI_BT_OP_PAGING_UNSUCCESS:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[BT OP] : Adjust for paging unsuccessfully!!\n"));
			pBtMgnt->ExtConfig.bHoldForBtOperation = _FALSE;
			pHalData->bt_coexist.CurrentState &=~ BT_COEX_STATE_BT_INQ_PAGE;
			break;
		case HCI_BT_OP_PAIRING_START:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[BT OP] : Adjust for Pairing start!!\n"));
			pBtMgnt->ExtConfig.bHoldForBtOperation = _TRUE;
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_INQ_PAGE;
			if(!pBtdm8723->bIgnoreWlanAct)
			{
				RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[BT OP] : need to ignore wlanAct!!\n"));
				btdmAdjust.bIgnoreWlanAct = _TRUE;
				btdm_SetBtdm(padapter, &btdmAdjust);
			}
			break;
		case HCI_BT_OP_PAIRING_FINISH:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[BT OP] : Adjust for Pairing finished!!\n"));
			pBtMgnt->ExtConfig.bHoldForBtOperation = _FALSE;
			pHalData->bt_coexist.CurrentState &=~ BT_COEX_STATE_BT_INQ_PAGE;
			break;

		case HCI_BT_OP_BT_DEV_ENABLE:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[BT OP] : Adjust for BT Device enable!!\n"));
			break;
		case HCI_BT_OP_BT_DEV_DISABLE:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[BT OP] : Adjust for BT Device disable!!\n"));
			break;
		default:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[BT OP] : Adjust for Unknown, error!!\n"));
			break;
	}
}


VOID
BTDM_2AntFwC2hBtInfo8723A(
	PADAPTER	padapter
	)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u1Byte	btInfo=0;

	btInfo = pHalData->bt_coexist.halCoex8723.c2hBtInfoOriginal;
	
	// check BIT2 first ==> check if bt is under inquiry or page scan
	if(btInfo & BIT(2))
	{
		pHalData->bt_coexist.halCoex8723.bC2hBtInquiryPage = _TRUE;
	}
	else
	{
		pHalData->bt_coexist.halCoex8723.bC2hBtInquiryPage = _FALSE;
	}

	if(btInfo&BTINFO_B_CONNECTION)
	{
		RTPRINT(FBT, BT_TRACE, ("[BTC2H], BTInfo: bConnect=TRUE\n"));
		pBtMgnt->ExtConfig.bBTBusy = _TRUE;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_IDLE;
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BTC2H], BTInfo: bConnect=FALSE\n"));
		pBtMgnt->ExtConfig.bBTBusy = _FALSE;
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_IDLE;
	}
//From 	
	BTDM_CheckWiFiState(padapter);
	if(pBtMgnt->ExtConfig.bManualControl)
	{
		RTPRINT(FBT, BT_TRACE, ("Action Manual control, won't execute bt coexist mechanism!!\n"));
		return;
	}
}


void BTDM_2AntBtCoexist8723A(PADAPTER padapter)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PBT_DBG 		pBtDbg = &pBTInfo->BtDbg;
	u8				BtState = 0, btInfoOriginal=0, btRetryCnt=0;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (BTDM_BtProfileSupport(padapter))
	{
		BTHCI_GetProfileNameMoto(padapter);
		btdm_BtCommonStateUpdate2Ant8723A(padapter);
		BTDM_CheckWiFiState(padapter);
		if (pBtMgnt->ExtConfig.bHoldForBtOperation)
		{
			RTPRINT(FBT, BT_TRACE, ("Action for BT Operation adjust!!\n"));
			return;
		}
		if (pBtDbg->dbgCtrl)
		{
			RTPRINT(FBT, BT_TRACE, ("[Dbg control], "));
		}

		BTDM_ResetActionProfileState(padapter);
		if (BTDM_IsActionSCO(padapter))
		{
			pBtMgnt->ExtConfig.btProfileCase = BT_COEX_MECH_SCO;
			btdm_BtStateUpdate2Ant8723ASco(padapter);
			if (btdm_Is2Ant8723ACommonAction(padapter))
			{
				pBtMgnt->ExtConfig.btProfileAction = BT_COEX_MECH_COMMON;
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant common.\n"));
			}
			else
			{
				pBtMgnt->ExtConfig.btProfileAction = BT_COEX_MECH_SCO;
				RTPRINT(FBT, BT_TRACE, ("Action SCO\n"));
				btdm_2Ant8723ASCOAction(padapter);
			}
		}
		else if (BTDM_IsActionHID(padapter))
		{
			pBtMgnt->ExtConfig.btProfileCase = BT_COEX_MECH_HID;
			btdm_BtStateUpdate2Ant8723AHid(padapter);
			pBtMgnt->ExtConfig.btProfileAction = BT_COEX_MECH_HID;
			RTPRINT(FBT, BT_TRACE, ("Action HID\n"));
			btdm_2Ant8723AHIDAction(padapter);
		}
		else if (BTDM_IsActionA2DP(padapter))
		{
			pBtMgnt->ExtConfig.btProfileCase = BT_COEX_MECH_A2DP;
			btdm_BtStateUpdate2Ant8723AA2dp(padapter);
			if (btdm_Is2Ant8723ACommonAction(padapter))
			{
				pBtMgnt->ExtConfig.btProfileAction = BT_COEX_MECH_COMMON;
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant common.\n"));
			}
			else
			{
				pBtMgnt->ExtConfig.btProfileAction = BT_COEX_MECH_A2DP;
				RTPRINT(FBT, BT_TRACE, ("Action A2DP for ACL only busy\n"));
//				btdm_2Ant8723AA2DPAction(padapter);
				btdm_2Ant8723AAclOnlyBusy(padapter);
			}
		}
		else if (BTDM_IsActionPAN(padapter))
		{
			pBtMgnt->ExtConfig.btProfileCase = BT_COEX_MECH_PAN;
			btdm_BtStateUpdate2Ant8723APan(padapter);
			if (btdm_Is2Ant8723ACommonAction(padapter))
			{
				pBtMgnt->ExtConfig.btProfileAction = BT_COEX_MECH_COMMON;
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant common.\n"));
			}
			else
			{
				pBtMgnt->ExtConfig.btProfileAction = BT_COEX_MECH_PAN;
				RTPRINT(FBT, BT_TRACE, ("Action PAN\n"));
				btdm_2Ant8723APANAction(padapter);
			}
		}
		else if (BTDM_IsActionHIDA2DP(padapter))
		{
			pBtMgnt->ExtConfig.btProfileCase = BT_COEX_MECH_HID_A2DP;
			btdm_BtStateUpdate2Ant8723AHidA2dp(padapter);
			RTPRINT(FBT, BT_TRACE, ("Action HID_A2DP\n"));
			pBtMgnt->ExtConfig.btProfileAction = BT_COEX_MECH_HID_A2DP;
			btdm_2Ant8723AHIDA2DPAction(padapter);
		}
		else if (BTDM_IsActionHIDPAN(padapter))
		{
			pBtMgnt->ExtConfig.btProfileCase = BT_COEX_MECH_HID_PAN;
			btdm_BtStateUpdate2Ant8723AHidPan(padapter);
			pBtMgnt->ExtConfig.btProfileAction = BT_COEX_MECH_HID_PAN;
			RTPRINT(FBT, BT_TRACE, ("Action HID_PAN\n"));
			btdm_2Ant8723AHIDPANAction(padapter);
		}
		else if (BTDM_IsActionPANA2DP(padapter))
		{
			pBtMgnt->ExtConfig.btProfileCase = BT_COEX_MECH_PAN_A2DP;
			btdm_BtStateUpdate2Ant8723APanA2dp(padapter);
			if (btdm_Is2Ant8723ACommonAction(padapter))
			{
				pBtMgnt->ExtConfig.btProfileAction = BT_COEX_MECH_COMMON;
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant common.\n"));
			}
			else
			{
				pBtMgnt->ExtConfig.btProfileAction = BT_COEX_MECH_PAN_A2DP;
				RTPRINT(FBT, BT_TRACE, ("Action PAN_A2DP\n"));
				btdm_2Ant8723APANA2DPAction(padapter);
			}
		}
		else
		{
			pBtMgnt->ExtConfig.btProfileCase = BT_COEX_MECH_NONE;
			RTPRINT(FBT, BT_TRACE, ("No Action Matched \n"));
			btdm_Is2Ant8723ACommonAction(padapter);
			pBtMgnt->ExtConfig.btProfileAction = BT_COEX_MECH_COMMON;
			RTPRINT(FBT, BT_TRACE, ("Action 2-Ant common.\n"));
		}
	}
	else
	{
		#if 1	
		RTPRINT(FBT, BT_TRACE, ("[BTCoex] Get bt info by fw!!\n"));
		//msg shows c2h rsp for bt_info is received or not.
		if (pHalData->bt_coexist.halCoex8723.bC2hBtInfoReqSent)
		{
			RTPRINT(FBT, BT_TRACE, ("[BTCoex] c2h for btInfo not rcvd yet!!\n"));
		}

		btRetryCnt = pHalData->bt_coexist.halCoex8723.btRetryCnt;
		btInfoOriginal = pHalData->bt_coexist.halCoex8723.c2hBtInfoOriginal;

		// when bt inquiry or page scan, we have to set h2c 0x25
		// ignore wlanact for continuous 4x2secs
		btdm_BtInqPageMonitor(padapter);
		BTDM_ResetActionProfileState(padapter);

				if (btdm_Is2Ant8723ACommonAction(padapter))
				{
			pBtMgnt->ExtConfig.btProfileCase = BT_COEX_MECH_COMMON;
					pBtMgnt->ExtConfig.btProfileAction = BT_COEX_MECH_COMMON;
					RTPRINT(FBT, BT_TRACE, ("Action 2-Ant common.\n"));
				}
				else
				{
			if( (btInfoOriginal&BTINFO_B_HID) ||
				(btInfoOriginal&BTINFO_B_SCO_BUSY) ||
				(btInfoOriginal&BTINFO_B_SCO_ESCO) )
					{
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BTINFO_B_HID_SCOESCO;
				pBtMgnt->ExtConfig.btProfileCase = BT_COEX_MECH_HID_SCO_ESCO;
				pBtMgnt->ExtConfig.btProfileAction = BT_COEX_MECH_HID_SCO_ESCO;
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BTInfo: bHid|bSCOBusy|bSCOeSCO\n"));
				btdm_2Ant8723AHidScoEsco(padapter);
				}
			else if( (btInfoOriginal&BTINFO_B_FTP) ||
				(btInfoOriginal&BTINFO_B_A2DP) )
				{
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BTINFO_B_FTP_A2DP;
				pBtMgnt->ExtConfig.btProfileCase = BT_COEX_MECH_FTP_A2DP;
				pBtMgnt->ExtConfig.btProfileAction = BT_COEX_MECH_FTP_A2DP;
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BTInfo: bFTP|bA2DP\n"));
				btdm_2Ant8723AFtpA2dp(padapter);
				}
				else
				{
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BTINFO_B_HID_SCOESCO;
				pBtMgnt->ExtConfig.btProfileCase = BT_COEX_MECH_NONE;
				pBtMgnt->ExtConfig.btProfileAction = BT_COEX_MECH_NONE;
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], BTInfo: undefined case!!!!\n"));
				btdm_2Ant8723AHidScoEsco(padapter);
				}
		}
	#endif
	}
}

// ===== End of sync from SD7 driver HAL/BTCoexist/HalBtc87232Ant.c =====
#endif

#ifdef __HALBTC8723_C__ // HAL/BTCoexist/HalBtc8723.c
// ===== Below this line is sync from SD7 driver HAL/BTCoexist/HalBtc8723.c =====

static u8 btCoexDbgBuf[BT_TMP_BUF_SIZE];
const char *const BtProfileString[]={
	"NONE",
	"A2DP",
	"PAN",
	"HID",
	"SCO",
};
const char *const BtSpecString[]={
	"1.0b",
	"1.1",
	"1.2",
	"2.0+EDR",
	"2.1+EDR",
	"3.0+HS",
	"4.0",
};
const char *const BtLinkRoleString[]={
	"Master",
	"Slave",
};

u8 btdm_BtWifiAntNum(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT_COEXIST_8723A	pBtCoex = &pHalData->bt_coexist.halCoex8723;

	RTPRINT(FBT, BT_TRACE, ("%s pHalData->bt_coexist.BluetoothCoexist =%x pHalData->EEPROMBluetoothCoexist=%x \n",
		__func__,pHalData->bt_coexist.BluetoothCoexist,pHalData->EEPROMBluetoothCoexist));	
	RTPRINT(FBT, BT_TRACE, ("%s pHalData->bt_coexist.BT_Ant_Num =%x pHalData->EEPROMBluetoothAntNum=%x \n",
		__func__,pHalData->bt_coexist.BT_Ant_Num,pHalData->EEPROMBluetoothAntNum));	
	if (Ant_x2 == pHalData->bt_coexist.BT_Ant_Num)
	{
		if (Ant_x2 == pBtCoex->TotalAntNum)
			return Ant_x2;
		else
			return Ant_x1;
	}
	else
	{
		return Ant_x1;
	}

	return Ant_x2;
}

void btdm_BtHwCountersMonitor(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u32				regHPTxRx, regLPTxRx, u4Tmp;
	u32				regHPTx=0, regHPRx=0, regLPTx=0, regLPRx=0;
//	u8				u1Tmp;

	regHPTxRx = REG_HIGH_PRIORITY_TXRX;
	regLPTxRx = REG_LOW_PRIORITY_TXRX;

	u4Tmp = rtw_read32(padapter, regHPTxRx);
	regHPTx = u4Tmp & bMaskLWord;
	regHPRx = (u4Tmp & bMaskHWord)>>16;

	u4Tmp = rtw_read32(padapter, regLPTxRx);
	regLPTx = u4Tmp & bMaskLWord;
	regLPRx = (u4Tmp & bMaskHWord)>>16;

	pHalData->bt_coexist.halCoex8723.highPriorityTx = regHPTx;
	pHalData->bt_coexist.halCoex8723.highPriorityRx = regHPRx;
	pHalData->bt_coexist.halCoex8723.lowPriorityTx = regLPTx;
	pHalData->bt_coexist.halCoex8723.lowPriorityRx = regLPRx;

//	RTPRINT(FBT, BT_TRACE, ("High Priority Tx/Rx (reg 0x%x)=%x(%d)/%x(%d)\n",
//		regHPTxRx, regHPTx, regHPTx, regHPRx, regHPRx));
//	RTPRINT(FBT, BT_TRACE, ("Low Priority Tx/Rx (reg 0x%x)=%x(%d)/%x(%d)\n",
//		regLPTxRx, regLPTx, regLPTx, regLPRx, regLPRx));

	// reset counter
	//u1Tmp = rtw_read8(padapter, 0x76e);
	//DbgPrint("read 2 back 0x76e= 0x%x\n", u1Tmp);
	//u4Tmp |= BIT3;
	rtw_write8(padapter, 0x76e, 0xc);
}

// This function check if 8723 bt is disabled
void btdm_BtEnableDisableCheck8723A(PADAPTER padapter)
{
	u8		btAlife = _TRUE;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);


#ifdef CHECK_BT_EXIST_FROM_REG
	u8		val8;

	// ox68[28]=1 => BT enable; otherwise disable
	val8 = rtw_read8(padapter, 0x6B);
	if (!(val8 & BIT(4))) btAlife = _FALSE;

	if (btAlife)
	{
		pHalData->bt_coexist.bCurBtDisabled = _FALSE;
//		RTPRINT(FBT, BT_TRACE, ("8723A BT is enabled !!\n"));
	}
	else
	{
		pHalData->bt_coexist.bCurBtDisabled = _TRUE;
//		RTPRINT(FBT, BT_TRACE, ("8723A BT is disabled !!\n"));
	}
#else
	if (pHalData->bt_coexist.halCoex8723.highPriorityTx == 0 &&
		pHalData->bt_coexist.halCoex8723.highPriorityRx == 0 &&
		pHalData->bt_coexist.halCoex8723.lowPriorityTx == 0 &&
		pHalData->bt_coexist.halCoex8723.lowPriorityRx == 0)
	{
		btAlife = _FALSE;
	}
	if (pHalData->bt_coexist.halCoex8723.highPriorityTx == 0xeaea &&
		pHalData->bt_coexist.halCoex8723.highPriorityRx == 0xeaea &&
		pHalData->bt_coexist.halCoex8723.lowPriorityTx == 0xeaea &&
		pHalData->bt_coexist.halCoex8723.lowPriorityRx == 0xeaea)
	{
		btAlife = _FALSE;
	}
	if (pHalData->bt_coexist.halCoex8723.highPriorityTx == 0xffff &&
		pHalData->bt_coexist.halCoex8723.highPriorityRx == 0xffff &&
		pHalData->bt_coexist.halCoex8723.lowPriorityTx == 0xffff &&
		pHalData->bt_coexist.halCoex8723.lowPriorityRx == 0xffff)
	{
		btAlife = _FALSE;
	}
	if (btAlife)
	{
		pHalData->bt_coexist.btActiveZeroCnt = 0;
		pHalData->bt_coexist.bCurBtDisabled = _FALSE;
		RTPRINT(FBT, BT_TRACE, ("8723A BT is enabled !!\n"));
	}
	else
	{
		pHalData->bt_coexist.btActiveZeroCnt++;
		RTPRINT(FBT, BT_TRACE, ("8723A bt all counters=0, %d times!!\n",
				pHalData->bt_coexist.btActiveZeroCnt));
		if (pHalData->bt_coexist.btActiveZeroCnt >= 2)
		{
			pHalData->bt_coexist.bCurBtDisabled = _TRUE;
			RTPRINT(FBT, BT_TRACE, ("8723A BT is disabled !!\n"));
		}
	}
#endif

	if (pHalData->bt_coexist.bPreBtDisabled !=
		pHalData->bt_coexist.bCurBtDisabled)
	{
		RTPRINT(FBT, BT_TRACE, ("8723A BT is from %s to %s!!\n",
			(pHalData->bt_coexist.bPreBtDisabled ? "disabled":"enabled"),
			(pHalData->bt_coexist.bCurBtDisabled ? "disabled":"enabled")));
		pHalData->bt_coexist.bPreBtDisabled = pHalData->bt_coexist.bCurBtDisabled;
	}
}

void btdm_BTCoexist8723AHandler(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData;


	pHalData = GET_HAL_DATA(padapter);

	if (btdm_BtWifiAntNum(padapter) == Ant_x2)
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], 2 Ant mechanism\n"));
		BTDM_2AntBtCoexist8723A(padapter);
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], 1 Ant mechanism\n"));
		BTDM_1AntBtCoexist8723A(padapter);
	}

	BTDM_UpdateCoexState(padapter);
}

//============================================================
// extern function start with BTDM_
//============================================================
u32 BTDM_BtTxRxCounterH(	PADAPTER	padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u32	counters=0;

	counters = pHalData->bt_coexist.halCoex8723.highPriorityTx+
		pHalData->bt_coexist.halCoex8723.highPriorityRx ;
	return counters;
}

u32 BTDM_BtTxRxCounterL(	PADAPTER	padapter 	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u4Byte	counters=0;

	counters = pHalData->bt_coexist.halCoex8723.lowPriorityTx+
		pHalData->bt_coexist.halCoex8723.lowPriorityRx ;
	return counters;
}

void BTDM_SetFwChnlInfo(PADAPTER padapter, RT_MEDIA_STATUS mstatus)
{
//	PMGNT_INFO	pMgntInfo = &padapter->MgntInfo;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	PBT30Info	pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT	pBtMgnt = &pBTInfo->BtMgnt;
	u8		H2C_Parameter[3] ={0};
	u8		chnl;


	if (!IS_HARDWARE_TYPE_8723A(padapter))
		return;

	// opMode
	if (RT_MEDIA_CONNECT == mstatus)
	{
		H2C_Parameter[0] = 0x1;	// 0: disconnected, 1:connected
	}

	if (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE) == _TRUE)
	{
		// channel
		chnl = pmlmeext->cur_channel;
		if (BTDM_IsHT40(padapter))
		{
			if (pmlmeext->cur_ch_offset == HAL_PRIME_CHNL_OFFSET_UPPER)
			{
				chnl += 2;
			}
			else if (pmlmeext->cur_ch_offset == HAL_PRIME_CHNL_OFFSET_LOWER)
			{
				chnl -= 2;
			}
		}
		H2C_Parameter[1] = chnl;
	}
	else	// check if HS link is exists
	{
		// channel
		if (BT_Operation(padapter))
			H2C_Parameter[1] = pBtMgnt->BTChannel;
		else
			H2C_Parameter[1] = pmlmeext->cur_channel;
	}

	if (BTDM_IsHT40(padapter))
	{
		H2C_Parameter[2] = 0x30;
	}
	else
	{
		H2C_Parameter[2] = 0x20;
	}

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], FW write 0x19=0x%x\n",
		H2C_Parameter[0]<<16|H2C_Parameter[1]<<8|H2C_Parameter[2]));

	FillH2CCmd(padapter, 0x19, 3, H2C_Parameter);
}

u8 BTDM_IsWifiConnectionExist(PADAPTER padapter)
{
	u8 bRet = _FALSE;


	if (BTHCI_HsConnectionEstablished(padapter))
		bRet = _TRUE;

	if (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE) == _TRUE)
		bRet = _TRUE;

	return bRet;
}

void BTDM_SetFw3a(
	PADAPTER	padapter,
	u8		byte1,
	u8		byte2,
	u8		byte3,
	u8		byte4,
	u8		byte5
	)
{
	u8			H2C_Parameter[5] = {0};

	if (BTDM_1Ant8723A(padapter) == _TRUE)
	{
		if ((check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE) == _FALSE) &&
			(get_fwstate(&padapter->mlmepriv) != WIFI_NULL_STATE))
		{
			if (byte1 & BIT(4))
			{
				byte1 &= ~BIT(4);
				byte1 |= BIT(5);
			}

			if (byte5 & BIT(6))
			{
				byte5 &= ~BIT(6);
				byte5 |= BIT(5);
			}
		}
	}

	H2C_Parameter[0] = byte1;
	H2C_Parameter[1] = byte2;
	H2C_Parameter[2] = byte3;
	H2C_Parameter[3] = byte4;
	H2C_Parameter[4] = byte5;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], FW write 0x3a(5bytes)=0x%02x%08x\n",
		H2C_Parameter[0],
		H2C_Parameter[1]<<24|H2C_Parameter[2]<<16|H2C_Parameter[3]<<8|H2C_Parameter[4]));

	FillH2CCmd(padapter, 0x3a, 5, H2C_Parameter);
}

void BTDM_ForceBtCoexMechanism(PADAPTER	padapter, u8 type)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);


	pHalData->bt_coexist.halCoex8723.bForceFwBtInfo = type;

	if (pHalData->bt_coexist.halCoex8723.bForceFwBtInfo)
	{
		DbgPrint("cosa force bt info from wifi fw !!!\n");
	}
	else
	{
		DbgPrint("cosa force bt coexist bt info from bt stack\n");
	}
}

void BTDM_QueryBtInformation(PADAPTER padapter)
{
	u8 H2C_Parameter[1] = {0};
	PHAL_DATA_TYPE pHalData;
	PBT_COEXIST_8723A pBtCoex;


	pHalData = GET_HAL_DATA(padapter);
	pBtCoex = &pHalData->bt_coexist.halCoex8723;

	if (BT_IsBtDisabled(padapter) == _TRUE)
	{
		pBtCoex->c2hBtInfo = BT_INFO_STATE_DISABLED;
		pBtCoex->bC2hBtInfoReqSent = _FALSE;
		return;
	}

	if (pBtCoex->c2hBtInfo == BT_INFO_STATE_DISABLED)
		pBtCoex->c2hBtInfo = BT_INFO_STATE_NO_CONNECTION;

	if (pBtCoex->bC2hBtInfoReqSent == _TRUE)
	{
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], didn't recv previous BtInfo report!\n"));
	}
	else
	{
		pBtCoex->bC2hBtInfoReqSent = _TRUE;
	}

	H2C_Parameter[0] |= BIT(0);	// trigger

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], Query Bt information, write 0x38=0x%x\n",
		H2C_Parameter[0]));

	FillH2CCmd(padapter, 0x38, 1, H2C_Parameter);
}

void BTDM_SetSwRfRxLpfCorner(PADAPTER padapter, u8 type)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (BT_RF_RX_LPF_CORNER_SHRINK == type)
	{
		//Shrink RF Rx LPF corner
		RTPRINT(FBT, BT_TRACE, ("Shrink RF Rx LPF corner!!\n"));
		PHY_SetRFReg(padapter, PathA, 0x1e, bRFRegOffsetMask, 0xf0ff7);
		pHalData->bt_coexist.bSWCoexistAllOff = _FALSE;
	}
	else if (BT_RF_RX_LPF_CORNER_RESUME == type)
	{
		//Resume RF Rx LPF corner
		RTPRINT(FBT, BT_TRACE, ("Resume RF Rx LPF corner!!\n"));
		PHY_SetRFReg(padapter, PathA, 0x1e, bRFRegOffsetMask, pHalData->bt_coexist.BtRfRegOrigin1E);
	}
}

void
BTDM_SetSwPenaltyTxRateAdaptive(
	PADAPTER	padapter,
	u8		raType
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8	tmpU1;

	tmpU1 = rtw_read8(padapter, 0x4fd);
	tmpU1 |= BIT(0);
	if (BT_TX_RATE_ADAPTIVE_LOW_PENALTY == raType)
	{
		RTPRINT(FBT, BT_TRACE, ("Tx rate adaptive, set low penalty!!\n"));
		tmpU1 &= ~BIT(2);
		pHalData->bt_coexist.bSWCoexistAllOff = _FALSE;
	}
	else if (BT_TX_RATE_ADAPTIVE_NORMAL == raType)
	{
		RTPRINT(FBT, BT_TRACE, ("Tx rate adaptive, set normal!!\n"));
		tmpU1 |= BIT(2);
	}

	rtw_write8(padapter, 0x4fd, tmpU1);
}

void BTDM_SetFwDecBtPwr(PADAPTER padapter, u8 bDecBtPwr)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			H2C_Parameter[1] = {0};


	H2C_Parameter[0] = 0;

	if (bDecBtPwr)
	{
		H2C_Parameter[0] |= BIT(1);
		pHalData->bt_coexist.bFWCoexistAllOff = _FALSE;
	}

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], decrease Bt Power : %s, write 0x21=0x%x\n",
		(bDecBtPwr? "Yes!!":"No!!"), H2C_Parameter[0]));

	FillH2CCmd(padapter, 0x21, 1, H2C_Parameter);
}

u8 BTDM_BtProfileSupport(PADAPTER padapter)
{
	u8			bRet = _FALSE;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);


	if (pBtMgnt->bSupportProfile &&
		!pHalData->bt_coexist.halCoex8723.bForceFwBtInfo)
	{
		bRet = _TRUE;
	}

	return bRet;
}

void BTDM_AdjustForBtOperation8723A(PADAPTER padapter)
{
	BTDM_2AntAdjustForBtOperation8723(padapter);
}

void BTDM_FwC2hBtRssi8723A(PADAPTER padapter, u8 *tmpBuf)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			percent=0, u1tmp=0;

	u1tmp = tmpBuf[0];
	percent = u1tmp*2+10;

	pHalData->bt_coexist.halCoex8723.btRssi = percent;
	RTPRINT(FBT, BT_TRACE, ("[BTC2H], Bt rssi, hex val=0x%x, percent=%d\n", u1tmp, percent));
}

void BTDM_FwC2hBtInfo8723A(PADAPTER padapter, u8 *tmpBuf, u8 length)
{
	PHAL_DATA_TYPE	pHalData;
	PBT30Info		pBTInfo;
	PBT_MGNT		pBtMgnt;
	PBT_COEXIST_8723A pBtCoex;
	u8	i;


	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;
	pBtCoex = &pHalData->bt_coexist.halCoex8723;

	pBtCoex->bC2hBtInfoReqSent = _FALSE;

	RTPRINT(FBT, BT_TRACE, ("[BTC2H], Bt info, length=%d, hex data=[", length));

	pBtCoex->btRetryCnt = 0;
	for (i=0; i<length; i++)
	{
		if (i == 0)
		{
			pBtCoex->c2hBtInfoOriginal = tmpBuf[i];
		}
		else if (i == 1)
		{
			pBtCoex->btRetryCnt = tmpBuf[i];
		}
		if (i == length-1)
		{
			RTPRINT(FBT, BT_TRACE, ("0x%02x]\n", tmpBuf[i]));
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("0x%02x, ", tmpBuf[i]));
		}
	}

	if (pBtMgnt->ExtConfig.bManualControl)
	{
		RTPRINT(FBT, BT_TRACE, ("%s: Action Manual control!!\n", __FUNCTION__));
		return;
	}

	if (btdm_BtWifiAntNum(padapter) == Ant_x1)
		BTDM_1AntFwC2hBtInfo8723A(padapter);
	else
		BTDM_2AntFwC2hBtInfo8723A(padapter);

	btdm_BTCoexist8723AHandler(padapter);
}

void BTDM_Display8723ABtCoexInfo(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT_COEXIST_8723A	pBtCoex = &pHalData->bt_coexist.halCoex8723;
	PBT30Info			pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT			pBtMgnt = &pBTInfo->BtMgnt;
	PBT_DBG 			pBtDbg = &pBTInfo->BtDbg;
	u8			u1Tmp, u1Tmp1, u1Tmp2, i;
	u32			u4Tmp;

	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n ============[BT Coexist info]============");
	DCMD_Printf(btCoexDbgBuf);

	if (!pHalData->bt_coexist.BluetoothCoexist)
	{
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n BT not exists !!!");
		DCMD_Printf(btCoexDbgBuf);
		return;
	}
	else
	{
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "Ant mechanism", \
			((pHalData->bt_coexist.BT_Ant_Num == Ant_x2)? 2:1));
		DCMD_Printf(btCoexDbgBuf);
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s ", "Profile notified", \
			((pBtMgnt->bSupportProfile)? "Yes":"No"));
		DCMD_Printf(btCoexDbgBuf);
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "Wifi rssi", \
			pHalData->dmpriv.UndecoratedSmoothedPWDB);
		DCMD_Printf(btCoexDbgBuf);
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "Bt rssi", \
			pHalData->bt_coexist.halCoex8723.btRssi);
		DCMD_Printf(btCoexDbgBuf);
	}

	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "Bt is under inquiry/page/pair", \
		((pBtMgnt->ExtConfig.bHoldForBtOperation)? 1:0));
	DCMD_Printf(btCoexDbgBuf);

	if (pBtMgnt->ExtConfig.bManualControl)
	{
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "Action Manual control!!");
		DCMD_Printf(btCoexDbgBuf);
	}
	else
	{
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "BT hci extension version", \
			pBtMgnt->ExtConfig.HCIExtensionVer);
		DCMD_Printf(btCoexDbgBuf);
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d", "NumberOfHandle / NumberOfSCO", \
			pBtMgnt->ExtConfig.NumberOfHandle, pBtMgnt->ExtConfig.NumberOfSCO);
		DCMD_Printf(btCoexDbgBuf);
	}

	if (!pBtMgnt->ExtConfig.bManualControl)
	{
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s / %s ", "WIfi state", \
			((pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_LEGACY)? "Legacy": \
			(((pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_HT20)? "HT20":"HT40"))),
			((pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_IDLE)? "Idle":\
			((pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_UPLINK)? "Uplink":"Downlink")));
		DCMD_Printf(btCoexDbgBuf);
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s / %s", "Bt state", \
			((BTDM_IsBTBusy(padapter))? "Busy": "Idle"), ((BTDM_IsBTUplink(padapter))? "Uplink":"Downlink"));
		DCMD_Printf(btCoexDbgBuf);

		if (pBtDbg->dbgCtrl)
		{
			rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s =", "[Dbg control]");
			DCMD_Printf(btCoexDbgBuf);
		}

		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s =", "Profile case/Mechanism type");
		DCMD_Printf(btCoexDbgBuf);
		switch (pBtMgnt->ExtConfig.btProfileCase)
		{
			case BT_COEX_MECH_NONE:
				rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, " NONE");
				break;
			case BT_COEX_MECH_SCO:
				rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, " SCO");
				break;
			case BT_COEX_MECH_HID:
				rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, " HID");
				break;
			case BT_COEX_MECH_A2DP:
				rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, " A2DP");
				break;
			case BT_COEX_MECH_PAN:
				rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, " PAN");
				break;
			case BT_COEX_MECH_HID_A2DP:
				rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, " HID+A2DP");
				break;
			case BT_COEX_MECH_HID_PAN:
				rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, " HID+PAN");
				break;
			case BT_COEX_MECH_PAN_A2DP:
				rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, " PAN+A2DP");
				break;
			default:
				rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, " Undefined");
				break;
		}
		DCMD_Printf(btCoexDbgBuf);
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, " / ");
		DCMD_Printf(btCoexDbgBuf);
		switch (pBtMgnt->ExtConfig.btProfileAction)
		{
			case BT_COEX_MECH_NONE:
				rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, " NONE");
				break;
			case BT_COEX_MECH_SCO:
				rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, " SCO");
				break;
			case BT_COEX_MECH_HID:
				rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, " HID");
				break;
			case BT_COEX_MECH_A2DP:
				rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, " A2DP");
				break;
			case BT_COEX_MECH_PAN:
				rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, " PAN");
				break;
			case BT_COEX_MECH_HID_A2DP:
				rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, " HID+A2DP");
				break;
			case BT_COEX_MECH_HID_PAN:
				rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, " HID+PAN");
				break;
			case BT_COEX_MECH_PAN_A2DP:
				rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, " PAN+A2DP");
				break;
			case BT_COEX_MECH_COMMON:
				rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, " COMMON");
				break;
			default:
				rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, " Undefined");
				break;
		}
		DCMD_Printf(btCoexDbgBuf);

		if (!pBtMgnt->ExtConfig.bManualControl)
		{
			for (i=0; i<pBtMgnt->ExtConfig.NumberOfHandle; i++)
			{
				if (pBtMgnt->ExtConfig.HCIExtensionVer >= 1)
				{
					rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s/ %s", "Bt link type/spec/role", \
						BtProfileString[pBtMgnt->ExtConfig.linkInfo[i].BTProfile],
						BtSpecString[pBtMgnt->ExtConfig.linkInfo[i].BTCoreSpec],
						BtLinkRoleString[pBtMgnt->ExtConfig.linkInfo[i].linkRole]);
					DCMD_Printf(btCoexDbgBuf);
				}
				else
				{
					rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s", "Bt link type/spec", \
						BtProfileString[pBtMgnt->ExtConfig.linkInfo[i].BTProfile],
						BtSpecString[pBtMgnt->ExtConfig.linkInfo[i].BTCoreSpec]);
					DCMD_Printf(btCoexDbgBuf);
				}
			}
		}
	}

	// Sw mechanism
	if (!pBtMgnt->ExtConfig.bManualControl)
	{
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Sw BT Coex mechanism]============");
		DCMD_Printf(btCoexDbgBuf);
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "Coex All off", \
			pBtCoex->btdm2Ant.bAllOff);
		DCMD_Printf(btCoexDbgBuf);
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "Reject Aggre packet", \
			pBtCoex->btdm2Ant.bRejectAggrePkt);
		DCMD_Printf(btCoexDbgBuf);
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "AGC Table", \
			pBtCoex->btdm2Ant.bAgcTableEn);
		DCMD_Printf(btCoexDbgBuf);
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "ADC Backoff", \
			pBtCoex->btdm2Ant.bAdcBackOffOn);
		DCMD_Printf(btCoexDbgBuf);
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "Low penalty RA", \
			pBtCoex->btdm2Ant.bLowPenaltyRateAdaptive);
		DCMD_Printf(btCoexDbgBuf);
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "RF Rx LPF Shrink", \
			pBtCoex->btdm2Ant.bRfRxLpfShrink);
		DCMD_Printf(btCoexDbgBuf);
	}
	u4Tmp = PHY_QueryRFReg(padapter, PathA, 0x1e, 0xff0);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x", "RF-A, 0x1e[11:4]/original val", \
		u4Tmp, pHalData->bt_coexist.BtRfRegOrigin1E);
	DCMD_Printf(btCoexDbgBuf);

	// Fw mechanism
	if (!pBtMgnt->ExtConfig.bManualControl)
	{
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Fw BT Coex mechanism]============");
		DCMD_Printf(btCoexDbgBuf);
	}
	if (!pBtMgnt->ExtConfig.bManualControl)
	{
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "2Ant+HID mode", \
		pBtCoex->btdm2Ant.b2AntHidEn);
		DCMD_Printf(btCoexDbgBuf);

		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x / 0x%x", "WLan Act Hi/Lo", \
			pBtCoex->btdm2Ant.wlanActHi, pBtCoex->btdm2Ant.wlanActLo);
		DCMD_Printf(btCoexDbgBuf);

		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "BT retry index", \
			pBtCoex->btdm2Ant.btRetryIndex);
		DCMD_Printf(btCoexDbgBuf);

		// TDMA mode related
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d", "TDMA Mode/Ant/NAV", \
			pBtCoex->btdm2Ant.bTdmaOn, pBtCoex->btdm2Ant.tdmaAnt,
			pBtCoex->btdm2Ant.tdmaNav);
		DCMD_Printf(btCoexDbgBuf);
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d", "TTDMA Mode/Ant/NAV", \
			pBtCoex->btdm2Ant.bTraTdmaOn, pBtCoex->btdm2Ant.traTdmaAnt,
			pBtCoex->btdm2Ant.traTdmaNav);
		DCMD_Printf(btCoexDbgBuf);
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "PsTDMA Mode", \
			pBtCoex->btdm2Ant.bPsTdmaOn);
		DCMD_Printf(btCoexDbgBuf);

		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "Decrease Bt Power", \
			pBtCoex->btdm2Ant.bDecBtPwr);
		DCMD_Printf(btCoexDbgBuf);
	}
	u1Tmp = rtw_read8(padapter, 0x778);
	u1Tmp1 = rtw_read8(padapter, 0x783);
	u1Tmp2 = rtw_read8(padapter, 0x796);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "0x778/ 0x783/ 0x796", \
		u1Tmp, u1Tmp1, u1Tmp2);
	DCMD_Printf(btCoexDbgBuf);

	if (!pBtMgnt->ExtConfig.bManualControl)
	{
		// Dac Swing
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x / 0x%x", "Fw DacSwing Ctrl/Val", \
			pBtCoex->btdm2Ant.tdmaDacSwing, pBtCoex->btdm2Ant.fwDacSwingLvl);
		DCMD_Printf(btCoexDbgBuf);
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x / 0x%x", "Sw DacSwing Ctrl/Val", \
			pBtCoex->btdm2Ant.bSwDacSwingOn, pBtCoex->btdm2Ant.swDacSwingLvl);
		DCMD_Printf(btCoexDbgBuf);
	}
	u4Tmp = rtw_read32(padapter, 0x880);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x880", \
		u4Tmp);
	DCMD_Printf(btCoexDbgBuf);

	// Hw mechanism
	if (!pBtMgnt->ExtConfig.bManualControl)
	{
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Hw BT Coex mechanism]============");
		DCMD_Printf(btCoexDbgBuf);
	}
	if (!pBtMgnt->ExtConfig.bManualControl)
	{
		// PTA mode related
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "PTA mode", \
			pBtCoex->btdm2Ant.bPtaOn);
		DCMD_Printf(btCoexDbgBuf);
	}

	u1Tmp = rtw_read8(padapter, 0x40);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x40", \
		u1Tmp);
	DCMD_Printf(btCoexDbgBuf);
	u4Tmp = rtw_read32(padapter, 0x6c0);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x6c0", \
		u4Tmp);
	DCMD_Printf(btCoexDbgBuf);
	u4Tmp = rtw_read32(padapter, 0x6c8);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x6c8", \
		u4Tmp);
	DCMD_Printf(btCoexDbgBuf);
	u1Tmp = rtw_read8(padapter, 0x6cc);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x6cc", \
		u1Tmp);
	DCMD_Printf(btCoexDbgBuf);

	u4Tmp = rtw_read32(padapter, 0x770);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x770(Hi pri Rx[31:16]/Tx[15:0])", \
		u4Tmp);
	DCMD_Printf(btCoexDbgBuf);
	u4Tmp = rtw_read32(padapter, 0x774);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x774(Lo pri Rx[31:16]/Tx[15:0])", \
		u4Tmp);
	DCMD_Printf(btCoexDbgBuf);

	// Tx mgnt queue hang or not, 0x41b should = 0xf, ex: 0xd ==>hang
	u1Tmp = rtw_read8(padapter, 0x41b);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x41b (hang chk == 0xf)", \
		u1Tmp);
	DCMD_Printf(btCoexDbgBuf);
}

void BTDM_8723AInit(PADAPTER padapter)
{
	if (IS_HARDWARE_TYPE_8723A(padapter))
	{
		if (btdm_BtWifiAntNum(padapter) == Ant_x2)
			BTDM_2AntParaInit(padapter);
		else
			BTDM_1AntParaInit(padapter);
	}
}

void BTDM_HWCoexAllOff8723A(PADAPTER padapter)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bManualControl)
		return;

	if (btdm_BtWifiAntNum(padapter) == Ant_x2)
		BTDM_2AntHwCoexAllOff8723A(padapter);
}

void BTDM_FWCoexAllOff8723A(PADAPTER padapter)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bManualControl)
		return;

	if (btdm_BtWifiAntNum(padapter) == Ant_x2)
		BTDM_2AntFwCoexAllOff8723A(padapter);
}

void BTDM_SWCoexAllOff8723A(PADAPTER padapter)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bManualControl)
		return;

	if (btdm_BtWifiAntNum(padapter) == Ant_x2)
		BTDM_2AntSwCoexAllOff8723A(padapter);
}

void
BTDM_Set8723ABtCoexCurrAntNum(
	PADAPTER	padapter,
	u8		antNum
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT_COEXIST_8723A	pBtCoex = &pHalData->bt_coexist.halCoex8723;

	if (antNum == 1)
	{
		pBtCoex->TotalAntNum = Ant_x1;
	}
	else if (antNum == 2)
	{
		pBtCoex->TotalAntNum = Ant_x2;
	}
}

void BTDM_LpsLeave(PADAPTER padapter)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bManualControl)
		return;

	if (btdm_BtWifiAntNum(padapter) == Ant_x1)
		BTDM_1AntLpsLeave(padapter);
}

void BTDM_ForHalt8723A(PADAPTER padapter)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bManualControl)
		return;

	if (btdm_BtWifiAntNum(padapter) == Ant_x1)
		BTDM_1AntForHalt(padapter);
}

void BTDM_WifiScanNotify8723A(PADAPTER padapter, u8 scanType)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bManualControl)
		return;

	if (btdm_BtWifiAntNum(padapter) == Ant_x1)
		BTDM_1AntWifiScanNotify(padapter, scanType);
}

void BTDM_WifiAssociateNotify8723A(PADAPTER padapter, u8 action)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bManualControl)
		return;

	if (btdm_BtWifiAntNum(padapter) == Ant_x1)
		BTDM_1AntWifiAssociateNotify(padapter, action);
}

void BTDM_MediaStatusNotify8723A(PADAPTER padapter, RT_MEDIA_STATUS	 mstatus)
{
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], MediaStatusNotify, %s\n", mstatus?"connect":"disconnect"));

	BTDM_SetFwChnlInfo(padapter, mstatus);

	if (btdm_BtWifiAntNum(padapter) == Ant_x1)
		BTDM_1AntMediaStatusNotify(padapter, mstatus);
}

void BTDM_ForDhcp8723A(PADAPTER padapter)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bManualControl)
		return;

	if (btdm_BtWifiAntNum(padapter) == Ant_x1)
		BTDM_1AntForDhcp(padapter);
}

u8 BTDM_1Ant8723A(PADAPTER padapter)
{
	if (btdm_BtWifiAntNum(padapter) == Ant_x1)
		return _TRUE;
	else
		return _FALSE;
}

void BTDM_BTCoexist8723A(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData;
	PBT30Info		pBTInfo;
	PBT_MGNT		pBtMgnt;
	PBT_COEXIST_8723A pBtCoex;


	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;
	pBtCoex = &pHalData->bt_coexist.halCoex8723;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], beacon pwdb = 0x%x(%d)\n",
		pHalData->dmpriv.EntryMinUndecoratedSmoothedPWDB,
		pHalData->dmpriv.EntryMinUndecoratedSmoothedPWDB));

	btdm_BtHwCountersMonitor(padapter);
	btdm_BtEnableDisableCheck8723A(padapter);

	if (pBtMgnt->ExtConfig.bManualControl)
	{
		RTPRINT(FBT, BT_TRACE, ("%s: Action Manual control!!\n", __FUNCTION__));
		return;
	}

	if (pBtCoex->bC2hBtInfoReqSent == _TRUE)
	{
		if (BT_IsBtDisabled(padapter) == _TRUE)
		{
			pBtCoex->c2hBtInfo = BT_INFO_STATE_DISABLED;
		}
		else
		{
			if (pBtCoex->c2hBtInfo == BT_INFO_STATE_DISABLED)
				pBtCoex->c2hBtInfo = BT_INFO_STATE_NO_CONNECTION;
		}

		btdm_BTCoexist8723AHandler(padapter);
	}
	else if (BT_IsBtDisabled(padapter) == _TRUE)
	{
		pBtCoex->c2hBtInfo = BT_INFO_STATE_DISABLED;
		btdm_BTCoexist8723AHandler(padapter);
	}

	BTDM_QueryBtInformation(padapter);
}

// ===== End of sync from SD7 driver HAL/BTCoexist/HalBtc8723.c =====
#endif

#ifdef __HALBTCCSR1ANT_C__ // HAL/BTCoexist/HalBtcCsr1Ant.c
// ===== Below this line is sync from SD7 driver HAL/BTCoexist/HalBtcCsr1Ant.c =====

//============================================================
// local function start with btdm_
//============================================================
void btdm_WriteReg860(PADAPTER padapter, u16 value)
{
	RTPRINT(FBT, BT_TRACE, ("btdm_WriteReg860(), value = 0x%x\n", value));
	PHY_SetBBReg(padapter, 0x860, bMaskLWord, value);
}

void btdm_CheckCounterOnly1Ant(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	u32 			BT_Polling, Ratio_Act, Ratio_STA;
	u32 			BT_Active, BT_State;
	u32			regBTActive = 0, regBTState = 0, regBTPolling=0;

	if (!pHalData->bt_coexist.BluetoothCoexist)
		return;
	if (pHalData->bt_coexist.BT_CoexistType != BT_CSR_BC8)
		return;
	if (pHalData->bt_coexist.BT_Ant_Num != Ant_x1)
		return;

	//
	// The following we only consider CSR BC8 and fw version should be >= 62
	//
	RTPRINT(FBT, BT_TRACE, ("[DM][BT], FirmwareVersion = 0x%x(%d)\n",
		pHalData->FirmwareVersion, pHalData->FirmwareVersion));
	{
		regBTActive = REG_BT_ACTIVE;
		regBTState = REG_BT_STATE;
		if (pHalData->FirmwareVersion >= FW_VER_BT_REG1)
			regBTPolling = REG_BT_POLLING1;
		else
			regBTPolling = REG_BT_POLLING;
	}

	BT_Active = rtw_read32(padapter, regBTActive);
	RTPRINT(FBT, BT_TRACE, ("[DM][BT], BT_Active(0x%x)=%x\n", regBTActive, BT_Active));
	BT_Active = BT_Active & 0x00ffffff;

	BT_State = rtw_read32(padapter, regBTState);
	RTPRINT(FBT, BT_TRACE, ("[DM][BT], BT_State(0x%x)=%x\n", regBTState, BT_State));
	BT_State = BT_State & 0x00ffffff;

	BT_Polling = rtw_read32(padapter, regBTPolling);
	RTPRINT(FBT, BT_TRACE, ("[DM][BT], BT_Polling(0x%x)=%x\n", regBTPolling, BT_Polling));

	Ratio_Act = BT_Active*1000/BT_Polling;
	Ratio_STA = BT_State*1000/BT_Polling;

	RTPRINT(FBT, BT_TRACE, ("[DM][BT], Ratio_Act=%d\n", Ratio_Act));
	RTPRINT(FBT, BT_TRACE, ("[DM][BT], Ratio_STA=%d\n", Ratio_STA));
}

u8
btdm_IsSingleAnt(
	PADAPTER	padapter,
	u8		bSingleAntOn,
	u8		bInterruptOn,
	u8		bMultiNAVOn
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8 bRet = _FALSE;

	if ((pHalData->bt_coexist.bInterruptOn == bInterruptOn) &&
		(pHalData->bt_coexist.bSingleAntOn == bSingleAntOn) &&
		(pHalData->bt_coexist.bMultiNAVOn == bMultiNAVOn))
	{
		bRet = _TRUE;
	}

	RTPRINT(FBT, BT_TRACE, ("[DM][BT], current SingleAntenna = [%s:%s:%s]\n",
		pHalData->bt_coexist.bSingleAntOn?"ON":"OFF",
		pHalData->bt_coexist.bInterruptOn?"ON":"OFF",
		pHalData->bt_coexist.bMultiNAVOn?"ON":"OFF"));

	return bRet;
}

u8 btdm_IsBalance(PADAPTER padapter, u8 bBalanceOn)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	RTPRINT(FBT, BT_TRACE, ("[DM][BT], btdm_IsBalance(), bBalanceOn=%s\n",
		bBalanceOn?"ON":"OFF"));

	if (pHalData->bt_coexist.bBalanceOn == bBalanceOn)
	{
		return _TRUE;
	}
	return _FALSE;
}

u8 btdm_EarphoneSpecDetect(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	switch (pHalData->bt_coexist.A2DPState)
	{
		case BT_A2DP_STATE_NOT_ENTERED:
			{
				RTPRINT(FBT, BT_TRACE, (" set default balance = ON, for WLANActH=12, WLANActL=24!!\n"));
				pHalData->bt_coexist.PreWLANActH = 12;
				pHalData->bt_coexist.PreWLANActL = 24;
				pHalData->bt_coexist.WLANActH = 12;
				pHalData->bt_coexist.WLANActL = 24;
				BTDM_Balance(padapter, _TRUE, pHalData->bt_coexist.WLANActH, pHalData->bt_coexist.WLANActL);
				BTDM_SingleAnt(padapter, _TRUE, _FALSE, _FALSE);
				pHalData->bt_coexist.A2DPState = BT_A2DP_STATE_DETECTING;
			}
			break;

		case BT_A2DP_STATE_DETECTING:
			{
				// 32,12; the most critical for BT
				// 12,24
				// 0,0
				if (btdm_IsSingleAnt(padapter, _TRUE, _FALSE, _FALSE))
				{
					if ((pHalData->bt_coexist.PreWLANActH == 0) &&
						(pHalData->bt_coexist.PreWLANActL == 0))
					{
						RTPRINT(FBT, BT_TRACE, ("[WLANActH, WLANActL] = [0,0]\n"));
						pHalData->bt_coexist.WLANActH = 12;
						pHalData->bt_coexist.WLANActL = 24;
					}
					else if ((pHalData->bt_coexist.PreWLANActH == 12) &&
						(pHalData->bt_coexist.PreWLANActL == 24))
					{
						RTPRINT(FBT, BT_TRACE, ("[WLANActH, WLANActL] = [12,24]\n"));
						if (((pHalData->bt_coexist.Ratio_Tx>600) &&
							(pHalData->bt_coexist.Ratio_PRI>500)) ||
							((pHalData->bt_coexist.Ratio_Tx*10 ) >
							(pHalData->bt_coexist.Ratio_PRI*15)))
						{
							RTPRINT(FBT, BT_TRACE, ("Ratio_Act > 600 && Ratio_STA > 500 or "));
							RTPRINT(FBT, BT_TRACE, ("Ratio_Act/Ratio_STA > 1.5\n"));
							pHalData->bt_coexist.WLANActH = 12;
							pHalData->bt_coexist.WLANActL = 24;
						}
						else
						{
							RTPRINT(FBT, BT_TRACE, (" cosa set to 32/12\n "));
							pHalData->bt_coexist.WLANActH = 32;
							pHalData->bt_coexist.WLANActL = 12;
						}
					}
					else if ((pHalData->bt_coexist.PreWLANActH == 32) &&
							(pHalData->bt_coexist.PreWLANActL == 12))
					{
						RTPRINT(FBT, BT_TRACE, ("[WLANActH, WLANActL] = [32,12]\n"));
						if (((pHalData->bt_coexist.Ratio_Tx>650) &&
							(pHalData->bt_coexist.Ratio_PRI>550)) ||
							((pHalData->bt_coexist.Ratio_Tx*10 ) >
							(pHalData->bt_coexist.Ratio_PRI*15)))
						{
							RTPRINT(FBT, BT_TRACE, ("Ratio_Act > 650 && Ratio_STA > 550 or "));
							RTPRINT(FBT, BT_TRACE, ("Ratio_Act/Ratio_STA > 1.5\n"));
							pHalData->bt_coexist.WLANActH = 12;
							pHalData->bt_coexist.WLANActL = 24;
						}
					}
					if ((pHalData->bt_coexist.PreWLANActH != pHalData->bt_coexist.WLANActH) ||
						(pHalData->bt_coexist.PreWLANActL != pHalData->bt_coexist.WLANActL))
					{
						BTDM_Balance(padapter, _TRUE, pHalData->bt_coexist.WLANActH, pHalData->bt_coexist.WLANActL);
						pHalData->bt_coexist.PreWLANActH = pHalData->bt_coexist.WLANActH;
						pHalData->bt_coexist.PreWLANActL = pHalData->bt_coexist.WLANActL;
					}
				}

				RTPRINT(FBT, BT_TRACE, ("earphone detected result: WLANActH=%d, WLANActL=%d\n",
					pHalData->bt_coexist.WLANActH, pHalData->bt_coexist.WLANActL));
			}
			break;

		case BT_A2DP_STATE_DETECTED:
			break;

		default:
			RT_ASSERT(_FALSE, ("btdm_EarphoneSpecDetect(), unknown case\n"));
			break;
	}
	return _TRUE;
}

//==============================================================
//
// Note:
// In the following, FW should be done before SW mechanism.
//
//==============================================================

void btdm_SCOActionBC81Ant(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8	btRssiState;

	if ((pmlmepriv->LinkDetectInfo.bTxBusyTraffic) ||
		!(pmlmepriv->LinkDetectInfo.bBusyTraffic))
	{
		RTPRINT(FBT, BT_TRACE, ("Wifi Uplink or Wifi is idle\n"));
		if (BTDM_IsSameCoexistState(padapter))
				return;
		BTDM_Balance(padapter, _FALSE, 0, 0);
		BTDM_SingleAnt(padapter, _TRUE, _TRUE, _FALSE);
	}
	else if (pmlmepriv->LinkDetectInfo.bRxBusyTraffic)
	{
		RTPRINT(FBT, BT_TRACE, ("Wifi Downlink\n"));

		if (btdm_IsSingleAnt(padapter, _FALSE, _FALSE, _FALSE))
		{
			btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_20, 0);
			if (BTDM_IsSameCoexistState(padapter))
				return;
			if ((btRssiState == BT_RSSI_STATE_LOW) ||
				(btRssiState == BT_RSSI_STATE_STAY_LOW))
			{
				BTDM_Balance(padapter, _FALSE, 0, 0);
				BTDM_SingleAnt(padapter, _TRUE, _TRUE, _FALSE);
			}
		}
		else
		{
			btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_45, 0);
			if (BTDM_IsSameCoexistState(padapter))
				return;
			if ((btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH))
			{
				BTDM_Balance(padapter, _FALSE, 0, 0);
				BTDM_SingleAnt(padapter, _FALSE, _FALSE, _FALSE);
			}
			else
			{
				BTDM_Balance(padapter, _FALSE, 0, 0);
				BTDM_SingleAnt(padapter, _TRUE, _TRUE, _FALSE);
			}
		}
	}
}

u8 btdm_SCOAction1Ant(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.NumberOfSCO > 0)
	{
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_SCO;
		btdm_SCOActionBC81Ant(padapter);
		return _TRUE;
	}
	else
	{
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_PROFILE_SCO;
		return _FALSE;
	}
}

void btdm_HIDActionBC81Ant(PADAPTER padapter)
{
#if 0
	if (BTDM_IsSameCoexistState(padapter))
		return;
#endif
	BTDM_Balance(padapter, _FALSE, 0, 0);
	BTDM_SingleAnt(padapter, _TRUE, _TRUE, _FALSE);
}

u8 btdm_HIDAction1Ant(PADAPTER padapter)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter)	;
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (BTHCI_CheckProfileExist(padapter,BT_PROFILE_HID) && pBtMgnt->ExtConfig.NumberOfHandle==1)
	{
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_HID;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_PROFILE_PAN;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_PROFILE_A2DP;
		btdm_HIDActionBC81Ant(padapter);
		return _TRUE;
	}
	else
	{
		return _FALSE;
	}
}

void btdm_A2DPActionBC81Ant(PADAPTER padapter)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			btRssiState;

	if (pBtMgnt->ExtConfig.bBTBusy)
	{
		RTPRINT(FBT, BT_TRACE, ("BT is non-idle!\n"));

		// We have to detect BT earphone spec first.
		btdm_EarphoneSpecDetect(padapter);

		if (!BTDM_IsHT40(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
			if (btdm_IsSingleAnt(padapter, _FALSE, _FALSE, _FALSE))
			{
				btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_30, 0);
				if (BTDM_IsSameCoexistState(padapter))
					return;

				if ((btRssiState == BT_RSSI_STATE_LOW) ||
					(btRssiState == BT_RSSI_STATE_STAY_LOW))
				{
					BTDM_Balance(padapter, _TRUE, pHalData->bt_coexist.WLANActH, pHalData->bt_coexist.WLANActL);
					BTDM_SingleAnt(padapter, _TRUE, _FALSE, _FALSE);
				}
			}
			else
			{
				btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_55, 0);
				if (BTDM_IsSameCoexistState(padapter))
					return;

				if ((btRssiState == BT_RSSI_STATE_HIGH) ||
					(btRssiState == BT_RSSI_STATE_STAY_HIGH))
				{
					BTDM_Balance(padapter, _FALSE, 0, 0);
					BTDM_SingleAnt(padapter, _FALSE, _FALSE, _FALSE);
				}
				else
				{
					BTDM_Balance(padapter, _TRUE, pHalData->bt_coexist.WLANActH, pHalData->bt_coexist.WLANActL);
					BTDM_SingleAnt(padapter, _TRUE, _FALSE, _FALSE);
				}
			}
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("HT40\n"));
			if (BTDM_IsSameCoexistState(padapter))
				return;
			BTDM_Balance(padapter, _TRUE, pHalData->bt_coexist.WLANActH, pHalData->bt_coexist.WLANActL);
			BTDM_SingleAnt(padapter, _TRUE, _FALSE, _FALSE);
		}
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("BT is idle!\n"));
		pHalData->bt_coexist.A2DPState = BT_A2DP_STATE_NOT_ENTERED;
		if (pHalData->bt_coexist.Ratio_PRI > 3)
		{
			RTPRINT(FBT, BT_TRACE, ("Ratio_STA > 3\n"));
		BTDM_Balance(padapter, _FALSE, 0, 0);
		BTDM_SingleAnt(padapter, _TRUE, _TRUE, _FALSE);
	}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("Ratio_STA <= 3\n"));
			BTDM_Balance(padapter, _TRUE, 32, 5);
			BTDM_SingleAnt(padapter, _TRUE, _FALSE, _FALSE);
		}
	}
}

u8 btdm_A2DPAction1Ant(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT30Info		pBTInfo = GET_BT_INFO(padapter)	;
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_A2DP) && pBtMgnt->ExtConfig.NumberOfHandle==1)
	{
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_A2DP;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_PROFILE_HID;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_PROFILE_PAN;
		btdm_A2DPActionBC81Ant(padapter);
		return _TRUE;
	}
	else
	{
		return _FALSE;
	}
}

void btdm_PANActionBC81Ant(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter)	;
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			btRssiState;

	if (pBtMgnt->ExtConfig.bBTBusy && !pBtMgnt->BtOperationOn)
	{
		RTPRINT(FBT, BT_TRACE, ("BT is non-idle && [BT 2.1]\n"));

		if (!BTDM_IsHT40(padapter))
			{
				RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
				if (btdm_IsSingleAnt(padapter, _FALSE, _FALSE, _FALSE))
				{
				btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_20, 0);
					if (BTDM_IsSameCoexistState(padapter))
						return;

					if ((btRssiState == BT_RSSI_STATE_LOW) ||
						(btRssiState == BT_RSSI_STATE_STAY_LOW))
					{
					BTDM_Balance(padapter, _TRUE, 0x1c, 0x20);
					BTDM_SingleAnt(padapter, _TRUE, _FALSE, _FALSE);
					}
				}
				else
				{
					btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_50, 0);
					if (BTDM_IsSameCoexistState(padapter))
						return;
					if ((btRssiState == BT_RSSI_STATE_HIGH) ||
						(btRssiState == BT_RSSI_STATE_STAY_HIGH))
					{
						BTDM_Balance(padapter, _FALSE, 0, 0);
						BTDM_SingleAnt(padapter, _FALSE, _FALSE, _FALSE);
					}
					else
					{
					BTDM_Balance(padapter, _TRUE, 0x1c, 0x20);
					BTDM_SingleAnt(padapter, _TRUE, _FALSE, _FALSE);
				}
			}
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("HT40\n"));
			if ((pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic) &&
				(pmlmepriv->LinkDetectInfo.bTxBusyTraffic))
			{
				RTPRINT(FBT, BT_TRACE, ("BT is Downlink and Wifi is Uplink\n"));
				if (btdm_IsSingleAnt(padapter, _FALSE, _FALSE, _FALSE))
				{
					btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_20, 0);
					if (BTDM_IsSameCoexistState(padapter))
						return;
					if ((btRssiState == BT_RSSI_STATE_LOW) ||
						(btRssiState == BT_RSSI_STATE_STAY_LOW))
					{
						BTDM_Balance(padapter, _TRUE, 0x1c, 0x20);
						BTDM_SingleAnt(padapter, _TRUE, _FALSE, _FALSE);
					}
				}
				else
				{
					btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_45, 0);
					if (BTDM_IsSameCoexistState(padapter))
						return;
					if ((btRssiState == BT_RSSI_STATE_HIGH) ||
						(btRssiState == BT_RSSI_STATE_STAY_HIGH))
					{
						BTDM_Balance(padapter, _FALSE, 0, 0);
						BTDM_SingleAnt(padapter, _FALSE, _FALSE, _FALSE);
					}
					else
					{
						BTDM_Balance(padapter, _TRUE, 0x1c, 0x20);
						BTDM_SingleAnt(padapter, _TRUE, _FALSE, _FALSE);
					}
				}
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("BT Uplink or BTdownlink+Wifi downlink\n"));
				BTDM_Balance(padapter, _TRUE, 0x1c, 0x20);
				BTDM_SingleAnt(padapter, _TRUE, _FALSE, _FALSE);
			}
		}
	}
	else if (pBtMgnt->ExtConfig.bBTBusy && pBtMgnt->BtOperationOn)
	{
		RTPRINT(FBT, BT_TRACE, ("BT is non-idle && [BT 3.0]\n"));
		BTDM_FWCoexAllOff(padapter);
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("BT is idle\n"));
		BTDM_Balance(padapter, _TRUE, 32, 5);
		BTDM_SingleAnt(padapter, _TRUE, _FALSE, _FALSE);
	}
}

u8 btdm_PANAction1Ant(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	if (BTHCI_CheckProfileExist(padapter,BT_PROFILE_PAN) && pBtMgnt->ExtConfig.NumberOfHandle==1)
	{
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_PAN;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_PROFILE_HID;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_PROFILE_A2DP;
		btdm_PANActionBC81Ant(padapter);
		return _TRUE;
	}
	else
	{
		return _FALSE;
	}
}

void btdm_HIDA2DPActionBC81Ant(PADAPTER padapter)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bBTBusy)
	{
		RTPRINT(FBT, BT_TRACE, ("BT is non-idle!\n"));
		BTDM_Balance(padapter, _TRUE, 0x5, 0x1a);
		BTDM_SingleAnt(padapter, _TRUE, _FALSE, _FALSE);
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("BT is idle!\n"));
		BTDM_Balance(padapter, _FALSE, 0, 0);
		BTDM_SingleAnt(padapter, _TRUE, _TRUE, _FALSE);
	}
}

u8 btdm_HIDA2DPAction1Ant(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (BTHCI_CheckProfileExist(padapter,BT_PROFILE_HID) && BTHCI_CheckProfileExist(padapter,BT_PROFILE_A2DP))
	{
		pHalData->bt_coexist.CurrentState |= (BT_COEX_STATE_PROFILE_HID|BT_COEX_STATE_PROFILE_A2DP);
		btdm_HIDA2DPActionBC81Ant(padapter);
		return _TRUE;
	}
	else
	{
		pHalData->bt_coexist.CurrentState &= ~(BT_COEX_STATE_PROFILE_HID|BT_COEX_STATE_PROFILE_A2DP);
		return _FALSE;
	}
}

void btdm_HIDPANActionBC81Ant(PADAPTER padapter)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	if ((pBtMgnt->ExtConfig.bBTBusy && !pBtMgnt->BtOperationOn))
	{
		RTPRINT(FBT, BT_TRACE, ("BT is non-idle && [BT 2.1]\n"));
		BTDM_Balance(padapter, _TRUE, 0x5, 0x1a);
		BTDM_SingleAnt(padapter, _TRUE, _FALSE, _FALSE);
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("BT is idle or [BT 3.0]\n"));
		BTDM_Balance(padapter, _FALSE, 0, 0);
		BTDM_SingleAnt(padapter, _TRUE, _TRUE, _FALSE);
	}
}

u8 btdm_HIDPANAction1Ant(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (BTHCI_CheckProfileExist(padapter,BT_PROFILE_HID) && BTHCI_CheckProfileExist(padapter,BT_PROFILE_PAN))
	{
		pHalData->bt_coexist.CurrentState |= (BT_COEX_STATE_PROFILE_HID|BT_COEX_STATE_PROFILE_PAN);
		btdm_HIDPANActionBC81Ant(padapter);
		return _TRUE;
	}
	else
	{
		pHalData->bt_coexist.CurrentState &= ~(BT_COEX_STATE_PROFILE_HID|BT_COEX_STATE_PROFILE_PAN);
		return _FALSE;
	}
}

void btdm_PANA2DPActionBC81Ant(PADAPTER padapter)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	if ((pBtMgnt->ExtConfig.bBTBusy && !pBtMgnt->BtOperationOn))
	{
		RTPRINT(FBT, BT_TRACE, ("BT is non-idle && [BT 2.1]\n"));
		BTDM_Balance(padapter, _TRUE, 0x5, 0x1a);
		BTDM_SingleAnt(padapter, _TRUE, _FALSE, _FALSE);
	}
	else if ((pBtMgnt->ExtConfig.bBTBusy && pBtMgnt->BtOperationOn))
	{
		RTPRINT(FBT, BT_TRACE, ("BT is non-idle && [BT 3.0]\n"));
		btdm_A2DPActionBC81Ant(padapter);
	}
	else
	{
		BTDM_Balance(padapter, _FALSE, 0, 0);
		BTDM_SingleAnt(padapter, _TRUE, _TRUE, _FALSE);
	}
}

u8 btdm_PANA2DPAction1Ant(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (BTHCI_CheckProfileExist(padapter,BT_PROFILE_PAN) && BTHCI_CheckProfileExist(padapter,BT_PROFILE_A2DP))
	{
		pHalData->bt_coexist.CurrentState |= (BT_COEX_STATE_PROFILE_PAN|BT_COEX_STATE_PROFILE_A2DP);
		btdm_PANA2DPActionBC81Ant(padapter);
		return _TRUE;
	}
	else
	{
		pHalData->bt_coexist.CurrentState &= ~(BT_COEX_STATE_PROFILE_PAN|BT_COEX_STATE_PROFILE_A2DP);
		return _FALSE;
	}
}

//============================================================
// extern function start with BTDM_
//============================================================

void BTDM_SetAntenna(PADAPTER padapter, u8 who)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	if (!IS_HARDWARE_TYPE_8192C(padapter))
		return;
	if (!pHalData->bt_coexist.BluetoothCoexist)
		return;
	if (pBtMgnt->ExtConfig.bManualControl)
		return;
	if (pHalData->bt_coexist.BT_CoexistType != BT_CSR_BC8)
		return;
	if (pHalData->bt_coexist.BT_Ant_Num != Ant_x1)
		return;
//	if (pHalData->bt_coexist.AntennaState == who)
//		return;

	switch (who)
	{
		case BTDM_ANT_BT_IDLE:
			RTPRINT(FBT, BT_TRACE, ("BTDM_SetAntenna(), BTDM_ANT_BT_IDLE\n"));
			BTDM_Balance(padapter, _FALSE, 0, 0);
			BTDM_SingleAnt(padapter, _TRUE, _TRUE, _FALSE);
			pHalData->bt_coexist.AntennaState = BTDM_ANT_BT_IDLE;
			break;

		case BTDM_ANT_WIFI:
			RTPRINT(FBT, BT_TRACE, ("BTDM_SetAntenna(), BTDM_ANT_WIFI\n"));
			BTDM_Balance(padapter, _FALSE, 0, 0);
			BTDM_SingleAnt(padapter, _FALSE, _FALSE, _FALSE);
			rtw_mdelay_os(3);	// 1 will fail, 2 ok
			btdm_WriteReg860(padapter, 0x130);
			pHalData->bt_coexist.AntennaState = BTDM_ANT_WIFI;
			break;

		case BTDM_ANT_BT:
			RTPRINT(FBT, BT_TRACE, ("BTDM_SetAntenna(), BTDM_ANT_BT\n"));
			BTDM_Balance(padapter, _FALSE, 0, 0);
			BTDM_SingleAnt(padapter, _FALSE, _FALSE, _FALSE);
			//btdm_WriteReg860(padapter, 0x230);
			pHalData->bt_coexist.AntennaState = BTDM_ANT_BT;
			break;

		default:
			RT_ASSERT(_FALSE, ("BTDM_SetAntenna(), error case\n"));
			break;
	}
}

void
BTDM_SingleAnt(
	PADAPTER	padapter,
	u8		bSingleAntOn,
	u8		bInterruptOn,
	u8		bMultiNAVOn
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			H2C_Parameter[3] = {0};

	if (pHalData->bt_coexist.BT_Ant_Num != Ant_x1)
		return;

	H2C_Parameter[2] = 0;
	H2C_Parameter[1] = 0;
	H2C_Parameter[0] = 0;

	if (bInterruptOn)
	{
		H2C_Parameter[2] |= 0x02;	//BIT1
		pHalData->bt_coexist.bFWCoexistAllOff = _FALSE;
	}
	pHalData->bt_coexist.bInterruptOn = bInterruptOn;

	if (bSingleAntOn)
	{
		H2C_Parameter[2] |= 0x10;	//BIT4
		pHalData->bt_coexist.bFWCoexistAllOff = _FALSE;
	}
	pHalData->bt_coexist.bSingleAntOn = bSingleAntOn;

	if (bMultiNAVOn)
	{
		H2C_Parameter[2] |= 0x20;	//BIT5
		pHalData->bt_coexist.bFWCoexistAllOff = _FALSE;
	}
	pHalData->bt_coexist.bMultiNAVOn = bMultiNAVOn;

	RTPRINT(FBT, BT_TRACE, ("[DM][BT], SingleAntenna=[%s:%s:%s], write 0xe = 0x%x\n",
		bSingleAntOn?"ON":"OFF", bInterruptOn?"ON":"OFF", bMultiNAVOn?"ON":"OFF",
		H2C_Parameter[0]<<16|H2C_Parameter[1]<<8|H2C_Parameter[2]));

	if (IS_HARDWARE_TYPE_8192C(padapter))
	{
		FillH2CCmd(padapter, 0xe, 3, H2C_Parameter);
	}
	else if (IS_HARDWARE_TYPE_8192D(padapter))
	{
		FillH2CCmd(padapter, 0x12, 3, H2C_Parameter);
	}
}

void BTDM_CheckBTIdleChange1Ant(PADAPTER padapter)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	u8				stateChange = _FALSE;
	u32 			BT_Polling, Ratio_Act, Ratio_STA;
	u32				BT_Active, BT_State;
	u32				regBTActive=0, regBTState=0, regBTPolling=0;

	if (!pHalData->bt_coexist.BluetoothCoexist)
		return;
	if (pBtMgnt->ExtConfig.bManualControl)
		return;
	if (pHalData->bt_coexist.BT_CoexistType != BT_CSR_BC8)
		return;
	if (pHalData->bt_coexist.BT_Ant_Num != Ant_x1)
		return;

	//
	// The following we only consider CSR BC8 and fw version should be >= 62
	//
	RTPRINT(FBT, BT_TRACE, ("[DM][BT], FirmwareVersion = 0x%x(%d)\n",
	pHalData->FirmwareVersion, pHalData->FirmwareVersion));
	{
		regBTActive = REG_BT_ACTIVE;
		regBTState = REG_BT_STATE;
		if (pHalData->FirmwareVersion >= FW_VER_BT_REG1)
			regBTPolling = REG_BT_POLLING1;
		else
			regBTPolling = REG_BT_POLLING;
	}

	BT_Active = rtw_read32(padapter, regBTActive);
	RTPRINT(FBT, BT_TRACE, ("[DM][BT], BT_Active(0x%x)=%x\n", regBTActive, BT_Active));
	BT_Active = BT_Active & 0x00ffffff;

	BT_State = rtw_read32(padapter, regBTState);
	RTPRINT(FBT, BT_TRACE, ("[DM][BT], BT_State(0x%x)=%x\n", regBTState, BT_State));
	BT_State = BT_State & 0x00ffffff;

	BT_Polling = rtw_read32(padapter, regBTPolling);
	RTPRINT(FBT, BT_TRACE, ("[DM][BT], BT_Polling(0x%x)=%x\n", regBTPolling, BT_Polling));

	if (BT_Active==0xffffffff && BT_State==0xffffffff && BT_Polling==0xffffffff )
		return;
	if (BT_Polling == 0)
		return;

	Ratio_Act = BT_Active*1000/BT_Polling;
	Ratio_STA = BT_State*1000/BT_Polling;

	pHalData->bt_coexist.Ratio_Tx = Ratio_Act;
	pHalData->bt_coexist.Ratio_PRI = Ratio_STA;

	RTPRINT(FBT, BT_TRACE, ("[DM][BT], Ratio_Act=%d\n", Ratio_Act));
	RTPRINT(FBT, BT_TRACE, ("[DM][BT], Ratio_STA=%d\n", Ratio_STA));

	if (Ratio_STA<60 && Ratio_Act<500)	// BT PAN idle
	{
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_PAN_IDLE;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_PAN_DOWNLINK;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_PAN_UPLINK;
	}
	else
	{
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_PAN_IDLE;

		if (Ratio_STA)
		{
			// Check if BT PAN (under BT 2.1) is uplink or downlink
			if ((Ratio_Act/Ratio_STA) < 2)
			{
				// BT PAN Uplink
				pHalData->bt_coexist.BT21TrafficStatistics.bTxBusyTraffic = _TRUE;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_PAN_UPLINK;
				pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic = _FALSE;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_PAN_DOWNLINK;
			}
			else
			{
				// BT PAN downlink
				pHalData->bt_coexist.BT21TrafficStatistics.bTxBusyTraffic = _FALSE;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_PAN_UPLINK;
				pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic = _TRUE;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_PAN_DOWNLINK;
			}
		}
		else
		{
			// BT PAN downlink
			pHalData->bt_coexist.BT21TrafficStatistics.bTxBusyTraffic = _FALSE;
			pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_PAN_UPLINK;
			pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic = _TRUE;
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_PAN_DOWNLINK;
		}
	}

	// Check BT is idle or not
	if (pBtMgnt->ExtConfig.NumberOfHandle==0 &&
		pBtMgnt->ExtConfig.NumberOfSCO==0)
	{
		pBtMgnt->ExtConfig.bBTBusy = _FALSE;
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_IDLE;
	}
	else
	{
		if (Ratio_STA<60)
		{
			pBtMgnt->ExtConfig.bBTBusy = _FALSE;
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_IDLE;
		}
		else
		{
			pBtMgnt->ExtConfig.bBTBusy = _TRUE;
			pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_IDLE;
		}
	}

	if (pBtMgnt->ExtConfig.NumberOfHandle==0 &&
		pBtMgnt->ExtConfig.NumberOfSCO==0)
	{
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_RSSI_LOW;
		pBtMgnt->ExtConfig.MIN_BT_RSSI = 0;
		BTDM_SetAntenna(padapter, BTDM_ANT_BT_IDLE);
	}
	else
	{
		if (pBtMgnt->ExtConfig.MIN_BT_RSSI <= -5)
		{
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_RSSI_LOW;
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], core stack notify bt rssi Low\n"));
		}
		else
		{
			pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_RSSI_LOW;
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], core stack notify bt rssi Normal\n"));
		}
	}

	if (pHalData->bt_coexist.bBTBusyTraffic !=
		pBtMgnt->ExtConfig.bBTBusy)
	{	// BT idle or BT non-idle
		pHalData->bt_coexist.bBTBusyTraffic = pBtMgnt->ExtConfig.bBTBusy;
		stateChange = _TRUE;
	}

	if (stateChange)
	{
		if (!pBtMgnt->ExtConfig.bBTBusy)
		{
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], BT is idle or disable\n"));
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], BT is non-idle\n"));
		}
	}
	if (!pBtMgnt->ExtConfig.bBTBusy)
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], BT is idle or disable\n"));
#if 0
		if (MgntRoamingInProgress(pMgntInfo) ||
			MgntIsLinkInProgress(pMgntInfo) ||
			MgntScanInProgress(pMgntInfo))
#else
		if (check_fwstate(&padapter->mlmepriv, WIFI_UNDER_LINKING|WIFI_SITE_MONITOR) == _TRUE)
#endif
		{
			BTDM_SetAntenna(padapter, BTDM_ANT_WIFI);
		}
	}
}

void BTDM_BTCoexistWithProfile1Ant(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bManualControl)
	{
		btdm_CheckCounterOnly1Ant(padapter);
		return;
	}

	RTPRINT(FIOCTL, IOCTL_BT_FLAG_MON, ("CurrentBTConnectionCnt=%d, BtOperationOn=%d, bBTConnectInProgress=%d !!\n",
		pBtMgnt->CurrentBTConnectionCnt, pBtMgnt->BtOperationOn, pBtMgnt->bBTConnectInProgress));

	if ((pHalData->bt_coexist.BluetoothCoexist) &&
		(pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC8))
	{
		BTHCI_GetProfileNameMoto(padapter);
		BTHCI_GetBTRSSI(padapter);
		BTDM_CheckBTIdleChange1Ant(padapter);
		BTDM_CheckWiFiState(padapter);

		if (btdm_SCOAction1Ant(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], Action SCO\n"));
		}
		else if (btdm_HIDAction1Ant(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], Action HID\n"));
		}
		else if (btdm_A2DPAction1Ant(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], Action A2DP\n"));
		}
		else if (btdm_PANAction1Ant(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], Action PAN\n"));
		}
		else if (btdm_HIDA2DPAction1Ant(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], Action HID_A2DP\n"));
		}
		else if (btdm_HIDPANAction1Ant(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], Action HID_PAN\n"));
		}
		else if (btdm_PANA2DPAction1Ant(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], Action PAN_A2DP\n"));
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], No Action case!!!\n"));
		}

		if (!BTDM_IsSameCoexistState(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], Coexist State[bitMap] change from 0x%"i64fmt"x to 0x%"i64fmt"x\n",
				pHalData->bt_coexist.PreviousState,
				pHalData->bt_coexist.CurrentState));
			pHalData->bt_coexist.PreviousState = pHalData->bt_coexist.CurrentState;

			RTPRINT(FBT, BT_TRACE, ("["));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_BT30)
				RTPRINT(FBT, BT_TRACE, ("BT 3.0, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_HT20)
				RTPRINT(FBT, BT_TRACE, ("HT20, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_HT40)
				RTPRINT(FBT, BT_TRACE, ("HT40, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_LEGACY)
				RTPRINT(FBT, BT_TRACE, ("Legacy, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_RSSI_LOW)
				RTPRINT(FBT, BT_TRACE, ("Rssi_Low, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_RSSI_MEDIUM)
				RTPRINT(FBT, BT_TRACE, ("Rssi_Mid, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_RSSI_HIGH)
				RTPRINT(FBT, BT_TRACE, ("Rssi_High, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_IDLE)
				RTPRINT(FBT, BT_TRACE, ("Wifi_Idle, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_UPLINK)
				RTPRINT(FBT, BT_TRACE, ("Wifi_Uplink, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_DOWNLINK)
				RTPRINT(FBT, BT_TRACE, ("Wifi_Downlink, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_BT_IDLE)
				RTPRINT(FBT, BT_TRACE, ("BT_idle, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_BT_PAN_IDLE)
				RTPRINT(FBT, BT_TRACE, ("BT_PAN_idle, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_BT_PAN_UPLINK)
				RTPRINT(FBT, BT_TRACE, ("BT_PAN_uplink, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_BT_PAN_DOWNLINK)
				RTPRINT(FBT, BT_TRACE, ("BT_PAN_downlink, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_PROFILE_HID)
				RTPRINT(FBT, BT_TRACE, ("PRO_HID, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_PROFILE_A2DP)
				RTPRINT(FBT, BT_TRACE, ("PRO_A2DP, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_PROFILE_PAN)
				RTPRINT(FBT, BT_TRACE, ("PRO_PAN, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_PROFILE_SCO)
				RTPRINT(FBT, BT_TRACE, ("PRO_SCO, "));
			RTPRINT(FBT, BT_TRACE, ("]\n"));
		}
	}
}

// ===== End of sync from SD7 driver HAL/BTCoexist/HalBtcCsr1Ant.c =====
#endif

#ifdef __HALBTCCSR2ANT_C__ // HAL/BTCoexist/HalBtcCsr2Ant.c
// ===== Below this line is sync from SD7 driver HAL/BTCoexist/HalBtcCsr2Ant.c =====

//============================================================
// local function start with btdm_
//============================================================
void
btdm_BtEnableDisableCheck(
	PADAPTER	padapter,
	u32		BT_Active
	)
{
	// This function check if 92D bt is disabled
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (IS_HARDWARE_TYPE_8192D(padapter))
	{
		if (BT_Active)
		{
			pHalData->bt_coexist.btActiveZeroCnt = 0;
			pHalData->bt_coexist.bCurBtDisabled = _FALSE;
			RTPRINT(FBT, BT_TRACE, ("92D Bt is enabled !!\n"));
		}
		else
		{
			pHalData->bt_coexist.btActiveZeroCnt++;
			RTPRINT(FBT, BT_TRACE, ("92D BT_Active = 0, cnt = %d!!\n",
					pHalData->bt_coexist.btActiveZeroCnt));
			if (pHalData->bt_coexist.btActiveZeroCnt >= 2)
			{
				pHalData->bt_coexist.bCurBtDisabled = _TRUE;
				RTPRINT(FBT, BT_TRACE, ("92D Bt is disabled !!\n"));
			}
		}
		if (pHalData->bt_coexist.bPreBtDisabled !=
			pHalData->bt_coexist.bCurBtDisabled )
		{
			RTPRINT(FBT, BT_TRACE, ("92D Bt is from %s to %s!!\n",
				(pHalData->bt_coexist.bPreBtDisabled ? "disabled":"enabled"),
				(pHalData->bt_coexist.bCurBtDisabled ? "disabled":"enabled")));
			pHalData->bt_coexist.bNeedToRoamForBtDisableEnable = _TRUE;
			pHalData->bt_coexist.bPreBtDisabled = pHalData->bt_coexist.bCurBtDisabled;
		}
	}
}

void btdm_CheckBTState2Ant(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
//	PRT_HIGH_THROUGHPUT	pHTInfo = GET_HT_INFO(pMgntInfo);
	u8			stateChange = _FALSE;
	u32 			BT_Polling, Ratio_Act, Ratio_STA;
	u32 			BT_Active, BT_State;
	u32			regBTActive = 0, regBTState = 0, regBTPolling=0;
	u32			btBusyThresh = 0;

	RTPRINT(FBT, BT_TRACE, ("FirmwareVersion = 0x%x(%d)\n",
	pHalData->FirmwareVersion, pHalData->FirmwareVersion));

	if (IS_HARDWARE_TYPE_8192C(padapter))
	{
		if (pHalData->FirmwareVersion < FW_VER_BT_REG)
		{
			regBTActive = REG_BT_ACTIVE_OLD;
			regBTState = REG_BT_STATE_OLD;
			regBTPolling = REG_BT_POLLING_OLD;
		}
		else
		{
			regBTActive = REG_BT_ACTIVE;
			regBTState = REG_BT_STATE;
			if (pHalData->FirmwareVersion >= FW_VER_BT_REG1)
				regBTPolling = REG_BT_POLLING1;
			else
				regBTPolling = REG_BT_POLLING;
		}
	}
	else if (IS_HARDWARE_TYPE_8192D(padapter))
	{
		regBTActive = REG_BT_ACTIVE;
		regBTState = REG_BT_STATE;
		regBTPolling = REG_BT_POLLING1;
	}

	if (IS_HARDWARE_TYPE_8192D(padapter))
	{
		btBusyThresh = 40;
	}
	else
	{
		btBusyThresh = 60;
	}

	BT_Active = rtw_read32(padapter, regBTActive);
	RTPRINT(FBT, BT_TRACE, ("BT_Active(0x%x)=%x\n", regBTActive, BT_Active));
	BT_Active = BT_Active & 0x00ffffff;

	BT_State = rtw_read32(padapter, regBTState);
	RTPRINT(FBT, BT_TRACE, ("BT_State(0x%x)=%x\n", regBTState, BT_State));
	BT_State = BT_State & 0x00ffffff;

	BT_Polling = rtw_read32(padapter, regBTPolling);
	RTPRINT(FBT, BT_TRACE, ("BT_Polling(0x%x)=%x\n", regBTPolling, BT_Polling));

	if (BT_Active==0xffffffff && BT_State==0xffffffff && BT_Polling==0xffffffff )
		return;

	// 2011/05/04 MH For Slim combo test meet a problem. Surprise remove and WLAN is running
	// DHCP process. At the same time, the register read value might be zero. And cause BSOD 0x7f
	// EXCEPTION_DIVIDED_BY_ZERO. In This case, the stack content may always be wrong due to
	// HW divide trap.
	if (BT_Polling==0)
		return;

	btdm_BtEnableDisableCheck(padapter, BT_Active);

	Ratio_Act = BT_Active*1000/BT_Polling;
	Ratio_STA = BT_State*1000/BT_Polling;

	pHalData->bt_coexist.Ratio_Tx = Ratio_Act;
	pHalData->bt_coexist.Ratio_PRI = Ratio_STA;

	RTPRINT(FBT, BT_TRACE, ("Ratio_Act=%d\n", Ratio_Act));
	RTPRINT(FBT, BT_TRACE, ("Ratio_STA=%d\n", Ratio_STA));

	if (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC8)
	{
		if (Ratio_STA < 60)	// BT PAN idle
		{
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_PAN_IDLE;
			pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_PAN_DOWNLINK;
			pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_PAN_UPLINK;
		}
		else
		{
			pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_PAN_IDLE;

			// Check if BT PAN (under BT 2.1) is uplink or downlink
			if ((Ratio_Act/Ratio_STA) < 2)
			{	// BT PAN Uplink
				pHalData->bt_coexist.BT21TrafficStatistics.bTxBusyTraffic = _TRUE;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_PAN_UPLINK;
				pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic = _FALSE;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_PAN_DOWNLINK;
			}
			else
			{	// BT PAN downlink
				pHalData->bt_coexist.BT21TrafficStatistics.bTxBusyTraffic = _FALSE;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_PAN_UPLINK;
				pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic = _TRUE;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_PAN_DOWNLINK;
			}
		}
	}
	else
	{
		// BC4, doesn't use the following variables.
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_PAN_IDLE;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_PAN_DOWNLINK;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_PAN_UPLINK;
	}


	// Check BT is idle or not
	if (pBtMgnt->ExtConfig.NumberOfHandle==0 &&
		pBtMgnt->ExtConfig.NumberOfSCO==0)
	{
		pBtMgnt->ExtConfig.bBTBusy = _FALSE;
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_IDLE;
	}
	else
	{
		if (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC4)
		{
			if (Ratio_Act < 20)
			{
				pBtMgnt->ExtConfig.bBTBusy = _FALSE;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_IDLE;
			}
			else
			{
				pBtMgnt->ExtConfig.bBTBusy = _TRUE;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_IDLE;
			}
		}
		else if (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC8)
		{
			if (Ratio_STA < btBusyThresh)
			{
				pBtMgnt->ExtConfig.bBTBusy = _FALSE;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_IDLE;
			}
			else
			{
				pBtMgnt->ExtConfig.bBTBusy = _TRUE;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_IDLE;
			}

			if ((Ratio_STA < btBusyThresh) ||
				(Ratio_Act<180 && Ratio_STA<130))
			{
				pBtMgnt->ExtConfig.bBTA2DPBusy = _FALSE;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_A2DP_IDLE;
			}
			else
			{
				pBtMgnt->ExtConfig.bBTA2DPBusy =_TRUE;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_A2DP_IDLE;
			}
		}
	}

	if (pBtMgnt->ExtConfig.NumberOfHandle==0 &&
		pBtMgnt->ExtConfig.NumberOfSCO==0)
	{
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_RSSI_LOW;
		pBtMgnt->ExtConfig.MIN_BT_RSSI = 0;
	}
	else
	{
		if (pBtMgnt->ExtConfig.MIN_BT_RSSI <= -5)
		{
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_RSSI_LOW;
			RTPRINT(FBT, BT_TRACE, ("[bt rssi], Low\n"));
		}
		else
		{
			pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_RSSI_LOW;
			RTPRINT(FBT, BT_TRACE, ("[bt rssi], Normal\n"));
		}
	}

	if (pHalData->bt_coexist.bBTBusyTraffic !=
		pBtMgnt->ExtConfig.bBTBusy)
	{	// BT idle or BT non-idle
		pHalData->bt_coexist.bBTBusyTraffic = pBtMgnt->ExtConfig.bBTBusy;
		stateChange = _TRUE;
	}

	if (stateChange)
	{
		if (!pBtMgnt->ExtConfig.bBTBusy)
		{
			u8	tempu1Byte;
			RTPRINT(FBT, BT_TRACE, ("[BT] BT is idle or disable\n"));

			tempu1Byte = rtw_read8(padapter, 0x4fd);
			tempu1Byte |= BIT(2);

			rtw_write8(padapter, 0x4fd, tempu1Byte);

			//Resume RF Rx LPF corner
			if (IS_HARDWARE_TYPE_8192D(padapter))
			{
				PHY_SetRFReg(padapter, PathA, 0x1e, bRFRegOffsetMask, pHalData->bt_coexist.BtRfRegOrigin1E);
			}
			else
			{
				PHY_SetRFReg(padapter, PathA, 0x1e, 0xf0, pHalData->bt_coexist.BtRfRegOrigin1E);
			}
			BTDM_CoexAllOff(padapter);

			RTPRINT(FBT, BT_TRACE, ("BT_Turn OFF Coexist bt is off \n"));

			rtw_write8(padapter, REG_GPIO_MUXCFG, 0x0);
		}
		else
		{
			u8	tempu1Byte;
			RTPRINT(FBT, BT_TRACE, ("[BT] BT is non-idle\n"));

			tempu1Byte = rtw_read8(padapter, 0x4fd);
			tempu1Byte &=~ BIT(2);
			rtw_write8(padapter, 0x4fd, tempu1Byte);

			//Shrink RF Rx LPF corner
			if (IS_HARDWARE_TYPE_8192D(padapter))
			{
				PHY_SetRFReg(padapter, PathA, 0x1e, bRFRegOffsetMask, 0xf2ff7);
			}
			else
			{
				//Shrink RF Rx LPF corner, 0x1e[7:4]=1111
				PHY_SetRFReg(padapter, PathA, 0x1e, 0xf0, 0xf);
			}
		}
	}

	if (stateChange)
	{
		if (pBtMgnt->ExtConfig.bBTBusy)
		{
			BTDM_RejectAPAggregatedPacket(padapter, _TRUE);
		}
		else
		{
			BTDM_RejectAPAggregatedPacket(padapter, _FALSE);
		}
	}
}

void btdm_WLANActOff(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	//Only used in BC4 setting
	rtw_write8(padapter, REG_GPIO_MUXCFG, 0x0);
	BTDM_Balance(padapter, _FALSE, 0, 0);
	BTDM_DiminishWiFi(padapter, _FALSE, _FALSE, 0x0, BT_FW_NAV_OFF);
}

void btdm_WLANActBTPrecedence(PADAPTER padapter)
{
	BTDM_Balance(padapter, _FALSE, 0, 0);
	BTDM_DiminishWiFi(padapter, _FALSE, _FALSE, 0x0, BT_FW_NAV_OFF);

	rtw_write32(padapter, 0x6c4,0x55555555);
	rtw_write32(padapter, 0x6c8,0x000000f0);
	rtw_write32(padapter, 0x6cc,0x40000010);
	rtw_write8(padapter, REG_GPIO_MUXCFG, 0xa0);
}

//==============================================================
//
// Note:
// In the following, FW should be done before SW mechanism.
// BTDM_Balance(), BTDM_DiminishWiFi(), BT_NAV() should be done
// before BTDM_AGCTable(), BTDM_BBBackOffLevel(), btdm_DacSwing().
//
//==============================================================

void btdm_DacSwing(PADAPTER padapter, u8 type)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.BT_Ant_Num != Ant_x2)
		return;

	if (type == BT_DACSWING_OFF)
	{
		RTPRINT(FBT, BT_TRACE, ("[BT]DACSwing Off!\n"));
		PHY_SetBBReg(padapter, 0x880, 0xfc000000, 0x30);
	}
	else if (type == BT_DACSWING_M4)
	{
		if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_BT_RSSI_LOW)
		{
			RTPRINT(FBT, BT_TRACE, ("[BT]DACSwing -4 original, but Low RSSI!\n"));
			PHY_SetBBReg(padapter, 0x880, 0xfc000000, 0x18);
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("[BT]DACSwing -4!\n"));
			PHY_SetBBReg(padapter, 0x880, 0xfc000000, 0x20);
		}
	}
	else if (type == BT_DACSWING_M7)
	{
		RTPRINT(FBT, BT_TRACE, ("[BT]DACSwing -7!\n"));
		PHY_SetBBReg(padapter, 0x880, 0xfc000000, 0x18);
	}
	else if (type == BT_DACSWING_M10)
	{
		RTPRINT(FBT, BT_TRACE, ("[BT]DACSwing -10!\n"));
		PHY_SetBBReg(padapter, 0x880, 0xfc000000, 0x10);
	}

	if (type != BT_DACSWING_OFF)
		pHalData->bt_coexist.bSWCoexistAllOff = _FALSE;
}

void btdm_A2DPActionBC42Ant(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			btRssiState;

	rtw_write8(padapter, REG_GPIO_MUXCFG, 0x0);

	if (pBtMgnt->ExtConfig.bBTBusy)
	{
		RTPRINT(FBT, BT_TRACE, ("BT is non-idle!\n"));

		if (BTDM_IsHT40(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("HT40\n"));
			if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
				return;
#if 0
			// Do the FW mechanism first
			BTDM_FWCoexAllOff(padapter);
#else
			// Do the FW mechanism first
			if (pmlmepriv->LinkDetectInfo.bTxBusyTraffic)
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi Uplink\n"));
				BTDM_Balance(padapter, _TRUE, 0xc, 0x18);
				BTDM_DiminishWiFi(padapter, _FALSE, _FALSE, 0x20, BT_FW_NAV_OFF);
			}
			else if (pmlmepriv->LinkDetectInfo.bRxBusyTraffic)
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi Downlink\n"));
				BTDM_FWCoexAllOff(padapter);
			}
#endif
			// Then do the SW mechanism
			BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
			BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
			btdm_DacSwing(padapter, BT_DACSWING_M4);
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
			btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_47, 0);
			if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
				return;
#if 0
			// Do the FW mechanism first
			BTDM_FWCoexAllOff(padapter);
#else
			if (pmlmepriv->LinkDetectInfo.bTxBusyTraffic)
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi Uplink\n"));
				BTDM_Balance(padapter, _TRUE, 0xc, 0x18);
				BTDM_DiminishWiFi(padapter, _FALSE, _FALSE, 0x20, BT_FW_NAV_OFF);
			}
			else if (pmlmepriv->LinkDetectInfo.bRxBusyTraffic)
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi Downlink\n"));
				BTDM_FWCoexAllOff(padapter);
			}
#endif
			// Then do the SW mechanism
			if ((btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH))
			{
				BTDM_AGCTable(padapter, BT_AGCTABLE_ON);
				BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
				btdm_DacSwing(padapter, BT_DACSWING_M4);
			}
			else
			{
				BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
				BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_OFF);
				btdm_DacSwing(padapter, BT_DACSWING_M4);
			}
		}
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("BT is idle!\n"));
		BTDM_CoexAllOff(padapter);
	}
}

void btdm_A2DPActionBC82Ant(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter)	;
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			btRssiState;

	if (pBtMgnt->ExtConfig.bBTA2DPBusy && pmlmepriv->LinkDetectInfo.bBusyTraffic)
	{
		RTPRINT(FBT, BT_TRACE, ("BT is non-idle && Wifi is non-idle!\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_47, 0);
		if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
			return;

		// Do the FW mechanism first
		if (pmlmepriv->LinkDetectInfo.bTxBusyTraffic)
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi Uplink\n"));
			BTDM_Balance(padapter, _TRUE, 0xc, 0x18);
			BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
		}
		else if (pmlmepriv->LinkDetectInfo.bRxBusyTraffic)
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi Downlink\n"));
			BTDM_Balance(padapter, _TRUE, 0x10, 0x18);
			BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
		}

		// Then do the SW mechanism
		if (BTDM_IsHT40(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("HT40\n"));
			BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
			BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
			btdm_DacSwing(padapter, BT_DACSWING_OFF);
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
			if ((btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH))
			{
				BTDM_AGCTable(padapter, BT_AGCTABLE_ON);
				BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
				btdm_DacSwing(padapter, BT_DACSWING_OFF);
			}
			else
			{
				BTDM_SWCoexAllOff(padapter);
			}
		}
	}
	else if (pBtMgnt->ExtConfig.bBTA2DPBusy)
	{
		RTPRINT(FBT, BT_TRACE, ("BT is non-idle!\n"));
		if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
			return;

		// Do the FW mechanism first
		BTDM_Balance(padapter, _FALSE, 0, 0);
		BTDM_DiminishWiFi(padapter, _TRUE, _TRUE, 0x18, BT_FW_NAV_OFF);

		// Then do the SW mechanism
		BTDM_SWCoexAllOff(padapter);
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("BT is idle and Wifi is idle!\n"));
		BTDM_CoexAllOff(padapter);
	}
}

void btdm_A2DPActionBC82Ant92d(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter)	;
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			btRssiState, rssiState1;

	if (pBtMgnt->ExtConfig.bBTA2DPBusy && pmlmepriv->LinkDetectInfo.bBusyTraffic)
	{
		RTPRINT(FBT, BT_TRACE, ("BT is non-idle && Wifi is non-idle!\n"));
		if (BTDM_IsHT40(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("HT40\n"));
			rssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, BT_FW_COEX_THRESH_47, 0);
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
			if (BTDM_IsWifiUplink(padapter))
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi Uplink\n"));
				rssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, BT_FW_COEX_THRESH_25, 0);
			}
			else if (BTDM_IsWifiDownlink(padapter))
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi Downlink\n"));
				rssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, BT_FW_COEX_THRESH_40, 0);
			}
		}
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_47, 0);
		if (!BTDM_IsCoexistStateChanged(padapter))
			return;

		// Do the FW mechanism first
		if (BTDM_IsWifiUplink(padapter))
		{
			BTDM_Balance(padapter, _TRUE, 0xc, 0x18);
			BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
		}
		else if (BTDM_IsWifiDownlink(padapter))
		{
			BTDM_Balance(padapter, _TRUE, 0x10, 0x18);
			BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
		}

		// Then do the SW mechanism
		if ((rssiState1 == BT_RSSI_STATE_HIGH) ||
			(rssiState1 == BT_RSSI_STATE_STAY_HIGH))
		{
			BTDM_AGCTable(padapter, BT_AGCTABLE_ON);
		}
		else
		{
			BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
		}

		if (BTDM_IsHT40(padapter))
		{
			BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
			btdm_DacSwing(padapter, BT_DACSWING_OFF);
		}
		else
		{
			if ((btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH))
			{
				BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
				btdm_DacSwing(padapter, BT_DACSWING_OFF);
			}
			else
			{
				BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_OFF);
				btdm_DacSwing(padapter, BT_DACSWING_OFF);
			}
		}
	}
	else if (pBtMgnt->ExtConfig.bBTA2DPBusy)
	{
		RTPRINT(FBT, BT_TRACE, ("BT is non-idle!\n"));
		if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
			return;

		// Do the FW mechanism first
		BTDM_Balance(padapter, _FALSE, 0, 0);
		BTDM_DiminishWiFi(padapter, _TRUE, _TRUE, 0x18, BT_FW_NAV_OFF);

		// Then do the SW mechanism
		BTDM_SWCoexAllOff(padapter);
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("BT is idle and Wifi is idle!\n"));
		BTDM_CoexAllOff(padapter);
	}
}

u8 btdm_A2DPAction2Ant(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PBT_DBG			pBtDbg = &pBTInfo->BtDbg;
	u8			bEnter = _FALSE;

	if (pBtDbg->dbgCtrl == _TRUE )
	{
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_A2DP)
			bEnter = _TRUE;
	}
	else
	{
		if (BTHCI_CheckProfileExist(padapter,BT_PROFILE_A2DP) && pBtMgnt->ExtConfig.NumberOfHandle==1)
			bEnter = _TRUE;
	}

	if (bEnter)
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], btdm_A2DPAction2Ant(), "));
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_A2DP;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_PROFILE_HID;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_PROFILE_PAN;

		if (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC4)
		{
			RTPRINT(FBT, BT_TRACE, ("[BC4]\n"));
			btdm_A2DPActionBC42Ant(padapter);
		}
		else if (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC8)
		{
			RTPRINT(FBT, BT_TRACE, ("[BC8]\n"));
			if (IS_HARDWARE_TYPE_8192D(padapter))
				btdm_A2DPActionBC82Ant92d(padapter);
			else
				btdm_A2DPActionBC82Ant(padapter);
		}
		return _TRUE;
	}
	else
	{
		return _FALSE;
	}
}

void btdm_PANActionBC42Ant(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
		return;

	rtw_write8(padapter, REG_GPIO_MUXCFG, 0x0);

	if (pBtMgnt->BtOperationOn)
	{
		RTPRINT(FBT, BT_TRACE, ("[BT 3.0]\n"));
		BTDM_Balance(padapter, _FALSE, 0, 0);
		BTDM_DiminishWiFi(padapter, _FALSE, _FALSE, 0x0, BT_FW_NAV_OFF);
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BT 2.1]\n"));
		if (pBtMgnt->ExtConfig.bBTBusy && pmlmepriv->LinkDetectInfo.bBusyTraffic)
		{
			RTPRINT(FBT, BT_TRACE, ("BT is non-idle && Wifi is non-idle!\n"));
			BTDM_Balance(padapter, _TRUE, 0x20, 0x10);
			BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("BT is idle or Wifi is idle!\n"));
			BTDM_Balance(padapter, _FALSE, 0, 0);
			BTDM_DiminishWiFi(padapter, _FALSE, _FALSE, 0x0, BT_FW_NAV_OFF);
		}
	}
	BTDM_SWCoexAllOff(padapter);
}

void btdm_PANActionBC82Ant(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter)	;
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			btRssiState;

	if (pBtMgnt->BtOperationOn)
	{
		RTPRINT(FBT, BT_TRACE, ("[BT 3.0]\n"));
		if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
			return;

		BTDM_CoexAllOff(padapter);
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BT 2.1]\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 3, BT_FW_COEX_THRESH_25, BT_FW_COEX_THRESH_50);
		if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
			return;

		if (pBtMgnt->ExtConfig.bBTBusy && pmlmepriv->LinkDetectInfo.bBusyTraffic)
		{
			RTPRINT(FBT, BT_TRACE, ("BT is non-idle && Wifi is non-idle!\n"));

			if ((btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH))
			{
				RTPRINT(FBT, BT_TRACE, ("RSSI stay HIGH or High \n"));
				// Do the FW mechanism first
				if (pHalData->bt_coexist.BT21TrafficStatistics.bTxBusyTraffic)
				{
					RTPRINT(FBT, BT_TRACE, ("BT Uplink\n"));
					BTDM_Balance(padapter, _TRUE, 0x20, 0x20);
					BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
				}
				else if (pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic)
				{
					RTPRINT(FBT, BT_TRACE, ("BT Downlink\n"));
					BTDM_FWCoexAllOff(padapter);
				}
				// Then do the SW mechanism
				if (BTDM_IsHT40(padapter))
				{
					RTPRINT(FBT, BT_TRACE, ("HT40\n"));
					BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
				}
				else
				{
					RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
					BTDM_AGCTable(padapter, BT_AGCTABLE_ON);
				}
				BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
				if (pHalData->bt_coexist.BT21TrafficStatistics.bTxBusyTraffic)
				{
					RTPRINT(FBT, BT_TRACE, ("BT Uplink\n"));
					btdm_DacSwing(padapter, BT_DACSWING_OFF);
				}
				else if (pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic)
				{
					RTPRINT(FBT, BT_TRACE, ("BT Downlink\n"));
					btdm_DacSwing(padapter, BT_DACSWING_M4);
				}
			}
			else if ((btRssiState == BT_RSSI_STATE_MEDIUM) ||
				(btRssiState == BT_RSSI_STATE_STAY_MEDIUM))
			{
				RTPRINT(FBT, BT_TRACE, ("RSSI stay Medium or Medium \n"));
				// Do the FW mechanism first
				BTDM_Balance(padapter, _TRUE, 0x20, 0x20);

				if (pmlmepriv->LinkDetectInfo.bTxBusyTraffic)
				{
					RTPRINT(FBT, BT_TRACE, ("Wifi Uplink\n"));
					BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
				}
				else if (pmlmepriv->LinkDetectInfo.bRxBusyTraffic)
				{
					RTPRINT(FBT, BT_TRACE, ("Wifi Downlink\n"));
					if (BTDM_IsHT40(padapter))
						BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);//BT_FW_NAV_ON);
					else
						BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
				}
				// Then do the SW mechanism
				if (BTDM_IsHT40(padapter))
				{
					RTPRINT(FBT, BT_TRACE, ("HT40\n"));
					BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
				}
				else
				{
					RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
					BTDM_AGCTable(padapter, BT_AGCTABLE_ON);
				}
				BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
				btdm_DacSwing(padapter, BT_DACSWING_OFF);
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("RSSI stay LOW or LOW \n"));
				// Do the FW mechanism first
				BTDM_Balance(padapter, _TRUE, 0x20, 0x20);

				if (pmlmepriv->LinkDetectInfo.bTxBusyTraffic)
				{
					RTPRINT(FBT, BT_TRACE, ("Wifi Uplink\n"));
					BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
				}
				else if (pmlmepriv->LinkDetectInfo.bRxBusyTraffic)
				{
					RTPRINT(FBT, BT_TRACE, ("Wifi Downlink\n"));
					if (pHalData->bt_coexist.BT21TrafficStatistics.bTxBusyTraffic)
					{
						RTPRINT(FBT, BT_TRACE, ("BT Uplink\n"));
						if (BTDM_IsHT40(padapter))
						{
							RTPRINT(FBT, BT_TRACE, ("HT40\n"));
							BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);//BT_FW_NAV_ON);
						}
						else
						{
							RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
							BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
						}
					}
					else if (pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic)
					{
						RTPRINT(FBT, BT_TRACE, ("BT Downlink\n"));
						BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
					}
				}
				// Then do the SW mechanism
				BTDM_SWCoexAllOff(padapter);
			}
		}
		else if (pBtMgnt->ExtConfig.bBTBusy &&
				!pmlmepriv->LinkDetectInfo.bBusyTraffic &&
				(BTDM_GetRxSS(padapter) < 30))
		{
			RTPRINT(FBT, BT_TRACE, ("BT is non-idle && Wifi is idle!\n"));
			RTPRINT(FBT, BT_TRACE, ("RSSI < 30\n"));
			// Do the FW mechanism first
			BTDM_Balance(padapter, _TRUE, 0x0a, 0x20);
			BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
			// Then do the SW mechanism
			BTDM_SWCoexAllOff(padapter);
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("BT is idle or Wifi is idle!\n"));
			BTDM_CoexAllOff(padapter);
		}
	}
}

void btdm_PANActionBC82Ant92d(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter)	;
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			btRssiState, rssiState1;

	if (pBtMgnt->BtOperationOn)
	{
		RTPRINT(FBT, BT_TRACE, ("[BT 3.0]\n"));
		if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
			return;

		BTDM_CoexAllOff(padapter);
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BT 2.1]\n"));
		if (BTDM_IsHT40(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("HT40\n"));
			rssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, BT_FW_COEX_THRESH_47, 0);
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
			rssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, BT_FW_COEX_THRESH_25, 0);
		}
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 3, BT_FW_COEX_THRESH_25, BT_FW_COEX_THRESH_50);
		if (!BTDM_IsCoexistStateChanged(padapter))
			return;

		if (pBtMgnt->ExtConfig.bBTBusy && pmlmepriv->LinkDetectInfo.bBusyTraffic)
		{
			RTPRINT(FBT, BT_TRACE, ("BT is non-idle && Wifi is non-idle!\n"));

			if ((btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH))
			{
				RTPRINT(FBT, BT_TRACE, ("RSSI stay HIGH or High \n"));
				// Do the FW mechanism first
				if (pHalData->bt_coexist.BT21TrafficStatistics.bTxBusyTraffic)
				{
					RTPRINT(FBT, BT_TRACE, ("BT Uplink\n"));
					BTDM_Balance(padapter, _TRUE, 0x20, 0x20);
					BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
				}
				else if (pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic)
				{
					RTPRINT(FBT, BT_TRACE, ("BT Downlink\n"));
					BTDM_FWCoexAllOff(padapter);
				}
				// Then do the SW mechanism
				if ((rssiState1 == BT_RSSI_STATE_HIGH) ||
					(rssiState1 == BT_RSSI_STATE_STAY_HIGH))
				{
					BTDM_AGCTable(padapter, BT_AGCTABLE_ON);
				}
				else
				{
					BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
				}
				BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
				if (pHalData->bt_coexist.BT21TrafficStatistics.bTxBusyTraffic)
				{
					RTPRINT(FBT, BT_TRACE, ("BT Uplink\n"));
					btdm_DacSwing(padapter, BT_DACSWING_OFF);
				}
				else if (pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic)
				{
					RTPRINT(FBT, BT_TRACE, ("BT Downlink\n"));
					btdm_DacSwing(padapter, BT_DACSWING_M4);
				}
			}
			else if ((btRssiState == BT_RSSI_STATE_MEDIUM) ||
				(btRssiState == BT_RSSI_STATE_STAY_MEDIUM))
			{
				RTPRINT(FBT, BT_TRACE, ("RSSI stay Medium or Medium \n"));
				// Do the FW mechanism first
				BTDM_Balance(padapter, _TRUE, 0x20, 0x20);

				if (pmlmepriv->LinkDetectInfo.bTxBusyTraffic)
				{
					RTPRINT(FBT, BT_TRACE, ("Wifi Uplink\n"));
					BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
				}
				else if (pmlmepriv->LinkDetectInfo.bRxBusyTraffic)
				{
					RTPRINT(FBT, BT_TRACE, ("Wifi Downlink\n"));
					if (BTDM_IsHT40(padapter))
						BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);//BT_FW_NAV_ON);
					else
						BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
				}
				// Then do the SW mechanism
				if ((rssiState1 == BT_RSSI_STATE_HIGH) ||
					(rssiState1 == BT_RSSI_STATE_STAY_HIGH))
				{
					BTDM_AGCTable(padapter, BT_AGCTABLE_ON);
				}
				else
				{
					BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
				}
				BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
				btdm_DacSwing(padapter, BT_DACSWING_OFF);
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("RSSI stay LOW or LOW \n"));
				// Do the FW mechanism first
				BTDM_Balance(padapter, _TRUE, 0x20, 0x20);

				if (pmlmepriv->LinkDetectInfo.bTxBusyTraffic)
				{
					RTPRINT(FBT, BT_TRACE, ("Wifi Uplink\n"));
					BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
				}
				else if (pmlmepriv->LinkDetectInfo.bRxBusyTraffic)
				{
					RTPRINT(FBT, BT_TRACE, ("Wifi Downlink\n"));
					if (pHalData->bt_coexist.BT21TrafficStatistics.bTxBusyTraffic)
					{
						RTPRINT(FBT, BT_TRACE, ("BT Uplink\n"));
						if (BTDM_IsHT40(padapter))
						{
							RTPRINT(FBT, BT_TRACE, ("HT40\n"));
							BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);//BT_FW_NAV_ON);
						}
						else
						{
							RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
							BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
						}
					}
					else if (pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic)
					{
						RTPRINT(FBT, BT_TRACE, ("BT Downlink\n"));
						BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
					}
				}
				// Then do the SW mechanism
				if ((rssiState1 == BT_RSSI_STATE_HIGH) ||
					(rssiState1 == BT_RSSI_STATE_STAY_HIGH))
				{
					BTDM_AGCTable(padapter, BT_AGCTABLE_ON);
				}
				else
				{
					BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
				}
				BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_OFF);
				btdm_DacSwing(padapter, BT_DACSWING_OFF);
			}
		}
		else if (pBtMgnt->ExtConfig.bBTBusy &&
				!pmlmepriv->LinkDetectInfo.bBusyTraffic &&
				(BTDM_GetRxSS(padapter) < 30))
		{
			RTPRINT(FBT, BT_TRACE, ("BT is non-idle && Wifi is idle!\n"));
			RTPRINT(FBT, BT_TRACE, ("RSSI < 30\n"));
			// Do the FW mechanism first
			BTDM_Balance(padapter, _TRUE, 0x0a, 0x20);
			BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
			// Then do the SW mechanism
			BTDM_SWCoexAllOff(padapter);
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("BT is idle or Wifi is idle!\n"));
			BTDM_CoexAllOff(padapter);
		}
	}
}

u8 btdm_PANAction2Ant(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PBT_DBG			pBtDbg = &pBTInfo->BtDbg;
	u8			bEnter = _FALSE;

	if (pBtDbg->dbgCtrl == _TRUE )
	{
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_PAN)
			bEnter = _TRUE;
	}
	else
	{
		if (BTHCI_CheckProfileExist(padapter,BT_PROFILE_PAN) && pBtMgnt->ExtConfig.NumberOfHandle==1)
			bEnter = _TRUE;
	}

	if (bEnter)
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], btdm_PANAction2Ant(), "));
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_PAN;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_PROFILE_HID;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_PROFILE_A2DP;

		if (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC4)
		{
			RTPRINT(FBT, BT_TRACE, ("[BC4]\n"));
			btdm_PANActionBC42Ant(padapter);
		}
		else if (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC8)
		{
			RTPRINT(FBT, BT_TRACE, ("[BC8]\n"));
			if (IS_HARDWARE_TYPE_8192D(padapter))
				btdm_PANActionBC82Ant92d(padapter);
			else
			btdm_PANActionBC82Ant(padapter);
		}
		return _TRUE;
	}
	else
	{
		return _FALSE;
	}
}

void btdm_HIDActionBC42Ant(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
		return;

	if (BTDM_Legacy(padapter))
	{
		RTPRINT(FBT, BT_TRACE, ("Current Wireless Mode is B/G\n"));
		btdm_WLANActBTPrecedence(padapter);
	}
	else if (pmlmepriv->LinkDetectInfo.bTxBusyTraffic)
	{
		RTPRINT(FBT, BT_TRACE, ("Wifi Uplink\n"));
		btdm_WLANActBTPrecedence(padapter);
	}
	else if (pmlmepriv->LinkDetectInfo.bRxBusyTraffic)
	{
		RTPRINT(FBT, BT_TRACE, ("Wifi Downlink\n"));
		btdm_WLANActOff(padapter);
	}
	else if (!pmlmepriv->LinkDetectInfo.bBusyTraffic)
	{
		RTPRINT(FBT, BT_TRACE, ("Wifi Idel \n"));
		btdm_WLANActOff(padapter);
	}
	BTDM_SWCoexAllOff(padapter);
}

void btdm_HIDActionBC82Ant(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter)	;
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			btRssiState;

#ifdef CONFIG_USB_HCI
	if (pHalData->CustomerID == RT_CID_PLANEX)
	{
		if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
			return;
		btdm_HIDActionBC42Ant(padapter);
		return;
	}
#endif

	if (pmlmepriv->LinkDetectInfo.bTxBusyTraffic)
	{
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_45, 0);
	}
	else if (pmlmepriv->LinkDetectInfo.bRxBusyTraffic)
	{
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_20, 0);
	}

	if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
		return;

	if (pBtMgnt->ExtConfig.bBTBusy && pmlmepriv->LinkDetectInfo.bBusyTraffic)
	{
		RTPRINT(FBT, BT_TRACE, ("BT is non-idle && Wifi is non-idle!\n"));
		// Do the FW mechanism first
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
			(btRssiState == BT_RSSI_STATE_STAY_HIGH))
		{
			BTDM_FWCoexAllOff(padapter);
		}
		else
		{
			if (pmlmepriv->LinkDetectInfo.bTxBusyTraffic)
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi Uplink\n"));
				BTDM_Balance(padapter, _FALSE, 0, 0);
				BTDM_DiminishWiFi(padapter, _TRUE, _TRUE, 0x18, BT_FW_NAV_OFF);
			}
			else if (pmlmepriv->LinkDetectInfo.bRxBusyTraffic)
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi Downlink\n"));
				BTDM_Balance(padapter, _TRUE, 0x15, 0x15);
				BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x30, BT_FW_NAV_OFF);
			}
		}
		// Then do the SW mechanism
		BTDM_SWCoexAllOff(padapter);
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("BT is idle or Wifi is idle!\n"));
		BTDM_CoexAllOff(padapter);
	}
}

void btdm_HIDActionBC82Ant92d(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter)	;
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			btRssiState;

	if (pmlmepriv->LinkDetectInfo.bTxBusyTraffic)
	{
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_45, 0);
	}
	else if (pmlmepriv->LinkDetectInfo.bRxBusyTraffic)
	{
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_20, 0);
	}

	if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
		return;

	if (pBtMgnt->ExtConfig.bBTBusy && pmlmepriv->LinkDetectInfo.bBusyTraffic)
	{
		RTPRINT(FBT, BT_TRACE, ("BT is non-idle && Wifi is non-idle!\n"));
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
			(btRssiState == BT_RSSI_STATE_STAY_HIGH))
		{
			// Do the FW mechanism first
			BTDM_FWCoexAllOff(padapter);

			// Then do the SW mechanism
			BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
			BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_OFF);
			btdm_DacSwing(padapter, BT_DACSWING_M4);
		}
		else
		{
			// Do the FW mechanism first
			if (pmlmepriv->LinkDetectInfo.bTxBusyTraffic)
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi Uplink\n"));
				BTDM_Balance(padapter, _FALSE, 0, 0);
				BTDM_DiminishWiFi(padapter, _TRUE, _TRUE, 0x18, BT_FW_NAV_OFF);
			}
			else if (pmlmepriv->LinkDetectInfo.bRxBusyTraffic)
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi Downlink\n"));
				BTDM_Balance(padapter, _TRUE, 0x15, 0x15);
				BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x30, BT_FW_NAV_OFF);
			}
			// Then do the SW mechanism
			BTDM_SWCoexAllOff(padapter);
		}
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("BT is idle or Wifi is idle!\n"));
		BTDM_CoexAllOff(padapter);
	}
}

u8 btdm_HIDAction2Ant(PADAPTER padapter)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter)	;
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PBT_DBG			pBtDbg = &pBTInfo->BtDbg;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			bEnter = _FALSE;

	if (pBtDbg->dbgCtrl == _TRUE )
	{
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_HID)
			bEnter = _TRUE;
	}
	else
	{
		if (BTHCI_CheckProfileExist(padapter,BT_PROFILE_HID) && pBtMgnt->ExtConfig.NumberOfHandle==1)
			bEnter = _TRUE;
	}

	if (bEnter)
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], btdm_HIDAction2Ant(), "));
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_HID;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_PROFILE_PAN;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_PROFILE_A2DP;

		if (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC4)
		{
			RTPRINT(FBT, BT_TRACE, ("[BC4]\n"));
			btdm_HIDActionBC42Ant(padapter);
		}
		else if (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC8)
		{
			RTPRINT(FBT, BT_TRACE, ("[BC8]\n"));
			if (IS_HARDWARE_TYPE_8192D(padapter))
				btdm_HIDActionBC82Ant92d(padapter);
			else
			btdm_HIDActionBC42Ant(padapter);
		}
		return _TRUE;
	}
	else
	{
		return _FALSE;
	}
}

void btdm_SCOActionBC42Ant(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
		return;

	btdm_WLANActOff(padapter);
	BTDM_SWCoexAllOff(padapter);
}

void btdm_SCOActionBC82Ant(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8	btRssiState;

	if (BTDM_IsHT40(padapter))
	{
		if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
			return;

		RTPRINT(FBT, BT_TRACE, ("HT40\n"));
		// Do the FW mechanism first
		BTDM_Balance(padapter, _FALSE, 0, 0);
		BTDM_DiminishWiFi(padapter, _FALSE, _FALSE, 0, BT_FW_NAV_OFF);

		// Then do the SW mechanism
		BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
		BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
		btdm_DacSwing(padapter, BT_DACSWING_OFF);
	}
	else
	{
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_47, 0);
		if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
			return;

		RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
		// Do the FW mechanism first
		BTDM_Balance(padapter, _FALSE, 0, 0);
		BTDM_DiminishWiFi(padapter, _FALSE, _FALSE, 0, BT_FW_NAV_OFF);

		// Then do the SW mechanism
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
			(btRssiState == BT_RSSI_STATE_STAY_HIGH))
		{
			BTDM_AGCTable(padapter, BT_AGCTABLE_ON);
			BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
			btdm_DacSwing(padapter, BT_DACSWING_OFF);
		}
		else
		{
			BTDM_SWCoexAllOff(padapter);
		}
	}
}

void btdm_SCOActionBC82Ant92d(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8	btRssiState, rssiState1;

	rssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, BT_FW_COEX_THRESH_47, 0);
	if (BTDM_IsHT40(padapter))
	{
		if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
			return;

		RTPRINT(FBT, BT_TRACE, ("HT40\n"));
		// Do the FW mechanism first
		BTDM_Balance(padapter, _FALSE, 0, 0);
		BTDM_DiminishWiFi(padapter, _FALSE, _FALSE, 0, BT_FW_NAV_OFF);

		// Then do the SW mechanism
		BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
		BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
		btdm_DacSwing(padapter, BT_DACSWING_OFF);
	}
	else
	{
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_47, 0);
		if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
			return;

		RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
		// Do the FW mechanism first
		BTDM_Balance(padapter, _FALSE, 0, 0);
		BTDM_DiminishWiFi(padapter, _FALSE, _FALSE, 0, BT_FW_NAV_OFF);

		// Then do the SW mechanism
		if ((rssiState1 == BT_RSSI_STATE_HIGH) ||
			(rssiState1 == BT_RSSI_STATE_STAY_HIGH))
		{
			BTDM_AGCTable(padapter, BT_AGCTABLE_ON);
		}
		else
		{
			BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
		}
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
			(btRssiState == BT_RSSI_STATE_STAY_HIGH))
		{
			BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
			btdm_DacSwing(padapter, BT_DACSWING_OFF);
		}
		else
		{
			BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_OFF);
			btdm_DacSwing(padapter, BT_DACSWING_OFF);
		}
	}
}

u8 btdm_SCOAction2Ant(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PBT_DBG			pBtDbg = &pBTInfo->BtDbg;
	u8			bEnter = _FALSE;

	if (pBtDbg->dbgCtrl == _TRUE )
	{
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_SCO)
			bEnter = _TRUE;
	}
	else
	{
		if (pBtMgnt->ExtConfig.NumberOfSCO > 0)
			bEnter = _TRUE;
	}
	if (bEnter)
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], btdm_SCOAction2Ant(), "));
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_SCO;

		if (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC4)
		{
			RTPRINT(FBT, BT_TRACE, ("[BC4]\n"));
			btdm_SCOActionBC42Ant(padapter);
		}
		else if (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC8)
		{
			RTPRINT(FBT, BT_TRACE, ("[BC8]\n"));
			if (IS_HARDWARE_TYPE_8192D(padapter))
				btdm_SCOActionBC82Ant92d(padapter);
			else
			btdm_SCOActionBC82Ant(padapter);
		}
		return _TRUE;
	}
	else
	{
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_PROFILE_SCO;
		return _FALSE;
	}
}

void btdm_HIDA2DPActionBC42Ant(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			btRssiState;

	if (pBtMgnt->ExtConfig.bBTBusy)
	{
		RTPRINT(FBT, BT_TRACE, ("BT is non-idle!\n"));

		if (BTDM_IsHT40(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("HT40\n"));
			if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
				return;
#if 0
			// Do the FW mechanism first
			BTDM_FWCoexAllOff(padapter);
#else
			// Do the FW mechanism first
			if (pmlmepriv->LinkDetectInfo.bTxBusyTraffic)
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi Uplink\n"));
				BTDM_Balance(padapter, _TRUE, 0x7, 0x20);
				BTDM_DiminishWiFi(padapter, _FALSE, _FALSE, 0x20, BT_FW_NAV_OFF);
			}
			else if (pmlmepriv->LinkDetectInfo.bRxBusyTraffic)
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi Downlink\n"));
				BTDM_FWCoexAllOff(padapter);
			}
#endif
			// Then do the SW mechanism
			BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
			BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
			btdm_DacSwing(padapter, BT_DACSWING_M7);
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
			btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_47, 0);
			if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
				return;
#if 0
			// Do the FW mechanism first
			BTDM_FWCoexAllOff(padapter);
#else
			// Do the FW mechanism first
			if (pmlmepriv->LinkDetectInfo.bTxBusyTraffic)
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi Uplink\n"));
				BTDM_Balance(padapter, _TRUE, 0x7, 0x20);
				BTDM_DiminishWiFi(padapter, _FALSE, _FALSE, 0x20, BT_FW_NAV_OFF);
			}
			else if (pmlmepriv->LinkDetectInfo.bRxBusyTraffic)
			{
				RTPRINT(FBT, BT_TRACE, ("Wifi Downlink\n"));
				BTDM_FWCoexAllOff(padapter);
			}
#endif

			// Then do the SW mechanism
			if ((btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH))
			{
				BTDM_AGCTable(padapter, BT_AGCTABLE_ON);
				BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
				btdm_DacSwing(padapter, BT_DACSWING_M7);
			}
			else
			{
				BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
				BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_OFF);
				btdm_DacSwing(padapter, BT_DACSWING_M7);
			}
		}
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("BT is idle!\n"));
		BTDM_CoexAllOff(padapter);
	}
}

void btdm_HIDA2DPActionBC82Ant(PADAPTER padapter)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			btRssiState;

	if (pBtMgnt->ExtConfig.bBTBusy)
	{
		RTPRINT(FBT, BT_TRACE, ("BT is non-idle!\n"));

		if (BTDM_IsHT40(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("HT40\n"));
			if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
				return;

			// Do the FW mechanism first
			BTDM_FWCoexAllOff(padapter);

			// Then do the SW mechanism
			BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
			BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
			btdm_DacSwing(padapter, BT_DACSWING_M7);
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
			btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_47, 0);
			if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
				return;

			BTDM_FWCoexAllOff(padapter);

			// Then do the SW mechanism
			if ((btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH))
			{
				BTDM_AGCTable(padapter, BT_AGCTABLE_ON);
				BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
				btdm_DacSwing(padapter, BT_DACSWING_M7);
			}
			else
			{
				BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
				BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_OFF);
				btdm_DacSwing(padapter, BT_DACSWING_M7);
			}
		}
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("BT is idle!\n"));
		BTDM_CoexAllOff(padapter);
	}
}

void btdm_HIDA2DPActionBC82Ant92d(PADAPTER padapter)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			btRssiState, rssiState1;

	rssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, BT_FW_COEX_THRESH_35, 0);
	if (pBtMgnt->ExtConfig.bBTBusy)
	{
		RTPRINT(FBT, BT_TRACE, ("BT is non-idle!\n"));

		if (BTDM_IsHT40(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("HT40\n"));
			if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
				return;

			// Do the FW mechanism first
			BTDM_FWCoexAllOff(padapter);

			// Then do the SW mechanism
			if ((rssiState1 == BT_RSSI_STATE_HIGH) ||
				(rssiState1 == BT_RSSI_STATE_STAY_HIGH))
			{
				BTDM_AGCTable(padapter, BT_AGCTABLE_ON);
			}
			else
			{
				BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
			}
			BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
			btdm_DacSwing(padapter, BT_DACSWING_M7);
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
			btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_47, 0);
			if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
				return;

			BTDM_FWCoexAllOff(padapter);

			// Then do the SW mechanism
			if ((rssiState1 == BT_RSSI_STATE_HIGH) ||
				(rssiState1 == BT_RSSI_STATE_STAY_HIGH))
			{
				BTDM_AGCTable(padapter, BT_AGCTABLE_ON);
			}
			else
			{
				BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
			}
			if ((btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH))
			{
				BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
				btdm_DacSwing(padapter, BT_DACSWING_M7);
			}
			else
			{
				BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_OFF);
				btdm_DacSwing(padapter, BT_DACSWING_M7);
			}
		}
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("BT is idle!\n"));
		BTDM_CoexAllOff(padapter);
	}
}

u8 btdm_HIDA2DPAction2Ant(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_DBG			pBtDbg = &pBTInfo->BtDbg;
	u8			bEnter = _FALSE;

	if (pBtDbg->dbgCtrl == _TRUE )
	{
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_HID_A2DP)
			bEnter = _TRUE;
	}
	else
	{
		if (BTHCI_CheckProfileExist(padapter,BT_PROFILE_HID) && BTHCI_CheckProfileExist(padapter,BT_PROFILE_A2DP))
			bEnter = _TRUE;
	}

	if (bEnter)
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], btdm_HIDA2DPAction2Ant(), "));
		pHalData->bt_coexist.CurrentState |= (BT_COEX_STATE_PROFILE_HID|BT_COEX_STATE_PROFILE_A2DP);

		if (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC4)
		{
			RTPRINT(FBT, BT_TRACE, ("[BC4]\n"));
			btdm_HIDA2DPActionBC42Ant(padapter);
		}
		else if (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC8)
		{
			RTPRINT(FBT, BT_TRACE, ("[BC8]\n"));
			if (IS_HARDWARE_TYPE_8192D(padapter))
				btdm_HIDA2DPActionBC82Ant92d(padapter);
			else
			btdm_HIDA2DPActionBC82Ant(padapter);
		}
		return _TRUE;
	}
	else
	{
		pHalData->bt_coexist.CurrentState &= ~(BT_COEX_STATE_PROFILE_HID|BT_COEX_STATE_PROFILE_A2DP);
		return _FALSE;
	}
}

void btdm_HIDPANActionBC42Ant(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
		return;

	if (pBtMgnt->BtOperationOn)
	{
		RTPRINT(FBT, BT_TRACE, ("[BT 3.0]\n"));
			btdm_WLANActBTPrecedence(padapter);
		}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BT 2.1]\n"));
		if (BTDM_Legacy(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("B/G mode \n"));
			btdm_WLANActBTPrecedence(padapter);
		}
		else if (pmlmepriv->LinkDetectInfo.bTxBusyTraffic)
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi Uplink \n"));
			btdm_WLANActBTPrecedence(padapter);
		}
		else if (pmlmepriv->LinkDetectInfo.bRxBusyTraffic)
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi Downlink \n"));
			rtw_write8(padapter, REG_GPIO_MUXCFG, 0x0);
			BTDM_Balance(padapter, _TRUE, 0x20, 0x10);
			BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
		}
		else if (!pmlmepriv->LinkDetectInfo.bBusyTraffic)
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi Idel \n"));
			btdm_WLANActOff(padapter);
		}
	}
	BTDM_SWCoexAllOff(padapter);
}

void btdm_HIDPANActionBC82Ant(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			btRssiState;

	if (!pBtMgnt->BtOperationOn)
	{
		RTPRINT(FBT, BT_TRACE, ("[BT 2.1]\n"));

		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_25, 0);
		if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
			return;

		if ((pBtMgnt->ExtConfig.bBTBusy && padapter->mlmepriv.LinkDetectInfo.bBusyTraffic))
		{
			RTPRINT(FBT, BT_TRACE, ("BT is non-idle && Wifi is non-idle, "));

			// Do the FW mechanism first
			if (pHalData->bt_coexist.BT21TrafficStatistics.bTxBusyTraffic)
			{
				RTPRINT(FBT, BT_TRACE, ("BT Uplink\n"));
				BTDM_Balance(padapter, _TRUE, 0x15, 0x20);
			}
			else if (pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic)
			{
				RTPRINT(FBT, BT_TRACE, ("BT Downlink\n"));
				BTDM_Balance(padapter, _TRUE, 0x10, 0x20);
			}
			BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);

			// Then do the SW mechanism
			if ((btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH))
			{
				if (BTDM_IsHT40(padapter))
				{
					RTPRINT(FBT, BT_TRACE, ("HT40\n"));
					BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
					BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
					btdm_DacSwing(padapter, BT_DACSWING_OFF);
				}
				else
				{
					RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
					BTDM_AGCTable(padapter, BT_AGCTABLE_ON);
					BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
					btdm_DacSwing(padapter, BT_DACSWING_OFF);
				}
			}
			else
			{
				BTDM_SWCoexAllOff(padapter);
			}
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("BT is idle or Wifi is idle!\n"));
			BTDM_CoexAllOff(padapter);
		}
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BT 3.0]\n"));
#ifdef CONFIG_USB_HCI
		if (BTDM_IsWifiUplink(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi Uplink\n"));
			btdm_WLANActBTPrecedence(padapter);
			if (pHalData->CustomerID == RT_CID_PLANEX)
				btdm_DacSwing(padapter, BT_DACSWING_M10);
			else
				btdm_DacSwing(padapter, BT_DACSWING_M7);
		}
		else if (BTDM_IsWifiDownlink(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("Wifi Downlink\n"));
			if (pHalData->CustomerID == RT_CID_PLANEX)
				btdm_DacSwing(padapter, BT_DACSWING_M10);
			else
				btdm_DacSwing(padapter, BT_DACSWING_M7);
		}
#elif defined(CONFIG_PCI_HCI)
		if (pBtMgnt->ExtConfig.bBTBusy)
		{
			RTPRINT(FBT, BT_TRACE, ("BT is non-idle\n"));
			BTDM_FWCoexAllOff(padapter);
			BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
			BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_OFF);
			btdm_DacSwing(padapter, BT_DACSWING_M4);
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("BT is idle\n"));
			btdm_DacSwing(padapter, BT_DACSWING_OFF);
		}
#endif
	}
}

u8 btdm_HIDPANAction2Ant(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_DBG			pBtDbg = &pBTInfo->BtDbg;
	u8			bEnter = _FALSE;

	if (pBtDbg->dbgCtrl == _TRUE )
	{
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_HID_PAN)
			bEnter = _TRUE;
	}
	else
	{
		if (BTHCI_CheckProfileExist(padapter,BT_PROFILE_HID) && BTHCI_CheckProfileExist(padapter,BT_PROFILE_PAN))
			bEnter = _TRUE;
	}

	if (bEnter)
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], btdm_HIDPANAction2Ant(), "));
		pHalData->bt_coexist.CurrentState |= (BT_COEX_STATE_PROFILE_HID|BT_COEX_STATE_PROFILE_PAN);

		if (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC4)
		{
			RTPRINT(FBT, BT_TRACE, ("[BC4]\n"));
			btdm_HIDPANActionBC42Ant(padapter);
		}
		else if (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC8)
		{
			RTPRINT(FBT, BT_TRACE, ("[BC8]\n"));
			btdm_HIDPANActionBC82Ant(padapter);
		}
		return _TRUE;
	}
	else
	{
		pHalData->bt_coexist.CurrentState &= ~(BT_COEX_STATE_PROFILE_HID|BT_COEX_STATE_PROFILE_PAN);
		return _FALSE;
	}

}

void btdm_PANA2DPActionBC42Ant(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
		return;

	rtw_write8(padapter, REG_GPIO_MUXCFG, 0x0);
	if (pBtMgnt->BtOperationOn)
	{
		RTPRINT(FBT, BT_TRACE, ("[BT 3.0]\n"));

		if (pBtMgnt->ExtConfig.bBTBusy)
		{
			RTPRINT(FBT, BT_TRACE, ("BT is non-idle\n"));
			BTDM_FWCoexAllOff(padapter);

			BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
			BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_OFF);
			btdm_DacSwing(padapter, BT_DACSWING_M4);
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("BT is idle\n"));
			BTDM_CoexAllOff(padapter);
		}
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BT 2.1]\n"));
		if (pBtMgnt->ExtConfig.bBTBusy && pmlmepriv->LinkDetectInfo.bBusyTraffic)
		{
			RTPRINT(FBT, BT_TRACE, ("BT is non-idle && Wifi is non-idle!\n"));
			BTDM_Balance(padapter, _TRUE, 0x20, 0x10);
			BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("BT is idle or Wifi is idle!\n"));
			BTDM_Balance(padapter, _FALSE, 0, 0);
			BTDM_DiminishWiFi(padapter, _FALSE, _FALSE, 0x0, BT_FW_NAV_OFF);
		}
		BTDM_SWCoexAllOff(padapter);
	}
}

void btdm_PANA2DPActionBC82Ant(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			btRssiState;

	if (!pBtMgnt->BtOperationOn)
	{
		RTPRINT(FBT, BT_TRACE, ("[BT 2.1]\n"));

		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, BT_FW_COEX_THRESH_25, 0);
		if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
			return;

		if ((pBtMgnt->ExtConfig.bBTBusy && pmlmepriv->LinkDetectInfo.bBusyTraffic))
		{
			RTPRINT(FBT, BT_TRACE, ("BT is non-idle && Wifi is non-idle, "));

			// Do the FW mechanism first
			if (pHalData->bt_coexist.BT21TrafficStatistics.bTxBusyTraffic)
			{
				RTPRINT(FBT, BT_TRACE, ("BT Uplink\n"));
				BTDM_Balance(padapter, _TRUE, 0x15, 0x20);
			}
			else if (pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic)
			{
				RTPRINT(FBT, BT_TRACE, ("BT Downlink\n"));
				BTDM_Balance(padapter, _TRUE, 0x10, 0x20);
			}
			BTDM_DiminishWiFi(padapter, _TRUE, _FALSE, 0x20, BT_FW_NAV_OFF);

			// Then do the SW mechanism
			if ((btRssiState == BT_RSSI_STATE_HIGH) ||
				(btRssiState == BT_RSSI_STATE_STAY_HIGH))
			{
				if (BTDM_IsHT40(padapter))
				{
					RTPRINT(FBT, BT_TRACE, ("HT40\n"));
					BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
					BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
					btdm_DacSwing(padapter, BT_DACSWING_OFF);
				}
				else
				{
					RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
					BTDM_AGCTable(padapter, BT_AGCTABLE_ON);
					BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
					btdm_DacSwing(padapter, BT_DACSWING_OFF);
				}
			}
			else
			{
				BTDM_SWCoexAllOff(padapter);
			}
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("BT is idle or Wifi is idle!\n"));
			BTDM_CoexAllOff(padapter);
		}
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[BT 3.0]\n"));
		if (pBtMgnt->ExtConfig.bBTBusy)
		{
			RTPRINT(FBT, BT_TRACE, ("BT is non-idle\n"));
			BTDM_FWCoexAllOff(padapter);
			BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
			BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_OFF);
			btdm_DacSwing(padapter, BT_DACSWING_M4);
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("BT is idle\n"));
			btdm_DacSwing(padapter, BT_DACSWING_OFF);
		}
	}
}

u8 btdm_PANA2DPAction2Ant(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_DBG			pBtDbg = &pBTInfo->BtDbg;
	u8			bEnter = _FALSE;

	if (pBtDbg->dbgCtrl == _TRUE )
	{
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_PAN_A2DP)
			bEnter = _TRUE;
	}
	else
	{
		if (BTHCI_CheckProfileExist(padapter,BT_PROFILE_PAN) && BTHCI_CheckProfileExist(padapter,BT_PROFILE_A2DP))
			bEnter = _TRUE;
	}

	if (bEnter)
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], btdm_PANA2DPAction2Ant(), "));
		pHalData->bt_coexist.CurrentState |= (BT_COEX_STATE_PROFILE_PAN|BT_COEX_STATE_PROFILE_A2DP);

		if (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC4)
		{
			RTPRINT(FBT, BT_TRACE, ("[BC4]\n"));
			btdm_PANA2DPActionBC42Ant(padapter);
		}
		else if (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC8)
		{
			RTPRINT(FBT, BT_TRACE, ("[BC8]\n"));
			btdm_PANA2DPActionBC82Ant(padapter);
		}
		return _TRUE;
	}
	else
	{
		pHalData->bt_coexist.CurrentState &= ~(BT_COEX_STATE_PROFILE_PAN|BT_COEX_STATE_PROFILE_A2DP);
		return _FALSE;
	}
}

//============================================================
// extern function start with BTDM_
//============================================================

void BTDM_SwCoexAllOff92C(PADAPTER padapter)
{
	BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
	BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_OFF);
	btdm_DacSwing(padapter, BT_DACSWING_OFF);
}

void BTDM_SwCoexAllOff92D(PADAPTER padapter)
{
	BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
	BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_OFF);
	btdm_DacSwing(padapter, BT_DACSWING_OFF);
}

void
BTDM_DiminishWiFi(
	PADAPTER	padapter,
	u8			bDACOn,
	u8			bInterruptOn,
	u8			DACSwingLevel,
	u8			bNAVOn
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			H2C_Parameter[3] = {0};

	if (pHalData->bt_coexist.BT_Ant_Num != Ant_x2)
		return;

	if ((pHalData->bt_coexist.CurrentState & BT_COEX_STATE_BT_RSSI_LOW) &&
		(DACSwingLevel == 0x20))
	{
		RTPRINT(FBT, BT_TRACE, ("[BT]DiminishWiFi 0x20 original, but set 0x18 for Low RSSI!\n"));
		DACSwingLevel = 0x18;
	}

	H2C_Parameter[2] = 0;
	H2C_Parameter[1] = DACSwingLevel;
	H2C_Parameter[0] = 0;
	if (bDACOn)
	{
		H2C_Parameter[2] |= 0x01;	//BIT0
		if (bInterruptOn)
		{
			H2C_Parameter[2] |= 0x02;	//BIT1
		}
		pHalData->bt_coexist.bFWCoexistAllOff = _FALSE;
	}
	if (bNAVOn)
	{
		H2C_Parameter[2] |= 0x08;	//BIT3
		pHalData->bt_coexist.bFWCoexistAllOff = _FALSE;
	}

	RTPRINT(FBT, BT_TRACE, ("[DM][BT], bDACOn = %s, bInterruptOn = %s, write 0xe = 0x%x\n",
		bDACOn?"ON":"OFF", bInterruptOn?"ON":"OFF",
		H2C_Parameter[0]<<16|H2C_Parameter[1]<<8|H2C_Parameter[2]));
	RTPRINT(FBT, BT_TRACE, ("[DM][BT], bNAVOn = %s\n",
		bNAVOn?"ON":"OFF"));

	if (IS_HARDWARE_TYPE_8192C(padapter))
	{
		FillH2CCmd(padapter, 0xe, 3, H2C_Parameter);
	}
	else if (IS_HARDWARE_TYPE_8192D(padapter))
	{
		FillH2CCmd(padapter, 0x12, 3, H2C_Parameter);
	}
}

void BTDM_BTCoexistWithProfile2Ant(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bManualControl)
		return;

	RTPRINT(FIOCTL, IOCTL_BT_FLAG_MON, ("CurrentBTConnectionCnt=%d, BtOperationOn=%d, bBTConnectInProgress=%d !!\n",
		pBtMgnt->CurrentBTConnectionCnt, pBtMgnt->BtOperationOn, pBtMgnt->bBTConnectInProgress));

	if ((pHalData->bt_coexist.BluetoothCoexist) &&
		((pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC4) ||
		(pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC8)))
	{
		BTHCI_GetProfileNameMoto(padapter);
		BTHCI_GetBTRSSI(padapter);
		btdm_CheckBTState2Ant(padapter);
		BTDM_CheckWiFiState(padapter);

		if (btdm_SCOAction2Ant(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("Action SCO\n"));
		}
		else if (btdm_HIDAction2Ant(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("Action HID\n"));
		}
		else if (btdm_A2DPAction2Ant(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("Action A2DP\n"));
		}
		else if (btdm_PANAction2Ant(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("Action PAN\n"));
		}
		else if (btdm_HIDA2DPAction2Ant(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("Action HID_A2DP\n"));
		}
		else if (btdm_HIDPANAction2Ant(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("Action HID_PAN\n"));
		}
		else if (btdm_PANA2DPAction2Ant(padapter))
		{
			RTPRINT(FBT, BT_TRACE, ("Action PAN_A2DP\n"));
		}
		else
		{
			RTPRINT(FBT, BT_TRACE, ("No Action Matched \n"));
		}

		if (pHalData->bt_coexist.PreviousState != pHalData->bt_coexist.CurrentState)
		{
			RTPRINT(FBT, BT_TRACE, ("Coexist State change from 0x%"i64fmt"x to 0x%"i64fmt"x\n",
				pHalData->bt_coexist.PreviousState,
				pHalData->bt_coexist.CurrentState));
			pHalData->bt_coexist.PreviousState = pHalData->bt_coexist.CurrentState;

			RTPRINT(FBT, BT_TRACE, ("["));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_BT30)
				RTPRINT(FBT, BT_TRACE, ("BT 3.0, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_HT20)
				RTPRINT(FBT, BT_TRACE, ("HT20, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_HT40)
				RTPRINT(FBT, BT_TRACE, ("HT40, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_LEGACY)
				RTPRINT(FBT, BT_TRACE, ("Legacy, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_RSSI_LOW)
				RTPRINT(FBT, BT_TRACE, ("Rssi_Low, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_RSSI_MEDIUM)
				RTPRINT(FBT, BT_TRACE, ("Rssi_Mid, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_RSSI_HIGH)
				RTPRINT(FBT, BT_TRACE, ("Rssi_High, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_IDLE)
				RTPRINT(FBT, BT_TRACE, ("Wifi_Idle, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_UPLINK)
				RTPRINT(FBT, BT_TRACE, ("Wifi_Uplink, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_WIFI_DOWNLINK)
				RTPRINT(FBT, BT_TRACE, ("Wifi_Downlink, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_BT_IDLE)
				RTPRINT(FBT, BT_TRACE, ("BT_idle, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_BT_PAN_IDLE)
				RTPRINT(FBT, BT_TRACE, ("BT_PAN_idle, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_BT_PAN_UPLINK)
				RTPRINT(FBT, BT_TRACE, ("BT_PAN_uplink, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_BT_PAN_DOWNLINK)
				RTPRINT(FBT, BT_TRACE, ("BT_PAN_downlink, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_PROFILE_HID)
				RTPRINT(FBT, BT_TRACE, ("PRO_HID, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_PROFILE_A2DP)
				RTPRINT(FBT, BT_TRACE, ("PRO_A2DP, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_PROFILE_PAN)
				RTPRINT(FBT, BT_TRACE, ("PRO_PAN, "));
			if (pHalData->bt_coexist.CurrentState & BT_COEX_STATE_PROFILE_SCO)
				RTPRINT(FBT, BT_TRACE, ("PRO_SCO, "));
			RTPRINT(FBT, BT_TRACE, ("]\n"));
		}
	}
}

// ===== End of sync from SD7 driver HAL/BTCoexist/HalBtcCsr2Ant.c =====
#endif

#ifdef __HALBTCOEXIST_C__ // HAL/BTCoexist/HalBtCoexist.c
// ===== Below this line is sync from SD7 driver HAL/BTCoexist/HalBtCoexist.c =====

//============================================================
// local function
//============================================================
void btdm_BTCoexistWithProfile(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.BT_Ant_Num == Ant_x2)
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], 2 Ant mechanism\n"));
		BTDM_BTCoexistWithProfile2Ant(padapter);
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], 1 Ant mechanism\n"));
		BTDM_BTCoexistWithProfile1Ant(padapter);
	}
}

void btdm_ResetFWCoexState(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	pHalData->bt_coexist.CurrentState = 0;
	pHalData->bt_coexist.PreviousState = 0;
}

void btdm_InitBtCoexistDM(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);

	// 20100415 Joseph: Restore RF register 0x1E and 0x1F value for further usage.
	if (IS_HARDWARE_TYPE_8723A(padapter))
	{
		pHalData->bt_coexist.BtRfRegOrigin1E = PHY_QueryRFReg(padapter, PathA, RF_RCK1, bRFRegOffsetMask);
	}
	else if (IS_HARDWARE_TYPE_8192D(padapter))
	{
		pHalData->bt_coexist.BtRfRegOrigin1E = PHY_QueryRFReg(padapter, PathA, RF_RCK1, bRFRegOffsetMask);
	}
	else
	{
		pHalData->bt_coexist.BtRfRegOrigin1E = PHY_QueryRFReg(padapter, PathA, RF_RCK1, 0xf0);
	}
	pHalData->bt_coexist.BtRfRegOrigin1F = PHY_QueryRFReg(padapter, PathA, RF_RCK2, 0xf0);

	pHalData->bt_coexist.CurrentState = 0;
	pHalData->bt_coexist.PreviousState = 0;

	BTDM_8723AInit(padapter);
	pHalData->bt_coexist.bInitlized = _TRUE;
}
#if 0
void btdm_FWCoexAllOff92C(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.BT_Ant_Num == Ant_x2)
	{
		BTDM_Balance(padapter, _FALSE, 0, 0);
		BTDM_DiminishWiFi(padapter, _FALSE, _FALSE, 0, BT_FW_NAV_OFF);
	}
	else
	{
		BTDM_Balance(padapter, _FALSE, 0, 0);
		BTDM_SingleAnt(padapter, _FALSE, _FALSE, _FALSE);
	}
}

void btdm_FWCoexAllOff92D(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.BT_Ant_Num == Ant_x2)
	{
		BTDM_Balance(padapter, _FALSE, 0, 0);
		BTDM_DiminishWiFi(padapter, _FALSE, _FALSE, 0, BT_FW_NAV_OFF);
	}
	else
	{
		BTDM_Balance(padapter, _FALSE, 0, 0);
		BTDM_SingleAnt(padapter, _FALSE, _FALSE, _FALSE);
	}
}

void
btdm_BTCoexist8192C(
	PADAPTER	padapter
	)
{
	PMGNT_INFO		pMgntInfo = &(padapter->MgntInfo);
	PBT_MGNT		pBtMgnt=GET_BT_INFO(padapter)->BtMgnt;

	if (pBtMgnt->bSupportProfile)
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], profile notification co-exist mechanism\n"));
		btdm_BTCoexistWithProfile(padapter);
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], No profile notification!!\n"));
	}
}

u8 btdm_IsBTCoexistEnter(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	struct mlme_priv *pmlmepriv;
	struct mlme_ext_priv *pmlmeext;
	PBT_MGNT		pBtMgnt;
	PHAL_DATA_TYPE	pHalData;
	u8			bRet;


	pmlmepriv = &padapter->mlmepriv;
	pmlmeext = &padapter->mlmeextpriv;
	pHalData = GET_HAL_DATA(padapter);
	pBtMgnt = &pHalData->BtInfo.BtMgnt;
	bRet = _TRUE;

	RTPRINT(FBT, BT_TRACE, ("[DM][BT], padapter->interfaceIndex = %d\n",
		padapter->interfaceIndex));

	if (SINGLEMAC_SINGLEPHY == pHalData->MacPhyMode92D)
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], Single Mac & Single Phy\n"));
	}
	else if (DUALMAC_SINGLEPHY == pHalData->MacPhyMode92D)
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], Dual Mac & Single Phy, do nothing!\n"));
		bRet = _FALSE;
	}
	else if (DUALMAC_DUALPHY == pHalData->MacPhyMode92D)
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], Dual Mac & Dual Phy, do nothing!\n"));
		bRet = _FALSE;
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], Unknown Mac & Phy, do nothing!\n"));
		bRet = _FALSE;
	}

//	switch (pHalData->RF_Type)
	switch (pHalData->rf_type)
	{
		case RF_1T2R:
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], RF 1T2R\n"));
			break;
		case RF_2T4R:
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], RF 2T4R\n"));
			break;
		case RF_2T2R:
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], RF 2T2R\n"));
			break;
		case RF_1T1R:
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], RF 1T1R\n"));
			break;
		default:
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], Unknown RF type!\n"));
			bRet = _FALSE;
			break;
	}

	RTPRINT(FBT, BT_TRACE, ("[DM][BT], CurrentBssWirelessMode=%d, \
		dot11CurrentWirelessMode=%d, Hal CurrentWirelessMode=%d \n", \
		pMgntInfo->CurrentBssWirelessMode, pMgntInfo->dot11CurrentWirelessMode,
		pHalData->CurrentWirelessMode));

	if (WIRELESS_MODE_N_5G == pMgntInfo->dot11CurrentWirelessMode ||
		WIRELESS_MODE_A == pMgntInfo->dot11CurrentWirelessMode)
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], 5G or A band, do nothing and disable all bt coex mechanism!\n"));
		BTDM_CoexAllOff(padapter);
		bRet = _FALSE;
	}

	return bRet;
}

void btdm_BTCoexist8192D(PADAPTER padapter)
{
	PMGNT_INFO		pMgntInfo = &(padapter->MgntInfo);
	PBT_MGNT		pBtMgnt=GET_BT_INFO(padapter)->BtMgnt;
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);

	if (!btdm_IsBTCoexistEnter(padapter))
		return;

	if ((pBtMgnt->bSupportProfile) ||
		(pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC8))
	{
		if (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC8 &&
			!pBtMgnt->bSupportProfile)
		{
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], BTDM_Coexist(): Not specify condition\n"));
		}

		RTPRINT(FBT, BT_TRACE, ("[DM][BT], BTDM_CoexistWithProfile()\n"));
		btdm_BTCoexistWithProfile(padapter);
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], No profile notification!!\n"));
	}
}

void
btdm_AgcTable92d(
	PADAPTER	padapter,
	u8		type
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (type == BT_AGCTABLE_OFF)
	{
		RTPRINT(FBT, BT_TRACE, ("[BT]AGCTable Off!\n"));
		PHY_SetRFReg(padapter, PathA, RF_RX_G1, bRFRegOffsetMask, 0x30a99);
		PHY_SetRFReg(padapter, PathA, RF_RX_AGC_HP, bRFRegOffsetMask, 0xdc000);

		rtw_write32(padapter, 0xc78, 0x7B000001);
		rtw_write32(padapter, 0xc78, 0x7B010001);
		rtw_write32(padapter, 0xc78, 0x7B020001);
		rtw_write32(padapter, 0xc78, 0x7B030001);
		rtw_write32(padapter, 0xc78, 0x7B040001);
		rtw_write32(padapter, 0xc78, 0x7B050001);
		rtw_write32(padapter, 0xc78, 0x7B060001);
		rtw_write32(padapter, 0xc78, 0x7A070001);
		rtw_write32(padapter, 0xc78, 0x79080001);
		rtw_write32(padapter, 0xc78, 0x78090001);
		rtw_write32(padapter, 0xc78, 0x770A0001);
		rtw_write32(padapter, 0xc78, 0x760B0001);
		rtw_write32(padapter, 0xc78, 0x750C0001);
		rtw_write32(padapter, 0xc78, 0x740D0001);
		rtw_write32(padapter, 0xc78, 0x730E0001);
		rtw_write32(padapter, 0xc78, 0x720F0001);
		rtw_write32(padapter, 0xc78, 0x71100001);
		rtw_write32(padapter, 0xc78, 0x70110001);
		rtw_write32(padapter, 0xc78, 0x6F120001);
		rtw_write32(padapter, 0xc78, 0x6E130001);
		rtw_write32(padapter, 0xc78, 0x6D140001);
		rtw_write32(padapter, 0xc78, 0x6C150001);
		rtw_write32(padapter, 0xc78, 0x6B160001);
		rtw_write32(padapter, 0xc78, 0x6A170001);
		rtw_write32(padapter, 0xc78, 0x69180001);
		rtw_write32(padapter, 0xc78, 0x68190001);
		rtw_write32(padapter, 0xc78, 0x671A0001);
		rtw_write32(padapter, 0xc78, 0x661B0001);
		rtw_write32(padapter, 0xc78, 0x651C0001);
		rtw_write32(padapter, 0xc78, 0x641D0001);
		rtw_write32(padapter, 0xc78, 0x631E0001);
		rtw_write32(padapter, 0xc78, 0x621F0001);
		rtw_write32(padapter, 0xc78, 0x61200001);
		rtw_write32(padapter, 0xc78, 0x60210001);
		rtw_write32(padapter, 0xc78, 0x49220001);
		rtw_write32(padapter, 0xc78, 0x48230001);
		rtw_write32(padapter, 0xc78, 0x47240001);
		rtw_write32(padapter, 0xc78, 0x46250001);
		rtw_write32(padapter, 0xc78, 0x45260001);
		rtw_write32(padapter, 0xc78, 0x44270001);
		rtw_write32(padapter, 0xc78, 0x43280001);
		rtw_write32(padapter, 0xc78, 0x42290001);
		rtw_write32(padapter, 0xc78, 0x412A0001);
		rtw_write32(padapter, 0xc78, 0x402B0001);

		pHalData->bt_coexist.b92DAgcTableOn = _FALSE;
	}
	else if (type == BT_AGCTABLE_ON)
	{
		RTPRINT(FBT, BT_TRACE, ("[BT]AGCTable ON!\n"));
		PHY_SetRFReg(padapter, PathA, RF_RX_G1, bRFRegOffsetMask, 0xa99);
		PHY_SetRFReg(padapter, PathA, RF_RX_AGC_HP, bRFRegOffsetMask, 0xd4000);

		rtw_write32(padapter, 0xc78, 0x7b000001);
		rtw_write32(padapter, 0xc78, 0x7b010001);
		rtw_write32(padapter, 0xc78, 0x7b020001);
		rtw_write32(padapter, 0xc78, 0x7b030001);
		rtw_write32(padapter, 0xc78, 0x7b040001);
		rtw_write32(padapter, 0xc78, 0x7b050001);
		rtw_write32(padapter, 0xc78, 0x7b060001);
		rtw_write32(padapter, 0xc78, 0x7b070001);
		rtw_write32(padapter, 0xc78, 0x7b080001);
		rtw_write32(padapter, 0xc78, 0x7b090001);
		rtw_write32(padapter, 0xc78, 0x7b0A0001);
		rtw_write32(padapter, 0xc78, 0x7b0B0001);
		rtw_write32(padapter, 0xc78, 0x7a0C0001);
		rtw_write32(padapter, 0xc78, 0x790D0001);
		rtw_write32(padapter, 0xc78, 0x780E0001);
		rtw_write32(padapter, 0xc78, 0x770F0001);
		rtw_write32(padapter, 0xc78, 0x76100001);
		rtw_write32(padapter, 0xc78, 0x75110001);
		rtw_write32(padapter, 0xc78, 0x74120001);
		rtw_write32(padapter, 0xc78, 0x73130001);
		rtw_write32(padapter, 0xc78, 0x72140001);
		rtw_write32(padapter, 0xc78, 0x71150001);
		rtw_write32(padapter, 0xc78, 0x70160001);
		rtw_write32(padapter, 0xc78, 0x6f170001);
		rtw_write32(padapter, 0xc78, 0x6e180001);
		rtw_write32(padapter, 0xc78, 0x6d190001);
		rtw_write32(padapter, 0xc78, 0x6c1A0001);
		rtw_write32(padapter, 0xc78, 0x6b1B0001);
		rtw_write32(padapter, 0xc78, 0x6a1C0001);
		rtw_write32(padapter, 0xc78, 0x691D0001);
		rtw_write32(padapter, 0xc78, 0x4f1E0001);
		rtw_write32(padapter, 0xc78, 0x4e1F0001);
		rtw_write32(padapter, 0xc78, 0x4d200001);
		rtw_write32(padapter, 0xc78, 0x4c210001);
		rtw_write32(padapter, 0xc78, 0x4b220001);
		rtw_write32(padapter, 0xc78, 0x4a230001);
		rtw_write32(padapter, 0xc78, 0x49240001);
		rtw_write32(padapter, 0xc78, 0x48250001);
		rtw_write32(padapter, 0xc78, 0x47260001);
		rtw_write32(padapter, 0xc78, 0x46270001);
		rtw_write32(padapter, 0xc78, 0x45280001);
		rtw_write32(padapter, 0xc78, 0x44290001);
		rtw_write32(padapter, 0xc78, 0x432A0001);
		rtw_write32(padapter, 0xc78, 0x422B0001);

		pHalData->bt_coexist.b92DAgcTableOn = _TRUE;
		pHalData->bt_coexist.bSWCoexistAllOff = _FALSE;
	}
}
#endif
//============================================================
// extern function
//============================================================
void BTDM_CheckAntSelMode(PADAPTER padapter)
{
#if 0
	if (!IS_HARDWARE_TYPE_8192C(padapter))
		return;
	BTDM_CheckBTIdleChange1Ant(padapter);
#endif
}

u8 BTDM_NeedToRoamForBtEnableDisable(PADAPTER padapter)
{
#if 0
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(padapter);

	if (IS_HARDWARE_TYPE_8192D(padapter))
	{
		if (pHalData->bt_coexist.bNeedToRoamForBtDisableEnable)
		{
			pHalData->bt_coexist.bNeedToRoamForBtDisableEnable = _FALSE;
			RTPRINT(FBT, BT_TRACE, ("92D bt need to roam caused by bt enable/disable!!!\n"));
			return _TRUE;
		}
	}
#endif
	return _FALSE;
}

void BTDM_FwC2hBtRssi(PADAPTER padapter, u8 *tmpBuf)
{
	if (IS_HARDWARE_TYPE_8723A(padapter))
		BTDM_FwC2hBtRssi8723A(padapter, tmpBuf);
}

void BTDM_FwC2hBtInfo(PADAPTER padapter, u8 *tmpBuf, u8 length)
{
	if (IS_HARDWARE_TYPE_8723A(padapter))
		BTDM_FwC2hBtInfo8723A(padapter, tmpBuf, length);
}

void BTDM_DisplayBtCoexInfo(PADAPTER padapter)
{
	if (IS_HARDWARE_TYPE_8723A(padapter))
		BTDM_Display8723ABtCoexInfo(padapter);
}

void BTDM_RejectAPAggregatedPacket(PADAPTER padapter, u8 bReject)
{
#if 0
	PMGNT_INFO				pMgntInfo = &padapter->MgntInfo;
	PRT_HIGH_THROUGHPUT	pHTInfo = GET_HT_INFO(pMgntInfo);
	PRX_TS_RECORD			pRxTs = NULL;

	{
		if (bReject)
		{
			// Do not allow receiving A-MPDU aggregation.
			if (pMgntInfo->IOTPeer == HT_IOT_PEER_CISCO)
			{
				if (pHTInfo->bAcceptAddbaReq)
				{
					RTPRINT(FBT, BT_TRACE, ("BT_Disallow AMPDU \n"));
					pHTInfo->bAcceptAddbaReq = _FALSE;
					if (GetTs(padapter, (PTS_COMMON_INFO*)(&pRxTs), pMgntInfo->Bssid, 0, RX_DIR, _FALSE))
						TsInitDelBA(padapter, (PTS_COMMON_INFO)pRxTs, RX_DIR);
				}
			}
			else
			{
				if (!pHTInfo->bAcceptAddbaReq)
				{
					RTPRINT(FBT, BT_TRACE, ("BT_Allow AMPDU BT Idle\n"));
					pHTInfo->bAcceptAddbaReq = _TRUE;
				}
			}
		}
		else
		{
			if (pMgntInfo->IOTPeer == HT_IOT_PEER_CISCO)
			{
				if (!pHTInfo->bAcceptAddbaReq)
				{
					RTPRINT(FBT, BT_TRACE, ("BT_Allow AMPDU \n"));
					pHTInfo->bAcceptAddbaReq = _TRUE;
				}
			}
		}
	}
#endif
}

u8 BTDM_IsHT40(PADAPTER padapter)
{
	u8 isHT40 = _TRUE;
	HT_CHANNEL_WIDTH bw;

#if 0
	padapter->HalFunc.GetHwRegHandler(padapter, HW_VAR_BW_MODE, (pu8)(&bw));
#else
#if 1
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	bw = pHalData->CurrentChannelBW;
#else
	bw = padapter->mlmeextpriv.cur_bwmode;
#endif
#endif

	if (bw == HT_CHANNEL_WIDTH_20)
	{
		isHT40 = _FALSE;
	}
	else if (bw == HT_CHANNEL_WIDTH_40)
	{
		isHT40 = _TRUE;
	}

	return isHT40;
}

u8 BTDM_Legacy(PADAPTER padapter)
{
	struct mlme_ext_priv *pmlmeext;
	u8			isLegacy = _FALSE;

	pmlmeext = &padapter->mlmeextpriv;
	if ((pmlmeext->cur_wireless_mode == WIRELESS_11B) ||
		(pmlmeext->cur_wireless_mode == WIRELESS_11G) ||
		(pmlmeext->cur_wireless_mode == WIRELESS_11BG))
		isLegacy = _TRUE;

	return isLegacy;
}

void BTDM_CheckWiFiState(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData;
	struct mlme_priv *pmlmepriv;
	PBT30Info		pBTInfo;
	PBT_MGNT		pBtMgnt;


	pHalData = GET_HAL_DATA(padapter);
	pmlmepriv = &padapter->mlmepriv;
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;

	if (pmlmepriv->LinkDetectInfo.bBusyTraffic)
	{
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_IDLE;

		if (pmlmepriv->LinkDetectInfo.bTxBusyTraffic)
		{
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_UPLINK;
		}
		else
		{
			pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_UPLINK;
		}

		if (pmlmepriv->LinkDetectInfo.bRxBusyTraffic)
		{
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_DOWNLINK;
		}
		else
		{
			pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_DOWNLINK;
		}
	}
	else
	{
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_IDLE;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_UPLINK;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_DOWNLINK;
	}

	if (BTDM_Legacy(padapter))
	{
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_LEGACY;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_HT20;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_HT40;
	}
	else
	{
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_LEGACY;
		if (BTDM_IsHT40(padapter))
		{
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_HT40;
			pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_HT20;
		}
		else
		{
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_HT20;
			pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_HT40;
		}
	}

	if (pBtMgnt->BtOperationOn)
	{
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT30;
	}
	else
	{
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT30;
	}
}

s32 BTDM_GetRxSS(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	struct mlme_priv *pmlmepriv;
	PHAL_DATA_TYPE	pHalData;
	s32			UndecoratedSmoothedPWDB = 0;


	pmlmepriv = &padapter->mlmepriv;
	pHalData = GET_HAL_DATA(padapter);

//	if (pMgntInfo->bMediaConnect)	// Default port
	if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
	{
		UndecoratedSmoothedPWDB = GET_UNDECORATED_AVERAGE_RSSI(padapter);
	}
	else // associated entry pwdb
	{
		UndecoratedSmoothedPWDB = pHalData->dmpriv.EntryMinUndecoratedSmoothedPWDB;
		//pHalData->BT_EntryMinUndecoratedSmoothedPWDB
	}
	RTPRINT(FBT, BT_TRACE, ("BTDM_GetRxSS() = %d\n", UndecoratedSmoothedPWDB));
	return UndecoratedSmoothedPWDB;
}

s32 BTDM_GetRxBeaconSS(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	struct mlme_priv *pmlmepriv;
	PHAL_DATA_TYPE	pHalData;
	s32			pwdbBeacon = 0;


	pmlmepriv = &padapter->mlmepriv;
	pHalData = GET_HAL_DATA(padapter);

//	if (pMgntInfo->bMediaConnect)	// Default port
	if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
	{
		//pwdbBeacon = pHalData->dmpriv.UndecoratedSmoothedBeacon;
		pwdbBeacon= pHalData->dmpriv.EntryMinUndecoratedSmoothedPWDB;
	}
	RTPRINT(FBT, BT_TRACE, ("BTDM_GetRxBeaconSS() = %d\n", pwdbBeacon));
	return pwdbBeacon;
}

// Get beacon rssi state
u8
BTDM_CheckCoexBcnRssiState(
	PADAPTER	padapter,
	u8			levelNum,
	u8			RssiThresh,
	u8			RssiThresh1
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	s32			pwdbBeacon = 0;
	u8			bcnRssiState;

	pwdbBeacon = BTDM_GetRxBeaconSS(padapter);

	if (levelNum == 2)
	{
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_MEDIUM;

		if ((pHalData->bt_coexist.preRssiStateBeacon == BT_RSSI_STATE_LOW) ||
			(pHalData->bt_coexist.preRssiStateBeacon == BT_RSSI_STATE_STAY_LOW))
		{
			if (pwdbBeacon >= (RssiThresh+BT_FW_COEX_THRESH_TOL))
			{
				bcnRssiState = BT_RSSI_STATE_HIGH;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_BEACON_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state switch to High\n"));
			}
			else
			{
				bcnRssiState = BT_RSSI_STATE_STAY_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state stay at Low\n"));
			}
		}
		else
		{
			if (pwdbBeacon < RssiThresh)
			{
				bcnRssiState = BT_RSSI_STATE_LOW;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_BEACON_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state switch to Low\n"));
			}
			else
			{
				bcnRssiState = BT_RSSI_STATE_STAY_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state stay at High\n"));
			}
		}
	}
	else if (levelNum == 3)
	{
		if (RssiThresh > RssiThresh1)
		{
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON thresh error!!\n"));
			return pHalData->bt_coexist.preRssiStateBeacon;
		}

		if ((pHalData->bt_coexist.preRssiStateBeacon == BT_RSSI_STATE_LOW) ||
			(pHalData->bt_coexist.preRssiStateBeacon == BT_RSSI_STATE_STAY_LOW))
		{
			if (pwdbBeacon >= (RssiThresh+BT_FW_COEX_THRESH_TOL))
			{
				bcnRssiState = BT_RSSI_STATE_MEDIUM;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_BEACON_MEDIUM;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state switch to Medium\n"));
			}
			else
			{
				bcnRssiState = BT_RSSI_STATE_STAY_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state stay at Low\n"));
			}
		}
		else if ((pHalData->bt_coexist.preRssiStateBeacon == BT_RSSI_STATE_MEDIUM) ||
			(pHalData->bt_coexist.preRssiStateBeacon == BT_RSSI_STATE_STAY_MEDIUM))
		{
			if (pwdbBeacon >= (RssiThresh1+BT_FW_COEX_THRESH_TOL))
			{
				bcnRssiState = BT_RSSI_STATE_HIGH;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_BEACON_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_MEDIUM;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state switch to High\n"));
			}
			else if (pwdbBeacon < RssiThresh)
			{
				bcnRssiState = BT_RSSI_STATE_LOW;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_BEACON_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_MEDIUM;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state switch to Low\n"));
			}
			else
			{
				bcnRssiState = BT_RSSI_STATE_STAY_MEDIUM;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state stay at Medium\n"));
			}
		}
		else
		{
			if (pwdbBeacon < RssiThresh1)
			{
				bcnRssiState = BT_RSSI_STATE_MEDIUM;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_BEACON_MEDIUM;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state switch to Medium\n"));
			}
			else
			{
				bcnRssiState = BT_RSSI_STATE_STAY_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state stay at High\n"));
			}
		}
	}

	pHalData->bt_coexist.preRssiStateBeacon = bcnRssiState;

	return bcnRssiState;
}

u8
BTDM_CheckCoexRSSIState1(
	PADAPTER	padapter,
	u8			levelNum,
	u8			RssiThresh,
	u8			RssiThresh1
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	s32			UndecoratedSmoothedPWDB = 0;
	u8			btRssiState;

	UndecoratedSmoothedPWDB = BTDM_GetRxSS(padapter);

	if (levelNum == 2)
	{
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_MEDIUM;

		if ((pHalData->bt_coexist.preRssiState1 == BT_RSSI_STATE_LOW) ||
			(pHalData->bt_coexist.preRssiState1 == BT_RSSI_STATE_STAY_LOW))
		{
			if (UndecoratedSmoothedPWDB >= (RssiThresh+BT_FW_COEX_THRESH_TOL))
			{
				btRssiState = BT_RSSI_STATE_HIGH;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_1_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state switch to High\n"));
			}
			else
			{
				btRssiState = BT_RSSI_STATE_STAY_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state stay at Low\n"));
			}
		}
		else
		{
			if (UndecoratedSmoothedPWDB < RssiThresh)
			{
				btRssiState = BT_RSSI_STATE_LOW;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_1_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state switch to Low\n"));
			}
			else
			{
				btRssiState = BT_RSSI_STATE_STAY_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state stay at High\n"));
			}
		}
	}
	else if (levelNum == 3)
	{
		if (RssiThresh > RssiThresh1)
		{
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 thresh error!!\n"));
			return pHalData->bt_coexist.preRssiState1;
		}

		if ((pHalData->bt_coexist.preRssiState1 == BT_RSSI_STATE_LOW) ||
			(pHalData->bt_coexist.preRssiState1 == BT_RSSI_STATE_STAY_LOW))
		{
			if (UndecoratedSmoothedPWDB >= (RssiThresh+BT_FW_COEX_THRESH_TOL))
			{
				btRssiState = BT_RSSI_STATE_MEDIUM;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_1_MEDIUM;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state switch to Medium\n"));
			}
			else
			{
				btRssiState = BT_RSSI_STATE_STAY_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state stay at Low\n"));
			}
		}
		else if ((pHalData->bt_coexist.preRssiState1 == BT_RSSI_STATE_MEDIUM) ||
			(pHalData->bt_coexist.preRssiState1 == BT_RSSI_STATE_STAY_MEDIUM))
		{
			if (UndecoratedSmoothedPWDB >= (RssiThresh1+BT_FW_COEX_THRESH_TOL))
			{
				btRssiState = BT_RSSI_STATE_HIGH;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_1_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_MEDIUM;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state switch to High\n"));
			}
			else if (UndecoratedSmoothedPWDB < RssiThresh)
			{
				btRssiState = BT_RSSI_STATE_LOW;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_1_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_MEDIUM;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state switch to Low\n"));
			}
			else
			{
				btRssiState = BT_RSSI_STATE_STAY_MEDIUM;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state stay at Medium\n"));
			}
		}
		else
		{
			if (UndecoratedSmoothedPWDB < RssiThresh1)
			{
				btRssiState = BT_RSSI_STATE_MEDIUM;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_1_MEDIUM;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state switch to Medium\n"));
			}
			else
			{
				btRssiState = BT_RSSI_STATE_STAY_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state stay at High\n"));
			}
		}
	}

	pHalData->bt_coexist.preRssiState1 = btRssiState;

	return btRssiState;
}

u8
BTDM_CheckCoexRSSIState(
	PADAPTER	padapter,
	u8			levelNum,
	u8			RssiThresh,
	u8			RssiThresh1
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	s32			UndecoratedSmoothedPWDB = 0;
	u8			btRssiState;

	UndecoratedSmoothedPWDB = BTDM_GetRxSS(padapter);

	if (levelNum == 2)
	{
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_MEDIUM;

		if ((pHalData->bt_coexist.preRssiState == BT_RSSI_STATE_LOW) ||
			(pHalData->bt_coexist.preRssiState == BT_RSSI_STATE_STAY_LOW))
		{
			if (UndecoratedSmoothedPWDB >= (RssiThresh+BT_FW_COEX_THRESH_TOL))
			{
				btRssiState = BT_RSSI_STATE_HIGH;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state switch to High\n"));
			}
			else
			{
				btRssiState = BT_RSSI_STATE_STAY_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state stay at Low\n"));
			}
		}
		else
		{
			if (UndecoratedSmoothedPWDB < RssiThresh)
			{
				btRssiState = BT_RSSI_STATE_LOW;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state switch to Low\n"));
			}
			else
			{
				btRssiState = BT_RSSI_STATE_STAY_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state stay at High\n"));
			}
		}
	}
	else if (levelNum == 3)
	{
		if (RssiThresh > RssiThresh1)
		{
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI thresh error!!\n"));
			return pHalData->bt_coexist.preRssiState;
		}

		if ((pHalData->bt_coexist.preRssiState == BT_RSSI_STATE_LOW) ||
			(pHalData->bt_coexist.preRssiState == BT_RSSI_STATE_STAY_LOW))
		{
			if (UndecoratedSmoothedPWDB >= (RssiThresh+BT_FW_COEX_THRESH_TOL))
			{
				btRssiState = BT_RSSI_STATE_MEDIUM;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_MEDIUM;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state switch to Medium\n"));
			}
			else
			{
				btRssiState = BT_RSSI_STATE_STAY_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state stay at Low\n"));
			}
		}
		else if ((pHalData->bt_coexist.preRssiState == BT_RSSI_STATE_MEDIUM) ||
			(pHalData->bt_coexist.preRssiState == BT_RSSI_STATE_STAY_MEDIUM))
		{
			if (UndecoratedSmoothedPWDB >= (RssiThresh1+BT_FW_COEX_THRESH_TOL))
			{
				btRssiState = BT_RSSI_STATE_HIGH;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_MEDIUM;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state switch to High\n"));
			}
			else if (UndecoratedSmoothedPWDB < RssiThresh)
			{
				btRssiState = BT_RSSI_STATE_LOW;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_MEDIUM;
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state switch to Low\n"));
			}
			else
			{
				btRssiState = BT_RSSI_STATE_STAY_MEDIUM;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state stay at Medium\n"));
			}
		}
		else
		{
			if (UndecoratedSmoothedPWDB < RssiThresh1)
			{
				btRssiState = BT_RSSI_STATE_MEDIUM;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_MEDIUM;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state switch to Medium\n"));
			}
			else
			{
				btRssiState = BT_RSSI_STATE_STAY_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state stay at High\n"));
			}
		}
	}

	pHalData->bt_coexist.preRssiState = btRssiState;

	return btRssiState;
}

u8 BTDM_DisableEDCATurbo(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	PBT_MGNT		pBtMgnt;
	PHAL_DATA_TYPE	pHalData;
	u8			bBtChangeEDCA = _FALSE;
	u32			EDCA_BT_BE = 0x5ea42b, cur_EDCA_reg;
	u16			aggr_num;
	u8			bRet = _FALSE;


	pHalData = GET_HAL_DATA(padapter);
	pBtMgnt = &pHalData->BtInfo.BtMgnt;

	if (!pHalData->bt_coexist.BluetoothCoexist)
	{
		bRet = _FALSE;
		pHalData->bt_coexist.lastBtEdca = 0;
		return bRet;
	}
	if (!((pBtMgnt->bSupportProfile) ||
		(pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC8)))
	{
		bRet = _FALSE;
		pHalData->bt_coexist.lastBtEdca = 0;
		return bRet;
	}

	if (BT_1Ant(padapter))
	{
		bRet = _FALSE;
		pHalData->bt_coexist.lastBtEdca = 0;
		return bRet;
	}

	if (pHalData->bt_coexist.exec_cnt < 3)
		pHalData->bt_coexist.exec_cnt++;
	else
		pHalData->bt_coexist.bEDCAInitialized = _TRUE;

	// When BT is non idle
	if (!(pHalData->bt_coexist.CurrentState & BT_COEX_STATE_BT_IDLE))
	{
		RTPRINT(FBT, BT_TRACE, ("BT state non idle, set bt EDCA\n"));

		//aggr_num = 0x0909;
		if (pHalData->odmpriv.DM_EDCA_Table.bCurrentTurboEDCA == _TRUE)
		{
			bBtChangeEDCA = _TRUE;
			pHalData->odmpriv.DM_EDCA_Table.bCurrentTurboEDCA = _FALSE;
//			pHalData->bIsCurRDLState = _FALSE;
			pHalData->dmpriv.prv_traffic_idx = 3;
		}
		cur_EDCA_reg = rtw_read32(padapter, REG_EDCA_BE_PARAM);

		if (cur_EDCA_reg != EDCA_BT_BE)
		{
			bBtChangeEDCA = _TRUE;
		}
		if (bBtChangeEDCA || !pHalData->bt_coexist.bEDCAInitialized)
		{
			rtw_write32(padapter, REG_EDCA_BE_PARAM, EDCA_BT_BE);
			pHalData->bt_coexist.lastBtEdca = EDCA_BT_BE;
		}
		bRet = _TRUE;
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("BT state idle, set original EDCA\n"));
		pHalData->bt_coexist.lastBtEdca = 0;
		bRet = _FALSE;
	}

#ifdef CONFIG_PCI_HCI
	if (IS_HARDWARE_TYPE_8192C(padapter))
	{
		// When BT is non idle
		if (!(pHalData->bt_coexist.CurrentState & BT_COEX_STATE_BT_IDLE))
		{
			aggr_num = 0x0909;
		}
		else
		{
			aggr_num = 0x0A0A;
		}

		if ((pHalData->bt_coexist.last_aggr_num != aggr_num) || !pHalData->bt_coexist.bEDCAInitialized)
		{
			RTPRINT(FBT, BT_TRACE, ("BT write AGGR NUM = 0x%x\n", aggr_num));
			rtw_write16(padapter, REG_MAX_AGGR_NUM, aggr_num);
			pHalData->bt_coexist.last_aggr_num = aggr_num;
		}
	}
#endif

	return bRet;
}

void
BTDM_Balance(
	PADAPTER	padapter,
	u8			bBalanceOn,
	u8			ms0,
	u8			ms1
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8	H2C_Parameter[3] = {0};

	if (bBalanceOn)
	{
		H2C_Parameter[2] = 1;
		H2C_Parameter[1] = ms1;
		H2C_Parameter[0] = ms0;
		pHalData->bt_coexist.bFWCoexistAllOff = _FALSE;
	}
	else
	{
		H2C_Parameter[2] = 0;
		H2C_Parameter[1] = 0;
		H2C_Parameter[0] = 0;
	}
	pHalData->bt_coexist.bBalanceOn = bBalanceOn;

	RTPRINT(FBT, BT_TRACE, ("[DM][BT], Balance=[%s:%dms:%dms], write 0xc=0x%x\n",
		bBalanceOn?"ON":"OFF", ms0, ms1,
		H2C_Parameter[0]<<16|H2C_Parameter[1]<<8|H2C_Parameter[2]));

	FillH2CCmd(padapter, 0xc, 3, H2C_Parameter);
}

void BTDM_AGCTable(PADAPTER padapter, u8 type)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
#if 0
	if (IS_HARDWARE_TYPE_8192D(padapter))
	{
		btdm_AgcTable92d(padapter, type);
		return;
	}
#endif
	if (type == BT_AGCTABLE_OFF)
	{
		RTPRINT(FBT, BT_TRACE, ("[BT]AGCTable Off!\n"));
		rtw_write32(padapter, 0xc78,0x641c0001);
		rtw_write32(padapter, 0xc78,0x631d0001);
		rtw_write32(padapter, 0xc78,0x621e0001);
		rtw_write32(padapter, 0xc78,0x611f0001);
		rtw_write32(padapter, 0xc78,0x60200001);

		PHY_SetRFReg(padapter, PathA, RF_RX_AGC_HP, bRFRegOffsetMask, 0x32000);
		PHY_SetRFReg(padapter, PathA, RF_RX_AGC_HP, bRFRegOffsetMask, 0x71000);
		PHY_SetRFReg(padapter, PathA, RF_RX_AGC_HP, bRFRegOffsetMask, 0xb0000);
		PHY_SetRFReg(padapter, PathA, RF_RX_AGC_HP, bRFRegOffsetMask, 0xfc000);
		if (IS_HARDWARE_TYPE_8723A(padapter))
			PHY_SetRFReg(padapter, PathA, RF_RX_G1, bRFRegOffsetMask, 0x30355);
		else
			PHY_SetRFReg(padapter, PathA, RF_RX_G1, bRFRegOffsetMask, 0x10255);

		if (IS_HARDWARE_TYPE_8723A(padapter))
			pHalData->bt_coexist.b8723aAgcTableOn = _FALSE;
	}
	else if (type == BT_AGCTABLE_ON)
	{
		RTPRINT(FBT, BT_TRACE, ("[BT]AGCTable On!\n"));
		rtw_write32(padapter, 0xc78,0x4e1c0001);
		rtw_write32(padapter, 0xc78,0x4d1d0001);
		rtw_write32(padapter, 0xc78,0x4c1e0001);
		rtw_write32(padapter, 0xc78,0x4b1f0001);
		rtw_write32(padapter, 0xc78,0x4a200001);

		PHY_SetRFReg(padapter, PathA, RF_RX_AGC_HP, bRFRegOffsetMask, 0xdc000);
		PHY_SetRFReg(padapter, PathA, RF_RX_AGC_HP, bRFRegOffsetMask, 0x90000);
		PHY_SetRFReg(padapter, PathA, RF_RX_AGC_HP, bRFRegOffsetMask, 0x51000);
		PHY_SetRFReg(padapter, PathA, RF_RX_AGC_HP, bRFRegOffsetMask, 0x12000);
		if (IS_HARDWARE_TYPE_8723A(padapter))
			PHY_SetRFReg(padapter, PathA, RF_RX_G1, bRFRegOffsetMask, 0x00355);
		else
			PHY_SetRFReg(padapter, PathA, RF_RX_G1, bRFRegOffsetMask, 0x00255);

		if (IS_HARDWARE_TYPE_8723A(padapter))
			pHalData->bt_coexist.b8723aAgcTableOn = _TRUE;

		pHalData->bt_coexist.bSWCoexistAllOff = _FALSE;
	}
}

void BTDM_BBBackOffLevel(PADAPTER padapter, u8 type)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (type == BT_BB_BACKOFF_OFF)
	{
		RTPRINT(FBT, BT_TRACE, ("[BT]BBBackOffLevel Off!\n"));
		rtw_write32(padapter, 0xc04,0x3a05611);
	}
	else if (type == BT_BB_BACKOFF_ON)
	{
		RTPRINT(FBT, BT_TRACE, ("[BT]BBBackOffLevel On!\n"));
		rtw_write32(padapter, 0xc04,0x3a07611);
		pHalData->bt_coexist.bSWCoexistAllOff = _FALSE;
	}
}

void BTDM_FWCoexAllOff(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);;


	RTPRINT(FBT, BT_TRACE, ("BTDM_FWCoexAllOff()\n"));
#if 0
	if (!pBtMgnt->bSupportProfile)
		return;
#endif
	if (pHalData->bt_coexist.bFWCoexistAllOff)
		return;
	RTPRINT(FBT, BT_TRACE, ("BTDM_FWCoexAllOff(), real Do\n"));

#if 0
	if (IS_HARDWARE_TYPE_8192C(padapter))
	{
		btdm_FWCoexAllOff92C(padapter);
	}
	else if (IS_HARDWARE_TYPE_8192D(padapter))
	{
		btdm_FWCoexAllOff92D(padapter);
	}
	else if (IS_HARDWARE_TYPE_8723A(padapter))
	{
		BTDM_FWCoexAllOff8723A(padapter);
	}
#else
	BTDM_FWCoexAllOff8723A(padapter);
#endif

	pHalData->bt_coexist.bFWCoexistAllOff = _TRUE;
}

void BTDM_SWCoexAllOff(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);;


	RTPRINT(FBT, BT_TRACE, ("BTDM_SWCoexAllOff()\n"));
#if 0
	if (!pBtMgnt->bSupportProfile)
		return;
#endif
	if (pHalData->bt_coexist.bSWCoexistAllOff)
		return;
	RTPRINT(FBT, BT_TRACE, ("BTDM_SWCoexAllOff(), real Do\n"));
#if 0
	if (IS_HARDWARE_TYPE_8192C(padapter))
	{
		BTDM_SwCoexAllOff92C(padapter);
	}
	else if (IS_HARDWARE_TYPE_8192D(padapter))
	{
		BTDM_SwCoexAllOff92D(padapter);
	}
	else if (IS_HARDWARE_TYPE_8723A(padapter))
	{
		BTDM_SWCoexAllOff8723A(padapter);
	}
#else
	BTDM_SWCoexAllOff8723A(padapter);
#endif

	pHalData->bt_coexist.bSWCoexistAllOff = _TRUE;
}

void BTDM_HWCoexAllOff(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);;


	RTPRINT(FBT, BT_TRACE, ("BTDM_HWCoexAllOff()\n"));
#if 0
	if (!pBtMgnt->bSupportProfile)
		return;
#endif
	if (pHalData->bt_coexist.bHWCoexistAllOff)
		return;
	RTPRINT(FBT, BT_TRACE, ("BTDM_HWCoexAllOff(), real Do\n"));

	if (IS_HARDWARE_TYPE_8723A(padapter))
	{
		BTDM_HWCoexAllOff8723A(padapter);
	}

	pHalData->bt_coexist.bHWCoexistAllOff = _TRUE;
}

void BTDM_CoexAllOff(PADAPTER padapter)
{
	BTDM_FWCoexAllOff(padapter);
	BTDM_SWCoexAllOff(padapter);
	BTDM_HWCoexAllOff(padapter);
}

void BTDM_TurnOffBtCoexistBeforeEnterLPS(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
//	PRT_POWER_SAVE_CONTROL	pPSC = GET_POWER_SAVE_CONTROL(pMgntInfo);
	struct pwrctrl_priv *ppwrctrl = &padapter->pwrctrlpriv;


	// Add temporarily.
	if ((!pHalData->bt_coexist.BluetoothCoexist) ||(!pBtMgnt->bSupportProfile))
		return;

	// 8723 1Ant doesn't need to turn off bt coexist mechanism.
	if (BTDM_1Ant8723A(padapter))
		return;

	if (IS_HARDWARE_TYPE_8192C(padapter) ||
		IS_HARDWARE_TYPE_8192D(padapter) ||
		IS_HARDWARE_TYPE_8723A(padapter))
	{
		//
		// Before enter LPS, turn off FW BT Co-exist mechanism
		//
		if (ppwrctrl->bLeisurePs)
		{
			RTPRINT(FBT, BT_TRACE, ("[BT][DM], Before enter LPS, turn off all Coexist DM\n"));
			btdm_ResetFWCoexState(padapter);
			BTDM_CoexAllOff(padapter);
			BTDM_SetAntenna(padapter, BTDM_ANT_BT);
		}
	}
}

void BTDM_TurnOffBtCoexistBeforeEnterIPS(PADAPTER padapter)
{
//	PMGNT_INFO		pMgntInfo = &padapter->MgntInfo;
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
//	PRT_POWER_SAVE_CONTROL	pPSC = GET_POWER_SAVE_CONTROL(pMgntInfo);
	struct pwrctrl_priv *ppwrctrl = &padapter->pwrctrlpriv;

	if (!pHalData->bt_coexist.BluetoothCoexist) 
		return;

	// 8723 1Ant doesn't need to turn off bt coexist mechanism.
	if (BTDM_1Ant8723A(padapter))
		return;

	if (IS_HARDWARE_TYPE_8192C(padapter) ||
		IS_HARDWARE_TYPE_8192D(padapter) ||
		IS_HARDWARE_TYPE_8723A(padapter))
	{
		//
		// Before enter IPS, turn off FW BT Co-exist mechanism
		//
//		if (pPSC->bInactivePs)
		if (ppwrctrl->reg_rfoff == rf_on)
		{
			RTPRINT(FBT, BT_TRACE, ("[BT][DM], Before enter IPS, turn off all Coexist DM\n"));
			btdm_ResetFWCoexState(padapter);
			BTDM_CoexAllOff(padapter);
			BTDM_SetAntenna(padapter, BTDM_ANT_BT);
		}
	}
}

void BTDM_Coexist(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);

	if (!pHalData->bt_coexist.BluetoothCoexist)
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], BT not exists!!\n"));
		return;
	}

	if (!pHalData->bt_coexist.bInitlized)
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], btdm_InitBtCoexistDM()\n"));
		btdm_InitBtCoexistDM(padapter);
	}

	RTPRINT(FBT, BT_TRACE, ("\n\n[DM][BT], BTDM start!!\n"));

	BTDM_PWDBMonitor(padapter);

	if (IS_HARDWARE_TYPE_8723A(padapter))
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], HW type is 8723\n"));
		BTDM_BTCoexist8723A(padapter);
	}
#if 0
	else if (IS_HARDWARE_TYPE_8192C(padapter))
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], HW type is 88C\n"));
		btdm_BTCoexist8192C(padapter);
	}
	else if (IS_HARDWARE_TYPE_8192D(padapter))
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], HW type is 92D\n"));
		btdm_BTCoexist8192D(padapter);
	}
#endif
	RTPRINT(FBT, BT_TRACE, ("[DM][BT], BTDM end!!\n\n"));
}

void BTDM_UpdateCoexState(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);

	if (!BTDM_IsSameCoexistState(padapter))
	{	
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], Coexist State[bitMap] change from 0x%"i64fmt"x to 0x%"i64fmt"x,  changeBits=0x%"i64fmt"x\n", 
			pHalData->bt_coexist.PreviousState,
			pHalData->bt_coexist.CurrentState,
			(pHalData->bt_coexist.PreviousState^pHalData->bt_coexist.CurrentState)));
		pHalData->bt_coexist.PreviousState = pHalData->bt_coexist.CurrentState;
	}
}

u8 BTDM_IsSameCoexistState(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
	{
		return _TRUE;
	}
	else
	{
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], Coexist state changed!!\n"));
		return _FALSE;
	}
}

void BTDM_PWDBMonitor(PADAPTER padapter)
{
//	PMGNT_INFO 		pMgntInfo = &(GetDefaultAdapter(padapter)->MgntInfo);
	PBT30Info		pBTInfo = GET_BT_INFO(GetDefaultAdapter(padapter));
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			H2C_Parameter[3] = {0};
	s32			tmpBTEntryMaxPWDB=0, tmpBTEntryMinPWDB=0xff;
	u8			i;

	if (pBtMgnt->BtOperationOn)
	{
		for (i = 0; i < MAX_BT_ASOC_ENTRY_NUM; i++)
		{
			if (pBTInfo->BtAsocEntry[i].bUsed)
			{
				if (pBTInfo->BtAsocEntry[i].UndecoratedSmoothedPWDB < tmpBTEntryMinPWDB)
					tmpBTEntryMinPWDB = pBTInfo->BtAsocEntry[i].UndecoratedSmoothedPWDB;
				if (pBTInfo->BtAsocEntry[i].UndecoratedSmoothedPWDB > tmpBTEntryMaxPWDB)
					tmpBTEntryMaxPWDB = pBTInfo->BtAsocEntry[i].UndecoratedSmoothedPWDB;

				//
				// Report every BT connection (HS mode) RSSI to FW
				//
				H2C_Parameter[2] = (u8)(pBTInfo->BtAsocEntry[i].UndecoratedSmoothedPWDB & 0xFF);
				H2C_Parameter[0] = (MAX_FW_SUPPORT_MACID_NUM-1-i);
				RTPRINT(FDM, DM_BT30, ("RSSI report for BT[%d], H2C_Par = 0x%x\n", i, H2C_Parameter[0]));
				FillH2CCmd(padapter, RSSI_SETTING_EID, 3, H2C_Parameter);
				RTPRINT_ADDR(FDM, (DM_PWDB|DM_BT30), ("BT_Entry Mac :"), pBTInfo->BtAsocEntry[i].BTRemoteMACAddr)
				RTPRINT(FDM, (DM_PWDB|DM_BT30), ("BT rx pwdb[%d] = 0x%x(%d)\n", i, pBTInfo->BtAsocEntry[i].UndecoratedSmoothedPWDB,
					pBTInfo->BtAsocEntry[i].UndecoratedSmoothedPWDB));
			}
		}
		if (tmpBTEntryMaxPWDB != 0)	// If associated entry is found
		{
			pHalData->dmpriv.BT_EntryMaxUndecoratedSmoothedPWDB = tmpBTEntryMaxPWDB;
			RTPRINT(FDM, (DM_PWDB|DM_BT30), ("BT_EntryMaxPWDB = 0x%x(%d)\n",
				tmpBTEntryMaxPWDB, tmpBTEntryMaxPWDB));
		}
		else
		{
			pHalData->dmpriv.BT_EntryMaxUndecoratedSmoothedPWDB = 0;
		}
		if (tmpBTEntryMinPWDB != 0xff) // If associated entry is found
		{
			pHalData->dmpriv.BT_EntryMinUndecoratedSmoothedPWDB = tmpBTEntryMinPWDB;
			RTPRINT(FDM, (DM_PWDB|DM_BT30), ("BT_EntryMinPWDB = 0x%x(%d)\n",
				tmpBTEntryMinPWDB, tmpBTEntryMinPWDB));
		}
		else
		{
			pHalData->dmpriv.BT_EntryMinUndecoratedSmoothedPWDB = 0;
		}
	}
}

u8 BTDM_DigByBtRssi(PADAPTER padapter)
{
//	PMGNT_INFO 		pMgntInfo = &(GetDefaultAdapter(padapter)->MgntInfo);
	PBT30Info		pBTInfo = GET_BT_INFO(GetDefaultAdapter(padapter));
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8				bRet = _FALSE;
	PDM_ODM_T		pDM_OutSrc = &pHalData->odmpriv;
	u8				digForBtHs=0, cckCcaThres=0;


	//
	// When running under HS mode, use bt related Dig and cck threshold.
	//
	if (pBtMgnt->BtOperationOn)
	{
		if (pBtMgnt->bBTConnectInProgress)
		{
			if (IS_HARDWARE_TYPE_8723A(padapter))
				digForBtHs = 0x28;
			else
				digForBtHs = 0x22;
		}
		else
		{
			//
			// Decide DIG value by BT RSSI.
			//
			digForBtHs = (u8)pHalData->dmpriv.BT_EntryMinUndecoratedSmoothedPWDB;

			if (IS_HARDWARE_TYPE_8723A(padapter))
				digForBtHs += 0x04;

			if (digForBtHs > DM_DIG_MAX_NIC)
				digForBtHs = DM_DIG_MAX_NIC;
			if (digForBtHs < DM_DIG_MIN_NIC)
				digForBtHs = DM_DIG_MIN_NIC;

			RTPRINT(FDM, DM_BT30, ("BTDM_DigByBtRssi(), digForBtHs=0x%x\n",
					digForBtHs));
		}
		ODM_Write_DIG(pDM_OutSrc, digForBtHs);

		//
		// Decide cck packet threshold
		//
		cckCcaThres = 0xcd;
		ODM_Write_CCK_CCA_Thres(pDM_OutSrc, cckCcaThres);

		bRet = _TRUE;
	}

	return bRet;
}

u8 BTDM_IsBTBusy(PADAPTER padapter)
{
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bBTBusy)
		return _TRUE;
	else
		return _FALSE;
}

u8 BTDM_IsWifiBusy(PADAPTER padapter)
{
//	PMGNT_INFO 		pMgntInfo = &(GetDefaultAdapter(padapter)->MgntInfo);
	struct mlme_priv *pmlmepriv = &(GetDefaultAdapter(padapter)->mlmepriv);
	PBT30Info		pBTInfo = GET_BT_INFO(padapter);
	PBT_TRAFFIC 	pBtTraffic = &pBTInfo->BtTraffic;


	if (pmlmepriv->LinkDetectInfo.bBusyTraffic ||
		pBtTraffic->Bt30TrafficStatistics.bTxBusyTraffic ||
		pBtTraffic->Bt30TrafficStatistics.bRxBusyTraffic)
		return _TRUE;
	else
		return _FALSE;
}

u8 BTDM_IsCoexistStateChanged(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
		return _FALSE;
	else
		return _TRUE;
}

u8 BTDM_IsWifiUplink(PADAPTER padapter)
{
//	PMGNT_INFO 		pMgntInfo = &(GetDefaultAdapter(padapter)->MgntInfo);
	struct mlme_priv *pmlmepriv;
	PBT30Info		pBTInfo;
	PBT_TRAFFIC		pBtTraffic;


	pmlmepriv = &padapter->mlmepriv;
	pBTInfo = GET_BT_INFO(padapter);
	pBtTraffic = &pBTInfo->BtTraffic;

	if ((pmlmepriv->LinkDetectInfo.bTxBusyTraffic) ||
		(pBtTraffic->Bt30TrafficStatistics.bTxBusyTraffic))
		return _TRUE;
	else
		return _FALSE;
}

u8 BTDM_IsWifiDownlink(PADAPTER padapter)
{
//	PMGNT_INFO 		pMgntInfo = &(GetDefaultAdapter(padapter)->MgntInfo);
	struct mlme_priv *pmlmepriv;
	PBT30Info		pBTInfo;
	PBT_TRAFFIC		pBtTraffic;


	pmlmepriv = &padapter->mlmepriv;
	pBTInfo = GET_BT_INFO(padapter);
	pBtTraffic = &pBTInfo->BtTraffic;

	if ((pmlmepriv->LinkDetectInfo.bRxBusyTraffic) ||
		(pBtTraffic->Bt30TrafficStatistics.bRxBusyTraffic))
		return _TRUE;
	else
		return _FALSE;
}

u8 BTDM_IsBTHSMode(PADAPTER padapter)
{
//	PMGNT_INFO 		pMgntInfo = &(GetDefaultAdapter(padapter)->MgntInfo);
	PHAL_DATA_TYPE	pHalData;
	PBT_MGNT		pBtMgnt;


	pHalData = GET_HAL_DATA(padapter);
	pBtMgnt = &pHalData->BtInfo.BtMgnt;

	if (pBtMgnt->BtOperationOn)
		return _TRUE;
	else
		return _FALSE;
}

u8 BTDM_IsBTUplink(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.BT21TrafficStatistics.bTxBusyTraffic)
		return _TRUE;
	else
		return _FALSE;
}

u8 BTDM_IsBTDownlink(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic)
		return _TRUE;
	else
		return _FALSE;
}

void BTDM_AdjustForBtOperation(PADAPTER padapter)
{
	RTPRINT(FBT, BT_TRACE, ("[BT][DM], BTDM_AdjustForBtOperation()\n"));
	if (IS_HARDWARE_TYPE_8723A(padapter))
	{
		BTDM_AdjustForBtOperation8723A(padapter);
	}
}

u8 BTDM_AdjustRssiForAgcTableOn(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (!IS_HARDWARE_TYPE_8192D(padapter) &&
		!IS_HARDWARE_TYPE_8723A(padapter))
		return 0;

	if (pHalData->bt_coexist.b92DAgcTableOn)
		return 12;

	if (pHalData->bt_coexist.b8723aAgcTableOn)
		return 6;

	return 0;
}

void BTDM_SetBtCoexCurrAntNum(PADAPTER padapter, u8 antNum)
{
	if (IS_HARDWARE_TYPE_8723A(padapter))
		BTDM_Set8723ABtCoexCurrAntNum(padapter, antNum);
}

void BTDM_ForHalt(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (!pHalData->bt_coexist.BluetoothCoexist)
	{
		return;
	}

	if (IS_HARDWARE_TYPE_8723A(padapter))
	{
		BTDM_ForHalt8723A(padapter);
		GET_HAL_DATA(padapter)->bt_coexist.bInitlized = _FALSE;
	}
}

void BTDM_WifiScanNotify(PADAPTER padapter, u8 scanType)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (!pHalData->bt_coexist.BluetoothCoexist)
	{
		return;
	}

	if (IS_HARDWARE_TYPE_8723A(padapter))
		BTDM_WifiScanNotify8723A(padapter, scanType);
}

void BTDM_WifiAssociateNotify(PADAPTER padapter, u8 action)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (!pHalData->bt_coexist.BluetoothCoexist)
	{
		return;
	}

	if (IS_HARDWARE_TYPE_8723A(padapter))
		BTDM_WifiAssociateNotify8723A(padapter, action);
}

void BTDM_MediaStatusNotify(PADAPTER padapter, RT_MEDIA_STATUS mstatus)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (!pHalData->bt_coexist.BluetoothCoexist)
	{
		return;
	}

	if (IS_HARDWARE_TYPE_8723A(padapter))
		BTDM_MediaStatusNotify8723A(padapter, mstatus);
}

void BTDM_ForDhcp(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (!pHalData->bt_coexist.BluetoothCoexist)
	{
		return;
	}

	if (IS_HARDWARE_TYPE_8723A(padapter))
		BTDM_ForDhcp8723A(padapter);
}

void BTDM_ResetActionProfileState(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	pHalData->bt_coexist.CurrentState &= ~\
		(BT_COEX_STATE_PROFILE_HID|BT_COEX_STATE_PROFILE_A2DP|
		BT_COEX_STATE_PROFILE_PAN|BT_COEX_STATE_PROFILE_SCO);
}

u8 BTDM_IsActionSCO(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData;
	PBT30Info		pBTInfo;
	PBT_MGNT		pBtMgnt;
	PBT_DBG			pBtDbg;
	u8			bRet;


	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;
	pBtDbg = &pBTInfo->BtDbg;
	bRet = _FALSE;

	if (pBtDbg->dbgCtrl == _TRUE)
	{
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_SCO)
		{
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_SCO;
			bRet = _TRUE;
		}
	}
	else
	{
		if (pBtMgnt->ExtConfig.NumberOfSCO > 0)
		{
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_SCO;
			bRet = _TRUE;
		}
	}
	return bRet;
}

u8 BTDM_IsActionHID(PADAPTER padapter)
{
	PBT30Info		pBTInfo;
	PHAL_DATA_TYPE	pHalData;
	PBT_MGNT		pBtMgnt;
	PBT_DBG			pBtDbg;
	u8			bRet;


	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;
	pBtDbg = &pBTInfo->BtDbg;
	bRet = _FALSE;

	if (pBtDbg->dbgCtrl == _TRUE )
	{
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_HID)
		{
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_HID;
			bRet = _TRUE;
		}
	}
	else
	{
		if (BTHCI_CheckProfileExist(padapter,BT_PROFILE_HID) && pBtMgnt->ExtConfig.NumberOfHandle==1)
		{
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_HID;
			bRet = _TRUE;
		}
	}
	return bRet;
}

u8 BTDM_IsActionA2DP(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData;
	PBT30Info		pBTInfo;
	PBT_MGNT		pBtMgnt;
	PBT_DBG			pBtDbg;
	u8			bRet;


	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;
	pBtDbg = &pBTInfo->BtDbg;
	bRet = _FALSE;

	if (pBtDbg->dbgCtrl == _TRUE )
	{
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_A2DP)
		{
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_A2DP;
			bRet = _TRUE;
		}
	}
	else
	{
		if (BTHCI_CheckProfileExist(padapter,BT_PROFILE_A2DP) && pBtMgnt->ExtConfig.NumberOfHandle==1)
		{
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_A2DP;
			bRet = _TRUE;
		}
	}
	return bRet;
}

u8 BTDM_IsActionPAN(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData;
	PBT30Info		pBTInfo;
	PBT_MGNT		pBtMgnt;
	PBT_DBG			pBtDbg;
	u8			bRet;


	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;
	pBtDbg = &pBTInfo->BtDbg;
	bRet = _FALSE;

	if (pBtDbg->dbgCtrl == _TRUE)
	{
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_PAN)
		{
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_PAN;
			bRet = _TRUE;
		}
	}
	else
	{
		if (BTHCI_CheckProfileExist(padapter,BT_PROFILE_PAN) && pBtMgnt->ExtConfig.NumberOfHandle==1)
		{
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_PAN;
			bRet = _TRUE;
		}
	}
	return bRet;
}

u8 BTDM_IsActionHIDA2DP(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData;
	PBT30Info		pBTInfo;
	PBT_MGNT		pBtMgnt;
	PBT_DBG			pBtDbg;
	u8			bRet;


	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;
	pBtDbg = &pBTInfo->BtDbg;
	bRet = _FALSE;

	if (pBtDbg->dbgCtrl == _TRUE)
	{
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_HID_A2DP)
		{
			pHalData->bt_coexist.CurrentState |= (BT_COEX_STATE_PROFILE_HID|BT_COEX_STATE_PROFILE_A2DP);
			bRet = _TRUE;
		}
	}
	else
	{
		if (BTHCI_CheckProfileExist(padapter,BT_PROFILE_HID) && BTHCI_CheckProfileExist(padapter,BT_PROFILE_A2DP))
		{
			pHalData->bt_coexist.CurrentState |= (BT_COEX_STATE_PROFILE_HID|BT_COEX_STATE_PROFILE_A2DP);
			bRet = _TRUE;
		}
	}
	return bRet;
}

u8 BTDM_IsActionHIDPAN(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData;
	PBT30Info		pBTInfo;
	PBT_DBG			pBtDbg;
	u8			bRet;


	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtDbg = &pBTInfo->BtDbg;
	bRet = _FALSE;

	if (pBtDbg->dbgCtrl == _TRUE )
	{
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_HID_PAN)
		{
			pHalData->bt_coexist.CurrentState |= (BT_COEX_STATE_PROFILE_HID|BT_COEX_STATE_PROFILE_PAN);
			bRet = _TRUE;
		}
	}
	else
	{
		if (BTHCI_CheckProfileExist(padapter,BT_PROFILE_HID) && BTHCI_CheckProfileExist(padapter,BT_PROFILE_PAN))
		{
			pHalData->bt_coexist.CurrentState |= (BT_COEX_STATE_PROFILE_HID|BT_COEX_STATE_PROFILE_PAN);
			bRet = _TRUE;
		}
	}
	return bRet;
}

u8 BTDM_IsActionPANA2DP(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData;
	PBT30Info		pBTInfo;
	PBT_DBG			pBtDbg;
	u8			bRet;


	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtDbg = &pBTInfo->BtDbg;
	bRet = _FALSE;

	if (pBtDbg->dbgCtrl == _TRUE )
	{
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_PAN_A2DP)
		{
			pHalData->bt_coexist.CurrentState |= (BT_COEX_STATE_PROFILE_PAN|BT_COEX_STATE_PROFILE_A2DP);
			bRet = _TRUE;
		}
	}
	else
	{
		if (BTHCI_CheckProfileExist(padapter,BT_PROFILE_PAN) && BTHCI_CheckProfileExist(padapter,BT_PROFILE_A2DP))
		{
			pHalData->bt_coexist.CurrentState |= (BT_COEX_STATE_PROFILE_PAN|BT_COEX_STATE_PROFILE_A2DP);
			bRet = _TRUE;
		}
	}
	return bRet;
}

u8 BTDM_IsBtDisabled(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.bCurBtDisabled)
		return _TRUE;
	else
		return _FALSE;
}

//============================================
// Started with "WA_" means this is a work around function.
// Because fw need to count bt HW counters
//(BT_ACTIVE/BT_STATE/BT_POLLING)
// in beacon related interrupt, so we have to write beacon control
// register now.
//============================================
void WA_BTDM_EnableBTFwCounterPolling(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	// Currently, only 88cu and 92de need to enter the function
	if (!IS_HARDWARE_TYPE_8192CU(padapter) &&
		!IS_HARDWARE_TYPE_8192DE(padapter))
		return;

	if (!pHalData->bt_coexist.BluetoothCoexist)
	{
		return;
	}
	else
	{
		//
		// Enable BT firmware counter statistics.
		// We have to set 0x550[3]=1 to enable it.
		// Advised by Scott.
		//
		u8	u1val = 0;
		u1val = rtw_read8(padapter, REG_BCN_CTRL);
		u1val |= BIT3;
		rtw_write8(padapter, REG_BCN_CTRL, u1val);
	}
}

// ===== End of sync from SD7 driver HAL/BTCoexist/HalBtCoexist.c =====
#endif

#ifdef __HALBT_C__ // HAL/HalBT.c
// ===== Below this line is sync from SD7 driver HAL/HalBT.c =====

//==================================================
//	local function
//==================================================
#if 0
static void halbt_SetBTSwitchCtrl(PADAPTER padapter)
{
	// switch control, here we set pathA to control
	// 0x878[13] = 1, 0:pathB, 1:pathA(default)
	PHY_SetBBReg(padapter, rFPGA0_XAB_RFParameter, BIT(13), 0x1);

	// antsel control, here we use phy0 and enable antsel.
	// 0x87c[16:15] = b'11, enable antsel, antsel output pin
	// 0x87c[30] = 0, 0: phy0, 1:phy 1
	PHY_SetBBReg(padapter, rFPGA0_XCD_RFParameter, bMaskDWord, 0x1fff8);

	// antsel to Bt or Wifi, it depends Bt on/off.
	// 0x860[9:8] = 'b10, b10:Bt On, WL2G off(default), b01:Bt off, WL2G on.
	PHY_SetBBReg(padapter, rFPGA0_XA_RFInterfaceOE, BIT(9)|BIT(8), 0x2);

	// sw/hw control switch, here we set sw control
	// 0x870[9:8] = 'b11 sw control, 'b00 hw control
	PHY_SetBBReg(padapter, rFPGA0_XAB_RFInterfaceSW, BIT(9)|BIT(8), 0x3);
}
#endif

static void halbt_InitHwConfig8723A(PADAPTER padapter)
{
}

//==================================================
//	extern function
//==================================================
u8 HALBT_GetPGAntNum(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	return pHalData->bt_coexist.BT_Ant_Num;
}

void HALBT_SetKey(PADAPTER padapter, u8 EntryNum)
{
	PBT30Info		pBTinfo;
	PBT_ASOC_ENTRY	pBtAssocEntry;
	u16				usConfig = 0;


//	RT_TRACE(COMP_SEC , DBG_LOUD , (" ==> HALBT_SetKey\n"));

	pBTinfo = GET_BT_INFO(padapter);
	pBtAssocEntry = &(pBTinfo->BtAsocEntry[EntryNum]);

	pBtAssocEntry->HwCAMIndex = BT_HWCAM_STAR + EntryNum;

	usConfig = CAM_VALID | (CAM_AES << 2);
//	CAM_program_entry(padapter, pBtAssocEntry->HwCAMIndex, pBtAssocEntry->BTRemoteMACAddr, pBtAssocEntry->PTK + TKIP_ENC_KEY_POS, usConfig);
	write_cam(padapter, pBtAssocEntry->HwCAMIndex, usConfig, pBtAssocEntry->BTRemoteMACAddr, pBtAssocEntry->PTK + TKIP_ENC_KEY_POS);
}

void HALBT_RemoveKey(PADAPTER padapter, u8 EntryNum)
{
	PBT30Info		pBTinfo;
	PBT_ASOC_ENTRY	pBtAssocEntry;


//	RT_TRACE(COMP_SEC , DBG_LOUD , (" ==> HALBT_RemoveKey\n"));

	pBTinfo = GET_BT_INFO(padapter);
	pBtAssocEntry = &(pBTinfo->BtAsocEntry[EntryNum]);

	if (pBTinfo->BtAsocEntry[EntryNum].HwCAMIndex != 0)
	{
		// ToDo : add New HALBT_RemoveKey function !!
		if (pBtAssocEntry->HwCAMIndex >= BT_HWCAM_STAR && pBtAssocEntry->HwCAMIndex < HALF_CAM_ENTRY)
		{
//			CamDeleteOneEntry(padapter, pBtAssocEntry->BTRemoteMACAddr , pBtAssocEntry->HwCAMIndex);
			CAM_empty_entry(padapter, pBtAssocEntry->HwCAMIndex);
//			RT_TRACE(COMP_SEC , DBG_LOUD , (" BT_ResetEntry Remove Key Index : %d \n",pBtAssocEntry->HwCAMIndex));
		}
		pBTinfo->BtAsocEntry[EntryNum].HwCAMIndex = 0;
	}
}

void HALBT_InitBTVars8723A(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData;


	pHalData = GET_HAL_DATA(padapter);

	pHalData->bt_coexist.BluetoothCoexist = pHalData->EEPROMBluetoothCoexist;
	pHalData->bt_coexist.BT_Ant_Num = pHalData->EEPROMBluetoothAntNum;
	pHalData->bt_coexist.BT_CoexistType = pHalData->EEPROMBluetoothType;
	pHalData->bt_coexist.BT_Ant_isolation = pHalData->EEPROMBluetoothAntIsolation;
	pHalData->bt_coexist.BT_RadioSharedType = pHalData->EEPROMBluetoothRadioShared;

	RT_TRACE(_module_hal_init_c_, _drv_info_, ("BT Coexistance = 0x%x\n", pHalData->bt_coexist.BluetoothCoexist));
	if (pHalData->bt_coexist.BluetoothCoexist)
	{
		if (pHalData->bt_coexist.BT_Ant_Num == Ant_x2)
		{
			BTDM_SetBtCoexCurrAntNum(padapter, 2);
			RT_TRACE(_module_hal_init_c_, _drv_info_,("BlueTooth BT_Ant_Num = Antx2\n"));
		}
		else if (pHalData->bt_coexist.BT_Ant_Num == Ant_x1)
		{
			BTDM_SetBtCoexCurrAntNum(padapter, 1);
			RT_TRACE(_module_hal_init_c_, _drv_info_,("BlueTooth BT_Ant_Num = Antx1\n"));
		}
		pHalData->bt_coexist.bBTBusyTraffic = _FALSE;
		pHalData->bt_coexist.bBTTrafficModeSet = _FALSE;
		pHalData->bt_coexist.bBTNonTrafficModeSet = _FALSE;
		pHalData->bt_coexist.CurrentState = 0;
		pHalData->bt_coexist.PreviousState = 0;

		RT_TRACE(_module_hal_init_c_, _drv_info_,("BT_RadioSharedType = 0x%x\n", pHalData->bt_coexist.BT_RadioSharedType));
	}
}

u8 HALBT_IsBTExist(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.BluetoothCoexist)
		return _TRUE;
	else
		return _FALSE;
}

u8 HALBT_BTChipType(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	return pHalData->bt_coexist.BT_CoexistType;
}

void HALBT_InitHwConfig(PADAPTER padapter)
{
#if 0
	if (IS_HARDWARE_TYPE_8192C(padapter))
	{
		halbt_InitHwConfig92C(padapter);
	}
	else if (IS_HARDWARE_TYPE_8192D(padapter))
	{
		halbt_InitHwConfig92D(padapter);
	}
	else if (IS_HARDWARE_TYPE_8723A(padapter))
#endif
	{
		halbt_InitHwConfig8723A(padapter);
		BTDM_Coexist(padapter);
	}
}

void HALBT_IPSRFOffCheck(PADAPTER padapter)
{
	PBT30Info		pBTinfo;
	PBT_MGNT		pBtMgnt;
	PHAL_DATA_TYPE	pHalData;


	pBTinfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTinfo->BtMgnt;
	pHalData = GET_HAL_DATA(padapter);

	if (IS_HARDWARE_TYPE_8192C(padapter) ||
		IS_HARDWARE_TYPE_8192D(padapter) ||
		IS_HARDWARE_TYPE_8723A(padapter))
	{
		if ((pHalData->bt_coexist.BluetoothCoexist) &&
			(pBtMgnt->bSupportProfile))
		{
			RTPRINT(FBT, BT_TRACE, ("[BT][DM], HALBT_IPSRFOffCheck(), turn off all Coexist DM\n"));
			BTDM_CoexAllOff(padapter);
		}
	}
}

void HALBT_LPSRFOffCheck(PADAPTER padapter)
{
	PBT30Info		pBTinfo;
	PBT_MGNT		pBtMgnt;
	PHAL_DATA_TYPE	pHalData;


	pBTinfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTinfo->BtMgnt;
	pHalData = GET_HAL_DATA(padapter);

	if (IS_HARDWARE_TYPE_8192C(padapter) ||
		IS_HARDWARE_TYPE_8192D(padapter) ||
		IS_HARDWARE_TYPE_8723A(padapter))
	{
		if ((pHalData->bt_coexist.BluetoothCoexist) &&
			(pBtMgnt->bSupportProfile))
		{
			RTPRINT(FBT, BT_TRACE, ("[BT][DM], HALBT_LPSRFOffCheck(), turn off all Coexist DM\n"));
			BTDM_CoexAllOff(padapter);
		}
	}
}

void HALBT_SetRtsCtsNoLenLimit(PADAPTER padapter)
{
#if (RTS_CTS_NO_LEN_LIMIT == 1)
	rtw_write32(padapter, 0x4c8, 0xc140402);
#endif
}

u8 HALBT_OnlySupport1T(PADAPTER padapter)
{
#if 0
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	if (IS_HARDWARE_TYPE_8192DE(padapter))
	{
		if ((SINGLEMAC_SINGLEPHY == pHalData->MacPhyMode92D) &&
			(pHalData->bt_coexist.BluetoothCoexist) &&
			(pHalData->CurrentBandType92D==BAND_ON_2_4G) &&
			(!pHalData->bt_coexist.bCurBtDisabled))
		{
			RTPRINT(FIOCTL, IOCTL_STATE, ("[92d], 1T condition!!\n"));
			return _TRUE;
		}
	}
	RTPRINT(FIOCTL, IOCTL_STATE, ("[92d], 2T condition!!\n"));
#endif
	return _FALSE;
}

u8
HALBT_BtRegAccess(
	PADAPTER	padapter,
	u32			accessType,
	u32			regType,
	u32			regOffset,
	u32			wValue,
	u32			*pRetVal
	)
{
	u8	H2C_Parameter[5] = {0};

	if (IS_HARDWARE_TYPE_8723A(padapter))
	{
		*pRetVal = 0x223;
		//FillH2CCmd(padapter, 0xaf, 5, H2C_Parameter);
	}
	else
	{
		*pRetVal = 0xffffffff;
		return _FALSE;
	}

	return _TRUE;
}

void HALBT_SwitchWirelessMode(PADAPTER padapter, u8 targetWirelessMode)
{
#if 0
	PMGNT_INFO	pMgntInfo = &padapter->MgntInfo;
	u8	band;

	if (!IS_HARDWARE_TYPE_8192D(padapter))
		return;

	RTPRINT(FIOCTL, IOCTL_STATE, ("switch to wireless mode = 0x%x!!\n", targetWirelessMode));
	pMgntInfo->dot11CurrentWirelessMode = targetWirelessMode;
	pMgntInfo->SettingBeforeScan.WirelessMode = pMgntInfo->dot11CurrentWirelessMode;//For N solution won't be change the wireless mode in scan
	padapter->HalFunc.SetWirelessModeHandler(padapter, pMgntInfo->dot11CurrentWirelessMode);

	if ((targetWirelessMode == WIRELESS_MODE_N_5G) ||
		(targetWirelessMode == WIRELESS_MODE_A))
		band = BAND_ON_5G;
	else
		band = BAND_ON_2_4G;
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_DUAL_SWITCH_BAND, &band);
	rtw_mdelay_os(50);
#endif
}

// ===== End of sync from SD7 driver HAL/HalBT.c =====
#endif

