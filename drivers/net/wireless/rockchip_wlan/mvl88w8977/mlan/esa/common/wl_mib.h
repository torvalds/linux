/** @file  wl_mib.h
 *
 *  @brief This file contains the MIB structure definitions based on IEEE 802.11 specification.
 *
 * Copyright (C) 2014-2017, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

/******************************************************
Change log:
    03/07/2014: Initial version
******************************************************/
#if !defined(WL_MIB_H__)

#define WL_MIB_H__
#include "wl_mib_rom.h"

/*============================================================================= */
/*                    Management Information Base STRUCTURES (IEEE 802.11) */
/*============================================================================= */

/*-----------------------------*/
/* Station Configuration Table */
/*-----------------------------*/

typedef struct MIB_StaCfg_s
{
	UINT8 CfPeriod;		/* 0 to 255              */
	UINT16 CfpMax;		/* 0 to 65535            */
	UINT8 PwrMgtMode;	/* PwrMgmtMode_e values  */
	UINT8 OpRateSet[16];	/* 16 byte array is sufficient for
				   14 rate */
#ifdef DOT11H
	Boolean dot11SpectrumManagementRequired;
#endif
	Boolean dot11WWSenabled;
} MIB_STA_CFG;

/*------------------------*/
/* WEP Key Mappings Table */
/*------------------------*/

/* This struct is used in ROM and it should not be changed at all */
typedef struct MIB_WepKeyMappings_s {
	UINT32 WepKeyMappingIdx;
	IEEEtypes_MacAddr_t WepKeyMappingAddr;
	UINT8 WepKeyMappingWepOn;	/* SNMP_Boolean_e values */
	UINT8 WepKeyMappingVal[WEP_KEY_USER_INPUT];	/* 5 byte string */
	UINT8 WepKeyMappingStatus;	/* SNMP_Rowstatus_e values */
} MIB_WEP_KEY_MAPPINGS;

/*---------------*/
/* Privacy Table */
/*---------------*/

typedef struct MIB_PrivacyTable_s {
	UINT8 PrivInvoked;	/* SNMP_Boolean_e values */
	UINT8 WepDefaultKeyId;	/* 0 to 3 */
	UINT32 WepKeyMappingLen;	/* 10 to 4294967295 */
	UINT8 ExcludeUnencrypt;	/* SNMP_Boolean_e values */
	UINT32 WepIcvErrCnt;
	UINT32 WepExcludedCnt;
	UINT8 RSNEnabled;	/* SNMP_Boolean_e values */
} MIB_PRIVACY_TABLE;

/*============================================================================= */
/*                             MAC ATTRIBUTES */
/*============================================================================= */

/*---------------------*/
/* MAC Operation Table */
/*---------------------*/

typedef struct MIB_OpData_s
{
	IEEEtypes_MacAddr_t StaMacAddr;
	UINT16 RtsThresh;	/* 0 to 2347 */
	UINT8 ShortRetryLim;	/* 1 to 255 */
	UINT8 LongRetryLim;	/* 1 to 255 */
	UINT16 FragThresh;	/* 256 to 2346 */
	UINT32 MaxTxMsduLife;	/* 1 to 4294967295 */
	UINT32 MaxRxLife;	/* 1 to 4294967295 */
#ifdef IN_USE
	UINT8 ManufId[128];	/* 128 byte string */
	UINT8 ProdId[128];	/* 128 byte string */
#endif
} MIB_OP_DATA;

/*----------------*/
/* Counters Table */
/*----------------*/

typedef struct MIB_Counters_s {
	UINT32 RxFrmCnt;
	UINT32 MulticastTxFrmCnt;
	UINT32 FailedCnt;
	UINT32 RetryCnt;
	UINT32 MultRetryCnt;
	UINT32 FrmDupCnt;
	UINT32 RtsSuccessCnt;
	UINT32 RtsFailCnt;
	UINT32 AckFailCnt;
	UINT32 RxFragCnt;
	UINT32 MulticastRxFrmCnt;
	UINT32 FcsErrCnt;
	UINT32 TxFrmCnt;
	UINT32 WepUndecryptCnt;
} MIB_COUNTERS;

/*-----------------------*/
/* Group Addresses Table */
/*-----------------------*/

