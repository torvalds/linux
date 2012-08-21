/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 *
 * MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek Software")
 * have been modified by MediaTek Inc. All revisions are subject to any receiver's
 * applicable license agreements with MediaTek Inc.
 */


/*! \file
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
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

#ifndef _STP_EXP_H_
#define _STP_EXP_H_

#include "osal_typedef.h"
#include "osal.h"

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

#define BT_TASK_INDX        (0)
#define FM_TASK_INDX        (1)
#define GPS_TASK_INDX       (2)
#define WIFI_TASK_INDX      (3)
#define WMT_TASK_INDX       (4)
#define STP_TASK_INDX       (5)
#define INFO_TASK_INDX      (6)
#define MTKSTP_MAX_TASK_NUM (7)

#define MTKSTP_BUFFER_SIZE  (16384) //Size of RX Queue



/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
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
typedef void (*MTK_WCN_STP_EVENT_CB)(void);
typedef INT32 (*MTK_WCN_STP_IF_TX)(const UINT8 *data, const UINT32 size, UINT32 *written_size);
/* export for HIF driver */
typedef void (*MTK_WCN_STP_IF_RX)(const UINT8 *data, INT32 size);

typedef enum {
    STP_UART_IF_TX = 0,
    STP_SDIO_IF_TX,
    STP_MAX_IF_TX
}ENUM_STP_TX_IF_TYPE;

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/


/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_receive_data
* DESCRIPTION
*  receive data from serial protocol engine
* PARAMETERS
*  buffer      [IN]        data buffer
*  length      [IN]        data buffer length
* RETURNS
*  INT32    >= 0: size of data received; < 0: error
*****************************************************************************/
extern INT32 mtk_wcn_stp_receive_data(UINT8 *buffer, UINT32 length, UINT8 type);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_send_data
* DESCRIPTION
*  subfunction send data through STP
* PARAMETERS
*  buffer      [IN]        data buffer
*  length      [IN]        data buffer length
*  type        [IN]        subfunction type
* RETURNS
*  INT32    >= 0: length transmitted; < 0: error
*****************************************************************************/
extern INT32 mtk_wcn_stp_send_data(const UINT8 *buffer, const UINT32 length, const UINT8 type);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_is_rxqueue_empty
* DESCRIPTION
*  Is certain rx queue empty?
* PARAMETERS
*  type        [IN]        subfunction type
* RETURNS
*  INT32    0: queue is NOT empyt; !0: queue is empty
*****************************************************************************/
extern MTK_WCN_BOOL mtk_wcn_stp_is_rxqueue_empty(UINT8 type);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_is_enable
* DESCRIPTION
*  Is STP ready?
* PARAMETERS
*  none.
* RETURNS
*  MTK_WCN_BOOL    TRUE:ready, FALSE:not ready
*****************************************************************************/
extern MTK_WCN_BOOL mtk_wcn_stp_is_ready(void);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_parser_data
* DESCRIPTION
*  push data to serial transport protocol parser engine
* PARAMETERS
*  buffer      [IN]        data buffer
*  length      [IN]        data buffer length
* RETURNS
*  void
*****************************************************************************/
extern INT32 mtk_wcn_stp_parser_data(UINT8 *buffer, UINT32 length);

/*****************************************************************************
* FUNCTION
*  set_bluetooth_rx_interface
* DESCRIPTION
*  Set bluetooth rx interface
* PARAMETERS
*  rx interface type
* RETURNS
*  void
*****************************************************************************/
extern void mtk_wcn_stp_set_bluez(MTK_WCN_BOOL sdio_flag);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_register_tx_event_cb
* DESCRIPTION
*  regiter Tx event callback function
* PARAMETERS
*  func
* RETURNS
*  int: 0:successful , -1: fail
*****************************************************************************/
extern int mtk_wcn_stp_register_tx_event_cb(int type, MTK_WCN_STP_EVENT_CB func);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_register_event_cb
* DESCRIPTION
*  regiter Rx event callback function
* PARAMETERS
*  func
* RETURNS
*  int: 0:successful , -1: fail
*****************************************************************************/
extern int mtk_wcn_stp_register_event_cb(int type, MTK_WCN_STP_EVENT_CB func);


/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_register_if_tx
* DESCRIPTION
*  regiter Tx event callback function
* PARAMETERS
*  stp_if: SDIO or UART, fnnc: Call back function
* RETURNS
*  int: 0:successful , -1: fail
*****************************************************************************/
extern int mtk_wcn_stp_register_if_tx(ENUM_STP_TX_IF_TYPE stp_if, MTK_WCN_STP_IF_TX func);


/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_register_if_rx
* DESCRIPTION
*  regiter Rx event callback function
* PARAMETERS
*  stp_if: SDIO or UART, fnnc: Call back function
* RETURNS
*  int: 0:successful , -1: fail
*****************************************************************************/
extern int mtk_wcn_stp_register_if_rx(MTK_WCN_STP_IF_RX func);

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _WMT_EXP_H_ */













