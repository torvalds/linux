/* Target-dependent code for MIPS systems running NetBSD.
   Copyright 2002, 2003 Free Software Foundation, Inc.
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
#include "regcache.h"
#include "target.h"
#include "value.h"
#include "osabi.h"

#include "nbsd-tdep.h"
#include "mipsnbsd-tdep.h"

#include "solib-svr4.h"

/* Conveniently, GDB uses the same register numbering as the
   ptrace register structure used by NetBSD/mips.  */

void
mipsnbsd_supply_reg (char *regs, int regno)
{
  int i;

  for (i = 0; i <= PC_REGNUM; i++)
    {
      if (regno == i || regno == -1)
	{
	  if (CANNOT_FETCH_REGISTER (i))
	    supply_register (i, NULL);
	  else
            supply_register (i, regs + (i * mips_regsize (current_gdbarch)));
        }
    }
}

void
mipsnbsd_fill_reg (char *regs, int regno)
{
  int i;

  for (i = 0; i <= PC_REGNUM; i++)
    if ((regno == i || regno == -1) && ! CANNOT_STORE_REGISTER (i))
      regcache_collect (i, regs + (i * mips_regsize (current_gdbarch)));
}

void
mipsnbsd_supply_fpreg (char *fpregs, int regno)
{
  int i;

  for (i = FP0_REGNUM;
       i <= mips_regnum (current_gdbarch)->fp_implementation_revision;
       i++)
    {
      if (regno == i || regno == -1)
	{
	  if (CANNOT_FETCH_REGISTER (i))
	    supply_register (i, NULL);
	  else
            supply_register (i, fpregs + ((i - FP0_REGNUM) * mips_regsize (current_gdbarch)));
	}
    }
}

void
mipsnbsd_fill_fpreg (char *fpregs, int regno)
{
  int i;

  for (i = FP0_REGNUM; i <= mips_regnum (current_gdbarch)->fp_control_status;
       i++)
    if ((regno == i || regno == -1) && ! CANNOT_STORE_REGISTER (i))
      regcache_collect (i, fpregs + ((i - FP0_REGNUM) * mips_regsize (current_gdbarch)));
}

static void
fetch_core_registers (char *core_reg_sect, unsigned core_reg_size, int which,
                      CORE_ADDR ignore)
{
  char *regs, *fpregs;

  /* We get everything from one section.  */
  if (which != 0)
    return;

  regs = core_reg_sect;
  fpregs = core_reg_sect + SIZEOF_STRUCT_REG;

  /* Integer registers.  */
  mipsnbsd_supply_reg (regs, -1);

  /* Floating point registers.  */
  mipsnbsd_supply_fpreg (fpregs, -1);
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
	mipsnbsd_supply_reg (core_reg_sect, -1);
      break;

    case 2:  /* Floating point registers.  */
      if (core_reg_size != SIZEOF_STRUCT_FPREG)
	warning ("Wrong size register set in core file.");
      else
	mipsnbsd_supply_fpreg (core_reg_sect, -1);
      break;

    default:
      /* Don't know what kind of register request this is; just ignore it.  */
      break;
    }
}

static struct core_fns mipsnbsd_core_fns =
{
  bfd_target_unknown_flavour,		/* core_flavour */
  default_check_format,			/* check_format */
  default_core_sniffer,			/* core_sniffer */
  fetch_core_registers,			/* core_read_registers */
  NULL					/* next */
};

static struct core_fns mipsnbsd_elfcore_fns =
{
  bfd_target_elf_flavour,		/* core_flavour */
  default_check_format,			/* check_format */
  default_core_sniffer,			/* core_sniffer */
  fetch_elfcore_registers,		/* core_read_registers */
  NULL					/* next */
};

/* Under NetBSD/mips, signal handler invocations can be identified by the
   designated code sequence that is used to return from a signal handler.
   In particular, the return address of a signal handler points to the
   following code sequence:

	addu	a0, sp, 16
	li	v0, 295			# __sigreturn14
	syscall
   
   Each instruction has a unique encoding, so we simply attempt to match
   the instruction the PC is pointing to with any of the above instructions.
   If there is a hit, we know the offset to the start of the designated
   sequence and can then check whether we really are executing in the
   signal trampoline.  If not, -1 is returned, otherwise the offset from the
   start of the return sequence is returned.  */

#define RETCODE_NWORDS	3
#define RETCODE_SIZE	(RETCODE_NWORDS * 4)

static const unsigned char sigtramp_retcode_mipsel[RETCODE_SIZE] =
{
  0x10, 0x00, 0xa4, 0x27,	/* addu a0, sp, 16 */
  0x27, 0x01, 0x02, 0x24,	/* li v0, 295 */
  0x0c, 0x00, 0x00, 0x00,	/* syscall */
};

static const unsigned char sigtramp_retcode_mipseb[RETCODE_SIZE] =
{
  0x27, 0xa4, 0x00, 0x10,	/* addu a0, sp, 16 */
  0x24, 0x02, 0x01, 0x27,	/* li v0, 295 */
  0x00, 0x00, 0x00, 0x0c,	/* syscall */
};