typedef struct MIB_GroupAddr_s {
	UINT32 GroupAddrIdx;
	IEEEtypes_MacAddr_t Addr;
	UINT8 GroupAddrStatus;	/* SNMP_Rowstatus_e values */
} MIB_GROUP_ADDR;

/*----------------------------*/
/* Resource Information Table */
/*----------------------------*/

typedef struct MIB_RsrcInfo_s {
	UINT8 ManufOui[3];	/* 3 byte string */
	UINT8 ManufName[128];	/* 128 byte string */
	UINT8 ManufProdName[128];	/* 128 byte string */
	UINT8 ManufProdVer[128];	/* 128 byte string */
} MIB_RESOURCE_INFO;

/*============================================================================= */
/*                             PHY ATTRIBUTES */
/*============================================================================= */

/*---------------------*/
/* PHY Operation Table */
/*---------------------*/
typedef struct MIB_PhyOpTable_s {
	UINT8 PhyType;		/* SNMP_PhyType_e values */
	UINT32 CurrRegDomain;
	UINT8 TempType;		/* SNMP_TempType_e values */
} MIB_PHY_OP_TABLE;

/*-------------------*/

/* PHY Antenna Table */

/*-------------------*/

typedef struct MIB_PhyAntTable_s {
	UINT8 CurrTxAnt;	/* 1 to 255 */
	UINT8 DivSupport;	/* SNMP_DivSupp_e values */
	UINT8 CurrRxAnt;	/* 1 to 255 */
} MIB_PHY_ANT_TABLE;

typedef struct MIB_PhyAntSelect_s {
	UINT8 SelectRxAnt;	/* 0 to 1 */
	UINT8 SelectTxAnt;	/* 0 to 1 */
	UINT8 DiversityRxAnt;	/* Boolean */
	UINT8 DiversityTxAnt;	/* Boolean */
} MIB_PHY_ANT_SELECT;

/*--------------------------*/
/* PHY Transmit Power Table */
/*--------------------------*/

typedef struct MIB_PhyTxPwrTable_s {
	UINT8 NumSuppPwrLevels;	/* 1 to 8 */
	UINT16 TxPwrLevel1;	/* 0 to 10000 */
	UINT16 TxPwrLevel2;	/* 0 to 10000 */
	UINT16 TxPwrLevel3;	/* 0 to 10000 */
	UINT16 TxPwrLevel4;	/* 0 to 10000 */
	UINT16 TxPwrLevel5;	/* 0 to 10000 */
	UINT16 TxPwrLevel6;	/* 0 to 10000 */
	UINT16 TxPwrLevel7;	/* 0 to 10000 */
	UINT16 TxPwrLevel8;	/* 0 to 10000 */
	UINT8 CurrTxPwrLevel;	/* 1 to 8 */
} MIB_PHY_TX_POWER_TABLE;

/*---------------------------------------------*/

/* PHY Frequency Hopping Spread Spectrum Table */

/*---------------------------------------------*/

typedef struct MIB_PhyFHSSTable_s {

	UINT8 HopTime;		/* 224? */
	UINT8 CurrChanNum;	/* 0 to 99 */
	UINT16 MaxDwellTime;	/* 0 to 65535 */
	UINT16 CurrDwellTime;	/* 0 to 65535 */
	UINT16 CurrSet;		/* 0 to 255 */
	UINT16 CurrPattern;	/* 0 to 255 */
	UINT16 CurrIdx;		/* 0 to 255 */

} MIB_PHY_FHSS_TABLE;

/*-------------------------------------------*/

/* PHY Direct Sequence Spread Spectrum Table */

/*-------------------------------------------*/

typedef enum MIB_CCAMode_s {
	ENERGY_DETECT_ONLY = 1,
	CARRIER_SENSE_ONLY = 2,
	CARRIER_SENSE_AND_ENERGY_DETECT = 4
} MIB_CCA_MODE;

typedef struct MIB_PhyDSSSTable_s {
	UINT8 CurrChan;		/* 0 to 14 */
	UINT8 CcaModeSupp;	/* 1 to 7 */
	UINT16 CurrCcaMode;	/* MIB_CCA_MODE values only */
	UINT32 EdThresh;
} MIB_PHY_DSSS_TABLE;

/*--------------*/

/* PHY IR Table */

/*--------------*/

typedef struct MIB_PhyIRTable_s {
	UINT32 CcaWatchDogTmrMax;
	UINT32 CcaWatchDogCntMax;
	UINT32 CcaWatchDogTmrMin;
	UINT32 CcaWatchDogCntMin;
} MIB_PHY_IR_TABLE;

