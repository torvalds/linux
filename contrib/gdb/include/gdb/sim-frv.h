/* This file defines the interface between the FR-V simulator and GDB.

   Copyright 2003 Free Software Foundation, Inc.

   Contributed by Red Hat.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.  */

#if !defined (SIM_FRV_H)
#define SIM_FRV_H

#ifdef __cplusplus
extern "C" { // }
#endif

enum sim_frv_regs
{
  SIM_FRV_GR0_REGNUM  = 0,
  SIM_FRV_GR63_REGNUM = 63,
  SIM_FRV_FR0_REGNUM  = 64,
  SIM_FRV_FR63_REGNUM = 127,
  SIM_FRV_PC_REGNUM   = 128,

  /* An FR-V architecture may have up to 4096 special purpose registers
     (SPRs).  In order to determine a specific constant used to access
     a particular SPR, one of the H_SPR_ prefixed offsets defined in
     opcodes/frv-desc.h should be added to SIM_FRV_SPR0_REGNUM.  So,
     for example, the number that GDB uses to fetch the link register
     from the simulator is (SIM_FRV_SPR0_REGNUM + H_SPR_LR).  */
  SIM_FRV_SPR0_REGNUM = 129,
  SIM_FRV_SPR4095_REGNUM = SIM_FRV_SPR0_REGNUM + 4095
};

#ifdef __cplusplus
}
#endif

#endif
