/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * File: 80211mgr.h
 *
 * Purpose: 802.11 management frames pre-defines.
 *
 *
 * Author: Lyndon Chen
 *
 * Date: May 8, 2002
 *
 */

#ifndef __80211MGR_H__
#define __80211MGR_H__

#include "ttype.h"
#include "80211hdr.h"

/*---------------------  Export Definitions -------------------------*/

#define WLAN_MIN_ARRAY          1

// Information Element ID value
#define WLAN_EID_SSID           0
#define WLAN_EID_SUPP_RATES     1
#define WLAN_EID_FH_PARMS       2
#define WLAN_EID_DS_PARMS       3
#define WLAN_EID_CF_PARMS       4
#define WLAN_EID_TIM            5
#define WLAN_EID_IBSS_PARMS     6
#define WLAN_EID_COUNTRY        7
#define WLAN_EID_CHALLENGE      16
#define WLAN_EID_PWR_CONSTRAINT 32
#define WLAN_EID_PWR_CAPABILITY 33
#define WLAN_EID_TPC_REQ        34
#define WLAN_EID_TPC_REP        35
#define WLAN_EID_SUPP_CH        36
#define WLAN_EID_CH_SWITCH      37
#define WLAN_EID_MEASURE_REQ    38
#define WLAN_EID_MEASURE_REP    39
#define WLAN_EID_QUIET          40
#define WLAN_EID_IBSS_DFS       41
#define WLAN_EID_ERP            42
// reference 802.11i 7.3.2 table 20
#define WLAN_EID_RSN            48
#define WLAN_EID_EXTSUPP_RATES  50
// reference WiFi WPA spec.
#define WLAN_EID_RSN_WPA        221

#define WLAN_EID_ERP_NONERP_PRESENT             0x01
#define WLAN_EID_ERP_USE_PROTECTION             0x02
#define WLAN_EID_ERP_BARKER_MODE                0x04

// Reason Codes
#define WLAN_MGMT_REASON_RSVD                       0
#define WLAN_MGMT_REASON_UNSPEC                     1
#define WLAN_MGMT_REASON_PRIOR_AUTH_INVALID         2
#define WLAN_MGMT_REASON_DEAUTH_LEAVING             3
#define WLAN_MGMT_REASON_DISASSOC_INACTIVE          4
#define WLAN_MGMT_REASON_DISASSOC_AP_BUSY           5
#define WLAN_MGMT_REASON_CLASS2_NONAUTH             6
#define WLAN_MGMT_REASON_CLASS3_NONASSOC            7
#define WLAN_MGMT_REASON_DISASSOC_STA_HASLEFT       8
#define WLAN_MGMT_REASON_CANT_ASSOC_NONAUTH         9
#define WLAN_MGMT_REASON_DISASSOC_PWR_CAP_UNACCEPT      10
#define WLAN_MGMT_REASON_DISASSOC_SUPP_CH_UNACCEPT      11
#define WLAN_MGMT_REASON_INVALID_IE                 13
#define WLAN_MGMT_REASON_MIC_FAILURE                14
#define WLAN_MGMT_REASON_4WAY_HANDSHAKE_TIMEOUT     15
#define WLAN_MGMT_REASON_GRPKEY_UPDATE_TIMEOUT      16
#define WLAN_MGMT_REASON_4WAY_INFO_DIFFERENT        17
#define WLAN_MGMT_REASON_MULTCAST_CIPHER_INVALID    18
#define WLAN_MGMT_REASON_UNCAST_CIPHER_INVALID      19
#define WLAN_MGMT_REASON_AKMP_INVALID               20
#define WLAN_MGMT_REASON_RSNE_UNSUPPORTED           21
#define WLAN_MGMT_REASON_RSNE_CAP_INVALID           22
#define WLAN_MGMT_REASON_80211X_AUTH_FAILED         23