/*----------------------------------------*/

/* PHY Regulatory Domains Supported Table */

/*----------------------------------------*/

typedef struct MIB_PhyRegDomainsSupp_s {
	UINT32 RegDomainsSuppIdx;
	UINT8 RegDomainsSuppVal;	/*SNMP_RegDomainsSuppVal_e values */
} MIB_PHY_REG_DOMAINS_SUPPPORTED;

/*-------------------------*/

/* PHY Antennas List Table */

/*-------------------------*/

typedef struct MIB_PhyAntList_s {
	UINT8 AntListIdx;
	UINT8 SuppTxAnt;	/*SNMP_Boolean_e values */
	UINT8 SuppRxAnt;	/*SNMP_Boolean_e values */
	UINT8 RxDiv;		/*SNMP_Boolean_e values */
} MIB_PHY_ANT_LIST;

/*----------------------------------------*/

/* PHY Supported Receive Data Rates Table */

/*----------------------------------------*/

typedef struct MIB_PhySuppDataRatesRx_s {
	UINT8 SuppDataRatesRxIdx;	/*1 to 8 */
	UINT8 SuppDataRatesRxVal;	/*2 to 127 */
} MIB_PHY_SUPP_DATA_RATES_RX;

typedef struct MIB_DHCP_s {
	UINT32 IPAddr;
	UINT32 SubnetMask;
	UINT32 GwyAddr;

#ifdef GATEWAY
	UINT32 PrimaryDNS;
	UINT32 SecondaryDNS;
#endif

} MIB_DHCP;

#if defined(GATEWAY)

typedef struct MIB_IP_LAN_s {
	UINT32 IPAddr;
	UINT32 SubnetMask;
} MIB_IP_LAN;

#endif

/* Added for WB31 */

typedef struct _MIB_WB {
	UINT8 devName[16];	// Must be a string:
	//      15 Max characters
	UINT8 cloneMacAddr[6];	// cloned MAC Address
	UINT8 opMode;		// 0 for infrastructure,
	// 1 for ad-hoc
	UINT8 macCloneEnable;	// boolean
} MIB_WB;

/* Added for WB31 end */

/*---------------------*/

/* RSN Config Table */

/*---------------------*/

typedef struct MIB_RSNConfig_s {
	UINT32 Index;
	UINT32 Version;
	UINT32 PairwiseKeysSupported;
	UINT8 MulticastCipher[4];
	UINT8 GroupRekeyMethod;
	UINT32 GroupRekeyTime;
	UINT32 GroupRekeyPackets;
	UINT8 GroupRekeyStrict;
	UINT8 PSKValue[40];
	UINT8 PSKPassPhrase[64];
	UINT8 TSNEnabled;
	UINT32 GroupMasterRekeyTime;
	UINT32 GroupUpdateTimeOut;
	UINT32 GroupUpdateCount;
	UINT32 PairwiseUpdateTimeOut;
	UINT32 PairwiseUpdateCount;
} MIB_RSNCONFIG;

/*---------------------*/

/* RSN Unicast Cipher Suites Config Table */

/*---------------------*/

typedef struct MIB_RSNConfigUnicastCiphers_s {
	UINT32 Index;
	UINT8 UnicastCipher[4];
	UINT8 Enabled;
} MIB_RSNCONFIG_UNICAST_CIPHERS;

/*---------------------*/

/* RSN Authentication Suites Config Table */

/*---------------------*/

typedef struct MIB_RSNConfigAuthSuites_s {
	UINT32 Index;
	UINT8 AuthSuites[4];
	UINT8 Enabled;
} MIB_RSNCONFIG_AUTH_SUITES;

typedef struct Mrvl_MIB_RSN_GrpKey_s {
	UINT8 GrpMasterKey[32];
	UINT8 EncryptKey[16];
	UINT32 TxMICKey[2];
	UINT32 RxMICKey[2];
	UINT32 g_IV32;
	UINT16 g_IV16;
	UINT16 g_Phase1Key[5];
	UINT8 g_KeyIndex;
} MRVL_MIB_RSN_GRP_KEY;

#ifdef MIB_STATS

