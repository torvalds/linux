/* Language independent support for printing types for GDB, the GNU debugger.
   Copyright 1986, 1988, 1989, 1991-1993, 1999, 2000
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

#ifndef TYPEPRINT_H
#define TYPEPRINT_H

struct ui_file;

void print_type_scalar (struct type * type, LONGEST, struct ui_file *);

void c_type_print_varspec_suffix (struct type *, struct ui_file *, int,
				  int, int);
#endif
