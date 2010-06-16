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
	mac_pci.h

    Abstract:

    Revision History:
    Who          When          What
    ---------    ----------    ----------------------------------------------
 */

#ifndef __MAC_PCI_H__
#define __MAC_PCI_H__

#include "../rtmp_type.h"
#include "rtmp_mac.h"
#include "rtmp_phy.h"
#include "../rtmp_iface.h"
#include "../rtmp_dot11.h"

/* */
/* Device ID & Vendor ID related definitions, */
/* NOTE: you should not add the new VendorID/DeviceID here unless you not sure it belongs to what chip. */
/* */
#define NIC_PCI_VENDOR_ID		0x1814
#define PCIBUS_INTEL_VENDOR	0x8086

#if !defined(PCI_CAP_ID_EXP)
#define PCI_CAP_ID_EXP			    0x10
#endif
#if !defined(PCI_EXP_LNKCTL)
#define PCI_EXP_LNKCTL			    0x10
#endif
#if !defined(PCI_CLASS_BRIDGE_PCI)
#define PCI_CLASS_BRIDGE_PCI		0x0604
#endif

#define TXINFO_SIZE						0
#define RTMP_PKT_TAIL_PADDING			0
#define fRTMP_ADAPTER_NEED_STOP_TX	0

#define AUX_CTRL           0x10c

/* */
/* TX descriptor format, Tx     ring, Mgmt Ring */
/* */
struct PACKED rt_txd {
	/* Word 0 */
	u32 SDPtr0;
	/* Word 1 */
	u32 SDLen1:14;
	u32 LastSec1:1;
	u32 Burst:1;
	u32 SDLen0:14;
	u32 LastSec0:1;
	u32 DMADONE:1;
	/*Word2 */
	u32 SDPtr1;
	/*Word3 */
	u32 rsv2:24;
	u32 WIV:1;		/* Wireless Info Valid. 1 if Driver already fill WI,  o if DMA needs to copy WI to correctposition */
	u32 QSEL:2;		/* select on-chip FIFO ID for 2nd-stage output scheduler.0:MGMT, 1:HCCA 2:EDCA */
	u32 rsv:2;
	u32 TCO:1;		/* */
	u32 UCO:1;		/* */
	u32 ICO:1;		/* */
};

/* */
/* Rx descriptor format, Rx Ring */
/* */
typedef struct PACKED rt_rxd {
	/* Word 0 */
	u32 SDP0;
	/* Word 1 */
	u32 SDL1:14;
	u32 Rsv:2;
	u32 SDL0:14;
	u32 LS0:1;
	u32 DDONE:1;
	/* Word 2 */
	u32 SDP1;
	/* Word 3 */
	u32 BA:1;
	u32 DATA:1;
	u32 NULLDATA:1;
	u32 FRAG:1;
	u32 U2M:1;		/* 1: this RX frame is unicast to me */
	u32 Mcast:1;		/* 1: this is a multicast frame */
	u32 Bcast:1;		/* 1: this is a broadcast frame */
	u32 MyBss:1;		/* 1: this frame belongs to the same BSSID */
	u32 Crc:1;		/* 1: CRC error */
	u32 CipherErr:2;	/* 0: decryption okay, 1:ICV error, 2:MIC error, 3:KEY not valid */
	u32 AMSDU:1;		/* rx with 802.3 header, not 802.11 header. */
	u32 HTC:1;
	u32 RSSI:1;
	u32 L2PAD:1;
	u32 AMPDU:1;
	u32 Decrypted:1;	/* this frame is being decrypted. */
	u32 PlcpSignal:1;	/* To be moved */
	u32 PlcpRssil:1;	/* To be moved */
	u32 Rsv1:13;
} RT28XX_RXD_STRUC, *PRT28XX_RXD_STRUC;

typedef union _TX_ATTENUATION_CTRL_STRUC {
	struct {
		unsigned long RF_ISOLATION_ENABLE:1;
		unsigned long Reserve2:7;
		unsigned long PCIE_PHY_TX_ATTEN_VALUE:3;
		unsigned long PCIE_PHY_TX_ATTEN_EN:1;
		unsigned long Reserve1:20;
	} field;

	unsigned long word;
} TX_ATTENUATION_CTRL_STRUC, *PTX_ATTENUATION_CTRL_STRUC;

/* ----------------- EEPROM Related MACRO ----------------- */

