/* Native-dependent definitions for LynxOS.

   Copyright 1993, 1994, 1995, 1996, 1999, 2000, 2003 Free Software
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

#ifndef NM_LYNX_H
#define NM_LYNX_H

struct target_waitstatus;

#include <sys/conf.h>
#include <sys/kernel.h>
/* sys/kernel.h should define this, but doesn't always, sigh. */
#ifndef __LYNXOS
#define __LYNXOS
#endif
#include <sys/mem.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/itimer.h>
#include <sys/file.h>
#include <sys/proc.h>
#include "gdbthread.h"

/* This is the amount to subtract from u.u_ar0 to get the offset in
   the core file of the register values.  */

#define KERNEL_U_ADDR USRSTACK

/* As of LynxOS 2.2.2 (beta 8/15/94), this is int.  Previous versions seem to
   have had no prototype, so I'm not sure why GDB used to define this to
   char *.  */
#define PTRACE_ARG3_TYPE int

/* Override copies of {fetch,store}_inferior_registers in infptrace.c.  */

#define FETCH_INFERIOR_REGISTERS

/* Thread ID of stopped thread.  */

#define WIFTID(x) (((union wait *)&x)->w_tid)

/* Override child_wait in inftarg.c */

#define CHILD_WAIT

/* Override child_resume in infptrace.c */

#define CHILD_RESUME

/* Override child_thread_alive in intarg.c */

#define CHILD_THREAD_ALIVE

#include "target.h"

extern ptid_t child_wait (ptid_t ptid,
                                struct target_waitstatus *status);

/* Lynx needs a special definition of this so that we can
   print out the pid and thread number seperately.  */


/* override child_pid_to_str in inftarg.c */
#define CHILD_PID_TO_STR
extern char *lynx_pid_to_str (ptid_t ptid);

#endif /* NM_LYNX_H */
