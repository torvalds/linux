/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Author: Hartmut Brandt <harti@freebsd.org>
 *
 * $Begemot: libunimsg/libngatm/sscfucust.h,v 1.4 2004/07/08 08:21:40 brandt Exp $
 *
 * Customisation of the SSCFU code for the user space library.
 */
#include <sys/types.h>
#include <sys/queue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef SSCFU_DEBUG
#include <assert.h>
#endif
#include <netnatm/unimsg.h>

/*
 * Allocate zeroed or non-zeroed memory of some size and cast it.
 * Return NULL on failure.
 */
#define MEMINIT()

#define MEMZALLOC(PTR,CAST,SIZE) do {				\
	void *_m = malloc(SIZE);				\
	if (_m != NULL)						\
		bzero(_m, SIZE);				\
	(PTR) = (CAST)_m;					\
} while(0)

#define MEMFREE(PTR) \
	free(PTR)

#define SIG_ALLOC(PTR) \
	MEMZALLOC(PTR, struct sscfu_sig *, sizeof(struct sscfu_sig))
#define SIG_FREE(PTR) \
	MEMFREE(PTR)

/*
 * Signal queues
 */
typedef TAILQ_ENTRY(sscfu_sig) sscfu_sigq_link_t;
typedef TAILQ_HEAD(sscfu_sigq, sscfu_sig) sscfu_sigq_head_t;
#define SIGQ_INIT(Q) 		TAILQ_INIT(Q)
#define SIGQ_APPEND(Q,S)	TAILQ_INSERT_TAIL(Q, S, link)
#define SIGQ_GET(Q)							\
    ({									\
	struct sscfu_sig *_s = NULL;					\
									\
	if(!TAILQ_EMPTY(Q)) {						\
		_s = TAILQ_FIRST(Q);					\
		TAILQ_REMOVE(Q, _s, link);				\
	}								\
	_s;								\
    })

#define SIGQ_CLEAR(Q)							\
    do {								\
	struct sscfu_sig *_s1, *_s2;					\
									\
	_s1 = TAILQ_FIRST(Q);						\
	while(_s1 != NULL) {						\
		_s2 = TAILQ_NEXT(_s1, link);				\
		if(_s1->m)						\
			MBUF_FREE(_s1->m);				\
		SIG_FREE(_s1);						\
		_s1 = _s2;						\
	}								\
	TAILQ_INIT(Q);							\
    } while(0)


/*
 * Message buffers
 */
#define MBUF_FREE(M)	uni_msg_destroy(M)

#ifdef SSCFU_DEBUG
#define	ASSERT(S)	assert(S)
#else
#define	ASSERT(S)
#endif
