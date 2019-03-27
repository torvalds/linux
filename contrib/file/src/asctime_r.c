/*	$File: asctime_r.c,v 1.1 2012/05/15 17:14:36 christos Exp $	*/

#include "file.h"
#ifndef	lint
FILE_RCSID("@(#)$File: asctime_r.c,v 1.1 2012/05/15 17:14:36 christos Exp $")
#endif	/* lint */
#include <time.h>
#include <string.h>

/* asctime_r is not thread-safe anyway */
char *
asctime_r(const struct tm *t, char *dst)
{
	char *p = asctime(t);
	if (p == NULL)
		return NULL;
	memcpy(dst, p, 26);
	return dst;
}
