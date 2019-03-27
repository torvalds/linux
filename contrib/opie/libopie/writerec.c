/* writerec.c: The __opiewriterec() library function.

%%% copyright-cmetz-96
This software is Copyright 1996-2001 by Craig Metz, All Rights Reserved.
The Inner Net License Version 3 applies to this software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

	History:

	Modified by cmetz for OPIE 2.4. Check that seed and sequence number are
		valid.
	Modified by cmetz for OPIE 2.31. Removed active attack protection
		support. Fixed passwd bug.
	Created by cmetz for OPIE 2.3 from passwd.c.

$FreeBSD$
*/
#include "opie_cfg.h"

#include <stdio.h>
#if TM_IN_SYS_TIME
#include <sys/time.h>
#else /* TM_IN_SYS_TIME */
#include <time.h>
#endif /* TM_IN_SYS_TIME */
#include <sys/types.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */
#include <ctype.h>
#include "opie.h"

char *__opienone = "****************";

int __opiewriterec FUNCTION((opie), struct opie *opie)
{
  char buf[17], buf2[64];
  time_t now;
  FILE *f, *f2 = NULL;
  int i = 0;
  char *c;

  time(&now);
  if (strftime(buf2, sizeof(buf2), " %b %d,%Y %T", localtime(&now)) < 1)
    return -1;

  if (!(opie->opie_flags & __OPIE_FLAGS_READ)) {
    struct opie opie2;
    i = opielookup(&opie2, opie->opie_principal);
    opie->opie_flags = opie2.opie_flags;
    opie->opie_recstart = opie2.opie_recstart;
  }

  for (c = opie->opie_seed; *c; c++)
    if (!isalnum(*c))
      return -1;

  if ((opie->opie_n < 0) || (opie->opie_n > 9999))
      return -1;

  switch(i) {
  case 0:
    if (!(f = __opieopen(KEY_FILE, 1, 0600)))
      return -1;
    if (fseek(f, opie->opie_recstart, SEEK_SET))
      return -1;
    break;
  case 1:
    if (!(f = __opieopen(KEY_FILE, 2, 0600)))
      return -1;
    break;
  default:
    return -1;
  }

  if (fprintf(f, "%s %04d %-16s %s %-21s\n", opie->opie_principal, opie->opie_n, opie->opie_seed, opie->opie_val ? opie->opie_val : __opienone, buf2) < 1)
    return -1;

  fclose(f);

  return 0;
}
