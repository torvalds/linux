/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
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
 *************************************************************************

    Module Name:
    rtmp.h

    Abstract:
    Miniport generic portion header file

    Revision History:
    Who         When          What
    --------    ----------    ----------------------------------------------
    Paul Lin    2002-08-01    created
    James Tan   2002-09-06    modified (Revise NTCRegTable)
    John Chang  2004-09-06    modified for RT2600
*/
#ifndef __RTMP_H__
#define __RTMP_H__

#include "spectrum_def.h"
#include "rtmp_dot11.h"
#include "rtmp_chip.h"

typedef struct _RTMP_ADAPTER RTMP_ADAPTER;
typedef struct _RTMP_ADAPTER *PRTMP_ADAPTER;

typedef struct _RTMP_CHIP_OP_ RTMP_CHIP_OP;

/*#define DBG           1 */

/*#define DBG_DIAGNOSE          1 */

/*+++Add by shiang for merge MiniportMMRequest() and MiniportDataMMRequest() into one function */
#define MAX_DATAMM_RETRY	3
#define MGMT_USE_QUEUE_FLAG	0x80
/*---Add by shiang for merge MiniportMMRequest() and MiniportDataMMRequest() into one function */

#define	MAXSEQ		(0xFFF)

extern unsigned char SNAP_AIRONET[];
extern unsigned char CISCO_OUI[];
extern u8 BaSizeArray[4];

extern u8 BROADCAST_ADDR[MAC_ADDR_LEN];
extern u8 ZERO_MAC_ADDR[MAC_ADDR_LEN];
extern unsigned long BIT32[32];
extern u8 BIT8[8];
extern char *CipherName[];
extern char *MCSToMbps[];
extern u8 RxwiMCSToOfdmRate[12];
extern u8 SNAP_802_1H[6];
extern u8 SNAP_BRIDGE_TUNNEL[6];
extern u8 SNAP_AIRONET[8];
extern u8 CKIP_LLC_SNAP[8];
extern u8 EAPOL_LLC_SNAP[8];
extern u8 EAPOL[2];
extern u8 IPX[2];
extern u8 APPLE_TALK[2];
extern u8 RateIdToPlcpSignal[12];	/* see IEEE802.11a-1999 p.14 */
extern u8 OfdmRateToRxwiMCS[];
extern u8 OfdmSignalToRateId[16];
extern u8 default_cwmin[4];
extern u8 default_cwmax[4];
extern u8 default_sta_aifsn[4];
extern u8 MapUserPriorityToAccessCategory[8];

extern u16 RateUpPER[];
extern u16 RateDownPER[];
extern u8 Phy11BNextRateDownward[];
extern u8 Phy11BNextRateUpward[];
extern u8 Phy11BGNextRateDownward[];
extern u8 Phy11BGNextRateUpward[];
extern u8 Phy11ANextRateDownward[];
extern u8 Phy11ANextRateUpward[];
extern char RssiSafeLevelForTxRate[];
extern u8 RateIdToMbps[];
extern u16 RateIdTo500Kbps[];

extern u8 CipherSuiteWpaNoneTkip[];
extern u8 CipherSuiteWpaNoneTkipLen;

extern u8 CipherSuiteWpaNoneAes[];
extern u8 CipherSuiteWpaNoneAesLen;

extern u8 SsidIe;
extern u8 SupRateIe;
extern u8 ExtRateIe;

extern u8 HtCapIe;
extern u8 AddHtInfoIe;
extern u8 NewExtChanIe;

extern u8 ErpIe;
extern u8 DsIe;
extern u8 TimIe;
extern u8 WpaIe;
extern u8 Wpa2Ie;
extern u8 IbssIe;
extern u8 Ccx2Ie;
extern u8 WapiIe;

extern u8 WPA_OUI[];
extern u8 RSN_OUI[];
extern u8 WAPI_OUI[];
extern u8 WME_INFO_ELEM[];
extern u8 WME_PARM_ELEM[];
extern u8 Ccx2QosInfo[];
extern u8 Ccx2IeInfo[];
extern u8 RALINK_OUI[];
extern u8 PowerConstraintIE[];

extern u8 RateSwitchTable[];
extern u8 RateSwitchTable11B[];
extern u8 RateSwitchTable11G[];
extern u8 RateSwitchTable11BG[];

extern u8 RateSwitchTable11BGN1S[];
extern u8 RateSwitchTable11BGN2S[];
extern u8 RateSwitchTable11BGN2SForABand[];
extern u8 RateSwitchTable11N1S[];
extern u8 RateSwitchTable11N2S[];
extern u8 RateSwitchTable11N2SForABand[];

extern u8 PRE_N_HT_OUI[];

typedef struct _RSSI_SAMPLE {
	char LastRssi0;		/* last received RSSI */
	char LastRssi1;		/* last received RSSI */
	char LastRssi2;		/* last received RSSI */
	char AvgRssi0;
	char AvgRssi1;
	char AvgRssi2;
	short AvgRssi0X8;
	short AvgRssi1X8;
	short AvgRssi2X8;
} RSSI_SAMPLE;

/* */
/*  Queue structure and macros */
/* */
typedef struct _QUEUE_ENTRY {
	struct _QUEUE_ENTRY *Next;
} QUEUE_ENTRY, *PQUEUE_ENTRY;

/* Queue structure */
typedef struct _QUEUE_HEADER {
	PQUEUE_ENTRY Head;
	PQUEUE_ENTRY Tail;
	unsigned long Number;
} QUEUE_HEADER, *PQUEUE_HEADER;

#define InitializeQueueHeader(QueueHeader)              \
{                                                       \
	(QueueHeader)->Head = (QueueHeader)->Tail = NULL;   \
	(QueueHeader)->Number = 0;                          \
}

#define RemoveHeadQueue(QueueHeader)                \
(QueueHeader)->Head;                                \
{                                                   \
	PQUEUE_ENTRY pNext;                             \
	if ((QueueHeader)->Head != NULL)				\
	{												\
		pNext = (QueueHeader)->Head->Next;          \
		(QueueHeader)->Head->Next = NULL;		\
		(QueueHeader)->Head = pNext;                \
		if (pNext == NULL)                          \
			(QueueHeader)->Tail = NULL;             \
		(QueueHeader)->Number--;                    \
	}												\
}

#define InsertHeadQueue(QueueHeader, QueueEntry)            \
{                                                           \
		((PQUEUE_ENTRY)QueueEntry)->Next = (QueueHeader)->Head; \
		(QueueHeader)->Head = (PQUEUE_ENTRY)(QueueEntry);       \
		if ((QueueHeader)->Tail == NULL)                        \
			(QueueHeader)->Tail = (PQUEUE_ENTRY)(QueueEntry);   \
		(QueueHeader)->Number++;                                \
}

#define InsertTailQueue(QueueHeader, QueueEntry)                \
{                                                               \
	((PQUEUE_ENTRY)QueueEntry)->Next = NULL;                    \
	if ((QueueHeader)->Tail)                                    \
		(QueueHeader)->Tail->Next = (PQUEUE_ENTRY)(QueueEntry); \
	else                                                        \
		(QueueHeader)->Head = (PQUEUE_ENTRY)(QueueEntry);       \
	(QueueHeader)->Tail = (PQUEUE_ENTRY)(QueueEntry);           \
	(QueueHeader)->Number++;                                    \
}

#define InsertTailQueueAc(pAd, pEntry, QueueHeader, QueueEntry)			\
{																		\
	((PQUEUE_ENTRY)QueueEntry)->Next = NULL;							\
	if ((QueueHeader)->Tail)											\
		(QueueHeader)->Tail->Next = (PQUEUE_ENTRY)(QueueEntry);			\
	else																\
		(QueueHeader)->Head = (PQUEUE_ENTRY)(QueueEntry);				\
	(QueueHeader)->Tail = (PQUEUE_ENTRY)(QueueEntry);					\
	(QueueHeader)->Number++;											\
}

/* */
/*  Macros for flag and ref count operations */
/* */
#define RTMP_SET_FLAG(_M, _F)       ((_M)->Flags |= (_F))
#define RTMP_CLEAR_FLAG(_M, _F)     ((_M)->Flags &= ~(_F))
#define RTMP_CLEAR_FLAGS(_M)        ((_M)->Flags = 0)
#define RTMP_TEST_FLAG(_M, _F)      (((_M)->Flags & (_F)) != 0)
#define RTMP_TEST_FLAGS(_M, _F)     (((_M)->Flags & (_F)) == (_F))
/* Macro for power save flag. */
#define RTMP_SET_PSFLAG(_M, _F)       ((_M)->PSFlags |= (_F))
#define RTMP_CLEAR_PSFLAG(_M, _F)     ((_M)->PSFlags &= ~(_F))
#define RTMP_CLEAR_PSFLAGS(_M)        ((_M)->PSFlags = 0)
#define RTMP_TEST_PSFLAG(_M, _F)      (((_M)->PSFlags & (_F)) != 0)
#define RTMP_TEST_PSFLAGS(_M, _F)     (((_M)->PSFlags & (_F)) == (_F))

#define OPSTATUS_SET_FLAG(_pAd, _F)     ((_pAd)->CommonCfg.OpStatusFlags |= (_F))
#define OPSTATUS_CLEAR_FLAG(_pAd, _F)   ((_pAd)->CommonCfg.OpStatusFlags &= ~(_F))
#define OPSTATUS_TEST_FLAG(_pAd, _F)    (((_pAd)->CommonCfg.OpStatusFlags & (_F)) != 0)

#define CLIENT_STATUS_SET_FLAG(_pEntry,_F)      ((_pEntry)->ClientStatusFlags |= (_F))
#define CLIENT_STATUS_CLEAR_FLAG(_pEntry,_F)    ((_pEntry)->ClientStatusFlags &= ~(_F))
#define CLIENT_STATUS_TEST_FLAG(_pEntry,_F)     (((_pEntry)->ClientStatusFlags & (_F)) != 0)

#define RX_FILTER_SET_FLAG(_pAd, _F)    ((_pAd)->CommonCfg.PacketFilter |= (_F))
#define RX_FILTER_CLEAR_FLAG(_pAd, _F)  ((_pAd)->CommonCfg.PacketFilter &= ~(_F))
#define RX_FILTER_TEST_FLAG(_pAd, _F)   (((_pAd)->CommonCfg.PacketFilter & (_F)) != 0)

#define STA_NO_SECURITY_ON(_p)          (_p->StaCfg.WepStatus == Ndis802_11EncryptionDisabled)
#define STA_WEP_ON(_p)                  (_p->StaCfg.WepStatus == Ndis802_11Encryption1Enabled)
#define STA_TKIP_ON(_p)                 (_p->StaCfg.WepStatus == Ndis802_11Encryption2Enabled)
#define STA_AES_ON(_p)                  (_p->StaCfg.WepStatus == Ndis802_11Encryption3Enabled)

#define STA_TGN_WIFI_ON(_p)             (_p->StaCfg.bTGnWifiTest == TRUE)

#define CKIP_KP_ON(_p)				((((_p)->StaCfg.CkipFlag) & 0x10) && ((_p)->StaCfg.bCkipCmicOn == TRUE))
#define CKIP_CMIC_ON(_p)			((((_p)->StaCfg.CkipFlag) & 0x08) && ((_p)->StaCfg.bCkipCmicOn == TRUE))

#define INC_RING_INDEX(_idx, _RingSize)    \
{                                          \
    (_idx) = (_idx+1) % (_RingSize);       \
}

/* StaActive.SupportedHtPhy.MCSSet is copied from AP beacon.  Don't need to update here. */
#define COPY_HTSETTINGS_FROM_MLME_AUX_TO_ACTIVE_CFG(_pAd)                                 \
{                                                                                       \
	_pAd->StaActive.SupportedHtPhy.ChannelWidth = _pAd->MlmeAux.HtCapability.HtCapInfo.ChannelWidth;      \
	_pAd->StaActive.SupportedHtPhy.MimoPs = _pAd->MlmeAux.HtCapability.HtCapInfo.MimoPs;      \
	_pAd->StaActive.SupportedHtPhy.GF = _pAd->MlmeAux.HtCapability.HtCapInfo.GF;      \
	_pAd->StaActive.SupportedHtPhy.ShortGIfor20 = _pAd->MlmeAux.HtCapability.HtCapInfo.ShortGIfor20;      \
	_pAd->StaActive.SupportedHtPhy.ShortGIfor40 = _pAd->MlmeAux.HtCapability.HtCapInfo.ShortGIfor40;      \
	_pAd->StaActive.SupportedHtPhy.TxSTBC = _pAd->MlmeAux.HtCapability.HtCapInfo.TxSTBC;      \
	_pAd->StaActive.SupportedHtPhy.RxSTBC = _pAd->MlmeAux.HtCapability.HtCapInfo.RxSTBC;      \
	_pAd->StaActive.SupportedHtPhy.ExtChanOffset = _pAd->MlmeAux.AddHtInfo.AddHtInfo.ExtChanOffset;      \
	_pAd->StaActive.SupportedHtPhy.RecomWidth = _pAd->MlmeAux.AddHtInfo.AddHtInfo.RecomWidth;      \
	_pAd->StaActive.SupportedHtPhy.OperaionMode = _pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode;      \
	_pAd->StaActive.SupportedHtPhy.NonGfPresent = _pAd->MlmeAux.AddHtInfo.AddHtInfo2.NonGfPresent;      \
	NdisMoveMemory((_pAd)->MacTab.Content[BSSID_WCID].HTCapability.MCSSet, (_pAd)->StaActive.SupportedPhyInfo.MCSSet, sizeof(u8)* 16);\
}

#define COPY_AP_HTSETTINGS_FROM_BEACON(_pAd, _pHtCapability)                                 \
{                                                                                       \
	_pAd->MacTab.Content[BSSID_WCID].AMsduSize = (u8)(_pHtCapability->HtCapInfo.AMsduSize);	\
	_pAd->MacTab.Content[BSSID_WCID].MmpsMode= (u8)(_pHtCapability->HtCapInfo.MimoPs);	\
	_pAd->MacTab.Content[BSSID_WCID].MaxRAmpduFactor = (u8)(_pHtCapability->HtCapParm.MaxRAmpduFactor);	\
}

/* */
/* MACRO for 32-bit PCI register read / write */
/* */
/* Usage : RTMP_IO_READ32( */
/*              PRTMP_ADAPTER pAd, */
/*              unsigned long Register_Offset, */
/*              unsigned long * pValue) */
/* */
/*         RTMP_IO_WRITE32( */
/*              PRTMP_ADAPTER pAd, */
/*              unsigned long Register_Offset, */
/*              unsigned long Value) */
/* */

/* */
/* Common fragment list structure -  Identical to the scatter gather frag list structure */
/* */
/*#define RTMP_SCATTER_GATHER_ELEMENT         SCATTER_GATHER_ELEMENT */
/*#define PRTMP_SCATTER_GATHER_ELEMENT        PSCATTER_GATHER_ELEMENT */
#define NIC_MAX_PHYS_BUF_COUNT              8

typedef struct _RTMP_SCATTER_GATHER_ELEMENT {
	void *Address;
	unsigned long Length;
	unsigned long *Reserved;
} RTMP_SCATTER_GATHER_ELEMENT, *PRTMP_SCATTER_GATHER_ELEMENT;

typedef struct _RTMP_SCATTER_GATHER_LIST {
	unsigned long NumberOfElements;
	unsigned long *Reserved;
	RTMP_SCATTER_GATHER_ELEMENT Elements[NIC_MAX_PHYS_BUF_COUNT];
} RTMP_SCATTER_GATHER_LIST, *PRTMP_SCATTER_GATHER_LIST;

/* */
/*  Some utility macros */
/* */
#ifndef min
#define min(_a, _b)     (((_a) < (_b)) ? (_a) : (_b))
#endif

#ifndef max
#define max(_a, _b)     (((_a) > (_b)) ? (_a) : (_b))
#endif

#define GET_LNA_GAIN(_pAd)	((_pAd->LatchRfRegs.Channel <= 14) ? (_pAd->BLNAGain) : ((_pAd->LatchRfRegs.Channel <= 64) ? (_pAd->ALNAGain0) : ((_pAd->LatchRfRegs.Channel <= 128) ? (_pAd->ALNAGain1) : (_pAd->ALNAGain2))))

#define INC_COUNTER64(Val)          (Val.QuadPart++)

#define INFRA_ON(_p)                (OPSTATUS_TEST_FLAG(_p, fOP_STATUS_INFRA_ON))
#define ADHOC_ON(_p)                (OPSTATUS_TEST_FLAG(_p, fOP_STATUS_ADHOC_ON))
#define MONITOR_ON(_p)              (((_p)->StaCfg.BssType) == BSS_MONITOR)
#define IDLE_ON(_p)                 (!INFRA_ON(_p) && !ADHOC_ON(_p))

/* Check LEAP & CCKM flags */
#define LEAP_ON(_p)                 (((_p)->StaCfg.LeapAuthMode) == CISCO_AuthModeLEAP)
#define LEAP_CCKM_ON(_p)            ((((_p)->StaCfg.LeapAuthMode) == CISCO_AuthModeLEAP) && ((_p)->StaCfg.LeapAuthInfo.CCKM == TRUE))

/* if orginal Ethernet frame contains no LLC/SNAP, then an extra LLC/SNAP encap is required */
#define EXTRA_LLCSNAP_ENCAP_FROM_PKT_START(_pBufVA, _pExtraLlcSnapEncap)		\
{																\
	if (((*(_pBufVA + 12) << 8) + *(_pBufVA + 13)) > 1500)		\
	{															\
		_pExtraLlcSnapEncap = SNAP_802_1H;						\
		if (NdisEqualMemory(IPX, _pBufVA + 12, 2) || 			\
			NdisEqualMemory(APPLE_TALK, _pBufVA + 12, 2))		\
		{														\
			_pExtraLlcSnapEncap = SNAP_BRIDGE_TUNNEL;			\
		}														\
	}															\
	else														\
	{															\
		_pExtraLlcSnapEncap = NULL;								\
	}															\
}

/* New Define for new Tx Path. */
#define EXTRA_LLCSNAP_ENCAP_FROM_PKT_OFFSET(_pBufVA, _pExtraLlcSnapEncap)	\
{																\
	if (((*(_pBufVA) << 8) + *(_pBufVA + 1)) > 1500)			\
	{															\
		_pExtraLlcSnapEncap = SNAP_802_1H;						\
		if (NdisEqualMemory(IPX, _pBufVA, 2) || 				\
			NdisEqualMemory(APPLE_TALK, _pBufVA, 2))			\
		{														\
			_pExtraLlcSnapEncap = SNAP_BRIDGE_TUNNEL;			\
		}														\
	}															\
	else														\
	{															\
		_pExtraLlcSnapEncap = NULL;								\
	}															\
}

#define MAKE_802_3_HEADER(_p, _pMac1, _pMac2, _pType)                   \
{                                                                       \
    NdisMoveMemory(_p, _pMac1, MAC_ADDR_LEN);                           \
    NdisMoveMemory((_p + MAC_ADDR_LEN), _pMac2, MAC_ADDR_LEN);          \
    NdisMoveMemory((_p + MAC_ADDR_LEN * 2), _pType, LENGTH_802_3_TYPE); \
}

/* if pData has no LLC/SNAP (neither RFC1042 nor Bridge tunnel), keep it that way. */
/* else if the received frame is LLC/SNAP-encaped IPX or APPLETALK, preserve the LLC/SNAP field */
/* else remove the LLC/SNAP field from the result Ethernet frame */
/* Patch for WHQL only, which did not turn on Netbios but use IPX within its payload */
/* Note: */
/*     _pData & _DataSize may be altered (remove 8-byte LLC/SNAP) by this MACRO */
/*     _pRemovedLLCSNAP: pointer to removed LLC/SNAP; NULL is not removed */
#define CONVERT_TO_802_3(_p8023hdr, _pDA, _pSA, _pData, _DataSize, _pRemovedLLCSNAP)      \
{                                                                       \
    char LLC_Len[2];                                                    \
                                                                        \
    _pRemovedLLCSNAP = NULL;                                            \
    if (NdisEqualMemory(SNAP_802_1H, _pData, 6)  ||                     \
        NdisEqualMemory(SNAP_BRIDGE_TUNNEL, _pData, 6))                 \
    {                                                                   \
        u8 *pProto = _pData + 6;                                     \
                                                                        \
        if ((NdisEqualMemory(IPX, pProto, 2) || NdisEqualMemory(APPLE_TALK, pProto, 2)) &&  \
            NdisEqualMemory(SNAP_802_1H, _pData, 6))                    \
        {                                                               \
            LLC_Len[0] = (u8)(_DataSize / 256);                      \
            LLC_Len[1] = (u8)(_DataSize % 256);                      \
            MAKE_802_3_HEADER(_p8023hdr, _pDA, _pSA, LLC_Len);          \
        }                                                               \
        else                                                            \
        {                                                               \
            MAKE_802_3_HEADER(_p8023hdr, _pDA, _pSA, pProto);           \
            _pRemovedLLCSNAP = _pData;                                  \
            _DataSize -= LENGTH_802_1_H;                                \
            _pData += LENGTH_802_1_H;                                   \
        }                                                               \
    }                                                                   \
    else                                                                \
    {                                                                   \
        LLC_Len[0] = (u8)(_DataSize / 256);                          \
        LLC_Len[1] = (u8)(_DataSize % 256);                          \
        MAKE_802_3_HEADER(_p8023hdr, _pDA, _pSA, LLC_Len);              \
    }                                                                   \
}

/* Enqueue this frame to MLME engine */
/* We need to enqueue the whole frame because MLME need to pass data type */
/* information from 802.11 header */
#ifdef RTMP_MAC_PCI
#define REPORT_MGMT_FRAME_TO_MLME(_pAd, Wcid, _pFrame, _FrameSize, _Rssi0, _Rssi1, _Rssi2, _PlcpSignal)        \
{                                                                                       \
    u32 High32TSF, Low32TSF;                                                          \
    RTMP_IO_READ32(_pAd, TSF_TIMER_DW1, &High32TSF);                                       \
    RTMP_IO_READ32(_pAd, TSF_TIMER_DW0, &Low32TSF);                                        \
    MlmeEnqueueForRecv(_pAd, Wcid, High32TSF, Low32TSF, (u8)_Rssi0, (u8)_Rssi1,(u8)_Rssi2,_FrameSize, _pFrame, (u8)_PlcpSignal);   \
}
#endif /* RTMP_MAC_PCI // */
#ifdef RTMP_MAC_USB
#define REPORT_MGMT_FRAME_TO_MLME(_pAd, Wcid, _pFrame, _FrameSize, _Rssi0, _Rssi1, _Rssi2, _PlcpSignal)        \
{                                                                                       \
    u32 High32TSF=0, Low32TSF=0;                                                          \
    MlmeEnqueueForRecv(_pAd, Wcid, High32TSF, Low32TSF, (u8)_Rssi0, (u8)_Rssi1,(u8)_Rssi2,_FrameSize, _pFrame, (u8)_PlcpSignal);   \
}
#endif /* RTMP_MAC_USB // */

#define MAC_ADDR_EQUAL(pAddr1,pAddr2)           RTMPEqualMemory((void *)(pAddr1), (void *)(pAddr2), MAC_ADDR_LEN)
#define SSID_EQUAL(ssid1, len1, ssid2, len2)    ((len1==len2) && (RTMPEqualMemory(ssid1, ssid2, len1)))

/* */
/* Check if it is Japan W53(ch52,56,60,64) channel. */
/* */
#define JapanChannelCheck(channel)  ((channel == 52) || (channel == 56) || (channel == 60) || (channel == 64))

#define STA_EXTRA_SETTING(_pAd)

#define STA_PORT_SECURED(_pAd) \
{ \
	BOOLEAN	Cancelled; \
	(_pAd)->StaCfg.PortSecured = WPA_802_1X_PORT_SECURED; \
	NdisAcquireSpinLock(&((_pAd)->MacTabLock)); \
	(_pAd)->MacTab.Content[BSSID_WCID].PortSecured = (_pAd)->StaCfg.PortSecured; \
	(_pAd)->MacTab.Content[BSSID_WCID].PrivacyFilter = Ndis802_11PrivFilterAcceptAll;\
	NdisReleaseSpinLock(&(_pAd)->MacTabLock); \
	RTMPCancelTimer(&((_pAd)->Mlme.LinkDownTimer), &Cancelled);\
	STA_EXTRA_SETTING(_pAd); \
}

/* */
/*  Data buffer for DMA operation, the buffer must be contiguous physical memory */
/*  Both DMA to / from CPU use the same structure. */
/* */
typedef struct _RTMP_DMABUF {
	unsigned long AllocSize;
	void *AllocVa;		/* TxBuf virtual address */
	NDIS_PHYSICAL_ADDRESS AllocPa;	/* TxBuf physical address */
} RTMP_DMABUF, *PRTMP_DMABUF;

/* */
/* Control block (Descriptor) for all ring descriptor DMA operation, buffer must be */
/* contiguous physical memory. NDIS_PACKET stored the binding Rx packet descriptor */
/* which won't be released, driver has to wait until upper layer return the packet */
/* before giveing up this rx ring descriptor to ASIC. NDIS_BUFFER is assocaited pair */
/* to describe the packet buffer. For Tx, NDIS_PACKET stored the tx packet descriptor */
/* which driver should ACK upper layer when the tx is physically done or failed. */
/* */
typedef struct _RTMP_DMACB {
	unsigned long AllocSize;	/* Control block size */
	void *AllocVa;		/* Control block virtual address */
	NDIS_PHYSICAL_ADDRESS AllocPa;	/* Control block physical address */
	PNDIS_PACKET pNdisPacket;
	PNDIS_PACKET pNextNdisPacket;

	RTMP_DMABUF DmaBuf;	/* Associated DMA buffer structure */
} RTMP_DMACB, *PRTMP_DMACB;

