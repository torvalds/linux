/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_2/include/queue.h#1 $
*/

/*! \file   queue.h
    \brief  Definition for singly queue operations.

    In this file we define the singly queue data structure and its
    queue operation MACROs.
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
** $Log: queue.h $
 *
 * 03 18 2011 cp.wu
 * [WCXRP00000577] [MT6620 Wi-Fi][Driver][FW] Create V2.0 branch for firmware and driver
 * create V2.0 driver release based on label "MT6620_WIFI_DRIVER_V2_0_110318_1600" from main trunk
 *
 * 07 16 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * bugfix for SCN migration
 * 1) modify QUEUE_CONCATENATE_QUEUES() so it could be used to concatence with an empty queue
 * 2) before AIS issues scan request, network(BSS) needs to be activated first
 * 3) only invoke COPY_SSID when using specified SSID for scan
 *
 * 07 08 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base 
 * [MT6620 5931] Create driver base
 *
 * 04 20 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP 
 * .
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-10 20:11:46 GMT mtk01426
**  Init for develop
**
*/

#ifndef _QUEUE_H
#define _QUEUE_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "gl_typedef.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
/* Singly Queue Structures - Entry Part */
typedef struct _QUE_ENTRY_T {
    struct _QUE_ENTRY_T *prNext;
    struct _QUE_ENTRY_T *prPrev; /* For Rx buffer reordering used only */
} QUE_ENTRY_T, *P_QUE_ENTRY_T;

/* Singly Queue Structures - Queue Part */
typedef struct _QUE_T {
    P_QUE_ENTRY_T   prHead;
    P_QUE_ENTRY_T   prTail;
    UINT_32         u4NumElem;
} QUE_T, *P_QUE_T;


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
#define QUEUE_INITIALIZE(prQueue) \
        { \
            (prQueue)->prHead = (P_QUE_ENTRY_T)NULL; \
            (prQueue)->prTail = (P_QUE_ENTRY_T)NULL; \
            (prQueue)->u4NumElem = 0; \
        }

#define QUEUE_IS_EMPTY(prQueue)             (((P_QUE_T)(prQueue))->prHead == (P_QUE_ENTRY_T)NULL)

#define QUEUE_IS_NOT_EMPTY(prQueue)         ((prQueue)->u4NumElem > 0)

#define QUEUE_GET_HEAD(prQueue)             ((prQueue)->prHead)

#define QUEUE_GET_TAIL(prQueue)             ((prQueue)->prTail)

#define QUEUE_GET_NEXT_ENTRY(prQueueEntry)  ((prQueueEntry)->prNext)

#define QUEUE_INSERT_HEAD(prQueue, prQueueEntry) \
        { \
            ASSERT(prQueue); \
            ASSERT(prQueueEntry); \
            (prQueueEntry)->prNext = (prQueue)->prHead; \
            (prQueue)->prHead = (prQueueEntry); \
            if ((prQueue)->prTail == (P_QUE_ENTRY_T)NULL) { \
                (prQueue)->prTail = (prQueueEntry); \
            } \
            ((prQueue)->u4NumElem)++; \
        }

#define QUEUE_INSERT_TAIL(prQueue, prQueueEntry) \
        { \
            ASSERT(prQueue); \
            ASSERT(prQueueEntry); \
            (prQueueEntry)->prNext = (P_QUE_ENTRY_T)NULL; \
            if ((prQueue)->prTail) { \
                ((prQueue)->prTail)->prNext = (prQueueEntry); \
            } else { \
                (prQueue)->prHead = (prQueueEntry); \
            } \
            (prQueue)->prTail = (prQueueEntry); \
            ((prQueue)->u4NumElem)++; \
        }

/* NOTE: We assume the queue entry located at the beginning of "prQueueEntry Type",
 * so that we can cast the queue entry to other data type without doubts.
 * And this macro also decrease the total entry count at the same time.
 */
#define QUEUE_REMOVE_HEAD(prQueue, prQueueEntry, _P_TYPE) \
        { \
            ASSERT(prQueue); \
            prQueueEntry = (_P_TYPE)((prQueue)->prHead); \
            if (prQueueEntry) { \
                (prQueue)->prHead = ((P_QUE_ENTRY_T)(prQueueEntry))->prNext; \
                if ((prQueue)->prHead == (P_QUE_ENTRY_T)NULL){ \
                    (prQueue)->prTail = (P_QUE_ENTRY_T)NULL; \
                } \
                ((P_QUE_ENTRY_T)(prQueueEntry))->prNext = (P_QUE_ENTRY_T)NULL; \
                ((prQueue)->u4NumElem)--; \
            } \
        }

#define QUEUE_MOVE_ALL(prDestQueue, prSrcQueue) \
        { \
            ASSERT(prDestQueue); \
            ASSERT(prSrcQueue); \
            *(P_QUE_T)prDestQueue = *(P_QUE_T)prSrcQueue; \
            QUEUE_INITIALIZE(prSrcQueue); \
        }

#define QUEUE_CONCATENATE_QUEUES(prDestQueue, prSrcQueue) \
        { \
            ASSERT(prDestQueue); \
            ASSERT(prSrcQueue); \
            if (prSrcQueue->u4NumElem > 0) { \
                if ((prDestQueue)->prTail) { \
                    ((prDestQueue)->prTail)->prNext = (prSrcQueue)->prHead; \
                } else { \
                    (prDestQueue)->prHead = (prSrcQueue)->prHead; \
                } \
                (prDestQueue)->prTail = (prSrcQueue)->prTail; \
                ((prDestQueue)->u4NumElem) += ((prSrcQueue)->u4NumElem); \
                QUEUE_INITIALIZE(prSrcQueue); \
            } \
        }


/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _QUEUE_H */

