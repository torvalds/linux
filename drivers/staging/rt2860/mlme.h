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
	--------	----------		------------------------------
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
#define REORDER_EXEC_INTV		100	/* 0.1 sec */

/* The definition of Radar detection duration region */
#define CE		0
#define FCC		1
#define JAP		2
#define JAP_W53	3
#define JAP_W56	4
#define MAX_RD_REGION 5

#define BEACON_LOST_TIME            (4 * OS_HZ)	/* 2048 msec = 2 sec */

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
#define TID_MAC_HASH(Addr, TID)            (TID^Addr[0] ^ Addr[1] ^ Addr[2] ^ Addr[3] ^ Addr[4] ^ Addr[5])
#define TID_MAC_HASH_INDEX(Addr, TID)      (TID_MAC_HASH(Addr, TID) % HASH_TABLE_SIZE)

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

#define CAP_GENERATE(ess, ibss, priv, s_pre, s_slot, spectrum)  (((ess) ? 0x0001 : 0x0000) | ((ibss) ? 0x0002 : 0x0000) | ((priv) ? 0x0010 : 0x0000) | ((s_pre) ? 0x0020 : 0x0000) | ((s_slot) ? 0x0400 : 0x0000) | ((spectrum) ? 0x0100 : 0x0000))

#define ERP_IS_NON_ERP_PRESENT(x)        (((x) & 0x01) != 0)	/* 802.11g */
#define ERP_IS_USE_PROTECTION(x)         (((x) & 0x02) != 0)	/* 802.11g */
#define ERP_IS_USE_BARKER_PREAMBLE(x)    (((x) & 0x04) != 0)	/* 802.11g */

#define DRS_TX_QUALITY_WORST_BOUND       8	/* 3  // just test by gary */
#define DRS_PENALTY                      8

#define BA_NOTUSE	2
/*BA Policy subfiled value in ADDBA frame */
#define IMMED_BA	1
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
if (((__pEntry)) != NULL) { \
	(__pEntry)->OneSecTxRetryOkCount = 0; \
	(__pEntry)->OneSecTxFailCount = 0; \
	(__pEntry)->OneSecTxNoRetryOkCount = 0; \
}

/* */
/* 802.11 frame formats */
/* */
/*  HT Capability INFO field in HT Cap IE . */
struct PACKED rt_ht_cap_info {
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
};

/*  HT Capability INFO field in HT Cap IE . */
struct PACKED rt_ht_cap_parm {
	u8 MaxRAmpduFactor:2;
	u8 MpduDensity:3;
	u8 rsv:3;		/*momi power safe */
};

/*  HT Capability INFO field in HT Cap IE . */
struct PACKED rt_ht_mcs_set {
	u8 MCSSet[10];
	u8 SupRate[2];	/* unit : 1Mbps */
	u8 TxMCSSetDefined:1;
	u8 TxRxNotEqual:1;
	u8 TxStream:2;
	u8 MpduDensity:1;
	u8 rsv:3;
	u8 rsv3[3];
};

/*  HT Capability INFO field in HT Cap IE . */
struct PACKED rt_ext_ht_cap_info {
	u16 Pco:1;
	u16 TranTime:2;
	u16 rsv:5;		/*momi power safe */
	u16 MCSFeedback:2;	/*0:no MCS feedback, 2:unsolicited MCS feedback, 3:Full MCS feedback,  1:rsv. */
	u16 PlusHTC:1;	/*+HTC control field support */
	u16 RDGSupport:1;	/*reverse Direction Grant  support */
	u16 rsv2:4;
};

/*  HT Beamforming field in HT Cap IE . */
struct PACKED rt_ht_bf_cap {
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
};

/*  HT antenna selection field in HT Cap IE . */
struct PACKED rt_ht_as_cap {
	u8 AntSelect:1;
	u8 ExpCSIFbkTxASEL:1;
	u8 AntIndFbkTxASEL:1;
	u8 ExpCSIFbk:1;
	u8 AntIndFbk:1;
	u8 RxASel:1;
	u8 TxSoundPPDU:1;
	u8 rsv:1;
};

