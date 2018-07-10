/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include <math.h>  /* just for HUGE_VAL */

#define NOT_DIGIT(a) (((unsigned char)(a-'0')) > 9)

#if 0 // UNUSED
double FAST_FUNC bb_strtod(const char *arg, char **endp)
{
	double v;
	char *endptr;

	/* Allow .NN form. People want to use "sleep .15" etc */
	if (arg[0] != '-' && arg[0] != '.' && NOT_DIGIT(arg[0]))
		goto err;
	errno = 0;
	v = strtod(arg, &endptr);
	if (endp)
		*endp = endptr;
	if (endptr[0]) {
		/* "1234abcg" or out-of-range? */
		if (isalnum(endptr[0]) || errno) {
 err:
			errno = ERANGE;
			return HUGE_VAL;
		}
		/* good number, just suspicious terminator */
		errno = EINVAL;
	}
	return v;
}
#endif

#if 0
/* String to timespec: "NNNN[.NNNNN]" -> struct timespec.
 * Can be used for other fixed-point needs.
 * Returns pointer past last converted char,
 * and returns errno similar to bb_strtoXX functions.
 */
char* FAST_FUNC bb_str_to_ts(struct timespec *ts, const char *arg)
{
	if (sizeof(ts->tv_sec) <= sizeof(int))
		ts->tv_sec = bb_strtou(arg, &arg, 10);
	else if (sizeof(ts->tv_sec) <= sizeof(long))
		ts->tv_sec = bb_strtoul(arg, &arg, 10);
	else
		ts->tv_sec = bb_strtoull(arg, &arg, 10);
	ts->tv_nsec = 0;

	if (*arg != '.')
		return arg;

	/* !EINVAL: number is not ok (alphanumeric ending, overflow etc) */
	if (errno != EINVAL)
		return arg;

	if (!*++arg) /* "NNN." */
		return arg;

	{ /* "NNN.xxx" - parse xxx */
		int ndigits;
		char *p;
		char buf[10]; /* we never use more than 9 digits */

		/* Need to make a copy to avoid false overflow */
		safe_strncpy(buf, arg, 10);
		ts->tv_nsec = bb_strtou(buf, &p, 10);
		ndigits = p - buf;
		arg += ndigits;
		/* normalize to nsec */
		while (ndigits < 9) {
			ndigits++;
			ts->tv_nsec *= 10;
		}
		while (isdigit(*arg)) /* skip possible 10th plus digits */
			arg++;
	}
	return arg;
}
#endif
