/* Macro definitions for Power PC running embedded ABI.
   Copyright 1995, 1996, 1997, 1998, 1999, 2000
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

#ifndef TM_PPC_EABI_H
#define TM_PPC_EABI_H

/* Use generic RS6000 definitions. */
#include "rs6000/tm-rs6000.h"

#undef PROCESS_LINENUMBER_HOOK

#undef TEXT_SEGMENT_BASE
#define TEXT_SEGMENT_BASE 1

/* The value of symbols of type N_SO and N_FUN maybe null when 
   it shouldn't be. */
#define SOFUN_ADDRESS_MAYBE_MISSING

/* Use generic shared library machinery.  */
#include "solib.h"

#endif /* TM_PPC_EABI_H */
