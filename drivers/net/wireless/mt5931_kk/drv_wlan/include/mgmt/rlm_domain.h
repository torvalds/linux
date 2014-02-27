/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/include/mgmt/rlm_domain.h#1 $
*/

/*! \file   "rlm_domain.h"
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
** $Log: rlm_domain.h $
 *
 * 09 29 2011 cm.chang
 * NULL
 * Change the function prototype of rlmDomainGetChnlList()
 *
 * 09 08 2011 cm.chang
 * [WCXRP00000969] [MT6620 Wi-Fi][Driver][FW] Channel list for 5G band based on country code
 * Use new fields ucChannelListMap and ucChannelListIndex in NVRAM
 *
 * 08 31 2011 cm.chang
 * [WCXRP00000969] [MT6620 Wi-Fi][Driver][FW] Channel list for 5G band based on country code
 * .
 *
 * 06 01 2011 cm.chang
 * [WCXRP00000756] [MT6620 Wi-Fi][Driver] 1. AIS follow channel of BOW 2. Provide legal channel function
 * Provide legal channel function based on domain
 *
 * 12 07 2010 cm.chang
 * [WCXRP00000238] MT6620 Wi-Fi][Driver][FW] Support regulation domain setting from NVRAM and supplicant
 * 1. Country code is from NVRAM or supplicant
 * 2. Change band definition in CMD/EVENT.
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 28 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * 1st draft code for RLM module
 *
 * 02 23 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Add support scan channel 1~14 and update scan result's frequency infou1rwduu`wvpghlqg|n`slk+mpdkb
 *
 * 01 13 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Provide query function about full channle list.
 *
 * Dec 1 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Declare public rDomainInfo
 *
**
*/

#ifndef _RLM_DOMAIN_H
#define _RLM_DOMAIN_H

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
#define MAX_SUBBAND_NUM     6


#define COUNTRY_CODE_NULL               ((UINT_16)0x0)

