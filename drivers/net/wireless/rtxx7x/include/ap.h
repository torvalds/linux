/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#ifndef __AP_H__
#define __AP_H__


/* ============================================================= */
/*      Function Prototypes */
/* ============================================================= */

/* ap_data.c */

BOOLEAN APBridgeToWirelessSta(
    IN  PRTMP_ADAPTER   pAd,
    IN  PUCHAR          pHeader,
    IN  UINT            HdrLen,
    IN  PUCHAR          pData,
    IN  UINT            DataLen,
    IN  ULONG           fromwdsidx);

VOID RTMP_BASetup(
	IN PRTMP_ADAPTER pAd,
	IN PMAC_TABLE_ENTRY pMacEntry,
	IN UINT8 UserPriority);

VOID	APSendPackets(
	IN	NDIS_HANDLE		MiniportAdapterContext,
	IN	PPNDIS_PACKET	ppPacketArray,
	IN	UINT			NumberOfPackets);

NDIS_STATUS APSendPacket(
    IN  PRTMP_ADAPTER   pAd,
    IN  PNDIS_PACKET    pPacket);

NDIS_STATUS APInsertPsQueue(
	IN PRTMP_ADAPTER pAd,
	IN PNDIS_PACKET pPacket,
	IN MAC_TABLE_ENTRY *pMacEntry,
	IN UCHAR QueIdx);

NDIS_STATUS APHardTransmit(
	IN	PRTMP_ADAPTER	pAd,
	IN	TX_BLK			*pTxBlk,
	IN	UCHAR			QueIdx);

VOID APRxEAPOLFrameIndicate(
	IN	PRTMP_ADAPTER	pAd,
	IN	MAC_TABLE_ENTRY	*pEntry,
	IN	RX_BLK			*pRxBlk,
	IN	UCHAR			FromWhichBSSID);

NDIS_STATUS APCheckRxError(
	IN	PRTMP_ADAPTER	pAd,
	IN	PRT28XX_RXD_STRUC		pRxD,
	IN	UCHAR			Wcid);

BOOLEAN APCheckClass2Class3Error(
    IN  PRTMP_ADAPTER   pAd,
	IN ULONG Wcid, 
	IN  PHEADER_802_11  pHeader);

VOID APHandleRxPsPoll(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			pAddr,
	IN	USHORT			Aid,
    IN	BOOLEAN			isActive);

VOID    RTMPDescriptorEndianChange(
    IN  PUCHAR          pData,
    IN  ULONG           DescriptorType);
    
VOID    RTMPFrameEndianChange(
    IN  PRTMP_ADAPTER   pAd,
    IN  PUCHAR          pData,
    IN  ULONG           Dir,
    IN  BOOLEAN         FromRxDoneInt);

/* ap_assoc.c */

VOID APAssocStateMachineInit(
    IN  PRTMP_ADAPTER   pAd, 
    IN  STATE_MACHINE *S, 
    OUT STATE_MACHINE_FUNC Trans[]);

VOID  APPeerAssocReqAction(
    IN  PRTMP_ADAPTER   pAd, 
    IN  MLME_QUEUE_ELEM *Elem);

VOID  APPeerReassocReqAction(
    IN  PRTMP_ADAPTER   pAd, 
    IN  MLME_QUEUE_ELEM *Elem);

VOID  APPeerDisassocReqAction(
    IN  PRTMP_ADAPTER   pAd, 
    IN  MLME_QUEUE_ELEM *Elem);

VOID MbssKickOutStas(
	IN PRTMP_ADAPTER pAd,
	IN INT apidx,
	IN USHORT Reason);

VOID APMlmeKickOutSta(
    IN PRTMP_ADAPTER pAd, 
	IN PUCHAR pStaAddr, 
	IN UCHAR Wcid,
	IN USHORT Reason);

VOID APMlmeDisassocReqAction(
    IN PRTMP_ADAPTER pAd, 
    IN MLME_QUEUE_ELEM *Elem);

VOID  APCls3errAction(
    IN  PRTMP_ADAPTER   pAd, 
	IN 	ULONG Wcid,
    IN	PHEADER_802_11	pHeader);


