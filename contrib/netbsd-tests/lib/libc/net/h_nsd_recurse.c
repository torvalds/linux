/*	$NetBSD: h_nsd_recurse.c,v 1.2 2011/01/13 02:24:51 pgoyette Exp $ */

/*-
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: h_nsd_recurse.c,v 1.2 2011/01/13 02:24:51 pgoyette Exp $");

#define	_REENTRANT

#include <assert.h>
#include <nsswitch.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static const ns_src testsrc[] = {
	{ "test",	NS_SUCCESS },
	{ NULL, 0 }
};

static int
func3(void *rv, void *cb_data, va_list ap)
{
	(void)printf("func3: enter\n");
	(void)printf("func3: exit\n");

	return NS_SUCCESS;
}

static int
func2(void *rv, void *cb_data, va_list ap)
{
	static const ns_dtab dtab[] = {
		{ "test",	func3,		NULL },
		{ NULL, NULL, NULL }
	};
	int r;

	(void)printf("func2: enter\n");
	r = nsdispatch(NULL, dtab, "test", "test", testsrc);
	(void)printf("func2: exit\n");

	return r;
}

static int
func1(void)
{
	static const ns_dtab dtab[] = {
		{ "test",	func2,		NULL },
		{ NULL, NULL, NULL }
	};
	int r;

	(void)printf("func1: enter\n");
	r = nsdispatch(NULL, dtab, "test", "test", testsrc);
	(void)printf("func1: exit\n");

	return r;
}

static void *
thrfunc(void *arg)
{
	pthread_exit(NULL);
}

int
main(int argc, char *argv[])
{
	pthread_t thr;
	void *threval;

	assert(pthread_create(&thr, NULL, thrfunc, NULL) == 0);
	assert(func1() == NS_SUCCESS);
	assert(pthread_join(thr, &threval) == 0);
}
