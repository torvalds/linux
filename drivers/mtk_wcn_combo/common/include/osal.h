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

#ifndef _OSAL_H_
#define _OSAL_H_

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/
#define OS_BIT_OPS_SUPPORT (1)

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

#include "osal_typedef.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

#define MAX_THREAD_NAME_LEN (16)
#define MAX_WAKE_LOCK_NAME_LEN (16)
#define OSAL_OP_BUF_SIZE (64)
#define OSAL_OP_DATA_SIZE (32)
#define DBG_LOG_STR_SIZE (512)

#define OSAL_NAME_MAX (256)


/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/


#define osal_sizeof(x) sizeof(x)
#define osal_array_size(x) sizeof(x)/sizeof(x[0])


#define WMT_OP_BIT(x) (0x1UL << x)
#define WMT_OP_HIF_BIT WMT_OP_BIT(0)


#define RB_SIZE(prb) ((prb)->size)
#define RB_MASK(prb) (RB_SIZE(prb) - 1)
#define RB_COUNT(prb) ((prb)->write - (prb)->read)
#define RB_FULL(prb) (RB_COUNT(prb) >= RB_SIZE(prb))
#define RB_EMPTY(prb) ((prb)->write == (prb)->read)

#define RB_INIT(prb, qsize) \
   { \
   (prb)->read = (prb)->write = 0; \
   (prb)->size = (qsize); \
   }

#define RB_PUT(prb, value) \
{ \
    if (!RB_FULL( prb )) { \
        (prb)->queue[ (prb)->write & RB_MASK(prb) ] = value; \
        ++((prb)->write); \
    } \
    else { \
        osal_assert(!RB_FULL(prb)); \
    } \
}

#define RB_GET(prb, value) \
{ \
    if (!RB_EMPTY(prb)) { \
        value = (prb)->queue[ (prb)->read & RB_MASK(prb) ]; \
        ++((prb)->read); \
        if (RB_EMPTY(prb)) { \
            (prb)->read = (prb)->write = 0; \
        } \
    } \
    else { \
        value = NULL; \
        osal_assert(!RB_EMPTY(prb)); \
    } \
}


/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

typedef VOID  (*P_TIMEOUT_HANDLER)(ULONG);
typedef INT32 (*P_COND)(VOID *);

typedef struct _OSAL_TIMER_
{
    TIMER_REF pTimer;
    P_TIMEOUT_HANDLER timeoutHandler;
    ULONG timeroutHandlerData;
}OSAL_TIMER, *P_OSAL_TIMER;

typedef struct _OSAL_UNSLEEPABLE_LOCK_
{
    SPINLOCK_REF pLock;
    ULONG flag;
}OSAL_UNSLEEPABLE_LOCK, *P_OSAL_UNSLEEPABLE_LOCK;

typedef struct _OSAL_SLEEPABLE_LOCK_
{
    MUTEX_REF pLock;
}OSAL_SLEEPABLE_LOCK, *P_OSAL_SLEEPABLE_LOCK;


typedef struct _OSAL_SIGNAL_
{
    COMPLETION_REF pComp;
    UINT32 timeoutValue;
}OSAL_SIGNAL, *P_OSAL_SIGNAL;


typedef struct _OSAL_EVENT_
{
    WAITQUEUE_REF pWaitQueue;
    UINT32 timeoutValue;
    INT32 waitFlag;

}OSAL_EVENT, *P_OSAL_EVENT;

typedef struct _OSAL_THREAD_
{
    THREAD_REF pThread;
    VOID *pThreadFunc;
    VOID *pThreadData;
    char threadName[MAX_THREAD_NAME_LEN];
}OSAL_THREAD, *P_OSAL_THREAD;

typedef struct _OSAL_FIFO_
{
    /*fifo definition*/
    VOID  *pFifoBody;
    SPINLOCK_REF fifoSpinlock;
    /*fifo operations*/
    INT32 (*FifoInit)(struct _OSAL_FIFO_ *pFifo, UINT8 *buf, UINT32);
    INT32 (*FifoDeInit)(struct _OSAL_FIFO_  *pFifo);
    INT32 (*FifoReset)(struct _OSAL_FIFO_  *pFifo);
    INT32 (*FifoSz)(struct _OSAL_FIFO_  *pFifo);
    INT32 (*FifoAvailSz)(struct _OSAL_FIFO_  *pFifo);
    INT32 (*FifoLen)(struct _OSAL_FIFO_  *pFifo);
    INT32 (*FifoIsEmpty)(struct _OSAL_FIFO_  *pFifo);
    INT32 (*FifoIsFull)(struct _OSAL_FIFO_  *pFifo);
    INT32 (*FifoDataIn)(struct _OSAL_FIFO_  *pFifo, const VOID *buf, UINT32 len);
    INT32 (*FifoDataOut)(struct _OSAL_FIFO_  *pFifo, VOID *buf, UINT32 len);
} OSAL_FIFO, *P_OSAL_FIFO;

