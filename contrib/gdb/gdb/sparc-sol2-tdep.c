/* Target-dependent code for Solaris SPARC.

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
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include "defs.h"
#include "frame.h"
#include "frame-unwind.h"
#include "gdbcore.h"
#include "symtab.h"
#include "objfiles.h"
#include "osabi.h"
#include "regcache.h"
#include "target.h"
#include "trad-frame.h"

#include "gdb_assert.h"
#include "gdb_string.h"

#include "sparc-tdep.h"

/* From <sys/regset.h>.  */
const struct sparc_gregset sparc32_sol2_gregset =
{
  32 * 4,			/* %psr */
  33 * 4,			/* %pc */
  34 * 4,			/* %npc */
  35 * 4,			/* %y */
  36 * 4,			/* %wim */
  37 * 4,			/* %tbr */
  1 * 4,			/* %g1 */
  16 * 4,			/* %l0 */
};


/* The Solaris signal trampolines reside in libc.  For normal signals,
   the function `sigacthandler' is used.  This signal trampoline will
   call the signal handler using the System V calling convention,
   where the third argument is a pointer to an instance of
   `ucontext_t', which has a member `uc_mcontext' that contains the
   saved registers.  Incidentally, the kernel passes the `ucontext_t'
   pointer as the third argument of the signal trampoline too, and
   `sigacthandler' simply passes it on. However, if you link your
   program with "-L/usr/ucblib -R/usr/ucblib -lucb", the function
   `ucbsigvechandler' will be used, which invokes the using the BSD
   convention, where the third argument is a pointer to an instance of
   `struct sigcontext'.  It is the `ucbsigvechandler' function that
   converts the `ucontext_t' to a `sigcontext', and back.  Unless the
   signal handler modifies the `struct sigcontext' we can safely
   ignore this.  */

int
sparc_sol2_pc_in_sigtramp (CORE_ADDR pc, char *name)
{
  return (name && (strcmp (name, "sigacthandler") == 0
		   || strcmp (name, "ucbsigvechandler") == 0));
}

static struct sparc_frame_cache *
sparc32_sol2_sigtramp_frame_cache (struct frame_info *next_frame,
				   void **this_cache)
{
  struct sparc_frame_cache *cache;
  CORE_ADDR mcontext_addr, addr;
  int regnum;

  if (*this_cache)
    return *this_cache;

  cache = sparc_frame_cache (next_frame, this_cache);
  gdb_assert (cache == *this_cache);

  cache->saved_regs = trad_frame_alloc_saved_regs (next_frame);

  /* The third argument is a pointer to an instance of `ucontext_t',
     which has a member `uc_mcontext' that contains the saved
     registers.  */
  regnum = (cache->frameless_p ? SPARC_O2_REGNUM : SPARC_I2_REGNUM);
  mcontext_addr = frame_unwind_register_unsigned (next_frame, regnum) + 40;

  cache->saved_regs[SPARC32_PSR_REGNUM].addr = mcontext_addr + 0 * 4;
  cache->saved_regs[SPARC32_PC_REGNUM].addr = mcontext_addr + 1 * 4;
  cache->saved_regs[SPARC32_NPC_REGNUM].addr = mcontext_addr + 2 * 4;
  cache->saved_regs[SPARC32_Y_REGNUM].addr = mcontext_addr + 3 * 4;

  /* Since %g0 is always zero, keep the identity encoding.  */
  for (regnum = SPARC_G1_REGNUM, addr = mcontext_addr + 4 * 4;
       regnum <= SPARC_O7_REGNUM; regnum++, addr += 4)
    cache->saved_regs[regnum].addr = addr;

  if (get_frame_memory_unsigned (next_frame, mcontext_addr + 19 * 4, 4))
    {
      /* The register windows haven't been flushed.  */
      for (regnum = SPARC_L0_REGNUM; regnum <= SPARC_I7_REGNUM; regnum++)
	trad_frame_set_unknown (cache->saved_regs, regnum);
    }
  else
    {
      addr = cache->saved_regs[SPARC_SP_REGNUM].addr;
      addr = get_frame_memory_unsigned (next_frame, addr, 4);
      for (regnum = SPARC_L0_REGNUM;
	   regnum <= SPARC_I7_REGNUM; regnum++, addr += 4)
	cache->saved_regs[regnum].addr = addr;
    }

  return cache;
}

static void
sparc32_sol2_sigtramp_frame_this_id (struct frame_info *next_frame,
				     void **this_cache,
				     struct frame_id *this_id)
{
  struct sparc_frame_cache *cache =
    sparc32_sol2_sigtramp_frame_cache (next_frame, this_cache);

  (*this_id) = frame_id_build (cache->base, cache->pc);
}

static void
sparc32_sol2_sigtramp_frame_prev_register (struct frame_info *next_frame,
					   void **this_cache,
					   int regnum, int *optimizedp,
					   enum lval_type *lvalp,
					   CORE_ADDR *addrp,
					   int *realnump, void *valuep)
{
  struct sparc_frame_cache *cache =
    sparc32_sol2_sigtramp_frame_cache (next_frame, this_cache);

  trad_frame_prev_register (next_frame, cache->saved_regs, regnum,
			    optimizedp, lvalp, addrp, realnump, valuep);
}

static const struct frame_unwind sparc32_sol2_sigtramp_frame_unwind =
{
  SIGTRAMP_FRAME,
  sparc32_sol2_sigtramp_frame_this_id,
  sparc32_sol2_sigtramp_frame_prev_register
};

static const struct frame_unwind *
sparc32_sol2_sigtramp_frame_sniffer (struct frame_info *next_frame)
{
  CORE_ADDR pc = frame_pc_unwind (next_frame);
  char *name;

  find_pc_partial_function (pc, &name, NULL, NULL);
  if (sparc_sol2_pc_in_sigtramp (pc, name))
    return &sparc32_sol2_sigtramp_frame_unwind;

  return NULL;
}


void
sparc32_sol2_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* Solaris has SVR4-style shared libraries...  */
  set_gdbarch_in_solib_call_trampoline (gdbarch, in_plt_section);
  set_gdbarch_skip_trampoline_code (gdbarch, find_solib_trampoline_target);

  /* ...which means that we need some special handling when doing
     prologue analysis.  */
  tdep->plt_entry_size = 12;

  /* Solaris has kernel-assisted single-stepping support.  */
  set_gdbarch_software_single_step (gdbarch, NULL);

  set_gdbarch_pc_in_sigtramp (gdbarch, sparc_sol2_pc_in_sigtramp);
  frame_unwind_append_sniffer (gdbarch, sparc32_sol2_sigtramp_frame_sniffer);
}


/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_sparc_sol2_tdep (void);

void
_initialize_sparc_sol2_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_sparc, 0,
			  GDB_OSABI_SOLARIS, sparc32_sol2_init_abi);
}
