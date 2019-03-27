/*	$NetBSD: bl.h,v 1.13 2016/03/11 17:16:40 christos Exp $	*/

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
#ifndef _BL_H
#define _BL_H

#include <stdbool.h>
#include <stdarg.h>
#include <sys/param.h>
#include <sys/socket.h>
#include "blacklist.h"

typedef enum {
	BL_INVALID,
	BL_ADD,
	BL_DELETE,
	BL_ABUSE,
	BL_BADUSER
} bl_type_t;

typedef struct {
	bl_type_t bi_type;
	int bi_fd;
	uid_t bi_uid;
	gid_t bi_gid;
	socklen_t bi_slen;
	struct sockaddr_storage bi_ss;
	char bi_msg[1024];
} bl_info_t;

#define bi_cred bi_u._bi_cred

#ifndef _PATH_BLSOCK
#define _PATH_BLSOCK "/var/run/blacklistd.sock"
#endif

__BEGIN_DECLS

typedef struct blacklist *bl_t;

bl_t bl_create(bool, const char *, void (*)(int, const char *, va_list));
void bl_destroy(bl_t);
int bl_send(bl_t, bl_type_t, int, const struct sockaddr *, socklen_t,
    const char *);
int bl_getfd(bl_t);
bl_info_t *bl_recv(bl_t);
bool bl_isconnected(bl_t);

__END_DECLS

#endif /* _BL_H */