/* ISO/IEC 3166-1 two-character country codes */
#define COUNTRY_CODE_AG (((UINT_16) 'A' << 8) | (UINT_16) 'G') /* Antigua/Barbuda */
#define COUNTRY_CODE_AI (((UINT_16) 'A' << 8) | (UINT_16) 'I') /* Anguilla      */
#define COUNTRY_CODE_AR (((UINT_16) 'A' << 8) | (UINT_16) 'T') /* Argentina     */
#define COUNTRY_CODE_AT (((UINT_16) 'A' << 8) | (UINT_16) 'T') /* Austria       */
#define COUNTRY_CODE_AU (((UINT_16) 'A' << 8) | (UINT_16) 'U') /* Australia     */
#define COUNTRY_CODE_AW (((UINT_16) 'A' << 8) | (UINT_16) 'W') /* Aruba         */
#define COUNTRY_CODE_BB (((UINT_16) 'B' << 8) | (UINT_16) 'B') /* Barbados      */
#define COUNTRY_CODE_BE (((UINT_16) 'B' << 8) | (UINT_16) 'E') /* Belgium       */
#define COUNTRY_CODE_BM (((UINT_16) 'B' << 8) | (UINT_16) 'M') /* Bermuda       */
#define COUNTRY_CODE_BO (((UINT_16) 'B' << 8) | (UINT_16) 'O') /* Bolivia       */
#define COUNTRY_CODE_BR (((UINT_16) 'B' << 8) | (UINT_16) 'R') /* Brazil        */
#define COUNTRY_CODE_BS (((UINT_16) 'B' << 8) | (UINT_16) 'S') /* Bahamas       */
#define COUNTRY_CODE_BY (((UINT_16) 'B' << 8) | (UINT_16) 'Y') /* Belarus       */
#define COUNTRY_CODE_CA (((UINT_16) 'C' << 8) | (UINT_16) 'A') /* Canada        */
#define COUNTRY_CODE_CH (((UINT_16) 'C' << 8) | (UINT_16) 'H') /* Switzerland   */
#define COUNTRY_CODE_CL (((UINT_16) 'C' << 8) | (UINT_16) 'L') /* Chile         */
#define COUNTRY_CODE_CN (((UINT_16) 'C' << 8) | (UINT_16) 'N') /* China         */
#define COUNTRY_CODE_CO (((UINT_16) 'C' << 8) | (UINT_16) 'O') /* Colombia      */
#define COUNTRY_CODE_CR (((UINT_16) 'C' << 8) | (UINT_16) 'R') /* Costa Rica    */
#define COUNTRY_CODE_CU (((UINT_16) 'C' << 8) | (UINT_16) 'U') /* Cuba          */
#define COUNTRY_CODE_DE (((UINT_16) 'D' << 8) | (UINT_16) 'E') /* Germany       */
#define COUNTRY_CODE_DK (((UINT_16) 'D' << 8) | (UINT_16) 'K') /* Denmark       */
#define COUNTRY_CODE_DM (((UINT_16) 'D' << 8) | (UINT_16) 'M') /* Dominica      */
#define COUNTRY_CODE_DO (((UINT_16) 'D' << 8) | (UINT_16) 'O') /* Dominican Republic */
#define COUNTRY_CODE_EC (((UINT_16) 'E' << 8) | (UINT_16) 'C') /* Ecuador       */
#define COUNTRY_CODE_EG (((UINT_16) 'E' << 8) | (UINT_16) 'G') /* Egypt         */
#define COUNTRY_CODE_ES (((UINT_16) 'E' << 8) | (UINT_16) 'S') /* Spain         */
#define COUNTRY_CODE_EU (((UINT_16) 'E' << 8) | (UINT_16) 'U') /* ETSI (Europe) */
#define COUNTRY_CODE_FI (((UINT_16) 'F' << 8) | (UINT_16) 'I') /* Finland       */
#define COUNTRY_CODE_FR (((UINT_16) 'F' << 8) | (UINT_16) 'R') /* France        */
#define COUNTRY_CODE_GB (((UINT_16) 'G' << 8) | (UINT_16) 'B') /* United Kingdom */
#define COUNTRY_CODE_GD (((UINT_16) 'G' << 8) | (UINT_16) 'D') /* Grenada       */
#define COUNTRY_CODE_GR (((UINT_16) 'G' << 8) | (UINT_16) 'R') /* Greece        */
#define COUNTRY_CODE_GY (((UINT_16) 'G' << 8) | (UINT_16) 'Y') /* Guyana        */
#define COUNTRY_CODE_HK (((UINT_16) 'H' << 8) | (UINT_16) 'K') /* Hong Kong     */
#define COUNTRY_CODE_HT (((UINT_16) 'H' << 8) | (UINT_16) 'T') /* Haiti         */
#define COUNTRY_CODE_HN (((UINT_16) 'H' << 8) | (UINT_16) 'N') /* Honduras      */
#define COUNTRY_CODE_ID (((UINT_16) 'I' << 8) | (UINT_16) 'D') /* Indonesia     */
#define COUNTRY_CODE_IE (((UINT_16) 'I' << 8) | (UINT_16) 'E') /* Ireland       */
#define COUNTRY_CODE_IL (((UINT_16) 'I' << 8) | (UINT_16) 'L') /* Israel        */
#define COUNTRY_CODE_IN (((UINT_16) 'I' << 8) | (UINT_16) 'N') /* India         */
#define COUNTRY_CODE_IR (((UINT_16) 'I' << 8) | (UINT_16) 'R') /* Iran          */
#define COUNTRY_CODE_IS (((UINT_16) 'I' << 8) | (UINT_16) 'S') /* Iceland       */
#define COUNTRY_CODE_IT (((UINT_16) 'I' << 8) | (UINT_16) 'T') /* Italy         */
#define COUNTRY_CODE_JM (((UINT_16) 'J' << 8) | (UINT_16) 'M') /* Jamaica       */
#define COUNTRY_CODE_JO (((UINT_16) 'J' << 8) | (UINT_16) 'O') /* Jordan        */
#define COUNTRY_CODE_JP (((UINT_16) 'J' << 8) | (UINT_16) 'P') /* Japan         */
#define COUNTRY_CODE_KN (((UINT_16) 'K' << 8) | (UINT_16) 'N') /* Saint Kitts and Nevis */
#define COUNTRY_CODE_KR (((UINT_16) 'K' << 8) | (UINT_16) 'R') /* South Korea   */
#define COUNTRY_CODE_KW (((UINT_16) 'K' << 8) | (UINT_16) 'W') /* Kuwait        */
#define COUNTRY_CODE_LC (((UINT_16) 'L' << 8) | (UINT_16) 'C') /* Saint Lucia   */
#define COUNTRY_CODE_LI (((UINT_16) 'L' << 8) | (UINT_16) 'I') /* Liechtenstein */
#define COUNTRY_CODE_LK (((UINT_16) 'L' << 8) | (UINT_16) 'K') /* Sri Lanka     */
#define COUNTRY_CODE_LU (((UINT_16) 'L' << 8) | (UINT_16) 'U') /* Luxembourg    */
#define COUNTRY_CODE_MA (((UINT_16) 'M' << 8) | (UINT_16) 'A') /* Morocco       */
#define COUNTRY_CODE_MD (((UINT_16) 'M' << 8) | (UINT_16) 'D') /* Moldova       */
#define COUNTRY_CODE_MX (((UINT_16) 'M' << 8) | (UINT_16) 'X') /* Mexico        */
#define COUNTRY_CODE_MY (((UINT_16) 'M' << 8) | (UINT_16) 'Y') /* Malaysia      */
#define COUNTRY_CODE_NI (((UINT_16) 'N' << 8) | (UINT_16) 'I') /* Nicaragua     */
#define COUNTRY_CODE_NL (((UINT_16) 'N' << 8) | (UINT_16) 'L') /* Netherlands   */
#define COUNTRY_CODE_NO (((UINT_16) 'N' << 8) | (UINT_16) 'O') /* Norway        */
#define COUNTRY_CODE_NZ (((UINT_16) 'N' << 8) | (UINT_16) 'Z') /* New Zealand   */
#define COUNTRY_CODE_OM (((UINT_16) 'O' << 8) | (UINT_16) 'M') /* Oman          */
#define COUNTRY_CODE_PE (((UINT_16) 'P' << 8) | (UINT_16) 'E') /* Peru          */
#define COUNTRY_CODE_PG (((UINT_16) 'P' << 8) | (UINT_16) 'G') /* Papua New Guinea */
#define COUNTRY_CODE_PH (((UINT_16) 'P' << 8) | (UINT_16) 'H') /* Philippines   */
#define COUNTRY_CODE_PK (((UINT_16) 'P' << 8) | (UINT_16) 'K') /* Pakistan      */
#define COUNTRY_CODE_PR (((UINT_16) 'P' << 8) | (UINT_16) 'R') /* Puerto Rico   */
#define COUNTRY_CODE_PT (((UINT_16) 'P' << 8) | (UINT_16) 'T') /* Portugal      */
#define COUNTRY_CODE_PY (((UINT_16) 'P' << 8) | (UINT_16) 'Y') /* Paraguay      */
#define COUNTRY_CODE_PZ (((UINT_16) 'P' << 8) | (UINT_16) 'Z') /* Panama        */
#define COUNTRY_CODE_RU (((UINT_16) 'R' << 8) | (UINT_16) 'U') /* Russian       */
#define COUNTRY_CODE_SA (((UINT_16) 'S' << 8) | (UINT_16) 'A') /* Saudi Arabia  */
#define COUNTRY_CODE_SE (((UINT_16) 'S' << 8) | (UINT_16) 'E') /* Sweden        */
#define COUNTRY_CODE_SG (((UINT_16) 'S' << 8) | (UINT_16) 'G') /* Singapore     */
#define COUNTRY_CODE_SR (((UINT_16) 'S' << 8) | (UINT_16) 'R') /* Suriname      */
#define COUNTRY_CODE_TW (((UINT_16) 'T' << 8) | (UINT_16) 'W') /* Taiwan        */
#define COUNTRY_CODE_TH (((UINT_16) 'T' << 8) | (UINT_16) 'H') /* Thailand      */
#define COUNTRY_CODE_TR (((UINT_16) 'T' << 8) | (UINT_16) 'R') /* Turkey        */
#define COUNTRY_CODE_TT (((UINT_16) 'T' << 8) | (UINT_16) 'T') /* Trinidad      */
#define COUNTRY_CODE_UA (((UINT_16) 'U' << 8) | (UINT_16) 'A') /* Ukraine       */
#define COUNTRY_CODE_US (((UINT_16) 'U' << 8) | (UINT_16) 'S') /* United States */
#define COUNTRY_CODE_UY (((UINT_16) 'U' << 8) | (UINT_16) 'Y') /* Uruguay       */
#define COUNTRY_CODE_VC (((UINT_16) 'V' << 8) | (UINT_16) 'C') /* Saint Vincent */
#define COUNTRY_CODE_VE (((UINT_16) 'V' << 8) | (UINT_16) 'E') /* Venezuela     */
#define COUNTRY_CODE_VN (((UINT_16) 'V' << 8) | (UINT_16) 'N') /* Vietnam       */