/* Draft 1.0 set IE length 26, but is extensible.. */
#define SIZE_HT_CAP_IE		26
/* The structure for HT Capability IE. */
struct PACKED rt_ht_capability_ie {
	struct rt_ht_cap_info HtCapInfo;
	struct rt_ht_cap_parm HtCapParm;
/*      struct rt_ht_mcs_set              HtMCSSet; */
	u8 MCSSet[16];
	struct rt_ext_ht_cap_info ExtHtCapInfo;
	struct rt_ht_bf_cap TxBFCap;	/* beamforming cap. rt2860c not support beamforming. */
	struct rt_ht_as_cap ASCap;	/*antenna selection. */
};

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

struct PACKED rt_overlap_bss_scan_ie {
	u16 ScanPassiveDwell;
	u16 ScanActiveDwell;
	u16 TriggerScanInt;	/* Trigger scan interval */
	u16 PassiveTalPerChannel;	/* passive total per channel */
	u16 ActiveTalPerChannel;	/* active total per channel */
	u16 DelayFactor;	/* BSS width channel transition delay factor */
	u16 ScanActThre;	/* Scan Activity threshold */
};

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

struct rt_trigger_eventa {
	BOOLEAN bValid;
	u8 BSSID[6];
	u8 RegClass;		/* Regulatory Class */
	u16 Channel;
	unsigned long CDCounter;	/* Maintain a separate count down counter for each Event A. */
};

/* 20/40 trigger event table */
/* If one Event A delete or created, or if Event B is detected or not detected, STA should send 2040BSSCoexistence to AP. */
#define MAX_TRIGGER_EVENT		64
struct rt_trigger_event_tab {
	u8 EventANo;
	struct rt_trigger_eventa EventA[MAX_TRIGGER_EVENT];
	unsigned long EventBCountDown;	/* Count down counter for Event B. */
};

/* 7.3.27 20/40 Bss Coexistence Mgmt capability used in extended capabilities information IE( ID = 127 = IE_EXT_CAPABILITY). */
/*      This is the first octet and was defined in 802.11n D3.03 and 802.11yD9.0 */
struct PACKED rt_ext_cap_info_element {
	u8 BssCoexistMgmtSupport:1;
	u8 rsv:1;
	u8 ExtendChannelSwitch:1;
	u8 rsv2:5;
};

/* 802.11n 7.3.2.61 */
struct PACKED rt_bss_2040_coexist_element {
	u8 ElementID;	/* ID = IE_2040_BSS_COEXIST = 72 */
	u8 Len;
	BSS_2040_COEXIST_IE BssCoexistIe;
};

/*802.11n 7.3.2.59 */
struct PACKED rt_bss_2040_intolerant_ch_report {
	u8 ElementID;	/* ID = IE_2040_BSS_INTOLERANT_REPORT = 73 */
	u8 Len;
	u8 RegulatoryClass;
	u8 ChList[0];
};

/* The structure for channel switch annoucement IE. This is in 802.11n D3.03 */
struct PACKED rt_cha_switch_announce_ie {
	u8 SwitchMode;	/*channel switch mode */
	u8 NewChannel;	/* */
	u8 SwitchCount;	/* */
};

/* The structure for channel switch annoucement IE. This is in 802.11n D3.03 */
struct PACKED rt_sec_cha_offset_ie {
	u8 SecondaryChannelOffset;	/* 1: Secondary above, 3: Secondary below, 0: no Secondary */
};

/* This structure is extracted from struct struct rt_ht_capability */
struct rt_ht_phy_info {
	BOOLEAN bHtEnable;	/* If we should use ht rate. */
	BOOLEAN bPreNHt;	/* If we should use ht rate. */
	/*Substract from HT Capability IE */
	u8 MCSSet[16];
};

/*This structure substracts ralink supports from all 802.11n-related features. */
/*Features not listed here but contained in 802.11n spec are not supported in rt2860. */
struct rt_ht_capability {
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
};

/*   field in Addtional HT Information IE . */
struct PACKED rt_add_htinfo {
	u8 ExtChanOffset:2;
	u8 RecomWidth:1;
	u8 RifsMode:1;
	u8 S_PSMPSup:1;	/*Indicate support for scheduled PSMP */
	u8 SerInterGranu:3;	/*service interval granularity */
};