// Status Codes
#define WLAN_MGMT_STATUS_SUCCESS                        0
#define WLAN_MGMT_STATUS_UNSPEC_FAILURE                 1
#define WLAN_MGMT_STATUS_CAPS_UNSUPPORTED               10
#define WLAN_MGMT_STATUS_REASSOC_NO_ASSOC               11
#define WLAN_MGMT_STATUS_ASSOC_DENIED_UNSPEC            12
#define WLAN_MGMT_STATUS_UNSUPPORTED_AUTHALG            13
#define WLAN_MGMT_STATUS_RX_AUTH_NOSEQ                  14
#define WLAN_MGMT_STATUS_CHALLENGE_FAIL                 15
#define WLAN_MGMT_STATUS_AUTH_TIMEOUT                   16
#define WLAN_MGMT_STATUS_ASSOC_DENIED_BUSY              17
#define WLAN_MGMT_STATUS_ASSOC_DENIED_RATES             18
#define WLAN_MGMT_STATUS_ASSOC_DENIED_SHORTPREAMBLE     19
#define WLAN_MGMT_STATUS_ASSOC_DENIED_PBCC              20
#define WLAN_MGMT_STATUS_ASSOC_DENIED_AGILITY           21

// reference 802.11h 7.3.1.9
//
#define WLAN_MGMT_STATUS_ASSOC_REJECT_BCS_SPECTRUM_MNG  22
#define WLAN_MGMT_STATUS_ASSOC_REJECT_BCS_PWR_CAP       23
#define WLAN_MGMT_STATUS_ASSOC_REJECT_BCS_SUPP_CH       24
//
// reference 802.11g 7.3.1.9
//
#define WLAN_MGMT_STATUS_SHORTSLOTTIME_UNSUPPORTED      25
#define WLAN_MGMT_STATUS_DSSSOFDM_UNSUPPORTED           26
//
// reference 802.11i 7.3.1.9 table 19
//
#define WLAN_MGMT_STATUS_INVALID_IE                     40
#define WLAN_MGMT_STATUS_GROUP_CIPHER_INVALID           41
#define WLAN_MGMT_STATUS_PAIRWISE_CIPHER_INVALID        42
#define WLAN_MGMT_STATUS_AKMP_INVALID                   43
#define WLAN_MGMT_STATUS_UNSUPPORT_RSN_IE_VER           44
#define WLAN_MGMT_STATUS_INVALID_RSN_IE_CAP             45
#define WLAN_MGMT_STATUS_CIPHER_REJECT                  46

// Auth Algorithm
#define WLAN_AUTH_ALG_OPENSYSTEM                0
#define WLAN_AUTH_ALG_SHAREDKEY                 1

// Management Frame Field Offsets
// Note: Not all fields are listed because of variable lengths.
// Note: These offsets are from the start of the frame data

#define WLAN_BEACON_OFF_TS                  0
#define WLAN_BEACON_OFF_BCN_INT             8
#define WLAN_BEACON_OFF_CAPINFO             10
#define WLAN_BEACON_OFF_SSID                12

#define WLAN_DISASSOC_OFF_REASON            0

#define WLAN_ASSOCREQ_OFF_CAP_INFO          0
#define WLAN_ASSOCREQ_OFF_LISTEN_INT        2
#define WLAN_ASSOCREQ_OFF_SSID              4

#define WLAN_ASSOCRESP_OFF_CAP_INFO         0
#define WLAN_ASSOCRESP_OFF_STATUS           2
#define WLAN_ASSOCRESP_OFF_AID              4
#define WLAN_ASSOCRESP_OFF_SUPP_RATES       6

#define WLAN_REASSOCREQ_OFF_CAP_INFO        0
#define WLAN_REASSOCREQ_OFF_LISTEN_INT      2
#define WLAN_REASSOCREQ_OFF_CURR_AP         4
#define WLAN_REASSOCREQ_OFF_SSID            10

#define WLAN_REASSOCRESP_OFF_CAP_INFO       0
#define WLAN_REASSOCRESP_OFF_STATUS         2
#define WLAN_REASSOCRESP_OFF_AID            4
#define WLAN_REASSOCRESP_OFF_SUPP_RATES     6

#define WLAN_PROBEREQ_OFF_SSID              0

#define WLAN_PROBERESP_OFF_TS               0
#define WLAN_PROBERESP_OFF_BCN_INT          8
#define WLAN_PROBERESP_OFF_CAP_INFO         10
#define WLAN_PROBERESP_OFF_SSID             12

#define WLAN_AUTHEN_OFF_AUTH_ALG            0
#define WLAN_AUTHEN_OFF_AUTH_SEQ            2
#define WLAN_AUTHEN_OFF_STATUS              4
#define WLAN_AUTHEN_OFF_CHALLENGE           6

#define WLAN_DEAUTHEN_OFF_REASON            0

