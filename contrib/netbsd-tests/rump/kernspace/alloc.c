/*	$NetBSD: alloc.c,v 1.1 2010/06/14 21:06:09 pooka Exp $	*/

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
__RCSID("$NetBSD: alloc.c,v 1.1 2010/06/14 21:06:09 pooka Exp $");
#endif /* !lint */

#include <sys/param.h>
#include <sys/condvar.h>
#include <sys/kmem.h>
#include <sys/kthread.h>
#include <sys/mutex.h>
#include <sys/pool.h>
#include <sys/proc.h>

#include <uvm/uvm.h>

#include <rump/rumpuser.h>
#include "kernspace.h"

static void *store[32];
static struct pool pp1, pp2;

static kmutex_t mtx;
static kcondvar_t kcv;
static int curstat;

static void
hthr(void *arg)
{
	int i;

	mutex_enter(&mtx);
	curstat++;
	cv_signal(&kcv);

	while (curstat < 2)
		cv_wait(&kcv, &mtx);
	mutex_exit(&mtx);

	/* try to guarantee that the sleep is triggered in PR_WAITOK */
	while ((kernel_map->flags & VM_MAP_WANTVA) == 0)
		kpause("take5", false, 1, NULL);

	for (i = 0; i < __arraycount(store); i++) {
		pool_put(&pp1, store[i]);
	}

	kthread_exit(0);
}

void
rumptest_alloc(size_t thelimit)
{
	char *c;
	int succ, i;

	mutex_init(&mtx, MUTEX_DEFAULT, IPL_NONE);
	cv_init(&kcv, "venailu");

	kthread_create(PRI_NONE, KTHREAD_MPSAFE, NULL, hthr, NULL, NULL, "h");

	pool_init(&pp1, 1024, 0, 0, 0, "vara-allas",
	    &pool_allocator_nointr, IPL_NONE);
	pool_init(&pp2, 1024, 0, 0, 0, "allas",
	    &pool_allocator_nointr, IPL_NONE);
	
	for (i = 0; i < __arraycount(store); i++) {
		store[i] = pool_get(&pp1, PR_NOWAIT);
		if (store[i] == NULL) {
			panic("pool_get store failed");
		}
	}

	/* wait until other thread runs */
	mutex_enter(&mtx);
	while (curstat == 0)
		cv_wait(&kcv, &mtx);
	mutex_exit(&mtx);

	for (succ = 0;; succ++) {
		if (succ * 1024 > thelimit)
			panic("managed to allocate over limit");
		if ((c = pool_get(&pp2, PR_NOWAIT)) == NULL) {
			mutex_enter(&mtx);
			curstat++;
			cv_signal(&kcv);
			mutex_exit(&mtx);
			if (pool_get(&pp2, PR_WAITOK) == NULL)
				panic("pool get PR_WAITOK failed");
			break;
		}
		*c = 'a';
	}
}
