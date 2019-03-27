/* This file defines the interface between the sh simulator and gdb.
   Copyright (C) 2000, 2002 Free Software Foundation, Inc.

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
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#if !defined (SIM_SH_H)
#define SIM_SH_H

#ifdef __cplusplus
extern "C" { // }
#endif

/* The simulator makes use of the following register information. */

enum
{
  SIM_SH_R0_REGNUM = 0,
  SIM_SH_R1_REGNUM,
  SIM_SH_R2_REGNUM,
  SIM_SH_R3_REGNUM,
  SIM_SH_R4_REGNUM,
  SIM_SH_R5_REGNUM,
  SIM_SH_R6_REGNUM,
  SIM_SH_R7_REGNUM,
  SIM_SH_R8_REGNUM,
  SIM_SH_R9_REGNUM,
  SIM_SH_R10_REGNUM,
  SIM_SH_R11_REGNUM,
  SIM_SH_R12_REGNUM,
  SIM_SH_R13_REGNUM,
  SIM_SH_R14_REGNUM,
  SIM_SH_R15_REGNUM,
  SIM_SH_PC_REGNUM,
  SIM_SH_PR_REGNUM,
  SIM_SH_GBR_REGNUM,
  SIM_SH_VBR_REGNUM,
  SIM_SH_MACH_REGNUM,
  SIM_SH_MACL_REGNUM,
  SIM_SH_SR_REGNUM,
  SIM_SH_FPUL_REGNUM,
  SIM_SH_FPSCR_REGNUM,
  SIM_SH_FR0_REGNUM, /* FRn registers: sh3e / sh4 */
  SIM_SH_FR1_REGNUM,
  SIM_SH_FR2_REGNUM,
  SIM_SH_FR3_REGNUM,
  SIM_SH_FR4_REGNUM,
  SIM_SH_FR5_REGNUM,
  SIM_SH_FR6_REGNUM,
  SIM_SH_FR7_REGNUM,
  SIM_SH_FR8_REGNUM,
  SIM_SH_FR9_REGNUM,
  SIM_SH_FR10_REGNUM,
  SIM_SH_FR11_REGNUM,
  SIM_SH_FR12_REGNUM,
  SIM_SH_FR13_REGNUM,
  SIM_SH_FR14_REGNUM,
  SIM_SH_FR15_REGNUM,
  SIM_SH_SSR_REGNUM, /* sh3{,e,-dsp}, sh4 */
  SIM_SH_SPC_REGNUM, /* sh3{,e,-dsp}, sh4 */
  SIM_SH_R0_BANK0_REGNUM, /* SIM_SH_Rn_BANKm_REGNUM: sh3[e] / sh4 */
  SIM_SH_R1_BANK0_REGNUM,
  SIM_SH_R2_BANK0_REGNUM,
  SIM_SH_R3_BANK0_REGNUM,
  SIM_SH_R4_BANK0_REGNUM,
  SIM_SH_R5_BANK0_REGNUM,
  SIM_SH_R6_BANK0_REGNUM,
  SIM_SH_R7_BANK0_REGNUM,
  SIM_SH_R0_BANK1_REGNUM,
  SIM_SH_R1_BANK1_REGNUM,
  SIM_SH_R2_BANK1_REGNUM,
  SIM_SH_R3_BANK1_REGNUM,
  SIM_SH_R4_BANK1_REGNUM,
  SIM_SH_R5_BANK1_REGNUM,
  SIM_SH_R6_BANK1_REGNUM,
  SIM_SH_R7_BANK1_REGNUM,
  SIM_SH_XF0_REGNUM,
  SIM_SH_XF1_REGNUM,
  SIM_SH_XF2_REGNUM,
  SIM_SH_XF3_REGNUM,
  SIM_SH_XF4_REGNUM,
  SIM_SH_XF5_REGNUM,
  SIM_SH_XF6_REGNUM,
  SIM_SH_XF7_REGNUM,
  SIM_SH_XF8_REGNUM,
  SIM_SH_XF9_REGNUM,
  SIM_SH_XF10_REGNUM,
  SIM_SH_XF11_REGNUM,
  SIM_SH_XF12_REGNUM,
  SIM_SH_XF13_REGNUM,
  SIM_SH_XF14_REGNUM,
  SIM_SH_XF15_REGNUM,
  SIM_SH_SGR_REGNUM,
  SIM_SH_DBR_REGNUM,
  SIM_SH4_NUM_REGS, /* 77 */

  /* sh[3]-dsp */
  SIM_SH_DSR_REGNUM,
  SIM_SH_A0G_REGNUM,
  SIM_SH_A0_REGNUM,
  SIM_SH_A1G_REGNUM,
  SIM_SH_A1_REGNUM,
  SIM_SH_M0_REGNUM,
  SIM_SH_M1_REGNUM,
  SIM_SH_X0_REGNUM,
  SIM_SH_X1_REGNUM,
  SIM_SH_Y0_REGNUM,
  SIM_SH_Y1_REGNUM,
  SIM_SH_MOD_REGNUM,
  SIM_SH_RS_REGNUM,
  SIM_SH_RE_REGNUM,
  SIM_SH_R0_BANK_REGNUM,
  SIM_SH_R1_BANK_REGNUM,
  SIM_SH_R2_BANK_REGNUM,
  SIM_SH_R3_BANK_REGNUM,
  SIM_SH_R4_BANK_REGNUM,
  SIM_SH_R5_BANK_REGNUM,
  SIM_SH_R6_BANK_REGNUM,
  SIM_SH_R7_BANK_REGNUM
  /* 100..127: room for expansion.  */
};

enum
{
  SIM_SH64_R0_REGNUM = 0,
  SIM_SH64_SP_REGNUM = 15,
  SIM_SH64_PC_REGNUM = 64,
  SIM_SH64_SR_REGNUM = 65,
  SIM_SH64_SSR_REGNUM = 66,
  SIM_SH64_SPC_REGNUM = 67,
  SIM_SH64_TR0_REGNUM = 68,
  SIM_SH64_FPCSR_REGNUM = 76,
  SIM_SH64_FR0_REGNUM = 77
};

enum
{
  SIM_SH64_NR_REGS = 141,  /* total number of architectural registers */
  SIM_SH64_NR_R_REGS = 64, /* number of general registers */
  SIM_SH64_NR_TR_REGS = 8, /* number of target registers */
  SIM_SH64_NR_FP_REGS = 64 /* number of floating point registers */
};

#ifdef __cplusplus
}
#endif

#endif