/* 8051 firmware image for RT2860 - base address = 0x4000 */
#define FIRMWARE_IMAGE_BASE     0x2000
#define MAX_FIRMWARE_IMAGE_SIZE 0x2000	/* 8kbyte */

/* ----------------- Frimware Related MACRO ----------------- */
#define RTMP_WRITE_FIRMWARE(_pAd, _pFwImage, _FwLen)			\
	do {								\
		unsigned long	_i, _firm;					\
		RTMP_IO_WRITE32(_pAd, PBF_SYS_CTRL, 0x10000);		\
									\
		for (_i = 0; _i < _FwLen; _i += 4) {				\
				_firm = _pFwImage[_i] +				\
			   (_pFwImage[_i+3] << 24) +			\
			   (_pFwImage[_i+2] << 16) +			\
			   (_pFwImage[_i+1] << 8);			\
			RTMP_IO_WRITE32(_pAd, FIRMWARE_IMAGE_BASE + _i, _firm);	\
		}							\
		RTMP_IO_WRITE32(_pAd, PBF_SYS_CTRL, 0x00000);		\
		RTMP_IO_WRITE32(_pAd, PBF_SYS_CTRL, 0x00001);		\
									\
		/* initialize BBP R/W access agent */			\
		RTMP_IO_WRITE32(_pAd, H2M_BBP_AGENT, 0);		\
		RTMP_IO_WRITE32(_pAd, H2M_MAILBOX_CSR, 0);		\
	} while (0)

/* ----------------- TX Related MACRO ----------------- */
#define RTMP_START_DEQUEUE(pAd, QueIdx, irqFlags)		do {} while (0)
#define RTMP_STOP_DEQUEUE(pAd, QueIdx, irqFlags)		do {} while (0)

#define RTMP_HAS_ENOUGH_FREE_DESC(pAd, pTxBlk, freeNum, pPacket) \
		((freeNum) >= (unsigned long)(pTxBlk->TotalFragNum + RTMP_GET_PACKET_FRAGMENTS(pPacket) + 3))	/* rough estimate we will use 3 more descriptor. */
#define RTMP_RELEASE_DESC_RESOURCE(pAd, QueIdx)			do {} while (0)

#define NEED_QUEUE_BACK_FOR_AGG(pAd, QueIdx, freeNum, _TxFrameType) \
		(((freeNum != (TX_RING_SIZE-1)) && \
		(pAd->TxSwQueue[QueIdx].Number == 0)) || (freeNum < 3))

#define HAL_KickOutMgmtTx(_pAd, _QueIdx, _pPacket, _pSrcBufVA, _SrcBufLen)	\
			RtmpPCIMgmtKickOut(_pAd, _QueIdx, _pPacket, _pSrcBufVA, _SrcBufLen)

#define HAL_WriteSubTxResource(pAd, pTxBlk, bIsLast, pFreeNumber)	\
				/* RtmpPCI_WriteSubTxResource(pAd, pTxBlk, bIsLast, pFreeNumber) */

#define HAL_WriteTxResource(pAd, pTxBlk, bIsLast, pFreeNumber)	\
			RtmpPCI_WriteSingleTxResource(pAd, pTxBlk, bIsLast, pFreeNumber)

#define HAL_WriteFragTxResource(pAd, pTxBlk, fragNum, pFreeNumber) \
			RtmpPCI_WriteFragTxResource(pAd, pTxBlk, fragNum, pFreeNumber)

#define HAL_WriteMultiTxResource(pAd, pTxBlk, frameNum, pFreeNumber) \
			RtmpPCI_WriteMultiTxResource(pAd, pTxBlk, frameNum, pFreeNumber)

#define HAL_FinalWriteTxResource(_pAd, _pTxBlk, _TotalMPDUSize, _FirstTxIdx)	\
			RtmpPCI_FinalWriteTxResource(_pAd, _pTxBlk, _TotalMPDUSize, _FirstTxIdx)

#define HAL_LastTxIdx(_pAd, _QueIdx, _LastTxIdx) \
				/*RtmpPCIDataLastTxIdx(_pAd, _QueIdx,_LastTxIdx) */

#define HAL_KickOutTx(_pAd, _pTxBlk, _QueIdx)	\
			RTMP_IO_WRITE32((_pAd), TX_CTX_IDX0+((_QueIdx)*0x10), (_pAd)->TxRing[(_QueIdx)].TxCpuIdx)
