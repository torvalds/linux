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

#include "pt_event_queue.h"

#include <string.h>


static inline uint8_t pt_evq_inc(uint8_t idx)
{
	idx += 1;
	idx %= evq_max;

	return idx;
}

static struct pt_event *pt_event_init(struct pt_event *event)
{
	if (event)
		memset(event, 0, sizeof(*event));

	return event;
}

void pt_evq_init(struct pt_event_queue *evq)
{
	if (!evq)
		return;

	memset(evq, 0, sizeof(*evq));
}

struct pt_event *pt_evq_standalone(struct pt_event_queue *evq)
{
	if (!evq)
		return NULL;

	return pt_event_init(&evq->standalone);
}

struct pt_event *pt_evq_enqueue(struct pt_event_queue *evq,
				enum pt_event_binding evb)
{
	uint8_t begin, end, gap, idx;

	if (!evq)
		return NULL;

	if (evb_max <= evb)
		return NULL;

	begin = evq->begin[evb];
	idx = evq->end[evb];

	if (evq_max <= begin)
		return NULL;

	if (evq_max <= idx)
		return NULL;

	end = pt_evq_inc(idx);
	gap = pt_evq_inc(end);

	/* Leave a gap so we don't overwrite the last dequeued event. */
	if (begin == gap)
		return NULL;

	evq->end[evb] = end;

	return pt_event_init(&evq->queue[evb][idx]);
}

struct pt_event *pt_evq_dequeue(struct pt_event_queue *evq,
				enum pt_event_binding evb)
{
	uint8_t begin, end;

	if (!evq)
		return NULL;

	if (evb_max <= evb)
		return NULL;

	begin = evq->begin[evb];
	end = evq->end[evb];

	if (evq_max <= begin)
		return NULL;

	if (evq_max <= end)
		return NULL;

	if (begin == end)
		return NULL;

	evq->begin[evb] = pt_evq_inc(begin);

	return &evq->queue[evb][begin];
}

int pt_evq_clear(struct pt_event_queue *evq, enum pt_event_binding evb)
{
	if (!evq)
		return -pte_internal;

	if (evb_max <= evb)
		return -pte_internal;

	evq->begin[evb] = 0;
	evq->end[evb] = 0;

	return 0;
}

int pt_evq_empty(const struct pt_event_queue *evq, enum pt_event_binding evb)
{
	uint8_t begin, end;

	if (!evq)
		return -pte_internal;

	if (evb_max <= evb)
		return -pte_internal;

	begin = evq->begin[evb];
	end = evq->end[evb];

	if (evq_max <= begin)
		return -pte_internal;

	if (evq_max <= end)
		return -pte_internal;

	return begin == end;
}

int pt_evq_pending(const struct pt_event_queue *evq, enum pt_event_binding evb)
{
	int errcode;

	errcode = pt_evq_empty(evq, evb);
	if (errcode < 0)
		return errcode;

	return !errcode;
}

struct pt_event *pt_evq_find(struct pt_event_queue *evq,
			     enum pt_event_binding evb,
			     enum pt_event_type evt)
{
	uint8_t begin, end;

	if (!evq)
		return NULL;

	if (evb_max <= evb)
		return NULL;

	begin = evq->begin[evb];
	end = evq->end[evb];

	if (evq_max <= begin)
		return NULL;

	if (evq_max <= end)
		return NULL;

	for (; begin != end; begin = pt_evq_inc(begin)) {
		struct pt_event *ev;

		ev = &evq->queue[evb][begin];
		if (ev->type == evt)
			return ev;
	}

	return NULL;
}
