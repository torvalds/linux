/* lock.c: The opielock() library function.

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

	Modified by cmetz for OPIE 2.4. Use snprintf.
	Modified by cmetz for OPIE 2.31. Put locks in a separate dir.
            Bug fixes.
	Modified by cmetz for OPIE 2.3. Do refcounts whether or not we
            actually lock. Fixed USER_LOCKING=0 case.
	Modified by cmetz for OPIE 2.22. Added reference count for locks.
	    Changed lock filename/refcount symbol names to better indicate
	    that they're not user serviceable.
	Modified by cmetz for OPIE 2.2. Use FUNCTION declaration et al.
            Use "principal" instead of "name" to make it clearer.
            Ifdef around some headers, be more careful about allowed
            error return values. Check open() return value properly.
            Avoid NULL.
        Created at NRL for OPIE 2.2 from opiesubr2.c

$FreeBSD$
*/
#include "opie_cfg.h"
#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */
#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <sys/stat.h>
#include <syslog.h>
#include <fcntl.h>
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */
#include <errno.h>
#include "opie.h"

#if !HAVE_LSTAT
#define lstat(x, y) stat(x, y)
#endif /* !HAVE_LSTAT */

int __opie_lockrefcount = 0;
static int do_atexit = 1;

VOIDRET opiedisableaeh FUNCTION_NOARGS
{
  do_atexit = 0;
}
#if USER_LOCKING
char *__opie_lockfilename = (char *)0;

/* atexit() handler for opielock() */
VOIDRET opieunlockaeh FUNCTION_NOARGS
{
  if (__opie_lockfilename) {
    __opie_lockrefcount = 0;
    opieunlock();
  }
}
#endif /* USER_LOCKING */

/* 
   Serialize (we hope) authentication of user to prevent race conditions.
   Creates a lock file with a name of OPIE_LOCK_PREFIX with the user name
   appended. This file contains the pid of the lock's owner and a time()
   stamp. We use the former to check for dead owners and the latter to
   provide an upper bound on the lock duration. If there are any problems,
   we assume the lock is bogus.

   The value of this locking and its security implications are still not
   completely clear and require further study.

   One could conceivably hack this facility to provide locking of user
   accounts after several authentication failures.
 
   Return -1 on low-level error, 0 if ok, 1 on locking failure.
*/
int opielock FUNCTION((principal), char *principal)
{
#if USER_LOCKING
  int fh, waits = 0, rval = -1, pid, t, i;
  char buffer[128], buffer2[128], *c, *c2;
  struct stat statbuf[2];

  if (getuid() && geteuid()) {
#if DEBUG
    syslog(LOG_DEBUG, "opielock: requires superuser priveleges");
#endif /* DEBUG */
    return -1;
  };

  if (__opie_lockfilename) {
    __opie_lockrefcount++;
    return 0;
  }

  if (!(__opie_lockfilename = (char *)malloc(sizeof(OPIE_LOCK_DIR) + 1 + strlen(principal))))
    return -1;

  strcpy(__opie_lockfilename, OPIE_LOCK_DIR);

  if (mkdir(__opie_lockfilename, 0700) < 0)
    if (errno != EEXIST)
      return -1;

  if (lstat(__opie_lockfilename, &statbuf[0]) < 0)
    return -1;

  if (statbuf[0].st_uid) {
#if DEBUG
    syslog(LOG_DEBUG, "opielock: %s isn't owned by the superuser.", __opie_lockfilename);
#endif /* DEBUG */
    return -1;
  };

  if (!S_ISDIR(statbuf[0].st_mode)) {
#if DEBUG
    syslog(LOG_DEBUG, "opielock: %s isn't a directory.", __opie_lockfilename);
#endif /* DEBUG */
    return -1;
  };

  if ((statbuf[0].st_mode & 0777) != 00700) {
#if DEBUG
    syslog(LOG_DEBUG, "opielock: permissions on %s are not correct.", __opie_lockfilename);
#endif /* DEBUG */
    return -1;
  };

  strcat(__opie_lockfilename, "/");
  strcat(__opie_lockfilename, principal);

  fh = -1;
  while (fh < 0) {
    if (!lstat(__opie_lockfilename, &statbuf[0]))
      if (!S_ISREG(statbuf[0].st_mode)) 
        goto lockret;

    if ((fh = open(__opie_lockfilename, O_WRONLY | O_CREAT | O_EXCL, 0600)) < 0) {
      if (lstat(__opie_lockfilename, &statbuf[1]) < 0)
        goto lockret;
      if (statbuf[0].st_ino != statbuf[1].st_ino)
        goto lockret;
      if (statbuf[0].st_mode != statbuf[1].st_mode)
        goto lockret;
      if ((fh = open(__opie_lockfilename, O_RDONLY, 0600)) < 0)
        goto lockret;
      if ((i = read(fh, buffer, sizeof(buffer))) <= 0)
        goto lockret;

      buffer[sizeof(buffer) - 1] = 0;
      buffer[i - 1] = 0;

      if (!(c = strchr(buffer, '\n')))
        break;

      *(c++) = 0;

      if (!(c2 = strchr(c, '\n')))
        break;

      *(c2++) = 0;

      if (!(pid = atoi(buffer)))
        break;

      if (!(t = atoi(c)))
        break;

      if ((t + OPIE_LOCK_TIMEOUT) < time(0))
        break;

      if (kill(pid, 0))
        break;

      close(fh);
      fh = 0;
      sleep(1);
      if (waits++ > 3) {
        rval = 1; 
        goto lockret;
      };
    };
  };

  if (lstat(__opie_lockfilename, &statbuf[0]) < 0)
    goto lockret;
  if (fstat(fh, &statbuf[1]) < 0)
    goto lockret;
  if (!S_ISREG(statbuf[0].st_mode) || (statbuf[0].st_mode != statbuf[1].st_mode) || (statbuf[0].st_ino != statbuf[1].st_ino))
    goto lockret;

  if (snprintf(buffer, sizeof(buffer), "%d\n%d\n", getpid(), time(0)) >= sizeof(buffer))
    goto lockret;

  i = strlen(buffer) + 1;
  if (lseek(fh, 0, SEEK_SET)) { 
    close(fh);
    unlink(__opie_lockfilename);
    fh = 0;
    goto lockret;
  };
  if (write(fh, buffer, i) != i) {
    close(fh);
    unlink(__opie_lockfilename);
    fh = 0;
    goto lockret;
  };
  close(fh);
  if ((fh = open(__opie_lockfilename, O_RDWR, 0600)) < 0) {
    unlink(__opie_lockfilename);
    goto lockret;
  };
  if (read(fh, buffer2, i) != i) {
    close(fh);
    unlink(__opie_lockfilename);
    fh = 0;
    goto lockret;
  };
  close(fh);
  if (memcmp(buffer, buffer2, i)) {
    unlink(__opie_lockfilename);
    goto lockret;
  };
    
  __opie_lockrefcount++;
  rval = 0;
  if (do_atexit)
    atexit(opieunlockaeh);

lockret:
  if (fh >= 0)
    close(fh);
  if (!__opie_lockrefcount) {
    free (__opie_lockfilename);
    __opie_lockfilename = NULL;
  };
  return rval;
#else /* USER_LOCKING */
  __opie_lockrefcount++;
  return 0;
#endif /* USER_LOCKING */
}