struct PACKED rt_add_htinfo2 {
	u16 OperaionMode:2;
	u16 NonGfPresent:1;
	u16 rsv:1;
	u16 OBSS_NonHTExist:1;
	u16 rsv2:11;
};

/* TODO: Need sync with spec about the definition of StbcMcs. In Draft 3.03, it's reserved. */
struct PACKED rt_add_htinfo3 {
	u16 StbcMcs:6;
	u16 DualBeacon:1;
	u16 DualCTSProtect:1;
	u16 STBCBeacon:1;
	u16 LsigTxopProt:1;	/* L-SIG TXOP protection full support */
	u16 PcoActive:1;
	u16 PcoPhase:1;
	u16 rsv:4;
};

#define SIZE_ADD_HT_INFO_IE		22
struct PACKED rt_add_ht_info_ie {
	u8 ControlChan;
	struct rt_add_htinfo AddHtInfo;
	struct rt_add_htinfo2 AddHtInfo2;
	struct rt_add_htinfo3 AddHtInfo3;
	u8 MCSSet[16];	/* Basic MCS set */
};

struct PACKED rt_new_ext_chan_ie {
	u8 NewExtChanOffset;
};

struct PACKED rt_frame_802_11 {
	struct rt_header_802_11 Hdr;
	u8 Octet[1];
};

/* QoSNull embedding of management action. When HT Control MA field set to 1. */
struct PACKED rt_ma_body {
	u8 Category;
	u8 Action;
	u8 Octet[1];
};

struct PACKED rt_header_802_3 {
	u8 DAAddr1[MAC_ADDR_LEN];
	u8 SAAddr2[MAC_ADDR_LEN];
	u8 Octet[2];
};
/*//Block ACK related format */
/* 2-byte BA Parameter  field  in       DELBA frames to terminate an already set up bA */
struct PACKED rt_delba_parm {
	u16 Rsv:11;		/* always set to 0 */
	u16 Initiator:1;	/* 1: originator    0:recipient */
	u16 TID:4;		/* value of TC os TS */
};

/* 2-byte BA Parameter Set field  in ADDBA frames to signal parm for setting up a BA */
struct PACKED rt_ba_parm {
	u16 AMSDUSupported:1;	/* 0: not permitted             1: permitted */
	u16 BAPolicy:1;	/* 1: immediately BA    0:delayed BA */
	u16 TID:4;		/* value of TC os TS */
	u16 BufSize:10;	/* number of buffe of size 2304 octetsr */
};

/* 2-byte BA Starting Seq CONTROL field */
typedef union PACKED _BASEQ_CONTROL {
	struct PACKED {
		u16 FragNum:4;	/* always set to 0 */
		u16 StartSeq:12;	/* sequence number of the 1st MSDU for which this BAR is sent */
	} field;
	u16 word;
} BASEQ_CONTROL, *PBASEQ_CONTROL;

/*BAControl and BARControl are the same */
/* 2-byte BA CONTROL field in BA frame */
struct PACKED rt_ba_control {
	u16 ACKPolicy:1;	/* only related to N-Delayed BA. But not support in RT2860b. 0:NormalACK  1:No ACK */
	u16 MTID:1;		/*EWC V1.24 */
	u16 Compressed:1;
	u16 Rsv:9;
	u16 TID:4;
};

/* 2-byte BAR CONTROL field in BAR frame */
struct PACKED rt_bar_control {
	u16 ACKPolicy:1;	/* 0:normal ack,  1:no ack. */
	u16 MTID:1;		/*if this bit1, use  struct rt_frame_mtba_req,  if 0, use struct rt_frame_ba_req */
	u16 Compressed:1;
	u16 Rsv1:9;
	u16 TID:4;
};

/* BARControl in MTBAR frame */
struct PACKED rt_mtbar_control {
	u16 ACKPolicy:1;
	u16 MTID:1;
	u16 Compressed:1;
	u16 Rsv1:9;
	u16 NumTID:4;
};

