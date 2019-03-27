/* Native support for SCO OpenServer 5.
   Copyright 1996, 1998, 2002 Free Software Foundation, Inc.
   Re-written by J. Kean Johnston <jkj@sco.com>.
   Originally written by Robert Lipe <robertl@dgii.com>, based on 
   work by Ian Lance Taylor <ian@cygnus.com> and 
   Martin Walker <maw@netcom.com>.

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

#ifndef NM_I386SCO5_H
#define NM_I386SCO5_H

/* Basically, its a lot like the older versions ... */
#include "i386/nm-i386sco.h"

/* ... but it can do a lot of SVR4 type stuff too.  */
#define SVR4_SHARED_LIBS
#include "solib.h"		/* Pick up shared library support.  */

/* SCO is unlike other SVR4 systems in that it has SVR4 style shared
   libs, with a slight twist.  We expect 3 traps (2 for the exec and
   one for the dynamic loader).  After the third trap we insert the
   shared library breakpoints, then wait for the 4th trap.  */

#undef START_INFERIOR_TRAPS_EXPECTED
#define START_INFERIOR_TRAPS_EXPECTED 3

/* SCO does not provide <sys/ptrace.h>.  However, infptrace.c does not
   have defaults for these values.  */

#define PTRACE_ATTACH 10
#define PTRACE_DETACH 11

/* Return the size of the user struct.  */

#define KERNEL_U_SIZE kernel_u_size ()
extern int kernel_u_size (void);

/* We can attach and detach.  */
#define ATTACH_DETACH

/* Hardware-assisted breakpoints and watchpoints.  */

/* We can also do hardware watchpoints.  */
#define TARGET_HAS_HARDWARE_WATCHPOINTS
#define TARGET_CAN_USE_HARDWARE_WATCHPOINT(type, cnt, ot) 1

/* After a watchpoint trap, the PC points to the instruction which
   caused the trap.  But we can continue over it without disabling the
   trap.  */
#define HAVE_CONTINUABLE_WATCHPOINT 1
#define HAVE_STEPPABLE_WATCHPOINT

#define STOPPED_BY_WATCHPOINT(W)  \
  i386_stopped_by_watchpoint (PIDGET (inferior_ptid))

#define target_insert_watchpoint(addr, len, type)  \
  i386_insert_watchpoint (PIDGET (inferior_ptid), addr, len, type)

#define target_remove_watchpoint(addr, len, type)  \
  i386_remove_watchpoint (PIDGET (inferior_ptid), addr, len)

#endif /* nm-i386sco5.h */
