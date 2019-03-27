// OBSOLETE /* Definitions to make GDB run on a mips box under 4.3bsd.
// OBSOLETE    Copyright 1986, 1987, 1989, 1993, 1994, 1995, 1996, 1998
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
// OBSOLETE #ifdef ultrix
// OBSOLETE /* Needed for DECstation core files.  */
// OBSOLETE #include <machine/param.h>
// OBSOLETE #define KERNEL_U_ADDR UADDR
// OBSOLETE 
// OBSOLETE /* Native Ultrix cc has broken long long support.  */
// OBSOLETE #ifndef __GNUC__
// OBSOLETE #undef CC_HAS_LONG_LONG
// OBSOLETE #endif
// OBSOLETE #endif
// OBSOLETE 
// OBSOLETE #if ! defined (__GNUC__) && ! defined (offsetof)
// OBSOLETE #define offsetof(TYPE, MEMBER) ((unsigned long) &((TYPE *)0)->MEMBER)
// OBSOLETE #endif
// OBSOLETE 
// OBSOLETE /* Only used for core files on DECstations.
// OBSOLETE    First four registers at u.u_ar0 are saved arguments, and
// OBSOLETE    there is no r0 saved.   Float registers are saved
// OBSOLETE    in u_pcb.pcb_fpregs, not relative to u.u_ar0.  */
// OBSOLETE 
// OBSOLETE #define REGISTER_U_ADDR(addr, blockend, regno) 		\
// OBSOLETE 	{ \
// OBSOLETE 	  if (regno < FP0_REGNUM) \
// OBSOLETE 	    addr = blockend + sizeof(int) * (4 + regno - 1); \
// OBSOLETE 	  else \
// OBSOLETE 	    addr = offsetof (struct user, u_pcb.pcb_fpregs[0]) + \
// OBSOLETE 		   sizeof (int) * (regno - FP0_REGNUM); \
// OBSOLETE 	}
// OBSOLETE 
// OBSOLETE /* Kernel is a bit tenacious about sharing text segments, disallowing bpts.  */
// OBSOLETE #define	ONE_PROCESS_WRITETEXT
// OBSOLETE 
// OBSOLETE /* HAVE_SGTTY also works, last we tried.
// OBSOLETE 
// OBSOLETE    But we have termios, at least as of Ultrix 4.2A, so use it.  */
// OBSOLETE #define HAVE_TERMIOS
