/* Target-dependent code for OpenBSD/i386.

   Copyright 1988, 1989, 1991, 1992, 1994, 1996, 2000, 2001, 2002,
   2003, 2004
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

#include "defs.h"
#include "arch-utils.h"
#include "gdbcore.h"
#include "regcache.h"
#include "regset.h"
#include "osabi.h"
#include "target.h"

#include "gdb_assert.h"
#include "gdb_string.h"

#include "i386-tdep.h"
#include "i387-tdep.h"
#include "solib-svr4.h"

/* Support for signal handlers.  */

/* Since OpenBSD 3.2, the sigtramp routine is mapped at a random page
   in virtual memory.  The randomness makes it somewhat tricky to
   detect it, but fortunately we can rely on the fact that the start
   of the sigtramp routine is page-aligned.  By the way, the mapping
   is read-only, so you cannot place a breakpoint in the signal
   trampoline.  */

/* Default page size.  */
static const int i386obsd_page_size = 4096;

/* Return whether PC is in an OpenBSD sigtramp routine.  */

static int
i386obsd_pc_in_sigtramp (CORE_ADDR pc, char *name)
{
  CORE_ADDR start_pc = (pc & ~(i386obsd_page_size - 1));
  const char sigreturn[] =
  {
    0xb8,
    0x67, 0x00, 0x00, 0x00,	/* movl $SYS_sigreturn, %eax */
    0xcd, 0x80			/* int $0x80 */
  };
  char *buf;

  /* Avoid reading memory from the target if possible.  If we're in a
     named function, we're certainly not in a sigtramp routine
     provided by the kernel.  Take synthetic function names into
     account though.  */
  if (name && name[0] != '<')
    return 0;

  /* If we can't read the instructions at START_PC, return zero.  */
  buf = alloca (sizeof sigreturn);
  if (target_read_memory (start_pc + 0x14, buf, sizeof sigreturn))
    return 0;

  /* Check for sigreturn(2).  */
  if (memcmp (buf, sigreturn, sizeof sigreturn) == 0)
    return 1;

  /* Check for a traditional BSD sigtramp routine.  */
  return i386bsd_pc_in_sigtramp (pc, name);
}

/* Return the start address of the sigtramp routine.  */

static CORE_ADDR
i386obsd_sigtramp_start (CORE_ADDR pc)
{
  CORE_ADDR start_pc = (pc & ~(i386obsd_page_size - 1));

  if (i386bsd_pc_in_sigtramp (pc, NULL))
    return i386bsd_sigtramp_start (pc);

  return start_pc;
}

/* Return the end address of the sigtramp routine.  */

static CORE_ADDR
i386obsd_sigtramp_end (CORE_ADDR pc)
{
  CORE_ADDR start_pc = (pc & ~(i386obsd_page_size - 1));

  if (i386bsd_pc_in_sigtramp (pc, NULL))
    return i386bsd_sigtramp_end (pc);

  return start_pc + 0x22;
}

/* Mapping between the general-purpose registers in `struct reg'
   format and GDB's register cache layout.  */

/* From <machine/reg.h>.  */
static int i386obsd_r_reg_offset[] =
{
  0 * 4,			/* %eax */
  1 * 4,			/* %ecx */
  2 * 4,			/* %edx */
  3 * 4,			/* %ebx */
  4 * 4,			/* %esp */
  5 * 4,			/* %ebp */
  6 * 4,			/* %esi */
  7 * 4,			/* %edi */
  8 * 4,			/* %eip */
  9 * 4,			/* %eflags */
  10 * 4,			/* %cs */
  11 * 4,			/* %ss */
  12 * 4,			/* %ds */
  13 * 4,			/* %es */
  14 * 4,			/* %fs */
  15 * 4			/* %gs */
};

static void
i386obsd_aout_supply_regset (const struct regset *regset,
			     struct regcache *regcache, int regnum,
			     const void *regs, size_t len)
{
  const struct gdbarch_tdep *tdep = regset->descr;

  gdb_assert (len >= tdep->sizeof_gregset + I387_SIZEOF_FSAVE);

  i386_supply_gregset (regset, regcache, regnum, regs, tdep->sizeof_gregset);
  i387_supply_fsave (regcache, regnum, (char *) regs + tdep->sizeof_gregset);
}

