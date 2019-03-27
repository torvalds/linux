/*	$NetBSD: unwind.c,v 1.1 2012/05/26 22:02:29 christos Exp $	*/

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
#include <sys/types.h>
#include <stdio.h>

#include "unwind.h"
#include "execinfo.h"

struct tracer_context {
	void **arr;
	size_t len;
	size_t n;
};

static _Unwind_Reason_Code
tracer(struct _Unwind_Context *ctx, void *arg)
{
	struct tracer_context *t = arg;
	if (t->n == (size_t)~0) {
		/* Skip backtrace frame */
		t->n = 0;
		return 0;
	}
	if (t->n < t->len)
		t->arr[t->n++] = _Unwind_GetIP(ctx);
	return 0;
}

size_t
backtrace(void **arr, size_t len)
{
	struct tracer_context ctx;

	ctx.arr = arr;
	ctx.len = len;
	ctx.n = (size_t)~0;

	_Unwind_Backtrace(tracer, &ctx);
	if (ctx.n != (size_t)~0 && ctx.n > 0)
		ctx.arr[--ctx.n] = NULL;	/* Skip frame below __start */

	return ctx.n;
}
