/* output-file.c -  Deal with the output file
   Copyright 1987, 1990, 1991, 1992, 1993, 1994, 1996, 1998, 1999, 2001,
   2003, 2004, 2005, 2006 Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "as.h"
#include "output-file.h"

#ifndef TARGET_MACH
#define TARGET_MACH 0
#endif

bfd *stdoutput;

void
output_file_create (char *name)
{
  if (name[0] == '-' && name[1] == '\0')
    as_fatal (_("can't open a bfd on stdout %s"), name);

  else if (!(stdoutput = bfd_openw (name, TARGET_FORMAT)))
    {
      bfd_error_type err = bfd_get_error ();

      if (err == bfd_error_invalid_target)
	as_fatal (_("selected target format '%s' unknown"), TARGET_FORMAT);
      else
	as_fatal (_("can't create %s: %s"), name, bfd_errmsg (err));
    }

  bfd_set_format (stdoutput, bfd_object);
  bfd_set_arch_mach (stdoutput, TARGET_ARCH, TARGET_MACH);
  if (flag_traditional_format)
    stdoutput->flags |= BFD_TRADITIONAL_FORMAT;
}

void
output_file_close (char *filename)
{
  bfd_boolean res;

  if (stdoutput == NULL)
    return;
    
  /* Close the bfd.  */
  res = bfd_close (stdoutput);

  /* Prevent an infinite loop - if the close failed we will call as_fatal
     which will call xexit() which may call this function again...  */
  stdoutput = NULL;

  if (! res)
    as_fatal (_("can't close %s: %s"), filename,
	      bfd_errmsg (bfd_get_error ()));
}
