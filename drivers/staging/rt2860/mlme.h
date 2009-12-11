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
	mlme.h

	Abstract:

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
	John Chang	2003-08-28		Created
	John Chang  2004-09-06      modified for RT2600

*/
#ifndef __MLME_H__
#define __MLME_H__

#include "rtmp_dot11.h"

/* maximum supported capability information - */
/* ESS, IBSS, Privacy, Short Preamble, Spectrum mgmt, Short Slot */
#define SUPPORTED_CAPABILITY_INFO   0x0533

#define END_OF_ARGS                 -1
#define LFSR_MASK                   0x80000057
#define MLME_TASK_EXEC_INTV         100/*200*/	/* */
#define LEAD_TIME                   5
#define MLME_TASK_EXEC_MULTIPLE       10  /*5*/	/* MLME_TASK_EXEC_MULTIPLE * MLME_TASK_EXEC_INTV = 1 sec */
#define REORDER_EXEC_INTV         	100	/* 0.1 sec */

/* The definition of Radar detection duration region */
#define CE		0
#define FCC		1
#define JAP		2
#define JAP_W53	3
#define JAP_W56	4
#define MAX_RD_REGION 5

#define BEACON_LOST_TIME            4 * OS_HZ	/* 2048 msec = 2 sec */

#define DLS_TIMEOUT                 1200	/* unit: msec */
#define AUTH_TIMEOUT                300	/* unit: msec */
#define ASSOC_TIMEOUT               300	/* unit: msec */
#define JOIN_TIMEOUT                2000	/* unit: msec */
#define SHORT_CHANNEL_TIME          90	/* unit: msec */
#define MIN_CHANNEL_TIME            110	/* unit: msec, for dual band scan */
#define MAX_CHANNEL_TIME            140	/* unit: msec, for single band scan */
#define	FAST_ACTIVE_SCAN_TIME	    30	/* Active scan waiting for probe response time */
#define CW_MIN_IN_BITS              4	/* actual CwMin = 2^CW_MIN_IN_BITS - 1 */
#define LINK_DOWN_TIMEOUT           20000	/* unit: msec */
#define AUTO_WAKEUP_TIMEOUT			70	/*unit: msec */

#define CW_MAX_IN_BITS              10	/* actual CwMax = 2^CW_MAX_IN_BITS - 1 */

/* Note: RSSI_TO_DBM_OFFSET has been changed to variable for new RF (2004-0720). */
/* SHould not refer to this constant anymore */
/*#define RSSI_TO_DBM_OFFSET          120 // for RT2530 RSSI-115 = dBm */
#define RSSI_FOR_MID_TX_POWER       -55	/* -55 db is considered mid-distance */
#define RSSI_FOR_LOW_TX_POWER       -45	/* -45 db is considered very short distance and */
					/* eligible to use a lower TX power */
#define RSSI_FOR_LOWEST_TX_POWER    -30
/*#define MID_TX_POWER_DELTA          0   // 0 db from full TX power upon mid-distance to AP */
#define LOW_TX_POWER_DELTA          6	/* -3 db from full TX power upon very short distance. 1 grade is 0.5 db */
#define LOWEST_TX_POWER_DELTA       16	/* -8 db from full TX power upon shortest distance. 1 grade is 0.5 db */

#define RSSI_TRIGGERED_UPON_BELOW_THRESHOLD     0
#define RSSI_TRIGGERED_UPON_EXCCEED_THRESHOLD   1
#define RSSI_THRESHOLD_FOR_ROAMING              25
#define RSSI_DELTA                              5

/* Channel Quality Indication */
#define CQI_IS_GOOD(cqi)            ((cqi) >= 50)
/*#define CQI_IS_FAIR(cqi)          (((cqi) >= 20) && ((cqi) < 50)) */
#define CQI_IS_POOR(cqi)            (cqi < 50)	/*(((cqi) >= 5) && ((cqi) < 20)) */
#define CQI_IS_BAD(cqi)             (cqi < 5)
#define CQI_IS_DEAD(cqi)            (cqi == 0)

/* weighting factor to calculate Channel quality, total should be 100% */
#define RSSI_WEIGHTING                   50
#define TX_WEIGHTING                     30
#define RX_WEIGHTING                     20

#define BSS_NOT_FOUND                    0xFFFFFFFF

#define MAX_LEN_OF_MLME_QUEUE            40	/*10 */

#define SCAN_PASSIVE                     18	/* scan with no probe request, only wait beacon and probe response */
#define SCAN_ACTIVE                      19	/* scan with probe request, and wait beacon and probe response */
#define	SCAN_CISCO_PASSIVE				 20	/* Single channel passive scan */
#define	SCAN_CISCO_ACTIVE				 21	/* Single channel active scan */
#define	SCAN_CISCO_NOISE				 22	/* Single channel passive scan for noise histogram collection */
#define	SCAN_CISCO_CHANNEL_LOAD			 23	/* Single channel passive scan for channel load collection */
#define FAST_SCAN_ACTIVE                 24	/* scan with probe request, and wait beacon and probe response */

#define MAC_ADDR_IS_GROUP(Addr)       (((Addr[0]) & 0x01))
#define MAC_ADDR_HASH(Addr)            (Addr[0] ^ Addr[1] ^ Addr[2] ^ Addr[3] ^ Addr[4] ^ Addr[5])
#define MAC_ADDR_HASH_INDEX(Addr)      (MAC_ADDR_HASH(Addr) % HASH_TABLE_SIZE)
#define TID_MAC_HASH(Addr,TID)            (TID^Addr[0] ^ Addr[1] ^ Addr[2] ^ Addr[3] ^ Addr[4] ^ Addr[5])
#define TID_MAC_HASH_INDEX(Addr,TID)      (TID_MAC_HASH(Addr,TID) % HASH_TABLE_SIZE)

/* LED Control */
/* assoiation ON. one LED ON. another blinking when TX, OFF when idle */
/* no association, both LED off */
#define ASIC_LED_ACT_ON(pAd)        RTMP_IO_WRITE32(pAd, MAC_CSR14, 0x00031e46)
#define ASIC_LED_ACT_OFF(pAd)       RTMP_IO_WRITE32(pAd, MAC_CSR14, 0x00001e46)

