/*	$NetBSD: tsleep.c,v 1.4 2014/03/21 22:18:57 dholland Exp $	*/

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
__RCSID("$NetBSD: tsleep.c,v 1.4 2014/03/21 22:18:57 dholland Exp $");
#endif /* !lint */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/proc.h>

#include "kernspace.h"

#define NTHREADS 10

/*
 * mpsafe thread.  need dedicated interlock
 */
static kmutex_t mymtx;

static void
tinythread(void *arg)
{
	static int wakeups;
	int i, rv;
	bool relock = ((uintptr_t)arg % 2) == 0;

	for (i = 0; i < 1000; i++) {
		mutex_enter(&mymtx);
		wakeup(tinythread);
		if (wakeups >= NTHREADS-1) {
			mutex_exit(&mymtx);
			break;
		}
		rv = mtsleep(tinythread, relock ? 0 : PNORELOCK,
		    "haa", 0, &mymtx);
		if (relock)
			mutex_exit(&mymtx);
		if (rv != 0)
			panic("mtsleep failed");
	}

	mutex_enter(&mymtx);
	wakeups++;
	wakeup(tinythread);

	rv = mtsleep(rumptest_tsleep, PNORELOCK, "kepuli", 1, &mymtx);
	if (rv != EWOULDBLOCK)
		panic("mtsleep unexpected return value %d", rv);

	kthread_exit(0);
}

void
rumptest_tsleep()
{
	struct lwp *notbigl[NTHREADS];
	int rv, i;

	mutex_init(&mymtx, MUTEX_DEFAULT, IPL_NONE);

	for (i = 0; i < NTHREADS; i++) {
		rv = kthread_create(PRI_NONE, KTHREAD_MUSTJOIN| KTHREAD_MPSAFE,
		    NULL, tinythread, (void *)(uintptr_t)i, &notbigl[i], "nb");
		if (rv)
			panic("thread create failed: %d", rv);
	}

	for (i = 0; i < NTHREADS; i++) {
		kthread_join(notbigl[i]);
	}
}
