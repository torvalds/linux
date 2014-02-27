/*
** $Id: @(#) bss.h
*/

/*! \file   "bss.h"
    \brief  In this file we define the function prototype used in BSS/IBSS.

    The file contains the function declarations and defines for used in BSS/IBSS.
*/



/*
** $Log: bss.h $
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Let netdev bring up.
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Compile no error before trial run.
 *
 * 03 02 2012 terry.wu
 * NULL
 * Sync CFG80211 modification from branch 2,2.
 *
 * 09 14 2011 yuche.tsai
 * NULL
 * Add P2P IE in assoc response.
 *
 * 03 19 2011 yuche.tsai
 * [WCXRP00000581] [Volunteer Patch][MT6620][Driver] P2P IE in Assoc Req Issue
 * Make assoc req to append P2P IE if wifi direct is enabled.
 *
 * 03 02 2011 wh.su
 * [WCXRP00000448] [MT6620 Wi-Fi][Driver] Fixed WSC IE not send out at probe request
 * Add code to send beacon and probe response WSC IE at Auto GO.
 *
 * 02 23 2011 eddie.chen
 * [WCXRP00000463] [MT6620 Wi-Fi][FW/Driver][Hotspot] Cannot update WMM PS STA's partital bitmap
 * Fix parsing WMM INFO and bmp delivery bitmap definition.
 *
 * 01 31 2011 george.huang
 * [WCXRP00000333] [MT5931][FW] support SRAM power control drivers
 * Extend TIM PVB, from 2 to 3 octets.
 *
 * 11 29 2010 cp.wu
 * [WCXRP00000210] [MT6620 Wi-Fi][Driver][FW] Set RCPI value in STA_REC for initial TX rate selection of auto-rate algorithm
 * update ucRcpi of STA_RECORD_T for AIS when
 * 1) Beacons for IBSS merge is received
 * 2) Associate Response for a connecting peer is received
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 08 12 2010 kevin.huang
 * NULL
 * Update bssProcessProbeRequest() and bssSendBeaconProbeResponse() declarations
 *
 * 07 19 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * when IBSS is being merged-in, send command packet to PM for connected indication
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 25 2010 george.huang
 * [WPD00001556]Basic power managemenet function
 * Create beacon update path, with expose bssUpdateBeaconContent()
 *
 * 06 17 2010 yuche.tsai
 * [WPD00003839][MT6620 5931][P2P] Feature migration
 * Add CTRL FLAGS for Probe Response.
 *
 * 06 09 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add necessary changes to driver data paths.
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add aa_fsm.h, ais_fsm.h, bss.h, mib.h and scan.h.
 *
 * 06 04 2010 george.huang
 * [BORA00000678][MT6620]WiFi LP integration
 * [PM] Support U-APSD for STA mode
 *
 * 05 28 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Add ClientList handling API - bssClearClientList, bssAddStaRecToClientList
 *
 * 05 14 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Remove unused typedef.
 *
 * 05 12 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Fix file merge error
 *
 * 05 12 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Add Power Management - Legacy PS-POLL support.
 *
 * 04 19 2010 kevin.huang
 * [BORA00000714][WIFISYS][New Feature]Beacon Timeout Support
 * Add Beacon Timeout Support
 *  *  *  and will send Null frame to diagnose connection
 *
 * 03 16 2010 kevin.huang
 * [BORA00000663][WIFISYS][New Feature] AdHoc Mode Support
 * Add AdHoc Mode
 *
 * 02 23 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Add DTIM count update while TX Beacon
 *
 * 02 04 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Add AAA Module Support, Revise Net Type to Net Type Index for array lookup
*/

#ifndef _BSS_H
#define _BSS_H

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
//NOTE(Kevin): change define for george
//#define MAX_LEN_TIM_PARTIAL_BMP     (((MAX_ASSOC_ID + 1) + 7) / 8)   /* Required bits = (MAX_ASSOC_ID + 1) */
#define MAX_LEN_TIM_PARTIAL_BMP                     ((CFG_STA_REC_NUM + 7) / 8)  /* reserve length greater than maximum size of STA_REC */ //obsoleted: Assume we only use AID:1~15

/* CTRL FLAGS for Probe Response */
#define BSS_PROBE_RESP_USE_P2P_DEV_ADDR             BIT(0)
#define BSS_PROBE_RESP_INCLUDE_P2P_IE               BIT(1)

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

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
#define bssAssignAssocID(_prStaRec)      ((_prStaRec)->ucIndex + 1)

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
/*----------------------------------------------------------------------------*/
/* Routines for all Operation Modes                                           */
/*----------------------------------------------------------------------------*/
P_STA_RECORD_T
bssCreateStaRecFromBssDesc (
    IN P_ADAPTER_T                  prAdapter,
    IN ENUM_STA_TYPE_T              eStaType,
    IN ENUM_NETWORK_TYPE_INDEX_T    eNetTypeIndex,
    IN P_BSS_DESC_T                 prBssDesc
    );

