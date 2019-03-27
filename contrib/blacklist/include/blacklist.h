/*	$NetBSD: blacklist.h,v 1.3 2015/01/23 18:48:56 christos Exp $	*/

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
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
#ifndef _BLACKLIST_H
#define _BLACKLIST_H

#include <sys/socket.h>

__BEGIN_DECLS
struct blacklist *blacklist_open(void);
void blacklist_close(struct blacklist *);
int blacklist(int, int, const char *);
int blacklist_r(struct blacklist *, int, int, const char *);
int blacklist_sa(int, int, const struct sockaddr *, socklen_t, const char *);
int blacklist_sa_r(struct blacklist *, int, int,
    const struct sockaddr *, socklen_t, const char *);
__END_DECLS

/* action values for user applications */
#define BLACKLIST_API_ENUM	1
enum {
        BLACKLIST_AUTH_OK = 0,
        BLACKLIST_AUTH_FAIL,
        BLACKLIST_ABUSIVE_BEHAVIOR,
        BLACKLIST_BAD_USER
};

#endif /* _BLACKLIST_H */
