/* Support for printing Modula 2 values for GDB, the GNU debugger.
   Copyright 1986, 1988, 1989, 1991, 1992, 1996, 1998, 2000
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
#include "symtab.h"
#include "gdbtypes.h"
#include "m2-lang.h"

/* FIXME:  For now, just explicitly declare c_val_print and use it instead */

int
m2_val_print (struct type *type, char *valaddr, int embedded_offset,
	      CORE_ADDR address, struct ui_file *stream, int format,
	      int deref_ref, int recurse, enum val_prettyprint pretty)
{
  extern int c_val_print (struct type *, char *, int, CORE_ADDR,
			  struct ui_file *, int, int, int,
			  enum val_prettyprint);
  return (c_val_print (type, valaddr, 0, address, stream, format, deref_ref,
		       recurse, pretty));
}
