/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/include/nic/wlan_def.h#1 $
*/

/*! \file   "wlan_def.h"
    \brief  This file includes the basic definition of WLAN

*/



/*
** $Log: wlan_def.h $
 *
 * 12 05 2011 cp.wu
 * [WCXRP00001131] [MT6620 Wi-Fi][Driver][AIS] Implement connect-by-BSSID path
 * add CONNECT_BY_BSSID policy
 *
 * 10 12 2011 wh.su
 * [WCXRP00001036] [MT6620 Wi-Fi][Driver][FW] Adding the 802.11w code for MFP
 * adding the 802.11w related function and define .
 *
 * 06 22 2011 wh.su
 * [WCXRP00000806] [MT6620 Wi-Fi][Driver] Move the WPA/RSN IE and WAPI IE structure to mac.h and let the sw structure not align at byte
 * Move the WAPI/RSN IE to mac.h and SW structure not align to byte, 
 * Notice needed update P2P.ko.
 *
 * 04 08 2011 eddie.chen
 * [WCXRP00000617] [MT6620 Wi-Fi][DRV/FW] Fix for sigma
 * Fix for sigma
 *
 * 03 17 2011 yuche.tsai
 * NULL
 * Resize the Secondary Device Type array when WiFi Direct is enabled.
 *
 * 01 25 2011 yuche.tsai
 * [WCXRP00000388] [Volunteer Patch][MT6620][Driver/Fw] change Station Type in station record.
 * Change Station Type in Station Record, Modify MACRO definition for getting station type & network type index & Role.
 *
 * 01 25 2011 yuche.tsai
 * [WCXRP00000388] [Volunteer Patch][MT6620][Driver/Fw] change Station Type in station record.
 * Add new station type MACRO.
 *
 * 12 07 2010 cm.chang
 * [WCXRP00000238] MT6620 Wi-Fi][Driver][FW] Support regulation domain setting from NVRAM and supplicant
 * 1. Country code is from NVRAM or supplicant
 * 2. Change band definition in CMD/EVENT.
 *
 * 10 11 2010 kevin.huang
 * [WCXRP00000068] [MT6620 Wi-Fi][Driver][FW] Fix STA RECORD sync issue and remove unused code
 * Update ENUM_STA_ROLE_INDEX_T by using a fixed base value
 *
 * 10 04 2010 cp.wu
 * [WCXRP00000077] [MT6620 Wi-Fi][Driver][FW] Eliminate use of ENUM_NETWORK_TYPE_T and replaced by ENUM_NETWORK_TYPE_INDEX_T only
 * remove ENUM_NETWORK_TYPE_T definitions
 *
 * 09 14 2010 chinghwa.yu
 * NULL
 * Update OP_MODE_BOW and include bow_fsm.h.
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 08 31 2010 kevin.huang
 * NULL
 * Use LINK LIST operation to process SCAN result
 *
 * 08 29 2010 yuche.tsai
 * NULL
 * Change P2P Descriptor List to a pointer and allocate it dynamically to avoid structure corrupt by BssDescriptor free.
 *
 * 08 16 2010 kevin.huang
 * NULL
 * Refine AAA functions
 *
 * 08 12 2010 kevin.huang
 * NULL
 * Refine bssProcessProbeRequest() and bssSendBeaconProbeResponse()
 *
 * 08 12 2010 yuche.tsai
 * NULL
 * Add a pointer in BSS Descriptor for P2P Descriptor.
 *
 * 08 11 2010 yuche.tsai
 * NULL
 * Add an Interface in BSS Descriptor.
 *
 * 08 05 2010 yuche.tsai
 * NULL
 * Modify data structure for P2P Scan result.
 *
 * 07 26 2010 yuche.tsai
 *
 * Add an operation mode for P2P device.
 *
 * 07 23 2010 cp.wu
 *
 * P2P/RSN/WAPI IEs need to be declared with compact structure.
 *
 * 07 21 2010 yuche.tsai
 *
 * Add for P2P Scan Result Parsing & Saving.
 *
 * 07 20 2010 wh.su
 *
 * adding the wapi code.
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
 * 06 28 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * 1st draft code for RLM module
 *
 * 06 25 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * modify Beacon/ProbeResp to complete parsing,
 * because host software has looser memory usage restriction
 *
 * 06 21 2010 yuche.tsai
 * [WPD00003839][MT6620 5931][P2P] Feature migration
 * Add P2P present boolean flag in BSS & Pre-BSS descriptor.
 *
 * 06 18 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * migration the security related function from firmware.
 *
 * 06 11 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * auth.c is migrated.
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
 * add buildable & linkable ais_fsm.c
 *
 * related reference are still waiting to be resolved
 *
 * 06 09 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add definitions for module migration.
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * move bss related data types to wlan_def.h to avoid recursive dependency.
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * merge wlan_def.h.
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * merge cnm_scan.h and hem_mbox.h
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * merge wifi_var.h, precomp.h, cnm_timer.h (data type only)
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-10 20:16:40 GMT mtk01426
**  Init for develop
**
*/

#ifndef _WLAN_DEF_H
#define _WLAN_DEF_H

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
/* disconnect reason */
#define DISCONNECT_REASON_CODE_RESERVED         0
#define DISCONNECT_REASON_CODE_RADIO_LOST       1
#define DISCONNECT_REASON_CODE_DEAUTHENTICATED  2
#define DISCONNECT_REASON_CODE_DISASSOCIATED    3
#define DISCONNECT_REASON_CODE_NEW_CONNECTION   4

/* The rate definitions */
#define TX_MODE_CCK             0x00
#define TX_MODE_OFDM            0x40
#define TX_MODE_HT_MM           0x80
#define TX_MODE_HT_GF           0xC0

#define RATE_CCK_SHORT_PREAMBLE 0x10
#define RATE_OFDM               0x20

#define PHY_RATE_1M             0x0
#define PHY_RATE_2M             0x1
#define PHY_RATE_5_5M           0x2
#define PHY_RATE_11M            0x3
#define PHY_RATE_6M             0xB
#define PHY_RATE_9M             0xF
#define PHY_RATE_12M            0xA
#define PHY_RATE_18M            0xE
#define PHY_RATE_24M            0x9
#define PHY_RATE_36M            0xD
#define PHY_RATE_48M            0x8
#define PHY_RATE_54M            0xC
#define PHY_RATE_MCS0           0x0
#define PHY_RATE_MCS1           0x1
#define PHY_RATE_MCS2           0x2
#define PHY_RATE_MCS3           0x3
#define PHY_RATE_MCS4           0x4
#define PHY_RATE_MCS5           0x5
#define PHY_RATE_MCS6           0x6
#define PHY_RATE_MCS7           0x7
#define PHY_RATE_MCS32          0x20

