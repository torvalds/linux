/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/os/linux/gl_wext_priv.c#4 $
*/

/*! \file gl_wext_priv.c
    \brief This file includes private ioctl support.
*/



/*
** $Log: gl_wext_priv.c $
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Let netdev bring up.
 *
 * 06 13 2012 yuche.tsai
 * NULL
 * Update maintrunk driver.
 * Add support for driver compose assoc request frame.
 *
 * 03 20 2012 wh.su
 * [WCXRP00001153] [MT6620 Wi-Fi][Driver] Adding the get_ch_list and set_tx_power proto type function[WCXRP00001202] [MT6628 Wi-Fi][FW] Adding the New PN init code
 * use return to avoid the ioctl return not supported
 *
 * 03 02 2012 terry.wu
 * NULL
 * Snc CFG80211 modification for ICS migration from branch 2.2.
 *
 * 01 16 2012 wh.su
 * [WCXRP00001170] [MT6620 Wi-Fi][Driver] Adding the related code for set/get band ioctl
 * Adding the template code for set / get band IOCTL (with ICS supplicant_6)..
 *
 * 01 05 2012 wh.su
 * [WCXRP00001153] [MT6620 Wi-Fi][Driver] Adding the get_ch_list and set_tx_power proto type function
 * Adding the related ioctl / wlan oid function to set the Tx power cfg.
 *
 * 01 02 2012 wh.su
 * [WCXRP00001153] [MT6620 Wi-Fi][Driver] Adding the get_ch_list and set_tx_power proto type function
 * Adding the proto type function for set_int set_tx_power and get int get_ch_list.
 *
 * 11 10 2011 cp.wu
 * [WCXRP00001098] [MT6620 Wi-Fi][Driver] Replace printk by DBG LOG macros in linux porting layer
 * 1. eliminaite direct calls to printk in porting layer.
 * 2. replaced by DBGLOG, which would be XLOG on ALPS platforms.
 *
 * 11 02 2011 chinghwa.yu
 * [WCXRP00000063] Update BCM CoEx design and settings
 * Fixed typo.
 *
 * 09 20 2011 chinglan.wang
 * [WCXRP00000989] [WiFi Direct] [Driver] Add a new io control API to start the formation for the sigma test.
 * .
 *
 * 07 28 2011 chinghwa.yu
 * [WCXRP00000063] Update BCM CoEx design and settings
 * Add BWCS cmd and event.
 *
 * 07 18 2011 chinghwa.yu
 * [WCXRP00000063] Update BCM CoEx design and settings[WCXRP00000612] [MT6620 Wi-Fi] [FW] CSD update SWRDD algorithm
 * Add CMD/Event for RDD and BWCS.
 *
 * 03 17 2011 chinglan.wang
 * [WCXRP00000570] [MT6620 Wi-Fi][Driver] Add Wi-Fi Protected Setup v2.0 feature
 * .
 *
 * 03 07 2011 terry.wu
 * [WCXRP00000521] [MT6620 Wi-Fi][Driver] Remove non-standard debug message
 * Toggle non-standard debug messages to comments.
 *
 * 01 27 2011 cm.chang
 * [WCXRP00000402] [MT6620 Wi-Fi][Driver] Enable MCR read/write by iwpriv by default
 * .
 *
 * 01 26 2011 wh.su
 * [WCXRP00000396] [MT6620 Wi-Fi][Driver] Support Sw Ctrl ioctl at linux
 * adding the SW cmd ioctl support, use set/get structure ioctl.
 *
 * 01 20 2011 eddie.chen
 * [WCXRP00000374] [MT6620 Wi-Fi][DRV] SW debug control
 * Adjust OID order.
 *
 * 01 20 2011 eddie.chen
 * [WCXRP00000374] [MT6620 Wi-Fi][DRV] SW debug control
 * Add Oid for sw control debug command
 *
 * 01 07 2011 cm.chang
 * [WCXRP00000336] [MT6620 Wi-Fi][Driver] Add test mode commands in normal phone operation
 * Add a new compiling option to control if MCR read/write is permitted
 *
 * 12 31 2010 cm.chang
 * [WCXRP00000336] [MT6620 Wi-Fi][Driver] Add test mode commands in normal phone operation
 * Add some iwpriv commands to support test mode operation
 *
 * 12 15 2010 george.huang
 * [WCXRP00000152] [MT6620 Wi-Fi] AP mode power saving function
 * Support set PS profile and set WMM-PS related iwpriv.
 *
 * 11 08 2010 wh.su
 * [WCXRP00000171] [MT6620 Wi-Fi][Driver] Add message check code same behavior as mt5921
 * add the message check code from mt5921.
 *
 * 10 18 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check[WCXRP00000086] [MT6620 Wi-Fi][Driver] The mac address is all zero at android
 * complete implementation of Android NVRAM access
 *
 * 09 24 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check
 * correct typo for NVRAM access.
 *
 * 09 23 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check
 * add skeleton for NVRAM integration
 *
 * 08 04 2010 cp.wu
 * NULL
 * revert changelist #15371, efuse read/write access will be done by RF test approach
 *
 * 08 04 2010 cp.wu
 * NULL
 * add OID definitions for EFUSE read/write access.
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 06 01 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * enable OID_CUSTOM_MTK_WIFI_TEST for RFTest & META tool
 *
 * 05 29 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * fix private ioctl for rftest
 *
 * 04 21 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * add for private ioctl support
**  \main\maintrunk.MT5921\32 2009-10-08 10:33:25 GMT mtk01090
**  Avoid accessing private data of net_device directly. Replace with netdev_priv(). Add more checking for input parameters and pointers.
**  \main\maintrunk.MT5921\31 2009-09-29 16:46:21 GMT mtk01090
**  Remove unused functions
**  \main\maintrunk.MT5921\30 2009-09-29 14:46:47 GMT mtk01090
**  Fix compile warning
**  \main\maintrunk.MT5921\29 2009-09-29 14:28:48 GMT mtk01090
**  Fix compile warning
**  \main\maintrunk.MT5921\28 2009-09-28 22:21:38 GMT mtk01090
**  Refine lines to supress compile warning
**  \main\maintrunk.MT5921\27 2009-09-28 20:19:14 GMT mtk01090
**  Add private ioctl to carry OID structures. Restructure public/private ioctl interfaces to Linux kernel.
**  \main\maintrunk.MT5921\26 2009-08-18 22:56:53 GMT mtk01090
**  Add Linux SDIO (with mmc core) support.
**  Add Linux 2.6.21, 2.6.25, 2.6.26.
**  Fix compile warning in Linux.
**  \main\maintrunk.MT5921\25 2009-05-07 22:26:15 GMT mtk01089
**  Add mandatory and private IO control for Linux BWCS
**  \main\maintrunk.MT5921\24 2009-04-29 10:07:05 GMT mtk01088
**  fixed the compiling error at linux
**  \main\maintrunk.MT5921\23 2009-04-24 09:09:45 GMT mtk01088
**  mark the code not used at linux supplicant v0.6.7
**  \main\maintrunk.MT5921\22 2008-11-24 21:03:51 GMT mtk01425
**  1. Add PTA_ENABLED flag
**  \main\maintrunk.MT5921\21 2008-08-29 14:55:59 GMT mtk01088
**  adjust the code for meet the coding style, and add assert check
**  \main\maintrunk.MT5921\20 2008-07-16 15:23:20 GMT mtk01104
**  Support GPIO2 mode
**  \main\maintrunk.MT5921\19 2008-07-15 17:43:11 GMT mtk01084
**  modify variable name
**  \main\maintrunk.MT5921\18 2008-07-14 14:37:58 GMT mtk01104
**  Add exception handle about length in function priv_set_struct()
**  \main\maintrunk.MT5921\17 2008-07-14 13:55:32 GMT mtk01104
**  Support PRIV_CMD_BT_COEXIST
**  \main\maintrunk.MT5921\16 2008-07-09 00:20:15 GMT mtk01461
**  Add priv oid to support WMM_PS_TEST
**  \main\maintrunk.MT5921\15 2008-06-02 11:15:22 GMT mtk01461
**  Update after wlanoidSetPowerMode changed
**  \main\maintrunk.MT5921\14 2008-05-30 19:31:07 GMT mtk01461
**  Add IOCTL for Power Mode
**  \main\maintrunk.MT5921\13 2008-05-30 18:57:15 GMT mtk01461
**  Not use wlanoidSetCSUMOffloadForLinux()
**  \main\maintrunk.MT5921\12 2008-05-30 15:13:18 GMT mtk01084
**  rename wlanoid
**  \main\maintrunk.MT5921\11 2008-05-29 14:16:31 GMT mtk01084
**  rename for wlanoidSetBeaconIntervalForLinux
**  \main\maintrunk.MT5921\10 2008-04-17 23:06:37 GMT mtk01461
**  Add iwpriv support for AdHocMode setting
**  \main\maintrunk.MT5921\9 2008-03-31 21:00:55 GMT mtk01461
**  Add priv IOCTL for VOIP setting
**  \main\maintrunk.MT5921\8 2008-03-31 13:49:43 GMT mtk01461
**  Add priv ioctl to turn on / off roaming
**  \main\maintrunk.MT5921\7 2008-03-26 15:35:14 GMT mtk01461
**  Add CSUM offload priv ioctl for Linux
**  \main\maintrunk.MT5921\6 2008-03-11 14:50:59 GMT mtk01461
**  Unify priv ioctl
**  \main\maintrunk.MT5921\5 2007-11-06 19:32:30 GMT mtk01088
**  add WPS code
**  \main\maintrunk.MT5921\4 2007-10-30 12:01:39 GMT MTK01425
**  1. Update wlanQueryInformation and wlanSetInformation
*/

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "gl_os.h"
#include "gl_wext_priv.h"
#if CFG_SUPPORT_WAPI
#include "gl_sec.h"
#endif
#if CFG_ENABLE_WIFI_DIRECT
#include "gl_p2p_os.h"
#endif

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
#define NUM_SUPPORTED_OIDS      (sizeof(arWlanOidReqTable) / sizeof(WLAN_REQ_ENTRY))

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

static int
priv_get_ndis (
    IN struct net_device *prNetDev,
    IN NDIS_TRANSPORT_STRUCT* prNdisReq,
    OUT PUINT_32 pu4OutputLen
    );

static int
priv_set_ndis (
    IN struct net_device *prNetDev,
    IN NDIS_TRANSPORT_STRUCT* prNdisReq,
    OUT PUINT_32 pu4OutputLen
    );

#if 0 /* CFG_SUPPORT_WPS */
static int
priv_set_appie (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwReqInfo,
    IN union iwreq_data *prIwReqData,
    OUT char *pcExtra
    );

static int
priv_set_filter (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwReqInfo,
    IN union iwreq_data *prIwReqData,
    OUT char *pcExtra
    );
