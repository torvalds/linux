/* Host support for i386.
   Copyright 1986, 1987, 1989, 1992 Free Software Foundation, Inc.
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

/* I'm running gdb 3.4 under 386/ix 2.0.2, which is a derivative of AT&T's
   Sys V/386 3.2.

   On some machines, gdb crashes when it's starting up while calling the
   vendor's termio tgetent() routine.  It always works when run under
   itself (actually, under 3.2, it's not an infinitely recursive bug.)
   After some poking around, it appears that depending on the environment
   size, or whether you're running YP, or the phase of the moon or something,
   the stack is not always long-aligned when main() is called, and tgetent()
   takes strong offense at that.  On some machines this bug never appears, but
   on those where it does, it occurs quite reliably.  */
#define ALIGN_STACK_ON_STARTUP

/* define USG if you are using sys5 /usr/include's */
#define USG

#define HAVE_TERMIO

/* This is the amount to subtract from u.u_ar0
   to get the offset in the core file of the register values.  */

#define KERNEL_U_ADDR 0xe0000000