#define RATE_CCK_1M_LONG        (TX_MODE_CCK | PHY_RATE_1M)
#define RATE_CCK_2M_LONG        (TX_MODE_CCK | PHY_RATE_2M)
#define RATE_CCK_5_5M_LONG      (TX_MODE_CCK | PHY_RATE_5_5M)
#define RATE_CCK_11M_LONG       (TX_MODE_CCK | PHY_RATE_11M)
#define RATE_CCK_2M_SHORT       (TX_MODE_CCK | PHY_RATE_2M | RATE_CCK_SHORT_PREAMBLE)
#define RATE_CCK_5_5M_SHORT     (TX_MODE_CCK | PHY_RATE_5_5M | RATE_CCK_SHORT_PREAMBLE)
#define RATE_CCK_11M_SHORT      (TX_MODE_CCK | PHY_RATE_11M | RATE_CCK_SHORT_PREAMBLE)
#define RATE_OFDM_6M            (TX_MODE_OFDM | RATE_OFDM | PHY_RATE_6M)
#define RATE_OFDM_9M            (TX_MODE_OFDM | RATE_OFDM | PHY_RATE_9M)
#define RATE_OFDM_12M           (TX_MODE_OFDM | RATE_OFDM | PHY_RATE_12M)
#define RATE_OFDM_18M           (TX_MODE_OFDM | RATE_OFDM | PHY_RATE_18M)
#define RATE_OFDM_24M           (TX_MODE_OFDM | RATE_OFDM | PHY_RATE_24M)
#define RATE_OFDM_36M           (TX_MODE_OFDM | RATE_OFDM | PHY_RATE_36M)
#define RATE_OFDM_48M           (TX_MODE_OFDM | RATE_OFDM | PHY_RATE_48M)
#define RATE_OFDM_54M           (TX_MODE_OFDM | RATE_OFDM | PHY_RATE_54M)

#define RATE_MM_MCS_0           (TX_MODE_HT_MM | PHY_RATE_MCS0)
#define RATE_MM_MCS_1           (TX_MODE_HT_MM | PHY_RATE_MCS1)
#define RATE_MM_MCS_2           (TX_MODE_HT_MM | PHY_RATE_MCS2)
#define RATE_MM_MCS_3           (TX_MODE_HT_MM | PHY_RATE_MCS3)
#define RATE_MM_MCS_4           (TX_MODE_HT_MM | PHY_RATE_MCS4)
#define RATE_MM_MCS_5           (TX_MODE_HT_MM | PHY_RATE_MCS5)
#define RATE_MM_MCS_6           (TX_MODE_HT_MM | PHY_RATE_MCS6)
#define RATE_MM_MCS_7           (TX_MODE_HT_MM | PHY_RATE_MCS7)
#define RATE_MM_MCS_32          (TX_MODE_HT_MM | PHY_RATE_MCS32)

#define RATE_GF_MCS_0           (TX_MODE_HT_GF | PHY_RATE_MCS0)
#define RATE_GF_MCS_1           (TX_MODE_HT_GF | PHY_RATE_MCS1)
#define RATE_GF_MCS_2           (TX_MODE_HT_GF | PHY_RATE_MCS2)
#define RATE_GF_MCS_3           (TX_MODE_HT_GF | PHY_RATE_MCS3)
#define RATE_GF_MCS_4           (TX_MODE_HT_GF | PHY_RATE_MCS4)
#define RATE_GF_MCS_5           (TX_MODE_HT_GF | PHY_RATE_MCS5)
#define RATE_GF_MCS_6           (TX_MODE_HT_GF | PHY_RATE_MCS6)
#define RATE_GF_MCS_7           (TX_MODE_HT_GF | PHY_RATE_MCS7)
#define RATE_GF_MCS_32          (TX_MODE_HT_GF | PHY_RATE_MCS32)

#define RATE_TX_MODE_MASK                   BITS(6,7)
#define RATE_TX_MODE_OFFSET                 6
#define RATE_CODE_GET_TX_MODE(_ucRateCode)  ((_ucRateCode & RATE_TX_MODE_MASK) >> RATE_TX_MODE_OFFSET)
#define RATE_PHY_RATE_MASK                  BITS(0,5)
#define RATE_PHY_RATE_OFFSET                0
#define RATE_CODE_GET_PHY_RATE(_ucRateCode) ((_ucRateCode & RATE_PHY_RATE_MASK) >> RATE_PHY_RATE_OFFSET)
#define RATE_PHY_RATE_SHORT_PREAMBLE        BIT(4)
#define RATE_CODE_IS_SHORT_PREAMBLE(_ucRateCode)  ((_ucRateCode & RATE_PHY_RATE_SHORT_PREAMBLE)?TRUE:FALSE)


#define CHNL_LIST_SZ_2G         14
#define CHNL_LIST_SZ_5G         14

/*! CNM(STA_RECORD_T) related definition */
#define CFG_STA_REC_NUM         20

/* PHY TYPE bit definitions */
#define PHY_TYPE_BIT_HR_DSSS    BIT(PHY_TYPE_HR_DSSS_INDEX) /* HR/DSSS PHY (clause 18) */
#define PHY_TYPE_BIT_ERP        BIT(PHY_TYPE_ERP_INDEX)     /* ERP PHY (clause 19) */
#define PHY_TYPE_BIT_OFDM       BIT(PHY_TYPE_OFDM_INDEX)    /* OFDM 5 GHz PHY (clause 17) */
#define PHY_TYPE_BIT_HT         BIT(PHY_TYPE_HT_INDEX)      /* HT PHY (clause 20) */


/* PHY TYPE set definitions */
#define PHY_TYPE_SET_802_11ABGN (PHY_TYPE_BIT_OFDM | \
                                 PHY_TYPE_BIT_HR_DSSS | \
                                 PHY_TYPE_BIT_ERP | \
                                 PHY_TYPE_BIT_HT)

#define PHY_TYPE_SET_802_11BGN  (PHY_TYPE_BIT_HR_DSSS | \
                                 PHY_TYPE_BIT_ERP | \
                                 PHY_TYPE_BIT_HT)

#define PHY_TYPE_SET_802_11GN   (PHY_TYPE_BIT_ERP | \
                                 PHY_TYPE_BIT_HT)

#define PHY_TYPE_SET_802_11AN   (PHY_TYPE_BIT_OFDM | \
                                 PHY_TYPE_BIT_HT)

