/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

void FAST_FUNC bb_perror_msg(const char *s, ...)
{
	va_list p;

	va_start(p, s);
	/* Guard against "<error message>: Success" */
	bb_verror_msg(s, p, errno ? strerror(errno) : NULL);
	va_end(p);
}

void FAST_FUNC bb_perror_msg_and_die(const char *s, ...)
{
	va_list p;

	va_start(p, s);
	/* Guard against "<error message>: Success" */
	bb_verror_msg(s, p, errno ? strerror(errno) : NULL);
	va_end(p);
	xfunc_die();
}

void FAST_FUNC bb_simple_perror_msg(const char *s)
{
	bb_perror_msg("%s", s);
}

void FAST_FUNC bb_simple_perror_msg_and_die(const char *s)
{
	bb_perror_msg_and_die("%s", s);
}
