/**
 * error.c
 *
 * error reporting routines
 * basicly wrappers around printf
 *
 * (c) 2005 NLnet Labs
 *
 * See the file LICENSE for the license
 *
 */

#include "drill.h"
#include <ldns/ldns.h>

static void
warning_va_list(const char *fmt, va_list args)
{
        fprintf(stderr, "Warning: ");
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
}

void
warning(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	warning_va_list(fmt, args);
	va_end(args);
}

static void
error_va_list(const char *fmt, va_list args)
{
        fprintf(stderr, "Error: ");
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
}

void
error(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	error_va_list(fmt, args);
	va_end(args);
	exit(EXIT_FAILURE);
}

static void
verbose_va_list(const char *fmt, va_list args)
{
        vfprintf(stdout, fmt, args);
        fprintf(stdout, "\n");
}

/* print stuff */
void
mesg(const char *fmt, ...)
{
	va_list args;
	if (verbosity == -1) {
		return;
	}
	fprintf(stdout, ";; ");
	va_start(args, fmt);
	verbose_va_list(fmt, args);
	va_end(args);
}

#if 0
/* print stuff when in verbose mode (1) */
void
verbose(const char *fmt, ...)
{
	va_list args;
	if (verbosity < 1) {
		return;
	}

	va_start(args, fmt);
	verbose_va_list(fmt, args);
	va_end(args);
}
#endif
