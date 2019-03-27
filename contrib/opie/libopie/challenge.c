/* challenge.c: The opiechallenge() library function.

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

	Modified by cmetz for OPIE 2.32. Added extended response set
		identifier to the challenge.
	Modified by cmetz for OPIE 2.3. Use opie_ prefix. Send debug info to
		syslog. Add sha plumbing.
	Modified by cmetz for OPIE 2.2. Use FUNCTION declaration et al.
        Created at NRL for OPIE 2.2 from opiesubr2.c

$FreeBSD$

*/
#include "opie_cfg.h"
#include <stdio.h>
#include <string.h>
#if DEBUG
#include <syslog.h>
#endif /* DEBUG */
#include "opie.h"

/* Return an OTP challenge string for user 'name'. 

   The return values are:

   0  = All good
   -1 = Low-level error (file, memory, I/O, etc.)
   1  = High-level error (user not found or locked)

   This function MUST eventually be followed by an opieverify() to release
   the user lock and file handles.

   This function will give you a blanked-out state block if it returns a
   nonzero status. Even though it returns a non-zero status and a blank
   state block, you still MUST call opieverify() to clear the lock and
   any internal state (the latter condition is not actually used yet).
*/

static char *algids[] = { NULL, NULL, NULL, "sha1", "md4", "md5" };

int opiechallenge FUNCTION((mp, name, ss), struct opie *mp AND char *name AND char *ss)
{
  int rval = -1;

  rval = opielookup(mp, name);
#if DEBUG
  if (rval) syslog(LOG_DEBUG, "opiechallenge: opielookup(mp, name=%s) returned %d", name, rval);
#endif /* DEBUG */

  if (!rval) {
    rval = opielock(name);
#if DEBUG
    if (rval) syslog(LOG_DEBUG, "opiechallenge: opielock(name=%s) returned %d", name, rval);
#endif /* DEBUG */
  }

  if (rval ||
    (snprintf(ss, OPIE_CHALLENGE_MAX+1, "otp-%s %d %s ext", algids[MDX], mp->opie_n - 1, mp->opie_seed) >= OPIE_CHALLENGE_MAX+1)) {
    if (!rval)
      rval = 1;
    opierandomchallenge(ss);
    memset(mp, 0, sizeof(*mp));
  }

  return rval;
}
