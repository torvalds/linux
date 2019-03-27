/* Native-dependent code for Solaris SPARC.

   Copyright 2003, 2004 Free Software Foundation, Inc.

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

#include "defs.h"
#include "regcache.h"

#include <sys/procfs.h>
#include "gregset.h"

#include "sparc-tdep.h"

/* This file provids the (temporary) glue between the Solaris SPARC
   target dependent code and the machine independent SVR4 /proc
   support.  */

/* Solaris 7 (Solaris 2.7, SunOS 5.7) and up support two process data
   models, the traditional 32-bit data model (ILP32) and the 64-bit
   data model (LP64).  The format of /proc depends on the data model
   of the observer (the controlling process, GDB in our case).  The
   Solaris header files conveniently define PR_MODEL_NATIVE to the
   data model of the controlling process.  If its value is
   PR_MODEL_LP64, we know that GDB is being compiled as a 64-bit
   program.

   GNU/Linux uses the same formats as Solaris for its core files (but
   not for ptrace(2)).  The GNU/Linux headers don't define
   PR_MODEL_NATIVE though.  Therefore we rely on the __arch64__ define
   provided by GCC to determine the appropriate data model.

   Note that a 32-bit GDB won't be able to debug a 64-bit target
   process using /proc on Solaris.  */

#if (defined (__arch64__) || \
     (defined (PR_MODEL_NATIVE) && (PR_MODEL_NATIVE == PR_MODEL_LP64)))

#include "sparc64-tdep.h"

#define sparc_supply_gregset sparc64_supply_gregset
#define sparc_supply_fpregset sparc64_supply_fpregset
#define sparc_collect_gregset sparc64_collect_gregset
#define sparc_collect_fpregset sparc64_collect_fpregset

#define sparc_sol2_gregset sparc64_sol2_gregset
#define sparc_sol2_fpregset sparc64_sol2_fpregset

#else

#define sparc_supply_gregset sparc32_supply_gregset
#define sparc_supply_fpregset sparc32_supply_fpregset
#define sparc_collect_gregset sparc32_collect_gregset
#define sparc_collect_fpregset sparc32_collect_fpregset

#define sparc_sol2_gregset sparc32_sol2_gregset
#define sparc_sol2_fpregset sparc32_sol2_fpregset

#endif

void
supply_gregset (prgregset_t *gregs)
{
  sparc_supply_gregset (&sparc_sol2_gregset, current_regcache, -1, gregs);
}

void
supply_fpregset (prfpregset_t *fpregs)
{
  sparc_supply_fpregset (current_regcache, -1, fpregs);
}

void
fill_gregset (prgregset_t *gregs, int regnum)
{
  sparc_collect_gregset (&sparc_sol2_gregset, current_regcache, regnum, gregs);
}

void
fill_fpregset (prfpregset_t *fpregs, int regnum)
{
  sparc_collect_fpregset (current_regcache, regnum, fpregs);
}
