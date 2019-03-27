/* Native-dependent definitions for Solaris SPARC.

   Copyright 2003 Free Software Foundation, Inc.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#ifndef NM_SOL2_H
#define NM_SOL2_H

#define GDB_GREGSET_T prgregset_t
#define GDB_FPREGSET_T prfpregset_t

/* Shared library support.  */

#include "solib.h"

/* Hardware wactchpoints.  */

/* Solaris 2.6 and above can do HW watchpoints.  */
#ifdef NEW_PROC_API

#define TARGET_HAS_HARDWARE_WATCHPOINTS

/* The man page for proc(4) on Solaris 2.6 and up says that the system
   can support "thousands" of hardware watchpoints, but gives no
   method for finding out how many; It doesn't say anything about the
   allowed size for the watched area either.  So we just tell GDB
   'yes'.  */
#define TARGET_REGION_SIZE_OK_FOR_HW_WATCHPOINT(SIZE) 1

/* When a hardware watchpoint fires off the PC will be left at the
   instruction following the one which caused the watchpoint.  It will
   *NOT* be necessary for GDB to step over the watchpoint.  */
#define HAVE_CONTINUABLE_WATCHPOINT 1

extern int procfs_stopped_by_watchpoint (ptid_t);
#define STOPPED_BY_WATCHPOINT(W) \
  procfs_stopped_by_watchpoint(inferior_ptid)

/* Use these macros for watchpoint insertion/deletion.  TYPE can be 0
   (write watch), 1 (read watch), 2 (access watch (read/write).  */

extern int procfs_set_watchpoint (ptid_t, CORE_ADDR, int, int, int);
#define target_insert_watchpoint(ADDR, LEN, TYPE) \
        procfs_set_watchpoint (inferior_ptid, ADDR, LEN, TYPE, 1)
#define target_remove_watchpoint(ADDR, LEN, TYPE) \
        procfs_set_watchpoint (inferior_ptid, ADDR, 0, 0, 0)

#endif /* NEW_PROC_API */

#endif /* nm-sol2.h */
