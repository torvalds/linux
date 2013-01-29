#ifndef CSR_SCHED_H__
#define CSR_SCHED_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/
#include <linux/types.h>
#include "csr_time.h"

/* An identifier issued by the scheduler. */
typedef u32 CsrSchedIdentifier;

/* A task identifier */
typedef u16 CsrSchedTaskId;

/* A queue identifier */
typedef u16 CsrSchedQid;

/* A message identifier */
typedef CsrSchedIdentifier CsrSchedMsgId;

/* A timer event identifier */
typedef CsrSchedIdentifier CsrSchedTid;
#define CSR_SCHED_TID_INVALID     ((CsrSchedTid) 0)

/* Time constants. */
#define CSR_SCHED_TIME_MAX                (0xFFFFFFFF)
#define CSR_SCHED_MILLISECOND             (1000)
#define CSR_SCHED_SECOND                  (1000 * CSR_SCHED_MILLISECOND)
#define CSR_SCHED_MINUTE                  (60 * CSR_SCHED_SECOND)

/* Queue and primitive that identifies the environment */
#define CSR_SCHED_TASK_ID        0xFFFF
#define CSR_SCHED_PRIM                   (CSR_SCHED_TASK_ID)
#define CSR_SCHED_EXCLUDED_MODULE_QUEUE      0xFFFF

/*
 * Background interrupt definitions
 */
typedef u16 CsrSchedBgint;
#define CSR_SCHED_BGINT_INVALID ((CsrSchedBgint) 0xFFFF)

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSchedMessagePut
 *
 *  DESCRIPTION
 *      Sends a message consisting of the integer "mi" and the void * pointer
 *      "mv" to the message queue "q".
 *
 *      "mi" and "mv" are neither inspected nor changed by the scheduler - the
 *      task that owns "q" is expected to make sense of the values. "mv" may
 *      be null.
 *
 *  NOTE
 *      If "mv" is not null then it will typically be a chunk of kmalloc()ed
 *      memory, though there is no need for it to be so. Tasks should normally
 *      obey the convention that when a message built with kmalloc()ed memory
 *      is given to CsrSchedMessagePut() then ownership of the memory is ceded to the
 *      scheduler - and eventually to the recipient task. I.e., the receiver of
 *      the message will be expected to kfree() the message storage.
 *
 *  RETURNS
 *      void.
 *
 *----------------------------------------------------------------------------*/
#if defined(CSR_LOG_ENABLE) && defined(CSR_LOG_INCLUDE_FILE_NAME_AND_LINE_NUMBER)
void CsrSchedMessagePutStringLog(CsrSchedQid q,
    u16 mi,
    void *mv,
    u32 line,
    const char *file);
#define CsrSchedMessagePut(q, mi, mv) CsrSchedMessagePutStringLog((q), (mi), (mv), __LINE__, __FILE__)
#else
void CsrSchedMessagePut(CsrSchedQid q,
    u16 mi,
    void *mv);
#endif

#endif