typedef struct Mrvl_MIB_StatsDetails {
	/* WARNING: Do not change the order of variables in this structure */
	UINT32 TKIPLocalMICFailures;	/* OID: 0x0b -> 0  */
	UINT32 CCMPDecryptErrors;	/* OID: 0x0c -> 1  */
	UINT32 WEPUndecryptableCount;	/* OID: 0x0d -> 2  */
	UINT32 WEPICVErrorCount;	/* OID: 0x0e -> 3  */
	UINT32 DecryptFailureCount;	/* OID: 0x0f -> 4  */
	UINT32 failed;		/* OID: 0x12 -> 5  */
	UINT32 retry;		/* OID: 0x13 -> 6  */
	UINT32 multiretry;	/* OID: 0x14 -> 7  */
	UINT32 framedup;	/* OID: 0x15 -> 8  */
	UINT32 rtssuccess;	/* OID: 0x16 -> 9  */
	UINT32 rtsfailure;	/* OID: 0x17 -> 10 */
	UINT32 ackfailure;	/* OID: 0x18 -> 11 */
	UINT32 rxfrag;		/* OID: 0x19 -> 12 */
	UINT32 mcastrxframe;	/* OID: 0x1a -> 13 */
	UINT32 fcserror;	/* OID: 0x1b -> 14 */
	UINT32 txframe;		/* OID: 0x1c -> 15 */
	UINT32 rsntkipcminvoked;	/* OID: 0x1d -> 16 */
	UINT32 rsn4wayhandshakefailure;	/* OID: 0x1e -> 17 */
	UINT32 mcasttxframe;	/* OID: 0x1f -> 18 */
	UINT32 TKIPICVErrors;	/* Not in the OID list */
	UINT32 TKIPReplays;	/* Not in the OID list */
	UINT32 CCMPReplays;	/* Not in the OID list */
	UINT32 CMACICVErrors;	/* Not in the OID list */
	UINT32 CMACReplays;	/* Not in the OID list */
	UINT32 WEPFragError;	/* Not in the OID list */
	UINT32 DecryptSuccessCount;	/* Not in the OID list */
	UINT32 wepicverrCnt[4];	/* Not in the OID list */

	/* EAPoL Tx Stats */
	UINT16 eapolSentTotalCnt;
	UINT16 eapolSentFrmFwCnt;
	UINT16 eapolSentSuccessCnt;
	UINT16 eapolSentFailCnt;

	/* EAPoL Rx Stats */
	UINT16 eapolRxTotalCnt;
	UINT16 eapolRxForESUPPCnt;

	/* Key Stats */
	UINT16 PTKRecvdTotalCnt;
	UINT16 PTKSentFrmESUPPCnt;

	UINT16 GTKRecvdTotalCnt;
	UINT16 GTKSentFrmESUPPCnt;
} MRVL_MIB_STATSDETAILS;

#define NUM_OF_STATS_OIDS (19)

#define INC_MIB_STAT(x, a) if (x && x->pMibStats) { x->pMibStats->data.mib.a++; }
#define INC_MIB_STAT2(x, a, b) if (x && x->pMibStats) { x->pMibStats->data.mib.a++; x->pMibStats->data.mib.b++;}
#define INC_MIB_STAT3(x, a, b, c) if (x && x->pMibStats) { x->pMibStats->data.mib.a++; x->pMibStats->data.mib.b++; x->pMibStats->data.mib.c++;}
#define CLR_MIB_STAT(x, a) if (x && x->pMibStats) { x->pMibStats->data.mib.a = 0; }

typedef struct Mrvl_MIB_Stats {
	union {
		MRVL_MIB_STATSDETAILS mib;
		UINT32 mib_stats[NUM_OF_STATS_OIDS];
	} data;
} MRVL_MIB_STATS;
#endif

typedef struct MIB_BURST_MODE {
	UINT8 mib_burstmode;
	UINT32 mib_burstrate;
} MIB_BURST_MODE;

#ifdef BRIDGE_STP

typedef struct mib_dot1dPortEntry_s {
	UINT8 mib_dot1dStpPortPriority;	/* (0..255) */
	UINT8 mib_dot1dStpPortEnable;	/*1: enable; 2: disable */
	UINT16 mib_dot1dStpPortPathCost;	/* (1..65535) */
} mib_dot1dPortEntry_t;

