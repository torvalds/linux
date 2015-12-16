/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lnet/lnet/lib-eq.c
 *
 * Library level Event queue management routines
 */

#define DEBUG_SUBSYSTEM S_LNET
#include "../../include/linux/lnet/lib-lnet.h"

/**
 * Create an event queue that has room for \a count number of events.
 *
 * The event queue is circular and older events will be overwritten by new
 * ones if they are not removed in time by the user using the functions
 * LNetEQGet(), LNetEQWait(), or LNetEQPoll(). It is up to the user to
 * determine the appropriate size of the event queue to prevent this loss
 * of events. Note that when EQ handler is specified in \a callback, no
 * event loss can happen, since the handler is run for each event deposited
 * into the EQ.
 *
 * \param count The number of events to be stored in the event queue. It
 * will be rounded up to the next power of two.
 * \param callback A handler function that runs when an event is deposited
 * into the EQ. The constant value LNET_EQ_HANDLER_NONE can be used to
 * indicate that no event handler is desired.
 * \param handle On successful return, this location will hold a handle for
 * the newly created EQ.
 *
 * \retval 0       On success.
 * \retval -EINVAL If an parameter is not valid.
 * \retval -ENOMEM If memory for the EQ can't be allocated.
 *
 * \see lnet_eq_handler_t for the discussion on EQ handler semantics.
 */
int
LNetEQAlloc(unsigned int count, lnet_eq_handler_t callback,
	    lnet_handle_eq_t *handle)
{
	lnet_eq_t *eq;

	LASSERT(the_lnet.ln_init);
	LASSERT(the_lnet.ln_refcount > 0);

	/* We need count to be a power of 2 so that when eq_{enq,deq}_seq
	 * overflow, they don't skip entries, so the queue has the same
	 * apparent capacity at all times */

	count = roundup_pow_of_two(count);

	if (callback != LNET_EQ_HANDLER_NONE && count != 0)
		CWARN("EQ callback is guaranteed to get every event, do you still want to set eqcount %d for polling event which will have locking overhead? Please contact with developer to confirm\n", count);

	/* count can be 0 if only need callback, we can eliminate
	 * overhead of enqueue event */
	if (count == 0 && callback == LNET_EQ_HANDLER_NONE)
		return -EINVAL;

	eq = lnet_eq_alloc();
	if (eq == NULL)
		return -ENOMEM;

	if (count != 0) {
		LIBCFS_ALLOC(eq->eq_events, count * sizeof(lnet_event_t));
		if (eq->eq_events == NULL)
			goto failed;
		/* NB allocator has set all event sequence numbers to 0,
		 * so all them should be earlier than eq_deq_seq */
	}

	eq->eq_deq_seq = 1;
	eq->eq_enq_seq = 1;
	eq->eq_size = count;
	eq->eq_callback = callback;

	eq->eq_refs = cfs_percpt_alloc(lnet_cpt_table(),
				       sizeof(*eq->eq_refs[0]));
	if (eq->eq_refs == NULL)
		goto failed;

	/* MUST hold both exclusive lnet_res_lock */
	lnet_res_lock(LNET_LOCK_EX);
	/* NB: hold lnet_eq_wait_lock for EQ link/unlink, so we can do
	 * both EQ lookup and poll event with only lnet_eq_wait_lock */
	lnet_eq_wait_lock();

	lnet_res_lh_initialize(&the_lnet.ln_eq_container, &eq->eq_lh);
	list_add(&eq->eq_list, &the_lnet.ln_eq_container.rec_active);

	lnet_eq_wait_unlock();
	lnet_res_unlock(LNET_LOCK_EX);

	lnet_eq2handle(handle, eq);
	return 0;

failed:
	if (eq->eq_events != NULL)
		LIBCFS_FREE(eq->eq_events, count * sizeof(lnet_event_t));

	if (eq->eq_refs != NULL)
		cfs_percpt_free(eq->eq_refs);

	lnet_eq_free(eq);
	return -ENOMEM;
}
EXPORT_SYMBOL(LNetEQAlloc);

/**
 * Release the resources associated with an event queue if it's idle;
 * otherwise do nothing and it's up to the user to try again.
 *
 * \param eqh A handle for the event queue to be released.
 *
 * \retval 0 If the EQ is not in use and freed.
 * \retval -ENOENT If \a eqh does not point to a valid EQ.
 * \retval -EBUSY  If the EQ is still in use by some MDs.
 */
