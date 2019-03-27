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
 * $Begemot: libunimsg/libngatm/sscopcust.h,v 1.4 2004/07/08 08:21:40 brandt Exp $
 *
 * Customisation of the SSCOP code for the user space library.
 */
#include <sys/types.h>
#include <sys/queue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#ifdef SSCOP_DEBUG
#include <assert.h>
#endif
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netnatm/unimsg.h>

/*
 * Allocate zeroed or non-zeroed memory of some size and cast it.
 * Return NULL on failure.
 */
#define	MEMINIT()

#define	MEMZALLOC(PTR,CAST,SIZE) do {				\
	void *_m = malloc(SIZE);				\
	if (_m != NULL)						\
		bzero(_m, SIZE);				\
	(PTR) = (CAST)_m;					\
} while(0)

#define	MEMFREE(PTR) free(PTR);

#define	MSG_ALLOC(PTR) \
	MEMZALLOC(PTR, struct sscop_msg *, sizeof(struct sscop_msg))
#define	MSG_FREE(PTR) \
	MEMFREE(PTR)

#define	SIG_ALLOC(PTR) \
	MEMZALLOC(PTR, struct sscop_sig *, sizeof(struct sscop_sig))
#define	SIG_FREE(PTR) \
	MEMFREE(PTR)

/*
 * Timer support.
 */
typedef void *sscop_timer_t;
#define	TIMER_INIT(S,T)	(S)->t_##T = NULL
#define	TIMER_STOP(S,T) do {						\
	if ((S)->t_##T != NULL) {					\
		(S)->funcs->stop_timer((S), (S)->aarg, (S)->t_##T);	\
		(S)->t_##T = NULL;					\
	}								\
    } while(0)