/* bit definition of the 2-byte pBEACON->Capability field */
#define CAP_IS_ESS_ON(x)                 (((x) & 0x0001) != 0)
#define CAP_IS_IBSS_ON(x)                (((x) & 0x0002) != 0)
#define CAP_IS_CF_POLLABLE_ON(x)         (((x) & 0x0004) != 0)
#define CAP_IS_CF_POLL_REQ_ON(x)         (((x) & 0x0008) != 0)
#define CAP_IS_PRIVACY_ON(x)             (((x) & 0x0010) != 0)
#define CAP_IS_SHORT_PREAMBLE_ON(x)      (((x) & 0x0020) != 0)
#define CAP_IS_PBCC_ON(x)                (((x) & 0x0040) != 0)
#define CAP_IS_AGILITY_ON(x)             (((x) & 0x0080) != 0)
#define CAP_IS_SPECTRUM_MGMT(x)          (((x) & 0x0100) != 0)	/* 802.11e d9 */
#define CAP_IS_QOS(x)                    (((x) & 0x0200) != 0)	/* 802.11e d9 */
#define CAP_IS_SHORT_SLOT(x)             (((x) & 0x0400) != 0)
#define CAP_IS_APSD(x)                   (((x) & 0x0800) != 0)	/* 802.11e d9 */
#define CAP_IS_IMMED_BA(x)               (((x) & 0x1000) != 0)	/* 802.11e d9 */
#define CAP_IS_DSSS_OFDM(x)              (((x) & 0x2000) != 0)
#define CAP_IS_DELAY_BA(x)               (((x) & 0x4000) != 0)	/* 802.11e d9 */

#define CAP_GENERATE(ess,ibss,priv,s_pre,s_slot,spectrum)  (((ess) ? 0x0001 : 0x0000) | ((ibss) ? 0x0002 : 0x0000) | ((priv) ? 0x0010 : 0x0000) | ((s_pre) ? 0x0020 : 0x0000) | ((s_slot) ? 0x0400 : 0x0000) | ((spectrum) ? 0x0100 : 0x0000))

#define ERP_IS_NON_ERP_PRESENT(x)        (((x) & 0x01) != 0)	/* 802.11g */
#define ERP_IS_USE_PROTECTION(x)         (((x) & 0x02) != 0)	/* 802.11g */
#define ERP_IS_USE_BARKER_PREAMBLE(x)    (((x) & 0x04) != 0)	/* 802.11g */

#define DRS_TX_QUALITY_WORST_BOUND       8	/* 3  // just test by gary */
#define DRS_PENALTY                      8

#define BA_NOTUSE 	2
/*BA Policy subfiled value in ADDBA frame */
#define IMMED_BA 	1
#define DELAY_BA	0

/* BA Initiator subfield in DELBA frame */
#define ORIGINATOR	1
#define RECIPIENT	0

/* ADDBA Status Code */
#define ADDBA_RESULTCODE_SUCCESS					0
#define ADDBA_RESULTCODE_REFUSED					37
#define ADDBA_RESULTCODE_INVALID_PARAMETERS			38

/* DELBA Reason Code */
#define DELBA_REASONCODE_QSTA_LEAVING				36
#define DELBA_REASONCODE_END_BA						37
#define DELBA_REASONCODE_UNKNOWN_BA					38
#define DELBA_REASONCODE_TIMEOUT					39

/* reset all OneSecTx counters */
#define RESET_ONE_SEC_TX_CNT(__pEntry) \
if (((__pEntry)) != NULL) \
{ \
	(__pEntry)->OneSecTxRetryOkCount = 0; \
	(__pEntry)->OneSecTxFailCount = 0; \
	(__pEntry)->OneSecTxNoRetryOkCount = 0; \
}

/* */
/* 802.11 frame formats */
/* */
/*  HT Capability INFO field in HT Cap IE . */
typedef struct PACKED {
	u16 AdvCoding:1;
	u16 ChannelWidth:1;
	u16 MimoPs:2;	/*momi power safe */
	u16 GF:1;		/*green field */
	u16 ShortGIfor20:1;
	u16 ShortGIfor40:1;	/*for40MHz */
	u16 TxSTBC:1;
	u16 RxSTBC:2;
	u16 DelayedBA:1;	/*rt2860c not support */
	u16 AMsduSize:1;	/* only support as zero */
	u16 CCKmodein40:1;
	u16 PSMP:1;
	u16 Forty_Mhz_Intolerant:1;
	u16 LSIGTxopProSup:1;
} HT_CAP_INFO, *PHT_CAP_INFO;

/*  HT Capability INFO field in HT Cap IE . */
typedef struct PACKED {
	u8 MaxRAmpduFactor:2;
	u8 MpduDensity:3;
	u8 rsv:3;		/*momi power safe */
} HT_CAP_PARM, *PHT_CAP_PARM;

/*  HT Capability INFO field in HT Cap IE . */
typedef struct PACKED {
	u8 MCSSet[10];
	u8 SupRate[2];	/* unit : 1Mbps */
	u8 TxMCSSetDefined:1;
	u8 TxRxNotEqual:1;
	u8 TxStream:2;
	u8 MpduDensity:1;
	u8 rsv:3;
	u8 rsv3[3];
} HT_MCS_SET, *PHT_MCS_SET;

/*  HT Capability INFO field in HT Cap IE . */
typedef struct PACKED {
	u16 Pco:1;
	u16 TranTime:2;
	u16 rsv:5;		/*momi power safe */
	u16 MCSFeedback:2;	/*0:no MCS feedback, 2:unsolicited MCS feedback, 3:Full MCS feedback,  1:rsv. */
	u16 PlusHTC:1;	/*+HTC control field support */
	u16 RDGSupport:1;	/*reverse Direction Grant  support */
	u16 rsv2:4;
} EXT_HT_CAP_INFO, *PEXT_HT_CAP_INFO;

/*  HT Beamforming field in HT Cap IE . */
typedef struct PACKED _HT_BF_CAP {
	unsigned long TxBFRecCapable:1;
	unsigned long RxSoundCapable:1;
	unsigned long TxSoundCapable:1;
	unsigned long RxNDPCapable:1;
	unsigned long TxNDPCapable:1;
	unsigned long ImpTxBFCapable:1;
	unsigned long Calibration:2;
	unsigned long ExpCSICapable:1;
	unsigned long ExpNoComSteerCapable:1;
	unsigned long ExpComSteerCapable:1;
	unsigned long ExpCSIFbk:2;
	unsigned long ExpNoComBF:2;
	unsigned long ExpComBF:2;
	unsigned long MinGrouping:2;
	unsigned long CSIBFAntSup:2;
	unsigned long NoComSteerBFAntSup:2;
	unsigned long ComSteerBFAntSup:2;
	unsigned long CSIRowBFSup:2;
	unsigned long ChanEstimation:2;
	unsigned long rsv:3;
} HT_BF_CAP, *PHT_BF_CAP;