int
LNetEQFree(lnet_handle_eq_t eqh)
{
	struct lnet_eq *eq;
	lnet_event_t *events = NULL;
	int **refs = NULL;
	int *ref;
	int rc = 0;
	int size = 0;
	int i;

	LASSERT(the_lnet.ln_init);
	LASSERT(the_lnet.ln_refcount > 0);

	lnet_res_lock(LNET_LOCK_EX);
	/* NB: hold lnet_eq_wait_lock for EQ link/unlink, so we can do
	 * both EQ lookup and poll event with only lnet_eq_wait_lock */
	lnet_eq_wait_lock();

	eq = lnet_handle2eq(&eqh);
	if (eq == NULL) {
		rc = -ENOENT;
		goto out;
	}

	cfs_percpt_for_each(ref, i, eq->eq_refs) {
		LASSERT(*ref >= 0);
		if (*ref == 0)
			continue;

		CDEBUG(D_NET, "Event equeue (%d: %d) busy on destroy.\n",
		       i, *ref);
		rc = -EBUSY;
		goto out;
	}

	/* stash for free after lock dropped */
	events = eq->eq_events;
	size = eq->eq_size;
	refs = eq->eq_refs;

	lnet_res_lh_invalidate(&eq->eq_lh);
	list_del(&eq->eq_list);
	lnet_eq_free(eq);
 out:
	lnet_eq_wait_unlock();
	lnet_res_unlock(LNET_LOCK_EX);

	if (events != NULL)
		LIBCFS_FREE(events, size * sizeof(lnet_event_t));
	if (refs != NULL)
		cfs_percpt_free(refs);

	return rc;
}
EXPORT_SYMBOL(LNetEQFree);

void
lnet_eq_enqueue_event(lnet_eq_t *eq, lnet_event_t *ev)
{
	/* MUST called with resource lock hold but w/o lnet_eq_wait_lock */
	int index;

	if (eq->eq_size == 0) {
		LASSERT(eq->eq_callback != LNET_EQ_HANDLER_NONE);
		eq->eq_callback(ev);
		return;
	}

	lnet_eq_wait_lock();
	ev->sequence = eq->eq_enq_seq++;

	LASSERT(eq->eq_size == LOWEST_BIT_SET(eq->eq_size));
	index = ev->sequence & (eq->eq_size - 1);

	eq->eq_events[index] = *ev;

	if (eq->eq_callback != LNET_EQ_HANDLER_NONE)
		eq->eq_callback(ev);

	/* Wake anyone waiting in LNetEQPoll() */
	if (waitqueue_active(&the_lnet.ln_eq_waitq))
		wake_up_all(&the_lnet.ln_eq_waitq);
	lnet_eq_wait_unlock();
}

static int
lnet_eq_dequeue_event(lnet_eq_t *eq, lnet_event_t *ev)
{
	int new_index = eq->eq_deq_seq & (eq->eq_size - 1);
	lnet_event_t *new_event = &eq->eq_events[new_index];
	int rc;

	/* must called with lnet_eq_wait_lock hold */
	if (LNET_SEQ_GT(eq->eq_deq_seq, new_event->sequence))
		return 0;

	/* We've got a new event... */
	*ev = *new_event;

	CDEBUG(D_INFO, "event: %p, sequence: %lu, eq->size: %u\n",
	       new_event, eq->eq_deq_seq, eq->eq_size);

	/* ...but did it overwrite an event we've not seen yet? */
	if (eq->eq_deq_seq == new_event->sequence) {
		rc = 1;
	} else {
		/* don't complain with CERROR: some EQs are sized small
		 * anyway; if it's important, the caller should complain */
		CDEBUG(D_NET, "Event Queue Overflow: eq seq %lu ev seq %lu\n",
		       eq->eq_deq_seq, new_event->sequence);
		rc = -EOVERFLOW;
	}

	eq->eq_deq_seq = new_event->sequence + 1;
	return rc;
}

/**
 * A nonblocking function that can be used to get the next event in an EQ.
 * If an event handler is associated with the EQ, the handler will run before
 * this function returns successfully. The event is removed from the queue.
 *
 * \param eventq A handle for the event queue.
 * \param event On successful return (1 or -EOVERFLOW), this location will
 * hold the next event in the EQ.
 *
 * \retval 0	  No pending event in the EQ.
 * \retval 1	  Indicates success.
 * \retval -ENOENT    If \a eventq does not point to a valid EQ.
 * \retval -EOVERFLOW Indicates success (i.e., an event is returned) and that
 * at least one event between this event and the last event obtained from the
 * EQ has been dropped due to limited space in the EQ.
 */

