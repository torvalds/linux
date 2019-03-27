/* Target-dependent code for FreeBSD/sparc64.

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
#include "gdbcore.h"
#include "osabi.h"
#include "regcache.h"
#include "regset.h"
#include "target.h"
#include "trad-frame.h"

#include "gdb_assert.h"
#include "gdb_string.h"

#include "sparc64-tdep.h"

/* From <machine/reg.h>.  */
const struct sparc_gregset sparc64fbsd_gregset =
{
  26 * 8,			/* "tstate" */
  25 * 8,			/* %pc */
  24 * 8,			/* %npc */
  28 * 8,			/* %y */
  16 * 8,			/* %fprs */
  -1,
  1 * 8,			/* %g1 */
  -1,				/* %l0 */
  8				/* sizeof (%y) */
};


static void
sparc64fbsd_supply_gregset (const struct regset *regset,
			    struct regcache *regcache,
			    int regnum, const void *gregs, size_t len)
{
  sparc64_supply_gregset (regset->descr, regcache, regnum, gregs);
}

static void
sparc64fbsd_supply_fpregset (const struct regset *regset,
			     struct regcache *regcache,
			     int regnum, const void *fpregs, size_t len)
{
  sparc64_supply_fpregset (regcache, regnum, fpregs);
}

void
supply_gregset (const void *gregs)
{
  sparc64_supply_gregset (&sparc64fbsd_gregset, current_regcache, -1, gregs);
}

void
supply_fpregset (const void *fpregs)
{
  sparc64_supply_fpregset (current_regcache, -1, fpregs);
}

void
fill_gregset (void *gregs, int regnum)
{
  sparc64_collect_gregset (&sparc64fbsd_gregset, current_regcache, regnum,
			   gregs);
}

void
fill_fpregset (void *fpregs, int regnum)
{
  sparc64_collect_fpregset (current_regcache, regnum, fpregs);
}


/* Signal trampolines.  */

static int
sparc64fbsd_pc_in_sigtramp (CORE_ADDR pc, char *name)
{
  return (name && strcmp (name, "__sigtramp") == 0);
}

static struct sparc_frame_cache *
sparc64fbsd_sigtramp_frame_cache (struct frame_info *next_frame,
				   void **this_cache)
{
  struct sparc_frame_cache *cache;
  CORE_ADDR addr, mcontext_addr, sp;
  LONGEST fprs;
  int regnum;

  if (*this_cache)
    return *this_cache;

  cache = sparc_frame_cache (next_frame, this_cache);
  gdb_assert (cache == *this_cache);

  cache->saved_regs = trad_frame_alloc_saved_regs (next_frame);

  /* The third argument is a pointer to an instance of `ucontext_t',
     which has a member `uc_mcontext' that contains the saved
     registers.  */
  addr = frame_unwind_register_unsigned (next_frame, SPARC_O2_REGNUM);
  mcontext_addr = addr + 64;

  /* The following registers travel in the `mc_local' slots of
     `mcontext_t'.  */
  addr = mcontext_addr + 16 * 8;
  cache->saved_regs[SPARC64_FPRS_REGNUM].addr = addr + 0 * 8;
  cache->saved_regs[SPARC64_FSR_REGNUM].addr = addr + 1 * 8;

  /* The following registers travel in the `mc_in' slots of
     `mcontext_t'.  */
  addr = mcontext_addr + 24 * 8;
  cache->saved_regs[SPARC64_NPC_REGNUM].addr = addr + 0 * 8;
  cache->saved_regs[SPARC64_PC_REGNUM].addr = addr + 1 * 8;
  cache->saved_regs[SPARC64_STATE_REGNUM].addr = addr + 2 * 8;
  cache->saved_regs[SPARC64_Y_REGNUM].addr = addr + 4 * 8;

  /* The `global' and `out' registers travel in the `mc_global' and
     `mc_out' slots of `mcontext_t', except for %g0.  Since %g0 is
     always zero, keep the identity encoding.  */
  for (regnum = SPARC_G1_REGNUM, addr = mcontext_addr + 8;
       regnum <= SPARC_O7_REGNUM; regnum++, addr += 8)
    cache->saved_regs[regnum].addr = addr;

  /* The `local' and `in' registers have been saved in the register
     save area.  */
  addr = cache->saved_regs[SPARC_SP_REGNUM].addr;
  sp = get_frame_memory_unsigned (next_frame, addr, 8);
  for (regnum = SPARC_L0_REGNUM, addr = sp + BIAS;
       regnum <= SPARC_I7_REGNUM; regnum++, addr += 8)
    cache->saved_regs[regnum].addr = addr;

  /* The floating-point registers are only saved if the FEF bit in
     %fprs has been set.  */

#define FPRS_FEF	(1 << 2)

  addr = cache->saved_regs[SPARC64_FPRS_REGNUM].addr;
  fprs = get_frame_memory_unsigned (next_frame, addr, 8);
  if (fprs & FPRS_FEF)
    {
      for (regnum = SPARC_F0_REGNUM, addr = mcontext_addr + 32 * 8;
	   regnum <= SPARC_F31_REGNUM; regnum++, addr += 4)
	cache->saved_regs[regnum].addr = addr;

      for (regnum = SPARC64_F32_REGNUM;
	   regnum <= SPARC64_F62_REGNUM; regnum++, addr += 8)
	cache->saved_regs[regnum].addr = addr;
    }

  return cache;
}

