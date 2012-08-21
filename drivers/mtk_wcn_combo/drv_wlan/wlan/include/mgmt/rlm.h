/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_2/include/mgmt/rlm.h#1 $
*/

/*! \file   "rlm.h"
    \brief
*/

/*******************************************************************************
* Copyright (c) 2009 MediaTek Inc.
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
** $Log: rlm.h $
 *
 * 10 19 2011 yuche.tsai
 * [WCXRP00001045] [WiFi Direct][Driver] Check 2.1 branch.
 * Branch 2.1
 * Davinci Maintrunk Label: MT6620_WIFI_DRIVER_FW_TRUNK_MT6620E5_111019_0926.
 *
 * 04 12 2011 cm.chang
 * [WCXRP00000634] [MT6620 Wi-Fi][Driver][FW] 2nd BSS will not support 40MHz bandwidth for concurrency
 * .
 *
 * 03 18 2011 cp.wu
 * [WCXRP00000577] [MT6620 Wi-Fi][Driver][FW] Create V2.0 branch for firmware and driver
 * create V2.0 driver release based on label "MT6620_WIFI_DRIVER_V2_0_110318_1600" from main trunk
 *
 * 01 13 2011 cm.chang
 * [WCXRP00000358] [MT6620 Wi-Fi][Driver] Provide concurrent information for each module
 * Refine function when rcv a 20/40M public action frame
 *
 * 01 13 2011 cm.chang
 * [WCXRP00000354] [MT6620 Wi-Fi][Driver][FW] Follow NVRAM bandwidth setting
 * Use SCO of BSS_INFO to replace user-defined setting variables
 *
 * 01 12 2011 cm.chang
 * [WCXRP00000354] [MT6620 Wi-Fi][Driver][FW] Follow NVRAM bandwidth setting
 * User-defined bandwidth is for 2.4G and 5G individually
 *
 * 12 07 2010 cm.chang
 * [WCXRP00000239] MT6620 Wi-Fi][Driver][FW] Merge concurrent branch back to maintrunk
 * 1. BSSINFO include RLM parameter
 * 2. free all sta records when network is disconnected
 *
 * 12 07 2010 cm.chang
 * [WCXRP00000238] MT6620 Wi-Fi][Driver][FW] Support regulation domain setting from NVRAM and supplicant
 * 1. Country code is from NVRAM or supplicant
 * 2. Change band definition in CMD/EVENT.
 *
 * 10 18 2010 cm.chang
 * [WCXRP00000114] [MT6620 Wi-Fi] [Driver] Fix compiling warning in Linux about RLM network index checking
 * Enum member cannot be used as compiling option decision in Linux
 *
 * 09 10 2010 cm.chang
 * NULL
 * Always update Beacon content if FW sync OBSS info
 *
 * 08 31 2010 kevin.huang
 * NULL
 * Use LINK LIST operation to process SCAN result
 *
 * 08 24 2010 cm.chang
 * NULL
 * Support RLM initail channel of Ad-hoc, P2P and BOW
 *
 * 08 23 2010 chinghwa.yu
 * NULL
 * Update for BOW.
 *
 * 08 20 2010 cm.chang
 * NULL
 * Migrate RLM code to host from FW
 *
 * 08 16 2010 cp.wu
 * NULL
 * Replace CFG_SUPPORT_BOW by CFG_ENABLE_BT_OVER_WIFI.
 * There is no CFG_SUPPORT_BOW in driver domain source.
 *
 * 08 02 2010 yuche.tsai
 * NULL
 * P2P Group Negotiation Code Check in.
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 08 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Check draft RLM code for HT cap
 *
 * 06 28 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * 1st draft code for RLM module
 *
 * 06 02 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Add RX HT GF compiling option
 *
 * 06 02 2010 chinghwa.yu
 * [BORA00000563]Add WiFi CoEx BCM module
 * Roll back to remove CFG_SUPPORT_BCM_TEST.
 *
 * 06 01 2010 chinghwa.yu
 * [BORA00000563]Add WiFi CoEx BCM module
 * Update BCM Test and RW configuration.
 *
 * 05 31 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Add some compiling options to control 11n functions
 *
 * 05 18 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Ad-hoc Beacon should not carry HT OP and OBSS IEs
 *
 * 05 17 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * MT6620 does not support L-SIG TXOP
 *
 * 05 05 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * First draft support for 20/40M bandwidth for AP mode
 *
 * 04 24 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * g_aprBssInfo[] depends on CFG_SUPPORT_P2P and CFG_SUPPORT_BOW
 *
 * 04 22 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * First draft code to support protection in AP mode
 *
 * 04 07 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Different invoking order for WTBL entry of associated AP
 *
 * 03 24 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Not carry  HT cap when being associated with b/g only AP
 *
 * 03 03 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Move default value of HT capability to rlm.h
 *
 * 02 12 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Use bss info array for concurrent handle
 *
 * 01 22 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support protection and bandwidth switch
 *
 * 01 08 2010 kevin.huang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 *
 * Modify the prototype of rlmRecAssocRspHtInfo()
 *
 * Dec 9 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add several function prototypes for HT operation
 *
 * Nov 18 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 *
 *
**
*/

