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

//extern UCHAR BROADCAST_ADDR[];

// maximum supported capability information -
// ESS, IBSS, Privacy, Short Preamble, Spectrum mgmt, Short Slot
#define SUPPORTED_CAPABILITY_INFO   0x0533

#define END_OF_ARGS                 -1
#define LFSR_MASK                   0x80000057
#define MLME_TASK_EXEC_INTV         100/*200*/       //
#define LEAD_TIME                   5
#define MLME_TASK_EXEC_MULTIPLE       10  /*5*/       // MLME_TASK_EXEC_MULTIPLE * MLME_TASK_EXEC_INTV = 1 sec
#define REORDER_EXEC_INTV         	100       // 0.1 sec
//#define TBTT_PRELOAD_TIME         384        // usec. LomgPreamble + 24-byte at 1Mbps

// The definition of Radar detection duration region
#define CE		0
#define FCC		1
#define JAP		2
#define JAP_W53	3
#define JAP_W56	4
#define MAX_RD_REGION 5

#ifdef	NDIS51_MINIPORT
#define BEACON_LOST_TIME            4000       // 2048 msec = 2 sec
#else
#define BEACON_LOST_TIME            4 * OS_HZ    // 2048 msec = 2 sec
#endif

#define DLS_TIMEOUT                 1200      // unit: msec
#define AUTH_TIMEOUT                300       // unit: msec
#define ASSOC_TIMEOUT               300       // unit: msec
#define JOIN_TIMEOUT                2 * OS_HZ      // unit: msec
#define SHORT_CHANNEL_TIME          90        // unit: msec
#define MIN_CHANNEL_TIME            110        // unit: msec, for dual band scan
#define MAX_CHANNEL_TIME            140       // unit: msec, for single band scan
#define	FAST_ACTIVE_SCAN_TIME	    30 		  // Active scan waiting for probe response time
#define CW_MIN_IN_BITS              4         // actual CwMin = 2^CW_MIN_IN_BITS - 1

#ifndef CONFIG_AP_SUPPORT
#define CW_MAX_IN_BITS              10        // actual CwMax = 2^CW_MAX_IN_BITS - 1
#endif

// Note: RSSI_TO_DBM_OFFSET has been changed to variable for new RF (2004-0720).
// SHould not refer to this constant anymore
//#define RSSI_TO_DBM_OFFSET          120 // for RT2530 RSSI-115 = dBm
#define RSSI_FOR_MID_TX_POWER       -55  // -55 db is considered mid-distance
#define RSSI_FOR_LOW_TX_POWER       -45  // -45 db is considered very short distance and
                                        // eligible to use a lower TX power
#define RSSI_FOR_LOWEST_TX_POWER    -30
//#define MID_TX_POWER_DELTA          0   // 0 db from full TX power upon mid-distance to AP
#define LOW_TX_POWER_DELTA          6    // -3 db from full TX power upon very short distance. 1 grade is 0.5 db
#define LOWEST_TX_POWER_DELTA       16   // -8 db from full TX power upon shortest distance. 1 grade is 0.5 db

#define RSSI_TRIGGERED_UPON_BELOW_THRESHOLD     0
#define RSSI_TRIGGERED_UPON_EXCCEED_THRESHOLD   1
#define RSSI_THRESHOLD_FOR_ROAMING              25
#define RSSI_DELTA                              5

// Channel Quality Indication
#define CQI_IS_GOOD(cqi)            ((cqi) >= 50)
//#define CQI_IS_FAIR(cqi)          (((cqi) >= 20) && ((cqi) < 50))
#define CQI_IS_POOR(cqi)            (cqi < 50)  //(((cqi) >= 5) && ((cqi) < 20))
#define CQI_IS_BAD(cqi)             (cqi < 5)
#define CQI_IS_DEAD(cqi)            (cqi == 0)

// weighting factor to calculate Channel quality, total should be 100%
#define RSSI_WEIGHTING                   50
#define TX_WEIGHTING                     30
#define RX_WEIGHTING                     20

//#define PEER_KEY_NOT_USED                0
//#define PEER_KEY_64_BIT                  64
//#define PEER_KEY_128_BIT                 128

//#define PEER_KEY_64BIT_LEN               8
//#define PEER_KEY_128BIT_LEN              16

#define BSS_NOT_FOUND                    0xFFFFFFFF

#define MAX_LEN_OF_MLME_QUEUE            40 //10

#define SCAN_PASSIVE                     18		// scan with no probe request, only wait beacon and probe response
#define SCAN_ACTIVE                      19		// scan with probe request, and wait beacon and probe response
#define	SCAN_CISCO_PASSIVE				 20		// Single channel passive scan
#define	SCAN_CISCO_ACTIVE				 21		// Single channel active scan
#define	SCAN_CISCO_NOISE				 22		// Single channel passive scan for noise histogram collection
#define	SCAN_CISCO_CHANNEL_LOAD			 23		// Single channel passive scan for channel load collection
#define FAST_SCAN_ACTIVE                 24		// scan with probe request, and wait beacon and probe response

//#define BSS_TABLE_EMPTY(x)             ((x).BssNr == 0)
#define MAC_ADDR_IS_GROUP(Addr)       (((Addr[0]) & 0x01))
#define MAC_ADDR_HASH(Addr)            (Addr[0] ^ Addr[1] ^ Addr[2] ^ Addr[3] ^ Addr[4] ^ Addr[5])
#define MAC_ADDR_HASH_INDEX(Addr)      (MAC_ADDR_HASH(Addr) % HASH_TABLE_SIZE)
#define TID_MAC_HASH(Addr,TID)            (TID^Addr[0] ^ Addr[1] ^ Addr[2] ^ Addr[3] ^ Addr[4] ^ Addr[5])
#define TID_MAC_HASH_INDEX(Addr,TID)      (TID_MAC_HASH(Addr,TID) % HASH_TABLE_SIZE)

// LED Control
// assoiation ON. one LED ON. another blinking when TX, OFF when idle
// no association, both LED off
#define ASIC_LED_ACT_ON(pAd)        RTMP_IO_WRITE32(pAd, MAC_CSR14, 0x00031e46)
#define ASIC_LED_ACT_OFF(pAd)       RTMP_IO_WRITE32(pAd, MAC_CSR14, 0x00001e46)