#endif /* CFG_SUPPORT_WPS */

static BOOLEAN
reqSearchSupportedOidEntry (
    IN  UINT_32            rOid,
    OUT P_WLAN_REQ_ENTRY    *ppWlanReqEntry
    );

#if 0
static WLAN_STATUS
reqExtQueryConfiguration (
    IN  P_GLUE_INFO_T   prGlueInfo,
    OUT PVOID           pvQueryBuffer,
    IN UINT_32          u4QueryBufferLen,
    OUT PUINT_32        pu4QueryInfoLen
    );

static WLAN_STATUS
reqExtSetConfiguration (
    IN  P_GLUE_INFO_T prGlueInfo,
    IN  PVOID         pvSetBuffer,
    IN  UINT_32       u4SetBufferLen,
    OUT PUINT_32      pu4SetInfoLen
    );
#endif

static WLAN_STATUS
reqExtSetAcpiDevicePowerState (
    IN  P_GLUE_INFO_T prGlueInfo,
    IN  PVOID         pvSetBuffer,
    IN  UINT_32       u4SetBufferLen,
    OUT PUINT_32      pu4SetInfoLen
    );

/*******************************************************************************
*                       P R I V A T E   D A T A
********************************************************************************
*/
static UINT_8 aucOidBuf[4096] = {0};

/* OID processing table */
/* Order is important here because the OIDs should be in order of
   increasing value for binary searching. */