#define	TIMER_RESTART(S,T) do {						\
	if ((S)->t_##T != NULL)						\
		(S)->funcs->stop_timer((S), (S)->aarg, (S)->t_##T);	\
	(S)->t_##T = (S)->funcs->start_timer((S), (S)->aarg,		\
	    (S)->timer##T, T##_func);					\
    } while(0)
#define	TIMER_ISACT(S,T)	((S)->t_##T != NULL)

#define	TIMER_FUNC(T,N)							\
static void								\
T##_func(void *varg)							\
{									\
	struct sscop *sscop = varg;					\
	VERBOSE(sscop, SSCOP_DBG_TIMER, (sscop, sscop->aarg,		\
	    "timer_" #T " expired"));					\
	sscop->t_##T = NULL;						\
	sscop_signal(sscop, SIG_T_##N, NULL);				\
}


/*
 * Message queues
 */
typedef TAILQ_ENTRY(sscop_msg) sscop_msgq_link_t;
typedef TAILQ_HEAD(sscop_msgq, sscop_msg) sscop_msgq_head_t;
#define	MSGQ_EMPTY(Q) TAILQ_EMPTY(Q)
#define	MSGQ_INIT(Q) TAILQ_INIT(Q)
#define	MSGQ_FOREACH(P,Q) TAILQ_FOREACH(P,Q,link)
#define	MSGQ_REMOVE(Q,M) TAILQ_REMOVE(Q,M,link)
#define	MSGQ_INSERT_BEFORE(B,M) TAILQ_INSERT_BEFORE(B,M,link)
#define	MSGQ_APPEND(Q,M) TAILQ_INSERT_TAIL(Q,M,link)
#define	MSGQ_PEEK(Q) (TAILQ_EMPTY((Q)) ? NULL : TAILQ_FIRST((Q)))
#define	MSGQ_GET(Q)							\
    ({									\
	struct sscop_msg *_m = NULL;					\
									\
	if(!TAILQ_EMPTY(Q)) {						\
		_m = TAILQ_FIRST(Q);					\
		TAILQ_REMOVE(Q, _m, link);				\
	}								\
	_m;								\
    })

#define	MSGQ_CLEAR(Q)							\
	do {								\
		struct sscop_msg *_m1, *_m2;				\
									\
		_m1 = TAILQ_FIRST(Q);					\
		while(_m1 != NULL) {					\
			_m2 = TAILQ_NEXT(_m1, link);			\
			SSCOP_MSG_FREE(_m1);				\
			_m1 = _m2;					\
		}							\
		TAILQ_INIT((Q));					\
	} while(0)

/*
 * Signal queues
 */
typedef TAILQ_ENTRY(sscop_sig) sscop_sigq_link_t;
typedef TAILQ_HEAD(sscop_sigq, sscop_sig) sscop_sigq_head_t;
#define	SIGQ_INIT(Q) 		TAILQ_INIT(Q)
#define	SIGQ_APPEND(Q,S)	TAILQ_INSERT_TAIL(Q, S, link)
#define	SIGQ_EMPTY(Q)		TAILQ_EMPTY(Q)
#define	SIGQ_GET(Q)							\
    ({									\
	struct sscop_sig *_s = NULL;					\
									\
	if(!TAILQ_EMPTY(Q)) {						\
		_s = TAILQ_FIRST(Q);					\
		TAILQ_REMOVE(Q, _s, link);				\
	}								\
	_s;								\
    })

#define	SIGQ_MOVE(F,T)							\
    do {								\
	struct sscop_sig *_s;						\
									\
	while(!TAILQ_EMPTY(F)) {					\
		_s = TAILQ_FIRST(F);					\
		TAILQ_REMOVE(F, _s, link);				\
		TAILQ_INSERT_TAIL(T, _s, link);				\
	}								\
    } while(0)

#define	SIGQ_PREPEND(F,T)						\
    do {								\
	struct sscop_sig *_s;						\
									\
	while(!TAILQ_EMPTY(F)) {					\
		_s = TAILQ_LAST(F, sscop_sigq);				\
		TAILQ_REMOVE(F, _s, link);				\
		TAILQ_INSERT_HEAD(T, _s, link);				\
	}								\
    } while(0)

#define	SIGQ_CLEAR(Q)							\
    do {								\
	struct sscop_sig *_s1, *_s2;					\
									\
	_s1 = TAILQ_FIRST(Q);						\
	while(_s1 != NULL) {						\
		_s2 = TAILQ_NEXT(_s1, link);				\
		SSCOP_MSG_FREE(_s1->msg);				\
		SIG_FREE(_s1);						\
		_s1 = _s2;						\
	}								\
	TAILQ_INIT(Q);							\
    } while(0)



/*
 * Message buffers
 */
/* Free a buffer (if there is one) */
#define	MBUF_FREE(M)	do { if(M) uni_msg_destroy(M); } while(0)

/* duplicate a buffer */
#define	MBUF_DUP(M) uni_msg_dup(M)

/* compute current length */
#define	MBUF_LEN(M) uni_msg_len((M))

/*
 * Return the i-th word counted from the end of the buffer.
 * i=-1 will return the last 32bit word, i=-2 the 2nd last.
 * Assumes that there is enough space.
 */
#define	MBUF_TRAIL32(M,I) uni_msg_trail32((M), (I))

/*
 * Strip 32bit value from the end
 */
#define	MBUF_STRIP32(M) uni_msg_strip32((M))

/*
 * Strip 32bit value from head
 */
#define	MBUF_GET32(M) uni_msg_get32((M))

/*
 * Append a 32bit value to an mbuf. Failures are ignored.
 */
#define	MBUF_APPEND32(M,W) uni_msg_append32((M), (W))

/*
 * Pad a message to a multiple of four byte and return the amount of padding
 * Failures are ignored.
 */
#define	MBUF_PAD4(M)							\
    ({									\
	int _npad = 0;							\
	while (uni_msg_len(M) % 4 != 0) {				\
		uni_msg_append8((M), 0);				\
		_npad++;						\
	}								\
	_npad;								\
    })

#define	MBUF_UNPAD(M,P) do { (M)->b_wptr -= (P); } while(0)

/*
 * Allocate a message that will probably hold N bytes.
 */
#define	MBUF_ALLOC(N) uni_msg_alloc(N)

#ifdef SSCOP_DEBUG
#define	ASSERT(X)	assert(X)
#else
#define	ASSERT(X)
#endif
