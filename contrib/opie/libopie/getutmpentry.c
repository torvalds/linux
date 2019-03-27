/* getutmpentry.c: The __opiegetutmpentry() library function.

%%% copyright-cmetz-96
This software is Copyright 1996-2001 by Craig Metz, All Rights Reserved.
The Inner Net License Version 3 applies to this software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

        History:

	Modified by cmetz for OPIE 2.31. Cache result.
        Created by cmetz for OPIE 2.3 (re-write).
*/

#include "opie_cfg.h"
#include <stdio.h>
#include <sys/types.h>

#if DOUTMPX
#include <utmpx.h>
#define setutent setutxent
#define getutline(x) getutxline(x)
#define utmp utmpx
#else
#include <utmp.h>
#endif /* DOUTMPX */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#if DEBUG
#include <syslog.h>
#endif /* DEBUG */
#include "opie.h"

#if !HAVE_GETUTLINE && !DOUTMPX
struct utmp *getutline __P((struct utmp *));
#endif /* HAVE_GETUTLINE && !DOUTMPX */

static struct utmp u;

int __opiegetutmpentry FUNCTION((line, utmp), char *line AND struct utmp *utmp)
{
  struct utmp *pu;

  if (u.ut_line[0]) {
    pu = &u;
    goto gotit;
  };

  memset(&u, 0, sizeof(u));

  if (!strncmp(line, "/dev/", 5)) {
    strncpy(u.ut_line, line + 5, sizeof(u.ut_line));
    setutent();
    if ((pu = getutline(&u)))
      goto gotit;

#ifdef hpux
    strcpy(u.ut_line, "pty/");
    strncpy(u.ut_line + 4, line + 5, sizeof(u.ut_line) - 4);
    setutent();
    if ((pu = getutline(&u)))
      goto gotit;
#endif /* hpux */
  }

  strncpy(u.ut_line, line, sizeof(u.ut_line));
  setutent();
  if ((pu = getutline(&u)))
    goto gotit;

#if DEBUG
  syslog(LOG_DEBUG, "__opiegetutmpentry: failed to find entry for line %s", line);
#endif /* DEBUG */
  return -1;

gotit:
#if DEBUG
  syslog(LOG_DEBUG, "__opiegetutmpentry: succeeded with line %s", pu->ut_line);
#endif /* DEBUG */
  memcpy(utmp, pu, sizeof(struct utmp));
  return 0;
}
