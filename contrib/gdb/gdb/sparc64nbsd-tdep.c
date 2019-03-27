/* Target-dependent code for NetBSD/sparc64.

   Copyright 2002, 2003, 2004 Free Software Foundation, Inc.
   Based on code contributed by Wasabi Systems, Inc.

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
#include "symtab.h"
#include "solib-svr4.h"
#include "trad-frame.h"

#include "gdb_assert.h"
#include "gdb_string.h"

#include "sparc64-tdep.h"
#include "nbsd-tdep.h"

/* From <machine/reg.h>.  */
const struct sparc_gregset sparc64nbsd_gregset =
{
  0 * 8,			/* "tstate" */
  1 * 8,			/* %pc */
  2 * 8,			/* %npc */
  3 * 8,			/* %y */
  -1,				/* %fprs */
  -1,
  5 * 8,			/* %g1 */
  -1,				/* %l0 */
  4				/* sizeof (%y) */
};


static void
sparc64nbsd_supply_gregset (const struct regset *regset,
			    struct regcache *regcache,
			    int regnum, const void *gregs, size_t len)
{
  sparc64_supply_gregset (regset->descr, regcache, regnum, gregs);
}

static void
sparc64nbsd_supply_fpregset (const struct regset *regset,
			     struct regcache *regcache,
			     int regnum, const void *fpregs, size_t len)
{
  sparc64_supply_fpregset (regcache, regnum, fpregs);
}


/* Signal trampolines.  */

/* The following variables describe the location of an on-stack signal
   trampoline.  The current values correspond to the memory layout for
   NetBSD 1.3 and up.  These shouldn't be necessary for NetBSD 2.0 and
   up, since NetBSD uses signal trampolines provided by libc now.  */

static const CORE_ADDR sparc64nbsd_sigtramp_start = 0xffffffffffffdee4ULL;
static const CORE_ADDR sparc64nbsd_sigtramp_end = 0xffffffffffffe000ULL;

static int
sparc64nbsd_pc_in_sigtramp (CORE_ADDR pc, char *name)
{
  if (pc >= sparc64nbsd_sigtramp_start && pc < sparc64nbsd_sigtramp_end)
    return 1;

  return nbsd_pc_in_sigtramp (pc, name);
}

struct trad_frame_saved_reg *
sparc64nbsd_sigcontext_saved_regs (CORE_ADDR sigcontext_addr,
				   struct frame_info *next_frame)
{
  struct trad_frame_saved_reg *saved_regs;
  CORE_ADDR addr, sp;
  int regnum, delta;

  saved_regs = trad_frame_alloc_saved_regs (next_frame);

  /* The registers are saved in bits and pieces scattered all over the
     place.  The code below records their location on the assumption
     that the part of the signal trampoline that saves the state has
     been executed.  */

  saved_regs[SPARC_SP_REGNUM].addr = sigcontext_addr + 8;
  saved_regs[SPARC64_PC_REGNUM].addr = sigcontext_addr + 16;
  saved_regs[SPARC64_NPC_REGNUM].addr = sigcontext_addr + 24;
  saved_regs[SPARC64_STATE_REGNUM].addr = sigcontext_addr + 32;
  saved_regs[SPARC_G1_REGNUM].addr = sigcontext_addr + 40;
  saved_regs[SPARC_O0_REGNUM].addr = sigcontext_addr + 48;

  /* The remaining `global' registers and %y are saved in the `local'
     registers.  */
  delta = SPARC_L0_REGNUM - SPARC_G0_REGNUM;
  for (regnum = SPARC_G2_REGNUM; regnum <= SPARC_G7_REGNUM; regnum++)
    saved_regs[regnum].realreg = regnum + delta;
  saved_regs[SPARC64_Y_REGNUM].realreg = SPARC_L1_REGNUM;

  /* The remaining `out' registers can be found in the current frame's
     `in' registers.  */
  delta = SPARC_I0_REGNUM - SPARC_O0_REGNUM;
  for (regnum = SPARC_O1_REGNUM; regnum <= SPARC_O5_REGNUM; regnum++)
    saved_regs[regnum].realreg = regnum + delta;
  saved_regs[SPARC_O7_REGNUM].realreg = SPARC_I7_REGNUM;

