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
uisqueue_InterlockedOr(unsigned long long __iomem *Target,
		       unsigned long long Set)
{
	unsigned long long i;
	unsigned long long j;

	j = readq(Target);
	do {
		i = j;
		j = uislibcmpxchg64((__force unsigned long long *)Target,
				    i, i | Set, sizeof(*(Target)));

	} while (i != j);

	return j;
}
EXPORT_SYMBOL_GPL(uisqueue_InterlockedOr);

unsigned long long
uisqueue_InterlockedAnd(unsigned long long __iomem *Target,
			unsigned long long Set)
{
	unsigned long long i;
	unsigned long long j;

	j = readq(Target);
	do {
		i = j;
		j = uislibcmpxchg64((__force unsigned long long *)Target,
				    i, i & Set, sizeof(*(Target)));

	} while (i != j);

	return j;
}
EXPORT_SYMBOL_GPL(uisqueue_InterlockedAnd);

static U8
do_locked_client_insert(struct uisqueue_info *queueinfo,
			unsigned int whichqueue,
			void *pSignal,
			spinlock_t *lock,
			unsigned char issueInterruptIfEmpty,
			U64 interruptHandle, U8 *channelId)
{
	unsigned long flags;
	unsigned char queueWasEmpty;
	unsigned int locked = 0;
	unsigned int acquired = 0;
	U8 rc = 0;

	spin_lock_irqsave(lock, flags);
	locked = 1;

	if (!ULTRA_CHANNEL_CLIENT_ACQUIRE_OS(queueinfo->chan, channelId, NULL))
		goto Away;

	acquired = 1;

	queueWasEmpty = visor_signalqueue_empty(queueinfo->chan, whichqueue);
	if (!visor_signal_insert(queueinfo->chan, whichqueue, pSignal))
		goto Away;
	ULTRA_CHANNEL_CLIENT_RELEASE_OS(queueinfo->chan, channelId, NULL);
	acquired = 0;
	spin_unlock_irqrestore(lock, flags);
	locked = 0;

	queueinfo->packets_sent++;

	rc = 1;
Away:
	if (acquired) {
		ULTRA_CHANNEL_CLIENT_RELEASE_OS(queueinfo->chan, channelId,
						NULL);
		acquired = 0;
	}
	if (locked) {
		spin_unlock_irqrestore((spinlock_t *) lock, flags);
		locked = 0;
	}

	return rc;
}

int
uisqueue_put_cmdrsp_with_lock_client(struct uisqueue_info *queueinfo,
				     struct uiscmdrsp *cmdrsp,
				     unsigned int whichqueue,
				     void *insertlock,
				     unsigned char issueInterruptIfEmpty,
				     U64 interruptHandle,
				     char oktowait, U8 *channelId)
{
	while (!do_locked_client_insert(queueinfo, whichqueue, cmdrsp,
					(spinlock_t *) insertlock,
					issueInterruptIfEmpty,
					interruptHandle, channelId)) {
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
