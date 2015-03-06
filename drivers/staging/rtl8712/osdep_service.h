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

#define _init_queue(pqueue)				\
	do {						\
		INIT_LIST_HEAD(&((pqueue)->queue));	\
		spin_lock_init(&((pqueue)->lock));	\
	} while (0)

#define LIST_CONTAINOR(ptr, type, member) \
	((type *)((char *)(ptr)-(SIZE_T)(&((type *)0)->member)))

static inline void _cancel_timer(struct timer_list *ptimer, u8 *bcancelled)
{
	del_timer(ptimer);
	*bcancelled = true; /*true ==1; false==0*/
}

#ifndef BIT
	#define BIT(x)	(1 << (x))
#endif

static inline u32 _down_sema(struct semaphore *sema)
{
	if (down_interruptible(sema))
		return _FAIL;
	return _SUCCESS;
}

static inline u32 end_of_queue_search(struct list_head *head,
		struct list_head *plist)
{
	return (head == plist);
}

static inline void sleep_schedulable(int ms)
{
	u32 delta;

	delta = msecs_to_jiffies(ms);/*(ms)*/
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(delta);
}

static inline void flush_signals_thread(void)
{
	if (signal_pending(current))
		flush_signals(current);
}

#endif