#define PHY_TYPE_SET_802_11ABG  (PHY_TYPE_BIT_OFDM | \
                                 PHY_TYPE_BIT_HR_DSSS | \
                                 PHY_TYPE_BIT_ERP)

#define PHY_TYPE_SET_802_11BG   (PHY_TYPE_BIT_HR_DSSS | \
                                 PHY_TYPE_BIT_ERP)

#define PHY_TYPE_SET_802_11A    (PHY_TYPE_BIT_OFDM)

#define PHY_TYPE_SET_802_11G    (PHY_TYPE_BIT_ERP)

#define PHY_TYPE_SET_802_11B    (PHY_TYPE_BIT_HR_DSSS)

#define PHY_TYPE_SET_802_11N    (PHY_TYPE_BIT_HT)


/* Rate set bit definitions */
#define RATE_SET_BIT_1M         BIT(RATE_1M_INDEX)      /* Bit 0: 1M */
#define RATE_SET_BIT_2M         BIT(RATE_2M_INDEX)      /* Bit 1: 2M */
#define RATE_SET_BIT_5_5M       BIT(RATE_5_5M_INDEX)    /* Bit 2: 5.5M */
#define RATE_SET_BIT_11M        BIT(RATE_11M_INDEX)     /* Bit 3: 11M */
#define RATE_SET_BIT_22M        BIT(RATE_22M_INDEX)     /* Bit 4: 22M */
#define RATE_SET_BIT_33M        BIT(RATE_33M_INDEX)     /* Bit 5: 33M */
#define RATE_SET_BIT_6M         BIT(RATE_6M_INDEX)      /* Bit 6: 6M */
#define RATE_SET_BIT_9M         BIT(RATE_9M_INDEX)      /* Bit 7: 9M */
#define RATE_SET_BIT_12M        BIT(RATE_12M_INDEX)     /* Bit 8: 12M */
#define RATE_SET_BIT_18M        BIT(RATE_18M_INDEX)     /* Bit 9: 18M */
#define RATE_SET_BIT_24M        BIT(RATE_24M_INDEX)     /* Bit 10: 24M */
#define RATE_SET_BIT_36M        BIT(RATE_36M_INDEX)     /* Bit 11: 36M */
#define RATE_SET_BIT_48M        BIT(RATE_48M_INDEX)     /* Bit 12: 48M */
#define RATE_SET_BIT_54M        BIT(RATE_54M_INDEX)     /* Bit 13: 54M */
#define RATE_SET_BIT_HT_PHY     BIT(RATE_HT_PHY_INDEX)  /* Bit 14: BSS Selector */


/* Rate set definitions */
#define RATE_SET_HR_DSSS            (RATE_SET_BIT_1M | \
                                     RATE_SET_BIT_2M | \
                                     RATE_SET_BIT_5_5M | \
                                     RATE_SET_BIT_11M)

#define RATE_SET_ERP                (RATE_SET_BIT_1M | \
                                     RATE_SET_BIT_2M | \
                                     RATE_SET_BIT_5_5M | \
                                     RATE_SET_BIT_11M | \
                                     RATE_SET_BIT_6M | \
                                     RATE_SET_BIT_9M | \
                                     RATE_SET_BIT_12M | \
                                     RATE_SET_BIT_18M | \
                                     RATE_SET_BIT_24M | \
                                     RATE_SET_BIT_36M | \
                                     RATE_SET_BIT_48M | \
                                     RATE_SET_BIT_54M)

#define RATE_SET_ERP_P2P            (RATE_SET_BIT_6M | \
                                     RATE_SET_BIT_9M | \
                                     RATE_SET_BIT_12M | \
                                     RATE_SET_BIT_18M | \
                                     RATE_SET_BIT_24M | \
                                     RATE_SET_BIT_36M | \
                                     RATE_SET_BIT_48M | \
                                     RATE_SET_BIT_54M)

#define RATE_SET_OFDM               (RATE_SET_BIT_6M | \
                                     RATE_SET_BIT_9M | \
                                     RATE_SET_BIT_12M | \
                                     RATE_SET_BIT_18M | \
                                     RATE_SET_BIT_24M | \
                                     RATE_SET_BIT_36M | \
                                     RATE_SET_BIT_48M | \
                                     RATE_SET_BIT_54M)

#define RATE_SET_HT                 (RATE_SET_ERP)
//#define RATE_SET_HT                 (RATE_SET_ERP | RATE_SET_BIT_HT_PHY) /* NOTE(Kevin): TBD */


#define RATE_SET_ALL_ABG             RATE_SET_ERP

#define BASIC_RATE_SET_HR_DSSS      (RATE_SET_BIT_1M | \
                                     RATE_SET_BIT_2M)

#define BASIC_RATE_SET_HR_DSSS_ERP  (RATE_SET_BIT_1M | \
                                     RATE_SET_BIT_2M | \
                                     RATE_SET_BIT_5_5M | \
                                     RATE_SET_BIT_11M)

#define BASIC_RATE_SET_ERP          (RATE_SET_BIT_1M | \
                                     RATE_SET_BIT_2M | \
                                     RATE_SET_BIT_5_5M | \
                                     RATE_SET_BIT_11M | \
                                     RATE_SET_BIT_6M | \
                                     RATE_SET_BIT_12M | \
                                     RATE_SET_BIT_24M)

#define BASIC_RATE_SET_OFDM         (RATE_SET_BIT_6M | \
                                     RATE_SET_BIT_12M | \
                                     RATE_SET_BIT_24M)

#define BASIC_RATE_SET_ERP_P2P      (RATE_SET_BIT_6M | \
                                     RATE_SET_BIT_12M | \
                                     RATE_SET_BIT_24M)

#define INITIAL_RATE_SET_RCPI_100    RATE_SET_ALL_ABG

#define INITIAL_RATE_SET_RCPI_80    (RATE_SET_BIT_1M | \
                                     RATE_SET_BIT_2M | \
                                     RATE_SET_BIT_5_5M | \
                                     RATE_SET_BIT_11M | \
                                     RATE_SET_BIT_6M | \
                                     RATE_SET_BIT_9M | \
                                     RATE_SET_BIT_12M | \
                                     RATE_SET_BIT_24M)

#define INITIAL_RATE_SET_RCPI_60    (RATE_SET_BIT_1M | \
                                     RATE_SET_BIT_2M | \
                                     RATE_SET_BIT_5_5M | \
                                     RATE_SET_BIT_11M | \
                                     RATE_SET_BIT_6M)

#define INITIAL_RATE_SET(_rcpi)     (INITIAL_RATE_SET_ ## _rcpi)