typedef struct _RTMP_TX_RING {
	RTMP_DMACB Cell[TX_RING_SIZE];
	u32 TxCpuIdx;
	u32 TxDmaIdx;
	u32 TxSwFreeIdx;	/* software next free tx index */
} RTMP_TX_RING, *PRTMP_TX_RING;

typedef struct _RTMP_RX_RING {
	RTMP_DMACB Cell[RX_RING_SIZE];
	u32 RxCpuIdx;
	u32 RxDmaIdx;
	int RxSwReadIdx;	/* software next read index */
} RTMP_RX_RING, *PRTMP_RX_RING;

typedef struct _RTMP_MGMT_RING {
	RTMP_DMACB Cell[MGMT_RING_SIZE];
	u32 TxCpuIdx;
	u32 TxDmaIdx;
	u32 TxSwFreeIdx;	/* software next free tx index */
} RTMP_MGMT_RING, *PRTMP_MGMT_RING;

/* */
/*  Statistic counter structure */
/* */
typedef struct _COUNTER_802_3 {
	/* General Stats */
	unsigned long GoodTransmits;
	unsigned long GoodReceives;
	unsigned long TxErrors;
	unsigned long RxErrors;
	unsigned long RxNoBuffer;

	/* Ethernet Stats */
	unsigned long RcvAlignmentErrors;
	unsigned long OneCollision;
	unsigned long MoreCollisions;

} COUNTER_802_3, *PCOUNTER_802_3;

typedef struct _COUNTER_802_11 {
	unsigned long Length;
	LARGE_INTEGER LastTransmittedFragmentCount;
	LARGE_INTEGER TransmittedFragmentCount;
	LARGE_INTEGER MulticastTransmittedFrameCount;
	LARGE_INTEGER FailedCount;
	LARGE_INTEGER RetryCount;
	LARGE_INTEGER MultipleRetryCount;
	LARGE_INTEGER RTSSuccessCount;
	LARGE_INTEGER RTSFailureCount;
	LARGE_INTEGER ACKFailureCount;
	LARGE_INTEGER FrameDuplicateCount;
	LARGE_INTEGER ReceivedFragmentCount;
	LARGE_INTEGER MulticastReceivedFrameCount;
	LARGE_INTEGER FCSErrorCount;
} COUNTER_802_11, *PCOUNTER_802_11;

typedef struct _COUNTER_RALINK {
	unsigned long TransmittedByteCount;	/* both successful and failure, used to calculate TX throughput */
	unsigned long ReceivedByteCount;	/* both CRC okay and CRC error, used to calculate RX throughput */
	unsigned long BeenDisassociatedCount;
	unsigned long BadCQIAutoRecoveryCount;
	unsigned long PoorCQIRoamingCount;
	unsigned long MgmtRingFullCount;
	unsigned long RxCountSinceLastNULL;
	unsigned long RxCount;
	unsigned long RxRingErrCount;
	unsigned long KickTxCount;
	unsigned long TxRingErrCount;
	LARGE_INTEGER RealFcsErrCount;
	unsigned long PendingNdisPacketCount;

	unsigned long OneSecOsTxCount[NUM_OF_TX_RING];
	unsigned long OneSecDmaDoneCount[NUM_OF_TX_RING];
	u32 OneSecTxDoneCount;
	unsigned long OneSecRxCount;
	u32 OneSecTxAggregationCount;
	u32 OneSecRxAggregationCount;
	u32 OneSecReceivedByteCount;
	u32 OneSecFrameDuplicateCount;

	u32 OneSecTransmittedByteCount;	/* both successful and failure, used to calculate TX throughput */
	u32 OneSecTxNoRetryOkCount;
	u32 OneSecTxRetryOkCount;
	u32 OneSecTxFailCount;
	u32 OneSecFalseCCACnt;	/* CCA error count, for debug purpose, might move to global counter */
	u32 OneSecRxOkCnt;	/* RX without error */
	u32 OneSecRxOkDataCnt;	/* unicast-to-me DATA frame count */
	u32 OneSecRxFcsErrCnt;	/* CRC error */
	u32 OneSecBeaconSentCnt;
	u32 LastOneSecTotalTxCount;	/* OneSecTxNoRetryOkCount + OneSecTxRetryOkCount + OneSecTxFailCount */
	u32 LastOneSecRxOkDataCnt;	/* OneSecRxOkDataCnt */
	unsigned long DuplicateRcv;
	unsigned long TxAggCount;
	unsigned long TxNonAggCount;
	unsigned long TxAgg1MPDUCount;
	unsigned long TxAgg2MPDUCount;
	unsigned long TxAgg3MPDUCount;
	unsigned long TxAgg4MPDUCount;
	unsigned long TxAgg5MPDUCount;
	unsigned long TxAgg6MPDUCount;
	unsigned long TxAgg7MPDUCount;
	unsigned long TxAgg8MPDUCount;
	unsigned long TxAgg9MPDUCount;
	unsigned long TxAgg10MPDUCount;
	unsigned long TxAgg11MPDUCount;
	unsigned long TxAgg12MPDUCount;
	unsigned long TxAgg13MPDUCount;
	unsigned long TxAgg14MPDUCount;
	unsigned long TxAgg15MPDUCount;
	unsigned long TxAgg16MPDUCount;

	LARGE_INTEGER TransmittedOctetsInAMSDU;
	LARGE_INTEGER TransmittedAMSDUCount;
	LARGE_INTEGER ReceivedOctesInAMSDUCount;
	LARGE_INTEGER ReceivedAMSDUCount;
	LARGE_INTEGER TransmittedAMPDUCount;
	LARGE_INTEGER TransmittedMPDUsInAMPDUCount;
	LARGE_INTEGER TransmittedOctetsInAMPDUCount;
	LARGE_INTEGER MPDUInReceivedAMPDUCount;
} COUNTER_RALINK, *PCOUNTER_RALINK;

typedef struct _COUNTER_DRS {
	/* to record the each TX rate's quality. 0 is best, the bigger the worse. */
	u16 TxQuality[MAX_STEP_OF_TX_RATE_SWITCH];
	u8 PER[MAX_STEP_OF_TX_RATE_SWITCH];
	u8 TxRateUpPenalty;	/* extra # of second penalty due to last unstable condition */
	unsigned long CurrTxRateStableTime;	/* # of second in current TX rate */
	BOOLEAN fNoisyEnvironment;
	BOOLEAN fLastSecAccordingRSSI;
	u8 LastSecTxRateChangeAction;	/* 0: no change, 1:rate UP, 2:rate down */
	u8 LastTimeTxRateChangeAction;	/*Keep last time value of LastSecTxRateChangeAction */
	unsigned long LastTxOkCount;
} COUNTER_DRS, *PCOUNTER_DRS;

/***************************************************************************
  *	security key related data structure
  **************************************************************************/
typedef struct _CIPHER_KEY {
	u8 Key[16];		/* right now we implement 4 keys, 128 bits max */
	u8 RxMic[8];		/* make alignment */
	u8 TxMic[8];
	u8 TxTsc[6];		/* 48bit TSC value */
	u8 RxTsc[6];		/* 48bit TSC value */
	u8 CipherAlg;	/* 0-none, 1:WEP64, 2:WEP128, 3:TKIP, 4:AES, 5:CKIP64, 6:CKIP128 */
	u8 KeyLen;
	u8 BssId[6];
	/* Key length for each key, 0: entry is invalid */
	u8 Type;		/* Indicate Pairwise/Group when reporting MIC error */
} CIPHER_KEY, *PCIPHER_KEY;

/* structure to define WPA Group Key Rekey Interval */
typedef struct PACKED _RT_802_11_WPA_REKEY {
	unsigned long ReKeyMethod;	/* mechanism for rekeying: 0:disable, 1: time-based, 2: packet-based */
	unsigned long ReKeyInterval;	/* time-based: seconds, packet-based: kilo-packets */
} RT_WPA_REKEY, *PRT_WPA_REKEY, RT_802_11_WPA_REKEY, *PRT_802_11_WPA_REKEY;

#ifdef RTMP_MAC_USB
/***************************************************************************
  *	RTUSB I/O related data structure
  **************************************************************************/
typedef struct _RT_SET_ASIC_WCID {
	unsigned long WCID;		/* mechanism for rekeying: 0:disable, 1: time-based, 2: packet-based */
	unsigned long SetTid;		/* time-based: seconds, packet-based: kilo-packets */
	unsigned long DeleteTid;	/* time-based: seconds, packet-based: kilo-packets */
	u8 Addr[MAC_ADDR_LEN];	/* avoid in interrupt when write key */
} RT_SET_ASIC_WCID, *PRT_SET_ASIC_WCID;

typedef struct _RT_SET_ASIC_WCID_ATTRI {
	unsigned long WCID;		/* mechanism for rekeying: 0:disable, 1: time-based, 2: packet-based */
	unsigned long Cipher;		/* ASIC Cipher definition */
	u8 Addr[ETH_LENGTH_OF_ADDRESS];
} RT_SET_ASIC_WCID_ATTRI, *PRT_SET_ASIC_WCID_ATTRI;

/* for USB interface, avoid in interrupt when write key */
typedef struct RT_ADD_PAIRWISE_KEY_ENTRY {
	u8 MacAddr[6];
	u16 MacTabMatchWCID;	/* ASIC */
	CIPHER_KEY CipherKey;
} RT_ADD_PAIRWISE_KEY_ENTRY, *PRT_ADD_PAIRWISE_KEY_ENTRY;

/* Cipher suite type for mixed mode group cipher, P802.11i-2004 */
typedef enum _RT_802_11_CIPHER_SUITE_TYPE {
	Cipher_Type_NONE,
	Cipher_Type_WEP40,
	Cipher_Type_TKIP,
	Cipher_Type_RSVD,
	Cipher_Type_CCMP,
	Cipher_Type_WEP104
} RT_802_11_CIPHER_SUITE_TYPE, *PRT_802_11_CIPHER_SUITE_TYPE;
#endif /* RTMP_MAC_USB // */

typedef struct {
	u8 Addr[MAC_ADDR_LEN];
	u8 ErrorCode[2];	/*00 01-Invalid authentication type */
	/*00 02-Authentication timeout */
	/*00 03-Challenge from AP failed */
	/*00 04-Challenge to AP failed */
	BOOLEAN Reported;
} ROGUEAP_ENTRY, *PROGUEAP_ENTRY;

typedef struct {
	u8 RogueApNr;
	ROGUEAP_ENTRY RogueApEntry[MAX_LEN_OF_BSS_TABLE];
} ROGUEAP_TABLE, *PROGUEAP_TABLE;

/* */
/* Cisco IAPP format */
/* */
typedef struct _CISCO_IAPP_CONTENT_ {
	u16 Length;		/*IAPP Length */
	u8 MessageType;	/*IAPP type */
	u8 FunctionCode;	/*IAPP function type */
	u8 DestinaionMAC[MAC_ADDR_LEN];
	u8 SourceMAC[MAC_ADDR_LEN];
	u16 Tag;		/*Tag(element IE) - Adjacent AP report */
	u16 TagLength;	/*Length of element not including 4 byte header */
	u8 OUI[4];		/*0x00, 0x40, 0x96, 0x00 */
	u8 PreviousAP[MAC_ADDR_LEN];	/*MAC Address of access point */
	u16 Channel;
	u16 SsidLen;
	u8 Ssid[MAX_LEN_OF_SSID];
	u16 Seconds;		/*Seconds that the client has been disassociated. */
} CISCO_IAPP_CONTENT, *PCISCO_IAPP_CONTENT;

/*
  *	Fragment Frame structure
  */
typedef struct _FRAGMENT_FRAME {
	PNDIS_PACKET pFragPacket;
	unsigned long RxSize;
	u16 Sequence;
	u16 LastFrag;
	unsigned long Flags;		/* Some extra frame information. bit 0: LLC presented */
} FRAGMENT_FRAME, *PFRAGMENT_FRAME;

/* */
/* Packet information for NdisQueryPacket */
/* */
typedef struct _PACKET_INFO {
	u32 PhysicalBufferCount;	/* Physical breaks of buffer descripor chained */
	u32 BufferCount;	/* Number of Buffer descriptor chained */
	u32 TotalPacketLength;	/* Self explained */
	PNDIS_BUFFER pFirstBuffer;	/* Pointer to first buffer descriptor */
} PACKET_INFO, *PPACKET_INFO;

/* */
/*  Arcfour Structure Added by PaulWu */
/* */
typedef struct _ARCFOUR {
	u32 X;
	u32 Y;
	u8 STATE[256];
} ARCFOURCONTEXT, *PARCFOURCONTEXT;

/* */
/* Tkip Key structure which RC4 key & MIC calculation */
/* */
typedef struct _TKIP_KEY_INFO {
	u32 nBytesInM;		/* # bytes in M for MICKEY */
	unsigned long IV16;
	unsigned long IV32;
	unsigned long K0;		/* for MICKEY Low */
	unsigned long K1;		/* for MICKEY Hig */
	unsigned long L;		/* Current state for MICKEY */
	unsigned long R;		/* Current state for MICKEY */
	unsigned long M;		/* Message accumulator for MICKEY */
	u8 RC4KEY[16];
	u8 MIC[8];
} TKIP_KEY_INFO, *PTKIP_KEY_INFO;

/* */
/* Private / Misc data, counters for driver internal use */
/* */
typedef struct __PRIVATE_STRUC {
	u32 SystemResetCnt;	/* System reset counter */
	u32 TxRingFullCnt;	/* Tx ring full occurrance number */
	u32 PhyRxErrCnt;	/* PHY Rx error count, for debug purpose, might move to global counter */
	/* Variables for WEP encryption / decryption in rtmp_wep.c */
	u32 FCSCRC32;
	ARCFOURCONTEXT WEPCONTEXT;
	/* Tkip stuff */
	TKIP_KEY_INFO Tx;
	TKIP_KEY_INFO Rx;
} PRIVATE_STRUC, *PPRIVATE_STRUC;

/***************************************************************************
  *	Channel and BBP related data structures
  **************************************************************************/
/* structure to tune BBP R66 (BBP TUNING) */
typedef struct _BBP_R66_TUNING {
	BOOLEAN bEnable;
	u16 FalseCcaLowerThreshold;	/* default 100 */
	u16 FalseCcaUpperThreshold;	/* default 512 */
	u8 R66Delta;
	u8 R66CurrentValue;
	BOOLEAN R66LowerUpperSelect;	/*Before LinkUp, Used LowerBound or UpperBound as R66 value. */
} BBP_R66_TUNING, *PBBP_R66_TUNING;

/* structure to store channel TX power */
typedef struct _CHANNEL_TX_POWER {
	u16 RemainingTimeForUse;	/*unit: sec */
	u8 Channel;
	char Power;
	char Power2;
	u8 MaxTxPwr;
	u8 DfsReq;
} CHANNEL_TX_POWER, *PCHANNEL_TX_POWER;

/* structure to store 802.11j channel TX power */
typedef struct _CHANNEL_11J_TX_POWER {
	u8 Channel;
	u8 BW;		/* BW_10 or BW_20 */
	char Power;
	char Power2;
	u16 RemainingTimeForUse;	/*unit: sec */
} CHANNEL_11J_TX_POWER, *PCHANNEL_11J_TX_POWER;

typedef struct _SOFT_RX_ANT_DIVERSITY_STRUCT {
	u8 EvaluatePeriod;	/* 0:not evalute status, 1: evaluate status, 2: switching status */
	u8 EvaluateStableCnt;
	u8 Pair1PrimaryRxAnt;	/* 0:Ant-E1, 1:Ant-E2 */
	u8 Pair1SecondaryRxAnt;	/* 0:Ant-E1, 1:Ant-E2 */
	u8 Pair2PrimaryRxAnt;	/* 0:Ant-E3, 1:Ant-E4 */
	u8 Pair2SecondaryRxAnt;	/* 0:Ant-E3, 1:Ant-E4 */
	short Pair1AvgRssi[2];	/* AvgRssi[0]:E1, AvgRssi[1]:E2 */
	short Pair2AvgRssi[2];	/* AvgRssi[0]:E3, AvgRssi[1]:E4 */
	short Pair1LastAvgRssi;	/* */
	short Pair2LastAvgRssi;	/* */
	unsigned long RcvPktNumWhenEvaluate;
	BOOLEAN FirstPktArrivedWhenEvaluate;
	RALINK_TIMER_STRUCT RxAntDiversityTimer;
} SOFT_RX_ANT_DIVERSITY, *PSOFT_RX_ANT_DIVERSITY;

/***************************************************************************
  *	structure for radar detection and channel switch
  **************************************************************************/
typedef struct _RADAR_DETECT_STRUCT {
	/*BOOLEAN           IEEE80211H;                     // 0: disable, 1: enable IEEE802.11h */
	u8 CSCount;		/*Channel switch counter */
	u8 CSPeriod;		/*Channel switch period (beacon count) */
	u8 RDCount;		/*Radar detection counter */
	u8 RDMode;		/*Radar Detection mode */
	u8 RDDurRegion;	/*Radar detection duration region */
	u8 BBPR16;
	u8 BBPR17;
	u8 BBPR18;
	u8 BBPR21;
	u8 BBPR22;
	u8 BBPR64;
	unsigned long InServiceMonitorCount;	/* unit: sec */
	u8 DfsSessionTime;
	BOOLEAN bFastDfs;
	u8 ChMovingTime;
	u8 LongPulseRadarTh;
} RADAR_DETECT_STRUCT, *PRADAR_DETECT_STRUCT;

typedef enum _ABGBAND_STATE_ {
	UNKNOWN_BAND,
	BG_BAND,
	A_BAND,
} ABGBAND_STATE;

#ifdef RTMP_MAC_PCI
/* Power save method control */
typedef union _PS_CONTROL {
	struct {
		unsigned long EnablePSinIdle:1;	/* Enable radio off when not connect to AP. radio on only when sitesurvey, */
		unsigned long EnableNewPS:1;	/* Enable new  Chip power save fucntion . New method can only be applied in chip version after 2872. and PCIe. */
		unsigned long rt30xxPowerMode:2;	/* Power Level Mode for rt30xx chip */
		unsigned long rt30xxFollowHostASPM:1;	/* Card Follows Host's setting for rt30xx chip. */
		unsigned long rt30xxForceASPMTest:1;	/* Force enable L1 for rt30xx chip. This has higher priority than rt30xxFollowHostASPM Mode. */
		unsigned long rsv:26;	/* Radio Measurement Enable */
	} field;
	unsigned long word;
} PS_CONTROL, *PPS_CONTROL;
#endif /* RTMP_MAC_PCI // */

/***************************************************************************
  *	structure for MLME state machine
  **************************************************************************/
typedef struct _MLME_STRUCT {
	/* STA state machines */
	STATE_MACHINE CntlMachine;
	STATE_MACHINE AssocMachine;
	STATE_MACHINE AuthMachine;
	STATE_MACHINE AuthRspMachine;
	STATE_MACHINE SyncMachine;
	STATE_MACHINE WpaPskMachine;
	STATE_MACHINE LeapMachine;
	STATE_MACHINE_FUNC AssocFunc[ASSOC_FUNC_SIZE];
	STATE_MACHINE_FUNC AuthFunc[AUTH_FUNC_SIZE];
	STATE_MACHINE_FUNC AuthRspFunc[AUTH_RSP_FUNC_SIZE];
	STATE_MACHINE_FUNC SyncFunc[SYNC_FUNC_SIZE];
	STATE_MACHINE_FUNC ActFunc[ACT_FUNC_SIZE];
	/* Action */
	STATE_MACHINE ActMachine;

	/* common WPA state machine */
	STATE_MACHINE WpaMachine;
	STATE_MACHINE_FUNC WpaFunc[WPA_FUNC_SIZE];

	unsigned long ChannelQuality;	/* 0..100, Channel Quality Indication for Roaming */
	unsigned long Now32;		/* latch the value of NdisGetSystemUpTime() */
	unsigned long LastSendNULLpsmTime;

	BOOLEAN bRunning;
	NDIS_SPIN_LOCK TaskLock;
	MLME_QUEUE Queue;

	u32 ShiftReg;

	RALINK_TIMER_STRUCT PeriodicTimer;
	RALINK_TIMER_STRUCT APSDPeriodicTimer;
	RALINK_TIMER_STRUCT LinkDownTimer;
	RALINK_TIMER_STRUCT LinkUpTimer;
#ifdef RTMP_MAC_PCI
	u8 bPsPollTimerRunning;
	RALINK_TIMER_STRUCT PsPollTimer;
	RALINK_TIMER_STRUCT RadioOnOffTimer;
#endif				/* RTMP_MAC_PCI // */
	unsigned long PeriodicRound;
	unsigned long OneSecPeriodicRound;

	u8 RealRxPath;
	BOOLEAN bLowThroughput;
	BOOLEAN bEnableAutoAntennaCheck;
	RALINK_TIMER_STRUCT RxAntEvalTimer;

#ifdef RT30xx
	u8 CaliBW40RfR24;
	u8 CaliBW20RfR24;
#endif				/* RT30xx // */

#ifdef RTMP_MAC_USB
	RALINK_TIMER_STRUCT AutoWakeupTimer;
	BOOLEAN AutoWakeupTimerRunning;
#endif				/* RTMP_MAC_USB // */
} MLME_STRUCT, *PMLME_STRUCT;

/***************************************************************************
  *	802.11 N related data structures
  **************************************************************************/
struct reordering_mpdu {
	struct reordering_mpdu *next;
	PNDIS_PACKET pPacket;	/* coverted to 802.3 frame */
	int Sequence;		/* sequence number of MPDU */
	BOOLEAN bAMSDU;
};

struct reordering_list {
	struct reordering_mpdu *next;
	int qlen;
};

struct reordering_mpdu_pool {
	void *mem;
	NDIS_SPIN_LOCK lock;
	struct reordering_list freelist;
};

typedef enum _REC_BLOCKACK_STATUS {
	Recipient_NONE = 0,
	Recipient_USED,
	Recipient_HandleRes,
	Recipient_Accept
} REC_BLOCKACK_STATUS, *PREC_BLOCKACK_STATUS;

typedef enum _ORI_BLOCKACK_STATUS {
	Originator_NONE = 0,
	Originator_USED,
	Originator_WaitRes,
	Originator_Done
} ORI_BLOCKACK_STATUS, *PORI_BLOCKACK_STATUS;

typedef struct _BA_ORI_ENTRY {
	u8 Wcid;
	u8 TID;
	u8 BAWinSize;
	u8 Token;
/* Sequence is to fill every outgoing QoS DATA frame's sequence field in 802.11 header. */
	u16 Sequence;
	u16 TimeOutValue;
	ORI_BLOCKACK_STATUS ORI_BA_Status;
	RALINK_TIMER_STRUCT ORIBATimer;
	void *pAdapter;
} BA_ORI_ENTRY, *PBA_ORI_ENTRY;

typedef struct _BA_REC_ENTRY {
	u8 Wcid;
	u8 TID;
	u8 BAWinSize;	/* 7.3.1.14. each buffer is capable of holding a max AMSDU or MSDU. */
	/*u8 NumOfRxPkt; */
	/*u8    Curindidx; // the head in the RX reordering buffer */
	u16 LastIndSeq;
/*      u16          LastIndSeqAtTimer; */
	u16 TimeOutValue;
	RALINK_TIMER_STRUCT RECBATimer;
	unsigned long LastIndSeqAtTimer;
	unsigned long nDropPacket;
	unsigned long rcvSeq;
	REC_BLOCKACK_STATUS REC_BA_Status;
/*      u8   RxBufIdxUsed; */
	/* corresponding virtual address for RX reordering packet storage. */
	/*RTMP_REORDERDMABUF MAP_RXBuf[MAX_RX_REORDERBUF]; */
	NDIS_SPIN_LOCK RxReRingLock;	/* Rx Ring spinlock */
/*      struct _BA_REC_ENTRY *pNext; */
	void *pAdapter;
	struct reordering_list list;
} BA_REC_ENTRY, *PBA_REC_ENTRY;

typedef struct {
	unsigned long numAsRecipient;	/* I am recipient of numAsRecipient clients. These client are in the BARecEntry[] */
	unsigned long numAsOriginator;	/* I am originator of   numAsOriginator clients. These clients are in the BAOriEntry[] */
	unsigned long numDoneOriginator;	/* count Done Originator sessions */
	BA_ORI_ENTRY BAOriEntry[MAX_LEN_OF_BA_ORI_TABLE];
	BA_REC_ENTRY BARecEntry[MAX_LEN_OF_BA_REC_TABLE];
} BA_TABLE, *PBA_TABLE;

/*For QureyBATableOID use; */
typedef struct PACKED _OID_BA_REC_ENTRY {
	u8 MACAddr[MAC_ADDR_LEN];
	u8 BaBitmap;		/* if (BaBitmap&(1<<TID)), this session with{MACAddr, TID}exists, so read BufSize[TID] for BufferSize */
	u8 rsv;
	u8 BufSize[8];
	REC_BLOCKACK_STATUS REC_BA_Status[8];
} OID_BA_REC_ENTRY, *POID_BA_REC_ENTRY;

/*For QureyBATableOID use; */
typedef struct PACKED _OID_BA_ORI_ENTRY {
	u8 MACAddr[MAC_ADDR_LEN];
	u8 BaBitmap;		/* if (BaBitmap&(1<<TID)), this session with{MACAddr, TID}exists, so read BufSize[TID] for BufferSize, read ORI_BA_Status[TID] for status */
	u8 rsv;
	u8 BufSize[8];
	ORI_BLOCKACK_STATUS ORI_BA_Status[8];
} OID_BA_ORI_ENTRY, *POID_BA_ORI_ENTRY;

typedef struct _QUERYBA_TABLE {
	OID_BA_ORI_ENTRY BAOriEntry[32];
	OID_BA_REC_ENTRY BARecEntry[32];
	u8 OriNum;		/* Number of below BAOriEntry */
	u8 RecNum;		/* Number of below BARecEntry */
} QUERYBA_TABLE, *PQUERYBA_TABLE;