/**
 * Block the calling process until there is an event in the EQ.
 * If an event handler is associated with the EQ, the handler will run before
 * this function returns successfully. This function returns the next event
 * in the EQ and removes it from the EQ.
 *
 * \param eventq A handle for the event queue.
 * \param event On successful return (1 or -EOVERFLOW), this location will
 * hold the next event in the EQ.
 *
 * \retval 1	  Indicates success.
 * \retval -ENOENT    If \a eventq does not point to a valid EQ.
 * \retval -EOVERFLOW Indicates success (i.e., an event is returned) and that
 * at least one event between this event and the last event obtained from the
 * EQ has been dropped due to limited space in the EQ.
 */

static int
lnet_eq_wait_locked(int *timeout_ms)
__must_hold(&the_lnet.ln_eq_wait_lock)
{
	int tms = *timeout_ms;
	int wait;
	wait_queue_t wl;
	unsigned long now;

	if (tms == 0)
		return -1; /* don't want to wait and no new event */

	init_waitqueue_entry(&wl, current);
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&the_lnet.ln_eq_waitq, &wl);

	lnet_eq_wait_unlock();

	if (tms < 0) {
		schedule();

	} else {
		now = jiffies;
		schedule_timeout(msecs_to_jiffies(tms));
		tms -= jiffies_to_msecs(jiffies - now);
		if (tms < 0) /* no more wait but may have new event */
			tms = 0;
	}

	wait = tms != 0; /* might need to call here again */
	*timeout_ms = tms;

	lnet_eq_wait_lock();
	remove_wait_queue(&the_lnet.ln_eq_waitq, &wl);

	return wait;
}

/**
 * Block the calling process until there's an event from a set of EQs or
 * timeout happens.
 *
 * If an event handler is associated with the EQ, the handler will run before
 * this function returns successfully, in which case the corresponding event
 * is consumed.
 *
 * LNetEQPoll() provides a timeout to allow applications to poll, block for a
 * fixed period, or block indefinitely.
 *
 * \param eventqs,neq An array of EQ handles, and size of the array.
 * \param timeout_ms Time in milliseconds to wait for an event to occur on
 * one of the EQs. The constant LNET_TIME_FOREVER can be used to indicate an
 * infinite timeout.
 * \param event,which On successful return (1 or -EOVERFLOW), \a event will
 * hold the next event in the EQs, and \a which will contain the index of the
 * EQ from which the event was taken.
 *
 * \retval 0	  No pending event in the EQs after timeout.
 * \retval 1	  Indicates success.
 * \retval -EOVERFLOW Indicates success (i.e., an event is returned) and that
 * at least one event between this event and the last event obtained from the
 * EQ indicated by \a which has been dropped due to limited space in the EQ.
 * \retval -ENOENT    If there's an invalid handle in \a eventqs.
 */
int
LNetEQPoll(lnet_handle_eq_t *eventqs, int neq, int timeout_ms,
	   lnet_event_t *event, int *which)
{
	int wait = 1;
	int rc;
	int i;

	LASSERT(the_lnet.ln_init);
	LASSERT(the_lnet.ln_refcount > 0);

	if (neq < 1)
		return -ENOENT;

	lnet_eq_wait_lock();

	for (;;) {
		for (i = 0; i < neq; i++) {
			lnet_eq_t *eq = lnet_handle2eq(&eventqs[i]);

			if (eq == NULL) {
				lnet_eq_wait_unlock();
				return -ENOENT;
			}

			rc = lnet_eq_dequeue_event(eq, event);
			if (rc != 0) {
				lnet_eq_wait_unlock();
				*which = i;
				return rc;
			}
		}

		if (wait == 0)
			break;

		/*
		 * return value of lnet_eq_wait_locked:
		 * -1 : did nothing and it's sure no new event
		 *  1 : sleep inside and wait until new event
		 *  0 : don't want to wait anymore, but might have new event
		 *      so need to call dequeue again
		 */
		wait = lnet_eq_wait_locked(&timeout_ms);
		if (wait < 0) /* no new event */
			break;
	}

	lnet_eq_wait_unlock();
	return 0;
}
