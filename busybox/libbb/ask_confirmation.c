/* vi: set sw=4 ts=4: */
/*
 * bb_ask_y_confirmation implementation for busybox
 *
 * Copyright (C) 2003  Manuel Novoa III  <mjn3@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

/* Read a line from fp.  If the first non-whitespace char is 'y' or 'Y',
 * return 1.  Otherwise return 0.
 */
int FAST_FUNC bb_ask_y_confirmation_FILE(FILE *fp)
{
	char first = 0;
	int c;

	fflush_all();
	while (((c = fgetc(fp)) != EOF) && (c != '\n')) {
		if (first == 0 && !isblank(c)) {
			first = c|0x20;
		}
	}

	return first == 'y';
}

int FAST_FUNC bb_ask_y_confirmation(void)
{
	return bb_ask_y_confirmation_FILE(stdin);
}