static void
sparc64fbsd_sigtramp_frame_this_id (struct frame_info *next_frame,
				    void **this_cache,
				    struct frame_id *this_id)
{
  struct sparc_frame_cache *cache =
    sparc64fbsd_sigtramp_frame_cache (next_frame, this_cache);

  (*this_id) = frame_id_build (cache->base, cache->pc);
}

static void
sparc64fbsd_sigtramp_frame_prev_register (struct frame_info *next_frame,
					  void **this_cache,
					  int regnum, int *optimizedp,
					  enum lval_type *lvalp,
					  CORE_ADDR *addrp,
					  int *realnump, void *valuep)
{
  struct sparc_frame_cache *cache =
    sparc64fbsd_sigtramp_frame_cache (next_frame, this_cache);

  trad_frame_prev_register (next_frame, cache->saved_regs, regnum,
			    optimizedp, lvalp, addrp, realnump, valuep);
}

static const struct frame_unwind sparc64fbsd_sigtramp_frame_unwind =
{
  SIGTRAMP_FRAME,
  sparc64fbsd_sigtramp_frame_this_id,
  sparc64fbsd_sigtramp_frame_prev_register
};

static const struct frame_unwind *
sparc64fbsd_sigtramp_frame_sniffer (struct frame_info *next_frame)
{
  CORE_ADDR pc = frame_pc_unwind (next_frame);
  char *name;

  find_pc_partial_function (pc, &name, NULL, NULL);
  if (sparc64fbsd_pc_in_sigtramp (pc, name))
    return &sparc64fbsd_sigtramp_frame_unwind;

  return NULL;
}


static void
sparc64fbsd_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  tdep->gregset = XMALLOC (struct regset);
  tdep->gregset->descr = &sparc64fbsd_gregset;
  tdep->gregset->supply_regset = sparc64fbsd_supply_gregset;
  tdep->sizeof_gregset = 256;

  tdep->fpregset = XMALLOC (struct regset);
  tdep->fpregset->supply_regset = sparc64fbsd_supply_fpregset;
  tdep->sizeof_fpregset = 272;

  set_gdbarch_pc_in_sigtramp (gdbarch, sparc64fbsd_pc_in_sigtramp);
  frame_unwind_append_sniffer (gdbarch, sparc64fbsd_sigtramp_frame_sniffer);

  sparc64_init_abi (info, gdbarch);
}

/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_sparc64fbsd_tdep (void);

void
_initialize_sparc64fbsd_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_sparc, bfd_mach_sparc_v9,
			  GDB_OSABI_FREEBSD_ELF, sparc64fbsd_init_abi);
}
