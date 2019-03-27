/* $Header: /p/tcsh/cvsroot/tcsh/tc.sig.c,v 3.40 2012/01/25 15:34:41 christos Exp $ */
/*
 * tc.sig.c: Signal routine emulations
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "sh.h"

RCSID("$tcsh: tc.sig.c,v 3.40 2012/01/25 15:34:41 christos Exp $")

#include "tc.wait.h"

void
sigset_interrupting(int sig, void (*fn) (int))
{
    struct sigaction act;

    act.sa_handler = fn;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(sig, &act, NULL) == 0) {
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, sig);
	sigprocmask(SIG_UNBLOCK, &set, NULL);
    }
}

static volatile sig_atomic_t alrmcatch_pending; /* = 0; */
static volatile sig_atomic_t pchild_pending; /* = 0; */
static volatile sig_atomic_t phup_pending; /* = 0; */
static volatile sig_atomic_t pintr_pending; /* = 0; */
int alrmcatch_disabled; /* = 0; */
int phup_disabled; /* = 0; */
int pchild_disabled; /* = 0; */
int pintr_disabled; /* = 0; */
int handle_interrupt; /* = 0; */

int
handle_pending_signals(void)
{
    int rv = 0;
    if (!phup_disabled && phup_pending) {
	phup_pending = 0;
	handle_interrupt++;
	phup();
	handle_interrupt--;
    }
    if (!pintr_disabled && pintr_pending) {
	pintr_pending = 0;
	handle_interrupt++;
	pintr();
	handle_interrupt--;
	rv = 1;
    }
    if (!pchild_disabled && pchild_pending) {
	pchild_pending = 0;
	handle_interrupt++;
	pchild();
	handle_interrupt--;
    }
    if (!alrmcatch_disabled && alrmcatch_pending) {
	alrmcatch_pending = 0;
	handle_interrupt++;
	alrmcatch();
	handle_interrupt--;
    }
    return rv;
}

void
queue_alrmcatch(int sig)
{
    USE(sig);
    alrmcatch_pending = 1;
}

void
queue_pchild(int sig)
{
    USE(sig);
    pchild_pending = 1;
}

void
queue_phup(int sig)
{
    USE(sig);
    phup_pending = 1;
}

void
queue_pintr(int sig)
{
    USE(sig);
    pintr_pending = 1;
}

void
disabled_cleanup(void *xdisabled)
{
    int *disabled;

    disabled = xdisabled;
    if (--*disabled == 0)
	handle_pending_signals();
}

void
pintr_disabled_restore(void *xold)
{
    int *old;

    old = xold;
    pintr_disabled = *old;
}

void
pintr_push_enable(int *saved)
{
    *saved = pintr_disabled;
    pintr_disabled = 0;
    cleanup_push(saved, pintr_disabled_restore);
    handle_pending_signals();
}