// bit definition of the 2-byte pBEACON->Capability field
#define CAP_IS_ESS_ON(x)                 (((x) & 0x0001) != 0)
#define CAP_IS_IBSS_ON(x)                (((x) & 0x0002) != 0)
#define CAP_IS_CF_POLLABLE_ON(x)         (((x) & 0x0004) != 0)
#define CAP_IS_CF_POLL_REQ_ON(x)         (((x) & 0x0008) != 0)
#define CAP_IS_PRIVACY_ON(x)             (((x) & 0x0010) != 0)
#define CAP_IS_SHORT_PREAMBLE_ON(x)      (((x) & 0x0020) != 0)
#define CAP_IS_PBCC_ON(x)                (((x) & 0x0040) != 0)
#define CAP_IS_AGILITY_ON(x)             (((x) & 0x0080) != 0)
#define CAP_IS_SPECTRUM_MGMT(x)          (((x) & 0x0100) != 0)  // 802.11e d9
#define CAP_IS_QOS(x)                    (((x) & 0x0200) != 0)  // 802.11e d9
#define CAP_IS_SHORT_SLOT(x)             (((x) & 0x0400) != 0)
#define CAP_IS_APSD(x)                   (((x) & 0x0800) != 0)  // 802.11e d9
#define CAP_IS_IMMED_BA(x)               (((x) & 0x1000) != 0)  // 802.11e d9
#define CAP_IS_DSSS_OFDM(x)              (((x) & 0x2000) != 0)
#define CAP_IS_DELAY_BA(x)               (((x) & 0x4000) != 0)  // 802.11e d9

#define CAP_GENERATE(ess,ibss,priv,s_pre,s_slot,spectrum)  (((ess) ? 0x0001 : 0x0000) | ((ibss) ? 0x0002 : 0x0000) | ((priv) ? 0x0010 : 0x0000) | ((s_pre) ? 0x0020 : 0x0000) | ((s_slot) ? 0x0400 : 0x0000) | ((spectrum) ? 0x0100 : 0x0000))

//#define STA_QOS_CAPABILITY               0 // 1-byte. see 802.11e d9.0 for bit definition

#define ERP_IS_NON_ERP_PRESENT(x)        (((x) & 0x01) != 0)    // 802.11g
#define ERP_IS_USE_PROTECTION(x)         (((x) & 0x02) != 0)    // 802.11g
#define ERP_IS_USE_BARKER_PREAMBLE(x)    (((x) & 0x04) != 0)    // 802.11g

#define DRS_TX_QUALITY_WORST_BOUND       8// 3  // just test by gary
#define DRS_PENALTY                      8

#define BA_NOTUSE 	2
//BA Policy subfiled value in ADDBA frame
#define IMMED_BA 	1
#define DELAY_BA	0

// BA Initiator subfield in DELBA frame
#define ORIGINATOR	1
#define RECIPIENT	0

// ADDBA Status Code
#define ADDBA_RESULTCODE_SUCCESS					0
#define ADDBA_RESULTCODE_REFUSED					37
#define ADDBA_RESULTCODE_INVALID_PARAMETERS			38

// DELBA Reason Code
#define DELBA_REASONCODE_QSTA_LEAVING				36
#define DELBA_REASONCODE_END_BA						37
#define DELBA_REASONCODE_UNKNOWN_BA					38
#define DELBA_REASONCODE_TIMEOUT					39

// reset all OneSecTx counters
#define RESET_ONE_SEC_TX_CNT(__pEntry) \
if (((__pEntry)) != NULL) \
{ \
	(__pEntry)->OneSecTxRetryOkCount = 0; \
	(__pEntry)->OneSecTxFailCount = 0; \
	(__pEntry)->OneSecTxNoRetryOkCount = 0; \
}

//
// 802.11 frame formats
//
//  HT Capability INFO field in HT Cap IE .
typedef struct PACKED {
	USHORT	AdvCoding:1;
	USHORT	ChannelWidth:1;
	USHORT	MimoPs:2;//momi power safe
	USHORT	GF:1;	//green field
	USHORT	ShortGIfor20:1;
	USHORT	ShortGIfor40:1;	//for40MHz
	USHORT	TxSTBC:1;
	USHORT	RxSTBC:2;
	USHORT	DelayedBA:1;	//rt2860c not support
	USHORT	AMsduSize:1;	// only support as zero
	USHORT	CCKmodein40:1;
	USHORT	PSMP:1;
	USHORT	Forty_Mhz_Intolerant:1;
	USHORT	LSIGTxopProSup:1;
} HT_CAP_INFO, *PHT_CAP_INFO;

//  HT Capability INFO field in HT Cap IE .
typedef struct PACKED {
	UCHAR	MaxRAmpduFactor:2;
	UCHAR	MpduDensity:3;
	UCHAR	rsv:3;//momi power safe
} HT_CAP_PARM, *PHT_CAP_PARM;

//  HT Capability INFO field in HT Cap IE .
typedef struct PACKED {
	UCHAR	MCSSet[10];
	UCHAR	SupRate[2];  // unit : 1Mbps
	UCHAR	TxMCSSetDefined:1;
	UCHAR	TxRxNotEqual:1;
	UCHAR	TxStream:2;
	UCHAR	MpduDensity:1;
	UCHAR	rsv:3;
	UCHAR	rsv3[3];
} HT_MCS_SET, *PHT_MCS_SET;

//  HT Capability INFO field in HT Cap IE .
typedef struct PACKED {
	USHORT	Pco:1;
	USHORT	TranTime:2;
	USHORT	rsv:5;//momi power safe
	USHORT	MCSFeedback:2;	//0:no MCS feedback, 2:unsolicited MCS feedback, 3:Full MCS feedback,  1:rsv.
	USHORT	PlusHTC:1;	//+HTC control field support
	USHORT	RDGSupport:1;	//reverse Direction Grant  support
	USHORT	rsv2:4;
} EXT_HT_CAP_INFO, *PEXT_HT_CAP_INFO;

//  HT Beamforming field in HT Cap IE .
typedef struct PACKED _HT_BF_CAP{
	ULONG	TxBFRecCapable:1;
	ULONG	RxSoundCapable:1;
	ULONG	TxSoundCapable:1;
	ULONG	RxNDPCapable:1;
	ULONG	TxNDPCapable:1;
	ULONG	ImpTxBFCapable:1;
	ULONG	Calibration:2;
	ULONG	ExpCSICapable:1;
	ULONG	ExpNoComSteerCapable:1;
	ULONG	ExpComSteerCapable:1;
	ULONG	ExpCSIFbk:2;
	ULONG	ExpNoComBF:2;
	ULONG	ExpComBF:2;
	ULONG	MinGrouping:2;
	ULONG	CSIBFAntSup:2;
	ULONG	NoComSteerBFAntSup:2;
	ULONG	ComSteerBFAntSup:2;
	ULONG	CSIRowBFSup:2;
	ULONG	ChanEstimation:2;
	ULONG	rsv:3;
} HT_BF_CAP, *PHT_BF_CAP;

//  HT antenna selection field in HT Cap IE .
typedef struct PACKED _HT_AS_CAP{
	UCHAR	AntSelect:1;
	UCHAR	ExpCSIFbkTxASEL:1;
	UCHAR	AntIndFbkTxASEL:1;
	UCHAR	ExpCSIFbk:1;
	UCHAR	AntIndFbk:1;
	UCHAR	RxASel:1;
	UCHAR	TxSoundPPDU:1;
	UCHAR	rsv:1;
} HT_AS_CAP, *PHT_AS_CAP;