VOID
bssComposeNullFrame (
    IN P_ADAPTER_T      prAdapter,
    IN PUINT_8          pucBuffer,
    IN P_STA_RECORD_T   prStaRec
    );

VOID
bssComposeQoSNullFrame (
    IN P_ADAPTER_T      prAdapter,
    IN PUINT_8          pucBuffer,
    IN P_STA_RECORD_T   prStaRec,
    IN UINT_8           ucUP,
    IN BOOLEAN          fgSetEOSP
    );

WLAN_STATUS
bssSendNullFrame (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prStaRec,
    IN PFN_TX_DONE_HANDLER  pfTxDoneHandler
    );

WLAN_STATUS
bssSendQoSNullFrame (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prStaRec,
    IN UINT_8               ucUP,
    IN PFN_TX_DONE_HANDLER  pfTxDoneHandler
    );


/*----------------------------------------------------------------------------*/
/* Routines for both IBSS(AdHoc) and BSS(AP)                                  */
/*----------------------------------------------------------------------------*/
VOID
bssGenerateExtSuppRate_IE (
    IN P_ADAPTER_T      prAdapter,
    IN P_MSDU_INFO_T    prMsduInfo
    );

VOID
bssBuildBeaconProbeRespFrameCommonIEs (
    IN P_MSDU_INFO_T    prMsduInfo,
    IN P_BSS_INFO_T     prBssInfo,
    IN PUINT_8          pucDestAddr
    );

VOID
bssComposeBeaconProbeRespFrameHeaderAndFF (
    IN PUINT_8      pucBuffer,
    IN PUINT_8      pucDestAddr,
    IN PUINT_8      pucOwnMACAddress,
    IN PUINT_8      pucBSSID,
    IN UINT_16      u2BeaconInterval,
    IN UINT_16      u2CapInfo
    );

WLAN_STATUS
bssSendBeaconProbeResponse (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN PUINT_8 pucDestAddr,
    IN UINT_32 u4ControlFlags
    );

WLAN_STATUS
bssProcessProbeRequest (
    IN P_ADAPTER_T  prAdapter,
    IN P_SW_RFB_T   prSwRfb
    );

VOID
bssClearClientList (
    IN P_ADAPTER_T  prAdapter,
    IN P_BSS_INFO_T prBssInfo
    );

VOID
bssAddStaRecToClientList (
    IN P_ADAPTER_T  prAdapter,
    IN P_BSS_INFO_T prBssInfo,
    IN P_STA_RECORD_T prStaRec
    );

VOID
bssRemoveStaRecFromClientList (
    IN P_ADAPTER_T  prAdapter,
    IN P_BSS_INFO_T prBssInfo,
    IN P_STA_RECORD_T prStaRec
    );


/*----------------------------------------------------------------------------*/
/* Routines for IBSS(AdHoc) only                                              */
/*----------------------------------------------------------------------------*/
VOID
ibssProcessMatchedBeacon (
    IN P_ADAPTER_T  prAdapter,
    IN P_BSS_INFO_T prBssInfo,
    IN P_BSS_DESC_T prBssDesc,
    IN UINT_8       ucRCPI
    );

WLAN_STATUS
ibssCheckCapabilityForAdHocMode (
    IN P_ADAPTER_T  prAdapter,
    IN P_BSS_DESC_T prBssDesc
    );

VOID
ibssInitForAdHoc (
    IN P_ADAPTER_T  prAdapter,
    IN P_BSS_INFO_T prBssInfo
    );

WLAN_STATUS
bssUpdateBeaconContent (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex
    );


/*----------------------------------------------------------------------------*/
/* Routines for BSS(AP) only                                                  */
/*----------------------------------------------------------------------------*/
VOID
bssInitForAP (
    IN P_ADAPTER_T  prAdapter,
    IN P_BSS_INFO_T prBssInfo,
    IN BOOLEAN fgIsRateUpdate
    );

VOID
bssUpdateDTIMCount (
    IN P_ADAPTER_T  prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex
    );

VOID
bssSetTIMBitmap (
    IN P_ADAPTER_T  prAdapter,
    IN P_BSS_INFO_T prBssInfo,
    IN UINT_16 u2AssocId
    );


/*link function to p2p module for txBcnIETable*/

/* WMM-2.2.2 WMM ACI to AC coding */
typedef enum _ENUM_ACI_T {
    ACI_BE = 0,
    ACI_BK = 1,
    ACI_VI = 2,
    ACI_VO = 3,
    ACI_NUM
} ENUM_ACI_T, *P_ENUM_ACI_T;

typedef enum _ENUM_AC_PRIORITY_T {
    AC_BK_PRIORITY = 0,
    AC_BE_PRIORITY,
    AC_VI_PRIORITY,
    AC_VO_PRIORITY
} ENUM_AC_PRIORITY_T, *P_ENUM_AC_PRIORITY_T;


#endif /* _BSS_H */

