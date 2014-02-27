/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/include/nic/nic.h#1 $
*/

/*! \file   "nic.h"
    \brief  The declaration of nic functions

    Detail description.
*/

/*******************************************************************************
* Copyright (c) 2007 MediaTek Inc.
*
* All rights reserved. Copying, compilation, modification, distribution
* or any other use whatsoever of this material is strictly prohibited
* except in accordance with a Software License Agreement with
* MediaTek Inc.
********************************************************************************
*/

/*******************************************************************************
* LEGAL DISCLAIMER
*
* BY OPENING THIS FILE, BUYER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND
* AGREES THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK
* SOFTWARE") RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE
* PROVIDED TO BUYER ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY
* DISCLAIMS ANY AND ALL WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT
* LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
* PARTICULAR PURPOSE OR NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE
* ANY WARRANTY WHATSOEVER WITH RESPECT TO THE SOFTWARE OF ANY THIRD PARTY
* WHICH MAY BE USED BY, INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK
* SOFTWARE, AND BUYER AGREES TO LOOK ONLY TO SUCH THIRD PARTY FOR ANY
* WARRANTY CLAIM RELATING THERETO. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE
* FOR ANY MEDIATEK SOFTWARE RELEASES MADE TO BUYER'S SPECIFICATION OR TO
* CONFORM TO A PARTICULAR STANDARD OR OPEN FORUM.
*
* BUYER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND CUMULATIVE
* LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL
* BE, AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT
* ISSUE, OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY
* BUYER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
*
* THE TRANSACTION CONTEMPLATED HEREUNDER SHALL BE CONSTRUED IN ACCORDANCE
* WITH THE LAWS OF THE STATE OF CALIFORNIA, USA, EXCLUDING ITS CONFLICT
* OF LAWS PRINCIPLES.  ANY DISPUTES, CONTROVERSIES OR CLAIMS ARISING
* THEREOF AND RELATED THERETO SHALL BE SETTLED BY ARBITRATION IN SAN
* FRANCISCO, CA, UNDER THE RULES OF THE INTERNATIONAL CHAMBER OF COMMERCE
* (ICC).
********************************************************************************
*/

