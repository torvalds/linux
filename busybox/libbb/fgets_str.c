/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) many different people.
 * If you wrote this, please acknowledge your work.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

static char *xmalloc_fgets_internal(FILE *file, const char *terminating_string, int chop_off, size_t *maxsz_p)
{
	char *linebuf = NULL;
	const int term_length = strlen(terminating_string);
	int end_string_offset;
	int linebufsz = 0;
	int idx = 0;
	int ch;
	size_t maxsz = *maxsz_p;

	while (1) {
		ch = fgetc(file);
		if (ch == EOF) {
			if (idx == 0)
				return linebuf; /* NULL */
			break;
		}

		if (idx >= linebufsz) {
			linebufsz += 200;
			linebuf = xrealloc(linebuf, linebufsz);
			if (idx >= maxsz) {
				linebuf[idx] = ch;
				idx++;
				break;
			}
		}

		linebuf[idx] = ch;
		idx++;

		/* Check for terminating string */
		end_string_offset = idx - term_length;
		if (end_string_offset >= 0
		 && memcmp(&linebuf[end_string_offset], terminating_string, term_length) == 0
		) {
			if (chop_off)
				idx -= term_length;
			break;
		}
	}
	/* Grow/shrink *first*, then store NUL */
	linebuf = xrealloc(linebuf, idx + 1);
	linebuf[idx] = '\0';
	*maxsz_p = idx;
	return linebuf;
}

/* Read up to TERMINATING_STRING from FILE and return it,
 * including terminating string.
 * Non-terminated string can be returned if EOF is reached.
 * Return NULL if EOF is reached immediately.  */
char* FAST_FUNC xmalloc_fgets_str(FILE *file, const char *terminating_string)
{
	size_t maxsz = INT_MAX - 4095;
	return xmalloc_fgets_internal(file, terminating_string, 0, &maxsz);
}

char* FAST_FUNC xmalloc_fgets_str_len(FILE *file, const char *terminating_string, size_t *maxsz_p)
{
	size_t maxsz;

	if (!maxsz_p) {
		maxsz = INT_MAX - 4095;
		maxsz_p = &maxsz;
	}
	return xmalloc_fgets_internal(file, terminating_string, 0, maxsz_p);
}

char* FAST_FUNC xmalloc_fgetline_str(FILE *file, const char *terminating_string)
{
	size_t maxsz = INT_MAX - 4095;
	return xmalloc_fgets_internal(file, terminating_string, 1, &maxsz);
}
