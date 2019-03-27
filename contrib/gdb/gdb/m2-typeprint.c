/* Support for printing Modula 2 types for GDB, the GNU debugger.
   Copyright 1986, 1988, 1989, 1991, 1992, 1995, 2000
   Free Software Foundation, Inc.

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
#include "bfd.h"		/* Binary File Description */
#include "symtab.h"
#include "gdbtypes.h"
#include "expression.h"
#include "value.h"
#include "gdbcore.h"
#include "target.h"
#include "m2-lang.h"
#include <errno.h>

void
m2_print_type (struct type *type, char *varstring, struct ui_file *stream,
	       int show, int level)
{
  extern void c_print_type (struct type *, char *, struct ui_file *, int,
			    int);

  c_print_type (type, varstring, stream, show, level);	/* FIXME */
}
