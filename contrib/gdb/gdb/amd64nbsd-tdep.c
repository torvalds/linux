/* Target-dependent code for NetBSD/amd64.

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
#include "arch-utils.h"
#include "frame.h"
#include "gdbcore.h"
#include "osabi.h"

#include "gdb_assert.h"

#include "amd64-tdep.h"
#include "nbsd-tdep.h"
#include "solib-svr4.h"

/* Support for signal handlers.  */

/* Assuming NEXT_FRAME is for a frame following a BSD sigtramp
   routine, return the address of the associated sigcontext structure.  */

static CORE_ADDR
amd64nbsd_sigcontext_addr (struct frame_info *next_frame)
{
  CORE_ADDR sp;

  /* The stack pointer points at `struct sigcontext' upon entry of a
     signal trampoline.  */
  sp = frame_unwind_register_unsigned (next_frame, AMD64_RSP_REGNUM);
  return sp;
}

/* NetBSD 2.0 or later.  */

/* Mapping between the general-purpose registers in `struct reg'
   format and GDB's register cache layout.  */

/* From <machine/reg.h>.  */
int amd64nbsd_r_reg_offset[] =
{
  14 * 8,			/* %rax */
  13 * 8,			/* %rbx */
  3 * 8,			/* %rcx */
  2 * 8,			/* %rdx */
  1 * 8,			/* %rsi */
  0 * 8,			/* %rdi */
  12 * 8,			/* %rbp */
  24 * 8,			/* %rsp */
  4 * 8,			/* %r8 .. */
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
amd64nbsd_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  int *sc_reg_offset;
  int i;

  /* Initialize general-purpose register set details first.  */
  tdep->gregset_reg_offset = amd64nbsd_r_reg_offset;
  tdep->gregset_num_regs = ARRAY_SIZE (amd64nbsd_r_reg_offset);
  tdep->sizeof_gregset = 26 * 8;

  amd64_init_abi (info, gdbarch);

  tdep->jb_pc_offset = 7 * 8;

  /* NetBSD has its own convention for signal trampolines.  */
  set_gdbarch_pc_in_sigtramp (gdbarch, nbsd_pc_in_sigtramp);
  tdep->sigcontext_addr = amd64nbsd_sigcontext_addr;

  /* Initialize the array with register offsets in `struct
     sigcontext'.  This `struct sigcontext' has an sc_mcontext member
     at offset 32, and in <machine/reg.h> we have an explicit comment
     saying that `struct reg' is the same as mcontext.__gregs.  */
  tdep->sc_num_regs = ARRAY_SIZE (amd64nbsd_r_reg_offset);
  tdep->sc_reg_offset = XCALLOC (tdep->sc_num_regs, int);
  for (i = 0; i < tdep->sc_num_regs; i++)
    {
      if (amd64nbsd_r_reg_offset[i] < 0)
	tdep->sc_reg_offset[i] = -1;
      else
	tdep->sc_reg_offset[i] = 32 + amd64nbsd_r_reg_offset[i];
    }

  /* NetBSD uses SVR4-style shared libraries.  */
  set_solib_svr4_fetch_link_map_offsets
    (gdbarch, svr4_lp64_fetch_link_map_offsets);
}


/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_amd64nbsd_tdep (void);

void
_initialize_amd64nbsd_ndep (void)
{
  /* The NetBSD/amd64 native dependent code makes this assumption.  */
  gdb_assert (ARRAY_SIZE (amd64nbsd_r_reg_offset) == AMD64_NUM_GREGS);

  gdbarch_register_osabi (bfd_arch_i386, bfd_mach_x86_64,
			  GDB_OSABI_NETBSD_ELF, amd64nbsd_init_abi);
}