/* dot11RegDomainsSupportValue */
#define MIB_REG_DOMAIN_FCC              0x10    /* FCC (US) */
#define MIB_REG_DOMAIN_IC               0x20    /* IC or DOC (Canada) */
#define MIB_REG_DOMAIN_ETSI             0x30    /* ETSI (Europe) */
#define MIB_REG_DOMAIN_SPAIN            0x31    /* Spain */
#define MIB_REG_DOMAIN_FRANCE           0x32    /* France */
#define MIB_REG_DOMAIN_JAPAN            0x40    /* MPHPT (Japan) */
#define MIB_REG_DOMAIN_OTHER            0x00    /* other */


/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/* Define channel offset in unit of 5MHz bandwidth */
typedef enum _ENUM_CHNL_SPAN_T {
    CHNL_SPAN_5     = 1,
    CHNL_SPAN_10    = 2,
    CHNL_SPAN_20    = 4,
    CHNL_SPAN_40    = 8
} ENUM_CHNL_SPAN_T, *P_ENUM_CHNL_SPAN_T;

/* Define BSS operating bandwidth */
typedef enum _ENUM_CHNL_BW_T {
    CHNL_BW_20,
    CHNL_BW_20_40,
    CHNL_BW_10,
    CHNL_BW_5
} ENUM_CHNL_BW_T, *P_ENUM_CHNL_BW_T;