static WLAN_REQ_ENTRY arWlanOidReqTable[] = {
    /*
    {(NDIS_OID)rOid,
        (PUINT_8)pucOidName,
        fgQryBufLenChecking, fgSetBufLenChecking, fgIsHandleInGlueLayerOnly, u4InfoBufLen,
        pfOidQueryHandler,
        pfOidSetHandler}
    */
    /* General Operational Characteristics */

    /* Ethernet Operational Characteristics */
    {OID_802_3_CURRENT_ADDRESS,
        DISP_STRING("OID_802_3_CURRENT_ADDRESS"),
        TRUE, TRUE, ENUM_OID_DRIVER_CORE, 6,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQueryCurrentAddr,
        NULL},

    /* OID_802_3_MULTICAST_LIST */
    /* OID_802_3_MAXIMUM_LIST_SIZE */
    /* Ethernet Statistics */

    /* NDIS 802.11 Wireless LAN OIDs */
    {OID_802_11_SUPPORTED_RATES,
        DISP_STRING("OID_802_11_SUPPORTED_RATES"),
        TRUE, FALSE, ENUM_OID_DRIVER_CORE, sizeof(PARAM_RATES_EX),
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQuerySupportedRates,
        NULL},
    /*
    {OID_802_11_CONFIGURATION,
        DISP_STRING("OID_802_11_CONFIGURATION"),
        TRUE, TRUE, ENUM_OID_GLUE_EXTENSION, sizeof(PARAM_802_11_CONFIG_T),
        (PFN_OID_HANDLER_FUNC_REQ)reqExtQueryConfiguration,
        (PFN_OID_HANDLER_FUNC_REQ)reqExtSetConfiguration},
    */
    {OID_PNP_SET_POWER,
        DISP_STRING("OID_PNP_SET_POWER"),
        TRUE, FALSE, ENUM_OID_GLUE_EXTENSION, sizeof(PARAM_DEVICE_POWER_STATE),
        NULL,
        (PFN_OID_HANDLER_FUNC_REQ)reqExtSetAcpiDevicePowerState},

    /* Custom OIDs */
    {OID_CUSTOM_OID_INTERFACE_VERSION,
        DISP_STRING("OID_CUSTOM_OID_INTERFACE_VERSION"),
        TRUE, FALSE, ENUM_OID_DRIVER_CORE, 4,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQueryOidInterfaceVersion,
        NULL},

    /*
#if PTA_ENABLED
    {OID_CUSTOM_BT_COEXIST_CTRL,
        DISP_STRING("OID_CUSTOM_BT_COEXIST_CTRL"),
        FALSE, TRUE, ENUM_OID_DRIVER_CORE, sizeof(PARAM_CUSTOM_BT_COEXIST_T),
        NULL,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetBtCoexistCtrl},
#endif
    */

    /*
    {OID_CUSTOM_POWER_MANAGEMENT_PROFILE,
        DISP_STRING("OID_CUSTOM_POWER_MANAGEMENT_PROFILE"),
        FALSE, FALSE, ENUM_OID_DRIVER_CORE, 0,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQueryPwrMgmtProfParam,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetPwrMgmtProfParam},
    {OID_CUSTOM_PATTERN_CONFIG,
        DISP_STRING("OID_CUSTOM_PATTERN_CONFIG"),
        TRUE, TRUE, ENUM_OID_DRIVER_CORE, sizeof(PARAM_CUSTOM_PATTERN_SEARCH_CONFIG_STRUC_T),
        NULL,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetPatternConfig},
    {OID_CUSTOM_BG_SSID_SEARCH_CONFIG,
        DISP_STRING("OID_CUSTOM_BG_SSID_SEARCH_CONFIG"),
        FALSE, FALSE, ENUM_OID_DRIVER_CORE, 0,
        NULL,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetBgSsidParam},
    {OID_CUSTOM_VOIP_SETUP,
        DISP_STRING("OID_CUSTOM_VOIP_SETUP"),
        TRUE, TRUE, ENUM_OID_DRIVER_CORE, 4,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQueryVoipConnectionStatus,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetVoipConnectionStatus},
    {OID_CUSTOM_ADD_TS,
        DISP_STRING("OID_CUSTOM_ADD_TS"),
        TRUE, TRUE, ENUM_OID_DRIVER_CORE, 4,
        NULL,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidAddTS},
    {OID_CUSTOM_DEL_TS,
        DISP_STRING("OID_CUSTOM_DEL_TS"),
        TRUE, TRUE, ENUM_OID_DRIVER_CORE, 4,
        NULL,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidDelTS},
    */

    /*
#if CFG_LP_PATTERN_SEARCH_SLT
    {OID_CUSTOM_SLT,
        DISP_STRING("OID_CUSTOM_SLT"),
        FALSE, FALSE, ENUM_OID_DRIVER_CORE, 0,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQuerySltResult,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetSltMode},
#endif

    {OID_CUSTOM_ROAMING_EN,
        DISP_STRING("OID_CUSTOM_ROAMING_EN"),
        TRUE, TRUE, ENUM_OID_DRIVER_CORE, 4,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQueryRoamingFunction,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetRoamingFunction},
    {OID_CUSTOM_WMM_PS_TEST,
        DISP_STRING("OID_CUSTOM_WMM_PS_TEST"),
        TRUE, TRUE, ENUM_OID_DRIVER_CORE, 4,
        NULL,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetWiFiWmmPsTest},
    {OID_CUSTOM_COUNTRY_STRING,
        DISP_STRING("OID_CUSTOM_COUNTRY_STRING"),
        FALSE, FALSE, ENUM_OID_DRIVER_CORE, 0,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQueryCurrentCountry,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetCurrentCountry},

#if CFG_SUPPORT_802_11D
    {OID_CUSTOM_MULTI_DOMAIN_CAPABILITY,
        DISP_STRING("OID_CUSTOM_MULTI_DOMAIN_CAPABILITY"),
        FALSE, FALSE, ENUM_OID_DRIVER_CORE, 0,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQueryMultiDomainCap,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetMultiDomainCap},
#endif

    {OID_CUSTOM_GPIO2_MODE,
        DISP_STRING("OID_CUSTOM_GPIO2_MODE"),
        FALSE, TRUE, ENUM_OID_DRIVER_CORE, sizeof(ENUM_PARAM_GPIO2_MODE_T),
        NULL,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetGPIO2Mode},
    {OID_CUSTOM_CONTINUOUS_POLL,
        DISP_STRING("OID_CUSTOM_CONTINUOUS_POLL"),
        FALSE, TRUE, ENUM_OID_DRIVER_CORE, sizeof(PARAM_CONTINUOUS_POLL_T),
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQueryContinuousPollInterval,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetContinuousPollProfile},
    {OID_CUSTOM_DISABLE_BEACON_DETECTION,
        DISP_STRING("OID_CUSTOM_DISABLE_BEACON_DETECTION"),
        FALSE, TRUE, ENUM_OID_DRIVER_CORE, 4,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQueryDisableBeaconDetectionFunc,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetDisableBeaconDetectionFunc},
    */

    /* WPS */
    /*
    {OID_CUSTOM_DISABLE_PRIVACY_CHECK,
        DISP_STRING("OID_CUSTOM_DISABLE_PRIVACY_CHECK"),
        FALSE, TRUE, ENUM_OID_DRIVER_CORE, 4,
        NULL,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetDisablePriavcyCheck},
    */

    {OID_CUSTOM_MCR_RW,
        DISP_STRING("OID_CUSTOM_MCR_RW"),
        TRUE, TRUE, ENUM_OID_DRIVER_CORE, sizeof(PARAM_CUSTOM_MCR_RW_STRUC_T),
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQueryMcrRead,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetMcrWrite},

    {OID_CUSTOM_EEPROM_RW,
        DISP_STRING("OID_CUSTOM_EEPROM_RW"),
        TRUE, TRUE, ENUM_OID_DRIVER_CORE, sizeof(PARAM_CUSTOM_EEPROM_RW_STRUC_T),
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQueryEepromRead,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetEepromWrite},

    {OID_CUSTOM_SW_CTRL,
        DISP_STRING("OID_CUSTOM_SW_CTRL"),
        TRUE, TRUE, ENUM_OID_DRIVER_CORE, sizeof(PARAM_CUSTOM_SW_CTRL_STRUC_T),
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQuerySwCtrlRead,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetSwCtrlWrite},

    {OID_CUSTOM_MEM_DUMP,
        DISP_STRING("OID_CUSTOM_MEM_DUMP"),
        TRUE, TRUE, ENUM_OID_DRIVER_CORE, sizeof(PARAM_CUSTOM_MEM_DUMP_STRUC_T),
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQueryMemDump,
        NULL},

    {OID_CUSTOM_TEST_MODE,
        DISP_STRING("OID_CUSTOM_TEST_MODE"),
        FALSE, FALSE, ENUM_OID_DRIVER_CORE, 0,
        NULL,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidRftestSetTestMode},

    /*
    {OID_CUSTOM_TEST_RX_STATUS,
        DISP_STRING("OID_CUSTOM_TEST_RX_STATUS"),
        FALSE, TRUE, ENUM_OID_DRIVER_CORE, sizeof(PARAM_CUSTOM_RFTEST_RX_STATUS_STRUC_T),
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQueryRfTestRxStatus,
        NULL},
    {OID_CUSTOM_TEST_TX_STATUS,
        DISP_STRING("OID_CUSTOM_TEST_TX_STATUS"),
        FALSE, TRUE, ENUM_OID_DRIVER_CORE, sizeof(PARAM_CUSTOM_RFTEST_TX_STATUS_STRUC_T),
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQueryRfTestTxStatus,
        NULL},
    */
    {OID_CUSTOM_ABORT_TEST_MODE,
        DISP_STRING("OID_CUSTOM_ABORT_TEST_MODE"),
        FALSE, FALSE, ENUM_OID_DRIVER_CORE, 0,
        NULL,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidRftestSetAbortTestMode},
    {OID_CUSTOM_MTK_WIFI_TEST,
        DISP_STRING("OID_CUSTOM_MTK_WIFI_TEST"),
        TRUE, TRUE, ENUM_OID_DRIVER_CORE, sizeof(PARAM_MTK_WIFI_TEST_STRUC_T),
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidRftestQueryAutoTest,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidRftestSetAutoTest},

    /* OID_CUSTOM_EMULATION_VERSION_CONTROL */

    /* BWCS */
#if CFG_SUPPORT_BCM && CFG_SUPPORT_BCM_BWCS
    {OID_CUSTOM_BWCS_CMD,
        DISP_STRING("OID_CUSTOM_BWCS_CMD"),
        FALSE, FALSE, ENUM_OID_DRIVER_CORE, sizeof(PTA_IPC_T),
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQueryBT,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetBT},
#endif

/*    {OID_CUSTOM_SINGLE_ANTENNA,
        DISP_STRING("OID_CUSTOM_SINGLE_ANTENNA"),
        FALSE, FALSE, ENUM_OID_DRIVER_CORE, 4,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQueryBtSingleAntenna,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetBtSingleAntenna},
    {OID_CUSTOM_SET_PTA,
        DISP_STRING("OID_CUSTOM_SET_PTA"),
        FALSE, FALSE, ENUM_OID_DRIVER_CORE, 4,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQueryPta,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetPta},
    */

    { OID_CUSTOM_MTK_NVRAM_RW,
        DISP_STRING("OID_CUSTOM_MTK_NVRAM_RW"),
        TRUE, TRUE, ENUM_OID_DRIVER_CORE, sizeof(PARAM_CUSTOM_NVRAM_RW_STRUCT_T),
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQueryNvramRead,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetNvramWrite },

    { OID_CUSTOM_CFG_SRC_TYPE,
        DISP_STRING("OID_CUSTOM_CFG_SRC_TYPE"),
        FALSE, FALSE, ENUM_OID_DRIVER_CORE, sizeof(ENUM_CFG_SRC_TYPE_T),
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQueryCfgSrcType,
        NULL },

    { OID_CUSTOM_EEPROM_TYPE,
        DISP_STRING("OID_CUSTOM_EEPROM_TYPE"),
        FALSE, FALSE, ENUM_OID_DRIVER_CORE, sizeof(ENUM_EEPROM_TYPE_T),
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidQueryEepromType,
        NULL },

#if CFG_SUPPORT_WAPI
    {OID_802_11_WAPI_MODE,
        DISP_STRING("OID_802_11_WAPI_MODE"),
        FALSE, TRUE, ENUM_OID_DRIVER_CORE, 4,
        NULL,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetWapiMode},
    {OID_802_11_WAPI_ASSOC_INFO,
        DISP_STRING("OID_802_11_WAPI_ASSOC_INFO"),
        FALSE, FALSE, ENUM_OID_DRIVER_CORE, 0,
        NULL,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetWapiAssocInfo},
    {OID_802_11_SET_WAPI_KEY,
        DISP_STRING("OID_802_11_SET_WAPI_KEY"),
        FALSE, FALSE, ENUM_OID_DRIVER_CORE, sizeof(PARAM_WPI_KEY_T),
        NULL,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetWapiKey},
#endif

#if CFG_SUPPORT_WPS2
    {OID_802_11_WSC_ASSOC_INFO,
        DISP_STRING("OID_802_11_WSC_ASSOC_INFO"),
        FALSE, FALSE, ENUM_OID_DRIVER_CORE, 0,
        NULL,
        (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetWSCAssocInfo},
#endif
};

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/*!
* \brief Dispatching function for private ioctl region (SIOCIWFIRSTPRIV ~
*   SIOCIWLASTPRIV).
*
* \param[in] prNetDev Net device requested.
* \param[in] prIfReq Pointer to ifreq structure.
* \param[in] i4Cmd Command ID between SIOCIWFIRSTPRIV and SIOCIWLASTPRIV.
*
* \retval 0 for success.
* \retval -EOPNOTSUPP If cmd is not supported.
* \retval -EFAULT For fail.
*
*/
/*----------------------------------------------------------------------------*/
int
priv_support_ioctl (
    IN struct net_device *prNetDev,
    IN OUT struct ifreq *prIfReq,
    IN int i4Cmd
    )
{
    /* prIfReq is verified in the caller function wlanDoIOCTL() */
    struct iwreq *prIwReq = (struct iwreq *)prIfReq;
    struct iw_request_info rIwReqInfo;

    /* prDev is verified in the caller function wlanDoIOCTL() */

    /* Prepare the call */
    rIwReqInfo.cmd = (__u16)i4Cmd;
    rIwReqInfo.flags = 0;

    switch (i4Cmd) {
    case IOCTL_SET_INT:
        /* NOTE(Kevin): 1/3 INT Type <= IFNAMSIZ, so we don't need copy_from/to_user() */
        return priv_set_int(prNetDev, &rIwReqInfo, &(prIwReq->u), (char *) &(prIwReq->u));

    case IOCTL_GET_INT:
        /* NOTE(Kevin): 1/3 INT Type <= IFNAMSIZ, so we don't need copy_from/to_user() */
        return priv_get_int(prNetDev, &rIwReqInfo, &(prIwReq->u), (char *) &(prIwReq->u));

    case IOCTL_SET_STRUCT:
    case IOCTL_SET_STRUCT_FOR_EM:
        return priv_set_struct(prNetDev, &rIwReqInfo, &prIwReq->u, (char *) &(prIwReq->u));

    case IOCTL_GET_STRUCT:
        return priv_get_struct(prNetDev, &rIwReqInfo, &prIwReq->u, (char *) &(prIwReq->u));

    default:
        return -EOPNOTSUPP;

    } /* end of switch */

}/* priv_support_ioctl */


/*----------------------------------------------------------------------------*/
/*!
* \brief Private ioctl set int handler.
*
* \param[in] prNetDev Net device requested.
* \param[in] prIwReqInfo Pointer to iwreq structure.
* \param[in] prIwReqData The ioctl data structure, use the field of sub-command.
* \param[in] pcExtra The buffer with input value
*
* \retval 0 For success.
* \retval -EOPNOTSUPP If cmd is not supported.
* \retval -EINVAL If a value is out of range.
*
*/
/*----------------------------------------------------------------------------*/
int
priv_set_int (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwReqInfo,
    IN union iwreq_data *prIwReqData,
    IN char *pcExtra
    )
{
    UINT_32                     u4SubCmd;
    PUINT_32                    pu4IntBuf;
    P_NDIS_TRANSPORT_STRUCT     prNdisReq;
    P_GLUE_INFO_T               prGlueInfo;
    UINT_32                     u4BufLen = 0;
    int                         status = 0;
    P_PTA_IPC_T         prPtaIpc;

    ASSERT(prNetDev);
    ASSERT(prIwReqInfo);
    ASSERT(prIwReqData);
    ASSERT(pcExtra);

    if (FALSE == GLUE_CHK_PR3(prNetDev, prIwReqData, pcExtra)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    u4SubCmd = (UINT_32) prIwReqData->mode;
    pu4IntBuf = (PUINT_32) pcExtra;

    switch (u4SubCmd) {
    case PRIV_CMD_TEST_MODE:
        //printk("TestMode=%ld\n", pu4IntBuf[1]);
        prNdisReq = (P_NDIS_TRANSPORT_STRUCT) &aucOidBuf[0];

        if (pu4IntBuf[1] == PRIV_CMD_TEST_MAGIC_KEY) {
            prNdisReq->ndisOidCmd = OID_CUSTOM_TEST_MODE;
        }
        else if (pu4IntBuf[1] == 0) {
            prNdisReq->ndisOidCmd = OID_CUSTOM_ABORT_TEST_MODE;
        }
        else {
            status = 0;
            break;
        }
        prNdisReq->inNdisOidlength = 0;
        prNdisReq->outNdisOidLength = 0;

        /* Execute this OID */
        status = priv_set_ndis(prNetDev, prNdisReq, &u4BufLen);
        break;

    case PRIV_CMD_TEST_CMD:
        //printk("CMD=0x%08lx, data=0x%08lx\n", pu4IntBuf[1], pu4IntBuf[2]);
        prNdisReq = (P_NDIS_TRANSPORT_STRUCT) &aucOidBuf[0];

        kalMemCopy(&prNdisReq->ndisOidContent[0], &pu4IntBuf[1], 8);

        prNdisReq->ndisOidCmd = OID_CUSTOM_MTK_WIFI_TEST;
        prNdisReq->inNdisOidlength = 8;
        prNdisReq->outNdisOidLength = 8;

        /* Execute this OID */
        status = priv_set_ndis(prNetDev, prNdisReq, &u4BufLen);
        break;

#if CFG_SUPPORT_PRIV_MCR_RW
    case PRIV_CMD_ACCESS_MCR:
        //printk("addr=0x%08lx, data=0x%08lx\n", pu4IntBuf[1], pu4IntBuf[2]);
        prNdisReq = (P_NDIS_TRANSPORT_STRUCT) &aucOidBuf[0];

        if (!prGlueInfo->fgMcrAccessAllowed) {
            if (pu4IntBuf[1] == PRIV_CMD_TEST_MAGIC_KEY &&
                pu4IntBuf[2] == PRIV_CMD_TEST_MAGIC_KEY) {
                prGlueInfo->fgMcrAccessAllowed = TRUE;
            }
            status = 0;
            break;
        }

        kalMemCopy(&prNdisReq->ndisOidContent[0], &pu4IntBuf[1], 8);

        prNdisReq->ndisOidCmd = OID_CUSTOM_MCR_RW;
        prNdisReq->inNdisOidlength = 8;
        prNdisReq->outNdisOidLength = 8;

        /* Execute this OID */
        status = priv_set_ndis(prNetDev, prNdisReq, &u4BufLen);
        break;
#endif

    case PRIV_CMD_SW_CTRL:
        //printk("addr=0x%08lx, data=0x%08lx\n", pu4IntBuf[1], pu4IntBuf[2]);
        prNdisReq = (P_NDIS_TRANSPORT_STRUCT) &aucOidBuf[0];

        kalMemCopy(&prNdisReq->ndisOidContent[0], &pu4IntBuf[1], 8);

        prNdisReq->ndisOidCmd = OID_CUSTOM_SW_CTRL;
        prNdisReq->inNdisOidlength = 8;
        prNdisReq->outNdisOidLength = 8;

        /* Execute this OID */
        status = priv_set_ndis(prNetDev, prNdisReq, &u4BufLen);
        break;


    #if 0
    case PRIV_CMD_BEACON_PERIOD:
        rStatus = wlanSetInformation(prGlueInfo->prAdapter,
                           wlanoidSetBeaconInterval,
                           (PVOID)&pu4IntBuf[1], /* pu4IntBuf[0] is used as input SubCmd */
                           sizeof(UINT_32),
                           &u4BufLen);
        break;
    #endif

#if CFG_TCP_IP_CHKSUM_OFFLOAD
    case PRIV_CMD_CSUM_OFFLOAD:
        {
            UINT_32 u4CSUMFlags;


            if (pu4IntBuf[1] == 1) {
                u4CSUMFlags = CSUM_OFFLOAD_EN_ALL;
            }
            else if (pu4IntBuf[1] == 0) {
                u4CSUMFlags = 0;
            }
            else {
                return -EINVAL;
            }

            if (kalIoctl(prGlueInfo,
                        wlanoidSetCSUMOffload,
                        (PVOID)&u4CSUMFlags,
                        sizeof(UINT_32),
                        FALSE,
                        FALSE,
                        TRUE,
                        FALSE,
                        &u4BufLen
            ) == WLAN_STATUS_SUCCESS) {
                if (pu4IntBuf[1] == 1) {
                    prNetDev->features |= NETIF_F_HW_CSUM;
                } else if (pu4IntBuf[1] == 0) {
                    prNetDev->features &= ~NETIF_F_HW_CSUM;
                }
            }
        }
        break;
#endif /* CFG_TCP_IP_CHKSUM_OFFLOAD */

    case PRIV_CMD_POWER_MODE:
            kalIoctl(prGlueInfo,
                        wlanoidSet802dot11PowerSaveProfile,
                        (PVOID)&pu4IntBuf[1], /* pu4IntBuf[0] is used as input SubCmd */
                        sizeof(UINT_32),
                        FALSE,
                        FALSE,
                        TRUE,
                        FALSE,
                        &u4BufLen);
        break;

    case PRIV_CMD_WMM_PS:
        {
            PARAM_CUSTOM_WMM_PS_TEST_STRUC_T rWmmPsTest;

            rWmmPsTest.bmfgApsdEnAc = (UINT_8)pu4IntBuf[1];
            rWmmPsTest.ucIsEnterPsAtOnce = (UINT_8)pu4IntBuf[2];
            rWmmPsTest.ucIsDisableUcTrigger = (UINT_8)pu4IntBuf[3];
            rWmmPsTest.reserved = 0;

            kalIoctl(prGlueInfo,
                        wlanoidSetWiFiWmmPsTest,
                        (PVOID)&rWmmPsTest,
                        sizeof(PARAM_CUSTOM_WMM_PS_TEST_STRUC_T),
                        FALSE,
                        FALSE,
                        TRUE,
                        FALSE,
                        &u4BufLen);
        }
        break;

    #if 0
    case PRIV_CMD_ADHOC_MODE:
        rStatus = wlanSetInformation(prGlueInfo->prAdapter,
                           wlanoidSetAdHocMode,
                           (PVOID)&pu4IntBuf[1], /* pu4IntBuf[0] is used as input SubCmd */
                           sizeof(UINT_32),
                           &u4BufLen);
        break;
    #endif

    case PRIV_CUSTOM_BWCS_CMD:

        DBGLOG(REQ, INFO, ("pu4IntBuf[1] = %x, size of PTA_IPC_T = %d.\n", pu4IntBuf[1], sizeof(PARAM_PTA_IPC_T)));

        prPtaIpc = (P_PTA_IPC_T) aucOidBuf;
        prPtaIpc->u.aucBTPParams[0] = (UINT_8) (pu4IntBuf[1] >> 24);
        prPtaIpc->u.aucBTPParams[1] = (UINT_8) (pu4IntBuf[1] >> 16);
        prPtaIpc->u.aucBTPParams[2] = (UINT_8) (pu4IntBuf[1] >> 8);
        prPtaIpc->u.aucBTPParams[3] = (UINT_8) (pu4IntBuf[1]);

        DBGLOG(REQ, INFO, ("BCM BWCS CMD : PRIV_CUSTOM_BWCS_CMD : aucBTPParams[0] = %02x, aucBTPParams[1] = %02x, aucBTPParams[2] = %02x, aucBTPParams[3] = %02x.\n",
            prPtaIpc->u.aucBTPParams[0],
            prPtaIpc->u.aucBTPParams[1],
            prPtaIpc->u.aucBTPParams[2],
            prPtaIpc->u.aucBTPParams[3]));

#if 0
        status = wlanSetInformation(prGlueInfo->prAdapter,
                            wlanoidSetBT,
                            (PVOID)&aucOidBuf[0],
                            u4CmdLen,
                            &u4BufLen);
#endif

        status = wlanoidSetBT(prGlueInfo->prAdapter,
                            (PVOID)&aucOidBuf[0],
                            sizeof(PARAM_PTA_IPC_T),
                            &u4BufLen);

        if (WLAN_STATUS_SUCCESS != status) {
            status = -EFAULT;
        }

        break;

    case PRIV_CMD_BAND_CONFIG:
        {
            DBGLOG(INIT, INFO, ("CMD set_band=%u\n", pu4IntBuf[1]));
        }
        break;

#if CFG_ENABLE_WIFI_DIRECT
    case PRIV_CMD_P2P_MODE:
        {
            PARAM_CUSTOM_P2P_SET_STRUC_T rSetP2P;
            WLAN_STATUS rWlanStatus = WLAN_STATUS_SUCCESS;

            rSetP2P.u4Enable = pu4IntBuf[1];
            rSetP2P.u4Mode = pu4IntBuf[2];

            if(!rSetP2P.u4Enable) {
                p2pNetUnregister(prGlueInfo, TRUE);
            }

            rWlanStatus = kalIoctl(prGlueInfo,
                                wlanoidSetP2pMode,
                                (PVOID)&rSetP2P, /* pu4IntBuf[0] is used as input SubCmd */
                                sizeof(PARAM_CUSTOM_P2P_SET_STRUC_T),
                                FALSE,
                                FALSE,
                                TRUE,
                                FALSE,
                                &u4BufLen);

            if(rSetP2P.u4Enable) {
                p2pNetRegister(prGlueInfo, TRUE);
            }


        }
        break;
#endif

    default:
        return -EOPNOTSUPP;
    }

    return status;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief Private ioctl get int handler.
*
* \param[in] pDev Net device requested.
* \param[out] pIwReq Pointer to iwreq structure.
* \param[in] prIwReqData The ioctl req structure, use the field of sub-command.
* \param[out] pcExtra The buffer with put the return value
*
* \retval 0 For success.
* \retval -EOPNOTSUPP If cmd is not supported.
* \retval -EFAULT For fail.
*
*/
/*----------------------------------------------------------------------------*/
int
priv_get_int (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwReqInfo,
    IN union iwreq_data *prIwReqData,
    IN OUT char *pcExtra
    )
{
    UINT_32                     u4SubCmd;
    PUINT_32                    pu4IntBuf;
    P_GLUE_INFO_T               prGlueInfo;
    UINT_32                     u4BufLen = 0;
    int                         status = 0;
    P_NDIS_TRANSPORT_STRUCT     prNdisReq;
    INT_32                      ch[50];

    ASSERT(prNetDev);
    ASSERT(prIwReqInfo);
    ASSERT(prIwReqData);
    ASSERT(pcExtra);
    if (FALSE == GLUE_CHK_PR3(prNetDev, prIwReqData, pcExtra)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    u4SubCmd = (UINT_32) prIwReqData->mode;
    pu4IntBuf = (PUINT_32) pcExtra;

    switch (u4SubCmd) {
    case PRIV_CMD_TEST_CMD:
        //printk("CMD=0x%08lx, data=0x%08lx\n", pu4IntBuf[1], pu4IntBuf[2]);
        prNdisReq = (P_NDIS_TRANSPORT_STRUCT) &aucOidBuf[0];

        kalMemCopy(&prNdisReq->ndisOidContent[0], &pu4IntBuf[1], 8);

        prNdisReq->ndisOidCmd = OID_CUSTOM_MTK_WIFI_TEST;
        prNdisReq->inNdisOidlength = 8;
        prNdisReq->outNdisOidLength = 8;

        status = priv_get_ndis(prNetDev, prNdisReq, &u4BufLen);
        if (status == 0) {
            //printk("Result=%ld\n", *(PUINT_32)&prNdisReq->ndisOidContent[4]);
            prIwReqData->mode = *(PUINT_32)&prNdisReq->ndisOidContent[4];
            /*
            if (copy_to_user(prIwReqData->data.pointer,
                             &prNdisReq->ndisOidContent[4], 4)) {
                printk(KERN_NOTICE "priv_get_int() copy_to_user oidBuf fail(3)\n");
                return -EFAULT;
            }
            */
        }
        return status;

#if CFG_SUPPORT_PRIV_MCR_RW
    case PRIV_CMD_ACCESS_MCR:
        //printk("addr=0x%08lx\n", pu4IntBuf[1]);
        prNdisReq = (P_NDIS_TRANSPORT_STRUCT) &aucOidBuf[0];

        if (!prGlueInfo->fgMcrAccessAllowed) {
            status = 0;
            return status;
        }

        kalMemCopy(&prNdisReq->ndisOidContent[0], &pu4IntBuf[1], 8);

        prNdisReq->ndisOidCmd = OID_CUSTOM_MCR_RW;
        prNdisReq->inNdisOidlength = 8;
        prNdisReq->outNdisOidLength = 8;

        status = priv_get_ndis(prNetDev, prNdisReq, &u4BufLen);
        if (status == 0) {
            //printk("Result=%ld\n", *(PUINT_32)&prNdisReq->ndisOidContent[4]);
            prIwReqData->mode = *(PUINT_32)&prNdisReq->ndisOidContent[4];
        }
        return status;
#endif

    case PRIV_CMD_DUMP_MEM:
        prNdisReq = (P_NDIS_TRANSPORT_STRUCT) &aucOidBuf[0];

#if 1
        if (!prGlueInfo->fgMcrAccessAllowed) {
            status = 0;
            return status;
        }
#endif
        kalMemCopy(&prNdisReq->ndisOidContent[0], &pu4IntBuf[1], 8);

        prNdisReq->ndisOidCmd = OID_CUSTOM_MEM_DUMP;
        prNdisReq->inNdisOidlength = 8;
        prNdisReq->outNdisOidLength = 8;

        status = priv_get_ndis(prNetDev, prNdisReq, &u4BufLen);
        if (status == 0) {
            prIwReqData->mode = *(PUINT_32)&prNdisReq->ndisOidContent[0];
        }
        return status;

    case PRIV_CMD_SW_CTRL:
        //printk(" addr=0x%08lx\n", pu4IntBuf[1]);

        prNdisReq = (P_NDIS_TRANSPORT_STRUCT) &aucOidBuf[0];

        kalMemCopy(&prNdisReq->ndisOidContent[0], &pu4IntBuf[1], 8);

        prNdisReq->ndisOidCmd = OID_CUSTOM_SW_CTRL;
        prNdisReq->inNdisOidlength = 8;
        prNdisReq->outNdisOidLength = 8;

        status = priv_get_ndis(prNetDev, prNdisReq, &u4BufLen);
        if (status == 0) {
            //printk("Result=%ld\n", *(PUINT_32)&prNdisReq->ndisOidContent[4]);
            prIwReqData->mode = *(PUINT_32)&prNdisReq->ndisOidContent[4];
        }
        return status;

    #if 0
    case PRIV_CMD_BEACON_PERIOD:
        status = wlanQueryInformation(prGlueInfo->prAdapter,
                             wlanoidQueryBeaconInterval,
                             (PVOID)pu4IntBuf,
                             sizeof(UINT_32),
                             &u4BufLen);
        return status;

    case PRIV_CMD_POWER_MODE:
        status = wlanQueryInformation(prGlueInfo->prAdapter,
                             wlanoidQuery802dot11PowerSaveProfile,
                             (PVOID)pu4IntBuf,
                             sizeof(UINT_32),
                             &u4BufLen);
        return status;

    case PRIV_CMD_ADHOC_MODE:
        status = wlanQueryInformation(prGlueInfo->prAdapter,
                             wlanoidQueryAdHocMode,
                             (PVOID)pu4IntBuf,
                             sizeof(UINT_32),
                             &u4BufLen);
        return status;
    #endif

    case PRIV_CMD_BAND_CONFIG:
        DBGLOG(INIT, INFO, ("CMD get_band=\n"));
        prIwReqData->mode = 0;
        return status;

    default:
        break;
    }

    u4SubCmd = (UINT_32) prIwReqData->data.flags;

    switch (u4SubCmd) {
    case PRIV_CMD_GET_CH_LIST:
        {
            UINT_16 i, j = 0;
            UINT_8 NumOfChannel = 50;
            UINT_8 ucMaxChannelNum = 50;
            RF_CHANNEL_INFO_T aucChannelList[50];

            kalGetChannelList(prGlueInfo, BAND_NULL, ucMaxChannelNum, &NumOfChannel, aucChannelList);
            if (NumOfChannel > 50)
                NumOfChannel = 50;

            if (kalIsAPmode(prGlueInfo)) {
                for (i = 0; i < NumOfChannel; i++) {
                    if ((aucChannelList[i].ucChannelNum <= 13) ||
                        (aucChannelList[i].ucChannelNum == 36 || aucChannelList[i].ucChannelNum == 40 ||
                        aucChannelList[i].ucChannelNum == 44 || aucChannelList[i].ucChannelNum == 48)) {
                        ch[j] = (INT_32)aucChannelList[i].ucChannelNum;
                        j++;
                    }
                }
            }
            else {
                for (j = 0; j < NumOfChannel; j++) {
                    ch[j] = (INT_32)aucChannelList[j].ucChannelNum;
                }
            }

            prIwReqData->data.length = j;
            if (copy_to_user(prIwReqData->data.pointer, ch, NumOfChannel*sizeof(INT_32))) {
                 return -EFAULT;
            }
            else
                 return status;
        }
    default:
        return -EOPNOTSUPP;
    }

    return status;
} /* priv_get_int */


/*----------------------------------------------------------------------------*/
/*!
* \brief Private ioctl set int array handler.
*
* \param[in] prNetDev Net device requested.
* \param[in] prIwReqInfo Pointer to iwreq structure.
* \param[in] prIwReqData The ioctl data structure, use the field of sub-command.
* \param[in] pcExtra The buffer with input value
*
* \retval 0 For success.
* \retval -EOPNOTSUPP If cmd is not supported.
* \retval -EINVAL If a value is out of range.
*
*/
/*----------------------------------------------------------------------------*/
int
priv_set_ints (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwReqInfo,
    IN union iwreq_data *prIwReqData,
    IN char *pcExtra
    )
{
    UINT_32                     u4SubCmd, u4BufLen;
    P_GLUE_INFO_T               prGlueInfo;
    int                         status = 0;
    WLAN_STATUS                 rStatus = WLAN_STATUS_SUCCESS;
    P_SET_TXPWR_CTRL_T          prTxpwr;

    ASSERT(prNetDev);
    ASSERT(prIwReqInfo);
    ASSERT(prIwReqData);
    ASSERT(pcExtra);

    if (FALSE == GLUE_CHK_PR3(prNetDev, prIwReqData, pcExtra)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    u4SubCmd = (UINT_32) prIwReqData->data.flags;

    switch (u4SubCmd) {
    case PRIV_CMD_SET_TX_POWER:
        {
        INT_32 *setting = prIwReqData->data.pointer;
        UINT_16 i;

#if 0
        printk("Tx power num = %d\n", prIwReqData->data.length);

        printk("Tx power setting = %d %d %d %d\n",
                            setting[0], setting[1], setting[2], setting[3]);
#endif
        prTxpwr = &prGlueInfo->rTxPwr;
        if (setting[0] == 0 && prIwReqData->data.length == 4 /* argc num */) {
            /* 0 (All networks), 1 (legacy STA), 2 (Hotspot AP), 3 (P2P), 4 (BT over Wi-Fi) */
            if (setting[1] == 1 || setting[1] == 0) {
                if (setting[2] == 0 || setting[2] == 1)
                    prTxpwr->c2GLegacyStaPwrOffset = setting[3];
                if (setting[2] == 0 || setting[2] == 2)
                    prTxpwr->c5GLegacyStaPwrOffset = setting[3];
            }
            if (setting[1] == 2 || setting[1] == 0) {
                if (setting[2] == 0 || setting[2] == 1)
                    prTxpwr->c2GHotspotPwrOffset = setting[3];
                if (setting[2] == 0 || setting[2] == 2)
                    prTxpwr->c5GHotspotPwrOffset = setting[3];
            }
            if (setting[1] == 3 || setting[1] == 0) {
                if (setting[2] == 0 || setting[2] == 1)
                    prTxpwr->c2GP2pPwrOffset = setting[3];
                if (setting[2] == 0 || setting[2] == 2)
                    prTxpwr->c5GP2pPwrOffset = setting[3];
            }
            if (setting[1] == 4 || setting[1] == 0) {
                if (setting[2] == 0 || setting[2] == 1)
                    prTxpwr->c2GBowPwrOffset = setting[3];
                if (setting[2] == 0 || setting[2] == 2)
                    prTxpwr->c5GBowPwrOffset = setting[3];
            }
        }
        else if (setting[0] == 1 && prIwReqData->data.length == 2) {
            prTxpwr->ucConcurrencePolicy = setting[1];
        }
        else if (setting[0] == 2 && prIwReqData->data.length == 3) {
            if (setting[1] == 0) {
                for (i=0; i<14; i++)
                    prTxpwr->acTxPwrLimit2G[i] = setting[2];
            }
            else if (setting[1] <= 14)
                prTxpwr->acTxPwrLimit2G[setting[1] - 1] = setting[2];
        }
        else if (setting[0] == 3 && prIwReqData->data.length == 3) {
            if (setting[1] == 0) {
                for (i=0; i<4; i++)
                    prTxpwr->acTxPwrLimit5G[i] = setting[2];
            }
            else if (setting[1] <= 4)
                prTxpwr->acTxPwrLimit5G[setting[1] - 1] = setting[2];
        }
        else if (setting[0] == 4 && prIwReqData->data.length == 2) {
            if (setting[1] == 0) {
                wlanDefTxPowerCfg(prGlueInfo->prAdapter);
            }
            rStatus = kalIoctl(prGlueInfo,
                wlanoidSetTxPower,
                prTxpwr,
                sizeof(SET_TXPWR_CTRL_T),
                TRUE,
                FALSE,
                FALSE,
                FALSE,
                &u4BufLen);
        }
        else
            return -EFAULT;
        }
        return status;
    default:
        break;
    }

    return status;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief Private ioctl get int array handler.
*
* \param[in] pDev Net device requested.
* \param[out] pIwReq Pointer to iwreq structure.
* \param[in] prIwReqData The ioctl req structure, use the field of sub-command.
* \param[out] pcExtra The buffer with put the return value
*
* \retval 0 For success.
* \retval -EOPNOTSUPP If cmd is not supported.
* \retval -EFAULT For fail.
*
*/
/*----------------------------------------------------------------------------*/
int
priv_get_ints (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwReqInfo,
    IN union iwreq_data *prIwReqData,
    IN OUT char *pcExtra
    )
{
    UINT_32                     u4SubCmd;
    P_GLUE_INFO_T               prGlueInfo;
    int                         status = 0;
    INT_32                      ch[50];

    ASSERT(prNetDev);
    ASSERT(prIwReqInfo);
    ASSERT(prIwReqData);
    ASSERT(pcExtra);
    if (FALSE == GLUE_CHK_PR3(prNetDev, prIwReqData, pcExtra)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    u4SubCmd = (UINT_32) prIwReqData->data.flags;

    switch (u4SubCmd) {
    case PRIV_CMD_GET_CH_LIST:
        {
            UINT_16 i;
            UINT_8 NumOfChannel = 50;
            UINT_8 ucMaxChannelNum = 50;
            RF_CHANNEL_INFO_T aucChannelList[50];

            kalGetChannelList(prGlueInfo, BAND_NULL, ucMaxChannelNum, &NumOfChannel, aucChannelList);
            if (NumOfChannel > 50)
                NumOfChannel = 50;

            for (i = 0; i < NumOfChannel; i++) {
                ch[i] = (INT_32)aucChannelList[i].ucChannelNum;
            }

            prIwReqData->data.length = NumOfChannel;
            if (copy_to_user(prIwReqData->data.pointer, ch, NumOfChannel*sizeof(INT_32))) {
                 return -EFAULT;
            }
            else
                 return status;
        }
    default:
        break;
    }

    return status;
} /* priv_get_int */

/*----------------------------------------------------------------------------*/
/*!
* \brief Private ioctl set structure handler.
*
* \param[in] pDev Net device requested.
* \param[in] prIwReqData Pointer to iwreq_data structure.
*
* \retval 0 For success.
* \retval -EOPNOTSUPP If cmd is not supported.
* \retval -EINVAL If a value is out of range.
*
*/
/*----------------------------------------------------------------------------*/
int
priv_set_struct (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwReqInfo,
    IN union iwreq_data *prIwReqData,
    IN char *pcExtra
    )
{
    UINT_32 u4SubCmd = 0;
    int                 status = 0;
    //WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32         u4CmdLen = 0;
    P_NDIS_TRANSPORT_STRUCT     prNdisReq;
    PUINT_32 pu4IntBuf = NULL;

    P_GLUE_INFO_T   prGlueInfo = NULL;
    UINT_32         u4BufLen = 0;

    ASSERT(prNetDev);
    //ASSERT(prIwReqInfo);
    ASSERT(prIwReqData);
    //ASSERT(pcExtra);

	kalMemZero(&aucOidBuf[0], sizeof(aucOidBuf));

    if (FALSE == GLUE_CHK_PR2(prNetDev, prIwReqData)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    u4SubCmd = (UINT_32) prIwReqData->data.flags;

#if 0
    printk(KERN_INFO DRV_NAME"priv_set_struct(): prIwReqInfo->cmd(0x%X), u4SubCmd(%ld)\n",
        prIwReqInfo->cmd,
        u4SubCmd
        );
#endif

    switch (u4SubCmd) {
#if 0 //PTA_ENABLED
    case PRIV_CMD_BT_COEXIST:
        u4CmdLen = prIwReqData->data.length * sizeof(UINT_32);
        ASSERT(sizeof(PARAM_CUSTOM_BT_COEXIST_T) >= u4CmdLen);
        if (sizeof(PARAM_CUSTOM_BT_COEXIST_T) < u4CmdLen) {
            return -EFAULT;
        }

        if (copy_from_user(&aucOidBuf[0], prIwReqData->data.pointer, u4CmdLen)) {
            status = -EFAULT; //return -EFAULT;
            break;
        }

        rStatus = wlanSetInformation(prGlueInfo->prAdapter,
                            wlanoidSetBtCoexistCtrl,
                            (PVOID)&aucOidBuf[0],
                            u4CmdLen,
                            &u4BufLen);
        if (WLAN_STATUS_SUCCESS != rStatus) {
            status = -EFAULT;
        }
        break;
#endif

    case PRIV_CUSTOM_BWCS_CMD:
        u4CmdLen = prIwReqData->data.length * sizeof(UINT_32);
        ASSERT(sizeof(PARAM_PTA_IPC_T) >= u4CmdLen);
        if (sizeof(PARAM_PTA_IPC_T) < u4CmdLen) {
            return -EFAULT;
        }
#if CFG_SUPPORT_BCM && CFG_SUPPORT_BCM_BWCS && CFG_SUPPORT_BCM_BWCS_DEBUG
        DBGLOG(REQ, INFO, ("ucCmdLen = %d, size of PTA_IPC_T = %d, prIwReqData->data = 0x%x.\n", u4CmdLen, sizeof(PARAM_PTA_IPC_T), prIwReqData->data));

        DBGLOG(REQ, INFO, ("priv_set_struct(): prIwReqInfo->cmd(0x%X), u4SubCmd(%ld)\n",
            prIwReqInfo->cmd,
            u4SubCmd
            ));

        DBGLOG(REQ, INFO, ("*pcExtra = 0x%x\n", *pcExtra));
 #endif

        if (copy_from_user(&aucOidBuf[0], prIwReqData->data.pointer, u4CmdLen)) {
            status = -EFAULT; //return -EFAULT;
            break;
        }
#if CFG_SUPPORT_BCM && CFG_SUPPORT_BCM_BWCS && CFG_SUPPORT_BCM_BWCS_DEBUG
        DBGLOG(REQ, INFO, ("priv_set_struct(): BWCS CMD = %02x%02x%02x%02x\n",
            aucOidBuf[2], aucOidBuf[3], aucOidBuf[4], aucOidBuf[5]));
#endif

#if 0
        status = wlanSetInformation(prGlueInfo->prAdapter,
                            wlanoidSetBT,
                            (PVOID)&aucOidBuf[0],
                            u4CmdLen,
                            &u4BufLen);
#endif

#if 1
        status = wlanoidSetBT(prGlueInfo->prAdapter,
                            (PVOID)&aucOidBuf[0],
                            u4CmdLen,
                            &u4BufLen);
#endif

        if (WLAN_STATUS_SUCCESS != status) {
            status = -EFAULT;
        }

        break;

#if CFG_SUPPORT_WPS2
    case PRIV_CMD_WSC_PROBE_REQ:
		{
			/* retrieve IE for Probe Request */
            if (prIwReqData->data.length > 0) {
				if (copy_from_user(prGlueInfo->aucWSCIE, prIwReqData->data.pointer,
					prIwReqData->data.length)) {
                    status = -EFAULT;
				    break;
                }
				prGlueInfo->u2WSCIELen = prIwReqData->data.length;
            }
			else {
			    prGlueInfo->u2WSCIELen = 0;
			}
    	}
		break;
#endif
    case PRIV_CMD_OID:
        if (copy_from_user(&aucOidBuf[0],
                            prIwReqData->data.pointer,
                            prIwReqData->data.length)) {
            status = -EFAULT;
            break;
        }
        if (!kalMemCmp(&aucOidBuf[0], pcExtra, prIwReqData->data.length)) {
            DBGLOG(REQ, INFO, ("pcExtra buffer is valid\n"));
        }
        else
            DBGLOG(REQ, INFO, ("pcExtra 0x%p\n", pcExtra));

        /* Execute this OID */
        status = priv_set_ndis(prNetDev, (P_NDIS_TRANSPORT_STRUCT)&aucOidBuf[0], &u4BufLen);
        /* Copy result to user space */
        ((P_NDIS_TRANSPORT_STRUCT)&aucOidBuf[0])->outNdisOidLength = u4BufLen;

        if (copy_to_user(prIwReqData->data.pointer,
                        &aucOidBuf[0],
                        OFFSET_OF(NDIS_TRANSPORT_STRUCT, ndisOidContent))) {
            DBGLOG(REQ, INFO, ("copy_to_user oidBuf fail\n"));
            status = -EFAULT;
        }

        break;

    case PRIV_CMD_SW_CTRL:
        pu4IntBuf = (PUINT_32)prIwReqData->data.pointer;
        prNdisReq = (P_NDIS_TRANSPORT_STRUCT) &aucOidBuf[0];

        //kalMemCopy(&prNdisReq->ndisOidContent[0], prIwReqData->data.pointer, 8);
        if (copy_from_user(&prNdisReq->ndisOidContent[0],
                           prIwReqData->data.pointer,
                           prIwReqData->data.length)) {
            status = -EFAULT;
            break;
        }
        prNdisReq->ndisOidCmd = OID_CUSTOM_SW_CTRL;
        prNdisReq->inNdisOidlength = 8;
        prNdisReq->outNdisOidLength = 8;

        /* Execute this OID */
        status = priv_set_ndis(prNetDev, prNdisReq, &u4BufLen);
        break;

    default:
        return -EOPNOTSUPP;
    }

    return status;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief Private ioctl get struct handler.
*
* \param[in] pDev Net device requested.
* \param[out] pIwReq Pointer to iwreq structure.
* \param[in] cmd Private sub-command.
*
* \retval 0 For success.
* \retval -EFAULT If copy from user space buffer fail.
* \retval -EOPNOTSUPP Parameter "cmd" not recognized.
*
*/
/*----------------------------------------------------------------------------*/
int
priv_get_struct (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwReqInfo,
    IN union iwreq_data *prIwReqData,
    IN OUT char *pcExtra
    )
{
    UINT_32 u4SubCmd = 0;
    P_NDIS_TRANSPORT_STRUCT prNdisReq= NULL;

    P_GLUE_INFO_T   prGlueInfo = NULL;
    UINT_32         u4BufLen = 0;
    PUINT_32        pu4IntBuf = NULL;
    int             status = 0;

    kalMemZero(&aucOidBuf[0], sizeof(aucOidBuf));

    ASSERT(prNetDev);
    ASSERT(prIwReqData);
    if (!prNetDev || !prIwReqData) {
        DBGLOG(REQ, INFO, ("priv_get_struct(): invalid param(0x%p, 0x%p)\n",
            prNetDev, prIwReqData));
        return -EINVAL;
    }

    u4SubCmd = (UINT_32) prIwReqData->data.flags;
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));
    ASSERT(prGlueInfo);
    if (!prGlueInfo) {
        DBGLOG(REQ, INFO, ("priv_get_struct(): invalid prGlueInfo(0x%p, 0x%p)\n",
            prNetDev, *((P_GLUE_INFO_T *) netdev_priv(prNetDev))));
        return -EINVAL;
    }

#if 0
    printk(KERN_INFO DRV_NAME"priv_get_struct(): prIwReqInfo->cmd(0x%X), u4SubCmd(%ld)\n",
        prIwReqInfo->cmd,
        u4SubCmd
        );
#endif
    memset(aucOidBuf, 0, sizeof(aucOidBuf));

    switch (u4SubCmd) {
    case PRIV_CMD_OID:
        if (copy_from_user(&aucOidBuf[0],
                prIwReqData->data.pointer,
                sizeof(NDIS_TRANSPORT_STRUCT))) {
            DBGLOG(REQ, INFO, ("priv_get_struct() copy_from_user oidBuf fail\n"));
            return -EFAULT;
        }

        prNdisReq = (P_NDIS_TRANSPORT_STRUCT)&aucOidBuf[0];
#if 0
        printk(KERN_NOTICE "\n priv_get_struct cmd 0x%02x len:%d OID:0x%08x OID Len:%d\n",
            cmd,
            pIwReq->u.data.length,
            ndisReq->ndisOidCmd,
            ndisReq->inNdisOidlength);
#endif
        if (priv_get_ndis(prNetDev, prNdisReq, &u4BufLen) == 0) {
            prNdisReq->outNdisOidLength = u4BufLen;
            if (copy_to_user(prIwReqData->data.pointer,
                    &aucOidBuf[0],
                    u4BufLen + sizeof(NDIS_TRANSPORT_STRUCT) - sizeof(prNdisReq->ndisOidContent))) {
                DBGLOG(REQ, INFO, ("priv_get_struct() copy_to_user oidBuf fail(1)\n"));
                return -EFAULT;
            }
            return 0;
        }
        else {
            prNdisReq->outNdisOidLength = u4BufLen;
            if (copy_to_user(prIwReqData->data.pointer,
                    &aucOidBuf[0],
                    OFFSET_OF(NDIS_TRANSPORT_STRUCT, ndisOidContent))) {
                DBGLOG(REQ, INFO, ("priv_get_struct() copy_to_user oidBuf fail(2)\n"));
            }
            return -EFAULT;
        }
        break;

    case PRIV_CMD_SW_CTRL:
        pu4IntBuf = (PUINT_32)prIwReqData->data.pointer;
        prNdisReq = (P_NDIS_TRANSPORT_STRUCT) &aucOidBuf[0];

        if (copy_from_user(&prNdisReq->ndisOidContent[0],
                prIwReqData->data.pointer,
                prIwReqData->data.length)) {
            DBGLOG(REQ, INFO, ("priv_get_struct() copy_from_user oidBuf fail\n"));
            return -EFAULT;
        }

        prNdisReq->ndisOidCmd = OID_CUSTOM_SW_CTRL;
        prNdisReq->inNdisOidlength = 8;
        prNdisReq->outNdisOidLength = 8;

        status = priv_get_ndis(prNetDev, prNdisReq, &u4BufLen);
        if (status == 0) {
            prNdisReq->outNdisOidLength = u4BufLen;
            //printk("len=%d Result=%08lx\n", u4BufLen, *(PUINT_32)&prNdisReq->ndisOidContent[4]);

            if (copy_to_user(prIwReqData->data.pointer,
                    &prNdisReq->ndisOidContent[4],
                    4 /* OFFSET_OF(NDIS_TRANSPORT_STRUCT, ndisOidContent)*/)) {
                DBGLOG(REQ, INFO, ("priv_get_struct() copy_to_user oidBuf fail(2)\n"));
            }
        }
        return 0;
        break;
    default:
        DBGLOG(REQ, WARN, ("get struct cmd:0x%lx\n", u4SubCmd));
        return -EOPNOTSUPP;
    }
} /* priv_get_struct */

/*----------------------------------------------------------------------------*/
/*!
* \brief The routine handles a set operation for a single OID.
*
* \param[in] pDev Net device requested.
* \param[in] ndisReq Ndis request OID information copy from user.
* \param[out] outputLen_p If the call is successful, returns the number of
*                         bytes written into the query buffer. If the
*                         call failed due to invalid length of the query
*                         buffer, returns the amount of storage needed..
*
* \retval 0 On success.
* \retval -EOPNOTSUPP If cmd is not supported.
*
*/
/*----------------------------------------------------------------------------*/
static int
priv_set_ndis (
    IN struct net_device *prNetDev,
    IN NDIS_TRANSPORT_STRUCT* prNdisReq,
    OUT PUINT_32 pu4OutputLen
    )
{
    P_WLAN_REQ_ENTRY prWlanReqEntry = NULL;
    WLAN_STATUS status = WLAN_STATUS_SUCCESS;
    P_GLUE_INFO_T prGlueInfo = NULL;
    UINT_32             u4SetInfoLen = 0;

    ASSERT(prNetDev);
    ASSERT(prNdisReq);
    ASSERT(pu4OutputLen);

    if (!prNetDev || !prNdisReq || !pu4OutputLen) {
        DBGLOG(REQ, INFO, ("priv_set_ndis(): invalid param(0x%p, 0x%p, 0x%p)\n",
            prNetDev, prNdisReq, pu4OutputLen));
        return -EINVAL;
    }

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));
    ASSERT(prGlueInfo);
    if (!prGlueInfo) {
        DBGLOG(REQ, INFO, ("priv_set_ndis(): invalid prGlueInfo(0x%p, 0x%p)\n",
            prNetDev, *((P_GLUE_INFO_T *) netdev_priv(prNetDev))));
        return -EINVAL;
    }

#if 0
    printk(KERN_INFO DRV_NAME"priv_set_ndis(): prNdisReq->ndisOidCmd(0x%lX)\n",
        prNdisReq->ndisOidCmd
        );
#endif

    if (FALSE == reqSearchSupportedOidEntry(prNdisReq->ndisOidCmd,
                                            &prWlanReqEntry)) {
        //WARNLOG(("Set OID: 0x%08lx (unknown)\n", prNdisReq->ndisOidCmd));
        return -EOPNOTSUPP;
    }

    if (NULL == prWlanReqEntry->pfOidSetHandler) {
        //WARNLOG(("Set %s: Null set handler\n", prWlanReqEntry->pucOidName));
        return -EOPNOTSUPP;
    }

#if 0
    printk(KERN_INFO DRV_NAME"priv_set_ndis(): %s\n",
        prWlanReqEntry->pucOidName
        );
#endif

    if (prWlanReqEntry->fgSetBufLenChecking) {
        if (prNdisReq->inNdisOidlength != prWlanReqEntry->u4InfoBufLen) {
            DBGLOG(REQ, WARN, ("Set %s: Invalid length (current=%ld, needed=%ld)\n",
                prWlanReqEntry->pucOidName,
                prNdisReq->inNdisOidlength,
                prWlanReqEntry->u4InfoBufLen));

            *pu4OutputLen = prWlanReqEntry->u4InfoBufLen;
            return -EINVAL;
        }
    }

    if (prWlanReqEntry->eOidMethod == ENUM_OID_GLUE_ONLY) {
        /* GLUE sw info only */
        status = prWlanReqEntry->pfOidSetHandler(prGlueInfo,
                                                    prNdisReq->ndisOidContent,
                                                    prNdisReq->inNdisOidlength,
                                                    &u4SetInfoLen);
    }
    else if (prWlanReqEntry->eOidMethod == ENUM_OID_GLUE_EXTENSION) {
        /* multiple sw operations */
        status = prWlanReqEntry->pfOidSetHandler(prGlueInfo,
                                                    prNdisReq->ndisOidContent,
                                                    prNdisReq->inNdisOidlength,
                                                    &u4SetInfoLen);
    }
    else if (prWlanReqEntry->eOidMethod == ENUM_OID_DRIVER_CORE) {
        /* driver core*/

        status = kalIoctl(prGlueInfo,
            (PFN_OID_HANDLER_FUNC)prWlanReqEntry->pfOidSetHandler,
            prNdisReq->ndisOidContent,
            prNdisReq->inNdisOidlength,
            FALSE,
            FALSE,
            TRUE,
            FALSE,
            &u4SetInfoLen);
    }
    else {
        DBGLOG(REQ, INFO, ("priv_set_ndis(): unsupported OID method:0x%x\n",
                    prWlanReqEntry->eOidMethod));
        return -EOPNOTSUPP;
    }

    *pu4OutputLen = u4SetInfoLen;

    switch (status) {
    case WLAN_STATUS_SUCCESS:
        break;

    case WLAN_STATUS_INVALID_LENGTH:
        //WARNLOG(("Set %s: Invalid length (current=%ld, needed=%ld)\n",
           // prWlanReqEntry->pucOidName,
            //prNdisReq->inNdisOidlength,
            //u4SetInfoLen));
        break;
    }

    if (WLAN_STATUS_SUCCESS != status) {
        return -EFAULT;
    }

    return 0;
} /* priv_set_ndis */

/*----------------------------------------------------------------------------*/
/*!
* \brief The routine handles a query operation for a single OID. Basically we
*   return information about the current state of the OID in question.
*
* \param[in] pDev Net device requested.
* \param[in] ndisReq Ndis request OID information copy from user.
* \param[out] outputLen_p If the call is successful, returns the number of
*                        bytes written into the query buffer. If the
*                        call failed due to invalid length of the query
*                        buffer, returns the amount of storage needed..
*
* \retval 0 On success.
* \retval -EOPNOTSUPP If cmd is not supported.
* \retval -EINVAL invalid input parameters
*
*/
/*----------------------------------------------------------------------------*/
static int
priv_get_ndis (
    IN struct net_device *prNetDev,
    IN NDIS_TRANSPORT_STRUCT* prNdisReq,
    OUT PUINT_32 pu4OutputLen
    )
{
    P_WLAN_REQ_ENTRY prWlanReqEntry = NULL;
    UINT_32 u4BufLen = 0;
    WLAN_STATUS status = WLAN_STATUS_SUCCESS;
    P_GLUE_INFO_T prGlueInfo = NULL;

    ASSERT(prNetDev);
    ASSERT(prNdisReq);
    ASSERT(pu4OutputLen);

    if (!prNetDev || !prNdisReq || !pu4OutputLen) {
        DBGLOG(REQ, INFO, ("priv_get_ndis(): invalid param(0x%p, 0x%p, 0x%p)\n",
            prNetDev, prNdisReq, pu4OutputLen));
        return -EINVAL;
    }

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));
    ASSERT(prGlueInfo);
    if (!prGlueInfo) {
        DBGLOG(REQ, INFO, ("priv_get_ndis(): invalid prGlueInfo(0x%p, 0x%p)\n",
            prNetDev, *((P_GLUE_INFO_T *) netdev_priv(prNetDev))));
        return -EINVAL;
    }

#if 0
    printk(KERN_INFO DRV_NAME"priv_get_ndis(): prNdisReq->ndisOidCmd(0x%lX)\n",
        prNdisReq->ndisOidCmd
        );
#endif

    if (FALSE == reqSearchSupportedOidEntry(prNdisReq->ndisOidCmd,
                                            &prWlanReqEntry)) {
        //WARNLOG(("Query OID: 0x%08lx (unknown)\n", prNdisReq->ndisOidCmd));
        return -EOPNOTSUPP;
    }


    if (NULL == prWlanReqEntry->pfOidQueryHandler) {
        //WARNLOG(("Query %s: Null query handler\n", prWlanReqEntry->pucOidName));
        return -EOPNOTSUPP;
    }

#if 0
    printk(KERN_INFO DRV_NAME"priv_get_ndis(): %s\n",
        prWlanReqEntry->pucOidName
        );
#endif

    if (prWlanReqEntry->fgQryBufLenChecking) {
        if (prNdisReq->inNdisOidlength < prWlanReqEntry->u4InfoBufLen) {
            /* Not enough room in InformationBuffer. Punt */
            //WARNLOG(("Query %s: Buffer too short (current=%ld, needed=%ld)\n",
                //prWlanReqEntry->pucOidName,
                //prNdisReq->inNdisOidlength,
                //prWlanReqEntry->u4InfoBufLen));

            *pu4OutputLen = prWlanReqEntry->u4InfoBufLen;

            status = WLAN_STATUS_INVALID_LENGTH;
            return -EINVAL;
        }
    }


    if (prWlanReqEntry->eOidMethod == ENUM_OID_GLUE_ONLY) {
        /* GLUE sw info only */
        status = prWlanReqEntry->pfOidQueryHandler(prGlueInfo,
                                                    prNdisReq->ndisOidContent,
                                                    prNdisReq->inNdisOidlength,
                                                    &u4BufLen);
    }
    else if (prWlanReqEntry->eOidMethod == ENUM_OID_GLUE_EXTENSION) {
        /* multiple sw operations */
        status = prWlanReqEntry->pfOidQueryHandler(prGlueInfo,
                                                    prNdisReq->ndisOidContent,
                                                    prNdisReq->inNdisOidlength,
                                                    &u4BufLen);
    }
    else if (prWlanReqEntry->eOidMethod == ENUM_OID_DRIVER_CORE) {
        /* driver core*/

        status = kalIoctl(prGlueInfo,
            (PFN_OID_HANDLER_FUNC)prWlanReqEntry->pfOidQueryHandler,
            prNdisReq->ndisOidContent,
            prNdisReq->inNdisOidlength,
            TRUE,
            TRUE,
            TRUE,
            FALSE,
            &u4BufLen);
    }
    else {
        DBGLOG(REQ, INFO, ("priv_set_ndis(): unsupported OID method:0x%x\n",
                    prWlanReqEntry->eOidMethod));
        return -EOPNOTSUPP;
    }

    *pu4OutputLen = u4BufLen;

    switch (status) {
    case WLAN_STATUS_SUCCESS:
        break;

    case WLAN_STATUS_INVALID_LENGTH:
        //WARNLOG(("Set %s: Invalid length (current=%ld, needed=%ld)\n",
           // prWlanReqEntry->pucOidName,
            //prNdisReq->inNdisOidlength,
            //u4BufLen));
        break;
    }

    if (WLAN_STATUS_SUCCESS != status) {
        return -EOPNOTSUPP;
    }

    return 0;
} /* priv_get_ndis */

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to search desired OID.
*
* \param rOid[in]               Desired NDIS_OID
* \param ppWlanReqEntry[out]    Found registered OID entry
*
* \retval TRUE: Matched OID is found
* \retval FALSE: No matched OID is found
*/
/*----------------------------------------------------------------------------*/
static BOOLEAN
reqSearchSupportedOidEntry (
    IN  UINT_32            rOid,
    OUT P_WLAN_REQ_ENTRY    *ppWlanReqEntry
    )
{
    INT_32 i, j, k;

    i = 0;
    j = NUM_SUPPORTED_OIDS - 1;

    while (i <= j) {
        k = (i + j) / 2;

        if (rOid == arWlanOidReqTable[k].rOid) {
            *ppWlanReqEntry = &arWlanOidReqTable[k];
            return TRUE;
        } else if (rOid < arWlanOidReqTable[k].rOid) {
            j = k - 1;
        } else {
            i = k + 1;
        }
    }

    return FALSE;
}   /* reqSearchSupportedOidEntry */

#if 0
/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to query the radio configuration used in IBSS
*        mode and RF test mode.
*
* \param[in] prGlueInfo         Pointer to the GLUE_INFO_T structure.
* \param[out] pvQueryBuffer     Pointer to the buffer that holds the result of the query.
* \param[in] u4QueryBufferLen   The length of the query buffer.
* \param[out] pu4QueryInfoLen   If the call is successful, returns the number of
*                               bytes written into the query buffer. If the call
*                               failed due to invalid length of the query buffer,
*                               returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
static WLAN_STATUS
reqExtQueryConfiguration (
    IN  P_GLUE_INFO_T   prGlueInfo,
    OUT PVOID           pvQueryBuffer,
    IN UINT_32          u4QueryBufferLen,
    OUT PUINT_32        pu4QueryInfoLen
    )
{
    P_PARAM_802_11_CONFIG_T prQueryConfig = (P_PARAM_802_11_CONFIG_T)pvQueryBuffer;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4QueryInfoLen = 0;

    DEBUGFUNC("wlanoidQueryConfiguration");


    ASSERT(prGlueInfo);
    ASSERT(pu4QueryInfoLen);

    *pu4QueryInfoLen = sizeof(PARAM_802_11_CONFIG_T);
    if (u4QueryBufferLen < sizeof(PARAM_802_11_CONFIG_T)) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    ASSERT(pvQueryBuffer);

    kalMemZero(prQueryConfig, sizeof(PARAM_802_11_CONFIG_T));

    /* Update the current radio configuration. */
    prQueryConfig->u4Length = sizeof(PARAM_802_11_CONFIG_T);

#if defined(_HIF_SDIO)
    rStatus = sdio_io_ctrl(prGlueInfo,
                            wlanoidSetBeaconInterval,
                            &prQueryConfig->u4BeaconPeriod,
                            sizeof(UINT_32),
                            TRUE,
                            TRUE,
                            &u4QueryInfoLen);
#else
    rStatus = wlanQueryInformation(prGlueInfo->prAdapter,
                                   wlanoidQueryBeaconInterval,
                                   &prQueryConfig->u4BeaconPeriod,
                                   sizeof(UINT_32),
                                   &u4QueryInfoLen);
#endif
    if (rStatus != WLAN_STATUS_SUCCESS) {
        return rStatus;
    }

#if defined(_HIF_SDIO)
    rStatus = sdio_io_ctrl(prGlueInfo,
                            wlanoidQueryAtimWindow,
                            &prQueryConfig->u4ATIMWindow,
                            sizeof(UINT_32),
                            TRUE,
                            TRUE,
                            &u4QueryInfoLen);
#else
    rStatus = wlanQueryInformation(prGlueInfo->prAdapter,
                                   wlanoidQueryAtimWindow,
                                   &prQueryConfig->u4ATIMWindow,
                                   sizeof(UINT_32),
                                   &u4QueryInfoLen);
#endif
    if (rStatus != WLAN_STATUS_SUCCESS) {
        return rStatus;
    }

#if defined(_HIF_SDIO)
    rStatus = sdio_io_ctrl(prGlueInfo,
                            wlanoidQueryFrequency,
                            &prQueryConfig->u4DSConfig,
                            sizeof(UINT_32),
                            TRUE,
                            TRUE,
                            &u4QueryInfoLen);
#else
    rStatus = wlanQueryInformation(prGlueInfo->prAdapter,
                                   wlanoidQueryFrequency,
                                   &prQueryConfig->u4DSConfig,
                                   sizeof(UINT_32),
                                   &u4QueryInfoLen);
#endif
    if (rStatus != WLAN_STATUS_SUCCESS) {
        return rStatus;
    }

    prQueryConfig->rFHConfig.u4Length = sizeof(PARAM_802_11_CONFIG_FH_T);

    return rStatus;

} /* end of reqExtQueryConfiguration() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set the radio configuration used in IBSS
*        mode.
*
* \param[in] prGlueInfo     Pointer to the GLUE_INFO_T structure.
* \param[in] pvSetBuffer    A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_NOT_ACCEPTED
*/
/*----------------------------------------------------------------------------*/
static WLAN_STATUS
reqExtSetConfiguration (
    IN  P_GLUE_INFO_T prGlueInfo,
    IN  PVOID         pvSetBuffer,
    IN  UINT_32       u4SetBufferLen,
    OUT PUINT_32      pu4SetInfoLen
    )
{
    WLAN_STATUS  rStatus = WLAN_STATUS_SUCCESS;
    P_PARAM_802_11_CONFIG_T prNewConfig = (P_PARAM_802_11_CONFIG_T)pvSetBuffer;
    UINT_32 u4SetInfoLen = 0;

    DEBUGFUNC("wlanoidSetConfiguration");


    ASSERT(prGlueInfo);
    ASSERT(pu4SetInfoLen);

    *pu4SetInfoLen = sizeof(PARAM_802_11_CONFIG_T);

    if (u4SetBufferLen < *pu4SetInfoLen) {
        return WLAN_STATUS_INVALID_LENGTH;
    }

    /* OID_802_11_CONFIGURATION. If associated, NOT_ACCEPTED shall be returned. */
    if (prGlueInfo->eParamMediaStateIndicated == PARAM_MEDIA_STATE_CONNECTED) {
        return WLAN_STATUS_NOT_ACCEPTED;
    }

    ASSERT(pvSetBuffer);

#if defined(_HIF_SDIO)
    rStatus = sdio_io_ctrl(prGlueInfo,
                            wlanoidSetBeaconInterval,
                            &prNewConfig->u4BeaconPeriod,
                            sizeof(UINT_32),
                            FALSE,
                            TRUE,
                            &u4SetInfoLen);
#else
    rStatus = wlanSetInformation(prGlueInfo->prAdapter,
                                 wlanoidSetBeaconInterval,
                                 &prNewConfig->u4BeaconPeriod,
                                 sizeof(UINT_32),
                                 &u4SetInfoLen);
#endif
    if (rStatus != WLAN_STATUS_SUCCESS) {
        return rStatus;
    }

#if defined(_HIF_SDIO)
    rStatus = sdio_io_ctrl(prGlueInfo,
                            wlanoidSetAtimWindow,
                             &prNewConfig->u4ATIMWindow,
                            sizeof(UINT_32),
                            FALSE,
                            TRUE,
                            &u4SetInfoLen);
#else
    rStatus = wlanSetInformation(prGlueInfo->prAdapter,
                                 wlanoidSetAtimWindow,
                                 &prNewConfig->u4ATIMWindow,
                                 sizeof(UINT_32),
                                 &u4SetInfoLen);
#endif
    if (rStatus != WLAN_STATUS_SUCCESS) {
        return rStatus;
    }

#if defined(_HIF_SDIO)
    rStatus = sdio_io_ctrl(prGlueInfo,
                            wlanoidSetFrequency,
                            &prNewConfig->u4DSConfig,
                            sizeof(UINT_32),
                            FALSE,
                            TRUE,
                            &u4SetInfoLen);
#else
    rStatus = wlanSetInformation(prGlueInfo->prAdapter,
                                 wlanoidSetFrequency,
                                 &prNewConfig->u4DSConfig,
                                 sizeof(UINT_32),
                                 &u4SetInfoLen);
#endif

    if (rStatus != WLAN_STATUS_SUCCESS) {
        return rStatus;
    }

    return rStatus;

} /* end of reqExtSetConfiguration() */

#endif
/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set beacon detection function enable/disable state
*        This is mainly designed for usage under BT inquiry state (disable function).
*
* \param[in] pvAdapter Pointer to the Adapter structure
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen The length of the set buffer
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*   bytes read from the set buffer. If the call failed due to invalid length of
*   the set buffer, returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_DATA If new setting value is wrong.
* \retval WLAN_STATUS_INVALID_LENGTH
*
*/
/*----------------------------------------------------------------------------*/
static WLAN_STATUS
reqExtSetAcpiDevicePowerState (
    IN  P_GLUE_INFO_T prGlueInfo,
    IN  PVOID         pvSetBuffer,
    IN  UINT_32       u4SetBufferLen,
    OUT PUINT_32      pu4SetInfoLen
    )
{
    WLAN_STATUS  rStatus = WLAN_STATUS_SUCCESS;

    ASSERT(prGlueInfo);
    ASSERT(pvSetBuffer);
    ASSERT(pu4SetInfoLen);

    /* WIFI is enabled, when ACPI is D0 (ParamDeviceStateD0 = 1). And vice versa */

    //rStatus = wlanSetInformation(prGlueInfo->prAdapter,
    //                   wlanoidSetAcpiDevicePowerState,
    //                   pvSetBuffer,
    //                   u4SetBufferLen,
    //                   pu4SetInfoLen);
    return rStatus;
}

