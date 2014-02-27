/*
** $Id: @(#)
*/

/*! \file   "scan.h"
    \brief

*/



/*
** $Log: scan.h $
 *
 * 01 16 2012 cp.wu
 * [MT6620 Wi-Fi][Driver] API and behavior modification for preferred band configuration with corresponding network configuration 
 * add wlanSetPreferBandByNetwork() for glue layer to invoke for setting preferred band configuration corresponding to network type.
 *
 * 08 11 2011 cp.wu
 * [WCXRP00000830] [MT6620 Wi-Fi][Firmware] Use MDRDY counter to detect empty channel for shortening scan time
 * sparse channel detection:
 * driver: collect sparse channel information with scan-done event

 *
 * 07 18 2011 cp.wu
 * [WCXRP00000858] [MT5931][Driver][Firmware] Add support for scan to search for more than one SSID in a single scanning request
 * add framework in driver domain for supporting new SCAN_REQ_V2 for more than 1 SSID support as well as uProbeDelay in NDIS 6.x driver model
 *
 * 06 27 2011 cp.wu
 * [WCXRP00000815] [MT6620 Wi-Fi][Driver] allow single BSSID with multiple SSID settings to work around some tricky AP which use space character as hidden SSID
 * allow to have a single BSSID with multiple SSID to be presented in scanning result
 *
 * 04 18 2011 terry.wu
 * [WCXRP00000660] [MT6620 Wi-Fi][Driver] Remove flag CFG_WIFI_DIRECT_MOVED
 * Remove flag CFG_WIFI_DIRECT_MOVED.
 *
 * 02 09 2011 wh.su
 * [WCXRP00000433] [MT6620 Wi-Fi][Driver] Remove WAPI structure define for avoid P2P module with structure miss-align pointer issue
 * always pre-allio WAPI related structure for align p2p module.
 *
 * 01 14 2011 yuche.tsai
 * [WCXRP00000352] [Volunteer Patch][MT6620][Driver] P2P Statsion Record Client List Issue
 * Fix compile error.
 *
 * 09 08 2010 cp.wu
 * NULL
 * use static memory pool for storing IEs of scanning result.
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 08 31 2010 kevin.huang
 * NULL
 * Use LINK LIST operation to process SCAN result
 *
 * 08 30 2010 cp.wu
 * NULL
 * eliminate klockwork errors
 *
 * 08 16 2010 cp.wu
 * NULL
 * add interface for RLM to trigger OBSS-SCAN.
 *
 * 08 12 2010 yuche.tsai
 * NULL
 * Add a functio prototype to find p2p descriptor of a bss descriptor directly.
 *
 * 08 11 2010 yuche.tsai
 * NULL
 * Add function prototype for return channel.
 * modify data structure for scan specific device ID or TYPE. (Move from P2P Connection Settings to Scan Param)
 *
 * 08 05 2010 yuche.tsai
 * NULL
 * Check-in P2P Device Discovery Feature.
 *
 * 08 02 2010 yuche.tsai
 * NULL
 * P2P Group Negotiation Code Check in.
 *
 * 07 26 2010 yuche.tsai
 *
 * Add a option for channel time extention in scan abort command.
 *
 * 07 21 2010 yuche.tsai
 *
 * Add for P2P Scan Result Parsing & Saving.
 *
 * 07 19 2010 yuche.tsai
 *
 * Scan status "FIND" is used for P2P FSM find state.
 *
 * 07 19 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * SCN module is now able to handle multiple concurrent scanning requests
 *
 * 07 14 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * pass band with channel number information as scan parameter
 *
 * 07 14 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * remove timer in DRV-SCN.
 *
 * 07 09 2010 cp.wu
 *
 * 1) separate AIS_FSM state for two kinds of scanning. (OID triggered scan, and scan-for-connection)
 * 2) eliminate PRE_BSS_DESC_T, Beacon/PrebResp is now parsed in single pass
 * 3) implment DRV-SCN module, currently only accepts single scan request, other request will be directly dropped by returning BUSY
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 01 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add scan uninitialization procedure
 *
 * 07 01 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * implementation of DRV-SCN and related mailbox message handling.
 *
 * 06 25 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * modify Beacon/ProbeResp to complete parsing,
 * because host software has looser memory usage restriction
 *
 * 06 17 2010 yuche.tsai
 * [WPD00003839][MT6620 5931][P2P] Feature migration
 * Add P2P related field in SCAN_PARAM_T.
 *
 * 06 14 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * saa_fsm.c is migrated.
 *
 * 06 14 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add management dispatching function table.
 *
 * 06 14 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * restore utility function invoking via hem_mbox to direct calls
 *
 * 06 11 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * auth.c is migrated.
 *
 * 06 10 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add buildable & linkable ais_fsm.c
 *
 * related reference are still waiting to be resolved
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add aa_fsm.h, ais_fsm.h, bss.h, mib.h and scan.h.
 *
 * 05 12 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Add Power Management - Legacy PS-POLL support.
 *
 * 04 13 2010 kevin.huang
 * [BORA00000663][WIFISYS][New Feature] AdHoc Mode Support
 *
 * Add new HW CH macro support
 *
 * 03 16 2010 kevin.huang
 * [BORA00000663][WIFISYS][New Feature] AdHoc Mode Support
 * Add AdHoc Mode
 *
 * 03 10 2010 kevin.huang
 * [BORA00000654][WIFISYS][New Feature] CNM Module - Ch Manager Support
 *
 *  *  *  *  *  * Add Channel Manager for arbitration of JOIN and SCAN Req
 *
 * 02 26 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Modify scanBuildProbeReqFrameCommonIEs() to support P2P SCAN
 *
 * 02 23 2010 wh.su
 * [BORA00000592][MT6620 Wi-Fi] Adding the security related code for driver
 * refine the scan procedure, reduce the WPA and WAPI IE parsing, and move the parsing to the time for join.
 *
 * 02 23 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Add support scan channel 1~14 and update scan result's frequency infou1rwduu`wvpghlqg|n`slk+mpdkb
 *
 * 02 04 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Add AAA Module Support, Revise Net Type to Net Type Index for array lookup
 *
 * 01 27 2010 wh.su
 * [BORA00000476][Wi-Fi][firmware] Add the security module initialize code
 * add and fixed some security function.
 *
 * 01 07 2010 kevin.huang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 *
 * Simplify the process of Beacon during SCAN and remove redundant variable in PRE_BSS_DESC_T
 *
 * Dec 7 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * adding variable for wapi ap
 *
 * Dec 4 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * remove non-used secuirty variavle
 *
 * Dec 3 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Refine data structure of BSS_DESC_T and PRE_BSS_DESC_T
 *
 * Nov 24 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add eNetType to rScanParam and revise MGMT Handler with Retain Status
 *
 * Nov 23 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add ucAvailablePhyTypeSet to BSS_DESC_T
 *
 * Nov 20 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add aucSrcAddress to SCAN_PARAM_T for P2P's Device Address
 *
 * Nov 19 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * adding the security related variable
 *
 * Nov 18 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * adding the security ie filed for scan parsing
 *
 * Nov 16 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add scanSearchBssDescByPolicy()
 *
 * Nov 5 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add function declarations of scan_fsm.c
 *
 * Oct 30 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add scan.h to source control
**
*/

