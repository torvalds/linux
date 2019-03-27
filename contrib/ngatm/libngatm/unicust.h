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
 * $Begemot: libunimsg/libngatm/unicust.h,v 1.4 2003/09/19 13:10:35 hbb Exp $
 *
 * Customisation of signalling source to user space.
 */
#include <sys/types.h>
#include <sys/queue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define ASSERT(E, M) assert(E)

static __inline__ void *
mzalloc(size_t s)
{
	void *ptr = malloc(s);

	if (ptr)
		bzero(ptr, s);
	return (ptr);
}

#define INS_ALLOC()	mzalloc(sizeof(struct uni))
#define INS_FREE(P)	free(P)

#define UNI_ALLOC()	mzalloc(sizeof(struct uni_all))
#define UNI_FREE(P)	free(P)

#define SIG_ALLOC()	mzalloc(sizeof(struct sig))
#define SIG_FREE(P)	free(P)

#define CALL_ALLOC()	mzalloc(sizeof(struct call))
#define CALL_FREE(P)	free(P)

#define PARTY_ALLOC()	mzalloc(sizeof(struct party))
#define PARTY_FREE(P)	free(P)

/*
 * Timers
 */
struct uni_timer {
	void *c;
};

#define _TIMER_INIT(X,T)	(X)->T.c = NULL
#define _TIMER_DESTROY(U,F)	_TIMER_STOP(U,F)
#define _TIMER_STOP(U,F)					\
    do {							\
	if (F.c != NULL) {					\
		(U)->funcs->stop_timer(U, U->arg, F.c);		\
		F.c = NULL;					\
	}							\
    } while(0)
#define _TIMER_START(UNI,ARG,FIELD,DUE,FUNC)			\
	(void)(FIELD.c = (UNI)->funcs->start_timer(UNI,		\
	    UNI->arg, DUE, FUNC, ARG))

#define TIMER_ISACT(X,T)	(X->T.c != NULL)

#define TIMER_FUNC_UNI(T,F)						\
static void F(struct uni *);						\
static void								\
_##T##_func(void *varg)							\
{									\
	struct uni *uni = (struct uni *)varg;				\
	uni->T.c = NULL;						\
	(F)(uni);							\
}

/*
 * Be careful: call may be invalid after the call to F
 */
#define TIMER_FUNC_CALL(T,F)						\
static void F(struct call *);						\
static void								\
_##T##_func(void *varg)							\
{									\
	struct call *call = (struct call *)varg;			\
	call->T.c = NULL;						\
	(F)(call);							\
}

/*
 * Be careful: call/party may be invalid after the call to F
 */
#define TIMER_FUNC_PARTY(T,F)						\
static void F(struct party *);						\
static void								\
_##T##_func(void *varg)							\
{									\
	struct party *party = (struct party *)varg;			\
	party->T.c = NULL;						\
	(F)(party);							\
}