USHORT APBuildAssociation(
    IN PRTMP_ADAPTER pAd,
    IN MAC_TABLE_ENTRY *pEntry,
    IN USHORT CapabilityInfo,
    IN UCHAR  MaxSupportedRateIn500Kbps,
    IN UCHAR  *RSN,
    IN UCHAR  *pRSNLen, 
    IN BOOLEAN bWmmCapable,
    IN ULONG  RalinkIe,
    IN EXT_CAP_INFO_ELEMENT ExtCapInfo,
	IN HT_CAPABILITY_IE		*pHtCapability,
	OUT UCHAR		 *pHtCapabilityLen,
    OUT USHORT *pAid);

/*
VOID	RTMPAddClientSec(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR	BssIdx,
	IN UCHAR		 KeyIdx,
	IN UCHAR		 CipherAlg,
	IN PUCHAR		 pKey,
	IN PUCHAR		 pTxMic,
	IN PUCHAR		 pRxMic,
	IN MAC_TABLE_ENTRY *pEntry);
*/

/* ap_auth.c */

void APAuthStateMachineInit(
    IN PRTMP_ADAPTER pAd, 
    IN STATE_MACHINE *Sm, 
    OUT STATE_MACHINE_FUNC Trans[]);

VOID APCls2errAction(
    IN PRTMP_ADAPTER pAd, 
	IN 	ULONG Wcid,
    IN	PHEADER_802_11	pHeader);

/* ap_connect.c */


VOID APMakeBssBeacon(
    IN  PRTMP_ADAPTER   pAd,
	IN	INT				apidx);

VOID  APUpdateBeaconFrame(
    IN  PRTMP_ADAPTER   pAd,
	IN	INT				apidx);

VOID APMakeAllBssBeacon(
    IN  PRTMP_ADAPTER   pAd);

VOID  APUpdateAllBeaconFrame(
    IN  PRTMP_ADAPTER   pAd);


/* ap_sync.c */

VOID APSyncStateMachineInit(
    IN PRTMP_ADAPTER pAd,
    IN STATE_MACHINE *Sm,
    OUT STATE_MACHINE_FUNC Trans[]);

VOID APScanTimeout(
	IN PVOID SystemSpecific1,
	IN PVOID FunctionContext,
	IN PVOID SystemSpecific2,
	IN PVOID SystemSpecific3);

VOID APInvalidStateWhenScan(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem);

VOID APScanTimeoutAction(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem);

VOID APPeerProbeReqAction(
    IN  PRTMP_ADAPTER pAd, 
    IN  MLME_QUEUE_ELEM *Elem);

VOID APPeerBeaconAction(
    IN PRTMP_ADAPTER pAd, 
    IN MLME_QUEUE_ELEM *Elem);

VOID APMlmeScanReqAction(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem);

VOID APPeerBeaconAtScanAction(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem);

VOID APScanCnclAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem);

VOID ApSiteSurvey(
	IN	PRTMP_ADAPTER  		pAd,
	IN	PNDIS_802_11_SSID	pSsid,
	IN	UCHAR				ScanType,
	IN	BOOLEAN				ChannelSel);

VOID SupportRate(
	IN PUCHAR SupRate,
	IN UCHAR SupRateLen,
	IN PUCHAR ExtRate,
	IN UCHAR ExtRateLen,
	OUT PUCHAR *Rates,
	OUT PUCHAR RatesLen,
	OUT PUCHAR pMaxSupportRate);


BOOLEAN ApScanRunning(
	IN PRTMP_ADAPTER pAd);
	
#ifdef DOT11N_DRAFT3
VOID APOverlappingBSSScan(
	IN RTMP_ADAPTER *pAd);

INT GetBssCoexEffectedChRange(
	IN RTMP_ADAPTER *pAd,
	IN BSS_COEX_CH_RANGE *pCoexChRange);

#endif /* DOT11N_DRAFT3 */

/* ap_wpa.c */
VOID WpaStateMachineInit(
    IN  PRTMP_ADAPTER   pAd, 
    IN  STATE_MACHINE *Sm, 
    OUT STATE_MACHINE_FUNC Trans[]);

/* ap_mlme.c */
VOID APMlmePeriodicExec(
    IN  PRTMP_ADAPTER   pAd);

