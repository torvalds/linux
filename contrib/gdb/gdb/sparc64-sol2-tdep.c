/* Target-dependent code for Solaris UltraSPARC.

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
#include "frame.h"
#include "frame-unwind.h"
#include "gdbarch.h"
#include "symtab.h"
#include "objfiles.h"
#include "osabi.h"
#include "trad-frame.h"

#include "gdb_assert.h"

#include "sparc64-tdep.h"

/* From <sys/regset.h>.  */
const struct sparc_gregset sparc64_sol2_gregset =
{
  32 * 8,			/* "tstate" */
  33 * 8,			/* %pc */
  34 * 8,			/* %npc */
  35 * 8,			/* %y */
  -1,				/* %wim */
  -1,				/* %tbr */
  1 * 8,			/* %g1 */
  16 * 8,			/* %l0 */
  8				/* sizeof (%y) */
};


static struct sparc_frame_cache *
sparc64_sol2_sigtramp_frame_cache (struct frame_info *next_frame,
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
  mcontext_addr = frame_unwind_register_unsigned (next_frame, regnum) + 64;

  cache->saved_regs[SPARC64_CCR_REGNUM].addr = mcontext_addr + 0 * 8;
  cache->saved_regs[SPARC64_PC_REGNUM].addr = mcontext_addr + 1 * 8;
  cache->saved_regs[SPARC64_NPC_REGNUM].addr = mcontext_addr + 2 * 8;
  cache->saved_regs[SPARC64_Y_REGNUM].addr = mcontext_addr + 3 * 8;
  cache->saved_regs[SPARC64_ASI_REGNUM].addr = mcontext_addr + 19 * 8; 
  cache->saved_regs[SPARC64_FPRS_REGNUM].addr = mcontext_addr + 20 * 8;

  /* Since %g0 is always zero, keep the identity encoding.  */
  for (regnum = SPARC_G1_REGNUM, addr = mcontext_addr + 4 * 8;
       regnum <= SPARC_O7_REGNUM; regnum++, addr += 8)
    cache->saved_regs[regnum].addr = addr;

  if (get_frame_memory_unsigned (next_frame, mcontext_addr + 21 * 8, 8))
    {
      /* The register windows haven't been flushed.  */
      for (regnum = SPARC_L0_REGNUM; regnum <= SPARC_I7_REGNUM; regnum++)
	trad_frame_set_unknown (cache->saved_regs, regnum);
    }
  else
    {
      CORE_ADDR sp;

      addr = cache->saved_regs[SPARC_SP_REGNUM].addr;
      sp = get_frame_memory_unsigned (next_frame, addr, 8);
      for (regnum = SPARC_L0_REGNUM, addr = sp + BIAS;
	   regnum <= SPARC_I7_REGNUM; regnum++, addr += 8)
	cache->saved_regs[regnum].addr = addr;
    }

  return cache;
}

static void
sparc64_sol2_sigtramp_frame_this_id (struct frame_info *next_frame,
				     void **this_cache,
				     struct frame_id *this_id)
{
  struct sparc_frame_cache *cache =
    sparc64_sol2_sigtramp_frame_cache (next_frame, this_cache);

  (*this_id) = frame_id_build (cache->base, cache->pc);
}

static void
sparc64_sol2_sigtramp_frame_prev_register (struct frame_info *next_frame,
					   void **this_cache,
					   int regnum, int *optimizedp,
					   enum lval_type *lvalp,
					   CORE_ADDR *addrp,
					   int *realnump, void *valuep)
{
  struct sparc_frame_cache *cache =
    sparc64_sol2_sigtramp_frame_cache (next_frame, this_cache);

  trad_frame_prev_register (next_frame, cache->saved_regs, regnum,
			    optimizedp, lvalp, addrp, realnump, valuep);
}

static const struct frame_unwind sparc64_sol2_sigtramp_frame_unwind =
{
  SIGTRAMP_FRAME,
  sparc64_sol2_sigtramp_frame_this_id,
  sparc64_sol2_sigtramp_frame_prev_register
};

static const struct frame_unwind *
sparc64_sol2_sigtramp_frame_sniffer (struct frame_info *next_frame)
{
  CORE_ADDR pc = frame_pc_unwind (next_frame);
  char *name;

  find_pc_partial_function (pc, &name, NULL, NULL);
  if (sparc_sol2_pc_in_sigtramp (pc, name))
    return &sparc64_sol2_sigtramp_frame_unwind;

  return NULL;
}


void
sparc64_sol2_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  set_gdbarch_pc_in_sigtramp (gdbarch, sparc_sol2_pc_in_sigtramp);
  frame_unwind_append_sniffer (gdbarch, sparc64_sol2_sigtramp_frame_sniffer);

  sparc64_init_abi (info, gdbarch);

  /* Solaris has SVR4-style shared libraries...  */
  set_gdbarch_in_solib_call_trampoline (gdbarch, in_plt_section);
  set_gdbarch_skip_trampoline_code (gdbarch, find_solib_trampoline_target);

  /* ...which means that we need some special handling when doing
     prologue analysis.  */
  tdep->plt_entry_size = 16;

  /* Solaris has kernel-assisted single-stepping support.  */
  set_gdbarch_software_single_step (gdbarch, NULL);
}


/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_sparc64_sol2_tdep (void);

void
_initialize_sparc64_sol2_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_sparc, bfd_mach_sparc_v9,
			  GDB_OSABI_SOLARIS, sparc64_sol2_init_abi);
}
