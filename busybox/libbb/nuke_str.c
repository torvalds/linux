/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 2008 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//kbuild:lib-y += nuke_str.o

#include "libbb.h"

void FAST_FUNC nuke_str(char *str)
{
        if (str) {
		while (*str)
			*str++ = 0;
		/* or: memset(str, 0, strlen(str)); - not as small as above */
	}
}
