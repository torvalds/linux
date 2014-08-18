/*
 * This file is part of the Linux kernel, and is made available under
 * the terms of the GNU General Public License version 2.
 *
 * Misc librarized functions for cmdline poking.
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <asm/setup.h>

static inline int myisspace(u8 c)
{
	return c <= ' ';	/* Close enough approximation */
}

/**
 * Find a boolean option (like quiet,noapic,nosmp....)
 *
 * @cmdline: the cmdline string
 * @option: option string to look for
 *
 * Returns the position of that @option (starts counting with 1)
 * or 0 on not found.
 */
int cmdline_find_option_bool(const char *cmdline, const char *option)
{
	char c;
	int len, pos = 0, wstart = 0;
	const char *opptr = NULL;
	enum {
		st_wordstart = 0,	/* Start of word/after whitespace */
		st_wordcmp,	/* Comparing this word */
		st_wordskip,	/* Miscompare, skip */
	} state = st_wordstart;

	if (!cmdline)
		return -1;      /* No command line */

	len = min_t(int, strlen(cmdline), COMMAND_LINE_SIZE);
	if (!len)
		return 0;

	while (len--) {
		c = *(char *)cmdline++;
		pos++;

		switch (state) {
		case st_wordstart:
			if (!c)
				return 0;
			else if (myisspace(c))
				break;

			state = st_wordcmp;
			opptr = option;
			wstart = pos;
			/* fall through */

		case st_wordcmp:
			if (!*opptr)
				if (!c || myisspace(c))
					return wstart;
				else
					state = st_wordskip;
			else if (!c)
				return 0;
			else if (c != *opptr++)
				state = st_wordskip;
			else if (!len)		/* last word and is matching */
				return wstart;
			break;

		case st_wordskip:
			if (!c)
				return 0;
			else if (myisspace(c))
				state = st_wordstart;
			break;
		}
	}

	return 0;	/* Buffer overrun */
}