typedef union _BACAP_STRUC {
	struct {
		u32 RxBAWinLimit:8;
		u32 TxBAWinLimit:8;
		u32 AutoBA:1;	/* automatically BA */
		u32 Policy:2;	/* 0: DELAY_BA 1:IMMED_BA  (//BA Policy subfiled value in ADDBA frame)   2:BA-not use */
		u32 MpduDensity:3;
		u32 AmsduEnable:1;	/*Enable AMSDU transmisstion */
		u32 AmsduSize:1;	/* 0:3839, 1:7935 bytes. u32  MSDUSizeToBytes[]        = { 3839, 7935}; */
		u32 MMPSmode:2;	/* MIMO power save more, 0:static, 1:dynamic, 2:rsv, 3:mimo enable */
		u32 bHtAdhoc:1;	/* adhoc can use ht rate. */
		u32 b2040CoexistScanSup:1;	/*As Sta, support do 2040 coexistence scan for AP. As Ap, support monitor trigger event to check if can use BW 40MHz. */
		u32 : 4;
	} field;
	u32 word;
} BACAP_STRUC, *PBACAP_STRUC;

typedef struct {
	BOOLEAN IsRecipient;
	u8 MACAddr[MAC_ADDR_LEN];
	u8 TID;
	u8 nMSDU;
	u16 TimeOut;
	BOOLEAN bAllTid;	/* If True, delete all TID for BA sessions with this MACaddr. */
} OID_ADD_BA_ENTRY, *POID_ADD_BA_ENTRY;

#define IS_HT_STA(_pMacEntry)	\
	(_pMacEntry->MaxHTPhyMode.field.MODE >= MODE_HTMIX)

#define IS_HT_RATE(_pMacEntry)	\
	(_pMacEntry->HTPhyMode.field.MODE >= MODE_HTMIX)

#define PEER_IS_HT_RATE(_pMacEntry)	\
	(_pMacEntry->HTPhyMode.field.MODE >= MODE_HTMIX)

/*This structure is for all 802.11n card InterOptibilityTest action. Reset all Num every n second.  (Details see MLMEPeriodic) */
typedef struct _IOT_STRUC {
	u8 Threshold[2];
	u8 ReorderTimeOutNum[MAX_LEN_OF_BA_REC_TABLE];	/* compare with threshold[0] */
	u8 RefreshNum[MAX_LEN_OF_BA_REC_TABLE];	/* compare with threshold[1] */
	unsigned long OneSecInWindowCount;
	unsigned long OneSecFrameDuplicateCount;
	unsigned long OneSecOutWindowCount;
	u8 DelOriAct;
	u8 DelRecAct;
	u8 RTSShortProt;
	u8 RTSLongProt;
	BOOLEAN bRTSLongProtOn;
	BOOLEAN bLastAtheros;
	BOOLEAN bCurrentAtheros;
	BOOLEAN bNowAtherosBurstOn;
	BOOLEAN bNextDisableRxBA;
	BOOLEAN bToggle;
} IOT_STRUC, *PIOT_STRUC;

/* This is the registry setting for 802.11n transmit setting.  Used in advanced page. */
typedef union _REG_TRANSMIT_SETTING {
	struct {
		/*u32  PhyMode:4; */
		/*u32  MCS:7;                 // MCS */
		u32 rsv0:10;
		u32 TxBF:1;
		u32 BW:1;	/*channel bandwidth 20MHz or 40 MHz */
		u32 ShortGI:1;
		u32 STBC:1;	/*SPACE */
		u32 TRANSNO:2;
		u32 HTMODE:1;
		u32 EXTCHA:2;
		u32 rsv:13;
	} field;
	u32 word;
} REG_TRANSMIT_SETTING, *PREG_TRANSMIT_SETTING;

typedef union _DESIRED_TRANSMIT_SETTING {
	struct {
		u16 MCS:7;	/* MCS */
		u16 PhyMode:4;
		u16 FixedTxMode:2;	/* If MCS isn't AUTO, fix rate in CCK, OFDM or HT mode. */
		u16 rsv:3;
	} field;
	u16 word;
} DESIRED_TRANSMIT_SETTING, *PDESIRED_TRANSMIT_SETTING;

#ifdef RTMP_MAC_USB
/***************************************************************************
  *	USB-based chip Beacon related data structures
  **************************************************************************/
#define BEACON_BITMAP_MASK		0xff
typedef struct _BEACON_SYNC_STRUCT_ {
	u8 BeaconBuf[HW_BEACON_MAX_COUNT][HW_BEACON_OFFSET];
	u8 BeaconTxWI[HW_BEACON_MAX_COUNT][TXWI_SIZE];
	unsigned long TimIELocationInBeacon[HW_BEACON_MAX_COUNT];
	unsigned long CapabilityInfoLocationInBeacon[HW_BEACON_MAX_COUNT];
	BOOLEAN EnableBeacon;	/* trigger to enable beacon transmission. */
	u8 BeaconBitMap;	/* NOTE: If the MAX_MBSSID_NUM is larger than 8, this parameter need to change. */
	u8 DtimBitOn;	/* NOTE: If the MAX_MBSSID_NUM is larger than 8, this parameter need to change. */
} BEACON_SYNC_STRUCT;
#endif /* RTMP_MAC_USB // */

/***************************************************************************
  *	Multiple SSID related data structures
  **************************************************************************/
#define WLAN_MAX_NUM_OF_TIM			((MAX_LEN_OF_MAC_TABLE >> 3) + 1)	/* /8 + 1 */
#define WLAN_CT_TIM_BCMC_OFFSET		0	/* unit: 32B */

/* clear bcmc TIM bit */
#define WLAN_MR_TIM_BCMC_CLEAR(apidx) \
	pAd->ApCfg.MBSSID[apidx].TimBitmaps[WLAN_CT_TIM_BCMC_OFFSET] &= ~BIT8[0];

/* set bcmc TIM bit */
#define WLAN_MR_TIM_BCMC_SET(apidx) \
	pAd->ApCfg.MBSSID[apidx].TimBitmaps[WLAN_CT_TIM_BCMC_OFFSET] |= BIT8[0];

/* clear a station PS TIM bit */
#define WLAN_MR_TIM_BIT_CLEAR(ad_p, apidx, wcid) \
	{	u8 tim_offset = wcid >> 3; \
		u8 bit_offset = wcid & 0x7; \
		ad_p->ApCfg.MBSSID[apidx].TimBitmaps[tim_offset] &= (~BIT8[bit_offset]); }

/* set a station PS TIM bit */
#define WLAN_MR_TIM_BIT_SET(ad_p, apidx, wcid) \
	{	u8 tim_offset = wcid >> 3; \
		u8 bit_offset = wcid & 0x7; \
		ad_p->ApCfg.MBSSID[apidx].TimBitmaps[tim_offset] |= BIT8[bit_offset]; }

/* configuration common to OPMODE_AP as well as OPMODE_STA */
typedef struct _COMMON_CONFIG {

	BOOLEAN bCountryFlag;
	u8 CountryCode[3];
	u8 Geography;
	u8 CountryRegion;	/* Enum of country region, 0:FCC, 1:IC, 2:ETSI, 3:SPAIN, 4:France, 5:MKK, 6:MKK1, 7:Israel */
	u8 CountryRegionForABand;	/* Enum of country region for A band */
	u8 PhyMode;		/* PHY_11A, PHY_11B, PHY_11BG_MIXED, PHY_ABG_MIXED */
	u16 Dsifs;		/* in units of usec */
	unsigned long PacketFilter;	/* Packet filter for receiving */
	u8 RegulatoryClass;

	char Ssid[MAX_LEN_OF_SSID];	/* NOT NULL-terminated */
	u8 SsidLen;		/* the actual ssid length in used */
	u8 LastSsidLen;	/* the actual ssid length in used */
	char LastSsid[MAX_LEN_OF_SSID];	/* NOT NULL-terminated */
	u8 LastBssid[MAC_ADDR_LEN];

	u8 Bssid[MAC_ADDR_LEN];
	u16 BeaconPeriod;
	u8 Channel;
	u8 CentralChannel;	/* Central Channel when using 40MHz is indicating. not real channel. */

	u8 SupRate[MAX_LEN_OF_SUPPORTED_RATES];
	u8 SupRateLen;
	u8 ExtRate[MAX_LEN_OF_SUPPORTED_RATES];
	u8 ExtRateLen;
	u8 DesireRate[MAX_LEN_OF_SUPPORTED_RATES];	/* OID_802_11_DESIRED_RATES */
	u8 MaxDesiredRate;
	u8 ExpectedACKRate[MAX_LEN_OF_SUPPORTED_RATES];

	unsigned long BasicRateBitmap;	/* backup basic ratebitmap */

	BOOLEAN bAPSDCapable;
	BOOLEAN bInServicePeriod;
	BOOLEAN bAPSDAC_BE;
	BOOLEAN bAPSDAC_BK;
	BOOLEAN bAPSDAC_VI;
	BOOLEAN bAPSDAC_VO;

	/* because TSPEC can modify the APSD flag, we need to keep the APSD flag
	   requested in association stage from the station;
	   we need to recover the APSD flag after the TSPEC is deleted. */
	BOOLEAN bACMAPSDBackup[4];	/* for delivery-enabled & trigger-enabled both */
	BOOLEAN bACMAPSDTr[4];	/* no use */

	BOOLEAN bNeedSendTriggerFrame;
	BOOLEAN bAPSDForcePowerSave;	/* Force power save mode, should only use in APSD-STAUT */
	unsigned long TriggerTimerCount;
	u8 MaxSPLength;
	u8 BBPCurrentBW;	/* BW_10,       BW_20, BW_40 */
	/* move to MULTISSID_STRUCT for MBSS */
	/*HTTRANSMIT_SETTING    HTPhyMode, MaxHTPhyMode, MinHTPhyMode;// For transmit phy setting in TXWI. */
	REG_TRANSMIT_SETTING RegTransmitSetting;	/*registry transmit setting. this is for reading registry setting only. not useful. */
	/*u8       FixedTxMode;              // Fixed Tx Mode (CCK, OFDM), for HT fixed tx mode (GF, MIX) , refer to RegTransmitSetting.field.HTMode */
	u8 TxRate;		/* Same value to fill in TXD. TxRate is 6-bit */
	u8 MaxTxRate;	/* RATE_1, RATE_2, RATE_5_5, RATE_11 */
	u8 TxRateIndex;	/* Tx rate index in RateSwitchTable */
	u8 TxRateTableSize;	/* Valid Tx rate table size in RateSwitchTable */
	/*BOOLEAN               bAutoTxRateSwitch; */
	u8 MinTxRate;	/* RATE_1, RATE_2, RATE_5_5, RATE_11 */
	u8 RtsRate;		/* RATE_xxx */
	HTTRANSMIT_SETTING MlmeTransmit;	/* MGMT frame PHY rate setting when operatin at Ht rate. */
	u8 MlmeRate;		/* RATE_xxx, used to send MLME frames */
	u8 BasicMlmeRate;	/* Default Rate for sending MLME frames */

	u16 RtsThreshold;	/* in unit of BYTE */
	u16 FragmentThreshold;	/* in unit of BYTE */

	u8 TxPower;		/* in unit of mW */
	unsigned long TxPowerPercentage;	/* 0~100 % */
	unsigned long TxPowerDefault;	/* keep for TxPowerPercentage */
	u8 PwrConstraint;

	BACAP_STRUC BACapability;	/*   NO USE = 0XFF  ;  IMMED_BA =1  ;  DELAY_BA=0 */
	BACAP_STRUC REGBACapability;	/*   NO USE = 0XFF  ;  IMMED_BA =1  ;  DELAY_BA=0 */

	IOT_STRUC IOTestParm;	/* 802.11n InterOpbility Test Parameter; */
	unsigned long TxPreamble;	/* Rt802_11PreambleLong, Rt802_11PreambleShort, Rt802_11PreambleAuto */
	BOOLEAN bUseZeroToDisableFragment;	/* Microsoft use 0 as disable */
	unsigned long UseBGProtection;	/* 0: auto, 1: always use, 2: always not use */
	BOOLEAN bUseShortSlotTime;	/* 0: disable, 1 - use short slot (9us) */
	BOOLEAN bEnableTxBurst;	/* 1: enble TX PACKET BURST (when BA is established or AP is not a legacy WMM AP), 0: disable TX PACKET BURST */
	BOOLEAN bAggregationCapable;	/* 1: enable TX aggregation when the peer supports it */
	BOOLEAN bPiggyBackCapable;	/* 1: enable TX piggy-back according MAC's version */
	BOOLEAN bIEEE80211H;	/* 1: enable IEEE802.11h spec. */
	unsigned long DisableOLBCDetect;	/* 0: enable OLBC detect; 1 disable OLBC detect */

	BOOLEAN bRdg;

	BOOLEAN bWmmCapable;	/* 0:disable WMM, 1:enable WMM */
	QOS_CAPABILITY_PARM APQosCapability;	/* QOS capability of the current associated AP */
	EDCA_PARM APEdcaParm;	/* EDCA parameters of the current associated AP */
	QBSS_LOAD_PARM APQbssLoad;	/* QBSS load of the current associated AP */
	u8 AckPolicy[4];	/* ACK policy of the specified AC. see ACK_xxx */
	BOOLEAN bDLSCapable;	/* 0:disable DLS, 1:enable DLS */
	/* a bitmap of BOOLEAN flags. each bit represent an operation status of a particular */
	/* BOOLEAN control, either ON or OFF. These flags should always be accessed via */
	/* OPSTATUS_TEST_FLAG(), OPSTATUS_SET_FLAG(), OP_STATUS_CLEAR_FLAG() macros. */
	/* see fOP_STATUS_xxx in RTMP_DEF.C for detail bit definition */
	unsigned long OpStatusFlags;

	BOOLEAN NdisRadioStateOff;	/*For HCT 12.0, set this flag to TRUE instead of called MlmeRadioOff. */
	ABGBAND_STATE BandState;	/* For setting BBP used on B/G or A mode. */

	/* IEEE802.11H--DFS. */
	RADAR_DETECT_STRUCT RadarDetect;

	/* HT */
	u8 BASize;		/* USer desired BAWindowSize. Should not exceed our max capability */
	/*RT_HT_CAPABILITY      SupportedHtPhy; */
	RT_HT_CAPABILITY DesiredHtPhy;
	HT_CAPABILITY_IE HtCapability;
	ADD_HT_INFO_IE AddHTInfo;	/* Useful as AP. */
	/*This IE is used with channel switch announcement element when changing to a new 40MHz. */
	/*This IE is included in channel switch ammouncement frames 7.4.1.5, beacons, probe Rsp. */
	NEW_EXT_CHAN_IE NewExtChanOffset;	/*7.3.2.20A, 1 if extension channel is above the control channel, 3 if below, 0 if not present */

	BOOLEAN bHTProtect;
	BOOLEAN bMIMOPSEnable;
	BOOLEAN bBADecline;
/*2008/11/05: KH add to support Antenna power-saving of AP<-- */
	BOOLEAN bGreenAPEnable;
/*2008/11/05: KH add to support Antenna power-saving of AP--> */
	BOOLEAN bDisableReordering;
	BOOLEAN bForty_Mhz_Intolerant;
	BOOLEAN bExtChannelSwitchAnnouncement;
	BOOLEAN bRcvBSSWidthTriggerEvents;
	unsigned long LastRcvBSSWidthTriggerEventsTime;

	u8 TxBASize;

	/* Enable wireless event */
	BOOLEAN bWirelessEvent;
	BOOLEAN bWiFiTest;	/* Enable this parameter for WiFi test */

	/* Tx & Rx Stream number selection */
	u8 TxStream;
	u8 RxStream;

	BOOLEAN bHardwareRadio;	/* Hardware controlled Radio enabled */

#ifdef RTMP_MAC_USB
	BOOLEAN bMultipleIRP;	/* Multiple Bulk IN flag */
	u8 NumOfBulkInIRP;	/* if bMultipleIRP == TRUE, NumOfBulkInIRP will be 4 otherwise be 1 */
	RT_HT_CAPABILITY SupportedHtPhy;
	unsigned long MaxPktOneTxBulk;
	u8 TxBulkFactor;
	u8 RxBulkFactor;

	BOOLEAN IsUpdateBeacon;
	BEACON_SYNC_STRUCT *pBeaconSync;
	RALINK_TIMER_STRUCT BeaconUpdateTimer;
	u32 BeaconAdjust;
	u32 BeaconFactor;
	u32 BeaconRemain;
#endif				/* RTMP_MAC_USB // */

	NDIS_SPIN_LOCK MeasureReqTabLock;
	PMEASURE_REQ_TAB pMeasureReqTab;

	NDIS_SPIN_LOCK TpcReqTabLock;
	PTPC_REQ_TAB pTpcReqTab;

	BOOLEAN PSPXlink;	/* 0: Disable. 1: Enable */

#if defined(RT305x)||defined(RT30xx)
	/* request by Gary, for High Power issue */
	u8 HighPowerPatchDisabled;
#endif

	BOOLEAN HT_DisallowTKIP;	/* Restrict the encryption type in 11n HT mode */
} COMMON_CONFIG, *PCOMMON_CONFIG;

/* Modified by Wu Xi-Kun 4/21/2006 */
/* STA configuration and status */
typedef struct _STA_ADMIN_CONFIG {
	/* GROUP 1 - */
	/*   User configuration loaded from Registry, E2PROM or OID_xxx. These settings describe */
	/*   the user intended configuration, but not necessary fully equal to the final */
	/*   settings in ACTIVE BSS after negotiation/compromize with the BSS holder (either */
	/*   AP or IBSS holder). */
	/*   Once initialized, user configuration can only be changed via OID_xxx */
	u8 BssType;		/* BSS_INFRA or BSS_ADHOC */
	u16 AtimWin;		/* used when starting a new IBSS */

	/* GROUP 2 - */
	/*   User configuration loaded from Registry, E2PROM or OID_xxx. These settings describe */
	/*   the user intended configuration, and should be always applied to the final */
	/*   settings in ACTIVE BSS without compromising with the BSS holder. */
	/*   Once initialized, user configuration can only be changed via OID_xxx */
	u8 RssiTrigger;
	u8 RssiTriggerMode;	/* RSSI_TRIGGERED_UPON_BELOW_THRESHOLD or RSSI_TRIGGERED_UPON_EXCCEED_THRESHOLD */
	u16 DefaultListenCount;	/* default listen count; */
	unsigned long WindowsPowerMode;	/* Power mode for AC power */
	unsigned long WindowsBatteryPowerMode;	/* Power mode for battery if exists */
	BOOLEAN bWindowsACCAMEnable;	/* Enable CAM power mode when AC on */
	BOOLEAN bAutoReconnect;	/* Set to TRUE when setting OID_802_11_SSID with no matching BSSID */
	unsigned long WindowsPowerProfile;	/* Windows power profile, for NDIS5.1 PnP */

	/* MIB:ieee802dot11.dot11smt(1).dot11StationConfigTable(1) */
	u16 Psm;		/* power management mode   (PWR_ACTIVE|PWR_SAVE) */
	u16 DisassocReason;
	u8 DisassocSta[MAC_ADDR_LEN];
	u16 DeauthReason;
	u8 DeauthSta[MAC_ADDR_LEN];
	u16 AuthFailReason;
	u8 AuthFailSta[MAC_ADDR_LEN];

	NDIS_802_11_PRIVACY_FILTER PrivacyFilter;	/* PrivacyFilter enum for 802.1X */
	NDIS_802_11_AUTHENTICATION_MODE AuthMode;	/* This should match to whatever microsoft defined */
	NDIS_802_11_WEP_STATUS WepStatus;
	NDIS_802_11_WEP_STATUS OrigWepStatus;	/* Original wep status set from OID */

	/* Add to support different cipher suite for WPA2/WPA mode */
	NDIS_802_11_ENCRYPTION_STATUS GroupCipher;	/* Multicast cipher suite */
	NDIS_802_11_ENCRYPTION_STATUS PairCipher;	/* Unicast cipher suite */
	BOOLEAN bMixCipher;	/* Indicate current Pair & Group use different cipher suites */
	u16 RsnCapability;

	NDIS_802_11_WEP_STATUS GroupKeyWepStatus;

	u8 WpaPassPhrase[64];	/* WPA PSK pass phrase */
	u32 WpaPassPhraseLen;	/* the length of WPA PSK pass phrase */
	u8 PMK[32];		/* WPA PSK mode PMK */
	u8 PTK[64];		/* WPA PSK mode PTK */
	u8 GTK[32];		/* GTK from authenticator */
	BSSID_INFO SavedPMK[PMKID_NO];
	u32 SavedPMKNum;	/* Saved PMKID number */

	u8 DefaultKeyId;

	/* WPA 802.1x port control, WPA_802_1X_PORT_SECURED, WPA_802_1X_PORT_NOT_SECURED */
	u8 PortSecured;

	/* For WPA countermeasures */
	unsigned long LastMicErrorTime;	/* record last MIC error time */
	unsigned long MicErrCnt;	/* Should be 0, 1, 2, then reset to zero (after disassoiciation). */
	BOOLEAN bBlockAssoc;	/* Block associate attempt for 60 seconds after counter measure occurred. */
	/* For WPA-PSK supplicant state */
	WPA_STATE WpaState;	/* Default is SS_NOTUSE and handled by microsoft 802.1x */
	u8 ReplayCounter[8];
	u8 ANonce[32];	/* ANonce for WPA-PSK from aurhenticator */
	u8 SNonce[32];	/* SNonce for WPA-PSK */

	u8 LastSNR0;		/* last received BEACON's SNR */
	u8 LastSNR1;		/* last received BEACON's SNR for 2nd  antenna */
	RSSI_SAMPLE RssiSample;
	unsigned long NumOfAvgRssiSample;

	unsigned long LastBeaconRxTime;	/* OS's timestamp of the last BEACON RX time */
	unsigned long Last11bBeaconRxTime;	/* OS's timestamp of the last 11B BEACON RX time */
	unsigned long Last11gBeaconRxTime;	/* OS's timestamp of the last 11G BEACON RX time */
	unsigned long Last20NBeaconRxTime;	/* OS's timestamp of the last 20MHz N BEACON RX time */

	unsigned long LastScanTime;	/* Record last scan time for issue BSSID_SCAN_LIST */
	unsigned long ScanCnt;		/* Scan counts since most recent SSID, BSSID, SCAN OID request */
	BOOLEAN bSwRadio;	/* Software controlled Radio On/Off, TRUE: On */
	BOOLEAN bHwRadio;	/* Hardware controlled Radio On/Off, TRUE: On */
	BOOLEAN bRadio;		/* Radio state, And of Sw & Hw radio state */
	BOOLEAN bHardwareRadio;	/* Hardware controlled Radio enabled */
	BOOLEAN bShowHiddenSSID;	/* Show all known SSID in SSID list get operation */

	/* New for WPA, windows want us to keep association information and */
	/* Fixed IEs from last association response */
	NDIS_802_11_ASSOCIATION_INFORMATION AssocInfo;
	u16 ReqVarIELen;	/* Length of next VIE include EID & Length */
	u8 ReqVarIEs[MAX_VIE_LEN];	/* The content saved here should be little-endian format. */
	u16 ResVarIELen;	/* Length of next VIE include EID & Length */
	u8 ResVarIEs[MAX_VIE_LEN];

	u8 RSNIE_Len;
	u8 RSN_IE[MAX_LEN_OF_RSNIE];	/* The content saved here should be little-endian format. */

	unsigned long CLBusyBytes;	/* Save the total bytes received durning channel load scan time */
	u16 RPIDensity[8];	/* Array for RPI density collection */

	u8 RMReqCnt;		/* Number of measurement request saved. */
	u8 CurrentRMReqIdx;	/* Number of measurement request saved. */
	BOOLEAN ParallelReq;	/* Parallel measurement, only one request performed, */
	/* It must be the same channel with maximum duration */
	u16 ParallelDuration;	/* Maximum duration for parallel measurement */
	u8 ParallelChannel;	/* Only one channel with parallel measurement */
	u16 IAPPToken;	/* IAPP dialog token */
	/* Hack for channel load and noise histogram parameters */
	u8 NHFactor;		/* Parameter for Noise histogram */
	u8 CLFactor;		/* Parameter for channel load */

	RALINK_TIMER_STRUCT StaQuickResponeForRateUpTimer;
	BOOLEAN StaQuickResponeForRateUpTimerRunning;

	u8 DtimCount;	/* 0.. DtimPeriod-1 */
	u8 DtimPeriod;	/* default = 3 */

	/*////////////////////////////////////////////////////////////////////////////////////// */
	/* This is only for WHQL test. */
	BOOLEAN WhqlTest;
	/*////////////////////////////////////////////////////////////////////////////////////// */

	RALINK_TIMER_STRUCT WpaDisassocAndBlockAssocTimer;
	/* Fast Roaming */
	BOOLEAN bAutoRoaming;	/* 0:disable auto roaming by RSSI, 1:enable auto roaming by RSSI */
	char dBmToRoam;		/* the condition to roam when receiving Rssi less than this value. It's negative value. */

	BOOLEAN IEEE8021X;
	BOOLEAN IEEE8021x_required_keys;
	CIPHER_KEY DesireSharedKey[4];	/* Record user desired WEP keys */
	u8 DesireSharedKeyId;

	/* 0: driver ignores wpa_supplicant */
	/* 1: wpa_supplicant initiates scanning and AP selection */
	/* 2: driver takes care of scanning, AP selection, and IEEE 802.11 association parameters */
	u8 WpaSupplicantUP;
	u8 WpaSupplicantScanCount;
	BOOLEAN bRSN_IE_FromWpaSupplicant;

	char dev_name[16];
	u16 OriDevType;

	BOOLEAN bTGnWifiTest;
	BOOLEAN bScanReqIsFromWebUI;

	HTTRANSMIT_SETTING HTPhyMode, MaxHTPhyMode, MinHTPhyMode;	/* For transmit phy setting in TXWI. */
	DESIRED_TRANSMIT_SETTING DesiredTransmitSetting;
	RT_HT_PHY_INFO DesiredHtPhyInfo;
	BOOLEAN bAutoTxRateSwitch;

#ifdef RTMP_MAC_PCI
	u8 BBPR3;
	/* PS Control has 2 meanings for advanced power save function. */
	/* 1. EnablePSinIdle : When no connection, always radio off except need to do site survey. */
	/* 2. EnableNewPS  : will save more current in sleep or radio off mode. */
	PS_CONTROL PSControl;
#endif				/* RTMP_MAC_PCI // */

	BOOLEAN bAutoConnectByBssid;
	unsigned long BeaconLostTime;	/* seconds */
	BOOLEAN bForceTxBurst;	/* 1: force enble TX PACKET BURST, 0: disable */
} STA_ADMIN_CONFIG, *PSTA_ADMIN_CONFIG;

