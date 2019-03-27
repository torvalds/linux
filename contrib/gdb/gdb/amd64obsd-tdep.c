/* Target-dependent code for OpenBSD/amd64.

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
#include "gdbcore.h"
#include "osabi.h"
#include "regset.h"
#include "target.h"

#include "gdb_assert.h"
#include "gdb_string.h"

#include "amd64-tdep.h"
#include "i387-tdep.h"
#include "solib-svr4.h"

/* Support for core dumps.  */

static void
amd64obsd_supply_regset (const struct regset *regset,
			 struct regcache *regcache, int regnum,
			 const void *regs, size_t len)
{
  const struct gdbarch_tdep *tdep = regset->descr;

  gdb_assert (len >= tdep->sizeof_gregset + I387_SIZEOF_FXSAVE);

  i386_supply_gregset (regset, regcache, regnum, regs, tdep->sizeof_gregset);
  amd64_supply_fxsave (regcache, regnum, (char *)regs + tdep->sizeof_gregset);
}

static const struct regset *
amd64obsd_regset_from_core_section (struct gdbarch *gdbarch,
				    const char *sect_name, size_t sect_size)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* OpenBSD core dumps don't use seperate register sets for the
     general-purpose and floating-point registers.  */

  if (strcmp (sect_name, ".reg") == 0
      && sect_size >= tdep->sizeof_gregset + I387_SIZEOF_FXSAVE)
    {
      if (tdep->gregset == NULL)
	{
	  tdep->gregset = XMALLOC (struct regset);
	  tdep->gregset->descr = tdep;
	  tdep->gregset->supply_regset = amd64obsd_supply_regset;
	}
      return tdep->gregset;
    }

  return NULL;
}


/* Support for signal handlers.  */

static const int amd64obsd_page_size = 4096;

static int
amd64obsd_pc_in_sigtramp (CORE_ADDR pc, char *name)
{
  CORE_ADDR start_pc = (pc & ~(amd64obsd_page_size - 1));
  const char sigreturn[] =
  {
    0x48, 0xc7, 0xc0,
    0x67, 0x00, 0x00, 0x00,	/* movq $SYS_sigreturn, %rax */
    0xcd, 0x80			/* int $0x80 */
  };
  char *buf;

  if (name)
    return 0;

  /* If we can't read the instructions at START_PC, return zero.  */
  buf = alloca (sizeof sigreturn);
  if (target_read_memory (start_pc + 0x7, buf, sizeof sigreturn))
    return 0;

  /* Check for sigreturn(2).  */
  if (memcmp (buf, sigreturn, sizeof sigreturn))
    return 0;

  return 1;
}

/* Assuming NEXT_FRAME is for a frame following a BSD sigtramp
   routine, return the address of the associated sigcontext structure.  */

static CORE_ADDR
amd64obsd_sigcontext_addr (struct frame_info *next_frame)
{
  /* The %rsp register points at `struct sigcontext' upon entry of a
     signal trampoline.  */
  return frame_unwind_register_unsigned (next_frame, AMD64_RSP_REGNUM);
}

/* OpenBSD 3.5 or later.  */

/* Mapping between the general-purpose registers in `struct reg'
   format and GDB's register cache layout.  */

/* From <machine/reg.h>.  */
int amd64obsd_r_reg_offset[] =
{
  14 * 8,			/* %rax */
  13 * 8,			/* %rbx */
  3 * 8,			/* %rcx */
  2 * 8,			/* %rdx */
  1 * 8,			/* %rsi */
  0 * 8,			/* %rdi */
  12 * 8,			/* %rbp */
  15 * 8,			/* %rsp */
  4 * 8,			/* %r8 .. */
  5 * 8,
  6 * 8,
  7 * 8,
  8 * 8,
  9 * 8,
  10 * 8,
  11 * 8,			/* ... %r15 */
  16 * 8,			/* %rip */
  17 * 8,			/* %eflags */
  18 * 8,			/* %cs */
  19 * 8,			/* %ss */
  20 * 8,			/* %ds */
  21 * 8,			/* %es */
  22 * 8,			/* %fs */
  23 * 8			/* %gs */
};

/* From <machine/signal.h>.  */
static int amd64obsd_sc_reg_offset[] =
{
  14 * 8,			/* %rax */
  13 * 8,			/* %rbx */
  3 * 8,			/* %rcx */
  2 * 8,			/* %rdx */
  1 * 8,			/* %rsi */
  0 * 8,			/* %rdi */
  12 * 8,			/* %rbp */
  24 * 8,			/* %rsp */
  4 * 8,			/* %r8 ... */
  5 * 8,
  6 * 8,
  7 * 8,
  8 * 8,
  9 * 8,
  10 * 8,
  11 * 8,			/* ... %r15 */
  21 * 8,			/* %rip */
  23 * 8,			/* %eflags */
  22 * 8,			/* %cs */
  25 * 8,			/* %ss */
  18 * 8,			/* %ds */
  17 * 8,			/* %es */
  16 * 8,			/* %fs */
  15 * 8			/* %gs */
};

static void
amd64obsd_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  amd64_init_abi (info, gdbarch);

  /* Initialize general-purpose register set details.  */
  tdep->gregset_reg_offset = amd64obsd_r_reg_offset;
  tdep->gregset_num_regs = ARRAY_SIZE (amd64obsd_r_reg_offset);
  tdep->sizeof_gregset = 24 * 8;

  set_gdbarch_regset_from_core_section (gdbarch,
					amd64obsd_regset_from_core_section);

  tdep->jb_pc_offset = 7 * 8;

  set_gdbarch_pc_in_sigtramp (gdbarch, amd64obsd_pc_in_sigtramp);
  tdep->sigcontext_addr = amd64obsd_sigcontext_addr;
  tdep->sc_reg_offset = amd64obsd_sc_reg_offset;
  tdep->sc_num_regs = ARRAY_SIZE (amd64obsd_sc_reg_offset);

  /* OpenBSD uses SVR4-style shared libraries.  */
  set_solib_svr4_fetch_link_map_offsets
    (gdbarch, svr4_lp64_fetch_link_map_offsets);
}


/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_amd64obsd_tdep (void);

void
_initialize_amd64obsd_tdep (void)
{
  /* The OpenBSD/amd64 native dependent code makes this assumption.  */
  gdb_assert (ARRAY_SIZE (amd64obsd_r_reg_offset) == AMD64_NUM_GREGS);

  gdbarch_register_osabi (bfd_arch_i386, bfd_mach_x86_64,
			  GDB_OSABI_OPENBSD_ELF, amd64obsd_init_abi);

  /* OpenBSD uses traditional (a.out) NetBSD-style core dumps.  */
  gdbarch_register_osabi (bfd_arch_i386, bfd_mach_x86_64,
			  GDB_OSABI_NETBSD_AOUT, amd64obsd_init_abi);
}
