/* unlock.c: The opieunlock() library function.

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

	Modified by cmetz for OPIE 2.31. Bug fix.
	Modified by cmetz for OPIE 2.3. Do refcounts whether or not
            we actually lock. Fixed USER_LOCKING=0 case.
	Modified by cmetz for OPIE 2.22. Added reference count support.
	    Changed lock filename/refcount symbol names to better indicate
	    that they're not user serviceable.
	Modified by cmetz for OPIE 2.2. Use FUNCTION declaration.
            Check for read() == -1. ifdef around unistd.h.
        Created at NRL for OPIE 2.2 from opiesubr2.c
*/
#include "opie_cfg.h"
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <fcntl.h>
#include "opie.h"

extern int __opie_lockrefcount;
#if USER_LOCKING
extern char *__opie_lockfilename;
#endif /* USER_LOCKING */

/* 
  Just remove the lock, right?
  Well, not exactly -- we need to make sure it's ours. 
*/
int opieunlock FUNCTION_NOARGS
{
#if USER_LOCKING
  int fh, rval = -1, pid, t, i;
  char buffer[128], *c, *c2;

  if (--__opie_lockrefcount > 0)
    return 0;

  if (!__opie_lockfilename)
    return -1;

  if (!(fh = open(__opie_lockfilename, O_RDWR, 0600)))
    goto unlockret;

  if ((i = read(fh, buffer, sizeof(buffer))) < 0)
    goto unlockret;

  buffer[sizeof(buffer) - 1] = 0;
  buffer[i - 1] = 0;

  if (!(c = strchr(buffer, '\n')))
    goto unlockret;

  *(c++) = 0;

  if (!(c2 = strchr(c, '\n')))
    goto unlockret;

  *(c2++) = 0;

  if (!(pid = atoi(buffer)))
    goto unlockret;

  if (!(t = atoi(c)))
    goto unlockret;

  if ((pid != getpid()) && (time(0) <= OPIE_LOCK_TIMEOUT + t) && (!kill(pid, 0))) { 
    rval = 1;
    goto unlockret1;
  }

  rval = 0;

unlockret:
  unlink(__opie_lockfilename);

unlockret1:
  if (fh)
    close(fh);
  free(__opie_lockfilename);
  __opie_lockfilename = NULL;
  return rval;
#else /* USER_LOCKING */
  if (__opie_lockrefcount-- > 0)
    return 0;

  return -1;
#endif /* USER_LOCKING */
}
