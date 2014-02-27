/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/os/linux/include/gl_wext_priv.h#3 $
*/

/*! \file   gl_wext_priv.h
    \brief  This file includes private ioctl support.
*/



/*
** $Log: gl_wext_priv.h $
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
 * 11 08 2011 yuche.tsai
 * [WCXRP00001094] [Volunteer Patch][Driver] Driver version & supplicant version query & set support for service discovery version check.
 * Add a CMD ID for P2P driver version query.
 *
 * 03 17 2011 chinglan.wang
 * [WCXRP00000570] [MT6620 Wi-Fi][Driver] Add Wi-Fi Protected Setup v2.0 feature
 * .
 *
 * 03 02 2011 wh.su
 * [WCXRP00000506] [MT6620 Wi-Fi][Driver][FW] Add Security check related code
 * Add security check code.
 *
 * 01 27 2011 cm.chang
 * [WCXRP00000402] [MT6620 Wi-Fi][Driver] Enable MCR read/write by iwpriv by default
 * .
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
 * 11 08 2010 wh.su
 * [WCXRP00000171] [MT6620 Wi-Fi][Driver] Add message check code same behavior as mt5921
 * add the message check code from mt5921.
 *
 * 10 18 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check[WCXRP00000086] [MT6620 Wi-Fi][Driver] The mac address is all zero at android
 * complete implementation of Android NVRAM access
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
 * 03 31 2010 wh.su
 * [WPD00003816][MT6620 Wi-Fi] Adding the security support
 * modify the wapi related code for new driver's design.
 *
 * 03 24 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * initial import for Linux port
**  \main\maintrunk.MT5921\16 2009-09-29 16:47:23 GMT mtk01090
**  Remove unused functions
**  \main\maintrunk.MT5921\15 2009-09-28 20:19:31 GMT mtk01090
**  Add private ioctl to carry OID structures. Restructure public/private ioctl interfaces to Linux kernel.
**  \main\maintrunk.MT5921\14 2009-05-07 22:26:06 GMT mtk01089
**  add private IO control for Linux BWCS
**  \main\maintrunk.MT5921\13 2008-08-29 14:55:20 GMT mtk01088
**  adjust the code to meet coding style
**  \main\maintrunk.MT5921\12 2008-07-16 15:23:45 GMT mtk01104
**  Support GPIO2 mode
**  \main\maintrunk.MT5921\11 2008-07-14 13:55:58 GMT mtk01104
**  Support PRIV_CMD_BT_COEXIST
**  \main\maintrunk.MT5921\10 2008-07-09 00:20:24 GMT mtk01461
**  Add priv oid to support WMM_PS_TEST
**  \main\maintrunk.MT5921\9 2008-05-30 20:27:24 GMT mtk01461
**  Add POWER_MODE Private IOCTL cmd
**  \main\maintrunk.MT5921\8 2008-04-17 23:06:44 GMT mtk01461
**  Add iwpriv support for AdHocMode setting
**  \main\maintrunk.MT5921\7 2008-03-31 21:01:24 GMT mtk01461
**  Add priv IOCTL for VOIP settings
**  \main\maintrunk.MT5921\6 2008-03-31 13:49:47 GMT mtk01461
**  add priv ioctl arg definition for turning on / off roaming
**  \main\maintrunk.MT5921\5 2008-03-26 15:35:09 GMT mtk01461
**  Add CSUM offload priv ioctl for Linux
**  \main\maintrunk.MT5921\4 2008-03-11 14:51:11 GMT mtk01461
**  Refine private IOCTL functions
**  \main\maintrunk.MT5921\3 2007-11-06 19:36:25 GMT mtk01088
**  add the WPS related code
*/

#ifndef _GL_WEXT_PRIV_H
#define _GL_WEXT_PRIV_H
/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/
/* If it is set to 1, iwpriv will support register read/write */
#define CFG_SUPPORT_PRIV_MCR_RW         1

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
/* New wireless extensions API - SET/GET convention (even ioctl numbers are
 * root only)
 */
#define IOCTL_SET_INT                   (SIOCIWFIRSTPRIV + 0)
#define IOCTL_GET_INT                   (SIOCIWFIRSTPRIV + 1)