#ifndef _SCAN_H
#define _SCAN_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
/*! Maximum buffer size of SCAN list */
#define SCN_MAX_BUFFER_SIZE                 (CFG_MAX_NUM_BSS_LIST * ALIGN_4(sizeof(BSS_DESC_T)))

#define SCN_RM_POLICY_EXCLUDE_CONNECTED     BIT(0) // Remove SCAN result except the connected one.
#define SCN_RM_POLICY_TIMEOUT               BIT(1) // Remove the timeout one
#define SCN_RM_POLICY_OLDEST_HIDDEN         BIT(2) // Remove the oldest one with hidden ssid
#define SCN_RM_POLICY_SMART_WEAKEST         BIT(3) /* If there are more than half BSS which has the
                                                    * same ssid as connection setting, remove the weakest one from them
                                                    * Else remove the weakest one.
                                                    */
#define SCN_RM_POLICY_ENTIRE                BIT(4) // Remove entire SCAN result

#define SCN_BSS_DESC_SAME_SSID_THRESHOLD    3 /* This is used by POLICY SMART WEAKEST,
                                               * If exceed this value, remove weakest BSS_DESC_T
                                               * with same SSID first in large network.
                                               */

#define SCN_BSS_DESC_REMOVE_TIMEOUT_SEC     5 // Second.
                                              /* This is used by POLICY TIMEOUT,
                                               * If exceed this value, remove timeout BSS_DESC_T.
                                               */