//
// Cipher Suite Selectors defined in 802.11i
//
#define WLAN_11i_CSS_USE_GROUP              0
#define WLAN_11i_CSS_WEP40                  1
#define WLAN_11i_CSS_TKIP                   2
#define WLAN_11i_CSS_CCMP                   4
#define WLAN_11i_CSS_WEP104                 5
#define WLAN_11i_CSS_UNKNOWN                255

//
// Authentication and Key Management Suite Selectors defined in 802.11i
//
#define WLAN_11i_AKMSS_802_1X               1
#define WLAN_11i_AKMSS_PSK                  2
#define WLAN_11i_AKMSS_UNKNOWN              255

// Measurement type definitions reference ieee 802.11h Table 20b
#define MEASURE_TYPE_BASIC      0
#define MEASURE_TYPE_CCA        1
#define MEASURE_TYPE_RPI        2

// Measurement request mode definitions reference ieee 802.11h Figure 46h
#define MEASURE_MODE_ENABLE     0x02
#define MEASURE_MODE_REQ        0x04
#define MEASURE_MODE_REP        0x08

// Measurement report mode definitions reference ieee 802.11h Figure 46m
#define MEASURE_MODE_LATE       0x01
#define MEASURE_MODE_INCAPABLE  0x02
#define MEASURE_MODE_REFUSED    0x04

/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Types  ------------------------------*/

// Information Element Types

#pragma pack(1)
typedef struct tagWLAN_IE {
	unsigned char byElementID;
	unsigned char len;
} __attribute__ ((__packed__))
WLAN_IE, *PWLAN_IE;

// Service Set Identity (SSID)
#pragma pack(1)
typedef struct tagWLAN_IE_SSID {
	unsigned char byElementID;
	unsigned char len;
	unsigned char abySSID[1];
} __attribute__ ((__packed__))
WLAN_IE_SSID, *PWLAN_IE_SSID;

// Supported Rates
#pragma pack(1)
typedef struct tagWLAN_IE_SUPP_RATES {
	unsigned char byElementID;
	unsigned char len;
	unsigned char abyRates[1];
} __attribute__ ((__packed__))
WLAN_IE_SUPP_RATES,  *PWLAN_IE_SUPP_RATES;

// FH Parameter Set
#pragma pack(1)
typedef struct _WLAN_IE_FH_PARMS {
	unsigned char byElementID;
	unsigned char len;
	unsigned short wDwellTime;
	unsigned char byHopSet;
	unsigned char byHopPattern;
	unsigned char byHopIndex;
} WLAN_IE_FH_PARMS,  *PWLAN_IE_FH_PARMS;

// DS Parameter Set
#pragma pack(1)
typedef struct tagWLAN_IE_DS_PARMS {
	unsigned char byElementID;
	unsigned char len;
	unsigned char byCurrChannel;
} __attribute__ ((__packed__))
WLAN_IE_DS_PARMS,  *PWLAN_IE_DS_PARMS;

// CF Parameter Set
#pragma pack(1)
typedef struct tagWLAN_IE_CF_PARMS {
	unsigned char byElementID;
	unsigned char len;
	unsigned char byCFPCount;
	unsigned char byCFPPeriod;
	unsigned short wCFPMaxDuration;
	unsigned short wCFPDurRemaining;
} __attribute__ ((__packed__))
WLAN_IE_CF_PARMS,  *PWLAN_IE_CF_PARMS;

// TIM
#pragma pack(1)
typedef struct tagWLAN_IE_TIM {
	unsigned char byElementID;
	unsigned char len;
	unsigned char byDTIMCount;
	unsigned char byDTIMPeriod;
	unsigned char byBitMapCtl;
	unsigned char byVirtBitMap[1];
} __attribute__ ((__packed__))
WLAN_IE_TIM,  *PWLAN_IE_TIM;

// IBSS Parameter Set
#pragma pack(1)
typedef struct tagWLAN_IE_IBSS_PARMS {
	unsigned char byElementID;
	unsigned char len;
	unsigned short wATIMWindow;
} __attribute__ ((__packed__))
WLAN_IE_IBSS_PARMS, *PWLAN_IE_IBSS_PARMS;

