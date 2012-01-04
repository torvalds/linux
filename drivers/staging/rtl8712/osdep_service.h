/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef __OSDEP_SERVICE_H_
#define __OSDEP_SERVICE_H_

#define _SUCCESS	1
#define _FAIL		0

#include <linux/version.h>
#include <linux/spinlock.h>

#include <linux/interrupt.h>
#include <linux/semaphore.h>
#include <linux/sched.h>
#include <linux/sem.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <net/iw_handler.h>
#include <linux/proc_fs.h>      /* Necessary because we use the proc fs */

#include "basic_types.h"

struct	__queue	{
	struct	list_head	queue;
	spinlock_t lock;
};

#define _pkt struct sk_buff
#define _buffer unsigned char
#define thread_exit() complete_and_exit(NULL, 0)
#define _workitem struct work_struct

#define _init_queue(pqueue)				\
	do {						\
		_init_listhead(&((pqueue)->queue));	\
		spin_lock_init(&((pqueue)->lock));	\
	} while (0)

static inline struct list_head *get_next(struct list_head *list)
{
	return list->next;
}

static inline struct list_head *get_list_head(struct  __queue *queue)
{
	return &(queue->queue);
}

#define LIST_CONTAINOR(ptr, type, member) \
	((type *)((char *)(ptr)-(SIZE_T)(&((type *)0)->member)))

static inline void _enter_hwio_critical(struct semaphore *prwlock,
					unsigned long *pirqL)
{
	down(prwlock);
}

static inline void _exit_hwio_critical(struct semaphore *prwlock,
				       unsigned long *pirqL)
{
	up(prwlock);
}

static inline void list_delete(struct list_head *plist)
{
	list_del_init(plist);
}

static inline void _init_timer(struct timer_list *ptimer,
			       struct  net_device *padapter,
			       void *pfunc, void *cntx)
{
	ptimer->function = pfunc;
	ptimer->data = (addr_t)cntx;
	init_timer(ptimer);
}

static inline void _set_timer(struct timer_list *ptimer, u32 delay_time)
{
	mod_timer(ptimer, (jiffies+(delay_time*HZ/1000)));
}

static inline void _cancel_timer(struct timer_list *ptimer, u8 *bcancelled)
{
	del_timer(ptimer);
	*bcancelled = true; /*true ==1; false==0*/
}

static inline void _init_workitem(_workitem *pwork, void *pfunc, void *cntx)
{
	INIT_WORK(pwork, pfunc);
}

static inline void _set_workitem(_workitem *pwork)
{
	schedule_work(pwork);
}

#include "rtl871x_byteorder.h"

#ifndef BIT
	#define BIT(x)	(1 << (x))
#endif

/*
For the following list_xxx operations,
caller must guarantee the atomic context.
Otherwise, there will be racing condition.
*/
static inline u32 is_list_empty(struct list_head *phead)
{
	if (list_empty(phead))
		return true;
	else
		return false;
}

static inline void list_insert_tail(struct list_head *plist,
				    struct list_head *phead)
{
	list_add_tail(plist, phead);
}

static inline u32 _down_sema(struct semaphore *sema)
{
	if (down_interruptible(sema))
		return _FAIL;
	else
		return _SUCCESS;
}

static inline void _rtl_rwlock_init(struct semaphore *prwlock)
{
	sema_init(prwlock, 1);
}

static inline void _init_listhead(struct list_head *list)
{
	INIT_LIST_HEAD(list);
}

static inline u32 _queue_empty(struct  __queue *pqueue)
{
	return is_list_empty(&(pqueue->queue));
}

static inline u32 end_of_queue_search(struct list_head *head, struct list_head *plist)
{
	if (head == plist)
		return true;
	else
		return false;
}

static inline void sleep_schedulable(int ms)
{
	u32 delta;

	delta = (ms * HZ) / 1000;/*(ms)*/
	if (delta == 0)
		delta = 1;/* 1 ms */
	set_current_state(TASK_INTERRUPTIBLE);
	if (schedule_timeout(delta) != 0)
		return ;
}

static inline u8 *_malloc(u32 sz)
{
	return	kmalloc(sz, GFP_ATOMIC);
}

static inline unsigned char _cancel_timer_ex(struct timer_list *ptimer)
{
	return del_timer(ptimer);
}

static inline void thread_enter(void *context)
{
	allow_signal(SIGTERM);
}

static inline void flush_signals_thread(void)
{
	if (signal_pending(current))
		flush_signals(current);
}

static inline u32 _RND8(u32 sz)
{
	return ((sz >> 3) + ((sz & 7) ? 1 : 0)) << 3;
}

static inline u32 _RND128(u32 sz)
{
	return ((sz >> 7) + ((sz & 127) ? 1 : 0)) << 7;
}

static inline u32 _RND256(u32 sz)
{
	return ((sz >> 8) + ((sz & 255) ? 1 : 0)) << 8;
}

static inline u32 _RND512(u32 sz)
{
	return ((sz >> 9) + ((sz & 511) ? 1 : 0)) << 9;
}

#endif

