/* vi: set sw=4 ts=4: */
/*
 * Busybox xregcomp utility routine.  This isn't in libbb.h because the
 * C library we're linking against may not support regex.h.
 *
 * Based in part on code from sash, Copyright (c) 1999 by David I. Bell
 * Permission has been granted to redistribute this code under GPL.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#ifndef BB_REGEX_H
#define BB_REGEX_H 1

#include <regex.h>

PUSH_AND_SET_FUNCTION_VISIBILITY_TO_HIDDEN

char* regcomp_or_errmsg(regex_t *preg, const char *regex, int cflags) FAST_FUNC;
void xregcomp(regex_t *preg, const char *regex, int cflags) FAST_FUNC;

POP_SAVED_FUNCTION_VISIBILITY

#endif