VOID APMlmeSelectTxRateTable(
	IN PRTMP_ADAPTER		pAd,
	IN PMAC_TABLE_ENTRY		pEntry,
	IN PUCHAR				*ppTable,
	IN PUCHAR				pTableSize,
	IN PUCHAR				pInitTxRateIdx);

VOID APMlmeSetTxRate(
	IN PRTMP_ADAPTER		pAd,
	IN PMAC_TABLE_ENTRY		pEntry,
	IN PRTMP_TX_RATE_SWITCH	pTxRate);

VOID APMlmeSelectRateSwitchTable11N3SReplacement(
	IN PUCHAR	*ppTable);


VOID APMlmeDynamicTxRateSwitching(
    IN PRTMP_ADAPTER pAd);

VOID APQuickResponeForRateUpExec(
    IN PVOID SystemSpecific1, 
    IN PVOID FunctionContext, 
    IN PVOID SystemSpecific2, 
    IN PVOID SystemSpecific3);

BOOLEAN APMsgTypeSubst(
    IN PRTMP_ADAPTER pAd,
    IN PFRAME_802_11 pFrame, 
    OUT INT *Machine, 
    OUT INT *MsgType);

VOID APQuickResponeForRateUpExec(
    IN PVOID SystemSpecific1, 
    IN PVOID FunctionContext, 
    IN PVOID SystemSpecific2, 
    IN PVOID SystemSpecific3);

#ifdef NEW_RATE_ADAPT_SUPPORT
VOID APMlmeDynamicTxRateSwitchingAdapt(
    IN PRTMP_ADAPTER pAd,
    IN ULONG idx);

VOID APQuickResponeForRateUpExecAdapt(
    IN PRTMP_ADAPTER pAd,
    IN ULONG idx);
#endif /*NEW_RATE_ADAPT_SUPPORT */

#ifdef RTMP_MAC_USB
VOID BeaconUpdateExec(
    IN PVOID SystemSpecific1, 
    IN PVOID FunctionContext, 
    IN PVOID SystemSpecific2, 
    IN PVOID SystemSpecific3);
#endif /* RTMP_MAC_USB */

VOID RTMPSetPiggyBack(
	IN PRTMP_ADAPTER	pAd,
	IN BOOLEAN			bPiggyBack);

VOID APAsicEvaluateRxAnt(
	IN PRTMP_ADAPTER	pAd);

VOID APAsicRxAntEvalTimeout(
	IN PRTMP_ADAPTER	pAd);

/* ap.c */
NDIS_STATUS APInitialize(
    IN  PRTMP_ADAPTER   pAd);

VOID APShutdown(
    IN PRTMP_ADAPTER    pAd);

VOID APStartUp(
    IN  PRTMP_ADAPTER   pAd);

VOID APStop(
    IN  PRTMP_ADAPTER   pAd);

VOID APCleanupPsQueue(
    IN  PRTMP_ADAPTER   pAd,
    IN  PQUEUE_HEADER   pQueue);

VOID MacTableReset(
    IN  PRTMP_ADAPTER   pAd);

MAC_TABLE_ENTRY *MacTableInsertEntry(
    IN  PRTMP_ADAPTER   pAd, 
    IN  PUCHAR          pAddr,
	IN	UCHAR			apidx,
	IN	UCHAR           OpMode,
	IN BOOLEAN	CleanAll); 

BOOLEAN MacTableDeleteEntry(
    IN  PRTMP_ADAPTER   pAd, 
	IN USHORT wcid,
    IN  PUCHAR          pAddr);

MAC_TABLE_ENTRY *MacTableLookup(
    IN  PRTMP_ADAPTER   pAd, 
    IN  PUCHAR          pAddr);

VOID MacTableMaintenance(
    IN PRTMP_ADAPTER pAd);

UINT32 MacTableAssocStaNumGet(
	IN PRTMP_ADAPTER pAd);

MAC_TABLE_ENTRY *APSsPsInquiry(
    IN  PRTMP_ADAPTER   pAd, 
    IN  PUCHAR          pAddr, 
    OUT SST             *Sst, 
    OUT USHORT          *Aid,
    OUT UCHAR           *PsMode,
    OUT UCHAR           *Rate); 

BOOLEAN APPsIndicate(
    IN  PRTMP_ADAPTER   pAd, 
    IN  PUCHAR          pAddr, 
	IN ULONG Wcid, 
    IN  UCHAR           Psm);

