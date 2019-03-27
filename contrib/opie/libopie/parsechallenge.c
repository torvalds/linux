/* parsechallenge.c: The __opieparsechallenge() library function.

%%% copyright-cmetz-96
This software is Copyright 1996-2001 by Craig Metz, All Rights Reserved.
The Inner Net License Version 3 applies to this software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

        History:

	Modified by cmetz for OPIE 2.4. Use OPIE_SEQUENCE_MAX, check for
	        sequence number of zero.
	Modified by cmetz for OPIE 2.32. Check for extended response sets.
		Change prefix to double underscore.
	Created by cmetz for OPIE 2.3 using generator.c as a guide.
*/

#include "opie_cfg.h"
#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */
#include <ctype.h>
#include <stdlib.h>
#include "opie.h"

struct algorithm {
  char *name;
  int num;
};

static struct algorithm algorithms[] = {
  { "md5", 5 },
  { "md4", 4 },
  { "sha1", 3 },
  { NULL, 0 },
};

int __opieparsechallenge FUNCTION((buffer, algorithm, sequence, seed, exts), char *buffer AND int *algorithm AND int *sequence AND char **seed AND int *exts)
{
  char *c;

  if (!(c = strchr(buffer, ' ')))
    return 1;

  {
    struct algorithm *a;

    for (a = algorithms; a->name && strncmp(buffer, a->name, (int)(c - buffer)); a++);
    if (!a->name)
      return -1;

    *algorithm = a->num;
  }

  if (((*sequence = strtoul(++c, &c, 10)) > OPIE_SEQUENCE_MAX) || !*sequence)
    return -1;

  while(*c && isspace(*c)) c++;
  if (!*c)
    return -1;

  buffer = c;
  while(*c && !isspace(*c)) c++;

  {
    int i = (int)(c - buffer);

    if ((i > OPIE_SEED_MAX) || (i < OPIE_SEED_MIN))
      return -1;
  }

  *seed = buffer;
  *(c++) = 0;

  while(*c && !isspace(*c)) c++;
  if (*c && !strncmp(c, "ext", 3))
    *exts = 1;
  else
    *exts = 0;

  return 0;
}
