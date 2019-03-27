/* Low level DECstation interface to ptrace, for GDB when running native.
   Copyright 1988, 1989, 1991, 1992, 1993, 1995, 1996, 1999, 2000, 2001
   Free Software Foundation, Inc.
   Contributed by Alessandro Forin(af@cs.cmu.edu) at CMU
   and by Per Bothner(bothner@cs.wisc.edu) at U.Wisconsin.

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
#include "inferior.h"
#include "gdbcore.h"
#include "regcache.h"
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/user.h>
#undef JB_S0
#undef JB_S1
#undef JB_S2
#undef JB_S3
#undef JB_S4
#undef JB_S5
#undef JB_S6
#undef JB_S7
#undef JB_SP
#undef JB_S8
#undef JB_PC
#undef JB_SR
#undef NJBREGS
#include <setjmp.h>		/* For JB_XXX.  */

/* Size of elements in jmpbuf */

#define JB_ELEMENT_SIZE 4

/* Map gdb internal register number to ptrace ``address''.
   These ``addresses'' are defined in DECstation <sys/ptrace.h> */

static int
register_ptrace_addr (int regno)
{
  return (regno < 32 ? GPR_BASE + regno
	  : regno == mips_regnum (current_gdbarch)->pc ? PC
	  : regno == mips_regnum (current_gdbarch)->cause ? CAUSE
	  : regno == mips_regnum (current_gdbarch)->hi ? MMHI
	  : regno == mips_regnum (current_gdbarch)->lo ? MMLO
	  : regno == mips_regnum (current_gdbarch)->fp_control_status ? FPC_CSR
	  : regno == mips_regnum (current_gdbarch)->fp_implementation_revision ? FPC_EIR
	  : regno >= FP0_REGNUM ? FPR_BASE + (regno - FP0_REGNUM)
	  : 0);
}

static void fetch_core_registers (char *, unsigned, int, CORE_ADDR);

/* Get all registers from the inferior */

void
fetch_inferior_registers (int regno)
{
  unsigned int regaddr;
  char buf[MAX_REGISTER_SIZE];
  int i;
  char zerobuf[MAX_REGISTER_SIZE];
  memset (zerobuf, 0, MAX_REGISTER_SIZE);

  deprecated_registers_fetched ();

  for (regno = 1; regno < NUM_REGS; regno++)
    {
      regaddr = register_ptrace_addr (regno);
      for (i = 0; i < DEPRECATED_REGISTER_RAW_SIZE (regno); i += sizeof (int))
	{
	  *(int *) &buf[i] = ptrace (PT_READ_U, PIDGET (inferior_ptid),
				     (PTRACE_ARG3_TYPE) regaddr, 0);
	  regaddr += sizeof (int);
	}
      supply_register (regno, buf);
    }

  supply_register (ZERO_REGNUM, zerobuf);
  /* Frame ptr reg must appear to be 0; it is faked by stack handling code. */
  supply_register (DEPRECATED_FP_REGNUM, zerobuf);
}

/* Store our register values back into the inferior.
   If REGNO is -1, do this for all registers.
   Otherwise, REGNO specifies which register (so we can save time).  */

void
store_inferior_registers (int regno)
{
  unsigned int regaddr;
  char buf[80];

  if (regno > 0)
    {
      if (regno == ZERO_REGNUM || regno == PS_REGNUM
	  || regno == mips_regnum (current_gdbarch)->badvaddr
	  || regno == mips_regnum (current_gdbarch)->cause
	  || regno == mips_regnum (current_gdbarch)->fp_implementation_revision
	  || regno == DEPRECATED_FP_REGNUM
	  || (regno >= FIRST_EMBED_REGNUM && regno <= LAST_EMBED_REGNUM))
	return;
      regaddr = register_ptrace_addr (regno);
      errno = 0;
      ptrace (PT_WRITE_U, PIDGET (inferior_ptid), (PTRACE_ARG3_TYPE) regaddr,
	      read_register (regno));
      if (errno != 0)
	{
	  sprintf (buf, "writing register number %d", regno);
	  perror_with_name (buf);
	}
    }
  else
    {
      for (regno = 0; regno < NUM_REGS; regno++)
	store_inferior_registers (regno);
    }
}


