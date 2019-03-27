/* open.c: The __opieopen() library function.

%%% copyright-cmetz-96
This software is Copyright 1996-2001 by Craig Metz, All Rights Reserved.
The Inner Net License Version 3 applies to this software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

	History:

	Modified by cmetz for OPIE 2.4. More portable way to get the mode
		string for fopen.
	Created by cmetz for OPIE 2.3.
*/
#include "opie_cfg.h"

#include <stdio.h>
#include <sys/types.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <sys/stat.h>
#include <errno.h>

#include "opie.h"

#if !HAVE_LSTAT
#define lstat(x, y) stat(x, y)
#endif /* !HAVE_LSTAT */

FILE *__opieopen FUNCTION((file, rw, mode), char *file AND int rw AND int mode)
{
  FILE *f;
  struct stat st;

  if (lstat(file, &st)) {
    if (errno != ENOENT)
      return NULL;

    if (!(f = fopen(file, "w")))
      return NULL;

    fclose(f);

    if (chmod(file, mode))
      return NULL;

    if (lstat(file, &st))
      return NULL;
  }

  if (!S_ISREG(st.st_mode))
    return NULL;

  {
    char *fmode;

    switch(rw) {
      case 0:
	fmode = "r";
	break;
      case 1:
	fmode = "r+";
	break;
      case 2:
	fmode = "a";
	break;
      default:
	return NULL;
    };
    
    if (!(f = fopen(file, fmode)))
      return NULL;
  }

  return f;
}