#ifdef SYSTEM_LOG_SUPPORT
VOID ApLogEvent(
    IN PRTMP_ADAPTER    pAd,
    IN PUCHAR           pAddr,
    IN USHORT           Event);
#else
#define ApLogEvent(_pAd, _pAddr, _Event)
#endif /* SYSTEM_LOG_SUPPORT */

#ifdef DOT11_N_SUPPORT
VOID APUpdateOperationMode(
    IN PRTMP_ADAPTER pAd);
#endif /* DOT11_N_SUPPORT */

VOID APUpdateCapabilityAndErpIe(
	IN PRTMP_ADAPTER pAd);

BOOLEAN ApCheckAccessControlList(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR        pAddr,
	IN UCHAR         Apidx);

VOID ApUpdateAccessControlList(
    IN PRTMP_ADAPTER pAd,
    IN UCHAR         Apidx);

VOID ApEnqueueNullFrame(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR        pAddr,
	IN UCHAR         TxRate,
	IN UCHAR         PID,
	IN UCHAR         apidx,
    IN BOOLEAN       bQosNull,
    IN BOOLEAN       bEOSP,
    IN UCHAR         OldUP);

/* ap_sanity.c */


BOOLEAN PeerAssocReqCmmSanity(
    IN PRTMP_ADAPTER pAd, 
	IN BOOLEAN isRessoc,
    IN VOID *Msg, 
    IN ULONG MsgLen, 
    OUT PUCHAR pAddr2,
    OUT USHORT *pCapabilityInfo, 
    OUT USHORT *pListenInterval, 
    OUT PUCHAR pApAddr,
    OUT UCHAR *pSsidLen,
    OUT char *Ssid,
    OUT UCHAR *pRatesLen,
    OUT UCHAR Rates[],
    OUT UCHAR *RSN,
    OUT UCHAR *pRSNLen,
    OUT BOOLEAN *pbWmmCapable,
    OUT ULONG  *pRalinkIe,
    OUT EXT_CAP_INFO_ELEMENT	*pExtCapInfo,
    OUT UCHAR		 *pHtCapabilityLen,
    OUT HT_CAPABILITY_IE *pHtCapability);


BOOLEAN PeerDisassocReqSanity(
    IN PRTMP_ADAPTER pAd, 
    IN VOID *Msg, 
    IN ULONG MsgLen, 
    OUT PUCHAR pAddr2, 
    OUT	UINT16	*SeqNum,
    OUT USHORT *Reason);

BOOLEAN PeerDeauthReqSanity(
    IN PRTMP_ADAPTER pAd, 
    IN VOID *Msg, 
    IN ULONG MsgLen, 
    OUT PUCHAR pAddr2, 
   	OUT	UINT16	*SeqNum,    
    OUT USHORT *Reason);

BOOLEAN APPeerAuthSanity(
    IN PRTMP_ADAPTER pAd, 
    IN VOID *Msg, 
    IN ULONG MsgLen, 
	OUT PUCHAR pAddr1, 
    OUT PUCHAR pAddr2, 
    OUT USHORT *Alg, 
    OUT USHORT *Seq, 
    OUT USHORT *Status, 
    OUT CHAR *ChlgText
	);


#ifdef DOT1X_SUPPORT
/* ap_cfg.h */
INT	Set_OwnIPAddr_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT	Set_EAPIfName_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT	Set_PreAuthIfName_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

/* Define in ap.c */
BOOLEAN DOT1X_InternalCmdAction(
    IN  PRTMP_ADAPTER	pAd,
    IN  MAC_TABLE_ENTRY *pEntry,
    IN	UINT8			cmd);

BOOLEAN DOT1X_EapTriggerAction(
    IN  PRTMP_ADAPTER	pAd,
    IN  MAC_TABLE_ENTRY *pEntry);
#endif /* DOT1X_SUPPORT */
#endif  /* __AP_H__ */

VOID AP_E2PROM_IOCTL_PostCtrl(
	IN	RTMP_IOCTL_INPUT_STRUCT	*wrq,
	IN	PSTRING					msg);

VOID IAPP_L2_UpdatePostCtrl(
	IN PRTMP_ADAPTER	pAd,
    IN UINT8 *mac_p,
    IN INT  bssid);
