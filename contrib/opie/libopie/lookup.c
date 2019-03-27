/* lookup.c: The opielookup() library function.

%%% copyright-cmetz-96
This software is Copyright 1996-2001 by Craig Metz, All Rights Reserved.
The Inner Net License Version 3 applies to this software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

        History:

        Created by cmetz for OPIE 2.3 (re-write).
*/

#include "opie_cfg.h"
#include <stdio.h>
#include <string.h>
#include "opie.h"

int opielookup FUNCTION((opie, principal), struct opie *opie AND char *principal)
{
  int i;

  memset(opie, 0, sizeof(struct opie));
  opie->opie_principal = principal;

  if (i = __opiereadrec(opie))
    return i;

  return (opie->opie_flags & __OPIE_FLAGS_RW) ? 0 : 2;
}

