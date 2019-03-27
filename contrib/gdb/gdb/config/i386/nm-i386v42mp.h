/* Native support for i386 running SVR4.
   Copyright 1986, 1987, 1989, 1992, 1996, 1997, 1998
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

#ifndef NM_I386V42MP_H
#define NM_I386V42MP_H

#include "config/nm-sysv4.h"

/* define to select for other sysv4.2mp weirdness (see procfs.c) */

#define UNIXWARE

#if 0
/* The following macros extract process and lwp/thread ids from a
   composite id.

   For consistency with UnixWare core files, allocate bits 0-15 for
   process ids and bits 16 and up for lwp ids.  Reserve bit 31 for
   negative return values to indicate exceptions, and use bit 30 as a
   flag to indicate a user-mode thread, leaving 14 bits for lwp
   ids. */

/* Number of bits in composite id allocated to process number. */
#define PIDBITS 16

/* Return the process id stored in composite PID. */
#define PIDGET(PID)             (((PID) & ((1 << PIDBITS) - 1)))

/* Return the thread or lwp id stored in composite PID. */
#define TIDGET(PID)             (((PID) & 0x3fffffff) >> PIDBITS)
#define LIDGET(PID)             TIDGET(PID)

/* Construct a composite id from lwp LID and the process portion of
   composite PID. */
#define MERGEPID(PID, LID)      (PIDGET(PID) | ((LID) << PIDBITS))
#define MKLID(PID, LID)         MERGEPID(PID, LID)

/* Construct a composite id from thread TID and the process portion of
   composite PID. */
#define MKTID(PID, TID)         (MERGEPID(PID, TID) | 0x40000000)

/* Return whether PID contains a user-space thread id. */
#define ISTID(PID)              ((PID) & 0x40000000)
#endif

/* New definitions of the ptid stuff.  Due to the way the
   code is structured in uw-thread.c, I'm overloading the thread id
   and lwp id onto the lwp field.  The tid field is used to indicate
   whether the lwp is a tid or not.  
   
   FIXME: Check that core file support is not broken.  (See original
   #if 0'd comments above.)
   FIXME: Restructure uw-thread.c so that the struct ptid fields
   can be used as intended. */

/* Return the process id stored in composite PID. */
#define PIDGET(PID) (ptid_get_pid (PID))

/* Return the thread or lwp id stored in composite PID. */
#define TIDGET(PID) (ptid_get_lwp (PID))
#define LIDGET(PID) TIDGET(PID)

#define MERGEPID(PID, LID) (ptid_build ((PID), (LID), 0))
#define MKLID(PID, LID) (ptid_build ((PID), (LID), 0))

/* Construct a composite id from thread TID and the process portion of
   composite PID. */
#define MKTID(PID, TID) (ptid_build ((PID), (TID), 1))

/* Return whether PID contains a user-space thread id. */
#define ISTID(PID) (ptid_get_tid (PID))

#endif /* NM_I386V42MP_H */
