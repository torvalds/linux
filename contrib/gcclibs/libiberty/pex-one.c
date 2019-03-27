/* Execute a program and wait for a result.
   Copyright (C) 2005 Free Software Foundation, Inc.

This file is part of the libiberty library.
Libiberty is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

Libiberty is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with libiberty; see the file COPYING.LIB.  If not,
write to the Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "libiberty.h"

const char *
pex_one (int flags, const char *executable, char * const *argv,
	 const char *pname, const char *outname, const char *errname,
	 int *status, int *err)
{
  struct pex_obj *obj;
  const char *errmsg;

  obj = pex_init (0, pname, NULL);
  errmsg = pex_run (obj, flags, executable, argv, outname, errname, err);
  if (errmsg == NULL)
    {
      if (!pex_get_status (obj, 1, status))
	{
	  *err = 0;
	  errmsg = "pex_get_status failed";
	}
    }
  pex_free (obj);
  return errmsg;  
}
