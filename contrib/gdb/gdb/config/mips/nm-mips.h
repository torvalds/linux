// OBSOLETE /* Native definitions for GDB on DECstations, Sony News. and MIPS Riscos systems
// OBSOLETE    Copyright 1986, 1987, 1989, 1992, 1995, 1996, 2000
// OBSOLETE    Free Software Foundation, Inc.
// OBSOLETE    Contributed by Per Bothner(bothner@cs.wisc.edu) at U.Wisconsin
// OBSOLETE    and by Alessandro Forin(af@cs.cmu.edu) at CMU
// OBSOLETE 
// OBSOLETE    This file is part of GDB.
// OBSOLETE 
// OBSOLETE    This program is free software; you can redistribute it and/or modify
// OBSOLETE    it under the terms of the GNU General Public License as published by
// OBSOLETE    the Free Software Foundation; either version 2 of the License, or
// OBSOLETE    (at your option) any later version.
// OBSOLETE 
// OBSOLETE    This program is distributed in the hope that it will be useful,
// OBSOLETE    but WITHOUT ANY WARRANTY; without even the implied warranty of
// OBSOLETE    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// OBSOLETE    GNU General Public License for more details.
// OBSOLETE 
// OBSOLETE    You should have received a copy of the GNU General Public License
// OBSOLETE    along with this program; if not, write to the Free Software
// OBSOLETE    Foundation, Inc., 59 Temple Place - Suite 330,
// OBSOLETE    Boston, MA 02111-1307, USA.  */
// OBSOLETE 
// OBSOLETE /* Override copies of {fetch,store}_inferior_registers in infptrace.c.  */
// OBSOLETE #define FETCH_INFERIOR_REGISTERS
// OBSOLETE 
// OBSOLETE /* Figure out where the longjmp will land.  We expect that we have just entered
// OBSOLETE    longjmp and haven't yet setup the stack frame, so the args are still in the
// OBSOLETE    argument regs.  a0 (CALL_ARG0) points at the jmp_buf structure from which we
// OBSOLETE    extract the pc (JB_PC) that we will land at.  The pc is copied into ADDR.
// OBSOLETE    This routine returns true on success */
// OBSOLETE 
// OBSOLETE #define GET_LONGJMP_TARGET(ADDR) get_longjmp_target(ADDR)
// OBSOLETE extern int get_longjmp_target (CORE_ADDR *);