#define IOCTL_SET_ADDRESS               (SIOCIWFIRSTPRIV + 2)
#define IOCTL_GET_ADDRESS               (SIOCIWFIRSTPRIV + 3)
#define IOCTL_SET_STR                   (SIOCIWFIRSTPRIV + 4)
#define IOCTL_GET_STR                   (SIOCIWFIRSTPRIV + 5)
#define IOCTL_SET_KEY                   (SIOCIWFIRSTPRIV + 6)
#define IOCTL_GET_KEY                   (SIOCIWFIRSTPRIV + 7)
#define IOCTL_SET_STRUCT                (SIOCIWFIRSTPRIV + 8)
#define IOCTL_GET_STRUCT                (SIOCIWFIRSTPRIV + 9)
#define IOCTL_SET_STRUCT_FOR_EM         (SIOCIWFIRSTPRIV + 11)
#define IOCTL_SET_INTS                  (SIOCIWFIRSTPRIV + 12)
#define IOCTL_GET_INTS                  (SIOCIWFIRSTPRIV + 13)

#define PRIV_CMD_REG_DOMAIN             0
#define PRIV_CMD_BEACON_PERIOD          1
#define PRIV_CMD_ADHOC_MODE             2

#if CFG_TCP_IP_CHKSUM_OFFLOAD
    #define PRIV_CMD_CSUM_OFFLOAD       3
#endif /* CFG_TCP_IP_CHKSUM_OFFLOAD */

#define PRIV_CMD_ROAMING                4
#define PRIV_CMD_VOIP_DELAY             5
#define PRIV_CMD_POWER_MODE             6

#define PRIV_CMD_WMM_PS                 7
#define PRIV_CMD_BT_COEXIST             8
#define PRIV_GPIO2_MODE                 9

#define PRIV_CUSTOM_SET_PTA        		10
#define PRIV_CUSTOM_CONTINUOUS_POLL     11
#define PRIV_CUSTOM_SINGLE_ANTENNA		12
#define PRIV_CUSTOM_BWCS_CMD			13
#define PRIV_CUSTOM_DISABLE_BEACON_DETECTION	14//later
#define PRIV_CMD_OID                    15
#define PRIV_SEC_MSG_OID                16

#define PRIV_CMD_TEST_MODE              17
#define PRIV_CMD_TEST_CMD               18
#define PRIV_CMD_ACCESS_MCR             19
#define PRIV_CMD_SW_CTRL                20

#if 1 /* ANTI_PRIVCY */
#define PRIV_SEC_CHECK_OID              21
#endif

#define PRIV_CMD_WSC_PROBE_REQ          22

#define PRIV_CMD_P2P_VERSION                   23

#define PRIV_CMD_GET_CH_LIST            24

#define PRIV_CMD_SET_TX_POWER           25

#define PRIV_CMD_BAND_CONFIG            26

#define PRIV_CMD_DUMP_MEM               27

#define PRIV_CMD_P2P_MODE               28

#define PRIV_CMD_GET_BUILD_DATE_CODE    29

/* 802.3 Objects (Ethernet) */
#define OID_802_3_CURRENT_ADDRESS           0x01010102

/* IEEE 802.11 OIDs */
#define OID_802_11_SUPPORTED_RATES              0x0D01020E
#define OID_802_11_CONFIGURATION                0x0D010211

/* PnP and PM OIDs, NDIS default OIDS */
#define OID_PNP_SET_POWER                               0xFD010101

#define OID_CUSTOM_OID_INTERFACE_VERSION                0xFFA0C000

/* MT5921 specific OIDs */
#define OID_CUSTOM_BT_COEXIST_CTRL                      0xFFA0C580
#define OID_CUSTOM_POWER_MANAGEMENT_PROFILE             0xFFA0C581
#define OID_CUSTOM_PATTERN_CONFIG                       0xFFA0C582
#define OID_CUSTOM_BG_SSID_SEARCH_CONFIG                0xFFA0C583
#define OID_CUSTOM_VOIP_SETUP                           0xFFA0C584
#define OID_CUSTOM_ADD_TS                               0xFFA0C585
#define OID_CUSTOM_DEL_TS                               0xFFA0C586
#define OID_CUSTOM_SLT                               0xFFA0C587
#define OID_CUSTOM_ROAMING_EN                           0xFFA0C588
#define OID_CUSTOM_WMM_PS_TEST                          0xFFA0C589
#define OID_CUSTOM_COUNTRY_STRING                       0xFFA0C58A
#define OID_CUSTOM_MULTI_DOMAIN_CAPABILITY              0xFFA0C58B
#define OID_CUSTOM_GPIO2_MODE                           0xFFA0C58C
#define OID_CUSTOM_CONTINUOUS_POLL                      0xFFA0C58D
#define OID_CUSTOM_DISABLE_BEACON_DETECTION             0xFFA0C58E

/* CR1460, WPS privacy bit check disable */
#define OID_CUSTOM_DISABLE_PRIVACY_CHECK                0xFFA0C600

/* Precedent OIDs */
#define OID_CUSTOM_MCR_RW                               0xFFA0C801
#define OID_CUSTOM_EEPROM_RW                            0xFFA0C803
#define OID_CUSTOM_SW_CTRL                              0xFFA0C805
#define OID_CUSTOM_MEM_DUMP                             0xFFA0C807