/*  HT antenna selection field in HT Cap IE . */
typedef struct PACKED _HT_AS_CAP {
	u8 AntSelect:1;
	u8 ExpCSIFbkTxASEL:1;
	u8 AntIndFbkTxASEL:1;
	u8 ExpCSIFbk:1;
	u8 AntIndFbk:1;
	u8 RxASel:1;
	u8 TxSoundPPDU:1;
	u8 rsv:1;
} HT_AS_CAP, *PHT_AS_CAP;

/* Draft 1.0 set IE length 26, but is extensible.. */
#define SIZE_HT_CAP_IE		26
/* The structure for HT Capability IE. */
typedef struct PACKED _HT_CAPABILITY_IE {
	HT_CAP_INFO HtCapInfo;
	HT_CAP_PARM HtCapParm;
/*      HT_MCS_SET              HtMCSSet; */
	u8 MCSSet[16];
	EXT_HT_CAP_INFO ExtHtCapInfo;
	HT_BF_CAP TxBFCap;	/* beamforming cap. rt2860c not support beamforming. */
	HT_AS_CAP ASCap;	/*antenna selection. */
} HT_CAPABILITY_IE, *PHT_CAPABILITY_IE;

/* 802.11n draft3 related structure definitions. */
/* 7.3.2.60 */
#define dot11OBSSScanPassiveDwell							20	/* in TU. min amount of time that the STA continously scans each channel when performing an active OBSS scan. */
#define dot11OBSSScanActiveDwell							10	/* in TU.min amount of time that the STA continously scans each channel when performing an passive OBSS scan. */
#define dot11BSSWidthTriggerScanInterval					300	/* in sec. max interval between scan operations to be performed to detect BSS channel width trigger events. */
#define dot11OBSSScanPassiveTotalPerChannel					200	/* in TU. min total amount of time that the STA scans each channel when performing a passive OBSS scan. */
#define dot11OBSSScanActiveTotalPerChannel					20	/*in TU. min total amount of time that the STA scans each channel when performing a active OBSS scan */
#define dot11BSSWidthChannelTransactionDelayFactor			5	/* min ratio between the delay time in performing a switch from 20MHz BSS to 20/40 BSS operation and the maximum */
																/*      interval between overlapping BSS scan operations. */
#define dot11BSSScanActivityThreshold						25	/* in %%, max total time that a STA may be active on the medium during a period of */
																/*      (dot11BSSWidthChannelTransactionDelayFactor * dot11BSSWidthTriggerScanInterval) seconds without */
																/*      being obligated to perform OBSS Scan operations. default is 25(== 0.25%) */

typedef struct PACKED _OVERLAP_BSS_SCAN_IE {
	u16 ScanPassiveDwell;
	u16 ScanActiveDwell;
	u16 TriggerScanInt;	/* Trigger scan interval */
	u16 PassiveTalPerChannel;	/* passive total per channel */
	u16 ActiveTalPerChannel;	/* active total per channel */
	u16 DelayFactor;	/* BSS width channel transition delay factor */
	u16 ScanActThre;	/* Scan Activity threshold */
} OVERLAP_BSS_SCAN_IE, *POVERLAP_BSS_SCAN_IE;

/*  7.3.2.56. 20/40 Coexistence element used in  Element ID = 72 = IE_2040_BSS_COEXIST */
typedef union PACKED _BSS_2040_COEXIST_IE {
	struct PACKED {
		u8 InfoReq:1;
		u8 Intolerant40:1;	/* Inter-BSS. set 1 when prohibits a receiving BSS from operating as a 20/40 Mhz BSS. */
		u8 BSS20WidthReq:1;	/* Intra-BSS set 1 when prohibits a receiving AP from operating its BSS as a 20/40MHz BSS. */
		u8 rsv:5;
	} field;
	u8 word;
} BSS_2040_COEXIST_IE, *PBSS_2040_COEXIST_IE;

typedef struct _TRIGGER_EVENTA {
	BOOLEAN bValid;
	u8 BSSID[6];
	u8 RegClass;		/* Regulatory Class */
	u16 Channel;
	unsigned long CDCounter;	/* Maintain a seperate count down counter for each Event A. */
} TRIGGER_EVENTA, *PTRIGGER_EVENTA;

/* 20/40 trigger event table */
/* If one Event A delete or created, or if Event B is detected or not detected, STA should send 2040BSSCoexistence to AP. */
#define MAX_TRIGGER_EVENT		64
typedef struct _TRIGGER_EVENT_TAB {
	u8 EventANo;
	TRIGGER_EVENTA EventA[MAX_TRIGGER_EVENT];
	unsigned long EventBCountDown;	/* Count down counter for Event B. */
} TRIGGER_EVENT_TAB, *PTRIGGER_EVENT_TAB;

/* 7.3.27 20/40 Bss Coexistence Mgmt capability used in extended capabilities information IE( ID = 127 = IE_EXT_CAPABILITY). */
/*      This is the first octet and was defined in 802.11n D3.03 and 802.11yD9.0 */
typedef struct PACKED _EXT_CAP_INFO_ELEMENT {
	u8 BssCoexistMgmtSupport:1;
	u8 rsv:1;
	u8 ExtendChannelSwitch:1;
	u8 rsv2:5;
} EXT_CAP_INFO_ELEMENT, *PEXT_CAP_INFO_ELEMENT;

/* 802.11n 7.3.2.61 */
typedef struct PACKED _BSS_2040_COEXIST_ELEMENT {
	u8 ElementID;	/* ID = IE_2040_BSS_COEXIST = 72 */
	u8 Len;
	BSS_2040_COEXIST_IE BssCoexistIe;
} BSS_2040_COEXIST_ELEMENT, *PBSS_2040_COEXIST_ELEMENT;