/*
** $Log: nic.h $
 *
 * 11 01 2011 chinglan.wang
 * NULL
 * Modify the Wi-Fi method of the flush TX queue when disconnect the AP.
 * If disconnect the AP and flush all the data frame in the TX queue, WPS cannot do the 4-way handshake to connect to the AP..
 *
 * 07 18 2011 chinghwa.yu
 * [WCXRP00000063] Update BCM CoEx design and settings[WCXRP00000612] [MT6620 Wi-Fi] [FW] CSD update SWRDD algorithm
 * Add CMD/Event for RDD and BWCS.
 *
 * 05 11 2011 cp.wu
 * [WCXRP00000718] [MT6620 Wi-Fi] modify the behavior of setting tx power
 * ACPI APIs migrate to wlan_lib.c for glue layer to invoke.
 *
 * 04 11 2011 yuche.tsai
 * [WCXRP00000627] [Volunteer Patch][MT6620][Driver] Pending MMPUD of P2P Network may crash system issue.
 * Fix kernel panic issue when MMPDU of P2P is pending in driver.
 *
 * 03 02 2011 cp.wu
 * [WCXRP00000503] [MT6620 Wi-Fi][Driver] Take RCPI brought by association response as initial RSSI right after connection is built.
 * use RCPI brought by ASSOC-RESP after connection is built as initial RCPI to avoid using a uninitialized MAC-RX RCPI.
 *
 * 02 01 2011 cm.chang
 * [WCXRP00000415] [MT6620 Wi-Fi][Driver] Check if any memory leakage happens when uninitializing in DGB mode
 * .
 *
 * 01 27 2011 tsaiyuan.hsu
 * [WCXRP00000392] [MT6620 Wi-Fi][Driver] Add Roaming Support
 * add roaming fsm
 * 1. not support 11r, only use strength of signal to determine roaming.
 * 2. not enable CFG_SUPPORT_ROAMING until completion of full test.
 * 3. in 6620, adopt work-around to avoid sign extension problem of cck of hw
 * 4. assume that change of link quality in smooth way.
 *
 * 10 26 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check[WCXRP00000137] [MT6620 Wi-Fi] [FW] Support NIC capability query command
 * 1) update NVRAM content template to ver 1.02
 * 2) add compile option for querying NIC capability (default: off)
 * 3) modify AIS 5GHz support to run-time option, which could be turned on by registry or NVRAM setting
 * 4) correct auto-rate compiler error under linux (treat warning as error)
 * 5) simplify usage of NVRAM and REG_INFO_T
 * 6) add version checking between driver and firmware
 *
 * 10 26 2010 eddie.chen
 * [WCXRP00000134] [MT6620 Wi-Fi][Driver] Add a registry to enable auto rate for SQA test by using E1 EVB
 * Add auto rate parameter in registry.
 *
 * 10 12 2010 cp.wu
 * [WCXRP00000084] [MT6620 Wi-Fi][Driver][FW] Add fixed rate support for distance test
 * add HT (802.11n) fixed rate support.
 *
 * 10 08 2010 cp.wu
 * [WCXRP00000084] [MT6620 Wi-Fi][Driver][FW] Add fixed rate support for distance test
 * adding fixed rate support for distance test. (from registry setting)
 *
 * 10 05 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check
 * 1) add NVRAM access API
 * 2) fake scanning result when NVRAM doesn't exist and/or version mismatch. (off by compiler option)
 * 3) add OID implementation for NVRAM read/write service
 *
 * 10 04 2010 cp.wu
 * [WCXRP00000077] [MT6620 Wi-Fi][Driver][FW] Eliminate use of ENUM_NETWORK_TYPE_T and replaced by ENUM_NETWORK_TYPE_INDEX_T only
 * remove ENUM_NETWORK_TYPE_T definitions
 *
 * 09 21 2010 cp.wu
 * [WCXRP00000053] [MT6620 Wi-Fi][Driver] Reset incomplete and might leads to BSOD when entering RF test with AIS associated
 * Do a complete reset with STA-REC null checking for RF test re-entry
 *
 * 09 08 2010 cp.wu
 * NULL
 * use static memory pool for storing IEs of scanning result.
 *
 * 09 01 2010 cp.wu
 * NULL
 * HIFSYS Clock Source Workaround
 *
 * 08 25 2010 george.huang
 * NULL
 * update OID/ registry control path for PM related settings
 *
 * 08 12 2010 cp.wu
 * NULL
 * [AIS-FSM] honor registry setting for adhoc running mode. (A/B/G)
 *
 * 08 03 2010 cp.wu
 * NULL
 * Centralize mgmt/system service procedures into independent calls.
 *
 * 07 28 2010 cp.wu
 * NULL
 * 1) eliminate redundant variable eOPMode in prAdapter->rWlanInfo
 * 2) change nicMediaStateChange() API prototype
 *
 * 07 14 2010 yarco.yang
 *
 * 1. Remove CFG_MQM_MIGRATION
 * 2. Add CMD_UPDATE_WMM_PARMS command
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 06 2010 george.huang
 * [WPD00001556]Basic power managemenet function
 * Update arguments for nicUpdateBeaconIETemplate()
 *
 * 07 06 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * STA-REC is maintained by CNM only.
 *
 * 07 05 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) ignore RSN checking when RSN is not turned on.
 * 2) set STA-REC deactivation callback as NULL
 * 3) add variable initialization API based on PHY configuration
 *
 * 06 30 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * sync. with CMD/EVENT document ver0.07.
 *
 * 06 29 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) sync to. CMD/EVENT document v0.03
 * 2) simplify DTIM period parsing in scan.c only, bss.c no longer parses it again.
 * 3) send command packet to indicate FW-PM after
 *     a) 1st beacon is received after AIS has connected to an AP
 *     b) IBSS-ALONE has been created
 *     c) IBSS-MERGE has occured
 *
 * 06 25 2010 george.huang
 * [WPD00001556]Basic power managemenet function
 * Create beacon update path, with expose bssUpdateBeaconContent()
 *
 * 06 22 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) add command warpper for STA-REC/BSS-INFO sync.
 * 2) enhance command packet sending procedure for non-oid part
 * 3) add command packet definitions for STA-REC/BSS-INFO sync.
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * implement TX_DONE callback path.
 *
 * 06 11 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) migrate assoc.c.
 * 2) add ucTxSeqNum for tracking frames which needs TX-DONE awareness
 * 3) add configuration options for CNM_MEM and RSN modules
 * 4) add data path for management frames
 * 5) eliminate rPacketInfo of MSDU_INFO_T
 *
 * 06 10 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) eliminate CFG_CMD_EVENT_VERSION_0_9
 * 2) when disconnected, indicate nic directly (no event is needed)
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 04 26 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) surpress compiler warning
 * 2) when acqruing LP-own, keep writing WHLPCR whenever OWN is not acquired yet
 *
 * 04 13 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support
 * add framework for BT-over-Wi-Fi support.
 *  *  *  *  *  *  *  *  * 1) prPendingCmdInfo is replaced by queue for multiple handler capability
 *  *  *  *  *  *  *  *  * 2) command sequence number is now increased atomically
 *  *  *  *  *  *  *  *  * 3) private data could be hold and taken use for other purpose
 *
 * 04 12 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add channel frequency <-> number conversion
 *
 * 03 19 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) add ACPI D0/D3 state switching support
 *  *  *  *  * 2) use more formal way to handle interrupt when the status is retrieved from enhanced RX response
 *
 * 03 17 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * always process TX interrupt first then RX interrupt.
 *
 * 02 25 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * correct behavior to prevent duplicated RX handling for RX0_DONE and RX1_DONE
 *
 * 02 23 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add checksum offloading support.
**  \main\maintrunk.MT6620WiFiDriver_Prj\5 2009-10-13 21:58:58 GMT mtk01084
**  update for new HW architecture design
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-04-24 21:12:55 GMT mtk01104
**  Add function prototype nicRestoreSpiDefMode()
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-03-19 18:32:54 GMT mtk01084
**  update for basic power management functions
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-10 20:16:32 GMT mtk01426
**  Init for develop
**
*/