#define RCPI_100                    100 /* -60 dBm */
#define RCPI_80                     80  /* -70 dBm */
#define RCPI_60                     60  /* -80 dBm */


/* The number of RCPI records used to calculate their average value */
#define MAX_NUM_RCPI_RECORDS        10

/* The number of RCPI records used to calculate their average value */
#define NO_RCPI_RECORDS             -128
#define MAX_RCPI_DBM                0
#define MIN_RCPI_DBM                -100


#define MAC_TX_RESERVED_FIELD       0 /* NOTE(Kevin): Should defined in tx.h */

#define MAX_ASSOC_ID                (CFG_STA_REC_NUM)   /* Available AID: 1 ~ 20(STA_REC_NUM) */


#define MAX_DEAUTH_INFO_COUNT       4       /* NOTE(Kevin): Used in auth.c */
#define MIN_DEAUTH_INTERVAL_MSEC    500     /* The minimum interval if continuously send Deauth Frame */

/* Authentication Type */
#define AUTH_TYPE_OPEN_SYSTEM                       BIT(AUTH_ALGORITHM_NUM_OPEN_SYSTEM)
#define AUTH_TYPE_SHARED_KEY                        BIT(AUTH_ALGORITHM_NUM_SHARED_KEY)
#define AUTH_TYPE_FAST_BSS_TRANSITION               BIT(AUTH_ALGORITHM_NUM_FAST_BSS_TRANSITION)

/* Authentication Retry Limit */
#define TX_AUTH_ASSOCI_RETRY_LIMIT                  2
#define TX_AUTH_ASSOCI_RETRY_LIMIT_FOR_ROAMING      1

/* WMM-2.2.1 WMM Information Element */
#define ELEM_MAX_LEN_WMM_INFO       7

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
typedef UINT_16                     PHY_TYPE, *P_PHY_TYPE;
typedef UINT_8                      RCPI, *P_RCPI;
typedef UINT_8                      ALC_VAL, *P_ALC_VAL;

typedef enum _ENUM_HW_BSSID_T {
    BSSID_0 = 0,
    BSSID_1,
    BSSID_NUM
} ENUM_HW_BSSID_T;

typedef enum _ENUM_HW_MAC_ADDR_T {
    MAC_ADDR_0 = 0,
    MAC_ADDR_1,
    MAC_ADDR_NUM
} ENUM_HW_MAC_ADDR_T;

typedef enum _ENUM_HW_OP_MODE_T {
    HW_OP_MODE_STA = 0,
    HW_OP_MODE_AP,
    HW_OP_MODE_ADHOC,
    HW_OP_MODE_NUM
} ENUM_HW_OP_MODE_T;

typedef enum _ENUM_TSF_T {
    ENUM_LOCAL_TSF_0,
    ENUM_LOCAL_TSF_1,
    ENUM_LOCAL_TSF_NUM
} ENUM_LOCAL_TSF_T, *P_ENUM_LOCAL_TSF_T;

typedef enum _HAL_TS_HW_UPDATE_MODE {
    HAL_TSF_HW_UPDATE_BY_TICK_AND_RECEIVED_FRAME,
    HAL_TSF_HW_UPDATE_BY_TICK_ONLY,
    HAL_TSF_HW_UPDATE_BY_RECEIVED_FRAME_ONLY,
    HAL_TSF_HW_UPDATE_BY_TICK_AND_RECEIVED_FRAME_AD_HOC
} HAL_TSF_HW_UPDATE_MODE;


typedef enum _ENUM_AC_T {
    AC0 = 0,
    AC1,
    AC2,
    AC3,
    AC_NUM
} ENUM_AC_T, *P_ENUM_AC_T;


/* The Type of Network been activated */
typedef enum _ENUM_NETWORK_TYPE_INDEX_T {
    NETWORK_TYPE_AIS_INDEX = 0,
    NETWORK_TYPE_P2P_INDEX,
    NETWORK_TYPE_BOW_INDEX,
    NETWORK_TYPE_INDEX_NUM
} ENUM_NETWORK_TYPE_INDEX_T;


/* The Type of STA Type. */
typedef enum _ENUM_STA_TYPE_INDEX_T {
    STA_TYPE_LEGACY_INDEX = 0,
    STA_TYPE_P2P_INDEX,
    STA_TYPE_BOW_INDEX,
    STA_TYPE_INDEX_NUM
}
ENUM_STA_TYPE_INDEX_T;

#define STA_ROLE_BASE_INDEX     4

typedef enum _ENUM_STA_ROLE_INDEX_T {
    STA_ROLE_ADHOC_INDEX = STA_ROLE_BASE_INDEX, //4
    STA_ROLE_CLIENT_INDEX,
    STA_ROLE_AP_INDEX,
    STA_ROLE_DLS_INDEX
} ENUM_STA_ROLE_INDEX_T;

/* The Power State of a specific Network */
typedef enum _ENUM_PWR_STATE_T {
    PWR_STATE_IDLE = 0,
    PWR_STATE_ACTIVE,
    PWR_STATE_PS,
    PWR_STATE_NUM
} ENUM_PWR_STATE_T;

typedef enum _ENUM_PHY_TYPE_INDEX_T {
    //PHY_TYPE_DSSS_INDEX,      /* DSSS PHY (clause 15) -- Not used anymore */
    PHY_TYPE_HR_DSSS_INDEX = 0, /* HR/DSSS PHY (clause 18) */
    PHY_TYPE_ERP_INDEX,         /* ERP PHY (clause 19) */
    PHY_TYPE_ERP_P2P_INDEX,     /* ERP PHY (clause 19) w/o HR/DSSS */
    PHY_TYPE_OFDM_INDEX,        /* OFDM 5 GHz PHY (clause 17) */
    PHY_TYPE_HT_INDEX,          /* HT PHY (clause 20) */
    PHY_TYPE_INDEX_NUM // 5
} ENUM_PHY_TYPE_INDEX_T, *P_ENUM_PHY_TYPE_INDEX_T;

typedef enum _ENUM_ACPI_STATE_T {
    ACPI_STATE_D0 = 0,
    ACPI_STATE_D1,
    ACPI_STATE_D2,
    ACPI_STATE_D3
} ENUM_ACPI_STATE_T;

/* The operation mode of a specific Network */
typedef enum _ENUM_OP_MODE_T {
    OP_MODE_INFRASTRUCTURE = 0,         /* Infrastructure/GC */
    OP_MODE_IBSS,                       /* AdHoc */
    OP_MODE_ACCESS_POINT,               /* For GO */
    OP_MODE_P2P_DEVICE,                    /* P2P Device */
    OP_MODE_BOW,
    OP_MODE_NUM
} ENUM_OP_MODE_T, *P_ENUM_OP_MODE_T;

