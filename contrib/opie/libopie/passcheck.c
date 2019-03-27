/* passcheck.c: The opiepasscheck() library function.

%%% portions-copyright-cmetz-96
Portions of this software are Copyright 1996-1999 by Craig Metz, All Rights
Reserved. The Inner Net License Version 2 applies to these portions of
the software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

Portions of this software are Copyright 1995 by Randall Atkinson and Dan
McDonald, All Rights Reserved. All Rights under this copyright are assigned
to the U.S. Naval Research Laboratory (NRL). The NRL Copyright Notice and
License Agreement applies to this software.

        History:

	Modified by cmetz for OPIE 2.3. OPIE_PASS_{MIN,MAX} changed to
		OPIE_SECRET_{MIN,MAX}.
	Modified by cmetz for OPIE 2.2. Use FUNCTION declaration et al.
        Created at NRL for OPIE 2.2 from opiesubr.c.
*/
#include "opie_cfg.h"

#include <stdio.h>
#include <string.h>

#include "opie.h"

/* 
   Applies "good password" rules to the secret pass phrase.

   We currently implement the following:

   Passwords must be at least OPIE_SECRET_MIN (10) characters long.
   Passwords must be at most OPIE_SECRET_MAX (127) characters long.

   N.B.: Passing NULL pointers to this function is a bad idea.
*/
int opiepasscheck FUNCTION((secret), char *secret)
{
	int len = strlen(secret);

	if (len < OPIE_SECRET_MIN)
		return 1;

	if (len > OPIE_SECRET_MAX)
		return 1;

	return 0;
}