struct PACKED rt_per_tid_info {
	u16 Rsv1:12;
	u16 TID:4;
};

struct rt_each_tid {
	struct rt_per_tid_info PerTID;
	BASEQ_CONTROL BAStartingSeq;
};

/* BAREQ AND MTBAREQ have the same subtype BAR, 802.11n BAR use compressed bitmap. */
struct PACKED rt_frame_ba_req {
	struct rt_frame_control FC;
	u16 Duration;
	u8 Addr1[MAC_ADDR_LEN];
	u8 Addr2[MAC_ADDR_LEN];
	struct rt_bar_control BARControl;
	BASEQ_CONTROL BAStartingSeq;
};

struct PACKED rt_frame_mtba_req {
	struct rt_frame_control FC;
	u16 Duration;
	u8 Addr1[MAC_ADDR_LEN];
	u8 Addr2[MAC_ADDR_LEN];
	struct rt_mtbar_control MTBARControl;
	struct rt_per_tid_info PerTIDInfo;
	BASEQ_CONTROL BAStartingSeq;
};

/* Compressed format is mandantory in HT STA */
struct PACKED rt_frame_mtba {
	struct rt_frame_control FC;
	u16 Duration;
	u8 Addr1[MAC_ADDR_LEN];
	u8 Addr2[MAC_ADDR_LEN];
	struct rt_ba_control BAControl;
	BASEQ_CONTROL BAStartingSeq;
	u8 BitMap[8];
};

struct PACKED rt_frame_psmp_action {
	struct rt_header_802_11 Hdr;
	u8 Category;
	u8 Action;
	u8 Psmp;		/* 7.3.1.25 */
};

struct PACKED rt_frame_action_hdr {
	struct rt_header_802_11 Hdr;
	u8 Category;
	u8 Action;
};

/*Action Frame */
/*Action Frame  Category:Spectrum,  Action:Channel Switch. 7.3.2.20 */
struct PACKED rt_chan_switch_announce {
	u8 ElementID;	/* ID = IE_CHANNEL_SWITCH_ANNOUNCEMENT = 37 */
	u8 Len;
	struct rt_cha_switch_announce_ie CSAnnounceIe;
};

/*802.11n : 7.3.2.20a */
struct PACKED rt_second_chan_offset {
	u8 ElementID;	/* ID = IE_SECONDARY_CH_OFFSET = 62 */
	u8 Len;
	struct rt_sec_cha_offset_ie SecChOffsetIe;
};

struct PACKED rt_frame_spetrum_cs {
	struct rt_header_802_11 Hdr;
	u8 Category;
	u8 Action;
	struct rt_chan_switch_announce CSAnnounce;
	struct rt_second_chan_offset SecondChannel;
};

struct PACKED rt_frame_addba_req {
	struct rt_header_802_11 Hdr;
	u8 Category;
	u8 Action;
	u8 Token;		/* 1 */
	struct rt_ba_parm BaParm;		/*  2 - 10 */
	u16 TimeOutValue;	/* 0 - 0 */
	BASEQ_CONTROL BaStartSeq;	/* 0-0 */
};

struct PACKED rt_frame_addba_rsp {
	struct rt_header_802_11 Hdr;
	u8 Category;
	u8 Action;
	u8 Token;
	u16 StatusCode;
	struct rt_ba_parm BaParm;		/*0 - 2 */
	u16 TimeOutValue;
};

struct PACKED rt_frame_delba_req {
	struct rt_header_802_11 Hdr;
	u8 Category;
	u8 Action;
	struct rt_delba_parm DelbaParm;
	u16 ReasonCode;
};

/*7.2.1.7 */
struct PACKED rt_frame_bar {
	struct rt_frame_control FC;
	u16 Duration;
	u8 Addr1[MAC_ADDR_LEN];
	u8 Addr2[MAC_ADDR_LEN];
	struct rt_bar_control BarControl;
	BASEQ_CONTROL StartingSeq;
};