#if 0
/* If channel width is CHNL_BW_20_40, the first channel will be SCA and
 * the second channel is SCB, then iteratively.
 * Note the final channel will not be SCA.
 */
typedef struct _DOMAIN_SUBBAND_INFO {
    UINT_8              ucRegClass;
    ENUM_BAND_T         eBand;
    ENUM_CHNL_SPAN_T    eChannelSpan;
    UINT_8              ucFirstChannelNum;
    UINT_8              ucNumChannels;
    ENUM_CHNL_BW_T      eChannelBw;
    BOOLEAN             fgDfsNeeded;
    BOOLEAN             fgIbssProhibited;
} DOMAIN_SUBBAND_INFO, *P_DOMAIN_SUBBAND_INFO;

/* Use it as all available channel list for STA */
typedef struct _DOMAIN_INFO_ENTRY {
    UINT_16             u2CountryCode;
    UINT_16             u2MibRegDomainValue;

    /* If different attributes, put them into different rSubBands.
     * For example, DFS shall be used or not.
     */
    DOMAIN_SUBBAND_INFO rSubBand[MAX_SUBBAND_NUM];
} DOMAIN_INFO_ENTRY, *P_DOMAIN_INFO_ENTRY;

#else /* New definition 20110830 */

/* In all bands, the first channel will be SCA and the second channel is SCB,
 * then iteratively.
 * Note the final channel will not be SCA.
 */
typedef struct _DOMAIN_SUBBAND_INFO {
    /* Note1: regulation class depends on operation bandwidth and RF band.
     *  For example: 2.4GHz, 1~13, 20MHz ==> regulation class = 81
     *               2.4GHz, 1~13, SCA   ==> regulation class = 83
     *               2.4GHz, 1~13, SCB   ==> regulation class = 84
     * Note2: TX power limit is not specified here because path loss is unknown
     */
    UINT_8              ucRegClass;         /* Regulation class for 20MHz */
    UINT_8              ucBand;             /* Type: ENUM_BAND_T */
    UINT_8              ucChannelSpan;      /* Type: ENUM_CHNL_SPAN_T */
    UINT_8              ucFirstChannelNum;
    UINT_8              ucNumChannels;
    UINT_8              ucReserved;         /* Type: BOOLEAN (fgDfsNeeded) */
} DOMAIN_SUBBAND_INFO, *P_DOMAIN_SUBBAND_INFO;