/*802.11n 7.3.2.59 */
typedef struct PACKED _BSS_2040_INTOLERANT_CH_REPORT {
	u8 ElementID;	/* ID = IE_2040_BSS_INTOLERANT_REPORT = 73 */
	u8 Len;
	u8 RegulatoryClass;
	u8 ChList[0];
} BSS_2040_INTOLERANT_CH_REPORT, *PBSS_2040_INTOLERANT_CH_REPORT;

/* The structure for channel switch annoucement IE. This is in 802.11n D3.03 */
typedef struct PACKED _CHA_SWITCH_ANNOUNCE_IE {
	u8 SwitchMode;	/*channel switch mode */
	u8 NewChannel;	/* */
	u8 SwitchCount;	/* */
} CHA_SWITCH_ANNOUNCE_IE, *PCHA_SWITCH_ANNOUNCE_IE;

/* The structure for channel switch annoucement IE. This is in 802.11n D3.03 */
typedef struct PACKED _SEC_CHA_OFFSET_IE {
	u8 SecondaryChannelOffset;	/* 1: Secondary above, 3: Secondary below, 0: no Secondary */
} SEC_CHA_OFFSET_IE, *PSEC_CHA_OFFSET_IE;

/* This structure is extracted from struct RT_HT_CAPABILITY */
typedef struct {
	BOOLEAN bHtEnable;	/* If we should use ht rate. */
	BOOLEAN bPreNHt;	/* If we should use ht rate. */
	/*Substract from HT Capability IE */
	u8 MCSSet[16];
} RT_HT_PHY_INFO, *PRT_HT_PHY_INFO;

/*This structure substracts ralink supports from all 802.11n-related features. */
/*Features not listed here but contained in 802.11n spec are not supported in rt2860. */
typedef struct {
	u16 ChannelWidth:1;
	u16 MimoPs:2;	/*mimo power safe MMPS_ */
	u16 GF:1;		/*green field */
	u16 ShortGIfor20:1;
	u16 ShortGIfor40:1;	/*for40MHz */
	u16 TxSTBC:1;
	u16 RxSTBC:2;	/* 2 bits */
	u16 AmsduEnable:1;	/* Enable to transmit A-MSDU. Suggest disable. We should use A-MPDU to gain best benifit of 802.11n */
	u16 AmsduSize:1;	/* Max receiving A-MSDU size */
	u16 rsv:5;

	/*Substract from Addiont HT INFO IE */
	u8 MaxRAmpduFactor:2;
	u8 MpduDensity:3;
	u8 ExtChanOffset:2;	/* Please not the difference with following     u8   NewExtChannelOffset; from 802.11n */
	u8 RecomWidth:1;

	u16 OperaionMode:2;
	u16 NonGfPresent:1;
	u16 rsv3:1;
	u16 OBSS_NonHTExist:1;
	u16 rsv2:11;

	/* New Extension Channel Offset IE */
	u8 NewExtChannelOffset;
	/* Extension Capability IE = 127 */
	u8 BSSCoexist2040;
} RT_HT_CAPABILITY, *PRT_HT_CAPABILITY;

/*   field in Addtional HT Information IE . */
typedef struct PACKED {
	u8 ExtChanOffset:2;
	u8 RecomWidth:1;
	u8 RifsMode:1;
	u8 S_PSMPSup:1;	/*Indicate support for scheduled PSMP */
	u8 SerInterGranu:3;	/*service interval granularity */
} ADD_HTINFO, *PADD_HTINFO;

typedef struct PACKED {
	u16 OperaionMode:2;
	u16 NonGfPresent:1;
	u16 rsv:1;
	u16 OBSS_NonHTExist:1;
	u16 rsv2:11;
} ADD_HTINFO2, *PADD_HTINFO2;

/* TODO: Need sync with spec about the definition of StbcMcs. In Draft 3.03, it's reserved. */
typedef struct PACKED {
	u16 StbcMcs:6;
	u16 DualBeacon:1;
	u16 DualCTSProtect:1;
	u16 STBCBeacon:1;
	u16 LsigTxopProt:1;	/* L-SIG TXOP protection full support */
	u16 PcoActive:1;
	u16 PcoPhase:1;
	u16 rsv:4;
} ADD_HTINFO3, *PADD_HTINFO3;

#define SIZE_ADD_HT_INFO_IE		22
typedef struct PACKED {
	u8 ControlChan;
	ADD_HTINFO AddHtInfo;
	ADD_HTINFO2 AddHtInfo2;
	ADD_HTINFO3 AddHtInfo3;
	u8 MCSSet[16];	/* Basic MCS set */
} ADD_HT_INFO_IE, *PADD_HT_INFO_IE;

typedef struct PACKED {
	u8 NewExtChanOffset;
} NEW_EXT_CHAN_IE, *PNEW_EXT_CHAN_IE;

typedef struct PACKED _FRAME_802_11 {
	HEADER_802_11 Hdr;
	u8 Octet[1];
} FRAME_802_11, *PFRAME_802_11;

/* QoSNull embedding of management action. When HT Control MA field set to 1. */
typedef struct PACKED _MA_BODY {
	u8 Category;
	u8 Action;
	u8 Octet[1];
} MA_BODY, *PMA_BODY;

typedef struct PACKED _HEADER_802_3 {
	u8 DAAddr1[MAC_ADDR_LEN];
	u8 SAAddr2[MAC_ADDR_LEN];
	u8 Octet[2];
} HEADER_802_3, *PHEADER_802_3;
/*//Block ACK related format */
/* 2-byte BA Parameter  field  in       DELBA frames to terminate an already set up bA */
typedef struct PACKED {
	u16 Rsv:11;		/* always set to 0 */
	u16 Initiator:1;	/* 1: originator    0:recipient */
	u16 TID:4;		/* value of TC os TS */
} DELBA_PARM, *PDELBA_PARM;

/* 2-byte BA Parameter Set field  in ADDBA frames to signal parm for setting up a BA */
typedef struct PACKED {
	u16 AMSDUSupported:1;	/* 0: not permitted             1: permitted */
	u16 BAPolicy:1;	/* 1: immediately BA    0:delayed BA */
	u16 TID:4;		/* value of TC os TS */
	u16 BufSize:10;	/* number of buffe of size 2304 octetsr */
} BA_PARM, *PBA_PARM;

/* 2-byte BA Starting Seq CONTROL field */
typedef union PACKED {
	struct PACKED {
		u16 FragNum:4;	/* always set to 0 */
		u16 StartSeq:12;	/* sequence number of the 1st MSDU for which this BAR is sent */
	} field;
	u16 word;
} BASEQ_CONTROL, *PBASEQ_CONTROL;