typedef enum _ENUM_CHNL_EXT_T {
    CHNL_EXT_SCN = 0,
    CHNL_EXT_SCA = 1,
    CHNL_EXT_RES = 2,
    CHNL_EXT_SCB = 3
} ENUM_CHNL_EXT_T, *P_ENUM_CHNL_EXT_T;

/* This starting freq of the band is unit of kHz */
typedef enum _ENUM_BAND_T {
    BAND_NULL,
    BAND_2G4,
    BAND_5G,
    BAND_NUM
} ENUM_BAND_T, *P_ENUM_BAND_T;

/* Provide supported channel list to other components in array format */
typedef struct _RF_CHANNEL_INFO_T {
    ENUM_BAND_T     eBand;
    UINT_8          ucChannelNum;
} RF_CHANNEL_INFO_T, *P_RF_CHANNEL_INFO_T;

typedef enum _ENUM_RATE_INDEX_T {
    RATE_1M_INDEX = 0,      /* 1M */
    RATE_2M_INDEX,          /* 2M */
    RATE_5_5M_INDEX,        /* 5.5M */
    RATE_11M_INDEX,         /* 11M */
    RATE_22M_INDEX,         /* 22M */
    RATE_33M_INDEX,         /* 33M */
    RATE_6M_INDEX,          /* 6M */
    RATE_9M_INDEX,          /* 9M */
    RATE_12M_INDEX,         /* 12M */
    RATE_18M_INDEX,         /* 18M */
    RATE_24M_INDEX,         /* 24M */
    RATE_36M_INDEX,         /* 36M */
    RATE_48M_INDEX,         /* 48M */
    RATE_54M_INDEX,         /* 54M */
    RATE_HT_PHY_INDEX,      /* BSS Selector - HT PHY */
    RATE_NUM // 15
} ENUM_RATE_INDEX_T, *P_ENUM_RATE_INDEX_T;

typedef enum _ENUM_HT_RATE_INDEX_T {
    HT_RATE_MCS0_INDEX = 0,
    HT_RATE_MCS1_INDEX,
    HT_RATE_MCS2_INDEX,
    HT_RATE_MCS3_INDEX,
    HT_RATE_MCS4_INDEX,
    HT_RATE_MCS5_INDEX,
    HT_RATE_MCS6_INDEX,
    HT_RATE_MCS7_INDEX,
    HT_RATE_MCS32_INDEX,
    HT_RATE_NUM // 9
} ENUM_HT_RATE_INDEX_T, *P_ENUM_HT_RATE_INDEX_T;

typedef enum _ENUM_PREMABLE_OPTION_T {
    PREAMBLE_DEFAULT_LONG_NONE = 0, /* LONG for PHY_TYPE_HR_DSSS, NONE for PHY_TYPE_OFDM */
    PREAMBLE_OPTION_SHORT, /* SHORT mandatory for PHY_TYPE_ERP, SHORT option for PHY_TYPE_HR_DSSS */
    PREAMBLE_HT_MIXED_MODE,
    PREAMBLE_HT_GREEN_FIELD,
    PREAMBLE_OPTION_NUM
} ENUM_PREMABLE_OPTION_T, *P_ENUM_PREMABLE_OPTION_T;

typedef enum _ENUM_MODULATION_SYSTEM_T {
    MODULATION_SYSTEM_CCK = 0,
    MODULATION_SYSTEM_OFDM,
    MODULATION_SYSTEM_HT20,
    MODULATION_SYSTEM_HT40,
    MODULATION_SYSTEM_NUM
} ENUM_MODULATION_SYSTEM_T, *P_ENUM_MODULATION_SYSTEM_T;

typedef enum _ENUM_MODULATION_TYPE_T {
    MODULATION_TYPE_CCK_BPSK = 0,
    MODULATION_TYPE_QPSK,
    MODULATION_TYPE_16QAM,
    MODULATION_TYPE_64QAM,
    MODULATION_TYPE_NUM
} ENUM_MODULATION_TYPE_T, *P_ENUM_MODULATION_TYPE_T;

typedef enum _ENUM_PS_FORWARDING_TYPE_T {
    PS_FORWARDING_TYPE_NON_PS = 0,
    PS_FORWARDING_TYPE_DELIVERY_ENABLED,
    PS_FORWARDING_TYPE_NON_DELIVERY_ENABLED,
    PS_FORWARDING_MORE_DATA_ENABLED,
    PS_FORWARDING_TYPE_NUM
} ENUM_PS_FORWARDING_TYPE_T, *P_ENUM_PS_FORWARDING_TYPE_T;

typedef struct _DEAUTH_INFO_T {
    UINT_8 aucRxAddr[MAC_ADDR_LEN];
    OS_SYSTIME rLastSendTime;
} DEAUTH_INFO_T, *P_DEAUTH_INFO_T;

/*----------------------------------------------------------------------------*/
/* Information Element (IE) handlers                                          */
/*----------------------------------------------------------------------------*/
typedef VOID (*PFN_APPEND_IE_FUNC)(P_ADAPTER_T, P_MSDU_INFO_T);
typedef VOID (*PFN_HANDLE_IE_FUNC)(P_ADAPTER_T, P_SW_RFB_T, P_IE_HDR_T);
typedef VOID (*PFN_VERIFY_IE_FUNC)(P_ADAPTER_T, P_SW_RFB_T, P_IE_HDR_T, PUINT_16);
typedef UINT_32 (*PFN_CALCULATE_VAR_IE_LEN_FUNC)(P_ADAPTER_T, ENUM_NETWORK_TYPE_INDEX_T, P_STA_RECORD_T);

typedef struct _APPEND_IE_ENTRY_T {
    UINT_16             u2EstimatedIELen;
    PFN_APPEND_IE_FUNC  pfnAppendIE;
} APPEND_IE_ENTRY_T, *P_APPEND_IE_ENTRY_T;

typedef struct _APPEND_VAR_IE_ENTRY_T {
    UINT_16                         u2EstimatedFixedIELen; /* For Fixed Length */
    PFN_CALCULATE_VAR_IE_LEN_FUNC   pfnCalculateVariableIELen;
    PFN_APPEND_IE_FUNC              pfnAppendIE;
} APPEND_VAR_IE_ENTRY_T, *P_APPEND_VAR_IE_ENTRY_T;

typedef struct _HANDLE_IE_ENTRY_T {
    UINT_8              ucElemID;
    PFN_HANDLE_IE_FUNC  pfnHandleIE;
} HANDLE_IE_ENTRY_T, *P_HANDLE_IE_ENTRY_T;

