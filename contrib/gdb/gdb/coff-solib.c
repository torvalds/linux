/* Handle COFF SVR3 shared libraries for GDB, the GNU Debugger.
   Copyright 1993, 1994, 1998, 1999, 2000 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */


#include "defs.h"

#include "frame.h"
#include "bfd.h"
#include "gdbcore.h"
#include "symtab.h"
#include "symfile.h"
#include "objfiles.h"

/*

   GLOBAL FUNCTION

   coff_solib_add -- add a shared library files to the symtab list.  We
   examine the `.lib' section of the exec file and determine the names of
   the shared libraries.

   This function is responsible for discovering those names and
   addresses, and saving sufficient information about them to allow
   their symbols to be read at a later time.

   SYNOPSIS

   void coff_solib_add (char *arg_string, int from_tty,
   struct target_ops *target, int readsyms)

   DESCRIPTION

 */

void
coff_solib_add (char *arg_string, int from_tty, struct target_ops *target, int readsyms)
{
  asection *libsect;

  if (!readsyms)
    return;

  libsect = bfd_get_section_by_name (exec_bfd, ".lib");

  if (libsect)
    {
      int libsize;
      unsigned char *lib;
      struct libent
	{
	  bfd_byte len[4];
	  bfd_byte nameoffset[4];
	};

      libsize = bfd_section_size (exec_bfd, libsect);

      lib = (unsigned char *) alloca (libsize);

      bfd_get_section_contents (exec_bfd, libsect, lib, 0, libsize);

      while (libsize > 0)
	{
	  struct libent *ent;
	  struct objfile *objfile;
	  int len, nameoffset;
	  char *filename;

	  ent = (struct libent *) lib;

	  len = bfd_get_32 (exec_bfd, ent->len);

	  nameoffset = bfd_get_32 (exec_bfd, ent->nameoffset);

	  if (len <= 0)
	    break;

	  filename = (char *) ent + nameoffset * 4;

	  objfile = symbol_file_add (filename, from_tty,
				     NULL,	/* no offsets */
				     0,		/* not mainline */
				     OBJF_SHARED);	/* flags */

	  libsize -= len * 4;
	  lib += len * 4;
	}

      /* Getting new symbols may change our opinion about what is
         frameless.  */
      reinit_frame_cache ();
    }
}

/*

   GLOBAL FUNCTION

   coff_solib_create_inferior_hook -- shared library startup support

   SYNOPSIS

   void coff_solib_create_inferior_hook()

   DESCRIPTION

   When gdb starts up the inferior, the kernel maps in the shared
   libraries.  We get here with the target stopped at it's first
   instruction, and the libraries already mapped.  At this      point, this
   function gets called via expansion of the macro
   SOLIB_CREATE_INFERIOR_HOOK.
 */

void
coff_solib_create_inferior_hook (void)
{
  coff_solib_add ((char *) 0, 0, (struct target_ops *) 0, auto_solib_add);
}