typedef struct mib_dot1dStp_s {
	UINT8 mib_dot1dStpPortPriority;	/* (0..255) */
	UINT8 mib_dot1dStpPortEnable;	/*1: enable; 2: disable */
	UINT16 mib_dot1dStpPortPathCost;	/* (1..65535) */
	UINT32 mib_dot1dTpAgingTime;	/* (10..1000000) */
	UINT16 mib_dot1dStpPriority;
	UINT16 mib_dot1dStpBridgeMaxAge;
	UINT16 mib_dot1dStpBridgeHelloTime;
	UINT16 mib_dot1dStpBridgeForwardDelay;
} mib_dot1dStp_t;

typedef struct mib_priv_dot1dStp_s {
	UINT8 mib_priv_dot1dStpEnable;	/* 1 or 0 */
} mib_priv_dot1dStp_t;

#endif

typedef struct MIB_802DOT11_s {

    /*-----------------------------------------*/

	/* Station Management Attributes */

    /*-----------------------------------------*/

	MIB_STA_CFG StationConfig;	/* station configuration table */

	MIB_WEP_DEFAULT_KEYS WepDefaultKeys[4];	/* wep default keys table */

	MIB_WEP_KEY_MAPPINGS WepKeyMappings;	/* wep key mappings table */

	MIB_PRIVACY_TABLE Privacy;	/* privacy table */

	/* SMT Notification Objects */

#ifdef AP_SW

	MIB_DISASSOC_NOT NoteDisassoc;	/* disassociate notification */

	MIB_DEAUTH_NOT NoteDeauth;	/* deauthentication notification */

	MIB_AUTH_FAIL_NOT NoteAuthFail;	/* authentication fail notification */

#endif

    /*-----------------------------------------*/

	/* MAC Attributes */

    /*-----------------------------------------*/

	MIB_OP_DATA OperationTable;

#ifdef WMM_IMPLEMENTED
	MIB_EDCA_CONFIG EdcaConfigTable[WMM_MAX_TIDS];
#endif

#ifdef AP_SW

	MIB_COUNTERS CountersTable;

	MIB_GROUP_ADDR GroupAddrTable;

    /*-----------------------------------------*/

	/* Resource Type                           */

    /*-----------------------------------------*/

	MIB_RESOURCE_INFO ResourceInfo;

    /*-----------------------------------------*/

	/* PHY Attributes                          */

    /*-----------------------------------------*/

	MIB_PHY_OP_TABLE PhyOpTable;

	MIB_PHY_TX_POWER_TABLE PhyPowerTable;

	MIB_PHY_FHSS_TABLE PhyFHSSTable;

	MIB_PHY_IR_TABLE PhyIRTable;

	MIB_PHY_REG_DOMAINS_SUPPPORTED PhyRegDomainsSupp;

	MIB_PHY_ANT_LIST AntennasListTable;

#endif

	MIB_PHY_ANT_TABLE PhyAntTable;

	MIB_PHY_DSSS_TABLE PhyDSSSTable;

	MIB_PHY_SUPP_DATA_RATES_TX SuppDataRatesTx[IEEEtypes_MAX_DATA_RATES_G];

	MIB_PHY_SUPP_DATA_RATES_RX SuppDataRatesRx;

#if defined(AP_SW)
	MIB_RSNCONFIG RSNConfig;
#endif

	//MIB_RSNCONFIG_UNICAST_CIPHERS UnicastCiphers;

	//MIB_RSNCONFIG_AUTH_SUITES  RSNConfigAuthSuites;

#ifdef AP_WPA2

	MIB_RSNCONFIGWPA2 RSNConfigWPA2;

	MIB_RSNCONFIGWPA2_UNICAST_CIPHERS WPA2UnicastCiphers;

	MIB_RSNCONFIGWPA2_UNICAST_CIPHERS WPA2UnicastCiphers2;

	MIB_RSNCONFIGWPA2_AUTH_SUITES WPA2AuthSuites;

#endif

#ifdef BURST_MODE

	MIB_BURST_MODE BurstMode;

#endif

	MIB_PHY_ANT_SELECT PhyAntSelect;

#ifdef WEP_RSN_STATS_MIB
	MIB_RSNSTATS RSNStats;
#endif

} MIB_802DOT11;

extern BOOLEAN mib_InitSta(MIB_802DOT11 *mib);

extern BOOLEAN mib_InitAp(MIB_802DOT11 *mib);

#endif /* _WL_MIB_H_ */