typedef struct _VERIFY_IE_ENTRY_T {
    UINT_8              ucElemID;
    PFN_VERIFY_IE_FUNC  pfnVarifyIE;
} VERIFY_IE_ENTRY_T, *P_VERIFY_IE_ENTRY_T;

/*----------------------------------------------------------------------------*/
/* Parameters of User Configuration                                           */
/*----------------------------------------------------------------------------*/
typedef enum _ENUM_PARAM_CONNECTION_POLICY_T {
    CONNECT_BY_SSID_BEST_RSSI = 0,
    CONNECT_BY_SSID_GOOD_RSSI_MIN_CH_LOAD,
    CONNECT_BY_SSID_ANY, /* NOTE(Kevin): Needed by WHQL */
    CONNECT_BY_BSSID,
    CONNECT_BY_CUSTOMIZED_RULE /* NOTE(Kevin): TBD */
} ENUM_PARAM_CONNECTION_POLICY_T, *P_ENUM_PARAM_CONNECTION_POLICY_T;

typedef enum _ENUM_PARAM_PREAMBLE_TYPE_T {
    PREAMBLE_TYPE_LONG = 0,
    PREAMBLE_TYPE_SHORT,
    PREAMBLE_TYPE_AUTO                  /*!< Try preamble short first, if fail tray preamble long. */
} ENUM_PARAM_PREAMBLE_TYPE_T, *P_ENUM_PARAM_PREAMBLE_TYPE_T;

/* This is enum defined for user to select a phy config listed in combo box */
typedef enum _ENUM_PARAM_PHY_CONFIG_T {
    PHY_CONFIG_802_11ABG = 0,           /*!< Can associated with 802.11abg AP but without n capability, Scan dual band. */
    PHY_CONFIG_802_11BG,                /*!< Can associated with 802_11bg AP, Scan single band and not report 5G BSSs. */
    PHY_CONFIG_802_11G,                 /*!< Can associated with 802_11g only AP, Scan single band and not report 5G BSSs. */
    PHY_CONFIG_802_11A,                 /*!< Can associated with 802_11a only AP, Scan single band and not report 2.4G BSSs. */
    PHY_CONFIG_802_11B,                 /*!< Can associated with 802_11b only AP, Scan single band and not report 5G BSSs. */
    PHY_CONFIG_802_11ABGN,              /*!< Can associated with 802.11abgn AP, Scan dual band. */
    PHY_CONFIG_802_11BGN,               /*!< Can associated with 802_11bgn AP, Scan single band and not report 5G BSSs. */
    PHY_CONFIG_802_11AN,                /*!< Can associated with 802_11an AP, Scan single band and not report 2.4G BSSs. */
    PHY_CONFIG_802_11GN,                /*!< Can associated with 802_11gn AP, Scan single band and not report 5G BSSs. */
    PHY_CONFIG_NUM // 9
} ENUM_PARAM_PHY_CONFIG_T, *P_ENUM_PARAM_PHY_CONFIG_T;

/* This is enum defined for user to select an AP Mode */
typedef enum _ENUM_PARAM_AP_MODE_T {
    AP_MODE_11B = 0,                /*!< Create 11b BSS if we support 802.11abg/802.11bg. */
    AP_MODE_MIXED_11BG,             /*!< Create 11bg mixed BSS if we support 802.11abg/802.11bg/802.11g. */
    AP_MODE_11G,                    /*!< Create 11g only BSS if we support 802.11abg/802.11bg/802.11g. */
    AP_MODE_11G_P2P,                /*!< Create 11g only BSS for P2P if we support 802.11abg/802.11bg/802.11g. */
    AP_MODE_11A,                    /*!< Create 11a only BSS if we support 802.11abg. */
    AP_MODE_NUM // 4
} ENUM_PARAM_AP_MODE_T, *P_ENUM_PARAM_AP_MODE_T;


/* Masks for determining the Network Type or the Station Role, given the ENUM_STA_TYPE_T */
#define NETWORK_TYPE_AIS_MASK               BIT(NETWORK_TYPE_AIS_INDEX)
#define NETWORK_TYPE_P2P_MASK               BIT(NETWORK_TYPE_P2P_INDEX)
#define NETWORK_TYPE_BOW_MASK               BIT(NETWORK_TYPE_BOW_INDEX)
#define STA_TYPE_LEGACY_MASK                 BIT(STA_TYPE_LEGACY_INDEX)
#define STA_TYPE_P2P_MASK                       BIT(STA_TYPE_P2P_INDEX)
#define STA_TYPE_BOW_MASK                     BIT(STA_TYPE_BOW_INDEX)
#define STA_TYPE_ADHOC_MASK                 BIT(STA_ROLE_ADHOC_INDEX)
#define STA_TYPE_CLIENT_MASK                BIT(STA_ROLE_CLIENT_INDEX)
#define STA_TYPE_AP_MASK                    BIT(STA_ROLE_AP_INDEX)
#define STA_TYPE_DLS_MASK                   BIT(STA_ROLE_DLS_INDEX)

/* Macros for obtaining the Network Type or the Station Role, given the ENUM_STA_TYPE_T */
#define IS_STA_IN_AIS(_prStaRec)        ((_prStaRec)->ucNetTypeIndex == NETWORK_TYPE_AIS_INDEX)
#define IS_STA_IN_P2P(_prStaRec)        ((_prStaRec)->ucNetTypeIndex ==  NETWORK_TYPE_P2P_INDEX)
#define IS_STA_IN_BOW(_prStaRec)        ((_prStaRec)->ucNetTypeIndex ==  NETWORK_TYPE_BOW_INDEX)
#define IS_STA_LEGACY_TYPE(_prStaRec)        ((_prStaRec->eStaType) & STA_TYPE_LEGACY_MASK)
#define IS_STA_P2P_TYPE(_prStaRec)        ((_prStaRec->eStaType) & STA_TYPE_P2P_MASK)
#define IS_STA_BOW_TYPE(_prStaRec)        ((_prStaRec->eStaType) & STA_TYPE_BOW_MASK)
#define IS_ADHOC_STA(_prStaRec)         ((_prStaRec->eStaType) & STA_TYPE_ADHOC_MASK)
#define IS_CLIENT_STA(_prStaRec)        ((_prStaRec->eStaType) & STA_TYPE_CLIENT_MASK)
#define IS_AP_STA(_prStaRec)            ((_prStaRec->eStaType) & STA_TYPE_AP_MASK)
#define IS_DLS_STA(_prStaRec)           ((_prStaRec->eStaType) & STA_TYPE_DLS_MASK)

