/* passwd.c: The opiepasswd() library function.

%%% copyright-cmetz-96
This software is Copyright 1996-2001 by Craig Metz, All Rights Reserved.
The Inner Net License Version 3 applies to this software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

	History:

	Modified by cmetz for OPIE 2.32. Renamed mode to flags. Made flag
		values symbolic constants. Added a flag for insecure override
		support.
	Modified by cmetz for OPIE 2.31. Removed active attack protection
		support.
	Modified by cmetz for OPIE 2.3. Split most of the function off
		and turned this into a front-end for the new __opiewriterec().
		Added code to compute the key from the secret. Use the opie_
		prefix. Use new opieatob8() and opiebtoa8() return values.
	Created by cmetz for OPIE 2.22.
*/

#include <string.h>
#include "opie_cfg.h"
#include "opie.h"

int opiepasswd FUNCTION((old, flags, principal, n, seed, ks), struct opie *old AND int flags AND char *principal AND int n AND char *seed AND char *ks)
{
  int i;
  struct opie opie;

  if ((flags & OPIEPASSWD_CONSOLE) && opieinsecure())
#if INSECURE_OVERRIDE
    if (!(flags & OPIEPASSWD_FORCE))
#endif /* INSECURE_OVERRIDE */
    return -1;

  memset(&opie, 0, sizeof(struct opie));

  if (old) {
    opie.opie_flags = old->opie_flags;
    opie.opie_recstart = old->opie_recstart;
  }

  opie.opie_principal = principal;
  opie.opie_n = n;
  opie.opie_seed = seed;

  if (ks) {
    struct opie_otpkey key;
    
    if (flags & OPIEPASSWD_CONSOLE) {
      if (opiekeycrunch(MDX, &key, seed, ks))
	return -1;
      for (i = n; i; i--)
	opiehash(&key, MDX);
      if (!(opie.opie_val = opiebtoa8(opie.opie_buf, &key)))
	return -1;
    } else {
      if ((opieetob(&key, ks) != 1) && !opieatob8(&key, ks))
	  return 1;
      if (!(opie.opie_val = opiebtoa8(opie.opie_buf, &key)))
	return 1;
    }
  }

  if (opielock(principal))
    return -1;

  i = __opiewriterec(&opie);

  if (opieunlock())
    return -1;

  return i;
}
