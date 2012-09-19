#ifndef CSR_FRAMEWORK_EXT_TYPES_H__
#define CSR_FRAMEWORK_EXT_TYPES_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#ifdef __KERNEL__
#include <linux/kthread.h>
#include <linux/semaphore.h>
#else
#include <pthread.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif


#ifdef __KERNEL__

struct CsrThread
{
    struct task_struct *thread_task;
    char                name[16];
};

struct CsrEvent
{
    /* wait_queue for waking the kernel thread */
    wait_queue_head_t wakeup_q;
    unsigned int      wakeup_flag;
};

typedef struct CsrEvent CsrEventHandle;
typedef struct semaphore CsrMutexHandle;
typedef struct CsrThread CsrThreadHandle;

#else /* __KERNEL __ */

struct CsrEvent
{
    pthread_cond_t  event;
    pthread_mutex_t mutex;
    u32       eventBits;
};

typedef struct CsrEvent CsrEventHandle;
typedef pthread_mutex_t CsrMutexHandle;
typedef pthread_t CsrThreadHandle;

#endif /* __KERNEL__ */

#ifdef __cplusplus
}
#endif

#endif