/* RF Test specific OIDs */
#define OID_CUSTOM_TEST_MODE                            0xFFA0C901
#define OID_CUSTOM_TEST_RX_STATUS                       0xFFA0C903
#define OID_CUSTOM_TEST_TX_STATUS                       0xFFA0C905
#define OID_CUSTOM_ABORT_TEST_MODE                      0xFFA0C906
#define OID_CUSTOM_MTK_WIFI_TEST                        0xFFA0C911

/* BWCS */
#define OID_CUSTOM_BWCS_CMD                             0xFFA0C931
#define OID_CUSTOM_SINGLE_ANTENNA                       0xFFA0C932
#define OID_CUSTOM_SET_PTA                              0xFFA0C933

/* NVRAM */
#define OID_CUSTOM_MTK_NVRAM_RW                         0xFFA0C941
#define OID_CUSTOM_CFG_SRC_TYPE                         0xFFA0C942
#define OID_CUSTOM_EEPROM_TYPE                          0xFFA0C943


#if CFG_SUPPORT_WAPI
#define OID_802_11_WAPI_MODE                            0xFFA0CA00
#define OID_802_11_WAPI_ASSOC_INFO                      0xFFA0CA01
#define OID_802_11_SET_WAPI_KEY                         0xFFA0CA02
#endif

#if CFG_SUPPORT_WPS2
#define OID_802_11_WSC_ASSOC_INFO                       0xFFA0CB00
#endif


/* Define magic key of test mode (Don't change it for future compatibity) */
#define PRIV_CMD_TEST_MAGIC_KEY                         2011

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
/* NIC BBCR configuration entry structure */
typedef struct _PRIV_CONFIG_ENTRY {
    UINT_8      ucOffset;
    UINT_8      ucValue;
} PRIV_CONFIG_ENTRY, *PPRIV_CONFIG_ENTRY;

typedef WLAN_STATUS (*PFN_OID_HANDLER_FUNC_REQ) (
    IN  PVOID       prAdapter,
    IN OUT PVOID    pvBuf,
    IN  UINT_32     u4BufLen,
    OUT PUINT_32    pu4OutInfoLen
    );

typedef enum _ENUM_OID_METHOD_T {
    ENUM_OID_GLUE_ONLY,
    ENUM_OID_GLUE_EXTENSION,
    ENUM_OID_DRIVER_CORE
} ENUM_OID_METHOD_T, *P_ENUM_OID_METHOD_T;

/* OID set/query processing entry */
typedef struct _WLAN_REQ_ENTRY {
    UINT_32            rOid;            /* OID */
    PUINT_8             pucOidName;      /* OID name text */
    BOOLEAN             fgQryBufLenChecking;
    BOOLEAN             fgSetBufLenChecking;
    ENUM_OID_METHOD_T   eOidMethod;
    UINT_32             u4InfoBufLen;
    PFN_OID_HANDLER_FUNC_REQ    pfOidQueryHandler; /*  PFN_OID_HANDLER_FUNC*/
    PFN_OID_HANDLER_FUNC_REQ    pfOidSetHandler; /* PFN_OID_HANDLER_FUNC */
} WLAN_REQ_ENTRY, *P_WLAN_REQ_ENTRY;

typedef struct _NDIS_TRANSPORT_STRUCT {
    UINT_32 ndisOidCmd;
    UINT_32 inNdisOidlength;
    UINT_32 outNdisOidLength;
    UINT_8 ndisOidContent[16];
} NDIS_TRANSPORT_STRUCT, *P_NDIS_TRANSPORT_STRUCT;

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
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

int
priv_set_int(
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwReqInfo,
    IN union iwreq_data *prIwReqData,
    IN char *pcExtra
    );

int
priv_get_int(
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwReqInfo,
    IN union iwreq_data *prIwReqData,
    IN OUT char *pcExtra
    );

int
priv_set_ints(
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwReqInfo,
    IN union iwreq_data *prIwReqData,
    IN char *pcExtra
    );

int
priv_get_ints(
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwReqInfo,
    IN union iwreq_data *prIwReqData,
    IN OUT char *pcExtra
    );

int
priv_set_struct(
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwReqInfo,
    IN union iwreq_data *prIwReqData,
    IN char *pcExtra
    );

int
priv_get_struct (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwReqInfo,
    IN union iwreq_data *prIwReqData,
    IN OUT char *pcExtra
    );

int
priv_support_ioctl (
    IN struct net_device *prDev,
    IN OUT struct ifreq *prReq,
    IN int i4Cmd
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _GL_WEXT_PRIV_H */

