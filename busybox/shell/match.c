/*
 * ##/%% variable matching code ripped out of ash shell for code sharing
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * Copyright (c) 1989, 1991, 1993, 1994
 *      The Regents of the University of California.  All rights reserved.
 *
 * Copyright (c) 1997-2005 Herbert Xu <herbert@gondor.apana.org.au>
 * was re-ported from NetBSD and debianized.
 */
#ifdef STANDALONE
# include <stdbool.h>
# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <unistd.h>
# define FAST_FUNC /* nothing */
# define PUSH_AND_SET_FUNCTION_VISIBILITY_TO_HIDDEN /* nothing */
# define POP_SAVED_FUNCTION_VISIBILITY /* nothing */
#else
# include "libbb.h"
#endif
#include <fnmatch.h>
#include "match.h"

char* FAST_FUNC scan_and_match(char *string, const char *pattern, unsigned flags)
{
	char *loc;
	char *end;
	unsigned len = strlen(string);
	int early_exit;

	/* We can stop the scan early only if the string part
	 * we are matching against is shrinking, and the pattern has
	 * an unquoted "star" at the corresponding end. There are two cases.
	 * Case 1:
	 * "qwerty" does not match against pattern "*zy",
	 * no point in trying to match "werty", "erty" etc:
	 */
	early_exit = (flags == (SCAN_MOVE_FROM_LEFT + SCAN_MATCH_RIGHT_HALF) && pattern[0] == '*');

	if (flags & SCAN_MOVE_FROM_LEFT) {
		loc = string;
		end = string + len + 1;
	} else {
		loc = string + len;
		end = string - 1;
		if (flags == (SCAN_MOVE_FROM_RIGHT + SCAN_MATCH_LEFT_HALF)) {
			/* Case 2:
			 * "qwerty" does not match against pattern "qz*",
			 * no point in trying to match "qwert", "qwer" etc:
			 */
			const char *p = pattern + strlen(pattern);
			if (--p >= pattern && *p == '*') {
				early_exit = 1;
				while (--p >= pattern && *p == '\\')
					early_exit ^= 1;
			}
		}
	}

	while (loc != end) {
		char c;
		int r;

		c = *loc;
		if (flags & SCAN_MATCH_LEFT_HALF) {
			*loc = '\0';
			r = fnmatch(pattern, string, 0);
			//bb_error_msg("fnmatch('%s','%s',0):%d", pattern, string, r);
			*loc = c;
		} else {
			r = fnmatch(pattern, loc, 0);
			//bb_error_msg("fnmatch('%s','%s',0):%d", pattern, loc, r);
		}
		if (r == 0) /* match found */
			return loc;
		if (early_exit) {
#ifdef STANDALONE
			printf("(early exit) ");
#endif
			break;
		}

		if (flags & SCAN_MOVE_FROM_LEFT) {
			loc++;
		} else {
			loc--;
		}
	}
	return NULL;
}

#ifdef STANDALONE
int main(int argc, char *argv[])
{
	char *string;
	char *op;
	char *pattern;
	char *loc;

	setvbuf(stdout, NULL, _IONBF, 0);

	if (!argv[1]) {
		puts(
			"Usage: match <test> [test...]\n\n"
			"Where a <test> is the form: <string><op><match>\n"
			"This is to test the shell ${var#val} expression type.\n\n"
			"e.g. `match 'abc#a*'` -> bc"
		);
		return 1;
	}

	while (*++argv) {
		size_t off;
		unsigned scan_flags;

		string = *argv;
		off = strcspn(string, "#%");
		if (!off) {
			printf("invalid format\n");
			continue;
		}
		op = string + off;
		scan_flags = pick_scan(op[0], op[1]);

		printf("'%s': flags:%x, ", string, scan_flags);
		pattern = op + 1;
		if (op[0] == op[1])
			pattern++;
		op[0] = '\0';

		loc = scan_and_match(string, pattern, scan_flags);

		if (scan_flags & SCAN_MATCH_LEFT_HALF) {
			printf("'%s'\n", loc);
		} else {
			if (loc)
				*loc = '\0';
			printf("'%s'\n", string);
		}
	}

	return 0;
}
#endif
