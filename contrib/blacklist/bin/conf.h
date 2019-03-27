/*	$NetBSD: conf.h,v 1.6 2015/01/27 19:40:36 christos Exp $	*/

/*-
 * Copyright (c) 2015 The NetBSD Foundation, Inc.
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
#ifndef _CONF_H
#define _CONF_H

#include <sys/socket.h>

struct conf {
	struct sockaddr_storage	c_ss;
	int			c_lmask;
	int			c_port;
	int			c_proto;
	int			c_family;
	int			c_uid;
	int			c_nfail;
	char			c_name[128];
	int			c_rmask;
	int			c_duration;
};

struct confset {
	struct conf *cs_c;
	size_t cs_n;
	size_t cs_m;
};

#define CONFNAMESZ sizeof(((struct conf *)0)->c_name)

__BEGIN_DECLS
const char *conf_print(char *, size_t, const char *, const char *,
    const struct conf *);
void conf_parse(const char *);
const struct conf *conf_find(int, uid_t, const struct sockaddr_storage *,
    struct conf *);
__END_DECLS

#endif /* _CONF_H */
