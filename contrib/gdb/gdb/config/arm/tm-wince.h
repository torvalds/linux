/* Definitions to target GDB for Windows CE target
   Copyright 2000 Free Software Foundation, Inc.

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

#ifndef TM_WINCE_H
#define TM_WINCE_H

#include "arm/tm-arm.h"

#undef SOFTWARE_SINGLE_STEP_P
#define SOFTWARE_SINGLE_STEP_P() 1

#undef SOFTWARE_SINGLE_STEP
#define SOFTWARE_SINGLE_STEP(sig, bp_p) wince_software_single_step (sig, bp_p)

void wince_software_single_step (unsigned int, int);

#endif /* TM_WINCE_H */
