/*-
 * Copyright (c) 2003-2010 Tim Kientzle
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "archive_platform.h"
__FBSDID("$FreeBSD$");

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#if defined(_WIN32) && !defined(__CYGWIN__)
#include <windows.h>
#include <winbase.h>
#endif

#include "archive_private.h"

static void
errmsg(const char *m)
{
	size_t s = strlen(m);
	ssize_t written;

	while (s > 0) {
		written = write(2, m, strlen(m));
		if (written <= 0)
			return;
		m += written;
		s -= written;
	}
}

static __LA_DEAD void
diediedie(void)
{
#if defined(_WIN32) && !defined(__CYGWIN__) && defined(_DEBUG)
	/* Cause a breakpoint exception  */
	DebugBreak();
#endif
	abort();        /* Terminate the program abnormally. */
}

static const char *
state_name(unsigned s)
{
	switch (s) {
	case ARCHIVE_STATE_NEW:		return ("new");
	case ARCHIVE_STATE_HEADER:	return ("header");
	case ARCHIVE_STATE_DATA:	return ("data");
	case ARCHIVE_STATE_EOF:		return ("eof");
	case ARCHIVE_STATE_CLOSED:	return ("closed");
	case ARCHIVE_STATE_FATAL:	return ("fatal");
	default:			return ("??");
	}
}

static const char *
archive_handle_type_name(unsigned m)
{
	switch (m) {
	case ARCHIVE_WRITE_MAGIC:	return ("archive_write");
	case ARCHIVE_READ_MAGIC:	return ("archive_read");
	case ARCHIVE_WRITE_DISK_MAGIC:	return ("archive_write_disk");
	case ARCHIVE_READ_DISK_MAGIC:	return ("archive_read_disk");
	case ARCHIVE_MATCH_MAGIC:	return ("archive_match");
	default:			return NULL;
	}
}


static char *
write_all_states(char *buff, unsigned int states)
{
	unsigned int lowbit;

	buff[0] = '\0';

	/* A trick for computing the lowest set bit. */
	while ((lowbit = states & (1 + ~states)) != 0) {
		states &= ~lowbit;		/* Clear the low bit. */
		strcat(buff, state_name(lowbit));
		if (states != 0)
			strcat(buff, "/");
	}
	return buff;
}

/*
 * Check magic value and current state.
 *   Magic value mismatches are fatal and result in calls to abort().
 *   State mismatches return ARCHIVE_FATAL.
 *   Otherwise, returns ARCHIVE_OK.
 *
 * This is designed to catch serious programming errors that violate
 * the libarchive API.
 */
int
__archive_check_magic(struct archive *a, unsigned int magic,
    unsigned int state, const char *function)
{
	char states1[64];
	char states2[64];
	const char *handle_type;

	/*
	 * If this isn't some form of archive handle,
	 * then the library user has screwed up so bad that
	 * we don't even have a reliable way to report an error.
	 */
	handle_type = archive_handle_type_name(a->magic);

	if (!handle_type) {
		errmsg("PROGRAMMER ERROR: Function ");
		errmsg(function);
		errmsg(" invoked with invalid archive handle.\n");
		diediedie();
	}

	if (a->magic != magic) {
		archive_set_error(a, -1,
		    "PROGRAMMER ERROR: Function '%s' invoked"
		    " on '%s' archive object, which is not supported.",
		    function,
		    handle_type);
		a->state = ARCHIVE_STATE_FATAL;
		return (ARCHIVE_FATAL);
	}

	if ((a->state & state) == 0) {
		/* If we're already FATAL, don't overwrite the error. */
		if (a->state != ARCHIVE_STATE_FATAL)
			archive_set_error(a, -1,
			    "INTERNAL ERROR: Function '%s' invoked with"
			    " archive structure in state '%s',"
			    " should be in state '%s'",
			    function,
			    write_all_states(states1, a->state),
			    write_all_states(states2, state));
		a->state = ARCHIVE_STATE_FATAL;
		return (ARCHIVE_FATAL);
	}
	return ARCHIVE_OK;
}
