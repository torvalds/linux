/* Target-dependent header for the MIPS architecture, for GDB, the GNU Debugger.

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

#ifndef MIPS_TDEP_H
#define MIPS_TDEP_H

struct gdbarch;

/* All the possible MIPS ABIs. */
enum mips_abi
  {
    MIPS_ABI_UNKNOWN = 0,
    MIPS_ABI_N32,
    MIPS_ABI_O32,
    MIPS_ABI_N64,
    MIPS_ABI_O64,
    MIPS_ABI_EABI32,
    MIPS_ABI_EABI64,
    MIPS_ABI_LAST
  };

/* Return the MIPS ABI associated with GDBARCH.  */
enum mips_abi mips_abi (struct gdbarch *gdbarch);

/* For wince :-(.  */
extern CORE_ADDR mips_next_pc (CORE_ADDR pc);

/* Return the "MIPS" register size.  Just a short cut to the BFD
   architecture's word size.  */
extern int mips_regsize (struct gdbarch *gdbarch);

/* Return the current index for various MIPS registers.  */
struct mips_regnum
{
  int pc;
  int fp0;
  int fp_implementation_revision;
  int fp_control_status;
  int badvaddr;		/* Bad vaddr for addressing exception.  */
  int cause;		/* Describes last exception.  */
  int hi;		/* Multiply/divide temp.  */
  int lo;		/* ...  */
};
extern const struct mips_regnum *mips_regnum (struct gdbarch *gdbarch);

enum {
  MIPS_S0_REGNUM = 16,
  MIPS_S1_REGNUM = 17,
  MIPS_S2_REGNUM = 18,
  MIPS_S3_REGNUM = 19,
  MIPS_S4_REGNUM = 20,
  MIPS_S5_REGNUM = 21,
  MIPS_S6_REGNUM = 22,
  MIPS_S7_REGNUM = 23,
  MIPS_SP_REGNUM = 29,
  MIPS_FP_REGNUM = 30,
  MIPS_RA_REGNUM = 31,
  MIPS_EMBED_LO_REGNUM = 33,
  MIPS_EMBED_HI_REGNUM = 34,
  MIPS_EMBED_BADVADDR_REGNUM = 35,
  MIPS_EMBED_CAUSE_REGNUM = 36,
  MIPS_EMBED_PC_REGNUM = 37,
  MIPS_EMBED_FP0_REGNUM = 40
};

/* Defined in mips-tdep.c and used in remote-mips.c */
extern void deprecated_mips_set_processor_regs_hack (void);


#endif /* MIPS_TDEP_H */