/* The ENUM_STA_TYPE_T accounts for ENUM_NETWORK_TYPE_T and ENUM_STA_ROLE_INDEX_T.
 * *   It is a merged version of Network Type and STA Role.
 * */
typedef enum _ENUM_STA_TYPE_T {
    STA_TYPE_LEGACY_AP       = (STA_TYPE_LEGACY_MASK | STA_TYPE_AP_MASK),
    STA_TYPE_LEGACY_CLIENT = (STA_TYPE_LEGACY_MASK | STA_TYPE_CLIENT_MASK),
    STA_TYPE_ADHOC_PEER     = (STA_TYPE_LEGACY_MASK | STA_TYPE_ADHOC_MASK),
#if CFG_ENABLE_WIFI_DIRECT
    STA_TYPE_P2P_GO         = (STA_TYPE_P2P_MASK | STA_TYPE_AP_MASK),
    STA_TYPE_P2P_GC         = (STA_TYPE_P2P_MASK | STA_TYPE_CLIENT_MASK),
#endif
#if CFG_ENABLE_BT_OVER_WIFI
    STA_TYPE_BOW_AP         = (STA_TYPE_BOW_MASK | STA_TYPE_AP_MASK),
    STA_TYPE_BOW_CLIENT     = (STA_TYPE_BOW_MASK | STA_TYPE_CLIENT_MASK),
#endif
    STA_TYPE_DLS_PEER       = (STA_TYPE_LEGACY_MASK | STA_TYPE_DLS_MASK)
} ENUM_STA_TYPE_T, *P_ENUM_STA_TYPE_T;

/* The type of BSS we discovered */
typedef enum _ENUM_BSS_TYPE_T {
    BSS_TYPE_INFRASTRUCTURE = 1,
    BSS_TYPE_IBSS,
    BSS_TYPE_P2P_DEVICE,
    BSS_TYPE_BOW_DEVICE,
    BSS_TYPE_NUM
} ENUM_BSS_TYPE_T, *P_ENUM_BSS_TYPE_T;

/*----------------------------------------------------------------------------*/
/* RSN structures                                                             */
/*----------------------------------------------------------------------------*/
//#if defined(WINDOWS_DDK) || defined(WINDOWS_CE)
//#pragma pack(1)
//#endif

#define MAX_NUM_SUPPORTED_CIPHER_SUITES 8     /* max number of supported cipher suites */
#if CFG_SUPPORT_802_11W
#define MAX_NUM_SUPPORTED_AKM_SUITES    8     /* max number of supported AKM suites */
#else
#define MAX_NUM_SUPPORTED_AKM_SUITES    6     /* max number of supported AKM suites */
#endif

/* Structure of RSN Information */
typedef struct _RSN_INFO_T {
    UINT_8          ucElemId;
    UINT_16         u2Version;
    UINT_32         u4GroupKeyCipherSuite;
    UINT_32         u4PairwiseKeyCipherSuiteCount;
    UINT_32         au4PairwiseKeyCipherSuite[MAX_NUM_SUPPORTED_CIPHER_SUITES];
    UINT_32         u4AuthKeyMgtSuiteCount;
    UINT_32         au4AuthKeyMgtSuite[MAX_NUM_SUPPORTED_AKM_SUITES];
    UINT_16         u2RsnCap;
    BOOLEAN         fgRsnCapPresent;
} /*__KAL_ATTRIB_PACKED__*/ RSN_INFO_T, *P_RSN_INFO_T;

#define MAX_NUM_SUPPORTED_WAPI_AKM_SUITES    1 /* max number of supported AKM suites */
#define MAX_NUM_SUPPORTED_WAPI_CIPHER_SUITES 1 /* max number of supported cipher suites */

/* Structure of WAPI Information */
typedef struct _WAPI_INFO_T {
    UINT_8          ucElemId;
    UCHAR           ucLength;
    UINT_16         u2Version;
    UINT_32         u4AuthKeyMgtSuiteCount;
    UINT_32         au4AuthKeyMgtSuite[MAX_NUM_SUPPORTED_WAPI_AKM_SUITES];
    UINT_32         u4PairwiseKeyCipherSuiteCount;
    UINT_32         au4PairwiseKeyCipherSuite[MAX_NUM_SUPPORTED_WAPI_CIPHER_SUITES];
    UINT_32         u4GroupKeyCipherSuite;
    UINT_16         u2WapiCap;
    UINT_16         u2Bkid;
    UINT_8          aucBkid[1][16];
} /* __KAL_ATTRIB_PACKED__*/ WAPI_INFO_T, *P_WAPI_INFO_T;

//#if defined(WINDOWS_DDK) || defined(WINDOWS_CE)
//#pragma pack()
//#endif


#if CFG_ENABLE_WIFI_DIRECT

typedef struct _P2P_DEVICE_TYPE_T {
    UINT_16 u2CategoryID;
    UINT_16 u2SubCategoryID;
} P2P_DEVICE_TYPE_T, *P_P2P_DEVICE_TYPE_T;

typedef struct _P2P_DEVICE_DESC_T {
    LINK_ENTRY_T        rLinkEntry;
    BOOLEAN             fgDevInfoValid;
    UINT_8              aucDeviceAddr[MAC_ADDR_LEN]; /* Device Address. */
    UINT_8              aucInterfaceAddr[MAC_ADDR_LEN]; /* Interface Address. */
    UINT_8              ucDeviceCapabilityBitmap;
    UINT_8              ucGroupCapabilityBitmap;
    UINT_16             u2ConfigMethod; /* Configure Method support. */
    P2P_DEVICE_TYPE_T   rPriDevType;
    UINT_8              ucSecDevTypeNum;
    P2P_DEVICE_TYPE_T   arSecDevType[8];   // Reference to P2P_GC_MAX_CACHED_SEC_DEV_TYPE_COUNT
    UINT_16             u2NameLength;
    UINT_8              aucName[32];                // Reference to WPS_ATTRI_MAX_LEN_DEVICE_NAME
    // TODO: Service Information or PasswordID valid?
} P2P_DEVICE_DESC_T, *P_P2P_DEVICE_DESC_T;

#endif