/* This data structure keep the current active BSS/IBSS's configuration that this STA */
/* had agreed upon joining the network. Which means these parameters are usually decided */
/* by the BSS/IBSS creator instead of user configuration. Data in this data structurre */
/* is valid only when either ADHOC_ON(pAd) or INFRA_ON(pAd) is TRUE. */
/* Normally, after SCAN or failed roaming attempts, we need to recover back to */
/* the current active settings. */
typedef struct _STA_ACTIVE_CONFIG {
	u16 Aid;
	u16 AtimWin;		/* in kusec; IBSS parameter set element */
	u16 CapabilityInfo;
	u16 CfpMaxDuration;
	u16 CfpPeriod;

	/* Copy supported rate from desired AP's beacon. We are trying to match */
	/* AP's supported and extended rate settings. */
	u8 SupRate[MAX_LEN_OF_SUPPORTED_RATES];
	u8 ExtRate[MAX_LEN_OF_SUPPORTED_RATES];
	u8 SupRateLen;
	u8 ExtRateLen;
	/* Copy supported ht from desired AP's beacon. We are trying to match */
	RT_HT_PHY_INFO SupportedPhyInfo;
	RT_HT_CAPABILITY SupportedHtPhy;
} STA_ACTIVE_CONFIG, *PSTA_ACTIVE_CONFIG;

typedef struct _MAC_TABLE_ENTRY {
	/*Choose 1 from ValidAsWDS and ValidAsCLI  to validize. */
	BOOLEAN ValidAsCLI;	/* Sta mode, set this TRUE after Linkup,too. */
	BOOLEAN ValidAsWDS;	/* This is WDS Entry. only for AP mode. */
	BOOLEAN ValidAsApCli;	/*This is a AP-Client entry, only for AP mode which enable AP-Client functions. */
	BOOLEAN ValidAsMesh;
	BOOLEAN ValidAsDls;	/* This is DLS Entry. only for STA mode. */
	BOOLEAN isCached;
	BOOLEAN bIAmBadAtheros;	/* Flag if this is Atheros chip that has IOT problem.  We need to turn on RTS/CTS protection. */

	u8 EnqueueEapolStartTimerRunning;	/* Enqueue EAPoL-Start for triggering EAP SM */
	/*jan for wpa */
	/* record which entry revoke MIC Failure , if it leaves the BSS itself, AP won't update aMICFailTime MIB */
	u8 CMTimerRunning;
	u8 apidx;		/* MBSS number */
	u8 RSNIE_Len;
	u8 RSN_IE[MAX_LEN_OF_RSNIE];
	u8 ANonce[LEN_KEY_DESC_NONCE];
	u8 SNonce[LEN_KEY_DESC_NONCE];
	u8 R_Counter[LEN_KEY_DESC_REPLAY];
	u8 PTK[64];
	u8 ReTryCounter;
	RALINK_TIMER_STRUCT RetryTimer;
	RALINK_TIMER_STRUCT EnqueueStartForPSKTimer;	/* A timer which enqueue EAPoL-Start for triggering PSK SM */
	NDIS_802_11_AUTHENTICATION_MODE AuthMode;	/* This should match to whatever microsoft defined */
	NDIS_802_11_WEP_STATUS WepStatus;
	NDIS_802_11_WEP_STATUS GroupKeyWepStatus;
	AP_WPA_STATE WpaState;
	GTK_STATE GTKState;
	u16 PortSecured;
	NDIS_802_11_PRIVACY_FILTER PrivacyFilter;	/* PrivacyFilter enum for 802.1X */
	CIPHER_KEY PairwiseKey;
	void *pAd;
	int PMKID_CacheIdx;
	u8 PMKID[LEN_PMKID];

	u8 Addr[MAC_ADDR_LEN];
	u8 PsMode;
	SST Sst;
	AUTH_STATE AuthState;	/* for SHARED KEY authentication state machine used only */
	BOOLEAN IsReassocSta;	/* Indicate whether this is a reassociation procedure */
	u16 Aid;
	u16 CapabilityInfo;
	u8 LastRssi;
	unsigned long NoDataIdleCount;
	u16 StationKeepAliveCount;	/* unit: second */
	unsigned long PsQIdleCount;
	QUEUE_HEADER PsQueue;

	u32 StaConnectTime;	/* the live time of this station since associated with AP */

	BOOLEAN bSendBAR;
	u16 NoBADataCountDown;

	u32 CachedBuf[16];	/* u32 (4 bytes) for alignment */
	u32 TxBFCount;		/* 3*3 */
	u32 FIFOCount;
	u32 DebugFIFOCount;
	u32 DebugTxCount;
	BOOLEAN bDlsInit;

/*==================================================== */
/*WDS entry needs these */
/* if ValidAsWDS==TRUE, MatchWDSTabIdx is the index in WdsTab.MacTab */
	u32 MatchWDSTabIdx;
	u8 MaxSupportedRate;
	u8 CurrTxRate;
	u8 CurrTxRateIndex;
	/* to record the each TX rate's quality. 0 is best, the bigger the worse. */
	u16 TxQuality[MAX_STEP_OF_TX_RATE_SWITCH];
/*      u16          OneSecTxOkCount; */
	u32 OneSecTxNoRetryOkCount;
	u32 OneSecTxRetryOkCount;
	u32 OneSecTxFailCount;
	u32 ContinueTxFailCnt;
	u32 CurrTxRateStableTime;	/* # of second in current TX rate */
	u8 TxRateUpPenalty;	/* extra # of second penalty due to last unstable condition */
/*==================================================== */

	BOOLEAN fNoisyEnvironment;
	BOOLEAN fLastSecAccordingRSSI;
	u8 LastSecTxRateChangeAction;	/* 0: no change, 1:rate UP, 2:rate down */
	char LastTimeTxRateChangeAction;	/*Keep last time value of LastSecTxRateChangeAction */
	unsigned long LastTxOkCount;
	u8 PER[MAX_STEP_OF_TX_RATE_SWITCH];

	/* a bitmap of BOOLEAN flags. each bit represent an operation status of a particular */
	/* BOOLEAN control, either ON or OFF. These flags should always be accessed via */
	/* CLIENT_STATUS_TEST_FLAG(), CLIENT_STATUS_SET_FLAG(), CLIENT_STATUS_CLEAR_FLAG() macros. */
	/* see fOP_STATUS_xxx in RTMP_DEF.C for detail bit definition. fCLIENT_STATUS_AMSDU_INUSED */
	unsigned long ClientStatusFlags;

	HTTRANSMIT_SETTING HTPhyMode, MaxHTPhyMode, MinHTPhyMode;	/* For transmit phy setting in TXWI. */

	/* HT EWC MIMO-N used parameters */
	u16 RXBAbitmap;	/* fill to on-chip  RXWI_BA_BITMASK in 8.1.3RX attribute entry format */
	u16 TXBAbitmap;	/* This bitmap as originator, only keep in software used to mark AMPDU bit in TXWI */
	u16 TXAutoBAbitmap;
	u16 BADeclineBitmap;
	u16 BARecWcidArray[NUM_OF_TID];	/* The mapping wcid of recipient session. if RXBAbitmap bit is masked */
	u16 BAOriWcidArray[NUM_OF_TID];	/* The mapping wcid of originator session. if TXBAbitmap bit is masked */
	u16 BAOriSequence[NUM_OF_TID];	/* The mapping wcid of originator session. if TXBAbitmap bit is masked */

	/* 802.11n features. */
	u8 MpduDensity;
	u8 MaxRAmpduFactor;
	u8 AMsduSize;
	u8 MmpsMode;		/* MIMO power save more. */

	HT_CAPABILITY_IE HTCapability;

	BOOLEAN bAutoTxRateSwitch;

	u8 RateLen;
	struct _MAC_TABLE_ENTRY *pNext;
	u16 TxSeq[NUM_OF_TID];
	u16 NonQosDataSeq;

	RSSI_SAMPLE RssiSample;

	u32 TXMCSExpected[16];
	u32 TXMCSSuccessful[16];
	u32 TXMCSFailed[16];
	u32 TXMCSAutoFallBack[16][16];

	unsigned long LastBeaconRxTime;

	unsigned long AssocDeadLine;
} MAC_TABLE_ENTRY, *PMAC_TABLE_ENTRY;

typedef struct _MAC_TABLE {
	u16 Size;
	MAC_TABLE_ENTRY *Hash[HASH_TABLE_SIZE];
	MAC_TABLE_ENTRY Content[MAX_LEN_OF_MAC_TABLE];
	QUEUE_HEADER McastPsQueue;
	unsigned long PsQIdleCount;
	BOOLEAN fAnyStationInPsm;
	BOOLEAN fAnyStationBadAtheros;	/* Check if any Station is atheros 802.11n Chip.  We need to use RTS/CTS with Atheros 802,.11n chip. */
	BOOLEAN fAnyTxOPForceDisable;	/* Check if it is necessary to disable BE TxOP */
	BOOLEAN fAllStationAsRalink;	/* Check if all stations are ralink-chipset */
	BOOLEAN fAnyStationIsLegacy;	/* Check if I use legacy rate to transmit to my BSS Station/ */
	BOOLEAN fAnyStationNonGF;	/* Check if any Station can't support GF. */
	BOOLEAN fAnyStation20Only;	/* Check if any Station can't support GF. */
	BOOLEAN fAnyStationMIMOPSDynamic;	/* Check if any Station is MIMO Dynamic */
	BOOLEAN fAnyBASession;	/* Check if there is BA session.  Force turn on RTS/CTS */
/*2008/10/28: KH add to support Antenna power-saving of AP<-- */
/*2008/10/28: KH add to support Antenna power-saving of AP--> */
} MAC_TABLE, *PMAC_TABLE;

struct wificonf {
	BOOLEAN bShortGI;
	BOOLEAN bGreenField;
};

typedef struct _RTMP_DEV_INFO_ {
	u8 chipName[16];
	RTMP_INF_TYPE infType;
} RTMP_DEV_INFO;

struct _RTMP_CHIP_OP_ {
	/*  Calibration access related callback functions */
	int (*eeinit) (RTMP_ADAPTER * pAd);	/* int (*eeinit)(RTMP_ADAPTER *pAd); */
	int (*eeread) (RTMP_ADAPTER * pAd, u16 offset, u16 *pValue);	/* int (*eeread)(RTMP_ADAPTER *pAd, int offset, u16 *pValue); */

	/* MCU related callback functions */
	int (*loadFirmware) (RTMP_ADAPTER * pAd);	/* int (*loadFirmware)(RTMP_ADAPTER *pAd); */
	int (*eraseFirmware) (RTMP_ADAPTER * pAd);	/* int (*eraseFirmware)(RTMP_ADAPTER *pAd); */
	int (*sendCommandToMcu) (RTMP_ADAPTER * pAd, u8 cmd, u8 token, u8 arg0, u8 arg1);;	/* int (*sendCommandToMcu)(RTMP_ADAPTER *pAd, u8 cmd, u8 token, u8 arg0, u8 arg1); */

	/* RF access related callback functions */
	REG_PAIR *pRFRegTable;
	void (*AsicRfInit) (RTMP_ADAPTER * pAd);
	void (*AsicRfTurnOn) (RTMP_ADAPTER * pAd);
	void (*AsicRfTurnOff) (RTMP_ADAPTER * pAd);
	void (*AsicReverseRfFromSleepMode) (RTMP_ADAPTER * pAd);
	void (*AsicHaltAction) (RTMP_ADAPTER * pAd);
};

/* */
/*  The miniport adapter structure */
/* */
struct _RTMP_ADAPTER {
	void *OS_Cookie;	/* save specific structure relative to OS */
	PNET_DEV net_dev;
	unsigned long VirtualIfCnt;

	RTMP_CHIP_OP chipOps;
	u16 ThisTbttNumToNextWakeUp;

#ifdef RTMP_MAC_PCI
/*****************************************************************************************/
/*      PCI related parameters																  */
/*****************************************************************************************/
	u8 *CSRBaseAddress;	/* PCI MMIO Base Address, all access will use */
	unsigned int irq_num;

	u16 LnkCtrlBitMask;
	u16 RLnkCtrlConfiguration;
	u16 RLnkCtrlOffset;
	u16 HostLnkCtrlConfiguration;
	u16 HostLnkCtrlOffset;
	u16 PCIePowerSaveLevel;
	unsigned long Rt3xxHostLinkCtrl;	/* USed for 3090F chip */
	unsigned long Rt3xxRalinkLinkCtrl;	/* USed for 3090F chip */
	u16 DeviceID;	/* Read from PCI config */
	unsigned long AccessBBPFailCount;
	BOOLEAN bPCIclkOff;	/* flag that indicate if the PICE power status in Configuration SPace.. */
	BOOLEAN bPCIclkOffDisableTx;	/* */

	BOOLEAN brt30xxBanMcuCmd;	/*when = 0xff means all commands are ok to set . */
	BOOLEAN b3090ESpecialChip;	/*3090E special chip that write EEPROM 0x24=0x9280. */
	unsigned long CheckDmaBusyCount;	/* Check Interrupt Status Register Count. */

	u32 int_enable_reg;
	u32 int_disable_mask;
	u32 int_pending;

	RTMP_DMABUF TxBufSpace[NUM_OF_TX_RING];	/* Shared memory of all 1st pre-allocated TxBuf associated with each TXD */
	RTMP_DMABUF RxDescRing;	/* Shared memory for RX descriptors */
	RTMP_DMABUF TxDescRing[NUM_OF_TX_RING];	/* Shared memory for Tx descriptors */
	RTMP_TX_RING TxRing[NUM_OF_TX_RING];	/* AC0~4 + HCCA */
#endif				/* RTMP_MAC_PCI // */

	NDIS_SPIN_LOCK irq_lock;
	u8 irq_disabled;

#ifdef RTMP_MAC_USB
/*****************************************************************************************/
/*      USB related parameters                                                           */
/*****************************************************************************************/
	struct usb_config_descriptor *config;
	u32 BulkInEpAddr;	/* bulk-in endpoint address */
	u32 BulkOutEpAddr[6];	/* bulk-out endpoint address */

	u32 NumberOfPipes;
	u16 BulkOutMaxPacketSize;
	u16 BulkInMaxPacketSize;

	/*======Control Flags */
	long PendingIoCount;
	unsigned long BulkFlags;
	BOOLEAN bUsbTxBulkAggre;	/* Flags for bulk out data priority */

	/*======Cmd Thread */
	CmdQ CmdQ;
	NDIS_SPIN_LOCK CmdQLock;	/* CmdQLock spinlock */
	RTMP_OS_TASK cmdQTask;

	/*======Semaphores (event) */
	RTMP_OS_SEM UsbVendorReq_semaphore;
	void *UsbVendorReqBuf;
	wait_queue_head_t *wait;
#endif				/* RTMP_MAC_USB // */

/*****************************************************************************************/
/*      RBUS related parameters																  */
/*****************************************************************************************/

/*****************************************************************************************/
/*      Both PCI/USB related parameters														  */
/*****************************************************************************************/
	/*RTMP_DEV_INFO                 chipInfo; */
	RTMP_INF_TYPE infType;

/*****************************************************************************************/
/*      Driver Mgmt related parameters														  */
/*****************************************************************************************/
	RTMP_OS_TASK mlmeTask;
#ifdef RTMP_TIMER_TASK_SUPPORT
	/* If you want use timer task to handle the timer related jobs, enable this. */
	RTMP_TIMER_TASK_QUEUE TimerQ;
	NDIS_SPIN_LOCK TimerQLock;
	RTMP_OS_TASK timerTask;
#endif				/* RTMP_TIMER_TASK_SUPPORT // */

/*****************************************************************************************/
/*      Tx related parameters                                                           */
/*****************************************************************************************/
	BOOLEAN DeQueueRunning[NUM_OF_TX_RING];	/* for ensuring RTUSBDeQueuePacket get call once */
	NDIS_SPIN_LOCK DeQueueLock[NUM_OF_TX_RING];

#ifdef RTMP_MAC_USB
	/* Data related context and AC specified, 4 AC supported */
	NDIS_SPIN_LOCK BulkOutLock[6];	/* BulkOut spinlock for 4 ACs */
	NDIS_SPIN_LOCK MLMEBulkOutLock;	/* MLME BulkOut lock */

	HT_TX_CONTEXT TxContext[NUM_OF_TX_RING];
	NDIS_SPIN_LOCK TxContextQueueLock[NUM_OF_TX_RING];	/* TxContextQueue spinlock */

	/* 4 sets of Bulk Out index and pending flag */
	u8 NextBulkOutIndex[4];	/* only used for 4 EDCA bulkout pipe */

	BOOLEAN BulkOutPending[6];	/* used for total 6 bulkout pipe */
	u8 bulkResetPipeid;
	BOOLEAN MgmtBulkPending;
	unsigned long bulkResetReq[6];
#endif				/* RTMP_MAC_USB // */

	/* resource for software backlog queues */
	QUEUE_HEADER TxSwQueue[NUM_OF_TX_RING];	/* 4 AC + 1 HCCA */
	NDIS_SPIN_LOCK TxSwQueueLock[NUM_OF_TX_RING];	/* TxSwQueue spinlock */

	RTMP_DMABUF MgmtDescRing;	/* Shared memory for MGMT descriptors */
	RTMP_MGMT_RING MgmtRing;
	NDIS_SPIN_LOCK MgmtRingLock;	/* Prio Ring spinlock */

/*****************************************************************************************/
/*      Rx related parameters                                                           */
/*****************************************************************************************/

#ifdef RTMP_MAC_PCI
	RTMP_RX_RING RxRing;
	NDIS_SPIN_LOCK RxRingLock;	/* Rx Ring spinlock */
#ifdef RT3090
	NDIS_SPIN_LOCK McuCmdLock;	/*MCU Command Queue spinlock */
#endif				/* RT3090 // */
#endif				/* RTMP_MAC_PCI // */
#ifdef RTMP_MAC_USB
	RX_CONTEXT RxContext[RX_RING_SIZE];	/* 1 for redundant multiple IRP bulk in. */
	NDIS_SPIN_LOCK BulkInLock;	/* BulkIn spinlock for 4 ACs */
	u8 PendingRx;	/* The Maximum pending Rx value should be       RX_RING_SIZE. */
	u8 NextRxBulkInIndex;	/* Indicate the current RxContext Index which hold by Host controller. */
	u8 NextRxBulkInReadIndex;	/* Indicate the current RxContext Index which driver can read & process it. */
	unsigned long NextRxBulkInPosition;	/* Want to contatenate 2 URB buffer while 1st is bulkin failed URB. This Position is 1st URB TransferLength. */
	unsigned long TransferBufferLength;	/* current length of the packet buffer */
	unsigned long ReadPosition;	/* current read position in a packet buffer */
#endif				/* RTMP_MAC_USB // */

/*****************************************************************************************/
/*      ASIC related parameters                                                          */
/*****************************************************************************************/
	u32 MACVersion;	/* MAC version. Record rt2860C(0x28600100) or rt2860D (0x28600101).. */

	/* --------------------------- */
	/* E2PROM */
	/* --------------------------- */
	unsigned long EepromVersion;	/* byte 0: version, byte 1: revision, byte 2~3: unused */
	unsigned long FirmwareVersion;	/* byte 0: Minor version, byte 1: Major version, otherwise unused. */
	u16 EEPROMDefaultValue[NUM_EEPROM_BBP_PARMS];
	u8 EEPROMAddressNum;	/* 93c46=6  93c66=8 */
	BOOLEAN EepromAccess;
	u8 EFuseTag;

	/* --------------------------- */
	/* BBP Control */
	/* --------------------------- */
	u8 BbpWriteLatch[140];	/* record last BBP register value written via BBP_IO_WRITE/BBP_IO_WRITE_VY_REG_ID */
	char BbpRssiToDbmDelta;	/* change from u8 to char for high power */
	BBP_R66_TUNING BbpTuning;

	/* ---------------------------- */
	/* RFIC control */
	/* ---------------------------- */
	u8 RfIcType;		/* RFIC_xxx */
	unsigned long RfFreqOffset;	/* Frequency offset for channel switching */
	RTMP_RF_REGS LatchRfRegs;	/* latch th latest RF programming value since RF IC doesn't support READ */

	EEPROM_ANTENNA_STRUC Antenna;	/* Since ANtenna definition is different for a & g. We need to save it for future reference. */
	EEPROM_NIC_CONFIG2_STRUC NicConfig2;

	/* This soft Rx Antenna Diversity mechanism is used only when user set */
	/* RX Antenna = DIVERSITY ON */
	SOFT_RX_ANT_DIVERSITY RxAnt;

	u8 RFProgSeq;
	CHANNEL_TX_POWER TxPower[MAX_NUM_OF_CHANNELS];	/* Store Tx power value for all channels. */
	CHANNEL_TX_POWER ChannelList[MAX_NUM_OF_CHANNELS];	/* list all supported channels for site survey */
	CHANNEL_11J_TX_POWER TxPower11J[MAX_NUM_OF_11JCHANNELS];	/* 802.11j channel and bw */
	CHANNEL_11J_TX_POWER ChannelList11J[MAX_NUM_OF_11JCHANNELS];	/* list all supported channels for site survey */

	u8 ChannelListNum;	/* number of channel in ChannelList[] */
	u8 Bbp94;
	BOOLEAN BbpForCCK;
	unsigned long Tx20MPwrCfgABand[5];
	unsigned long Tx20MPwrCfgGBand[5];
	unsigned long Tx40MPwrCfgABand[5];
	unsigned long Tx40MPwrCfgGBand[5];

	BOOLEAN bAutoTxAgcA;	/* Enable driver auto Tx Agc control */
	u8 TssiRefA;		/* Store Tssi reference value as 25 temperature. */
	u8 TssiPlusBoundaryA[5];	/* Tssi boundary for increase Tx power to compensate. */
	u8 TssiMinusBoundaryA[5];	/* Tssi boundary for decrease Tx power to compensate. */
	u8 TxAgcStepA;	/* Store Tx TSSI delta increment / decrement value */
	char TxAgcCompensateA;	/* Store the compensation (TxAgcStep * (idx-1)) */

	BOOLEAN bAutoTxAgcG;	/* Enable driver auto Tx Agc control */
	u8 TssiRefG;		/* Store Tssi reference value as 25 temperature. */
	u8 TssiPlusBoundaryG[5];	/* Tssi boundary for increase Tx power to compensate. */
	u8 TssiMinusBoundaryG[5];	/* Tssi boundary for decrease Tx power to compensate. */
	u8 TxAgcStepG;	/* Store Tx TSSI delta increment / decrement value */
	char TxAgcCompensateG;	/* Store the compensation (TxAgcStep * (idx-1)) */

	char BGRssiOffset0;	/* Store B/G RSSI#0 Offset value on EEPROM 0x46h */
	char BGRssiOffset1;	/* Store B/G RSSI#1 Offset value */
	char BGRssiOffset2;	/* Store B/G RSSI#2 Offset value */

	char ARssiOffset0;	/* Store A RSSI#0 Offset value on EEPROM 0x4Ah */
	char ARssiOffset1;	/* Store A RSSI#1 Offset value */
	char ARssiOffset2;	/* Store A RSSI#2 Offset value */

	char BLNAGain;		/* Store B/G external LNA#0 value on EEPROM 0x44h */
	char ALNAGain0;		/* Store A external LNA#0 value for ch36~64 */
	char ALNAGain1;		/* Store A external LNA#1 value for ch100~128 */
	char ALNAGain2;		/* Store A external LNA#2 value for ch132~165 */
#ifdef RT30xx
	/* for 3572 */
	u8 Bbp25;
	u8 Bbp26;

	u8 TxMixerGain24G;	/* Tx mixer gain value from EEPROM to improve Tx EVM / Tx DAC, 2.4G */
	u8 TxMixerGain5G;
#endif				/* RT30xx // */
	/* ---------------------------- */
	/* LED control */
	/* ---------------------------- */
	MCU_LEDCS_STRUC LedCntl;
	u16 Led1;		/* read from EEPROM 0x3c */
	u16 Led2;		/* EEPROM 0x3e */
	u16 Led3;		/* EEPROM 0x40 */
	u8 LedIndicatorStrength;
	u8 RssiSingalstrengthOffet;
	BOOLEAN bLedOnScanning;
	u8 LedStatus;

/*****************************************************************************************/
/*      802.11 related parameters                                                        */
/*****************************************************************************************/
	/* outgoing BEACON frame buffer and corresponding TXD */
	TXWI_STRUC BeaconTxWI;
	u8 *BeaconBuf;
	u16 BeaconOffset[HW_BEACON_MAX_COUNT];

	/* pre-build PS-POLL and NULL frame upon link up. for efficiency purpose. */
	PSPOLL_FRAME PsPollFrame;
	HEADER_802_11 NullFrame;

#ifdef RTMP_MAC_USB
	TX_CONTEXT BeaconContext[BEACON_RING_SIZE];
	TX_CONTEXT NullContext;
	TX_CONTEXT PsPollContext;
	TX_CONTEXT RTSContext;
#endif				/* RTMP_MAC_USB // */

/*=========AP=========== */

/*=======STA=========== */
	/* ----------------------------------------------- */
	/* STA specific configuration & operation status */
	/* used only when pAd->OpMode == OPMODE_STA */
	/* ----------------------------------------------- */
	STA_ADMIN_CONFIG StaCfg;	/* user desired settings */
	STA_ACTIVE_CONFIG StaActive;	/* valid only when ADHOC_ON(pAd) || INFRA_ON(pAd) */
	char nickname[IW_ESSID_MAX_SIZE + 1];	/* nickname, only used in the iwconfig i/f */
	int PreMediaState;

/*=======Common=========== */
	/* OP mode: either AP or STA */
	u8 OpMode;		/* OPMODE_STA, OPMODE_AP */

