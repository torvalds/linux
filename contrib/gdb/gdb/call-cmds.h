/* ***DEPRECATED***  The gdblib files must not be calling/using things in any
   of the possible command languages.  If necessary, a hook (that may be
   present or not) must be used and set to the appropriate routine by any
   command language that cares about it.  If you are having to include this
   file you are possibly doing things the old way.  This file will disapear.
   2000-12-01 fnasser@redhat.com    */

/* Prototypes for GDB commands that are called internally by other functions.
   Copyright 1992, 2000, 2001 Free Software Foundation, Inc.

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

#ifndef CALL_CMDS_H
#define CALL_CMDS_H

extern void initialize_all_files (void);

extern void core_file_command (char *, int);

extern void break_command (char *, int);

#endif
