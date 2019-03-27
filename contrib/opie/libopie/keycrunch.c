/* keycrunch.c: The opiekeycrunch() library function.

%%% copyright-cmetz-96
This software is Copyright 1996-2001 by Craig Metz, All Rights Reserved.
The Inner Net License Version 3 applies to this software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

	History:

	Modified by cmetz for OPIE 2.4. Use struct opie_otpkey for arg.
	Created by cmetz for OPIE 2.3 using the old keycrunch.c as a guide.
*/

#include "opie_cfg.h"

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */
#include <ctype.h>

#include "opie.h"

int opiekeycrunch FUNCTION((algorithm, result, seed, secret), int algorithm AND
struct opie_otpkey *result AND char *seed AND char *secret)
{
  int i, rval = -1;
  char *c;

  if (!result || !seed || !secret)
    return 1;

  i = strlen(seed) + strlen(secret);
  if (!(c = malloc(i + 1)))
    return -1;

  {
    char *c2 = c;

    if (algorithm & 0x10)
      while(*c2 = *(secret++)) c2++;

    while(*seed)
      if (isspace(*(c2++) = tolower(*(seed++))))
	goto kcret;

    if (!(algorithm & 0x10))
      strcpy(c2, secret);
  }

  opiehashlen(algorithm & 0x0f, c, result, i);
  rval = 0;

kcret:
  {
    char *c2 = c;
    while(*c2)
      *(c2++) = 0;
  }

  free(c);
  return rval;
}
