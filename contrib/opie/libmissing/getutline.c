/* getutline.c: A replacement for the getutline() function

%%% copyright-cmetz-96
This software is Copyright 1996-2001 by Craig Metz, All Rights Reserved.
The Inner Net License Version 3 applies to this software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

        History:

	Modified by cmetz for OPIE 2.32. Fixed check for fread() return
		value.
	Modified by cmetz for OPIE 2.31. If the OS won't tell us where
		_PATH_UTMP is, play the SVID game, then use
		Autoconf-discovered values.
	Created by cmetz for OPIE 2.3.
*/

#include "opie_cfg.h"
#include <stdio.h>
#include <utmp.h>
#include "opie.h"

static struct utmp u;

#ifndef _PATH_UTMP
#ifdef UTMP_FILE
#define _PATH_UTMP UTMP_FILE
#else /* UTMP_FILE */
#define _PATH_UTMP PATH_UTMP_AC
#endif /* UTMP_FILE */
#endif /* _PATH_UTMP */

struct utmp *getutline FUNCTION((utmp), struct utmp *utmp)
{
  FILE *f;
  int i;

  if (!(f = __opieopen(_PATH_UTMP, 0, 0644)))
    return 0;

#if HAVE_TTYSLOT
  if (i = ttyslot()) {
    if (fseek(f, i * sizeof(struct utmp), SEEK_SET) < 0)
      goto ret;
    if (fread(&u, sizeof(struct utmp), 1, f) != 1)
      goto ret;
    fclose(f);
    return &u;
  }
#endif /* HAVE_TTYSLOT */

  while(fread(&u, sizeof(struct utmp), 1, f) == 1) {
    if (!strncmp(utmp->ut_line, u.ut_line, sizeof(u.ut_line) - 1)) {
      fclose(f);
      return &u;
    }
  }

ret:
  fclose(f);
  return NULL;
}