#ifndef _RLM_H
#define _RLM_H

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
#define ELEM_EXT_CAP_DEFAULT_VAL \
        (ELEM_EXT_CAP_20_40_COEXIST_SUPPORT /*| ELEM_EXT_CAP_PSMP_CAP*/)


#if CFG_SUPPORT_RX_STBC
    #define FIELD_HT_CAP_INFO_RX_STBC   HT_CAP_INFO_RX_STBC_1_SS
#else
    #define FIELD_HT_CAP_INFO_RX_STBC   HT_CAP_INFO_RX_STBC_NO_SUPPORTED
#endif

#if CFG_SUPPORT_RX_SGI
    #define FIELD_HT_CAP_INFO_SGI_20M   HT_CAP_INFO_SHORT_GI_20M
    #define FIELD_HT_CAP_INFO_SGI_40M   HT_CAP_INFO_SHORT_GI_40M
#else
    #define FIELD_HT_CAP_INFO_SGI_20M   0
    #define FIELD_HT_CAP_INFO_SGI_40M   0
#endif

#if CFG_SUPPORT_RX_HT_GF
    #define FIELD_HT_CAP_INFO_HT_GF     HT_CAP_INFO_HT_GF
#else
    #define FIELD_HT_CAP_INFO_HT_GF     0
#endif

#define HT_CAP_INFO_DEFAULT_VAL \
        (HT_CAP_INFO_SUP_CHNL_WIDTH | FIELD_HT_CAP_INFO_HT_GF | \
         FIELD_HT_CAP_INFO_SGI_20M | FIELD_HT_CAP_INFO_SGI_40M | \
         FIELD_HT_CAP_INFO_RX_STBC | HT_CAP_INFO_DSSS_CCK_IN_40M)



#define AMPDU_PARAM_DEFAULT_VAL \
        (AMPDU_PARAM_MAX_AMPDU_LEN_64K | AMPDU_PARAM_MSS_NO_RESTRICIT)


#define SUP_MCS_TX_DEFAULT_VAL \
        SUP_MCS_TX_SET_DEFINED   /* TX defined and TX/RX equal (TBD) */

#if CFG_SUPPORT_MFB
    #define FIELD_HT_EXT_CAP_MFB    HT_EXT_CAP_MCS_FEEDBACK_BOTH
#else
    #define FIELD_HT_EXT_CAP_MFB    HT_EXT_CAP_MCS_FEEDBACK_NO_FB
#endif

#if CFG_SUPPORT_RX_RDG
    #define FIELD_HT_EXT_CAP_RDR    HT_EXT_CAP_RD_RESPONDER
#else
    #define FIELD_HT_EXT_CAP_RDR    0
#endif

#if CFG_SUPPORT_MFB || CFG_SUPPORT_RX_RDG
    #define FIELD_HT_EXT_CAP_HTC    HT_EXT_CAP_HTC_SUPPORT
#else
    #define FIELD_HT_EXT_CAP_HTC    0
#endif

#define HT_EXT_CAP_DEFAULT_VAL \
        (HT_EXT_CAP_PCO | HT_EXT_CAP_PCO_TRANS_TIME_NONE | \
         FIELD_HT_EXT_CAP_MFB | FIELD_HT_EXT_CAP_HTC | \
         FIELD_HT_EXT_CAP_RDR)

#define TX_BEAMFORMING_CAP_DEFAULT_VAL              0
#define ASEL_CAP_DEFAULT_VAL                        0


/* Define bandwidth from user setting */
#define CONFIG_BW_20_40M            0
#define CONFIG_BW_20M               1   /* 20MHz only */

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

/* It is used for RLM module to judge if specific network is valid
 * Note: Ad-hoc mode of AIS is not included now. (TBD)
 */
#define RLM_NET_PARAM_VALID(_prBssInfo) \
        (IS_BSS_ACTIVE(_prBssInfo) && \
         ((_prBssInfo)->eConnectionState == PARAM_MEDIA_STATE_CONNECTED || \
          (_prBssInfo)->eCurrentOPMode == OP_MODE_ACCESS_POINT || \
          (_prBssInfo)->eCurrentOPMode == OP_MODE_IBSS || \
          RLM_NET_IS_BOW(_prBssInfo)) \
        )

#define RLM_NET_IS_11N(_prBssInfo) \
        ((_prBssInfo)->ucPhyTypeSet & PHY_TYPE_SET_802_11N)
#define RLM_NET_IS_11GN(_prBssInfo) \
        ((_prBssInfo)->ucPhyTypeSet & PHY_TYPE_SET_802_11GN)


/* This macro is used to sweep all 3 networks */
#define RLM_NET_FOR_EACH(_ucNetIdx) \
    for ((_ucNetIdx) = 0; \
         (_ucNetIdx) < NETWORK_TYPE_INDEX_NUM; \
         (_ucNetIdx)++)