/*BAControl and BARControl are the same */
/* 2-byte BA CONTROL field in BA frame */
typedef struct PACKED {
	u16 ACKPolicy:1;	/* only related to N-Delayed BA. But not support in RT2860b. 0:NormalACK  1:No ACK */
	u16 MTID:1;		/*EWC V1.24 */
	u16 Compressed:1;
	u16 Rsv:9;
	u16 TID:4;
} BA_CONTROL, *PBA_CONTROL;

/* 2-byte BAR CONTROL field in BAR frame */
typedef struct PACKED {
	u16 ACKPolicy:1;	/* 0:normal ack,  1:no ack. */
	u16 MTID:1;		/*if this bit1, use  FRAME_MTBA_REQ,  if 0, use FRAME_BA_REQ */
	u16 Compressed:1;
	u16 Rsv1:9;
	u16 TID:4;
} BAR_CONTROL, *PBAR_CONTROL;

/* BARControl in MTBAR frame */
typedef struct PACKED {
	u16 ACKPolicy:1;
	u16 MTID:1;
	u16 Compressed:1;
	u16 Rsv1:9;
	u16 NumTID:4;
} MTBAR_CONTROL, *PMTBAR_CONTROL;

typedef struct PACKED {
	u16 Rsv1:12;
	u16 TID:4;
} PER_TID_INFO, *PPER_TID_INFO;

typedef struct {
	PER_TID_INFO PerTID;
	BASEQ_CONTROL BAStartingSeq;
} EACH_TID, *PEACH_TID;

/* BAREQ AND MTBAREQ have the same subtype BAR, 802.11n BAR use compressed bitmap. */
typedef struct PACKED _FRAME_BA_REQ {
	FRAME_CONTROL FC;
	u16 Duration;
	u8 Addr1[MAC_ADDR_LEN];
	u8 Addr2[MAC_ADDR_LEN];
	BAR_CONTROL BARControl;
	BASEQ_CONTROL BAStartingSeq;
} FRAME_BA_REQ, *PFRAME_BA_REQ;

typedef struct PACKED _FRAME_MTBA_REQ {
	FRAME_CONTROL FC;
	u16 Duration;
	u8 Addr1[MAC_ADDR_LEN];
	u8 Addr2[MAC_ADDR_LEN];
	MTBAR_CONTROL MTBARControl;
	PER_TID_INFO PerTIDInfo;
	BASEQ_CONTROL BAStartingSeq;
} FRAME_MTBA_REQ, *PFRAME_MTBA_REQ;

/* Compressed format is mandantory in HT STA */
typedef struct PACKED _FRAME_MTBA {
	FRAME_CONTROL FC;
	u16 Duration;
	u8 Addr1[MAC_ADDR_LEN];
	u8 Addr2[MAC_ADDR_LEN];
	BA_CONTROL BAControl;
	BASEQ_CONTROL BAStartingSeq;
	u8 BitMap[8];
} FRAME_MTBA, *PFRAME_MTBA;

typedef struct PACKED _FRAME_PSMP_ACTION {
	HEADER_802_11 Hdr;
	u8 Category;
	u8 Action;
	u8 Psmp;		/* 7.3.1.25 */
} FRAME_PSMP_ACTION, *PFRAME_PSMP_ACTION;

typedef struct PACKED _FRAME_ACTION_HDR {
	HEADER_802_11 Hdr;
	u8 Category;
	u8 Action;
} FRAME_ACTION_HDR, *PFRAME_ACTION_HDR;

/*Action Frame */
/*Action Frame  Category:Spectrum,  Action:Channel Switch. 7.3.2.20 */
typedef struct PACKED _CHAN_SWITCH_ANNOUNCE {
	u8 ElementID;	/* ID = IE_CHANNEL_SWITCH_ANNOUNCEMENT = 37 */
	u8 Len;
	CHA_SWITCH_ANNOUNCE_IE CSAnnounceIe;
} CHAN_SWITCH_ANNOUNCE, *PCHAN_SWITCH_ANNOUNCE;

/*802.11n : 7.3.2.20a */
typedef struct PACKED _SECOND_CHAN_OFFSET {
	u8 ElementID;	/* ID = IE_SECONDARY_CH_OFFSET = 62 */
	u8 Len;
	SEC_CHA_OFFSET_IE SecChOffsetIe;
} SECOND_CHAN_OFFSET, *PSECOND_CHAN_OFFSET;

typedef struct PACKED _FRAME_SPETRUM_CS {
	HEADER_802_11 Hdr;
	u8 Category;
	u8 Action;
	CHAN_SWITCH_ANNOUNCE CSAnnounce;
	SECOND_CHAN_OFFSET SecondChannel;
} FRAME_SPETRUM_CS, *PFRAME_SPETRUM_CS;

typedef struct PACKED _FRAME_ADDBA_REQ {
	HEADER_802_11 Hdr;
	u8 Category;
	u8 Action;
	u8 Token;		/* 1 */
	BA_PARM BaParm;		/*  2 - 10 */
	u16 TimeOutValue;	/* 0 - 0 */
	BASEQ_CONTROL BaStartSeq;	/* 0-0 */
} FRAME_ADDBA_REQ, *PFRAME_ADDBA_REQ;

typedef struct PACKED _FRAME_ADDBA_RSP {
	HEADER_802_11 Hdr;
	u8 Category;
	u8 Action;
	u8 Token;
	u16 StatusCode;
	BA_PARM BaParm;		/*0 - 2 */
	u16 TimeOutValue;
} FRAME_ADDBA_RSP, *PFRAME_ADDBA_RSP;

typedef struct PACKED _FRAME_DELBA_REQ {
	HEADER_802_11 Hdr;
	u8 Category;
	u8 Action;
	DELBA_PARM DelbaParm;
	u16 ReasonCode;
} FRAME_DELBA_REQ, *PFRAME_DELBA_REQ;

/*7.2.1.7 */
typedef struct PACKED _FRAME_BAR {
	FRAME_CONTROL FC;
	u16 Duration;
	u8 Addr1[MAC_ADDR_LEN];
	u8 Addr2[MAC_ADDR_LEN];
	BAR_CONTROL BarControl;
	BASEQ_CONTROL StartingSeq;
} FRAME_BAR, *PFRAME_BAR;

