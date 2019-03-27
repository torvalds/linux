/* i386-nto-tdep.c - i386 specific functionality for QNX Neutrino.

   Copyright 2003 Free Software Foundation, Inc.

   Contributed by QNX Software Systems Ltd.

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

#include "gdb_string.h"
#include "gdb_assert.h"
#include "defs.h"
#include "frame.h"
#include "target.h"
#include "regcache.h"
#include "solib-svr4.h"
#include "i386-tdep.h"
#include "nto-tdep.h"
#include "osabi.h"
#include "i387-tdep.h"

#ifndef X86_CPU_FXSR
#define X86_CPU_FXSR (1L << 12)
#endif

/* Why 13?  Look in our /usr/include/x86/context.h header at the
   x86_cpu_registers structure and you'll see an 'exx' junk register
   that is just filler.  Don't ask me, ask the kernel guys.  */
#define NUM_GPREGS 13

/* Map a GDB register number to an offset in the reg structure.  */
static int regmap[] = {
  (7 * 4),			/* eax */
  (6 * 4),			/* ecx */
  (5 * 4),			/* edx */
  (4 * 4),			/* ebx */
  (11 * 4),			/* esp */
  (2 * 4),			/* epb */
  (1 * 4),			/* esi */
  (0 * 4),			/* edi */
  (8 * 4),			/* eip */
  (10 * 4),			/* eflags */
  (9 * 4),			/* cs */
  (12 * 4),			/* ss */
  (-1 * 4)			/* filler */
};

/* Given a gdb regno, return the offset into Neutrino's register structure
   or -1 if register is unknown.  */
static int
nto_reg_offset (int regno)
{
  return (regno >= 0 && regno < NUM_GPREGS) ? regmap[regno] : -1;
}

static void
i386nto_supply_gregset (char *gpregs)
{
  unsigned regno;
  int empty = 0;

  for (regno = 0; regno < FP0_REGNUM; regno++)
    {
      int offset = nto_reg_offset (regno);
      if (offset == -1)
	supply_register (regno, (char *) &empty);
      else
	supply_register (regno, gpregs + offset);
    }
}

static void
i386nto_supply_fpregset (char *fpregs)
{
  if (nto_cpuinfo_valid && nto_cpuinfo_flags | X86_CPU_FXSR)
    i387_supply_fxsave (current_regcache, -1, fpregs);
  else
    i387_supply_fsave (current_regcache, -1, fpregs);
}

static void
i386nto_supply_regset (int regset, char *data)
{
  switch (regset)
    {
    case NTO_REG_GENERAL:	/* QNX has different ordering of GP regs than GDB.  */
      i386nto_supply_gregset (data);
      break;
    case NTO_REG_FLOAT:
      i386nto_supply_fpregset (data);
      break;
    }
}

static int
i386nto_regset_id (int regno)
{
  if (regno == -1)
    return NTO_REG_END;
  else if (regno < FP0_REGNUM)
    return NTO_REG_GENERAL;
  else if (regno < FPC_REGNUM)
    return NTO_REG_FLOAT;

  return -1;			/* Error.  */
}

static int
i386nto_register_area (int regno, int regset, unsigned *off)
{
  int len;

  *off = 0;
  if (regset == NTO_REG_GENERAL)
    {
      if (regno == -1)
	return NUM_GPREGS * 4;

      *off = nto_reg_offset (regno);
      if (*off == -1)
	return 0;
      return 4;
    }
  else if (regset == NTO_REG_FLOAT)
    {
      unsigned off_adjust, regsize, regset_size;

      if (nto_cpuinfo_valid && nto_cpuinfo_flags | X86_CPU_FXSR)
	{
	  off_adjust = 32;
	  regsize = 16;
	  regset_size = 512;
	}
      else
	{
	  off_adjust = 28;
	  regsize = 10;
	  regset_size = 128;
	}

      if (regno == -1)
	return regset_size;

      *off = (regno - FP0_REGNUM) * regsize + off_adjust;
      return 10;
      /* Why 10 instead of regsize?  GDB only stores 10 bytes per FP
         register so if we're sending a register back to the target,
         we only want pdebug to write 10 bytes so as not to clobber
         the reserved 6 bytes in the fxsave structure.  */
    }
  return -1;
}