/* Figure out where the longjmp will land.
   We expect the first arg to be a pointer to the jmp_buf structure from which
   we extract the pc (JB_PC) that we will land at.  The pc is copied into PC.
   This routine returns true on success. */

int
get_longjmp_target (CORE_ADDR *pc)
{
  CORE_ADDR jb_addr;
  char *buf;

  buf = alloca (TARGET_PTR_BIT / TARGET_CHAR_BIT);
  jb_addr = read_register (A0_REGNUM);

  if (target_read_memory (jb_addr + JB_PC * JB_ELEMENT_SIZE, buf,
			  TARGET_PTR_BIT / TARGET_CHAR_BIT))
    return 0;

  *pc = extract_unsigned_integer (buf, TARGET_PTR_BIT / TARGET_CHAR_BIT);

  return 1;
}

/* Extract the register values out of the core file and store
   them where `read_register' will find them.

   CORE_REG_SECT points to the register values themselves, read into memory.
   CORE_REG_SIZE is the size of that area.
   WHICH says which set of registers we are handling (0 = int, 2 = float
   on machines where they are discontiguous).
   REG_ADDR is the offset from u.u_ar0 to the register values relative to
   core_reg_sect.  This is used with old-fashioned core files to
   locate the registers in a large upage-plus-stack ".reg" section.
   Original upage address X is at location core_reg_sect+x+reg_addr.
 */

static void
fetch_core_registers (char *core_reg_sect, unsigned core_reg_size, int which,
		      CORE_ADDR reg_addr)
{
  int regno;
  unsigned int addr;
  int bad_reg = -1;
  reg_ptr = -reg_addr;	/* Original u.u_ar0 is -reg_addr. */

  char zerobuf[MAX_REGISTER_SIZE];
  memset (zerobuf, 0, MAX_REGISTER_SIZE);


  /* If u.u_ar0 was an absolute address in the core file, relativize it now,
     so we can use it as an offset into core_reg_sect.  When we're done,
     "register 0" will be at core_reg_sect+reg_ptr, and we can use
     register_addr to offset to the other registers.  If this is a modern
     core file without a upage, reg_ptr will be zero and this is all a big
     NOP.  */
  if (reg_ptr > core_reg_size)
#ifdef KERNEL_U_ADDR
    reg_ptr -= KERNEL_U_ADDR;
#else
    error ("Old mips core file can't be processed on this machine.");
#endif

  for (regno = 0; regno < NUM_REGS; regno++)
    {
      addr = register_addr (regno, reg_ptr);
      if (addr >= core_reg_size)
	{
	  if (bad_reg < 0)
	    bad_reg = regno;
	}
      else
	{
	  supply_register (regno, core_reg_sect + addr);
	}
    }
  if (bad_reg >= 0)
    {
      error ("Register %s not found in core file.", REGISTER_NAME (bad_reg));
    }
  supply_register (ZERO_REGNUM, zerobuf);
  /* Frame ptr reg must appear to be 0; it is faked by stack handling code. */
  supply_register (DEPRECATED_FP_REGNUM, zerobuf);
}

/* Return the address in the core dump or inferior of register REGNO.
   BLOCKEND is the address of the end of the user structure.  */

CORE_ADDR
register_addr (int regno, CORE_ADDR blockend)
{
  CORE_ADDR addr;

  if (regno < 0 || regno >= NUM_REGS)
    error ("Invalid register number %d.", regno);

  REGISTER_U_ADDR (addr, blockend, regno);

  return addr;
}


/* Register that we are able to handle mips core file formats.
   FIXME: is this really bfd_target_unknown_flavour? */

static struct core_fns mips_core_fns =
{
  bfd_target_unknown_flavour,		/* core_flavour */
  default_check_format,			/* check_format */
  default_core_sniffer,			/* core_sniffer */
  fetch_core_registers,			/* core_read_registers */
  NULL					/* next */
};

void
_initialize_core_mips (void)
{
  add_core_fns (&mips_core_fns);
}