#ifndef _NIC_H
#define _NIC_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/


struct _REG_ENTRY_T {
    UINT_32      u4Offset;
    UINT_32      u4Value;
};

struct _TABLE_ENTRY_T {
    P_REG_ENTRY_T     pu4TablePtr;
    UINT_16      u2Size;
};

/*! INT status to event map */
typedef struct _INT_EVENT_MAP_T {
    UINT_32     u4Int;
    UINT_32     u4Event;
} INT_EVENT_MAP_T, *P_INT_EVENT_MAP_T;


enum ENUM_INT_EVENT_T {
    INT_EVENT_ABNORMAL,
    INT_EVENT_SW_INT,
    INT_EVENT_TX,
    INT_EVENT_RX,
    INT_EVENT_NUM
};

typedef enum _ENUM_IE_UPD_METHOD_T {
    IE_UPD_METHOD_UPDATE_RANDOM,
    IE_UPD_METHOD_UPDATE_ALL,
    IE_UPD_METHOD_DELETE_ALL,
} ENUM_IE_UPD_METHOD_T, *P_ENUM_IE_UPD_METHOD_T;


/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
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


/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
/*----------------------------------------------------------------------------*/
/* Routines in nic.c                                                          */
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicAllocateAdapterMemory (
    IN P_ADAPTER_T prAdapter
    );

VOID
nicReleaseAdapterMemory (
    IN P_ADAPTER_T prAdapter
    );

VOID
nicDisableInterrupt (
    IN P_ADAPTER_T prAdapter
    );

VOID
nicEnableInterrupt (
    IN P_ADAPTER_T prAdapter
    );

WLAN_STATUS
nicProcessIST (
    IN P_ADAPTER_T prAdapter
    );

WLAN_STATUS
nicProcessIST_impl (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4IntStatus
    );

WLAN_STATUS
nicInitializeAdapter (
    IN P_ADAPTER_T prAdapter
    );

VOID
nicMCRInit (
    IN P_ADAPTER_T prAdapter
    );

BOOL
nicVerifyChipID (
    IN P_ADAPTER_T prAdapter
    );


#if CFG_SDIO_INTR_ENHANCE
VOID
nicSDIOInit (
    IN P_ADAPTER_T prAdapter
    );

VOID
nicSDIOReadIntStatus (
    IN P_ADAPTER_T prAdapter,
    OUT PUINT_32   pu4IntStatus
    );
#endif

BOOLEAN
nicpmSetDriverOwn (
    IN P_ADAPTER_T prAdapter
    );

VOID
nicpmSetFWOwn (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN     fgEnableGlobalInt
    );

BOOLEAN
nicpmSetAcpiPowerD0 (
    IN P_ADAPTER_T prAdapter
    );