// Draft 1.0 set IE length 26, but is extensible..
#define SIZE_HT_CAP_IE		26
// The structure for HT Capability IE.
typedef struct PACKED _HT_CAPABILITY_IE{
	HT_CAP_INFO		HtCapInfo;
	HT_CAP_PARM		HtCapParm;
//	HT_MCS_SET		HtMCSSet;
	UCHAR			MCSSet[16];
	EXT_HT_CAP_INFO	ExtHtCapInfo;
	HT_BF_CAP		TxBFCap;	// beamforming cap. rt2860c not support beamforming.
	HT_AS_CAP		ASCap;	//antenna selection.
} HT_CAPABILITY_IE, *PHT_CAPABILITY_IE;


// 802.11n draft3 related structure definitions.
// 7.3.2.60
#define dot11OBSSScanPassiveDwell							20	// in TU. min amount of time that the STA continously scans each channel when performing an active OBSS scan.
#define dot11OBSSScanActiveDwell							10	// in TU.min amount of time that the STA continously scans each channel when performing an passive OBSS scan.
#define dot11BSSWidthTriggerScanInterval					300  // in sec. max interval between scan operations to be performed to detect BSS channel width trigger events.
#define dot11OBSSScanPassiveTotalPerChannel					200	// in TU. min total amount of time that the STA scans each channel when performing a passive OBSS scan.
#define dot11OBSSScanActiveTotalPerChannel					20	//in TU. min total amount of time that the STA scans each channel when performing a active OBSS scan
#define dot11BSSWidthChannelTransactionDelayFactor			5	// min ratio between the delay time in performing a switch from 20MHz BSS to 20/40 BSS operation and the maxima
																//	interval between overlapping BSS scan operations.
#define dot11BSSScanActivityThreshold						25	// in %%, max total time that a STA may be active on the medium during a period of
																//	(dot11BSSWidthChannelTransactionDelayFactor * dot11BSSWidthTriggerScanInterval) seconds without
																//	being obligated to perform OBSS Scan operations. default is 25(== 0.25%)

typedef struct PACKED _OVERLAP_BSS_SCAN_IE{
	USHORT		ScanPassiveDwell;
	USHORT		ScanActiveDwell;
	USHORT		TriggerScanInt;				// Trigger scan interval
	USHORT		PassiveTalPerChannel;		// passive total per channel
	USHORT		ActiveTalPerChannel;		// active total per channel
	USHORT		DelayFactor;				// BSS width channel transition delay factor
	USHORT		ScanActThre;				// Scan Activity threshold
}OVERLAP_BSS_SCAN_IE, *POVERLAP_BSS_SCAN_IE;


//  7.3.2.56. 20/40 Coexistence element used in  Element ID = 72 = IE_2040_BSS_COEXIST
typedef union PACKED _BSS_2040_COEXIST_IE{
 struct PACKED {
	UCHAR	InfoReq:1;
	UCHAR	Intolerant40:1;			// Inter-BSS. set 1 when prohibits a receiving BSS from operating as a 20/40 Mhz BSS.
	UCHAR	BSS20WidthReq:1;		// Intra-BSS set 1 when prohibits a receiving AP from operating its BSS as a 20/40MHz BSS.
	UCHAR	rsv:5;
    } field;
 UCHAR   word;
} BSS_2040_COEXIST_IE, *PBSS_2040_COEXIST_IE;


typedef struct  _TRIGGER_EVENTA{
	BOOLEAN			bValid;
	UCHAR	BSSID[6];
	UCHAR	RegClass;	// Regulatory Class
	USHORT	Channel;
	ULONG	CDCounter;   // Maintain a seperate count down counter for each Event A.
} TRIGGER_EVENTA, *PTRIGGER_EVENTA;

// 20/40 trigger event table
// If one Event A delete or created, or if Event B is detected or not detected, STA should send 2040BSSCoexistence to AP.
#define MAX_TRIGGER_EVENT		64
typedef struct  _TRIGGER_EVENT_TAB{
	UCHAR	EventANo;
	TRIGGER_EVENTA	EventA[MAX_TRIGGER_EVENT];
	ULONG			EventBCountDown;	// Count down counter for Event B.
} TRIGGER_EVENT_TAB, *PTRIGGER_EVENT_TAB;

// 7.3.27 20/40 Bss Coexistence Mgmt capability used in extended capabilities information IE( ID = 127 = IE_EXT_CAPABILITY).
//	This is the first octet and was defined in 802.11n D3.03 and 802.11yD9.0
typedef struct PACKED _EXT_CAP_INFO_ELEMENT{
	UCHAR	BssCoexistMgmtSupport:1;
	UCHAR	rsv:1;
	UCHAR	ExtendChannelSwitch:1;
	UCHAR	rsv2:5;
}EXT_CAP_INFO_ELEMENT, *PEXT_CAP_INFO_ELEMENT;


// 802.11n 7.3.2.61
typedef struct PACKED _BSS_2040_COEXIST_ELEMENT{
	UCHAR					ElementID;		// ID = IE_2040_BSS_COEXIST = 72
	UCHAR					Len;
	BSS_2040_COEXIST_IE		BssCoexistIe;
}BSS_2040_COEXIST_ELEMENT, *PBSS_2040_COEXIST_ELEMENT;


//802.11n 7.3.2.59
typedef struct PACKED _BSS_2040_INTOLERANT_CH_REPORT{
	UCHAR				ElementID;		// ID = IE_2040_BSS_INTOLERANT_REPORT = 73
	UCHAR				Len;
	UCHAR				RegulatoryClass;
	UCHAR				ChList[0];
}BSS_2040_INTOLERANT_CH_REPORT, *PBSS_2040_INTOLERANT_CH_REPORT;


// The structure for channel switch annoucement IE. This is in 802.11n D3.03
typedef struct PACKED _CHA_SWITCH_ANNOUNCE_IE{
	UCHAR			SwitchMode;	//channel switch mode
	UCHAR			NewChannel;	//
	UCHAR			SwitchCount;	//
} CHA_SWITCH_ANNOUNCE_IE, *PCHA_SWITCH_ANNOUNCE_IE;


// The structure for channel switch annoucement IE. This is in 802.11n D3.03
typedef struct PACKED _SEC_CHA_OFFSET_IE{
	UCHAR			SecondaryChannelOffset;	 // 1: Secondary above, 3: Secondary below, 0: no Secondary
} SEC_CHA_OFFSET_IE, *PSEC_CHA_OFFSET_IE;


// This structure is extracted from struct RT_HT_CAPABILITY
typedef struct {
	BOOLEAN			bHtEnable;	 // If we should use ht rate.
	BOOLEAN			bPreNHt;	 // If we should use ht rate.
	//Substract from HT Capability IE
	UCHAR			MCSSet[16];	//only supoort MCS=0-15,32 ,
} RT_HT_PHY_INFO, *PRT_HT_PHY_INFO;

