/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

/* Like strncpy but make sure the resulting string is always 0 terminated. */
char* FAST_FUNC safe_strncpy(char *dst, const char *src, size_t size)
{
	if (!size) return dst;
	dst[--size] = '\0';
	return strncpy(dst, src, size);
}

/* Like strcpy but can copy overlapping strings. */
void FAST_FUNC overlapping_strcpy(char *dst, const char *src)
{
	/* Cheap optimization for dst == src case -
	 * better to have it here than in many callers.
	 */
	if (dst != src) {
		while ((*dst = *src) != '\0') {
			dst++;
			src++;
		}
	}
}
