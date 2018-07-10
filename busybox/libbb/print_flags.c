/* vi: set sw=4 ts=4: */
/*
 * Print string that matches bit masked flags
 *
 * Copyright (C) 2008 Natanael Copa <natanael.copa@gmail.com>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

/* returns a set with the flags not printed */
int FAST_FUNC print_flags_separated(const int *masks, const char *labels, int flags, const char *separator)
{
	const char *need_separator = NULL;
	while (*labels) {
		if (flags & *masks) {
			printf("%s%s",
				need_separator ? need_separator : "",
				labels);
			need_separator = separator;
			flags &= ~ *masks;
		}
		masks++;
		labels += strlen(labels) + 1;
	}
	return flags;
}

int FAST_FUNC print_flags(const masks_labels_t *ml, int flags)
{
	return print_flags_separated(ml->masks, ml->labels, flags, NULL);
}