/*7.2.1.7 */
typedef struct PACKED _FRAME_BA {
	FRAME_CONTROL FC;
	u16 Duration;
	u8 Addr1[MAC_ADDR_LEN];
	u8 Addr2[MAC_ADDR_LEN];
	BAR_CONTROL BarControl;
	BASEQ_CONTROL StartingSeq;
	u8 bitmask[8];
} FRAME_BA, *PFRAME_BA;

/* Radio Measuement Request Frame Format */
typedef struct PACKED _FRAME_RM_REQ_ACTION {
	HEADER_802_11 Hdr;
	u8 Category;
	u8 Action;
	u8 Token;
	u16 Repetition;
	u8 data[0];
} FRAME_RM_REQ_ACTION, *PFRAME_RM_REQ_ACTION;

typedef struct PACKED {
	u8 ID;
	u8 Length;
	u8 ChannelSwitchMode;
	u8 NewRegClass;
	u8 NewChannelNum;
	u8 ChannelSwitchCount;
} HT_EXT_CHANNEL_SWITCH_ANNOUNCEMENT_IE,
    *PHT_EXT_CHANNEL_SWITCH_ANNOUNCEMENT_IE;

/* */
/* _Limit must be the 2**n - 1 */
/* _SEQ1 , _SEQ2 must be within 0 ~ _Limit */
/* */
#define SEQ_STEPONE(_SEQ1, _SEQ2, _Limit)	((_SEQ1 == ((_SEQ2+1) & _Limit)))
#define SEQ_SMALLER(_SEQ1, _SEQ2, _Limit)	(((_SEQ1-_SEQ2) & ((_Limit+1)>>1)))
#define SEQ_LARGER(_SEQ1, _SEQ2, _Limit)	((_SEQ1 != _SEQ2) && !(((_SEQ1-_SEQ2) & ((_Limit+1)>>1))))
#define SEQ_WITHIN_WIN(_SEQ1, _SEQ2, _WIN, _Limit) (SEQ_LARGER(_SEQ1, _SEQ2, _Limit) &&  \
												SEQ_SMALLER(_SEQ1, ((_SEQ2+_WIN+1)&_Limit), _Limit))

/* */
/* Contention-free parameter (without ID and Length) */
/* */
typedef struct PACKED {
	BOOLEAN bValid;		/* 1: variable contains valid value */
	u8 CfpCount;
	u8 CfpPeriod;
	u16 CfpMaxDuration;
	u16 CfpDurRemaining;
} CF_PARM, *PCF_PARM;

typedef struct _CIPHER_SUITE {
	NDIS_802_11_ENCRYPTION_STATUS PairCipher;	/* Unicast cipher 1, this one has more secured cipher suite */
	NDIS_802_11_ENCRYPTION_STATUS PairCipherAux;	/* Unicast cipher 2 if AP announce two unicast cipher suite */
	NDIS_802_11_ENCRYPTION_STATUS GroupCipher;	/* Group cipher */
	u16 RsnCapability;	/* RSN capability from beacon */
	BOOLEAN bMixMode;	/* Indicate Pair & Group cipher might be different */
} CIPHER_SUITE, *PCIPHER_SUITE;

/* EDCA configuration from AP's BEACON/ProbeRsp */
typedef struct {
	BOOLEAN bValid;		/* 1: variable contains valid value */
	BOOLEAN bAdd;		/* 1: variable contains valid value */
	BOOLEAN bQAck;
	BOOLEAN bQueueRequest;
	BOOLEAN bTxopRequest;
	BOOLEAN bAPSDCapable;
/*  BOOLEAN     bMoreDataAck; */
	u8 EdcaUpdateCount;
	u8 Aifsn[4];		/* 0:AC_BK, 1:AC_BE, 2:AC_VI, 3:AC_VO */
	u8 Cwmin[4];
	u8 Cwmax[4];
	u16 Txop[4];		/* in unit of 32-us */
	BOOLEAN bACM[4];	/* 1: Admission Control of AC_BK is mandattory */
} EDCA_PARM, *PEDCA_PARM;

/* QBSS LOAD information from QAP's BEACON/ProbeRsp */
typedef struct {
	BOOLEAN bValid;		/* 1: variable contains valid value */
	u16 StaNum;
	u8 ChannelUtilization;
	u16 RemainingAdmissionControl;	/* in unit of 32-us */
} QBSS_LOAD_PARM, *PQBSS_LOAD_PARM;

/* QBSS Info field in QSTA's assoc req */
typedef struct PACKED {
	u8 UAPSD_AC_VO:1;
	u8 UAPSD_AC_VI:1;
	u8 UAPSD_AC_BK:1;
	u8 UAPSD_AC_BE:1;
	u8 Rsv1:1;
	u8 MaxSPLength:2;
	u8 Rsv2:1;
} QBSS_STA_INFO_PARM, *PQBSS_STA_INFO_PARM;

/* QBSS Info field in QAP's Beacon/ProbeRsp */
typedef struct PACKED {
	u8 ParamSetCount:4;
	u8 Rsv:3;
	u8 UAPSD:1;
} QBSS_AP_INFO_PARM, *PQBSS_AP_INFO_PARM;

/* QOS Capability reported in QAP's BEACON/ProbeRsp */
/* QOS Capability sent out in QSTA's AssociateReq/ReAssociateReq */
typedef struct {
	BOOLEAN bValid;		/* 1: variable contains valid value */
	BOOLEAN bQAck;
	BOOLEAN bQueueRequest;
	BOOLEAN bTxopRequest;
/*  BOOLEAN     bMoreDataAck; */
	u8 EdcaUpdateCount;
} QOS_CAPABILITY_PARM, *PQOS_CAPABILITY_PARM;

typedef struct {
	u8 IELen;
	u8 IE[MAX_CUSTOM_LEN];
} WPA_IE_;

