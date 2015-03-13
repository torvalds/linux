/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 *published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/
#include <drv_types.h>
#include <rtl8723a_hal.h>
#include <usb_ops_linux.h>

#define DIS_PS_RX_BCN

u32 BTCoexDbgLevel = _bt_dbg_off_;

#define RTPRINT(_Comp, _Level, Fmt)\
do {\
	if ((BTCoexDbgLevel == _bt_dbg_on_)) {\
		printk Fmt;\
	}					\
} while (0)

#define RTPRINT_ADDR(dbgtype, dbgflag, printstr, _Ptr)\
if ((BTCoexDbgLevel == _bt_dbg_on_)) {\
	u32 __i;						\
	u8 *ptr = (u8 *)_Ptr;	\
	printk printstr;				\
	printk(" ");					\
	for (__i = 0; __i < 6; __i++)		\
		printk("%02X%s", ptr[__i], (__i == 5)?"":"-");		\
	printk("\n");							\
}
#define RTPRINT_DATA(dbgtype, dbgflag, _TitleString, _HexData, _HexDataLen)\
if ((BTCoexDbgLevel == _bt_dbg_on_)) {\
	u32 __i;						\
	u8 *ptr = (u8 *)_HexData;				\
	printk(_TitleString);					\
	for (__i = 0; __i < (u32)_HexDataLen; __i++) {		\
		printk("%02X%s", ptr[__i], (((__i + 1) % 4) == 0)?"  ":" ");\
		if (((__i + 1) % 16) == 0)			\
			printk("\n");				\
	}								\
	printk("\n");							\
}
/*  Added by Annie, 2005-11-22. */
#define MAX_STR_LEN	64
/*  I want to see ASCII 33 to 126 only. Otherwise, I print '?'. */
#define PRINTABLE(_ch)	(_ch >= ' ' && _ch <= '~')
#define RT_PRINT_STR(_Comp, _Level, _TitleString, _Ptr, _Len)		\
	{								\
		u32 __i;						\
		u8 buffer[MAX_STR_LEN];					\
		u32 length = (_Len < MAX_STR_LEN) ? _Len : (MAX_STR_LEN-1);\
		memset(buffer, 0, MAX_STR_LEN);				\
		memcpy(buffer, (u8 *)_Ptr, length);			\
		for (__i = 0; __i < length; __i++) {			\
			if (!PRINTABLE(buffer[__i]))			\
				buffer[__i] = '?';			\
		}							\
		buffer[length] = '\0';					\
		printk(_TitleString);					\
		printk(": %d, <%s>\n", _Len, buffer);			\
	}

#define DCMD_Printf(...)
#define RT_ASSERT(...)

#define rsprintf snprintf

#define GetDefaultAdapter(padapter)	padapter

#define PlatformZeroMemory(ptr, sz)	memset(ptr, 0, sz)

#define PlatformProcessHCICommands(...)
#define PlatformTxBTQueuedPackets(...)
#define PlatformIndicateBTACLData(...)	(RT_STATUS_SUCCESS)
#define PlatformAcquireSpinLock(padapter, type)
#define PlatformReleaseSpinLock(padapter, type)

#define GET_UNDECORATED_AVERAGE_RSSI(padapter)	\
			(GET_HAL_DATA(padapter)->dmpriv.EntryMinUndecoratedSmoothedPWDB)
#define RT_RF_CHANGE_SOURCE u32

enum {
	RT_JOIN_INFRA   = 1,
	RT_JOIN_IBSS  = 2,
	RT_START_IBSS = 3,
	RT_NO_ACTION  = 4,
};

/*  power saving */

/*  ===== Below this line is sync from SD7 driver COMMOM/BT.c ===== */

static u8 BT_Operation(struct rtw_adapter *padapter)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->BtOperationOn)
		return true;
	else
		return false;
}

static u8 BT_IsLegalChannel(struct rtw_adapter *padapter, u8 channel)
{
	struct rt_channel_info *pChanneList = NULL;
	u8 channelLen, i;

	pChanneList = padapter->mlmeextpriv.channel_set;
	channelLen = padapter->mlmeextpriv.max_chan_nums;

	for (i = 0; i < channelLen; i++) {
		RTPRINT(FIOCTL, IOCTL_STATE,
			("Check if chnl(%d) in channel plan contains bt target chnl(%d) for BT connection\n",
			 pChanneList[i].ChannelNum, channel));
		if ((channel == pChanneList[i].ChannelNum) ||
		    (channel == pChanneList[i].ChannelNum + 2))
			return channel;
	}
	return 0;
}

void BT_SignalCompensation(struct rtw_adapter *padapter, u8 *rssi_wifi, u8 *rssi_bt)
{
	BTDM_SignalCompensation(padapter, rssi_wifi, rssi_bt);
}

void rtl8723a_BT_wifiscan_notify(struct rtw_adapter *padapter, u8 scanType)
{
	BTHCI_WifiScanNotify(padapter, scanType);
	BTDM_CheckAntSelMode(padapter);
	BTDM_WifiScanNotify(padapter, scanType);
}

void rtl8723a_BT_wifiassociate_notify(struct rtw_adapter *padapter, u8 action)
{
	/*  action : */
	/*  true = associate start */
	/*  false = associate finished */
	if (action)
		BTDM_CheckAntSelMode(padapter);

	BTDM_WifiAssociateNotify(padapter, action);
}

void BT_HaltProcess(struct rtw_adapter *padapter)
{
	BTDM_ForHalt(padapter);
}

/*  ===== End of sync from SD7 driver COMMOM/BT.c ===== */

#define i64fmt		"ll"
#define UINT64_C(v)  (v)

#define FillOctetString(_os, _octet, _len)		\
	(_os).Octet = (u8 *)(_octet);			\
	(_os).Length = (_len);

static enum rt_status PlatformIndicateBTEvent(
	struct rtw_adapter *padapter,
	void						*pEvntData,
	u32						dataLen
	)
{
	enum rt_status	rt_status = RT_STATUS_FAILURE;

	RTPRINT(FIOCTL, IOCTL_BT_EVENT_DETAIL, ("BT event start, %d bytes data to Transferred!!\n", dataLen));
	RTPRINT_DATA(FIOCTL, IOCTL_BT_EVENT_DETAIL, "To transfer Hex Data :\n",
		pEvntData, dataLen);

	BT_EventParse(padapter, pEvntData, dataLen);

	printk(KERN_WARNING "%s: Linux has no way to report BT event!!\n", __func__);

	RTPRINT(FIOCTL, IOCTL_BT_EVENT_DETAIL, ("BT event end, %s\n",
		(rt_status == RT_STATUS_SUCCESS) ? "SUCCESS" : "FAIL"));

	return rt_status;
}

/*  ===== Below this line is sync from SD7 driver COMMOM/bt_hci.c ===== */

static u8 bthci_GetLocalChannel(struct rtw_adapter *padapter)
{
	return padapter->mlmeextpriv.cur_channel;
}

static u8 bthci_GetCurrentEntryNum(struct rtw_adapter *padapter, u8 PhyHandle)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	u8 i;

	for (i = 0; i < MAX_BT_ASOC_ENTRY_NUM; i++) {
		if ((pBTInfo->BtAsocEntry[i].bUsed) &&
		    (pBTInfo->BtAsocEntry[i].PhyLinkCmdData.BtPhyLinkhandle == PhyHandle))
			return i;
	}

	return 0xFF;
}

static void bthci_DecideBTChannel(struct rtw_adapter *padapter, u8 EntryNum)
{
/*PMGNT_INFO pMgntInfo = &padapter->MgntInfo; */
	struct mlme_priv *pmlmepriv;
	struct bt_30info *pBTInfo;
	struct bt_mgnt *pBtMgnt;
	struct bt_hci_info *pBtHciInfo;
	struct chnl_txpower_triple *pTriple_subband = NULL;
	struct common_triple *pTriple;
	u8 i, j, localchnl, firstRemoteLegalChnlInTriplet = 0;
	u8 regulatory_skipLen = 0;
	u8 subbandTripletCnt = 0;

	pmlmepriv = &padapter->mlmepriv;
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;
	pBtHciInfo = &pBTInfo->BtHciInfo;

	pBtMgnt->CheckChnlIsSuit = true;
	localchnl = bthci_GetLocalChannel(padapter);

	pTriple = (struct common_triple *)
		&pBtHciInfo->BTPreChnllist[COUNTRY_STR_LEN];

	/*  contains country string, len is 3 */
	for (i = 0; i < (pBtHciInfo->BtPreChnlListLen-COUNTRY_STR_LEN); i += 3, pTriple++) {
		/*  */
		/*  check every triplet, an triplet may be */
		/*  regulatory extension identifier or sub-band triplet */
		/*  */
		if (pTriple->byte_1st == 0xc9) {
			/*  Regulatory Extension Identifier, skip it */
			RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO),
				("Find Regulatory ID, regulatory class = %d\n", pTriple->byte_2nd));
			regulatory_skipLen += 3;
			pTriple_subband = NULL;
			continue;
		} else {	/*  Sub-band triplet */
			RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Find Sub-band triplet \n"));
			subbandTripletCnt++;
			pTriple_subband = (struct chnl_txpower_triple *)pTriple;
			/*  if remote first legal channel not found, then find first remote channel */
			/*  and it's legal for our channel plan. */

			/*  search the sub-band triplet and find if remote channel is legal to our channel plan. */
			for (j = pTriple_subband->FirstChnl; j < (pTriple_subband->FirstChnl+pTriple_subband->NumChnls); j++) {
				RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), (" Check if chnl(%d) is legal\n", j));
				if (BT_IsLegalChannel(padapter, j)) {
					/*  remote channel is legal for our channel plan. */
					firstRemoteLegalChnlInTriplet = j;
					RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO),
						("Find first remote legal channel : %d\n",
						firstRemoteLegalChnlInTriplet));

					/*  If we find a remote legal channel in the sub-band triplet */
					/*  and only BT connection is established(local not connect to any AP or IBSS), */
					/*  then we just switch channel to remote channel. */
					if (!(check_fwstate(pmlmepriv, WIFI_ASOC_STATE|WIFI_ADHOC_STATE|WIFI_AP_STATE) ||
					    BTHCI_HsConnectionEstablished(padapter))) {
						pBtMgnt->BTChannel = firstRemoteLegalChnlInTriplet;
						RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Remote legal channel (%d) is selected, Local not connect to any!!\n", pBtMgnt->BTChannel));
						return;
					} else {
						if ((localchnl >= firstRemoteLegalChnlInTriplet) &&
						    (localchnl < (pTriple_subband->FirstChnl+pTriple_subband->NumChnls))) {
							pBtMgnt->BTChannel = localchnl;
							RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Local channel (%d) is selected, wifi or BT connection exists\n", pBtMgnt->BTChannel));
							return;
						}
					}
					break;
				}
			}
		}
	}

	if (subbandTripletCnt) {
		/* if any preferred channel triplet exists */
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("There are %d sub band triplet exists, ", subbandTripletCnt));
		if (firstRemoteLegalChnlInTriplet == 0) {
			/* no legal channel is found, reject the connection. */
			RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("no legal channel is found!!\n"));
		} else {
			/*  Remote Legal channel is found but not match to local */
			/* wifi connection exists), so reject the connection. */
			RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO),
				("Remote Legal channel is found but not match to local(wifi connection exists)!!\n"));
		}
		pBtMgnt->CheckChnlIsSuit = false;
	} else {
		/*  There are not any preferred channel triplet exists */
		/*  Use current legal channel as the bt channel. */
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("No sub band triplet exists!!\n"));
	}
	pBtMgnt->BTChannel = localchnl;
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Local channel (%d) is selected!!\n", pBtMgnt->BTChannel));
}

/* Success:return true */
/* Fail:return false */
static u8 bthci_GetAssocInfo(struct rtw_adapter *padapter, u8 EntryNum)
{
	struct bt_30info *pBTInfo;
	struct bt_hci_info *pBtHciInfo;
	u8 tempBuf[256];
	u8 i = 0;
	u8 BaseMemoryShift = 0;
	u16	TotalLen = 0;
	struct amp_assoc_structure *pAmpAsoc;

	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("GetAssocInfo start\n"));
	pBTInfo = GET_BT_INFO(padapter);
	pBtHciInfo = &pBTInfo->BtHciInfo;

	if (pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.LenSoFar == 0) {
		if (pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.AMPAssocRemLen < (MAX_AMP_ASSOC_FRAG_LEN))
			TotalLen = pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.AMPAssocRemLen;
		else if (pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.AMPAssocRemLen == (MAX_AMP_ASSOC_FRAG_LEN))
			TotalLen = MAX_AMP_ASSOC_FRAG_LEN;
	} else if (pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.LenSoFar > 0)
		TotalLen = pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.LenSoFar;

	while ((pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.LenSoFar >= BaseMemoryShift) || TotalLen > BaseMemoryShift) {
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD_DETAIL, ("GetAssocInfo, TotalLen =%d, BaseMemoryShift =%d\n", TotalLen, BaseMemoryShift));
		memcpy(tempBuf,
			(u8 *)pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.AMPAssocfragment+BaseMemoryShift,
			TotalLen-BaseMemoryShift);
		RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD_DETAIL, "GetAssocInfo :\n",
			tempBuf, TotalLen-BaseMemoryShift);

		pAmpAsoc = (struct amp_assoc_structure *)tempBuf;
		le16_to_cpus(&pAmpAsoc->Length);
		BaseMemoryShift += 3 + pAmpAsoc->Length;

		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("TypeID = 0x%x, ", pAmpAsoc->TypeID));
		RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD, "Hex Data: \n", pAmpAsoc->Data, pAmpAsoc->Length);
		switch (pAmpAsoc->TypeID) {
		case AMP_MAC_ADDR:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("==> AMP_MAC_ADDR\n"));
			if (pAmpAsoc->Length > 6)
				return false;
			memcpy(pBTInfo->BtAsocEntry[EntryNum].BTRemoteMACAddr, pAmpAsoc->Data, 6);
			RTPRINT_ADDR(FIOCTL, IOCTL_BT_HCICMD, ("Remote Mac address \n"), pBTInfo->BtAsocEntry[EntryNum].BTRemoteMACAddr);
			break;
		case AMP_PREFERRED_CHANNEL_LIST:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("==> AMP_PREFERRED_CHANNEL_LIST\n"));
			pBtHciInfo->BtPreChnlListLen = pAmpAsoc->Length;
			memcpy(pBtHciInfo->BTPreChnllist,
				pAmpAsoc->Data,
				pBtHciInfo->BtPreChnlListLen);
			RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD, "Preferred channel list : \n", pBtHciInfo->BTPreChnllist, pBtHciInfo->BtPreChnlListLen);
			bthci_DecideBTChannel(padapter, EntryNum);
			break;
		case AMP_CONNECTED_CHANNEL:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("==> AMP_CONNECTED_CHANNEL\n"));
			pBtHciInfo->BTConnectChnlListLen = pAmpAsoc->Length;
			memcpy(pBtHciInfo->BTConnectChnllist,
				pAmpAsoc->Data,
				pBtHciInfo->BTConnectChnlListLen);
			break;
		case AMP_80211_PAL_CAP_LIST:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("==> AMP_80211_PAL_CAP_LIST\n"));
			pBTInfo->BtAsocEntry[EntryNum].BTCapability = *(u32 *)(pAmpAsoc->Data);
			if (pBTInfo->BtAsocEntry[EntryNum].BTCapability & 0x00000001) {
				/*  TODO: */

				/* Signifies PAL capable of utilizing received activity reports. */
			}
			if (pBTInfo->BtAsocEntry[EntryNum].BTCapability & 0x00000002) {
				/*  TODO: */
				/* Signifies PAL is capable of utilizing scheduling information received in an activity reports. */
			}
			break;
		case AMP_80211_PAL_VISION:
			pBtHciInfo->BTPalVersion = *(u8 *)(pAmpAsoc->Data);
			pBtHciInfo->BTPalCompanyID = *(u16 *)(((u8 *)(pAmpAsoc->Data))+1);
			pBtHciInfo->BTPalsubversion = *(u16 *)(((u8 *)(pAmpAsoc->Data))+3);
			RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("==> AMP_80211_PAL_VISION PalVersion  0x%x, PalCompanyID  0x%x, Palsubversion 0x%x\n",
				pBtHciInfo->BTPalVersion,
				pBtHciInfo->BTPalCompanyID,
				pBtHciInfo->BTPalsubversion));
			break;
		default:
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("==> Unsupport TypeID !!\n"));
			break;
		}
		i++;
	}
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("GetAssocInfo end\n"));

	return true;
}

static u8 bthci_AddEntry(struct rtw_adapter *padapter)
{
	struct bt_30info *pBTInfo;
	struct bt_mgnt *pBtMgnt;
	u8 i;

	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;

	for (i = 0; i < MAX_BT_ASOC_ENTRY_NUM; i++) {
		if (pBTInfo->BtAsocEntry[i].bUsed == false) {
			pBTInfo->BtAsocEntry[i].bUsed = true;
			pBtMgnt->CurrentConnectEntryNum = i;
			break;
		}
	}

	if (i == MAX_BT_ASOC_ENTRY_NUM) {
		RTPRINT(FIOCTL, IOCTL_STATE, ("bthci_AddEntry(), Add entry fail!!\n"));
		return false;
	}
	return true;
}

static u8 bthci_DiscardTxPackets(struct rtw_adapter *padapter, u16 LLH)
{
	return false;
}

static u8
bthci_CheckLogLinkBehavior(
	struct rtw_adapter *padapter,
	struct hci_flow_spec			TxFlowSpec
	)
{
	u8 ID = TxFlowSpec.Identifier;
	u8 ServiceType = TxFlowSpec.ServiceType;
	u16	MaxSDUSize = TxFlowSpec.MaximumSDUSize;
	u32	SDUInterArrivatime = TxFlowSpec.SDUInterArrivalTime;
	u8 match = false;

	switch (ID) {
	case 1:
		if (ServiceType == BT_LL_BE) {
			match = true;
			RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Logical Link Type =  TX best effort flowspec\n"));
		} else if ((ServiceType == BT_LL_GU) && (MaxSDUSize == 0xffff)) {
			match = true;
			RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Logical Link Type =  RX guaranteed latency flowspec\n"));
		} else if ((ServiceType == BT_LL_GU) && (MaxSDUSize == 2500)) {
			RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Logical Link Type =  RX guaranteed Large latency flowspec\n"));
		}
		break;
	case 2:
		if (ServiceType == BT_LL_BE) {
			match = true;
			RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Logical Link Type =  RX best effort flowspec\n"));

		}
		break;
	case 3:
		if ((ServiceType == BT_LL_GU) && (MaxSDUSize == 1492)) {
			match = true;
			RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Logical Link Type =  TX guaranteed latency flowspec\n"));
		} else if ((ServiceType == BT_LL_GU) && (MaxSDUSize == 2500)) {
			RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Logical Link Type =  TX guaranteed Large latency flowspec\n"));
		}
		break;
	case 4:
		if (ServiceType == BT_LL_BE) {
			if ((SDUInterArrivatime == 0xffffffff) && (ServiceType == BT_LL_BE) && (MaxSDUSize == 1492)) {
				match = true;
				RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Logical Link Type =  TX/RX aggregated best effort flowspec\n"));
			}
		} else if (ServiceType == BT_LL_GU) {
			if (SDUInterArrivatime == 100) {
				match = true;
				RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Logical Link Type =  TX/RX guaranteed bandwidth flowspec\n"));
			}
		}
		break;
	default:
		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Logical Link Type =  Unknow Type !!!!!!!!\n"));
		break;
	}

	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO),
		("ID = 0x%x, ServiceType = 0x%x, MaximumSDUSize = 0x%x, SDUInterArrivalTime = 0x%x, AccessLatency = 0x%x, FlushTimeout = 0x%x\n",
		TxFlowSpec.Identifier, TxFlowSpec.ServiceType, MaxSDUSize,
		SDUInterArrivatime, TxFlowSpec.AccessLatency, TxFlowSpec.FlushTimeout));
	return match;
}

static u16 bthci_AssocMACAddr(struct rtw_adapter *padapter, void	*pbuf)
{
	struct amp_assoc_structure *pAssoStrc = (struct amp_assoc_structure *)pbuf;
	pAssoStrc->TypeID = AMP_MAC_ADDR;
	pAssoStrc->Length = 0x06;
	memcpy(&pAssoStrc->Data[0], padapter->eeprompriv.mac_addr, 6);
	RTPRINT_DATA(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO),
		     ("AssocMACAddr : \n"), pAssoStrc, pAssoStrc->Length+3);

	return pAssoStrc->Length + 3;
}

static u16
bthci_PALCapabilities(
	struct rtw_adapter *padapter,
	void	*pbuf
	)
{
	struct amp_assoc_structure *pAssoStrc = (struct amp_assoc_structure *)pbuf;

	pAssoStrc->TypeID = AMP_80211_PAL_CAP_LIST;
	pAssoStrc->Length = 0x04;

	pAssoStrc->Data[0] = 0x00;
	pAssoStrc->Data[1] = 0x00;

	RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD_DETAIL, ("PALCapabilities:\n"), pAssoStrc, pAssoStrc->Length+3);
	RTPRINT(FIOCTL, IOCTL_BT_LOGO, ("PALCapabilities \n"));

	RTPRINT(FIOCTL, IOCTL_BT_LOGO, (" TypeID = 0x%x,\n Length = 0x%x,\n Content = 0x0000\n",
		pAssoStrc->TypeID,
		pAssoStrc->Length));

	return pAssoStrc->Length + 3;
}

static u16 bthci_AssocPreferredChannelList(struct rtw_adapter *padapter,
					   void *pbuf, u8 EntryNum)
{
	struct bt_30info *pBTInfo;
	struct amp_assoc_structure *pAssoStrc;
	struct amp_pref_chnl_regulatory *pReg;
	struct chnl_txpower_triple *pTriple;
	char ctrString[3] = {'X', 'X', 'X'};
	u32 len = 0;
	u8 preferredChnl;

	pBTInfo = GET_BT_INFO(padapter);
	pAssoStrc = (struct amp_assoc_structure *)pbuf;
	pReg = (struct amp_pref_chnl_regulatory *)&pAssoStrc->Data[3];

	preferredChnl = bthci_GetLocalChannel(padapter);
	pAssoStrc->TypeID = AMP_PREFERRED_CHANNEL_LIST;

	/*  locale unknown */
	memcpy(&pAssoStrc->Data[0], &ctrString[0], 3);
	pReg->reXId = 201;
	pReg->regulatoryClass = 254;
	pReg->coverageClass = 0;
	len += 6;
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD | IOCTL_BT_LOGO), ("PREFERRED_CHNL_LIST\n"));
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD | IOCTL_BT_LOGO), ("XXX, 201, 254, 0\n"));
	/*  at the following, chnl 1~11 should be contained */
	pTriple = (struct chnl_txpower_triple *)&pAssoStrc->Data[len];

	/*  (1) if any wifi or bt HS connection exists */
	if ((pBTInfo->BtAsocEntry[EntryNum].AMPRole == AMP_BTAP_CREATOR) ||
	    (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE |
			   WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE |
			   WIFI_AP_STATE)) ||
	    BTHCI_HsConnectionEstablished(padapter)) {
		pTriple->FirstChnl = preferredChnl;
		pTriple->NumChnls = 1;
		pTriple->MaxTxPowerInDbm = 20;
		len += 3;
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD | IOCTL_BT_LOGO), ("First Channel = %d, Channel Num = %d, MaxDbm = %d\n",
			pTriple->FirstChnl,
			pTriple->NumChnls,
			pTriple->MaxTxPowerInDbm));
	}

	pAssoStrc->Length = (u16)len;
	RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD, ("AssocPreferredChannelList : \n"), pAssoStrc, pAssoStrc->Length+3);

	return pAssoStrc->Length + 3;
}

static u16 bthci_AssocPALVer(struct rtw_adapter *padapter, void *pbuf)
{
	struct amp_assoc_structure *pAssoStrc = (struct amp_assoc_structure *)pbuf;
	u8 *pu1Tmp;
	u16	*pu2Tmp;

	pAssoStrc->TypeID = AMP_80211_PAL_VISION;
	pAssoStrc->Length = 0x5;
	pu1Tmp = &pAssoStrc->Data[0];
	*pu1Tmp = 0x1;	/*  PAL Version */
	pu2Tmp = (u16 *)&pAssoStrc->Data[1];
	*pu2Tmp = 0x5D;	/*  SIG Company identifier of 802.11 PAL vendor */
	pu2Tmp = (u16 *)&pAssoStrc->Data[3];
	*pu2Tmp = 0x1;	/*  PAL Sub-version specifier */

	RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD_DETAIL, ("AssocPALVer : \n"), pAssoStrc, pAssoStrc->Length+3);
	RTPRINT(FIOCTL, IOCTL_BT_LOGO, ("AssocPALVer \n"));

	RTPRINT(FIOCTL, IOCTL_BT_LOGO, (" TypeID = 0x%x,\n Length = 0x%x,\n PAL Version = 0x01,\n PAL vendor = 0x01,\n PAL Sub-version specifier = 0x01\n",
		pAssoStrc->TypeID,
		pAssoStrc->Length));
	return pAssoStrc->Length + 3;
}

static u8 bthci_CheckRfStateBeforeConnect(struct rtw_adapter *padapter)
{
	struct bt_30info *pBTInfo;
	enum rt_rf_power_state		RfState;

	pBTInfo = GET_BT_INFO(padapter);

	RfState = padapter->pwrctrlpriv.rf_pwrstate;

	if (RfState != rf_on) {
		mod_timer(&pBTInfo->BTPsDisableTimer,
			  jiffies + msecs_to_jiffies(50));
		return false;
	}
	return true;
}

static void bthci_ResponderStartToScan(struct rtw_adapter *padapter)
{
}

static u8 bthci_PhyLinkConnectionInProgress(struct rtw_adapter *padapter, u8 PhyLinkHandle)
{
	struct bt_30info *pBTInfo;
	struct bt_mgnt *pBtMgnt;

	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->bPhyLinkInProgress &&
		(pBtMgnt->BtCurrentPhyLinkhandle == PhyLinkHandle))
		return true;
	return false;
}

static void bthci_ResetFlowSpec(struct rtw_adapter *padapter, u8 EntryNum, u8 index)
{
	struct bt_30info *pBTinfo;

	pBTinfo = GET_BT_INFO(padapter);

	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].BtLogLinkhandle = 0;
	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].BtPhyLinkhandle = 0;
	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].bLLCompleteEventIsSet = false;
	pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[index].bLLCancelCMDIsSetandComplete = false;
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

static void bthci_ResetEntry(struct rtw_adapter *padapter, u8 EntryNum)
{
	struct bt_30info *pBTinfo;
	struct bt_mgnt *pBtMgnt;
	u8 j;

	pBTinfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTinfo->BtMgnt;

	pBTinfo->BtAsocEntry[EntryNum].bUsed = false;
	pBTinfo->BtAsocEntry[EntryNum].BtCurrentState = HCI_STATE_DISCONNECTED;
	pBTinfo->BtAsocEntry[EntryNum].BtNextState = HCI_STATE_DISCONNECTED;

	pBTinfo->BtAsocEntry[EntryNum].AmpAsocCmdData.AMPAssocRemLen = 0;
	pBTinfo->BtAsocEntry[EntryNum].AmpAsocCmdData.BtPhyLinkhandle = 0;
	if (pBTinfo->BtAsocEntry[EntryNum].AmpAsocCmdData.AMPAssocfragment != NULL)
		memset(pBTinfo->BtAsocEntry[EntryNum].AmpAsocCmdData.AMPAssocfragment, 0, TOTAL_ALLOCIATE_ASSOC_LEN);
	pBTinfo->BtAsocEntry[EntryNum].AmpAsocCmdData.LenSoFar = 0;

	pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKeyType = 0;
	pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle = 0;
	memset(pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKey, 0,
	       pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKeyLen);
	pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKeyLen = 0;

	/* 0x640; 0.625ms*1600 = 1000ms, 0.625ms*16000 = 10000ms */
	pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.LinkSuperversionTimeout = 0x3e80;

	pBTinfo->BtAsocEntry[EntryNum].AMPRole = AMP_BTAP_NONE;

	pBTinfo->BtAsocEntry[EntryNum].mAssoc = false;
	pBTinfo->BtAsocEntry[EntryNum].b4waySuccess = false;

	/*  Reset BT WPA */
	pBTinfo->BtAsocEntry[EntryNum].KeyReplayCounter = 0;
	pBTinfo->BtAsocEntry[EntryNum].BTWPAAuthState = STATE_WPA_AUTH_UNINITIALIZED;

	pBTinfo->BtAsocEntry[EntryNum].bSendSupervisionPacket = false;
	pBTinfo->BtAsocEntry[EntryNum].NoRxPktCnt = 0;
	pBTinfo->BtAsocEntry[EntryNum].ShortRangeMode = 0;
	pBTinfo->BtAsocEntry[EntryNum].rxSuvpPktCnt = 0;

	for (j = 0; j < MAX_LOGICAL_LINK_NUM; j++)
		bthci_ResetFlowSpec(padapter, EntryNum, j);

	pBtMgnt->BTAuthCount = 0;
	pBtMgnt->BTAsocCount = 0;
	pBtMgnt->BTCurrentConnectType = BT_DISCONNECT;
	pBtMgnt->BTReceiveConnectPkt = BT_DISCONNECT;

	HALBT_RemoveKey(padapter, EntryNum);
}

static void bthci_RemoveEntryByEntryNum(struct rtw_adapter *padapter, u8 EntryNum)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	bthci_ResetEntry(padapter, EntryNum);

	if (pBtMgnt->CurrentBTConnectionCnt > 0)
		pBtMgnt->CurrentBTConnectionCnt--;

	RTPRINT(FIOCTL, IOCTL_STATE, ("[BT Flag], CurrentBTConnectionCnt = %d!!\n",
		pBtMgnt->CurrentBTConnectionCnt));

	if (pBtMgnt->CurrentBTConnectionCnt > 0) {
		pBtMgnt->BtOperationOn = true;
	} else {
		pBtMgnt->BtOperationOn = false;
		RTPRINT(FIOCTL, IOCTL_STATE, ("[BT Flag], Bt Operation OFF!!\n"));
	}

	if (!pBtMgnt->BtOperationOn) {
		del_timer_sync(&pBTInfo->BTHCIDiscardAclDataTimer);
		del_timer_sync(&pBTInfo->BTBeaconTimer);
		pBtMgnt->bStartSendSupervisionPkt = false;
	}
}

static u8
bthci_CommandCompleteHeader(
	u8 *pbuf,
	u16		OGF,
	u16		OCF,
	enum hci_status	status
	)
{
	struct packet_irp_hcievent_data *PPacketIrpEvent = (struct packet_irp_hcievent_data *)pbuf;
	u8 NumHCI_Comm = 0x1;

	PPacketIrpEvent->EventCode = HCI_EVENT_COMMAND_COMPLETE;
	PPacketIrpEvent->Data[0] = NumHCI_Comm;	/* packet # */
	PPacketIrpEvent->Data[1] = HCIOPCODELOW(OCF, OGF);
	PPacketIrpEvent->Data[2] = HCIOPCODEHIGHT(OCF, OGF);

	if (OGF == OGF_EXTENSION) {
		if (OCF == HCI_SET_RSSI_VALUE) {
			RTPRINT(FIOCTL, (IOCTL_BT_EVENT_PERIODICAL),
				("[BT event], CommandComplete, Num_HCI_Comm = 0x%x, Opcode = 0x%02x%02x, status = 0x%x, OGF = 0x%x, OCF = 0x%x\n",
				NumHCI_Comm, (HCIOPCODEHIGHT(OCF, OGF)), (HCIOPCODELOW(OCF, OGF)), status, OGF, OCF));
		} else {
			RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_EXT),
				("[BT event], CommandComplete, Num_HCI_Comm = 0x%x, Opcode = 0x%02x%02x, status = 0x%x, OGF = 0x%x, OCF = 0x%x\n",
				NumHCI_Comm, (HCIOPCODEHIGHT(OCF, OGF)), (HCIOPCODELOW(OCF, OGF)), status, OGF, OCF));
		}
	} else {
		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO),
			("[BT event], CommandComplete, Num_HCI_Comm = 0x%x, Opcode = 0x%02x%02x, status = 0x%x, OGF = 0x%x, OCF = 0x%x\n",
			NumHCI_Comm, (HCIOPCODEHIGHT(OCF, OGF)), (HCIOPCODELOW(OCF, OGF)), status, OGF, OCF));
	}
	return 3;
}

static u8 bthci_ExtensionEventHeaderRtk(u8 *pbuf, u8 extensionEvent)
{
	struct packet_irp_hcievent_data *PPacketIrpEvent = (struct packet_irp_hcievent_data *)pbuf;
	PPacketIrpEvent->EventCode = HCI_EVENT_EXTENSION_RTK;
	PPacketIrpEvent->Data[0] = extensionEvent;	/* extension event code */

	return 1;
}

static enum rt_status
bthci_IndicateEvent(
	struct rtw_adapter *padapter,
	void		*pEvntData,
	u32		dataLen
	)
{
	enum rt_status	rt_status;

	rt_status = PlatformIndicateBTEvent(padapter, pEvntData, dataLen);

	return rt_status;
}

static void
bthci_EventWriteRemoteAmpAssoc(
	struct rtw_adapter *padapter,
	enum hci_status	status,
	u8 PLHandle
	)
{
	u8 localBuf[TmpLocalBufSize] = "";
	u8 *pRetPar;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;

	PlatformZeroMemory(&localBuf[0], TmpLocalBufSize);
	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_STATUS_PARAMETERS,
		HCI_WRITE_REMOTE_AMP_ASSOC,
		status);
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("PhyLinkHandle = 0x%x, status = %d\n", PLHandle, status));
	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;		/* status */
	pRetPar[1] = PLHandle;
	len += 2;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
}

static void
bthci_EventEnhancedFlushComplete(
	struct rtw_adapter *padapter,
	u16					LLH
	)
{
	u8 localBuf[4] = "";
	struct packet_irp_hcievent_data *PPacketIrpEvent;

	RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("EventEnhancedFlushComplete, LLH = 0x%x\n", LLH));

	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);
	PPacketIrpEvent->EventCode = HCI_EVENT_ENHANCED_FLUSH_COMPLETE;
	PPacketIrpEvent->Length = 2;
	/* Logical link handle */
	PPacketIrpEvent->Data[0] = TWOBYTE_LOWBYTE(LLH);
	PPacketIrpEvent->Data[1] = TWOBYTE_HIGHTBYTE(LLH);

	bthci_IndicateEvent(padapter, PPacketIrpEvent, 4);
}

static void
bthci_EventShortRangeModeChangeComplete(
	struct rtw_adapter *padapter,
	enum hci_status				HciStatus,
	u8 		ShortRangeState,
	u8 		EntryNum
	)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	u8 localBuf[5] = "";
	struct packet_irp_hcievent_data *PPacketIrpEvent;

	if (!(pBtHciInfo->BTEventMaskPage2 & EMP2_HCI_EVENT_SHORT_RANGE_MODE_CHANGE_COMPLETE)) {
		RTPRINT(FIOCTL, IOCTL_BT_EVENT,
			("[BT event], Short Range Mode Change Complete, Ignore to send this event due to event mask page 2\n"));
		return;
	}
	RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], Short Range Mode Change Complete, Status = %d\n , PLH = 0x%x\n, Short_Range_Mode_State = 0x%x\n",
		HciStatus, pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle, ShortRangeState));

	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);
	PPacketIrpEvent->EventCode = HCI_EVENT_SHORT_RANGE_MODE_CHANGE_COMPLETE;
	PPacketIrpEvent->Length = 3;
	PPacketIrpEvent->Data[0] = HciStatus;
	PPacketIrpEvent->Data[1] = pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle;
	PPacketIrpEvent->Data[2] = ShortRangeState;
	bthci_IndicateEvent(padapter, PPacketIrpEvent, 5);
}

static void bthci_EventSendFlowSpecModifyComplete(struct rtw_adapter *padapter,
						  enum hci_status HciStatus,
						  u16 logicHandle)
{
	u8 localBuf[5] = "";
	struct packet_irp_hcievent_data *PPacketIrpEvent;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;

	if (!(pBtHciInfo->BTEventMaskPage2 & EMP2_HCI_EVENT_FLOW_SPEC_MODIFY_COMPLETE)) {
		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO),
			("[BT event], Flow Spec Modify Complete, Ignore to send this event due to event mask page 2\n"));
		return;
	}
	RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO),
		("[BT event], Flow Spec Modify Complete, status = 0x%x, LLH = 0x%x\n", HciStatus, logicHandle));
	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);
	PPacketIrpEvent->EventCode = HCI_EVENT_FLOW_SPEC_MODIFY_COMPLETE;
	PPacketIrpEvent->Length = 3;

	PPacketIrpEvent->Data[0] = HciStatus;
	/* Logical link handle */
	PPacketIrpEvent->Data[1] = TWOBYTE_LOWBYTE(logicHandle);
	PPacketIrpEvent->Data[2] = TWOBYTE_HIGHTBYTE(logicHandle);

	bthci_IndicateEvent(padapter, PPacketIrpEvent, 5);
}

static void
bthci_EventExtWifiScanNotify(
	struct rtw_adapter *padapter,
	u8 			scanType
	)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	u8 len = 0;
	u8 localBuf[7] = "";
	u8 *pRetPar;
	u8 *pu1Temp;
	struct packet_irp_hcievent_data *PPacketIrpEvent;

	if (!pBtMgnt->BtOperationOn)
		return;

	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_ExtensionEventHeaderRtk(&localBuf[0], HCI_EVENT_EXT_WIFI_SCAN_NOTIFY);

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pu1Temp = (u8 *)&pRetPar[0];
	*pu1Temp = scanType;
	len += 1;

	PPacketIrpEvent->Length = len;

	if (bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2) == RT_STATUS_SUCCESS) {
		RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], Wifi scan notify, scan type = %d\n",
			scanType));
	}
}

static void
bthci_EventAMPReceiverReport(
	struct rtw_adapter *padapter,
	u8 Reason
	)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;

	if (pBtHciInfo->bTestNeedReport) {
		u8 localBuf[20] = "";
		u32	*pu4Temp;
		u16	*pu2Temp;
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), (" HCI_EVENT_AMP_RECEIVER_REPORT\n"));
		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);
		PPacketIrpEvent->EventCode = HCI_EVENT_AMP_RECEIVER_REPORT;
		PPacketIrpEvent->Length = 2;

		PPacketIrpEvent->Data[0] = pBtHciInfo->TestCtrType;

		PPacketIrpEvent->Data[1] = Reason;

		pu4Temp = (u32 *)&PPacketIrpEvent->Data[2];
		*pu4Temp = pBtHciInfo->TestEventType;

		pu2Temp = (u16 *)&PPacketIrpEvent->Data[6];
		*pu2Temp = pBtHciInfo->TestNumOfFrame;

		pu2Temp = (u16 *)&PPacketIrpEvent->Data[8];
		*pu2Temp = pBtHciInfo->TestNumOfErrFrame;

		pu4Temp = (u32 *)&PPacketIrpEvent->Data[10];
		*pu4Temp = pBtHciInfo->TestNumOfBits;

		pu4Temp = (u32 *)&PPacketIrpEvent->Data[14];
		*pu4Temp = pBtHciInfo->TestNumOfErrBits;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, 20);

		/* Return to Idel state with RX and TX off. */

	}

	pBtHciInfo->TestNumOfFrame = 0x00;
}

static void
bthci_EventChannelSelected(
	struct rtw_adapter *padapter,
	u8 	EntryNum
	)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	u8 localBuf[3] = "";
	struct packet_irp_hcievent_data *PPacketIrpEvent;

	if (!(pBtHciInfo->BTEventMaskPage2 & EMP2_HCI_EVENT_CHANNEL_SELECT)) {
		RTPRINT(FIOCTL, IOCTL_BT_EVENT,
			("[BT event], Channel Selected, Ignore to send this event due to event mask page 2\n"));
		return;
	}

	RTPRINT(FIOCTL, IOCTL_BT_EVENT|IOCTL_STATE,
		("[BT event], Channel Selected, PhyLinkHandle %d\n",
		pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle));

	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);
	PPacketIrpEvent->EventCode = HCI_EVENT_CHANNEL_SELECT;
	PPacketIrpEvent->Length = 1;
	PPacketIrpEvent->Data[0] = pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle;
	bthci_IndicateEvent(padapter, PPacketIrpEvent, 3);
}

static void
bthci_EventDisconnectPhyLinkComplete(
	struct rtw_adapter *padapter,
	enum hci_status				HciStatus,
	enum hci_status				Reason,
	u8 		EntryNum
	)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	u8 localBuf[5] = "";
	struct packet_irp_hcievent_data *PPacketIrpEvent;

	if (!(pBtHciInfo->BTEventMaskPage2 & EMP2_HCI_EVENT_DISCONNECT_PHY_LINK_COMPLETE)) {
		RTPRINT(FIOCTL, IOCTL_BT_EVENT,
			("[BT event], Disconnect Physical Link Complete, Ignore to send this event due to event mask page 2\n"));
		return;
	}
	RTPRINT(FIOCTL, IOCTL_BT_EVENT,
		("[BT event], Disconnect Physical Link Complete, Status = 0x%x, PLH = 0x%x Reason = 0x%x\n",
		HciStatus, pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle, Reason));
	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);
	PPacketIrpEvent->EventCode = HCI_EVENT_DISCONNECT_PHY_LINK_COMPLETE;
	PPacketIrpEvent->Length = 3;
	PPacketIrpEvent->Data[0] = HciStatus;
	PPacketIrpEvent->Data[1] = pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle;
	PPacketIrpEvent->Data[2] = Reason;
	bthci_IndicateEvent(padapter, PPacketIrpEvent, 5);
}

static void
bthci_EventPhysicalLinkComplete(
	struct rtw_adapter *padapter,
	enum hci_status				HciStatus,
	u8 		EntryNum,
	u8 		PLHandle
	)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	struct bt_dgb *pBtDbg = &pBTInfo->BtDbg;
	u8 localBuf[4] = "";
	struct packet_irp_hcievent_data *PPacketIrpEvent;
	u8 PL_handle;

	pBtMgnt->bPhyLinkInProgress = false;
	pBtDbg->dbgHciInfo.hciCmdPhyLinkStatus = HciStatus;
	if (!(pBtHciInfo->BTEventMaskPage2 & EMP2_HCI_EVENT_PHY_LINK_COMPLETE)) {
		RTPRINT(FIOCTL, IOCTL_BT_EVENT,
			("[BT event], Physical Link Complete, Ignore to send this event due to event mask page 2\n"));
		return;
	}

	if (EntryNum == 0xff) {
		/*  connection not started yet, just use the input physical link handle to response. */
		PL_handle = PLHandle;
	} else {
		/*  connection is under progress, use the phy link handle we recorded. */
		PL_handle  = pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle;
		pBTInfo->BtAsocEntry[EntryNum].bNeedPhysLinkCompleteEvent = false;
	}

	RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], Physical Link Complete, Status = 0x%x PhyLinkHandle = 0x%x\n", HciStatus,
		PL_handle));

	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);
	PPacketIrpEvent->EventCode = HCI_EVENT_PHY_LINK_COMPLETE;
	PPacketIrpEvent->Length = 2;

	PPacketIrpEvent->Data[0] = HciStatus;
	PPacketIrpEvent->Data[1] = PL_handle;
	bthci_IndicateEvent(padapter, PPacketIrpEvent, 4);

}

static void
bthci_EventCommandStatus(
	struct rtw_adapter *padapter,
	u8 		OGF,
	u16					OCF,
	enum hci_status				HciStatus
	)
{

	u8 localBuf[6] = "";
	struct packet_irp_hcievent_data *PPacketIrpEvent;
	u8 Num_Hci_Comm = 0x1;
	RTPRINT(FIOCTL, IOCTL_BT_EVENT,
		("[BT event], CommandStatus, Opcode = 0x%02x%02x, OGF = 0x%x,  OCF = 0x%x, Status = 0x%x, Num_HCI_COMM = 0x%x\n",
		(HCIOPCODEHIGHT(OCF, OGF)), (HCIOPCODELOW(OCF, OGF)), OGF, OCF, HciStatus, Num_Hci_Comm));

	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);
	PPacketIrpEvent->EventCode = HCI_EVENT_COMMAND_STATUS;
	PPacketIrpEvent->Length = 4;
	PPacketIrpEvent->Data[0] = HciStatus;	/* current pending */
	PPacketIrpEvent->Data[1] = Num_Hci_Comm;	/* packet # */
	PPacketIrpEvent->Data[2] = HCIOPCODELOW(OCF, OGF);
	PPacketIrpEvent->Data[3] = HCIOPCODEHIGHT(OCF, OGF);

	bthci_IndicateEvent(padapter, PPacketIrpEvent, 6);

}

static void
bthci_EventLogicalLinkComplete(
	struct rtw_adapter *padapter,
	enum hci_status				HciStatus,
	u8 		PhyLinkHandle,
	u16					LogLinkHandle,
	u8 		LogLinkIndex,
	u8 		EntryNum
	)
{
/*PMGNT_INFO pMgntInfo = &padapter->MgntInfo; */
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	u8 localBuf[7] = "";
	struct packet_irp_hcievent_data *PPacketIrpEvent;

	if (!(pBtHciInfo->BTEventMaskPage2 & EMP2_HCI_EVENT_LOGICAL_LINK_COMPLETE)) {
		RTPRINT(FIOCTL, IOCTL_BT_EVENT,
			("[BT event], Logical Link Complete, Ignore to send this event due to event mask page 2\n"));
		return;
	}
	RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], Logical Link Complete, PhyLinkHandle = 0x%x,  LogLinkHandle = 0x%x, Status = 0x%x\n",
		PhyLinkHandle, LogLinkHandle, HciStatus));

	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);
	PPacketIrpEvent->EventCode = HCI_EVENT_LOGICAL_LINK_COMPLETE;
	PPacketIrpEvent->Length = 5;

	PPacketIrpEvent->Data[0] = HciStatus;/* status code */
	/* Logical link handle */
	PPacketIrpEvent->Data[1] = TWOBYTE_LOWBYTE(LogLinkHandle);
	PPacketIrpEvent->Data[2] = TWOBYTE_HIGHTBYTE(LogLinkHandle);
	/* Physical link handle */
	PPacketIrpEvent->Data[3] = TWOBYTE_LOWBYTE(PhyLinkHandle);
	/* corresponding Tx flow spec ID */
	if (HciStatus == HCI_STATUS_SUCCESS) {
		PPacketIrpEvent->Data[4] =
			pBTInfo->BtAsocEntry[EntryNum].LogLinkCmdData[LogLinkIndex].Tx_Flow_Spec.Identifier;
	} else {
		PPacketIrpEvent->Data[4] = 0x0;
	}

	bthci_IndicateEvent(padapter, PPacketIrpEvent, 7);
}

static void
bthci_EventDisconnectLogicalLinkComplete(
	struct rtw_adapter *padapter,
	enum hci_status				HciStatus,
	u16					LogLinkHandle,
	enum hci_status				Reason
	)
{
	u8 localBuf[6] = "";
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	struct packet_irp_hcievent_data *PPacketIrpEvent;

	if (!(pBtHciInfo->BTEventMaskPage2 & EMP2_HCI_EVENT_DISCONNECT_LOGICAL_LINK_COMPLETE)) {
		RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], Disconnect Logical Link Complete, Ignore to send this event due to event mask page 2\n"));
		return;
	}
	RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], Disconnect Logical Link Complete, Status = 0x%x, LLH = 0x%x Reason = 0x%x\n", HciStatus, LogLinkHandle, Reason));

	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);
	PPacketIrpEvent->EventCode = HCI_EVENT_DISCONNECT_LOGICAL_LINK_COMPLETE;
	PPacketIrpEvent->Length = 4;

	PPacketIrpEvent->Data[0] = HciStatus;
	/* Logical link handle */
	PPacketIrpEvent->Data[1] = TWOBYTE_LOWBYTE(LogLinkHandle);
	PPacketIrpEvent->Data[2] = TWOBYTE_HIGHTBYTE(LogLinkHandle);
	/* Disconnect reason */
	PPacketIrpEvent->Data[3] = Reason;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, 6);
}

static void
bthci_EventFlushOccurred(
	struct rtw_adapter *padapter,
	u16					LogLinkHandle
	)
{
	u8 localBuf[4] = "";
	struct packet_irp_hcievent_data *PPacketIrpEvent;
	RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("bthci_EventFlushOccurred(), LLH = 0x%x\n", LogLinkHandle));

	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);
	PPacketIrpEvent->EventCode = HCI_EVENT_FLUSH_OCCRUED;
	PPacketIrpEvent->Length = 2;
	/* Logical link handle */
	PPacketIrpEvent->Data[0] = TWOBYTE_LOWBYTE(LogLinkHandle);
	PPacketIrpEvent->Data[1] = TWOBYTE_HIGHTBYTE(LogLinkHandle);

	bthci_IndicateEvent(padapter, PPacketIrpEvent, 4);
}

static enum hci_status
bthci_BuildPhysicalLink(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd,
	u16	OCF
)
{
	enum hci_status		status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	u8 EntryNum, PLH;

	/* Send HCI Command status event to AMP. */
	bthci_EventCommandStatus(padapter,
			LINK_CONTROL_COMMANDS,
			OCF,
			HCI_STATUS_SUCCESS);

	PLH = *((u8 *)pHciCmd->Data);

	/*  Check if resource or bt connection is under progress, if yes, reject the link creation. */
	if (!bthci_AddEntry(padapter)) {
		status = HCI_STATUS_CONNECT_RJT_LIMIT_RESOURCE;
		bthci_EventPhysicalLinkComplete(padapter, status, INVALID_ENTRY_NUM, PLH);
		return status;
	}

	EntryNum = pBtMgnt->CurrentConnectEntryNum;
	pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle = PLH;
	pBtMgnt->BtCurrentPhyLinkhandle = PLH;

	if (pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.AMPAssocfragment == NULL) {
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Create/Accept PhysicalLink, AMP controller is busy\n"));
		status = HCI_STATUS_CONTROLLER_BUSY;
		bthci_EventPhysicalLinkComplete(padapter, status, INVALID_ENTRY_NUM, PLH);
		return status;
	}

	/*  Record Key and the info */
	pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKeyLen = (*((u8 *)pHciCmd->Data+1));
	pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKeyType = (*((u8 *)pHciCmd->Data+2));
	memcpy(pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKey,
		(((u8 *)pHciCmd->Data+3)), pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKeyLen);
	memcpy(pBTInfo->BtAsocEntry[EntryNum].PMK, pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKey, PMK_LEN);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("BuildPhysicalLink, EntryNum = %d, PLH = 0x%x  KeyLen = 0x%x, KeyType = 0x%x\n",
		EntryNum, pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle,
		pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKeyLen,
		pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKeyType));
	RTPRINT_DATA(FIOCTL, (IOCTL_BT_LOGO|IOCTL_BT_HCICMD), ("BtAMPKey\n"), pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKey,
		pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtAMPKeyLen);
	RTPRINT_DATA(FIOCTL, (IOCTL_BT_LOGO|IOCTL_BT_HCICMD), ("PMK\n"), pBTInfo->BtAsocEntry[EntryNum].PMK,
		PMK_LEN);

	if (OCF == HCI_CREATE_PHYSICAL_LINK) {
		/* These macros require braces */
		BTHCI_SM_WITH_INFO(padapter, HCI_STATE_DISCONNECTED, STATE_CMD_CREATE_PHY_LINK, EntryNum);
	} else if (OCF == HCI_ACCEPT_PHYSICAL_LINK) {
		BTHCI_SM_WITH_INFO(padapter, HCI_STATE_DISCONNECTED, STATE_CMD_ACCEPT_PHY_LINK, EntryNum);
	}

	return status;
}

static void
bthci_BuildLogicalLink(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd,
	u16 OCF
	)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTinfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTinfo->BtMgnt;
	u8 PhyLinkHandle, EntryNum;
	static u16 AssignLogHandle = 1;

	struct hci_flow_spec	TxFlowSpec;
	struct hci_flow_spec	RxFlowSpec;
	u32	MaxSDUSize, ArriveTime, Bandwidth;

	PhyLinkHandle = *((u8 *)pHciCmd->Data);

	EntryNum = bthci_GetCurrentEntryNum(padapter, PhyLinkHandle);

	memcpy(&TxFlowSpec,
		&pHciCmd->Data[1], sizeof(struct hci_flow_spec));
	memcpy(&RxFlowSpec,
		&pHciCmd->Data[17], sizeof(struct hci_flow_spec));

	MaxSDUSize = TxFlowSpec.MaximumSDUSize;
	ArriveTime = TxFlowSpec.SDUInterArrivalTime;

	if (bthci_CheckLogLinkBehavior(padapter, TxFlowSpec) && bthci_CheckLogLinkBehavior(padapter, RxFlowSpec))
		Bandwidth = BTTOTALBANDWIDTH;
	else if (MaxSDUSize == 0xffff && ArriveTime == 0xffffffff)
		Bandwidth = BTTOTALBANDWIDTH;
	else
		Bandwidth = MaxSDUSize*8*1000/(ArriveTime+244);

	RTPRINT(FIOCTL, IOCTL_BT_HCICMD,
		("BuildLogicalLink, PhyLinkHandle = 0x%x, MaximumSDUSize = 0x%x, SDUInterArrivalTime = 0x%x, Bandwidth = 0x%x\n",
		PhyLinkHandle, MaxSDUSize, ArriveTime, Bandwidth));

	if (EntryNum == 0xff) {
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Invalid Physical Link handle = 0x%x, status = HCI_STATUS_UNKNOW_CONNECT_ID, return\n", PhyLinkHandle));
		status = HCI_STATUS_UNKNOW_CONNECT_ID;

		/* When we receive Create/Accept logical link command, we should send command status event first. */
		bthci_EventCommandStatus(padapter,
			LINK_CONTROL_COMMANDS,
			OCF,
			status);
		return;
	}

	if (!pBtMgnt->bLogLinkInProgress) {
		if (bthci_PhyLinkConnectionInProgress(padapter, PhyLinkHandle)) {
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Physical link connection in progress, status = HCI_STATUS_CMD_DISALLOW, return\n"));
			status = HCI_STATUS_CMD_DISALLOW;

			pBtMgnt->bPhyLinkInProgressStartLL = true;
			/* When we receive Create/Accept logical link command, we should send command status event first. */
			bthci_EventCommandStatus(padapter,
				LINK_CONTROL_COMMANDS,
				OCF,
				status);

			return;
		}

		if (Bandwidth > BTTOTALBANDWIDTH) {
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("status = HCI_STATUS_QOS_REJECT, Bandwidth = 0x%x, return\n", Bandwidth));
			status = HCI_STATUS_QOS_REJECT;

			/* When we receive Create/Accept logical link command, we should send command status event first. */
			bthci_EventCommandStatus(padapter,
				LINK_CONTROL_COMMANDS,
				OCF,
				status);
		} else {
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("status = HCI_STATUS_SUCCESS\n"));
			status = HCI_STATUS_SUCCESS;

			/* When we receive Create/Accept logical link command, we should send command status event first. */
			bthci_EventCommandStatus(padapter,
				LINK_CONTROL_COMMANDS,
				OCF,
				status);

		}

		if (pBTinfo->BtAsocEntry[EntryNum].BtCurrentState != HCI_STATE_CONNECTED) {
			bthci_EventLogicalLinkComplete(padapter,
				HCI_STATUS_CMD_DISALLOW, 0, 0, 0, EntryNum);
		} else {
			u8 i, find = 0;

			pBtMgnt->bLogLinkInProgress = true;

			/*  find an unused logical link index and copy the data */
			for (i = 0; i < MAX_LOGICAL_LINK_NUM; i++) {
				if (pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].BtLogLinkhandle == 0) {
					enum hci_status LogCompEventstatus = HCI_STATUS_SUCCESS;

					pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].BtPhyLinkhandle = *((u8 *)pHciCmd->Data);
					pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].BtLogLinkhandle = AssignLogHandle;
					RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("BuildLogicalLink, EntryNum = %d, physical link handle = 0x%x, logical link handle = 0x%x\n",
						EntryNum, pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle,
								  pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].BtLogLinkhandle));
					memcpy(&pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].Tx_Flow_Spec,
						&TxFlowSpec, sizeof(struct hci_flow_spec));
					memcpy(&pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].Rx_Flow_Spec,
						&RxFlowSpec, sizeof(struct hci_flow_spec));

					pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].bLLCompleteEventIsSet = false;

					if (pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].bLLCancelCMDIsSetandComplete)
						LogCompEventstatus = HCI_STATUS_UNKNOW_CONNECT_ID;
					bthci_EventLogicalLinkComplete(padapter,
						LogCompEventstatus,
						pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].BtPhyLinkhandle,
						pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].BtLogLinkhandle, i, EntryNum);

					pBTinfo->BtAsocEntry[EntryNum].LogLinkCmdData[i].bLLCompleteEventIsSet = true;

					find = 1;
					pBtMgnt->BtCurrentLogLinkhandle = AssignLogHandle;
					AssignLogHandle++;
					break;
				}
			}

			if (!find) {
				bthci_EventLogicalLinkComplete(padapter,
					HCI_STATUS_CONNECT_RJT_LIMIT_RESOURCE, 0, 0, 0, EntryNum);
			}
			pBtMgnt->bLogLinkInProgress = false;
		}
	} else {
		bthci_EventLogicalLinkComplete(padapter,
			HCI_STATUS_CONTROLLER_BUSY, 0, 0, 0, EntryNum);
	}

}

static void
bthci_StartBeaconAndConnect(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd,
	u8 CurrentAssocNum
	)
{
/*PMGNT_INFO pMgntInfo = &padapter->MgntInfo; */
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("StartBeaconAndConnect, CurrentAssocNum =%d, AMPRole =%d\n",
		CurrentAssocNum,
		pBTInfo->BtAsocEntry[CurrentAssocNum].AMPRole));

	if (!pBtMgnt->CheckChnlIsSuit) {
		bthci_EventPhysicalLinkComplete(padapter, HCI_STATUS_CONNECT_REJ_NOT_SUIT_CHNL_FOUND, CurrentAssocNum, INVALID_PL_HANDLE);
		bthci_RemoveEntryByEntryNum(padapter, CurrentAssocNum);
		return;
	}

	if (pBTInfo->BtAsocEntry[CurrentAssocNum].AMPRole == AMP_BTAP_CREATOR) {
		rsprintf((char *)pBTInfo->BtAsocEntry[CurrentAssocNum].BTSsidBuf, 32, "AMP-%02x-%02x-%02x-%02x-%02x-%02x",
		padapter->eeprompriv.mac_addr[0],
		padapter->eeprompriv.mac_addr[1],
		padapter->eeprompriv.mac_addr[2],
		padapter->eeprompriv.mac_addr[3],
		padapter->eeprompriv.mac_addr[4],
		padapter->eeprompriv.mac_addr[5]);
	} else if (pBTInfo->BtAsocEntry[CurrentAssocNum].AMPRole == AMP_BTAP_JOINER) {
		rsprintf((char *)pBTInfo->BtAsocEntry[CurrentAssocNum].BTSsidBuf, 32, "AMP-%02x-%02x-%02x-%02x-%02x-%02x",
		pBTInfo->BtAsocEntry[CurrentAssocNum].BTRemoteMACAddr[0],
		pBTInfo->BtAsocEntry[CurrentAssocNum].BTRemoteMACAddr[1],
		pBTInfo->BtAsocEntry[CurrentAssocNum].BTRemoteMACAddr[2],
		pBTInfo->BtAsocEntry[CurrentAssocNum].BTRemoteMACAddr[3],
		pBTInfo->BtAsocEntry[CurrentAssocNum].BTRemoteMACAddr[4],
		pBTInfo->BtAsocEntry[CurrentAssocNum].BTRemoteMACAddr[5]);
	}

	FillOctetString(pBTInfo->BtAsocEntry[CurrentAssocNum].BTSsid, pBTInfo->BtAsocEntry[CurrentAssocNum].BTSsidBuf, 21);
	pBTInfo->BtAsocEntry[CurrentAssocNum].BTSsid.Length = 21;

	/* To avoid set the start ap or connect twice, or the original connection will be disconnected. */
	if (!pBtMgnt->bBTConnectInProgress) {
		pBtMgnt->bBTConnectInProgress = true;
		RTPRINT(FIOCTL, IOCTL_STATE, ("[BT Flag], BT Connect in progress ON!!\n"));
		BTHCI_SM_WITH_INFO(padapter, HCI_STATE_STARTING, STATE_CMD_MAC_START_COMPLETE, CurrentAssocNum);

		/*  20100325 Joseph: Check RF ON/OFF. */
		/*  If RF OFF, it reschedule connecting operation after 50ms. */
		if (!bthci_CheckRfStateBeforeConnect(padapter))
			return;

		if (pBTInfo->BtAsocEntry[CurrentAssocNum].AMPRole == AMP_BTAP_CREATOR) {
			/* These macros need braces */
			BTHCI_SM_WITH_INFO(padapter, HCI_STATE_CONNECTING, STATE_CMD_MAC_CONNECT_COMPLETE, CurrentAssocNum);
		} else if (pBTInfo->BtAsocEntry[CurrentAssocNum].AMPRole == AMP_BTAP_JOINER) {
			bthci_ResponderStartToScan(padapter);
		}
	}
	RT_PRINT_STR(_module_rtl871x_mlme_c_, _drv_notice_,
		     "StartBeaconAndConnect, SSID:\n",
		     pBTInfo->BtAsocEntry[pBtMgnt->CurrentConnectEntryNum].BTSsid.Octet,
		     pBTInfo->BtAsocEntry[pBtMgnt->CurrentConnectEntryNum].BTSsid.Length);
}

static void bthci_ResetBtMgnt(struct bt_mgnt *pBtMgnt)
{
	pBtMgnt->BtOperationOn = false;
	pBtMgnt->bBTConnectInProgress = false;
	pBtMgnt->bLogLinkInProgress = false;
	pBtMgnt->bPhyLinkInProgress = false;
	pBtMgnt->bPhyLinkInProgressStartLL = false;
	pBtMgnt->DisconnectEntryNum = 0xff;
	pBtMgnt->bStartSendSupervisionPkt = false;
	pBtMgnt->JoinerNeedSendAuth = false;
	pBtMgnt->CurrentBTConnectionCnt = 0;
	pBtMgnt->BTCurrentConnectType = BT_DISCONNECT;
	pBtMgnt->BTReceiveConnectPkt = BT_DISCONNECT;
	pBtMgnt->BTAuthCount = 0;
	pBtMgnt->btLogoTest = 0;
}

static void bthci_ResetBtHciInfo(struct bt_hci_info *pBtHciInfo)
{
	pBtHciInfo->BTEventMask = 0;
	pBtHciInfo->BTEventMaskPage2 = 0;
	pBtHciInfo->ConnAcceptTimeout =  10000;
	pBtHciInfo->PageTimeout  =  0x30;
	pBtHciInfo->LocationDomainAware = 0x0;
	pBtHciInfo->LocationDomain = 0x5858;
	pBtHciInfo->LocationDomainOptions = 0x58;
	pBtHciInfo->LocationOptions = 0x0;
	pBtHciInfo->FlowControlMode = 0x1;	/*  0:Packet based data flow control mode(BR/EDR), 1: Data block based data flow control mode(AMP). */

	pBtHciInfo->enFlush_LLH = 0;
	pBtHciInfo->FLTO_LLH = 0;

	/* Test command only */
	pBtHciInfo->bTestIsEnd = true;
	pBtHciInfo->bInTestMode = false;
	pBtHciInfo->bTestNeedReport = false;
	pBtHciInfo->TestScenario = 0xff;
	pBtHciInfo->TestReportInterval = 0x01;
	pBtHciInfo->TestCtrType = 0x5d;
	pBtHciInfo->TestEventType = 0x00;
	pBtHciInfo->TestNumOfFrame = 0;
	pBtHciInfo->TestNumOfErrFrame = 0;
	pBtHciInfo->TestNumOfBits = 0;
	pBtHciInfo->TestNumOfErrBits = 0;
}

static void bthci_ResetBtSec(struct rtw_adapter *padapter, struct bt_security *pBtSec)
{
/*PMGNT_INFO	pMgntInfo = &padapter->MgntInfo; */

	/*  Set BT used HW or SW encrypt !! */
	if (GET_HAL_DATA(padapter)->bBTMode)
		pBtSec->bUsedHwEncrypt = true;
	else
		pBtSec->bUsedHwEncrypt = false;
	RT_TRACE(_module_rtl871x_security_c_, _drv_info_, ("%s: bUsedHwEncrypt =%d\n", __func__, pBtSec->bUsedHwEncrypt));

	pBtSec->RSNIE.Octet = pBtSec->RSNIEBuf;
}

static void bthci_ResetBtExtInfo(struct bt_mgnt *pBtMgnt)
{
	u8 i;

	for (i = 0; i < MAX_BT_ASOC_ENTRY_NUM; i++) {
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

	pBtMgnt->ExtConfig.bManualControl = false;
	pBtMgnt->ExtConfig.bBTBusy = false;
	pBtMgnt->ExtConfig.bBTA2DPBusy = false;
}

static enum hci_status bthci_CmdReset(struct rtw_adapter *_padapter, u8 bNeedSendEvent)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	struct rtw_adapter *padapter;
/*PMGNT_INFO pMgntInfo = &padapter->MgntInfo; */
	struct bt_30info *pBTInfo;
	struct bt_mgnt *pBtMgnt;
	struct bt_hci_info *pBtHciInfo;
	struct bt_security *pBtSec;
	struct bt_dgb *pBtDbg;
	u8 i;

	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("bthci_CmdReset()\n"));

	padapter = GetDefaultAdapter(_padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;
	pBtHciInfo = &pBTInfo->BtHciInfo;
	pBtSec = &pBTInfo->BtSec;
	pBtDbg = &pBTInfo->BtDbg;

	pBTInfo->padapter = padapter;

	for (i = 0; i < MAX_BT_ASOC_ENTRY_NUM; i++)
		bthci_ResetEntry(padapter, i);

	bthci_ResetBtMgnt(pBtMgnt);
	bthci_ResetBtHciInfo(pBtHciInfo);
	bthci_ResetBtSec(padapter, pBtSec);

	pBtMgnt->BTChannel = BT_Default_Chnl;
	pBtMgnt->CheckChnlIsSuit = true;

	pBTInfo->BTBeaconTmrOn = false;

	pBtMgnt->bCreateSpportQos = true;

	del_timer_sync(&pBTInfo->BTHCIDiscardAclDataTimer);
	del_timer_sync(&pBTInfo->BTBeaconTimer);

	HALBT_SetRtsCtsNoLenLimit(padapter);
	/*  */
	/*  Maybe we need to take care Group != AES case !! */
	/*  now we Pairwise and Group all used AES !! */

	bthci_ResetBtExtInfo(pBtMgnt);

	/* send command complete event here when all data are received. */
	if (bNeedSendEvent) {
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_RESET,
			status);

		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		/* status */
		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

static enum hci_status
bthci_CmdWriteRemoteAMPAssoc(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_dgb *pBtDbg = &pBTInfo->BtDbg;
	u8 CurrentAssocNum;
	u8 PhyLinkHandle;

	pBtDbg->dbgHciInfo.hciCmdCntWriteRemoteAmpAssoc++;
	PhyLinkHandle = *((u8 *)pHciCmd->Data);
	CurrentAssocNum = bthci_GetCurrentEntryNum(padapter, PhyLinkHandle);

	if (CurrentAssocNum == 0xff) {
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("WriteRemoteAMPAssoc, No such Handle in the Entry\n"));
		status = HCI_STATUS_UNKNOW_CONNECT_ID;
		bthci_EventWriteRemoteAmpAssoc(padapter, status, PhyLinkHandle);
		return status;
	}

	if (pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.AMPAssocfragment == NULL) {
		RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("WriteRemoteAMPAssoc, AMP controller is busy\n"));
		status = HCI_STATUS_CONTROLLER_BUSY;
		bthci_EventWriteRemoteAmpAssoc(padapter, status, PhyLinkHandle);
		return status;
	}

	pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.BtPhyLinkhandle = PhyLinkHandle;/* u8 *)pHciCmd->Data); */
	pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.LenSoFar = *((u16 *)((u8 *)pHciCmd->Data+1));
	pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.AMPAssocRemLen = *((u16 *)((u8 *)pHciCmd->Data+3));

	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("WriteRemoteAMPAssoc, LenSoFar = 0x%x, AssocRemLen = 0x%x\n",
		pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.LenSoFar,
		pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.AMPAssocRemLen));

	RTPRINT_DATA(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO),
		     ("WriteRemoteAMPAssoc fragment \n"),
		     pHciCmd->Data,
		     pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.AMPAssocRemLen+5);
	if ((pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.AMPAssocRemLen) > MAX_AMP_ASSOC_FRAG_LEN) {
		memcpy(((u8 *)pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.AMPAssocfragment+(pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.LenSoFar*(sizeof(u8)))),
			(u8 *)pHciCmd->Data+5,
			MAX_AMP_ASSOC_FRAG_LEN);
	} else {
		memcpy((u8 *)(pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.AMPAssocfragment)+(pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.LenSoFar*(sizeof(u8))),
			((u8 *)pHciCmd->Data+5),
			(pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.AMPAssocRemLen));

		RTPRINT_DATA(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), "WriteRemoteAMPAssoc :\n",
			pHciCmd->Data+5, pBTInfo->BtAsocEntry[CurrentAssocNum].AmpAsocCmdData.AMPAssocRemLen);

		if (!bthci_GetAssocInfo(padapter, CurrentAssocNum))
			status = HCI_STATUS_INVALID_HCI_CMD_PARA_VALUE;

		bthci_EventWriteRemoteAmpAssoc(padapter, status, PhyLinkHandle);

		bthci_StartBeaconAndConnect(padapter, pHciCmd, CurrentAssocNum);
	}

	return status;
}

/* 7.3.13 */
static enum hci_status bthci_CmdReadConnectionAcceptTimeout(struct rtw_adapter *padapter)
{
	enum hci_status		status = HCI_STATUS_SUCCESS;
/*PMGNT_INFO		pMgntInfo = &padapter->MgntInfo; */
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	u8 localBuf[8] = "";
	u8 *pRetPar;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;
	u16 *pu2Temp;

	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_SET_EVENT_MASK_COMMAND,
		HCI_READ_CONNECTION_ACCEPT_TIMEOUT,
		status);

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;		/* status */
	pu2Temp = (u16 *)&pRetPar[1];		/*  Conn_Accept_Timeout */
	*pu2Temp = pBtHciInfo->ConnAcceptTimeout;
	len += 3;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);

	return status;
}

/* 7.3.14 */
static enum hci_status
bthci_CmdWriteConnectionAcceptTimeout(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status		status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	u16	*pu2Temp;
	u8 localBuf[6] = "";
	u8 *pRetPar;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;

	pu2Temp = (u16 *)&pHciCmd->Data[0];
	pBtHciInfo->ConnAcceptTimeout = *pu2Temp;
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_DETAIL, ("ConnAcceptTimeout = 0x%x",
		pBtHciInfo->ConnAcceptTimeout));

	/* send command complete event here when all data are received. */
	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_SET_EVENT_MASK_COMMAND,
		HCI_WRITE_CONNECTION_ACCEPT_TIMEOUT,
		status);

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;		/* status */
	len += 1;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);

	return status;
}

static enum hci_status
bthci_CmdReadPageTimeout(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status		status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	u8 localBuf[8] = "";
	u8 *pRetPar;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;
	u16 *pu2Temp;

	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_SET_EVENT_MASK_COMMAND,
		HCI_READ_PAGE_TIMEOUT,
		status);

	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Read PageTimeout = 0x%x\n", pBtHciInfo->PageTimeout));
	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;		/* status */
	pu2Temp = (u16 *)&pRetPar[1];		/*  Page_Timeout */
	*pu2Temp = pBtHciInfo->PageTimeout;
	len += 3;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);

	return status;
}

static enum hci_status
bthci_CmdWritePageTimeout(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status		status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	u16	*pu2Temp;

	pu2Temp = (u16 *)&pHciCmd->Data[0];
	pBtHciInfo->PageTimeout = *pu2Temp;
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Write PageTimeout = 0x%x\n",
		pBtHciInfo->PageTimeout));

	/* send command complete event here when all data are received. */
	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_WRITE_PAGE_TIMEOUT,
			status);

		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		/* status */
		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

static enum hci_status
bthci_CmdReadLinkSupervisionTimeout(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status	status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTinfo = GET_BT_INFO(padapter);
	u8 physicalLinkHandle, EntryNum;

	physicalLinkHandle = *((u8 *)pHciCmd->Data);

	EntryNum = bthci_GetCurrentEntryNum(padapter, physicalLinkHandle);

	if (EntryNum == 0xff) {
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
		struct packet_irp_hcievent_data *PPacketIrpEvent;
		u16 *pu2Temp;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_READ_LINK_SUPERVISION_TIMEOUT,
			status);

		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;
		pRetPar[1] = pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle;
		pRetPar[2] = 0;
		pu2Temp = (u16 *)&pRetPar[3];		/*  Conn_Accept_Timeout */
		*pu2Temp = pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.LinkSuperversionTimeout;
		len += 5;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

static enum hci_status
bthci_CmdWriteLinkSupervisionTimeout(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status	status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTinfo = GET_BT_INFO(padapter);
	u8 physicalLinkHandle, EntryNum;

	physicalLinkHandle = *((u8 *)pHciCmd->Data);

	EntryNum = bthci_GetCurrentEntryNum(padapter, physicalLinkHandle);

	if (EntryNum == 0xff) {
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("WriteLinkSupervisionTimeout, No such Handle in the Entry\n"));
		status = HCI_STATUS_UNKNOW_CONNECT_ID;
	} else {
		if (pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle != physicalLinkHandle) {
			status = HCI_STATUS_UNKNOW_CONNECT_ID;
		} else {
			pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.LinkSuperversionTimeout = *((u16 *)(((u8 *)pHciCmd->Data)+2));
			RTPRINT(FIOCTL, IOCTL_STATE, ("BT Write LinkSuperversionTimeout[%d] = 0x%x\n",
				EntryNum, pBTinfo->BtAsocEntry[EntryNum].PhyLinkCmdData.LinkSuperversionTimeout));
		}
	}

	{
		u8 localBuf[8] = "";
		u8 *pRetPar;
		u8 len = 0;
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_WRITE_LINK_SUPERVISION_TIMEOUT,
			status);

		/*  Return parameters starts from here */
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

static enum hci_status
bthci_CmdEnhancedFlush(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status		status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTinfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTinfo->BtHciInfo;
	u16		logicHandle;
	u8 Packet_Type;

	logicHandle = *((u16 *)&pHciCmd->Data[0]);
	Packet_Type = pHciCmd->Data[2];

	if (Packet_Type != 0)
		status = HCI_STATUS_INVALID_HCI_CMD_PARA_VALUE;
	else
		pBtHciInfo->enFlush_LLH = logicHandle;

	if (bthci_DiscardTxPackets(padapter, pBtHciInfo->enFlush_LLH))
		bthci_EventFlushOccurred(padapter, pBtHciInfo->enFlush_LLH);

	/*  should send command status event */
	bthci_EventCommandStatus(padapter,
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_ENHANCED_FLUSH,
			status);

	if (pBtHciInfo->enFlush_LLH) {
		bthci_EventEnhancedFlushComplete(padapter, pBtHciInfo->enFlush_LLH);
		pBtHciInfo->enFlush_LLH = 0;
	}

	return status;
}

static enum hci_status
bthci_CmdReadLogicalLinkAcceptTimeout(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status		status = HCI_STATUS_SUCCESS;
/*PMGNT_INFO		pMgntInfo = &padapter->MgntInfo; */
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	u8 localBuf[8] = "";
	u8 *pRetPar;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;
	u16 *pu2Temp;

	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_SET_EVENT_MASK_COMMAND,
		HCI_READ_LOGICAL_LINK_ACCEPT_TIMEOUT,
		status);

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;

	pu2Temp = (u16 *)&pRetPar[1];		/*  Conn_Accept_Timeout */
	*pu2Temp = pBtHciInfo->LogicalAcceptTimeout;
	len += 3;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);

	return status;
}

static enum hci_status
bthci_CmdWriteLogicalLinkAcceptTimeout(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status		status = HCI_STATUS_SUCCESS;
/*PMGNT_INFO		pMgntInfo = &padapter->MgntInfo; */
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	u8 localBuf[6] = "";
	u8 *pRetPar;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;

	pBtHciInfo->LogicalAcceptTimeout = *((u16 *)pHciCmd->Data);

	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_SET_EVENT_MASK_COMMAND,
		HCI_WRITE_LOGICAL_LINK_ACCEPT_TIMEOUT,
		status);

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;

	len += 1;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	return status;
}

static enum hci_status
bthci_CmdSetEventMask(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status		status = HCI_STATUS_SUCCESS;
/*PMGNT_INFO		pMgntInfo = &padapter->MgntInfo; */
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	u8 *pu8Temp;
	u8 localBuf[6] = "";
	u8 *pRetPar;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;

	pu8Temp = (u8 *)&pHciCmd->Data[0];
	pBtHciInfo->BTEventMask = *pu8Temp;
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_DETAIL, ("BTEventMask = 0x%"i64fmt"x\n",
		pBtHciInfo->BTEventMask));

	/* send command complete event here when all data are received. */
	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_SET_EVENT_MASK_COMMAND,
		HCI_SET_EVENT_MASK,
		status);

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;		/* status */
	len += 1;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);

	return status;
}

/*  7.3.69 */
static enum hci_status
bthci_CmdSetEventMaskPage2(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status		status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	u8 *pu8Temp;
	u8 localBuf[6] = "";
	u8 *pRetPar;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;

	pu8Temp = (u8 *)&pHciCmd->Data[0];
	pBtHciInfo->BTEventMaskPage2 = *pu8Temp;
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("BTEventMaskPage2 = 0x%"i64fmt"x\n",
		pBtHciInfo->BTEventMaskPage2));

	/* send command complete event here when all data are received. */
	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_SET_EVENT_MASK_COMMAND,
		HCI_SET_EVENT_MASK_PAGE_2,
		status);

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;		/* status */
	len += 1;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);

	return status;
}

static enum hci_status
bthci_CmdReadLocationData(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status		status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	u8 localBuf[12] = "";
	u8 *pRetPar;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;
	u16 *pu2Temp;

	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_SET_EVENT_MASK_COMMAND,
		HCI_READ_LOCATION_DATA,
		status);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("DomainAware = 0x%x\n", pBtHciInfo->LocationDomainAware));
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Domain = 0x%x\n", pBtHciInfo->LocationDomain));
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("DomainOptions = 0x%x\n", pBtHciInfo->LocationDomainOptions));
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Options = 0x%x\n", pBtHciInfo->LocationOptions));

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;

	pRetPar[1] = pBtHciInfo->LocationDomainAware;	/* 0x0;	 Location_Domain_Aware */
	pu2Temp = (u16 *)&pRetPar[2];					/*  Location_Domain */
	*pu2Temp = pBtHciInfo->LocationDomain;		/* 0x5858; */
	pRetPar[4] = pBtHciInfo->LocationDomainOptions;	/* 0x58;	Location_Domain_Options */
	pRetPar[5] = pBtHciInfo->LocationOptions;		/* 0x0;	Location_Options */
	len += 6;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	return status;
}

static enum hci_status
bthci_CmdWriteLocationData(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	u16	*pu2Temp;
	u8 localBuf[6] = "";
	u8 *pRetPar;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;

	pBtHciInfo->LocationDomainAware = pHciCmd->Data[0];
	pu2Temp = (u16 *)&pHciCmd->Data[1];
	pBtHciInfo->LocationDomain = *pu2Temp;
	pBtHciInfo->LocationDomainOptions = pHciCmd->Data[3];
	pBtHciInfo->LocationOptions = pHciCmd->Data[4];
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("DomainAware = 0x%x\n", pBtHciInfo->LocationDomainAware));
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Domain = 0x%x\n", pBtHciInfo->LocationDomain));
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("DomainOptions = 0x%x\n", pBtHciInfo->LocationDomainOptions));
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Options = 0x%x\n", pBtHciInfo->LocationOptions));

	/* send command complete event here when all data are received. */
	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_SET_EVENT_MASK_COMMAND,
		HCI_WRITE_LOCATION_DATA,
		status);

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;		/* status */
	len += 1;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);

	return status;
}

static enum hci_status
bthci_CmdReadFlowControlMode(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	u8 localBuf[7] = "";
	u8 *pRetPar;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;

	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_SET_EVENT_MASK_COMMAND,
		HCI_READ_FLOW_CONTROL_MODE,
		status);

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;
	pRetPar[1] = pBtHciInfo->FlowControlMode;	/*  Flow Control Mode */
	len += 2;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	return status;
}

static enum hci_status
bthci_CmdWriteFlowControlMode(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	u8 localBuf[6] = "";
	u8 *pRetPar;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;

	pBtHciInfo->FlowControlMode = pHciCmd->Data[0];

	/* send command complete event here when all data are received. */
	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_SET_EVENT_MASK_COMMAND,
		HCI_WRITE_FLOW_CONTROL_MODE,
		status);

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;		/* status */
	len += 1;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);

	return status;
}

static enum hci_status
bthci_CmdReadBestEffortFlushTimeout(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTinfo = GET_BT_INFO(padapter);
	u16 i, j, logicHandle;
	u32 BestEffortFlushTimeout = 0xffffffff;
	u8 find = 0;

	logicHandle = *((u16 *)pHciCmd->Data);
	/*  find an matched logical link index and copy the data */
	for (j = 0; j < MAX_BT_ASOC_ENTRY_NUM; j++) {
		for (i = 0; i < MAX_LOGICAL_LINK_NUM; i++) {
			if (pBTinfo->BtAsocEntry[j].LogLinkCmdData[i].BtLogLinkhandle == logicHandle) {
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
		struct packet_irp_hcievent_data *PPacketIrpEvent;
		u32 *pu4Temp;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_READ_BEST_EFFORT_FLUSH_TIMEOUT,
			status);

		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;
		pu4Temp = (u32 *)&pRetPar[1];	/*  Best_Effort_Flush_Timeout */
		*pu4Temp = BestEffortFlushTimeout;
		len += 5;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}
	return status;
}

static enum hci_status
bthci_CmdWriteBestEffortFlushTimeout(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTinfo = GET_BT_INFO(padapter);
	u16 i, j, logicHandle;
	u32 BestEffortFlushTimeout = 0xffffffff;
	u8 find = 0;

	logicHandle = *((u16 *)pHciCmd->Data);
	BestEffortFlushTimeout = *((u32 *)(pHciCmd->Data+1));

	/*  find an matched logical link index and copy the data */
	for (j = 0; j < MAX_BT_ASOC_ENTRY_NUM; j++) {
		for (i = 0; i < MAX_LOGICAL_LINK_NUM; i++) {
			if (pBTinfo->BtAsocEntry[j].LogLinkCmdData[i].BtLogLinkhandle == logicHandle) {
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
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_SET_EVENT_MASK_COMMAND,
			HCI_WRITE_BEST_EFFORT_FLUSH_TIMEOUT,
			status);

		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;
		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}
	return status;
}

static enum hci_status
bthci_CmdShortRangeMode(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	u8 PhyLinkHandle, EntryNum, ShortRangeMode;

	PhyLinkHandle = pHciCmd->Data[0];
	ShortRangeMode = pHciCmd->Data[1];
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("PLH = 0x%x, Short_Range_Mode = 0x%x\n", PhyLinkHandle, ShortRangeMode));

	EntryNum = bthci_GetCurrentEntryNum(padapter, PhyLinkHandle);
	if (EntryNum != 0xff) {
		pBTInfo->BtAsocEntry[EntryNum].ShortRangeMode = ShortRangeMode;
	} else {
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

static enum hci_status bthci_CmdReadLocalSupportedCommands(struct rtw_adapter *padapter)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	u8 localBuf[TmpLocalBufSize] = "";
	u8 *pRetPar, *pSupportedCmds;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;

	/*  send command complete event here when all data are received. */
	PlatformZeroMemory(&localBuf[0], TmpLocalBufSize);
	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_INFORMATIONAL_PARAMETERS,
		HCI_READ_LOCAL_SUPPORTED_COMMANDS,
		status);

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;		/* status */
	len += 1;
	pSupportedCmds = &pRetPar[1];
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[5]= 0xc0\nBit [6]= Set Event Mask, [7]= Reset\n"));
	pSupportedCmds[5] = 0xc0;
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[6]= 0x01\nBit [0]= Set Event Filter\n"));
	pSupportedCmds[6] = 0x01;
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[7]= 0x0c\nBit [2]= Read Connection Accept Timeout, [3]= Write Connection Accept Timeout\n"));
	pSupportedCmds[7] = 0x0c;
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[10]= 0x80\nBit [7]= Host Number Of Completed Packets\n"));
	pSupportedCmds[10] = 0x80;
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[11]= 0x03\nBit [0]= Read Link Supervision Timeout, [1]= Write Link Supervision Timeout\n"));
	pSupportedCmds[11] = 0x03;
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[14]= 0xa8\nBit [3]= Read Local Version Information, [5]= Read Local Supported Features, [7]= Read Buffer Size\n"));
	pSupportedCmds[14] = 0xa8;
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[15]= 0x1c\nBit [2]= Read Failed Contact Count, [3]= Reset Failed Contact Count, [4]= Get Link Quality\n"));
	pSupportedCmds[15] = 0x1c;
	/* pSupportedCmds[16] = 0x04; */
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[19]= 0x40\nBit [6]= Enhanced Flush\n"));
	pSupportedCmds[19] = 0x40;
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[21]= 0xff\nBit [0]= Create Physical Link, [1]= Accept Physical Link, [2]= Disconnect Physical Link, [3]= Create Logical Link\n"));
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("	[4]= Accept Logical Link, [5]= Disconnect Logical Link, [6]= Logical Link Cancel, [7]= Flow Spec Modify\n"));
	pSupportedCmds[21] = 0xff;
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[22]= 0xff\nBit [0]= Read Logical Link Accept Timeout, [1]= Write Logical Link Accept Timeout, [2]= Set Event Mask Page 2, [3]= Read Location Data\n"));
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("	[4]= Write Location Data, [5]= Read Local AMP Info, [6]= Read Local AMP_ASSOC, [7]= Write Remote AMP_ASSOC\n"));
	pSupportedCmds[22] = 0xff;
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[23]= 0x07\nBit [0]= Read Flow Control Mode, [1]= Write Flow Control Mode, [2]= Read Data Block Size\n"));
	pSupportedCmds[23] = 0x07;
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD|IOCTL_BT_LOGO), ("Octet[24]= 0x1c\nBit [2]= Read Best Effort Flush Timeout, [3]= Write Best Effort Flush Timeout, [4]= Short Range Mode\n"));
	pSupportedCmds[24] = 0x1c;
	len += 64;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);

	return status;
}

static enum hci_status bthci_CmdReadLocalSupportedFeatures(struct rtw_adapter *padapter)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	u8 localBuf[TmpLocalBufSize] = "";
	u8 *pRetPar;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;

	/* send command complete event here when all data are received. */
	PlatformZeroMemory(&localBuf[0], TmpLocalBufSize);
	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_INFORMATIONAL_PARAMETERS,
		HCI_READ_LOCAL_SUPPORTED_FEATURES,
		status);

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;		/* status */
	len += 9;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	return status;
}

static enum hci_status bthci_CmdReadLocalAMPAssoc(struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	struct bt_dgb *pBtDbg = &pBTInfo->BtDbg;
	u8 PhyLinkHandle, EntryNum;

	pBtDbg->dbgHciInfo.hciCmdCntReadLocalAmpAssoc++;
	PhyLinkHandle = *((u8 *)pHciCmd->Data);
	EntryNum = bthci_GetCurrentEntryNum(padapter, PhyLinkHandle);

	if ((EntryNum == 0xff) && PhyLinkHandle != 0) {
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("ReadLocalAMPAssoc, EntryNum = %d  !!!!!, physical link handle = 0x%x\n",
		EntryNum, PhyLinkHandle));
		status = HCI_STATUS_UNKNOW_CONNECT_ID;
	} else if (pBtMgnt->bPhyLinkInProgressStartLL) {
		status = HCI_STATUS_UNKNOW_CONNECT_ID;
		pBtMgnt->bPhyLinkInProgressStartLL = false;
	} else {
		pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.BtPhyLinkhandle = *((u8 *)pHciCmd->Data);
		pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.LenSoFar = *((u16 *)((u8 *)pHciCmd->Data+1));
		pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.MaxRemoteASSOCLen = *((u16 *)((u8 *)pHciCmd->Data+3));
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD_DETAIL, ("ReadLocalAMPAssoc, LenSoFar =%d, MaxRemoteASSOCLen =%d\n",
			pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.LenSoFar,
			pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.MaxRemoteASSOCLen));
	}

	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("ReadLocalAMPAssoc, EntryNum = %d  !!!!!, physical link handle = 0x%x, LengthSoFar = %x  \n",
		EntryNum, PhyLinkHandle, pBTInfo->BtAsocEntry[EntryNum].AmpAsocCmdData.LenSoFar));

	/* send command complete event here when all data are received. */
	{
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		/* PVOID buffer = padapter->IrpHCILocalbuf.Ptr; */
		u8 localBuf[TmpLocalBufSize] = "";
		u16	*pRemainLen;
		u32	totalLen = 0;
		u16	typeLen = 0, remainLen = 0, ret_index = 0;
		u8 *pRetPar;

		PlatformZeroMemory(&localBuf[0], TmpLocalBufSize);
		/* PPacketIrpEvent = (struct packet_irp_hcievent_data *)(buffer); */
		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		totalLen += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_STATUS_PARAMETERS,
			HCI_READ_LOCAL_AMP_ASSOC,
			status);
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("ReadLocalAMPAssoc, Remaining_Len =%d  \n", remainLen));
		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[totalLen];
		pRetPar[0] = status;		/* status */
		pRetPar[1] = *((u8 *)pHciCmd->Data);
		pRemainLen = (u16 *)&pRetPar[2];	/* AMP_ASSOC_Remaining_Length */
		totalLen += 4;	/* 0]~[3] */
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
		PPacketIrpEvent->Length = (u8)totalLen;
		*pRemainLen = remainLen;	/*  AMP_ASSOC_Remaining_Length */
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("ReadLocalAMPAssoc, Remaining_Len =%d  \n", remainLen));
		RTPRINT_DATA(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("AMP_ASSOC_fragment : \n"), PPacketIrpEvent->Data, totalLen);

		bthci_IndicateEvent(padapter, PPacketIrpEvent, totalLen+2);
	}

	return status;
}

static enum hci_status bthci_CmdReadFailedContactCounter(struct rtw_adapter *padapter,
		       struct packet_irp_hcicmd_data *pHciCmd)
{

	enum hci_status		status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	u8 localBuf[TmpLocalBufSize] = "";
	u8 *pRetPar;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;
	u16 handle;

	handle = *((u16 *)pHciCmd->Data);
	/* send command complete event here when all data are received. */
	PlatformZeroMemory(&localBuf[0], TmpLocalBufSize);
	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_STATUS_PARAMETERS,
		HCI_READ_FAILED_CONTACT_COUNTER,
		status);

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;		/* status */
	pRetPar[1] = TWOBYTE_LOWBYTE(handle);
	pRetPar[2] = TWOBYTE_HIGHTBYTE(handle);
	pRetPar[3] = TWOBYTE_LOWBYTE(pBtHciInfo->FailContactCount);
	pRetPar[4] = TWOBYTE_HIGHTBYTE(pBtHciInfo->FailContactCount);
	len += 5;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);

	return status;
}

static enum hci_status
bthci_CmdResetFailedContactCounter(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status		status = HCI_STATUS_SUCCESS;
/*PMGNT_INFO		pMgntInfo = &padapter->MgntInfo; */
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	u16		handle;
	u8 localBuf[TmpLocalBufSize] = "";
	u8 *pRetPar;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;

	handle = *((u16 *)pHciCmd->Data);
	pBtHciInfo->FailContactCount = 0;

	/* send command complete event here when all data are received. */
	PlatformZeroMemory(&localBuf[0], TmpLocalBufSize);
	/* PPacketIrpEvent = (struct packet_irp_hcievent_data *)(buffer); */
	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_STATUS_PARAMETERS,
		HCI_RESET_FAILED_CONTACT_COUNTER,
		status);

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;		/* status */
	pRetPar[1] = TWOBYTE_LOWBYTE(handle);
	pRetPar[2] = TWOBYTE_HIGHTBYTE(handle);
	len += 3;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	return status;
}

/*  */
/*  BT 3.0+HS [Vol 2] 7.4.1 */
/*  */
static enum hci_status
bthci_CmdReadLocalVersionInformation(
	struct rtw_adapter *padapter
	)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	/* send command complete event here when all data are received. */
	u8 localBuf[TmpLocalBufSize] = "";
	u8 *pRetPar;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;
	u16 *pu2Temp;

	PlatformZeroMemory(&localBuf[0], TmpLocalBufSize);
	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_INFORMATIONAL_PARAMETERS,
		HCI_READ_LOCAL_VERSION_INFORMATION,
		status);

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;		/* status */
	pRetPar[1] = 0x05;			/*  HCI_Version */
	pu2Temp = (u16 *)&pRetPar[2];		/*  HCI_Revision */
	*pu2Temp = 0x0001;
	pRetPar[4] = 0x05;			/*  LMP/PAL_Version */
	pu2Temp = (u16 *)&pRetPar[5];		/*  Manufacturer_Name */
	*pu2Temp = 0x005d;
	pu2Temp = (u16 *)&pRetPar[7];		/*  LMP/PAL_Subversion */
	*pu2Temp = 0x0001;
	len += 9;
	PPacketIrpEvent->Length = len;

	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("LOCAL_VERSION_INFORMATION\n"));
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("Status  %x\n", status));
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("HCI_Version = 0x05\n"));
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("HCI_Revision = 0x0001\n"));
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("LMP/PAL_Version = 0x05\n"));
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("Manufacturer_Name = 0x0001\n"));
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("LMP/PAL_Subversion = 0x0001\n"));

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);

	return status;
}

/* 7.4.7 */
static enum hci_status bthci_CmdReadDataBlockSize(struct rtw_adapter *padapter)
{
	enum hci_status			status = HCI_STATUS_SUCCESS;
	u8 localBuf[TmpLocalBufSize] = "";
	u8 *pRetPar;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;
	u16 *pu2Temp;

	PlatformZeroMemory(&localBuf[0], TmpLocalBufSize);
	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_INFORMATIONAL_PARAMETERS,
		HCI_READ_DATA_BLOCK_SIZE,
		status);

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = HCI_STATUS_SUCCESS;	/* status */
	pu2Temp = (u16 *)&pRetPar[1];		/*  Max_ACL_Data_Packet_Length */
	*pu2Temp = Max80211PALPDUSize;

	pu2Temp = (u16 *)&pRetPar[3];		/*  Data_Block_Length */
	*pu2Temp = Max80211PALPDUSize;
	pu2Temp = (u16 *)&pRetPar[5];		/*  Total_Num_Data_Blocks */
	*pu2Temp = BTTotalDataBlockNum;
	len += 7;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);

	return status;
}

/*  7.4.5 */
static enum hci_status bthci_CmdReadBufferSize(struct rtw_adapter *padapter)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	u8 localBuf[TmpLocalBufSize] = "";
	u8 *pRetPar;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;
	u16 *pu2Temp;

	PlatformZeroMemory(&localBuf[0], TmpLocalBufSize);
	/* PPacketIrpEvent = (struct packet_irp_hcievent_data *)(buffer); */
	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_INFORMATIONAL_PARAMETERS,
		HCI_READ_BUFFER_SIZE,
		status);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Synchronous_Data_Packet_Length = 0x%x\n", BTSynDataPacketLength));
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Total_Num_ACL_Data_Packets = 0x%x\n", BTTotalDataBlockNum));
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("Total_Num_Synchronous_Data_Packets = 0x%x\n", BTTotalDataBlockNum));
	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;		/* status */
	pu2Temp = (u16 *)&pRetPar[1];		/*  HC_ACL_Data_Packet_Length */
	*pu2Temp = Max80211PALPDUSize;

	pRetPar[3] = BTSynDataPacketLength;	/*  HC_Synchronous_Data_Packet_Length */
	pu2Temp = (u16 *)&pRetPar[4];		/*  HC_Total_Num_ACL_Data_Packets */
	*pu2Temp = BTTotalDataBlockNum;
	pu2Temp = (u16 *)&pRetPar[6];		/*  HC_Total_Num_Synchronous_Data_Packets */
	*pu2Temp = BTTotalDataBlockNum;
	len += 8;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);

	return status;
}

static enum hci_status bthci_CmdReadLocalAMPInfo(struct rtw_adapter *padapter)
{
	enum hci_status	status = HCI_STATUS_SUCCESS;
	struct pwrctrl_priv *ppwrctrl = &padapter->pwrctrlpriv;
	u8 localBuf[TmpLocalBufSize] = "";
	u8 *pRetPar;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;
	u16 *pu2Temp;
	u32 *pu4Temp;
	u32	TotalBandwidth = BTTOTALBANDWIDTH, MaxBandGUBandwidth = BTMAXBANDGUBANDWIDTH;
	u8 ControlType = 0x01, AmpStatus = 0x01;
	u32	MaxFlushTimeout = 10000, BestEffortFlushTimeout = 5000;
	u16 MaxPDUSize = Max80211PALPDUSize, PalCap = 0x1, AmpAssocLen = Max80211AMPASSOCLen, MinLatency = 20;

	if ((ppwrctrl->rfoff_reason & RF_CHANGE_BY_HW) ||
	    (ppwrctrl->rfoff_reason & RF_CHANGE_BY_SW)) {
		AmpStatus = AMP_STATUS_NO_CAPACITY_FOR_BT;
	}

	PlatformZeroMemory(&localBuf[0], TmpLocalBufSize);
	/* PPacketIrpEvent = (struct packet_irp_hcievent_data *)(buffer); */
	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_STATUS_PARAMETERS,
		HCI_READ_LOCAL_AMP_INFO,
		status);

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;			/* status */
	pRetPar[1] = AmpStatus;			/*  AMP_Status */
	pu4Temp = (u32 *)&pRetPar[2];		/*  Total_Bandwidth */
	*pu4Temp = TotalBandwidth;		/* 0x19bfcc00;0x7530; */
	pu4Temp = (u32 *)&pRetPar[6];		/*  Max_Guaranteed_Bandwidth */
	*pu4Temp = MaxBandGUBandwidth;		/* 0x19bfcc00;0x4e20; */
	pu4Temp = (u32 *)&pRetPar[10];		/*  Min_Latency */
	*pu4Temp = MinLatency;			/* 150; */
	pu4Temp = (u32 *)&pRetPar[14];		/*  Max_PDU_Size */
	*pu4Temp = MaxPDUSize;
	pRetPar[18] = ControlType;		/*  Controller_Type */
	pu2Temp = (u16 *)&pRetPar[19];		/*  PAL_Capabilities */
	*pu2Temp = PalCap;
	pu2Temp = (u16 *)&pRetPar[21];		/*  AMP_ASSOC_Length */
	*pu2Temp = AmpAssocLen;
	pu4Temp = (u32 *)&pRetPar[23];		/*  Max_Flush_Timeout */
	*pu4Temp = MaxFlushTimeout;
	pu4Temp = (u32 *)&pRetPar[27];		/*  Best_Effort_Flush_Timeout */
	*pu4Temp = BestEffortFlushTimeout;
	len += 31;
	PPacketIrpEvent->Length = len;
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("AmpStatus = 0x%x\n",
		AmpStatus));
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("TotalBandwidth = 0x%x, MaxBandGUBandwidth = 0x%x, MinLatency = 0x%x, \n MaxPDUSize = 0x%x, ControlType = 0x%x\n",
		TotalBandwidth, MaxBandGUBandwidth, MinLatency, MaxPDUSize, ControlType));
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("PalCap = 0x%x, AmpAssocLen = 0x%x, MaxFlushTimeout = 0x%x, BestEffortFlushTimeout = 0x%x\n",
		PalCap, AmpAssocLen, MaxFlushTimeout, BestEffortFlushTimeout));
	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	return status;
}

static enum hci_status
bthci_CmdCreatePhysicalLink(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status	status;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_dgb *pBtDbg = &pBTInfo->BtDbg;

	pBtDbg->dbgHciInfo.hciCmdCntCreatePhyLink++;

	status = bthci_BuildPhysicalLink(padapter,
		pHciCmd, HCI_CREATE_PHYSICAL_LINK);

	return status;
}

static enum hci_status
bthci_CmdReadLinkQuality(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status			status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	u16				PLH;
	u8 	EntryNum, LinkQuality = 0x55;

	PLH = *((u16 *)&pHciCmd->Data[0]);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("PLH = 0x%x\n", PLH));

	EntryNum = bthci_GetCurrentEntryNum(padapter, (u8)PLH);
	if (EntryNum == 0xff) {
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("No such PLH(0x%x)\n", PLH));
		status = HCI_STATUS_UNKNOW_CONNECT_ID;
	}

	{
		u8 localBuf[11] = "";
		u8 *pRetPar;
		u8 len = 0;
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_STATUS_PARAMETERS,
			HCI_READ_LINK_QUALITY,
			status);

		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, (" PLH = 0x%x\n Link Quality = 0x%x\n", PLH, LinkQuality));

		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;			/* status */
		*((u16 *)&pRetPar[1]) = pBTInfo->BtAsocEntry[EntryNum].PhyLinkCmdData.BtPhyLinkhandle;	/*  Handle */
		pRetPar[3] = 0x55;	/* Link Quailty */
		len += 4;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

static enum hci_status
bthci_CmdCreateLogicalLink(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_dgb *pBtDbg = &pBTInfo->BtDbg;

	pBtDbg->dbgHciInfo.hciCmdCntCreateLogLink++;

	bthci_BuildLogicalLink(padapter, pHciCmd,
		HCI_CREATE_LOGICAL_LINK);

	return HCI_STATUS_SUCCESS;
}

static enum hci_status
bthci_CmdAcceptLogicalLink(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_dgb *pBtDbg = &pBTInfo->BtDbg;

	pBtDbg->dbgHciInfo.hciCmdCntAcceptLogLink++;

	bthci_BuildLogicalLink(padapter, pHciCmd,
		HCI_ACCEPT_LOGICAL_LINK);

	return HCI_STATUS_SUCCESS;
}

static enum hci_status
bthci_CmdDisconnectLogicalLink(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
/*PMGNT_INFO pMgntInfo = &padapter->MgntInfo; */
	struct bt_30info *pBTinfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTinfo->BtMgnt;
	struct bt_dgb *pBtDbg = &pBTinfo->BtDbg;
	u16	logicHandle;
	u8 i, j, find = 0, LogLinkCount = 0;

	pBtDbg->dbgHciInfo.hciCmdCntDisconnectLogLink++;

	logicHandle = *((u16 *)pHciCmd->Data);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("DisconnectLogicalLink, logicHandle = 0x%x\n", logicHandle));

	/*  find an created logical link index and clear the data */
	for (j = 0; j < MAX_BT_ASOC_ENTRY_NUM; j++) {
		for (i = 0; i < MAX_LOGICAL_LINK_NUM; i++) {
			if (pBTinfo->BtAsocEntry[j].LogLinkCmdData[i].BtLogLinkhandle == logicHandle) {
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

	/*  To check each */
	for (i = 0; i < MAX_LOGICAL_LINK_NUM; i++) {
		if (pBTinfo->BtAsocEntry[pBtMgnt->DisconnectEntryNum].LogLinkCmdData[i].BtLogLinkhandle != 0)
			LogLinkCount++;
	}

	/* When we receive Create logical link command, we should send command status event first. */
	bthci_EventCommandStatus(padapter,
			LINK_CONTROL_COMMANDS,
			HCI_DISCONNECT_LOGICAL_LINK,
			status);
	/*  */
	/* When we determines the logical link is established, we should send command complete event. */
	/*  */
	if (status == HCI_STATUS_SUCCESS) {
		bthci_EventDisconnectLogicalLinkComplete(padapter, status,
			logicHandle, HCI_STATUS_CONNECT_TERMINATE_LOCAL_HOST);
	}

	if (LogLinkCount == 0)
		mod_timer(&pBTinfo->BTDisconnectPhyLinkTimer,
			  jiffies + msecs_to_jiffies(100));

	return status;
}

static enum hci_status
bthci_CmdLogicalLinkCancel(struct rtw_adapter *padapter,
			   struct packet_irp_hcicmd_data *pHciCmd)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTinfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTinfo->BtMgnt;
	u8 CurrentEntryNum, CurrentLogEntryNum;

	u8 physicalLinkHandle, TxFlowSpecID, i;
	u16	CurrentLogicalHandle;

	physicalLinkHandle = *((u8 *)pHciCmd->Data);
	TxFlowSpecID = *(((u8 *)pHciCmd->Data)+1);

	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("LogicalLinkCancel, physicalLinkHandle = 0x%x, TxFlowSpecID = 0x%x\n",
		physicalLinkHandle, TxFlowSpecID));

	CurrentEntryNum = pBtMgnt->CurrentConnectEntryNum;
	CurrentLogicalHandle = pBtMgnt->BtCurrentLogLinkhandle;

	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("CurrentEntryNum = 0x%x, CurrentLogicalHandle = 0x%x\n",
		CurrentEntryNum, CurrentLogicalHandle));

	CurrentLogEntryNum = 0xff;
	for (i = 0; i < MAX_LOGICAL_LINK_NUM; i++) {
		if ((CurrentLogicalHandle == pBTinfo->BtAsocEntry[CurrentEntryNum].LogLinkCmdData[i].BtLogLinkhandle) &&
			(physicalLinkHandle == pBTinfo->BtAsocEntry[CurrentEntryNum].LogLinkCmdData[i].BtPhyLinkhandle)) {
			CurrentLogEntryNum = i;
			break;
		}
	}

	if (CurrentLogEntryNum == 0xff) {
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("LogicalLinkCancel, CurrentLogEntryNum == 0xff !!!!\n"));
		status = HCI_STATUS_UNKNOW_CONNECT_ID;
		return status;
	} else {
		if (pBTinfo->BtAsocEntry[CurrentEntryNum].LogLinkCmdData[CurrentLogEntryNum].bLLCompleteEventIsSet) {
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("LogicalLinkCancel, LLCompleteEventIsSet!!!!\n"));
			status = HCI_STATUS_ACL_CONNECT_EXISTS;
		}
	}

	{
		u8 localBuf[8] = "";
		u8 *pRetPar;
		u8 len = 0;
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			LINK_CONTROL_COMMANDS,
			HCI_LOGICAL_LINK_CANCEL,
			status);

		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		/* status */
		pRetPar[1] = pBTinfo->BtAsocEntry[CurrentEntryNum].LogLinkCmdData[CurrentLogEntryNum].BtPhyLinkhandle;
		pRetPar[2] = pBTinfo->BtAsocEntry[CurrentEntryNum].LogLinkCmdData[CurrentLogEntryNum].BtTxFlowSpecID;
		len += 3;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	pBTinfo->BtAsocEntry[CurrentEntryNum].LogLinkCmdData[CurrentLogEntryNum].bLLCancelCMDIsSetandComplete = true;

	return status;
}

static enum hci_status
bthci_CmdFlowSpecModify(struct rtw_adapter *padapter,
			struct packet_irp_hcicmd_data *pHciCmd)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
/*PMGNT_INFO pMgntInfo = &padapter->MgntInfo; */
	struct bt_30info *pBTinfo = GET_BT_INFO(padapter);
	u8 i, j, find = 0;
	u16 logicHandle;

	logicHandle = *((u16 *)pHciCmd->Data);
	/*  find an matched logical link index and copy the data */
	for (j = 0; j < MAX_BT_ASOC_ENTRY_NUM; j++) {
		for (i = 0; i < MAX_LOGICAL_LINK_NUM; i++) {
			if (pBTinfo->BtAsocEntry[j].LogLinkCmdData[i].BtLogLinkhandle == logicHandle) {
				memcpy(&pBTinfo->BtAsocEntry[j].LogLinkCmdData[i].Tx_Flow_Spec,
					&pHciCmd->Data[2], sizeof(struct hci_flow_spec));
				memcpy(&pBTinfo->BtAsocEntry[j].LogLinkCmdData[i].Rx_Flow_Spec,
					&pHciCmd->Data[18], sizeof(struct hci_flow_spec));

				bthci_CheckLogLinkBehavior(padapter, pBTinfo->BtAsocEntry[j].LogLinkCmdData[i].Tx_Flow_Spec);
				find = 1;
				break;
			}
		}
	}
	RTPRINT(FIOCTL, IOCTL_BT_LOGO, ("FlowSpecModify, LLH = 0x%x, \n", logicHandle));

	/* When we receive Flow Spec Modify command, we should send command status event first. */
	bthci_EventCommandStatus(padapter,
		LINK_CONTROL_COMMANDS,
		HCI_FLOW_SPEC_MODIFY,
		HCI_STATUS_SUCCESS);

	if (!find)
		status = HCI_STATUS_UNKNOW_CONNECT_ID;

	bthci_EventSendFlowSpecModifyComplete(padapter, status, logicHandle);

	return status;
}

static enum hci_status
bthci_CmdAcceptPhysicalLink(struct rtw_adapter *padapter,
			    struct packet_irp_hcicmd_data *pHciCmd)
{
	enum hci_status	status;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_dgb *pBtDbg = &pBTInfo->BtDbg;

	pBtDbg->dbgHciInfo.hciCmdCntAcceptPhyLink++;

	status = bthci_BuildPhysicalLink(padapter,
		pHciCmd, HCI_ACCEPT_PHYSICAL_LINK);

	return status;
}

static enum hci_status
bthci_CmdDisconnectPhysicalLink(struct rtw_adapter *padapter,
				struct packet_irp_hcicmd_data *pHciCmd)
{
	enum hci_status	status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_dgb *pBtDbg = &pBTInfo->BtDbg;
	u8 PLH, CurrentEntryNum, PhysLinkDisconnectReason;

	pBtDbg->dbgHciInfo.hciCmdCntDisconnectPhyLink++;

	PLH = *((u8 *)pHciCmd->Data);
	PhysLinkDisconnectReason = *((u8 *)pHciCmd->Data+1);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_DISCONNECT_PHYSICAL_LINK  PhyHandle = 0x%x, Reason = 0x%x\n",
		PLH, PhysLinkDisconnectReason));

	CurrentEntryNum = bthci_GetCurrentEntryNum(padapter, PLH);

	if (CurrentEntryNum == 0xff) {
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD,
			("DisconnectPhysicalLink, No such Handle in the Entry\n"));
		status = HCI_STATUS_UNKNOW_CONNECT_ID;
	} else {
		pBTInfo->BtAsocEntry[CurrentEntryNum].PhyLinkDisconnectReason =
			(enum hci_status)PhysLinkDisconnectReason;
	}
	/* Send HCI Command status event to AMP. */
	bthci_EventCommandStatus(padapter, LINK_CONTROL_COMMANDS,
				 HCI_DISCONNECT_PHYSICAL_LINK, status);

	if (status != HCI_STATUS_SUCCESS)
		return status;

	/* The macros below require { and } in the if statement */
	if (pBTInfo->BtAsocEntry[CurrentEntryNum].BtCurrentState == HCI_STATE_DISCONNECTED) {
		BTHCI_SM_WITH_INFO(padapter, HCI_STATE_DISCONNECTED, STATE_CMD_DISCONNECT_PHY_LINK, CurrentEntryNum);
	} else {
		BTHCI_SM_WITH_INFO(padapter, HCI_STATE_DISCONNECTING, STATE_CMD_DISCONNECT_PHY_LINK, CurrentEntryNum);
	}
	return status;
}

static enum hci_status
bthci_CmdSetACLLinkDataFlowMode(struct rtw_adapter *padapter,
				struct packet_irp_hcicmd_data *pHciCmd)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	u8 localBuf[8] = "";
	u8 *pRetPar;
	u8 len = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;
	u16 *pu2Temp;

	pBtMgnt->ExtConfig.CurrentConnectHandle = *((u16 *)pHciCmd->Data);
	pBtMgnt->ExtConfig.CurrentIncomingTrafficMode = *((u8 *)pHciCmd->Data)+2;
	pBtMgnt->ExtConfig.CurrentOutgoingTrafficMode = *((u8 *)pHciCmd->Data)+3;
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("Connection Handle = 0x%x, Incoming Traffic mode = 0x%x, Outgoing Traffic mode = 0x%x",
		pBtMgnt->ExtConfig.CurrentConnectHandle,
		pBtMgnt->ExtConfig.CurrentIncomingTrafficMode,
		pBtMgnt->ExtConfig.CurrentOutgoingTrafficMode));


	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_EXTENSION,
		HCI_SET_ACL_LINK_DATA_FLOW_MODE,
		status);

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;		/* status */

	pu2Temp = (u16 *)&pRetPar[1];
	*pu2Temp = pBtMgnt->ExtConfig.CurrentConnectHandle;
	len += 3;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	return status;
}

static enum hci_status
bthci_CmdSetACLLinkStatus(struct rtw_adapter *padapter,
			  struct packet_irp_hcicmd_data *pHciCmd)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	struct bt_dgb *pBtDbg = &pBTInfo->BtDbg;
	u8 i;
	u8 *pTriple;

	pBtDbg->dbgHciInfo.hciCmdCntSetAclLinkStatus++;
	RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD_EXT, "SetACLLinkStatus, Hex Data :\n",
			&pHciCmd->Data[0], pHciCmd->Length);

	/*  Only Core Stack v251 and later version support this command. */
	pBtMgnt->bSupportProfile = true;

	pBtMgnt->ExtConfig.NumberOfHandle = *((u8 *)pHciCmd->Data);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("NumberOfHandle = 0x%x\n", pBtMgnt->ExtConfig.NumberOfHandle));

	pTriple = &pHciCmd->Data[1];
	for (i = 0; i < pBtMgnt->ExtConfig.NumberOfHandle; i++) {
		pBtMgnt->ExtConfig.linkInfo[i].ConnectHandle = *((u16 *)&pTriple[0]);
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
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_SET_ACL_LINK_STATUS,
			status);

		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		/* status */

		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

static enum hci_status
bthci_CmdSetSCOLinkStatus(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	struct bt_dgb *pBtDbg = &pBTInfo->BtDbg;

	pBtDbg->dbgHciInfo.hciCmdCntSetScoLinkStatus++;
	pBtMgnt->ExtConfig.NumberOfSCO = *((u8 *)pHciCmd->Data);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("NumberOfSCO = 0x%x\n",
		pBtMgnt->ExtConfig.NumberOfSCO));

	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_SET_SCO_LINK_STATUS,
			status);

		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		/* status */

		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

static enum hci_status
bthci_CmdSetRSSIValue(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	s8		min_bt_rssi = 0;
	u8 i;
	for (i = 0; i < pBtMgnt->ExtConfig.NumberOfHandle; i++) {
		if (pBtMgnt->ExtConfig.linkInfo[i].ConnectHandle == *((u16 *)&pHciCmd->Data[0])) {
			pBtMgnt->ExtConfig.linkInfo[i].BT_RSSI = (s8)(pHciCmd->Data[2]);
			RTPRINT(FIOCTL, IOCTL_BT_EVENT_PERIODICAL,
			("Connection_Handle = 0x%x, RSSI = %d \n",
			pBtMgnt->ExtConfig.linkInfo[i].ConnectHandle,
			pBtMgnt->ExtConfig.linkInfo[i].BT_RSSI));
		}
		/*  get the minimum bt rssi value */
		if (pBtMgnt->ExtConfig.linkInfo[i].BT_RSSI <= min_bt_rssi)
			min_bt_rssi = pBtMgnt->ExtConfig.linkInfo[i].BT_RSSI;
	}

	pBtMgnt->ExtConfig.MIN_BT_RSSI = min_bt_rssi;
	RTPRINT(FBT, BT_TRACE, ("[bt rssi], the min rssi is %d\n", min_bt_rssi));

	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_SET_RSSI_VALUE,
			status);

		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		/* status */

		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

static enum hci_status
bthci_CmdSetCurrentBluetoothStatus(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status	status = HCI_STATUS_SUCCESS;
/*PMGNT_INFO	pMgntInfo = &padapter->MgntInfo; */
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	pBtMgnt->ExtConfig.CurrentBTStatus = *((u8 *)&pHciCmd->Data[0]);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("SetCurrentBluetoothStatus, CurrentBTStatus = 0x%x\n",
		pBtMgnt->ExtConfig.CurrentBTStatus));

	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_SET_CURRENT_BLUETOOTH_STATUS,
			status);

		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		/* status */
		len += 1;

		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

static enum hci_status
bthci_CmdExtensionVersionNotify(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status	status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	struct bt_dgb *pBtDbg = &pBTInfo->BtDbg;

	pBtDbg->dbgHciInfo.hciCmdCntExtensionVersionNotify++;
	RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD_EXT, "ExtensionVersionNotify, Hex Data :\n",
			&pHciCmd->Data[0], pHciCmd->Length);

	pBtMgnt->ExtConfig.HCIExtensionVer = *((u16 *)&pHciCmd->Data[0]);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCIExtensionVer = 0x%x\n", pBtMgnt->ExtConfig.HCIExtensionVer));

	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_EXTENSION_VERSION_NOTIFY,
			status);

		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		/* status */

		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

static enum hci_status
bthci_CmdLinkStatusNotify(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status	status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	struct bt_dgb *pBtDbg = &pBTInfo->BtDbg;
	u8 i;
	u8 *pTriple;

	pBtDbg->dbgHciInfo.hciCmdCntLinkStatusNotify++;
	RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD_EXT, "LinkStatusNotify, Hex Data :\n",
			&pHciCmd->Data[0], pHciCmd->Length);

	/*  Current only RTL8723 support this command. */
	pBtMgnt->bSupportProfile = true;

	pBtMgnt->ExtConfig.NumberOfHandle = *((u8 *)pHciCmd->Data);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("NumberOfHandle = 0x%x\n", pBtMgnt->ExtConfig.NumberOfHandle));
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCIExtensionVer = %d\n", pBtMgnt->ExtConfig.HCIExtensionVer));

	pTriple = &pHciCmd->Data[1];
	for (i = 0; i < pBtMgnt->ExtConfig.NumberOfHandle; i++) {
		if (pBtMgnt->ExtConfig.HCIExtensionVer < 1) {
			pBtMgnt->ExtConfig.linkInfo[i].ConnectHandle = *((u16 *)&pTriple[0]);
			pBtMgnt->ExtConfig.linkInfo[i].BTProfile = pTriple[2];
			pBtMgnt->ExtConfig.linkInfo[i].BTCoreSpec = pTriple[3];
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT,
				("Connection_Handle = 0x%x, BTProfile =%d, BTSpec =%d\n",
				pBtMgnt->ExtConfig.linkInfo[i].ConnectHandle,
				pBtMgnt->ExtConfig.linkInfo[i].BTProfile,
				pBtMgnt->ExtConfig.linkInfo[i].BTCoreSpec));
			pTriple += 4;
		} else if (pBtMgnt->ExtConfig.HCIExtensionVer >= 1) {
			pBtMgnt->ExtConfig.linkInfo[i].ConnectHandle = *((u16 *)&pTriple[0]);
			pBtMgnt->ExtConfig.linkInfo[i].BTProfile = pTriple[2];
			pBtMgnt->ExtConfig.linkInfo[i].BTCoreSpec = pTriple[3];
			pBtMgnt->ExtConfig.linkInfo[i].linkRole = pTriple[4];
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT,
				("Connection_Handle = 0x%x, BTProfile =%d, BTSpec =%d, LinkRole =%d\n",
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
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_LINK_STATUS_NOTIFY,
			status);

		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		/* status */

		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

static enum hci_status
bthci_CmdBtOperationNotify(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status	status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD_EXT, "Bt Operation notify, Hex Data :\n",
			&pHciCmd->Data[0], pHciCmd->Length);

	pBtMgnt->ExtConfig.btOperationCode = *((u8 *)pHciCmd->Data);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("btOperationCode = 0x%x\n", pBtMgnt->ExtConfig.btOperationCode));
	switch (pBtMgnt->ExtConfig.btOperationCode) {
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
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_BT_OPERATION_NOTIFY,
			status);

		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		/* status */

		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

static enum hci_status
bthci_CmdEnableWifiScanNotify(struct rtw_adapter *padapter,
			      struct packet_irp_hcicmd_data *pHciCmd)
{
	enum hci_status	status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	RTPRINT_DATA(FIOCTL, IOCTL_BT_HCICMD_EXT, "Enable Wifi scan notify, Hex Data :\n",
			&pHciCmd->Data[0], pHciCmd->Length);

	pBtMgnt->ExtConfig.bEnableWifiScanNotify = *((u8 *)pHciCmd->Data);
	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("bEnableWifiScanNotify = %d\n", pBtMgnt->ExtConfig.bEnableWifiScanNotify));

	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_ENABLE_WIFI_SCAN_NOTIFY,
			status);

		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		/* status */

		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

static enum hci_status
bthci_CmdWIFICurrentChannel(struct rtw_adapter *padapter,
			    struct packet_irp_hcicmd_data *pHciCmd)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	u8 chnl = pmlmeext->cur_channel;

	if (pmlmeext->cur_bwmode == HT_CHANNEL_WIDTH_40) {
		if (pmlmeext->cur_ch_offset == HAL_PRIME_CHNL_OFFSET_UPPER)
			chnl += 2;
		else if (pmlmeext->cur_ch_offset == HAL_PRIME_CHNL_OFFSET_LOWER)
			chnl -= 2;
	}

	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("Current Channel  = 0x%x\n", chnl));

	{
		u8 localBuf[8] = "";
		u8 *pRetPar;
		u8 len = 0;
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_WIFI_CURRENT_CHANNEL,
			status);

		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		/* status */
		pRetPar[1] = chnl;			/* current channel */
		len += 2;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

static enum hci_status
bthci_CmdWIFICurrentBandwidth(struct rtw_adapter *padapter,
			      struct packet_irp_hcicmd_data *pHciCmd)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	enum ht_channel_width bw;
	u8 CurrentBW = 0;

	bw = padapter->mlmeextpriv.cur_bwmode;

	if (bw == HT_CHANNEL_WIDTH_20)
		CurrentBW = 0;
	else if (bw == HT_CHANNEL_WIDTH_40)
		CurrentBW = 1;

	RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("Current BW = 0x%x\n",
		CurrentBW));

	{
		u8 localBuf[8] = "";
		u8 *pRetPar;
		u8 len = 0;
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_WIFI_CURRENT_BANDWIDTH,
			status);

		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		/* status */
		pRetPar[1] = CurrentBW;		/* current BW */
		len += 2;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

static enum hci_status
bthci_CmdWIFIConnectionStatus(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	u8 connectStatus = HCI_WIFI_NOT_CONNECTED;

	if (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE)) {
		if (padapter->stapriv.asoc_sta_count >= 3)
			connectStatus = HCI_WIFI_CONNECTED;
		else
			connectStatus = HCI_WIFI_NOT_CONNECTED;
	} else if (check_fwstate(&padapter->mlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE|WIFI_ASOC_STATE)) {
		connectStatus = HCI_WIFI_CONNECTED;
	} else if (check_fwstate(&padapter->mlmepriv, WIFI_UNDER_LINKING)) {
		connectStatus = HCI_WIFI_CONNECT_IN_PROGRESS;
	} else {
		connectStatus = HCI_WIFI_NOT_CONNECTED;
	}

	{
		u8 localBuf[8] = "";
		u8 *pRetPar;
		u8 len = 0;
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_EXTENSION,
			HCI_WIFI_CONNECTION_STATUS,
			status);

		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;			/* status */
		pRetPar[1] = connectStatus;	/* connect status */
		len += 2;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

static enum hci_status
bthci_CmdEnableDeviceUnderTestMode(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;

	pBtHciInfo->bInTestMode = true;
	pBtHciInfo->bTestIsEnd = false;

	/* send command complete event here when all data are received. */
	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_TESTING_COMMANDS,
			HCI_ENABLE_DEVICE_UNDER_TEST_MODE,
			status);

		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		/* status */
		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

static enum hci_status
bthci_CmdAMPTestEnd(struct rtw_adapter *padapter,
		    struct packet_irp_hcicmd_data *pHciCmd)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;

	if (!pBtHciInfo->bInTestMode) {
		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Not in Test mode, return status = HCI_STATUS_CMD_DISALLOW\n"));
		status = HCI_STATUS_CMD_DISALLOW;
		return status;
	}

	pBtHciInfo->bTestIsEnd = true;

	del_timer_sync(&pBTInfo->BTTestSendPacketTimer);

	rtl8723a_check_bssid(padapter, true);

	/* send command complete event here when all data are received. */
	{
		u8 localBuf[4] = "";
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("AMP Test End Event \n"));
		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);
		PPacketIrpEvent->EventCode = HCI_EVENT_AMP_TEST_END;
		PPacketIrpEvent->Length = 2;

		PPacketIrpEvent->Data[0] = status;
		PPacketIrpEvent->Data[1] = pBtHciInfo->TestScenario;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, 4);
	}

	bthci_EventAMPReceiverReport(padapter, 0x01);

	return status;
}

static enum hci_status
bthci_CmdAMPTestCommand(struct rtw_adapter *padapter,
			struct packet_irp_hcicmd_data *pHciCmd)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;

	if (!pBtHciInfo->bInTestMode) {
		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Not in Test mode, return status = HCI_STATUS_CMD_DISALLOW\n"));
		status = HCI_STATUS_CMD_DISALLOW;
		return status;
	}

	pBtHciInfo->TestScenario = *((u8 *)pHciCmd->Data);

	if (pBtHciInfo->TestScenario == 0x01)
		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("TX Single Test \n"));
	else if (pBtHciInfo->TestScenario == 0x02)
		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Receive Frame Test \n"));
	else
		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("No Such Test !!!!!!!!!!!!!!!!!! \n"));

	if (pBtHciInfo->bTestIsEnd) {
		u8 localBuf[5] = "";
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("AMP Test End Event \n"));
		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);
		PPacketIrpEvent->EventCode = HCI_EVENT_AMP_TEST_END;
		PPacketIrpEvent->Length = 2;

		PPacketIrpEvent->Data[0] = status;
		PPacketIrpEvent->Data[1] = pBtHciInfo->TestScenario ;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, 4);

		/* Return to Idel state with RX and TX off. */

		return status;
	}

	/*  should send command status event */
	bthci_EventCommandStatus(padapter,
			OGF_TESTING_COMMANDS,
			HCI_AMP_TEST_COMMAND,
			status);

	/* The HCI_AMP_Start Test Event shall be generated when the */
	/* HCI_AMP_Test_Command has completed and the first data is ready to be sent */
	/* or received. */

	{
		u8 localBuf[5] = "";
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), (" HCI_AMP_Start Test Event \n"));
		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);
		PPacketIrpEvent->EventCode = HCI_EVENT_AMP_START_TEST;
		PPacketIrpEvent->Length = 2;

		PPacketIrpEvent->Data[0] = status;
		PPacketIrpEvent->Data[1] = pBtHciInfo->TestScenario ;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, 4);

		/* Return to Idel state with RX and TX off. */
	}

	if (pBtHciInfo->TestScenario == 0x01) {
		/*
			When in a transmitter test scenario and the frames/bursts count have been
			transmitted the HCI_AMP_Test_End event shall be sent.
		*/
		mod_timer(&pBTInfo->BTTestSendPacketTimer,
			  jiffies + msecs_to_jiffies(50));
		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("TX Single Test \n"));
	} else if (pBtHciInfo->TestScenario == 0x02) {
		rtl8723a_check_bssid(padapter, false);
		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_BT_LOGO), ("Receive Frame Test \n"));
	}

	return status;
}

static enum hci_status
bthci_CmdEnableAMPReceiverReports(struct rtw_adapter *padapter,
				  struct packet_irp_hcicmd_data *pHciCmd)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;

	if (!pBtHciInfo->bInTestMode) {
		status = HCI_STATUS_CMD_DISALLOW;
		/* send command complete event here when all data are received. */
		{
			u8 localBuf[6] = "";
			u8 *pRetPar;
			u8 len = 0;
			struct packet_irp_hcievent_data *PPacketIrpEvent;

			PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

			len += bthci_CommandCompleteHeader(&localBuf[0],
				OGF_TESTING_COMMANDS,
				HCI_ENABLE_AMP_RECEIVER_REPORTS,
				status);

			/*  Return parameters starts from here */
			pRetPar = &PPacketIrpEvent->Data[len];
			pRetPar[0] = status;		/* status */
			len += 1;
			PPacketIrpEvent->Length = len;

			bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
		}
		return status;
	}

	pBtHciInfo->bTestNeedReport = *((u8 *)pHciCmd->Data);
	pBtHciInfo->TestReportInterval = (*((u8 *)pHciCmd->Data+2));

	bthci_EventAMPReceiverReport(padapter, 0x00);

	/* send command complete event here when all data are received. */
	{
		u8 localBuf[6] = "";
		u8 *pRetPar;
		u8 len = 0;
		struct packet_irp_hcievent_data *PPacketIrpEvent;

		PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

		len += bthci_CommandCompleteHeader(&localBuf[0],
			OGF_TESTING_COMMANDS,
			HCI_ENABLE_AMP_RECEIVER_REPORTS,
			status);

		/*  Return parameters starts from here */
		pRetPar = &PPacketIrpEvent->Data[len];
		pRetPar[0] = status;		/* status */
		len += 1;
		PPacketIrpEvent->Length = len;

		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
	}

	return status;
}

static enum hci_status
bthci_CmdHostBufferSize(struct rtw_adapter *padapter,
			struct packet_irp_hcicmd_data *pHciCmd)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	struct packet_irp_hcievent_data *PPacketIrpEvent;
	enum hci_status status = HCI_STATUS_SUCCESS;
	u8 localBuf[6] = "";
	u8 *pRetPar;
	u8 len = 0;

	pBTInfo->BtAsocEntry[pBtMgnt->CurrentConnectEntryNum].ACLPacketsData.ACLDataPacketLen = *((u16 *)pHciCmd->Data);
	pBTInfo->BtAsocEntry[pBtMgnt->CurrentConnectEntryNum].SyncDataPacketLen = *((u8 *)(pHciCmd->Data+2));
	pBTInfo->BtAsocEntry[pBtMgnt->CurrentConnectEntryNum].TotalNumACLDataPackets = *((u16 *)(pHciCmd->Data+3));
	pBTInfo->BtAsocEntry[pBtMgnt->CurrentConnectEntryNum].TotalSyncNumDataPackets = *((u16 *)(pHciCmd->Data+5));

	/* send command complete event here when all data are received. */
	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	len += bthci_CommandCompleteHeader(&localBuf[0],
		OGF_SET_EVENT_MASK_COMMAND,
		HCI_HOST_BUFFER_SIZE,
		status);

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[len];
	pRetPar[0] = status;		/* status */
	len += 1;
	PPacketIrpEvent->Length = len;

	bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);

	return status;
}

static enum hci_status
bthci_UnknownCMD(struct rtw_adapter *padapter, struct packet_irp_hcicmd_data *pHciCmd)
{
	enum hci_status status = HCI_STATUS_UNKNOW_HCI_CMD;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_dgb *pBtDbg = &pBTInfo->BtDbg;

	pBtDbg->dbgHciInfo.hciCmdCntUnknown++;
	bthci_EventCommandStatus(padapter,
			(u8)pHciCmd->OGF,
			pHciCmd->OCF,
			status);

	return status;
}

static enum hci_status
bthci_HandleOGFInformationalParameters(struct rtw_adapter *padapter,
				       struct packet_irp_hcicmd_data *pHciCmd)
{
	enum hci_status status = HCI_STATUS_SUCCESS;

	switch (pHciCmd->OCF) {
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

static enum hci_status
bthci_HandleOGFSetEventMaskCMD(struct rtw_adapter *padapter,
			       struct packet_irp_hcicmd_data *pHciCmd)
{
	enum hci_status status = HCI_STATUS_SUCCESS;

	switch (pHciCmd->OCF) {
	case HCI_SET_EVENT_MASK:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_SET_EVENT_MASK\n"));
		status = bthci_CmdSetEventMask(padapter, pHciCmd);
		break;
	case HCI_RESET:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_RESET\n"));
		status = bthci_CmdReset(padapter, true);
		break;
	case HCI_READ_CONNECTION_ACCEPT_TIMEOUT:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_CONNECTION_ACCEPT_TIMEOUT\n"));
		status = bthci_CmdReadConnectionAcceptTimeout(padapter);
		break;
	case HCI_SET_EVENT_FILTER:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_SET_EVENT_FILTER\n"));
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
		status = bthci_CmdHostBufferSize(padapter, pHciCmd);
		break;
	default:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("bthci_HandleOGFSetEventMaskCMD(), Unknown case = 0x%x\n", pHciCmd->OCF));
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_UNKNOWN_COMMAND\n"));
		status = bthci_UnknownCMD(padapter, pHciCmd);
		break;
	}
	return status;
}

static enum hci_status
bthci_HandleOGFStatusParameters(struct rtw_adapter *padapter,
				struct packet_irp_hcicmd_data *pHciCmd)
{
	enum hci_status status = HCI_STATUS_SUCCESS;

	switch (pHciCmd->OCF) {
	case HCI_READ_FAILED_CONTACT_COUNTER:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_FAILED_CONTACT_COUNTER\n"));
		status = bthci_CmdReadFailedContactCounter(padapter, pHciCmd);
		break;
	case HCI_RESET_FAILED_CONTACT_COUNTER:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_RESET_FAILED_CONTACT_COUNTER\n"));
		status = bthci_CmdResetFailedContactCounter(padapter, pHciCmd);
		break;
	case HCI_READ_LINK_QUALITY:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_LINK_QUALITY\n"));
		status = bthci_CmdReadLinkQuality(padapter, pHciCmd);
		break;
	case HCI_READ_RSSI:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_RSSI\n"));
		break;
	case HCI_READ_LOCAL_AMP_INFO:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_LOCAL_AMP_INFO\n"));
		status = bthci_CmdReadLocalAMPInfo(padapter);
		break;
	case HCI_READ_LOCAL_AMP_ASSOC:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_READ_LOCAL_AMP_ASSOC\n"));
		status = bthci_CmdReadLocalAMPAssoc(padapter, pHciCmd);
		break;
	case HCI_WRITE_REMOTE_AMP_ASSOC:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_WRITE_REMOTE_AMP_ASSOC\n"));
		status = bthci_CmdWriteRemoteAMPAssoc(padapter, pHciCmd);
		break;
	default:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("bthci_HandleOGFStatusParameters(), Unknown case = 0x%x\n", pHciCmd->OCF));
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_UNKNOWN_COMMAND\n"));
		status = bthci_UnknownCMD(padapter, pHciCmd);
		break;
	}
	return status;
}

static enum hci_status
bthci_HandleOGFLinkControlCMD(struct rtw_adapter *padapter,
			      struct packet_irp_hcicmd_data *pHciCmd)
{
	enum hci_status status = HCI_STATUS_SUCCESS;

	switch (pHciCmd->OCF) {
	case HCI_CREATE_PHYSICAL_LINK:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_CREATE_PHYSICAL_LINK\n"));
		status = bthci_CmdCreatePhysicalLink(padapter, pHciCmd);
		break;
	case HCI_ACCEPT_PHYSICAL_LINK:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_ACCEPT_PHYSICAL_LINK\n"));
		status = bthci_CmdAcceptPhysicalLink(padapter, pHciCmd);
		break;
	case HCI_DISCONNECT_PHYSICAL_LINK:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_DISCONNECT_PHYSICAL_LINK\n"));
		status = bthci_CmdDisconnectPhysicalLink(padapter, pHciCmd);
		break;
	case HCI_CREATE_LOGICAL_LINK:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_CREATE_LOGICAL_LINK\n"));
		status = bthci_CmdCreateLogicalLink(padapter, pHciCmd);
		break;
	case HCI_ACCEPT_LOGICAL_LINK:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_ACCEPT_LOGICAL_LINK\n"));
		status = bthci_CmdAcceptLogicalLink(padapter, pHciCmd);
		break;
	case HCI_DISCONNECT_LOGICAL_LINK:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_DISCONNECT_LOGICAL_LINK\n"));
		status = bthci_CmdDisconnectLogicalLink(padapter, pHciCmd);
		break;
	case HCI_LOGICAL_LINK_CANCEL:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_LOGICAL_LINK_CANCEL\n"));
		status = bthci_CmdLogicalLinkCancel(padapter, pHciCmd);
		break;
	case HCI_FLOW_SPEC_MODIFY:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_FLOW_SPEC_MODIFY\n"));
		status = bthci_CmdFlowSpecModify(padapter, pHciCmd);
		break;
	default:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("bthci_HandleOGFLinkControlCMD(), Unknown case = 0x%x\n", pHciCmd->OCF));
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_UNKNOWN_COMMAND\n"));
		status = bthci_UnknownCMD(padapter, pHciCmd);
		break;
	}
	return status;
}

static enum hci_status
bthci_HandleOGFTestingCMD(struct rtw_adapter *padapter,
			  struct packet_irp_hcicmd_data *pHciCmd)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	switch (pHciCmd->OCF) {
	case HCI_ENABLE_DEVICE_UNDER_TEST_MODE:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_ENABLE_DEVICE_UNDER_TEST_MODE\n"));
		bthci_CmdEnableDeviceUnderTestMode(padapter, pHciCmd);
		break;
	case HCI_AMP_TEST_END:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_AMP_TEST_END\n"));
		bthci_CmdAMPTestEnd(padapter, pHciCmd);
		break;
	case HCI_AMP_TEST_COMMAND:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_AMP_TEST_COMMAND\n"));
		bthci_CmdAMPTestCommand(padapter, pHciCmd);
		break;
	case HCI_ENABLE_AMP_RECEIVER_REPORTS:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_ENABLE_AMP_RECEIVER_REPORTS\n"));
		bthci_CmdEnableAMPReceiverReports(padapter, pHciCmd);
		break;
	default:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("HCI_UNKNOWN_COMMAND\n"));
		status = bthci_UnknownCMD(padapter, pHciCmd);
		break;
	}
	return status;
}

static enum hci_status
bthci_HandleOGFExtension(struct rtw_adapter *padapter,
			 struct packet_irp_hcicmd_data *pHciCmd)
{
	enum hci_status status = HCI_STATUS_SUCCESS;
	switch (pHciCmd->OCF) {
	case HCI_SET_ACL_LINK_DATA_FLOW_MODE:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_SET_ACL_LINK_DATA_FLOW_MODE\n"));
		status = bthci_CmdSetACLLinkDataFlowMode(padapter, pHciCmd);
		break;
	case HCI_SET_ACL_LINK_STATUS:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_SET_ACL_LINK_STATUS\n"));
		status = bthci_CmdSetACLLinkStatus(padapter, pHciCmd);
		break;
	case HCI_SET_SCO_LINK_STATUS:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_SET_SCO_LINK_STATUS\n"));
		status = bthci_CmdSetSCOLinkStatus(padapter, pHciCmd);
		break;
	case HCI_SET_RSSI_VALUE:
		RTPRINT(FIOCTL, IOCTL_BT_EVENT_PERIODICAL, ("HCI_SET_RSSI_VALUE\n"));
		status = bthci_CmdSetRSSIValue(padapter, pHciCmd);
		break;
	case HCI_SET_CURRENT_BLUETOOTH_STATUS:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_SET_CURRENT_BLUETOOTH_STATUS\n"));
		status = bthci_CmdSetCurrentBluetoothStatus(padapter, pHciCmd);
		break;
	/* The following is for RTK8723 */

	case HCI_EXTENSION_VERSION_NOTIFY:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_EXTENSION_VERSION_NOTIFY\n"));
		status = bthci_CmdExtensionVersionNotify(padapter, pHciCmd);
		break;
	case HCI_LINK_STATUS_NOTIFY:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_LINK_STATUS_NOTIFY\n"));
		status = bthci_CmdLinkStatusNotify(padapter, pHciCmd);
		break;
	case HCI_BT_OPERATION_NOTIFY:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_BT_OPERATION_NOTIFY\n"));
		status = bthci_CmdBtOperationNotify(padapter, pHciCmd);
		break;
	case HCI_ENABLE_WIFI_SCAN_NOTIFY:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_ENABLE_WIFI_SCAN_NOTIFY\n"));
		status = bthci_CmdEnableWifiScanNotify(padapter, pHciCmd);
		break;

	/* The following is for IVT */
	case HCI_WIFI_CURRENT_CHANNEL:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_WIFI_CURRENT_CHANNEL\n"));
		status = bthci_CmdWIFICurrentChannel(padapter, pHciCmd);
		break;
	case HCI_WIFI_CURRENT_BANDWIDTH:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_WIFI_CURRENT_BANDWIDTH\n"));
		status = bthci_CmdWIFICurrentBandwidth(padapter, pHciCmd);
		break;
	case HCI_WIFI_CONNECTION_STATUS:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_WIFI_CONNECTION_STATUS\n"));
		status = bthci_CmdWIFIConnectionStatus(padapter, pHciCmd);
		break;

	default:
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCI_UNKNOWN_COMMAND\n"));
		status = bthci_UnknownCMD(padapter, pHciCmd);
		break;
	}
	return status;
}

static void
bthci_StateStarting(struct rtw_adapter *padapter,
		    enum hci_state_with_cmd StateCmd, u8 EntryNum)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	RTPRINT(FIOCTL, IOCTL_STATE, ("[BT state], [Starting], "));
	switch (StateCmd) {
	case STATE_CMD_CONNECT_ACCEPT_TIMEOUT:
		RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_CONNECT_ACCEPT_TIMEOUT\n"));
		pBTInfo->BtAsocEntry[EntryNum].PhysLinkCompleteStatus = HCI_STATUS_CONNECT_ACCEPT_TIMEOUT;
		pBtMgnt->bNeedNotifyAMPNoCap = true;
		BTHCI_DisconnectPeer(padapter, EntryNum);
		break;
	case STATE_CMD_DISCONNECT_PHY_LINK:
		RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_DISCONNECT_PHY_LINK\n"));

		bthci_EventDisconnectPhyLinkComplete(padapter,
		HCI_STATUS_SUCCESS,
		pBTInfo->BtAsocEntry[EntryNum].PhyLinkDisconnectReason,
		EntryNum);

		del_timer_sync(&pBTInfo->BTHCIJoinTimeoutTimer);

		pBTInfo->BtAsocEntry[EntryNum].PhysLinkCompleteStatus = HCI_STATUS_UNKNOW_CONNECT_ID;

		BTHCI_DisconnectPeer(padapter, EntryNum);
		break;
	case STATE_CMD_MAC_START_COMPLETE:
		RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_MAC_START_COMPLETE\n"));
		if (pBTInfo->BtAsocEntry[EntryNum].AMPRole == AMP_BTAP_CREATOR)
			bthci_EventChannelSelected(padapter, EntryNum);
		break;
	default:
		RTPRINT(FIOCTL, IOCTL_STATE, ("State command(%d) is Wrong !!!\n", StateCmd));
		break;
	}
}

static void
bthci_StateConnecting(struct rtw_adapter *padapter,
		      enum hci_state_with_cmd StateCmd, u8 EntryNum)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	RTPRINT(FIOCTL, IOCTL_STATE, ("[BT state], [Connecting], "));
	switch (StateCmd) {
	case STATE_CMD_CONNECT_ACCEPT_TIMEOUT:
		RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_CONNECT_ACCEPT_TIMEOUT\n"));
		pBTInfo->BtAsocEntry[EntryNum].PhysLinkCompleteStatus = HCI_STATUS_CONNECT_ACCEPT_TIMEOUT;
		pBtMgnt->bNeedNotifyAMPNoCap = true;
		BTHCI_DisconnectPeer(padapter, EntryNum);
		break;
	case STATE_CMD_MAC_CONNECT_COMPLETE:
		RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_MAC_CONNECT_COMPLETE\n"));

		if (pBTInfo->BtAsocEntry[EntryNum].AMPRole == AMP_BTAP_JOINER) {
			RT_TRACE(_module_rtl871x_security_c_,
				 _drv_info_, ("StateConnecting \n"));
		}
		break;
	case STATE_CMD_DISCONNECT_PHY_LINK:
		RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_DISCONNECT_PHY_LINK\n"));

		bthci_EventDisconnectPhyLinkComplete(padapter,
		HCI_STATUS_SUCCESS,
		pBTInfo->BtAsocEntry[EntryNum].PhyLinkDisconnectReason,
		EntryNum);

		pBTInfo->BtAsocEntry[EntryNum].PhysLinkCompleteStatus = HCI_STATUS_UNKNOW_CONNECT_ID;

		del_timer_sync(&pBTInfo->BTHCIJoinTimeoutTimer);

		BTHCI_DisconnectPeer(padapter, EntryNum);

		break;
	case STATE_CMD_MAC_CONNECT_CANCEL_INDICATE:
		RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_MAC_CONNECT_CANCEL_INDICATE\n"));
		pBTInfo->BtAsocEntry[EntryNum].PhysLinkCompleteStatus = HCI_STATUS_CONTROLLER_BUSY;
		/*  Because this state cmd is caused by the BTHCI_EventAMPStatusChange(), */
		/*  we don't need to send event in the following BTHCI_DisconnectPeer() again. */
		pBtMgnt->bNeedNotifyAMPNoCap = false;
		BTHCI_DisconnectPeer(padapter, EntryNum);
		break;
	default:
		RTPRINT(FIOCTL, IOCTL_STATE, ("State command(%d) is Wrong !!!\n", StateCmd));
		break;
	}
}

static void
bthci_StateConnected(struct rtw_adapter *padapter,
		     enum hci_state_with_cmd StateCmd, u8 EntryNum)
{
/*PMGNT_INFO pMgntInfo = &padapter->MgntInfo; */
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	u8 i;
	u16 logicHandle = 0;

	RTPRINT(FIOCTL, IOCTL_STATE, ("[BT state], [Connected], "));
	switch (StateCmd) {
	case STATE_CMD_DISCONNECT_PHY_LINK:
		RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_DISCONNECT_PHY_LINK\n"));

		/* When we are trying to disconnect the phy link, we should disconnect log link first, */
		for (i = 0; i < MAX_LOGICAL_LINK_NUM; i++) {
			if (pBTInfo->BtAsocEntry[EntryNum].LogLinkCmdData->BtLogLinkhandle != 0) {
				logicHandle = pBTInfo->BtAsocEntry[EntryNum].LogLinkCmdData->BtLogLinkhandle;

				bthci_EventDisconnectLogicalLinkComplete(padapter, HCI_STATUS_SUCCESS,
					logicHandle, pBTInfo->BtAsocEntry[EntryNum].PhyLinkDisconnectReason);

				pBTInfo->BtAsocEntry[EntryNum].LogLinkCmdData->BtLogLinkhandle = 0;
			}
		}

		bthci_EventDisconnectPhyLinkComplete(padapter,
		HCI_STATUS_SUCCESS,
		pBTInfo->BtAsocEntry[EntryNum].PhyLinkDisconnectReason,
		EntryNum);

		del_timer_sync(&pBTInfo->BTHCIJoinTimeoutTimer);

		BTHCI_DisconnectPeer(padapter, EntryNum);
		break;

	case STATE_CMD_MAC_DISCONNECT_INDICATE:
		RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_MAC_DISCONNECT_INDICATE\n"));

		bthci_EventDisconnectPhyLinkComplete(padapter,
		HCI_STATUS_SUCCESS,
		/*  TODO: Remote Host not local host */
		HCI_STATUS_CONNECT_TERMINATE_LOCAL_HOST,
		EntryNum);
		BTHCI_DisconnectPeer(padapter, EntryNum);

		break;
	case STATE_CMD_ENTER_STATE:
		RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_ENTER_STATE\n"));

		if (pBtMgnt->bBTConnectInProgress) {
			pBtMgnt->bBTConnectInProgress = false;
			RTPRINT(FIOCTL, IOCTL_STATE, ("[BT Flag], BT Connect in progress OFF!!\n"));
		}
		pBTInfo->BtAsocEntry[EntryNum].BtCurrentState = HCI_STATE_CONNECTED;
		pBTInfo->BtAsocEntry[EntryNum].b4waySuccess = true;
		pBtMgnt->bStartSendSupervisionPkt = true;

		/*  for rate adaptive */

		rtl8723a_update_ramask(padapter,
				       MAX_FW_SUPPORT_MACID_NUM-1-EntryNum, 0);

		HalSetBrateCfg23a(padapter, padapter->mlmepriv.cur_network.network.SupportedRates);
		BTDM_SetFwChnlInfo(padapter, RT_MEDIA_CONNECT);
		break;
	default:
		RTPRINT(FIOCTL, IOCTL_STATE, ("State command(%d) is Wrong !!!\n", StateCmd));
		break;
	}
}

static void
bthci_StateAuth(struct rtw_adapter *padapter, enum hci_state_with_cmd StateCmd,
		u8 EntryNum)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	RTPRINT(FIOCTL, IOCTL_STATE, ("[BT state], [Authenticating], "));
	switch (StateCmd) {
	case STATE_CMD_CONNECT_ACCEPT_TIMEOUT:
		RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_CONNECT_ACCEPT_TIMEOUT\n"));
		pBTInfo->BtAsocEntry[EntryNum].PhysLinkCompleteStatus = HCI_STATUS_CONNECT_ACCEPT_TIMEOUT;
		pBtMgnt->bNeedNotifyAMPNoCap = true;
		BTHCI_DisconnectPeer(padapter, EntryNum);
		break;
	case STATE_CMD_DISCONNECT_PHY_LINK:
		RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_DISCONNECT_PHY_LINK\n"));
		bthci_EventDisconnectPhyLinkComplete(padapter,
		HCI_STATUS_SUCCESS,
		pBTInfo->BtAsocEntry[EntryNum].PhyLinkDisconnectReason,
		EntryNum);

		pBTInfo->BtAsocEntry[EntryNum].PhysLinkCompleteStatus = HCI_STATUS_UNKNOW_CONNECT_ID;

		del_timer_sync(&pBTInfo->BTHCIJoinTimeoutTimer);

		BTHCI_DisconnectPeer(padapter, EntryNum);
		break;
	case STATE_CMD_4WAY_FAILED:
		RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_4WAY_FAILED\n"));

		pBTInfo->BtAsocEntry[EntryNum].PhysLinkCompleteStatus = HCI_STATUS_AUTH_FAIL;
		pBtMgnt->bNeedNotifyAMPNoCap = true;

		BTHCI_DisconnectPeer(padapter, EntryNum);

		del_timer_sync(&pBTInfo->BTHCIJoinTimeoutTimer);
		break;
	case STATE_CMD_4WAY_SUCCESSED:
		RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_4WAY_SUCCESSED\n"));

		bthci_EventPhysicalLinkComplete(padapter, HCI_STATUS_SUCCESS, EntryNum, INVALID_PL_HANDLE);

		del_timer_sync(&pBTInfo->BTHCIJoinTimeoutTimer);

		BTHCI_SM_WITH_INFO(padapter, HCI_STATE_CONNECTED, STATE_CMD_ENTER_STATE, EntryNum);
		break;
	default:
		RTPRINT(FIOCTL, IOCTL_STATE, ("State command(%d) is Wrong !!!\n", StateCmd));
		break;
	}
}

static void
bthci_StateDisconnecting(struct rtw_adapter *padapter,
			 enum hci_state_with_cmd StateCmd, u8 EntryNum)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	RTPRINT(FIOCTL, IOCTL_STATE, ("[BT state], [Disconnecting], "));
	switch (StateCmd) {
	case STATE_CMD_MAC_CONNECT_CANCEL_INDICATE:
		RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_MAC_CONNECT_CANCEL_INDICATE\n"));
		if (pBTInfo->BtAsocEntry[EntryNum].bNeedPhysLinkCompleteEvent) {
			bthci_EventPhysicalLinkComplete(padapter,
				pBTInfo->BtAsocEntry[EntryNum].PhysLinkCompleteStatus,
				EntryNum, INVALID_PL_HANDLE);
		}

		if (pBtMgnt->bBTConnectInProgress) {
			pBtMgnt->bBTConnectInProgress = false;
			RTPRINT(FIOCTL, IOCTL_STATE, ("[BT Flag], BT Connect in progress OFF!!\n"));
		}

		BTHCI_SM_WITH_INFO(padapter, HCI_STATE_DISCONNECTED, STATE_CMD_ENTER_STATE, EntryNum);
		break;
	case STATE_CMD_DISCONNECT_PHY_LINK:
		RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_DISCONNECT_PHY_LINK\n"));

		bthci_EventDisconnectPhyLinkComplete(padapter,
		HCI_STATUS_SUCCESS,
		pBTInfo->BtAsocEntry[EntryNum].PhyLinkDisconnectReason,
		EntryNum);

		del_timer_sync(&pBTInfo->BTHCIJoinTimeoutTimer);

		BTHCI_DisconnectPeer(padapter, EntryNum);
		break;
	default:
		RTPRINT(FIOCTL, IOCTL_STATE, ("State command(%d) is Wrong !!!\n", StateCmd));
		break;
	}
}

static void
bthci_StateDisconnected(struct rtw_adapter *padapter,
			enum hci_state_with_cmd StateCmd, u8 EntryNum)
{
/*PMGNT_INFO pMgntInfo = &padapter->MgntInfo; */
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	RTPRINT(FIOCTL, IOCTL_STATE, ("[BT state], [Disconnected], "));
	switch (StateCmd) {
	case STATE_CMD_CREATE_PHY_LINK:
	case STATE_CMD_ACCEPT_PHY_LINK:
		if (StateCmd == STATE_CMD_CREATE_PHY_LINK)
			RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_CREATE_PHY_LINK\n"));
		else
			RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_ACCEPT_PHY_LINK\n"));

		RTPRINT(FIOCTL, IOCTL_STATE, ("[BT PS], Disable IPS and LPS\n"));
		ips_leave23a(padapter);
		LPS_Leave23a(padapter);

		pBtMgnt->bPhyLinkInProgress = true;
		pBtMgnt->BTCurrentConnectType = BT_DISCONNECT;
		pBtMgnt->CurrentBTConnectionCnt++;
		RTPRINT(FIOCTL, IOCTL_STATE, ("[BT Flag], CurrentBTConnectionCnt = %d\n",
			pBtMgnt->CurrentBTConnectionCnt));
		pBtMgnt->BtOperationOn = true;
		RTPRINT(FIOCTL, IOCTL_STATE, ("[BT Flag], Bt Operation ON!! CurrentConnectEntryNum = %d\n",
			pBtMgnt->CurrentConnectEntryNum));

		if (pBtMgnt->bBTConnectInProgress) {
			bthci_EventPhysicalLinkComplete(padapter, HCI_STATUS_CONTROLLER_BUSY, INVALID_ENTRY_NUM, pBtMgnt->BtCurrentPhyLinkhandle);
			bthci_RemoveEntryByEntryNum(padapter, EntryNum);
			return;
		}

		if (StateCmd == STATE_CMD_CREATE_PHY_LINK)
			pBTInfo->BtAsocEntry[EntryNum].AMPRole = AMP_BTAP_CREATOR;
		else
			pBTInfo->BtAsocEntry[EntryNum].AMPRole = AMP_BTAP_JOINER;

		/*  1. MAC not yet in selected channel */
		while (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE|WIFI_SITE_MONITOR)) {
			RTPRINT(FIOCTL, IOCTL_STATE, ("Scan/Roaming/Wifi Link is in Progress, wait 200 ms\n"));
			mdelay(200);
		}
		/*  2. MAC already in selected channel */
		RTPRINT(FIOCTL, IOCTL_STATE, ("Channel is Ready\n"));
		mod_timer(&pBTInfo->BTHCIJoinTimeoutTimer,
			  jiffies + msecs_to_jiffies(pBtHciInfo->ConnAcceptTimeout));

		pBTInfo->BtAsocEntry[EntryNum].bNeedPhysLinkCompleteEvent = true;
		break;
	case STATE_CMD_DISCONNECT_PHY_LINK:
		RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_DISCONNECT_PHY_LINK\n"));

		del_timer_sync(&pBTInfo->BTHCIJoinTimeoutTimer);

		bthci_EventDisconnectPhyLinkComplete(padapter,
		HCI_STATUS_SUCCESS,
		pBTInfo->BtAsocEntry[EntryNum].PhyLinkDisconnectReason,
		EntryNum);

		if (pBTInfo->BtAsocEntry[EntryNum].bNeedPhysLinkCompleteEvent) {
			bthci_EventPhysicalLinkComplete(padapter,
				HCI_STATUS_UNKNOW_CONNECT_ID,
				EntryNum, INVALID_PL_HANDLE);
		}

		if (pBtMgnt->bBTConnectInProgress) {
			pBtMgnt->bBTConnectInProgress = false;
			RTPRINT(FIOCTL, IOCTL_STATE, ("[BT Flag], BT Connect in progress OFF!!\n"));
		}
		BTHCI_SM_WITH_INFO(padapter, HCI_STATE_DISCONNECTED, STATE_CMD_ENTER_STATE, EntryNum);
		bthci_RemoveEntryByEntryNum(padapter, EntryNum);
		break;
	case STATE_CMD_ENTER_STATE:
		RTPRINT(FIOCTL, IOCTL_STATE, ("STATE_CMD_ENTER_STATE\n"));
		break;
	default:
		RTPRINT(FIOCTL, IOCTL_STATE, ("State command(%d) is Wrong !!!\n", StateCmd));
		break;
	}
}

void BTHCI_EventParse(struct rtw_adapter *padapter, void *pEvntData, u32 dataLen)
{
}

u8 BTHCI_HsConnectionEstablished(struct rtw_adapter *padapter)
{
	u8 bBtConnectionExist = false;
	struct bt_30info *pBtinfo = GET_BT_INFO(padapter);
	u8 i;

	for (i = 0; i < MAX_BT_ASOC_ENTRY_NUM; i++) {
		if (pBtinfo->BtAsocEntry[i].b4waySuccess) {
			bBtConnectionExist = true;
			break;
		}
	}

/*RTPRINT(FIOCTL, IOCTL_STATE, (" BTHCI_HsConnectionEstablished(), connection exist = %d\n", bBtConnectionExist)); */

	return bBtConnectionExist;
}

static u8
BTHCI_CheckProfileExist(struct rtw_adapter *padapter,
			enum bt_traffic_mode_profile Profile)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	u8 IsPRofile = false;
	u8 i = 0;

	for (i = 0; i < pBtMgnt->ExtConfig.NumberOfHandle; i++) {
		if (pBtMgnt->ExtConfig.linkInfo[i].TrafficProfile == Profile) {
			IsPRofile = true;
			break;
		}
	}

	return IsPRofile;
}

void BTHCI_UpdateBTProfileRTKToMoto(struct rtw_adapter *padapter)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	u8 i = 0;

	pBtMgnt->ExtConfig.NumberOfSCO = 0;

	for (i = 0; i < pBtMgnt->ExtConfig.NumberOfHandle; i++) {
		pBtMgnt->ExtConfig.linkInfo[i].TrafficProfile = BT_PROFILE_NONE;

		if (pBtMgnt->ExtConfig.linkInfo[i].BTProfile == BT_PROFILE_SCO)
			pBtMgnt->ExtConfig.NumberOfSCO++;

		pBtMgnt->ExtConfig.linkInfo[i].TrafficProfile = pBtMgnt->ExtConfig.linkInfo[i].BTProfile;
		switch (pBtMgnt->ExtConfig.linkInfo[i].TrafficProfile) {
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

void BTHCI_WifiScanNotify(struct rtw_adapter *padapter, u8 scanType)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bEnableWifiScanNotify)
		bthci_EventExtWifiScanNotify(padapter, scanType);
}

void
BTHCI_StateMachine(
	struct rtw_adapter *padapter,
	u8 		StateToEnter,
	enum hci_state_with_cmd		StateCmd,
	u8 		EntryNum
	)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	if (EntryNum == 0xff) {
		RTPRINT(FIOCTL, IOCTL_STATE, (" StateMachine, error EntryNum = 0x%x \n", EntryNum));
		return;
	}
	RTPRINT(FIOCTL, IOCTL_STATE, (" StateMachine, EntryNum = 0x%x, CurrentState = 0x%x, BtNextState = 0x%x,  StateCmd = 0x%x , StateToEnter = 0x%x\n",
		EntryNum, pBTInfo->BtAsocEntry[EntryNum].BtCurrentState, pBTInfo->BtAsocEntry[EntryNum].BtNextState, StateCmd, StateToEnter));

	if (pBTInfo->BtAsocEntry[EntryNum].BtNextState & StateToEnter) {
		pBTInfo->BtAsocEntry[EntryNum].BtCurrentState = StateToEnter;

		switch (StateToEnter) {
		case HCI_STATE_STARTING:
			pBTInfo->BtAsocEntry[EntryNum].BtNextState = HCI_STATE_DISCONNECTING | HCI_STATE_CONNECTING;
			bthci_StateStarting(padapter, StateCmd, EntryNum);
			break;
		case HCI_STATE_CONNECTING:
			pBTInfo->BtAsocEntry[EntryNum].BtNextState = HCI_STATE_CONNECTING | HCI_STATE_DISCONNECTING | HCI_STATE_AUTHENTICATING;
			bthci_StateConnecting(padapter, StateCmd, EntryNum);
			break;
		case HCI_STATE_AUTHENTICATING:
			pBTInfo->BtAsocEntry[EntryNum].BtNextState = HCI_STATE_DISCONNECTING | HCI_STATE_CONNECTED;
			bthci_StateAuth(padapter, StateCmd, EntryNum);
			break;
		case HCI_STATE_CONNECTED:
			pBTInfo->BtAsocEntry[EntryNum].BtNextState = HCI_STATE_CONNECTED | HCI_STATE_DISCONNECTING;
			bthci_StateConnected(padapter, StateCmd, EntryNum);
			break;
		case HCI_STATE_DISCONNECTING:
			pBTInfo->BtAsocEntry[EntryNum].BtNextState = HCI_STATE_DISCONNECTED | HCI_STATE_DISCONNECTING;
			bthci_StateDisconnecting(padapter, StateCmd, EntryNum);
			break;
		case HCI_STATE_DISCONNECTED:
			pBTInfo->BtAsocEntry[EntryNum].BtNextState = HCI_STATE_DISCONNECTED | HCI_STATE_STARTING | HCI_STATE_CONNECTING;
			bthci_StateDisconnected(padapter, StateCmd, EntryNum);
			break;
		default:
			RTPRINT(FIOCTL, IOCTL_STATE, (" StateMachine, Unknown state to enter!!!\n"));
			break;
		}
	} else {
		RTPRINT(FIOCTL, IOCTL_STATE, (" StateMachine, Wrong state to enter\n"));
	}

	/*  20100325 Joseph: Disable/Enable IPS/LPS according to BT status. */
	if (!pBtMgnt->bBTConnectInProgress && !pBtMgnt->BtOperationOn) {
		RTPRINT(FIOCTL, IOCTL_STATE, ("[BT PS], ips_enter23a()\n"));
		ips_enter23a(padapter);
	}
}

void BTHCI_DisconnectPeer(struct rtw_adapter *padapter, u8 EntryNum)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, (" BTHCI_DisconnectPeer()\n"));

	BTHCI_SM_WITH_INFO(padapter, HCI_STATE_DISCONNECTING, STATE_CMD_MAC_CONNECT_CANCEL_INDICATE, EntryNum);

	if (pBTInfo->BtAsocEntry[EntryNum].bUsed) {
/*BTPKT_SendDeauthentication(padapter, pBTInfo->BtAsocEntry[EntryNum].BTRemoteMACAddr, unspec_reason); not porting yet */
	}

	if (pBtMgnt->bBTConnectInProgress) {
		pBtMgnt->bBTConnectInProgress = false;
		RTPRINT(FIOCTL, IOCTL_STATE, ("[BT Flag], BT Connect in progress OFF!!\n"));
	}

	bthci_RemoveEntryByEntryNum(padapter, EntryNum);

	if (pBtMgnt->bNeedNotifyAMPNoCap) {
		RTPRINT(FIOCTL, IOCTL_STATE, ("[BT AMPStatus], set to invalid in BTHCI_DisconnectPeer()\n"));
		BTHCI_EventAMPStatusChange(padapter, AMP_STATUS_NO_CAPACITY_FOR_BT);
	}
}

void BTHCI_EventNumOfCompletedDataBlocks(struct rtw_adapter *padapter)
{
/*PMGNT_INFO pMgntInfo = &padapter->MgntInfo; */
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_hci_info *pBtHciInfo = &pBTInfo->BtHciInfo;
	u8 localBuf[TmpLocalBufSize] = "";
	u8 *pRetPar, *pTriple;
	u8 len = 0, i, j, handleNum = 0;
	struct packet_irp_hcievent_data *PPacketIrpEvent;
	u16 *pu2Temp, *pPackets, *pHandle, *pDblocks;
	u8 sent = 0;

	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);

	if (!(pBtHciInfo->BTEventMaskPage2 & EMP2_HCI_EVENT_NUM_OF_COMPLETE_DATA_BLOCKS)) {
		RTPRINT(FIOCTL, IOCTL_BT_EVENT, ("[BT event], Num Of Completed DataBlocks, Ignore to send NumOfCompletedDataBlocksEvent due to event mask page 2\n"));
		return;
	}

	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[0];
	pTriple = &pRetPar[3];
	for (j = 0; j < MAX_BT_ASOC_ENTRY_NUM; j++) {

		for (i = 0; i < MAX_LOGICAL_LINK_NUM; i++) {
			if (pBTInfo->BtAsocEntry[j].LogLinkCmdData[i].BtLogLinkhandle) {
				handleNum++;
				pHandle = (u16 *)&pTriple[0];	/*  Handle[i] */
				pPackets = (u16 *)&pTriple[2];	/*  Num_Of_Completed_Packets[i] */
				pDblocks = (u16 *)&pTriple[4];	/*  Num_Of_Completed_Blocks[i] */
				*pHandle = pBTInfo->BtAsocEntry[j].LogLinkCmdData[i].BtLogLinkhandle;
				*pPackets = (u16)pBTInfo->BtAsocEntry[j].LogLinkCmdData[i].TxPacketCount;
				*pDblocks = (u16)pBTInfo->BtAsocEntry[j].LogLinkCmdData[i].TxPacketCount;
				if (pBTInfo->BtAsocEntry[j].LogLinkCmdData[i].TxPacketCount) {
					sent = 1;
					RTPRINT(FIOCTL, IOCTL_BT_EVENT_DETAIL,
						("[BT event], Num Of Completed DataBlocks, Handle = 0x%x, Num_Of_Completed_Packets = 0x%x, Num_Of_Completed_Blocks = 0x%x\n",
					*pHandle, *pPackets, *pDblocks));
				}
				pBTInfo->BtAsocEntry[j].LogLinkCmdData[i].TxPacketCount = 0;
				len += 6;
				pTriple += len;
			}
		}
	}

	pRetPar[2] = handleNum;				/*  Number_of_Handles */
	len += 1;
	pu2Temp = (u16 *)&pRetPar[0];
	*pu2Temp = BTTotalDataBlockNum;
	len += 2;

	PPacketIrpEvent->EventCode = HCI_EVENT_NUM_OF_COMPLETE_DATA_BLOCKS;
	PPacketIrpEvent->Length = len;
	if (handleNum && sent)
		bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2);
}

void BTHCI_EventAMPStatusChange(struct rtw_adapter *padapter, u8 AMP_Status)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	struct packet_irp_hcievent_data *PPacketIrpEvent;
	u8 len = 0;
	u8 localBuf[7] = "";
	u8 *pRetPar;

	if (AMP_Status == AMP_STATUS_NO_CAPACITY_FOR_BT) {
		pBtMgnt->BTNeedAMPStatusChg = true;
		pBtMgnt->bNeedNotifyAMPNoCap = false;

		BTHCI_DisconnectAll(padapter);
	} else if (AMP_Status == AMP_STATUS_FULL_CAPACITY_FOR_BT) {
		pBtMgnt->BTNeedAMPStatusChg = false;
	}

	PPacketIrpEvent = (struct packet_irp_hcievent_data *)(&localBuf[0]);
	/*  Return parameters starts from here */
	pRetPar = &PPacketIrpEvent->Data[0];

	pRetPar[0] = 0;	/*  Status */
	len += 1;
	pRetPar[1] = AMP_Status;	/*  AMP_Status */
	len += 1;

	PPacketIrpEvent->EventCode = HCI_EVENT_AMP_STATUS_CHANGE;
	PPacketIrpEvent->Length = len;
	if (bthci_IndicateEvent(padapter, PPacketIrpEvent, len+2) == RT_STATUS_SUCCESS)
		RTPRINT(FIOCTL, (IOCTL_BT_EVENT|IOCTL_STATE), ("[BT event], AMP Status Change, AMP_Status = %d\n", AMP_Status));
}

void BTHCI_DisconnectAll(struct rtw_adapter *padapter)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	u8 i;

	RTPRINT(FIOCTL, IOCTL_STATE, (" DisconnectALL()\n"));

	for (i = 0; i < MAX_BT_ASOC_ENTRY_NUM; i++) {
		if (pBTInfo->BtAsocEntry[i].b4waySuccess) {
			BTHCI_SM_WITH_INFO(padapter, HCI_STATE_CONNECTED, STATE_CMD_DISCONNECT_PHY_LINK, i);
		} else if (pBTInfo->BtAsocEntry[i].bUsed) {
			if (pBTInfo->BtAsocEntry[i].BtCurrentState == HCI_STATE_CONNECTING) {
				BTHCI_SM_WITH_INFO(padapter, HCI_STATE_CONNECTING, STATE_CMD_MAC_CONNECT_CANCEL_INDICATE, i);
			} else if (pBTInfo->BtAsocEntry[i].BtCurrentState == HCI_STATE_DISCONNECTING) {
				BTHCI_SM_WITH_INFO(padapter, HCI_STATE_DISCONNECTING, STATE_CMD_MAC_CONNECT_CANCEL_INDICATE, i);
			}
		}
	}
}

enum hci_status
BTHCI_HandleHCICMD(
	struct rtw_adapter *padapter,
	struct packet_irp_hcicmd_data *pHciCmd
	)
{
	enum hci_status	status = HCI_STATUS_SUCCESS;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_dgb *pBtDbg = &pBTInfo->BtDbg;

	RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("\n"));
	RTPRINT(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), ("HCI Command start, OGF = 0x%x, OCF = 0x%x, Length = 0x%x\n",
		pHciCmd->OGF, pHciCmd->OCF, pHciCmd->Length));
	if (pHciCmd->Length) {
		RTPRINT_DATA(FIOCTL, (IOCTL_BT_HCICMD_DETAIL|IOCTL_BT_LOGO), "HCI Command, Hex Data :\n",
			&pHciCmd->Data[0], pHciCmd->Length);
	}
	if (pHciCmd->OGF == OGF_EXTENSION) {
		if (pHciCmd->OCF == HCI_SET_RSSI_VALUE)
			RTPRINT(FIOCTL, IOCTL_BT_EVENT_PERIODICAL, ("[BT cmd], "));
		else
			RTPRINT(FIOCTL, IOCTL_BT_HCICMD_EXT, ("[BT cmd], "));
	} else {
		RTPRINT(FIOCTL, IOCTL_BT_HCICMD, ("[BT cmd], "));
	}

	pBtDbg->dbgHciInfo.hciCmdCnt++;

	switch (pHciCmd->OGF) {
	case LINK_CONTROL_COMMANDS:
		status = bthci_HandleOGFLinkControlCMD(padapter, pHciCmd);
		break;
	case HOLD_MODE_COMMAND:
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
		status = bthci_HandleOGFExtension(padapter, pHciCmd);
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

/*  ===== End of sync from SD7 driver COMMOM/bt_hci.c ===== */

static const char *const BtStateString[] = {
	"BT_DISABLED",
	"BT_NO_CONNECTION",
	"BT_CONNECT_IDLE",
	"BT_INQ_OR_PAG",
	"BT_ACL_ONLY_BUSY",
	"BT_SCO_ONLY_BUSY",
	"BT_ACL_SCO_BUSY",
	"BT_ACL_INQ_OR_PAG",
	"BT_STATE_NOT_DEFINED"
};

/*  ===== Below this line is sync from SD7 driver HAL/BTCoexist/HalBtc87231Ant.c ===== */

static void btdm_SetFwIgnoreWlanAct(struct rtw_adapter *padapter, u8 bEnable)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	u8 H2C_Parameter[1] = {0};

	if (bEnable) {
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT Ignore Wlan_Act !!\n"));
		H2C_Parameter[0] |= BIT(0);		/*  function enable */
		pHalData->bt_coexist.bFWCoexistAllOff = false;
	} else {
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT don't ignore Wlan_Act !!\n"));
	}

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], set FW for BT Ignore Wlan_Act, write 0x25 = 0x%02x\n",
		H2C_Parameter[0]));

	FillH2CCmd(padapter, BT_IGNORE_WLAN_ACT_EID, 1, H2C_Parameter);
}

static void btdm_NotifyFwScan(struct rtw_adapter *padapter, u8 scanType)
{
	u8 H2C_Parameter[1] = {0};

	if (scanType == true)
		H2C_Parameter[0] = 0x1;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], Notify FW for wifi scan, write 0x3b = 0x%02x\n",
		H2C_Parameter[0]));

	FillH2CCmd(padapter, 0x3b, 1, H2C_Parameter);
}

static void btdm_1AntSetPSMode(struct rtw_adapter *padapter,
			       u8 enable, u8 smartps, u8 mode)
{
	struct pwrctrl_priv *pwrctrl;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], Current LPS(%s, %d), smartps =%d\n", enable == true?"ON":"OFF", mode, smartps));

	pwrctrl = &padapter->pwrctrlpriv;

	if (enable == true) {
		rtw_set_ps_mode23a(padapter, PS_MODE_MIN, smartps, mode);
	} else {
		rtw_set_ps_mode23a(padapter, PS_MODE_ACTIVE, 0, 0);
		LPS_RF_ON_check23a(padapter, 100);
	}
}

static void btdm_1AntTSFSwitch(struct rtw_adapter *padapter, u8 enable)
{
	u8 oldVal, newVal;

	oldVal = rtl8723au_read8(padapter, 0x550);

	if (enable)
		newVal = oldVal | EN_BCN_FUNCTION;
	else
		newVal = oldVal & ~EN_BCN_FUNCTION;

	if (oldVal != newVal)
		rtl8723au_write8(padapter, 0x550, newVal);
}

static u8 btdm_Is1AntPsTdmaStateChange(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct btdm_8723a_1ant *pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm1Ant;

	if ((pBtdm8723->bPrePsTdmaOn != pBtdm8723->bCurPsTdmaOn) ||
		(pBtdm8723->prePsTdma != pBtdm8723->curPsTdma))
		return true;
	else
		return false;
}

/*  Before enter TDMA, make sure Power Saving is enable! */
static void
btdm_1AntPsTdma(
	struct rtw_adapter *padapter,
	u8 bTurnOn,
	u8 type
	)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct btdm_8723a_1ant *pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm1Ant;

	pBtdm8723->bCurPsTdmaOn = bTurnOn;
	pBtdm8723->curPsTdma = type;
	if (bTurnOn) {
		switch (type) {
		case 1:	/*  A2DP Level-1 or FTP/OPP */
		default:
			if (btdm_Is1AntPsTdmaStateChange(padapter)) {
				/*  wide duration for WiFi */
				BTDM_SetFw3a(padapter, 0xd3, 0x1a, 0x1a, 0x0, 0x58);
			}
			break;
		case 2:	/*  A2DP Level-2 */
			if (btdm_Is1AntPsTdmaStateChange(padapter)) {
				/*  normal duration for WiFi */
				BTDM_SetFw3a(padapter, 0xd3, 0x12, 0x12, 0x0, 0x58);
			}
			break;
		case 3:	/*  BT FTP/OPP */
			if (btdm_Is1AntPsTdmaStateChange(padapter)) {
				/*  normal duration for WiFi */
				BTDM_SetFw3a(padapter, 0xd3, 0x30, 0x03, 0x10, 0x58);

			}
			break;
		case 4:	/*  for wifi scan & BT is connected */
			if (btdm_Is1AntPsTdmaStateChange(padapter)) {
				/*  protect 3 beacons in 3-beacon period & no Tx pause at BT slot */
				BTDM_SetFw3a(padapter, 0x93, 0x15, 0x03, 0x14, 0x0);
			}
			break;
		case 5:	/*  for WiFi connected-busy & BT is Non-Connected-Idle */
			if (btdm_Is1AntPsTdmaStateChange(padapter)) {
				/*  SCO mode, Ant fixed at WiFi, WLAN_Act toggle */
				BTDM_SetFw3a(padapter, 0x61, 0x15, 0x03, 0x31, 0x00);
			}
			break;
		case 9:	/*  ACL high-retry type - 2 */
			if (btdm_Is1AntPsTdmaStateChange(padapter)) {
				/*  narrow duration for WiFi */
				BTDM_SetFw3a(padapter, 0xd3, 0xa, 0xa, 0x0, 0x58); /* narrow duration for WiFi */
			}
			break;
		case 10: /*  for WiFi connect idle & BT ACL busy or WiFi Connected-Busy & BT is Inquiry */
			if (btdm_Is1AntPsTdmaStateChange(padapter))
				BTDM_SetFw3a(padapter, 0x13, 0xa, 0xa, 0x0, 0x40);
			break;
		case 11: /*  ACL high-retry type - 3 */
			if (btdm_Is1AntPsTdmaStateChange(padapter)) {
				/*  narrow duration for WiFi */
				BTDM_SetFw3a(padapter, 0xd3, 0x05, 0x05, 0x00, 0x58);
			}
			break;
		case 12: /*  for WiFi Connected-Busy & BT is Connected-Idle */
			if (btdm_Is1AntPsTdmaStateChange(padapter)) {
				/*  Allow High-Pri BT */
				BTDM_SetFw3a(padapter, 0xeb, 0x0a, 0x03, 0x31, 0x18);
			}
			break;
		case 20: /*  WiFi only busy , TDMA mode for power saving */
			if (btdm_Is1AntPsTdmaStateChange(padapter))
				BTDM_SetFw3a(padapter, 0x13, 0x25, 0x25, 0x00, 0x00);
			break;
		case 27: /*  WiFi DHCP/Site Survey & BT SCO busy */
			if (btdm_Is1AntPsTdmaStateChange(padapter))
				BTDM_SetFw3a(padapter, 0xa3, 0x25, 0x03, 0x31, 0x98);
			break;
		case 28: /*  WiFi DHCP/Site Survey & BT idle */
			if (btdm_Is1AntPsTdmaStateChange(padapter))
				BTDM_SetFw3a(padapter, 0x69, 0x25, 0x03, 0x31, 0x00);
			break;
		case 29: /*  WiFi DHCP/Site Survey & BT ACL busy */
			if (btdm_Is1AntPsTdmaStateChange(padapter)) {
				BTDM_SetFw3a(padapter, 0xeb, 0x1a, 0x1a, 0x01, 0x18);
				rtl8723au_write32(padapter, 0x6c0, 0x5afa5afa);
				rtl8723au_write32(padapter, 0x6c4, 0x5afa5afa);
			}
			break;
		case 30: /*  WiFi idle & BT Inquiry */
			if (btdm_Is1AntPsTdmaStateChange(padapter))
				BTDM_SetFw3a(padapter, 0x93, 0x15, 0x03, 0x14, 0x00);
			break;
		case 31:  /*  BT HID */
			if (btdm_Is1AntPsTdmaStateChange(padapter))
				BTDM_SetFw3a(padapter, 0xd3, 0x1a, 0x1a, 0x00, 0x58);
			break;
		case 32:  /*  BT SCO & Inquiry */
			if (btdm_Is1AntPsTdmaStateChange(padapter))
				BTDM_SetFw3a(padapter, 0xab, 0x0a, 0x03, 0x11, 0x98);
			break;
		case 33:  /*  BT SCO & WiFi site survey */
			if (btdm_Is1AntPsTdmaStateChange(padapter))
				BTDM_SetFw3a(padapter, 0xa3, 0x25, 0x03, 0x30, 0x98);
			break;
		case 34:  /*  BT HID & WiFi site survey */
			if (btdm_Is1AntPsTdmaStateChange(padapter))
				BTDM_SetFw3a(padapter, 0xd3, 0x1a, 0x1a, 0x00, 0x18);
			break;
		case 35:  /*  BT HID & WiFi Connecting */
			if (btdm_Is1AntPsTdmaStateChange(padapter))
				BTDM_SetFw3a(padapter, 0xe3, 0x1a, 0x1a, 0x00, 0x18);
			break;
		}
	} else {
		/*  disable PS-TDMA */
		switch (type) {
		case 8:
			if (btdm_Is1AntPsTdmaStateChange(padapter)) {
				/*  Antenna control by PTA, 0x870 = 0x310 */
				BTDM_SetFw3a(padapter, 0x8, 0x0, 0x0, 0x0, 0x0);
			}
			break;
		case 0:
		default:
			if (btdm_Is1AntPsTdmaStateChange(padapter)) {
				/*  Antenna control by PTA, 0x870 = 0x310 */
				BTDM_SetFw3a(padapter, 0x0, 0x0, 0x0, 0x8, 0x0);
			}
			/*  Switch Antenna to BT */
			rtl8723au_write16(padapter, 0x860, 0x210);
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], 0x860 = 0x210, Switch Antenna to BT\n"));
			break;
		case 9:
			if (btdm_Is1AntPsTdmaStateChange(padapter)) {
				/*  Antenna control by PTA, 0x870 = 0x310 */
				BTDM_SetFw3a(padapter, 0x0, 0x0, 0x0, 0x8, 0x0);
			}
			/*  Switch Antenna to WiFi */
			rtl8723au_write16(padapter, 0x860, 0x110);
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], 0x860 = 0x110, Switch Antenna to WiFi\n"));
			break;
		}
	}

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], Current TDMA(%s, %d)\n",
		pBtdm8723->bCurPsTdmaOn?"ON":"OFF", pBtdm8723->curPsTdma));

	/*  update pre state */
	pBtdm8723->bPrePsTdmaOn = pBtdm8723->bCurPsTdmaOn;
	pBtdm8723->prePsTdma = pBtdm8723->curPsTdma;
}

static void
_btdm_1AntSetPSTDMA(struct rtw_adapter *padapter, u8 bPSEn, u8 smartps,
		    u8 psOption, u8 bTDMAOn, u8 tdmaType)
{
	struct pwrctrl_priv *pwrctrl;
	struct hal_data_8723a *pHalData;
	struct btdm_8723a_1ant *pBtdm8723;
	u8 psMode;
	u8 bSwitchPS;

	if (!check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE) &&
	    (get_fwstate(&padapter->mlmepriv) != WIFI_NULL_STATE)) {
		btdm_1AntPsTdma(padapter, bTDMAOn, tdmaType);
		return;
	}
	psOption &= ~BIT(0);

	RTPRINT(FBT, BT_TRACE,
		("[BTCoex], Set LPS(%s, %d) TDMA(%s, %d)\n",
		bPSEn == true?"ON":"OFF", psOption,
		bTDMAOn == true?"ON":"OFF", tdmaType));

	pwrctrl = &padapter->pwrctrlpriv;
	pHalData = GET_HAL_DATA(padapter);
	pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm1Ant;

	if (bPSEn) {
		if (pBtdm8723->bWiFiHalt) {
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], Enable PS Fail, WiFi in Halt!!\n"));
			return;
		}

		if (pwrctrl->bInSuspend) {
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], Enable PS Fail, WiFi in Suspend!!\n"));
			return;
		}

		if (padapter->bDriverStopped) {
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], Enable PS Fail, WiFi driver stopped!!\n"));
			return;
		}

		if (padapter->bSurpriseRemoved) {
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], Enable PS Fail, WiFi Surprise Removed!!\n"));
			return;
		}

		psMode = PS_MODE_MIN;
	} else {
		psMode = PS_MODE_ACTIVE;
		psOption = 0;
	}

	if (psMode != pwrctrl->pwr_mode) {
		bSwitchPS = true;
	} else if (psMode != PS_MODE_ACTIVE) {
		if (psOption != pwrctrl->bcn_ant_mode)
			bSwitchPS = true;
		else if (smartps != pwrctrl->smart_ps)
			bSwitchPS = true;
		else
			bSwitchPS = false;
	} else {
		bSwitchPS = false;
	}

	if (bSwitchPS) {
		/*  disable TDMA */
		if (pBtdm8723->bCurPsTdmaOn) {
			if (!bTDMAOn) {
				btdm_1AntPsTdma(padapter, false, tdmaType);
			} else {
				if (!rtl8723a_BT_enabled(padapter) ||
				    (pHalData->bt_coexist.halCoex8723.c2hBtInfo == BT_INFO_STATE_NO_CONNECTION) ||
				    (pHalData->bt_coexist.halCoex8723.c2hBtInfo == BT_INFO_STATE_CONNECT_IDLE) ||
				    (tdmaType == 29))
					btdm_1AntPsTdma(padapter, false, 9);
				else
					btdm_1AntPsTdma(padapter, false, 0);
			}
		}

		/*  change Power Save State */
		btdm_1AntSetPSMode(padapter, bPSEn, smartps, psOption);
	}

	btdm_1AntPsTdma(padapter, bTDMAOn, tdmaType);
}

static void
btdm_1AntSetPSTDMA(struct rtw_adapter *padapter, u8 bPSEn,
		   u8 psOption, u8 bTDMAOn, u8 tdmaType)
{
	_btdm_1AntSetPSTDMA(padapter, bPSEn, 0, psOption, bTDMAOn, tdmaType);
}

static void btdm_1AntWifiParaAdjust(struct rtw_adapter *padapter, u8 bEnable)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct btdm_8723a_1ant *pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm1Ant;

	if (bEnable) {
		pBtdm8723->curWifiPara = 1;
		if (pBtdm8723->preWifiPara != pBtdm8723->curWifiPara)
			BTDM_SetSwPenaltyTxRateAdaptive(padapter, BT_TX_RATE_ADAPTIVE_LOW_PENALTY);
	} else {
		pBtdm8723->curWifiPara = 2;
		if (pBtdm8723->preWifiPara != pBtdm8723->curWifiPara)
			BTDM_SetSwPenaltyTxRateAdaptive(padapter, BT_TX_RATE_ADAPTIVE_NORMAL);
	}

}

static void btdm_1AntPtaParaReload(struct rtw_adapter *padapter)
{
	/*  PTA parameter */
	rtl8723au_write8(padapter, 0x6cc, 0x0);		/*  1-Ant coex */
	rtl8723au_write32(padapter, 0x6c8, 0xffff);	/*  wifi break table */
	rtl8723au_write32(padapter, 0x6c4, 0x55555555);	/*  coex table */

	/*  Antenna switch control parameter */
	rtl8723au_write32(padapter, 0x858, 0xaaaaaaaa);
	if (IS_8723A_A_CUT(GET_HAL_DATA(padapter)->VersionID)) {
		/*  SPDT(connected with TRSW) control by hardware PTA */
		rtl8723au_write32(padapter, 0x870, 0x0);
		rtl8723au_write8(padapter, 0x40, 0x24);
	} else {
		rtl8723au_write8(padapter, 0x40, 0x20);
		/*  set antenna at bt side if ANTSW is software control */
		rtl8723au_write16(padapter, 0x860, 0x210);
		/*  SPDT(connected with TRSW) control by hardware PTA */
		rtl8723au_write32(padapter, 0x870, 0x300);
		/*  ANTSW keep by GNT_BT */
		rtl8723au_write32(padapter, 0x874, 0x22804000);
	}

	/*  coexistence parameters */
	rtl8723au_write8(padapter, 0x778, 0x1);	/*  enable RTK mode PTA */

	/*  BT don't ignore WLAN_Act */
	btdm_SetFwIgnoreWlanAct(padapter, false);
}

/*
 * Return
 *1: upgrade (add WiFi duration time)
 *0: keep
 *-1: downgrade (add BT duration time)
 */
static s8 btdm_1AntTdmaJudgement(struct rtw_adapter *padapter, u8 retry)
{
	struct hal_data_8723a *pHalData;
	struct btdm_8723a_1ant *pBtdm8723;
	static s8 up, dn, m = 1, n = 3, WaitCount;
	s8 ret;

	pHalData = GET_HAL_DATA(padapter);
	pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm1Ant;
	ret = 0;

	if (pBtdm8723->psTdmaMonitorCnt == 0) {
		up = 0;
		dn = 0;
		m = 1;
		n = 3;
		WaitCount = 0;
	} else {
		WaitCount++;
	}

	if (retry == 0) {
	/*  no retry in the last 2-second duration */
		up++;
		dn--;
		if (dn < 0)
			dn = 0;
		if (up >= 3*m) {
			/*  retry = 0 in consecutive 3m*(2s), add WiFi duration */
			ret = 1;

			n = 3;
			up = 0;
			dn = 0;
			WaitCount = 0;
		}
	} else if (retry <= 3) {
		/*  retry<= 3 in the last 2-second duration */
		up--;
		dn++;
		if (up < 0)
			up = 0;

		if (dn == 2) {
			/*  retry<= 3 in consecutive 2*(2s), minus WiFi duration (add BT duration) */
			ret = -1;

			/*  record how many time downgrad WiFi duration */
			if (WaitCount <= 2)
				m++;
			else
				m = 1;
			/*  the max number of m is 20 */
			/*  the longest time of upgrade WiFi duration is 20*3*2s = 120s */
			if (m >= 20)
				m = 20;
			up = 0;
			dn = 0;
			WaitCount = 0;
		}
	} else {
		/*  retry count > 3 */
		/*  retry>3, minus WiFi duration (add BT duration) */
		ret = -1;

		/*  record how many time downgrad WiFi duration */
		if (WaitCount == 1)
			m++;
		else
			m = 1;
		if (m >= 20)
			m = 20;

		up = 0;
		dn = 0;
		WaitCount = 0;
	}
	return ret;
}

static void btdm_1AntTdmaDurationAdjustForACL(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct btdm_8723a_1ant *pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm1Ant;

	if (pBtdm8723->psTdmaGlobalCnt != pBtdm8723->psTdmaMonitorCnt) {
		pBtdm8723->psTdmaMonitorCnt = 0;
		pBtdm8723->psTdmaGlobalCnt = 0;
	}
	if (pBtdm8723->psTdmaMonitorCnt == 0) {
		btdm_1AntSetPSTDMA(padapter, true, 0, true, 2);
		pBtdm8723->psTdmaDuAdjType = 2;
	} else {
		/*  Now we only have 4 level Ps Tdma, */
		/*  if that's not the following 4 level(will changed by wifi scan, dhcp...), */
		/*  then we have to adjust it back to the previous record one. */
		if ((pBtdm8723->curPsTdma != 1) &&
		    (pBtdm8723->curPsTdma != 2) &&
		    (pBtdm8723->curPsTdma != 9) &&
		    (pBtdm8723->curPsTdma != 11)) {
			btdm_1AntSetPSTDMA(padapter, true, 0, true, pBtdm8723->psTdmaDuAdjType);
		} else {
			s32 judge = 0;

			judge = btdm_1AntTdmaJudgement(padapter, pHalData->bt_coexist.halCoex8723.btRetryCnt);
			if (judge == -1) {
				if (pBtdm8723->curPsTdma == 1) {
					/*  Decrease WiFi duration for high BT retry */
					if (pHalData->bt_coexist.halCoex8723.btInfoExt)
						pBtdm8723->psTdmaDuAdjType = 9;
					else
						pBtdm8723->psTdmaDuAdjType = 2;
					btdm_1AntSetPSTDMA(padapter, true, 0, true, pBtdm8723->psTdmaDuAdjType);
				} else if (pBtdm8723->curPsTdma == 2) {
					btdm_1AntSetPSTDMA(padapter, true, 0, true, 9);
					pBtdm8723->psTdmaDuAdjType = 9;
				} else if (pBtdm8723->curPsTdma == 9) {
					btdm_1AntSetPSTDMA(padapter, true, 0, true, 11);
					pBtdm8723->psTdmaDuAdjType = 11;
				}
			} else if (judge == 1) {
				if (pBtdm8723->curPsTdma == 11) {
					btdm_1AntSetPSTDMA(padapter, true, 0, true, 9);
					pBtdm8723->psTdmaDuAdjType = 9;
				} else if (pBtdm8723->curPsTdma == 9) {
					if (pHalData->bt_coexist.halCoex8723.btInfoExt)
						pBtdm8723->psTdmaDuAdjType = 9;
					else
						pBtdm8723->psTdmaDuAdjType = 2;
					btdm_1AntSetPSTDMA(padapter, true, 0, true, pBtdm8723->psTdmaDuAdjType);
				} else if (pBtdm8723->curPsTdma == 2) {
					if (pHalData->bt_coexist.halCoex8723.btInfoExt)
						pBtdm8723->psTdmaDuAdjType = 9;
					else
						pBtdm8723->psTdmaDuAdjType = 1;
					btdm_1AntSetPSTDMA(padapter, true, 0, true, pBtdm8723->psTdmaDuAdjType);
				}
			}
		}
		RTPRINT(FBT, BT_TRACE,
			("[BTCoex], ACL current TDMA(%s, %d)\n",
			(pBtdm8723->bCurPsTdmaOn ? "ON" : "OFF"), pBtdm8723->curPsTdma));
	}
	pBtdm8723->psTdmaMonitorCnt++;
}

static void btdm_1AntCoexProcessForWifiConnect(struct rtw_adapter *padapter)
{
	struct mlme_priv *pmlmepriv;
	struct hal_data_8723a *pHalData;
	struct bt_coexist_8723a *pBtCoex;
	struct btdm_8723a_1ant *pBtdm8723;
	u8 BtState;

	pmlmepriv = &padapter->mlmepriv;
	pHalData = GET_HAL_DATA(padapter);
	pBtCoex = &pHalData->bt_coexist.halCoex8723;
	pBtdm8723 = &pBtCoex->btdm1Ant;
	BtState = pBtCoex->c2hBtInfo;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], WiFi is %s\n",
				BTDM_IsWifiBusy(padapter)?"Busy":"IDLE"));
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT is %s\n",
				BtStateString[BtState]));

	padapter->pwrctrlpriv.btcoex_rfon = false;

	if (!BTDM_IsWifiBusy(padapter) &&
	    !check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) &&
	    (BtState == BT_INFO_STATE_NO_CONNECTION ||
	     BtState == BT_INFO_STATE_CONNECT_IDLE)) {
		switch (BtState) {
		case BT_INFO_STATE_NO_CONNECTION:
			_btdm_1AntSetPSTDMA(padapter, true, 2, 0x26, false, 9);
			break;
		case BT_INFO_STATE_CONNECT_IDLE:
			_btdm_1AntSetPSTDMA(padapter, true, 2, 0x26, false, 0);
			break;
		}
	} else {
		switch (BtState) {
		case BT_INFO_STATE_NO_CONNECTION:
		case BT_INFO_STATE_CONNECT_IDLE:
			/*  WiFi is Busy */
			btdm_1AntSetPSTDMA(padapter, false, 0, true, 5);
			rtl8723au_write32(padapter, 0x6c0, 0x5a5a5a5a);
			rtl8723au_write32(padapter, 0x6c4, 0x5a5a5a5a);
			break;
		case BT_INFO_STATE_ACL_INQ_OR_PAG:
			RTPRINT(FBT, BT_TRACE,
				("[BTCoex], BT PROFILE is "
				 "BT_INFO_STATE_ACL_INQ_OR_PAG\n"));
		case BT_INFO_STATE_INQ_OR_PAG:
			padapter->pwrctrlpriv.btcoex_rfon = true;
			btdm_1AntSetPSTDMA(padapter, true, 0, true, 30);
			break;
		case BT_INFO_STATE_SCO_ONLY_BUSY:
		case BT_INFO_STATE_ACL_SCO_BUSY:
			if (true == pBtCoex->bC2hBtInquiryPage)
				btdm_1AntSetPSTDMA(padapter, false, 0,
						   true, 32);
			else {
#ifdef BTCOEX_CMCC_TEST
				btdm_1AntSetPSTDMA(padapter, false, 0,
						   true, 23);
#else /*  !BTCOEX_CMCC_TEST */
				btdm_1AntSetPSTDMA(padapter, false, 0,
						   false, 8);
				rtl8723au_write32(padapter, 0x6c0, 0x5a5a5a5a);
				rtl8723au_write32(padapter, 0x6c4, 0x5a5a5a5a);
#endif /*  !BTCOEX_CMCC_TEST */
			}
			break;
		case BT_INFO_STATE_ACL_ONLY_BUSY:
			padapter->pwrctrlpriv.btcoex_rfon = true;
			if (pBtCoex->c2hBtProfile == BT_INFO_HID) {
				RTPRINT(FBT, BT_TRACE,
					("[BTCoex], BT PROFILE is HID\n"));
				btdm_1AntSetPSTDMA(padapter, true, 0, true, 31);
			} else if (pBtCoex->c2hBtProfile == BT_INFO_FTP) {
				RTPRINT(FBT, BT_TRACE,
					("[BTCoex], BT PROFILE is FTP/OPP\n"));
				btdm_1AntSetPSTDMA(padapter, true, 0, true, 3);
			} else if (pBtCoex->c2hBtProfile == (BT_INFO_A2DP|BT_INFO_FTP)) {
				RTPRINT(FBT, BT_TRACE,
					("[BTCoex], BT PROFILE is A2DP_FTP\n"));
				btdm_1AntSetPSTDMA(padapter, true, 0, true, 11);
			} else {
				if (pBtCoex->c2hBtProfile == BT_INFO_A2DP)
					RTPRINT(FBT, BT_TRACE,
						("[BTCoex], BT PROFILE is "
						 "A2DP\n"));
				else
					RTPRINT(FBT, BT_TRACE,
						("[BTCoex], BT PROFILE is "
						 "UNKNOWN(0x%02X)! Use A2DP "
						 "Profile\n",
						 pBtCoex->c2hBtProfile));
				btdm_1AntTdmaDurationAdjustForACL(padapter);
			}
			break;
		}
	}

	pBtdm8723->psTdmaGlobalCnt++;
}

static void
btdm_1AntUpdateHalRAMask(struct rtw_adapter *padapter, u32 mac_id, u32 filter)
{
	u8 init_rate = 0;
	u8 raid, arg;
	u32 mask;
	u8 shortGIrate = false;
	int supportRateNum = 0;
	struct sta_info	*psta;
	struct hal_data_8723a *pHalData;
	struct dm_priv *pdmpriv;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;
	struct wlan_bssid_ex *cur_network;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], %s, MACID =%d, filter = 0x%08x!!\n",
				__func__, mac_id, filter));

	pHalData = GET_HAL_DATA(padapter);
	pdmpriv = &pHalData->dmpriv;
	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;
	cur_network = &pmlmeinfo->network;

	if (mac_id >= NUM_STA) { /* CAM_SIZE */
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], %s, MACID =%d illegal!!\n",
					__func__, mac_id));
		return;
	}

	psta = pmlmeinfo->FW_sta_info[mac_id].psta;
	if (!psta) {
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], %s, Can't find station!!\n",
					__func__));
		return;
	}

	raid = psta->raid;

	switch (mac_id) {
	case 0:/*  for infra mode */
		supportRateNum =
			rtw_get_rateset_len23a(cur_network->SupportedRates);
		mask = update_supported_rate23a(cur_network->SupportedRates,
						supportRateNum);
		mask |= (pmlmeinfo->HT_enable) ?
			update_MSC_rate23a(&pmlmeinfo->ht_cap):0;
		if (support_short_GI23a(padapter, &pmlmeinfo->ht_cap))
			shortGIrate = true;
		break;
	case 1:/* for broadcast/multicast */
		supportRateNum = rtw_get_rateset_len23a(
			pmlmeinfo->FW_sta_info[mac_id].SupportedRates);
		mask = update_basic_rate23a(cur_network->SupportedRates,
					    supportRateNum);
		break;
	default: /* for each sta in IBSS */
		supportRateNum = rtw_get_rateset_len23a(
			pmlmeinfo->FW_sta_info[mac_id].SupportedRates);
		mask = update_supported_rate23a(cur_network->SupportedRates,
						supportRateNum);
		break;
	}
	mask |= ((raid<<28)&0xf0000000);
	mask &= 0xffffffff;
	mask &= ~filter;
	init_rate = get_highest_rate_idx23a(mask)&0x3f;

	arg = mac_id&0x1f;/* MACID */
	arg |= BIT(7);
	if (true == shortGIrate)
		arg |= BIT(5);

	RTPRINT(FBT, BT_TRACE,
		("[BTCoex], Update FW RAID entry, MASK = 0x%08x, "
		 "arg = 0x%02x\n", mask, arg));

	rtl8723a_set_raid_cmd(padapter, mask, arg);

	psta->init_rate = init_rate;
	pdmpriv->INIDATA_RATE[mac_id] = init_rate;
}

static void
btdm_1AntUpdateHalRAMaskForSCO(struct rtw_adapter *padapter, u8 forceUpdate)
{
	struct btdm_8723a_1ant *pBtdm8723;
	struct sta_priv *pstapriv;
	struct wlan_bssid_ex *cur_network;
	struct sta_info *psta;
	u32 macid;
	u32 filter = 0;

	pBtdm8723 = &GET_HAL_DATA(padapter)->bt_coexist.halCoex8723.btdm1Ant;

	if (pBtdm8723->bRAChanged == true && forceUpdate == false)
		return;

	pstapriv = &padapter->stapriv;
	cur_network = &padapter->mlmeextpriv.mlmext_info.network;
	psta = rtw_get_stainfo23a(pstapriv, cur_network->MacAddress);
	macid = psta->mac_id;

	filter |= BIT(_1M_RATE_);
	filter |= BIT(_2M_RATE_);
	filter |= BIT(_5M_RATE_);
	filter |= BIT(_11M_RATE_);
	filter |= BIT(_6M_RATE_);
	filter |= BIT(_9M_RATE_);

	btdm_1AntUpdateHalRAMask(padapter, macid, filter);

	pBtdm8723->bRAChanged = true;
}

static void btdm_1AntRecoverHalRAMask(struct rtw_adapter *padapter)
{
	struct btdm_8723a_1ant *pBtdm8723;
	struct sta_priv *pstapriv;
	struct wlan_bssid_ex *cur_network;
	struct sta_info *psta;

	pBtdm8723 = &GET_HAL_DATA(padapter)->bt_coexist.halCoex8723.btdm1Ant;

	if (pBtdm8723->bRAChanged == false)
		return;

	pstapriv = &padapter->stapriv;
	cur_network = &padapter->mlmeextpriv.mlmext_info.network;
	psta = rtw_get_stainfo23a(pstapriv, cur_network->MacAddress);

	Update_RA_Entry23a(padapter, psta);

	pBtdm8723->bRAChanged = false;
}

static void
btdm_1AntBTStateChangeHandler(struct rtw_adapter *padapter,
			      enum bt_state_1ant oldState,
			      enum bt_state_1ant newState)
{
	struct hal_data_8723a *phaldata;
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT state change, %s => %s\n",
				BtStateString[oldState],
				BtStateString[newState]));

	/*  BT default ignore wlan active, */
	/*  WiFi MUST disable this when BT is enable */
	if (newState > BT_INFO_STATE_DISABLED)
		btdm_SetFwIgnoreWlanAct(padapter, false);

	if ((check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE)) &&
	    (BTDM_IsWifiConnectionExist(padapter))) {
		if ((newState == BT_INFO_STATE_SCO_ONLY_BUSY) ||
		    (newState == BT_INFO_STATE_ACL_SCO_BUSY)) {
			btdm_1AntUpdateHalRAMaskForSCO(padapter, false);
		} else {
			/*  Recover original RA setting */
			btdm_1AntRecoverHalRAMask(padapter);
		}
	} else {
		phaldata = GET_HAL_DATA(padapter);
		phaldata->bt_coexist.halCoex8723.btdm1Ant.bRAChanged = false;
	}

	if (oldState == newState)
		return;

	if (oldState == BT_INFO_STATE_ACL_ONLY_BUSY) {
		struct hal_data_8723a *Hal = GET_HAL_DATA(padapter);
		Hal->bt_coexist.halCoex8723.btdm1Ant.psTdmaMonitorCnt = 0;
		Hal->bt_coexist.halCoex8723.btdm1Ant.psTdmaMonitorCntForSCO = 0;
	}

	if ((oldState == BT_INFO_STATE_SCO_ONLY_BUSY) ||
	    (oldState == BT_INFO_STATE_ACL_SCO_BUSY)) {
		struct hal_data_8723a *Hal = GET_HAL_DATA(padapter);
		Hal->bt_coexist.halCoex8723.btdm1Ant.psTdmaMonitorCntForSCO = 0;
	}

	/*  Active 2Ant mechanism when BT Connected */
	if ((oldState == BT_INFO_STATE_DISABLED) ||
	    (oldState == BT_INFO_STATE_NO_CONNECTION)) {
		if ((newState != BT_INFO_STATE_DISABLED) &&
		    (newState != BT_INFO_STATE_NO_CONNECTION)) {
			BTDM_SetSwRfRxLpfCorner(padapter,
						BT_RF_RX_LPF_CORNER_SHRINK);
			BTDM_AGCTable(padapter, BT_AGCTABLE_ON);
			BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
		}
	} else {
		if ((newState == BT_INFO_STATE_DISABLED) ||
		    (newState == BT_INFO_STATE_NO_CONNECTION)) {
			BTDM_SetSwRfRxLpfCorner(padapter,
						BT_RF_RX_LPF_CORNER_RESUME);
			BTDM_AGCTable(padapter, BT_AGCTABLE_OFF);
			BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_OFF);
		}
	}
}

static void btdm_1AntBtCoexistHandler(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData;
	struct bt_coexist_8723a *pBtCoex8723;
	struct btdm_8723a_1ant *pBtdm8723;

	pHalData = GET_HAL_DATA(padapter);
	pBtCoex8723 = &pHalData->bt_coexist.halCoex8723;
	pBtdm8723 = &pBtCoex8723->btdm1Ant;
	padapter->pwrctrlpriv.btcoex_rfon = false;
	if (!rtl8723a_BT_enabled(padapter)) {
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT is disabled\n"));

		if (BTDM_IsWifiConnectionExist(padapter)) {
			RTPRINT(FBT, BT_TRACE,
				("[BTCoex], wifi is connected\n"));

			if (BTDM_IsWifiBusy(padapter)) {
				RTPRINT(FBT, BT_TRACE,
					("[BTCoex], Wifi is busy\n"));
				btdm_1AntSetPSTDMA(padapter, false, 0,
						   false, 9);
			} else {
				RTPRINT(FBT, BT_TRACE,
					("[BTCoex], Wifi is idle\n"));
				_btdm_1AntSetPSTDMA(padapter, true, 2, 1,
						    false, 9);
			}
		} else {
			RTPRINT(FBT, BT_TRACE,
				("[BTCoex], wifi is disconnected\n"));

			btdm_1AntSetPSTDMA(padapter, false, 0, false, 9);
		}
	} else {
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT is enabled\n"));

		if (BTDM_IsWifiConnectionExist(padapter)) {
			RTPRINT(FBT, BT_TRACE,
				("[BTCoex], wifi is connected\n"));

			btdm_1AntWifiParaAdjust(padapter, true);
			btdm_1AntCoexProcessForWifiConnect(padapter);
		} else {
			RTPRINT(FBT, BT_TRACE,
				("[BTCoex], wifi is disconnected\n"));

			/*  Antenna switch at BT side(0x870 = 0x300,
			    0x860 = 0x210) after PSTDMA off */
			btdm_1AntWifiParaAdjust(padapter, false);
			btdm_1AntSetPSTDMA(padapter, false, 0, false, 0);
		}
	}

	btdm_1AntBTStateChangeHandler(padapter, pBtCoex8723->prec2hBtInfo,
				      pBtCoex8723->c2hBtInfo);
	pBtCoex8723->prec2hBtInfo = pBtCoex8723->c2hBtInfo;
}

void BTDM_1AntSignalCompensation(struct rtw_adapter *padapter,
				 u8 *rssi_wifi, u8 *rssi_bt)
{
	struct hal_data_8723a *pHalData;
	struct btdm_8723a_1ant *pBtdm8723;
	u8 RSSI_WiFi_Cmpnstn, RSSI_BT_Cmpnstn;

	pHalData = GET_HAL_DATA(padapter);
	pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm1Ant;
	RSSI_WiFi_Cmpnstn = 0;
	RSSI_BT_Cmpnstn = 0;

	switch (pBtdm8723->curPsTdma) {
	case 1: /*  WiFi 52ms */
		RSSI_WiFi_Cmpnstn = 11; /*  22*0.48 */
		break;
	case 2: /*  WiFi 36ms */
		RSSI_WiFi_Cmpnstn = 14; /*  22*0.64 */
		break;
	case 9: /*  WiFi 20ms */
		RSSI_WiFi_Cmpnstn = 18; /*  22*0.80 */
		break;
	case 11: /*  WiFi 10ms */
		RSSI_WiFi_Cmpnstn = 20; /*  22*0.90 */
		break;
	case 4: /*  WiFi 21ms */
		RSSI_WiFi_Cmpnstn = 17; /*  22*0.79 */
		break;
	case 16: /*  WiFi 24ms */
		RSSI_WiFi_Cmpnstn = 18; /*  22*0.76 */
		break;
	case 18: /*  WiFi 37ms */
		RSSI_WiFi_Cmpnstn = 14; /*  22*0.64 */
		break;
	case 23: /* Level-1, Antenna switch to BT at all time */
	case 24: /* Level-2, Antenna switch to BT at all time */
	case 25: /* Level-3a, Antenna switch to BT at all time */
	case 26: /* Level-3b, Antenna switch to BT at all time */
	case 27: /* Level-3b, Antenna switch to BT at all time */
	case 33: /* BT SCO & WiFi site survey */
		RSSI_WiFi_Cmpnstn = 22;
		break;
	default:
		break;
	}

	if (rssi_wifi && RSSI_WiFi_Cmpnstn) {
		RTPRINT(FBT, BT_TRACE,
			("[BTCoex], 1AntSgnlCmpnstn, case %d, WiFiCmpnstn "
			 "=%d(%d => %d)\n", pBtdm8723->curPsTdma,
			 RSSI_WiFi_Cmpnstn, *rssi_wifi,
			 *rssi_wifi+RSSI_WiFi_Cmpnstn));
		*rssi_wifi += RSSI_WiFi_Cmpnstn;
	}

	if (rssi_bt && RSSI_BT_Cmpnstn) {
		RTPRINT(FBT, BT_TRACE,
			("[BTCoex], 1AntSgnlCmpnstn, case %d, BTCmpnstn "
			 "=%d(%d => %d)\n", pBtdm8723->curPsTdma,
			 RSSI_BT_Cmpnstn, *rssi_bt, *rssi_bt+RSSI_BT_Cmpnstn));
		*rssi_bt += RSSI_BT_Cmpnstn;
	}
}

static void BTDM_1AntParaInit(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData;
	struct bt_coexist_8723a *pBtCoex;
	struct btdm_8723a_1ant *pBtdm8723;

	pHalData = GET_HAL_DATA(padapter);
	pBtCoex = &pHalData->bt_coexist.halCoex8723;
	pBtdm8723 = &pBtCoex->btdm1Ant;

	/*  Enable counter statistics */
	rtl8723au_write8(padapter, 0x76e, 0x4);
	btdm_1AntPtaParaReload(padapter);

	pBtdm8723->wifiRssiThresh = 48;

	pBtdm8723->bWiFiHalt = false;
	pBtdm8723->bRAChanged = false;

	if ((pBtCoex->c2hBtInfo != BT_INFO_STATE_DISABLED) &&
	    (pBtCoex->c2hBtInfo != BT_INFO_STATE_NO_CONNECTION)) {
		BTDM_SetSwRfRxLpfCorner(padapter, BT_RF_RX_LPF_CORNER_SHRINK);
		BTDM_AGCTable(padapter, BT_AGCTABLE_ON);
		BTDM_BBBackOffLevel(padapter, BT_BB_BACKOFF_ON);
	}
}

static void BTDM_1AntForHalt(struct rtw_adapter *padapter)
{
	RTPRINT(FBT, BT_TRACE, ("\n[BTCoex], 1Ant for halt\n"));

	GET_HAL_DATA(padapter)->bt_coexist.halCoex8723.btdm1Ant.bWiFiHalt =
		true;

	btdm_1AntWifiParaAdjust(padapter, false);

	/*  don't use btdm_1AntSetPSTDMA() here */
	/*  it will call rtw_set_ps_mode23a() and request pwrpriv->lock. */
	/*  This will lead to deadlock, if this function is called in IPS */
	/*  Lucas@20130205 */
	btdm_1AntPsTdma(padapter, false, 0);

	btdm_SetFwIgnoreWlanAct(padapter, true);
}

static void BTDM_1AntLpsLeave(struct rtw_adapter *padapter)
{
	RTPRINT(FBT, BT_TRACE, ("\n[BTCoex], 1Ant for LPS Leave\n"));

	/*  Prevent from entering LPS again */
	GET_HAL_DATA(padapter)->bt_coexist.halCoex8723.btdm1Ant.bWiFiHalt =
		true;

	btdm_1AntSetPSTDMA(padapter, false, 0, false, 8);
/*btdm_1AntPsTdma(padapter, false, 8); */
}

static void BTDM_1AntWifiAssociateNotify(struct rtw_adapter *padapter, u8 type)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	RTPRINT(FBT, BT_TRACE,
		("\n[BTCoex], 1Ant for associate, type =%d\n", type));

	if (type) {
		rtl8723a_CheckAntenna_Selection(padapter);
		if (!rtl8723a_BT_enabled(padapter))
			btdm_1AntSetPSTDMA(padapter, false, 0, false, 9);
		else {
			struct bt_coexist_8723a *pBtCoex;
			u8 BtState;

			pBtCoex = &pHalData->bt_coexist.halCoex8723;
			BtState = pBtCoex->c2hBtInfo;

			btdm_1AntTSFSwitch(padapter, true);

			if (BtState == BT_INFO_STATE_NO_CONNECTION ||
			    BtState == BT_INFO_STATE_CONNECT_IDLE) {
				btdm_1AntSetPSTDMA(padapter, false, 0,
						   true, 28);
			} else if (BtState == BT_INFO_STATE_SCO_ONLY_BUSY ||
				   BtState == BT_INFO_STATE_ACL_SCO_BUSY) {
				btdm_1AntSetPSTDMA(padapter, false, 0,
						   false, 8);
				rtl8723au_write32(padapter, 0x6c0, 0x5a5a5a5a);
				rtl8723au_write32(padapter, 0x6c4, 0x5a5a5a5a);
			} else if (BtState == BT_INFO_STATE_ACL_ONLY_BUSY ||
				   BtState == BT_INFO_STATE_ACL_INQ_OR_PAG) {
				if (pBtCoex->c2hBtProfile == BT_INFO_HID)
					btdm_1AntSetPSTDMA(padapter, false, 0,
							   true, 35);
				else
					btdm_1AntSetPSTDMA(padapter, false, 0,
							   true, 29);
			}
		}
	} else {
		if (!rtl8723a_BT_enabled(padapter)) {
			if (!BTDM_IsWifiConnectionExist(padapter)) {
				btdm_1AntPsTdma(padapter, false, 0);
				btdm_1AntTSFSwitch(padapter, false);
			}
		}

		btdm_1AntBtCoexistHandler(padapter);
	}
}

static void
BTDM_1AntMediaStatusNotify(struct rtw_adapter *padapter,
			   enum rt_media_status mstatus)
{
	struct bt_coexist_8723a *pBtCoex;

	pBtCoex = &GET_HAL_DATA(padapter)->bt_coexist.halCoex8723;

	RTPRINT(FBT, BT_TRACE,
		("\n\n[BTCoex]******************************\n"));
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], MediaStatus, WiFi %s !!\n",
			mstatus == RT_MEDIA_CONNECT?"CONNECT":"DISCONNECT"));
	RTPRINT(FBT, BT_TRACE, ("[BTCoex]******************************\n"));

	if (RT_MEDIA_CONNECT == mstatus) {
		if (check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE)) {
			if (pBtCoex->c2hBtInfo == BT_INFO_STATE_SCO_ONLY_BUSY ||
			    pBtCoex->c2hBtInfo == BT_INFO_STATE_ACL_SCO_BUSY)
				btdm_1AntUpdateHalRAMaskForSCO(padapter, true);
		}

		padapter->pwrctrlpriv.DelayLPSLastTimeStamp = jiffies;
		BTDM_1AntForDhcp(padapter);
	} else {
		/* DBG_8723A("%s rtl8723a_DeinitAntenna_Selection\n",
		   __func__); */
		rtl8723a_DeinitAntenna_Selection(padapter);
		btdm_1AntBtCoexistHandler(padapter);
		pBtCoex->btdm1Ant.bRAChanged = false;
	}
}

void BTDM_1AntForDhcp(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData;
	u8 BtState;
	struct bt_coexist_8723a *pBtCoex;
	struct btdm_8723a_1ant *pBtdm8723;

	pHalData = GET_HAL_DATA(padapter);
	pBtCoex = &pHalData->bt_coexist.halCoex8723;
	BtState = pBtCoex->c2hBtInfo;
	pBtdm8723 = &pBtCoex->btdm1Ant;

	RTPRINT(FBT, BT_TRACE, ("\n[BTCoex], 1Ant for DHCP\n"));
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], 1Ant for DHCP, WiFi is %s\n",
				BTDM_IsWifiBusy(padapter)?"Busy":"IDLE"));
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], 1Ant for DHCP, %s\n",
				BtStateString[BtState]));

	BTDM_1AntWifiAssociateNotify(padapter, true);
}

static void BTDM_1AntWifiScanNotify(struct rtw_adapter *padapter, u8 scanType)
{
	struct hal_data_8723a *pHalData;
	u8 BtState;
	struct bt_coexist_8723a *pBtCoex;
	struct btdm_8723a_1ant *pBtdm8723;

	pHalData = GET_HAL_DATA(padapter);
	BtState = pHalData->bt_coexist.halCoex8723.c2hBtInfo;
	pBtCoex = &pHalData->bt_coexist.halCoex8723;
	pBtdm8723 = &pBtCoex->btdm1Ant;

	RTPRINT(FBT, BT_TRACE, ("\n[BTCoex], 1Ant for wifi scan =%d!!\n",
				scanType));
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], 1Ant for wifi scan, WiFi is %s\n",
				BTDM_IsWifiBusy(padapter)?"Busy":"IDLE"));
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], 1Ant for wifi scan, %s\n",
				BtStateString[BtState]));

	if (scanType) {
		rtl8723a_CheckAntenna_Selection(padapter);
		if (!rtl8723a_BT_enabled(padapter)) {
			btdm_1AntSetPSTDMA(padapter, false, 0, false, 9);
		} else if (BTDM_IsWifiConnectionExist(padapter) == false) {
			BTDM_1AntWifiAssociateNotify(padapter, true);
		} else {
			if ((BtState == BT_INFO_STATE_SCO_ONLY_BUSY) ||
			    (BtState == BT_INFO_STATE_ACL_SCO_BUSY)) {
				if (pBtCoex->bC2hBtInquiryPage) {
					btdm_1AntSetPSTDMA(padapter, false, 0,
							   true, 32);
				} else {
					padapter->pwrctrlpriv.btcoex_rfon =
						true;
					btdm_1AntSetPSTDMA(padapter, true, 0,
							   true, 33);
				}
			} else if (true == pBtCoex->bC2hBtInquiryPage) {
				padapter->pwrctrlpriv.btcoex_rfon = true;
				btdm_1AntSetPSTDMA(padapter, true, 0, true, 30);
			} else if (BtState == BT_INFO_STATE_ACL_ONLY_BUSY) {
				padapter->pwrctrlpriv.btcoex_rfon = true;
				if (pBtCoex->c2hBtProfile == BT_INFO_HID)
					btdm_1AntSetPSTDMA(padapter, true, 0,
							   true, 34);
				else
					btdm_1AntSetPSTDMA(padapter, true, 0,
							   true, 4);
			} else {
				padapter->pwrctrlpriv.btcoex_rfon = true;
				btdm_1AntSetPSTDMA(padapter, true, 0, true, 5);
			}
		}

		btdm_NotifyFwScan(padapter, 1);
	} else {
		/*  WiFi_Finish_Scan */
		btdm_NotifyFwScan(padapter, 0);
		btdm_1AntBtCoexistHandler(padapter);
	}
}

static void BTDM_1AntFwC2hBtInfo8723A(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData;
	struct bt_30info *pBTInfo;
	struct bt_mgnt *pBtMgnt;
	struct bt_coexist_8723a *pBtCoex;
	u8 u1tmp, btState;

	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;
	pBtCoex = &pHalData->bt_coexist.halCoex8723;

	u1tmp = pBtCoex->c2hBtInfoOriginal;
	/*  sco BUSY bit is not used on voice over PCM platform */
	btState = u1tmp & 0xF;
	pBtCoex->c2hBtProfile = u1tmp & 0xE0;

	/*  default set bt to idle state. */
	pBtMgnt->ExtConfig.bBTBusy = false;
	pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_IDLE;

	/*  check BIT2 first ==> check if bt is under inquiry or page scan */
	if (btState & BIT(2))
		pBtCoex->bC2hBtInquiryPage = true;
	else
		pBtCoex->bC2hBtInquiryPage = false;
	btState &= ~BIT(2);

	if (!(btState & BIT(0)))
		pBtCoex->c2hBtInfo = BT_INFO_STATE_NO_CONNECTION;
	else {
		if (btState == 0x1)
			pBtCoex->c2hBtInfo = BT_INFO_STATE_CONNECT_IDLE;
		else if (btState == 0x9) {
			if (pBtCoex->bC2hBtInquiryPage == true)
				pBtCoex->c2hBtInfo =
					BT_INFO_STATE_ACL_INQ_OR_PAG;
			else
				pBtCoex->c2hBtInfo =
					BT_INFO_STATE_ACL_ONLY_BUSY;
			pBtMgnt->ExtConfig.bBTBusy = true;
		} else if (btState == 0x3) {
			pBtCoex->c2hBtInfo = BT_INFO_STATE_SCO_ONLY_BUSY;
			pBtMgnt->ExtConfig.bBTBusy = true;
		} else if (btState == 0xb) {
			pBtCoex->c2hBtInfo = BT_INFO_STATE_ACL_SCO_BUSY;
			pBtMgnt->ExtConfig.bBTBusy = true;
		} else
			pBtCoex->c2hBtInfo = BT_INFO_STATE_MAX;
		if (pBtMgnt->ExtConfig.bBTBusy)
			pHalData->bt_coexist.CurrentState &=
				~BT_COEX_STATE_BT_IDLE;
	}

	if (BT_INFO_STATE_NO_CONNECTION == pBtCoex->c2hBtInfo ||
	    BT_INFO_STATE_CONNECT_IDLE == pBtCoex->c2hBtInfo) {
		if (pBtCoex->bC2hBtInquiryPage)
			pBtCoex->c2hBtInfo = BT_INFO_STATE_INQ_OR_PAG;
	}

	RTPRINT(FBT, BT_TRACE, ("[BTC2H], %s(%d)\n",
			BtStateString[pBtCoex->c2hBtInfo], pBtCoex->c2hBtInfo));

	if (pBtCoex->c2hBtProfile != BT_INFO_HID)
		pBtCoex->c2hBtProfile &= ~BT_INFO_HID;
}

void BTDM_1AntBtCoexist8723A(struct rtw_adapter *padapter)
{
	struct mlme_priv *pmlmepriv;
	struct hal_data_8723a *pHalData;
	unsigned long delta_time;

	pmlmepriv = &padapter->mlmepriv;
	pHalData = GET_HAL_DATA(padapter);

	if (check_fwstate(pmlmepriv, WIFI_SITE_MONITOR)) {
		/*  already done in BTDM_1AntForScan() */
		RTPRINT(FBT, BT_TRACE,
			("[BTCoex], wifi is under scan progress!!\n"));
		return;
	}

	if (check_fwstate(pmlmepriv, WIFI_UNDER_LINKING)) {
		RTPRINT(FBT, BT_TRACE,
			("[BTCoex], wifi is under link progress!!\n"));
		return;
	}

	/*  under DHCP(Special packet) */
	delta_time = jiffies - padapter->pwrctrlpriv.DelayLPSLastTimeStamp;
	delta_time = jiffies_to_msecs(delta_time);
	if (delta_time < 500) {
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], wifi is under DHCP "
					"progress(%li ms)!!\n", delta_time));
		return;
	}

	BTDM_CheckWiFiState(padapter);

	btdm_1AntBtCoexistHandler(padapter);
}

/*  ===== End of sync from SD7 driver HAL/BTCoexist/HalBtc87231Ant.c ===== */

/*  ===== Below this line is sync from SD7 driver HAL/BTCoexist/HalBtc87232Ant.c ===== */

/*  local function start with btdm_ */
static u8 btdm_ActionAlgorithm(struct rtw_adapter *padapter)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct btdm_8723a_2ant *pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm2Ant;
	u8 bScoExist = false, bBtLinkExist = false, bBtHsModeExist = false;
	u8 algorithm = BT_2ANT_COEX_ALGO_UNDEFINED;

	if (pBtMgnt->ExtConfig.NumberOfHandle)
		bBtLinkExist = true;
	if (pBtMgnt->ExtConfig.NumberOfSCO)
		bScoExist = true;
	if (BT_HsConnectionEstablished(padapter))
		bBtHsModeExist = true;

	/*  here we get BT status first */
	/*  1) initialize */
	pBtdm8723->btStatus = BT_2ANT_BT_STATUS_IDLE;

	if ((bScoExist) || (bBtHsModeExist) ||
	    (BTHCI_CheckProfileExist(padapter, BT_PROFILE_HID))) {
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], SCO or HID or HS exists, set BT non-idle !!!\n"));
		pBtdm8723->btStatus = BT_2ANT_BT_STATUS_NON_IDLE;
	} else {
		/*  A2dp profile */
		if ((pBtMgnt->ExtConfig.NumberOfHandle == 1) &&
		    (BTHCI_CheckProfileExist(padapter, BT_PROFILE_A2DP))) {
			if (BTDM_BtTxRxCounterL(padapter) < 100) {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], A2DP, low priority tx+rx < 100, set BT connected-idle!!!\n"));
				pBtdm8723->btStatus = BT_2ANT_BT_STATUS_CONNECTED_IDLE;
			} else {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], A2DP, low priority tx+rx >= 100, set BT non-idle!!!\n"));
				pBtdm8723->btStatus = BT_2ANT_BT_STATUS_NON_IDLE;
			}
		}
		/*  Pan profile */
		if ((pBtMgnt->ExtConfig.NumberOfHandle == 1) &&
		    (BTHCI_CheckProfileExist(padapter, BT_PROFILE_PAN))) {
			if (BTDM_BtTxRxCounterL(padapter) < 600) {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], PAN, low priority tx+rx < 600, set BT connected-idle!!!\n"));
				pBtdm8723->btStatus = BT_2ANT_BT_STATUS_CONNECTED_IDLE;
			} else {
				if (pHalData->bt_coexist.halCoex8723.lowPriorityTx) {
					if ((pHalData->bt_coexist.halCoex8723.lowPriorityRx /
					    pHalData->bt_coexist.halCoex8723.lowPriorityTx) > 9) {
						RTPRINT(FBT, BT_TRACE, ("[BTCoex], PAN, low priority rx/tx > 9, set BT connected-idle!!!\n"));
						pBtdm8723->btStatus = BT_2ANT_BT_STATUS_CONNECTED_IDLE;
					}
				}
			}
			if (BT_2ANT_BT_STATUS_CONNECTED_IDLE != pBtdm8723->btStatus) {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], PAN, set BT non-idle!!!\n"));
				pBtdm8723->btStatus = BT_2ANT_BT_STATUS_NON_IDLE;
			}
		}
		/*  Pan+A2dp profile */
		if ((pBtMgnt->ExtConfig.NumberOfHandle == 2) &&
		    (BTHCI_CheckProfileExist(padapter, BT_PROFILE_A2DP)) &&
		    (BTHCI_CheckProfileExist(padapter, BT_PROFILE_PAN))) {
			if (BTDM_BtTxRxCounterL(padapter) < 600) {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], PAN+A2DP, low priority tx+rx < 600, set BT connected-idle!!!\n"));
				pBtdm8723->btStatus = BT_2ANT_BT_STATUS_CONNECTED_IDLE;
			} else {
				if (pHalData->bt_coexist.halCoex8723.lowPriorityTx) {
					if ((pHalData->bt_coexist.halCoex8723.lowPriorityRx /
					    pHalData->bt_coexist.halCoex8723.lowPriorityTx) > 9) {
						RTPRINT(FBT, BT_TRACE, ("[BTCoex], PAN+A2DP, low priority rx/tx > 9, set BT connected-idle!!!\n"));
						pBtdm8723->btStatus = BT_2ANT_BT_STATUS_CONNECTED_IDLE;
					}
				}
			}
			if (BT_2ANT_BT_STATUS_CONNECTED_IDLE != pBtdm8723->btStatus) {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], PAN+A2DP, set BT non-idle!!!\n"));
				pBtdm8723->btStatus = BT_2ANT_BT_STATUS_NON_IDLE;
			}
		}
	}
	if (BT_2ANT_BT_STATUS_IDLE != pBtdm8723->btStatus)
		pBtMgnt->ExtConfig.bBTBusy = true;
	else
		pBtMgnt->ExtConfig.bBTBusy = false;

	if (!bBtLinkExist) {
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], No profile exists!!!\n"));
		return algorithm;
	}

	if (pBtMgnt->ExtConfig.NumberOfHandle == 1) {
		if (bScoExist) {
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], SCO only\n"));
			algorithm = BT_2ANT_COEX_ALGO_SCO;
		} else {
			if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_HID)) {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], HID only\n"));
				algorithm = BT_2ANT_COEX_ALGO_HID;
			} else if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_A2DP)) {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], A2DP only\n"));
				algorithm = BT_2ANT_COEX_ALGO_A2DP;
			} else if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_PAN)) {
				if (bBtHsModeExist) {
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], PAN(HS) only\n"));
					algorithm = BT_2ANT_COEX_ALGO_PANHS;
				} else {
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], PAN(EDR) only\n"));
					algorithm = BT_2ANT_COEX_ALGO_PANEDR;
				}
			} else {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], Error!!! NO matched profile for NumberOfHandle =%d \n",
				pBtMgnt->ExtConfig.NumberOfHandle));
			}
		}
	} else if (pBtMgnt->ExtConfig.NumberOfHandle == 2) {
		if (bScoExist) {
			if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_HID)) {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], SCO + HID\n"));
				algorithm = BT_2ANT_COEX_ALGO_HID;
			} else if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_A2DP)) {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], Error!!! SCO + A2DP\n"));
			} else if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_PAN)) {
				if (bBtHsModeExist) {
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], SCO + PAN(HS)\n"));
					algorithm = BT_2ANT_COEX_ALGO_SCO;
				} else {
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], SCO + PAN(EDR)\n"));
					algorithm = BT_2ANT_COEX_ALGO_PANEDR_HID;
				}
			} else {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], Error!!! SCO exists but why NO matched ACL profile for NumberOfHandle =%d\n",
				pBtMgnt->ExtConfig.NumberOfHandle));
			}
		} else {
			if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_HID) &&
			    BTHCI_CheckProfileExist(padapter, BT_PROFILE_A2DP)) {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], HID + A2DP\n"));
				algorithm = BT_2ANT_COEX_ALGO_HID_A2DP;
		} else if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_HID) &&
			   BTHCI_CheckProfileExist(padapter, BT_PROFILE_PAN)) {
				if (bBtHsModeExist) {
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], HID + PAN(HS)\n"));
					algorithm = BT_2ANT_COEX_ALGO_HID_A2DP;
				} else {
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], HID + PAN(EDR)\n"));
					algorithm = BT_2ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_PAN) &&
				   BTHCI_CheckProfileExist(padapter, BT_PROFILE_A2DP)) {
				if (bBtHsModeExist) {
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], A2DP + PAN(HS)\n"));
					algorithm = BT_2ANT_COEX_ALGO_A2DP;
				} else {
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], A2DP + PAN(EDR)\n"));
					algorithm = BT_2ANT_COEX_ALGO_PANEDR_A2DP;
				}
			} else {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], Error!!! NO matched profile for NumberOfHandle =%d\n",
					pBtMgnt->ExtConfig.NumberOfHandle));
			}
		}
	} else if (pBtMgnt->ExtConfig.NumberOfHandle == 3) {
		if (bScoExist) {
			if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_HID) &&
			    BTHCI_CheckProfileExist(padapter, BT_PROFILE_A2DP)) {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], Error!!! SCO + HID + A2DP\n"));
			} else if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_HID) &&
				   BTHCI_CheckProfileExist(padapter, BT_PROFILE_PAN)) {
				if (bBtHsModeExist) {
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], SCO + HID + PAN(HS)\n"));
					algorithm = BT_2ANT_COEX_ALGO_HID_A2DP;
				} else {
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], SCO + HID + PAN(EDR)\n"));
					algorithm = BT_2ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_PAN) &&
				   BTHCI_CheckProfileExist(padapter, BT_PROFILE_A2DP)) {
				if (bBtHsModeExist) {
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], Error!!! SCO + A2DP + PAN(HS)\n"));
					algorithm = BT_2ANT_COEX_ALGO_SCO;
				} else {
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], Error!!! SCO + A2DP + PAN(EDR)\n"));
				}
			} else {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], Error!!! SCO exists but why NO matched profile for NumberOfHandle =%d\n",
					pBtMgnt->ExtConfig.NumberOfHandle));
			}
		} else {
			if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_HID) &&
			    BTHCI_CheckProfileExist(padapter, BT_PROFILE_PAN) &&
			    BTHCI_CheckProfileExist(padapter, BT_PROFILE_A2DP)) {
				if (bBtHsModeExist) {
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], HID + A2DP + PAN(HS)\n"));
					algorithm = BT_2ANT_COEX_ALGO_HID_A2DP_PANHS;
				} else {
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], HID + A2DP + PAN(EDR)\n"));
					algorithm = BT_2ANT_COEX_ALGO_HID_A2DP_PANEDR;
				}
			} else {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], Error!!! NO matched profile for NumberOfHandle =%d\n",
					pBtMgnt->ExtConfig.NumberOfHandle));
			}
		}
	} else if (pBtMgnt->ExtConfig.NumberOfHandle >= 3) {
		if (bScoExist) {
			if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_HID) &&
			    BTHCI_CheckProfileExist(padapter, BT_PROFILE_PAN) &&
			    BTHCI_CheckProfileExist(padapter, BT_PROFILE_A2DP)) {
				if (bBtHsModeExist)
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], Error!!! SCO + HID + A2DP + PAN(HS)\n"));
				else
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], Error!!! SCO + HID + A2DP + PAN(EDR)\n"));
			} else {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], Error!!! SCO exists but why NO matched profile for NumberOfHandle =%d\n",
					pBtMgnt->ExtConfig.NumberOfHandle));
			}
		} else {
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], Error!!! NO matched profile for NumberOfHandle =%d\n",
				pBtMgnt->ExtConfig.NumberOfHandle));
		}
	}
	return algorithm;
}

static u8 btdm_NeedToDecBtPwr(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	u8 bRet = false;

	if (BT_Operation(padapter)) {
		if (pHalData->dmpriv.EntryMinUndecoratedSmoothedPWDB > 47) {
			RTPRINT(FBT, BT_TRACE, ("Need to decrease bt power for HS mode!!\n"));
			bRet = true;
		} else {
			RTPRINT(FBT, BT_TRACE, ("NO Need to decrease bt power for HS mode!!\n"));
		}
	} else {
		if (BTDM_IsWifiConnectionExist(padapter)) {
			RTPRINT(FBT, BT_TRACE, ("Need to decrease bt power for Wifi is connected!!\n"));
			bRet = true;
		}
	}
	return bRet;
}

static void
btdm_SetCoexTable(struct rtw_adapter *padapter, u32 val0x6c0,
		  u32 val0x6c8, u8 val0x6cc)
{
	RTPRINT(FBT, BT_TRACE, ("set coex table, set 0x6c0 = 0x%x\n", val0x6c0));
	rtl8723au_write32(padapter, 0x6c0, val0x6c0);

	RTPRINT(FBT, BT_TRACE, ("set coex table, set 0x6c8 = 0x%x\n", val0x6c8));
	rtl8723au_write32(padapter, 0x6c8, val0x6c8);

	RTPRINT(FBT, BT_TRACE, ("set coex table, set 0x6cc = 0x%x\n", val0x6cc));
	rtl8723au_write8(padapter, 0x6cc, val0x6cc);
}

static void
btdm_SetSwFullTimeDacSwing(struct rtw_adapter *padapter, u8 bSwDacSwingOn,
			   u32 swDacSwingLvl)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	if (bSwDacSwingOn) {
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], SwDacSwing = 0x%x\n", swDacSwingLvl));
		PHY_SetBBReg(padapter, 0x880, 0xff000000, swDacSwingLvl);
		pHalData->bt_coexist.bSWCoexistAllOff = false;
	} else {
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], SwDacSwing Off!\n"));
		PHY_SetBBReg(padapter, 0x880, 0xff000000, 0xc0);
	}
}

static void
btdm_SetFwDacSwingLevel(struct rtw_adapter *padapter, u8 dacSwingLvl)
{
	u8 H2C_Parameter[1] = {0};

	H2C_Parameter[0] = dacSwingLvl;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], Set Dac Swing Level = 0x%x\n", dacSwingLvl));
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], write 0x29 = 0x%x\n", H2C_Parameter[0]));

	FillH2CCmd(padapter, 0x29, 1, H2C_Parameter);
}

static void btdm_2AntDecBtPwr(struct rtw_adapter *padapter, u8 bDecBtPwr)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct btdm_8723a_2ant *pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm2Ant;

	RTPRINT(FBT, BT_TRACE,
		("[BTCoex], Dec BT power = %s\n",
		((bDecBtPwr) ? "ON" : "OFF")));
	pBtdm8723->bCurDecBtPwr = bDecBtPwr;

	if (pBtdm8723->bPreDecBtPwr == pBtdm8723->bCurDecBtPwr)
		return;

	BTDM_SetFwDecBtPwr(padapter, pBtdm8723->bCurDecBtPwr);

	pBtdm8723->bPreDecBtPwr = pBtdm8723->bCurDecBtPwr;
}

static void
btdm_2AntFwDacSwingLvl(struct rtw_adapter *padapter, u8 fwDacSwingLvl)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct btdm_8723a_2ant *pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm2Ant;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], set FW Dac Swing level = %d\n",  fwDacSwingLvl));
	pBtdm8723->curFwDacSwingLvl = fwDacSwingLvl;

	/* RTPRINT(FBT, BT_TRACE, ("[BTCoex], preFwDacSwingLvl =%d, curFwDacSwingLvl =%d\n", */
	/*pBtdm8723->preFwDacSwingLvl, pBtdm8723->curFwDacSwingLvl)); */

	if (pBtdm8723->preFwDacSwingLvl == pBtdm8723->curFwDacSwingLvl)
		return;

	btdm_SetFwDacSwingLevel(padapter, pBtdm8723->curFwDacSwingLvl);

	pBtdm8723->preFwDacSwingLvl = pBtdm8723->curFwDacSwingLvl;
}

static void
btdm_2AntRfShrink(struct rtw_adapter *padapter, u8 bRxRfShrinkOn)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct btdm_8723a_2ant *pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm2Ant;

	RTPRINT(FBT, BT_TRACE,
		("[BTCoex], turn Rx RF Shrink = %s\n",
		((bRxRfShrinkOn) ? "ON" : "OFF")));
	pBtdm8723->bCurRfRxLpfShrink = bRxRfShrinkOn;

	/* RTPRINT(FBT, BT_TRACE, ("[BTCoex], bPreRfRxLpfShrink =%d, bCurRfRxLpfShrink =%d\n", */
	/*pBtdm8723->bPreRfRxLpfShrink, pBtdm8723->bCurRfRxLpfShrink)); */

	if (pBtdm8723->bPreRfRxLpfShrink == pBtdm8723->bCurRfRxLpfShrink)
		return;

	BTDM_SetSwRfRxLpfCorner(padapter, (u8)pBtdm8723->bCurRfRxLpfShrink);

	pBtdm8723->bPreRfRxLpfShrink = pBtdm8723->bCurRfRxLpfShrink;
}

static void
btdm_2AntLowPenaltyRa(struct rtw_adapter *padapter, u8 bLowPenaltyRa)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct btdm_8723a_2ant *pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm2Ant;

	RTPRINT(FBT, BT_TRACE,
		("[BTCoex], turn LowPenaltyRA = %s\n",
		((bLowPenaltyRa) ? "ON" : "OFF")));
	pBtdm8723->bCurLowPenaltyRa = bLowPenaltyRa;

	/* RTPRINT(FBT, BT_TRACE, ("[BTCoex], bPreLowPenaltyRa =%d, bCurLowPenaltyRa =%d\n", */
	/*pBtdm8723->bPreLowPenaltyRa, pBtdm8723->bCurLowPenaltyRa)); */

	if (pBtdm8723->bPreLowPenaltyRa == pBtdm8723->bCurLowPenaltyRa)
		return;

	BTDM_SetSwPenaltyTxRateAdaptive(padapter, (u8)pBtdm8723->bCurLowPenaltyRa);

	pBtdm8723->bPreLowPenaltyRa = pBtdm8723->bCurLowPenaltyRa;
}

static void
btdm_2AntDacSwing(struct rtw_adapter *padapter,
		  u8 bDacSwingOn, u32 dacSwingLvl)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct btdm_8723a_2ant *pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm2Ant;

	RTPRINT(FBT, BT_TRACE,
		("[BTCoex], turn DacSwing =%s, dacSwingLvl = 0x%x\n",
		(bDacSwingOn ? "ON" : "OFF"), dacSwingLvl));
	pBtdm8723->bCurDacSwingOn = bDacSwingOn;
	pBtdm8723->curDacSwingLvl = dacSwingLvl;

	if ((pBtdm8723->bPreDacSwingOn == pBtdm8723->bCurDacSwingOn) &&
	    (pBtdm8723->preDacSwingLvl == pBtdm8723->curDacSwingLvl))
		return;

	mdelay(30);
	btdm_SetSwFullTimeDacSwing(padapter, bDacSwingOn, dacSwingLvl);

	pBtdm8723->bPreDacSwingOn = pBtdm8723->bCurDacSwingOn;
	pBtdm8723->preDacSwingLvl = pBtdm8723->curDacSwingLvl;
}

static void btdm_2AntAdcBackOff(struct rtw_adapter *padapter, u8 bAdcBackOff)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct btdm_8723a_2ant *pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm2Ant;

	RTPRINT(FBT, BT_TRACE,
		("[BTCoex], turn AdcBackOff = %s\n",
		((bAdcBackOff) ? "ON" : "OFF")));
	pBtdm8723->bCurAdcBackOff = bAdcBackOff;

	if (pBtdm8723->bPreAdcBackOff == pBtdm8723->bCurAdcBackOff)
		return;

	BTDM_BBBackOffLevel(padapter, (u8)pBtdm8723->bCurAdcBackOff);

	pBtdm8723->bPreAdcBackOff = pBtdm8723->bCurAdcBackOff;
}

static void btdm_2AntAgcTable(struct rtw_adapter *padapter, u8 bAgcTableEn)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct btdm_8723a_2ant *pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm2Ant;

	RTPRINT(FBT, BT_TRACE,
		("[BTCoex], %s Agc Table\n", ((bAgcTableEn) ? "Enable" : "Disable")));
	pBtdm8723->bCurAgcTableEn = bAgcTableEn;

	/* RTPRINT(FBT, BT_TRACE, ("[BTCoex], bPreAgcTableEn =%d, bCurAgcTableEn =%d\n", */
	/*pBtdm8723->bPreAgcTableEn, pBtdm8723->bCurAgcTableEn)); */

	if (pBtdm8723->bPreAgcTableEn == pBtdm8723->bCurAgcTableEn)
		return;

	BTDM_AGCTable(padapter, (u8)bAgcTableEn);

	pBtdm8723->bPreAgcTableEn = pBtdm8723->bCurAgcTableEn;
}

static void
btdm_2AntCoexTable(struct rtw_adapter *padapter,
		   u32 val0x6c0, u32 val0x6c8, u8 val0x6cc)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct btdm_8723a_2ant *pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm2Ant;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], write Coex Table 0x6c0 = 0x%x, 0x6c8 = 0x%x, 0x6cc = 0x%x\n",
		val0x6c0, val0x6c8, val0x6cc));
	pBtdm8723->curVal0x6c0 = val0x6c0;
	pBtdm8723->curVal0x6c8 = val0x6c8;
	pBtdm8723->curVal0x6cc = val0x6cc;

	/* RTPRINT(FBT, BT_TRACE, ("[BTCoex], preVal0x6c0 = 0x%x, preVal0x6c8 = 0x%x, preVal0x6cc = 0x%x !!\n", */
	/*pBtdm8723->preVal0x6c0, pBtdm8723->preVal0x6c8, pBtdm8723->preVal0x6cc)); */
	/* RTPRINT(FBT, BT_TRACE, ("[BTCoex], curVal0x6c0 = 0x%x, curVal0x6c8 = 0x%x, curVal0x6cc = 0x%x !!\n", */
	/*pBtdm8723->curVal0x6c0, pBtdm8723->curVal0x6c8, pBtdm8723->curVal0x6cc)); */

	if ((pBtdm8723->preVal0x6c0 == pBtdm8723->curVal0x6c0) &&
	    (pBtdm8723->preVal0x6c8 == pBtdm8723->curVal0x6c8) &&
	    (pBtdm8723->preVal0x6cc == pBtdm8723->curVal0x6cc))
		return;

	btdm_SetCoexTable(padapter, val0x6c0, val0x6c8, val0x6cc);

	pBtdm8723->preVal0x6c0 = pBtdm8723->curVal0x6c0;
	pBtdm8723->preVal0x6c8 = pBtdm8723->curVal0x6c8;
	pBtdm8723->preVal0x6cc = pBtdm8723->curVal0x6cc;
}

static void btdm_2AntIgnoreWlanAct(struct rtw_adapter *padapter, u8 bEnable)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct btdm_8723a_2ant *pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm2Ant;

	RTPRINT(FBT, BT_TRACE,
		("[BTCoex], turn Ignore WlanAct %s\n", (bEnable ? "ON" : "OFF")));
	pBtdm8723->bCurIgnoreWlanAct = bEnable;


	if (pBtdm8723->bPreIgnoreWlanAct == pBtdm8723->bCurIgnoreWlanAct)
		return;

	btdm_SetFwIgnoreWlanAct(padapter, bEnable);
	pBtdm8723->bPreIgnoreWlanAct = pBtdm8723->bCurIgnoreWlanAct;
}

static void
btdm_2AntSetFw3a(struct rtw_adapter *padapter, u8 byte1, u8 byte2,
		 u8 byte3, u8 byte4, u8 byte5)
{
	u8 H2C_Parameter[5] = {0};

	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	/*  byte1[1:0] != 0 means enable pstdma */
	/*  for 2Ant bt coexist, if byte1 != 0 means enable pstdma */
	if (byte1)
		pHalData->bt_coexist.bFWCoexistAllOff = false;
	H2C_Parameter[0] = byte1;
	H2C_Parameter[1] = byte2;
	H2C_Parameter[2] = byte3;
	H2C_Parameter[3] = byte4;
	H2C_Parameter[4] = byte5;

	pHalData->bt_coexist.fw3aVal[0] = byte1;
	pHalData->bt_coexist.fw3aVal[1] = byte2;
	pHalData->bt_coexist.fw3aVal[2] = byte3;
	pHalData->bt_coexist.fw3aVal[3] = byte4;
	pHalData->bt_coexist.fw3aVal[4] = byte5;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], FW write 0x3a(5bytes) = 0x%x%08x\n",
		H2C_Parameter[0],
		H2C_Parameter[1]<<24|H2C_Parameter[2]<<16|H2C_Parameter[3]<<8|H2C_Parameter[4]));

	FillH2CCmd(padapter, 0x3a, 5, H2C_Parameter);
	}

static void btdm_2AntPsTdma(struct rtw_adapter *padapter, u8 bTurnOn, u8 type)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct btdm_8723a_2ant *pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm2Ant;
	u32			btTxRxCnt = 0;
	u8 bTurnOnByCnt = false;
	u8 psTdmaTypeByCnt = 0;

	btTxRxCnt = BTDM_BtTxRxCounterH(padapter)+BTDM_BtTxRxCounterL(padapter);
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT TxRx Counters = %d\n", btTxRxCnt));
	if (btTxRxCnt > 3000) {
		bTurnOnByCnt = true;
		psTdmaTypeByCnt = 8;

		RTPRINT(FBT, BT_TRACE,
			("[BTCoex], For BTTxRxCounters, turn %s PS TDMA, type =%d\n",
			(bTurnOnByCnt ? "ON" : "OFF"), psTdmaTypeByCnt));
		pBtdm8723->bCurPsTdmaOn = bTurnOnByCnt;
		pBtdm8723->curPsTdma = psTdmaTypeByCnt;
	} else {
		RTPRINT(FBT, BT_TRACE,
			("[BTCoex], turn %s PS TDMA, type =%d\n",
			(bTurnOn ? "ON" : "OFF"), type));
		pBtdm8723->bCurPsTdmaOn = bTurnOn;
		pBtdm8723->curPsTdma = type;
	}

	if ((pBtdm8723->bPrePsTdmaOn == pBtdm8723->bCurPsTdmaOn) &&
	    (pBtdm8723->prePsTdma == pBtdm8723->curPsTdma))
		return;

	if (bTurnOn) {
		switch (type) {
		case 1:
		default:
			btdm_2AntSetFw3a(padapter, 0xe3, 0x1a, 0x1a, 0xa1, 0x98);
			break;
		case 2:
			btdm_2AntSetFw3a(padapter, 0xe3, 0x12, 0x12, 0xa1, 0x98);
			break;
		case 3:
			btdm_2AntSetFw3a(padapter, 0xe3, 0xa, 0xa, 0xa1, 0x98);
			break;
		case 4:
			btdm_2AntSetFw3a(padapter, 0xa3, 0x5, 0x5, 0xa1, 0x80);
			break;
		case 5:
			btdm_2AntSetFw3a(padapter, 0xe3, 0x1a, 0x1a, 0x20, 0x98);
			break;
		case 6:
			btdm_2AntSetFw3a(padapter, 0xe3, 0x12, 0x12, 0x20, 0x98);
			break;
		case 7:
			btdm_2AntSetFw3a(padapter, 0xe3, 0xa, 0xa, 0x20, 0x98);
			break;
		case 8:
			btdm_2AntSetFw3a(padapter, 0xa3, 0x5, 0x5, 0x20, 0x80);
			break;
		case 9:
			btdm_2AntSetFw3a(padapter, 0xe3, 0x1a, 0x1a, 0xa1, 0x98);
			break;
		case 10:
			btdm_2AntSetFw3a(padapter, 0xe3, 0x12, 0x12, 0xa1, 0x98);
			break;
		case 11:
			btdm_2AntSetFw3a(padapter, 0xe3, 0xa, 0xa, 0xa1, 0x98);
			break;
		case 12:
			btdm_2AntSetFw3a(padapter, 0xe3, 0x5, 0x5, 0xa1, 0x98);
			break;
		case 13:
			btdm_2AntSetFw3a(padapter, 0xe3, 0x1a, 0x1a, 0x20, 0x98);
			break;
		case 14:
			btdm_2AntSetFw3a(padapter, 0xe3, 0x12, 0x12, 0x20, 0x98);
			break;
		case 15:
			btdm_2AntSetFw3a(padapter, 0xe3, 0xa, 0xa, 0x20, 0x98);
			break;
		case 16:
			btdm_2AntSetFw3a(padapter, 0xe3, 0x5, 0x5, 0x20, 0x98);
			break;
		case 17:
			btdm_2AntSetFw3a(padapter, 0xa3, 0x2f, 0x2f, 0x20, 0x80);
			break;
		case 18:
			btdm_2AntSetFw3a(padapter, 0xe3, 0x5, 0x5, 0xa1, 0x98);
			break;
		case 19:
			btdm_2AntSetFw3a(padapter, 0xe3, 0x25, 0x25, 0xa1, 0x98);
			break;
		case 20:
			btdm_2AntSetFw3a(padapter, 0xe3, 0x25, 0x25, 0x20, 0x98);
			break;
		}
	} else {
		/*  disable PS tdma */
		switch (type) {
		case 0:
			btdm_2AntSetFw3a(padapter, 0x0, 0x0, 0x0, 0x8, 0x0);
			break;
		case 1:
			btdm_2AntSetFw3a(padapter, 0x0, 0x0, 0x0, 0x0, 0x0);
			break;
		default:
			btdm_2AntSetFw3a(padapter, 0x0, 0x0, 0x0, 0x8, 0x0);
			break;
		}
	}

	/*  update pre state */
	pBtdm8723->bPrePsTdmaOn =  pBtdm8723->bCurPsTdmaOn;
	pBtdm8723->prePsTdma = pBtdm8723->curPsTdma;
}

static void btdm_2AntBtInquiryPage(struct rtw_adapter *padapter)
{
	btdm_2AntCoexTable(padapter, 0x55555555, 0xffff, 0x3);
	btdm_2AntIgnoreWlanAct(padapter, false);
	btdm_2AntPsTdma(padapter, true, 8);
}

static u8 btdm_HoldForBtInqPage(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	u32 curTime = jiffies;

	if (pHalData->bt_coexist.halCoex8723.bC2hBtInquiryPage) {
		/*  bt inquiry or page is started. */
		if (pHalData->bt_coexist.halCoex8723.btInqPageStartTime == 0) {
			pHalData->bt_coexist.halCoex8723.btInqPageStartTime = curTime;
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT Inquiry/page is started at time : 0x%lx \n",
			pHalData->bt_coexist.halCoex8723.btInqPageStartTime));
		}
	}
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT Inquiry/page started time : 0x%lx, curTime : 0x%x \n",
		pHalData->bt_coexist.halCoex8723.btInqPageStartTime, curTime));

	if (pHalData->bt_coexist.halCoex8723.btInqPageStartTime) {
		if (((curTime - pHalData->bt_coexist.halCoex8723.btInqPageStartTime)/1000000) >= 10) {
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], BT Inquiry/page >= 10sec!!!"));
			pHalData->bt_coexist.halCoex8723.btInqPageStartTime = 0;
		}
	}

	if (pHalData->bt_coexist.halCoex8723.btInqPageStartTime) {
		btdm_2AntCoexTable(padapter, 0x55555555, 0xffff, 0x3);
		btdm_2AntIgnoreWlanAct(padapter, false);
		btdm_2AntPsTdma(padapter, true, 8);
		return true;
	} else {
		return false;
	}
}

static u8 btdm_Is2Ant8723ACommonAction(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct btdm_8723a_2ant *pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm2Ant;
	u8 bCommon = false;

	RTPRINT(FBT, BT_TRACE, ("%s :BTDM_IsWifiConnectionExist =%x check_fwstate =%x pmlmepriv->fw_state = 0x%x\n", __func__, BTDM_IsWifiConnectionExist(padapter), check_fwstate(&padapter->mlmepriv, (_FW_UNDER_SURVEY|_FW_UNDER_LINKING)), padapter->mlmepriv.fw_state));

	if ((!BTDM_IsWifiConnectionExist(padapter)) &&
	    (!check_fwstate(&padapter->mlmepriv, (_FW_UNDER_SURVEY|_FW_UNDER_LINKING))) &&
	    (BT_2ANT_BT_STATUS_IDLE == pBtdm8723->btStatus)) {
		RTPRINT(FBT, BT_TRACE, ("Wifi idle + Bt idle!!\n"));

		btdm_2AntLowPenaltyRa(padapter, false);
		btdm_2AntRfShrink(padapter, false);
		btdm_2AntCoexTable(padapter, 0x55555555, 0xffff, 0x3);

		btdm_2AntIgnoreWlanAct(padapter, false);
		btdm_2AntPsTdma(padapter, false, 0);
		btdm_2AntFwDacSwingLvl(padapter, 0x20);
		btdm_2AntDecBtPwr(padapter, false);

		btdm_2AntAgcTable(padapter, false);
		btdm_2AntAdcBackOff(padapter, false);
		btdm_2AntDacSwing(padapter, false, 0xc0);

		bCommon = true;
	} else if (((BTDM_IsWifiConnectionExist(padapter)) ||
		   (check_fwstate(&padapter->mlmepriv, (_FW_UNDER_SURVEY|_FW_UNDER_LINKING)))) &&
		   (BT_2ANT_BT_STATUS_IDLE == pBtdm8723->btStatus)) {
		RTPRINT(FBT, BT_TRACE, ("Wifi non-idle + BT idle!!\n"));

		btdm_2AntLowPenaltyRa(padapter, true);
		btdm_2AntRfShrink(padapter, false);
		btdm_2AntCoexTable(padapter, 0x55555555, 0xffff, 0x3);

		btdm_2AntIgnoreWlanAct(padapter, false);
		btdm_2AntPsTdma(padapter, false, 0);
		btdm_2AntFwDacSwingLvl(padapter, 0x20);
		btdm_2AntDecBtPwr(padapter, true);

		btdm_2AntAgcTable(padapter, false);
		btdm_2AntAdcBackOff(padapter, false);
		btdm_2AntDacSwing(padapter, false, 0xc0);

		bCommon = true;
	} else if ((!BTDM_IsWifiConnectionExist(padapter)) &&
		   (!check_fwstate(&padapter->mlmepriv, (_FW_UNDER_SURVEY|_FW_UNDER_LINKING))) &&
		   (BT_2ANT_BT_STATUS_CONNECTED_IDLE == pBtdm8723->btStatus)) {
		RTPRINT(FBT, BT_TRACE, ("Wifi idle + Bt connected idle!!\n"));

		btdm_2AntLowPenaltyRa(padapter, true);
		btdm_2AntRfShrink(padapter, true);
		btdm_2AntCoexTable(padapter, 0x55555555, 0xffff, 0x3);

		btdm_2AntIgnoreWlanAct(padapter, false);
		btdm_2AntPsTdma(padapter, false, 0);
		btdm_2AntFwDacSwingLvl(padapter, 0x20);
		btdm_2AntDecBtPwr(padapter, false);

		btdm_2AntAgcTable(padapter, false);
		btdm_2AntAdcBackOff(padapter, false);
		btdm_2AntDacSwing(padapter, false, 0xc0);

		bCommon = true;
	} else if (((BTDM_IsWifiConnectionExist(padapter)) ||
		   (check_fwstate(&padapter->mlmepriv, (_FW_UNDER_SURVEY|_FW_UNDER_LINKING)))) &&
		   (BT_2ANT_BT_STATUS_CONNECTED_IDLE == pBtdm8723->btStatus)) {
		RTPRINT(FBT, BT_TRACE, ("Wifi non-idle + Bt connected idle!!\n"));

		btdm_2AntLowPenaltyRa(padapter, true);
		btdm_2AntRfShrink(padapter, true);
		btdm_2AntCoexTable(padapter, 0x55555555, 0xffff, 0x3);

		btdm_2AntIgnoreWlanAct(padapter, false);
		btdm_2AntPsTdma(padapter, false, 0);
		btdm_2AntFwDacSwingLvl(padapter, 0x20);
		btdm_2AntDecBtPwr(padapter, true);

		btdm_2AntAgcTable(padapter, false);
		btdm_2AntAdcBackOff(padapter, false);
		btdm_2AntDacSwing(padapter, false, 0xc0);

		bCommon = true;
	} else if ((!BTDM_IsWifiConnectionExist(padapter)) &&
		   (!check_fwstate(&padapter->mlmepriv, (_FW_UNDER_SURVEY|_FW_UNDER_LINKING))) &&
		   (BT_2ANT_BT_STATUS_NON_IDLE == pBtdm8723->btStatus)) {
		RTPRINT(FBT, BT_TRACE, ("Wifi idle + BT non-idle!!\n"));

		btdm_2AntLowPenaltyRa(padapter, true);
		btdm_2AntRfShrink(padapter, true);
		btdm_2AntCoexTable(padapter, 0x55555555, 0xffff, 0x3);

		btdm_2AntIgnoreWlanAct(padapter, false);
		btdm_2AntPsTdma(padapter, false, 0);
		btdm_2AntFwDacSwingLvl(padapter, 0x20);
		btdm_2AntDecBtPwr(padapter, false);

		btdm_2AntAgcTable(padapter, false);
		btdm_2AntAdcBackOff(padapter, false);
		btdm_2AntDacSwing(padapter, false, 0xc0);

		bCommon = true;
	} else {
		RTPRINT(FBT, BT_TRACE, ("Wifi non-idle + BT non-idle!!\n"));
		btdm_2AntLowPenaltyRa(padapter, true);
		btdm_2AntRfShrink(padapter, true);
		btdm_2AntCoexTable(padapter, 0x55555555, 0xffff, 0x3);
		btdm_2AntIgnoreWlanAct(padapter, false);
		btdm_2AntFwDacSwingLvl(padapter, 0x20);

		bCommon = false;
	}
	return bCommon;
}

static void
btdm_2AntTdmaDurationAdjust(struct rtw_adapter *padapter, u8 bScoHid,
			    u8 bTxPause, u8 maxInterval)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct btdm_8723a_2ant *pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm2Ant;
	static s32		up, dn, m, n, WaitCount;
	s32			result;   /* 0: no change, +1: increase WiFi duration, -1: decrease WiFi duration */
	u8 retryCount = 0;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], TdmaDurationAdjust()\n"));

	if (pBtdm8723->bResetTdmaAdjust) {
		pBtdm8723->bResetTdmaAdjust = false;
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], first run TdmaDurationAdjust()!!\n"));
		if (bScoHid) {
			if (bTxPause) {
				btdm_2AntPsTdma(padapter, true, 15);
				pBtdm8723->psTdmaDuAdjType = 15;
			} else {
				btdm_2AntPsTdma(padapter, true, 11);
				pBtdm8723->psTdmaDuAdjType = 11;
			}
		} else {
			if (bTxPause) {
				btdm_2AntPsTdma(padapter, true, 7);
				pBtdm8723->psTdmaDuAdjType = 7;
			} else {
				btdm_2AntPsTdma(padapter, true, 3);
				pBtdm8723->psTdmaDuAdjType = 3;
			}
		}
		up = 0;
		dn = 0;
		m = 1;
		n = 3;
		result = 0;
		WaitCount = 0;
	} else {
		/* accquire the BT TRx retry count from BT_Info byte2 */
		retryCount = pHalData->bt_coexist.halCoex8723.btRetryCnt;
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], retryCount = %d\n", retryCount));
		result = 0;
		WaitCount++;

		if (retryCount == 0) {  /*  no retry in the last 2-second duration */
			up++;
			dn--;

			if (dn <= 0)
				dn = 0;

			if (up >= n) {	/*  if 連續 n 個2秒 retry count為0, 則調寬WiFi duration */
				WaitCount = 0;
				n = 3;
				up = 0;
				dn = 0;
				result = 1;
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], Increase wifi duration!!\n"));
			}
		} else if (retryCount <= 3) {	/*  <= 3 retry in the last 2-second duration */
			up--;
			dn++;

			if (up <= 0)
				up = 0;

			if (dn == 2) {	/*  if 連續 2 個2秒 retry count< 3, 則調窄WiFi duration */
				if (WaitCount <= 2)
					m++; /*  避免一直在兩個level中來回 */
				else
					m = 1;

				if (m >= 20) /* m 最大值 = 20 ' 最大120秒 recheck是否調整 WiFi duration. */
					m = 20;

				n = 3*m;
				up = 0;
				dn = 0;
				WaitCount = 0;
				result = -1;
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], Decrease wifi duration for retryCounter<3!!\n"));
			}
		} else {  /* retry count > 3, 只要1次 retry count > 3, 則調窄WiFi duration */
			if (WaitCount == 1)
				m++; /*  避免一直在兩個level中來回 */
			else
				m = 1;

			if (m >= 20) /* m 最大值 = 20 ' 最大120秒 recheck是否調整 WiFi duration. */
				m = 20;
			n = 3*m;
			up = 0;
			dn = 0;
			WaitCount = 0;
			result = -1;
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], Decrease wifi duration for retryCounter>3!!\n"));
		}

		RTPRINT(FBT, BT_TRACE, ("[BTCoex], max Interval = %d\n", maxInterval));
		if (maxInterval == 1) {
			if (bTxPause) {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], TxPause = 1\n"));
				if (pBtdm8723->curPsTdma == 1) {
					btdm_2AntPsTdma(padapter, true, 5);
					pBtdm8723->psTdmaDuAdjType = 5;
				} else if (pBtdm8723->curPsTdma == 2) {
					btdm_2AntPsTdma(padapter, true, 6);
					pBtdm8723->psTdmaDuAdjType = 6;
				} else if (pBtdm8723->curPsTdma == 3) {
					btdm_2AntPsTdma(padapter, true, 7);
					pBtdm8723->psTdmaDuAdjType = 7;
				} else if (pBtdm8723->curPsTdma == 4) {
					btdm_2AntPsTdma(padapter, true, 8);
					pBtdm8723->psTdmaDuAdjType = 8;
				}
				if (pBtdm8723->curPsTdma == 9) {
					btdm_2AntPsTdma(padapter, true, 13);
					pBtdm8723->psTdmaDuAdjType = 13;
				} else if (pBtdm8723->curPsTdma == 10) {
					btdm_2AntPsTdma(padapter, true, 14);
					pBtdm8723->psTdmaDuAdjType = 14;
				} else if (pBtdm8723->curPsTdma == 11) {
					btdm_2AntPsTdma(padapter, true, 15);
					pBtdm8723->psTdmaDuAdjType = 15;
				} else if (pBtdm8723->curPsTdma == 12) {
					btdm_2AntPsTdma(padapter, true, 16);
					pBtdm8723->psTdmaDuAdjType = 16;
				}

				if (result == -1) {
					if (pBtdm8723->curPsTdma == 5) {
						btdm_2AntPsTdma(padapter, true, 6);
						pBtdm8723->psTdmaDuAdjType = 6;
					} else if (pBtdm8723->curPsTdma == 6) {
						btdm_2AntPsTdma(padapter, true, 7);
						pBtdm8723->psTdmaDuAdjType = 7;
					} else if (pBtdm8723->curPsTdma == 7) {
						btdm_2AntPsTdma(padapter, true, 8);
						pBtdm8723->psTdmaDuAdjType = 8;
					} else if (pBtdm8723->curPsTdma == 13) {
						btdm_2AntPsTdma(padapter, true, 14);
						pBtdm8723->psTdmaDuAdjType = 14;
					} else if (pBtdm8723->curPsTdma == 14) {
						btdm_2AntPsTdma(padapter, true, 15);
						pBtdm8723->psTdmaDuAdjType = 15;
					} else if (pBtdm8723->curPsTdma == 15) {
						btdm_2AntPsTdma(padapter, true, 16);
						pBtdm8723->psTdmaDuAdjType = 16;
					}
				} else if (result == 1) {
					if (pBtdm8723->curPsTdma == 8) {
						btdm_2AntPsTdma(padapter, true, 7);
						pBtdm8723->psTdmaDuAdjType = 7;
					} else if (pBtdm8723->curPsTdma == 7) {
						btdm_2AntPsTdma(padapter, true, 6);
						pBtdm8723->psTdmaDuAdjType = 6;
					} else if (pBtdm8723->curPsTdma == 6) {
						btdm_2AntPsTdma(padapter, true, 5);
						pBtdm8723->psTdmaDuAdjType = 5;
					} else if (pBtdm8723->curPsTdma == 16) {
						btdm_2AntPsTdma(padapter, true, 15);
						pBtdm8723->psTdmaDuAdjType = 15;
					} else if (pBtdm8723->curPsTdma == 15) {
						btdm_2AntPsTdma(padapter, true, 14);
						pBtdm8723->psTdmaDuAdjType = 14;
					} else if (pBtdm8723->curPsTdma == 14) {
						btdm_2AntPsTdma(padapter, true, 13);
						pBtdm8723->psTdmaDuAdjType = 13;
					}
				}
			} else {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], TxPause = 0\n"));
				if (pBtdm8723->curPsTdma == 5) {
					btdm_2AntPsTdma(padapter, true, 1);
					pBtdm8723->psTdmaDuAdjType = 1;
				} else if (pBtdm8723->curPsTdma == 6) {
					btdm_2AntPsTdma(padapter, true, 2);
					pBtdm8723->psTdmaDuAdjType = 2;
				} else if (pBtdm8723->curPsTdma == 7) {
					btdm_2AntPsTdma(padapter, true, 3);
					pBtdm8723->psTdmaDuAdjType = 3;
				} else if (pBtdm8723->curPsTdma == 8) {
					btdm_2AntPsTdma(padapter, true, 4);
					pBtdm8723->psTdmaDuAdjType = 4;
				}
				if (pBtdm8723->curPsTdma == 13) {
					btdm_2AntPsTdma(padapter, true, 9);
					pBtdm8723->psTdmaDuAdjType = 9;
				} else if (pBtdm8723->curPsTdma == 14) {
					btdm_2AntPsTdma(padapter, true, 10);
					pBtdm8723->psTdmaDuAdjType = 10;
				} else if (pBtdm8723->curPsTdma == 15) {
					btdm_2AntPsTdma(padapter, true, 11);
					pBtdm8723->psTdmaDuAdjType = 11;
				} else if (pBtdm8723->curPsTdma == 16) {
					btdm_2AntPsTdma(padapter, true, 12);
					pBtdm8723->psTdmaDuAdjType = 12;
				}

				if (result == -1) {
					if (pBtdm8723->curPsTdma == 1) {
						btdm_2AntPsTdma(padapter, true, 2);
						pBtdm8723->psTdmaDuAdjType = 2;
					} else if (pBtdm8723->curPsTdma == 2) {
						btdm_2AntPsTdma(padapter, true, 3);
						pBtdm8723->psTdmaDuAdjType = 3;
					} else if (pBtdm8723->curPsTdma == 3) {
						btdm_2AntPsTdma(padapter, true, 4);
						pBtdm8723->psTdmaDuAdjType = 4;
					} else if (pBtdm8723->curPsTdma == 9) {
						btdm_2AntPsTdma(padapter, true, 10);
						pBtdm8723->psTdmaDuAdjType = 10;
					} else if (pBtdm8723->curPsTdma == 10) {
						btdm_2AntPsTdma(padapter, true, 11);
						pBtdm8723->psTdmaDuAdjType = 11;
					} else if (pBtdm8723->curPsTdma == 11) {
						btdm_2AntPsTdma(padapter, true, 12);
						pBtdm8723->psTdmaDuAdjType = 12;
					}
				} else if (result == 1) {
					if (pBtdm8723->curPsTdma == 4) {
						btdm_2AntPsTdma(padapter, true, 3);
						pBtdm8723->psTdmaDuAdjType = 3;
					} else if (pBtdm8723->curPsTdma == 3) {
						btdm_2AntPsTdma(padapter, true, 2);
						pBtdm8723->psTdmaDuAdjType = 2;
					} else if (pBtdm8723->curPsTdma == 2) {
						btdm_2AntPsTdma(padapter, true, 1);
						pBtdm8723->psTdmaDuAdjType = 1;
					} else if (pBtdm8723->curPsTdma == 12) {
						btdm_2AntPsTdma(padapter, true, 11);
						pBtdm8723->psTdmaDuAdjType = 11;
					} else if (pBtdm8723->curPsTdma == 11) {
						btdm_2AntPsTdma(padapter, true, 10);
						pBtdm8723->psTdmaDuAdjType = 10;
					} else if (pBtdm8723->curPsTdma == 10) {
						btdm_2AntPsTdma(padapter, true, 9);
						pBtdm8723->psTdmaDuAdjType = 9;
					}
				}
			}
		} else if (maxInterval == 2) {
			if (bTxPause) {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], TxPause = 1\n"));
				if (pBtdm8723->curPsTdma == 1) {
					btdm_2AntPsTdma(padapter, true, 6);
					pBtdm8723->psTdmaDuAdjType = 6;
				} else if (pBtdm8723->curPsTdma == 2) {
					btdm_2AntPsTdma(padapter, true, 6);
					pBtdm8723->psTdmaDuAdjType = 6;
				} else if (pBtdm8723->curPsTdma == 3) {
					btdm_2AntPsTdma(padapter, true, 7);
					pBtdm8723->psTdmaDuAdjType = 7;
				} else if (pBtdm8723->curPsTdma == 4) {
					btdm_2AntPsTdma(padapter, true, 8);
					pBtdm8723->psTdmaDuAdjType = 8;
				}
				if (pBtdm8723->curPsTdma == 9) {
					btdm_2AntPsTdma(padapter, true, 14);
					pBtdm8723->psTdmaDuAdjType = 14;
				} else if (pBtdm8723->curPsTdma == 10) {
					btdm_2AntPsTdma(padapter, true, 14);
					pBtdm8723->psTdmaDuAdjType = 14;
				} else if (pBtdm8723->curPsTdma == 11) {
					btdm_2AntPsTdma(padapter, true, 15);
					pBtdm8723->psTdmaDuAdjType = 15;
				} else if (pBtdm8723->curPsTdma == 12) {
					btdm_2AntPsTdma(padapter, true, 16);
					pBtdm8723->psTdmaDuAdjType = 16;
				}
				if (result == -1) {
					if (pBtdm8723->curPsTdma == 5) {
						btdm_2AntPsTdma(padapter, true, 6);
						pBtdm8723->psTdmaDuAdjType = 6;
					} else if (pBtdm8723->curPsTdma == 6) {
						btdm_2AntPsTdma(padapter, true, 7);
						pBtdm8723->psTdmaDuAdjType = 7;
					} else if (pBtdm8723->curPsTdma == 7) {
						btdm_2AntPsTdma(padapter, true, 8);
						pBtdm8723->psTdmaDuAdjType = 8;
					} else if (pBtdm8723->curPsTdma == 13) {
						btdm_2AntPsTdma(padapter, true, 14);
						pBtdm8723->psTdmaDuAdjType = 14;
					} else if (pBtdm8723->curPsTdma == 14) {
						btdm_2AntPsTdma(padapter, true, 15);
						pBtdm8723->psTdmaDuAdjType = 15;
					} else if (pBtdm8723->curPsTdma == 15) {
						btdm_2AntPsTdma(padapter, true, 16);
						pBtdm8723->psTdmaDuAdjType = 16;
					}
				} else if (result == 1) {
					if (pBtdm8723->curPsTdma == 8) {
						btdm_2AntPsTdma(padapter, true, 7);
						pBtdm8723->psTdmaDuAdjType = 7;
					} else if (pBtdm8723->curPsTdma == 7) {
						btdm_2AntPsTdma(padapter, true, 6);
						pBtdm8723->psTdmaDuAdjType = 6;
					} else if (pBtdm8723->curPsTdma == 6) {
						btdm_2AntPsTdma(padapter, true, 6);
						pBtdm8723->psTdmaDuAdjType = 6;
					} else if (pBtdm8723->curPsTdma == 16) {
						btdm_2AntPsTdma(padapter, true, 15);
						pBtdm8723->psTdmaDuAdjType = 15;
					} else if (pBtdm8723->curPsTdma == 15) {
						btdm_2AntPsTdma(padapter, true, 14);
						pBtdm8723->psTdmaDuAdjType = 14;
					} else if (pBtdm8723->curPsTdma == 14) {
						btdm_2AntPsTdma(padapter, true, 14);
						pBtdm8723->psTdmaDuAdjType = 14;
					}
				}
			} else {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], TxPause = 0\n"));
				if (pBtdm8723->curPsTdma == 5) {
					btdm_2AntPsTdma(padapter, true, 2);
					pBtdm8723->psTdmaDuAdjType = 2;
				} else if (pBtdm8723->curPsTdma == 6) {
					btdm_2AntPsTdma(padapter, true, 2);
					pBtdm8723->psTdmaDuAdjType = 2;
				} else if (pBtdm8723->curPsTdma == 7) {
					btdm_2AntPsTdma(padapter, true, 3);
					pBtdm8723->psTdmaDuAdjType = 3;
				} else if (pBtdm8723->curPsTdma == 8) {
					btdm_2AntPsTdma(padapter, true, 4);
					pBtdm8723->psTdmaDuAdjType = 4;
				}
				if (pBtdm8723->curPsTdma == 13) {
					btdm_2AntPsTdma(padapter, true, 10);
					pBtdm8723->psTdmaDuAdjType = 10;
				} else if (pBtdm8723->curPsTdma == 14) {
					btdm_2AntPsTdma(padapter, true, 10);
					pBtdm8723->psTdmaDuAdjType = 10;
				} else if (pBtdm8723->curPsTdma == 15) {
					btdm_2AntPsTdma(padapter, true, 11);
					pBtdm8723->psTdmaDuAdjType = 11;
				} else if (pBtdm8723->curPsTdma == 16) {
					btdm_2AntPsTdma(padapter, true, 12);
					pBtdm8723->psTdmaDuAdjType = 12;
				}
				if (result == -1) {
					if (pBtdm8723->curPsTdma == 1) {
						btdm_2AntPsTdma(padapter, true, 2);
						pBtdm8723->psTdmaDuAdjType = 2;
					} else if (pBtdm8723->curPsTdma == 2) {
						btdm_2AntPsTdma(padapter, true, 3);
						pBtdm8723->psTdmaDuAdjType = 3;
					} else if (pBtdm8723->curPsTdma == 3) {
						btdm_2AntPsTdma(padapter, true, 4);
						pBtdm8723->psTdmaDuAdjType = 4;
					} else if (pBtdm8723->curPsTdma == 9) {
						btdm_2AntPsTdma(padapter, true, 10);
						pBtdm8723->psTdmaDuAdjType = 10;
					} else if (pBtdm8723->curPsTdma == 10) {
						btdm_2AntPsTdma(padapter, true, 11);
						pBtdm8723->psTdmaDuAdjType = 11;
					} else if (pBtdm8723->curPsTdma == 11) {
						btdm_2AntPsTdma(padapter, true, 12);
						pBtdm8723->psTdmaDuAdjType = 12;
					}
				} else if (result == 1) {
					if (pBtdm8723->curPsTdma == 4) {
						btdm_2AntPsTdma(padapter, true, 3);
						pBtdm8723->psTdmaDuAdjType = 3;
					} else if (pBtdm8723->curPsTdma == 3) {
						btdm_2AntPsTdma(padapter, true, 2);
						pBtdm8723->psTdmaDuAdjType = 2;
					} else if (pBtdm8723->curPsTdma == 2) {
						btdm_2AntPsTdma(padapter, true, 2);
						pBtdm8723->psTdmaDuAdjType = 2;
					} else if (pBtdm8723->curPsTdma == 12) {
						btdm_2AntPsTdma(padapter, true, 11);
						pBtdm8723->psTdmaDuAdjType = 11;
					} else if (pBtdm8723->curPsTdma == 11) {
						btdm_2AntPsTdma(padapter, true, 10);
						pBtdm8723->psTdmaDuAdjType = 10;
					} else if (pBtdm8723->curPsTdma == 10) {
						btdm_2AntPsTdma(padapter, true, 10);
						pBtdm8723->psTdmaDuAdjType = 10;
					}
				}
			}
		} else if (maxInterval == 3) {
			if (bTxPause) {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], TxPause = 1\n"));
				if (pBtdm8723->curPsTdma == 1) {
					btdm_2AntPsTdma(padapter, true, 7);
					pBtdm8723->psTdmaDuAdjType = 7;
				} else if (pBtdm8723->curPsTdma == 2) {
					btdm_2AntPsTdma(padapter, true, 7);
					pBtdm8723->psTdmaDuAdjType = 7;
				} else if (pBtdm8723->curPsTdma == 3) {
					btdm_2AntPsTdma(padapter, true, 7);
					pBtdm8723->psTdmaDuAdjType = 7;
				} else if (pBtdm8723->curPsTdma == 4) {
					btdm_2AntPsTdma(padapter, true, 8);
					pBtdm8723->psTdmaDuAdjType = 8;
				}
				if (pBtdm8723->curPsTdma == 9) {
					btdm_2AntPsTdma(padapter, true, 15);
					pBtdm8723->psTdmaDuAdjType = 15;
				} else if (pBtdm8723->curPsTdma == 10) {
					btdm_2AntPsTdma(padapter, true, 15);
					pBtdm8723->psTdmaDuAdjType = 15;
				} else if (pBtdm8723->curPsTdma == 11) {
					btdm_2AntPsTdma(padapter, true, 15);
					pBtdm8723->psTdmaDuAdjType = 15;
				} else if (pBtdm8723->curPsTdma == 12) {
					btdm_2AntPsTdma(padapter, true, 16);
					pBtdm8723->psTdmaDuAdjType = 16;
				}
				if (result == -1) {
					if (pBtdm8723->curPsTdma == 5) {
						btdm_2AntPsTdma(padapter, true, 7);
						pBtdm8723->psTdmaDuAdjType = 7;
					} else if (pBtdm8723->curPsTdma == 6) {
						btdm_2AntPsTdma(padapter, true, 7);
						pBtdm8723->psTdmaDuAdjType = 7;
					} else if (pBtdm8723->curPsTdma == 7) {
						btdm_2AntPsTdma(padapter, true, 8);
						pBtdm8723->psTdmaDuAdjType = 8;
					} else if (pBtdm8723->curPsTdma == 13) {
						btdm_2AntPsTdma(padapter, true, 15);
						pBtdm8723->psTdmaDuAdjType = 15;
					} else if (pBtdm8723->curPsTdma == 14) {
						btdm_2AntPsTdma(padapter, true, 15);
						pBtdm8723->psTdmaDuAdjType = 15;
					} else if (pBtdm8723->curPsTdma == 15) {
						btdm_2AntPsTdma(padapter, true, 16);
						pBtdm8723->psTdmaDuAdjType = 16;
					}
				} else if (result == 1) {
					if (pBtdm8723->curPsTdma == 8) {
						btdm_2AntPsTdma(padapter, true, 7);
						pBtdm8723->psTdmaDuAdjType = 7;
					} else if (pBtdm8723->curPsTdma == 7) {
						btdm_2AntPsTdma(padapter, true, 7);
						pBtdm8723->psTdmaDuAdjType = 7;
					} else if (pBtdm8723->curPsTdma == 6) {
						btdm_2AntPsTdma(padapter, true, 7);
						pBtdm8723->psTdmaDuAdjType = 7;
					} else if (pBtdm8723->curPsTdma == 16) {
						btdm_2AntPsTdma(padapter, true, 15);
						pBtdm8723->psTdmaDuAdjType = 15;
					} else if (pBtdm8723->curPsTdma == 15) {
						btdm_2AntPsTdma(padapter, true, 15);
						pBtdm8723->psTdmaDuAdjType = 15;
					} else if (pBtdm8723->curPsTdma == 14) {
						btdm_2AntPsTdma(padapter, true, 15);
						pBtdm8723->psTdmaDuAdjType = 15;
					}
				}
			} else {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], TxPause = 0\n"));
				if (pBtdm8723->curPsTdma == 5) {
					btdm_2AntPsTdma(padapter, true, 3);
					pBtdm8723->psTdmaDuAdjType = 3;
				} else if (pBtdm8723->curPsTdma == 6) {
					btdm_2AntPsTdma(padapter, true, 3);
					pBtdm8723->psTdmaDuAdjType = 3;
				} else if (pBtdm8723->curPsTdma == 7) {
					btdm_2AntPsTdma(padapter, true, 3);
					pBtdm8723->psTdmaDuAdjType = 3;
				} else if (pBtdm8723->curPsTdma == 8) {
					btdm_2AntPsTdma(padapter, true, 4);
					pBtdm8723->psTdmaDuAdjType = 4;
				}
				if (pBtdm8723->curPsTdma == 13) {
					btdm_2AntPsTdma(padapter, true, 11);
					pBtdm8723->psTdmaDuAdjType = 11;
				} else if (pBtdm8723->curPsTdma == 14) {
					btdm_2AntPsTdma(padapter, true, 11);
					pBtdm8723->psTdmaDuAdjType = 11;
				} else if (pBtdm8723->curPsTdma == 15) {
					btdm_2AntPsTdma(padapter, true, 11);
					pBtdm8723->psTdmaDuAdjType = 11;
				} else if (pBtdm8723->curPsTdma == 16) {
					btdm_2AntPsTdma(padapter, true, 12);
					pBtdm8723->psTdmaDuAdjType = 12;
				}
				if (result == -1) {
					if (pBtdm8723->curPsTdma == 1) {
						btdm_2AntPsTdma(padapter, true, 3);
						pBtdm8723->psTdmaDuAdjType = 3;
					} else if (pBtdm8723->curPsTdma == 2) {
						btdm_2AntPsTdma(padapter, true, 3);
						pBtdm8723->psTdmaDuAdjType = 3;
					} else if (pBtdm8723->curPsTdma == 3) {
						btdm_2AntPsTdma(padapter, true, 4);
						pBtdm8723->psTdmaDuAdjType = 4;
					} else if (pBtdm8723->curPsTdma == 9) {
						btdm_2AntPsTdma(padapter, true, 11);
						pBtdm8723->psTdmaDuAdjType = 11;
					} else if (pBtdm8723->curPsTdma == 10) {
						btdm_2AntPsTdma(padapter, true, 11);
						pBtdm8723->psTdmaDuAdjType = 11;
					} else if (pBtdm8723->curPsTdma == 11) {
						btdm_2AntPsTdma(padapter, true, 12);
						pBtdm8723->psTdmaDuAdjType = 12;
					}
				} else if (result == 1) {
					if (pBtdm8723->curPsTdma == 4) {
						btdm_2AntPsTdma(padapter, true, 3);
						pBtdm8723->psTdmaDuAdjType = 3;
					} else if (pBtdm8723->curPsTdma == 3) {
						btdm_2AntPsTdma(padapter, true, 3);
						pBtdm8723->psTdmaDuAdjType = 3;
					} else if (pBtdm8723->curPsTdma == 2) {
						btdm_2AntPsTdma(padapter, true, 3);
						pBtdm8723->psTdmaDuAdjType = 3;
					} else if (pBtdm8723->curPsTdma == 12) {
						btdm_2AntPsTdma(padapter, true, 11);
						pBtdm8723->psTdmaDuAdjType = 11;
					} else if (pBtdm8723->curPsTdma == 11) {
						btdm_2AntPsTdma(padapter, true, 11);
						pBtdm8723->psTdmaDuAdjType = 11;
					} else if (pBtdm8723->curPsTdma == 10) {
						btdm_2AntPsTdma(padapter, true, 11);
						pBtdm8723->psTdmaDuAdjType = 11;
					}
				}
			}
		}
	}
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], PsTdma type : recordPsTdma =%d\n", pBtdm8723->psTdmaDuAdjType));
	/*  if current PsTdma not match with the recorded one (when scan, dhcp...), */
	/*  then we have to adjust it back to the previous record one. */
	if (pBtdm8723->curPsTdma != pBtdm8723->psTdmaDuAdjType) {
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], PsTdma type dismatch!!!, curPsTdma =%d, recordPsTdma =%d\n",
			pBtdm8723->curPsTdma, pBtdm8723->psTdmaDuAdjType));

		if (!check_fwstate(&padapter->mlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING))
			btdm_2AntPsTdma(padapter, true, pBtdm8723->psTdmaDuAdjType);
		else
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], roaming/link/scan is under progress, will adjust next time!!!\n"));
	}
}

/*  default Action */
/*  SCO only or SCO+PAN(HS) */
static void btdm_2Ant8723ASCOAction(struct rtw_adapter *padapter)
{
	u8 btRssiState, btRssiState1;

	if (btdm_NeedToDecBtPwr(padapter))
		btdm_2AntDecBtPwr(padapter, true);
	else
		btdm_2AntDecBtPwr(padapter, false);

	if (BTDM_IsHT40(padapter)) {
		RTPRINT(FBT, BT_TRACE, ("HT40\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 37, 0);
		/*  fw mechanism */
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
		    (btRssiState == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 high \n"));
			PlatformEFIOWrite1Byte(padapter, 0x883, 0x40);
			btdm_2AntPsTdma(padapter, true, 11);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 low \n"));
			btdm_2AntPsTdma(padapter, true, 15);
		}

		/*  sw mechanism */
		btdm_2AntAgcTable(padapter, false);
		btdm_2AntAdcBackOff(padapter, true);
		btdm_2AntDacSwing(padapter, false, 0xc0);
	} else {
		RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 37, 0);
		btRssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, 27, 0);

		/*  fw mechanism */
		if ((btRssiState1 == BT_RSSI_STATE_HIGH) ||
		    (btRssiState1 == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 high \n"));
			PlatformEFIOWrite1Byte(padapter, 0x883, 0x40);
			btdm_2AntPsTdma(padapter, true, 11);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 low \n"));
			btdm_2AntPsTdma(padapter, true, 15);
		}

		/*  sw mechanism */
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
		    (btRssiState == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			btdm_2AntAgcTable(padapter, true);
			btdm_2AntAdcBackOff(padapter, true);
			btdm_2AntDacSwing(padapter, false, 0xc0);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			btdm_2AntAgcTable(padapter, false);
			btdm_2AntAdcBackOff(padapter, false);
			btdm_2AntDacSwing(padapter, false, 0xc0);
		}
	}
}

static void btdm_2Ant8723AHIDAction(struct rtw_adapter *padapter)
{
	u8 btRssiState, btRssiState1;

	if (btdm_NeedToDecBtPwr(padapter))
		btdm_2AntDecBtPwr(padapter, true);
	else
		btdm_2AntDecBtPwr(padapter, false);

	if (BTDM_IsHT40(padapter)) {
		RTPRINT(FBT, BT_TRACE, ("HT40\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 37, 0);
			/*  fw mechanism */
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
		    (btRssiState == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 high \n"));
			PlatformEFIOWrite1Byte(padapter, 0x883, 0x40);
			btdm_2AntPsTdma(padapter, true, 9);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 low \n"));
			btdm_2AntPsTdma(padapter, true, 13);
		}

		/*  sw mechanism */
		btdm_2AntAgcTable(padapter, false);
		btdm_2AntAdcBackOff(padapter, false);
		btdm_2AntDacSwing(padapter, false, 0xc0);
	} else {
		RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 37, 0);
		btRssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, 27, 0);

		/*  fw mechanism */
		if ((btRssiState1 == BT_RSSI_STATE_HIGH) ||
		    (btRssiState1 == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 high \n"));
			PlatformEFIOWrite1Byte(padapter, 0x883, 0x40);
			btdm_2AntPsTdma(padapter, true, 9);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 low \n"));
			btdm_2AntPsTdma(padapter, true, 13);
		}

		/*  sw mechanism */
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
		    (btRssiState == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			btdm_2AntAgcTable(padapter, true);
			btdm_2AntAdcBackOff(padapter, true);
			btdm_2AntDacSwing(padapter, false, 0xc0);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			btdm_2AntAgcTable(padapter, false);
			btdm_2AntAdcBackOff(padapter, false);
			btdm_2AntDacSwing(padapter, false, 0xc0);
		}
	}
}

/* A2DP only / PAN(EDR) only/ A2DP+PAN(HS) */
static void btdm_2Ant8723AA2DPAction(struct rtw_adapter *padapter)
{
	u8 btRssiState, btRssiState1;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	u8 btInfoExt = pHalData->bt_coexist.halCoex8723.btInfoExt;

	if (btdm_NeedToDecBtPwr(padapter))
		btdm_2AntDecBtPwr(padapter, true);
	else
		btdm_2AntDecBtPwr(padapter, false);

	if (BTDM_IsHT40(padapter)) {
		RTPRINT(FBT, BT_TRACE, ("HT40\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 37, 0);

		/*  fw mechanism */
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
		    (btRssiState == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			PlatformEFIOWrite1Byte(padapter, 0x883, 0x40);

			if (btInfoExt&BIT(0)) {	/* a2dp rate, 1:basic /0:edr */
				RTPRINT(FBT, BT_TRACE, ("a2dp basic rate \n"));
				btdm_2AntTdmaDurationAdjust(padapter, false, false, 3);
			} else {
				RTPRINT(FBT, BT_TRACE, ("a2dp edr rate \n"));
				btdm_2AntTdmaDurationAdjust(padapter, false, false, 1);
			}
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			if (btInfoExt&BIT(0)) {	/* a2dp rate, 1:basic /0:edr */
				RTPRINT(FBT, BT_TRACE, ("a2dp basic rate \n"));
				btdm_2AntTdmaDurationAdjust(padapter, false, true, 3);
			} else {
				RTPRINT(FBT, BT_TRACE, ("a2dp edr rate \n"));
			btdm_2AntTdmaDurationAdjust(padapter, false, true, 1);
			}
		}

		/*  sw mechanism */
		btdm_2AntAgcTable(padapter, false);
		btdm_2AntAdcBackOff(padapter, true);
		btdm_2AntDacSwing(padapter, false, 0xc0);
	} else {
		RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 37, 0);
		btRssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, 27, 0);

		/*  fw mechanism */
		if ((btRssiState1 == BT_RSSI_STATE_HIGH) ||
		    (btRssiState1 == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 high \n"));
			PlatformEFIOWrite1Byte(padapter, 0x883, 0x40);
			if (btInfoExt&BIT(0)) {	/* a2dp rate, 1:basic /0:edr */
				RTPRINT(FBT, BT_TRACE, ("a2dp basic rate \n"));
				btdm_2AntTdmaDurationAdjust(padapter, false, false, 3);
			} else {
				RTPRINT(FBT, BT_TRACE, ("a2dp edr rate \n"));
				btdm_2AntTdmaDurationAdjust(padapter, false, false, 1);
			}
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 low \n"));
			if (btInfoExt&BIT(0)) {	/* a2dp rate, 1:basic /0:edr */
				RTPRINT(FBT, BT_TRACE, ("a2dp basic rate \n"));
				btdm_2AntTdmaDurationAdjust(padapter, false, true, 3);
			} else {
				RTPRINT(FBT, BT_TRACE, ("a2dp edr rate \n"));
				btdm_2AntTdmaDurationAdjust(padapter, false, true, 1);
			}
		}

		/*  sw mechanism */
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
		    (btRssiState == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			btdm_2AntAgcTable(padapter, true);
			btdm_2AntAdcBackOff(padapter, true);
			btdm_2AntDacSwing(padapter, false, 0xc0);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			btdm_2AntAgcTable(padapter, false);
			btdm_2AntAdcBackOff(padapter, false);
			btdm_2AntDacSwing(padapter, false, 0xc0);
		}
	}
}

static void btdm_2Ant8723APANEDRAction(struct rtw_adapter *padapter)
{
	u8 btRssiState, btRssiState1;

	if (btdm_NeedToDecBtPwr(padapter))
		btdm_2AntDecBtPwr(padapter, true);
	else
		btdm_2AntDecBtPwr(padapter, false);

	if (BTDM_IsHT40(padapter)) {
		RTPRINT(FBT, BT_TRACE, ("HT40\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 37, 0);

		/*  fw mechanism */
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
		    (btRssiState == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			PlatformEFIOWrite1Byte(padapter, 0x883, 0x40);
			btdm_2AntPsTdma(padapter, true, 2);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			btdm_2AntPsTdma(padapter, true, 6);
		}

		/*  sw mechanism */
		btdm_2AntAgcTable(padapter, false);
		btdm_2AntAdcBackOff(padapter, true);
		btdm_2AntDacSwing(padapter, false, 0xc0);
	} else {
		RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 37, 0);
		btRssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, 27, 0);

		/*  fw mechanism */
		if ((btRssiState1 == BT_RSSI_STATE_HIGH) ||
		    (btRssiState1 == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 high \n"));
			PlatformEFIOWrite1Byte(padapter, 0x883, 0x40);
			btdm_2AntPsTdma(padapter, true, 2);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 low \n"));
			btdm_2AntPsTdma(padapter, true, 6);
		}

		/*  sw mechanism */
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
		    (btRssiState == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			btdm_2AntAgcTable(padapter, true);
			btdm_2AntAdcBackOff(padapter, true);
			btdm_2AntDacSwing(padapter, false, 0xc0);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			btdm_2AntAgcTable(padapter, false);
			btdm_2AntAdcBackOff(padapter, false);
			btdm_2AntDacSwing(padapter, false, 0xc0);
		}
	}
}

/* PAN(HS) only */
static void btdm_2Ant8723APANHSAction(struct rtw_adapter *padapter)
{
	u8 btRssiState;

	if (BTDM_IsHT40(padapter)) {
		RTPRINT(FBT, BT_TRACE, ("HT40\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 47, 0);
		/*  fw mechanism */
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
		    (btRssiState == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			btdm_2AntDecBtPwr(padapter, true);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			btdm_2AntDecBtPwr(padapter, false);
		}
		btdm_2AntPsTdma(padapter, false, 0);

		/*  sw mechanism */
		btdm_2AntAgcTable(padapter, false);
		btdm_2AntAdcBackOff(padapter, true);
		btdm_2AntDacSwing(padapter, false, 0xc0);
	} else {
		RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 47, 0);

		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
		    (btRssiState == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high\n"));
			/*  fw mechanism */
			btdm_2AntDecBtPwr(padapter, true);
			btdm_2AntPsTdma(padapter, false, 0);

			/*  sw mechanism */
			btdm_2AntAgcTable(padapter, true);
			btdm_2AntAdcBackOff(padapter, true);
			btdm_2AntDacSwing(padapter, false, 0xc0);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low\n"));
			/*  fw mechanism */
			btdm_2AntDecBtPwr(padapter, false);
			btdm_2AntPsTdma(padapter, false, 0);

			/*  sw mechanism */
			btdm_2AntAgcTable(padapter, false);
			btdm_2AntAdcBackOff(padapter, false);
			btdm_2AntDacSwing(padapter, false, 0xc0);
		}
	}
}

/* PAN(EDR)+A2DP */
static void btdm_2Ant8723APANEDRA2DPAction(struct rtw_adapter *padapter)
{
	u8 btRssiState, btRssiState1, btInfoExt;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	btInfoExt = pHalData->bt_coexist.halCoex8723.btInfoExt;

	if (btdm_NeedToDecBtPwr(padapter))
		btdm_2AntDecBtPwr(padapter, true);
	else
		btdm_2AntDecBtPwr(padapter, false);

	if (BTDM_IsHT40(padapter)) {
		RTPRINT(FBT, BT_TRACE, ("HT40\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 37, 0);

		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
		    (btRssiState == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			/*  fw mechanism */
			PlatformEFIOWrite1Byte(padapter, 0x883, 0x40);

			if (btInfoExt&BIT(0)) {	/* a2dp rate, 1:basic /0:edr */
				RTPRINT(FBT, BT_TRACE, ("a2dp basic rate \n"));
				btdm_2AntPsTdma(padapter, true, 4);
			} else {
				RTPRINT(FBT, BT_TRACE, ("a2dp edr rate \n"));
				btdm_2AntPsTdma(padapter, true, 2);
			}
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			/*  fw mechanism */
			if (btInfoExt&BIT(0)) {	/* a2dp rate, 1:basic /0:edr */
				RTPRINT(FBT, BT_TRACE, ("a2dp basic rate \n"));
				btdm_2AntPsTdma(padapter, true, 8);
			} else {
				RTPRINT(FBT, BT_TRACE, ("a2dp edr rate \n"));
				btdm_2AntPsTdma(padapter, true, 6);
			}
		}

		/*  sw mechanism */
		btdm_2AntAgcTable(padapter, false);
		btdm_2AntAdcBackOff(padapter, true);
		btdm_2AntDacSwing(padapter, false, 0xc0);
	} else {
		RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 37, 0);
		btRssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, 27, 0);

		if ((btRssiState1 == BT_RSSI_STATE_HIGH) ||
		    (btRssiState1 == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 high \n"));
			PlatformEFIOWrite1Byte(padapter, 0x883, 0x40);
			/*  fw mechanism */
			if (btInfoExt&BIT(0)) {	/* a2dp rate, 1:basic /0:edr */
				RTPRINT(FBT, BT_TRACE, ("a2dp basic rate \n"));
				btdm_2AntPsTdma(padapter, true, 4);
			} else {
				RTPRINT(FBT, BT_TRACE, ("a2dp edr rate \n"));
				btdm_2AntPsTdma(padapter, true, 2);
			}
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 low \n"));
			/*  fw mechanism */
			if (btInfoExt&BIT(0)) {	/* a2dp rate, 1:basic /0:edr */
				RTPRINT(FBT, BT_TRACE, ("a2dp basic rate \n"));
				btdm_2AntPsTdma(padapter, true, 8);
			} else {
				RTPRINT(FBT, BT_TRACE, ("a2dp edr rate \n"));
				btdm_2AntPsTdma(padapter, true, 6);
			}
		}

		/*  sw mechanism */
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
		    (btRssiState == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			btdm_2AntAgcTable(padapter, true);
			btdm_2AntAdcBackOff(padapter, true);
			btdm_2AntDacSwing(padapter, false, 0xc0);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			btdm_2AntAgcTable(padapter, false);
			btdm_2AntAdcBackOff(padapter, false);
			btdm_2AntDacSwing(padapter, false, 0xc0);
		}
	}
}

static void btdm_2Ant8723APANEDRHIDAction(struct rtw_adapter *padapter)
{
	u8 btRssiState, btRssiState1;

	if (btdm_NeedToDecBtPwr(padapter))
		btdm_2AntDecBtPwr(padapter, true);
	else
		btdm_2AntDecBtPwr(padapter, false);

	if (BTDM_IsHT40(padapter)) {
		RTPRINT(FBT, BT_TRACE, ("HT40\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 37, 0);
		/*  fw mechanism */
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
		    (btRssiState == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			PlatformEFIOWrite1Byte(padapter, 0x883, 0x40);
			btdm_2AntPsTdma(padapter, true, 10);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			btdm_2AntPsTdma(padapter, true, 14);
		}

		/*  sw mechanism */
		btdm_2AntAgcTable(padapter, false);
		btdm_2AntAdcBackOff(padapter, true);
		btdm_2AntDacSwing(padapter, false, 0xc0);
	} else {
		RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 37, 0);
		btRssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, 27, 0);

		/*  fw mechanism */
		if ((btRssiState1 == BT_RSSI_STATE_HIGH) ||
		    (btRssiState1 == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 high \n"));
			PlatformEFIOWrite1Byte(padapter, 0x883, 0x40);
			btdm_2AntPsTdma(padapter, true, 10);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 low \n"));
			btdm_2AntPsTdma(padapter, true, 14);
		}

		/*  sw mechanism */
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
		    (btRssiState == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			btdm_2AntAgcTable(padapter, true);
			btdm_2AntAdcBackOff(padapter, true);
			btdm_2AntDacSwing(padapter, false, 0xc0);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			btdm_2AntAgcTable(padapter, false);
			btdm_2AntAdcBackOff(padapter, false);
			btdm_2AntDacSwing(padapter, false, 0xc0);
		}
	}
}

/*  HID+A2DP+PAN(EDR) */
static void btdm_2Ant8723AHIDA2DPPANEDRAction(struct rtw_adapter *padapter)
{
	u8 btRssiState, btRssiState1, btInfoExt;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	btInfoExt = pHalData->bt_coexist.halCoex8723.btInfoExt;

	if (btdm_NeedToDecBtPwr(padapter))
		btdm_2AntDecBtPwr(padapter, true);
	else
		btdm_2AntDecBtPwr(padapter, false);

	if (BTDM_IsHT40(padapter)) {
		RTPRINT(FBT, BT_TRACE, ("HT40\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 37, 0);
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
		    (btRssiState == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			PlatformEFIOWrite1Byte(padapter, 0x883, 0x40);

			if (btInfoExt&BIT(0)) {	/* a2dp rate, 1:basic /0:edr */
				RTPRINT(FBT, BT_TRACE, ("a2dp basic rate \n"));
				btdm_2AntPsTdma(padapter, true, 12);
			} else {
				RTPRINT(FBT, BT_TRACE, ("a2dp edr rate \n"));
				btdm_2AntPsTdma(padapter, true, 10);
			}
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			if (btInfoExt&BIT(0)) {	/* a2dp rate, 1:basic /0:edr */
				RTPRINT(FBT, BT_TRACE, ("a2dp basic rate \n"));
				btdm_2AntPsTdma(padapter, true, 16);
			} else {
				RTPRINT(FBT, BT_TRACE, ("a2dp edr rate \n"));
				btdm_2AntPsTdma(padapter, true, 14);
			}
		}

		/*  sw mechanism */
		btdm_2AntAgcTable(padapter, false);
		btdm_2AntAdcBackOff(padapter, true);
		btdm_2AntDacSwing(padapter, false, 0xc0);
	} else {
		RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
		btRssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, 37, 0);
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 27, 0);
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
		    (btRssiState == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			PlatformEFIOWrite1Byte(padapter, 0x883, 0x40);

			if (btInfoExt&BIT(0)) {	/* a2dp rate, 1:basic /0:edr */
				RTPRINT(FBT, BT_TRACE, ("a2dp basic rate \n"));
				btdm_2AntPsTdma(padapter, true, 12);
			} else {
				RTPRINT(FBT, BT_TRACE, ("a2dp edr rate \n"));
				btdm_2AntPsTdma(padapter, true, 10);
			}
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			if (btInfoExt&BIT(0)) {	/* a2dp rate, 1:basic /0:edr */
				RTPRINT(FBT, BT_TRACE, ("a2dp basic rate \n"));
				btdm_2AntPsTdma(padapter, true, 16);
			} else {
				RTPRINT(FBT, BT_TRACE, ("a2dp edr rate \n"));
				btdm_2AntPsTdma(padapter, true, 14);
			}
		}

		/*  sw mechanism */
		if ((btRssiState1 == BT_RSSI_STATE_HIGH) ||
		    (btRssiState1 == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 high \n"));
			btdm_2AntAgcTable(padapter, true);
			btdm_2AntAdcBackOff(padapter, true);
			btdm_2AntDacSwing(padapter, false, 0xc0);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 high \n"));
			btdm_2AntAgcTable(padapter, false);
			btdm_2AntAdcBackOff(padapter, false);
			btdm_2AntDacSwing(padapter, false, 0xc0);
		}
	}
}

static void btdm_2Ant8723AHIDA2DPAction(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	u8 btRssiState, btRssiState1, btInfoExt;

	btInfoExt = pHalData->bt_coexist.halCoex8723.btInfoExt;

	if (btdm_NeedToDecBtPwr(padapter))
		btdm_2AntDecBtPwr(padapter, true);
	else
		btdm_2AntDecBtPwr(padapter, false);

	if (BTDM_IsHT40(padapter)) {
		RTPRINT(FBT, BT_TRACE, ("HT40\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 37, 0);
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
		    (btRssiState == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			PlatformEFIOWrite1Byte(padapter, 0x883, 0x40);

			if (btInfoExt&BIT(0)) {	/* a2dp rate, 1:basic /0:edr */
				RTPRINT(FBT, BT_TRACE, ("a2dp basic rate \n"));
				btdm_2AntTdmaDurationAdjust(padapter, true, false, 3);
			} else {
				RTPRINT(FBT, BT_TRACE, ("a2dp edr rate \n"));
				btdm_2AntTdmaDurationAdjust(padapter, true, false, 1);
			}
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			if (btInfoExt&BIT(0)) {	/* a2dp rate, 1:basic /0:edr */
				RTPRINT(FBT, BT_TRACE, ("a2dp basic rate \n"));
				btdm_2AntTdmaDurationAdjust(padapter, true, true, 3);
			} else {
				RTPRINT(FBT, BT_TRACE, ("a2dp edr rate \n"));
				btdm_2AntTdmaDurationAdjust(padapter, true, true, 1);
			}
		}
		/*  sw mechanism */
		btdm_2AntAgcTable(padapter, false);
		btdm_2AntAdcBackOff(padapter, true);
		btdm_2AntDacSwing(padapter, false, 0xc0);
	} else {
		RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 37, 0);
		btRssiState1 = BTDM_CheckCoexRSSIState(padapter, 2, 27, 0);

		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
		    (btRssiState == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			PlatformEFIOWrite1Byte(padapter, 0x883, 0x40);

			if (btInfoExt&BIT(0)) {	/* a2dp rate, 1:basic /0:edr */
				RTPRINT(FBT, BT_TRACE, ("a2dp basic rate \n"));
				btdm_2AntTdmaDurationAdjust(padapter, true, false, 3);
			} else {
				RTPRINT(FBT, BT_TRACE, ("a2dp edr rate \n"));
				btdm_2AntTdmaDurationAdjust(padapter, true, false, 1);
			}
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			if (btInfoExt&BIT(0)) {	/* a2dp rate, 1:basic /0:edr */
				RTPRINT(FBT, BT_TRACE, ("a2dp basic rate \n"));
				btdm_2AntTdmaDurationAdjust(padapter, true, true, 3);
			} else {
				RTPRINT(FBT, BT_TRACE, ("a2dp edr rate \n"));
				btdm_2AntTdmaDurationAdjust(padapter, true, true, 1);
			}
		}
		if ((btRssiState1 == BT_RSSI_STATE_HIGH) ||
		    (btRssiState1 == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 high \n"));
			/*  sw mechanism */
			btdm_2AntAgcTable(padapter, true);
			btdm_2AntAdcBackOff(padapter, true);
			btdm_2AntDacSwing(padapter, false, 0xc0);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 low \n"));
			/*  sw mechanism */
			btdm_2AntAgcTable(padapter, false);
			btdm_2AntAdcBackOff(padapter, false);
			btdm_2AntDacSwing(padapter, false, 0xc0);
		}
	}
}

static void btdm_2Ant8723AA2dp(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	u8 btRssiState, btRssiState1, btInfoExt;

	btInfoExt = pHalData->bt_coexist.halCoex8723.btInfoExt;

	if (btdm_NeedToDecBtPwr(padapter))
		btdm_2AntDecBtPwr(padapter, true);
	else
		btdm_2AntDecBtPwr(padapter, false);
	/*  coex table */
	btdm_2AntCoexTable(padapter, 0x55555555, 0xffff, 0x3);
	btdm_2AntIgnoreWlanAct(padapter, false);

	if (BTDM_IsHT40(padapter)) {
		RTPRINT(FBT, BT_TRACE, ("HT40\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 37, 0);
		/*  fw mechanism */
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
		    (btRssiState == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 high \n"));
			PlatformEFIOWrite1Byte(padapter, 0x883, 0x40);
			btdm_2AntTdmaDurationAdjust(padapter, false, false, 1);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 low \n"));
			btdm_2AntTdmaDurationAdjust(padapter, false, true, 1);
		}

		/*  sw mechanism */
		btdm_2AntAgcTable(padapter, false);
		btdm_2AntAdcBackOff(padapter, true);
		btdm_2AntDacSwing(padapter, false, 0xc0);
	} else {
		RTPRINT(FBT, BT_TRACE, ("HT20 or Legacy\n"));
		btRssiState = BTDM_CheckCoexRSSIState(padapter, 2, 47, 0);
		btRssiState1 = BTDM_CheckCoexRSSIState1(padapter, 2, 27, 0);

		/*  fw mechanism */
		if ((btRssiState1 == BT_RSSI_STATE_HIGH) ||
		    (btRssiState1 == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 high \n"));
			PlatformEFIOWrite1Byte(padapter, 0x883, 0x40);
			btdm_2AntTdmaDurationAdjust(padapter, false, false, 1);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi-1 low \n"));
			btdm_2AntTdmaDurationAdjust(padapter, false, true, 1);
		}

		/*  sw mechanism */
		if ((btRssiState == BT_RSSI_STATE_HIGH) ||
		    (btRssiState == BT_RSSI_STATE_STAY_HIGH)) {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi high \n"));
			btdm_2AntAgcTable(padapter, true);
			btdm_2AntAdcBackOff(padapter, true);
			btdm_2AntDacSwing(padapter, false, 0xc0);
		} else {
			RTPRINT(FBT, BT_TRACE, ("Wifi rssi low \n"));
			btdm_2AntAgcTable(padapter, false);
			btdm_2AntAdcBackOff(padapter, false);
			btdm_2AntDacSwing(padapter, false, 0xc0);
		}
	}
}

/*  extern function start with BTDM_ */
static void BTDM_2AntParaInit(struct rtw_adapter *padapter)
{

	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct btdm_8723a_2ant *pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm2Ant;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], 2Ant Parameter Init!!\n"));

	/*  Enable counter statistics */
	rtl8723au_write8(padapter, 0x76e, 0x4);
	rtl8723au_write8(padapter, 0x778, 0x3);
	rtl8723au_write8(padapter, 0x40, 0x20);

	/*  force to reset coex mechanism */
	pBtdm8723->preVal0x6c0 = 0x0;
	btdm_2AntCoexTable(padapter, 0x55555555, 0xffff, 0x3);

	pBtdm8723->bPrePsTdmaOn = true;
	btdm_2AntPsTdma(padapter, false, 0);

	pBtdm8723->preFwDacSwingLvl = 0x10;
	btdm_2AntFwDacSwingLvl(padapter, 0x20);

	pBtdm8723->bPreDecBtPwr = true;
	btdm_2AntDecBtPwr(padapter, false);

	pBtdm8723->bPreAgcTableEn = true;
	btdm_2AntAgcTable(padapter, false);

	pBtdm8723->bPreAdcBackOff = true;
	btdm_2AntAdcBackOff(padapter, false);

	pBtdm8723->bPreLowPenaltyRa = true;
	btdm_2AntLowPenaltyRa(padapter, false);

	pBtdm8723->bPreRfRxLpfShrink = true;
	btdm_2AntRfShrink(padapter, false);

	pBtdm8723->bPreDacSwingOn = true;
	btdm_2AntDacSwing(padapter, false, 0xc0);

	pBtdm8723->bPreIgnoreWlanAct = true;
	btdm_2AntIgnoreWlanAct(padapter, false);
}

static void BTDM_2AntHwCoexAllOff8723A(struct rtw_adapter *padapter)
{
	btdm_2AntCoexTable(padapter, 0x55555555, 0xffff, 0x3);
}

static void BTDM_2AntFwCoexAllOff8723A(struct rtw_adapter *padapter)
{
	btdm_2AntIgnoreWlanAct(padapter, false);
	btdm_2AntPsTdma(padapter, false, 0);
	btdm_2AntFwDacSwingLvl(padapter, 0x20);
	btdm_2AntDecBtPwr(padapter, false);
}

static void BTDM_2AntSwCoexAllOff8723A(struct rtw_adapter *padapter)
{
	btdm_2AntAgcTable(padapter, false);
	btdm_2AntAdcBackOff(padapter, false);
	btdm_2AntLowPenaltyRa(padapter, false);
	btdm_2AntRfShrink(padapter, false);
	btdm_2AntDacSwing(padapter, false, 0xc0);
}

static void BTDM_2AntFwC2hBtInfo8723A(struct rtw_adapter *padapter)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct btdm_8723a_2ant *pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm2Ant;
	u8 btInfo = 0;
	u8 algorithm = BT_2ANT_COEX_ALGO_UNDEFINED;
	u8 bBtLinkExist = false, bBtHsModeExist = false;

	btInfo = pHalData->bt_coexist.halCoex8723.c2hBtInfoOriginal;
	pBtdm8723->btStatus = BT_2ANT_BT_STATUS_IDLE;

	/*  check BIT2 first ==> check if bt is under inquiry or page scan */
	if (btInfo & BIT(2)) {
		if (!pHalData->bt_coexist.halCoex8723.bC2hBtInquiryPage) {
			pBtMgnt->ExtConfig.bHoldForBtOperation = true;
			pBtMgnt->ExtConfig.bHoldPeriodCnt = 1;
			btdm_2AntBtInquiryPage(padapter);
		} else {
			pBtMgnt->ExtConfig.bHoldPeriodCnt++;
			btdm_HoldForBtInqPage(padapter);
		}
		pHalData->bt_coexist.halCoex8723.bC2hBtInquiryPage = true;

	} else {
		pHalData->bt_coexist.halCoex8723.bC2hBtInquiryPage = false;
		pBtMgnt->ExtConfig.bHoldForBtOperation = false;
		pBtMgnt->ExtConfig.bHoldPeriodCnt = 0;

	}
	RTPRINT(FBT, BT_TRACE,
		("[BTC2H], pHalData->bt_coexist.halCoex8723.bC2hBtInquiryPage =%x pBtMgnt->ExtConfig.bHoldPeriodCnt =%x pBtMgnt->ExtConfig.bHoldForBtOperation =%x\n",
		pHalData->bt_coexist.halCoex8723.bC2hBtInquiryPage,
		pBtMgnt->ExtConfig.bHoldPeriodCnt,
		pBtMgnt->ExtConfig.bHoldForBtOperation));

	RTPRINT(FBT, BT_TRACE,
		("[BTC2H],   btInfo =%x   pHalData->bt_coexist.halCoex8723.c2hBtInfoOriginal =%x\n",
		btInfo, pHalData->bt_coexist.halCoex8723.c2hBtInfoOriginal));
	if (btInfo&BT_INFO_ACL) {
		RTPRINT(FBT, BT_TRACE, ("[BTC2H], BTInfo: bConnect = true   btInfo =%x\n", btInfo));
		bBtLinkExist = true;
		if (((btInfo&(BT_INFO_FTP|BT_INFO_A2DP|BT_INFO_HID|BT_INFO_SCO_BUSY)) != 0) ||
		    pHalData->bt_coexist.halCoex8723.btRetryCnt > 0) {
			pBtdm8723->btStatus = BT_2ANT_BT_STATUS_NON_IDLE;
		} else {
			pBtdm8723->btStatus = BT_2ANT_BT_STATUS_CONNECTED_IDLE;
		}

		if (btInfo&BT_INFO_SCO || btInfo&BT_INFO_SCO_BUSY) {
			if (btInfo&BT_INFO_FTP || btInfo&BT_INFO_A2DP || btInfo&BT_INFO_HID) {
				switch (btInfo&0xe0) {
				case BT_INFO_HID:
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], SCO + HID\n"));
					algorithm = BT_2ANT_COEX_ALGO_HID;
					break;
				case BT_INFO_A2DP:
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], Error!!! SCO + A2DP\n"));
					break;
				case BT_INFO_FTP:
					if (bBtHsModeExist) {
						RTPRINT(FBT, BT_TRACE, ("[BTCoex], SCO + PAN(HS)\n"));
						algorithm = BT_2ANT_COEX_ALGO_SCO;
					} else {
						RTPRINT(FBT, BT_TRACE, ("[BTCoex], SCO + PAN(EDR)\n"));
						algorithm = BT_2ANT_COEX_ALGO_PANEDR_HID;
					}
					break;
				case (BT_INFO_HID | BT_INFO_A2DP):
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], HID + A2DP\n"));
					algorithm = BT_2ANT_COEX_ALGO_HID_A2DP;
					break;
				case (BT_INFO_HID | BT_INFO_FTP):
					if (bBtHsModeExist) {
						RTPRINT(FBT, BT_TRACE, ("[BTCoex], HID + PAN(HS)\n"));
						algorithm = BT_2ANT_COEX_ALGO_HID_A2DP;
					} else {
						RTPRINT(FBT, BT_TRACE, ("[BTCoex], HID + PAN(EDR)\n"));
						algorithm = BT_2ANT_COEX_ALGO_PANEDR_HID;
					}
					break;
				case (BT_INFO_A2DP | BT_INFO_FTP):
					if (bBtHsModeExist) {
						RTPRINT(FBT, BT_TRACE, ("[BTCoex], A2DP + PAN(HS)\n"));
						algorithm = BT_2ANT_COEX_ALGO_A2DP;
					} else {
						RTPRINT(FBT, BT_TRACE, ("[BTCoex], A2DP + PAN(EDR)\n"));
						algorithm = BT_2ANT_COEX_ALGO_PANEDR_A2DP;
					}
					break;
				case (BT_INFO_HID | BT_INFO_A2DP | BT_INFO_FTP):
					if (bBtHsModeExist) {
						RTPRINT(FBT, BT_TRACE, ("[BTCoex], HID + A2DP + PAN(HS)\n"));
						algorithm = BT_2ANT_COEX_ALGO_HID_A2DP;
					} else {
						RTPRINT(FBT, BT_TRACE, ("[BTCoex], HID + A2DP + PAN(EDR)\n"));
						algorithm = BT_2ANT_COEX_ALGO_HID_A2DP_PANEDR;
					}
					break;
				}
			} else {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], SCO only\n"));
				algorithm = BT_2ANT_COEX_ALGO_SCO;
			}
		} else {
			RTPRINT(FBT, BT_TRACE, ("[BTCoex], non SCO\n"));
			switch (btInfo&0xe0) {
			case BT_INFO_HID:
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], HID\n"));
				algorithm = BT_2ANT_COEX_ALGO_HID;
				break;
			case BT_INFO_A2DP:
				RTPRINT(FBT, BT_TRACE, ("[BTCoex],  A2DP\n"));
				algorithm = BT_2ANT_COEX_ALGO_A2DP;
				break;
			case BT_INFO_FTP:
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], PAN(EDR)\n"));
				algorithm = BT_2ANT_COEX_ALGO_PANEDR_HID;
				break;
			case (BT_INFO_HID | BT_INFO_A2DP):
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], HID + A2DP\n"));
				algorithm = BT_2ANT_COEX_ALGO_HID_A2DP;
				break;
			case (BT_INFO_HID|BT_INFO_FTP):
				if (bBtHsModeExist) {
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], HID + PAN(HS)\n"));
					algorithm = BT_2ANT_COEX_ALGO_HID_A2DP;
				} else {
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], HID + PAN(EDR)\n"));
					algorithm = BT_2ANT_COEX_ALGO_PANEDR_HID;
				}
				break;
			case (BT_INFO_A2DP|BT_INFO_FTP):
				if (bBtHsModeExist) {
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], A2DP + PAN(HS)\n"));
					algorithm = BT_2ANT_COEX_ALGO_A2DP;
				} else {
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], A2DP + PAN(EDR)\n"));
					algorithm = BT_2ANT_COEX_ALGO_PANEDR_A2DP;
				}
				break;
			case (BT_INFO_HID|BT_INFO_A2DP|BT_INFO_FTP):
				if (bBtHsModeExist) {
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], HID + A2DP + PAN(HS)\n"));
					algorithm = BT_2ANT_COEX_ALGO_HID_A2DP;
				} else {
					RTPRINT(FBT, BT_TRACE, ("[BTCoex], HID + A2DP + PAN(EDR)\n"));
					algorithm = BT_2ANT_COEX_ALGO_HID_A2DP_PANEDR;
				}
				break;
			}

		}
	} else {
		RTPRINT(FBT, BT_TRACE, ("[BTC2H], BTInfo: bConnect = false\n"));
		pBtdm8723->btStatus = BT_2ANT_BT_STATUS_IDLE;
	}

	pBtdm8723->curAlgorithm = algorithm;
	RTPRINT(FBT, BT_TRACE, ("[BTCoex], Algorithm = %d \n", pBtdm8723->curAlgorithm));

/* From */
	BTDM_CheckWiFiState(padapter);
	if (pBtMgnt->ExtConfig.bManualControl) {
		RTPRINT(FBT, BT_TRACE, ("Action Manual control, won't execute bt coexist mechanism!!\n"));
		return;
	}
}

void BTDM_2AntBtCoexist8723A(struct rtw_adapter *padapter)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	struct bt_dgb *pBtDbg = &pBTInfo->BtDbg;
	u8 btInfoOriginal = 0;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct btdm_8723a_2ant *pBtdm8723 = &pHalData->bt_coexist.halCoex8723.btdm2Ant;

	if (BTDM_BtProfileSupport(padapter)) {
		if (pBtMgnt->ExtConfig.bHoldForBtOperation) {
			RTPRINT(FBT, BT_TRACE, ("Action for BT Operation adjust!!\n"));
			return;
		}
		if (pBtMgnt->ExtConfig.bHoldPeriodCnt) {
			RTPRINT(FBT, BT_TRACE, ("Hold BT inquiry/page scan setting (cnt = %d)!!\n",
				pBtMgnt->ExtConfig.bHoldPeriodCnt));
			if (pBtMgnt->ExtConfig.bHoldPeriodCnt >= 11) {
				pBtMgnt->ExtConfig.bHoldPeriodCnt = 0;
				/*  next time the coexist parameters should be reset again. */
			} else {
				pBtMgnt->ExtConfig.bHoldPeriodCnt++;
			}
			return;
		}

		if (pBtDbg->dbgCtrl)
			RTPRINT(FBT, BT_TRACE, ("[Dbg control], "));

		pBtdm8723->curAlgorithm = btdm_ActionAlgorithm(padapter);
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], Algorithm = %d \n", pBtdm8723->curAlgorithm));

		if (btdm_Is2Ant8723ACommonAction(padapter)) {
			RTPRINT(FBT, BT_TRACE, ("Action 2-Ant common.\n"));
			pBtdm8723->bResetTdmaAdjust = true;
		} else {
			if (pBtdm8723->curAlgorithm != pBtdm8723->preAlgorithm) {
				RTPRINT(FBT, BT_TRACE, ("[BTCoex], preAlgorithm =%d, curAlgorithm =%d\n",
				pBtdm8723->preAlgorithm, pBtdm8723->curAlgorithm));
				pBtdm8723->bResetTdmaAdjust = true;
			}
			switch (pBtdm8723->curAlgorithm) {
			case BT_2ANT_COEX_ALGO_SCO:
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant, algorithm = SCO.\n"));
				btdm_2Ant8723ASCOAction(padapter);
				break;
			case BT_2ANT_COEX_ALGO_HID:
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant, algorithm = HID.\n"));
				btdm_2Ant8723AHIDAction(padapter);
				break;
			case BT_2ANT_COEX_ALGO_A2DP:
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant, algorithm = A2DP.\n"));
				btdm_2Ant8723AA2DPAction(padapter);
				break;
			case BT_2ANT_COEX_ALGO_PANEDR:
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant, algorithm = PAN(EDR).\n"));
				btdm_2Ant8723APANEDRAction(padapter);
				break;
			case BT_2ANT_COEX_ALGO_PANHS:
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant, algorithm = HS mode.\n"));
				btdm_2Ant8723APANHSAction(padapter);
				break;
			case BT_2ANT_COEX_ALGO_PANEDR_A2DP:
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant, algorithm = PAN+A2DP.\n"));
				btdm_2Ant8723APANEDRA2DPAction(padapter);
				break;
			case BT_2ANT_COEX_ALGO_PANEDR_HID:
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant, algorithm = PAN(EDR)+HID.\n"));
				btdm_2Ant8723APANEDRHIDAction(padapter);
				break;
			case BT_2ANT_COEX_ALGO_HID_A2DP_PANEDR:
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant, algorithm = HID+A2DP+PAN.\n"));
				btdm_2Ant8723AHIDA2DPPANEDRAction(padapter);
				break;
			case BT_2ANT_COEX_ALGO_HID_A2DP:
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant, algorithm = HID+A2DP.\n"));
				btdm_2Ant8723AHIDA2DPAction(padapter);
				break;
			default:
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant, algorithm = 0.\n"));
				btdm_2Ant8723AA2DPAction(padapter);
				break;
			}
			pBtdm8723->preAlgorithm = pBtdm8723->curAlgorithm;
		}
	} else {
		RTPRINT(FBT, BT_TRACE, ("[BTCoex] Get bt info by fw!!\n"));
		/* msg shows c2h rsp for bt_info is received or not. */
		if (pHalData->bt_coexist.halCoex8723.bC2hBtInfoReqSent)
			RTPRINT(FBT, BT_TRACE, ("[BTCoex] c2h for btInfo not rcvd yet!!\n"));

		btInfoOriginal = pHalData->bt_coexist.halCoex8723.c2hBtInfoOriginal;

		if (pBtMgnt->ExtConfig.bHoldForBtOperation) {
			RTPRINT(FBT, BT_TRACE, ("Action for BT Operation adjust!!\n"));
			return;
		}
		if (pBtMgnt->ExtConfig.bHoldPeriodCnt) {
			RTPRINT(FBT, BT_TRACE,
				("Hold BT inquiry/page scan setting (cnt = %d)!!\n",
				pBtMgnt->ExtConfig.bHoldPeriodCnt));
			if (pBtMgnt->ExtConfig.bHoldPeriodCnt >= 11) {
				pBtMgnt->ExtConfig.bHoldPeriodCnt = 0;
				/*  next time the coexist parameters should be reset again. */
			} else {
				 pBtMgnt->ExtConfig.bHoldPeriodCnt++;
			}
			return;
		}

		if (pBtDbg->dbgCtrl)
			RTPRINT(FBT, BT_TRACE, ("[Dbg control], "));
		if (btdm_Is2Ant8723ACommonAction(padapter)) {
			RTPRINT(FBT, BT_TRACE, ("Action 2-Ant common.\n"));
			pBtdm8723->bResetTdmaAdjust = true;
		} else {
			if (pBtdm8723->curAlgorithm != pBtdm8723->preAlgorithm) {
				RTPRINT(FBT, BT_TRACE,
					("[BTCoex], preAlgorithm =%d, curAlgorithm =%d\n",
					pBtdm8723->preAlgorithm,
					pBtdm8723->curAlgorithm));
				pBtdm8723->bResetTdmaAdjust = true;
			}
			switch (pBtdm8723->curAlgorithm) {
			case BT_2ANT_COEX_ALGO_SCO:
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant, algorithm = SCO.\n"));
				btdm_2Ant8723ASCOAction(padapter);
				break;
			case BT_2ANT_COEX_ALGO_HID:
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant, algorithm = HID.\n"));
				btdm_2Ant8723AHIDAction(padapter);
				break;
			case BT_2ANT_COEX_ALGO_A2DP:
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant, algorithm = A2DP.\n"));
				btdm_2Ant8723AA2dp(padapter);
				break;
			case BT_2ANT_COEX_ALGO_PANEDR:
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant, algorithm = PAN(EDR).\n"));
				btdm_2Ant8723APANEDRAction(padapter);
				break;
			case BT_2ANT_COEX_ALGO_PANHS:
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant, algorithm = HS mode.\n"));
				btdm_2Ant8723APANHSAction(padapter);
				break;
			case BT_2ANT_COEX_ALGO_PANEDR_A2DP:
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant, algorithm = PAN+A2DP.\n"));
				btdm_2Ant8723APANEDRA2DPAction(padapter);
				break;
			case BT_2ANT_COEX_ALGO_PANEDR_HID:
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant, algorithm = PAN(EDR)+HID.\n"));
				btdm_2Ant8723APANEDRHIDAction(padapter);
				break;
			case BT_2ANT_COEX_ALGO_HID_A2DP_PANEDR:
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant, algorithm = HID+A2DP+PAN.\n"));
				btdm_2Ant8723AHIDA2DPPANEDRAction(padapter);
				break;
			case BT_2ANT_COEX_ALGO_HID_A2DP:
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant, algorithm = HID+A2DP.\n"));
				btdm_2Ant8723AHIDA2DPAction(padapter);
				break;
			default:
				RTPRINT(FBT, BT_TRACE, ("Action 2-Ant, algorithm = 0.\n"));
				btdm_2Ant8723AA2DPAction(padapter);
				break;
			}
			pBtdm8723->preAlgorithm = pBtdm8723->curAlgorithm;
		}
	}
}

/*  ===== End of sync from SD7 driver HAL/BTCoexist/HalBtc87232Ant.c ===== */

/*  ===== Below this line is sync from SD7 driver HAL/BTCoexist/HalBtc8723.c ===== */

static u8 btCoexDbgBuf[BT_TMP_BUF_SIZE];

static const char *const BtProfileString[] = {
	"NONE",
	"A2DP",
	"PAN",
	"HID",
	"SCO",
};

static const char *const BtSpecString[] = {
	"1.0b",
	"1.1",
	"1.2",
	"2.0+EDR",
	"2.1+EDR",
	"3.0+HS",
	"4.0",
};

static const char *const BtLinkRoleString[] = {
	"Master",
	"Slave",
};

static u8 btdm_BtWifiAntNum(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct bt_coexist_8723a *pBtCoex = &pHalData->bt_coexist.halCoex8723;

	if (Ant_x2 == pHalData->bt_coexist.BT_Ant_Num) {
		if (Ant_x2 == pBtCoex->TotalAntNum)
			return Ant_x2;
		else
			return Ant_x1;
	} else {
		return Ant_x1;
	}
	return Ant_x2;
}

static void btdm_BtHwCountersMonitor(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	u32	regHPTxRx, regLPTxRx, u4Tmp;
	u32	regHPTx = 0, regHPRx = 0, regLPTx = 0, regLPRx = 0;

	regHPTxRx = REG_HIGH_PRIORITY_TXRX;
	regLPTxRx = REG_LOW_PRIORITY_TXRX;

	u4Tmp = rtl8723au_read32(padapter, regHPTxRx);
	regHPTx = u4Tmp & bMaskLWord;
	regHPRx = (u4Tmp & bMaskHWord)>>16;

	u4Tmp = rtl8723au_read32(padapter, regLPTxRx);
	regLPTx = u4Tmp & bMaskLWord;
	regLPRx = (u4Tmp & bMaskHWord)>>16;

	pHalData->bt_coexist.halCoex8723.highPriorityTx = regHPTx;
	pHalData->bt_coexist.halCoex8723.highPriorityRx = regHPRx;
	pHalData->bt_coexist.halCoex8723.lowPriorityTx = regLPTx;
	pHalData->bt_coexist.halCoex8723.lowPriorityRx = regLPRx;

	RTPRINT(FBT, BT_TRACE, ("High Priority Tx/Rx = %d / %d\n", regHPTx, regHPRx));
	RTPRINT(FBT, BT_TRACE, ("Low Priority Tx/Rx = %d / %d\n", regLPTx, regLPRx));

	/*  reset counter */
	rtl8723au_write8(padapter, 0x76e, 0xc);
}

/*  This function check if 8723 bt is disabled */
static void btdm_BtEnableDisableCheck8723A(struct rtw_adapter *padapter)
{
	u8 btAlife = true;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

#ifdef CHECK_BT_EXIST_FROM_REG
	u8 val8;

	/*  ox68[28]= 1 => BT enable; otherwise disable */
	val8 = rtl8723au_read8(padapter, 0x6B);
	if (!(val8 & BIT(4)))
		btAlife = false;

	if (btAlife)
		pHalData->bt_coexist.bCurBtDisabled = false;
	else
		pHalData->bt_coexist.bCurBtDisabled = true;
#else
	if (pHalData->bt_coexist.halCoex8723.highPriorityTx == 0 &&
	    pHalData->bt_coexist.halCoex8723.highPriorityRx == 0 &&
	    pHalData->bt_coexist.halCoex8723.lowPriorityTx == 0 &&
	    pHalData->bt_coexist.halCoex8723.lowPriorityRx == 0)
		btAlife = false;
	if (pHalData->bt_coexist.halCoex8723.highPriorityTx == 0xeaea &&
	    pHalData->bt_coexist.halCoex8723.highPriorityRx == 0xeaea &&
	    pHalData->bt_coexist.halCoex8723.lowPriorityTx == 0xeaea &&
	    pHalData->bt_coexist.halCoex8723.lowPriorityRx == 0xeaea)
		btAlife = false;
	if (pHalData->bt_coexist.halCoex8723.highPriorityTx == 0xffff &&
	    pHalData->bt_coexist.halCoex8723.highPriorityRx == 0xffff &&
	    pHalData->bt_coexist.halCoex8723.lowPriorityTx == 0xffff &&
	    pHalData->bt_coexist.halCoex8723.lowPriorityRx == 0xffff)
		btAlife = false;
	if (btAlife) {
		pHalData->bt_coexist.btActiveZeroCnt = 0;
		pHalData->bt_coexist.bCurBtDisabled = false;
		RTPRINT(FBT, BT_TRACE, ("8723A BT is enabled !!\n"));
	} else {
		pHalData->bt_coexist.btActiveZeroCnt++;
		RTPRINT(FBT, BT_TRACE, ("8723A bt all counters = 0, %d times!!\n",
				pHalData->bt_coexist.btActiveZeroCnt));
		if (pHalData->bt_coexist.btActiveZeroCnt >= 2) {
			pHalData->bt_coexist.bCurBtDisabled = true;
			RTPRINT(FBT, BT_TRACE, ("8723A BT is disabled !!\n"));
		}
	}
#endif

	if (!pHalData->bt_coexist.bCurBtDisabled) {
		if (BTDM_IsWifiConnectionExist(padapter))
			BTDM_SetFwChnlInfo(padapter, RT_MEDIA_CONNECT);
		else
			BTDM_SetFwChnlInfo(padapter, RT_MEDIA_DISCONNECT);
	}

	if (pHalData->bt_coexist.bPreBtDisabled !=
	    pHalData->bt_coexist.bCurBtDisabled) {
		RTPRINT(FBT, BT_TRACE, ("8723A BT is from %s to %s!!\n",
			(pHalData->bt_coexist.bPreBtDisabled ? "disabled":"enabled"),
			(pHalData->bt_coexist.bCurBtDisabled ? "disabled":"enabled")));
		pHalData->bt_coexist.bPreBtDisabled = pHalData->bt_coexist.bCurBtDisabled;
	}
}

static void btdm_BTCoexist8723AHandler(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData;

	pHalData = GET_HAL_DATA(padapter);

	if (btdm_BtWifiAntNum(padapter) == Ant_x2) {
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], 2 Ant mechanism\n"));
		BTDM_2AntBtCoexist8723A(padapter);
	} else {
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], 1 Ant mechanism\n"));
		BTDM_1AntBtCoexist8723A(padapter);
	}

	if (!BTDM_IsSameCoexistState(padapter)) {
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], Coexist State[bitMap] change from 0x%"i64fmt"x to 0x%"i64fmt"x\n",
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

/*  extern function start with BTDM_ */
u32 BTDM_BtTxRxCounterH(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	u32	counters = 0;

	counters = pHalData->bt_coexist.halCoex8723.highPriorityTx+
		pHalData->bt_coexist.halCoex8723.highPriorityRx;
	return counters;
}

u32 BTDM_BtTxRxCounterL(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	u32	counters = 0;

	counters = pHalData->bt_coexist.halCoex8723.lowPriorityTx+
		pHalData->bt_coexist.halCoex8723.lowPriorityRx;
	return counters;
}

void BTDM_SetFwChnlInfo(struct rtw_adapter *padapter, enum rt_media_status mstatus)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	u8 H2C_Parameter[3] = {0};
	u8 chnl;

	/*  opMode */
	if (RT_MEDIA_CONNECT == mstatus)
		H2C_Parameter[0] = 0x1;	/*  0: disconnected, 1:connected */

	if (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE)) {
		/*  channel */
		chnl = pmlmeext->cur_channel;
		if (BTDM_IsHT40(padapter)) {
			if (pmlmeext->cur_ch_offset == HAL_PRIME_CHNL_OFFSET_UPPER)
				chnl -= 2;
			else if (pmlmeext->cur_ch_offset == HAL_PRIME_CHNL_OFFSET_LOWER)
				chnl += 2;
		}
		H2C_Parameter[1] = chnl;
	} else {	/*  check if HS link is exists */
		/*  channel */
		if (BT_Operation(padapter))
			H2C_Parameter[1] = pBtMgnt->BTChannel;
		else
			H2C_Parameter[1] = pmlmeext->cur_channel;
	}

	if (BTDM_IsHT40(padapter))
		H2C_Parameter[2] = 0x30;
	else
		H2C_Parameter[2] = 0x20;

	FillH2CCmd(padapter, 0x19, 3, H2C_Parameter);
}

u8 BTDM_IsWifiConnectionExist(struct rtw_adapter *padapter)
{
	u8 bRet = false;

	if (BTHCI_HsConnectionEstablished(padapter))
		bRet = true;

	if (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE) == true)
		bRet = true;

	return bRet;
}

void BTDM_SetFw3a(
	struct rtw_adapter *padapter,
	u8 byte1,
	u8 byte2,
	u8 byte3,
	u8 byte4,
	u8 byte5
	)
{
	u8 H2C_Parameter[5] = {0};

	if (rtl8723a_BT_using_antenna_1(padapter)) {
		if ((!check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE)) &&
		    (get_fwstate(&padapter->mlmepriv) != WIFI_NULL_STATE)) {
			/*  for softap mode */
			struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
			struct bt_coexist_8723a *pBtCoex = &pHalData->bt_coexist.halCoex8723;
			u8 BtState = pBtCoex->c2hBtInfo;

			if ((BtState != BT_INFO_STATE_NO_CONNECTION) &&
			    (BtState != BT_INFO_STATE_CONNECT_IDLE)) {
				if (byte1 & BIT(4)) {
					byte1 &= ~BIT(4);
					byte1 |= BIT(5);
				}

				byte5 |= BIT(5);
				if (byte5 & BIT(6))
					byte5 &= ~BIT(6);
			}
		}
	}

	H2C_Parameter[0] = byte1;
	H2C_Parameter[1] = byte2;
	H2C_Parameter[2] = byte3;
	H2C_Parameter[3] = byte4;
	H2C_Parameter[4] = byte5;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], FW write 0x3a(5bytes) = 0x%02x%08x\n",
		H2C_Parameter[0],
		H2C_Parameter[1]<<24|H2C_Parameter[2]<<16|H2C_Parameter[3]<<8|H2C_Parameter[4]));

	FillH2CCmd(padapter, 0x3a, 5, H2C_Parameter);
}

void BTDM_QueryBtInformation(struct rtw_adapter *padapter)
{
	u8 H2C_Parameter[1] = {0};
	struct hal_data_8723a *pHalData;
	struct bt_coexist_8723a *pBtCoex;

	pHalData = GET_HAL_DATA(padapter);
	pBtCoex = &pHalData->bt_coexist.halCoex8723;

	if (!rtl8723a_BT_enabled(padapter)) {
		pBtCoex->c2hBtInfo = BT_INFO_STATE_DISABLED;
		pBtCoex->bC2hBtInfoReqSent = false;
		return;
	}

	if (pBtCoex->c2hBtInfo == BT_INFO_STATE_DISABLED)
		pBtCoex->c2hBtInfo = BT_INFO_STATE_NO_CONNECTION;

	if (pBtCoex->bC2hBtInfoReqSent == true)
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], didn't recv previous BtInfo report!\n"));
	else
		pBtCoex->bC2hBtInfoReqSent = true;

	H2C_Parameter[0] |= BIT(0);	/*  trigger */

/*RTPRINT(FBT, BT_TRACE, ("[BTCoex], Query Bt information, write 0x38 = 0x%x\n", */
/*H2C_Parameter[0])); */

	FillH2CCmd(padapter, 0x38, 1, H2C_Parameter);
}

void BTDM_SetSwRfRxLpfCorner(struct rtw_adapter *padapter, u8 type)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	if (BT_RF_RX_LPF_CORNER_SHRINK == type) {
		/* Shrink RF Rx LPF corner */
		RTPRINT(FBT, BT_TRACE, ("Shrink RF Rx LPF corner!!\n"));
		PHY_SetRFReg(padapter, PathA, 0x1e, bRFRegOffsetMask, 0xf0ff7);
		pHalData->bt_coexist.bSWCoexistAllOff = false;
	} else if (BT_RF_RX_LPF_CORNER_RESUME == type) {
		/* Resume RF Rx LPF corner */
		RTPRINT(FBT, BT_TRACE, ("Resume RF Rx LPF corner!!\n"));
		PHY_SetRFReg(padapter, PathA, 0x1e, bRFRegOffsetMask, pHalData->bt_coexist.BtRfRegOrigin1E);
	}
}

void
BTDM_SetSwPenaltyTxRateAdaptive(
	struct rtw_adapter *padapter,
	u8 raType
	)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	u8 tmpU1;

	tmpU1 = rtl8723au_read8(padapter, 0x4fd);
	tmpU1 |= BIT(0);
	if (BT_TX_RATE_ADAPTIVE_LOW_PENALTY == raType) {
		tmpU1 &= ~BIT(2);
		pHalData->bt_coexist.bSWCoexistAllOff = false;
	} else if (BT_TX_RATE_ADAPTIVE_NORMAL == raType) {
		tmpU1 |= BIT(2);
	}

	rtl8723au_write8(padapter, 0x4fd, tmpU1);
}

void BTDM_SetFwDecBtPwr(struct rtw_adapter *padapter, u8 bDecBtPwr)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	u8 H2C_Parameter[1] = {0};

	H2C_Parameter[0] = 0;

	if (bDecBtPwr) {
		H2C_Parameter[0] |= BIT(1);
		pHalData->bt_coexist.bFWCoexistAllOff = false;
	}

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], decrease Bt Power : %s, write 0x21 = 0x%x\n",
		(bDecBtPwr ? "Yes!!" : "No!!"), H2C_Parameter[0]));

	FillH2CCmd(padapter, 0x21, 1, H2C_Parameter);
}

u8 BTDM_BtProfileSupport(struct rtw_adapter *padapter)
{
	u8 bRet = false;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	if (pBtMgnt->bSupportProfile &&
	    !pHalData->bt_coexist.halCoex8723.bForceFwBtInfo)
		bRet = true;

	return bRet;
}

static void BTDM_AdjustForBtOperation8723A(struct rtw_adapter *padapter)
{
	/* BTDM_2AntAdjustForBtOperation8723(padapter); */
}

static void BTDM_FwC2hBtRssi8723A(struct rtw_adapter *padapter, u8 *tmpBuf)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	u8 percent = 0, u1tmp = 0;

	u1tmp = tmpBuf[0];
	percent = u1tmp*2+10;

	pHalData->bt_coexist.halCoex8723.btRssi = percent;
/*RTPRINT(FBT, BT_TRACE, ("[BTC2H], BT RSSI =%d\n", percent)); */
}

void
rtl8723a_fw_c2h_BT_info(struct rtw_adapter *padapter, u8 *tmpBuf, u8 length)
{
	struct hal_data_8723a *pHalData;
	struct bt_30info *pBTInfo;
	struct bt_mgnt *pBtMgnt;
	struct bt_coexist_8723a *pBtCoex;
	u8 i;

	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;
	pBtCoex = &pHalData->bt_coexist.halCoex8723;

	pBtCoex->bC2hBtInfoReqSent = false;

	RTPRINT(FBT, BT_TRACE, ("[BTC2H], BT info[%d]=[", length));

	pBtCoex->btRetryCnt = 0;
	for (i = 0; i < length; i++) {
		switch (i) {
		case 0:
			pBtCoex->c2hBtInfoOriginal = tmpBuf[i];
			break;
		case 1:
			pBtCoex->btRetryCnt = tmpBuf[i];
			break;
		case 2:
			BTDM_FwC2hBtRssi8723A(padapter, &tmpBuf[i]);
			break;
		case 3:
			pBtCoex->btInfoExt = tmpBuf[i]&BIT(0);
			break;
		}

		if (i == length-1)
			RTPRINT(FBT, BT_TRACE, ("0x%02x]\n", tmpBuf[i]));
		else
			RTPRINT(FBT, BT_TRACE, ("0x%02x, ", tmpBuf[i]));
	}
	RTPRINT(FBT, BT_TRACE, ("[BTC2H], BT RSSI =%d\n", pBtCoex->btRssi));
	if (pBtCoex->btInfoExt)
		RTPRINT(FBT, BT_TRACE, ("[BTC2H], pBtCoex->btInfoExt =%x\n", pBtCoex->btInfoExt));

	if (btdm_BtWifiAntNum(padapter) == Ant_x1)
		BTDM_1AntFwC2hBtInfo8723A(padapter);
	else
		BTDM_2AntFwC2hBtInfo8723A(padapter);

	if (pBtMgnt->ExtConfig.bManualControl) {
		RTPRINT(FBT, BT_TRACE, ("%s: Action Manual control!!\n", __func__));
		return;
	}

	btdm_BTCoexist8723AHandler(padapter);
}

static void BTDM_Display8723ABtCoexInfo(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct bt_coexist_8723a *pBtCoex = &pHalData->bt_coexist.halCoex8723;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	u8 u1Tmp, u1Tmp1, u1Tmp2, i, btInfoExt, psTdmaCase = 0;
	u32 u4Tmp[4];
	u8 antNum = Ant_x2;

	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n ============[BT Coexist info]============");
	DCMD_Printf(btCoexDbgBuf);

	if (!rtl8723a_BT_coexist(padapter)) {
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n BT not exists !!!");
		DCMD_Printf(btCoexDbgBuf);
		return;
	}

	antNum = btdm_BtWifiAntNum(padapter);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/%d ", "Ant mechanism PG/Now run :", \
		((pHalData->bt_coexist.BT_Ant_Num == Ant_x2) ? 2 : 1), ((antNum == Ant_x2) ? 2 : 1));
	DCMD_Printf(btCoexDbgBuf);

	if (pBtMgnt->ExtConfig.bManualControl) {
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "[Action Manual control]!!");
		DCMD_Printf(btCoexDbgBuf);
	} else {
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s / %d", "BT stack/ hci ext ver", \
			((pBtMgnt->bSupportProfile) ? "Yes" : "No"), pBtMgnt->ExtConfig.HCIExtensionVer);
		DCMD_Printf(btCoexDbgBuf);
	}

	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\n %-35s = / %d", "Dot11 channel / BT channel", \
		pBtMgnt->BTChannel);
		DCMD_Printf(btCoexDbgBuf);

	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\n %-35s = %d / %d / %d", "Wifi/BT/HS rssi", \
		BTDM_GetRxSS(padapter),
		pHalData->bt_coexist.halCoex8723.btRssi,
		pHalData->dmpriv.EntryMinUndecoratedSmoothedPWDB);
			DCMD_Printf(btCoexDbgBuf);

	if (!pBtMgnt->ExtConfig.bManualControl) {
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\n %-35s = %s / %s ", "WIfi status",
			((BTDM_Legacy(padapter)) ? "Legacy" : (((BTDM_IsHT40(padapter)) ? "HT40" : "HT20"))),
			((!BTDM_IsWifiBusy(padapter)) ? "idle" : ((BTDM_IsWifiUplink(padapter)) ? "uplink" : "downlink")));
		DCMD_Printf(btCoexDbgBuf);

		if (pBtMgnt->bSupportProfile) {
			rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d / %d", "SCO/HID/PAN/A2DP",
				((BTHCI_CheckProfileExist(padapter, BT_PROFILE_SCO)) ? 1 : 0),
				((BTHCI_CheckProfileExist(padapter, BT_PROFILE_HID)) ? 1 : 0),
				((BTHCI_CheckProfileExist(padapter, BT_PROFILE_PAN)) ? 1 : 0),
				((BTHCI_CheckProfileExist(padapter, BT_PROFILE_A2DP)) ? 1 : 0));
		DCMD_Printf(btCoexDbgBuf);

			for (i = 0; i < pBtMgnt->ExtConfig.NumberOfHandle; i++) {
				if (pBtMgnt->ExtConfig.HCIExtensionVer >= 1) {
					rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s/ %s", "Bt link type/spec/role",
						BtProfileString[pBtMgnt->ExtConfig.linkInfo[i].BTProfile],
						BtSpecString[pBtMgnt->ExtConfig.linkInfo[i].BTCoreSpec],
						BtLinkRoleString[pBtMgnt->ExtConfig.linkInfo[i].linkRole]);
					DCMD_Printf(btCoexDbgBuf);

					btInfoExt = pHalData->bt_coexist.halCoex8723.btInfoExt;
					rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s", "A2DP rate", \
						 (btInfoExt & BIT(0)) ?
						 "Basic rate" : "EDR rate");
					DCMD_Printf(btCoexDbgBuf);
				} else {
					rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s", "Bt link type/spec", \
						BtProfileString[pBtMgnt->ExtConfig.linkInfo[i].BTProfile],
						BtSpecString[pBtMgnt->ExtConfig.linkInfo[i].BTCoreSpec]);
					DCMD_Printf(btCoexDbgBuf);
				}
			}
		}
	}

	/*  Sw mechanism */
	if (!pBtMgnt->ExtConfig.bManualControl) {
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Sw BT Coex mechanism]============");
		DCMD_Printf(btCoexDbgBuf);
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "AGC Table", \
			pBtCoex->btdm2Ant.bCurAgcTableEn);
		DCMD_Printf(btCoexDbgBuf);
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "ADC Backoff", \
			pBtCoex->btdm2Ant.bCurAdcBackOff);
		DCMD_Printf(btCoexDbgBuf);
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "Low penalty RA", \
			pBtCoex->btdm2Ant.bCurLowPenaltyRa);
		DCMD_Printf(btCoexDbgBuf);
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "RF Rx LPF Shrink", \
			pBtCoex->btdm2Ant.bCurRfRxLpfShrink);
		DCMD_Printf(btCoexDbgBuf);
	}
	u4Tmp[0] = PHY_QueryRFReg(padapter, PathA, 0x1e, 0xff0);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x", "RF-A, 0x1e[11:4]/original val", \
		u4Tmp[0], pHalData->bt_coexist.BtRfRegOrigin1E);
	DCMD_Printf(btCoexDbgBuf);

	/*  Fw mechanism */
	if (!pBtMgnt->ExtConfig.bManualControl) {
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Fw BT Coex mechanism]============");
		DCMD_Printf(btCoexDbgBuf);
	}
	if (!pBtMgnt->ExtConfig.bManualControl) {
		if (btdm_BtWifiAntNum(padapter) == Ant_x1)
			psTdmaCase = pHalData->bt_coexist.halCoex8723.btdm1Ant.curPsTdma;
		else
			psTdmaCase = pHalData->bt_coexist.halCoex8723.btdm2Ant.curPsTdma;
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x %02x %02x case-%d", "PS TDMA(0x3a)", \
			pHalData->bt_coexist.fw3aVal[0], pHalData->bt_coexist.fw3aVal[1],
			pHalData->bt_coexist.fw3aVal[2], pHalData->bt_coexist.fw3aVal[3],
			pHalData->bt_coexist.fw3aVal[4], psTdmaCase);
		DCMD_Printf(btCoexDbgBuf);

		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "Decrease Bt Power", \
			pBtCoex->btdm2Ant.bCurDecBtPwr);
		DCMD_Printf(btCoexDbgBuf);
	}
	u1Tmp = rtl8723au_read8(padapter, 0x778);
	u1Tmp1 = rtl8723au_read8(padapter, 0x783);
	u1Tmp2 = rtl8723au_read8(padapter, 0x796);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "0x778/ 0x783/ 0x796", \
		u1Tmp, u1Tmp1, u1Tmp2);
	DCMD_Printf(btCoexDbgBuf);

	if (!pBtMgnt->ExtConfig.bManualControl) {
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x / 0x%x", "Sw DacSwing Ctrl/Val", \
			pBtCoex->btdm2Ant.bCurDacSwingOn, pBtCoex->btdm2Ant.curDacSwingLvl);
		DCMD_Printf(btCoexDbgBuf);
	}
	u4Tmp[0] =  rtl8723au_read32(padapter, 0x880);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x880", \
		u4Tmp[0]);
	DCMD_Printf(btCoexDbgBuf);

	/*  Hw mechanism */
	if (!pBtMgnt->ExtConfig.bManualControl) {
		rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Hw BT Coex mechanism]============");
		DCMD_Printf(btCoexDbgBuf);
	}

	u1Tmp = rtl8723au_read8(padapter, 0x40);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x40", \
		u1Tmp);
	DCMD_Printf(btCoexDbgBuf);

	u4Tmp[0] = rtl8723au_read32(padapter, 0x550);
	u1Tmp = rtl8723au_read8(padapter, 0x522);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/0x%x", "0x550(bcn contrl)/0x522", \
		u4Tmp[0], u1Tmp);
	DCMD_Printf(btCoexDbgBuf);

	u4Tmp[0] = rtl8723au_read32(padapter, 0x484);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x484(rate adaptive)", \
		u4Tmp[0]);
	DCMD_Printf(btCoexDbgBuf);

	u4Tmp[0] = rtl8723au_read32(padapter, 0x50);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0xc50(dig)", \
		u4Tmp[0]);
	DCMD_Printf(btCoexDbgBuf);

	u4Tmp[0] = rtl8723au_read32(padapter, 0xda0);
	u4Tmp[1] = rtl8723au_read32(padapter, 0xda4);
	u4Tmp[2] = rtl8723au_read32(padapter, 0xda8);
	u4Tmp[3] = rtl8723au_read32(padapter, 0xdac);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x", "0xda0/0xda4/0xda8/0xdac(FA cnt)", \
		u4Tmp[0], u4Tmp[1], u4Tmp[2], u4Tmp[3]);
	DCMD_Printf(btCoexDbgBuf);

	u4Tmp[0] = rtl8723au_read32(padapter, 0x6c0);
	u4Tmp[1] = rtl8723au_read32(padapter, 0x6c4);
	u4Tmp[2] = rtl8723au_read32(padapter, 0x6c8);
	u1Tmp = rtl8723au_read8(padapter, 0x6cc);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x", "0x6c0/0x6c4/0x6c8/0x6cc(coexTable)", \
		u4Tmp[0], u4Tmp[1], u4Tmp[2], u1Tmp);
	DCMD_Printf(btCoexDbgBuf);

	/* u4Tmp = rtl8723au_read32(padapter, 0x770); */
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d", "0x770(Hi pri Rx[31:16]/Tx[15:0])", \
		pHalData->bt_coexist.halCoex8723.highPriorityRx,
		pHalData->bt_coexist.halCoex8723.highPriorityTx);
	DCMD_Printf(btCoexDbgBuf);
	/* u4Tmp = rtl8723au_read32(padapter, 0x774); */
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d", "0x774(Lo pri Rx[31:16]/Tx[15:0])", \
		pHalData->bt_coexist.halCoex8723.lowPriorityRx,
		pHalData->bt_coexist.halCoex8723.lowPriorityTx);
	DCMD_Printf(btCoexDbgBuf);

	/*  Tx mgnt queue hang or not, 0x41b should = 0xf, ex: 0xd ==>hang */
	u1Tmp = rtl8723au_read8(padapter, 0x41b);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x41b (hang chk == 0xf)", \
		u1Tmp);
	DCMD_Printf(btCoexDbgBuf);
	rsprintf(btCoexDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "lastHMEBoxNum", \
		pHalData->LastHMEBoxNum);
	DCMD_Printf(btCoexDbgBuf);
}

static void
BTDM_8723ASignalCompensation(struct rtw_adapter *padapter,
			     u8 *rssi_wifi, u8 *rssi_bt)
{
	if (btdm_BtWifiAntNum(padapter) == Ant_x1)
		BTDM_1AntSignalCompensation(padapter, rssi_wifi, rssi_bt);
}

static void BTDM_8723AInit(struct rtw_adapter *padapter)
{
	if (btdm_BtWifiAntNum(padapter) == Ant_x2)
		BTDM_2AntParaInit(padapter);
	else
		BTDM_1AntParaInit(padapter);
}

static void BTDM_HWCoexAllOff8723A(struct rtw_adapter *padapter)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bManualControl)
		return;

	if (btdm_BtWifiAntNum(padapter) == Ant_x2)
		BTDM_2AntHwCoexAllOff8723A(padapter);
}

static void BTDM_FWCoexAllOff8723A(struct rtw_adapter *padapter)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bManualControl)
		return;

	if (btdm_BtWifiAntNum(padapter) == Ant_x2)
		BTDM_2AntFwCoexAllOff8723A(padapter);
}

static void BTDM_SWCoexAllOff8723A(struct rtw_adapter *padapter)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bManualControl)
		return;

	if (btdm_BtWifiAntNum(padapter) == Ant_x2)
		BTDM_2AntSwCoexAllOff8723A(padapter);
}

static void
BTDM_Set8723ABtCoexCurrAntNum(struct rtw_adapter *padapter, u8 antNum)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct bt_coexist_8723a *pBtCoex = &pHalData->bt_coexist.halCoex8723;

	if (antNum == 1)
		pBtCoex->TotalAntNum = Ant_x1;
	else if (antNum == 2)
		pBtCoex->TotalAntNum = Ant_x2;
}

void rtl8723a_BT_lps_leave(struct rtw_adapter *padapter)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bManualControl)
		return;

	if (btdm_BtWifiAntNum(padapter) == Ant_x1)
		BTDM_1AntLpsLeave(padapter);
}

static void BTDM_ForHalt8723A(struct rtw_adapter *padapter)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bManualControl)
		return;

	if (btdm_BtWifiAntNum(padapter) == Ant_x1)
		BTDM_1AntForHalt(padapter);
}

static void BTDM_WifiScanNotify8723A(struct rtw_adapter *padapter, u8 scanType)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bManualControl)
		return;

	if (btdm_BtWifiAntNum(padapter) == Ant_x1)
		BTDM_1AntWifiScanNotify(padapter, scanType);
}

static void
BTDM_WifiAssociateNotify8723A(struct rtw_adapter *padapter, u8 action)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bManualControl)
		return;

	if (btdm_BtWifiAntNum(padapter) == Ant_x1)
		BTDM_1AntWifiAssociateNotify(padapter, action);
}

static void
BTDM_MediaStatusNotify8723A(struct rtw_adapter *padapter,
			    enum rt_media_status mstatus)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], MediaStatusNotify, %s\n",
		mstatus?"connect":"disconnect"));

	BTDM_SetFwChnlInfo(padapter, mstatus);

	if (pBtMgnt->ExtConfig.bManualControl)
		return;

	if (btdm_BtWifiAntNum(padapter) == Ant_x1)
		BTDM_1AntMediaStatusNotify(padapter, mstatus);
}

static void BTDM_ForDhcp8723A(struct rtw_adapter *padapter)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bManualControl)
		return;

	if (btdm_BtWifiAntNum(padapter) == Ant_x1)
		BTDM_1AntForDhcp(padapter);
}

bool rtl8723a_BT_using_antenna_1(struct rtw_adapter *padapter)
{
	if (btdm_BtWifiAntNum(padapter) == Ant_x1)
		return true;
	else
		return false;
}

static void BTDM_BTCoexist8723A(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData;
	struct bt_30info *pBTInfo;
	struct bt_mgnt *pBtMgnt;
	struct bt_coexist_8723a *pBtCoex;

	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;
	pBtCoex = &pHalData->bt_coexist.halCoex8723;

	RTPRINT(FBT, BT_TRACE, ("[BTCoex], beacon RSSI = 0x%x(%d)\n",
		pHalData->dmpriv.EntryMinUndecoratedSmoothedPWDB,
		pHalData->dmpriv.EntryMinUndecoratedSmoothedPWDB));

	btdm_BtHwCountersMonitor(padapter);
	btdm_BtEnableDisableCheck8723A(padapter);

	if (pBtMgnt->ExtConfig.bManualControl) {
		RTPRINT(FBT, BT_TRACE, ("%s: Action Manual control!!\n", __func__));
		return;
	}

	if (pBtCoex->bC2hBtInfoReqSent) {
		if (!rtl8723a_BT_enabled(padapter)) {
			pBtCoex->c2hBtInfo = BT_INFO_STATE_DISABLED;
		} else {
			if (pBtCoex->c2hBtInfo == BT_INFO_STATE_DISABLED)
				pBtCoex->c2hBtInfo = BT_INFO_STATE_NO_CONNECTION;
		}

		btdm_BTCoexist8723AHandler(padapter);
	} else if (!rtl8723a_BT_enabled(padapter)) {
		pBtCoex->c2hBtInfo = BT_INFO_STATE_DISABLED;
		btdm_BTCoexist8723AHandler(padapter);
	}

	BTDM_QueryBtInformation(padapter);
}

/*  ===== End of sync from SD7 driver HAL/BTCoexist/HalBtc8723.c ===== */

/*  ===== Below this line is sync from SD7 driver HAL/BTCoexist/HalBtcCsr1Ant.c ===== */

/*  local function start with btdm_ */
/*  extern function start with BTDM_ */

static void BTDM_SetAntenna(struct rtw_adapter *padapter, u8 who)
{
}

void
BTDM_SingleAnt(
	struct rtw_adapter *padapter,
	u8 bSingleAntOn,
	u8 bInterruptOn,
	u8 bMultiNAVOn
	)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	u8 H2C_Parameter[3] = {0};

	if (pHalData->bt_coexist.BT_Ant_Num != Ant_x1)
		return;

	H2C_Parameter[2] = 0;
	H2C_Parameter[1] = 0;
	H2C_Parameter[0] = 0;

	if (bInterruptOn) {
		H2C_Parameter[2] |= 0x02;	/* BIT1 */
		pHalData->bt_coexist.bFWCoexistAllOff = false;
	}
	pHalData->bt_coexist.bInterruptOn = bInterruptOn;

	if (bSingleAntOn) {
		H2C_Parameter[2] |= 0x10;	/* BIT4 */
		pHalData->bt_coexist.bFWCoexistAllOff = false;
	}
	pHalData->bt_coexist.bSingleAntOn = bSingleAntOn;

	if (bMultiNAVOn) {
		H2C_Parameter[2] |= 0x20;	/* BIT5 */
		pHalData->bt_coexist.bFWCoexistAllOff = false;
	}
	pHalData->bt_coexist.bMultiNAVOn = bMultiNAVOn;

	RTPRINT(FBT, BT_TRACE, ("[DM][BT], SingleAntenna =[%s:%s:%s], write 0xe = 0x%x\n",
		bSingleAntOn?"ON":"OFF", bInterruptOn?"ON":"OFF", bMultiNAVOn?"ON":"OFF",
		H2C_Parameter[0]<<16|H2C_Parameter[1]<<8|H2C_Parameter[2]));
}

void BTDM_CheckBTIdleChange1Ant(struct rtw_adapter *padapter)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
/*PMGNT_INFO		pMgntInfo = &padapter->MgntInfo; */
	u8 	stateChange = false;
	u32			BT_Polling, Ratio_Act, Ratio_STA;
	u32				BT_Active, BT_State;
	u32				regBTActive = 0, regBTState = 0, regBTPolling = 0;

	if (!rtl8723a_BT_coexist(padapter))
		return;
	if (pBtMgnt->ExtConfig.bManualControl)
		return;
	if (pHalData->bt_coexist.BT_CoexistType != BT_CSR_BC8)
		return;
	if (pHalData->bt_coexist.BT_Ant_Num != Ant_x1)
		return;

	/*  The following we only consider CSR BC8 and fw version should be >= 62 */
	RTPRINT(FBT, BT_TRACE, ("[DM][BT], FirmwareVersion = 0x%x(%d)\n",
	pHalData->FirmwareVersion, pHalData->FirmwareVersion));
	regBTActive = REG_BT_ACTIVE;
	regBTState = REG_BT_STATE;
	if (pHalData->FirmwareVersion >= FW_VER_BT_REG1)
		regBTPolling = REG_BT_POLLING1;
	else
		regBTPolling = REG_BT_POLLING;

	BT_Active = rtl8723au_read32(padapter, regBTActive);
	RTPRINT(FBT, BT_TRACE, ("[DM][BT], BT_Active(0x%x) =%x\n", regBTActive, BT_Active));
	BT_Active = BT_Active & 0x00ffffff;

	BT_State = rtl8723au_read32(padapter, regBTState);
	RTPRINT(FBT, BT_TRACE, ("[DM][BT], BT_State(0x%x) =%x\n", regBTState, BT_State));
	BT_State = BT_State & 0x00ffffff;

	BT_Polling = rtl8723au_read32(padapter, regBTPolling);
	RTPRINT(FBT, BT_TRACE, ("[DM][BT], BT_Polling(0x%x) =%x\n", regBTPolling, BT_Polling));

	if (BT_Active == 0xffffffff && BT_State == 0xffffffff && BT_Polling == 0xffffffff)
		return;
	if (BT_Polling == 0)
		return;

	Ratio_Act = BT_Active*1000/BT_Polling;
	Ratio_STA = BT_State*1000/BT_Polling;

	pHalData->bt_coexist.Ratio_Tx = Ratio_Act;
	pHalData->bt_coexist.Ratio_PRI = Ratio_STA;

	RTPRINT(FBT, BT_TRACE, ("[DM][BT], Ratio_Act =%d\n", Ratio_Act));
	RTPRINT(FBT, BT_TRACE, ("[DM][BT], Ratio_STA =%d\n", Ratio_STA));

	if (Ratio_STA < 60 && Ratio_Act < 500) {	/*  BT PAN idle */
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_PAN_IDLE;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_PAN_DOWNLINK;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_PAN_UPLINK;
	} else {
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_PAN_IDLE;

		if (Ratio_STA) {
			/*  Check if BT PAN (under BT 2.1) is uplink or downlink */
			if ((Ratio_Act/Ratio_STA) < 2) {
				/*  BT PAN Uplink */
				pHalData->bt_coexist.BT21TrafficStatistics.bTxBusyTraffic = true;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_PAN_UPLINK;
				pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic = false;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_PAN_DOWNLINK;
			} else {
				/*  BT PAN downlink */
				pHalData->bt_coexist.BT21TrafficStatistics.bTxBusyTraffic = false;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_PAN_UPLINK;
				pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic = true;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_PAN_DOWNLINK;
			}
		} else {
			/*  BT PAN downlink */
			pHalData->bt_coexist.BT21TrafficStatistics.bTxBusyTraffic = false;
			pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_PAN_UPLINK;
			pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic = true;
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_PAN_DOWNLINK;
		}
	}

	/*  Check BT is idle or not */
	if (pBtMgnt->ExtConfig.NumberOfHandle == 0 &&
	    pBtMgnt->ExtConfig.NumberOfSCO == 0) {
		pBtMgnt->ExtConfig.bBTBusy = false;
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_IDLE;
	} else {
		if (Ratio_STA < 60) {
			pBtMgnt->ExtConfig.bBTBusy = false;
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_IDLE;
		} else {
			pBtMgnt->ExtConfig.bBTBusy = true;
			pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_IDLE;
		}
	}

	if (pBtMgnt->ExtConfig.NumberOfHandle == 0 &&
	    pBtMgnt->ExtConfig.NumberOfSCO == 0) {
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_RSSI_LOW;
		pBtMgnt->ExtConfig.MIN_BT_RSSI = 0;
		BTDM_SetAntenna(padapter, BTDM_ANT_BT_IDLE);
	} else {
		if (pBtMgnt->ExtConfig.MIN_BT_RSSI <= -5) {
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT_RSSI_LOW;
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], core stack notify bt rssi Low\n"));
		} else {
			pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT_RSSI_LOW;
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], core stack notify bt rssi Normal\n"));
		}
	}

	if (pHalData->bt_coexist.bBTBusyTraffic != pBtMgnt->ExtConfig.bBTBusy) {
		/*  BT idle or BT non-idle */
		pHalData->bt_coexist.bBTBusyTraffic = pBtMgnt->ExtConfig.bBTBusy;
		stateChange = true;
	}

	if (stateChange) {
		if (!pBtMgnt->ExtConfig.bBTBusy)
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], BT is idle or disable\n"));
		else
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], BT is non-idle\n"));
	}
	if (!pBtMgnt->ExtConfig.bBTBusy) {
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], BT is idle or disable\n"));
		if (check_fwstate(&padapter->mlmepriv, WIFI_UNDER_LINKING|WIFI_SITE_MONITOR) == true)
			BTDM_SetAntenna(padapter, BTDM_ANT_WIFI);
	}
}

/*  ===== End of sync from SD7 driver HAL/BTCoexist/HalBtcCsr1Ant.c ===== */

/*  ===== Below this line is sync from SD7 driver HAL/BTCoexist/HalBtcCsr2Ant.c ===== */

/*  local function start with btdm_ */

/*  Note: */
/*  In the following, FW should be done before SW mechanism. */
/*  BTDM_Balance(), BTDM_DiminishWiFi(), BT_NAV() should be done */
/*  before BTDM_AGCTable(), BTDM_BBBackOffLevel(), btdm_DacSwing(). */

/*  extern function start with BTDM_ */

void
BTDM_DiminishWiFi(
	struct rtw_adapter *padapter,
	u8 bDACOn,
	u8 bInterruptOn,
	u8 DACSwingLevel,
	u8 bNAVOn
	)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	u8 H2C_Parameter[3] = {0};

	if (pHalData->bt_coexist.BT_Ant_Num != Ant_x2)
		return;

	if ((pHalData->bt_coexist.CurrentState & BT_COEX_STATE_BT_RSSI_LOW) &&
	    (DACSwingLevel == 0x20)) {
		RTPRINT(FBT, BT_TRACE, ("[BT]DiminishWiFi 0x20 original, but set 0x18 for Low RSSI!\n"));
		DACSwingLevel = 0x18;
	}

	H2C_Parameter[2] = 0;
	H2C_Parameter[1] = DACSwingLevel;
	H2C_Parameter[0] = 0;
	if (bDACOn) {
		H2C_Parameter[2] |= 0x01;	/* BIT0 */
		if (bInterruptOn)
			H2C_Parameter[2] |= 0x02;	/* BIT1 */
		pHalData->bt_coexist.bFWCoexistAllOff = false;
	}
	if (bNAVOn) {
		H2C_Parameter[2] |= 0x08;	/* BIT3 */
		pHalData->bt_coexist.bFWCoexistAllOff = false;
	}

	RTPRINT(FBT, BT_TRACE, ("[DM][BT], bDACOn = %s, bInterruptOn = %s, write 0xe = 0x%x\n",
		bDACOn?"ON":"OFF", bInterruptOn?"ON":"OFF",
		H2C_Parameter[0]<<16|H2C_Parameter[1]<<8|H2C_Parameter[2]));
	RTPRINT(FBT, BT_TRACE, ("[DM][BT], bNAVOn = %s\n",
		bNAVOn?"ON":"OFF"));
}

/*  ===== End of sync from SD7 driver HAL/BTCoexist/HalBtcCsr2Ant.c ===== */

/*  ===== Below this line is sync from SD7 driver HAL/BTCoexist/HalBtCoexist.c ===== */

/*  local function */
static void btdm_ResetFWCoexState(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	pHalData->bt_coexist.CurrentState = 0;
	pHalData->bt_coexist.PreviousState = 0;
}

static void btdm_InitBtCoexistDM(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	/*  20100415 Joseph: Restore RF register 0x1E and 0x1F value for further usage. */
	pHalData->bt_coexist.BtRfRegOrigin1E = PHY_QueryRFReg(padapter, PathA, RF_RCK1, bRFRegOffsetMask);
	pHalData->bt_coexist.BtRfRegOrigin1F = PHY_QueryRFReg(padapter, PathA, RF_RCK2, 0xf0);

	pHalData->bt_coexist.CurrentState = 0;
	pHalData->bt_coexist.PreviousState = 0;

	BTDM_8723AInit(padapter);
	pHalData->bt_coexist.bInitlized = true;
}

/*  */
/*  extern function */
/*  */
void BTDM_CheckAntSelMode(struct rtw_adapter *padapter)
{
}

void BTDM_FwC2hBtRssi(struct rtw_adapter *padapter, u8 *tmpBuf)
{
	BTDM_FwC2hBtRssi8723A(padapter, tmpBuf);
}

void BTDM_DisplayBtCoexInfo(struct rtw_adapter *padapter)
{
	BTDM_Display8723ABtCoexInfo(padapter);
}

void BTDM_RejectAPAggregatedPacket(struct rtw_adapter *padapter, u8 bReject)
{
}

u8 BTDM_IsHT40(struct rtw_adapter *padapter)
{
	u8 isht40 = true;
	enum ht_channel_width bw;

	bw = padapter->mlmeextpriv.cur_bwmode;

	if (bw == HT_CHANNEL_WIDTH_20)
		isht40 = false;
	else if (bw == HT_CHANNEL_WIDTH_40)
		isht40 = true;

	return isht40;
}

u8 BTDM_Legacy(struct rtw_adapter *padapter)
{
	struct mlme_ext_priv *pmlmeext;
	u8 isLegacy = false;

	pmlmeext = &padapter->mlmeextpriv;
	if ((pmlmeext->cur_wireless_mode == WIRELESS_11B) ||
		(pmlmeext->cur_wireless_mode == WIRELESS_11G) ||
		(pmlmeext->cur_wireless_mode == WIRELESS_11BG))
		isLegacy = true;

	return isLegacy;
}

void BTDM_CheckWiFiState(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData;
	struct mlme_priv *pmlmepriv;
	struct bt_30info *pBTInfo;
	struct bt_mgnt *pBtMgnt;

	pHalData = GET_HAL_DATA(padapter);
	pmlmepriv = &padapter->mlmepriv;
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;

	if (pmlmepriv->LinkDetectInfo.bBusyTraffic) {
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_IDLE;

		if (pmlmepriv->LinkDetectInfo.bTxBusyTraffic)
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_UPLINK;
		else
			pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_UPLINK;

		if (pmlmepriv->LinkDetectInfo.bRxBusyTraffic)
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_DOWNLINK;
		else
			pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_DOWNLINK;
	} else {
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_IDLE;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_UPLINK;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_DOWNLINK;
	}

	if (BTDM_Legacy(padapter)) {
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_LEGACY;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_HT20;
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_HT40;
	} else {
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_LEGACY;
		if (BTDM_IsHT40(padapter)) {
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_HT40;
			pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_HT20;
		} else {
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_HT20;
			pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_HT40;
		}
	}

	if (pBtMgnt->BtOperationOn)
		pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_BT30;
	else
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_BT30;
}

s32 BTDM_GetRxSS(struct rtw_adapter *padapter)
{
/*PMGNT_INFO		pMgntInfo = &padapter->MgntInfo; */
	struct mlme_priv *pmlmepriv;
	struct hal_data_8723a *pHalData;
	s32			UndecoratedSmoothedPWDB = 0;

	pmlmepriv = &padapter->mlmepriv;
	pHalData = GET_HAL_DATA(padapter);

	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		UndecoratedSmoothedPWDB = GET_UNDECORATED_AVERAGE_RSSI(padapter);
	} else { /*  associated entry pwdb */
		UndecoratedSmoothedPWDB = pHalData->dmpriv.EntryMinUndecoratedSmoothedPWDB;
		/* pHalData->BT_EntryMinUndecoratedSmoothedPWDB */
	}
	RTPRINT(FBT, BT_TRACE, ("BTDM_GetRxSS() = %d\n", UndecoratedSmoothedPWDB));
	return UndecoratedSmoothedPWDB;
}

static s32 BTDM_GetRxBeaconSS(struct rtw_adapter *padapter)
{
/*PMGNT_INFO		pMgntInfo = &padapter->MgntInfo; */
	struct mlme_priv *pmlmepriv;
	struct hal_data_8723a *pHalData;
	s32			pwdbBeacon = 0;

	pmlmepriv = &padapter->mlmepriv;
	pHalData = GET_HAL_DATA(padapter);

	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		/* pwdbBeacon = pHalData->dmpriv.UndecoratedSmoothedBeacon; */
		pwdbBeacon = pHalData->dmpriv.EntryMinUndecoratedSmoothedPWDB;
	}
	RTPRINT(FBT, BT_TRACE, ("BTDM_GetRxBeaconSS() = %d\n", pwdbBeacon));
	return pwdbBeacon;
}

/*  Get beacon rssi state */
u8 BTDM_CheckCoexBcnRssiState(struct rtw_adapter *padapter, u8 levelNum,
			      u8 RssiThresh, u8 RssiThresh1)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	s32 pwdbBeacon = 0;
	u8 bcnRssiState = 0;

	pwdbBeacon = BTDM_GetRxBeaconSS(padapter);

	if (levelNum == 2) {
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_MEDIUM;

		if ((pHalData->bt_coexist.preRssiStateBeacon == BT_RSSI_STATE_LOW) ||
		    (pHalData->bt_coexist.preRssiStateBeacon == BT_RSSI_STATE_STAY_LOW)) {
			if (pwdbBeacon >= (RssiThresh+BT_FW_COEX_THRESH_TOL)) {
				bcnRssiState = BT_RSSI_STATE_HIGH;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_BEACON_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state switch to High\n"));
			} else {
				bcnRssiState = BT_RSSI_STATE_STAY_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state stay at Low\n"));
			}
		} else {
			if (pwdbBeacon < RssiThresh) {
				bcnRssiState = BT_RSSI_STATE_LOW;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_BEACON_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state switch to Low\n"));
			} else {
				bcnRssiState = BT_RSSI_STATE_STAY_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state stay at High\n"));
			}
		}
	} else if (levelNum == 3) {
		if (RssiThresh > RssiThresh1) {
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON thresh error!!\n"));
			return pHalData->bt_coexist.preRssiStateBeacon;
		}

		if ((pHalData->bt_coexist.preRssiStateBeacon == BT_RSSI_STATE_LOW) ||
		    (pHalData->bt_coexist.preRssiStateBeacon == BT_RSSI_STATE_STAY_LOW)) {
			if (pwdbBeacon >= (RssiThresh+BT_FW_COEX_THRESH_TOL)) {
				bcnRssiState = BT_RSSI_STATE_MEDIUM;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_BEACON_MEDIUM;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state switch to Medium\n"));
			} else {
				bcnRssiState = BT_RSSI_STATE_STAY_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state stay at Low\n"));
			}
		} else if ((pHalData->bt_coexist.preRssiStateBeacon == BT_RSSI_STATE_MEDIUM) ||
			   (pHalData->bt_coexist.preRssiStateBeacon == BT_RSSI_STATE_STAY_MEDIUM)) {
			if (pwdbBeacon >= (RssiThresh1+BT_FW_COEX_THRESH_TOL)) {
				bcnRssiState = BT_RSSI_STATE_HIGH;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_BEACON_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_MEDIUM;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state switch to High\n"));
			} else if (pwdbBeacon < RssiThresh) {
				bcnRssiState = BT_RSSI_STATE_LOW;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_BEACON_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_MEDIUM;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state switch to Low\n"));
			} else {
				bcnRssiState = BT_RSSI_STATE_STAY_MEDIUM;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state stay at Medium\n"));
			}
		} else {
			if (pwdbBeacon < RssiThresh1) {
				bcnRssiState = BT_RSSI_STATE_MEDIUM;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_BEACON_MEDIUM;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_BEACON_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state switch to Medium\n"));
			} else {
				bcnRssiState = BT_RSSI_STATE_STAY_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_BEACON state stay at High\n"));
			}
		}
	}

	pHalData->bt_coexist.preRssiStateBeacon = bcnRssiState;

	return bcnRssiState;
}

u8 BTDM_CheckCoexRSSIState1(struct rtw_adapter *padapter, u8 levelNum,
			    u8 RssiThresh, u8 RssiThresh1)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	s32 UndecoratedSmoothedPWDB = 0;
	u8 btRssiState = 0;

	UndecoratedSmoothedPWDB = BTDM_GetRxSS(padapter);

	if (levelNum == 2) {
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_MEDIUM;

		if ((pHalData->bt_coexist.preRssiState1 == BT_RSSI_STATE_LOW) ||
		    (pHalData->bt_coexist.preRssiState1 == BT_RSSI_STATE_STAY_LOW)) {
			if (UndecoratedSmoothedPWDB >= (RssiThresh+BT_FW_COEX_THRESH_TOL)) {
				btRssiState = BT_RSSI_STATE_HIGH;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_1_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state switch to High\n"));
			} else {
				btRssiState = BT_RSSI_STATE_STAY_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state stay at Low\n"));
			}
		} else {
			if (UndecoratedSmoothedPWDB < RssiThresh) {
				btRssiState = BT_RSSI_STATE_LOW;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_1_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state switch to Low\n"));
			} else {
				btRssiState = BT_RSSI_STATE_STAY_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state stay at High\n"));
			}
		}
	} else if (levelNum == 3) {
		if (RssiThresh > RssiThresh1) {
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 thresh error!!\n"));
			return pHalData->bt_coexist.preRssiState1;
		}

		if ((pHalData->bt_coexist.preRssiState1 == BT_RSSI_STATE_LOW) ||
		    (pHalData->bt_coexist.preRssiState1 == BT_RSSI_STATE_STAY_LOW)) {
			if (UndecoratedSmoothedPWDB >= (RssiThresh+BT_FW_COEX_THRESH_TOL)) {
				btRssiState = BT_RSSI_STATE_MEDIUM;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_1_MEDIUM;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state switch to Medium\n"));
			} else {
				btRssiState = BT_RSSI_STATE_STAY_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state stay at Low\n"));
			}
		} else if ((pHalData->bt_coexist.preRssiState1 == BT_RSSI_STATE_MEDIUM) ||
			   (pHalData->bt_coexist.preRssiState1 == BT_RSSI_STATE_STAY_MEDIUM)) {
			if (UndecoratedSmoothedPWDB >= (RssiThresh1+BT_FW_COEX_THRESH_TOL)) {
				btRssiState = BT_RSSI_STATE_HIGH;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_1_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_MEDIUM;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state switch to High\n"));
			} else if (UndecoratedSmoothedPWDB < RssiThresh) {
				btRssiState = BT_RSSI_STATE_LOW;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_1_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_MEDIUM;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state switch to Low\n"));
			} else {
				btRssiState = BT_RSSI_STATE_STAY_MEDIUM;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state stay at Medium\n"));
			}
		} else {
			if (UndecoratedSmoothedPWDB < RssiThresh1) {
				btRssiState = BT_RSSI_STATE_MEDIUM;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_1_MEDIUM;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_1_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state switch to Medium\n"));
			} else {
				btRssiState = BT_RSSI_STATE_STAY_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI_1 state stay at High\n"));
			}
		}
	}

	pHalData->bt_coexist.preRssiState1 = btRssiState;

	return btRssiState;
}

u8 BTDM_CheckCoexRSSIState(struct rtw_adapter *padapter, u8 levelNum,
			   u8 RssiThresh, u8 RssiThresh1)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	s32 UndecoratedSmoothedPWDB = 0;
	u8 btRssiState = 0;

	UndecoratedSmoothedPWDB = BTDM_GetRxSS(padapter);

	if (levelNum == 2) {
		pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_MEDIUM;

		if ((pHalData->bt_coexist.preRssiState == BT_RSSI_STATE_LOW) ||
		    (pHalData->bt_coexist.preRssiState == BT_RSSI_STATE_STAY_LOW)) {
			if (UndecoratedSmoothedPWDB >= (RssiThresh+BT_FW_COEX_THRESH_TOL)) {
				btRssiState = BT_RSSI_STATE_HIGH;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state switch to High\n"));
			} else {
				btRssiState = BT_RSSI_STATE_STAY_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state stay at Low\n"));
			}
		} else {
			if (UndecoratedSmoothedPWDB < RssiThresh) {
				btRssiState = BT_RSSI_STATE_LOW;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state switch to Low\n"));
			} else {
				btRssiState = BT_RSSI_STATE_STAY_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state stay at High\n"));
			}
		}
	} else if (levelNum == 3) {
		if (RssiThresh > RssiThresh1) {
			RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI thresh error!!\n"));
			return pHalData->bt_coexist.preRssiState;
		}

		if ((pHalData->bt_coexist.preRssiState == BT_RSSI_STATE_LOW) ||
		    (pHalData->bt_coexist.preRssiState == BT_RSSI_STATE_STAY_LOW)) {
			if (UndecoratedSmoothedPWDB >= (RssiThresh+BT_FW_COEX_THRESH_TOL)) {
				btRssiState = BT_RSSI_STATE_MEDIUM;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_MEDIUM;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state switch to Medium\n"));
			} else {
				btRssiState = BT_RSSI_STATE_STAY_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state stay at Low\n"));
			}
		} else if ((pHalData->bt_coexist.preRssiState == BT_RSSI_STATE_MEDIUM) ||
			   (pHalData->bt_coexist.preRssiState == BT_RSSI_STATE_STAY_MEDIUM)) {
			if (UndecoratedSmoothedPWDB >= (RssiThresh1+BT_FW_COEX_THRESH_TOL)) {
				btRssiState = BT_RSSI_STATE_HIGH;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_MEDIUM;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state switch to High\n"));
			} else if (UndecoratedSmoothedPWDB < RssiThresh) {
				btRssiState = BT_RSSI_STATE_LOW;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_LOW;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_MEDIUM;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state switch to Low\n"));
			} else {
				btRssiState = BT_RSSI_STATE_STAY_MEDIUM;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state stay at Medium\n"));
			}
		} else {
			if (UndecoratedSmoothedPWDB < RssiThresh1) {
				btRssiState = BT_RSSI_STATE_MEDIUM;
				pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_WIFI_RSSI_MEDIUM;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_HIGH;
				pHalData->bt_coexist.CurrentState &= ~BT_COEX_STATE_WIFI_RSSI_LOW;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state switch to Medium\n"));
			} else {
				btRssiState = BT_RSSI_STATE_STAY_HIGH;
				RTPRINT(FBT, BT_TRACE, ("[DM][BT], RSSI state stay at High\n"));
			}
		}
	}

	pHalData->bt_coexist.preRssiState = btRssiState;

	return btRssiState;
}

bool rtl8723a_BT_disable_EDCA_turbo(struct rtw_adapter *padapter)
{
	struct bt_mgnt *pBtMgnt;
	struct hal_data_8723a *pHalData;
	u8 bBtChangeEDCA = false;
	u32 EDCA_BT_BE = 0x5ea42b, cur_EDCA_reg;
	bool bRet = false;

	pHalData = GET_HAL_DATA(padapter);
	pBtMgnt = &pHalData->BtInfo.BtMgnt;

	if (!rtl8723a_BT_coexist(padapter)) {
		bRet = false;
		pHalData->bt_coexist.lastBtEdca = 0;
		return bRet;
	}
	if (!((pBtMgnt->bSupportProfile) ||
	    (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC8))) {
		bRet = false;
		pHalData->bt_coexist.lastBtEdca = 0;
		return bRet;
	}

	if (rtl8723a_BT_using_antenna_1(padapter)) {
		bRet = false;
		pHalData->bt_coexist.lastBtEdca = 0;
		return bRet;
	}

	if (pHalData->bt_coexist.exec_cnt < 3)
		pHalData->bt_coexist.exec_cnt++;
	else
		pHalData->bt_coexist.bEDCAInitialized = true;

	/*  When BT is non idle */
	if (!(pHalData->bt_coexist.CurrentState & BT_COEX_STATE_BT_IDLE)) {
		RTPRINT(FBT, BT_TRACE, ("BT state non idle, set bt EDCA\n"));

		/* aggr_num = 0x0909; */
		if (pHalData->odmpriv.DM_EDCA_Table.bCurrentTurboEDCA) {
			bBtChangeEDCA = true;
			pHalData->odmpriv.DM_EDCA_Table.bCurrentTurboEDCA = false;
			pHalData->dmpriv.prv_traffic_idx = 3;
		}
		cur_EDCA_reg = rtl8723au_read32(padapter, REG_EDCA_BE_PARAM);

		if (cur_EDCA_reg != EDCA_BT_BE)
			bBtChangeEDCA = true;
		if (bBtChangeEDCA || !pHalData->bt_coexist.bEDCAInitialized) {
			rtl8723au_write32(padapter, REG_EDCA_BE_PARAM,
					  EDCA_BT_BE);
			pHalData->bt_coexist.lastBtEdca = EDCA_BT_BE;
		}
		bRet = true;
	} else {
		RTPRINT(FBT, BT_TRACE, ("BT state idle, set original EDCA\n"));
		pHalData->bt_coexist.lastBtEdca = 0;
		bRet = false;
	}
	return bRet;
}

void
BTDM_Balance(
	struct rtw_adapter *padapter,
	u8 bBalanceOn,
	u8 ms0,
	u8 ms1
	)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	u8 H2C_Parameter[3] = {0};

	if (bBalanceOn) {
		H2C_Parameter[2] = 1;
		H2C_Parameter[1] = ms1;
		H2C_Parameter[0] = ms0;
		pHalData->bt_coexist.bFWCoexistAllOff = false;
	} else {
		H2C_Parameter[2] = 0;
		H2C_Parameter[1] = 0;
		H2C_Parameter[0] = 0;
	}
	pHalData->bt_coexist.bBalanceOn = bBalanceOn;

	RTPRINT(FBT, BT_TRACE, ("[DM][BT], Balance =[%s:%dms:%dms], write 0xc = 0x%x\n",
		bBalanceOn?"ON":"OFF", ms0, ms1,
		H2C_Parameter[0]<<16|H2C_Parameter[1]<<8|H2C_Parameter[2]));

	FillH2CCmd(padapter, 0xc, 3, H2C_Parameter);
}

void BTDM_AGCTable(struct rtw_adapter *padapter, u8 type)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	if (type == BT_AGCTABLE_OFF) {
		RTPRINT(FBT, BT_TRACE, ("[BT]AGCTable Off!\n"));
		rtl8723au_write32(padapter, 0xc78, 0x641c0001);
		rtl8723au_write32(padapter, 0xc78, 0x631d0001);
		rtl8723au_write32(padapter, 0xc78, 0x621e0001);
		rtl8723au_write32(padapter, 0xc78, 0x611f0001);
		rtl8723au_write32(padapter, 0xc78, 0x60200001);

		PHY_SetRFReg(padapter, PathA, RF_RX_AGC_HP, bRFRegOffsetMask, 0x32000);
		PHY_SetRFReg(padapter, PathA, RF_RX_AGC_HP, bRFRegOffsetMask, 0x71000);
		PHY_SetRFReg(padapter, PathA, RF_RX_AGC_HP, bRFRegOffsetMask, 0xb0000);
		PHY_SetRFReg(padapter, PathA, RF_RX_AGC_HP, bRFRegOffsetMask, 0xfc000);
		PHY_SetRFReg(padapter, PathA, RF_RX_G1, bRFRegOffsetMask, 0x30355);

		pHalData->bt_coexist.b8723aAgcTableOn = false;
	} else if (type == BT_AGCTABLE_ON) {
		RTPRINT(FBT, BT_TRACE, ("[BT]AGCTable On!\n"));
		rtl8723au_write32(padapter, 0xc78, 0x4e1c0001);
		rtl8723au_write32(padapter, 0xc78, 0x4d1d0001);
		rtl8723au_write32(padapter, 0xc78, 0x4c1e0001);
		rtl8723au_write32(padapter, 0xc78, 0x4b1f0001);
		rtl8723au_write32(padapter, 0xc78, 0x4a200001);

		PHY_SetRFReg(padapter, PathA, RF_RX_AGC_HP, bRFRegOffsetMask, 0xdc000);
		PHY_SetRFReg(padapter, PathA, RF_RX_AGC_HP, bRFRegOffsetMask, 0x90000);
		PHY_SetRFReg(padapter, PathA, RF_RX_AGC_HP, bRFRegOffsetMask, 0x51000);
		PHY_SetRFReg(padapter, PathA, RF_RX_AGC_HP, bRFRegOffsetMask, 0x12000);
		PHY_SetRFReg(padapter, PathA, RF_RX_G1, bRFRegOffsetMask, 0x00355);

		pHalData->bt_coexist.b8723aAgcTableOn = true;

		pHalData->bt_coexist.bSWCoexistAllOff = false;
	}
}

void BTDM_BBBackOffLevel(struct rtw_adapter *padapter, u8 type)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	if (type == BT_BB_BACKOFF_OFF) {
		RTPRINT(FBT, BT_TRACE, ("[BT]BBBackOffLevel Off!\n"));
		rtl8723au_write32(padapter, 0xc04, 0x3a05611);
	} else if (type == BT_BB_BACKOFF_ON) {
		RTPRINT(FBT, BT_TRACE, ("[BT]BBBackOffLevel On!\n"));
		rtl8723au_write32(padapter, 0xc04, 0x3a07611);
		pHalData->bt_coexist.bSWCoexistAllOff = false;
	}
}

void BTDM_FWCoexAllOff(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	RTPRINT(FBT, BT_TRACE, ("BTDM_FWCoexAllOff()\n"));
	if (pHalData->bt_coexist.bFWCoexistAllOff)
		return;
	RTPRINT(FBT, BT_TRACE, ("BTDM_FWCoexAllOff(), real Do\n"));

	BTDM_FWCoexAllOff8723A(padapter);

	pHalData->bt_coexist.bFWCoexistAllOff = true;
}

void BTDM_SWCoexAllOff(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	RTPRINT(FBT, BT_TRACE, ("BTDM_SWCoexAllOff()\n"));
	if (pHalData->bt_coexist.bSWCoexistAllOff)
		return;
	RTPRINT(FBT, BT_TRACE, ("BTDM_SWCoexAllOff(), real Do\n"));
	BTDM_SWCoexAllOff8723A(padapter);

	pHalData->bt_coexist.bSWCoexistAllOff = true;
}

void BTDM_HWCoexAllOff(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	RTPRINT(FBT, BT_TRACE, ("BTDM_HWCoexAllOff()\n"));
	if (pHalData->bt_coexist.bHWCoexistAllOff)
		return;
	RTPRINT(FBT, BT_TRACE, ("BTDM_HWCoexAllOff(), real Do\n"));

	BTDM_HWCoexAllOff8723A(padapter);

	pHalData->bt_coexist.bHWCoexistAllOff = true;
}

void BTDM_CoexAllOff(struct rtw_adapter *padapter)
{
	BTDM_FWCoexAllOff(padapter);
	BTDM_SWCoexAllOff(padapter);
	BTDM_HWCoexAllOff(padapter);
}

void rtl8723a_BT_disable_coexist(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *ppwrctrl = &padapter->pwrctrlpriv;

	if (!rtl8723a_BT_coexist(padapter))
		return;

	/*  8723 1Ant doesn't need to turn off bt coexist mechanism. */
	if (rtl8723a_BT_using_antenna_1(padapter))
		return;

	/*  Before enter IPS, turn off FW BT Co-exist mechanism */
	if (ppwrctrl->reg_rfoff == rf_on) {
		RTPRINT(FBT, BT_TRACE, ("[BT][DM], Before enter IPS, turn off all Coexist DM\n"));
		btdm_ResetFWCoexState(padapter);
		BTDM_CoexAllOff(padapter);
		BTDM_SetAntenna(padapter, BTDM_ANT_BT);
	}
}

void BTDM_SignalCompensation(struct rtw_adapter *padapter, u8 *rssi_wifi, u8 *rssi_bt)
{
	BTDM_8723ASignalCompensation(padapter, rssi_wifi, rssi_bt);
}

void rtl8723a_BT_do_coexist(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	if (!rtl8723a_BT_coexist(padapter)) {
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], BT not exists!!\n"));
		return;
	}

	if (!pHalData->bt_coexist.bInitlized) {
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], btdm_InitBtCoexistDM()\n"));
		btdm_InitBtCoexistDM(padapter);
	}

	RTPRINT(FBT, BT_TRACE, ("\n\n[DM][BT], BTDM start!!\n"));

	BTDM_PWDBMonitor(padapter);

	RTPRINT(FBT, BT_TRACE, ("[DM][BT], HW type is 8723\n"));
	BTDM_BTCoexist8723A(padapter);
	RTPRINT(FBT, BT_TRACE, ("[DM][BT], BTDM end!!\n\n"));
}

void BTDM_UpdateCoexState(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	if (!BTDM_IsSameCoexistState(padapter)) {
		RTPRINT(FBT, BT_TRACE, ("[BTCoex], Coexist State[bitMap] change from 0x%"i64fmt"x to 0x%"i64fmt"x,  changeBits = 0x%"i64fmt"x\n",
			pHalData->bt_coexist.PreviousState,
			pHalData->bt_coexist.CurrentState,
			(pHalData->bt_coexist.PreviousState^pHalData->bt_coexist.CurrentState)));
		pHalData->bt_coexist.PreviousState = pHalData->bt_coexist.CurrentState;
	}
}

u8 BTDM_IsSameCoexistState(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState) {
		return true;
	} else {
		RTPRINT(FBT, BT_TRACE, ("[DM][BT], Coexist state changed!!\n"));
		return false;
	}
}

void BTDM_PWDBMonitor(struct rtw_adapter *padapter)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(GetDefaultAdapter(padapter));
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	u8 H2C_Parameter[3] = {0};
	s32 tmpBTEntryMaxPWDB = 0, tmpBTEntryMinPWDB = 0xff;
	u8 i;

	if (pBtMgnt->BtOperationOn) {
		for (i = 0; i < MAX_BT_ASOC_ENTRY_NUM; i++) {
			if (pBTInfo->BtAsocEntry[i].bUsed) {
				if (pBTInfo->BtAsocEntry[i].UndecoratedSmoothedPWDB < tmpBTEntryMinPWDB)
					tmpBTEntryMinPWDB = pBTInfo->BtAsocEntry[i].UndecoratedSmoothedPWDB;
				if (pBTInfo->BtAsocEntry[i].UndecoratedSmoothedPWDB > tmpBTEntryMaxPWDB)
					tmpBTEntryMaxPWDB = pBTInfo->BtAsocEntry[i].UndecoratedSmoothedPWDB;
				/*  Report every BT connection (HS mode) RSSI to FW */
				H2C_Parameter[2] = (u8)(pBTInfo->BtAsocEntry[i].UndecoratedSmoothedPWDB & 0xFF);
				H2C_Parameter[0] = (MAX_FW_SUPPORT_MACID_NUM-1-i);
				RTPRINT(FDM, DM_BT30, ("RSSI report for BT[%d], H2C_Par = 0x%x\n", i, H2C_Parameter[0]));
				FillH2CCmd(padapter, RSSI_SETTING_EID, 3, H2C_Parameter);
				RTPRINT_ADDR(FDM, (DM_PWDB|DM_BT30), ("BT_Entry Mac :"),
					     pBTInfo->BtAsocEntry[i].BTRemoteMACAddr)
				RTPRINT(FDM, (DM_PWDB|DM_BT30),
					("BT rx pwdb[%d] = 0x%x(%d)\n", i,
					pBTInfo->BtAsocEntry[i].UndecoratedSmoothedPWDB,
					pBTInfo->BtAsocEntry[i].UndecoratedSmoothedPWDB));
			}
		}
		if (tmpBTEntryMaxPWDB != 0) {	/*  If associated entry is found */
			pHalData->dmpriv.BT_EntryMaxUndecoratedSmoothedPWDB = tmpBTEntryMaxPWDB;
			RTPRINT(FDM, (DM_PWDB|DM_BT30), ("BT_EntryMaxPWDB = 0x%x(%d)\n",
				tmpBTEntryMaxPWDB, tmpBTEntryMaxPWDB));
		} else {
			pHalData->dmpriv.BT_EntryMaxUndecoratedSmoothedPWDB = 0;
		}
		if (tmpBTEntryMinPWDB != 0xff) { /*  If associated entry is found */
			pHalData->dmpriv.BT_EntryMinUndecoratedSmoothedPWDB = tmpBTEntryMinPWDB;
			RTPRINT(FDM, (DM_PWDB|DM_BT30), ("BT_EntryMinPWDB = 0x%x(%d)\n",
				tmpBTEntryMinPWDB, tmpBTEntryMinPWDB));
		} else {
			pHalData->dmpriv.BT_EntryMinUndecoratedSmoothedPWDB = 0;
		}
	}
}

u8 BTDM_IsBTBusy(struct rtw_adapter *padapter)
{
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_mgnt *pBtMgnt = &pBTInfo->BtMgnt;

	if (pBtMgnt->ExtConfig.bBTBusy)
		return true;
	else
		return false;
}

u8 BTDM_IsWifiBusy(struct rtw_adapter *padapter)
{
/*PMGNT_INFO		pMgntInfo = &GetDefaultAdapter(padapter)->MgntInfo; */
	struct mlme_priv *pmlmepriv = &GetDefaultAdapter(padapter)->mlmepriv;
	struct bt_30info *pBTInfo = GET_BT_INFO(padapter);
	struct bt_traffic *pBtTraffic = &pBTInfo->BtTraffic;

	if (pmlmepriv->LinkDetectInfo.bBusyTraffic ||
		pBtTraffic->Bt30TrafficStatistics.bTxBusyTraffic ||
		pBtTraffic->Bt30TrafficStatistics.bRxBusyTraffic)
		return true;
	else
		return false;
}

u8 BTDM_IsCoexistStateChanged(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.PreviousState == pHalData->bt_coexist.CurrentState)
		return false;
	else
		return true;
}

u8 BTDM_IsWifiUplink(struct rtw_adapter *padapter)
{
/*PMGNT_INFO		pMgntInfo = &GetDefaultAdapter(padapter)->MgntInfo; */
	struct mlme_priv *pmlmepriv;
	struct bt_30info *pBTInfo;
	struct bt_traffic *pBtTraffic;

	pmlmepriv = &padapter->mlmepriv;
	pBTInfo = GET_BT_INFO(padapter);
	pBtTraffic = &pBTInfo->BtTraffic;

	if ((pmlmepriv->LinkDetectInfo.bTxBusyTraffic) ||
		(pBtTraffic->Bt30TrafficStatistics.bTxBusyTraffic))
		return true;
	else
		return false;
}

u8 BTDM_IsWifiDownlink(struct rtw_adapter *padapter)
{
/*PMGNT_INFO		pMgntInfo = &GetDefaultAdapter(padapter)->MgntInfo; */
	struct mlme_priv *pmlmepriv;
	struct bt_30info *pBTInfo;
	struct bt_traffic *pBtTraffic;

	pmlmepriv = &padapter->mlmepriv;
	pBTInfo = GET_BT_INFO(padapter);
	pBtTraffic = &pBTInfo->BtTraffic;

	if ((pmlmepriv->LinkDetectInfo.bRxBusyTraffic) ||
		(pBtTraffic->Bt30TrafficStatistics.bRxBusyTraffic))
		return true;
	else
		return false;
}

u8 BTDM_IsBTHSMode(struct rtw_adapter *padapter)
{
/*PMGNT_INFO		pMgntInfo = &GetDefaultAdapter(padapter)->MgntInfo; */
	struct hal_data_8723a *pHalData;
	struct bt_mgnt *pBtMgnt;

	pHalData = GET_HAL_DATA(padapter);
	pBtMgnt = &pHalData->BtInfo.BtMgnt;

	if (pBtMgnt->BtOperationOn)
		return true;
	else
		return false;
}

u8 BTDM_IsBTUplink(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.BT21TrafficStatistics.bTxBusyTraffic)
		return true;
	else
		return false;
}

u8 BTDM_IsBTDownlink(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.BT21TrafficStatistics.bRxBusyTraffic)
		return true;
	else
		return false;
}

void BTDM_AdjustForBtOperation(struct rtw_adapter *padapter)
{
	RTPRINT(FBT, BT_TRACE, ("[BT][DM], BTDM_AdjustForBtOperation()\n"));
	BTDM_AdjustForBtOperation8723A(padapter);
}

void BTDM_SetBtCoexCurrAntNum(struct rtw_adapter *padapter, u8 antNum)
{
	BTDM_Set8723ABtCoexCurrAntNum(padapter, antNum);
}

void BTDM_ForHalt(struct rtw_adapter *padapter)
{
	if (!rtl8723a_BT_coexist(padapter))
		return;

	BTDM_ForHalt8723A(padapter);
	GET_HAL_DATA(padapter)->bt_coexist.bInitlized = false;
}

void BTDM_WifiScanNotify(struct rtw_adapter *padapter, u8 scanType)
{
	if (!rtl8723a_BT_coexist(padapter))
		return;

	BTDM_WifiScanNotify8723A(padapter, scanType);
}

void BTDM_WifiAssociateNotify(struct rtw_adapter *padapter, u8 action)
{
	if (!rtl8723a_BT_coexist(padapter))
		return;

	BTDM_WifiAssociateNotify8723A(padapter, action);
}

void rtl8723a_BT_mediastatus_notify(struct rtw_adapter *padapter,
				    enum rt_media_status mstatus)
{
	if (!rtl8723a_BT_coexist(padapter))
		return;

	BTDM_MediaStatusNotify8723A(padapter, mstatus);
}

void rtl8723a_BT_specialpacket_notify(struct rtw_adapter *padapter)
{
	if (!rtl8723a_BT_coexist(padapter))
		return;

	BTDM_ForDhcp8723A(padapter);
}

void BTDM_ResetActionProfileState(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	pHalData->bt_coexist.CurrentState &= ~\
		(BT_COEX_STATE_PROFILE_HID|BT_COEX_STATE_PROFILE_A2DP|
		BT_COEX_STATE_PROFILE_PAN|BT_COEX_STATE_PROFILE_SCO);
}

u8 BTDM_IsActionSCO(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData;
	struct bt_30info *pBTInfo;
	struct bt_mgnt *pBtMgnt;
	struct bt_dgb *pBtDbg;
	u8 bRet;

	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;
	pBtDbg = &pBTInfo->BtDbg;
	bRet = false;

	if (pBtDbg->dbgCtrl) {
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_SCO) {
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_SCO;
			bRet = true;
		}
	} else {
		if (pBtMgnt->ExtConfig.NumberOfSCO > 0) {
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_SCO;
			bRet = true;
		}
	}
	return bRet;
}

u8 BTDM_IsActionHID(struct rtw_adapter *padapter)
{
	struct bt_30info *pBTInfo;
	struct hal_data_8723a *pHalData;
	struct bt_mgnt *pBtMgnt;
	struct bt_dgb *pBtDbg;
	u8 bRet;

	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;
	pBtDbg = &pBTInfo->BtDbg;
	bRet = false;

	if (pBtDbg->dbgCtrl) {
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_HID) {
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_HID;
			bRet = true;
		}
	} else {
		if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_HID) &&
		    pBtMgnt->ExtConfig.NumberOfHandle == 1) {
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_HID;
			bRet = true;
		}
	}
	return bRet;
}

u8 BTDM_IsActionA2DP(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData;
	struct bt_30info *pBTInfo;
	struct bt_mgnt *pBtMgnt;
	struct bt_dgb *pBtDbg;
	u8 bRet;

	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;
	pBtDbg = &pBTInfo->BtDbg;
	bRet = false;

	if (pBtDbg->dbgCtrl) {
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_A2DP) {
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_A2DP;
			bRet = true;
		}
	} else {
		if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_A2DP) &&
		    pBtMgnt->ExtConfig.NumberOfHandle == 1) {
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_A2DP;
			bRet = true;
		}
	}
	return bRet;
}

u8 BTDM_IsActionPAN(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData;
	struct bt_30info *pBTInfo;
	struct bt_mgnt *pBtMgnt;
	struct bt_dgb *pBtDbg;
	u8 bRet;

	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;
	pBtDbg = &pBTInfo->BtDbg;
	bRet = false;

	if (pBtDbg->dbgCtrl) {
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_PAN) {
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_PAN;
			bRet = true;
		}
	} else {
		if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_PAN) &&
		    pBtMgnt->ExtConfig.NumberOfHandle == 1) {
			pHalData->bt_coexist.CurrentState |= BT_COEX_STATE_PROFILE_PAN;
			bRet = true;
		}
	}
	return bRet;
}

u8 BTDM_IsActionHIDA2DP(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData;
	struct bt_30info *pBTInfo;
	struct bt_mgnt *pBtMgnt;
	struct bt_dgb *pBtDbg;
	u8 bRet;

	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtMgnt = &pBTInfo->BtMgnt;
	pBtDbg = &pBTInfo->BtDbg;
	bRet = false;

	if (pBtDbg->dbgCtrl) {
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_HID_A2DP) {
			pHalData->bt_coexist.CurrentState |= (BT_COEX_STATE_PROFILE_HID|BT_COEX_STATE_PROFILE_A2DP);
			bRet = true;
		}
	} else {
		if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_HID) &&
		    BTHCI_CheckProfileExist(padapter, BT_PROFILE_A2DP)) {
			pHalData->bt_coexist.CurrentState |= (BT_COEX_STATE_PROFILE_HID|BT_COEX_STATE_PROFILE_A2DP);
			bRet = true;
		}
	}
	return bRet;
}

u8 BTDM_IsActionHIDPAN(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData;
	struct bt_30info *pBTInfo;
	struct bt_dgb *pBtDbg;
	u8 bRet;

	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtDbg = &pBTInfo->BtDbg;
	bRet = false;

	if (pBtDbg->dbgCtrl) {
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_HID_PAN) {
			pHalData->bt_coexist.CurrentState |= (BT_COEX_STATE_PROFILE_HID|BT_COEX_STATE_PROFILE_PAN);
			bRet = true;
		}
	} else {
		if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_HID) &&
		    BTHCI_CheckProfileExist(padapter, BT_PROFILE_PAN)) {
			pHalData->bt_coexist.CurrentState |= (BT_COEX_STATE_PROFILE_HID|BT_COEX_STATE_PROFILE_PAN);
			bRet = true;
		}
	}
	return bRet;
}

u8 BTDM_IsActionPANA2DP(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData;
	struct bt_30info *pBTInfo;
	struct bt_dgb *pBtDbg;
	u8 bRet;

	pHalData = GET_HAL_DATA(padapter);
	pBTInfo = GET_BT_INFO(padapter);
	pBtDbg = &pBTInfo->BtDbg;
	bRet = false;

	if (pBtDbg->dbgCtrl) {
		if (pBtDbg->dbgProfile == BT_DBG_PROFILE_PAN_A2DP) {
			pHalData->bt_coexist.CurrentState |= (BT_COEX_STATE_PROFILE_PAN|BT_COEX_STATE_PROFILE_A2DP);
			bRet = true;
		}
	} else {
		if (BTHCI_CheckProfileExist(padapter, BT_PROFILE_PAN) && BTHCI_CheckProfileExist(padapter, BT_PROFILE_A2DP)) {
			pHalData->bt_coexist.CurrentState |= (BT_COEX_STATE_PROFILE_PAN|BT_COEX_STATE_PROFILE_A2DP);
			bRet = true;
		}
	}
	return bRet;
}

bool rtl8723a_BT_enabled(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.bCurBtDisabled)
		return false;
	else
		return true;
}

/*  ===== End of sync from SD7 driver HAL/BTCoexist/HalBtCoexist.c ===== */

/*  ===== Below this line is sync from SD7 driver HAL/HalBT.c ===== */

/*  */
/*local function */
/*  */

static void halbt_InitHwConfig8723A(struct rtw_adapter *padapter)
{
}

/*  */
/*extern function */
/*  */
u8 HALBT_GetPGAntNum(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	return pHalData->bt_coexist.BT_Ant_Num;
}

void HALBT_SetKey(struct rtw_adapter *padapter, u8 EntryNum)
{
	struct bt_30info *pBTinfo;
	struct bt_asoc_entry *pBtAssocEntry;
	u16				usConfig = 0;

	pBTinfo = GET_BT_INFO(padapter);
	pBtAssocEntry = &pBTinfo->BtAsocEntry[EntryNum];

	pBtAssocEntry->HwCAMIndex = BT_HWCAM_STAR + EntryNum;

	usConfig = CAM_VALID | (CAM_AES << 2);
	rtl8723a_cam_write(padapter, pBtAssocEntry->HwCAMIndex, usConfig,
			   pBtAssocEntry->BTRemoteMACAddr,
			   pBtAssocEntry->PTK + TKIP_ENC_KEY_POS);
}

void HALBT_RemoveKey(struct rtw_adapter *padapter, u8 EntryNum)
{
	struct bt_30info *pBTinfo;
	struct bt_asoc_entry *pBtAssocEntry;

	pBTinfo = GET_BT_INFO(padapter);
	pBtAssocEntry = &pBTinfo->BtAsocEntry[EntryNum];

	if (pBTinfo->BtAsocEntry[EntryNum].HwCAMIndex != 0) {
		/*  ToDo : add New HALBT_RemoveKey function !! */
		if (pBtAssocEntry->HwCAMIndex >= BT_HWCAM_STAR &&
		    pBtAssocEntry->HwCAMIndex < HALF_CAM_ENTRY)
			rtl8723a_cam_empty_entry(padapter,
						 pBtAssocEntry->HwCAMIndex);
		pBTinfo->BtAsocEntry[EntryNum].HwCAMIndex = 0;
	}
}

void rtl8723a_BT_init_hal_vars(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData;

	pHalData = GET_HAL_DATA(padapter);

	pHalData->bt_coexist.BluetoothCoexist = pHalData->EEPROMBluetoothCoexist;
	pHalData->bt_coexist.BT_Ant_Num = pHalData->EEPROMBluetoothAntNum;
	pHalData->bt_coexist.BT_CoexistType = pHalData->EEPROMBluetoothType;
	pHalData->bt_coexist.BT_Ant_isolation = pHalData->EEPROMBluetoothAntIsolation;
	pHalData->bt_coexist.bt_radiosharedtype = pHalData->EEPROMBluetoothRadioShared;

	RT_TRACE(_module_hal_init_c_, _drv_info_,
		 ("BT Coexistance = 0x%x\n", rtl8723a_BT_coexist(padapter)));

	if (rtl8723a_BT_coexist(padapter)) {
		if (pHalData->bt_coexist.BT_Ant_Num == Ant_x2) {
			BTDM_SetBtCoexCurrAntNum(padapter, 2);
			RT_TRACE(_module_hal_init_c_, _drv_info_, ("BlueTooth BT_Ant_Num = Antx2\n"));
		} else if (pHalData->bt_coexist.BT_Ant_Num == Ant_x1) {
			BTDM_SetBtCoexCurrAntNum(padapter, 1);
			RT_TRACE(_module_hal_init_c_, _drv_info_, ("BlueTooth BT_Ant_Num = Antx1\n"));
		}
		pHalData->bt_coexist.bBTBusyTraffic = false;
		pHalData->bt_coexist.bBTTrafficModeSet = false;
		pHalData->bt_coexist.bBTNonTrafficModeSet = false;
		pHalData->bt_coexist.CurrentState = 0;
		pHalData->bt_coexist.PreviousState = 0;

		RT_TRACE(_module_hal_init_c_, _drv_info_,
			 ("bt_radiosharedType = 0x%x\n",
			 pHalData->bt_coexist.bt_radiosharedtype));
	}
}

bool rtl8723a_BT_coexist(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	if (pHalData->bt_coexist.BluetoothCoexist)
		return true;
	else
		return false;
}

u8 HALBT_BTChipType(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	return pHalData->bt_coexist.BT_CoexistType;
}

void rtl8723a_BT_init_hwconfig(struct rtw_adapter *padapter)
{
	halbt_InitHwConfig8723A(padapter);
	rtl8723a_BT_do_coexist(padapter);
}

void HALBT_SetRtsCtsNoLenLimit(struct rtw_adapter *padapter)
{
}

/*  ===== End of sync from SD7 driver HAL/HalBT.c ===== */

void rtl8723a_dual_antenna_detection(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData;
	struct dm_odm_t *pDM_Odm;
	struct sw_ant_sw *pDM_SWAT_Table;
	u8 i;

	pHalData = GET_HAL_DATA(padapter);
	pDM_Odm = &pHalData->odmpriv;
	pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;

	/*  */
	/*  <Roger_Notes> RTL8723A Single and Dual antenna dynamic detection
	    mechanism when RF power state is on. */
	/*  We should take power tracking, IQK, LCK, RCK RF read/write
	    operation into consideration. */
	/*  2011.12.15. */
	/*  */
	if (!pHalData->bAntennaDetected) {
		u8 btAntNum = BT_GetPGAntNum(padapter);

		/*  Set default antenna B status */
		if (btAntNum == Ant_x2)
			pDM_SWAT_Table->ANTB_ON = true;
		else if (btAntNum == Ant_x1)
			pDM_SWAT_Table->ANTB_ON = false;
		else
			pDM_SWAT_Table->ANTB_ON = true;

		if (pHalData->CustomerID != RT_CID_TOSHIBA) {
			for (i = 0; i < MAX_ANTENNA_DETECTION_CNT; i++) {
				if (ODM_SingleDualAntennaDetection
				    (&pHalData->odmpriv, ANTTESTALL) == true)
					break;
			}

			/*  Set default antenna number for BT coexistence */
			if (btAntNum == Ant_x2)
				BT_SetBtCoexCurrAntNum(padapter,
						       pDM_SWAT_Table->
						       ANTB_ON ? 2 : 1);
		}
		pHalData->bAntennaDetected = true;
	}
}
