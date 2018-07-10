/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Based in part on code from sash, Copyright (c) 1999 by David I. Bell
 * Permission has been granted to redistribute this code under GPL.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

/*
 * Return TRUE if fileName is a directory.
 * Nonexistent files return FALSE.
 */
int FAST_FUNC is_directory(const char *fileName, int followLinks)
{
	int status;
	struct stat statBuf;

	if (followLinks)
		status = stat(fileName, &statBuf);
	else
		status = lstat(fileName, &statBuf);

	status = (status == 0 && S_ISDIR(statBuf.st_mode));

	return status;
}
