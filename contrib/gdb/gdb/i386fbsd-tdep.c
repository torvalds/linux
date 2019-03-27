/* Target-dependent code for FreeBSD/i386.

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
#include "osabi.h"

#include "i386-tdep.h"
#include "i387-tdep.h"
#include "solib-svr4.h"

/* FreeBSD 3.0-RELEASE or later.  */

/* From <machine/reg.h>.  */
static int i386fbsd_r_reg_offset[] =
{
  9 * 4, 8 * 4, 7 * 4, 6 * 4,	/* %eax, %ecx, %edx, %ebx */
  15 * 4, 4 * 4,		/* %esp, %ebp */
  3 * 4, 2 * 4,			/* %esi, %edi */
  12 * 4, 14 * 4,		/* %eip, %eflags */
  13 * 4, 16 * 4,		/* %cs, %ss */
  1 * 4, 0 * 4, -1, -1		/* %ds, %es, %fs, %gs */
};

/* Sigtramp routine location.  */
CORE_ADDR i386fbsd_sigtramp_start_addr = 0xbfbfdf20;
CORE_ADDR i386fbsd_sigtramp_end_addr = 0xbfbfdff0;

/* From <machine/signal.h>.  */
static int i386fbsd_sc_reg_offset[] =
{
  8 + 14 * 4,			/* %eax */
  8 + 13 * 4,			/* %ecx */
  8 + 12 * 4,			/* %edx */
  8 + 11 * 4,			/* %ebx */
  8 + 0 * 4,                    /* %esp */
  8 + 1 * 4,                    /* %ebp */
  8 + 10 * 4,                   /* %esi */
  8 + 9 * 4,                    /* %edi */
  8 + 3 * 4,                    /* %eip */
  8 + 4 * 4,                    /* %eflags */
  8 + 7 * 4,                    /* %cs */
  8 + 8 * 4,                    /* %ss */
  8 + 6 * 4,                    /* %ds */
  8 + 5 * 4,                    /* %es */
  8 + 15 * 4,			/* %fs */
  8 + 16 * 4			/* %gs */
};

static void
i386fbsdaout_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* Obviously FreeBSD is BSD-based.  */
  i386bsd_init_abi (info, gdbarch);

  /* FreeBSD has a different `struct reg', and reserves some space for
     its FPU emulator in `struct fpreg'.  */
  tdep->gregset_reg_offset = i386fbsd_r_reg_offset;
  tdep->gregset_num_regs = ARRAY_SIZE (i386fbsd_r_reg_offset);
  tdep->sizeof_gregset = 18 * 4;
  tdep->sizeof_fpregset = 176;

  /* FreeBSD uses -freg-struct-return by default.  */
  tdep->struct_return = reg_struct_return;

  /* FreeBSD uses a different memory layout.  */
  tdep->sigtramp_start = i386fbsd_sigtramp_start_addr;
  tdep->sigtramp_end = i386fbsd_sigtramp_end_addr;

  /* FreeBSD has a more complete `struct sigcontext'.  */
  tdep->sc_reg_offset = i386fbsd_sc_reg_offset;
  tdep->sc_num_regs = ARRAY_SIZE (i386fbsd_sc_reg_offset);
}

static void
i386fbsd_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  /* It's almost identical to FreeBSD a.out.  */
  i386fbsdaout_init_abi (info, gdbarch);

  /* Except that it uses ELF.  */
  i386_elf_init_abi (info, gdbarch);

  /* FreeBSD ELF uses SVR4-style shared libraries.  */
  set_gdbarch_in_solib_call_trampoline
    (gdbarch, generic_in_solib_call_trampoline);
  set_solib_svr4_fetch_link_map_offsets
    (gdbarch, svr4_ilp32_fetch_link_map_offsets);
}

/* FreeBSD 4.0-RELEASE or later.  */

/* From <machine/reg.h>.  */
static int i386fbsd4_r_reg_offset[] =
{
  10 * 4, 9 * 4, 8 * 4, 7 * 4,	/* %eax, %ecx, %edx, %ebx */
  16 * 4, 5 * 4,		/* %esp, %ebp */
  4 * 4, 3 * 4,			/* %esi, %edi */
  13 * 4, 15 * 4,		/* %eip, %eflags */
  14 * 4, 17 * 4,		/* %cs, %ss */
  2 * 4, 1 * 4, 0 * 4, 18 * 4	/* %ds, %es, %fs, %gs */
};

/* From <machine/signal.h>.  */
int i386fbsd4_sc_reg_offset[] =
{
  20 + 11 * 4,			/* %eax */
  20 + 10 * 4,			/* %ecx */
  20 + 9 * 4,			/* %edx */
  20 + 8 * 4,			/* %ebx */
  20 + 17 * 4,			/* %esp */
  20 + 6 * 4,			/* %ebp */
  20 + 5 * 4,			/* %esi */
  20 + 4 * 4,			/* %edi */
  20 + 14 * 4,			/* %eip */
  20 + 16 * 4,			/* %eflags */
  20 + 15 * 4,			/* %cs */
  20 + 18 * 4,			/* %ss */
  20 + 3 * 4,			/* %ds */
  20 + 2 * 4,			/* %es */
  20 + 1 * 4,			/* %fs */
  20 + 0 * 4			/* %gs */
};

static void
i386fbsd4_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* Inherit stuff from older releases.  We assume that FreeBSD
     4.0-RELEASE always uses ELF.  */
  i386fbsd_init_abi (info, gdbarch);

  /* FreeBSD 4.0 introduced a new `struct reg'.  */
  tdep->gregset_reg_offset = i386fbsd4_r_reg_offset;
  tdep->gregset_num_regs = ARRAY_SIZE (i386fbsd4_r_reg_offset);
  tdep->sizeof_gregset = 19 * 4;

  /* FreeBSD 4.0 introduced a new `struct sigcontext'.  */
  tdep->sc_reg_offset = i386fbsd4_sc_reg_offset;
  tdep->sc_num_regs = ARRAY_SIZE (i386fbsd4_sc_reg_offset);
}


/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_i386fbsd_tdep (void);

void
_initialize_i386fbsd_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_i386, 0, GDB_OSABI_FREEBSD_AOUT,
			  i386fbsdaout_init_abi);
  gdbarch_register_osabi (bfd_arch_i386, 0, GDB_OSABI_FREEBSD_ELF,
			  i386fbsd4_init_abi);
}
