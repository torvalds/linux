/* Interface for functions using gregset and fpregset types.
   Copyright 2000, 2002 Free Software Foundation, Inc.

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

#ifndef GREGSET_H
#define GREGSET_H

#ifndef GDB_GREGSET_T
#define GDB_GREGSET_T gregset_t
#endif

#ifndef GDB_FPREGSET_T
#define GDB_FPREGSET_T fpregset_t
#endif

typedef GDB_GREGSET_T gdb_gregset_t;
typedef GDB_FPREGSET_T gdb_fpregset_t;

/* A gregset is a data structure supplied by the native OS containing
   the general register values of the debugged process.  Usually this
   includes integer registers and control registers.  An fpregset is a
   data structure containing the floating point registers.  These data
   structures were originally a part of the /proc interface, but have
   been borrowed or copied by other GDB targets, eg. GNU/Linux.  */

/* Copy register values from the native target gregset/fpregset
   into GDB's internal register cache.  */

extern void supply_gregset (gdb_gregset_t *gregs);
extern void supply_fpregset (gdb_fpregset_t *fpregs);

/* Copy register values from GDB's register cache into
   the native target gregset/fpregset.  If regno is -1, 
   copy all the registers.  */

extern void fill_gregset (gdb_gregset_t *gregs, int regno);
extern void fill_fpregset (gdb_fpregset_t *fpregs, int regno);

#ifdef FILL_FPXREGSET
/* GNU/Linux i386: Copy register values between GDB's internal register cache
   and the i386 extended floating point registers.  */

#ifndef GDB_FPXREGSET_T
#define GDB_FPXREGSET_T elf_fpxregset_t
#endif

typedef GDB_FPXREGSET_T gdb_fpxregset_t;

extern void supply_fpxregset (gdb_fpxregset_t *fpxregs);
extern void fill_fpxregset (gdb_fpxregset_t *fpxregs, int regno);
#endif

#endif