/* Use it as all available channel list for STA */
typedef struct _DOMAIN_INFO_ENTRY {
    PUINT_16            pu2CountryGroup;
    UINT_32             u4CountryNum;

    /* If different attributes, put them into different rSubBands.
     * For example, DFS shall be used or not.
     */
    DOMAIN_SUBBAND_INFO rSubBand[MAX_SUBBAND_NUM];
} DOMAIN_INFO_ENTRY, *P_DOMAIN_INFO_ENTRY;
#endif

/* The following definitions are not used yet */
typedef enum _ENUM_CH_SET_2G4_T {
    CH_SET_2G4_NA,
    CH_SET_2G4_1_11,
    CH_SET_2G4_1_13,
    CH_SET_2G4_1_14,
    CH_SET_2G4_NUM
} ENUM_CH_SET_2G4_T, *P_ENUM_CH_SET_2G4_T;

typedef enum _ENUM_CH_SET_UNII_LOW_T {
    CH_SET_UNII_LOW_NA,
    CH_SET_UNII_LOW_36_48,
    CH_SET_UNII_LOW_NUM
} ENUM_CH_SET_UNII_LOW_T, *P_ENUM_CH_SET_UNII_LOW_T;

typedef enum _ENUM_CH_SET_UNII_MID_T {
    CH_SET_UNII_MID_NA,
    CH_SET_UNII_MID_52_64,
    CH_SET_UNII_MID_NUM
} ENUM_CH_SET_UNII_MID_T, *P_ENUM_CH_SET_UNII_MID_T;

typedef enum _ENUM_CH_SET_UNII_WW_T {
    CH_SET_UNII_WW_NA,
    CH_SET_UNII_WW_100_128,
    CH_SET_UNII_WW_100_140,
    CH_SET_UNII_WW_100_116_132_140,
    CH_SET_UNII_WW_NUM
} ENUM_CH_SET_UNII_WW_T, *P_ENUM_CH_SET_UNII_WW_T;

typedef enum _ENUM_CH_SET_UNII_UPPER_T {
    CH_SET_UNII_UPPER_NA,
    CH_SET_UNII_UPPER_149_161,
    CH_SET_UNII_UPPER_149_165,
    CH_SET_UNII_UPPER_149_173,
    CH_SET_UNII_UPPER_NUM
} ENUM_CH_SET_UNII_UPPER_T, *P_ENUM_CH_SET_UNII_UPPER_T;

typedef struct _COUNTRY_CH_SET_T {
    ENUM_CH_SET_2G4_T           e2G4;
    ENUM_CH_SET_UNII_LOW_T      eUniiLow;
    ENUM_CH_SET_UNII_MID_T      eUniiMid;
    ENUM_CH_SET_UNII_WW_T       eUniiWw;
    ENUM_CH_SET_UNII_UPPER_T    eUniiUpper;
} COUNTRY_CH_SET_T, *P_COUNTRY_CH_SET_T;


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
P_DOMAIN_INFO_ENTRY
rlmDomainGetDomainInfo (
    P_ADAPTER_T     prAdapter
    );

VOID
rlmDomainGetChnlList (
    P_ADAPTER_T             prAdapter,
    ENUM_BAND_T             eSpecificBand,
    UINT_8                  ucMaxChannelNum,
    PUINT_8                 pucNumOfChannel,
    P_RF_CHANNEL_INFO_T     paucChannelList
    );

VOID
rlmDomainSendCmd (
    P_ADAPTER_T     prAdapter,
    BOOLEAN         fgIsOid
    );

BOOLEAN
rlmDomainIsLegalChannel (
    P_ADAPTER_T     prAdapter,
    ENUM_BAND_T     eBand,
    UINT_8          ucChannel
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _RLM_DOMAIN_H */


