///////////////////////////////////////////////////////////////////////////////
//
/// \file       util.c
/// \brief      Miscellaneous utility functions
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "private.h"
#include <stdarg.h>


/// Buffers for uint64_to_str() and uint64_to_nicestr()
static char bufs[4][128];

/// Thousand separator support in uint64_to_str() and uint64_to_nicestr()
static enum { UNKNOWN, WORKS, BROKEN } thousand = UNKNOWN;


extern void *
xrealloc(void *ptr, size_t size)
{
	assert(size > 0);

	// Save ptr so that we can free it if realloc fails.
	// The point is that message_fatal ends up calling stdio functions
	// which in some libc implementations might allocate memory from
	// the heap. Freeing ptr improves the chances that there's free
	// memory for stdio functions if they need it.
	void *p = ptr;
	ptr = realloc(ptr, size);

	if (ptr == NULL) {
		const int saved_errno = errno;
		free(p);
		message_fatal("%s", strerror(saved_errno));
	}

	return ptr;
}


extern char *
xstrdup(const char *src)
{
	assert(src != NULL);
	const size_t size = strlen(src) + 1;
	char *dest = xmalloc(size);
	return memcpy(dest, src, size);
}


extern uint64_t
str_to_uint64(const char *name, const char *value, uint64_t min, uint64_t max)
{
	uint64_t result = 0;

	// Skip blanks.
	while (*value == ' ' || *value == '\t')
		++value;

	// Accept special value "max". Supporting "min" doesn't seem useful.
	if (strcmp(value, "max") == 0)
		return max;

	if (*value < '0' || *value > '9')
		message_fatal(_("%s: Value is not a non-negative "
				"decimal integer"), value);

	do {
		// Don't overflow.
		if (result > UINT64_MAX / 10)
			goto error;

		result *= 10;

		// Another overflow check
		const uint32_t add = *value - '0';
		if (UINT64_MAX - add < result)
			goto error;

		result += add;
		++value;
	} while (*value >= '0' && *value <= '9');

	if (*value != '\0') {
		// Look for suffix. Originally this supported both base-2
		// and base-10, but since there seems to be little need
		// for base-10 in this program, treat everything as base-2
		// and also be more relaxed about the case of the first
		// letter of the suffix.
		uint64_t multiplier = 0;
		if (*value == 'k' || *value == 'K')
			multiplier = UINT64_C(1) << 10;
		else if (*value == 'm' || *value == 'M')
			multiplier = UINT64_C(1) << 20;
		else if (*value == 'g' || *value == 'G')
			multiplier = UINT64_C(1) << 30;

		++value;

		// Allow also e.g. Ki, KiB, and KB.
		if (*value != '\0' && strcmp(value, "i") != 0
				&& strcmp(value, "iB") != 0
				&& strcmp(value, "B") != 0)
			multiplier = 0;

		if (multiplier == 0) {
			message(V_ERROR, _("%s: Invalid multiplier suffix"),
					value - 1);
			message_fatal(_("Valid suffixes are `KiB' (2^10), "
					"`MiB' (2^20), and `GiB' (2^30)."));
		}

		// Don't overflow here either.
		if (result > UINT64_MAX / multiplier)
			goto error;

		result *= multiplier;
	}

	if (result < min || result > max)
		goto error;

	return result;

error:
	message_fatal(_("Value of the option `%s' must be in the range "
				"[%" PRIu64 ", %" PRIu64 "]"),
				name, min, max);
}


extern uint64_t
round_up_to_mib(uint64_t n)
{
	return (n >> 20) + ((n & ((UINT32_C(1) << 20) - 1)) != 0);
}


/// Check if thousand separator is supported. Run-time checking is easiest,
/// because it seems to be sometimes lacking even on POSIXish system.
static void
check_thousand_sep(uint32_t slot)
{
	if (thousand == UNKNOWN) {
		bufs[slot][0] = '\0';
		snprintf(bufs[slot], sizeof(bufs[slot]), "%'u", 1U);
		thousand = bufs[slot][0] == '1' ? WORKS : BROKEN;
	}

	return;
}


extern const char *
uint64_to_str(uint64_t value, uint32_t slot)
{
	assert(slot < ARRAY_SIZE(bufs));

	check_thousand_sep(slot);

	if (thousand == WORKS)
		snprintf(bufs[slot], sizeof(bufs[slot]), "%'" PRIu64, value);
	else
		snprintf(bufs[slot], sizeof(bufs[slot]), "%" PRIu64, value);

	return bufs[slot];
}


extern const char *
uint64_to_nicestr(uint64_t value, enum nicestr_unit unit_min,
		enum nicestr_unit unit_max, bool always_also_bytes,
		uint32_t slot)
{
	assert(unit_min <= unit_max);
	assert(unit_max <= NICESTR_TIB);
	assert(slot < ARRAY_SIZE(bufs));

	check_thousand_sep(slot);

	enum nicestr_unit unit = NICESTR_B;
	char *pos = bufs[slot];
	size_t left = sizeof(bufs[slot]);

	if ((unit_min == NICESTR_B && value < 10000)
			|| unit_max == NICESTR_B) {
		// The value is shown as bytes.
		if (thousand == WORKS)
			my_snprintf(&pos, &left, "%'u", (unsigned int)value);
		else
			my_snprintf(&pos, &left, "%u", (unsigned int)value);
	} else {
		// Scale the value to a nicer unit. Unless unit_min and
		// unit_max limit us, we will show at most five significant
		// digits with one decimal place.
		double d = (double)(value);
		do {
			d /= 1024.0;
			++unit;
		} while (unit < unit_min || (d > 9999.9 && unit < unit_max));

		if (thousand == WORKS)
			my_snprintf(&pos, &left, "%'.1f", d);
		else
			my_snprintf(&pos, &left, "%.1f", d);
	}

	static const char suffix[5][4] = { "B", "KiB", "MiB", "GiB", "TiB" };
	my_snprintf(&pos, &left, " %s", suffix[unit]);

	if (always_also_bytes && value >= 10000) {
		if (thousand == WORKS)
			snprintf(pos, left, " (%'" PRIu64 " B)", value);
		else
			snprintf(pos, left, " (%" PRIu64 " B)", value);
	}

	return bufs[slot];
}


extern void
my_snprintf(char **pos, size_t *left, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	const int len = vsnprintf(*pos, *left, fmt, ap);
	va_end(ap);

	// If an error occurred, we want the caller to think that the whole
	// buffer was used. This way no more data will be written to the
	// buffer. We don't need better error handling here, although it
	// is possible that the result looks garbage on the terminal if
	// e.g. an UTF-8 character gets split. That shouldn't (easily)
	// happen though, because the buffers used have some extra room.
	if (len < 0 || (size_t)(len) >= *left) {
		*left = 0;
	} else {
		*pos += len;
		*left -= len;
	}

	return;
}


extern bool
is_empty_filename(const char *filename)
{
	if (filename[0] == '\0') {
		message_error(_("Empty filename, skipping"));
		return true;
	}

	return false;
}


extern bool
is_tty_stdin(void)
{
	const bool ret = isatty(STDIN_FILENO);

	if (ret)
		message_error(_("Compressed data cannot be read from "
				"a terminal"));

	return ret;
}


extern bool
is_tty_stdout(void)
{
	const bool ret = isatty(STDOUT_FILENO);

	if (ret)
		message_error(_("Compressed data cannot be written to "
				"a terminal"));

	return ret;
}
