/* Target-dependent code for NetBSD/i386.

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

#include "gdb_assert.h"
#include "gdb_string.h"

#include "i386-tdep.h"
#include "i387-tdep.h"
#include "nbsd-tdep.h"
#include "solib-svr4.h"

/* From <machine/reg.h>.  */
static int i386nbsd_r_reg_offset[] =
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
i386nbsd_aout_supply_regset (const struct regset *regset,
			     struct regcache *regcache, int regnum,
			     const void *regs, size_t len)
{
  const struct gdbarch_tdep *tdep = regset->descr;

  gdb_assert (len >= tdep->sizeof_gregset + I387_SIZEOF_FSAVE);

  i386_supply_gregset (regset, regcache, regnum, regs, tdep->sizeof_gregset);
  i387_supply_fsave (regcache, regnum, (char *) regs + tdep->sizeof_gregset);
}

static const struct regset *
i386nbsd_aout_regset_from_core_section (struct gdbarch *gdbarch,
					const char *sect_name,
					size_t sect_size)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* NetBSD a.out core dumps don't use seperate register sets for the
     general-purpose and floating-point registers.  */

  if (strcmp (sect_name, ".reg") == 0
      && sect_size >= tdep->sizeof_gregset + I387_SIZEOF_FSAVE)
    {
      if (tdep->gregset == NULL)
	{
	  tdep->gregset = XMALLOC (struct regset);
	  tdep->gregset->descr = tdep;
	  tdep->gregset->supply_regset = i386nbsd_aout_supply_regset;
	}
      return tdep->gregset;
    }

  return NULL;
}

/* Under NetBSD/i386, signal handler invocations can be identified by the
   designated code sequence that is used to return from a signal handler.
   In particular, the return address of a signal handler points to the
   following code sequence:

	leal	0x10(%esp), %eax
	pushl	%eax
	pushl	%eax
	movl	$0x127, %eax		# __sigreturn14
	int	$0x80

   Each instruction has a unique encoding, so we simply attempt to match
   the instruction the PC is pointing to with any of the above instructions.
   If there is a hit, we know the offset to the start of the designated
   sequence and can then check whether we really are executing in the
   signal trampoline.  If not, -1 is returned, otherwise the offset from the
   start of the return sequence is returned.  */
#define RETCODE_INSN1		0x8d
#define RETCODE_INSN2		0x50
#define RETCODE_INSN3		0x50
#define RETCODE_INSN4		0xb8
#define RETCODE_INSN5		0xcd

#define RETCODE_INSN2_OFF	4
#define RETCODE_INSN3_OFF	5
#define RETCODE_INSN4_OFF	6
#define RETCODE_INSN5_OFF	11

static const unsigned char sigtramp_retcode[] =
{
  RETCODE_INSN1, 0x44, 0x24, 0x10,
  RETCODE_INSN2,
  RETCODE_INSN3,
  RETCODE_INSN4, 0x27, 0x01, 0x00, 0x00,
  RETCODE_INSN5, 0x80,
};

static LONGEST
i386nbsd_sigtramp_offset (CORE_ADDR pc)
{
  unsigned char ret[sizeof(sigtramp_retcode)], insn;
  LONGEST off;
  int i;

  if (read_memory_nobpt (pc, &insn, 1) != 0)
    return -1;

  switch (insn)
    {
    case RETCODE_INSN1:
      off = 0;
      break;

    case RETCODE_INSN2:
      /* INSN2 and INSN3 are the same.  Read at the location of PC+1
	 to determine if we're actually looking at INSN2 or INSN3.  */
      if (read_memory_nobpt (pc + 1, &insn, 1) != 0)
	return -1;

      if (insn == RETCODE_INSN3)
	off = RETCODE_INSN2_OFF;
      else
	off = RETCODE_INSN3_OFF;
      break;

    case RETCODE_INSN4:
      off = RETCODE_INSN4_OFF;
      break;

    case RETCODE_INSN5:
      off = RETCODE_INSN5_OFF;
      break;

    default:
      return -1;
    }

  pc -= off;

  if (read_memory_nobpt (pc, (char *) ret, sizeof (ret)) != 0)
    return -1;

  if (memcmp (ret, sigtramp_retcode, sizeof (ret)) == 0)
    return off;

  return -1;
}

static int
i386nbsd_pc_in_sigtramp (CORE_ADDR pc, char *name)
{
  return (nbsd_pc_in_sigtramp (pc, name)
	  || i386nbsd_sigtramp_offset (pc) >= 0);
}

/* From <machine/signal.h>.  */
int i386nbsd_sc_reg_offset[] =
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
i386nbsd_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* Obviously NetBSD is BSD-based.  */
  i386bsd_init_abi (info, gdbarch);

  /* NetBSD has a different `struct reg'.  */
  tdep->gregset_reg_offset = i386nbsd_r_reg_offset;
  tdep->gregset_num_regs = ARRAY_SIZE (i386nbsd_r_reg_offset);
  tdep->sizeof_gregset = 16 * 4;

  /* NetBSD has different signal trampoline conventions.  */
  set_gdbarch_pc_in_sigtramp (gdbarch, i386nbsd_pc_in_sigtramp);
  /* FIXME: kettenis/20020906: We should probably provide
     NetBSD-specific versions of these functions if we want to
     recognize signal trampolines that live on the stack.  */
  set_gdbarch_sigtramp_start (gdbarch, NULL);
  set_gdbarch_sigtramp_end (gdbarch, NULL);

  /* NetBSD uses -freg-struct-return by default.  */
  tdep->struct_return = reg_struct_return;

  /* NetBSD has a `struct sigcontext' that's different from the
     origional 4.3 BSD.  */
  tdep->sc_reg_offset = i386nbsd_sc_reg_offset;
  tdep->sc_num_regs = ARRAY_SIZE (i386nbsd_sc_reg_offset);
}

/* NetBSD a.out.  */

static void
i386nbsdaout_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  i386nbsd_init_abi (info, gdbarch);

  /* NetBSD a.out has a single register set.  */
  set_gdbarch_regset_from_core_section
    (gdbarch, i386nbsd_aout_regset_from_core_section);
}

/* NetBSD ELF.  */

static void
i386nbsdelf_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* It's still NetBSD.  */
  i386nbsd_init_abi (info, gdbarch);

  /* But ELF-based.  */
  i386_elf_init_abi (info, gdbarch);

  /* NetBSD ELF uses SVR4-style shared libraries.  */
  set_gdbarch_in_solib_call_trampoline
    (gdbarch, generic_in_solib_call_trampoline);
  set_solib_svr4_fetch_link_map_offsets
    (gdbarch, svr4_ilp32_fetch_link_map_offsets);

  /* NetBSD ELF uses -fpcc-struct-return by default.  */
  tdep->struct_return = pcc_struct_return;
}

void
_initialize_i386nbsd_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_i386, 0, GDB_OSABI_NETBSD_AOUT,
			  i386nbsdaout_init_abi);
  gdbarch_register_osabi (bfd_arch_i386, 0, GDB_OSABI_NETBSD_ELF,
			  i386nbsdelf_init_abi);
}
