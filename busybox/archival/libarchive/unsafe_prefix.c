/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include "bb_archive.h"

const char* FAST_FUNC strip_unsafe_prefix(const char *str)
{
	const char *cp = str;
	while (1) {
		char *cp2;
		if (*cp == '/') {
			cp++;
			continue;
		}
		if (is_prefixed_with(cp, "/../"+1)) {
			cp += 3;
			continue;
		}
		cp2 = strstr(cp, "/../");
		if (!cp2)
			break;
		cp = cp2 + 4;
	}
	if (cp != str) {
		static smallint warned = 0;
		if (!warned) {
			warned = 1;
			bb_error_msg("removing leading '%.*s' from member names",
				(int)(cp - str), str);
		}
	}
	return cp;
}
