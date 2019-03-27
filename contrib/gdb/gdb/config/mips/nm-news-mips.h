// OBSOLETE /* Definitions to make GDB run on a mips box under 4.3bsd.
// OBSOLETE    Copyright 1986, 1987, 1989, 1993, 1996 Free Software Foundation, Inc.
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
// OBSOLETE #ifndef NM_NEWS_MIPS_H
// OBSOLETE #define NM_NEWS_MIPS_H 1
// OBSOLETE 
// OBSOLETE /* Needed for RISC NEWS core files.  */
// OBSOLETE #include <machine/machparam.h>
// OBSOLETE #include <sys/types.h>
// OBSOLETE #define KERNEL_U_ADDR UADDR
// OBSOLETE 
// OBSOLETE #define REGISTER_U_ADDR(addr, blockend, regno) 		\
// OBSOLETE 	if (regno < 38) addr = (NBPG*UPAGES) + (regno - 38)*sizeof(int);\
// OBSOLETE 	else addr = 0;		/* ..somewhere in the pcb */
// OBSOLETE 
// OBSOLETE /* Kernel is a bit tenacious about sharing text segments, disallowing bpts.  */
// OBSOLETE #define	ONE_PROCESS_WRITETEXT
// OBSOLETE 
// OBSOLETE #include "mips/nm-mips.h"
// OBSOLETE 
// OBSOLETE /* Apparently not in <sys/types.h> */
// OBSOLETE typedef int pid_t;
// OBSOLETE 
// OBSOLETE #endif /* NM_NEWS_MIPS_H */
