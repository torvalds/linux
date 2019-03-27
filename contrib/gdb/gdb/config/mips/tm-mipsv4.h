// OBSOLETE /* Target machine description for MIPS running SVR4, for GDB.
// OBSOLETE    Copyright 1994, 1995, 1998, 1999, 2000 Free Software Foundation, Inc.
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
// OBSOLETE #include "mips/tm-mips.h"
// OBSOLETE #include "config/tm-sysv4.h"
// OBSOLETE 
// OBSOLETE /* The signal handler trampoline is called _sigtramp.  */
// OBSOLETE #undef IN_SIGTRAMP
// OBSOLETE #define IN_SIGTRAMP(pc, name) ((name) && DEPRECATED_STREQ ("_sigtramp", name))
// OBSOLETE 
// OBSOLETE /* On entry to the signal handler trampoline, an ucontext is already
// OBSOLETE    pushed on the stack. We can get at the saved registers via the
// OBSOLETE    mcontext which is contained within the ucontext.  */
// OBSOLETE #define SIGFRAME_BASE	0
// OBSOLETE #define SIGFRAME_REGSAVE_OFF	(SIGFRAME_BASE + 40)
// OBSOLETE #define SIGFRAME_PC_OFF		(SIGFRAME_BASE + 40 + 35 * 4)
// OBSOLETE #define SIGFRAME_FPREGSAVE_OFF	(SIGFRAME_BASE + 40 + 36 * 4)
// OBSOLETE 
// OBSOLETE /* Convert a DWARF register number to a gdb REGNUM.  */
// OBSOLETE #define DWARF_REG_TO_REGNUM(num) ((num) < 32 ? (num) : (num)+FP0_REGNUM-32)