	int IndicateMediaState;	/* Base on Indication state, default is NdisMediaStateDisConnected */

	/* MAT related parameters */

	/* configuration: read from Registry & E2PROM */
	BOOLEAN bLocalAdminMAC;	/* Use user changed MAC */
	u8 PermanentAddress[MAC_ADDR_LEN];	/* Factory default MAC address */
	u8 CurrentAddress[MAC_ADDR_LEN];	/* User changed MAC address */

	/* ------------------------------------------------------ */
	/* common configuration to both OPMODE_STA and OPMODE_AP */
	/* ------------------------------------------------------ */
	COMMON_CONFIG CommonCfg;
	MLME_STRUCT Mlme;

	/* AP needs those vaiables for site survey feature. */
	MLME_AUX MlmeAux;	/* temporary settings used during MLME state machine */
	BSS_TABLE ScanTab;	/* store the latest SCAN result */

	/*About MacTab, the sta driver will use #0 and #1 for multicast and AP. */
	MAC_TABLE MacTab;	/* ASIC on-chip WCID entry table.  At TX, ASIC always use key according to this on-chip table. */
	NDIS_SPIN_LOCK MacTabLock;

	BA_TABLE BATable;

	NDIS_SPIN_LOCK BATabLock;
	RALINK_TIMER_STRUCT RECBATimer;

	/* encryption/decryption KEY tables */
	CIPHER_KEY SharedKey[MAX_MBSSID_NUM][4];	/* STA always use SharedKey[BSS0][0..3] */

	/* RX re-assembly buffer for fragmentation */
	FRAGMENT_FRAME FragFrame;	/* Frame storage for fragment frame */

	/* various Counters */
	COUNTER_802_3 Counters8023;	/* 802.3 counters */
	COUNTER_802_11 WlanCounters;	/* 802.11 MIB counters */
	COUNTER_RALINK RalinkCounters;	/* Ralink propriety counters */
	COUNTER_DRS DrsCounters;	/* counters for Dynamic TX Rate Switching */
	PRIVATE_STRUC PrivateInfo;	/* Private information & counters */

	/* flags, see fRTMP_ADAPTER_xxx flags */
	unsigned long Flags;		/* Represent current device status */
	unsigned long PSFlags;		/* Power Save operation flag. */

	/* current TX sequence # */
	u16 Sequence;

	/* Control disconnect / connect event generation */
	/*+++Didn't used anymore */
	unsigned long LinkDownTime;
	/*--- */
	unsigned long LastRxRate;
	unsigned long LastTxRate;
	/*+++Used only for Station */
	BOOLEAN bConfigChanged;	/* Config Change flag for the same SSID setting */
	/*--- */

	unsigned long ExtraInfo;	/* Extra information for displaying status */
	unsigned long SystemErrorBitmap;	/* b0: E2PROM version error */

	/*+++Didn't used anymore */
	unsigned long MacIcVersion;	/* MAC/BBP serial interface issue solved after ver.D */
	/*--- */

	/* --------------------------- */
	/* System event log */
	/* --------------------------- */
	RT_802_11_EVENT_TABLE EventTab;

	BOOLEAN HTCEnable;

	/*****************************************************************************************/
	/*      Statistic related parameters                                                     */
	/*****************************************************************************************/
#ifdef RTMP_MAC_USB
	unsigned long BulkOutDataOneSecCount;
	unsigned long BulkInDataOneSecCount;
	unsigned long BulkLastOneSecCount;	/* BulkOutDataOneSecCount + BulkInDataOneSecCount */
	unsigned long watchDogRxCnt;
	unsigned long watchDogRxOverFlowCnt;
	unsigned long watchDogTxPendingCnt[NUM_OF_TX_RING];
	int TransferedLength[NUM_OF_TX_RING];
#endif				/* RTMP_MAC_USB // */

	BOOLEAN bUpdateBcnCntDone;
	unsigned long watchDogMacDeadlock;	/* prevent MAC/BBP into deadlock condition */
	/* ---------------------------- */
	/* DEBUG paramerts */
	/* ---------------------------- */
	/*unsigned long         DebugSetting[4]; */
	BOOLEAN bBanAllBaSetup;
	BOOLEAN bPromiscuous;

	/* ---------------------------- */
	/* rt2860c emulation-use Parameters */
	/* ---------------------------- */
	/*unsigned long         rtsaccu[30]; */
	/*unsigned long         ctsaccu[30]; */
	/*unsigned long         cfendaccu[30]; */
	/*unsigned long         bacontent[16]; */
	/*unsigned long         rxint[RX_RING_SIZE+1]; */
	/*u8         rcvba[60]; */
	BOOLEAN bLinkAdapt;
	BOOLEAN bForcePrintTX;
	BOOLEAN bForcePrintRX;
	/*BOOLEAN               bDisablescanning;               //defined in RT2870 USB */
	BOOLEAN bStaFifoTest;
	BOOLEAN bProtectionTest;
	BOOLEAN bBroadComHT;
	/*+++Following add from RT2870 USB. */
	unsigned long BulkOutReq;
	unsigned long BulkOutComplete;
	unsigned long BulkOutCompleteOther;
	unsigned long BulkOutCompleteCancel;	/* seems not use now? */
	unsigned long BulkInReq;
	unsigned long BulkInComplete;
	unsigned long BulkInCompleteFail;
	/*--- */

	struct wificonf WIFItestbed;

	struct reordering_mpdu_pool mpdu_blk_pool;

	unsigned long OneSecondnonBEpackets;	/* record non BE packets per second */

#ifdef LINUX
	struct iw_statistics iw_stats;

	struct net_device_stats stats;
#endif				/* LINUX // */

	unsigned long TbttTickCount;
#ifdef PCI_MSI_SUPPORT
	BOOLEAN HaveMsi;
#endif				/* PCI_MSI_SUPPORT // */

	u8 is_on;

#define TIME_BASE			(1000000/OS_HZ)
#define TIME_ONE_SECOND		(1000000/TIME_BASE)
	u8 flg_be_adjust;
	unsigned long be_adjust_last_time;

	u8 FlgCtsEnabled;
	u8 PM_FlgSuspend;

#ifdef RT30xx
#ifdef RTMP_EFUSE_SUPPORT
	BOOLEAN bUseEfuse;
	u8 EEPROMImage[1024];
#endif				/* RTMP_EFUSE_SUPPORT // */
#endif				/* RT30xx // */
};

#define DELAYINTMASK		0x0003fffb
#define INTMASK				0x0003fffb
#define IndMask				0x0003fffc
#define RxINT				0x00000005	/* Delayed Rx or indivi rx */
#define TxDataInt			0x000000fa	/* Delayed Tx or indivi tx */
#define TxMgmtInt			0x00000102	/* Delayed Tx or indivi tx */
#define TxCoherent			0x00020000	/* tx coherent */
#define RxCoherent			0x00010000	/* rx coherent */
#define McuCommand			0x00000200	/* mcu */
#define PreTBTTInt			0x00001000	/* Pre-TBTT interrupt */
#define TBTTInt				0x00000800	/* TBTT interrupt */
#define GPTimeOutInt			0x00008000	/* GPtimeout interrupt */
#define AutoWakeupInt		0x00004000	/* AutoWakeupInt interrupt */
#define FifoStaFullInt			0x00002000	/*  fifo statistics full interrupt */

/***************************************************************************
  *	Rx Path software control block related data structures
  **************************************************************************/
typedef struct _RX_BLK_ {
/*      RXD_STRUC               RxD; // sample */
	RT28XX_RXD_STRUC RxD;
	PRXWI_STRUC pRxWI;
	PHEADER_802_11 pHeader;
	PNDIS_PACKET pRxPacket;
	u8 *pData;
	u16 DataSize;
	u16 Flags;
	u8 UserPriority;	/* for calculate TKIP MIC using */
} RX_BLK;

#define RX_BLK_SET_FLAG(_pRxBlk, _flag)		(_pRxBlk->Flags |= _flag)
#define RX_BLK_TEST_FLAG(_pRxBlk, _flag)	(_pRxBlk->Flags & _flag)
#define RX_BLK_CLEAR_FLAG(_pRxBlk, _flag)	(_pRxBlk->Flags &= ~(_flag))

#define fRX_WDS			0x0001
#define fRX_AMSDU       0x0002
#define fRX_ARALINK     0x0004
#define fRX_HTC         0x0008
#define fRX_PAD         0x0010
#define fRX_AMPDU       0x0020
#define fRX_QOS			0x0040
#define fRX_INFRA		0x0080
#define fRX_EAP			0x0100
#define fRX_MESH		0x0200
#define fRX_APCLI		0x0400
#define fRX_DLS			0x0800
#define fRX_WPI			0x1000

#define LENGTH_AMSDU_SUBFRAMEHEAD	14
#define LENGTH_ARALINK_SUBFRAMEHEAD	14
#define LENGTH_ARALINK_HEADER_FIELD	 2

/***************************************************************************
  *	Tx Path software control block related data structures
  **************************************************************************/
#define TX_UNKOWN_FRAME			0x00
#define TX_MCAST_FRAME			0x01
#define TX_LEGACY_FRAME			0x02
#define TX_AMPDU_FRAME			0x04
#define TX_AMSDU_FRAME			0x08
#define TX_RALINK_FRAME			0x10
#define TX_FRAG_FRAME			0x20

/*      Currently the sizeof(TX_BLK) is 148 bytes. */
typedef struct _TX_BLK_ {
	u8 QueIdx;
	u8 TxFrameType;	/* Indicate the Transmission type of the all frames in one batch */
	u8 TotalFrameNum;	/* Total frame number want to send-out in one batch */
	u16 TotalFragNum;	/* Total frame fragments required in one batch */
	u16 TotalFrameLen;	/* Total length of all frames want to send-out in one batch */

	QUEUE_HEADER TxPacketList;
	MAC_TABLE_ENTRY *pMacEntry;	/* NULL: packet with 802.11 RA field is multicast/broadcast address */
	HTTRANSMIT_SETTING *pTransmit;

	/* Following structure used for the characteristics of a specific packet. */
	PNDIS_PACKET pPacket;
	u8 *pSrcBufHeader;	/* Reference to the head of sk_buff->data */
	u8 *pSrcBufData;	/* Reference to the sk_buff->data, will changed depends on hanlding progresss */
	u32 SrcBufLen;		/* Length of packet payload which not including Layer 2 header */
	u8 *pExtraLlcSnapEncap;	/* NULL means no extra LLC/SNAP is required */
	u8 HeaderBuf[128];	/* TempBuffer for TX_INFO + TX_WI + 802.11 Header + padding + AMSDU SubHeader + LLC/SNAP */
	/*RT2870 2.1.0.0 uses only 80 bytes */
	/*RT3070 2.1.1.0 uses only 96 bytes */
	/*RT3090 2.1.0.0 uses only 96 bytes */
	u8 MpduHeaderLen;	/* 802.11 header length NOT including the padding */
	u8 HdrPadLen;	/* recording Header Padding Length; */
	u8 apidx;		/* The interface associated to this packet */
	u8 Wcid;		/* The MAC entry associated to this packet */
	u8 UserPriority;	/* priority class of packet */
	u8 FrameGap;		/* what kind of IFS this packet use */
	u8 MpduReqNum;	/* number of fragments of this frame */
	u8 TxRate;		/* TODO: Obsoleted? Should change to MCS? */
	u8 CipherAlg;	/* cipher alogrithm */
	PCIPHER_KEY pKey;

	u16 Flags;		/*See following definitions for detail. */

	/*YOU SHOULD NOT TOUCH IT! Following parameters are used for hardware-depended layer. */
	unsigned long Priv;		/* Hardware specific value saved in here. */
} TX_BLK, *PTX_BLK;

#define fTX_bRtsRequired		0x0001	/* Indicate if need send RTS frame for protection. Not used in RT2860/RT2870. */
#define fTX_bAckRequired       	0x0002	/* the packet need ack response */
#define fTX_bPiggyBack     		0x0004	/* Legacy device use Piggback or not */
#define fTX_bHTRate         	0x0008	/* allow to use HT rate */
#define fTX_bForceNonQoS       	0x0010	/* force to transmit frame without WMM-QoS in HT mode */
#define fTX_bAllowFrag       	0x0020	/* allow to fragment the packet, A-MPDU, A-MSDU, A-Ralink is not allowed to fragment */
#define fTX_bMoreData			0x0040	/* there are more data packets in PowerSave Queue */
#define fTX_bWMM				0x0080	/* QOS Data */
#define fTX_bClearEAPFrame		0x0100

#define TX_BLK_SET_FLAG(_pTxBlk, _flag)		(_pTxBlk->Flags |= _flag)
#define TX_BLK_TEST_FLAG(_pTxBlk, _flag)	(((_pTxBlk->Flags & _flag) == _flag) ? 1 : 0)
#define TX_BLK_CLEAR_FLAG(_pTxBlk, _flag)	(_pTxBlk->Flags &= ~(_flag))

/***************************************************************************
  *	Other static inline function definitions
  **************************************************************************/
static inline void ConvertMulticastIP2MAC(u8 *pIpAddr,
					  u8 ** ppMacAddr,
					  u16 ProtoType)
{
	if (pIpAddr == NULL)
		return;

	if (ppMacAddr == NULL || *ppMacAddr == NULL)
		return;

	switch (ProtoType) {
	case ETH_P_IPV6:
/*                      memset(*ppMacAddr, 0, ETH_LENGTH_OF_ADDRESS); */
		*(*ppMacAddr) = 0x33;
		*(*ppMacAddr + 1) = 0x33;
		*(*ppMacAddr + 2) = pIpAddr[12];
		*(*ppMacAddr + 3) = pIpAddr[13];
		*(*ppMacAddr + 4) = pIpAddr[14];
		*(*ppMacAddr + 5) = pIpAddr[15];
		break;

	case ETH_P_IP:
	default:
/*                      memset(*ppMacAddr, 0, ETH_LENGTH_OF_ADDRESS); */
		*(*ppMacAddr) = 0x01;
		*(*ppMacAddr + 1) = 0x00;
		*(*ppMacAddr + 2) = 0x5e;
		*(*ppMacAddr + 3) = pIpAddr[1] & 0x7f;
		*(*ppMacAddr + 4) = pIpAddr[2];
		*(*ppMacAddr + 5) = pIpAddr[3];
		break;
	}

	return;
}

char *GetPhyMode(int Mode);
char *GetBW(int BW);

/* */
/*  Private routines in rtmp_init.c */
/* */
int RTMPAllocAdapterBlock(void *handle,
				  OUT PRTMP_ADAPTER * ppAdapter);

int RTMPAllocTxRxRingMemory(IN PRTMP_ADAPTER pAd);

void RTMPFreeAdapter(IN PRTMP_ADAPTER pAd);

int NICReadRegParameters(IN PRTMP_ADAPTER pAd,
				 IN NDIS_HANDLE WrapperConfigurationContext);

#ifdef RTMP_RF_RW_SUPPORT
void NICInitRFRegisters(IN PRTMP_ADAPTER pAd);

void RtmpChipOpsRFHook(IN RTMP_ADAPTER * pAd);

int RT30xxWriteRFRegister(IN PRTMP_ADAPTER pAd,
				  u8 regID, u8 value);

int RT30xxReadRFRegister(IN PRTMP_ADAPTER pAd,
				 u8 regID, u8 *pValue);
#endif /* RTMP_RF_RW_SUPPORT // */

void NICReadEEPROMParameters(IN PRTMP_ADAPTER pAd, u8 *mac_addr);

void NICInitAsicFromEEPROM(IN PRTMP_ADAPTER pAd);

int NICInitializeAdapter(IN PRTMP_ADAPTER pAd, IN BOOLEAN bHardReset);

int NICInitializeAsic(IN PRTMP_ADAPTER pAd, IN BOOLEAN bHardReset);

void NICIssueReset(IN PRTMP_ADAPTER pAd);

void RTMPRingCleanUp(IN PRTMP_ADAPTER pAd, u8 RingType);

void UserCfgInit(IN PRTMP_ADAPTER pAd);

void NICResetFromError(IN PRTMP_ADAPTER pAd);

int NICLoadFirmware(IN PRTMP_ADAPTER pAd);

void NICEraseFirmware(IN PRTMP_ADAPTER pAd);

int NICLoadRateSwitchingParams(IN PRTMP_ADAPTER pAd);

BOOLEAN NICCheckForHang(IN PRTMP_ADAPTER pAd);

void NICUpdateFifoStaCounters(IN PRTMP_ADAPTER pAd);

void NICUpdateRawCounters(IN PRTMP_ADAPTER pAd);

void RTMPZeroMemory(void *pSrc, unsigned long Length);

unsigned long RTMPCompareMemory(void *pSrc1, void *pSrc2, unsigned long Length);

void RTMPMoveMemory(void *pDest, void *pSrc, unsigned long Length);

void AtoH(char *src, u8 *dest, int destlen);

u8 BtoH(char ch);

void RTMPPatchMacBbpBug(IN PRTMP_ADAPTER pAd);

void RTMPInitTimer(IN PRTMP_ADAPTER pAd,
		   IN PRALINK_TIMER_STRUCT pTimer,
		   void *pTimerFunc, void *pData, IN BOOLEAN Repeat);

void RTMPSetTimer(IN PRALINK_TIMER_STRUCT pTimer, unsigned long Value);

void RTMPModTimer(IN PRALINK_TIMER_STRUCT pTimer, unsigned long Value);

void RTMPCancelTimer(IN PRALINK_TIMER_STRUCT pTimer, OUT BOOLEAN * pCancelled);

void RTMPSetLED(IN PRTMP_ADAPTER pAd, u8 Status);

void RTMPSetSignalLED(IN PRTMP_ADAPTER pAd, IN NDIS_802_11_RSSI Dbm);

void RTMPEnableRxTx(IN PRTMP_ADAPTER pAd);

/* */
/* prototype in action.c */
/* */
void ActionStateMachineInit(IN PRTMP_ADAPTER pAd,
			    IN STATE_MACHINE * S,
			    OUT STATE_MACHINE_FUNC Trans[]);

void MlmeADDBAAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void MlmeDELBAAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void MlmeDLSAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void MlmeInvalidAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void MlmeQOSAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void PeerAddBAReqAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void PeerAddBARspAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void PeerDelBAAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void PeerBAAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void SendPSMPAction(IN PRTMP_ADAPTER pAd, u8 Wcid, u8 Psmp);

void PeerRMAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void PeerPublicAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void PeerHTAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void PeerQOSAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void RECBATimerTimeout(void *SystemSpecific1,
		       void *FunctionContext,
		       void *SystemSpecific2, void *SystemSpecific3);

void ORIBATimerTimeout(IN PRTMP_ADAPTER pAd);

void SendRefreshBAR(IN PRTMP_ADAPTER pAd, IN MAC_TABLE_ENTRY * pEntry);

void ActHeaderInit(IN PRTMP_ADAPTER pAd,
		   IN OUT PHEADER_802_11 pHdr80211,
		   u8 *Addr1, u8 *Addr2, u8 *Addr3);

void BarHeaderInit(IN PRTMP_ADAPTER pAd,
		   IN OUT PFRAME_BAR pCntlBar, u8 *pDA, u8 *pSA);

void InsertActField(IN PRTMP_ADAPTER pAd,
		    u8 *pFrameBuf,
		    unsigned long *pFrameLen, u8 Category, u8 ActCode);

BOOLEAN CntlEnqueueForRecv(IN PRTMP_ADAPTER pAd,
			   unsigned long Wcid,
			   unsigned long MsgLen, IN PFRAME_BA_REQ pMsg);

/* */
/* Private routines in rtmp_data.c */
/* */
BOOLEAN RTMPHandleRxDoneInterrupt(IN PRTMP_ADAPTER pAd);

BOOLEAN RTMPHandleTxRingDmaDoneInterrupt(IN PRTMP_ADAPTER pAd,
					 INT_SOURCE_CSR_STRUC TxRingBitmap);

void RTMPHandleMgmtRingDmaDoneInterrupt(IN PRTMP_ADAPTER pAd);

void RTMPHandleTBTTInterrupt(IN PRTMP_ADAPTER pAd);

void RTMPHandlePreTBTTInterrupt(IN PRTMP_ADAPTER pAd);

void RTMPHandleTwakeupInterrupt(IN PRTMP_ADAPTER pAd);

void RTMPHandleRxCoherentInterrupt(IN PRTMP_ADAPTER pAd);

BOOLEAN TxFrameIsAggregatible(IN PRTMP_ADAPTER pAd,
			      u8 *pPrevAddr1, u8 *p8023hdr);

BOOLEAN PeerIsAggreOn(IN PRTMP_ADAPTER pAd,
		      unsigned long TxRate, IN PMAC_TABLE_ENTRY pMacEntry);

int Sniff2BytesFromNdisBuffer(IN PNDIS_BUFFER pFirstBuffer,
				      u8 DesiredOffset,
				      u8 *pByte0, u8 *pByte1);

int STASendPacket(IN PRTMP_ADAPTER pAd, IN PNDIS_PACKET pPacket);

void STASendPackets(IN NDIS_HANDLE MiniportAdapterContext,
		    IN PPNDIS_PACKET ppPacketArray, u32 NumberOfPackets);

void RTMPDeQueuePacket(IN PRTMP_ADAPTER pAd,
		       IN BOOLEAN bIntContext,
		       u8 QueIdx, u8 Max_Tx_Packets);

int RTMPHardTransmit(IN PRTMP_ADAPTER pAd,
			     IN PNDIS_PACKET pPacket,
			     u8 QueIdx, unsigned long *pFreeTXDLeft);

int STAHardTransmit(IN PRTMP_ADAPTER pAd,
			    IN TX_BLK * pTxBlk, u8 QueIdx);

void STARxEAPOLFrameIndicate(IN PRTMP_ADAPTER pAd,
			     IN MAC_TABLE_ENTRY * pEntry,
			     IN RX_BLK * pRxBlk, u8 FromWhichBSSID);

int RTMPFreeTXDRequest(IN PRTMP_ADAPTER pAd,
			       u8 RingType,
			       u8 NumberRequired, u8 *FreeNumberIs);

int MlmeHardTransmit(IN PRTMP_ADAPTER pAd,
			     u8 QueIdx, IN PNDIS_PACKET pPacket);

int MlmeHardTransmitMgmtRing(IN PRTMP_ADAPTER pAd,
				     u8 QueIdx, IN PNDIS_PACKET pPacket);

#ifdef RTMP_MAC_PCI
int MlmeHardTransmitTxRing(IN PRTMP_ADAPTER pAd,
				   u8 QueIdx, IN PNDIS_PACKET pPacket);

int MlmeDataHardTransmit(IN PRTMP_ADAPTER pAd,
				 u8 QueIdx, IN PNDIS_PACKET pPacket);

void RTMPWriteTxDescriptor(IN PRTMP_ADAPTER pAd,
			   IN PTXD_STRUC pTxD, IN BOOLEAN bWIV, u8 QSEL);
#endif /* RTMP_MAC_PCI // */

u16 RTMPCalcDuration(IN PRTMP_ADAPTER pAd, u8 Rate, unsigned long Size);

void RTMPWriteTxWI(IN PRTMP_ADAPTER pAd, IN PTXWI_STRUC pTxWI, IN BOOLEAN FRAG, IN BOOLEAN CFACK, IN BOOLEAN InsTimestamp, IN BOOLEAN AMPDU, IN BOOLEAN Ack, IN BOOLEAN NSeq,	/* HW new a sequence. */
		   u8 BASize,
		   u8 WCID,
		   unsigned long Length,
		   u8 PID,
		   u8 TID,
		   u8 TxRate,
		   u8 Txopmode,
		   IN BOOLEAN CfAck, IN HTTRANSMIT_SETTING * pTransmit);

void RTMPWriteTxWI_Data(IN PRTMP_ADAPTER pAd,
			IN OUT PTXWI_STRUC pTxWI, IN TX_BLK * pTxBlk);

void RTMPWriteTxWI_Cache(IN PRTMP_ADAPTER pAd,
			 IN OUT PTXWI_STRUC pTxWI, IN TX_BLK * pTxBlk);

void RTMPSuspendMsduTransmission(IN PRTMP_ADAPTER pAd);

void RTMPResumeMsduTransmission(IN PRTMP_ADAPTER pAd);

int MiniportMMRequest(IN PRTMP_ADAPTER pAd,
			      u8 QueIdx, u8 *pData, u32 Length);

/*+++mark by shiang, now this function merge to MiniportMMRequest() */
/*---mark by shiang, now this function merge to MiniportMMRequest() */

void RTMPSendNullFrame(IN PRTMP_ADAPTER pAd,
		       u8 TxRate, IN BOOLEAN bQosNull);

void RTMPSendDisassociationFrame(IN PRTMP_ADAPTER pAd);

void RTMPSendRTSFrame(IN PRTMP_ADAPTER pAd,
		      u8 *pDA,
		      IN unsigned int NextMpduSize,
		      u8 TxRate,
		      u8 RTSRate,
		      u16 AckDuration,
		      u8 QueIdx, u8 FrameGap);

PQUEUE_HEADER RTMPCheckTxSwQueue(IN PRTMP_ADAPTER pAd, u8 * QueIdx);

void RTMPReportMicError(IN PRTMP_ADAPTER pAd, IN PCIPHER_KEY pWpaKey);