#define SCN_PROBE_DELAY_MSEC                0

#define SCN_ADHOC_BSS_DESC_TIMEOUT_SEC      5 // Second.

/*----------------------------------------------------------------------------*/
/* MSG_SCN_SCAN_REQ                                                           */
/*----------------------------------------------------------------------------*/
#define SCAN_REQ_SSID_WILDCARD              BIT(0)
#define SCAN_REQ_SSID_P2P_WILDCARD          BIT(1)
#define SCAN_REQ_SSID_SPECIFIED             BIT(2)


/*----------------------------------------------------------------------------*/
/* Support Multiple SSID SCAN                                                 */
/*----------------------------------------------------------------------------*/
#define SCN_SSID_MAX_NUM                        4


/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
typedef enum _ENUM_SCAN_TYPE_T {
    SCAN_TYPE_PASSIVE_SCAN = 0,
    SCAN_TYPE_ACTIVE_SCAN,
    SCAN_TYPE_NUM
} ENUM_SCAN_TYPE_T, *P_ENUM_SCAN_TYPE_T;

typedef enum _ENUM_SCAN_STATE_T {
    SCAN_STATE_IDLE = 0,
    SCAN_STATE_SCANNING,
    SCAN_STATE_NUM
} ENUM_SCAN_STATE_T;

typedef enum _ENUM_SCAN_CHANNEL_T {
    SCAN_CHANNEL_FULL = 0,
    SCAN_CHANNEL_2G4,
    SCAN_CHANNEL_5G,
    SCAN_CHANNEL_P2P_SOCIAL,
    SCAN_CHANNEL_SPECIFIED,
    SCAN_CHANNEL_NUM
} ENUM_SCAN_CHANNEL, *P_ENUM_SCAN_CHANNEL;

typedef struct _MSG_SCN_FSM_T {
    MSG_HDR_T       rMsgHdr;        /* Must be the first member */
    UINT_32         u4Dummy;
} MSG_SCN_FSM_T, *P_MSG_SCN_FSM_T;



/*----------------------------------------------------------------------------*/
/* BSS Descriptors                                                            */
/*----------------------------------------------------------------------------*/
struct _BSS_DESC_T {
    LINK_ENTRY_T            rLinkEntry;

    UINT_8                  aucBSSID[MAC_ADDR_LEN];
    UINT_8                  aucSrcAddr[MAC_ADDR_LEN]; /* For IBSS, the SrcAddr is different from BSSID */

    BOOLEAN                 fgIsConnecting; /* If we are going to connect to this BSS
                                             * (JOIN or ROAMING to another BSS), don't
                                             * remove this record from BSS List.
                                             */
    BOOLEAN                 fgIsConnected; /* If we have connected to this BSS (NORMAL_TR),
                                            * don't removed this record from BSS list.
                                            */

    BOOLEAN                 fgIsHiddenSSID; /* When this flag is TRUE, means the SSID
                                             * of this BSS is not known yet.
                                             */
    UINT_8                  ucSSIDLen;
    UINT_8                  aucSSID[ELEM_MAX_LEN_SSID];

