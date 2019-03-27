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
#ifndef RATELIM_INTERNAL_H_INCLUDED_
#define RATELIM_INTERNAL_H_INCLUDED_

#ifdef __cplusplus
extern "C" {
#endif

#include "event2/util.h"

/** A token bucket is an internal structure that tracks how many bytes we are
 * currently willing to read or write on a given bufferevent or group of
 * bufferevents */
struct ev_token_bucket {
	/** How many bytes are we willing to read or write right now? These
	 * values are signed so that we can do "defecit spending" */
	ev_ssize_t read_limit, write_limit;
	/** When was this bucket last updated?  Measured in abstract 'ticks'
	 * relative to the token bucket configuration. */
	ev_uint32_t last_updated;
};

/** Configuration info for a token bucket or set of token buckets. */
struct ev_token_bucket_cfg {
	/** How many bytes are we willing to read on average per tick? */
	size_t read_rate;
	/** How many bytes are we willing to read at most in any one tick? */
	size_t read_maximum;
	/** How many bytes are we willing to write on average per tick? */
	size_t write_rate;
	/** How many bytes are we willing to write at most in any one tick? */
	size_t write_maximum;

	/* How long is a tick?  Note that fractions of a millisecond are
	 * ignored. */
	struct timeval tick_timeout;

	/* How long is a tick, in milliseconds?  Derived from tick_timeout. */
	unsigned msec_per_tick;
};

/** The current tick is 'current_tick': add bytes to 'bucket' as specified in
 * 'cfg'. */
int ev_token_bucket_update_(struct ev_token_bucket *bucket,
    const struct ev_token_bucket_cfg *cfg,
    ev_uint32_t current_tick);

/** In which tick does 'tv' fall according to 'cfg'?  Note that ticks can
 * overflow easily; your code needs to handle this. */
ev_uint32_t ev_token_bucket_get_tick_(const struct timeval *tv,
    const struct ev_token_bucket_cfg *cfg);

/** Adjust 'bucket' to respect 'cfg', and note that it was last updated in
 * 'current_tick'.  If 'reinitialize' is true, we are changing the
 * configuration of 'bucket'; otherwise, we are setting it up for the first
 * time.
 */
int ev_token_bucket_init_(struct ev_token_bucket *bucket,
    const struct ev_token_bucket_cfg *cfg,
    ev_uint32_t current_tick,
    int reinitialize);

int bufferevent_remove_from_rate_limit_group_internal_(struct bufferevent *bev,
    int unsuspend);

/** Decrease the read limit of 'b' by 'n' bytes */
#define ev_token_bucket_decrement_read(b,n)	\
	do {					\
		(b)->read_limit -= (n);		\
	} while (0)
/** Decrease the write limit of 'b' by 'n' bytes */
#define ev_token_bucket_decrement_write(b,n)	\
	do {					\
		(b)->write_limit -= (n);	\
	} while (0)

#ifdef __cplusplus
}
#endif

#endif
