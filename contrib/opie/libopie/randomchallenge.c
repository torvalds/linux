/* randomchallenge.c: The opierandomchallenge() library function.

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

	Modified by cmetz for OPIE 2.4. Use snprintf().
	Modified by cmetz for OPIE 2.32. Initialize algids[] with 0s
	     instead of NULL.
        Modified by cmetz for OPIE 2.3. Add sha support.
	Modified by cmetz for OPIE 2.22. Don't include stdio.h.
	     Use opienewseed(). Don't include unneeded headers.
	Modified by cmetz for OPIE 2.2. Use FUNCTION declaration et al.
             Changed use of gethostname() to uname(). Ifdefed around some
             headers.
        Created at NRL for OPIE 2.2 from opiesubr2.c
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "opie_cfg.h"
#include "opie.h"

static char *algids[] = { 0, 0, 0, "sha1", "md4", "md5" };

/* Generate a random challenge */
/* This could grow into quite a monster, really. Random is good enough for
   most situations; it is certainly better than a fixed string */
VOIDRET opierandomchallenge FUNCTION((prompt), char *prompt)
{
  char buf[OPIE_SEED_MAX+1];

  buf[0] = 0;
  if (opienewseed(buf))
    strcpy(buf, "ke4452");

  snprintf(prompt, OPIE_CHALLENGE_MAX+1, "otp-%s %d %s ext", algids[MDX],
  	(rand() % 499) + 1, buf);
}
