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

/* this is shorter than using __FILE__ (full path name) in
 * debug/info/error messages */
#define CURRENT_FILE_PC UISLIB_PC_uisqueue_c
#define __MYFILE__ "uisqueue.c"

#define CHECK_CACHE_ALIGN 0

/*****************************************************/
/* Exported functions                                */
/*****************************************************/

/*
 * Routine Description:
 * Tries to insert the prebuilt signal pointed to by pSignal into the nth
 * Queue of the Channel pointed to by pChannel
 *
 * Parameters:
 * pChannel: (IN) points to the IO Channel
 * Queue: (IN) nth Queue of the IO Channel
 * pSignal: (IN) pointer to the signal
 *
 * Assumptions:
 * - pChannel, Queue and pSignal are valid.
 * - If insertion fails due to a full queue, the caller will determine the
 * retry policy (e.g. wait & try again, report an error, etc.).
 *
 * Return value:
 * 1 if the insertion succeeds, 0 if the queue was full.
 */
unsigned char spar_signal_insert(struct channel_header __iomem *ch, u32 queue,
				 void *sig)
{
	void __iomem *psignal;
	unsigned int head, tail, nof;

	struct signal_queue_header __iomem *pqhdr =
	    (struct signal_queue_header __iomem *)
		((char __iomem *)ch + readq(&ch->ch_space_offset))
		+ queue;

	/* capture current head and tail */
	head = readl(&pqhdr->head);
	tail = readl(&pqhdr->tail);

	/* queue is full if (head + 1) % n equals tail */
	if (((head + 1) % readl(&pqhdr->max_slots)) == tail) {
		nof = readq(&pqhdr->num_overflows) + 1;
		writeq(nof, &pqhdr->num_overflows);
		return 0;
	}

	/* increment the head index */
	head = (head + 1) % readl(&pqhdr->max_slots);

	/* copy signal to the head location from the area pointed to
	 * by pSignal
	 */
	psignal = (char __iomem *)pqhdr + readq(&pqhdr->sig_base_offset) +
		(head * readl(&pqhdr->signal_size));
	memcpy_toio(psignal, sig, readl(&pqhdr->signal_size));

	mb(); /* channel synch */
	writel(head, &pqhdr->head);

	writeq(readq(&pqhdr->num_sent) + 1, &pqhdr->num_sent);
	return 1;
}
EXPORT_SYMBOL_GPL(spar_signal_insert);

/*
 * Routine Description:
 * Removes one signal from Channel pChannel's nth Queue at the
 * time of the call and copies it into the memory pointed to by
 * pSignal.
 *
 * Parameters:
 * pChannel: (IN) points to the IO Channel
 * Queue: (IN) nth Queue of the IO Channel
 * pSignal: (IN) pointer to where the signals are to be copied
 *
 * Assumptions:
 * - pChannel and Queue are valid.
 * - pSignal points to a memory area large enough to hold queue's SignalSize
 *
 * Return value:
 * 1 if the removal succeeds, 0 if the queue was empty.
 */
unsigned char
spar_signal_remove(struct channel_header __iomem *ch, u32 queue, void *sig)
{
	void __iomem *psource;
	unsigned int head, tail;
	struct signal_queue_header __iomem *pqhdr =
	    (struct signal_queue_header __iomem *)((char __iomem *)ch +
				    readq(&ch->ch_space_offset)) + queue;

	/* capture current head and tail */
	head = readl(&pqhdr->head);
	tail = readl(&pqhdr->tail);

	/* queue is empty if the head index equals the tail index */
	if (head == tail) {
		writeq(readq(&pqhdr->num_empty) + 1, &pqhdr->num_empty);
		return 0;
	}

	/* advance past the 'empty' front slot */
	tail = (tail + 1) % readl(&pqhdr->max_slots);

	/* copy signal from tail location to the area pointed to by pSignal */
	psource = (char __iomem *)pqhdr + readq(&pqhdr->sig_base_offset) +
		(tail * readl(&pqhdr->signal_size));
	memcpy_fromio(sig, psource, readl(&pqhdr->signal_size));

	mb(); /* channel synch */
	writel(tail, &pqhdr->tail);

	writeq(readq(&pqhdr->num_received) + 1,
	       &pqhdr->num_received);
	return 1;
}
EXPORT_SYMBOL_GPL(spar_signal_remove);

/*
 * Routine Description:
 * Removes all signals present in Channel pChannel's nth Queue at the
 * time of the call and copies them into the memory pointed to by
 * pSignal.  Returns the # of signals copied as the value of the routine.
 *
 * Parameters:
 * pChannel: (IN) points to the IO Channel
 * Queue: (IN) nth Queue of the IO Channel
 * pSignal: (IN) pointer to where the signals are to be copied
 *
 * Assumptions:
 * - pChannel and Queue are valid.
 * - pSignal points to a memory area large enough to hold Queue's MaxSignals
 * # of signals, each of which is Queue's SignalSize.
 *
 * Return value:
 * # of signals copied.
 */
unsigned int spar_signal_remove_all(struct channel_header *ch, u32 queue,
				    void *sig)
{
	void *psource;
	unsigned int head, tail, count = 0;
	struct signal_queue_header *pqhdr =
	    (struct signal_queue_header *)((char *)ch +
				    ch->ch_space_offset) + queue;

	/* capture current head and tail */
	head = pqhdr->head;
	tail = pqhdr->tail;

	/* queue is empty if the head index equals the tail index */
	if (head == tail)
		return 0;

	while (head != tail) {
		/* advance past the 'empty' front slot */
		tail = (tail + 1) % pqhdr->max_slots;

		/* copy signal from tail location to the area pointed
		 * to by pSignal
		 */
		psource =
		    (char *)pqhdr + pqhdr->sig_base_offset +
		    (tail * pqhdr->signal_size);
		memcpy((char *)sig + (pqhdr->signal_size * count),
		       psource, pqhdr->signal_size);

		mb(); /* channel synch */
		pqhdr->tail = tail;

		count++;
		pqhdr->num_received++;
	}

	return count;
}

/*
 * Routine Description:
 * Determine whether a signal queue is empty.
 *
 * Parameters:
 * pChannel: (IN) points to the IO Channel
 * Queue: (IN) nth Queue of the IO Channel
 *
 * Return value:
 * 1 if the signal queue is empty, 0 otherwise.
 */
unsigned char spar_signalqueue_empty(struct channel_header __iomem *ch,
				     u32 queue)
{
	struct signal_queue_header __iomem *pqhdr =
	    (struct signal_queue_header __iomem *)((char __iomem *)ch +
				    readq(&ch->ch_space_offset)) + queue;
	return readl(&pqhdr->head) == readl(&pqhdr->tail);
}
EXPORT_SYMBOL_GPL(spar_signalqueue_empty);

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
			void *signal,
			spinlock_t *lock,
			u8 *channel_id)
{
	unsigned long flags;
	u8 rc = 0;

	spin_lock_irqsave(lock, flags);
	if (!spar_channel_client_acquire_os(queueinfo->chan, channel_id))
		goto unlock;
	if (spar_signal_insert(queueinfo->chan, whichqueue, signal)) {
		queueinfo->packets_sent++;
		rc = 1;
	}
	spar_channel_client_release_os(queueinfo->chan, channel_id);
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
					(spinlock_t *)insertlock,
					channel_id)) {
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
	if (!spar_signal_remove(queueinfo->chan, whichqueue, cmdrsp))
		return 0;

	queueinfo->packets_received++;

	return 1;		/* Success */
}
EXPORT_SYMBOL_GPL(uisqueue_get_cmdrsp);
