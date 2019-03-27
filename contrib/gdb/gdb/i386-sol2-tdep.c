/* Target-dependent code for Solaris x86.
   Copyright 2002, 2003 Free Software Foundation, Inc.

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
#include "value.h"
#include "osabi.h"

#include "i386-tdep.h"

/* From <ia32/sys/reg.h>.  */
static int i386_sol2_gregset_reg_offset[] =
{
  11 * 4,			/* %eax */
  10 * 4,			/* %ecx */
  9 * 4,			/* %edx */
  8 * 4,			/* %ebx */
  17 * 4,			/* %esp */
  6 * 4,			/* %ebp */
  5 * 4,			/* %esi */
  4 * 4,			/* %edi */
  14 * 4,			/* %eip */
  16 * 4,			/* %eflags */
  15 * 4,			/* %cs */
  18 * 4,			/* %ss */
  3 * 4,			/* %ds */
  2 * 4,			/* %es */
  1 * 4,			/* %fs */
  0 * 4				/* %gs */
};

static int
i386_sol2_pc_in_sigtramp (CORE_ADDR pc, char *name)
{
  /* Signal handler frames under Solaris 2 are recognized by a return
     address of 0xffffffff.  */
  return (pc == 0xffffffff);
}

/* Solaris doesn't have a `struct sigcontext', but it does have a
   `mcontext_t' that contains the saved set of machine registers.  */

static CORE_ADDR
i386_sol2_mcontext_addr (struct frame_info *next_frame)
{
  CORE_ADDR sp, ucontext_addr;

  sp = frame_unwind_register_unsigned (next_frame, I386_ESP_REGNUM);
  ucontext_addr = get_frame_memory_unsigned (next_frame, sp + 8, 4);

  return ucontext_addr + 36;
}

/* Solaris 2.  */

static void
i386_sol2_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* Solaris is SVR4-based.  */
  i386_svr4_init_abi (info, gdbarch);

  /* Solaris reserves space for its FPU emulator in `fpregset_t'.
     There is also some space reserved for the registers of a Weitek
     math coprocessor.  */
  tdep->gregset_reg_offset = i386_sol2_gregset_reg_offset;
  tdep->gregset_num_regs = ARRAY_SIZE (i386_sol2_gregset_reg_offset);
  tdep->sizeof_gregset = 19 * 4;
  tdep->sizeof_fpregset = 380;

  tdep->sigcontext_addr = i386_sol2_mcontext_addr;
  tdep->sc_reg_offset = tdep->gregset_reg_offset;
  tdep->sc_num_regs = tdep->gregset_num_regs;

  /* Signal trampolines are slightly different from SVR4.  */
  set_gdbarch_pc_in_sigtramp (gdbarch, i386_sol2_pc_in_sigtramp);
}


static enum gdb_osabi
i386_sol2_osabi_sniffer (bfd *abfd)
{
  /* If we have a section named .SUNW_version, then it is almost
     certainly Solaris 2.  */
  if (bfd_get_section_by_name (abfd, ".SUNW_version"))
    return GDB_OSABI_SOLARIS;

  return GDB_OSABI_UNKNOWN;
}

/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_i386_sol2_tdep (void);

void
_initialize_i386_sol2_tdep (void)
{
  /* Register an ELF OS ABI sniffer for Solaris 2 binaries.  */
  gdbarch_register_osabi_sniffer (bfd_arch_i386, bfd_target_elf_flavour,
				  i386_sol2_osabi_sniffer);

  gdbarch_register_osabi (bfd_arch_i386, 0, GDB_OSABI_SOLARIS,
			  i386_sol2_init_abi);
}