static const struct regset *
i386obsd_aout_regset_from_core_section (struct gdbarch *gdbarch,
					const char *sect_name,
					size_t sect_size)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* OpenBSD a.out core dumps don't use seperate register sets for the
     general-purpose and floating-point registers.  */

  if (strcmp (sect_name, ".reg") == 0
      && sect_size >= tdep->sizeof_gregset + I387_SIZEOF_FSAVE)
    {
      if (tdep->gregset == NULL)
	{
	  tdep->gregset = XMALLOC (struct regset);
	  tdep->gregset->descr = tdep;
	  tdep->gregset->supply_regset = i386obsd_aout_supply_regset;
	}
      return tdep->gregset;
    }

  return NULL;
}


/* Sigtramp routine location for OpenBSD 3.1 and earlier releases.  */
CORE_ADDR i386obsd_sigtramp_start_addr = 0xbfbfdf20;
CORE_ADDR i386obsd_sigtramp_end_addr = 0xbfbfdff0;

/* From <machine/signal.h>.  */
int i386obsd_sc_reg_offset[I386_NUM_GREGS] =
{
  10 * 4,			/* %eax */
  9 * 4,			/* %ecx */
  8 * 4,			/* %edx */
  7 * 4,			/* %ebx */
  14 * 4,			/* %esp */
  6 * 4,			/* %ebp */
  5 * 4,			/* %esi */
  4 * 4,			/* %edi */
  11 * 4,			/* %eip */
  13 * 4,			/* %eflags */
  12 * 4,			/* %cs */
  15 * 4,			/* %ss */
  3 * 4,			/* %ds */
  2 * 4,			/* %es */
  1 * 4,			/* %fs */
  0 * 4				/* %gs */
};

static void 
i386obsd_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* Obviously OpenBSD is BSD-based.  */
  i386bsd_init_abi (info, gdbarch);

  /* OpenBSD has a different `struct reg'.  */
  tdep->gregset_reg_offset = i386obsd_r_reg_offset;
  tdep->gregset_num_regs = ARRAY_SIZE (i386obsd_r_reg_offset);
  tdep->sizeof_gregset = 16 * 4;

  /* OpenBSD uses -freg-struct-return by default.  */
  tdep->struct_return = reg_struct_return;

  /* OpenBSD uses a different memory layout.  */
  tdep->sigtramp_start = i386obsd_sigtramp_start_addr;
  tdep->sigtramp_end = i386obsd_sigtramp_end_addr;
  set_gdbarch_pc_in_sigtramp (gdbarch, i386obsd_pc_in_sigtramp);
  set_gdbarch_sigtramp_start (gdbarch, i386obsd_sigtramp_start);
  set_gdbarch_sigtramp_end (gdbarch, i386obsd_sigtramp_end);

  /* OpenBSD has a `struct sigcontext' that's different from the
     origional 4.3 BSD.  */
  tdep->sc_reg_offset = i386obsd_sc_reg_offset;
  tdep->sc_num_regs = ARRAY_SIZE (i386obsd_sc_reg_offset);
}

/* OpenBSD a.out.  */

static void
i386obsd_aout_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  i386obsd_init_abi (info, gdbarch);

  /* OpenBSD a.out has a single register set.  */
  set_gdbarch_regset_from_core_section
    (gdbarch, i386obsd_aout_regset_from_core_section);
}

/* OpenBSD ELF.  */

static void
i386obsd_elf_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* It's still OpenBSD.  */
  i386obsd_init_abi (info, gdbarch);

  /* But ELF-based.  */
  i386_elf_init_abi (info, gdbarch);

  /* OpenBSD ELF uses SVR4-style shared libraries.  */
  set_gdbarch_in_solib_call_trampoline
    (gdbarch, generic_in_solib_call_trampoline);
  set_solib_svr4_fetch_link_map_offsets
    (gdbarch, svr4_ilp32_fetch_link_map_offsets);
}


/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_i386obsd_tdep (void);

void
_initialize_i386obsd_tdep (void)
{
  /* FIXME: kettenis/20021020: Since OpenBSD/i386 binaries are
     indistingushable from NetBSD/i386 a.out binaries, building a GDB
     that should support both these targets will probably not work as
     expected.  */
#define GDB_OSABI_OPENBSD_AOUT GDB_OSABI_NETBSD_AOUT

  gdbarch_register_osabi (bfd_arch_i386, 0, GDB_OSABI_OPENBSD_AOUT,
			  i386obsd_aout_init_abi);
  gdbarch_register_osabi (bfd_arch_i386, 0, GDB_OSABI_OPENBSD_ELF,
			  i386obsd_elf_init_abi);
}