//This structure substracts ralink supports from all 802.11n-related features.
//Features not listed here but contained in 802.11n spec are not supported in rt2860.
typedef struct {
#if 0	// move to
	BOOLEAN			bHtEnable;	 // If we should use ht rate.
	BOOLEAN			bPreNHt;	 // If we should use ht rate.
	//Substract from HT Capability IE
	UCHAR			MCSSet[16];	//only supoort MCS=0-15,32 ,
#endif
	USHORT	ChannelWidth:1;
	USHORT	MimoPs:2;//mimo power safe MMPS_
	USHORT	GF:1;	//green field
	USHORT	ShortGIfor20:1;
	USHORT	ShortGIfor40:1;	//for40MHz
	USHORT	TxSTBC:1;
	USHORT	RxSTBC:2;	// 2 bits
	USHORT	AmsduEnable:1;	// Enable to transmit A-MSDU. Suggest disable. We should use A-MPDU to gain best benifit of 802.11n
	USHORT	AmsduSize:1;	// Max receiving A-MSDU size
	USHORT	rsv:5;

	//Substract from Addiont HT INFO IE
	UCHAR	MaxRAmpduFactor:2;
	UCHAR	MpduDensity:3;
	UCHAR	ExtChanOffset:2;	// Please not the difference with following 	UCHAR	NewExtChannelOffset; from 802.11n
	UCHAR	RecomWidth:1;

	USHORT	OperaionMode:2;
	USHORT	NonGfPresent:1;
	USHORT	rsv3:1;
	USHORT	OBSS_NonHTExist:1;
	USHORT	rsv2:11;

	// New Extension Channel Offset IE
	UCHAR	NewExtChannelOffset;
	// Extension Capability IE = 127
	UCHAR	BSSCoexist2040;
} RT_HT_CAPABILITY, *PRT_HT_CAPABILITY;

//   field in Addtional HT Information IE .
typedef struct PACKED {
	UCHAR	ExtChanOffset:2;
	UCHAR	RecomWidth:1;
	UCHAR	RifsMode:1;
	UCHAR	S_PSMPSup:1;	 //Indicate support for scheduled PSMP
	UCHAR	SerInterGranu:3;	 //service interval granularity
} ADD_HTINFO, *PADD_HTINFO;

typedef struct PACKED{
	USHORT	OperaionMode:2;
	USHORT	NonGfPresent:1;
	USHORT	rsv:1;
	USHORT	OBSS_NonHTExist:1;
	USHORT	rsv2:11;
} ADD_HTINFO2, *PADD_HTINFO2;


// TODO: Need sync with spec about the definition of StbcMcs. In Draft 3.03, it's reserved.
typedef struct PACKED{
	USHORT	StbcMcs:6;
	USHORT	DualBeacon:1;
	USHORT	DualCTSProtect:1;
	USHORT	STBCBeacon:1;
	USHORT	LsigTxopProt:1;	// L-SIG TXOP protection full support
	USHORT	PcoActive:1;
	USHORT	PcoPhase:1;
	USHORT	rsv:4;
} ADD_HTINFO3, *PADD_HTINFO3;

#define SIZE_ADD_HT_INFO_IE		22
typedef struct  PACKED{
	UCHAR				ControlChan;
	ADD_HTINFO			AddHtInfo;
	ADD_HTINFO2			AddHtInfo2;
	ADD_HTINFO3			AddHtInfo3;
	UCHAR				MCSSet[16];		// Basic MCS set
} ADD_HT_INFO_IE, *PADD_HT_INFO_IE;

typedef struct  PACKED{
	UCHAR				NewExtChanOffset;
} NEW_EXT_CHAN_IE, *PNEW_EXT_CHAN_IE;


// 4-byte HTC field.  maybe included in any frame except non-QOS data frame.  The Order bit must set 1.
typedef struct PACKED {
    UINT32		MA:1;	//management action payload exist in (QoS Null+HTC)
    UINT32		TRQ:1;	//sounding request
    UINT32		MRQ:1;	//MCS feedback. Request for a MCS feedback
    UINT32		MRSorASI:3;	// MRQ Sequence identifier. unchanged during entire procedure. 0x000-0x110.
    UINT32		MFS:3;	//SET to the received value of MRS. 0x111 for unsolicited MFB.
    UINT32		MFBorASC:7;	//Link adaptation feedback containing recommended MCS. 0x7f for no feedback or not available
    UINT32		CalPos:2;	// calibration position
    UINT32		CalSeq:2;  //calibration sequence
    UINT32		FBKReq:2;	//feedback request
    UINT32		CSISTEERING:2;	//CSI/ STEERING
    UINT32		ZLFAnnouce:1;	// ZLF announcement
    UINT32		rsv:5;  //calibration sequence
    UINT32		ACConstraint:1;	//feedback request
    UINT32		RDG:1;	//RDG / More PPDU
} HT_CONTROL, *PHT_CONTROL;

// 2-byte QOS CONTROL field
typedef struct PACKED {
    USHORT      TID:4;
    USHORT      EOSP:1;
    USHORT      AckPolicy:2;  //0: normal ACK 1:No ACK 2:scheduled under MTBA/PSMP  3: BA
    USHORT      AMsduPresent:1;
    USHORT      Txop_QueueSize:8;
} QOS_CONTROL, *PQOS_CONTROL;

// 2-byte Frame control field
typedef	struct	PACKED {
	USHORT		Ver:2;				// Protocol version
	USHORT		Type:2;				// MSDU type
	USHORT		SubType:4;			// MSDU subtype
	USHORT		ToDs:1;				// To DS indication
	USHORT		FrDs:1;				// From DS indication
	USHORT		MoreFrag:1;			// More fragment bit
	USHORT		Retry:1;			// Retry status bit
	USHORT		PwrMgmt:1;			// Power management bit
	USHORT		MoreData:1;			// More data bit
	USHORT		Wep:1;				// Wep data
	USHORT		Order:1;			// Strict order expected
} FRAME_CONTROL, *PFRAME_CONTROL;

typedef	struct	PACKED _HEADER_802_11	{
    FRAME_CONTROL   FC;
    USHORT          Duration;
    UCHAR           Addr1[MAC_ADDR_LEN];
    UCHAR           Addr2[MAC_ADDR_LEN];
	UCHAR			Addr3[MAC_ADDR_LEN];
	USHORT			Frag:4;
	USHORT			Sequence:12;
	UCHAR			Octet[0];
}	HEADER_802_11, *PHEADER_802_11;

typedef struct PACKED _FRAME_802_11 {
    HEADER_802_11   Hdr;
    UCHAR            Octet[1];
}   FRAME_802_11, *PFRAME_802_11;

// QoSNull embedding of management action. When HT Control MA field set to 1.
typedef struct PACKED _MA_BODY {
    UCHAR            Category;
    UCHAR            Action;
    UCHAR            Octet[1];
}   MA_BODY, *PMA_BODY;

typedef	struct	PACKED _HEADER_802_3	{
    UCHAR           DAAddr1[MAC_ADDR_LEN];
    UCHAR           SAAddr2[MAC_ADDR_LEN];
    UCHAR           Octet[2];
}	HEADER_802_3, *PHEADER_802_3;
////Block ACK related format
// 2-byte BA Parameter  field  in 	DELBA frames to terminate an already set up bA
typedef struct PACKED{
    USHORT      Rsv:11;	// always set to 0
    USHORT      Initiator:1;	// 1: originator    0:recipient
    USHORT      TID:4;	// value of TC os TS
} DELBA_PARM, *PDELBA_PARM;

