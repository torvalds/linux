/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

/* On exit: errno = 0 only if there was non-empty, '\0' terminated value
 * errno = EINVAL if value was not '\0' terminated, but otherwise ok
 *    Return value is still valid, caller should just check whether end[0]
 *    is a valid terminating char for particular case. OTOH, if caller
 *    requires '\0' terminated input, [s]he can just check errno == 0.
 * errno = ERANGE if value had alphanumeric terminating char ("1234abcg").
 * errno = ERANGE if value is out of range, missing, etc.
 * errno = ERANGE if value had minus sign for strtouXX (even "-0" is not ok )
 *    return value is all-ones in this case.
 *
 * Test code:
 * char *endptr;
 * const char *minus = "-";
 * errno = 0;
 * bb_strtoi(minus, &endptr, 0); // must set ERANGE
 * printf("minus:%p endptr:%p errno:%d EINVAL:%d\n", minus, endptr, errno, EINVAL);
 * errno = 0;
 * bb_strtoi("-0-", &endptr, 0); // must set EINVAL and point to second '-'
 * printf("endptr[0]:%c errno:%d EINVAL:%d\n", endptr[0], errno, EINVAL);
 */

static unsigned long long ret_ERANGE(void)
{
	errno = ERANGE; /* this ain't as small as it looks (on glibc) */
	return ULLONG_MAX;
}

static unsigned long long handle_errors(unsigned long long v, char **endp)
{
	char next_ch = **endp;

	/* errno is already set to ERANGE by strtoXXX if value overflowed */
	if (next_ch) {
		/* "1234abcg" or out-of-range? */
		if (isalnum(next_ch) || errno)
			return ret_ERANGE();
		/* good number, just suspicious terminator */
		errno = EINVAL;
	}
	return v;
}


unsigned long long FAST_FUNC bb_strtoull(const char *arg, char **endp, int base)
{
	unsigned long long v;
	char *endptr;

	if (!endp) endp = &endptr;
	*endp = (char*) arg;

	/* strtoul("  -4200000000") returns 94967296, errno 0 (!) */
	/* I don't think that this is right. Preventing this... */
	if (!isalnum(arg[0])) return ret_ERANGE();

	/* not 100% correct for lib func, but convenient for the caller */
	errno = 0;
	v = strtoull(arg, endp, base);
	return handle_errors(v, endp);
}

long long FAST_FUNC bb_strtoll(const char *arg, char **endp, int base)
{
	unsigned long long v;
	char *endptr;
	char first;

	if (!endp) endp = &endptr;
	*endp = (char*) arg;

	/* Check for the weird "feature":
	 * a "-" string is apparently a valid "number" for strto[u]l[l]!
	 * It returns zero and errno is 0! :( */
	first = (arg[0] != '-' ? arg[0] : arg[1]);
	if (!isalnum(first)) return ret_ERANGE();

	errno = 0;
	v = strtoll(arg, endp, base);
	return handle_errors(v, endp);
}

#if ULONG_MAX != ULLONG_MAX
unsigned long FAST_FUNC bb_strtoul(const char *arg, char **endp, int base)
{
	unsigned long v;
	char *endptr;

	if (!endp) endp = &endptr;
	*endp = (char*) arg;

	if (!isalnum(arg[0])) return ret_ERANGE();
	errno = 0;
	v = strtoul(arg, endp, base);
	return handle_errors(v, endp);
}

long FAST_FUNC bb_strtol(const char *arg, char **endp, int base)
{
	long v;
	char *endptr;
	char first;

	if (!endp) endp = &endptr;
	*endp = (char*) arg;

	first = (arg[0] != '-' ? arg[0] : arg[1]);
	if (!isalnum(first)) return ret_ERANGE();

	errno = 0;
	v = strtol(arg, endp, base);
	return handle_errors(v, endp);
}
#endif

#if UINT_MAX != ULONG_MAX
unsigned FAST_FUNC bb_strtou(const char *arg, char **endp, int base)
{
	unsigned long v;
	char *endptr;

	if (!endp) endp = &endptr;
	*endp = (char*) arg;

	if (!isalnum(arg[0])) return ret_ERANGE();
	errno = 0;
	v = strtoul(arg, endp, base);
	if (v > UINT_MAX) return ret_ERANGE();
	return handle_errors(v, endp);
}

int FAST_FUNC bb_strtoi(const char *arg, char **endp, int base)
{
	long v;
	char *endptr;
	char first;

	if (!endp) endp = &endptr;
	*endp = (char*) arg;

	first = (arg[0] != '-' ? arg[0] : arg[1]);
	if (!isalnum(first)) return ret_ERANGE();

	errno = 0;
	v = strtol(arg, endp, base);
	if (v > INT_MAX) return ret_ERANGE();
	if (v < INT_MIN) return ret_ERANGE();
	return handle_errors(v, endp);
}
#endif
