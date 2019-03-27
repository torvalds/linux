/* Work with executable files, for GDB, the GNU debugger.

   Copyright 2003 Free Software Foundation, Inc.

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

#ifndef EXEC_H
#define EXEC_H

#include "target.h"

struct section_table;
struct target_ops;
struct bfd;

extern struct target_ops exec_ops;

/* Builds a section table, given args BFD, SECTABLE_PTR, SECEND_PTR.
   Returns 0 if OK, 1 on error.  */

extern int build_section_table (struct bfd *, struct section_table **,
				struct section_table **);

#endif
