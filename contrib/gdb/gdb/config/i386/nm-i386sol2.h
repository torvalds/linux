/* Native support for i386 running Solaris 2.
   Copyright 1998, 1999, 2000 Free Software Foundation, Inc.

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

#include "config/nm-sysv4.h"

#ifdef NEW_PROC_API	/* Solaris 6 and above can do HW watchpoints */

#define TARGET_HAS_HARDWARE_WATCHPOINTS

/* The man page for proc4 on solaris 6 and 7 says that the system
   can support "thousands" of hardware watchpoints, but gives no
   method for finding out how many.  So just tell GDB 'yes'.  */
#define TARGET_CAN_USE_HARDWARE_WATCHPOINT(TYPE, CNT, OT) 1
#define TARGET_REGION_SIZE_OK_FOR_HW_WATCHPOINT(SIZE) 1

/* When a hardware watchpoint fires off the PC will be left at the
   instruction following the one which caused the watchpoint.  
   It will *NOT* be necessary for GDB to step over the watchpoint. */
#define HAVE_CONTINUABLE_WATCHPOINT 1

/* Solaris x86 2.6 and 2.7 targets have a kernel bug when stepping
   over an instruction that causes a page fault without triggering
   a hardware watchpoint. The kernel properly notices that it shouldn't
   stop, because the hardware watchpoint is not triggered, but it forgets
   the step request and continues the program normally.
   Work around the problem by removing hardware watchpoints if a step is
   requested, GDB will check for a hardware watchpoint trigger after the
   step anyway.  */
#define CANNOT_STEP_HW_WATCHPOINTS

extern int procfs_stopped_by_watchpoint (ptid_t);
#define STOPPED_BY_WATCHPOINT(W) \
  procfs_stopped_by_watchpoint(inferior_ptid)

/* Use these macros for watchpoint insertion/deletion.  */
/* type can be 0: write watch, 1: read watch, 2: access watch (read/write) */

extern int procfs_set_watchpoint (ptid_t, CORE_ADDR, int, int, int);
#define target_insert_watchpoint(ADDR, LEN, TYPE) \
        procfs_set_watchpoint (inferior_ptid, ADDR, LEN, TYPE, 1)
#define target_remove_watchpoint(ADDR, LEN, TYPE) \
        procfs_set_watchpoint (inferior_ptid, ADDR, 0, 0, 0)

#endif /* NEW_PROC_API */
