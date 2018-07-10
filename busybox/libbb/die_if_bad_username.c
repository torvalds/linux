/* vi: set sw=4 ts=4: */
/*
 * Check user and group names for illegal characters
 *
 * Copyright (C) 2008 Tito Ragusa <farmatito@tiscali.it>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

/* To avoid problems, the username should consist only of
 * letters, digits, underscores, periods, at signs and dashes,
 * and not start with a dash (as defined by IEEE Std 1003.1-2001).
 * For compatibility with Samba machine accounts $ is also supported
 * at the end of the username.
 */

void FAST_FUNC die_if_bad_username(const char *name)
{
	const char *start = name;

	/* 1st char being dash or dot isn't valid:
	 * for example, name like ".." can make adduser
	 * chown "/home/.." recursively - NOT GOOD.
	 * Name of just a single "$" is also rejected.
	 */
	goto skip;

	do {
		unsigned char ch;

		/* These chars are valid unless they are at the 1st pos: */
		if (*name == '-'
		 || *name == '.'
		/* $ is allowed if it's the last char: */
		 || (*name == '$' && !name[1])
		) {
			continue;
		}
 skip:
		ch = *name;
		if (ch == '_'
		/* || ch == '@' -- we disallow this too. Think about "user@host" */
		/* open-coded isalnum: */
		 || (ch >= '0' && ch <= '9')
		 || ((ch|0x20) >= 'a' && (ch|0x20) <= 'z')
		) {
			continue;
		}
		bb_error_msg_and_die("illegal character with code %u at position %u",
				(unsigned)ch, (unsigned)(name - start));
	} while (*++name);

	/* The minimum size of the login name is one char or two if
	 * last char is the '$'. Violations of this are caught above.
	 * The maximum size of the login name is LOGIN_NAME_MAX
	 * including the terminating null byte.
	 */
	if (name - start >= LOGIN_NAME_MAX)
		bb_error_msg_and_die("name is too long");
}