/*7.2.1.7 */
struct PACKED rt_frame_ba {
	struct rt_frame_control FC;
	u16 Duration;
	u8 Addr1[MAC_ADDR_LEN];
	u8 Addr2[MAC_ADDR_LEN];
	struct rt_bar_control BarControl;
	BASEQ_CONTROL StartingSeq;
	u8 bitmask[8];
};

/* Radio Measuement Request Frame Format */
struct PACKED rt_frame_rm_req_action {
	struct rt_header_802_11 Hdr;
	u8 Category;
	u8 Action;
	u8 Token;
	u16 Repetition;
	u8 data[0];
};

struct PACKED rt_ht_ext_channel_switch_announcement_ie {
	u8 ID;
	u8 Length;
	u8 ChannelSwitchMode;
	u8 NewRegClass;
	u8 NewChannelNum;
	u8 ChannelSwitchCount;
};

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
struct PACKED rt_cf_parm {
	BOOLEAN bValid;		/* 1: variable contains valid value */
	u8 CfpCount;
	u8 CfpPeriod;
	u16 CfpMaxDuration;
	u16 CfpDurRemaining;
};

struct rt_cipher_suite {
	NDIS_802_11_ENCRYPTION_STATUS PairCipher;	/* Unicast cipher 1, this one has more secured cipher suite */
	NDIS_802_11_ENCRYPTION_STATUS PairCipherAux;	/* Unicast cipher 2 if AP announce two unicast cipher suite */
	NDIS_802_11_ENCRYPTION_STATUS GroupCipher;	/* Group cipher */
	u16 RsnCapability;	/* RSN capability from beacon */
	BOOLEAN bMixMode;	/* Indicate Pair & Group cipher might be different */
};

/* EDCA configuration from AP's BEACON/ProbeRsp */
struct rt_edca_parm {
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
};

/* QBSS LOAD information from QAP's BEACON/ProbeRsp */
struct rt_qbss_load_parm {
	BOOLEAN bValid;		/* 1: variable contains valid value */
	u16 StaNum;
	u8 ChannelUtilization;
	u16 RemainingAdmissionControl;	/* in unit of 32-us */
};

/* QBSS Info field in QSTA's assoc req */
struct PACKED rt_qbss_sta_info_parm {
	u8 UAPSD_AC_VO:1;
	u8 UAPSD_AC_VI:1;
	u8 UAPSD_AC_BK:1;
	u8 UAPSD_AC_BE:1;
	u8 Rsv1:1;
	u8 MaxSPLength:2;
	u8 Rsv2:1;
};

/* QBSS Info field in QAP's Beacon/ProbeRsp */
struct PACKED rt_qbss_ap_info_parm {
	u8 ParamSetCount:4;
	u8 Rsv:3;
	u8 UAPSD:1;
};

/* QOS Capability reported in QAP's BEACON/ProbeRsp */
/* QOS Capability sent out in QSTA's AssociateReq/ReAssociateReq */
struct rt_qos_capability_parm {
	BOOLEAN bValid;		/* 1: variable contains valid value */
	BOOLEAN bQAck;
	BOOLEAN bQueueRequest;
	BOOLEAN bTxopRequest;
/*  BOOLEAN     bMoreDataAck; */
	u8 EdcaUpdateCount;
};

struct rt_wpa_ie {
	u8 IELen;
	u8 IE[MAX_CUSTOM_LEN];
};

struct rt_bss_entry {
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
	struct rt_ht_capability_ie HtCapability;
	u8 HtCapabilityLen;
	struct rt_add_ht_info_ie AddHtInfo;	/* AP might use this additional ht info IE */
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
	struct rt_cipher_suite WPA;	/* AP announced WPA cipher suite */
	struct rt_cipher_suite WPA2;	/* AP announced WPA2 cipher suite */

	/* New for microsoft WPA support */
	struct rt_ndis_802_11_fixed_ies FixIEs;
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
	struct rt_edca_parm EdcaParm;
	struct rt_qos_capability_parm QosCapability;
	struct rt_qbss_load_parm QbssLoad;
	struct rt_wpa_ie WpaIE;
	struct rt_wpa_ie RsnIE;
};

struct rt_bss_table {
	u8 BssNr;
	u8 BssOverlapNr;
	struct rt_bss_entry BssEntry[MAX_LEN_OF_BSS_TABLE];
};