BOOLEAN
nicpmSetAcpiPowerD3 (
    IN P_ADAPTER_T prAdapter
    );

#if defined(_HIF_SPI)
void
nicRestoreSpiDefMode(
    IN P_ADAPTER_T prAdapter
    );
#endif

VOID
nicProcessSoftwareInterrupt(
    IN P_ADAPTER_T prAdapter
    );

VOID
nicProcessAbnormalInterrupt (
    IN  P_ADAPTER_T prAdapter
    );

VOID
nicPutMailbox (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4MailboxNum,
    IN UINT_32 u4Data);

VOID
nicGetMailbox (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4MailboxNum,
    OUT PUINT_32 pu4Data);

VOID
nicSetSwIntr (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4SwIntrBitmap
    );

P_CMD_INFO_T
nicGetPendingCmdInfo (
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 ucSeqNum
    );

P_MSDU_INFO_T
nicGetPendingTxMsduInfo (
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 ucSeqNum
    );

P_MSDU_INFO_T
nicGetPendingStaMMPDU(
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 ucStaRecIdx
    );

VOID
nicFreePendingTxMsduInfoByNetwork (
    IN P_ADAPTER_T                  prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T    eNetworkType
    );

UINT_8
nicIncreaseCmdSeqNum (
    IN P_ADAPTER_T prAdapter
    );

UINT_8
nicIncreaseTxSeqNum (
    IN P_ADAPTER_T prAdapter
    );

/* Media State Change */
WLAN_STATUS
nicMediaStateChange (
    IN P_ADAPTER_T                  prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T    eNetworkType,
    IN P_EVENT_CONNECTION_STATUS    prConnectionStatus
    );

/* Utility function for channel number conversion */
UINT_32
nicChannelNum2Freq (
    IN UINT_32 u4ChannelNum
    );

UINT_32
nicFreq2ChannelNum (
    IN UINT_32 u4FreqInKHz
    );

/* firmware command wrapper */
    /* NETWORK (WIFISYS) */
WLAN_STATUS
nicActivateNetwork(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetworkTypeIdx
    );

WLAN_STATUS
nicDeactivateNetwork(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetworkTypeIdx
    );


    /* BSS-INFO */
WLAN_STATUS
nicUpdateBss(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetworkTypeIdx
    );

    /* BSS-INFO Indication (PM) */
WLAN_STATUS
nicPmIndicateBssCreated(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetworkTypeIdx
    );

WLAN_STATUS
nicPmIndicateBssConnected(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetworkTypeIdx
    );

WLAN_STATUS
nicPmIndicateBssAbort(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetworkTypeIdx
    );

    /* Beacon Template Update */
WLAN_STATUS
nicUpdateBeaconIETemplate (
    IN  P_ADAPTER_T prAdapter,
    IN  ENUM_IE_UPD_METHOD_T eIeUpdMethod,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN UINT_16 u2Capability,
    IN PUINT_8 aucIe,
    IN UINT_16 u2IELen
    );

WLAN_STATUS
nicQmUpdateWmmParms(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetworkTypeIdx
    );

WLAN_STATUS
nicSetAutoTxPower(
    IN P_ADAPTER_T      prAdapter,
    IN P_CMD_AUTO_POWER_PARAM_T   prAutoPwrParam
    );

/*----------------------------------------------------------------------------*/
/* Calibration Control                                                        */
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicUpdateTxPower(
    IN P_ADAPTER_T      prAdapter,
    IN P_CMD_TX_PWR_T   prTxPwrParam
    );

WLAN_STATUS
nicUpdate5GOffset(
    IN P_ADAPTER_T              prAdapter,
    IN P_CMD_5G_PWR_OFFSET_T    pr5GPwrOffset
    );

WLAN_STATUS
nicUpdateDPD(
    IN P_ADAPTER_T          prAdapter,
    IN P_CMD_PWR_PARAM_T    prDpdCalResult
    );


/*----------------------------------------------------------------------------*/
/* PHY configuration                                                          */
/*----------------------------------------------------------------------------*/
VOID
nicSetAvailablePhyTypeSet (
    IN P_ADAPTER_T prAdapter
    );

/*----------------------------------------------------------------------------*/
/* MGMT and System Service Control                                            */
/*----------------------------------------------------------------------------*/
VOID
nicInitSystemService (
    IN P_ADAPTER_T prAdapter
    );

