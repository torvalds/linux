/* vi: set sw=4 ts=4: */
/*
 * ascii-to-numbers implementations for busybox
 *
 * Copyright (C) 2003  Manuel Novoa III  <mjn3@codepoet.org>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "libbb.h"

#define type long long
#define xstrtou(rest) xstrtoull##rest
#define xstrto(rest) xstrtoll##rest
#define xatou(rest) xatoull##rest
#define xato(rest) xatoll##rest
#define XSTR_UTYPE_MAX ULLONG_MAX
#define XSTR_TYPE_MAX LLONG_MAX
#define XSTR_TYPE_MIN LLONG_MIN
#define XSTR_STRTOU strtoull
#include "xatonum_template.c"

#if ULONG_MAX != ULLONG_MAX
#define type long
#define xstrtou(rest) xstrtoul##rest
#define xstrto(rest) xstrtol##rest
#define xatou(rest) xatoul##rest
#define xato(rest) xatol##rest
#define XSTR_UTYPE_MAX ULONG_MAX
#define XSTR_TYPE_MAX LONG_MAX
#define XSTR_TYPE_MIN LONG_MIN
#define XSTR_STRTOU strtoul
#include "xatonum_template.c"
#endif

#if UINT_MAX != ULONG_MAX
static ALWAYS_INLINE
unsigned bb_strtoui(const char *str, char **end, int b)
{
	unsigned long v = strtoul(str, end, b);
	if (v > UINT_MAX) {
		errno = ERANGE;
		return UINT_MAX;
	}
	return v;
}
#define type int
#define xstrtou(rest) xstrtou##rest
#define xstrto(rest) xstrtoi##rest
#define xatou(rest) xatou##rest
#define xato(rest) xatoi##rest
#define XSTR_UTYPE_MAX UINT_MAX
#define XSTR_TYPE_MAX INT_MAX
#define XSTR_TYPE_MIN INT_MIN
/* libc has no strtoui, so we need to create/use our own */
#define XSTR_STRTOU bb_strtoui
#include "xatonum_template.c"
#endif

/* A few special cases */

int FAST_FUNC xatoi_positive(const char *numstr)
{
	return xatou_range(numstr, 0, INT_MAX);
}

uint16_t FAST_FUNC xatou16(const char *numstr)
{
	return xatou_range(numstr, 0, 0xffff);
}

const struct suffix_mult bkm_suffixes[] = {
	{ "b", 512 },
	{ "k", 1024 },
	{ "m", 1024*1024 },
	{ "", 0 }
};

const struct suffix_mult cwbkMG_suffixes[] = {
	{ "c", 1 },
	{ "w", 2 },
	{ "b", 512 },
	{ "kB", 1000 },
	{ "kD", 1000 },
	{ "k", 1024 },
	{ "KB", 1000 }, /* compat with coreutils dd */
	{ "KD", 1000 }, /* compat with coreutils dd */
	{ "K", 1024 },  /* compat with coreutils dd */
	{ "MB", 1000000 },
	{ "MD", 1000000 },
	{ "M", 1024*1024 },
	{ "GB", 1000000000 },
	{ "GD", 1000000000 },
	{ "G", 1024*1024*1024 },
	/* "D" suffix for decimal is not in coreutils manpage, looks like it's deprecated */
	/* coreutils also understands TPEZY suffixes for tera- and so on, with B suffix for decimal */
	{ "", 0 }
};

const struct suffix_mult kmg_i_suffixes[] = {
	{ "KiB", 1024 },
	{ "kiB", 1024 },
	{ "K", 1024 },
	{ "k", 1024 },
	{ "MiB", 1048576 },
	{ "miB", 1048576 },
	{ "M", 1048576 },
	{ "m", 1048576 },
	{ "GiB", 1073741824 },
	{ "giB", 1073741824 },
	{ "G", 1073741824 },
	{ "g", 1073741824 },
	{ "KB", 1000 },
	{ "MB", 1000000 },
	{ "GB", 1000000000 },
	{ "", 0 }
};
