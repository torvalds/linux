#ifndef __WILC_platfrom_H__
#define __WILC_platfrom_H__

/*!
 *  @file	wilc_platform.h
 *  @brief	platform specific file for Linux port
 *  @author	syounan
 *  @sa		wilc_oswrapper.h top level OS wrapper file
 *  @date	15 Dec 2010
 *  @version	1.0
 */


/******************************************************************
 *      Feature support checks
 *******************************************************************/

/* CONFIG_WILC_THREAD_FEATURE is implemented */

/* remove the following block when implementing its feature */
#ifdef CONFIG_WILC_THREAD_SUSPEND_CONTROL
#error This feature is not supported by this OS
#endif

/* remove the following block when implementing its feature */
#ifdef CONFIG_WILC_THREAD_STRICT_PRIORITY
#error This feature is not supported by this OS
#endif

/* CONFIG_WILC_SEMAPHORE_FEATURE is implemented */

/* remove the following block when implementing its feature
 * #ifdef CONFIG_WILC_SEMAPHORE_TIMEOUT
 * #error This feature is not supported by this OS
 #endif*/

/* CONFIG_WILC_SLEEP_FEATURE is implemented */

/* remove the following block when implementing its feature */
/* #ifdef CONFIG_WILC_SLEEP_HI_RES */
/* #error This feature is not supported by this OS */
/* #endif */

/* CONFIG_WILC_TIMER_FEATURE is implemented */

/* CONFIG_WILC_TIMER_PERIODIC is implemented */

/* CONFIG_WILC_MEMORY_FEATURE is implemented */

/* remove the following block when implementing its feature */
#ifdef CONFIG_WILC_MEMORY_POOLS
#error This feature is not supported by this OS
#endif

/* remove the following block when implementing its feature */
#ifdef CONFIG_WILC_MEMORY_DEBUG
#error This feature is not supported by this OS
#endif

/* remove the following block when implementing its feature */
#ifdef CONFIG_WILC_ASSERTION_SUPPORT
#error This feature is not supported by this OS
#endif

/* CONFIG_WILC_STRING_UTILS is implemented */

/* CONFIG_WILC_MSG_QUEUE_FEATURE is implemented */

/* remove the following block when implementing its feature */
#ifdef CONFIG_WILC_MSG_QUEUE_IPC_NAME
#error This feature is not supported by this OS
#endif

/* remove the following block when implementing its feature */
/*#ifdef CONFIG_WILC_MSG_QUEUE_TIMEOUT
 * #error This feature is not supported by this OS
 #endif*/

/* CONFIG_WILC_FILE_OPERATIONS_FEATURE is implemented */

/* CONFIG_WILC_FILE_OPERATIONS_STRING_API is implemented */

/* remove the following block when implementing its feature */
#ifdef CONFIG_WILC_FILE_OPERATIONS_PATH_API
#error This feature is not supported by this OS
#endif

/* CONFIG_WILC_TIME_FEATURE is implemented */

/* remove the following block when implementing its feature */
#ifdef CONFIG_WILC_TIME_UTC_SINCE_1970
#error This feature is not supported by this OS
#endif

/* remove the following block when implementing its feature */
#ifdef CONFIG_WILC_TIME_CALENDER
#error This feature is not supported by this OS
#endif

/* remove the following block when implementing its feature */
#ifdef CONFIG_WILC_EVENT_FEATURE
#error This feature is not supported by this OS
#endif

/* remove the following block when implementing its feature */
#ifdef CONFIG_WILC_EVENT_TIMEOUT
#error This feature is not supported by this OS
#endif

/* CONFIG_WILC_MATH_OPERATIONS_FEATURE is implemented */

/* CONFIG_WILC_EXTENDED_FILE_OPERATIONS is implemented */

/* CONFIG_WILC_EXTENDED_STRING_OPERATIONS is implemented */

/* CONFIG_WILC_EXTENDED_TIME_OPERATIONS is implemented */

/* remove the following block when implementing its feature */
#ifdef CONFIG_WILC_SOCKET_FEATURE
#error This feature is not supported by this OS
#endif

/******************************************************************
 *      OS specific includes
 *******************************************************************/
#define _XOPEN_SOURCE 600

#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/time.h>
#include <linux/version.h>
#include "linux/string.h"
/******************************************************************
 *      OS specific types
 *******************************************************************/

typedef struct task_struct *WILC_ThreadHandle;

typedef void *WILC_MemoryPoolHandle;
typedef struct semaphore WILC_SemaphoreHandle;

typedef struct timer_list WILC_TimerHandle;



/* Message Queue type is a structure */
typedef struct __Message_struct {
	void *pvBuffer;
	WILC_Uint32 u32Length;
	struct __Message_struct *pstrNext;
} Message;

typedef struct __MessageQueue_struct {
	WILC_SemaphoreHandle hSem;
	spinlock_t strCriticalSection;
	WILC_Bool bExiting;
	WILC_Uint32 u32ReceiversCount;
	Message *pstrMessageList;
} WILC_MsgQueueHandle;



/*Time represented in 64 bit format*/
typedef time_t WILC_Time;


/*******************************************************************
 *      others
 ********************************************************************/

/* Generic printf function */
#define __WILC_FILE__		__FILE__
#define __WILC_FUNCTION__	__FUNCTION__
#define __WILC_LINE__		__LINE__
#endif
