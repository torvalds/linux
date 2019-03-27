/* $NetBSD: h_atexit.c,v 1.1 2011/01/12 19:44:08 pgoyette Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code was contributed to The NetBSD Foundation by Jason R. Thorpe.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2011\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: h_atexit.c,v 1.1 2011/01/12 19:44:08 pgoyette Exp $");

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

extern int __cxa_atexit(void (*func)(void *), void *, void *);
extern void __cxa_finalize(void *);

#ifdef __FreeBSD__
/*
 * On shared object unload, in __cxa_finalize, call and clear all installed
 * atexit and __cxa_atexit handlers that are either installed by unloaded
 * dso, or points to the functions provided by the dso.
 *
 * The reason of the change is to ensure that there is no lingering pointers
 * to the unloaded code after the dlclose(3).  It is known reason for infinite
 * stream of the crash reports for many programs which use loadable modules
 * and fail to properly clean on module unload.  Examples are apache, php,
 * perl etc.
 *
 * You pass the &dso_handle_1 and &dso_handle_2, which points inside the
 * main binary, to the registration function.  The code from r211706,
 * correctly detects that they are for the main binary, and on the first
 * call to __cxa_finalize(), which also pass pointer to main binary, all
 * registered functions from the main binary are executed.
 */

static void *dso_handle_1 = (void *)1;
static void *dso_handle_2 = (void *)2;
static void *dso_handle_3 = (void *)3;
#else
static int dso_handle_1;
static int dso_handle_2;
static int dso_handle_3;
#endif

static int arg_1;
static int arg_2;
static int arg_3;

static int exiting_state;

#define	ASSERT(expr)							\
do {									\
	if ((expr) == 0) {						\
		write(STDERR_FILENO, __func__, strlen(__func__));	\
		write(STDERR_FILENO, ": ", 2);				\
		write(STDERR_FILENO, __STRING(expr),			\
		      strlen(__STRING(expr)));				\
		write(STDERR_FILENO, "\n", 1);				\
		my_abort();						\
	}								\
} while (/*CONSTCOND*/0)

#define	SUCCESS()							\
do {									\
	write(STDOUT_FILENO, __func__, strlen(__func__));		\
	write(STDOUT_FILENO, "\n", 1);					\
} while (/*CONSTCOND*/0)

static void
my_abort(void)
{

	kill(getpid(), SIGABRT);
	/* NOTREACHED */
}

static void
cxa_handler_5(void *arg)
{
	static int cxa_handler_5_called;

	ASSERT(arg == (void *)&arg_1);
	ASSERT(cxa_handler_5_called == 0);
	ASSERT(exiting_state == 5);

	exiting_state--;
	cxa_handler_5_called = 1;
	SUCCESS();
}

static void
cxa_handler_4(void *arg)
{
	static int cxa_handler_4_called;

	ASSERT(arg == (void *)&arg_1);
	ASSERT(cxa_handler_4_called == 0);
	ASSERT(exiting_state == 4);

	exiting_state--;
	cxa_handler_4_called = 1;
	SUCCESS();
}

static void
cxa_handler_3(void *arg)
{
	static int cxa_handler_3_called;

	ASSERT(arg == (void *)&arg_2);
	ASSERT(cxa_handler_3_called == 0);
	ASSERT(exiting_state == 3);

	exiting_state--;
	cxa_handler_3_called = 1;
	SUCCESS();
}

static void
cxa_handler_2(void *arg)
{
	static int cxa_handler_2_called;

	ASSERT(arg == (void *)&arg_3);
	ASSERT(cxa_handler_2_called == 0);
	ASSERT(exiting_state == 2);

	exiting_state--;
	cxa_handler_2_called = 1;
	SUCCESS();
}

static void
normal_handler_1(void)
{
	static int normal_handler_1_called;

	ASSERT(normal_handler_1_called == 0);
	ASSERT(exiting_state == 1);

	exiting_state--;
	normal_handler_1_called = 1;
	SUCCESS();
}

static void
normal_handler_0(void)
{
	static int normal_handler_0_called;

	ASSERT(normal_handler_0_called == 0);
	ASSERT(exiting_state == 0);

	normal_handler_0_called = 1;
	SUCCESS();
}

int
main(int argc, char *argv[])
{

	exiting_state = 5;

	ASSERT(0 == atexit(normal_handler_0));
	ASSERT(0 == atexit(normal_handler_1));
#ifdef __FreeBSD__
	ASSERT(0 == __cxa_atexit(cxa_handler_4, &arg_1, dso_handle_1));
	ASSERT(0 == __cxa_atexit(cxa_handler_5, &arg_1, dso_handle_1));
	ASSERT(0 == __cxa_atexit(cxa_handler_3, &arg_2, dso_handle_2));
	ASSERT(0 == __cxa_atexit(cxa_handler_2, &arg_3, dso_handle_3));

	__cxa_finalize(dso_handle_1);
	__cxa_finalize(dso_handle_2);
#else
	ASSERT(0 == __cxa_atexit(cxa_handler_4, &arg_1, &dso_handle_1));
	ASSERT(0 == __cxa_atexit(cxa_handler_5, &arg_1, &dso_handle_1));
	ASSERT(0 == __cxa_atexit(cxa_handler_3, &arg_2, &dso_handle_2));
	ASSERT(0 == __cxa_atexit(cxa_handler_2, &arg_3, &dso_handle_3));

	__cxa_finalize(&dso_handle_1);
	__cxa_finalize(&dso_handle_2);
#endif
	exit(0);
}
