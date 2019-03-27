/* Native definitions for alpha running OSF/1.

   Copyright 1993, 1994, 1995, 1998, 2000, 2004 Free Software
   Foundation, Inc.

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

/* Number of traps that happen between exec'ing the shell
   to run an inferior, and when we finally get to
   the inferior code.  This is 2 on most implementations.  */
#define START_INFERIOR_TRAPS_EXPECTED 3

/* ptrace register ``addresses'' are absolute.  */

#define U_REGS_OFFSET 0

/* FIXME: Shouldn't the default definition in inferior.h be int* ? */

#define PTRACE_ARG3_TYPE int*

/* ptrace transfers longs, the ptrace man page is lying.  */

#define PTRACE_XFER_TYPE long

/* The alpha does not step over a breakpoint, the manpage is lying again.  */

#define CANNOT_STEP_BREAKPOINT 1

/* Support for shared libraries.  */

#include "solib.h"

/* Given a pointer to either a gregset_t or fpregset_t, return a
   pointer to the first register.  */
#define ALPHA_REGSET_BASE(regsetp)     ((regsetp)->regs)
