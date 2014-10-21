/* uisqueue.c
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/* @ALL_INSPECTED */
#include <linux/kernel.h>
#include <linux/module.h>

#include "uisutils.h"

#include "chanstub.h"

/* this is shorter than using __FILE__ (full path name) in
 * debug/info/error messages */
#define CURRENT_FILE_PC UISLIB_PC_uisqueue_c
#define __MYFILE__ "uisqueue.c"

#define CHECK_CACHE_ALIGN 0

/*****************************************************/
/* Exported functions                                */
/*****************************************************/
unsigned long long
uisqueue_interlocked_or(unsigned long long __iomem *tgt,
		       unsigned long long set)
{
	unsigned long long i;
	unsigned long long j;

	j = readq(tgt);
	do {
		i = j;
		j = cmpxchg((__force unsigned long long *)tgt, i, i | set);

	} while (i != j);

	return j;
}
EXPORT_SYMBOL_GPL(uisqueue_interlocked_or);

unsigned long long
uisqueue_interlocked_and(unsigned long long __iomem *tgt,
			unsigned long long set)
{
	unsigned long long i;
	unsigned long long j;

	j = readq(tgt);
	do {
		i = j;
		j = cmpxchg((__force unsigned long long *)tgt, i, i & set);

	} while (i != j);

	return j;
}
EXPORT_SYMBOL_GPL(uisqueue_interlocked_and);

static u8
do_locked_client_insert(struct uisqueue_info *queueinfo,
			unsigned int whichqueue,
			void *pSignal,
			spinlock_t *lock,
			unsigned char issueInterruptIfEmpty,
			u64 interruptHandle, u8 *channelId)
{
	unsigned long flags;
	u8 rc = 0;

	spin_lock_irqsave(lock, flags);
	if (!ULTRA_CHANNEL_CLIENT_ACQUIRE_OS(queueinfo->chan, channelId, NULL))
		goto unlock;
	if (visor_signal_insert(queueinfo->chan, whichqueue, pSignal)) {
		queueinfo->packets_sent++;
		rc = 1;
	}
	ULTRA_CHANNEL_CLIENT_RELEASE_OS(queueinfo->chan, channelId, NULL);
unlock:
	spin_unlock_irqrestore((spinlock_t *)lock, flags);
	return rc;
}

int
uisqueue_put_cmdrsp_with_lock_client(struct uisqueue_info *queueinfo,
				     struct uiscmdrsp *cmdrsp,
				     unsigned int whichqueue,
				     void *insertlock,
				     unsigned char issue_irq_if_empty,
				     u64 irq_handle,
				     char oktowait, u8 *channel_id)
{
	while (!do_locked_client_insert(queueinfo, whichqueue, cmdrsp,
					(spinlock_t *) insertlock,
					issue_irq_if_empty,
					irq_handle, channel_id)) {
		if (oktowait != OK_TO_WAIT) {
			LOGERR("****FAILED visor_signal_insert failed; cannot wait; insert aborted\n");
			return 0;	/* failed to queue */
		}
		/* try again */
		LOGERR("****FAILED visor_signal_insert failed; waiting to try again\n");
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(10));
	}
	return 1;
}
EXPORT_SYMBOL_GPL(uisqueue_put_cmdrsp_with_lock_client);

/* uisqueue_get_cmdrsp gets the cmdrsp entry at the head of the queue
 * returns NULL if queue is empty */
int
uisqueue_get_cmdrsp(struct uisqueue_info *queueinfo,
		    void *cmdrsp, unsigned int whichqueue)
{
	if (!visor_signal_remove(queueinfo->chan, whichqueue, cmdrsp))
		return 0;

	queueinfo->packets_received++;

	return 1;		/* Success */
}
EXPORT_SYMBOL_GPL(uisqueue_get_cmdrsp);