/*			RtmpPCIDataKickOut(_pAd, _pTxBlk, _QueIdx)*/

#define HAL_KickOutNullFrameTx(_pAd, _QueIdx, _pNullFrame, _frameLen)	\
			MiniportMMRequest(_pAd, _QueIdx, _pNullFrame, _frameLen)

#define GET_TXRING_FREENO(_pAd, _QueIdx) \
	(_pAd->TxRing[_QueIdx].TxSwFreeIdx > _pAd->TxRing[_QueIdx].TxCpuIdx)	? \
			(_pAd->TxRing[_QueIdx].TxSwFreeIdx - _pAd->TxRing[_QueIdx].TxCpuIdx - 1) \
			 :	\
			(_pAd->TxRing[_QueIdx].TxSwFreeIdx + TX_RING_SIZE - _pAd->TxRing[_QueIdx].TxCpuIdx - 1);

#define GET_MGMTRING_FREENO(_pAd) \
	(_pAd->MgmtRing.TxSwFreeIdx > _pAd->MgmtRing.TxCpuIdx)	? \
			(_pAd->MgmtRing.TxSwFreeIdx - _pAd->MgmtRing.TxCpuIdx - 1) \
			 :	\
			(_pAd->MgmtRing.TxSwFreeIdx + MGMT_RING_SIZE - _pAd->MgmtRing.TxCpuIdx - 1);

/* ----------------- RX Related MACRO ----------------- */

/* ----------------- ASIC Related MACRO ----------------- */
/* reset MAC of a station entry to 0x000000000000 */
#define RTMP_STA_ENTRY_MAC_RESET(pAd, Wcid)	\
	AsicDelWcidTab(pAd, Wcid);

/* add this entry into ASIC RX WCID search table */
#define RTMP_STA_ENTRY_ADD(pAd, pEntry)		\
	AsicUpdateRxWCIDTable(pAd, pEntry->Aid, pEntry->Addr);

/* add by johnli, fix "in_interrupt" error when call "MacTableDeleteEntry" in Rx tasklet */
/* Set MAC register value according operation mode */
#define RTMP_UPDATE_PROTECT(pAd)	\
	AsicUpdateProtect(pAd, 0, (ALLN_SETPROTECT), TRUE, 0);
/* end johnli */

/* remove Pair-wise key material from ASIC */
#define RTMP_STA_ENTRY_KEY_DEL(pAd, BssIdx, Wcid)	\
	AsicRemovePairwiseKeyEntry(pAd, BssIdx, (u8)Wcid);

/* add Client security information into ASIC WCID table and IVEIV table */
#define RTMP_STA_SECURITY_INFO_ADD(pAd, apidx, KeyID, pEntry)		\
	RTMPAddWcidAttributeEntry(pAd, apidx, KeyID,			\
							pAd->SharedKey[apidx][KeyID].CipherAlg, pEntry);

#define RTMP_SECURITY_KEY_ADD(pAd, apidx, KeyID, pEntry)		\
	{	/* update pairwise key information to ASIC Shared Key Table */	\
		AsicAddSharedKeyEntry(pAd, apidx, KeyID,					\
						  pAd->SharedKey[apidx][KeyID].CipherAlg,		\
						  pAd->SharedKey[apidx][KeyID].Key,				\
						  pAd->SharedKey[apidx][KeyID].TxMic,			\
						  pAd->SharedKey[apidx][KeyID].RxMic);			\
		/* update ASIC WCID attribute table and IVEIV table */			\
		RTMPAddWcidAttributeEntry(pAd, apidx, KeyID,					\
						  pAd->SharedKey[apidx][KeyID].CipherAlg,		\
						  pEntry); }

/* Insert the BA bitmap to ASIC for the Wcid entry */
#define RTMP_ADD_BA_SESSION_TO_ASIC(_pAd, _Aid, _TID)	\
		do {					\
			u32 _Value = 0, _Offset;					\
			_Offset = MAC_WCID_BASE + (_Aid) * HW_WCID_ENTRY_SIZE + 4;	\
			RTMP_IO_READ32((_pAd), _Offset, &_Value);\
			_Value |= (0x10000<<(_TID));	\
			RTMP_IO_WRITE32((_pAd), _Offset, _Value);\
		} while (0)