typedef struct {
	u8 Bssid[MAC_ADDR_LEN];
	u8 Channel;
	u8 CentralChannel;	/*Store the wide-band central channel for 40MHz.  .used in 40MHz AP. Or this is the same as Channel. */
	u8 BssType;
	u16 AtimWin;
	u16 BeaconPeriod;

	u8 SupRate[MAX_LEN_OF_SUPPORTED_RATES];
	u8 SupRateLen;
	u8 ExtRate[MAX_LEN_OF_SUPPORTED_RATES];
	u8 ExtRateLen;
	HT_CAPABILITY_IE HtCapability;
	u8 HtCapabilityLen;
	ADD_HT_INFO_IE AddHtInfo;	/* AP might use this additional ht info IE */
	u8 AddHtInfoLen;
	u8 NewExtChanOffset;
	char Rssi;
	u8 Privacy;		/* Indicate security function ON/OFF. Don't mess up with auth mode. */
	u8 Hidden;

	u16 DtimPeriod;
	u16 CapabilityInfo;

	u16 CfpCount;
	u16 CfpPeriod;
	u16 CfpMaxDuration;
	u16 CfpDurRemaining;
	u8 SsidLen;
	char Ssid[MAX_LEN_OF_SSID];

	unsigned long LastBeaconRxTime;	/* OS's timestamp */

	BOOLEAN bSES;

	/* New for WPA2 */
	CIPHER_SUITE WPA;	/* AP announced WPA cipher suite */
	CIPHER_SUITE WPA2;	/* AP announced WPA2 cipher suite */

	/* New for microsoft WPA support */
	NDIS_802_11_FIXED_IEs FixIEs;
	NDIS_802_11_AUTHENTICATION_MODE AuthModeAux;	/* Addition mode for WPA2 / WPA capable AP */
	NDIS_802_11_AUTHENTICATION_MODE AuthMode;
	NDIS_802_11_WEP_STATUS WepStatus;	/* Unicast Encryption Algorithm extract from VAR_IE */
	u16 VarIELen;	/* Length of next VIE include EID & Length */
	u8 VarIEs[MAX_VIE_LEN];

	/* CCX Ckip information */
	u8 CkipFlag;

	/* CCX 2 TSF */
	u8 PTSF[4];		/* Parent TSF */
	u8 TTSF[8];		/* Target TSF */

	/* 802.11e d9, and WMM */
	EDCA_PARM EdcaParm;
	QOS_CAPABILITY_PARM QosCapability;
	QBSS_LOAD_PARM QbssLoad;
	WPA_IE_ WpaIE;
	WPA_IE_ RsnIE;
} BSS_ENTRY, *PBSS_ENTRY;

typedef struct {
	u8 BssNr;
	u8 BssOverlapNr;
	BSS_ENTRY BssEntry[MAX_LEN_OF_BSS_TABLE];
} BSS_TABLE, *PBSS_TABLE;

typedef struct _MLME_QUEUE_ELEM {
	unsigned long Machine;
	unsigned long MsgType;
	unsigned long MsgLen;
	u8 Msg[MGMT_DMA_BUFFER_SIZE];
	LARGE_INTEGER TimeStamp;
	u8 Rssi0;
	u8 Rssi1;
	u8 Rssi2;
	u8 Signal;
	u8 Channel;
	u8 Wcid;
	BOOLEAN Occupied;
} MLME_QUEUE_ELEM, *PMLME_QUEUE_ELEM;

typedef struct _MLME_QUEUE {
	unsigned long Num;
	unsigned long Head;
	unsigned long Tail;
	NDIS_SPIN_LOCK Lock;
	MLME_QUEUE_ELEM Entry[MAX_LEN_OF_MLME_QUEUE];
} MLME_QUEUE, *PMLME_QUEUE;

typedef void(*STATE_MACHINE_FUNC) (void * Adaptor, MLME_QUEUE_ELEM * Elem);

typedef struct _STATE_MACHINE {
	unsigned long Base;
	unsigned long NrState;
	unsigned long NrMsg;
	unsigned long CurrState;
	STATE_MACHINE_FUNC *TransFunc;
} STATE_MACHINE, *PSTATE_MACHINE;

/* MLME AUX data structure that hold temporarliy settings during a connection attempt. */
/* Once this attemp succeeds, all settings will be copy to pAd->StaActive. */
/* A connection attempt (user set OID, roaming, CCX fast roaming,..) consists of */
/* several steps (JOIN, AUTH, ASSOC or REASSOC) and may fail at any step. We purposely */
/* separate this under-trial settings away from pAd->StaActive so that once */
/* this new attempt failed, driver can auto-recover back to the active settings. */
typedef struct _MLME_AUX {
	u8 BssType;
	u8 Ssid[MAX_LEN_OF_SSID];
	u8 SsidLen;
	u8 Bssid[MAC_ADDR_LEN];
	u8 AutoReconnectSsid[MAX_LEN_OF_SSID];
	u8 AutoReconnectSsidLen;
	u16 Alg;
	u8 ScanType;
	u8 Channel;
	u8 CentralChannel;
	u16 Aid;
	u16 CapabilityInfo;
	u16 BeaconPeriod;
	u16 CfpMaxDuration;
	u16 CfpPeriod;
	u16 AtimWin;

	/* Copy supported rate from desired AP's beacon. We are trying to match */
	/* AP's supported and extended rate settings. */
	u8 SupRate[MAX_LEN_OF_SUPPORTED_RATES];
	u8 ExtRate[MAX_LEN_OF_SUPPORTED_RATES];
	u8 SupRateLen;
	u8 ExtRateLen;
	HT_CAPABILITY_IE HtCapability;
	u8 HtCapabilityLen;
	ADD_HT_INFO_IE AddHtInfo;	/* AP might use this additional ht info IE */
	u8 NewExtChannelOffset;
	/*RT_HT_CAPABILITY      SupportedHtPhy; */

	/* new for QOS */
	QOS_CAPABILITY_PARM APQosCapability;	/* QOS capability of the current associated AP */
	EDCA_PARM APEdcaParm;	/* EDCA parameters of the current associated AP */
	QBSS_LOAD_PARM APQbssLoad;	/* QBSS load of the current associated AP */

	/* new to keep Ralink specific feature */
	unsigned long APRalinkIe;

	BSS_TABLE SsidBssTab;	/* AP list for the same SSID */
	BSS_TABLE RoamTab;	/* AP list eligible for roaming */
	unsigned long BssIdx;
	unsigned long RoamIdx;

	BOOLEAN CurrReqIsFromNdis;

	RALINK_TIMER_STRUCT BeaconTimer, ScanTimer;
	RALINK_TIMER_STRUCT AuthTimer;
	RALINK_TIMER_STRUCT AssocTimer, ReassocTimer, DisassocTimer;
} MLME_AUX, *PMLME_AUX;