// Challenge Text
#pragma pack(1)
typedef struct tagWLAN_IE_CHALLENGE {
	unsigned char byElementID;
	unsigned char len;
	unsigned char abyChallenge[1];
} __attribute__ ((__packed__))
WLAN_IE_CHALLENGE,  *PWLAN_IE_CHALLENGE;

#pragma pack(1)
typedef struct tagWLAN_IE_RSN_EXT {
	unsigned char byElementID;
	unsigned char len;
	unsigned char abyOUI[4];
	unsigned short wVersion;
	unsigned char abyMulticast[4];
	unsigned short wPKCount;
	struct {
		unsigned char abyOUI[4];
	} PKSList[1]; // the rest is variable so need to
	// overlay ieauth structure
} WLAN_IE_RSN_EXT, *PWLAN_IE_RSN_EXT;

#pragma pack(1)
typedef struct tagWLAN_IE_RSN_AUTH {
	unsigned short wAuthCount;
	struct {
		unsigned char abyOUI[4];
	} AuthKSList[1];
} WLAN_IE_RSN_AUTH, *PWLAN_IE_RSN_AUTH;

// RSN Identity
#pragma pack(1)
typedef struct tagWLAN_IE_RSN {
	unsigned char byElementID;
	unsigned char len;
	unsigned short wVersion;
	unsigned char abyRSN[WLAN_MIN_ARRAY];
} WLAN_IE_RSN, *PWLAN_IE_RSN;

// ERP
#pragma pack(1)
typedef struct tagWLAN_IE_ERP {
	unsigned char byElementID;
	unsigned char len;
	unsigned char byContext;
} __attribute__ ((__packed__))
WLAN_IE_ERP,  *PWLAN_IE_ERP;

#pragma pack(1)
typedef struct _MEASEURE_REQ {
	unsigned char byChannel;
	unsigned char abyStartTime[8];
	unsigned char abyDuration[2];
} MEASEURE_REQ, *PMEASEURE_REQ,
	MEASEURE_REQ_BASIC, *PMEASEURE_REQ_BASIC,
	MEASEURE_REQ_CCA, *PMEASEURE_REQ_CCA,
	MEASEURE_REQ_RPI, *PMEASEURE_REQ_RPI;

typedef struct _MEASEURE_REP_BASIC {
	unsigned char byChannel;
	unsigned char abyStartTime[8];
	unsigned char abyDuration[2];
	unsigned char byMap;
} MEASEURE_REP_BASIC, *PMEASEURE_REP_BASIC;

typedef struct _MEASEURE_REP_CCA {
	unsigned char byChannel;
	unsigned char abyStartTime[8];
	unsigned char abyDuration[2];
	unsigned char byCCABusyFraction;
} MEASEURE_REP_CCA, *PMEASEURE_REP_CCA;

typedef struct _MEASEURE_REP_RPI {
	unsigned char byChannel;
	unsigned char abyStartTime[8];
	unsigned char abyDuration[2];
	unsigned char abyRPIdensity[8];
} MEASEURE_REP_RPI, *PMEASEURE_REP_RPI;

typedef union _MEASEURE_REP {
	MEASEURE_REP_BASIC  sBasic;
	MEASEURE_REP_CCA    sCCA;
	MEASEURE_REP_RPI    sRPI;
} MEASEURE_REP, *PMEASEURE_REP;

typedef struct _WLAN_IE_MEASURE_REQ {
	unsigned char byElementID;
	unsigned char len;
	unsigned char byToken;
	unsigned char byMode;
	unsigned char byType;
	MEASEURE_REQ        sReq;
} WLAN_IE_MEASURE_REQ, *PWLAN_IE_MEASURE_REQ;

typedef struct _WLAN_IE_MEASURE_REP {
	unsigned char byElementID;
	unsigned char len;
	unsigned char byToken;
	unsigned char byMode;
	unsigned char byType;
	MEASEURE_REP        sRep;
} WLAN_IE_MEASURE_REP, *PWLAN_IE_MEASURE_REP;

typedef struct _WLAN_IE_CH_SW {
	unsigned char byElementID;
	unsigned char len;
	unsigned char byMode;
	unsigned char byChannel;
	unsigned char byCount;
} WLAN_IE_CH_SW, *PWLAN_IE_CH_SW;

typedef struct _WLAN_IE_QUIET {
	unsigned char byElementID;
	unsigned char len;
	unsigned char byQuietCount;
	unsigned char byQuietPeriod;
	unsigned char abyQuietDuration[2];
	unsigned char abyQuietOffset[2];
} WLAN_IE_QUIET, *PWLAN_IE_QUIET;