struct rt_mlme_queue_elem {
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
};

struct rt_mlme_queue {
	unsigned long Num;
	unsigned long Head;
	unsigned long Tail;
	spinlock_t Lock;
	struct rt_mlme_queue_elem Entry[MAX_LEN_OF_MLME_QUEUE];
};

typedef void(*STATE_MACHINE_FUNC) (void *Adaptor, struct rt_mlme_queue_elem *Elem);

struct rt_state_machine {
	unsigned long Base;
	unsigned long NrState;
	unsigned long NrMsg;
	unsigned long CurrState;
	STATE_MACHINE_FUNC *TransFunc;
};

/* MLME AUX data structure that hold temporarliy settings during a connection attempt. */
/* Once this attemp succeeds, all settings will be copy to pAd->StaActive. */
/* A connection attempt (user set OID, roaming, CCX fast roaming,..) consists of */
/* several steps (JOIN, AUTH, ASSOC or REASSOC) and may fail at any step. We purposely */
/* separate this under-trial settings away from pAd->StaActive so that once */
/* this new attempt failed, driver can auto-recover back to the active settings. */
struct rt_mlme_aux {
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
	struct rt_ht_capability_ie HtCapability;
	u8 HtCapabilityLen;
	struct rt_add_ht_info_ie AddHtInfo;	/* AP might use this additional ht info IE */
	u8 NewExtChannelOffset;
	/*struct rt_ht_capability      SupportedHtPhy; */

	/* new for QOS */
	struct rt_qos_capability_parm APQosCapability;	/* QOS capability of the current associated AP */
	struct rt_edca_parm APEdcaParm;	/* EDCA parameters of the current associated AP */
	struct rt_qbss_load_parm APQbssLoad;	/* QBSS load of the current associated AP */

	/* new to keep Ralink specific feature */
	unsigned long APRalinkIe;

	struct rt_bss_table SsidBssTab;	/* AP list for the same SSID */
	struct rt_bss_table RoamTab;	/* AP list eligible for roaming */
	unsigned long BssIdx;
	unsigned long RoamIdx;

	BOOLEAN CurrReqIsFromNdis;

	struct rt_ralink_timer BeaconTimer, ScanTimer;
	struct rt_ralink_timer AuthTimer;
	struct rt_ralink_timer AssocTimer, ReassocTimer, DisassocTimer;
};

struct rt_mlme_addba_req {
	u8 Wcid;		/* */
	u8 pAddr[MAC_ADDR_LEN];
	u8 BaBufSize;
	u16 TimeOutValue;
	u8 TID;
	u8 Token;
	u16 BaStartSeq;
};

struct rt_mlme_delba_req {
	u8 Wcid;		/* */
	u8 Addr[MAC_ADDR_LEN];
	u8 TID;
	u8 Initiator;
};

/* assoc struct is equal to reassoc */
struct rt_mlme_assoc_req {
	u8 Addr[MAC_ADDR_LEN];
	u16 CapabilityInfo;
	u16 ListenIntv;
	unsigned long Timeout;
};

struct rt_mlme_disassoc_req {
	u8 Addr[MAC_ADDR_LEN];
	u16 Reason;
};

struct rt_mlme_auth_req {
	u8 Addr[MAC_ADDR_LEN];
	u16 Alg;
	unsigned long Timeout;
};

struct rt_mlme_deauth_req {
	u8 Addr[MAC_ADDR_LEN];
	u16 Reason;
};

struct rt_mlme_join_req {
	unsigned long BssIdx;
};

struct rt_mlme_scan_req {
	u8 Bssid[MAC_ADDR_LEN];
	u8 BssType;
	u8 ScanType;
	u8 SsidLen;
	char Ssid[MAX_LEN_OF_SSID];
};

struct rt_mlme_start_req {
	char Ssid[MAX_LEN_OF_SSID];
	u8 SsidLen;
};

struct PACKED rt_eid {
	u8 Eid;
	u8 Len;
	u8 Octet[1];
};

struct PACKED rt_rtmp_tx_rate_switch {
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
};

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
