/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/include/mgmt/swcr.h#1 $
*/

/*! \file   "swcr.h"
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
 *
 */

#ifndef _SWCR_H
#define _SWCR_H

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

#define SWCR_VAR(x) ((VOID *)&x)
#define SWCR_FUNC(x)  ((VOID *)x)

#define SWCR_T_FUNC BIT(7)

#define SWCR_L_32 3
#define SWCR_L_16 2
#define SWCR_L_8  1

#define SWCR_READ 0
#define SWCR_WRITE 1

#define SWCR_MAP_NUM(x)  (sizeof(x)/sizeof(x[0]))

#define SWCR_CR_NUM 7

#define SWCR_GET_RW_INDEX(action,rw,index) \
    index = action & 0x7F; \
    rw = action >> 7; 


extern UINT_32          g_au4SwCr[]; /*: 0: command other: data */

typedef VOID (*PFN_SWCR_RW_T)(P_ADAPTER_T prAdapter, UINT_8 ucRead, UINT_16 u2Addr, UINT_32 *pu4Data);
typedef VOID (*PFN_CMD_RW_T)(P_ADAPTER_T prAdapter, UINT_8 ucCate, UINT_8 ucAction, UINT_8 ucOpt0,UINT_8 ucOpt1);

typedef struct _SWCR_MAP_ENTRY_T {
    UINT_16             u2Type;
    PVOID               u4Addr;
} SWCR_MAP_ENTRY_T, *P_SWCR_MAP_ENTRY_T;


typedef struct _SWCR_MOD_MAP_ENTRY_T {
    UINT_8 ucMapNum;
    P_SWCR_MAP_ENTRY_T prSwCrMap;
} SWCR_MOD_MAP_ENTRY_T, *P_SWCR_MOD_MAP_ENTRY_T;

typedef enum _ENUM_SWCR_DBG_TYPE_T {
    SWCR_DBG_TYPE_ALL = 0,
    SWCR_DBG_TYPE_TXRX,
    SWCR_DBG_TYPE_RX_RATES,
    SWCR_DBG_TYPE_PS,
    SWCR_DBG_TYPE_NUM
} ENUM_SWCR_DBG_TYPE_T;

typedef enum _ENUM_SWCR_DBG_ALL_T {
    SWCR_DBG_ALL_TX_CNT = 0,
    SWCR_DBG_ALL_TX_BCN_CNT,
    SWCR_DBG_ALL_TX_FAILED_CNT,
    SWCR_DBG_ALL_TX_RETRY_CNT,
    SWCR_DBG_ALL_TX_AGING_TIMEOUT_CNT,
    SWCR_DBG_ALL_TX_PS_OVERFLOW_CNT,
    SWCR_DBG_ALL_TX_MGNT_DROP_CNT,
    SWCR_DBG_ALL_TX_ERROR_CNT,    
    
    SWCR_DBG_ALL_RX_CNT,
    SWCR_DBG_ALL_RX_DROP_CNT,
    SWCR_DBG_ALL_RX_DUP_DROP_CNT,
    SWCR_DBG_ALL_RX_TYPE_ERROR_DROP_CNT,    
    SWCR_DBG_ALL_RX_CLASS_ERROR_DROP_CNT,
    SWCR_DBG_ALL_RX_AMPDU_ERROR_DROP_CNT,
    
    SWCR_DBG_ALL_RX_STATUS_ERROR_DROP_CNT,
    SWCR_DBG_ALL_RX_FORMAT_ERROR_DROP_CNT,
    SWCR_DBG_ALL_RX_ICV_ERROR_DROP_CNT,
    SWCR_DBG_ALL_RX_KEY_ERROR_DROP_CNT,    
    SWCR_DBG_ALL_RX_TKIP_ERROR_DROP_CNT,
    SWCR_DBG_ALL_RX_MIC_ERROR_DROP_CNT,
    SWCR_DBG_ALL_RX_BIP_ERROR_DROP_CNT,
    
    SWCR_DBG_ALL_RX_FCSERR_CNT,
    SWCR_DBG_ALL_RX_FIFOFULL_CNT,
    SWCR_DBG_ALL_RX_PFDROP_CNT,
    
    SWCR_DBG_ALL_PWR_PS_POLL_CNT,
    SWCR_DBG_ALL_PWR_TRIGGER_NULL_CNT,
    SWCR_DBG_ALL_PWR_BCN_IND_CNT,
    SWCR_DBG_ALL_PWR_BCN_TIMEOUT_CNT,
    SWCR_DBG_ALL_PWR_PM_STATE0,
    SWCR_DBG_ALL_PWR_PM_STATE1,
    SWCR_DBG_ALL_PWR_CUR_PS_PROF0,
    SWCR_DBG_ALL_PWR_CUR_PS_PROF1,
    
    SWCR_DBG_ALL_AR_STA0_RATE,
    SWCR_DBG_ALL_AR_STA0_BWGI,
    SWCR_DBG_ALL_AR_STA0_RX_RATE_RCPI,

    SWCR_DBG_ALL_ROAMING_ENABLE,
    SWCR_DBG_ALL_ROAMING_ROAM_CNT,
    SWCR_DBG_ALL_ROAMING_INT_CNT,

    SWCR_DBG_ALL_BB_RX_MDRDY_CNT,
    SWCR_DBG_ALL_BB_RX_FCSERR_CNT,
    SWCR_DBG_ALL_BB_CCK_PD_CNT,
    SWCR_DBG_ALL_BB_OFDM_PD_CNT,
    SWCR_DBG_ALL_BB_CCK_SFDERR_CNT,
    SWCR_DBG_ALL_BB_CCK_SIGERR_CNT,
    SWCR_DBG_ALL_BB_OFDM_TAGERR_CNT,
    SWCR_DBG_ALL_BB_OFDM_SIGERR_CNT,
    
    SWCR_DBG_ALL_NUM
} ENUM_SWCR_DBG_ALL_T;

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

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/


/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

VOID swCrReadWriteCmd(
        P_ADAPTER_T prAdapter,
        UINT_8 ucRead, 
        UINT_16 u2Addr, 
        UINT_32 *pu4Data
        );

/* Debug Support */
VOID swCrFrameCheckEnable(P_ADAPTER_T  prAdapter, UINT_32 u4DumpType);
VOID swCrDebugInit(P_ADAPTER_T  prAdapter);
VOID swCrDebugCheckEnable(P_ADAPTER_T  prAdapter, BOOLEAN fgIsEnable, UINT_8 ucType, UINT_32 u4Timeout);
VOID swCrDebugUninit(P_ADAPTER_T  prAdapter);

#endif 