// 2-byte BA Parameter Set field  in ADDBA frames to signal parm for setting up a BA
typedef struct PACKED {
    USHORT      AMSDUSupported:1;	// 0: not permitted		1: permitted
    USHORT      BAPolicy:1;	// 1: immediately BA    0:delayed BA
    USHORT      TID:4;	// value of TC os TS
    USHORT      BufSize:10;	// number of buffe of size 2304 octetsr
} BA_PARM, *PBA_PARM;

// 2-byte BA Starting Seq CONTROL field
typedef union PACKED {
    struct PACKED {
    USHORT      FragNum:4;	// always set to 0
	USHORT      StartSeq:12;   // sequence number of the 1st MSDU for which this BAR is sent
    }   field;
    USHORT           word;
} BASEQ_CONTROL, *PBASEQ_CONTROL;

//BAControl and BARControl are the same
// 2-byte BA CONTROL field in BA frame
typedef struct PACKED {
    USHORT      ACKPolicy:1; // only related to N-Delayed BA. But not support in RT2860b. 0:NormalACK  1:No ACK
    USHORT      MTID:1;		//EWC V1.24
    USHORT      Compressed:1;
    USHORT      Rsv:9;
    USHORT      TID:4;
} BA_CONTROL, *PBA_CONTROL;

// 2-byte BAR CONTROL field in BAR frame
typedef struct PACKED {
    USHORT      ACKPolicy:1; // 0:normal ack,  1:no ack.
    USHORT      MTID:1;		//if this bit1, use  FRAME_MTBA_REQ,  if 0, use FRAME_BA_REQ
    USHORT      Compressed:1;
    USHORT      Rsv1:9;
    USHORT      TID:4;
} BAR_CONTROL, *PBAR_CONTROL;

// BARControl in MTBAR frame
typedef struct PACKED {
    USHORT      ACKPolicy:1;
    USHORT      MTID:1;
    USHORT      Compressed:1;
    USHORT      Rsv1:9;
    USHORT      NumTID:4;
} MTBAR_CONTROL, *PMTBAR_CONTROL;

typedef struct PACKED {
    USHORT      Rsv1:12;
    USHORT      TID:4;
} PER_TID_INFO, *PPER_TID_INFO;

typedef struct {
	PER_TID_INFO      PerTID;
	BASEQ_CONTROL 	 BAStartingSeq;
} EACH_TID, *PEACH_TID;


typedef struct PACKED _PSPOLL_FRAME {
    FRAME_CONTROL   FC;
    USHORT          Aid;
    UCHAR           Bssid[MAC_ADDR_LEN];
    UCHAR           Ta[MAC_ADDR_LEN];
}   PSPOLL_FRAME, *PPSPOLL_FRAME;

typedef	struct	PACKED _RTS_FRAME	{
    FRAME_CONTROL   FC;
    USHORT          Duration;
    UCHAR           Addr1[MAC_ADDR_LEN];
    UCHAR           Addr2[MAC_ADDR_LEN];
}RTS_FRAME, *PRTS_FRAME;

// BAREQ AND MTBAREQ have the same subtype BAR, 802.11n BAR use compressed bitmap.
typedef struct PACKED _FRAME_BA_REQ {
	FRAME_CONTROL   FC;
	USHORT          Duration;
	UCHAR           Addr1[MAC_ADDR_LEN];
	UCHAR           Addr2[MAC_ADDR_LEN];
	BAR_CONTROL  BARControl;
	BASEQ_CONTROL 	 BAStartingSeq;
}   FRAME_BA_REQ, *PFRAME_BA_REQ;

typedef struct PACKED _FRAME_MTBA_REQ {
	FRAME_CONTROL   FC;
	USHORT          Duration;
	UCHAR           Addr1[MAC_ADDR_LEN];
	UCHAR           Addr2[MAC_ADDR_LEN];
	MTBAR_CONTROL  MTBARControl;
	PER_TID_INFO	PerTIDInfo;
	BASEQ_CONTROL 	 BAStartingSeq;
}   FRAME_MTBA_REQ, *PFRAME_MTBA_REQ;

// Compressed format is mandantory in HT STA
typedef struct PACKED _FRAME_MTBA {
	FRAME_CONTROL   FC;
	USHORT          Duration;
	UCHAR           Addr1[MAC_ADDR_LEN];
	UCHAR           Addr2[MAC_ADDR_LEN];
	BA_CONTROL  BAControl;
	BASEQ_CONTROL 	 BAStartingSeq;
	UCHAR		BitMap[8];
}   FRAME_MTBA, *PFRAME_MTBA;

typedef struct PACKED _FRAME_PSMP_ACTION {
	HEADER_802_11   Hdr;
	UCHAR	Category;
	UCHAR	Action;
	UCHAR	Psmp;	// 7.3.1.25
}   FRAME_PSMP_ACTION, *PFRAME_PSMP_ACTION;

typedef struct PACKED _FRAME_ACTION_HDR {
	HEADER_802_11   Hdr;
	UCHAR	Category;
	UCHAR	Action;
}   FRAME_ACTION_HDR, *PFRAME_ACTION_HDR;

//Action Frame
//Action Frame  Category:Spectrum,  Action:Channel Switch. 7.3.2.20
typedef struct PACKED _CHAN_SWITCH_ANNOUNCE {
	UCHAR					ElementID;	// ID = IE_CHANNEL_SWITCH_ANNOUNCEMENT = 37
	UCHAR					Len;
	CHA_SWITCH_ANNOUNCE_IE	CSAnnounceIe;
}   CHAN_SWITCH_ANNOUNCE, *PCHAN_SWITCH_ANNOUNCE;


//802.11n : 7.3.2.20a
typedef struct PACKED _SECOND_CHAN_OFFSET {
	UCHAR				ElementID;		// ID = IE_SECONDARY_CH_OFFSET = 62
	UCHAR				Len;
	SEC_CHA_OFFSET_IE	SecChOffsetIe;
}   SECOND_CHAN_OFFSET, *PSECOND_CHAN_OFFSET;


typedef struct PACKED _FRAME_SPETRUM_CS {
	HEADER_802_11   Hdr;
	UCHAR	Category;
	UCHAR	Action;
	CHAN_SWITCH_ANNOUNCE	CSAnnounce;
	SECOND_CHAN_OFFSET		SecondChannel;
}   FRAME_SPETRUM_CS, *PFRAME_SPETRUM_CS;


typedef struct PACKED _FRAME_ADDBA_REQ {
	HEADER_802_11   Hdr;
	UCHAR	Category;
	UCHAR	Action;
	UCHAR	Token;	// 1
	BA_PARM		BaParm;	      //  2 - 10
	USHORT		TimeOutValue;	// 0 - 0
	BASEQ_CONTROL	BaStartSeq; // 0-0
}   FRAME_ADDBA_REQ, *PFRAME_ADDBA_REQ;

