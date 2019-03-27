/* version.c: The opieversion() library function.

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

	Modified by cmetz for OPIE 2.2. Use FUNCTION declaration et al.
        Created at NRL for OPIE 2.2 from opiesubr.c.
*/
#include <stdio.h>
#include <stdlib.h>
#include "opie_cfg.h"
#include "opie.h"

VOIDRET opieversion FUNCTION_NOARGS
{
  printf("\nOPIE %s (%s)\n\n", VERSION, DATE);
  exit(0);
}
