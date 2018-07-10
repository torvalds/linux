/* vi: set sw=4 ts=4: */
/*
 * Match fstypes for use in mount unmount
 * We accept notmpfs,nfs but not notmpfs,nonfs
 * This allows us to match fstypes that start with no like so
 *   mount -at ,noddy
 *
 * Returns 1 for a match, otherwise 0
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

int FAST_FUNC fstype_matches(const char *fstype, const char *comma_list)
{
	int match = 1;

	if (!comma_list)
		return match;

	if (comma_list[0] == 'n' && comma_list[1] == 'o') {
		match--;
		comma_list += 2;
	}

	while (1) {
		char *after_mnt_type = is_prefixed_with(comma_list, fstype);
		if (after_mnt_type
		 && (*after_mnt_type == '\0' || *after_mnt_type == ',')
		) {
			return match;
		}
		comma_list = strchr(comma_list, ',');
		if (!comma_list)
			break;
		comma_list++;
	}

	return !match;
}