typedef struct _MLME_ADDBA_REQ_STRUCT {
	u8 Wcid;		/* */
	u8 pAddr[MAC_ADDR_LEN];
	u8 BaBufSize;
	u16 TimeOutValue;
	u8 TID;
	u8 Token;
	u16 BaStartSeq;
} MLME_ADDBA_REQ_STRUCT, *PMLME_ADDBA_REQ_STRUCT;

typedef struct _MLME_DELBA_REQ_STRUCT {
	u8 Wcid;		/* */
	u8 Addr[MAC_ADDR_LEN];
	u8 TID;
	u8 Initiator;
} MLME_DELBA_REQ_STRUCT, *PMLME_DELBA_REQ_STRUCT;

/* assoc struct is equal to reassoc */
typedef struct _MLME_ASSOC_REQ_STRUCT {
	u8 Addr[MAC_ADDR_LEN];
	u16 CapabilityInfo;
	u16 ListenIntv;
	unsigned long Timeout;
} MLME_ASSOC_REQ_STRUCT, *PMLME_ASSOC_REQ_STRUCT, MLME_REASSOC_REQ_STRUCT,
    *PMLME_REASSOC_REQ_STRUCT;

typedef struct _MLME_DISASSOC_REQ_STRUCT {
	u8 Addr[MAC_ADDR_LEN];
	u16 Reason;
} MLME_DISASSOC_REQ_STRUCT, *PMLME_DISASSOC_REQ_STRUCT;

typedef struct _MLME_AUTH_REQ_STRUCT {
	u8 Addr[MAC_ADDR_LEN];
	u16 Alg;
	unsigned long Timeout;
} MLME_AUTH_REQ_STRUCT, *PMLME_AUTH_REQ_STRUCT;

typedef struct _MLME_DEAUTH_REQ_STRUCT {
	u8 Addr[MAC_ADDR_LEN];
	u16 Reason;
} MLME_DEAUTH_REQ_STRUCT, *PMLME_DEAUTH_REQ_STRUCT;

typedef struct {
	unsigned long BssIdx;
} MLME_JOIN_REQ_STRUCT;

typedef struct _MLME_SCAN_REQ_STRUCT {
	u8 Bssid[MAC_ADDR_LEN];
	u8 BssType;
	u8 ScanType;
	u8 SsidLen;
	char Ssid[MAX_LEN_OF_SSID];
} MLME_SCAN_REQ_STRUCT, *PMLME_SCAN_REQ_STRUCT;

typedef struct _MLME_START_REQ_STRUCT {
	char Ssid[MAX_LEN_OF_SSID];
	u8 SsidLen;
} MLME_START_REQ_STRUCT, *PMLME_START_REQ_STRUCT;

typedef struct PACKED {
	u8 Eid;
	u8 Len;
	u8 Octet[1];
} EID_STRUCT, *PEID_STRUCT, BEACON_EID_STRUCT, *PBEACON_EID_STRUCT;

typedef struct PACKED _RTMP_TX_RATE_SWITCH {
	u8 ItemNo;
	u8 STBC:1;
	u8 ShortGI:1;
	u8 BW:1;
	u8 Rsv1:1;
	u8 Mode:2;
	u8 Rsv2:2;
	u8 CurrMCS;
	u8 TrainUp;
	u8 TrainDown;
} RRTMP_TX_RATE_SWITCH, *PRTMP_TX_RATE_SWITCH;

/* ========================== AP mlme.h =============================== */
#define TBTT_PRELOAD_TIME       384	/* usec. LomgPreamble + 24-byte at 1Mbps */
#define DEFAULT_DTIM_PERIOD     1

#define MAC_TABLE_AGEOUT_TIME			300	/* unit: sec */
#define MAC_TABLE_ASSOC_TIMEOUT			5	/* unit: sec */
#define MAC_TABLE_FULL(Tab)				((Tab).size == MAX_LEN_OF_MAC_TABLE)

/* AP shall drop the sta if contine Tx fail count reach it. */
#define MAC_ENTRY_LIFE_CHECK_CNT		20	/* packet cnt. */

/* Value domain of pMacEntry->Sst */
typedef enum _Sst {
	SST_NOT_AUTH,		/* 0: equivalent to IEEE 802.11/1999 state 1 */
	SST_AUTH,		/* 1: equivalent to IEEE 802.11/1999 state 2 */
	SST_ASSOC		/* 2: equivalent to IEEE 802.11/1999 state 3 */
} SST;

/* value domain of pMacEntry->AuthState */
typedef enum _AuthState {
	AS_NOT_AUTH,
	AS_AUTH_OPEN,		/* STA has been authenticated using OPEN SYSTEM */
	AS_AUTH_KEY,		/* STA has been authenticated using SHARED KEY */
	AS_AUTHENTICATING	/* STA is waiting for AUTH seq#3 using SHARED KEY */
} AUTH_STATE;

/*for-wpa value domain of pMacEntry->WpaState  802.1i D3   p.114 */
typedef enum _ApWpaState {
	AS_NOTUSE,		/* 0 */
	AS_DISCONNECT,		/* 1 */
	AS_DISCONNECTED,	/* 2 */
	AS_INITIALIZE,		/* 3 */
	AS_AUTHENTICATION,	/* 4 */
	AS_AUTHENTICATION2,	/* 5 */
	AS_INITPMK,		/* 6 */
	AS_INITPSK,		/* 7 */
	AS_PTKSTART,		/* 8 */
	AS_PTKINIT_NEGOTIATING,	/* 9 */
	AS_PTKINITDONE,		/* 10 */
	AS_UPDATEKEYS,		/* 11 */
	AS_INTEGRITY_FAILURE,	/* 12 */
	AS_KEYUPDATE,		/* 13 */
} AP_WPA_STATE;

/* for-wpa value domain of pMacEntry->WpaState  802.1i D3   p.114 */
typedef enum _GTKState {
	REKEY_NEGOTIATING,
	REKEY_ESTABLISHED,
	KEYERROR,
} GTK_STATE;

/*  for-wpa  value domain of pMacEntry->WpaState  802.1i D3   p.114 */
typedef enum _WpaGTKState {
	SETKEYS,
	SETKEYS_DONE,
} WPA_GTK_STATE;
/* ====================== end of AP mlme.h ============================ */

#endif /* MLME_H__ */
