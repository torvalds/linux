/*	$NetBSD: lockme.c,v 1.1 2011/01/06 13:12:52 pooka Exp $	*/

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: lockme.c,v 1.1 2011/01/06 13:12:52 pooka Exp $");
#endif /* !lint */

#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>

#include "kernspace.h"

struct somemem {
	char foo;
	kmutex_t mutexetum;
	char oof;
};

void
rumptest_lockme(enum locktest what)
{
	struct somemem *some;
	kmutex_t mtx;
	krwlock_t rw;

	rw_init(&rw);
	mutex_init(&mtx, MUTEX_DEFAULT, IPL_NONE);

	switch (what) {
	case LOCKME_MTX:
		mutex_enter(&mtx);
		mutex_enter(&mtx);
		break;
	case LOCKME_RWDOUBLEX:
		rw_enter(&rw, RW_WRITER);
		rw_enter(&rw, RW_WRITER);
		break;
	case LOCKME_RWRX:
		rw_enter(&rw, RW_READER);
		rw_enter(&rw, RW_WRITER);
		break;
	case LOCKME_RWXR:
		rw_enter(&rw, RW_WRITER);
		rw_enter(&rw, RW_READER);
		break;
	case LOCKME_DOUBLEINIT:
		mutex_init(&mtx, MUTEX_DEFAULT, IPL_NONE);
		break;
	case LOCKME_DOUBLEFREE:
		mutex_destroy(&mtx);
		mutex_destroy(&mtx);
		break;
	case LOCKME_DESTROYHELD:
		mutex_enter(&mtx);
		mutex_destroy(&mtx);
		break;
	case LOCKME_MEMFREE:
		some = kmem_alloc(sizeof(*some), KM_SLEEP);
		mutex_init(&some->mutexetum, MUTEX_DEFAULT, IPL_NONE);
		kmem_free(some, sizeof(*some));
		break;
	}
}
