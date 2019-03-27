/*	$NetBSD: blacklist.c,v 1.5 2015/01/22 16:19:53 christos Exp $	*/

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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/cdefs.h>
__RCSID("$NetBSD: blacklist.c,v 1.5 2015/01/22 16:19:53 christos Exp $");

#include <stdio.h>
#include <bl.h>

#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>

int
blacklist_sa(int action, int rfd, const struct sockaddr *sa, socklen_t salen,
    const char *msg)
{
	struct blacklist *bl;
	int rv;
	if ((bl = blacklist_open()) == NULL)
		return -1;
	rv = blacklist_sa_r(bl, action, rfd, sa, salen, msg);
	blacklist_close(bl);
	return rv;
}

int
blacklist_sa_r(struct blacklist *bl, int action, int rfd,
	const struct sockaddr *sa, socklen_t slen, const char *msg)
{
	int internal_action;

	/* internal values are not the same as user application values */
	switch (action) {
	case BLACKLIST_AUTH_FAIL:
		internal_action = BL_ADD;
		break;
	case BLACKLIST_AUTH_OK:
		internal_action = BL_DELETE;
		break;
	case BLACKLIST_ABUSIVE_BEHAVIOR:
		internal_action = BL_ABUSE;
		break;
	case BLACKLIST_BAD_USER:
		internal_action = BL_BADUSER;
		break;
	default:
		internal_action = BL_INVALID;
		break;
	}
	return bl_send(bl, internal_action, rfd, sa, slen, msg);
}

int
blacklist(int action, int rfd, const char *msg)
{
	return blacklist_sa(action, rfd, NULL, 0, msg);
}

int
blacklist_r(struct blacklist *bl, int action, int rfd, const char *msg)
{
	return blacklist_sa_r(bl, action, rfd, NULL, 0, msg);
}

struct blacklist *
blacklist_open(void) {
	return bl_create(false, NULL, vsyslog);
}

void
blacklist_close(struct blacklist *bl)
{
	bl_destroy(bl);
}
