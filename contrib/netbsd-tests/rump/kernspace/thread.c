/*	$NetBSD: thread.c,v 1.2 2011/08/07 14:03:15 rmind Exp $	*/

/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#if !defined(lint)
__RCSID("$NetBSD: thread.c,v 1.2 2011/08/07 14:03:15 rmind Exp $");
#endif /* !lint */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/mutex.h>
#include <sys/proc.h>

#include "kernspace.h"

static volatile int testit;

static void
jointhread(void *arg)
{

	kpause("take5", false, 1, NULL);
	testit = 1;
	kthread_exit(0);
}

void
rumptest_threadjoin()
{
	struct lwp *newl;
	int rv;

	rv = kthread_create(PRI_NONE, KTHREAD_MUSTJOIN | KTHREAD_MPSAFE, NULL,
	    jointhread, NULL, &newl, "jointest");
	if (rv)
		panic("thread creation failed: %d", rv);
	rv = kthread_join(newl);
	if (rv)
		panic("thread join failed: %d", rv);

	if (testit != 1)
		panic("new thread did not run");
}

static kmutex_t mtx;
static kcondvar_t cv;
static int value;

static void
thethread(void *arg)
{

	mutex_enter(&mtx);
	value = 1;
	cv_signal(&cv);
	mutex_exit(&mtx);

	kthread_exit(0);
}

void
rumptest_thread()
{
	struct lwp *newl;
	int rv;

	mutex_init(&mtx, MUTEX_DEFAULT, IPL_NONE);
	cv_init(&cv, "jooei");
	rv = kthread_create(PRI_NONE, KTHREAD_MPSAFE, NULL,
	    thethread, NULL, &newl, "ktest");
	if (rv)
		panic("thread creation failed: %d", rv);

	mutex_enter(&mtx);
	while (value == 0)
		cv_wait(&cv, &mtx);
	mutex_exit(&mtx);

	/* try to verify thread really exists and we don't crash */
	kpause("take1", false, 1, NULL);
}
