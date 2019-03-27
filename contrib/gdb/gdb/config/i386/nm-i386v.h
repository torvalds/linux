/* Native support for i386 running System V (pre-SVR4).

   Copyright 1986, 1987, 1989, 1992, 1993, 1998, 2000, 2002
   Free Software Foundation, Inc.
   Changes for 80386 by Pace Willisson (pace@prep.ai.mit.edu), July 1988.

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

#ifndef NM_I386V_H
#define NM_I386V_H

/* Support for the user struct.  */

/* This is the amount to subtract from u.u_ar0
   to get the offset in the core file of the register values.  */

#define REGISTER_U_ADDR(addr, blockend, regnum) \
  (addr) = register_u_addr ((blockend), (regnum))
extern CORE_ADDR register_u_addr (CORE_ADDR blockend, int regnum);


/* Number of traps that happen between exec'ing the shell to run an
   inferior, and when we finally get to the inferior code.  This is 2
   on most implementations.  Override here to 4.  */

#undef START_INFERIOR_TRAPS_EXPECTED
#define START_INFERIOR_TRAPS_EXPECTED 4

#endif /* nm-i386v.h */