/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/
static const UINT_8 aucRateIndex2RateCode[PREAMBLE_OPTION_NUM][RATE_NUM] = {
    { /* Long Preamble */
        RATE_CCK_1M_LONG,       /* RATE_1M_INDEX = 0 */
        RATE_CCK_2M_LONG,       /* RATE_2M_INDEX */
        RATE_CCK_5_5M_LONG,     /* RATE_5_5M_INDEX */
        RATE_CCK_11M_LONG,      /* RATE_11M_INDEX */
        RATE_CCK_1M_LONG,       /* RATE_22M_INDEX - Not supported */
        RATE_CCK_1M_LONG,       /* RATE_33M_INDEX - Not supported */
        RATE_OFDM_6M,           /* RATE_6M_INDEX */
        RATE_OFDM_9M,           /* RATE_9M_INDEX */
        RATE_OFDM_12M,          /* RATE_12M_INDEX */
        RATE_OFDM_18M,          /* RATE_18M_INDEX */
        RATE_OFDM_24M,          /* RATE_24M_INDEX */
        RATE_OFDM_36M,          /* RATE_36M_INDEX */
        RATE_OFDM_48M,          /* RATE_48M_INDEX */
        RATE_OFDM_54M,           /* RATE_54M_INDEX */
    },
    { /* Short Preamble */
        RATE_CCK_1M_LONG,       /* RATE_1M_INDEX = 0 */
        RATE_CCK_2M_SHORT,      /* RATE_2M_INDEX */
        RATE_CCK_5_5M_SHORT,    /* RATE_5_5M_INDEX */
        RATE_CCK_11M_SHORT,     /* RATE_11M_INDEX */
        RATE_CCK_1M_LONG,       /* RATE_22M_INDEX - Not supported */
        RATE_CCK_1M_LONG,       /* RATE_33M_INDEX - Not supported */
        RATE_OFDM_6M,           /* RATE_6M_INDEX */
        RATE_OFDM_9M,           /* RATE_9M_INDEX */
        RATE_OFDM_12M,          /* RATE_12M_INDEX */
        RATE_OFDM_18M,          /* RATE_18M_INDEX */
        RATE_OFDM_24M,          /* RATE_24M_INDEX */
        RATE_OFDM_36M,          /* RATE_36M_INDEX */
        RATE_OFDM_48M,          /* RATE_48M_INDEX */
        RATE_OFDM_54M,           /* RATE_54M_INDEX */
    },
    { /* Mixed Mode(Option) */
        RATE_MM_MCS_0,               /* RATE_MCS0_INDEX, */
        RATE_MM_MCS_1,               /* RATE_MCS1_INDEX, */
        RATE_MM_MCS_2,               /* RATE_MCS2_INDEX, */
        RATE_MM_MCS_3,               /* RATE_MCS3_INDEX, */
        RATE_MM_MCS_4,               /* RATE_MCS4_INDEX, */
        RATE_MM_MCS_5,               /* RATE_MCS5_INDEX, */
        RATE_MM_MCS_6,               /* RATE_MCS6_INDEX, */
        RATE_MM_MCS_7,               /* RATE_MCS7_INDEX, */
        RATE_MM_MCS_32               /* RATE_MCS32_INDEX, */
    },
    { /* Green Field(Option) */
        RATE_GF_MCS_0,               /* RATE_MCS0_INDEX, */
        RATE_GF_MCS_1,               /* RATE_MCS1_INDEX, */
        RATE_GF_MCS_2,               /* RATE_MCS2_INDEX, */
        RATE_GF_MCS_3,               /* RATE_MCS3_INDEX, */
        RATE_GF_MCS_4,               /* RATE_MCS4_INDEX, */
        RATE_GF_MCS_5,               /* RATE_MCS5_INDEX, */
        RATE_GF_MCS_6,               /* RATE_MCS6_INDEX, */
        RATE_GF_MCS_7,               /* RATE_MCS7_INDEX, */
        RATE_GF_MCS_32               /* RATE_MCS32_INDEX, */
    }
};

static const UINT_8 aucRateTableSize[PREAMBLE_OPTION_NUM] = {
    RATE_HT_PHY_INDEX,
    RATE_HT_PHY_INDEX,
    HT_RATE_NUM,
    HT_RATE_NUM
};


/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/
/* Macros to get and set the wireless LAN frame fields those are 16/32 bits in
   length. */
#define WLAN_GET_FIELD_16(_memAddr_p, _value_p) \
        { \
            PUINT_8 __cp = (PUINT_8) (_memAddr_p); \
            *(PUINT_16)(_value_p) = ((UINT_16) __cp[0]) | ((UINT_16) __cp[1] << 8); \
        }

#define WLAN_GET_FIELD_BE16(_memAddr_p, _value_p) \
        { \
            PUINT_8 __cp = (PUINT_8) (_memAddr_p); \
            *(PUINT_16)(_value_p) = ((UINT_16) __cp[0] << 8) | ((UINT_16) __cp[1]); \
        }

#define WLAN_GET_FIELD_32(_memAddr_p, _value_p) \
        { \
            PUINT_8 __cp = (PUINT_8) (_memAddr_p); \
            *(PUINT_32)(_value_p) = ((UINT_32) __cp[0])       | ((UINT_32) __cp[1] << 8) | \
                                    ((UINT_32) __cp[2] << 16) | ((UINT_32) __cp[3] << 24); \
        }

#define WLAN_GET_FIELD_64(_memAddr_p, _value_p) \
        { \
            PUINT_8 __cp = (PUINT_8) (_memAddr_p); \
            *(PUINT_64)(_value_p) = \
                ((UINT_64) __cp[0])       | ((UINT_64) __cp[1] << 8)  | \
                ((UINT_64) __cp[2] << 16) | ((UINT_64) __cp[3] << 24) | \
                ((UINT_64) __cp[4] << 32) | ((UINT_64) __cp[5] << 40) | \
                ((UINT_64) __cp[6] << 48) | ((UINT_64) __cp[7] << 56); \
        }

#define WLAN_SET_FIELD_16(_memAddr_p, _value) \
        { \
            PUINT_8 __cp = (PUINT_8) (_memAddr_p); \
            __cp[0] = (UINT_8) (_value); \
            __cp[1] = (UINT_8) ((_value) >> 8); \
        }

#define WLAN_SET_FIELD_BE16(_memAddr_p, _value) \
       { \
            PUINT_8 __cp = (PUINT_8) (_memAddr_p); \
            __cp[0] = (UINT_8) ((_value) >> 8); \
            __cp[1] = (UINT_8) (_value); \
       }

#define WLAN_SET_FIELD_32(_memAddr_p, _value) \
        { \
            PUINT_8 __cp = (PUINT_8) (_memAddr_p); \
            __cp[0] = (UINT_8) (_value); \
            __cp[1] = (UINT_8) ((_value) >> 8); \
            __cp[2] = (UINT_8) ((_value) >> 16); \
            __cp[3] = (UINT_8) ((_value) >> 24); \
        }



/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/


/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _WLAN_DEF_H */