typedef struct PACKED _FRAME_ADDBA_RSP {
	HEADER_802_11   Hdr;
	UCHAR	Category;
	UCHAR	Action;
	UCHAR	Token;
	USHORT	StatusCode;
	BA_PARM		BaParm; //0 - 2
	USHORT		TimeOutValue;
}   FRAME_ADDBA_RSP, *PFRAME_ADDBA_RSP;

typedef struct PACKED _FRAME_DELBA_REQ {
	HEADER_802_11   Hdr;
	UCHAR	Category;
	UCHAR	Action;
	DELBA_PARM		DelbaParm;
	USHORT	ReasonCode;
}   FRAME_DELBA_REQ, *PFRAME_DELBA_REQ;


//7.2.1.7
typedef struct PACKED _FRAME_BAR {
	FRAME_CONTROL   FC;
	USHORT          Duration;
	UCHAR           Addr1[MAC_ADDR_LEN];
	UCHAR           Addr2[MAC_ADDR_LEN];
	BAR_CONTROL		BarControl;
	BASEQ_CONTROL	StartingSeq;
}   FRAME_BAR, *PFRAME_BAR;

//7.2.1.7
typedef struct PACKED _FRAME_BA {
	FRAME_CONTROL   FC;
	USHORT          Duration;
	UCHAR           Addr1[MAC_ADDR_LEN];
	UCHAR           Addr2[MAC_ADDR_LEN];
	BAR_CONTROL		BarControl;
	BASEQ_CONTROL	StartingSeq;
	UCHAR		bitmask[8];
}   FRAME_BA, *PFRAME_BA;


// Radio Measuement Request Frame Format
typedef struct PACKED _FRAME_RM_REQ_ACTION {
	HEADER_802_11   Hdr;
	UCHAR	Category;
	UCHAR	Action;
	UCHAR	Token;
	USHORT	Repetition;
	UCHAR   data[0];
}   FRAME_RM_REQ_ACTION, *PFRAME_RM_REQ_ACTION;

typedef struct PACKED {
	UCHAR		ID;
	UCHAR		Length;
	UCHAR		ChannelSwitchMode;
	UCHAR		NewRegClass;
	UCHAR		NewChannelNum;
	UCHAR		ChannelSwitchCount;
} HT_EXT_CHANNEL_SWITCH_ANNOUNCEMENT_IE, *PHT_EXT_CHANNEL_SWITCH_ANNOUNCEMENT_IE;


//
// _Limit must be the 2**n - 1
// _SEQ1 , _SEQ2 must be within 0 ~ _Limit
//
#define SEQ_STEPONE(_SEQ1, _SEQ2, _Limit)	((_SEQ1 == ((_SEQ2+1) & _Limit)))
#define SEQ_SMALLER(_SEQ1, _SEQ2, _Limit)	(((_SEQ1-_SEQ2) & ((_Limit+1)>>1)))
#define SEQ_LARGER(_SEQ1, _SEQ2, _Limit)	((_SEQ1 != _SEQ2) && !(((_SEQ1-_SEQ2) & ((_Limit+1)>>1))))
#define SEQ_WITHIN_WIN(_SEQ1, _SEQ2, _WIN, _Limit) (SEQ_LARGER(_SEQ1, _SEQ2, _Limit) &&  \
												SEQ_SMALLER(_SEQ1, ((_SEQ2+_WIN+1)&_Limit), _Limit))

//
// Contention-free parameter (without ID and Length)
//
typedef struct PACKED {
    BOOLEAN     bValid;         // 1: variable contains valid value
    UCHAR       CfpCount;
    UCHAR       CfpPeriod;
    USHORT      CfpMaxDuration;
    USHORT      CfpDurRemaining;
} CF_PARM, *PCF_PARM;

typedef	struct	_CIPHER_SUITE	{
	NDIS_802_11_ENCRYPTION_STATUS	PairCipher;		// Unicast cipher 1, this one has more secured cipher suite
	NDIS_802_11_ENCRYPTION_STATUS	PairCipherAux;	// Unicast cipher 2 if AP announce two unicast cipher suite
	NDIS_802_11_ENCRYPTION_STATUS	GroupCipher;	// Group cipher
	USHORT							RsnCapability;	// RSN capability from beacon
	BOOLEAN							bMixMode;		// Indicate Pair & Group cipher might be different
}	CIPHER_SUITE, *PCIPHER_SUITE;

// EDCA configuration from AP's BEACON/ProbeRsp
typedef struct {
    BOOLEAN     bValid;         // 1: variable contains valid value
    BOOLEAN     bAdd;         // 1: variable contains valid value
    BOOLEAN     bQAck;
    BOOLEAN     bQueueRequest;
    BOOLEAN     bTxopRequest;
    BOOLEAN     bAPSDCapable;
//  BOOLEAN     bMoreDataAck;
    UCHAR       EdcaUpdateCount;
    UCHAR       Aifsn[4];       // 0:AC_BK, 1:AC_BE, 2:AC_VI, 3:AC_VO
    UCHAR       Cwmin[4];
    UCHAR       Cwmax[4];
    USHORT      Txop[4];      // in unit of 32-us
    BOOLEAN     bACM[4];      // 1: Admission Control of AC_BK is mandattory
} EDCA_PARM, *PEDCA_PARM;

// QBSS LOAD information from QAP's BEACON/ProbeRsp
typedef struct {
    BOOLEAN     bValid;                     // 1: variable contains valid value
    USHORT      StaNum;
    UCHAR       ChannelUtilization;
    USHORT      RemainingAdmissionControl;  // in unit of 32-us
} QBSS_LOAD_PARM, *PQBSS_LOAD_PARM;

// QBSS Info field in QSTA's assoc req
typedef struct PACKED {
    UCHAR		UAPSD_AC_VO:1;
	UCHAR		UAPSD_AC_VI:1;
	UCHAR		UAPSD_AC_BK:1;
	UCHAR		UAPSD_AC_BE:1;
	UCHAR		Rsv1:1;
	UCHAR		MaxSPLength:2;
	UCHAR		Rsv2:1;
} QBSS_STA_INFO_PARM, *PQBSS_STA_INFO_PARM;

// QBSS Info field in QAP's Beacon/ProbeRsp
typedef struct PACKED {
    UCHAR		ParamSetCount:4;
	UCHAR		Rsv:3;
	UCHAR		UAPSD:1;
} QBSS_AP_INFO_PARM, *PQBSS_AP_INFO_PARM;

// QOS Capability reported in QAP's BEACON/ProbeRsp
// QOS Capability sent out in QSTA's AssociateReq/ReAssociateReq
typedef struct {
    BOOLEAN     bValid;                     // 1: variable contains valid value
    BOOLEAN     bQAck;
    BOOLEAN     bQueueRequest;
    BOOLEAN     bTxopRequest;
//  BOOLEAN     bMoreDataAck;
    UCHAR       EdcaUpdateCount;
} QOS_CAPABILITY_PARM, *PQOS_CAPABILITY_PARM;

