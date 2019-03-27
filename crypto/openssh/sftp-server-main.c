/* $OpenBSD: sftp-server-main.c,v 1.5 2016/02/15 09:47:49 dtucker Exp $ */
/*
 * Copyright (c) 2008 Markus Friedl.  All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "includes.h"

#include <sys/types.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#include "log.h"
#include "sftp.h"
#include "misc.h"
#include "xmalloc.h"

void
cleanup_exit(int i)
{
	sftp_server_cleanup_exit(i);
}

int
main(int argc, char **argv)
{
	struct passwd *user_pw;

	ssh_malloc_init();	/* must be called before any mallocs */
	/* Ensure that fds 0, 1 and 2 are open or directed to /dev/null */
	sanitise_stdfd();

	if ((user_pw = getpwuid(getuid())) == NULL) {
		fprintf(stderr, "No user found for uid %lu\n",
		    (u_long)getuid());
		return 1;
	}

	return (sftp_server_main(argc, argv, user_pw));
}
