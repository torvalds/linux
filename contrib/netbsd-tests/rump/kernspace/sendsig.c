/*	$NetBSD: sendsig.c,v 1.1 2011/01/14 13:08:00 pooka Exp $	*/

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
__RCSID("$NetBSD: sendsig.c,v 1.1 2011/01/14 13:08:00 pooka Exp $");
#endif /* !lint */

#include <sys/param.h>
#include <sys/proc.h>

#include <rump/rump.h>

#include "kernspace.h"

/*
 * loop until a non-system process appears and we can send it a signal
 */
void
rumptest_sendsig(char *signo)
{
	struct proc *p;
	bool sent = false;
	int sig;

	sig = strtoull(signo, NULL, 10);
	rump_boot_setsigmodel(RUMP_SIGMODEL_RAISE);

	mutex_enter(proc_lock);
	while (!sent) {
		PROCLIST_FOREACH(p, &allproc) {
			if (p->p_pid > 1) {
				mutex_enter(p->p_lock);
				psignal(p, sig);
				mutex_exit(p->p_lock);
				sent = true;
				break;
			}
		}
		kpause("w8", false, 1, proc_lock);
	}
	mutex_exit(proc_lock);

	/* restore default */
	rump_boot_setsigmodel(RUMP_SIGMODEL_PANIC);
}

void
rumptest_localsig(int signo)
{
	struct proc *p = curproc;

	mutex_enter(p->p_lock);
	psignal(p, signo);
	mutex_exit(p->p_lock);
}
