/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/include/queue.h#1 $
*/

/*! \file   queue.h
    \brief  Definition for singly queue operations.

    In this file we define the singly queue data structure and its
    queue operation MACROs.
*/



/*
** $Log: queue.h $
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

/*
  * To resolve compiler warning of address check -Waddress
  * Redefine a ASSERT dedicate for queue operation 
  */
#if DBG
    #define QUE_ASSERT ASSERT
#else
    #define QUE_ASSERT(_exp)
#endif

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
            QUE_ASSERT(prQueue); \
            QUE_ASSERT(prQueueEntry); \
            (prQueueEntry)->prNext = (prQueue)->prHead; \
            (prQueue)->prHead = (prQueueEntry); \
            if ((prQueue)->prTail == (P_QUE_ENTRY_T)NULL) { \
                (prQueue)->prTail = (prQueueEntry); \
            } \
            ((prQueue)->u4NumElem)++; \
        }

#define QUEUE_INSERT_TAIL(prQueue, prQueueEntry) \
        { \
            QUE_ASSERT(prQueue); \
            QUE_ASSERT(prQueueEntry); \
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
            QUE_ASSERT(prQueue); \
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
            QUE_ASSERT(prDestQueue); \
            QUE_ASSERT(prSrcQueue); \
            *(P_QUE_T)prDestQueue = *(P_QUE_T)prSrcQueue; \
            QUEUE_INITIALIZE(prSrcQueue); \
        }

#define QUEUE_CONCATENATE_QUEUES(prDestQueue, prSrcQueue) \
        { \
            QUE_ASSERT(prDestQueue); \
            QUE_ASSERT(prSrcQueue); \
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