    OS_SYSTIME              rUpdateTime;

    ENUM_BSS_TYPE_T         eBSSType;

    UINT_16                 u2CapInfo;

    UINT_16                 u2BeaconInterval;
    UINT_16                 u2ATIMWindow;

    UINT_16                 u2OperationalRateSet;
    UINT_16                 u2BSSBasicRateSet;
    BOOLEAN                 fgIsUnknownBssBasicRate;

    BOOLEAN                 fgIsERPPresent;
    BOOLEAN                 fgIsHTPresent;

    UINT_8                  ucPhyTypeSet;           /* Available PHY Type Set of this BSS */

    UINT_8                  ucChannelNum;

    ENUM_CHNL_EXT_T         eSco;                   /* Record bandwidth for association process
                                                       Some AP will send association resp by 40MHz BW */
    ENUM_BAND_T             eBand;

    UINT_8                  ucDTIMPeriod;

    BOOLEAN                 fgIsLargerTSF; /* This BSS's TimeStamp is larger than us(TCL == 1 in RX_STATUS_T) */

    UINT_8                  ucRCPI;

    UINT_8                  ucWmmFlag; /* A flag to indicate this BSS's WMM capability */

    /*! \brief The srbiter Search State will matched the scan result,
               and saved the selected cipher and akm, and report the score,
               for arbiter join state, join module will carry this target BSS
               to rsn generate ie function, for gen wpa/rsn ie */
    UINT_32                 u4RsnSelectedGroupCipher;
    UINT_32                 u4RsnSelectedPairwiseCipher;
    UINT_32                 u4RsnSelectedAKMSuite;

    UINT_16                 u2RsnCap;

    RSN_INFO_T              rRSNInfo;
    RSN_INFO_T              rWPAInfo;
#if 1//CFG_SUPPORT_WAPI
    WAPI_INFO_T             rIEWAPI;
    BOOL                    fgIEWAPI;
#endif
    BOOL                    fgIERSN;
    BOOL                    fgIEWPA;

    /*! \brief RSN parameters selected for connection */
    /*! \brief The Select score for final AP selection,
               0, no sec, 1,2,3 group cipher is WEP, TKIP, CCMP */
    UINT_8                  ucEncLevel;

#if CFG_ENABLE_WIFI_DIRECT
    BOOLEAN               fgIsP2PPresent;
    P_P2P_DEVICE_DESC_T  prP2pDesc;

    UINT_8                  aucIntendIfAddr[MAC_ADDR_LEN]; /* For IBSS, the SrcAddr is different from BSSID */
//    UINT_8 ucDevCapabilityBitmap;  /* Device Capability Attribute. (P2P_DEV_CAPABILITY_XXXX) */
//    UINT_8 ucGroupCapabilityBitmap;  /* Group Capability Attribute. (P2P_GROUP_CAPABILITY_XXXX) */

    LINK_T rP2pDeviceList;

//    P_LINK_T prP2pDeviceList;

    /* For
      *    1. P2P Capability.
      *    2. P2P Device ID. ( in aucSrcAddr[] )
      *    3. NOA   (TODO:)
      *    4. Extend Listen Timing. (Probe Rsp)  (TODO:)
      *    5. P2P Device Info. (Probe Rsp)
      *    6. P2P Group Info. (Probe Rsp)
      */
#endif

    BOOLEAN                 fgIsIEOverflow; /* The received IE length exceed the maximum IE buffer size */
    UINT_16                 u2IELength; /* The byte count of aucIEBuf[] */

    ULARGE_INTEGER          u8TimeStamp; /* Place u8TimeStamp before aucIEBuf[1] to force DW align */

    UINT_8                  aucIEBuf[CFG_IE_BUFFER_SIZE];
};


