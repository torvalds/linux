/* Target-dependent code for NetBSD/Alpha.

   Copyright 2002, 2003, 2004 Free Software Foundation, Inc.
   Contributed by Wasabi Systems, Inc.

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
#include "gdbcore.h"
#include "frame.h"
#include "regcache.h"
#include "value.h"
#include "osabi.h"

#include "solib-svr4.h"

#include "alpha-tdep.h"
#include "alphabsd-tdep.h"
#include "nbsd-tdep.h"

static void
fetch_core_registers (char *core_reg_sect, unsigned core_reg_size, int which,
                      CORE_ADDR ignore)
{
  char *regs, *fpregs;
  int regno;

  /* Table to map a gdb register number to a trapframe register index.  */
  static const int regmap[] =
  {
     0,   1,   2,   3,
     4,   5,   6,   7,
     8,   9,  10,  11,
    12,  13,  14,  15, 
    30,  31,  32,  16, 
    17,  18,  19,  20,
    21,  22,  23,  24,
    25,  29,  26
  };
#define SIZEOF_TRAPFRAME (33 * 8)

  /* We get everything from one section.  */
  if (which != 0)
    return;

  regs = core_reg_sect;
  fpregs = core_reg_sect + SIZEOF_TRAPFRAME;

  if (core_reg_size < (SIZEOF_TRAPFRAME + SIZEOF_STRUCT_FPREG))
    {
      warning ("Wrong size register set in core file.");
      return;
    }

  /* Integer registers.  */
  for (regno = 0; regno < ALPHA_ZERO_REGNUM; regno++)
    supply_register (regno, regs + (regmap[regno] * 8));
  supply_register (ALPHA_ZERO_REGNUM, NULL);
  supply_register (PC_REGNUM, regs + (28 * 8));

  /* Floating point registers.  */
  alphabsd_supply_fpreg (fpregs, -1);
}

static void
fetch_elfcore_registers (char *core_reg_sect, unsigned core_reg_size, int which,
                         CORE_ADDR ignore)
{
  switch (which)
    {
    case 0:  /* Integer registers.  */
      if (core_reg_size != SIZEOF_STRUCT_REG)
	warning ("Wrong size register set in core file.");
      else
	alphabsd_supply_reg (core_reg_sect, -1);
      break;

    case 2:  /* Floating point registers.  */
      if (core_reg_size != SIZEOF_STRUCT_FPREG)
	warning ("Wrong size FP register set in core file.");
      else
	alphabsd_supply_fpreg (core_reg_sect, -1);
      break;

    default:
      /* Don't know what kind of register request this is; just ignore it.  */
      break;
    }
}

static struct core_fns alphanbsd_core_fns =
{
  bfd_target_unknown_flavour,		/* core_flavour */
  default_check_format,			/* check_format */
  default_core_sniffer,			/* core_sniffer */
  fetch_core_registers,			/* core_read_registers */
  NULL					/* next */
};

static struct core_fns alphanbsd_elfcore_fns =
{
  bfd_target_elf_flavour,		/* core_flavour */
  default_check_format,			/* check_format */
  default_core_sniffer,			/* core_sniffer */
  fetch_elfcore_registers,		/* core_read_registers */
  NULL					/* next */
};

/* Under NetBSD/alpha, signal handler invocations can be identified by the
   designated code sequence that is used to return from a signal handler.
   In particular, the return address of a signal handler points to the
   following code sequence:

	ldq	a0, 0(sp)
	lda	sp, 16(sp)
	lda	v0, 295(zero)	# __sigreturn14
	call_pal callsys

   Each instruction has a unique encoding, so we simply attempt to match
   the instruction the PC is pointing to with any of the above instructions.
   If there is a hit, we know the offset to the start of the designated
   sequence and can then check whether we really are executing in the
   signal trampoline.  If not, -1 is returned, otherwise the offset from the
   start of the return sequence is returned.  */
static const unsigned char sigtramp_retcode[] =
{
  0x00, 0x00, 0x1e, 0xa6,	/* ldq a0, 0(sp) */
  0x10, 0x00, 0xde, 0x23,	/* lda sp, 16(sp) */
  0x27, 0x01, 0x1f, 0x20,	/* lda v0, 295(zero) */
  0x83, 0x00, 0x00, 0x00,	/* call_pal callsys */
};
#define RETCODE_NWORDS		4
#define RETCODE_SIZE		(RETCODE_NWORDS * 4)

LONGEST
alphanbsd_sigtramp_offset (CORE_ADDR pc)
{
  unsigned char ret[RETCODE_SIZE], w[4];
  LONGEST off;
  int i;

  if (read_memory_nobpt (pc, (char *) w, 4) != 0)
    return -1;

  for (i = 0; i < RETCODE_NWORDS; i++)
    {
      if (memcmp (w, sigtramp_retcode + (i * 4), 4) == 0)
	break;
    }
  if (i == RETCODE_NWORDS)
    return (-1);

  off = i * 4;
  pc -= off;

  if (read_memory_nobpt (pc, (char *) ret, sizeof (ret)) != 0)
    return -1;

  if (memcmp (ret, sigtramp_retcode, RETCODE_SIZE) == 0)
    return off;

  return -1;
}

static int
alphanbsd_pc_in_sigtramp (CORE_ADDR pc, char *func_name)
{
  return (nbsd_pc_in_sigtramp (pc, func_name)
	  || alphanbsd_sigtramp_offset (pc) >= 0);
}

static CORE_ADDR
alphanbsd_sigcontext_addr (struct frame_info *frame)
{
  /* FIXME: This is not correct for all versions of NetBSD/alpha.
     We will probably need to disassemble the trampoline to figure
     out which trampoline frame type we have.  */
  return get_frame_base (frame);
}

static void
alphanbsd_init_abi (struct gdbarch_info info,
                    struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* Hook into the DWARF CFI frame unwinder.  */
  alpha_dwarf2_init_abi (info, gdbarch);

  /* Hook into the MDEBUG frame unwinder.  */
  alpha_mdebug_init_abi (info, gdbarch);

  set_gdbarch_pc_in_sigtramp (gdbarch, alphanbsd_pc_in_sigtramp);

  /* NetBSD/alpha does not provide single step support via ptrace(2); we
     must use software single-stepping.  */
  set_gdbarch_software_single_step (gdbarch, alpha_software_single_step);

  set_solib_svr4_fetch_link_map_offsets (gdbarch,
                                 nbsd_lp64_solib_svr4_fetch_link_map_offsets);

  tdep->dynamic_sigtramp_offset = alphanbsd_sigtramp_offset;
  tdep->sigcontext_addr = alphanbsd_sigcontext_addr;

  tdep->jb_pc = 2;
  tdep->jb_elt_size = 8;
}

void
_initialize_alphanbsd_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_alpha, 0, GDB_OSABI_NETBSD_ELF,
                          alphanbsd_init_abi);
  gdbarch_register_osabi (bfd_arch_alpha, 0, GDB_OSABI_OPENBSD_ELF,
                          alphanbsd_init_abi);

  add_core_fns (&alphanbsd_core_fns);
  add_core_fns (&alphanbsd_elfcore_fns);
}
