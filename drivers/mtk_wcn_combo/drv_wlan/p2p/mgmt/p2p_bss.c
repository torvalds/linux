/*
** $Id: @(#) p2p_bss.c@@
*/

/*! \file   "p2p_bss.c"
    \brief  This file contains the functions for creating p2p BSS(AP).

    This file contains the functions for BSS(AP). We may create a BSS
    network, or merge with exist IBSS network and sending Beacon Frame or reply
    the Probe Response Frame for received Probe Request Frame.
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


/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

#include "p2p_precomp.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

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

APPEND_VAR_IE_ENTRY_T txProbRspIETableWIP2P[] = {
    { (ELEM_HDR_LEN + (RATE_NUM - ELEM_MAX_LEN_SUP_RATES)), NULL,                           bssGenerateExtSuppRate_IE   }   /* 50 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_HT_CAP),                 NULL,                           rlmRspGenerateHtCapIE       }   /* 45 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_HT_OP),                  NULL,                           rlmRspGenerateHtOpIE        }   /* 61 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_OBSS_SCAN),              NULL,                           rlmRspGenerateObssScanIE    }   /* 74 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_EXT_CAP),                NULL,                           rlmRspGenerateExtCapIE      }   /* 127 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_RSN),                    NULL,                           rsnGenerateRSNIE            }   /* 48 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_WPA),                    NULL,                           rsnGenerateWpaNoneIE        }   /* 221 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_WMM_PARAM),              NULL,                           mqmGenerateWmmParamIE       }   /* 221 */
};


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

UINT_32
p2pGetTxProbRspIeTableSize(
    VOID
    )
{
    return (sizeof(txProbRspIETableWIP2P)/sizeof(APPEND_VAR_IE_ENTRY_T));
}



