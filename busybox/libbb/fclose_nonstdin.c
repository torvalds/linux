/* vi: set sw=4 ts=4: */
/*
 * fclose_nonstdin implementation for busybox
 *
 * Copyright (C) 2003  Manuel Novoa III  <mjn3@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

/* A number of standard utilities can accept multiple command line args
 * of '-' for stdin, according to SUSv3.  So we encapsulate the check
 * here to save a little space.
 */
int FAST_FUNC fclose_if_not_stdin(FILE *f)
{
	/* Some more paranoid applets want ferror() check too */
	int r = ferror(f); /* NB: does NOT set errno! */
	if (r)
		errno = EIO; /* so we'll help it */
	if (f != stdin)
		return (r | fclose(f)); /* fclose does set errno on error */
	return r;
}
