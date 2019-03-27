/*-
 * Copyright (c) 2015 The NetBSD Foundation, Inc.
 * Copyright (c) 2016 The FreeBSD Foundation, Inc.
 * All rights reserved.
 *
 * Portions of this software were developed by Kurt Lidl
 * under sponsorship from the FreeBSD Foundation.
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

#include "includes.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>

#include "ssh.h"
#include "packet.h"
#include "log.h"
#include "misc.h"
#include "servconf.h"
#include <blacklist.h>
#include "blacklist_client.h"

static struct blacklist *blstate = NULL;

/* import */
extern ServerOptions options;

/* internal definition from bl.h */
struct blacklist *bl_create(bool, char *, void (*)(int, const char *, va_list));

/* impedence match vsyslog() to sshd's internal logging levels */
void
im_log(int priority, const char *message, va_list args)
{
	LogLevel imlevel;

	switch (priority) {
	case LOG_ERR:
		imlevel = SYSLOG_LEVEL_ERROR;
		break;
	case LOG_DEBUG:
		imlevel = SYSLOG_LEVEL_DEBUG1;
		break;
	case LOG_INFO:
		imlevel = SYSLOG_LEVEL_INFO;
		break;
	default:
		imlevel = SYSLOG_LEVEL_DEBUG2;
	}
	do_log(imlevel, message, args);
}

void
blacklist_init(void)
{

	if (options.use_blacklist)
		blstate = bl_create(false, NULL, im_log);
}

void
blacklist_notify(int action, const char *msg)
{

	if (blstate != NULL && packet_connection_is_on_socket())
		(void)blacklist_r(blstate, action,
		packet_get_connection_in(), msg);
}
