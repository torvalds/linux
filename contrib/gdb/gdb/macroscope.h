/* Interface to functions for deciding which macros are currently in scope.
   Copyright 2002 Free Software Foundation, Inc.
   Contributed by Red Hat, Inc.

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

#ifndef MACROSCOPE_H
#define MACROSCOPE_H

#include "macrotab.h"
#include "symtab.h"


/* All the information we need to decide which macro definitions are
   in scope: a source file (either a main source file or an
   #inclusion), and a line number in that file.  */
struct macro_scope {
  struct macro_source_file *file;
  int line;
};


/* Return a `struct macro_scope' object corresponding to the symtab
   and line given in SAL.  If we have no macro information for that
   location, or if SAL's pc is zero, return zero.  */
struct macro_scope *sal_macro_scope (struct symtab_and_line sal);


/* Return a `struct macro_scope' object describing the scope the `macro
   expand' and `macro expand-once' commands should use for looking up
   macros.  If we have a selected frame, this is the source location of
   its PC; otherwise, this is the last listing position.

   If we have no macro information for the current location, return zero.

   The object returned is allocated using xmalloc; the caller is
   responsible for freeing it.  */
struct macro_scope *default_macro_scope (void);


/* Look up the definition of the macro named NAME in scope at the source
   location given by BATON, which must be a pointer to a `struct
   macro_scope' structure.  This function is suitable for use as
   a macro_lookup_ftype function.  */
struct macro_definition *standard_macro_lookup (const char *name, void *baton);


#endif /* MACROSCOPE_H */