/* Remove the BA bitmap from ASIC for the Wcid entry */
/*              bitmap field starts at 0x10000 in ASIC WCID table */
#define RTMP_DEL_BA_SESSION_FROM_ASIC(_pAd, _Wcid, _TID)				\
		do {								\
			u32 _Value = 0, _Offset;				\
			_Offset = MAC_WCID_BASE + (_Wcid) * HW_WCID_ENTRY_SIZE + 4;	\
			RTMP_IO_READ32((_pAd), _Offset, &_Value);			\
			_Value &= (~(0x10000 << (_TID)));				\
			RTMP_IO_WRITE32((_pAd), _Offset, _Value);			\
		} while (0)

/* ----------------- Interface Related MACRO ----------------- */

/* */
/* Enable & Disable NIC interrupt via writing interrupt mask register */
/* Since it use ADAPTER structure, it have to be put after structure definition. */
/* */
#define RTMP_ASIC_INTERRUPT_DISABLE(_pAd)		\
	do {			\
		RTMP_IO_WRITE32((_pAd), INT_MASK_CSR, 0x0);     /* 0: disable */	\
		RTMP_CLEAR_FLAG((_pAd), fRTMP_ADAPTER_INTERRUPT_ACTIVE);		\
	} while (0)

#define RTMP_ASIC_INTERRUPT_ENABLE(_pAd)\
	do {				\
		RTMP_IO_WRITE32((_pAd), INT_MASK_CSR, (_pAd)->int_enable_reg /*DELAYINTMASK*/);     /* 1:enable */	\
		RTMP_SET_FLAG((_pAd), fRTMP_ADAPTER_INTERRUPT_ACTIVE);	\
	} while (0)

#define RTMP_IRQ_INIT(pAd)	\
	{	pAd->int_enable_reg = ((DELAYINTMASK) |		\
					(RxINT|TxDataInt|TxMgmtInt)) & ~(0x03);	\
		pAd->int_disable_mask = 0;						\
		pAd->int_pending = 0; }

#define RTMP_IRQ_ENABLE(pAd)					\
	{	/* clear garbage ints */			\
		RTMP_IO_WRITE32(pAd, INT_SOURCE_CSR, 0xffffffff);\
		RTMP_ASIC_INTERRUPT_ENABLE(pAd); }

/* ----------------- MLME Related MACRO ----------------- */
#define RTMP_MLME_HANDLER(pAd)			MlmeHandler(pAd)

#define RTMP_MLME_PRE_SANITY_CHECK(pAd)

#define RTMP_MLME_STA_QUICK_RSP_WAKE_UP(pAd)	\
		RTMPSetTimer(&pAd->StaCfg.StaQuickResponeForRateUpTimer, 100);

#define RTMP_MLME_RESET_STATE_MACHINE(pAd)	\
		MlmeRestartStateMachine(pAd)

#define RTMP_HANDLE_COUNTER_MEASURE(_pAd, _pEntry)\
		HandleCounterMeasure(_pAd, _pEntry)

/* ----------------- Power Save Related MACRO ----------------- */
#define RTMP_PS_POLL_ENQUEUE(pAd)				EnqueuePsPoll(pAd)

/* For RTMPPCIePowerLinkCtrlRestore () function */
#define RESTORE_HALT		1
#define RESTORE_WAKEUP		2
#define RESTORE_CLOSE           3

#define PowerSafeCID		1
#define PowerRadioOffCID	2
#define PowerWakeCID		3
#define CID0MASK		0x000000ff
#define CID1MASK		0x0000ff00
#define CID2MASK		0x00ff0000
#define CID3MASK		0xff000000

#define RTMP_STA_FORCE_WAKEUP(pAd, bFromTx) \
    RT28xxPciStaAsicForceWakeup(pAd, bFromTx);

#define RTMP_STA_SLEEP_THEN_AUTO_WAKEUP(pAd, TbttNumToNextWakeUp) \
    RT28xxPciStaAsicSleepThenAutoWakeup(pAd, TbttNumToNextWakeUp);

#define RTMP_SET_PSM_BIT(_pAd, _val) \
	MlmeSetPsmBit(_pAd, _val);

#define RTMP_MLME_RADIO_ON(pAd) \
    RT28xxPciMlmeRadioOn(pAd);

#define RTMP_MLME_RADIO_OFF(pAd) \
    RT28xxPciMlmeRadioOFF(pAd);

#endif /*__MAC_PCI_H__ // */