typedef struct _WLAN_IE_COUNTRY {
	unsigned char byElementID;
	unsigned char len;
	unsigned char abyCountryString[3];
	unsigned char abyCountryInfo[3];
} WLAN_IE_COUNTRY, *PWLAN_IE_COUNTRY;

typedef struct _WLAN_IE_PW_CONST {
	unsigned char byElementID;
	unsigned char len;
	unsigned char byPower;
} WLAN_IE_PW_CONST, *PWLAN_IE_PW_CONST;

typedef struct _WLAN_IE_PW_CAP {
	unsigned char byElementID;
	unsigned char len;
	unsigned char byMinPower;
	unsigned char byMaxPower;
} WLAN_IE_PW_CAP, *PWLAN_IE_PW_CAP;

typedef struct _WLAN_IE_SUPP_CH {
	unsigned char byElementID;
	unsigned char len;
	unsigned char abyChannelTuple[2];
} WLAN_IE_SUPP_CH, *PWLAN_IE_SUPP_CH;

typedef struct _WLAN_IE_TPC_REQ {
	unsigned char byElementID;
	unsigned char len;
} WLAN_IE_TPC_REQ, *PWLAN_IE_TPC_REQ;

typedef struct _WLAN_IE_TPC_REP {
	unsigned char byElementID;
	unsigned char len;
	unsigned char byTxPower;
	unsigned char byLinkMargin;
} WLAN_IE_TPC_REP, *PWLAN_IE_TPC_REP;

typedef struct _WLAN_IE_IBSS_DFS {
	unsigned char byElementID;
	unsigned char len;
	unsigned char abyDFSOwner[6];
	unsigned char byDFSRecovery;
	unsigned char abyChannelMap[2];
} WLAN_IE_IBSS_DFS, *PWLAN_IE_IBSS_DFS;

#pragma pack()

// Frame Types
// prototype structure, all mgmt frame types will start with these members
typedef struct tagWLAN_FR_MGMT {
	unsigned int	uType;
	unsigned int	len;
	unsigned char *pBuf;
	PUWLAN_80211HDR       pHdr;
} WLAN_FR_MGMT,  *PWLAN_FR_MGMT;

// Beacon frame
typedef struct tagWLAN_FR_BEACON {
	unsigned int	uType;
	unsigned int	len;
	unsigned char *pBuf;
	PUWLAN_80211HDR         pHdr;
	// fixed fields
	PQWORD                  pqwTimestamp;
	unsigned short *pwBeaconInterval;
	unsigned short *pwCapInfo;
	/*-- info elements ----------*/
	PWLAN_IE_SSID           pSSID;
	PWLAN_IE_SUPP_RATES     pSuppRates;
//  PWLAN_IE_FH_PARMS       pFHParms;
	PWLAN_IE_DS_PARMS       pDSParms;
	PWLAN_IE_CF_PARMS       pCFParms;
	PWLAN_IE_TIM            pTIM;
	PWLAN_IE_IBSS_PARMS     pIBSSParms;
	PWLAN_IE_RSN            pRSN;
	PWLAN_IE_RSN_EXT        pRSNWPA;
	PWLAN_IE_ERP            pERP;
	PWLAN_IE_SUPP_RATES     pExtSuppRates;
	PWLAN_IE_COUNTRY        pIE_Country;
	PWLAN_IE_PW_CONST       pIE_PowerConstraint;
	PWLAN_IE_CH_SW          pIE_CHSW;
	PWLAN_IE_IBSS_DFS       pIE_IBSSDFS;
	PWLAN_IE_QUIET          pIE_Quiet;
} WLAN_FR_BEACON, *PWLAN_FR_BEACON;

// IBSS ATIM frame
typedef struct tagWLAN_FR_IBSSATIM {
	unsigned int	uType;
	unsigned int	len;
	unsigned char *pBuf;
	PUWLAN_80211HDR         pHdr;

	// fixed fields
	// info elements
	// this frame type has a null body
} WLAN_FR_IBSSATIM, *PWLAN_FR_IBSSATIM;