typedef struct {
    UCHAR       IELen;
    UCHAR       IE[MAX_CUSTOM_LEN];
} WPA_IE_;

typedef struct {
    UCHAR   Bssid[MAC_ADDR_LEN];
    UCHAR   Channel;
	UCHAR   CentralChannel;	//Store the wide-band central channel for 40MHz.  .used in 40MHz AP. Or this is the same as Channel.
    UCHAR   BssType;
    USHORT  AtimWin;
    USHORT  BeaconPeriod;

    UCHAR   SupRate[MAX_LEN_OF_SUPPORTED_RATES];
    UCHAR   SupRateLen;
    UCHAR   ExtRate[MAX_LEN_OF_SUPPORTED_RATES];
    UCHAR   ExtRateLen;
	HT_CAPABILITY_IE HtCapability;
	UCHAR			HtCapabilityLen;
	ADD_HT_INFO_IE AddHtInfo;	// AP might use this additional ht info IE
	UCHAR			AddHtInfoLen;
	UCHAR			NewExtChanOffset;
	CHAR    Rssi;
    UCHAR   Privacy;			// Indicate security function ON/OFF. Don't mess up with auth mode.
	UCHAR	Hidden;

    USHORT  DtimPeriod;
    USHORT  CapabilityInfo;

    USHORT  CfpCount;
    USHORT  CfpPeriod;
    USHORT  CfpMaxDuration;
    USHORT  CfpDurRemaining;
    UCHAR   SsidLen;
    CHAR    Ssid[MAX_LEN_OF_SSID];

    ULONG   LastBeaconRxTime; // OS's timestamp

	BOOLEAN	bSES;

	// New for WPA2
	CIPHER_SUITE					WPA;			// AP announced WPA cipher suite
	CIPHER_SUITE					WPA2;			// AP announced WPA2 cipher suite

	// New for microsoft WPA support
	NDIS_802_11_FIXED_IEs	FixIEs;
	NDIS_802_11_AUTHENTICATION_MODE	AuthModeAux;	// Addition mode for WPA2 / WPA capable AP
	NDIS_802_11_AUTHENTICATION_MODE	AuthMode;
	NDIS_802_11_WEP_STATUS	WepStatus;				// Unicast Encryption Algorithm extract from VAR_IE
	USHORT					VarIELen;				// Length of next VIE include EID & Length
	UCHAR					VarIEs[MAX_VIE_LEN];

	// CCX Ckip information
    UCHAR   CkipFlag;

	// CCX 2 TSF
	UCHAR	PTSF[4];		// Parent TSF
	UCHAR	TTSF[8];		// Target TSF

    // 802.11e d9, and WMM
	EDCA_PARM           EdcaParm;
	QOS_CAPABILITY_PARM QosCapability;
	QBSS_LOAD_PARM      QbssLoad;
    WPA_IE_     WpaIE;
    WPA_IE_     RsnIE;
} BSS_ENTRY, *PBSS_ENTRY;

typedef struct {
    UCHAR           BssNr;
    UCHAR           BssOverlapNr;
    BSS_ENTRY       BssEntry[MAX_LEN_OF_BSS_TABLE];
} BSS_TABLE, *PBSS_TABLE;


typedef struct _MLME_QUEUE_ELEM {
    ULONG             Machine;
    ULONG             MsgType;
    ULONG             MsgLen;
    UCHAR             Msg[MGMT_DMA_BUFFER_SIZE];
    LARGE_INTEGER     TimeStamp;
    UCHAR             Rssi0;
    UCHAR             Rssi1;
    UCHAR             Rssi2;
    UCHAR             Signal;
    UCHAR             Channel;
    UCHAR             Wcid;
    BOOLEAN           Occupied;
} MLME_QUEUE_ELEM, *PMLME_QUEUE_ELEM;

typedef struct _MLME_QUEUE {
    ULONG             Num;
    ULONG             Head;
    ULONG             Tail;
    NDIS_SPIN_LOCK   Lock;
    MLME_QUEUE_ELEM  Entry[MAX_LEN_OF_MLME_QUEUE];
} MLME_QUEUE, *PMLME_QUEUE;

typedef VOID (*STATE_MACHINE_FUNC)(VOID *Adaptor, MLME_QUEUE_ELEM *Elem);

typedef struct _STATE_MACHINE {
    ULONG                           Base;
    ULONG                           NrState;
    ULONG                           NrMsg;
    ULONG                           CurrState;
    STATE_MACHINE_FUNC             *TransFunc;
} STATE_MACHINE, *PSTATE_MACHINE;


// MLME AUX data structure that hold temporarliy settings during a connection attempt.
// Once this attemp succeeds, all settings will be copy to pAd->StaActive.
// A connection attempt (user set OID, roaming, CCX fast roaming,..) consists of
// several steps (JOIN, AUTH, ASSOC or REASSOC) and may fail at any step. We purposely
// separate this under-trial settings away from pAd->StaActive so that once
// this new attempt failed, driver can auto-recover back to the active settings.
typedef struct _MLME_AUX {
    UCHAR               BssType;
    UCHAR               Ssid[MAX_LEN_OF_SSID];
    UCHAR               SsidLen;
    UCHAR               Bssid[MAC_ADDR_LEN];
	UCHAR				AutoReconnectSsid[MAX_LEN_OF_SSID];
	UCHAR				AutoReconnectSsidLen;
    USHORT              Alg;
    UCHAR               ScanType;
    UCHAR               Channel;
	UCHAR               CentralChannel;
    USHORT              Aid;
    USHORT              CapabilityInfo;
    USHORT              BeaconPeriod;
    USHORT              CfpMaxDuration;
    USHORT              CfpPeriod;
    USHORT              AtimWin;

	// Copy supported rate from desired AP's beacon. We are trying to match
	// AP's supported and extended rate settings.
	UCHAR		        SupRate[MAX_LEN_OF_SUPPORTED_RATES];
	UCHAR		        ExtRate[MAX_LEN_OF_SUPPORTED_RATES];
	UCHAR		        SupRateLen;
	UCHAR		        ExtRateLen;
	HT_CAPABILITY_IE		HtCapability;
	UCHAR		        	HtCapabilityLen;
	ADD_HT_INFO_IE		AddHtInfo;	// AP might use this additional ht info IE
	UCHAR			NewExtChannelOffset;
	//RT_HT_CAPABILITY	SupportedHtPhy;

    // new for QOS
    QOS_CAPABILITY_PARM APQosCapability;    // QOS capability of the current associated AP
    EDCA_PARM           APEdcaParm;         // EDCA parameters of the current associated AP
    QBSS_LOAD_PARM      APQbssLoad;         // QBSS load of the current associated AP

    // new to keep Ralink specific feature
    ULONG               APRalinkIe;

    BSS_TABLE           SsidBssTab;     // AP list for the same SSID
    BSS_TABLE           RoamTab;        // AP list eligible for roaming
    ULONG               BssIdx;
    ULONG               RoamIdx;

	BOOLEAN				CurrReqIsFromNdis;

    RALINK_TIMER_STRUCT BeaconTimer, ScanTimer;
    RALINK_TIMER_STRUCT AuthTimer;
    RALINK_TIMER_STRUCT AssocTimer, ReassocTimer, DisassocTimer;
} MLME_AUX, *PMLME_AUX;