typedef struct _OSAL_FIRMWARE_
{
    UINT32 size;
    const UINT8 *data;
}OSAL_FIRMWARE,*P_OSAL_FIRMWARE;

typedef struct _OSAL_OP_DAT {
    UINT32 opId; // Event ID
    UINT32 u4InfoBit; // Reserved
    UINT32 au4OpData[OSAL_OP_DATA_SIZE]; // OP Data
} OSAL_OP_DAT, *P_OSAL_OP_DAT;

typedef struct _OSAL_LXOP_ {
    OSAL_OP_DAT op;
    OSAL_SIGNAL signal;
    INT32 result;
} OSAL_OP, *P_OSAL_OP;

typedef struct _OSAL_LXOP_Q {
    OSAL_SLEEPABLE_LOCK sLock;
    UINT32 write;
    UINT32 read;
    UINT32 size;
    P_OSAL_OP queue[OSAL_OP_BUF_SIZE];
} OSAL_OP_Q, *P_OSAL_OP_Q;

typedef struct _OSAL_WAKE_LOCK_
{
   WAKELOCK_REF pWakeLock;
   UINT8  name[MAX_WAKE_LOCK_NAME_LEN];
} OSAL_WAKE_LOCK, *P_OSAL_WAKE_LOCK;

typedef enum{
    OSAL_RETURN_OK = 0,
    OSAL_RETIRN_BAD_ADDRESS = -14,
}OSAL_RETURN_ERR;

typedef struct _OSAL_BIT_OP_VAR_
{
    ULONG data;
    OSAL_UNSLEEPABLE_LOCK opLock;
}OSAL_BIT_OP_VAR, *P_OSAL_BIT_OP_VAR;

typedef UINT32 (*P_OSAL_EVENT_CHECKER)(P_OSAL_THREAD pThread);

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

extern UINT32 osal_strlen(const char *str);
extern INT32 osal_strcmp(const char *dst, const char *src);
extern INT32 osal_strncmp(const char *dst, const char *src, UINT32 len);
extern char * osal_strcpy(char *dst, const char *src);
extern char * osal_strncpy(char *dst, const char *src, UINT32 len);
extern char * osal_strcat(char *dst, const char *src);
extern char * osal_strncat(char *dst, const char *src, UINT32 len);
extern char * osal_strchr(const char *str, UINT8 c);
extern char * osal_strsep(char **str, const char *c);
extern LONG osal_strtol(const char *str, char **c, UINT32 adecimal);
extern INT32 osal_snprintf(char *buf, UINT32 len, const char*fmt, ...);

extern INT32 osal_print(const char *str, ...);
extern INT32 osal_dbg_print(const char *str, ...);
extern INT32 osal_err_print(const char *str, ...);
extern INT32 osal_info_print(const char *str, ...);
extern INT32 osal_warn_print(const char *str, ...);
extern INT32 osal_loud_print(const char *str, ...);


extern INT32 osal_dbg_assert(INT32 expr, const char *file, INT32 line);
extern INT32 osal_sprintf(char *str, const char *format, ...);
extern VOID* osal_malloc(UINT32 size);
extern VOID  osal_vfree(const VOID *dst);
extern VOID  osal_kfree(const VOID  *dst);
extern VOID* osal_memset(VOID *buf, INT32 i, UINT32 len);
extern VOID* osal_memcpy(VOID *dst, const VOID *src, UINT32 len);
extern INT32 osal_memcmp(const VOID *buf1, const VOID *buf2, UINT32 len);
extern VOID * osal_kzalloc_sleep(UINT32 size);
extern VOID* osal_kzalloc_unsleep(UINT32 size);

extern INT32 osal_msleep(UINT32 ms);

extern INT32 osal_timer_create(P_OSAL_TIMER);
extern INT32 osal_timer_start(P_OSAL_TIMER, UINT32);
extern INT32 osal_timer_stop(P_OSAL_TIMER);
extern INT32 osal_timer_stop_sync(P_OSAL_TIMER pTimer);
extern INT32 osal_timer_modify(P_OSAL_TIMER, UINT32);
extern INT32 osal_timer_delete(P_OSAL_TIMER);

extern INT32  osal_fifo_init(P_OSAL_FIFO pFifo, UINT8 *buffer, UINT32 size);
extern VOID   osal_fifo_deinit(P_OSAL_FIFO pFifo);
extern INT32  osal_fifo_reset(P_OSAL_FIFO pFifo);
extern UINT32 osal_fifo_in(P_OSAL_FIFO pFifo, PUINT8 buffer, UINT32 size);
extern UINT32 osal_fifo_out(P_OSAL_FIFO pFifo, PUINT8 buffer, UINT32 size);
extern UINT32 osal_fifo_len(P_OSAL_FIFO pFifo);
extern UINT32 osal_fifo_sz(P_OSAL_FIFO pFifo);
extern UINT32 osal_fifo_avail(P_OSAL_FIFO pFifo);
extern UINT32 osal_fifo_is_empty(P_OSAL_FIFO pFifo);
extern UINT32 osal_fifo_is_full(P_OSAL_FIFO pFifo);

