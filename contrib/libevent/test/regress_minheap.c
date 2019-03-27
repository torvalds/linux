/*
 * Copyright (c) 2009-2012 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "../minheap-internal.h"

#include <stdlib.h>
#include "event2/event_struct.h"

#include "tinytest.h"
#include "tinytest_macros.h"
#include "regress.h"

static void
set_random_timeout(struct event *ev)
{
	ev->ev_timeout.tv_sec = test_weakrand();
	ev->ev_timeout.tv_usec = test_weakrand() & 0xfffff;
	ev->ev_timeout_pos.min_heap_idx = -1;
}

static void
check_heap(struct min_heap *heap)
{
	unsigned i;
	for (i = 1; i < heap->n; ++i) {
		unsigned parent_idx = (i-1)/2;
		tt_want(evutil_timercmp(&heap->p[i]->ev_timeout,
			&heap->p[parent_idx]->ev_timeout, >=));
	}
}

static void
test_heap_randomized(void *ptr)
{
	struct min_heap heap;
	struct event *inserted[1024];
	struct event *e, *last_e;
	int i;

	min_heap_ctor_(&heap);

	for (i = 0; i < 1024; ++i) {
		inserted[i] = malloc(sizeof(struct event));
		set_random_timeout(inserted[i]);
		min_heap_push_(&heap, inserted[i]);
	}
	check_heap(&heap);

	tt_assert(min_heap_size_(&heap) == 1024);

	for (i = 0; i < 512; ++i) {
		min_heap_erase_(&heap, inserted[i]);
		if (0 == (i % 32))
			check_heap(&heap);
	}
	tt_assert(min_heap_size_(&heap) == 512);

	last_e = min_heap_pop_(&heap);
	while (1) {
		e = min_heap_pop_(&heap);
		if (!e)
			break;
		tt_want(evutil_timercmp(&last_e->ev_timeout,
			&e->ev_timeout, <=));
	}
	tt_assert(min_heap_size_(&heap) == 0);
end:
	for (i = 0; i < 1024; ++i)
		free(inserted[i]);

	min_heap_dtor_(&heap);
}

struct testcase_t minheap_testcases[] = {
	{ "randomized", test_heap_randomized, 0, NULL, NULL },
	END_OF_TESTCASES
};
