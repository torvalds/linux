/*! \file
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/



#ifndef _OSAL_H_
#define _OSAL_H_

#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>
#include <linux/kthread.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#if WMT_PLAT_ALPS
#include <linux/aee.h>
#endif
#include <linux/kfifo.h>
#include <linux/wakelock.h>
#include <linux/log2.h>
#include <osal_typedef.h>
#include <asm/atomic.h>

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

#define OS_BIT_OPS_SUPPORT 1

#define _osal_inline_ inline

#define MAX_THREAD_NAME_LEN 16
#define MAX_WAKE_LOCK_NAME_LEN 16
#define OSAL_OP_BUF_SIZE    64
#define OSAL_OP_DATA_SIZE   32
#define DBG_LOG_STR_SIZE    512

#define osal_sizeof(x) sizeof(x)

#define osal_array_size(x) sizeof(x)/sizeof(x[0])

#ifndef NAME_MAX
#define NAME_MAX 256
#endif


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



typedef VOID  (*P_TIMEOUT_HANDLER)(ULONG);
typedef INT32 (*P_COND)(VOID *);

typedef struct _OSAL_TIMER_
{
    struct timer_list timer;
    P_TIMEOUT_HANDLER timeoutHandler;
    ULONG timeroutHandlerData;
}OSAL_TIMER, *P_OSAL_TIMER;

typedef struct _OSAL_UNSLEEPABLE_LOCK_
{
    spinlock_t lock;
    ULONG flag;
}OSAL_UNSLEEPABLE_LOCK, *P_OSAL_UNSLEEPABLE_LOCK;

typedef struct _OSAL_SLEEPABLE_LOCK_
{
    struct mutex lock;
}OSAL_SLEEPABLE_LOCK, *P_OSAL_SLEEPABLE_LOCK;


typedef struct _OSAL_SIGNAL_
{
    struct completion comp;
    UINT32 timeoutValue;
}OSAL_SIGNAL, *P_OSAL_SIGNAL;


typedef struct _OSAL_EVENT_
{
    wait_queue_head_t waitQueue;
//    VOID *pWaitQueueData;
    UINT32 timeoutValue;
    INT32 waitFlag;

}OSAL_EVENT, *P_OSAL_EVENT;

typedef struct _OSAL_THREAD_
{
    struct task_struct *pThread;
    VOID *pThreadFunc;
    VOID *pThreadData;
    char threadName[MAX_THREAD_NAME_LEN];
}OSAL_THREAD, *P_OSAL_THREAD;

typedef struct _OSAL_FIFO_
{
    /*fifo definition*/
    VOID  *pFifoBody;
    spinlock_t fifoSpinlock;
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
    INT32 (*FifoDataOut)(struct _OSAL_FIFO_  *pFifo, void *buf, UINT32 len);  
} OSAL_FIFO, *P_OSAL_FIFO;

typedef struct firmware osal_firmware;

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
   struct wake_lock        wake_lock; 
   UINT8  name[MAX_WAKE_LOCK_NAME_LEN];
} OSAL_WAKE_LOCK, *P_OSAL_WAKE_LOCK;
#if 1
typedef struct _OSAL_BIT_OP_VAR_
{
    ULONG data;
    OSAL_UNSLEEPABLE_LOCK opLock;
}OSAL_BIT_OP_VAR, *P_OSAL_BIT_OP_VAR;
#else
#define OSAL_BIT_OP_VAR ULONG
#define P_OSAL_BIT_OP_VAR ULONG*


#endif
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
extern void osal_bug_on(unsigned long val);

extern LONG osal_strtol(const char *str, char **c, UINT32 adecimal);
extern INT32 osal_snprintf(char *buf, UINT32 len, const char*fmt, ...);

extern INT32 osal_print(const char *str, ...);
extern INT32 osal_dbg_print(const char *str, ...);


extern INT32 osal_dbg_assert(INT32 expr, const char *file, INT32 line);
extern INT32 osal_sprintf(char *str, const char *format, ...);
extern VOID* osal_malloc(UINT32 size);
extern VOID  osal_free(const VOID *dst);
extern VOID* osal_memset(VOID *buf, INT32 i, UINT32 len);
extern VOID* osal_memcpy(VOID *dst, const VOID *src, UINT32 len);
extern INT32 osal_memcmp(const VOID *buf1, const VOID *buf2, UINT32 len);

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
extern INT32  osal_wake_lock(P_OSAL_WAKE_LOCK plock); 
extern INT32  osal_wake_unlock(P_OSAL_WAKE_LOCK plock);
extern INT32  osal_wake_lock_count(P_OSAL_WAKE_LOCK plock);

#if defined(CONFIG_PROVE_LOCKING)
#define osal_unsleepable_lock_init(l) { spin_lock_init(&((l)->lock));}
#else
extern INT32 osal_unsleepable_lock_init (P_OSAL_UNSLEEPABLE_LOCK );
#endif
extern INT32 osal_lock_unsleepable_lock (P_OSAL_UNSLEEPABLE_LOCK );
extern INT32 osal_unlock_unsleepable_lock (P_OSAL_UNSLEEPABLE_LOCK );
extern INT32 osal_unsleepable_lock_deinit (P_OSAL_UNSLEEPABLE_LOCK );

#if defined(CONFIG_PROVE_LOCKING)
#define osal_sleepable_lock_init(l) { mutex_init(&((l)->lock));}
#else
extern INT32 osal_sleepable_lock_init (P_OSAL_SLEEPABLE_LOCK );
#endif
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

extern UINT16 osal_crc16(const UINT8 *buffer, const UINT32 length);
/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#define osal_err_print(fmt, arg...) osal_print(KERN_ERR fmt, ##arg)
#define osal_warn_print(fmt, arg...) osal_print(KERN_ERR fmt, ##arg)
#define osal_info_print(fmt, arg...) osal_print(KERN_ERR fmt, ##arg)
#define osal_load_print(fmt, arg...) osal_print(KERN_DEBUG fmt, ##arg)
#define osal_assert(condition) if (!(condition)) {osal_err_print("%s, %d, (%s)\n", __FILE__, __LINE__, #condition);}

#endif /* _OSAL_H_ */