typedef struct _SCAN_PARAM_T { /* Used by SCAN FSM */
    /* Active or Passive */
    ENUM_SCAN_TYPE_T            eScanType;

    /* Network Type */
    ENUM_NETWORK_TYPE_INDEX_T   eNetTypeIndex;

    /* Specified SSID Type */
    UINT_8                      ucSSIDType;
    UINT_8                      ucSSIDNum;

    /* Length of Specified SSID */
    UINT_8                      ucSpecifiedSSIDLen[SCN_SSID_MAX_NUM];

    /* Specified SSID */
    UINT_8                      aucSpecifiedSSID[SCN_SSID_MAX_NUM][ELEM_MAX_LEN_SSID];

#if CFG_ENABLE_WIFI_DIRECT
    BOOLEAN                     fgFindSpecificDev;                  /* P2P: Discovery Protocol */
    UINT_8                      aucDiscoverDevAddr[MAC_ADDR_LEN];
    BOOLEAN                     fgIsDevType;
    P2P_DEVICE_TYPE_T           rDiscoverDevType;

    UINT_16                     u2PassiveListenInterval;
    // TODO: Find Specific Device Type.
#endif /* CFG_SUPPORT_P2P */

    BOOLEAN                     fgIsObssScan;
    BOOLEAN                     fgIsScanV2;

    /* Run time flags */
    UINT_16                     u2ProbeDelayTime;

    /* channel information */
    ENUM_SCAN_CHANNEL   eScanChannel;
    UINT_8              ucChannelListNum;
    RF_CHANNEL_INFO_T   arChnlInfoList[MAXIMUM_OPERATION_CHANNEL_LIST];

    /* Feedback information */
    UINT_8              ucSeqNum;

    /* Information Element */
    UINT_16             u2IELen;
    UINT_8              aucIE[MAX_IE_LENGTH];

} SCAN_PARAM_T, *P_SCAN_PARAM_T;

typedef struct _SCAN_INFO_T {
    ENUM_SCAN_STATE_T       eCurrentState;  /* Store the STATE variable of SCAN FSM */

    OS_SYSTIME              rLastScanCompletedTime;

    SCAN_PARAM_T            rScanParam;

    UINT_32                 u4NumOfBssDesc;

    UINT_8                  aucScanBuffer[SCN_MAX_BUFFER_SIZE];

    LINK_T                  rBSSDescList;

    LINK_T                  rFreeBSSDescList;

    LINK_T                  rPendingMsgList;

    /* Sparse Channel Detection */
    BOOLEAN                 fgIsSparseChannelValid;
    RF_CHANNEL_INFO_T       rSparseChannel;

} SCAN_INFO_T, *P_SCAN_INFO_T;


/* Incoming Mailbox Messages */
typedef struct _MSG_SCN_SCAN_REQ_T {
    MSG_HDR_T           rMsgHdr;        /* Must be the first member */
    UINT_8              ucSeqNum;
    UINT_8              ucNetTypeIndex;
    ENUM_SCAN_TYPE_T    eScanType;
    UINT_8              ucSSIDType;     /* BIT(0) wildcard / BIT(1) P2P-wildcard / BIT(2) specific */
    UINT_8              ucSSIDLength;
    UINT_8              aucSSID[PARAM_MAX_LEN_SSID];
#if CFG_ENABLE_WIFI_DIRECT
    UINT_16             u2ChannelDwellTime;  /* In TU. 1024us. */
#endif
    ENUM_SCAN_CHANNEL   eScanChannel;
    UINT_8              ucChannelListNum;
    RF_CHANNEL_INFO_T   arChnlInfoList[MAXIMUM_OPERATION_CHANNEL_LIST];
    UINT_16             u2IELen;
    UINT_8              aucIE[MAX_IE_LENGTH];
} MSG_SCN_SCAN_REQ, *P_MSG_SCN_SCAN_REQ;

