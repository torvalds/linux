/* Macro definitions for GDB on an Intel i386 running Solaris 2.
   Copyright 1998, 1999, 2000 Free Software Foundation, Inc.

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

#ifndef TM_I386SOL2_H
#define TM_I386SOL2_H 1

#include "i386/tm-i386.h"

/* The SunPRO compiler puts out 0 instead of the address in N_SO symbols,
   and for SunPRO 3.0, N_FUN symbols too.  */
#define SOFUN_ADDRESS_MAYBE_MISSING

extern char *sunpro_static_transform_name (char *);
#define STATIC_TRANSFORM_NAME(x) sunpro_static_transform_name (x)
#define IS_STATIC_TRANSFORM_NAME(name) ((name)[0] == '.')

#endif /* ifndef TM_I386SOL2_H */
