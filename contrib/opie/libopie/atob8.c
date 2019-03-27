/* atob8.c: The opieatob8() library function.

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

	Modified by cmetz for OPIE 2.4. Use struct opie_otpkey for binary arg.
	Modified by cmetz for OPIE 2.3. Return the output variable.
	    Don't check parameters.
	Modified by cmetz for OPIE 2.2. Use FUNCTION declaration et al.
            Inlined and obseleted opieskipspace(). Inlined and obseleted
            opiehtoi().
        Created at NRL for OPIE 2.2 from opiesubr2.c
*/
#include "opie_cfg.h"
#include <stdio.h>
#include "opie.h"

/* Convert 8-byte hex-ascii string to binary array
 */
char *opieatob8 FUNCTION((out, in), struct opie_otpkey *outkey AND char *in)
{
  register int i;
  register int val;
  unsigned char *out = (unsigned char *)outkey;

  for (i = 0; i < 8; i++) {
    while (*in == ' ' || *in == '\t')
      in++;
    if (!*in)
      return NULL;

    if ((*in >= '0') && (*in <= '9'))
      val = *(in++) - '0';
    else
      if ((*in >= 'a') && (*in <= 'f'))
        val = *(in++) - 'a' + 10;
      else
        if ((*in >= 'A') && (*in <= 'F'))
          val = *(in++) - 'A' + 10;
        else
	  return NULL;

    *out = val << 4;

    while (*in == ' ' || *in == '\t')
      in++;
    if (!*in)
      return NULL;

    if ((*in >= '0') && (*in <= '9'))
      val = *(in++) - '0';
    else
      if ((*in >= 'a') && (*in <= 'f'))
        val = *(in++) - 'a' + 10;
      else
        if ((*in >= 'A') && (*in <= 'F'))
          val = *(in++) - 'A' + 10;
        else
          return NULL;

    *out++ |= val;
  }

  return out;
}