typedef struct _MSG_SCN_SCAN_REQ_V2_T {
    MSG_HDR_T           rMsgHdr;        /* Must be the first member */
    UINT_8              ucSeqNum;
    UINT_8              ucNetTypeIndex;
    ENUM_SCAN_TYPE_T    eScanType;
    UINT_8              ucSSIDType;     /* BIT(0) wildcard / BIT(1) P2P-wildcard / BIT(2) specific */
    UINT_8              ucSSIDNum;
    P_PARAM_SSID_T      prSsid;
    UINT_16             u2ProbeDelay;
    UINT_16             u2ChannelDwellTime;  /* In TU. 1024us. */
    ENUM_SCAN_CHANNEL   eScanChannel;
    UINT_8              ucChannelListNum;
    RF_CHANNEL_INFO_T   arChnlInfoList[MAXIMUM_OPERATION_CHANNEL_LIST];
    UINT_16             u2IELen;
    UINT_8              aucIE[MAX_IE_LENGTH];
} MSG_SCN_SCAN_REQ_V2, *P_MSG_SCN_SCAN_REQ_V2;


typedef struct _MSG_SCN_SCAN_CANCEL_T {
    MSG_HDR_T           rMsgHdr;        /* Must be the first member */
    UINT_8              ucSeqNum;
    UINT_8              ucNetTypeIndex;
#if CFG_ENABLE_WIFI_DIRECT
    BOOLEAN           fgIsChannelExt;
#endif
} MSG_SCN_SCAN_CANCEL, *P_MSG_SCN_SCAN_CANCEL;

/* Outgoing Mailbox Messages */
typedef enum _ENUM_SCAN_STATUS_T {
    SCAN_STATUS_DONE = 0,
    SCAN_STATUS_CANCELLED,
    SCAN_STATUS_FAIL,
    SCAN_STATUS_BUSY,
    SCAN_STATUS_NUM
} ENUM_SCAN_STATUS, *P_ENUM_SCAN_STATUS;

typedef struct _MSG_SCN_SCAN_DONE_T {
    MSG_HDR_T           rMsgHdr;        /* Must be the first member */
    UINT_8              ucSeqNum;
    UINT_8              ucNetTypeIndex;
    ENUM_SCAN_STATUS    eScanStatus;
} MSG_SCN_SCAN_DONE, *P_MSG_SCN_SCAN_DONE;

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
/*----------------------------------------------------------------------------*/
/* Routines in scan.c                                                         */
/*----------------------------------------------------------------------------*/
VOID
scnInit (
    IN P_ADAPTER_T prAdapter
    );

VOID
scnUninit (
    IN P_ADAPTER_T prAdapter
    );

/* BSS-DESC Search */
P_BSS_DESC_T
scanSearchBssDescByBssid (
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 aucBSSID[]
    );

P_BSS_DESC_T
scanSearchBssDescByBssidAndSsid (
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 aucBSSID[],
    IN BOOLEAN fgCheckSsid,
    IN P_PARAM_SSID_T prSsid
    );

P_BSS_DESC_T
scanSearchBssDescByTA (
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 aucSrcAddr[]
    );

P_BSS_DESC_T
scanSearchBssDescByTAAndSsid (
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 aucSrcAddr[],
    IN BOOLEAN fgCheckSsid,
    IN P_PARAM_SSID_T prSsid
    );


/* BSS-DESC Search - Alternative */
P_BSS_DESC_T
scanSearchExistingBssDesc (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_BSS_TYPE_T eBSSType,
    IN UINT_8 aucBSSID[],
    IN UINT_8 aucSrcAddr[]
    );

P_BSS_DESC_T
scanSearchExistingBssDescWithSsid (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_BSS_TYPE_T eBSSType,
    IN UINT_8 aucBSSID[],
    IN UINT_8 aucSrcAddr[],
    IN BOOLEAN fgCheckSsid,
    IN P_PARAM_SSID_T prSsid
    );


/* BSS-DESC Allocation */
P_BSS_DESC_T
scanAllocateBssDesc (
    IN P_ADAPTER_T prAdapter
    );