static int
i386nto_regset_fill (int regset, char *data)
{
  if (regset == NTO_REG_GENERAL)
    {
      int regno;

      for (regno = 0; regno < NUM_GPREGS; regno++)
	{
	  int offset = nto_reg_offset (regno);
	  if (offset != -1)
	    regcache_collect (regno, data + offset);
	}
    }
  else if (regset == NTO_REG_FLOAT)
    {
      if (nto_cpuinfo_valid && nto_cpuinfo_flags | X86_CPU_FXSR)
	i387_fill_fxsave (data, -1);
      else
	i387_fill_fsave (data, -1);
    }
  else
    return -1;

  return 0;
}

static struct link_map_offsets *
i386nto_svr4_fetch_link_map_offsets (void)
{
  static struct link_map_offsets lmo;
  static struct link_map_offsets *lmp = NULL;

  if (lmp == NULL)
    {
      lmp = &lmo;

      lmo.r_debug_size = 8;	/* The actual size is 20 bytes, but
				   only 8 bytes are used.  */
      lmo.r_map_offset = 4;
      lmo.r_map_size = 4;

      lmo.link_map_size = 20;	/* The actual size is 552 bytes, but
				   only 20 bytes are used.  */
      lmo.l_addr_offset = 0;
      lmo.l_addr_size = 4;

      lmo.l_name_offset = 4;
      lmo.l_name_size = 4;

      lmo.l_next_offset = 12;
      lmo.l_next_size = 4;

      lmo.l_prev_offset = 16;
      lmo.l_prev_size = 4;
    }

  return lmp;
}

static int
i386nto_pc_in_sigtramp (CORE_ADDR pc, char *name)
{
  return name && strcmp ("__signalstub", name) == 0;
}

#define I386_NTO_SIGCONTEXT_OFFSET 136

/* Assuming NEXT_FRAME is a frame following a QNX Neutrino sigtramp
   routine, return the address of the associated sigcontext structure.  */

static CORE_ADDR
i386nto_sigcontext_addr (struct frame_info *next_frame)
{
  char buf[4];
  CORE_ADDR sp;

  frame_unwind_register (next_frame, SP_REGNUM, buf);
  sp = extract_unsigned_integer (buf, 4);

  return sp + I386_NTO_SIGCONTEXT_OFFSET;
}

static void
init_i386nto_ops (void)
{
  current_nto_target.nto_regset_id = i386nto_regset_id;
  current_nto_target.nto_supply_gregset = i386nto_supply_gregset;
  current_nto_target.nto_supply_fpregset = i386nto_supply_fpregset;
  current_nto_target.nto_supply_altregset = nto_dummy_supply_regset;
  current_nto_target.nto_supply_regset = i386nto_supply_regset;
  current_nto_target.nto_register_area = i386nto_register_area;
  current_nto_target.nto_regset_fill = i386nto_regset_fill;
  current_nto_target.nto_fetch_link_map_offsets =
    i386nto_svr4_fetch_link_map_offsets;
}

static void
i386nto_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* NTO uses ELF.  */
  i386_elf_init_abi (info, gdbarch);

  /* Neutrino rewinds to look more normal.  Need to override the i386
     default which is [unfortunately] to decrement the PC.  */
  set_gdbarch_decr_pc_after_break (gdbarch, 0);

  /* NTO has shared libraries.  */
  set_gdbarch_in_solib_call_trampoline (gdbarch, in_plt_section);
  set_gdbarch_skip_trampoline_code (gdbarch, find_solib_trampoline_target);

  set_gdbarch_pc_in_sigtramp (gdbarch, i386nto_pc_in_sigtramp);
  tdep->sigcontext_addr = i386nto_sigcontext_addr;
  tdep->sc_pc_offset = 56;
  tdep->sc_sp_offset = 68;

  /* Setjmp()'s return PC saved in EDX (5).  */
  tdep->jb_pc_offset = 20;	/* 5x32 bit ints in.  */

  set_solib_svr4_fetch_link_map_offsets (gdbarch,
					 i386nto_svr4_fetch_link_map_offsets);

  /* Our loader handles solib relocations slightly differently than svr4.  */
  TARGET_SO_RELOCATE_SECTION_ADDRESSES = nto_relocate_section_addresses;

  /* Supply a nice function to find our solibs.  */
  TARGET_SO_FIND_AND_OPEN_SOLIB = nto_find_and_open_solib;

  init_i386nto_ops ();
}

void
_initialize_i386nto_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_i386, 0, GDB_OSABI_QNXNTO,
			  i386nto_init_abi);
}
