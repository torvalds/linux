/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 * Copyright (C) 2006 Rob Landley
 * Copyright (C) 2006 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "libbb.h"

/* All known arches use small ints for signals */
smallint bb_got_signal;

void record_signo(int signo)
{
	bb_got_signal = signo;
}

/* Saves 2 bytes on x86! Oh my... */
int FAST_FUNC sigaction_set(int signum, const struct sigaction *act)
{
	return sigaction(signum, act, NULL);
}

int FAST_FUNC sigprocmask_allsigs(int how)
{
	sigset_t set;
	sigfillset(&set);
	return sigprocmask(how, &set, NULL);
}

void FAST_FUNC bb_signals(int sigs, void (*f)(int))
{
	int sig_no = 0;
	int bit = 1;

	while (sigs) {
		if (sigs & bit) {
			sigs -= bit;
			signal(sig_no, f);
		}
		sig_no++;
		bit <<= 1;
	}
}

void FAST_FUNC bb_signals_recursive_norestart(int sigs, void (*f)(int))
{
	int sig_no = 0;
	int bit = 1;
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = f;
	/*sa.sa_flags = 0;*/
	/*sigemptyset(&sa.sa_mask); - hope memset did it*/

	while (sigs) {
		if (sigs & bit) {
			sigs -= bit;
			sigaction_set(sig_no, &sa);
		}
		sig_no++;
		bit <<= 1;
	}
}

void FAST_FUNC sig_block(int sig)
{
	sigset_t ss;
	sigemptyset(&ss);
	sigaddset(&ss, sig);
	sigprocmask(SIG_BLOCK, &ss, NULL);
}

void FAST_FUNC sig_unblock(int sig)
{
	sigset_t ss;
	sigemptyset(&ss);
	sigaddset(&ss, sig);
	sigprocmask(SIG_UNBLOCK, &ss, NULL);
}

void FAST_FUNC wait_for_any_sig(void)
{
	sigset_t ss;
	sigemptyset(&ss);
	sigsuspend(&ss);
}

/* Assuming the sig is fatal */
void FAST_FUNC kill_myself_with_sig(int sig)
{
	signal(sig, SIG_DFL);
	sig_unblock(sig);
	raise(sig);
	_exit(sig | 128); /* Should not reach it */
}

void FAST_FUNC signal_SA_RESTART_empty_mask(int sig, void (*handler)(int))
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	/*sigemptyset(&sa.sa_mask);*/
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = handler;
	sigaction_set(sig, &sa);
}

void FAST_FUNC signal_no_SA_RESTART_empty_mask(int sig, void (*handler)(int))
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	/*sigemptyset(&sa.sa_mask);*/
	/*sa.sa_flags = 0;*/
	sa.sa_handler = handler;
	sigaction_set(sig, &sa);
}