  /* The `local' and `in' registers have been saved in the register
     save area.  */
  addr = saved_regs[SPARC_SP_REGNUM].addr;
  sp = get_frame_memory_unsigned (next_frame, addr, 8);
  for (regnum = SPARC_L0_REGNUM, addr = sp + BIAS;
       regnum <= SPARC_I7_REGNUM; regnum++, addr += 8)
    saved_regs[regnum].addr = addr;

  /* TODO: Handle the floating-point registers.  */

  return saved_regs;
}

static struct sparc_frame_cache *
sparc64nbsd_sigcontext_frame_cache (struct frame_info *next_frame,
				    void **this_cache)
{
  struct sparc_frame_cache *cache;
  CORE_ADDR addr;

  if (*this_cache)
    return *this_cache;

  cache = sparc_frame_cache (next_frame, this_cache);
  gdb_assert (cache == *this_cache);

  /* If we couldn't find the frame's function, we're probably dealing
     with an on-stack signal trampoline.  */
  if (cache->pc == 0)
    {
      cache->pc = sparc64nbsd_sigtramp_start;

      /* Since we couldn't find the frame's function, the cache was
         initialized under the assumption that we're frameless.  */
      cache->frameless_p = 0;
      addr = frame_unwind_register_unsigned (next_frame, SPARC_FP_REGNUM);
      cache->base = addr;
    }

  /* We find the appropriate instance of `struct sigcontext' at a
     fixed offset in the signal frame.  */
  addr = cache->base + BIAS + 128 + 8;
  cache->saved_regs = sparc64nbsd_sigcontext_saved_regs (addr, next_frame);

  return cache;
}

static void
sparc64nbsd_sigcontext_frame_this_id (struct frame_info *next_frame,
				      void **this_cache,
				      struct frame_id *this_id)
{
  struct sparc_frame_cache *cache =
    sparc64nbsd_sigcontext_frame_cache (next_frame, this_cache);

  (*this_id) = frame_id_build (cache->base, cache->pc);
}

static void
sparc64nbsd_sigcontext_frame_prev_register (struct frame_info *next_frame,
					    void **this_cache,
					    int regnum, int *optimizedp,
					    enum lval_type *lvalp,
					    CORE_ADDR *addrp,
					    int *realnump, void *valuep)
{
  struct sparc_frame_cache *cache =
    sparc64nbsd_sigcontext_frame_cache (next_frame, this_cache);

  trad_frame_prev_register (next_frame, cache->saved_regs, regnum,
			    optimizedp, lvalp, addrp, realnump, valuep);
}

static const struct frame_unwind sparc64nbsd_sigcontext_frame_unwind =
{
  SIGTRAMP_FRAME,
  sparc64nbsd_sigcontext_frame_this_id,
  sparc64nbsd_sigcontext_frame_prev_register
};

static const struct frame_unwind *
sparc64nbsd_sigtramp_frame_sniffer (struct frame_info *next_frame)
{
  CORE_ADDR pc = frame_pc_unwind (next_frame);
  char *name;

  find_pc_partial_function (pc, &name, NULL, NULL);
  if (sparc64nbsd_pc_in_sigtramp (pc, name))
    {
      if (name == NULL || strncmp (name, "__sigtramp_sigcontext", 21))
	return &sparc64nbsd_sigcontext_frame_unwind;
    }

  return NULL;
}


static void
sparc64nbsd_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  tdep->gregset = XMALLOC (struct regset);
  tdep->gregset->descr = &sparc64nbsd_gregset;
  tdep->gregset->supply_regset = sparc64nbsd_supply_gregset;
  tdep->sizeof_gregset = 160;

  tdep->fpregset = XMALLOC (struct regset);
  tdep->fpregset->supply_regset = sparc64nbsd_supply_fpregset;
  tdep->sizeof_fpregset = 272;

  set_gdbarch_pc_in_sigtramp (gdbarch, sparc64nbsd_pc_in_sigtramp);
  frame_unwind_append_sniffer (gdbarch, sparc64nbsd_sigtramp_frame_sniffer);

  sparc64_init_abi (info, gdbarch);

  set_solib_svr4_fetch_link_map_offsets
    (gdbarch, nbsd_lp64_solib_svr4_fetch_link_map_offsets);
}


/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_sparc64nbsd_tdep (void);

void
_initialize_sparc64nbsd_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_sparc, bfd_mach_sparc_v9,
			  GDB_OSABI_NETBSD_ELF, sparc64nbsd_init_abi);
}
