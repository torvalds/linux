/* Definitions to target GDB to ARM embedded systems.
   Copyright 1986, 1987, 1988, 1989, 1991, 1993, 1994, 1995, 1996, 1997,
   1998, 1999, 2000 Free Software Foundation, Inc.

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

#ifndef TM_ARMEMBED_H
#define TM_ARMEMBED_H

/* Include the common ARM definitions. */
#include "arm/tm-arm.h"

/* The remote stub should be able to single-step. */
#undef SOFTWARE_SINGLE_STEP_P
#define SOFTWARE_SINGLE_STEP_P() 0

/* The first 0x20 bytes are the trap vectors.  */
#undef LOWEST_PC
#define LOWEST_PC	0x20

/* Override defaults.  */

#undef THUMB_LE_BREAKPOINT
#define THUMB_LE_BREAKPOINT {0xbe,0xbe}       
#undef THUMB_BE_BREAKPOINT
#define THUMB_BE_BREAKPOINT {0xbe,0xbe}       

/* Functions for dealing with Thumb call thunks.  */
#define IN_SOLIB_CALL_TRAMPOLINE(pc, name)	arm_in_call_stub (pc, name)
#define SKIP_TRAMPOLINE_CODE(pc)		arm_skip_stub (pc)
extern int arm_in_call_stub (CORE_ADDR pc, char *name);
extern CORE_ADDR arm_skip_stub (CORE_ADDR pc);

#undef  IN_SIGTRAMP
#define IN_SIGTRAMP(pc, name) 0

#endif /* TM_ARMEMBED_H */