extern INT32  osal_wake_lock_init(P_OSAL_WAKE_LOCK plock);
extern INT32  osal_wake_lock_deinit(P_OSAL_WAKE_LOCK plock);
extern INT32  osal_wake_lock(P_OSAL_WAKE_LOCK plock);
extern INT32  osal_wake_unlock(P_OSAL_WAKE_LOCK plock);
extern INT32  osal_wake_lock_count(P_OSAL_WAKE_LOCK plock);

extern INT32 osal_unsleepable_lock_init (P_OSAL_UNSLEEPABLE_LOCK );
extern INT32 osal_lock_unsleepable_lock (P_OSAL_UNSLEEPABLE_LOCK );
extern INT32 osal_unlock_unsleepable_lock (P_OSAL_UNSLEEPABLE_LOCK );
extern INT32 osal_unsleepable_lock_deinit (P_OSAL_UNSLEEPABLE_LOCK );

extern INT32 osal_sleepable_lock_init (P_OSAL_SLEEPABLE_LOCK );
extern INT32 osal_lock_sleepable_lock (P_OSAL_SLEEPABLE_LOCK );
extern INT32 osal_unlock_sleepable_lock (P_OSAL_SLEEPABLE_LOCK );
extern INT32 osal_sleepable_lock_deinit (P_OSAL_SLEEPABLE_LOCK );

extern INT32 osal_signal_init (P_OSAL_SIGNAL);
extern INT32 osal_wait_for_signal (P_OSAL_SIGNAL);
extern INT32
osal_wait_for_signal_timeout (
    P_OSAL_SIGNAL
    );
extern INT32
osal_raise_signal (
    P_OSAL_SIGNAL
    );
extern INT32
osal_signal_deinit (
    P_OSAL_SIGNAL
    );

extern INT32 osal_event_init(P_OSAL_EVENT);
extern INT32 osal_wait_for_event(P_OSAL_EVENT, P_COND , void *);
extern INT32 osal_wait_for_event_timeout(P_OSAL_EVENT , P_COND , void *);
extern INT32 osal_trigger_event(P_OSAL_EVENT);

extern INT32 osal_event_deinit (P_OSAL_EVENT);

extern INT32 osal_thread_create(P_OSAL_THREAD);
extern INT32 osal_thread_run(P_OSAL_THREAD);
extern INT32 osal_thread_should_stop(P_OSAL_THREAD);
extern INT32 osal_thread_stop(P_OSAL_THREAD);
/*extern INT32 osal_thread_wait_for_event(P_OSAL_THREAD, P_OSAL_EVENT);*/
extern INT32 osal_thread_wait_for_event(P_OSAL_THREAD, P_OSAL_EVENT, P_OSAL_EVENT_CHECKER);

/*check pOsalLxOp and OSAL_THREAD_SHOULD_STOP*/
extern INT32 osal_thread_destroy(P_OSAL_THREAD);

extern INT32 osal_clear_bit(UINT32 bitOffset, P_OSAL_BIT_OP_VAR pData);
extern INT32 osal_set_bit(UINT32 bitOffset, P_OSAL_BIT_OP_VAR pData);
extern INT32 osal_test_bit(UINT32 bitOffset, P_OSAL_BIT_OP_VAR pData);
extern INT32 osal_test_and_clear_bit(UINT32 bitOffset, P_OSAL_BIT_OP_VAR pData);
extern INT32 osal_test_and_set_bit(UINT32 bitOffset, P_OSAL_BIT_OP_VAR pData);

extern INT32 osal_dbg_assert_aee(const char *module, const char *detail_description);
extern INT32 osal_gettimeofday(PINT32 sec, PINT32 usec);
extern INT32 osal_printtimeofday(const PUINT8 prefix);

extern VOID
osal_buffer_dump (
    const UINT8 *buf,
    const UINT8 *title,
    UINT32 len,
    UINT32 limit
    );

extern UINT32 osal_op_get_id(P_OSAL_OP pOp);
extern MTK_WCN_BOOL osal_op_is_wait_for_signal(P_OSAL_OP pOp);
extern VOID osal_op_raise_signal(P_OSAL_OP pOp, INT32 result);

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#define osal_assert(condition) if (!(condition)) {osal_err_print("%s, %d, (%s)\n", __FILE__, __LINE__, #condition);}

#endif /* _OSAL_H_ */