/* BSS-DESC Removal */
VOID
scanRemoveBssDescsByPolicy (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4RemovePolicy
    );

VOID
scanRemoveBssDescByBssid (
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 aucBSSID[]
    );

VOID
scanRemoveBssDescByBandAndNetwork (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_BAND_T eBand,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex
    );

/* BSS-DESC State Change */
VOID
scanRemoveConnFlagOfBssDescByBssid (
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 aucBSSID[]
    );

#if 0
/* BSS-DESC Insertion */
P_BSS_DESC_T
scanAddToInternalScanResult (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSWRfb,
    IN P_BSS_DESC_T prBssDesc
    );
#endif

/* BSS-DESC Insertion - ALTERNATIVE */
P_BSS_DESC_T
scanAddToBssDesc (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb
    );

WLAN_STATUS
scanProcessBeaconAndProbeResp (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSWRfb
    );

VOID
scanBuildProbeReqFrameCommonIEs (
    IN P_MSDU_INFO_T    prMsduInfo,
    IN PUINT_8          pucDesiredSsid,
    IN UINT_32          u4DesiredSsidLen,
    IN UINT_16          u2SupportedRateSet
    );

WLAN_STATUS
scanSendProbeReqFrames (
    IN P_ADAPTER_T prAdapter,
    IN P_SCAN_PARAM_T prScanParam
    );

VOID
scanUpdateBssDescForSearch (
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_DESC_T prBssDesc
    );

P_BSS_DESC_T
scanSearchBssDescByPolicy (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex
    );

WLAN_STATUS
scanAddScanResult (
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_DESC_T prBssDesc,
    IN P_SW_RFB_T prSwRfb
    );

/*----------------------------------------------------------------------------*/
/* Routines in scan_fsm.c                                                     */
/*----------------------------------------------------------------------------*/
VOID
scnFsmSteps (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_SCAN_STATE_T eNextState
    );

/*----------------------------------------------------------------------------*/
/* Command Routines                                                           */
/*----------------------------------------------------------------------------*/
VOID
scnSendScanReq (
    IN P_ADAPTER_T prAdapter
    );

VOID
scnSendScanReqV2 (
    IN P_ADAPTER_T prAdapter
    );

/*----------------------------------------------------------------------------*/
/* RX Event Handling                                                          */
/*----------------------------------------------------------------------------*/
VOID
scnEventScanDone(
    IN P_ADAPTER_T          prAdapter,
    IN P_EVENT_SCAN_DONE    prScanDone
    );

/*----------------------------------------------------------------------------*/
/* Mailbox Message Handling                                                   */
/*----------------------------------------------------------------------------*/
VOID
scnFsmMsgStart (
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
scnFsmMsgAbort (
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
scnFsmHandleScanMsg (
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_SCN_SCAN_REQ prScanReqMsg
    );

VOID
scnFsmHandleScanMsgV2 (
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_SCN_SCAN_REQ_V2 prScanReqMsg
    );

VOID
scnFsmRemovePendingMsg (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_8       ucSeqNum,
    IN UINT_8       ucNetTypeIndex
    );

/*----------------------------------------------------------------------------*/
/* Mailbox Message Generation                                                 */
/*----------------------------------------------------------------------------*/
VOID
scnFsmGenerateScanDoneMsg (
    IN P_ADAPTER_T          prAdapter,
    IN UINT_8               ucSeqNum,
    IN UINT_8               ucNetTypeIndex,
    IN ENUM_SCAN_STATUS     eScanStatus
    );

/*----------------------------------------------------------------------------*/
/* Query for sparse channel                                                   */
/*----------------------------------------------------------------------------*/
BOOLEAN
scnQuerySparseChannel (
    IN P_ADAPTER_T      prAdapter,
    P_ENUM_BAND_T       prSparseBand,
    PUINT_8             pucSparseChannel
    );


#endif /* _SCAN_H */