static LONGEST
mipsnbsd_sigtramp_offset (CORE_ADDR pc)
{
  const char *retcode = TARGET_BYTE_ORDER == BFD_ENDIAN_BIG
  	? sigtramp_retcode_mipseb : sigtramp_retcode_mipsel;
  unsigned char ret[RETCODE_SIZE], w[4];
  LONGEST off;
  int i;

  if (read_memory_nobpt (pc, (char *) w, sizeof (w)) != 0)
    return -1;

  for (i = 0; i < RETCODE_NWORDS; i++)
    {
      if (memcmp (w, retcode + (i * 4), 4) == 0)
	break;
    }
  if (i == RETCODE_NWORDS)
    return -1;

  off = i * 4;
  pc -= off;

  if (read_memory_nobpt (pc, (char *) ret, sizeof (ret)) != 0)
    return -1;

  if (memcmp (ret, retcode, RETCODE_SIZE) == 0)
    return off;

  return -1;
}

static int
mipsnbsd_pc_in_sigtramp (CORE_ADDR pc, char *func_name)
{
  return (nbsd_pc_in_sigtramp (pc, func_name)
	  || mipsnbsd_sigtramp_offset (pc) >= 0);
}

/* Figure out where the longjmp will land.  We expect that we have
   just entered longjmp and haven't yet setup the stack frame, so
   the args are still in the argument regs.  A0_REGNUM points at the
   jmp_buf structure from which we extract the PC that we will land
   at.  The PC is copied into *pc.  This routine returns true on
   success.  */

#define NBSD_MIPS_JB_PC			(2 * 4)
#define NBSD_MIPS_JB_ELEMENT_SIZE	mips_regsize (current_gdbarch)
#define NBSD_MIPS_JB_OFFSET		(NBSD_MIPS_JB_PC * \
					 NBSD_MIPS_JB_ELEMENT_SIZE)

static int
mipsnbsd_get_longjmp_target (CORE_ADDR *pc)
{
  CORE_ADDR jb_addr;
  char *buf;

  buf = alloca (NBSD_MIPS_JB_ELEMENT_SIZE);

  jb_addr = read_register (A0_REGNUM);

  if (target_read_memory (jb_addr + NBSD_MIPS_JB_OFFSET, buf,
  			  NBSD_MIPS_JB_ELEMENT_SIZE))
    return 0;

  *pc = extract_unsigned_integer (buf, NBSD_MIPS_JB_ELEMENT_SIZE);

  return 1;
}

static int
mipsnbsd_cannot_fetch_register (int regno)
{
  return (regno == ZERO_REGNUM
	  || regno == mips_regnum (current_gdbarch)->fp_implementation_revision);
}

static int
mipsnbsd_cannot_store_register (int regno)
{
  return (regno == ZERO_REGNUM
	  || regno == mips_regnum (current_gdbarch)->fp_implementation_revision);
}

/* NetBSD/mips uses a slightly different link_map structure from the
   other NetBSD platforms.  */
static struct link_map_offsets *
mipsnbsd_ilp32_solib_svr4_fetch_link_map_offsets (void)
{
  static struct link_map_offsets lmo;
  static struct link_map_offsets *lmp = NULL;

  if (lmp == NULL) 
    {
      lmp = &lmo;

      lmo.r_debug_size = 16;

      lmo.r_map_offset = 4;
      lmo.r_map_size   = 4;

      lmo.link_map_size = 24;

      lmo.l_addr_offset = 0;
      lmo.l_addr_size   = 4;

      lmo.l_name_offset = 8;
      lmo.l_name_size   = 4;

      lmo.l_next_offset = 16;
      lmo.l_next_size   = 4;

      lmo.l_prev_offset = 20;
      lmo.l_prev_size   = 4;
    }

  return lmp;
}

static struct link_map_offsets *
mipsnbsd_lp64_solib_svr4_fetch_link_map_offsets (void)
{
  static struct link_map_offsets lmo;
  static struct link_map_offsets *lmp = NULL;

  if (lmp == NULL)
    {
      lmp = &lmo;

      lmo.r_debug_size = 32;

      lmo.r_map_offset = 8;  
      lmo.r_map_size   = 8;

      lmo.link_map_size = 48;

      lmo.l_addr_offset = 0;
      lmo.l_addr_size   = 8;

      lmo.l_name_offset = 16; 
      lmo.l_name_size   = 8;

      lmo.l_next_offset = 32;
      lmo.l_next_size   = 8;

      lmo.l_prev_offset = 40;
      lmo.l_prev_size   = 8;
    }

  return lmp;
}

static void
mipsnbsd_init_abi (struct gdbarch_info info,
                   struct gdbarch *gdbarch)
{
  set_gdbarch_pc_in_sigtramp (gdbarch, mipsnbsd_pc_in_sigtramp);

  set_gdbarch_get_longjmp_target (gdbarch, mipsnbsd_get_longjmp_target);

  set_gdbarch_cannot_fetch_register (gdbarch, mipsnbsd_cannot_fetch_register);
  set_gdbarch_cannot_store_register (gdbarch, mipsnbsd_cannot_store_register);

  set_gdbarch_software_single_step (gdbarch, mips_software_single_step);

  set_solib_svr4_fetch_link_map_offsets (gdbarch,
					 gdbarch_ptr_bit (gdbarch) == 32 ?
                            mipsnbsd_ilp32_solib_svr4_fetch_link_map_offsets :
			    mipsnbsd_lp64_solib_svr4_fetch_link_map_offsets);
}

void
_initialize_mipsnbsd_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_mips, 0, GDB_OSABI_NETBSD_ELF,
			  mipsnbsd_init_abi);

  add_core_fns (&mipsnbsd_core_fns);
  add_core_fns (&mipsnbsd_elfcore_fns);
}
