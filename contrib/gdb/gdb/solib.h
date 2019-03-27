/* Shared library declarations for GDB, the GNU Debugger.
   Copyright 1992, 1993, 1995, 1998, 1999, 2000, 2001, 2003
   Free Software Foundation, Inc.

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

#ifndef SOLIB_H
#define SOLIB_H

/* Forward decl's for prototypes */
struct target_ops;

/* Called when we free all symtabs, to free the shared library information
   as well. */

#define CLEAR_SOLIB			clear_solib

extern void clear_solib (void);

/* Called to add symbols from a shared library to gdb's symbol table. */

#define SOLIB_ADD(filename, from_tty, targ, readsyms) \
    solib_add (filename, from_tty, targ, readsyms)

extern void solib_add (char *, int, struct target_ops *, int);

/* Function to be called when the inferior starts up, to discover the names
   of shared libraries that are dynamically linked, the base addresses to
   which they are linked, and sufficient information to read in their symbols
   at a later time. */

#define SOLIB_CREATE_INFERIOR_HOOK(PID)	solib_create_inferior_hook()

/* Function to be called to remove the connection between debugger and
   dynamic linker that was established by SOLIB_CREATE_INFERIOR_HOOK.
   (This operation does not remove shared library information from
   the debugger, as CLEAR_SOLIB does.)

   This functionality is presently not implemented for this target.
 */
#define SOLIB_REMOVE_INFERIOR_HOOK(PID) (0)

extern void solib_create_inferior_hook (void);	/* solib.c */

/* This function returns TRUE if pc is the address of an instruction that
   lies within the dynamic linker (such as the event hook, or the dld
   itself).

   This function must be used only when a dynamic linker event has been
   caught, and the inferior is being stepped out of the hook, or undefined
   results are guaranteed.

   Presently, this functionality is not implemented.
 */

/*
   #define SOLIB_IN_DYNAMIC_LINKER(pid,pc) \
   error("catch of library loads/unloads not yet implemented on this platform")
 */

#define SOLIB_IN_DYNAMIC_LINKER(pid,pc) \
(0)

/* This function must be called when the inferior is killed, and the program
   restarted.  This is not the same as CLEAR_SOLIB, in that it doesn't discard
   any symbol tables.

   Presently, this functionality is not implemented.
 */
#define SOLIB_RESTART() \
  (0)

/* If we can't set a breakpoint, and it's in a shared library, just
   disable it.  */

#define DISABLE_UNSETTABLE_BREAK(addr)	(solib_address(addr) != NULL)

extern char *solib_address (CORE_ADDR);	/* solib.c */

/* If ADDR lies in a shared library, return its name.  */

#define PC_SOLIB(addr)	solib_address (addr)

/* Return 1 if PC lies in the dynamic symbol resolution code of the
   run time loader.  */

#define IN_SOLIB_DYNSYM_RESOLVE_CODE(pc) in_solib_dynsym_resolve_code (pc)

extern int in_solib_dynsym_resolve_code (CORE_ADDR);	/* solib.c */

/* Discard symbols that were auto-loaded from shared libraries. */

extern void no_shared_libraries (char *ignored, int from_tty);

#endif /* SOLIB_H */
