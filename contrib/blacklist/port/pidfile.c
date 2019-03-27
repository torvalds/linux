/*	$NetBSD: pidfile.c,v 1.2 2016/04/05 12:28:57 christos Exp $	*/

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe, Matthias Scheler and Julio Merino.
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
#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("$NetBSD: pidfile.c,v 1.2 2016/04/05 12:28:57 christos Exp $");
#endif

#include <sys/param.h>

#include <paths.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#ifdef HAVE_UTIL_H
#include <util.h>
#endif

static pid_t pidfile_pid;
static char *pidfile_path;

/* Deletes an existent pidfile iff it was created by this process. */
static void
pidfile_cleanup(void)
{

	if ((pidfile_path != NULL) && (pidfile_pid == getpid()))
		(void) unlink(pidfile_path);
}

/* Registers an atexit(3) handler to delete the pidfile we have generated.
 * We only register the handler when we create a pidfile, so we can assume
 * that the pidfile exists.
 *
 * Returns 0 on success or -1 if the handler could not be registered. */
static int
register_atexit_handler(void)
{
	static bool done = false;

	if (!done) {
		if (atexit(pidfile_cleanup) < 0)
			return -1;
		done = true;
	}

	return 0;
}

/* Given a new pidfile name in 'path', deletes any previously-created pidfile
 * if the previous file differs to the new one.
 *
 * If a previous file is deleted, returns 1, which means that a new pidfile
 * must be created.  Otherwise, this returns 0, which means that the existing
 * file does not need to be touched. */
static int
cleanup_old_pidfile(const char* path)
{
	if (pidfile_path != NULL) {
		if (strcmp(pidfile_path, path) != 0) {
			pidfile_cleanup();

			free(pidfile_path);
			pidfile_path = NULL;

			return 1;
		} else
			return 0;
	} else
		return 1;
}

/* Constructs a name for a pidfile in the default location (/var/run).  If
 * 'basename' is NULL, uses the name of the current program for the name of
 * the pidfile.
 *
 * Returns a pointer to a dynamically-allocatd string containing the absolute
 * path to the pidfile; NULL on failure. */
static char *
generate_varrun_path(const char *bname)
{
	char *path;

	if (bname == NULL)
		bname = getprogname();

	/* _PATH_VARRUN includes trailing / */
	if (asprintf(&path, "%s%s.pid", _PATH_VARRUN, bname) == -1)
		return NULL;
	return path;
}

/* Creates a pidfile with the provided name.  The new pidfile is "registered"
 * in the global variables pidfile_path and pidfile_pid so that any further
 * call to pidfile(3) can check if we are recreating the same file or a new
 * one.
 *
 * Returns 0 on success or -1 if there is any error. */
static int
create_pidfile(const char* path)
{
	FILE *f;

	if (register_atexit_handler() == -1)
		return -1;

	if (cleanup_old_pidfile(path) == 0)
		return 0;

	pidfile_path = strdup(path);
	if (pidfile_path == NULL)
		return -1;

	if ((f = fopen(path, "w")) == NULL) {
		free(pidfile_path);
		pidfile_path = NULL;
		return -1;
	}

	pidfile_pid = getpid();

	(void) fprintf(f, "%d\n", pidfile_pid);
	(void) fclose(f);

	return 0;
}

int
pidfile(const char *path)
{

	if (path == NULL || strchr(path, '/') == NULL) {
		char *default_path;

		if ((default_path = generate_varrun_path(path)) == NULL)
			return -1;

		if (create_pidfile(default_path) == -1) {
			free(default_path);
			return -1;
		}

		free(default_path);
		return 0;
	} else
		return create_pidfile(path);
}