void WpaMicFailureReportFrame(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void WpaDisassocApAndBlockAssoc(void *SystemSpecific1,
				void *FunctionContext,
				void *SystemSpecific2,
				void *SystemSpecific3);

void WpaStaPairwiseKeySetting(IN PRTMP_ADAPTER pAd);

void WpaStaGroupKeySetting(IN PRTMP_ADAPTER pAd);

int RTMPCloneNdisPacket(IN PRTMP_ADAPTER pAd,
				IN BOOLEAN pInsAMSDUHdr,
				IN PNDIS_PACKET pInPacket,
				OUT PNDIS_PACKET * ppOutPacket);

int RTMPAllocateNdisPacket(IN PRTMP_ADAPTER pAd,
				   IN PNDIS_PACKET * pPacket,
				   u8 *pHeader,
				   u32 HeaderLen,
				   u8 *pData, u32 DataLen);

void RTMPFreeNdisPacket(IN PRTMP_ADAPTER pAd, IN PNDIS_PACKET pPacket);

BOOLEAN RTMPFreeTXDUponTxDmaDone(IN PRTMP_ADAPTER pAd, u8 QueIdx);

BOOLEAN RTMPCheckDHCPFrame(IN PRTMP_ADAPTER pAd, IN PNDIS_PACKET pPacket);

BOOLEAN RTMPCheckEtherType(IN PRTMP_ADAPTER pAd, IN PNDIS_PACKET pPacket);

/* */
/* Private routines in rtmp_wep.c */
/* */
void RTMPInitWepEngine(IN PRTMP_ADAPTER pAd,
		       u8 *pKey,
		       u8 KeyId, u8 KeyLen, u8 *pDest);

void RTMPEncryptData(IN PRTMP_ADAPTER pAd,
		     u8 *pSrc, u8 *pDest, u32 Len);

BOOLEAN RTMPSoftDecryptWEP(IN PRTMP_ADAPTER pAd,
			   u8 *pData,
			   unsigned long DataByteCnt, IN PCIPHER_KEY pGroupKey);

void RTMPSetICV(IN PRTMP_ADAPTER pAd, u8 *pDest);

void ARCFOUR_INIT(IN PARCFOURCONTEXT Ctx, u8 *pKey, u32 KeyLen);

u8 ARCFOUR_BYTE(IN PARCFOURCONTEXT Ctx);

void ARCFOUR_DECRYPT(IN PARCFOURCONTEXT Ctx,
		     u8 *pDest, u8 *pSrc, u32 Len);

void ARCFOUR_ENCRYPT(IN PARCFOURCONTEXT Ctx,
		     u8 *pDest, u8 *pSrc, u32 Len);

void WPAARCFOUR_ENCRYPT(IN PARCFOURCONTEXT Ctx,
			u8 *pDest, u8 *pSrc, u32 Len);

u32 RTMP_CALC_FCS32(u32 Fcs, u8 *Cp, int Len);

/* */
/* MLME routines */
/* */

/* Asic/RF/BBP related functions */

void AsicAdjustTxPower(IN PRTMP_ADAPTER pAd);

void AsicUpdateProtect(IN PRTMP_ADAPTER pAd,
		       u16 OperaionMode,
		       u8 SetMask,
		       IN BOOLEAN bDisableBGProtect, IN BOOLEAN bNonGFExist);

void AsicSwitchChannel(IN PRTMP_ADAPTER pAd,
		       u8 Channel, IN BOOLEAN bScan);

void AsicLockChannel(IN PRTMP_ADAPTER pAd, u8 Channel);

void AsicRfTuningExec(void *SystemSpecific1,
		      void *FunctionContext,
		      void *SystemSpecific2, void *SystemSpecific3);

void AsicResetBBPAgent(IN PRTMP_ADAPTER pAd);

void AsicSleepThenAutoWakeup(IN PRTMP_ADAPTER pAd,
			     u16 TbttNumToNextWakeUp);

void AsicForceSleep(IN PRTMP_ADAPTER pAd);

void AsicForceWakeup(IN PRTMP_ADAPTER pAd, IN BOOLEAN bFromTx);

void AsicSetBssid(IN PRTMP_ADAPTER pAd, u8 *pBssid);

void AsicSetMcastWC(IN PRTMP_ADAPTER pAd);

void AsicDelWcidTab(IN PRTMP_ADAPTER pAd, u8 Wcid);

void AsicEnableRDG(IN PRTMP_ADAPTER pAd);

void AsicDisableRDG(IN PRTMP_ADAPTER pAd);

void AsicDisableSync(IN PRTMP_ADAPTER pAd);

void AsicEnableBssSync(IN PRTMP_ADAPTER pAd);

void AsicEnableIbssSync(IN PRTMP_ADAPTER pAd);

void AsicSetEdcaParm(IN PRTMP_ADAPTER pAd, IN PEDCA_PARM pEdcaParm);

void AsicSetSlotTime(IN PRTMP_ADAPTER pAd, IN BOOLEAN bUseShortSlotTime);

void AsicAddSharedKeyEntry(IN PRTMP_ADAPTER pAd,
			   u8 BssIndex,
			   u8 KeyIdx,
			   u8 CipherAlg,
			   u8 *pKey, u8 *pTxMic, u8 *pRxMic);

void AsicRemoveSharedKeyEntry(IN PRTMP_ADAPTER pAd,
			      u8 BssIndex, u8 KeyIdx);

void AsicUpdateWCIDAttribute(IN PRTMP_ADAPTER pAd,
			     u16 WCID,
			     u8 BssIndex,
			     u8 CipherAlg,
			     IN BOOLEAN bUsePairewiseKeyTable);

void AsicUpdateWCIDIVEIV(IN PRTMP_ADAPTER pAd,
			 u16 WCID, unsigned long uIV, unsigned long uEIV);

void AsicUpdateRxWCIDTable(IN PRTMP_ADAPTER pAd,
			   u16 WCID, u8 *pAddr);

void AsicAddKeyEntry(IN PRTMP_ADAPTER pAd,
		     u16 WCID,
		     u8 BssIndex,
		     u8 KeyIdx,
		     IN PCIPHER_KEY pCipherKey,
		     IN BOOLEAN bUsePairewiseKeyTable, IN BOOLEAN bTxKey);

void AsicAddPairwiseKeyEntry(IN PRTMP_ADAPTER pAd,
			     u8 *pAddr,
			     u8 WCID, IN CIPHER_KEY * pCipherKey);

void AsicRemovePairwiseKeyEntry(IN PRTMP_ADAPTER pAd,
				u8 BssIdx, u8 Wcid);

BOOLEAN AsicSendCommandToMcu(IN PRTMP_ADAPTER pAd,
			     u8 Command,
			     u8 Token, u8 Arg0, u8 Arg1);

#ifdef RTMP_MAC_PCI
BOOLEAN AsicCheckCommanOk(IN PRTMP_ADAPTER pAd, u8 Command);
#endif /* RTMP_MAC_PCI // */

void MacAddrRandomBssid(IN PRTMP_ADAPTER pAd, u8 *pAddr);

void MgtMacHeaderInit(IN PRTMP_ADAPTER pAd,
		      IN OUT PHEADER_802_11 pHdr80211,
		      u8 SubType,
		      u8 ToDs, u8 *pDA, u8 *pBssid);

void MlmeRadioOff(IN PRTMP_ADAPTER pAd);

void MlmeRadioOn(IN PRTMP_ADAPTER pAd);

void BssTableInit(IN BSS_TABLE * Tab);

void BATableInit(IN PRTMP_ADAPTER pAd, IN BA_TABLE * Tab);

unsigned long BssTableSearch(IN BSS_TABLE * Tab, u8 *pBssid, u8 Channel);

unsigned long BssSsidTableSearch(IN BSS_TABLE * Tab,
			 u8 *pBssid,
			 u8 *pSsid, u8 SsidLen, u8 Channel);

unsigned long BssTableSearchWithSSID(IN BSS_TABLE * Tab,
			     u8 *Bssid,
			     u8 *pSsid,
			     u8 SsidLen, u8 Channel);

unsigned long BssSsidTableSearchBySSID(IN BSS_TABLE * Tab,
			       u8 *pSsid, u8 SsidLen);

void BssTableDeleteEntry(IN OUT PBSS_TABLE pTab,
			 u8 *pBssid, u8 Channel);

void BATableDeleteORIEntry(IN OUT PRTMP_ADAPTER pAd,
			   IN BA_ORI_ENTRY * pBAORIEntry);

void BssEntrySet(IN PRTMP_ADAPTER pAd, OUT PBSS_ENTRY pBss, u8 *pBssid, char Ssid[], u8 SsidLen, u8 BssType, u16 BeaconPeriod, IN PCF_PARM CfParm, u16 AtimWin, u16 CapabilityInfo, u8 SupRate[], u8 SupRateLen, u8 ExtRate[], u8 ExtRateLen, IN HT_CAPABILITY_IE * pHtCapability, IN ADD_HT_INFO_IE * pAddHtInfo,	/* AP might use this additional ht info IE */
		 u8 HtCapabilityLen,
		 u8 AddHtInfoLen,
		 u8 NewExtChanOffset,
		 u8 Channel,
		 char Rssi,
		 IN LARGE_INTEGER TimeStamp,
		 u8 CkipFlag,
		 IN PEDCA_PARM pEdcaParm,
		 IN PQOS_CAPABILITY_PARM pQosCapability,
		 IN PQBSS_LOAD_PARM pQbssLoad,
		 u16 LengthVIE, IN PNDIS_802_11_VARIABLE_IEs pVIE);

unsigned long BssTableSetEntry(IN PRTMP_ADAPTER pAd, OUT PBSS_TABLE pTab, u8 *pBssid, char Ssid[], u8 SsidLen, u8 BssType, u16 BeaconPeriod, IN CF_PARM * CfParm, u16 AtimWin, u16 CapabilityInfo, u8 SupRate[], u8 SupRateLen, u8 ExtRate[], u8 ExtRateLen, IN HT_CAPABILITY_IE * pHtCapability, IN ADD_HT_INFO_IE * pAddHtInfo,	/* AP might use this additional ht info IE */
		       u8 HtCapabilityLen,
		       u8 AddHtInfoLen,
		       u8 NewExtChanOffset,
		       u8 Channel,
		       char Rssi,
		       IN LARGE_INTEGER TimeStamp,
		       u8 CkipFlag,
		       IN PEDCA_PARM pEdcaParm,
		       IN PQOS_CAPABILITY_PARM pQosCapability,
		       IN PQBSS_LOAD_PARM pQbssLoad,
		       u16 LengthVIE, IN PNDIS_802_11_VARIABLE_IEs pVIE);

void BATableInsertEntry(IN PRTMP_ADAPTER pAd,
			u16 Aid,
			u16 TimeOutValue,
			u16 StartingSeq,
			u8 TID,
			u8 BAWinSize,
			u8 OriginatorStatus, IN BOOLEAN IsRecipient);

void BssTableSsidSort(IN PRTMP_ADAPTER pAd,
		      OUT BSS_TABLE * OutTab, char Ssid[], u8 SsidLen);

void BssTableSortByRssi(IN OUT BSS_TABLE * OutTab);

void BssCipherParse(IN OUT PBSS_ENTRY pBss);

int MlmeQueueInit(IN MLME_QUEUE * Queue);

void MlmeQueueDestroy(IN MLME_QUEUE * Queue);

BOOLEAN MlmeEnqueue(IN PRTMP_ADAPTER pAd,
		    unsigned long Machine,
		    unsigned long MsgType, unsigned long MsgLen, void * Msg);

BOOLEAN MlmeEnqueueForRecv(IN PRTMP_ADAPTER pAd,
			   unsigned long Wcid,
			   unsigned long TimeStampHigh,
			   unsigned long TimeStampLow,
			   u8 Rssi0,
			   u8 Rssi1,
			   u8 Rssi2,
			   unsigned long MsgLen, void *Msg, u8 Signal);

BOOLEAN MlmeDequeue(IN MLME_QUEUE * Queue, OUT MLME_QUEUE_ELEM ** Elem);

void MlmeRestartStateMachine(IN PRTMP_ADAPTER pAd);

BOOLEAN MlmeQueueEmpty(IN MLME_QUEUE * Queue);

BOOLEAN MlmeQueueFull(IN MLME_QUEUE * Queue);

BOOLEAN MsgTypeSubst(IN PRTMP_ADAPTER pAd,
		     IN PFRAME_802_11 pFrame,
		     int * Machine, int * MsgType);

void StateMachineInit(IN STATE_MACHINE * Sm,
		      IN STATE_MACHINE_FUNC Trans[],
		      unsigned long StNr,
		      unsigned long MsgNr,
		      IN STATE_MACHINE_FUNC DefFunc,
		      unsigned long InitState, unsigned long Base);

void StateMachineSetAction(IN STATE_MACHINE * S,
			   unsigned long St, unsigned long Msg, IN STATE_MACHINE_FUNC F);

void StateMachinePerformAction(IN PRTMP_ADAPTER pAd,
			       IN STATE_MACHINE * S, IN MLME_QUEUE_ELEM * Elem);

void Drop(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void AssocStateMachineInit(IN PRTMP_ADAPTER pAd,
			   IN STATE_MACHINE * Sm,
			   OUT STATE_MACHINE_FUNC Trans[]);

void ReassocTimeout(void *SystemSpecific1,
		    void *FunctionContext,
		    void *SystemSpecific2, void *SystemSpecific3);

void AssocTimeout(void *SystemSpecific1,
		  void *FunctionContext,
		  void *SystemSpecific2, void *SystemSpecific3);

void DisassocTimeout(void *SystemSpecific1,
		     void *FunctionContext,
		     void *SystemSpecific2, void *SystemSpecific3);

/*---------------------------------------------- */
void MlmeAssocReqAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void MlmeReassocReqAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void MlmeDisassocReqAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void PeerAssocRspAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void PeerReassocRspAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void PeerDisassocAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void DisassocTimeoutAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void AssocTimeoutAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void ReassocTimeoutAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void Cls3errAction(IN PRTMP_ADAPTER pAd, u8 *pAddr);

void InvalidStateWhenAssoc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void InvalidStateWhenReassoc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void InvalidStateWhenDisassociate(IN PRTMP_ADAPTER pAd,
				  IN MLME_QUEUE_ELEM * Elem);

#ifdef RTMP_MAC_USB
void MlmeCntlConfirm(IN PRTMP_ADAPTER pAd, unsigned long MsgType, u16 Msg);
#endif /* RTMP_MAC_USB // */

void ComposePsPoll(IN PRTMP_ADAPTER pAd);

void ComposeNullFrame(IN PRTMP_ADAPTER pAd);

void AssocPostProc(IN PRTMP_ADAPTER pAd,
		   u8 *pAddr2,
		   u16 CapabilityInfo,
		   u16 Aid,
		   u8 SupRate[],
		   u8 SupRateLen,
		   u8 ExtRate[],
		   u8 ExtRateLen,
		   IN PEDCA_PARM pEdcaParm,
		   IN HT_CAPABILITY_IE * pHtCapability,
		   u8 HtCapabilityLen, IN ADD_HT_INFO_IE * pAddHtInfo);

void AuthStateMachineInit(IN PRTMP_ADAPTER pAd,
			  IN PSTATE_MACHINE sm, OUT STATE_MACHINE_FUNC Trans[]);

void AuthTimeout(void *SystemSpecific1,
		 void *FunctionContext,
		 void *SystemSpecific2, void *SystemSpecific3);

void MlmeAuthReqAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void PeerAuthRspAtSeq2Action(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void PeerAuthRspAtSeq4Action(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void AuthTimeoutAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void Cls2errAction(IN PRTMP_ADAPTER pAd, u8 *pAddr);

void MlmeDeauthReqAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void InvalidStateWhenAuth(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

/*============================================= */

void AuthRspStateMachineInit(IN PRTMP_ADAPTER pAd,
			     IN PSTATE_MACHINE Sm,
			     IN STATE_MACHINE_FUNC Trans[]);

void PeerDeauthAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void PeerAuthSimpleRspGenAndSend(IN PRTMP_ADAPTER pAd,
				 IN PHEADER_802_11 pHdr80211,
				 u16 Alg,
				 u16 Seq,
				 u16 Reason, u16 Status);

/* */
/* Private routines in dls.c */
/* */

/*======================================== */

void SyncStateMachineInit(IN PRTMP_ADAPTER pAd,
			  IN STATE_MACHINE * Sm,
			  OUT STATE_MACHINE_FUNC Trans[]);

void BeaconTimeout(void *SystemSpecific1,
		   void *FunctionContext,
		   void *SystemSpecific2, void *SystemSpecific3);

void ScanTimeout(void *SystemSpecific1,
		 void *FunctionContext,
		 void *SystemSpecific2, void *SystemSpecific3);

void InvalidStateWhenScan(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void InvalidStateWhenJoin(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void InvalidStateWhenStart(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void EnqueueProbeRequest(IN PRTMP_ADAPTER pAd);

BOOLEAN ScanRunning(IN PRTMP_ADAPTER pAd);
/*========================================= */

void MlmeCntlInit(IN PRTMP_ADAPTER pAd,
		  IN STATE_MACHINE * S, OUT STATE_MACHINE_FUNC Trans[]);

void MlmeCntlMachinePerformAction(IN PRTMP_ADAPTER pAd,
				  IN STATE_MACHINE * S,
				  IN MLME_QUEUE_ELEM * Elem);

void CntlIdleProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void CntlOidScanProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void CntlOidSsidProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void CntlOidRTBssidProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void CntlMlmeRoamingProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void CntlWaitDisassocProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void CntlWaitJoinProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void CntlWaitReassocProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void CntlWaitStartProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void CntlWaitAuthProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void CntlWaitAuthProc2(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void CntlWaitAssocProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void LinkUp(IN PRTMP_ADAPTER pAd, u8 BssType);

void LinkDown(IN PRTMP_ADAPTER pAd, IN BOOLEAN IsReqFromAP);

void IterateOnBssTab(IN PRTMP_ADAPTER pAd);

void IterateOnBssTab2(IN PRTMP_ADAPTER pAd);;

void JoinParmFill(IN PRTMP_ADAPTER pAd,
		  IN OUT MLME_JOIN_REQ_STRUCT * JoinReq, unsigned long BssIdx);

void AssocParmFill(IN PRTMP_ADAPTER pAd,
		   IN OUT MLME_ASSOC_REQ_STRUCT * AssocReq,
		   u8 *pAddr,
		   u16 CapabilityInfo,
		   unsigned long Timeout, u16 ListenIntv);

void ScanParmFill(IN PRTMP_ADAPTER pAd,
		  IN OUT MLME_SCAN_REQ_STRUCT * ScanReq,
		  char Ssid[],
		  u8 SsidLen, u8 BssType, u8 ScanType);

void DisassocParmFill(IN PRTMP_ADAPTER pAd,
		      IN OUT MLME_DISASSOC_REQ_STRUCT * DisassocReq,
		      u8 *pAddr, u16 Reason);

void StartParmFill(IN PRTMP_ADAPTER pAd,
		   IN OUT MLME_START_REQ_STRUCT * StartReq,
		   char Ssid[], u8 SsidLen);

void AuthParmFill(IN PRTMP_ADAPTER pAd,
		  IN OUT MLME_AUTH_REQ_STRUCT * AuthReq,
		  u8 *pAddr, u16 Alg);

void EnqueuePsPoll(IN PRTMP_ADAPTER pAd);

void EnqueueBeaconFrame(IN PRTMP_ADAPTER pAd);

void MlmeJoinReqAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void MlmeScanReqAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void MlmeStartReqAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void ScanTimeoutAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void BeaconTimeoutAtJoinAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void PeerBeaconAtScanAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void PeerBeaconAtJoinAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void PeerBeacon(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void PeerProbeReqAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem);

void ScanNextChannel(IN PRTMP_ADAPTER pAd);

unsigned long MakeIbssBeacon(IN PRTMP_ADAPTER pAd);

BOOLEAN MlmeScanReqSanity(IN PRTMP_ADAPTER pAd,
			  void * Msg,
			  unsigned long MsgLen,
			  u8 * BssType,
			  char ssid[],
			  u8 * SsidLen, u8 * ScanType);

BOOLEAN PeerBeaconAndProbeRspSanity(IN PRTMP_ADAPTER pAd,
				    void * Msg,
				    unsigned long MsgLen,
				    u8 MsgChannel,
				    u8 *pAddr2,
				    u8 *pBssid,
				    char Ssid[],
				    u8 * pSsidLen,
				    u8 * pBssType,
				    u16 * pBeaconPeriod,
				    u8 * pChannel,
				    u8 * pNewChannel,
				    OUT LARGE_INTEGER * pTimestamp,
				    OUT CF_PARM * pCfParm,
				    u16 * pAtimWin,
				    u16 * pCapabilityInfo,
				    u8 * pErp,
				    u8 * pDtimCount,
				    u8 * pDtimPeriod,
				    u8 * pBcastFlag,
				    u8 * pMessageToMe,
				    u8 SupRate[],
				    u8 * pSupRateLen,
				    u8 ExtRate[],
				    u8 * pExtRateLen,
				    u8 * pCkipFlag,
				    u8 * pAironetCellPowerLimit,
				    OUT PEDCA_PARM pEdcaParm,
				    OUT PQBSS_LOAD_PARM pQbssLoad,
				    OUT PQOS_CAPABILITY_PARM pQosCapability,
				    unsigned long * pRalinkIe,
				    u8 * pHtCapabilityLen,
				    u8 * pPreNHtCapabilityLen,
				    OUT HT_CAPABILITY_IE * pHtCapability,
				    u8 * AddHtInfoLen,
				    OUT ADD_HT_INFO_IE * AddHtInfo,
				    u8 * NewExtChannel,
				    u16 * LengthVIE,
				    OUT PNDIS_802_11_VARIABLE_IEs pVIE);

BOOLEAN PeerAddBAReqActionSanity(IN PRTMP_ADAPTER pAd,
				 void * pMsg,
				 unsigned long MsgLen, u8 *pAddr2);

BOOLEAN PeerAddBARspActionSanity(IN PRTMP_ADAPTER pAd,
				 void * pMsg, unsigned long MsgLen);

BOOLEAN PeerDelBAActionSanity(IN PRTMP_ADAPTER pAd,
			      u8 Wcid, void * pMsg, unsigned long MsgLen);

BOOLEAN MlmeAssocReqSanity(IN PRTMP_ADAPTER pAd,
			   void * Msg,
			   unsigned long MsgLen,
			   u8 *pApAddr,
			   u16 * CapabilityInfo,
			   unsigned long * Timeout, u16 * ListenIntv);

BOOLEAN MlmeAuthReqSanity(IN PRTMP_ADAPTER pAd,
			  void * Msg,
			  unsigned long MsgLen,
			  u8 *pAddr,
			  unsigned long * Timeout, u16 * Alg);

BOOLEAN MlmeStartReqSanity(IN PRTMP_ADAPTER pAd,
			   void * Msg,
			   unsigned long MsgLen,
			   char Ssid[], u8 * Ssidlen);

BOOLEAN PeerAuthSanity(IN PRTMP_ADAPTER pAd,
		       void * Msg,
		       unsigned long MsgLen,
		       u8 *pAddr,
		       u16 * Alg,
		       u16 * Seq,
		       u16 * Status, char ChlgText[]);

BOOLEAN PeerAssocRspSanity(IN PRTMP_ADAPTER pAd, void * pMsg, unsigned long MsgLen, u8 *pAddr2, u16 * pCapabilityInfo, u16 * pStatus, u16 * pAid, u8 SupRate[], u8 * pSupRateLen, u8 ExtRate[], u8 * pExtRateLen, OUT HT_CAPABILITY_IE * pHtCapability, OUT ADD_HT_INFO_IE * pAddHtInfo,	/* AP might use this additional ht info IE */
			   u8 * pHtCapabilityLen,
			   u8 * pAddHtInfoLen,
			   u8 * pNewExtChannelOffset,
			   OUT PEDCA_PARM pEdcaParm, u8 * pCkipFlag);

BOOLEAN PeerDisassocSanity(IN PRTMP_ADAPTER pAd,
			   void * Msg,
			   unsigned long MsgLen,
			   u8 *pAddr2, u16 * Reason);

BOOLEAN PeerWpaMessageSanity(IN PRTMP_ADAPTER pAd,
			     IN PEAPOL_PACKET pMsg,
			     unsigned long MsgLen,
			     u8 MsgType, IN MAC_TABLE_ENTRY * pEntry);

BOOLEAN PeerDeauthSanity(IN PRTMP_ADAPTER pAd,
			 void * Msg,
			 unsigned long MsgLen,
			 u8 *pAddr2, u16 * Reason);

BOOLEAN PeerProbeReqSanity(IN PRTMP_ADAPTER pAd,
			   void * Msg,
			   unsigned long MsgLen,
			   u8 *pAddr2,
			   char Ssid[], u8 * pSsidLen);

BOOLEAN GetTimBit(char * Ptr,
		  u16 Aid,
		  u8 * TimLen,
		  u8 * BcastFlag,
		  u8 * DtimCount,
		  u8 * DtimPeriod, u8 * MessageToMe);

u8 ChannelSanity(IN PRTMP_ADAPTER pAd, u8 channel);

NDIS_802_11_NETWORK_TYPE NetworkTypeInUseSanity(IN PBSS_ENTRY pBss);

BOOLEAN MlmeDelBAReqSanity(IN PRTMP_ADAPTER pAd,
			   void * Msg, unsigned long MsgLen);

BOOLEAN MlmeAddBAReqSanity(IN PRTMP_ADAPTER pAd,
			   void * Msg, unsigned long MsgLen, u8 *pAddr2);

unsigned long MakeOutgoingFrame(u8 * Buffer, unsigned long * Length, ...);

void LfsrInit(IN PRTMP_ADAPTER pAd, unsigned long Seed);

u8 RandomByte(IN PRTMP_ADAPTER pAd);

void AsicUpdateAutoFallBackTable(IN PRTMP_ADAPTER pAd, u8 *pTxRate);

void MlmePeriodicExec(void *SystemSpecific1,
		      void *FunctionContext,
		      void *SystemSpecific2, void *SystemSpecific3);

void LinkDownExec(void *SystemSpecific1,
		  void *FunctionContext,
		  void *SystemSpecific2, void *SystemSpecific3);

void STAMlmePeriodicExec(PRTMP_ADAPTER pAd);

void MlmeAutoScan(IN PRTMP_ADAPTER pAd);

void MlmeAutoReconnectLastSSID(IN PRTMP_ADAPTER pAd);

BOOLEAN MlmeValidateSSID(u8 *pSsid, u8 SsidLen);

void MlmeCheckForRoaming(IN PRTMP_ADAPTER pAd, unsigned long Now32);

BOOLEAN MlmeCheckForFastRoaming(IN PRTMP_ADAPTER pAd);

void MlmeDynamicTxRateSwitching(IN PRTMP_ADAPTER pAd);

void MlmeSetTxRate(IN PRTMP_ADAPTER pAd,
		   IN PMAC_TABLE_ENTRY pEntry, IN PRTMP_TX_RATE_SWITCH pTxRate);

void MlmeSelectTxRateTable(IN PRTMP_ADAPTER pAd,
			   IN PMAC_TABLE_ENTRY pEntry,
			   u8 ** ppTable,
			   u8 *pTableSize, u8 *pInitTxRateIdx);

void MlmeCalculateChannelQuality(IN PRTMP_ADAPTER pAd,
				 IN PMAC_TABLE_ENTRY pMacEntry, unsigned long Now);

void MlmeCheckPsmChange(IN PRTMP_ADAPTER pAd, unsigned long Now32);

void MlmeSetPsmBit(IN PRTMP_ADAPTER pAd, u16 psm);

void MlmeSetTxPreamble(IN PRTMP_ADAPTER pAd, u16 TxPreamble);

void UpdateBasicRateBitmap(IN PRTMP_ADAPTER pAd);

void MlmeUpdateTxRates(IN PRTMP_ADAPTER pAd,
		       IN BOOLEAN bLinkUp, u8 apidx);

void MlmeUpdateHtTxRates(IN PRTMP_ADAPTER pAd, u8 apidx);

void RTMPCheckRates(IN PRTMP_ADAPTER pAd,
		    IN u8 SupRate[], IN u8 * SupRateLen);

BOOLEAN RTMPCheckChannel(IN PRTMP_ADAPTER pAd,
			 u8 CentralChannel, u8 Channel);

BOOLEAN RTMPCheckHt(IN PRTMP_ADAPTER pAd,
		    u8 Wcid,
		    IN OUT HT_CAPABILITY_IE * pHtCapability,
		    IN OUT ADD_HT_INFO_IE * pAddHtInfo);

void StaQuickResponeForRateUpExec(void *SystemSpecific1,
				  void *FunctionContext,
				  void *SystemSpecific2,
				  void *SystemSpecific3);

void RTMPUpdateMlmeRate(IN PRTMP_ADAPTER pAd);

char RTMPMaxRssi(IN PRTMP_ADAPTER pAd,
		 char Rssi0, char Rssi1, char Rssi2);

#ifdef RT30xx
void AsicSetRxAnt(IN PRTMP_ADAPTER pAd, u8 Ant);

void RTMPFilterCalibration(IN PRTMP_ADAPTER pAd);

#ifdef RTMP_EFUSE_SUPPORT
/*2008/09/11:KH add to support efuse<-- */
int set_eFuseGetFreeBlockCount_Proc(IN PRTMP_ADAPTER pAd, char *arg);

int set_eFusedump_Proc(IN PRTMP_ADAPTER pAd, char *arg);

void eFusePhysicalReadRegisters(IN PRTMP_ADAPTER pAd,
				u16 Offset,
				u16 Length, u16 * pData);

int RtmpEfuseSupportCheck(IN RTMP_ADAPTER * pAd);

void eFuseGetFreeBlockCount(IN PRTMP_ADAPTER pAd, u32 *EfuseFreeBlock);

int eFuse_init(IN PRTMP_ADAPTER pAd);
/*2008/09/11:KH add to support efuse--> */
#endif /* RTMP_EFUSE_SUPPORT // */

/* add by johnli, RF power sequence setup */
void RT30xxLoadRFNormalModeSetup(IN PRTMP_ADAPTER pAd);

void RT30xxLoadRFSleepModeSetup(IN PRTMP_ADAPTER pAd);

void RT30xxReverseRFSleepModeSetup(IN PRTMP_ADAPTER pAd);
/* end johnli */

#ifdef RT3070
void NICInitRT3070RFRegisters(IN RTMP_ADAPTER * pAd);
#endif /* RT3070 // */
#ifdef RT3090
void NICInitRT3090RFRegisters(IN RTMP_ADAPTER * pAd);
#endif /* RT3090 // */

void RT30xxHaltAction(IN PRTMP_ADAPTER pAd);

void RT30xxSetRxAnt(IN PRTMP_ADAPTER pAd, u8 Ant);
#endif /* RT30xx // */

void AsicEvaluateRxAnt(IN PRTMP_ADAPTER pAd);

void AsicRxAntEvalTimeout(void *SystemSpecific1,
			  void *FunctionContext,
			  void *SystemSpecific2, void *SystemSpecific3);

void APSDPeriodicExec(void *SystemSpecific1,
		      void *FunctionContext,
		      void *SystemSpecific2, void *SystemSpecific3);

BOOLEAN RTMPCheckEntryEnableAutoRateSwitch(IN PRTMP_ADAPTER pAd,
					   IN PMAC_TABLE_ENTRY pEntry);

u8 RTMPStaFixedTxMode(IN PRTMP_ADAPTER pAd, IN PMAC_TABLE_ENTRY pEntry);

void RTMPUpdateLegacyTxSetting(u8 fixed_tx_mode, PMAC_TABLE_ENTRY pEntry);

BOOLEAN RTMPAutoRateSwitchCheck(IN PRTMP_ADAPTER pAd);

int MlmeInit(IN PRTMP_ADAPTER pAd);

void MlmeHandler(IN PRTMP_ADAPTER pAd);

void MlmeHalt(IN PRTMP_ADAPTER pAd);

void MlmeResetRalinkCounters(IN PRTMP_ADAPTER pAd);

void BuildChannelList(IN PRTMP_ADAPTER pAd);

u8 FirstChannel(IN PRTMP_ADAPTER pAd);

u8 NextChannel(IN PRTMP_ADAPTER pAd, u8 channel);

void ChangeToCellPowerLimit(IN PRTMP_ADAPTER pAd,
			    u8 AironetCellPowerLimit);

/* */
/* Prototypes of function definition in rtmp_tkip.c */
/* */
void RTMPInitTkipEngine(IN PRTMP_ADAPTER pAd,
			u8 *pTKey,
			u8 KeyId,
			u8 *pTA,
			u8 *pMICKey,
			u8 *pTSC, unsigned long *pIV16, unsigned long *pIV32);

void RTMPInitMICEngine(IN PRTMP_ADAPTER pAd,
		       u8 *pKey,
		       u8 *pDA,
		       u8 *pSA, u8 UserPriority, u8 *pMICKey);

BOOLEAN RTMPTkipCompareMICValue(IN PRTMP_ADAPTER pAd,
				u8 *pSrc,
				u8 *pDA,
				u8 *pSA,
				u8 *pMICKey,
				u8 UserPriority, u32 Len);

void RTMPCalculateMICValue(IN PRTMP_ADAPTER pAd,
			   IN PNDIS_PACKET pPacket,
			   u8 *pEncap,
			   IN PCIPHER_KEY pKey, u8 apidx);

void RTMPTkipAppendByte(IN PTKIP_KEY_INFO pTkip, u8 uChar);

void RTMPTkipAppend(IN PTKIP_KEY_INFO pTkip, u8 *pSrc, u32 nBytes);

void RTMPTkipGetMIC(IN PTKIP_KEY_INFO pTkip);

BOOLEAN RTMPSoftDecryptTKIP(IN PRTMP_ADAPTER pAd,
			    u8 *pData,
			    unsigned long DataByteCnt,
			    u8 UserPriority, IN PCIPHER_KEY pWpaKey);

BOOLEAN RTMPSoftDecryptAES(IN PRTMP_ADAPTER pAd,
			   u8 *pData,
			   unsigned long DataByteCnt, IN PCIPHER_KEY pWpaKey);

/* */
/* Prototypes of function definition in cmm_info.c */
/* */
int RT_CfgSetCountryRegion(IN PRTMP_ADAPTER pAd, char *arg, int band);

int RT_CfgSetWirelessMode(IN PRTMP_ADAPTER pAd, char *arg);

int RT_CfgSetShortSlot(IN PRTMP_ADAPTER pAd, char *arg);

int RT_CfgSetWepKey(IN PRTMP_ADAPTER pAd,
		    char *keyString,
		    IN CIPHER_KEY * pSharedKey, int keyIdx);

int RT_CfgSetWPAPSKKey(IN RTMP_ADAPTER * pAd,
		       char *keyString,
		       u8 * pHashStr,
		       int hashStrLen, u8 *pPMKBuf);

/* */
/* Prototypes of function definition in cmm_info.c */
/* */
void RTMPWPARemoveAllKeys(IN PRTMP_ADAPTER pAd);

void RTMPSetPhyMode(IN PRTMP_ADAPTER pAd, unsigned long phymode);

void RTMPUpdateHTIE(IN RT_HT_CAPABILITY * pRtHt,
		    u8 * pMcsSet,
		    OUT HT_CAPABILITY_IE * pHtCapability,
		    OUT ADD_HT_INFO_IE * pAddHtInfo);

void RTMPAddWcidAttributeEntry(IN PRTMP_ADAPTER pAd,
			       u8 BssIdx,
			       u8 KeyIdx,
			       u8 CipherAlg, IN MAC_TABLE_ENTRY * pEntry);

char *GetEncryptType(char enc);

char *GetAuthMode(char auth);

void RTMPSetHT(IN PRTMP_ADAPTER pAd, IN OID_SET_HT_PHYMODE * pHTPhyMode);

void RTMPSetIndividualHT(IN PRTMP_ADAPTER pAd, u8 apidx);

void RTMPSendWirelessEvent(IN PRTMP_ADAPTER pAd,
			   u16 Event_flag,
			   u8 *pAddr, u8 BssIdx, char Rssi);

char ConvertToRssi(IN PRTMP_ADAPTER pAd, char Rssi, u8 RssiNumber);

/*===================================
	Function prototype in cmm_wpa.c
  =================================== */
void RTMPToWirelessSta(IN PRTMP_ADAPTER pAd,
		       IN PMAC_TABLE_ENTRY pEntry,
		       u8 *pHeader802_3,
		       u32 HdrLen,
		       u8 *pData,
		       u32 DataLen, IN BOOLEAN bClearFrame);

void WpaDerivePTK(IN PRTMP_ADAPTER pAd,
		  u8 * PMK,
		  u8 * ANonce,
		  u8 * AA,
		  u8 * SNonce,
		  u8 * SA, u8 * output, u32 len);

void GenRandom(IN PRTMP_ADAPTER pAd, u8 * macAddr, u8 * random);

BOOLEAN RTMPCheckWPAframe(IN PRTMP_ADAPTER pAd,
			  IN PMAC_TABLE_ENTRY pEntry,
			  u8 *pData,
			  unsigned long DataByteCount, u8 FromWhichBSSID);

void AES_GTK_KEY_UNWRAP(u8 * key,
			u8 * plaintext,
			u32 c_len, u8 * ciphertext);

BOOLEAN RTMPParseEapolKeyData(IN PRTMP_ADAPTER pAd,
			      u8 *pKeyData,
			      u8 KeyDataLen,
			      u8 GroupKeyIndex,
			      u8 MsgType,
			      IN BOOLEAN bWPA2, IN MAC_TABLE_ENTRY * pEntry);

void ConstructEapolMsg(IN PMAC_TABLE_ENTRY pEntry,
		       u8 GroupKeyWepStatus,
		       u8 MsgType,
		       u8 DefaultKeyIdx,
		       u8 * KeyNonce,
		       u8 * TxRSC,
		       u8 * GTK,
		       u8 * RSNIE,
		       u8 RSNIE_Len, OUT PEAPOL_PACKET pMsg);

int RTMPSoftDecryptBroadCastData(IN PRTMP_ADAPTER pAd,
					 IN RX_BLK * pRxBlk,
					 IN NDIS_802_11_ENCRYPTION_STATUS
					 GroupCipher,
					 IN PCIPHER_KEY pShard_key);

void RTMPMakeRSNIE(IN PRTMP_ADAPTER pAd,
		   u32 AuthMode, u32 WepStatus, u8 apidx);

/* */
/* function prototype in ap_wpa.c */
/* */
void RTMPGetTxTscFromAsic(IN PRTMP_ADAPTER pAd,
			  u8 apidx, u8 *pTxTsc);

void APInstallPairwiseKey(PRTMP_ADAPTER pAd, PMAC_TABLE_ENTRY pEntry);

u32 APValidateRSNIE(IN PRTMP_ADAPTER pAd,
		     IN PMAC_TABLE_ENTRY pEntry,
		     u8 *pRsnIe, u8 rsnie_len);

void HandleCounterMeasure(IN PRTMP_ADAPTER pAd, IN MAC_TABLE_ENTRY * pEntry);

void WPAStart4WayHS(IN PRTMP_ADAPTER pAd,
		    IN MAC_TABLE_ENTRY * pEntry, unsigned long TimeInterval);

void WPAStart2WayGroupHS(IN PRTMP_ADAPTER pAd, IN MAC_TABLE_ENTRY * pEntry);

void PeerPairMsg1Action(IN PRTMP_ADAPTER pAd,
			IN MAC_TABLE_ENTRY * pEntry, IN MLME_QUEUE_ELEM * Elem);

void PeerPairMsg2Action(IN PRTMP_ADAPTER pAd,
			IN MAC_TABLE_ENTRY * pEntry, IN MLME_QUEUE_ELEM * Elem);

void PeerPairMsg3Action(IN PRTMP_ADAPTER pAd,
			IN MAC_TABLE_ENTRY * pEntry, IN MLME_QUEUE_ELEM * Elem);

void PeerPairMsg4Action(IN PRTMP_ADAPTER pAd,
			IN MAC_TABLE_ENTRY * pEntry, IN MLME_QUEUE_ELEM * Elem);

void PeerGroupMsg1Action(IN PRTMP_ADAPTER pAd,
			 IN PMAC_TABLE_ENTRY pEntry, IN MLME_QUEUE_ELEM * Elem);

void PeerGroupMsg2Action(IN PRTMP_ADAPTER pAd,
			 IN PMAC_TABLE_ENTRY pEntry,
			 void * Msg, u32 MsgLen);

void WpaDeriveGTK(u8 * PMK,
		  u8 * GNonce,
		  u8 * AA, u8 * output, u32 len);

void AES_GTK_KEY_WRAP(u8 * key,
		      u8 * plaintext,
		      u32 p_len, u8 * ciphertext);

/*typedef void (*TIMER_FUNCTION)(unsigned long); */

/* timeout -- ms */
void RTMP_SetPeriodicTimer(IN NDIS_MINIPORT_TIMER * pTimer,
			   IN unsigned long timeout);

void RTMP_OS_Init_Timer(IN PRTMP_ADAPTER pAd,
			IN NDIS_MINIPORT_TIMER * pTimer,
			IN TIMER_FUNCTION function, void *data);

void RTMP_OS_Add_Timer(IN NDIS_MINIPORT_TIMER * pTimer,
		       IN unsigned long timeout);

void RTMP_OS_Mod_Timer(IN NDIS_MINIPORT_TIMER * pTimer,
		       IN unsigned long timeout);

void RTMP_OS_Del_Timer(IN NDIS_MINIPORT_TIMER * pTimer,
		       OUT BOOLEAN * pCancelled);

void RTMP_OS_Release_Packet(IN PRTMP_ADAPTER pAd, IN PQUEUE_ENTRY pEntry);

void RTMPusecDelay(unsigned long usec);

int os_alloc_mem(IN RTMP_ADAPTER * pAd,
			 u8 ** mem, unsigned long size);

int os_free_mem(IN PRTMP_ADAPTER pAd, void *mem);

void RTMP_AllocateSharedMemory(IN PRTMP_ADAPTER pAd,
			       unsigned long Length,
			       IN BOOLEAN Cached,
			       void ** VirtualAddress,
			       OUT PNDIS_PHYSICAL_ADDRESS PhysicalAddress);

void RTMPFreeTxRxRingMemory(IN PRTMP_ADAPTER pAd);

int AdapterBlockAllocateMemory(void *handle, void ** ppAd);

void RTMP_AllocateTxDescMemory(IN PRTMP_ADAPTER pAd,
			       u32 Index,
			       unsigned long Length,
			       IN BOOLEAN Cached,
			       void ** VirtualAddress,
			       OUT PNDIS_PHYSICAL_ADDRESS PhysicalAddress);

void RTMP_AllocateFirstTxBuffer(IN PRTMP_ADAPTER pAd,
				u32 Index,
				unsigned long Length,
				IN BOOLEAN Cached,
				void ** VirtualAddress,
				OUT PNDIS_PHYSICAL_ADDRESS PhysicalAddress);

void RTMP_FreeFirstTxBuffer(IN PRTMP_ADAPTER pAd,
			    unsigned long Length,
			    IN BOOLEAN Cached,
			    void *VirtualAddress,
			    IN NDIS_PHYSICAL_ADDRESS PhysicalAddress);

void RTMP_AllocateMgmtDescMemory(IN PRTMP_ADAPTER pAd,
				 unsigned long Length,
				 IN BOOLEAN Cached,
				 void ** VirtualAddress,
				 OUT PNDIS_PHYSICAL_ADDRESS PhysicalAddress);

void RTMP_AllocateRxDescMemory(IN PRTMP_ADAPTER pAd,
			       unsigned long Length,
			       IN BOOLEAN Cached,
			       void ** VirtualAddress,
			       OUT PNDIS_PHYSICAL_ADDRESS PhysicalAddress);

void RTMP_FreeDescMemory(IN PRTMP_ADAPTER pAd,
			 unsigned long Length,
			 void *VirtualAddress,
			 IN NDIS_PHYSICAL_ADDRESS PhysicalAddress);

PNDIS_PACKET RtmpOSNetPktAlloc(IN RTMP_ADAPTER * pAd, IN int size);

PNDIS_PACKET RTMP_AllocateRxPacketBuffer(IN PRTMP_ADAPTER pAd,
					 unsigned long Length,
					 IN BOOLEAN Cached,
					 void ** VirtualAddress,
					 OUT PNDIS_PHYSICAL_ADDRESS
					 PhysicalAddress);

PNDIS_PACKET RTMP_AllocateTxPacketBuffer(IN PRTMP_ADAPTER pAd,
					 unsigned long Length,
					 IN BOOLEAN Cached,
					 void ** VirtualAddress);

PNDIS_PACKET RTMP_AllocateFragPacketBuffer(IN PRTMP_ADAPTER pAd,
					   unsigned long Length);

void RTMP_QueryPacketInfo(IN PNDIS_PACKET pPacket,
			  OUT PACKET_INFO * pPacketInfo,
			  u8 ** pSrcBufVA, u32 * pSrcBufLen);

void RTMP_QueryNextPacketInfo(IN PNDIS_PACKET * ppPacket,
			      OUT PACKET_INFO * pPacketInfo,
			      u8 ** pSrcBufVA, u32 * pSrcBufLen);

BOOLEAN RTMP_FillTxBlkInfo(IN RTMP_ADAPTER * pAd, IN TX_BLK * pTxBlk);

PRTMP_SCATTER_GATHER_LIST
rt_get_sg_list_from_packet(PNDIS_PACKET pPacket, RTMP_SCATTER_GATHER_LIST * sg);

void announce_802_3_packet(IN PRTMP_ADAPTER pAd, IN PNDIS_PACKET pPacket);

u32 BA_Reorder_AMSDU_Annnounce(IN PRTMP_ADAPTER pAd, IN PNDIS_PACKET pPacket);

PNET_DEV get_netdev_from_bssid(IN PRTMP_ADAPTER pAd, u8 FromWhichBSSID);

PNDIS_PACKET duplicate_pkt(IN PRTMP_ADAPTER pAd,
			   u8 *pHeader802_3,
			   u32 HdrLen,
			   u8 *pData,
			   unsigned long DataSize, u8 FromWhichBSSID);

PNDIS_PACKET duplicate_pkt_with_TKIP_MIC(IN PRTMP_ADAPTER pAd,
					 IN PNDIS_PACKET pOldPkt);

void ba_flush_reordering_timeout_mpdus(IN PRTMP_ADAPTER pAd,
				       IN PBA_REC_ENTRY pBAEntry,
				       unsigned long Now32);

void BAOriSessionSetUp(IN PRTMP_ADAPTER pAd,
		       IN MAC_TABLE_ENTRY * pEntry,
		       u8 TID,
		       u16 TimeOut,
		       unsigned long DelayTime, IN BOOLEAN isForced);

void BASessionTearDownALL(IN OUT PRTMP_ADAPTER pAd, u8 Wcid);

BOOLEAN OS_Need_Clone_Packet(void);

void build_tx_packet(IN PRTMP_ADAPTER pAd,
		     IN PNDIS_PACKET pPacket,
		     u8 *pFrame, unsigned long FrameLen);

void BAOriSessionTearDown(IN OUT PRTMP_ADAPTER pAd,
			  u8 Wcid,
			  u8 TID,
			  IN BOOLEAN bPassive, IN BOOLEAN bForceSend);

void BARecSessionTearDown(IN OUT PRTMP_ADAPTER pAd,
			  u8 Wcid, u8 TID, IN BOOLEAN bPassive);

BOOLEAN ba_reordering_resource_init(PRTMP_ADAPTER pAd, int num);
void ba_reordering_resource_release(PRTMP_ADAPTER pAd);

char *rstrtok(char *s, IN const char *ct);

/*//////// common ioctl functions ////////// */
int SetCommonHT(IN PRTMP_ADAPTER pAd);

int WpaCheckEapCode(IN PRTMP_ADAPTER pAd,
		    u8 *pFrame, u16 FrameLen, u16 OffSet);

void WpaSendMicFailureToWpaSupplicant(IN PRTMP_ADAPTER pAd,
				      IN BOOLEAN bUnicast);

int wext_notify_event_assoc(IN RTMP_ADAPTER * pAd);

BOOLEAN STARxDoneInterruptHandle(IN PRTMP_ADAPTER pAd, IN BOOLEAN argc);

/* AMPDU packet indication */
void Indicate_AMPDU_Packet(IN PRTMP_ADAPTER pAd,
			   IN RX_BLK * pRxBlk, u8 FromWhichBSSID);

/* AMSDU packet indication */
void Indicate_AMSDU_Packet(IN PRTMP_ADAPTER pAd,
			   IN RX_BLK * pRxBlk, u8 FromWhichBSSID);

/* Normal legacy Rx packet indication */
void Indicate_Legacy_Packet(IN PRTMP_ADAPTER pAd,
			    IN RX_BLK * pRxBlk, u8 FromWhichBSSID);

void Indicate_EAPOL_Packet(IN PRTMP_ADAPTER pAd,
			   IN RX_BLK * pRxBlk, u8 FromWhichBSSID);

void update_os_packet_info(IN PRTMP_ADAPTER pAd,
			   IN RX_BLK * pRxBlk, u8 FromWhichBSSID);

void wlan_802_11_to_802_3_packet(IN PRTMP_ADAPTER pAd,
				 IN RX_BLK * pRxBlk,
				 u8 *pHeader802_3,
				 u8 FromWhichBSSID);

/* remove LLC and get 802_3 Header */
#define  RTMP_802_11_REMOVE_LLC_AND_CONVERT_TO_802_3(_pRxBlk, _pHeader802_3)	\
{																				\
	u8 *_pRemovedLLCSNAP = NULL, *_pDA, *_pSA;                                 \
																				\
	if (RX_BLK_TEST_FLAG(_pRxBlk, fRX_MESH))                                    \
	{                                                                           \
		_pDA = _pRxBlk->pHeader->Addr3;                                         \
		_pSA = (u8 *)_pRxBlk->pHeader + sizeof(HEADER_802_11);                \
	}                                                                           \
	else                                                                        \
	{                                                                           \
		if (RX_BLK_TEST_FLAG(_pRxBlk, fRX_INFRA))                              	\
		{                                                                       \
			_pDA = _pRxBlk->pHeader->Addr1;                                     \
		if (RX_BLK_TEST_FLAG(_pRxBlk, fRX_DLS))									\
			_pSA = _pRxBlk->pHeader->Addr2;										\
		else																	\
			_pSA = _pRxBlk->pHeader->Addr3;                                     \
		}                                                                       \
		else                                                                    \
		{                                                                       \
			_pDA = _pRxBlk->pHeader->Addr1;                                     \
			_pSA = _pRxBlk->pHeader->Addr2;                                     \
		}                                                                       \
	}                                                                           \
																				\
	CONVERT_TO_802_3(_pHeader802_3, _pDA, _pSA, _pRxBlk->pData, 				\
		_pRxBlk->DataSize, _pRemovedLLCSNAP);                                   \
}

void Sta_Announce_or_Forward_802_3_Packet(IN PRTMP_ADAPTER pAd,
					  IN PNDIS_PACKET pPacket,
					  u8 FromWhichBSSID);

#define ANNOUNCE_OR_FORWARD_802_3_PACKET(_pAd, _pPacket, _FromWhichBSS)\
			Sta_Announce_or_Forward_802_3_Packet(_pAd, _pPacket, _FromWhichBSS);
			/*announce_802_3_packet(_pAd, _pPacket); */

PNDIS_PACKET DuplicatePacket(IN PRTMP_ADAPTER pAd,
			     IN PNDIS_PACKET pPacket, u8 FromWhichBSSID);

PNDIS_PACKET ClonePacket(IN PRTMP_ADAPTER pAd,
			 IN PNDIS_PACKET pPacket,
			 u8 *pData, unsigned long DataSize);

/* Normal, AMPDU or AMSDU */
void CmmRxnonRalinkFrameIndicate(IN PRTMP_ADAPTER pAd,
				 IN RX_BLK * pRxBlk, u8 FromWhichBSSID);

void CmmRxRalinkFrameIndicate(IN PRTMP_ADAPTER pAd,
			      IN MAC_TABLE_ENTRY * pEntry,
			      IN RX_BLK * pRxBlk, u8 FromWhichBSSID);

void Update_Rssi_Sample(IN PRTMP_ADAPTER pAd,
			IN RSSI_SAMPLE * pRssi, IN PRXWI_STRUC pRxWI);

PNDIS_PACKET GetPacketFromRxRing(IN PRTMP_ADAPTER pAd,
				 OUT PRT28XX_RXD_STRUC pSaveRxD,
				 OUT BOOLEAN * pbReschedule,
				 IN u32 * pRxPending);

PNDIS_PACKET RTMPDeFragmentDataFrame(IN PRTMP_ADAPTER pAd, IN RX_BLK * pRxBlk);

enum {
	DIDmsg_lnxind_wlansniffrm = 0x00000044,
	DIDmsg_lnxind_wlansniffrm_hosttime = 0x00010044,
	DIDmsg_lnxind_wlansniffrm_mactime = 0x00020044,
	DIDmsg_lnxind_wlansniffrm_channel = 0x00030044,
	DIDmsg_lnxind_wlansniffrm_rssi = 0x00040044,
	DIDmsg_lnxind_wlansniffrm_sq = 0x00050044,
	DIDmsg_lnxind_wlansniffrm_signal = 0x00060044,
	DIDmsg_lnxind_wlansniffrm_noise = 0x00070044,
	DIDmsg_lnxind_wlansniffrm_rate = 0x00080044,
	DIDmsg_lnxind_wlansniffrm_istx = 0x00090044,
	DIDmsg_lnxind_wlansniffrm_frmlen = 0x000A0044
};
enum {
	P80211ENUM_msgitem_status_no_value = 0x00
};
enum {
	P80211ENUM_truth_false = 0x00,
	P80211ENUM_truth_true = 0x01
};

/* Definition from madwifi */
typedef struct {
	u32 did;
	u16 status;
	u16 len;
	u32 data;
} p80211item_uint32_t;

typedef struct {
	u32 msgcode;
	u32 msglen;
#define WLAN_DEVNAMELEN_MAX 16
	u8 devname[WLAN_DEVNAMELEN_MAX];
	p80211item_uint32_t hosttime;
	p80211item_uint32_t mactime;
	p80211item_uint32_t channel;
	p80211item_uint32_t rssi;
	p80211item_uint32_t sq;
	p80211item_uint32_t signal;
	p80211item_uint32_t noise;
	p80211item_uint32_t rate;
	p80211item_uint32_t istx;
	p80211item_uint32_t frmlen;
} wlan_ng_prism2_header;

/* The radio capture header precedes the 802.11 header. */
typedef struct PACKED _ieee80211_radiotap_header {
	u8 it_version;	/* Version 0. Only increases
				 * for drastic changes,
				 * introduction of compatible
				 * new fields does not count.
				 */
	u8 it_pad;
	u16 it_len;		/* length of the whole
				 * header in bytes, including
				 * it_version, it_pad,
				 * it_len, and data fields.
				 */
	u32 it_present;	/* A bitmap telling which
				 * fields are present. Set bit 31
				 * (0x80000000) to extend the
				 * bitmap by another 32 bits.
				 * Additional extensions are made
				 * by setting bit 31.
				 */
} ieee80211_radiotap_header;

enum ieee80211_radiotap_type {
	IEEE80211_RADIOTAP_TSFT = 0,
	IEEE80211_RADIOTAP_FLAGS = 1,
	IEEE80211_RADIOTAP_RATE = 2,
	IEEE80211_RADIOTAP_CHANNEL = 3,
	IEEE80211_RADIOTAP_FHSS = 4,
	IEEE80211_RADIOTAP_DBM_ANTSIGNAL = 5,
	IEEE80211_RADIOTAP_DBM_ANTNOISE = 6,
	IEEE80211_RADIOTAP_LOCK_QUALITY = 7,
	IEEE80211_RADIOTAP_TX_ATTENUATION = 8,
	IEEE80211_RADIOTAP_DB_TX_ATTENUATION = 9,
	IEEE80211_RADIOTAP_DBM_TX_POWER = 10,
	IEEE80211_RADIOTAP_ANTENNA = 11,
	IEEE80211_RADIOTAP_DB_ANTSIGNAL = 12,
	IEEE80211_RADIOTAP_DB_ANTNOISE = 13
};

#define WLAN_RADIOTAP_PRESENT (			\
	(1 << IEEE80211_RADIOTAP_TSFT)	|	\
	(1 << IEEE80211_RADIOTAP_FLAGS) |	\
	(1 << IEEE80211_RADIOTAP_RATE)  | 	\
	 0)

typedef struct _wlan_radiotap_header {
	ieee80211_radiotap_header wt_ihdr;
	long long wt_tsft;
	u8 wt_flags;
	u8 wt_rate;
} wlan_radiotap_header;
/* Definition from madwifi */

void send_monitor_packets(IN PRTMP_ADAPTER pAd, IN RX_BLK * pRxBlk);

void RTMPSetDesiredRates(IN PRTMP_ADAPTER pAdapter, long Rates);

int Set_FixedTxMode_Proc(IN PRTMP_ADAPTER pAd, char *arg);

BOOLEAN RT28XXChipsetCheck(IN void *_dev_p);

void RT28XXDMADisable(IN RTMP_ADAPTER * pAd);

void RT28XXDMAEnable(IN RTMP_ADAPTER * pAd);

void RT28xx_UpdateBeaconToAsic(IN RTMP_ADAPTER * pAd,
			       int apidx,
			       unsigned long BeaconLen, unsigned long UpdatePos);

int rt28xx_init(IN PRTMP_ADAPTER pAd,
		char *pDefaultMac, char *pHostName);

int RtmpNetTaskInit(IN RTMP_ADAPTER * pAd);

void RtmpNetTaskExit(IN PRTMP_ADAPTER pAd);

int RtmpMgmtTaskInit(IN RTMP_ADAPTER * pAd);

void RtmpMgmtTaskExit(IN RTMP_ADAPTER * pAd);

void tbtt_tasklet(unsigned long data);

PNET_DEV RtmpPhyNetDevInit(IN RTMP_ADAPTER * pAd,
			   IN RTMP_OS_NETDEV_OP_HOOK * pNetHook);

BOOLEAN RtmpPhyNetDevExit(IN RTMP_ADAPTER * pAd, IN PNET_DEV net_dev);

int RtmpRaDevCtrlInit(IN RTMP_ADAPTER * pAd, IN RTMP_INF_TYPE infType);

BOOLEAN RtmpRaDevCtrlExit(IN RTMP_ADAPTER * pAd);

#ifdef RTMP_MAC_PCI
/* */
/* Function Prototype in cmm_data_pci.c */
/* */
u16 RtmpPCI_WriteTxResource(IN PRTMP_ADAPTER pAd,
			       IN TX_BLK * pTxBlk,
			       IN BOOLEAN bIsLast, u16 * FreeNumber);

u16 RtmpPCI_WriteSingleTxResource(IN PRTMP_ADAPTER pAd,
				     IN TX_BLK * pTxBlk,
				     IN BOOLEAN bIsLast,
				     u16 * FreeNumber);

u16 RtmpPCI_WriteMultiTxResource(IN PRTMP_ADAPTER pAd,
				    IN TX_BLK * pTxBlk,
				    u8 frameNum, u16 * FreeNumber);

u16 RtmpPCI_WriteFragTxResource(IN PRTMP_ADAPTER pAd,
				   IN TX_BLK * pTxBlk,
				   u8 fragNum, u16 * FreeNumber);

u16 RtmpPCI_WriteSubTxResource(IN PRTMP_ADAPTER pAd,
				  IN TX_BLK * pTxBlk,
				  IN BOOLEAN bIsLast, u16 * FreeNumber);

void RtmpPCI_FinalWriteTxResource(IN PRTMP_ADAPTER pAd,
				  IN TX_BLK * pTxBlk,
				  u16 totalMPDUSize,
				  u16 FirstTxIdx);

void RtmpPCIDataLastTxIdx(IN PRTMP_ADAPTER pAd,
			  u8 QueIdx, u16 LastTxIdx);

void RtmpPCIDataKickOut(IN PRTMP_ADAPTER pAd,
			IN TX_BLK * pTxBlk, u8 QueIdx);

int RtmpPCIMgmtKickOut(IN RTMP_ADAPTER * pAd,
		       u8 QueIdx,
		       IN PNDIS_PACKET pPacket,
		       u8 *pSrcBufVA, u32 SrcBufLen);

int RTMPCheckRxError(IN PRTMP_ADAPTER pAd,
			     IN PHEADER_802_11 pHeader,
			     IN PRXWI_STRUC pRxWI, IN PRT28XX_RXD_STRUC pRxD);

BOOLEAN RT28xxPciAsicRadioOff(IN PRTMP_ADAPTER pAd,
			      u8 Level, u16 TbttNumToNextWakeUp);

BOOLEAN RT28xxPciAsicRadioOn(IN PRTMP_ADAPTER pAd, u8 Level);

void RTMPInitPCIeLinkCtrlValue(IN PRTMP_ADAPTER pAd);

void RTMPFindHostPCIDev(IN PRTMP_ADAPTER pAd);

void RTMPPCIeLinkCtrlValueRestore(IN PRTMP_ADAPTER pAd, u8 Level);

void RTMPPCIeLinkCtrlSetting(IN PRTMP_ADAPTER pAd, u16 Max);

void RTMPrt3xSetPCIePowerLinkCtrl(IN PRTMP_ADAPTER pAd);

void PsPollWakeExec(void *SystemSpecific1,
		    void *FunctionContext,
		    void *SystemSpecific2, void *SystemSpecific3);

void RadioOnExec(void *SystemSpecific1,
		 void *FunctionContext,
		 void *SystemSpecific2, void *SystemSpecific3);

void RT28xxPciStaAsicForceWakeup(IN PRTMP_ADAPTER pAd, IN BOOLEAN bFromTx);

void RT28xxPciStaAsicSleepThenAutoWakeup(IN PRTMP_ADAPTER pAd,
					 u16 TbttNumToNextWakeUp);

void RT28xxPciMlmeRadioOn(IN PRTMP_ADAPTER pAd);

void RT28xxPciMlmeRadioOFF(IN PRTMP_ADAPTER pAd);
#endif /* RTMP_MAC_PCI // */

#ifdef RTMP_MAC_USB
/* */
/* Function Prototype in rtusb_bulk.c */
/* */
void RTUSBInitTxDesc(IN PRTMP_ADAPTER pAd,
		     IN PTX_CONTEXT pTxContext,
		     u8 BulkOutPipeId, IN usb_complete_t Func);

void RTUSBInitHTTxDesc(IN PRTMP_ADAPTER pAd,
		       IN PHT_TX_CONTEXT pTxContext,
		       u8 BulkOutPipeId,
		       unsigned long BulkOutSize, IN usb_complete_t Func);

void RTUSBInitRxDesc(IN PRTMP_ADAPTER pAd, IN PRX_CONTEXT pRxContext);

void RTUSBCleanUpDataBulkOutQueue(IN PRTMP_ADAPTER pAd);

void RTUSBCancelPendingBulkOutIRP(IN PRTMP_ADAPTER pAd);

void RTUSBBulkOutDataPacket(IN PRTMP_ADAPTER pAd,
			    u8 BulkOutPipeId, u8 Index);

void RTUSBBulkOutNullFrame(IN PRTMP_ADAPTER pAd);

void RTUSBBulkOutRTSFrame(IN PRTMP_ADAPTER pAd);

void RTUSBCancelPendingBulkInIRP(IN PRTMP_ADAPTER pAd);

void RTUSBCancelPendingIRPs(IN PRTMP_ADAPTER pAd);

void RTUSBBulkOutMLMEPacket(IN PRTMP_ADAPTER pAd, u8 Index);

void RTUSBBulkOutPsPoll(IN PRTMP_ADAPTER pAd);

void RTUSBCleanUpMLMEBulkOutQueue(IN PRTMP_ADAPTER pAd);

void RTUSBKickBulkOut(IN PRTMP_ADAPTER pAd);

void RTUSBBulkReceive(IN PRTMP_ADAPTER pAd);

void DoBulkIn(IN RTMP_ADAPTER * pAd);

void RTUSBInitRxDesc(IN PRTMP_ADAPTER pAd, IN PRX_CONTEXT pRxContext);

void RTUSBBulkRxHandle(IN unsigned long data);

/* */
/* Function Prototype in rtusb_io.c */
/* */
int RTUSBMultiRead(IN PRTMP_ADAPTER pAd,
			u16 Offset, u8 *pData, u16 length);

int RTUSBMultiWrite(IN PRTMP_ADAPTER pAd,
			 u16 Offset, u8 *pData, u16 length);

int RTUSBMultiWrite_OneByte(IN PRTMP_ADAPTER pAd,
				 u16 Offset, u8 *pData);

int RTUSBReadBBPRegister(IN PRTMP_ADAPTER pAd,
			      u8 Id, u8 *pValue);

int RTUSBWriteBBPRegister(IN PRTMP_ADAPTER pAd,
			       u8 Id, u8 Value);

int RTUSBWriteRFRegister(IN PRTMP_ADAPTER pAd, u32 Value);

int RTUSB_VendorRequest(IN PRTMP_ADAPTER pAd,
			     u32 TransferFlags,
			     u8 ReservedBits,
			     u8 Request,
			     u16 Value,
			     u16 Index,
			     void *TransferBuffer,
			     u32 TransferBufferLength);

int RTUSBReadEEPROM(IN PRTMP_ADAPTER pAd,
			 u16 Offset, u8 *pData, u16 length);

int RTUSBWriteEEPROM(IN PRTMP_ADAPTER pAd,
			  u16 Offset, u8 *pData, u16 length);

void RTUSBPutToSleep(IN PRTMP_ADAPTER pAd);

int RTUSBWakeUp(IN PRTMP_ADAPTER pAd);

void RTUSBInitializeCmdQ(IN PCmdQ cmdq);

int RTUSBEnqueueCmdFromNdis(IN PRTMP_ADAPTER pAd,
				    IN NDIS_OID Oid,
				    IN BOOLEAN SetInformation,
				    void *pInformationBuffer,
				    u32 InformationBufferLength);

int RTUSBEnqueueInternalCmd(IN PRTMP_ADAPTER pAd,
				    IN NDIS_OID Oid,
				    void *pInformationBuffer,
				    u32 InformationBufferLength);

void RTUSBDequeueCmd(IN PCmdQ cmdq, OUT PCmdQElmt * pcmdqelmt);

int RTUSBCmdThread(IN void *Context);

void RTUSBBssBeaconExit(IN RTMP_ADAPTER * pAd);

void RTUSBBssBeaconStop(IN RTMP_ADAPTER * pAd);

void RTUSBBssBeaconStart(IN RTMP_ADAPTER * pAd);

void RTUSBBssBeaconInit(IN RTMP_ADAPTER * pAd);

void RTUSBWatchDog(IN RTMP_ADAPTER * pAd);

int RTUSBWriteMACRegister(IN PRTMP_ADAPTER pAd,
			       u16 Offset, u32 Value);

int RTUSBReadMACRegister(IN PRTMP_ADAPTER pAd,
			      u16 Offset, u32 *pValue);

int RTUSBSingleWrite(IN RTMP_ADAPTER * pAd,
			  u16 Offset, u16 Value);

int RTUSBFirmwareWrite(IN PRTMP_ADAPTER pAd,
			    u8 *pFwImage, unsigned long FwLen);

int RTUSBVenderReset(IN PRTMP_ADAPTER pAd);

int RTUSBSetHardWareRegister(IN PRTMP_ADAPTER pAdapter, void *pBuf);

int RTUSBQueryHardWareRegister(IN PRTMP_ADAPTER pAdapter,
				       void *pBuf);

void CMDHandler(IN PRTMP_ADAPTER pAd);

int RTUSBWriteHWMACAddress(IN PRTMP_ADAPTER pAdapter);

void MacTableInitialize(IN PRTMP_ADAPTER pAd);

void MlmeSetPsm(IN PRTMP_ADAPTER pAd, u16 psm);

int RTMPWPAAddKeyProc(IN PRTMP_ADAPTER pAd, void *pBuf);

void AsicRxAntEvalAction(IN PRTMP_ADAPTER pAd);

void append_pkt(IN PRTMP_ADAPTER pAd,
		u8 *pHeader802_3,
		u32 HdrLen,
		u8 *pData,
		unsigned long DataSize, OUT PNDIS_PACKET * ppPacket);

u32 deaggregate_AMSDU_announce(IN PRTMP_ADAPTER pAd,
				PNDIS_PACKET pPacket,
				u8 *pData, unsigned long DataSize);

int RTMPCheckRxError(IN PRTMP_ADAPTER pAd,
			     IN PHEADER_802_11 pHeader,
			     IN PRXWI_STRUC pRxWI,
			     IN PRT28XX_RXD_STRUC pRxINFO);

void RTUSBMlmeHardTransmit(IN PRTMP_ADAPTER pAd, IN PMGMT_STRUC pMgmt);

int MlmeThread(void *Context);

/* */
/* Function Prototype in rtusb_data.c */
/* */
int RTUSBFreeDescriptorRequest(IN PRTMP_ADAPTER pAd,
				       u8 BulkOutPipeId,
				       u32 NumberRequired);

BOOLEAN RTUSBNeedQueueBackForAgg(IN RTMP_ADAPTER * pAd, u8 BulkOutPipeId);

void RTMPWriteTxInfo(IN PRTMP_ADAPTER pAd,
		     IN PTXINFO_STRUC pTxInfo,
		     u16 USBDMApktLen,
		     IN BOOLEAN bWiv,
		     u8 QueueSel, u8 NextValid, u8 TxBurst);

/* */
/* Function Prototype in cmm_data_usb.c */
/* */
u16 RtmpUSB_WriteSubTxResource(IN PRTMP_ADAPTER pAd,
				  IN TX_BLK * pTxBlk,
				  IN BOOLEAN bIsLast, u16 * FreeNumber);

u16 RtmpUSB_WriteSingleTxResource(IN PRTMP_ADAPTER pAd,
				     IN TX_BLK * pTxBlk,
				     IN BOOLEAN bIsLast,
				     u16 * FreeNumber);

u16 RtmpUSB_WriteFragTxResource(IN PRTMP_ADAPTER pAd,
				   IN TX_BLK * pTxBlk,
				   u8 fragNum, u16 * FreeNumber);

u16 RtmpUSB_WriteMultiTxResource(IN PRTMP_ADAPTER pAd,
				    IN TX_BLK * pTxBlk,
				    u8 frameNum, u16 * FreeNumber);

void RtmpUSB_FinalWriteTxResource(IN PRTMP_ADAPTER pAd,
				  IN TX_BLK * pTxBlk,
				  u16 totalMPDUSize, u16 TxIdx);

void RtmpUSBDataLastTxIdx(IN PRTMP_ADAPTER pAd,
			  u8 QueIdx, u16 TxIdx);

void RtmpUSBDataKickOut(IN PRTMP_ADAPTER pAd,
			IN TX_BLK * pTxBlk, u8 QueIdx);

int RtmpUSBMgmtKickOut(IN RTMP_ADAPTER * pAd,
		       u8 QueIdx,
		       IN PNDIS_PACKET pPacket,
		       u8 *pSrcBufVA, u32 SrcBufLen);

void RtmpUSBNullFrameKickOut(IN RTMP_ADAPTER * pAd,
			     u8 QueIdx,
			     u8 * pNullFrame, u32 frameLen);

void RtmpUsbStaAsicForceWakeupTimeout(void *SystemSpecific1,
				      void *FunctionContext,
				      void *SystemSpecific2,
				      void *SystemSpecific3);

void RT28xxUsbStaAsicForceWakeup(IN PRTMP_ADAPTER pAd, IN BOOLEAN bFromTx);

void RT28xxUsbStaAsicSleepThenAutoWakeup(IN PRTMP_ADAPTER pAd,
					 u16 TbttNumToNextWakeUp);

void RT28xxUsbMlmeRadioOn(IN PRTMP_ADAPTER pAd);

void RT28xxUsbMlmeRadioOFF(IN PRTMP_ADAPTER pAd);
#endif /* RTMP_MAC_USB // */

void AsicTurnOffRFClk(IN PRTMP_ADAPTER pAd, u8 Channel);

void AsicTurnOnRFClk(IN PRTMP_ADAPTER pAd, u8 Channel);

#ifdef RTMP_TIMER_TASK_SUPPORT
int RtmpTimerQThread(IN void *Context);

RTMP_TIMER_TASK_ENTRY *RtmpTimerQInsert(IN RTMP_ADAPTER * pAd,
					IN RALINK_TIMER_STRUCT * pTimer);

BOOLEAN RtmpTimerQRemove(IN RTMP_ADAPTER * pAd,
			 IN RALINK_TIMER_STRUCT * pTimer);

void RtmpTimerQExit(IN RTMP_ADAPTER * pAd);

void RtmpTimerQInit(IN RTMP_ADAPTER * pAd);
#endif /* RTMP_TIMER_TASK_SUPPORT // */

void AsicStaBbpTuning(IN PRTMP_ADAPTER pAd);

BOOLEAN StaAddMacTableEntry(IN PRTMP_ADAPTER pAd,
			    IN PMAC_TABLE_ENTRY pEntry,
			    u8 MaxSupportedRateIn500Kbps,
			    IN HT_CAPABILITY_IE * pHtCapability,
			    u8 HtCapabilityLen,
			    IN ADD_HT_INFO_IE * pAddHtInfo,
			    u8 AddHtInfoLen, u16 CapabilityInfo);

BOOLEAN AUTH_ReqSend(IN PRTMP_ADAPTER pAd,
		     IN PMLME_QUEUE_ELEM pElem,
		     IN PRALINK_TIMER_STRUCT pAuthTimer,
		     char *pSMName,
		     u16 SeqNo,
		     u8 *pNewElement, unsigned long ElementLen);

void RTMP_IndicateMediaState(IN PRTMP_ADAPTER pAd);

void ReSyncBeaconTime(IN PRTMP_ADAPTER pAd);

void RTMPSetAGCInitValue(IN PRTMP_ADAPTER pAd, u8 BandWidth);

int rt28xx_close(IN PNET_DEV dev);
int rt28xx_open(IN PNET_DEV dev);

#define VIRTUAL_IF_INC(__pAd) ((__pAd)->VirtualIfCnt++)
#define VIRTUAL_IF_DEC(__pAd) ((__pAd)->VirtualIfCnt--)
#define VIRTUAL_IF_NUM(__pAd) ((__pAd)->VirtualIfCnt)

#ifdef LINUX
__inline int VIRTUAL_IF_UP(PRTMP_ADAPTER pAd)
{
	if (VIRTUAL_IF_NUM(pAd) == 0) {
		if (rt28xx_open(pAd->net_dev) != 0) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("rt28xx_open return fail!\n"));
			return -1;
		}
	} else {
	}
	VIRTUAL_IF_INC(pAd);
	return 0;
}

__inline void VIRTUAL_IF_DOWN(PRTMP_ADAPTER pAd)
{
	VIRTUAL_IF_DEC(pAd);
	if (VIRTUAL_IF_NUM(pAd) == 0)
		rt28xx_close(pAd->net_dev);
	return;
}
#endif /* LINUX // */

/*
	OS Related funciton prototype definitions.
	TODO: Maybe we need to move these function prototypes to other proper place.
*/
int RtmpOSWrielessEventSend(IN RTMP_ADAPTER * pAd,
			    u32 eventType,
			    int flags,
			    u8 *pSrcMac,
			    u8 *pData, u32 dataLen);

int RtmpOSNetDevAddrSet(IN PNET_DEV pNetDev, u8 *pMacAddr);

int RtmpOSNetDevAttach(IN PNET_DEV pNetDev,
		       IN RTMP_OS_NETDEV_OP_HOOK * pDevOpHook);

void RtmpOSNetDevClose(IN PNET_DEV pNetDev);

void RtmpOSNetDevDetach(IN PNET_DEV pNetDev);

int RtmpOSNetDevAlloc(IN PNET_DEV * pNewNetDev, u32 privDataSize);

void RtmpOSNetDevFree(IN PNET_DEV pNetDev);

PNET_DEV RtmpOSNetDevGetByName(IN PNET_DEV pNetDev, char *pDevName);

void RtmpOSNetDeviceRefPut(IN PNET_DEV pNetDev);

PNET_DEV RtmpOSNetDevCreate(IN RTMP_ADAPTER * pAd,
			    int devType,
			    int devNum,
			    int privMemSize, char *pNamePrefix);

/*
	Task operation related function prototypes
*/
void RtmpOSTaskCustomize(IN RTMP_OS_TASK * pTask);

int RtmpOSTaskNotifyToExit(IN RTMP_OS_TASK * pTask);

int RtmpOSTaskKill(IN RTMP_OS_TASK * pTask);

int RtmpOSTaskInit(IN RTMP_OS_TASK * pTask,
			   char *pTaskName, void * pPriv);

int RtmpOSTaskAttach(IN RTMP_OS_TASK * pTask,
			     IN int (*fn) (void *), IN void *arg);

/*
	File operation related function prototypes
*/
RTMP_OS_FD RtmpOSFileOpen(IN char *pPath, IN int flag, IN int mode);

int RtmpOSFileClose(IN RTMP_OS_FD osfd);

void RtmpOSFileSeek(IN RTMP_OS_FD osfd, IN int offset);

int RtmpOSFileRead(IN RTMP_OS_FD osfd, IN char *pDataPtr, IN int readLen);

int RtmpOSFileWrite(IN RTMP_OS_FD osfd, IN char *pDataPtr, IN int writeLen);

#endif /* __RTMP_H__ */