/* This macro is used to sweep all networks excluding BOW */
#if CFG_ENABLE_BT_OVER_WIFI
    /* Note: value of enum NETWORK_TYPE_BOW_INDEX is validated in
     *       rlmStuctureCheck().
     */
    #define RLM_NET_FOR_EACH_NO_BOW(_ucNetIdx) \
        for ((_ucNetIdx) = 0; \
             (_ucNetIdx) < NETWORK_TYPE_BOW_INDEX; \
             (_ucNetIdx)++)

    #define RLM_NET_IS_BOW(_prBssInfo) \
            ((_prBssInfo)->ucNetTypeIndex == NETWORK_TYPE_BOW_INDEX)

#else
    #define RLM_NET_FOR_EACH_NO_BOW(_ucNetIdx)  RLM_NET_FOR_EACH(_ucNetIdx)
    #define RLM_NET_IS_BOW(_prBssInfo)          (FALSE)

#endif /* end of CFG_ENABLE_BT_OVER_WIFI */


/* The bandwidth modes are not used anymore. They represent if AP
 * can use 20/40 bandwidth, not all modes. (20110411)
 */
#define RLM_AP_IS_BW_40_ALLOWED(_prAdapter, _prBssInfo) \
    (((_prBssInfo)->eBand == BAND_2G4 && \
      (_prAdapter)->rWifiVar.rConnSettings.uc2G4BandwidthMode \
      == CONFIG_BW_20_40M) || \
     ((_prBssInfo)->eBand == BAND_5G && \
      (_prAdapter)->rWifiVar.rConnSettings.uc5GBandwidthMode \
      == CONFIG_BW_20_40M))

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
VOID
rlmFsmEventInit (
    P_ADAPTER_T     prAdapter
    );

VOID
rlmFsmEventUninit (
    P_ADAPTER_T     prAdapter
    );

VOID
rlmReqGenerateHtCapIE (
    P_ADAPTER_T     prAdapter,
    P_MSDU_INFO_T   prMsduInfo
    );

VOID
rlmReqGenerateExtCapIE (
    P_ADAPTER_T     prAdapter,
    P_MSDU_INFO_T   prMsduInfo
    );

VOID
rlmRspGenerateHtCapIE (
    P_ADAPTER_T     prAdapter,
    P_MSDU_INFO_T   prMsduInfo
    );

VOID
rlmRspGenerateExtCapIE (
    P_ADAPTER_T     prAdapter,
    P_MSDU_INFO_T   prMsduInfo
    );

VOID
rlmRspGenerateHtOpIE (
    P_ADAPTER_T     prAdapter,
    P_MSDU_INFO_T   prMsduInfo
    );

VOID
rlmRspGenerateErpIE (
    P_ADAPTER_T     prAdapter,
    P_MSDU_INFO_T   prMsduInfo
    );

VOID
rlmProcessBcn (
    P_ADAPTER_T prAdapter,
    P_SW_RFB_T  prSwRfb,
    PUINT_8     pucIE,
    UINT_16     u2IELength
    );

VOID
rlmProcessAssocRsp (
    P_ADAPTER_T prAdapter,
    P_SW_RFB_T  prSwRfb,
    PUINT_8     pucIE,
    UINT_16     u2IELength
    );

VOID
rlmFillSyncCmdParam (
    P_CMD_SET_BSS_RLM_PARAM_T   prCmdBody,
    P_BSS_INFO_T                prBssInfo
    );

VOID
rlmSyncOperationParams (
    P_ADAPTER_T         prAdapter,
    P_BSS_INFO_T        prBssInfo
    );

VOID
rlmBssInitForAPandIbss (
    P_ADAPTER_T     prAdapter,
    P_BSS_INFO_T    prBssInfo
    );

VOID
rlmProcessAssocReq (
    P_ADAPTER_T prAdapter,
    P_SW_RFB_T  prSwRfb,
    PUINT_8     pucIE,
    UINT_16     u2IELength
    );

VOID
rlmBssAborted (
    P_ADAPTER_T     prAdapter,
    P_BSS_INFO_T    prBssInfo
    );

VOID
linkToRlmRspGenerateObssScanIE (
    P_ADAPTER_T     prAdapter,
    P_MSDU_INFO_T   prMsduInfo
    );


/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#ifndef _lint
__KAL_INLINE__ VOID
rlmDataTypeCheck (
    VOID
    )
{
#if CFG_ENABLE_BT_OVER_WIFI
    DATA_STRUC_INSPECTING_ASSERT(
        NETWORK_TYPE_AIS_INDEX < NETWORK_TYPE_BOW_INDEX);

    #if CFG_ENABLE_WIFI_DIRECT
        DATA_STRUC_INSPECTING_ASSERT(
            NETWORK_TYPE_P2P_INDEX < NETWORK_TYPE_BOW_INDEX);
    #endif
#endif

    return;
}
#endif /* _lint */

#endif /* _RLM_H */


