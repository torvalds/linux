/* Target-dependent definitions for NetBSD/mips.
   Copyright 2002 Free Software Foundation, Inc.
   Contributed by Wasabi Systems, Inc.

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

#ifndef TM_NBSD_H
#define TM_NBSD_H

#include "mips/tm-mips.h"
#include "solib.h"

/* We don't want to inherit tm-mips.h's shared library trampoline code.  */
#undef IN_SOLIB_CALL_TRAMPOLINE
#undef IN_SOLIB_RETURN_TRAMPOLINE
#undef SKIP_TRAMPOLINE_CODE
#undef IGNORE_HELPER_CALL

/* XXX undef a bunch of stuff we want to use multi-arch */
#undef IN_SIGTRAMP

#endif /* TM_NBSD_H */