// Disassociation
typedef struct tagWLAN_FR_DISASSOC {
	unsigned int	uType;
	unsigned int	len;
	unsigned char *pBuf;
	PUWLAN_80211HDR         pHdr;
	/*-- fixed fields -----------*/
	unsigned short *pwReason;
	/*-- info elements ----------*/
} WLAN_FR_DISASSOC, *PWLAN_FR_DISASSOC;

// Association Request
typedef struct tagWLAN_FR_ASSOCREQ {
	unsigned int	uType;
	unsigned int	len;
	unsigned char *pBuf;
	PUWLAN_80211HDR         pHdr;
	/*-- fixed fields -----------*/
	unsigned short *pwCapInfo;
	unsigned short *pwListenInterval;
	/*-- info elements ----------*/
	PWLAN_IE_SSID           pSSID;
	PWLAN_IE_SUPP_RATES     pSuppRates;
	PWLAN_IE_RSN            pRSN;
	PWLAN_IE_RSN_EXT        pRSNWPA;
	PWLAN_IE_SUPP_RATES     pExtSuppRates;
	PWLAN_IE_PW_CAP         pCurrPowerCap;
	PWLAN_IE_SUPP_CH        pCurrSuppCh;
} WLAN_FR_ASSOCREQ, *PWLAN_FR_ASSOCREQ;

// Association Response
typedef struct tagWLAN_FR_ASSOCRESP {
	unsigned int	uType;
	unsigned int	len;
	unsigned char *pBuf;
	PUWLAN_80211HDR         pHdr;
	/*-- fixed fields -----------*/
	unsigned short *pwCapInfo;
	unsigned short *pwStatus;
	unsigned short *pwAid;
	/*-- info elements ----------*/
	PWLAN_IE_SUPP_RATES     pSuppRates;
	PWLAN_IE_SUPP_RATES     pExtSuppRates;
} WLAN_FR_ASSOCRESP, *PWLAN_FR_ASSOCRESP;

// Reassociation Request
typedef struct tagWLAN_FR_REASSOCREQ {
	unsigned int	uType;
	unsigned int	len;
	unsigned char *pBuf;
	PUWLAN_80211HDR         pHdr;

	/*-- fixed fields -----------*/
	unsigned short *pwCapInfo;
	unsigned short *pwListenInterval;
	PIEEE_ADDR              pAddrCurrAP;

	/*-- info elements ----------*/
	PWLAN_IE_SSID           pSSID;
	PWLAN_IE_SUPP_RATES     pSuppRates;
	PWLAN_IE_RSN            pRSN;
	PWLAN_IE_RSN_EXT        pRSNWPA;
	PWLAN_IE_SUPP_RATES     pExtSuppRates;
} WLAN_FR_REASSOCREQ, *PWLAN_FR_REASSOCREQ;

// Reassociation Response
typedef struct tagWLAN_FR_REASSOCRESP {
	unsigned int	uType;
	unsigned int	len;
	unsigned char *pBuf;
	PUWLAN_80211HDR         pHdr;
	/*-- fixed fields -----------*/
	unsigned short *pwCapInfo;
	unsigned short *pwStatus;
	unsigned short *pwAid;
	/*-- info elements ----------*/
	PWLAN_IE_SUPP_RATES     pSuppRates;
	PWLAN_IE_SUPP_RATES     pExtSuppRates;
} WLAN_FR_REASSOCRESP, *PWLAN_FR_REASSOCRESP;

// Probe Request
typedef struct tagWLAN_FR_PROBEREQ {
	unsigned int	uType;
	unsigned int	len;
	unsigned char *pBuf;
	PUWLAN_80211HDR         pHdr;
	/*-- fixed fields -----------*/
	/*-- info elements ----------*/
	PWLAN_IE_SSID           pSSID;
	PWLAN_IE_SUPP_RATES     pSuppRates;
	PWLAN_IE_SUPP_RATES     pExtSuppRates;
} WLAN_FR_PROBEREQ, *PWLAN_FR_PROBEREQ;

