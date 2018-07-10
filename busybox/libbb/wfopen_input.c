/* vi: set sw=4 ts=4: */
/*
 * wfopen_input implementation for busybox
 *
 * Copyright (C) 2003  Manuel Novoa III  <mjn3@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

/* A number of applets need to open a file for reading, where the filename
 * is a command line arg.  Since often that arg is '-' (meaning stdin),
 * we avoid testing everywhere by consolidating things in this routine.
 */

FILE* FAST_FUNC fopen_or_warn_stdin(const char *filename)
{
	FILE *fp = stdin;

	if (filename != bb_msg_standard_input
	 && NOT_LONE_DASH(filename)
	) {
		fp = fopen_or_warn(filename, "r");
	}
	return fp;
}

FILE* FAST_FUNC xfopen_stdin(const char *filename)
{
	FILE *fp = fopen_or_warn_stdin(filename);
	if (fp)
		return fp;
	xfunc_die();  /* We already output an error message. */
}

int FAST_FUNC open_or_warn_stdin(const char *filename)
{
	int fd = STDIN_FILENO;

	if (filename != bb_msg_standard_input
	 && NOT_LONE_DASH(filename)
	) {
		fd = open_or_warn(filename, O_RDONLY);
	}

	return fd;
}

int FAST_FUNC xopen_stdin(const char *filename)
{
	int fd = open_or_warn_stdin(filename);
	if (fd >= 0)
		return fd;
	xfunc_die();  /* We already output an error message. */
}
