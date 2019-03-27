/* newseed.c: The opienewseed() library function.

%%% copyright-cmetz-96
This software is Copyright 1996-2001 by Craig Metz, All Rights Reserved.
The Inner Net License Version 3 applies to this software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

	History:

	Modified by cmetz for OPIE 2.4. Greatly simplified increment. Now does
		not add digits. Reformatted the code.
	Modified by cmetz for OPIE 2.32. Added syslog.h if DEBUG.
	Modified by cmetz for OPIE 2.31. Added time.h.
	Created by cmetz for OPIE 2.22.

$FreeBSD$
*/

#include "opie_cfg.h"
#ifndef HAVE_TIME_H
#define HAVE_TIME_H 1
#endif
#if HAVE_TIME_H
#include <time.h>
#endif /* HAVE_TIME_H */
#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */
#include <ctype.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#if HAVE_SYS_UTSNAME_H
#include <sys/utsname.h>
#endif /* HAVE_SYS_UTSNAME_H */
#include <errno.h>
#if DEBUG
#include <syslog.h>
#endif /* DEBUG */
#include <stdio.h>
#include <stdlib.h>
#include "opie.h"

int opienewseed FUNCTION((seed), char *seed)
{
	if (!seed)
		return -1;

	if (seed[0]) {
		char *c;
		unsigned int i, max;

		if ((i = strlen(seed)) > OPIE_SEED_MAX)
			i = OPIE_SEED_MAX;

		for (c = seed + i - 1, max = 1;
				(c >= seed) && isdigit(*c); c--)
			max *= 10;

		if ((i = strtoul(++c, (char **)0, 10)) < max) {
			if (++i >= max)
				i = 1;

			sprintf(c, "%d", i);
			return 0;
		}
	}

	{
		time_t now;

		time(&now);
		srand(now);
	}

	{
		struct utsname utsname;

		if (uname(&utsname) < 0) {
#if DEBUG
			syslog(LOG_DEBUG, "uname: %s(%d)", strerror(errno),
				errno);
#endif /* DEBUG */
			utsname.nodename[0] = 'k';
			utsname.nodename[1] = 'e';
		}
		utsname.nodename[2] = 0;

		if (snprintf(seed, OPIE_SEED_MAX+1, "%s%04d", utsname.nodename,
				(rand() % 9999) + 1) >= OPIE_SEED_MAX+1)
			return -1;
		return 0;
	}
}

