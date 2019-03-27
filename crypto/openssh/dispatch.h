/* $OpenBSD: dispatch.h,v 1.14 2017/05/31 07:00:13 markus Exp $ */

/*
 * Copyright (c) 2000 Markus Friedl.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef DISPATCH_H
#define DISPATCH_H

#define DISPATCH_MAX	255

enum {
	DISPATCH_BLOCK,
	DISPATCH_NONBLOCK
};

struct ssh;

typedef int dispatch_fn(int, u_int32_t, struct ssh *);

int	dispatch_protocol_error(int, u_int32_t, struct ssh *);
int	dispatch_protocol_ignore(int, u_int32_t, struct ssh *);
void	ssh_dispatch_init(struct ssh *, dispatch_fn *);
void	ssh_dispatch_set(struct ssh *, int, dispatch_fn *);
void	ssh_dispatch_range(struct ssh *, u_int, u_int, dispatch_fn *);
int	ssh_dispatch_run(struct ssh *, int, volatile sig_atomic_t *);
void	ssh_dispatch_run_fatal(struct ssh *, int, volatile sig_atomic_t *);

#define dispatch_init(dflt) \
	ssh_dispatch_init(active_state, (dflt))
#define dispatch_range(from, to, fn) \
	ssh_dispatch_range(active_state, (from), (to), (fn))
#define dispatch_set(type, fn) \
	ssh_dispatch_set(active_state, (type), (fn))

#endif
