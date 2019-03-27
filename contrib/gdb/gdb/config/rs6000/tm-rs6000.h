/* Parameters for target execution on an RS6000, for GDB, the GNU debugger.

   Copyright 1986, 1987, 1989, 1991, 1992, 1993, 1994, 1995, 1996,
   1997, 1998, 1999, 2000, 2004 Free Software Foundation, Inc.

   Contributed by IBM Corporation.

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

struct frame_info;

#define GDB_MULTI_ARCH 1

/* Minimum possible text address in AIX */

#define TEXT_SEGMENT_BASE	0x10000000

/* Return whether PC in function NAME is in code that should be skipped when
   single-stepping.  */

#define IN_SOLIB_RETURN_TRAMPOLINE(pc, name) \
  rs6000_in_solib_return_trampoline (pc, name)
extern int rs6000_in_solib_return_trampoline (CORE_ADDR, char *);

/* If PC is in some function-call trampoline code, return the PC
   where the function itself actually starts.  If not, return NULL.  */

#define	SKIP_TRAMPOLINE_CODE(pc)	rs6000_skip_trampoline_code (pc)
extern CORE_ADDR rs6000_skip_trampoline_code (CORE_ADDR);

/* AIX has a couple of strange returns from wait().  */

#define CHILD_SPECIAL_WAITSTATUS(ourstatus, hoststatus) ( \
  /* "stop after load" status.  */ \
  (hoststatus) == 0x57c ? (ourstatus)->kind = TARGET_WAITKIND_LOADED, 1 : \
  \
  /* signal 0. I have no idea why wait(2) returns with this status word.  */ \
  /* It looks harmless. */ \
  (hoststatus) == 0x7f ? (ourstatus)->kind = TARGET_WAITKIND_SPURIOUS, 1 : \
  \
  /* A normal waitstatus.  Let the usual macros deal with it.  */ \
  0)

/* In xcoff, we cannot process line numbers when we see them. This is
   mainly because we don't know the boundaries of the include files. So,
   we postpone that, and then enter and sort(?) the whole line table at
   once, when we are closing the current symbol table in end_symtab(). */

#define	PROCESS_LINENUMBER_HOOK()	aix_process_linenos ()
extern void aix_process_linenos (void);

/* Register numbers of various important registers.
   Note that some of these values are "real" register numbers,
   and correspond to the general registers of the machine,
   and some are "phony" register numbers which are too large
   to be actual register numbers as far as the user is concerned
   but do serve to get the desired values when passed to read_register.  */

#define FP0_REGNUM 32		/* Floating point register 0 */
#define FPLAST_REGNUM 63	/* Last floating point register */

/* Define other aspects of the stack frame.  */

#define DEPRECATED_INIT_FRAME_PC_FIRST(fromleaf, prev) \
  (fromleaf ? DEPRECATED_SAVED_PC_AFTER_CALL (prev->next) : \
	      prev->next ? DEPRECATED_FRAME_SAVED_PC (prev->next) : read_pc ())

/* Notice when a new child process is started. */

#define TARGET_CREATE_INFERIOR_HOOK rs6000_create_inferior
extern void rs6000_create_inferior (int);

/* Hook in rs6000-tdep.c for determining the TOC address when
   calling functions in the inferior.  */

extern CORE_ADDR (*rs6000_find_toc_address_hook) (CORE_ADDR);

/* Hook in rs6000-tdep.c to set the current architecture when starting a
   child process. */

extern void (*rs6000_set_host_arch_hook) (int);

/* We need solib.h for building cross debuggers.  However, we don't want
   to clobber any special solib support required by native debuggers, so
   only include solib.h if SOLIB_ADD is not defined.  */
#ifndef SOLIB_ADD
#include "solib.h"
#endif
