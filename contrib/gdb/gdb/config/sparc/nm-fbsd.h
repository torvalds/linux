/* Native-dependent definitions for FreeBSD/sparc64.

   Copyright 2002, 2003 Free Software Foundation, Inc.
   Contributed by David E. O'Brien <obrien@FreeBSD.org>.

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

#ifndef NM_FBSD_H
#define NM_FBSD_H

/* Type of the third argument to the `ptrace' system call.  */
#define PTRACE_ARG3_TYPE caddr_t

/* Override copies of {fetch,store}_inferior_registers in `infptrace.c'.  */
#define FETCH_INFERIOR_REGISTERS

/* We can attach and detach.  */
#define ATTACH_DETACH


/* Shared library support.  */

#include "solib.h"

#endif /* nm-fbsd.h */