VOID
nicResetSystemService (
    IN P_ADAPTER_T prAdapter
    );

VOID
nicUninitSystemService (
    IN P_ADAPTER_T prAdapter
    );

VOID
nicInitMGMT (
    IN P_ADAPTER_T prAdapter,
    IN P_REG_INFO_T prRegInfo
    );

VOID
nicUninitMGMT (
    IN P_ADAPTER_T prAdapter
    );

WLAN_STATUS
nicConfigPowerSaveProfile (
    IN  P_ADAPTER_T prAdapter,
    ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    PARAM_POWER_MODE ePwrMode,
    BOOLEAN fgEnCmdEvent
    );

/*----------------------------------------------------------------------------*/
/* Scan Result Processing                                                     */
/*----------------------------------------------------------------------------*/
VOID
nicAddScanResult (
    IN P_ADAPTER_T                  prAdapter,
    IN PARAM_MAC_ADDRESS            rMacAddr,
    IN P_PARAM_SSID_T               prSsid,
    IN UINT_32                      u4Privacy,
    IN PARAM_RSSI                   rRssi,
    IN ENUM_PARAM_NETWORK_TYPE_T    eNetworkType,
    IN P_PARAM_802_11_CONFIG_T      prConfiguration,
    IN ENUM_PARAM_OP_MODE_T         eOpMode,
    IN PARAM_RATES_EX               rSupportedRates,
    IN UINT_16                      u2IELength,
    IN PUINT_8                      pucIEBuf
    );

VOID
nicFreeScanResultIE (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_32      u4Idx
    );

#if (MT6620_E1_ASIC_HIFSYS_WORKAROUND == 1)
/*----------------------------------------------------------------------------*/
/* Workaround Control                                                         */
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicEnableClockGating (
    IN P_ADAPTER_T prAdapter
    );

WLAN_STATUS
nicDisableClockGating (
    IN P_ADAPTER_T prAdapter
    );
#endif


/*----------------------------------------------------------------------------*/
/* Fixed Rate Hacking                                                         */
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicUpdateRateParams (
    IN P_ADAPTER_T                  prAdapter,
    IN ENUM_REGISTRY_FIXED_RATE_T   eRateSetting,
    IN PUINT_8                      pucDesiredPhyTypeSet,
    IN PUINT_16                     pu2DesiredNonHTRateSet,
    IN PUINT_16                     pu2BSSBasicRateSet,
    IN PUINT_8                      pucMcsSet,
    IN PUINT_8                      pucSupMcs32,
    IN PUINT_16                     u2HtCapInfo
    );

/*----------------------------------------------------------------------------*/
/* Write registers                                                            */
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicWriteMcr (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_32   u4Address,
    IN  UINT_32  u4Value
    );

/*----------------------------------------------------------------------------*/
/* Update auto rate                                                           */
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicRlmArUpdateParms(
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4ArSysParam0,
    IN UINT_32 u4ArSysParam1,
    IN UINT_32 u4ArSysParam2,
    IN UINT_32 u4ArSysParam3
    );

/*----------------------------------------------------------------------------*/
/* Enable/Disable Roaming                                                     */
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicRoamingUpdateParams(
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4EnableRoaming
    );


VOID
nicPrintFirmwareAssertInfo(
    IN P_ADAPTER_T prAdapter
    );

/*----------------------------------------------------------------------------*/
/* Link Quality Updating                                                      */
/*----------------------------------------------------------------------------*/
VOID
nicUpdateLinkQuality(
    IN P_ADAPTER_T                  prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T    eNetTypeIdx,
    IN P_EVENT_LINK_QUALITY         prEventLinkQuality
    );

VOID
nicUpdateRSSI(
    IN P_ADAPTER_T                  prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T    eNetTypeIdx,
    IN INT_8                        cRssi,
    IN INT_8                        cLinkQuality
    );

VOID
nicUpdateLinkSpeed(
    IN P_ADAPTER_T                  prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T    eNetTypeIdx,
    IN UINT_16                      u2LinkSpeed
    );

#if CFG_SUPPORT_RDD_TEST_MODE
WLAN_STATUS
nicUpdateRddTestMode(
    IN P_ADAPTER_T      prAdapter,
    IN P_CMD_RDD_CH_T   prRddChParam
    );
#endif

#endif /* _NIC_H */

