#ifndef __WILC_platfrom_H__
#define __WILC_platfrom_H__

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

typedef struct timer_list WILC_TimerHandle;



/* Message Queue type is a structure */
typedef struct __Message_struct {
	void *pvBuffer;
	u32 u32Length;
	struct __Message_struct *pstrNext;
} Message;

typedef struct __MessageQueue_struct {
	struct semaphore hSem;
	spinlock_t strCriticalSection;
	bool bExiting;
	u32 u32ReceiversCount;
	Message *pstrMessageList;
} WILC_MsgQueueHandle;



/*Time represented in 64 bit format*/
typedef time_t WILC_Time;


/*******************************************************************
 *      others
 ********************************************************************/

/* Generic printf function */
#define __WILC_FILE__		__FILE__
#define __WILC_FUNCTION__	__func__
#define __WILC_LINE__		__LINE__
#endif