typedef struct _MLME_ADDBA_REQ_STRUCT{
	UCHAR   Wcid;	//
	UCHAR   pAddr[MAC_ADDR_LEN];
	UCHAR   BaBufSize;
	USHORT	TimeOutValue;
	UCHAR   TID;
	UCHAR   Token;
	USHORT	BaStartSeq;
} MLME_ADDBA_REQ_STRUCT, *PMLME_ADDBA_REQ_STRUCT;


typedef struct _MLME_DELBA_REQ_STRUCT{
	UCHAR   Wcid;	//
	UCHAR     Addr[MAC_ADDR_LEN];
	UCHAR   TID;
	UCHAR	Initiator;
} MLME_DELBA_REQ_STRUCT, *PMLME_DELBA_REQ_STRUCT;

// assoc struct is equal to reassoc
typedef struct _MLME_ASSOC_REQ_STRUCT{
    UCHAR     Addr[MAC_ADDR_LEN];
    USHORT    CapabilityInfo;
    USHORT    ListenIntv;
    ULONG     Timeout;
} MLME_ASSOC_REQ_STRUCT, *PMLME_ASSOC_REQ_STRUCT, MLME_REASSOC_REQ_STRUCT, *PMLME_REASSOC_REQ_STRUCT;

typedef struct _MLME_DISASSOC_REQ_STRUCT{
    UCHAR     Addr[MAC_ADDR_LEN];
    USHORT    Reason;
} MLME_DISASSOC_REQ_STRUCT, *PMLME_DISASSOC_REQ_STRUCT;

typedef struct _MLME_AUTH_REQ_STRUCT {
    UCHAR        Addr[MAC_ADDR_LEN];
    USHORT       Alg;
    ULONG        Timeout;
} MLME_AUTH_REQ_STRUCT, *PMLME_AUTH_REQ_STRUCT;

typedef struct _MLME_DEAUTH_REQ_STRUCT {
    UCHAR        Addr[MAC_ADDR_LEN];
    USHORT       Reason;
} MLME_DEAUTH_REQ_STRUCT, *PMLME_DEAUTH_REQ_STRUCT;

typedef struct {
    ULONG      BssIdx;
} MLME_JOIN_REQ_STRUCT;

typedef struct _MLME_SCAN_REQ_STRUCT {
    UCHAR      Bssid[MAC_ADDR_LEN];
    UCHAR      BssType;
    UCHAR      ScanType;
    UCHAR      SsidLen;
    CHAR       Ssid[MAX_LEN_OF_SSID];
} MLME_SCAN_REQ_STRUCT, *PMLME_SCAN_REQ_STRUCT;

typedef struct _MLME_START_REQ_STRUCT {
    CHAR        Ssid[MAX_LEN_OF_SSID];
    UCHAR       SsidLen;
} MLME_START_REQ_STRUCT, *PMLME_START_REQ_STRUCT;

typedef struct PACKED {
    UCHAR   Eid;
    UCHAR   Len;
    CHAR   Octet[1];
} EID_STRUCT,*PEID_STRUCT, BEACON_EID_STRUCT, *PBEACON_EID_STRUCT;

typedef struct PACKED _RTMP_TX_RATE_SWITCH
{
	UCHAR   ItemNo;
	UCHAR	STBC:1;
	UCHAR	ShortGI:1;
	UCHAR	BW:1;
	UCHAR	Rsv1:1;
	UCHAR	Mode:2;
	UCHAR	Rsv2:2;
	UCHAR   CurrMCS;
	UCHAR   TrainUp;
	UCHAR   TrainDown;
} RRTMP_TX_RATE_SWITCH, *PRTMP_TX_RATE_SWITCH;

// ========================== AP mlme.h ===============================
#define TBTT_PRELOAD_TIME       384        // usec. LomgPreamble + 24-byte at 1Mbps
#define DEFAULT_DTIM_PERIOD     1

// weighting factor to calculate Channel quality, total should be 100%
//#define RSSI_WEIGHTING                   0
//#define TX_WEIGHTING                     40
//#define RX_WEIGHTING                     60

#define MAC_TABLE_AGEOUT_TIME			300			// unit: sec
#define MAC_TABLE_ASSOC_TIMEOUT			5			// unit: sec
#define MAC_TABLE_FULL(Tab)				((Tab).size == MAX_LEN_OF_MAC_TABLE)

// AP shall drop the sta if contine Tx fail count reach it.
#define MAC_ENTRY_LIFE_CHECK_CNT		20			// packet cnt.

// Value domain of pMacEntry->Sst
typedef enum _Sst {
    SST_NOT_AUTH,   // 0: equivalent to IEEE 802.11/1999 state 1
    SST_AUTH,       // 1: equivalent to IEEE 802.11/1999 state 2
    SST_ASSOC       // 2: equivalent to IEEE 802.11/1999 state 3
} SST;

// value domain of pMacEntry->AuthState
typedef enum _AuthState {
    AS_NOT_AUTH,
    AS_AUTH_OPEN,       // STA has been authenticated using OPEN SYSTEM
    AS_AUTH_KEY,        // STA has been authenticated using SHARED KEY
    AS_AUTHENTICATING   // STA is waiting for AUTH seq#3 using SHARED KEY
} AUTH_STATE;

//for-wpa value domain of pMacEntry->WpaState  802.1i D3   p.114
typedef enum _ApWpaState {
    AS_NOTUSE,              // 0
    AS_DISCONNECT,          // 1
    AS_DISCONNECTED,        // 2
    AS_INITIALIZE,          // 3
    AS_AUTHENTICATION,      // 4
    AS_AUTHENTICATION2,     // 5
    AS_INITPMK,             // 6
    AS_INITPSK,             // 7
    AS_PTKSTART,            // 8
    AS_PTKINIT_NEGOTIATING, // 9
    AS_PTKINITDONE,         // 10
    AS_UPDATEKEYS,          // 11
    AS_INTEGRITY_FAILURE,   // 12
    AS_KEYUPDATE,           // 13
} AP_WPA_STATE;

// for-wpa value domain of pMacEntry->WpaState  802.1i D3   p.114
typedef enum _GTKState {
    REKEY_NEGOTIATING,
    REKEY_ESTABLISHED,
    KEYERROR,
} GTK_STATE;

//  for-wpa  value domain of pMacEntry->WpaState  802.1i D3   p.114
typedef enum _WpaGTKState {
    SETKEYS,
    SETKEYS_DONE,
} WPA_GTK_STATE;
// ====================== end of AP mlme.h ============================


#endif	// MLME_H__
