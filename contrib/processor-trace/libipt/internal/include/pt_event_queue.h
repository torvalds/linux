/*
 * Copyright (c) 2013-2018, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PT_EVENT_QUEUE_H
#define PT_EVENT_QUEUE_H

#include "intel-pt.h"

#include <stdint.h>


/* Events are grouped by the packet the event binds to. */
enum pt_event_binding {
	evb_psbend,
	evb_tip,
	evb_fup,

	evb_max
};

enum {
	/* The maximal number of pending events - should be a power of two. */
	evq_max = 8
};

/* A queue of events. */
struct pt_event_queue {
	/* A collection of event queues, one per binding. */
	struct pt_event queue[evb_max][evq_max];

	/* The begin and end indices for the above event queues. */
	uint8_t begin[evb_max];
	uint8_t end[evb_max];

	/* A standalone event to be published immediately. */
	struct pt_event standalone;
};


/* Initialize (or reset) an event queue. */
extern void pt_evq_init(struct pt_event_queue *);

/* Get a standalone event.
 *
 * Returns a pointer to the standalone event on success.
 * Returns NULL if @evq is NULL.
 */
extern struct pt_event *pt_evq_standalone(struct pt_event_queue *evq);

/* Enqueue an event.
 *
 * Adds a new event to @evq for binding @evb.
 *
 * Returns a pointer to the new event on success.
 * Returns NULL if @evq is NULL or @evb is invalid.
 * Returns NULL if @evq is full.
 */
extern struct pt_event *pt_evq_enqueue(struct pt_event_queue *evq,
				       enum pt_event_binding evb);


/* Dequeue an event.
 *
 * Removes the first event for binding @evb from @evq.
 *
 * Returns a pointer to the dequeued event on success.
 * Returns NULL if @evq is NULL or @evb is invalid.
 * Returns NULL if @evq is empty.
 */
extern struct pt_event *pt_evq_dequeue(struct pt_event_queue *evq,
				       enum pt_event_binding evb);

/* Clear a queue and discard events.
 *
 * Removes all events for binding @evb from @evq.
 *
 * Returns zero on success, a negative error code otherwise.
 * Returns -pte_internal if @evq is NULL or @evb is invalid.
 */
extern int pt_evq_clear(struct pt_event_queue *evq,
			enum pt_event_binding evb);

/* Check for emptiness.
 *
 * Check if @evq for binding @evb is empty.
 *
 * Returns a positive number if @evq is empty.
 * Returns zero if @evq is not empty.
 * Returns -pte_internal if @evq is NULL or @evb is invalid.
 */
extern int pt_evq_empty(const struct pt_event_queue *evq,
			enum pt_event_binding evb);

/* Check for non-emptiness.
 *
 * Check if @evq for binding @evb contains pending events.
 *
 * Returns a positive number if @evq is not empty.
 * Returns zero if @evq is empty.
 * Returns -pte_internal if @evq is NULL or @evb is invalid.
 */
extern int pt_evq_pending(const struct pt_event_queue *evq,
			  enum pt_event_binding evb);

/* Find an event by type.
 *
 * Searches @evq for binding @evb for an event of type @evt.
 *
 * Returns a pointer to the first matching event on success.
 * Returns NULL if there is no such event.
 * Returns NULL if @evq is NULL.
 * Returns NULL if @evb or @evt is invalid.
 */
extern struct pt_event *pt_evq_find(struct pt_event_queue *evq,
				    enum pt_event_binding evb,
				    enum pt_event_type evt);

#endif /* PT_EVENT_QUEUE_H */