// Probe Response
typedef struct tagWLAN_FR_PROBERESP {
	unsigned int	uType;
	unsigned int	len;
	unsigned char *pBuf;
	PUWLAN_80211HDR         pHdr;
	/*-- fixed fields -----------*/
	PQWORD                  pqwTimestamp;
	unsigned short *pwBeaconInterval;
	unsigned short *pwCapInfo;
	/*-- info elements ----------*/
	PWLAN_IE_SSID           pSSID;
	PWLAN_IE_SUPP_RATES     pSuppRates;
	PWLAN_IE_DS_PARMS       pDSParms;
	PWLAN_IE_CF_PARMS       pCFParms;
	PWLAN_IE_IBSS_PARMS     pIBSSParms;
	PWLAN_IE_RSN            pRSN;
	PWLAN_IE_RSN_EXT        pRSNWPA;
	PWLAN_IE_ERP            pERP;
	PWLAN_IE_SUPP_RATES     pExtSuppRates;
	PWLAN_IE_COUNTRY        pIE_Country;
	PWLAN_IE_PW_CONST       pIE_PowerConstraint;
	PWLAN_IE_CH_SW          pIE_CHSW;
	PWLAN_IE_IBSS_DFS       pIE_IBSSDFS;
	PWLAN_IE_QUIET          pIE_Quiet;
} WLAN_FR_PROBERESP, *PWLAN_FR_PROBERESP;

// Authentication
typedef struct tagWLAN_FR_AUTHEN {
	unsigned int	uType;
	unsigned int	len;
	unsigned char *pBuf;
	PUWLAN_80211HDR         pHdr;
	/*-- fixed fields -----------*/
	unsigned short *pwAuthAlgorithm;
	unsigned short *pwAuthSequence;
	unsigned short *pwStatus;
	/*-- info elements ----------*/
	PWLAN_IE_CHALLENGE      pChallenge;
} WLAN_FR_AUTHEN, *PWLAN_FR_AUTHEN;

// Deauthenication
typedef struct tagWLAN_FR_DEAUTHEN {
	unsigned int	uType;
	unsigned int	len;
	unsigned char *pBuf;
	PUWLAN_80211HDR         pHdr;
	/*-- fixed fields -----------*/
	unsigned short *pwReason;

	/*-- info elements ----------*/
} WLAN_FR_DEAUTHEN, *PWLAN_FR_DEAUTHEN;

/*---------------------  Export Functions  --------------------------*/

void
vMgrEncodeBeacon(
	PWLAN_FR_BEACON  pFrame
);

void
vMgrDecodeBeacon(
	PWLAN_FR_BEACON  pFrame
);

void
vMgrEncodeIBSSATIM(
	PWLAN_FR_IBSSATIM   pFrame
);

void
vMgrDecodeIBSSATIM(
	PWLAN_FR_IBSSATIM   pFrame
);

void
vMgrEncodeDisassociation(
	PWLAN_FR_DISASSOC  pFrame
);

void
vMgrDecodeDisassociation(
	PWLAN_FR_DISASSOC  pFrame
);

void
vMgrEncodeAssocRequest(
	PWLAN_FR_ASSOCREQ  pFrame
);

void
vMgrDecodeAssocRequest(
	PWLAN_FR_ASSOCREQ  pFrame
);

void
vMgrEncodeAssocResponse(
	PWLAN_FR_ASSOCRESP  pFrame
);

void
vMgrDecodeAssocResponse(
	PWLAN_FR_ASSOCRESP  pFrame
);

void
vMgrEncodeReassocRequest(
	PWLAN_FR_REASSOCREQ  pFrame
);

void
vMgrDecodeReassocRequest(
	PWLAN_FR_REASSOCREQ  pFrame
);

void
vMgrEncodeProbeRequest(
	PWLAN_FR_PROBEREQ  pFrame
);

void
vMgrDecodeProbeRequest(
	PWLAN_FR_PROBEREQ  pFrame
);

void
vMgrEncodeProbeResponse(
	PWLAN_FR_PROBERESP  pFrame
);

void
vMgrDecodeProbeResponse(
	PWLAN_FR_PROBERESP  pFrame
);

void
vMgrEncodeAuthen(
	PWLAN_FR_AUTHEN  pFrame
);

void
vMgrDecodeAuthen(
	PWLAN_FR_AUTHEN  pFrame
);

void
vMgrEncodeDeauthen(
	PWLAN_FR_DEAUTHEN  pFrame
);

void
vMgrDecodeDeauthen(
	PWLAN_FR_DEAUTHEN  pFrame
);

void
vMgrEncodeReassocResponse(
	PWLAN_FR_REASSOCRESP  pFrame
);

void
vMgrDecodeReassocResponse(
	PWLAN_FR_REASSOCRESP  pFrame
);

#endif// __80211MGR_H__
