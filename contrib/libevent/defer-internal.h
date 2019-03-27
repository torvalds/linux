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
#ifndef DEFER_INTERNAL_H_INCLUDED_
#define DEFER_INTERNAL_H_INCLUDED_

#ifdef __cplusplus
extern "C" {
#endif

#include "event2/event-config.h"
#include "evconfig-private.h"

#include <sys/queue.h>

struct event_callback;
typedef void (*deferred_cb_fn)(struct event_callback *, void *);

/**
   Initialize an empty, non-pending event_callback.

   @param deferred The struct event_callback structure to initialize.
   @param priority The priority that the callback should run at.
   @param cb The function to run when the struct event_callback executes.
   @param arg The function's second argument.
 */
void event_deferred_cb_init_(struct event_callback *, ev_uint8_t, deferred_cb_fn, void *);
/**
   Change the priority of a non-pending event_callback.
 */
void event_deferred_cb_set_priority_(struct event_callback *, ev_uint8_t);
/**
   Cancel a struct event_callback if it is currently scheduled in an event_base.
 */
void event_deferred_cb_cancel_(struct event_base *, struct event_callback *);
/**
   Activate a struct event_callback if it is not currently scheduled in an event_base.

   Return true if it was not previously scheduled.
 */
int event_deferred_cb_schedule_(struct event_base *, struct event_callback *);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_INTERNAL_H_INCLUDED_ */

