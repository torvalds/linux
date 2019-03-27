/* This file defines the interface between the h8300 simulator and gdb.
   Copyright (C) 2002 Free Software Foundation, Inc.

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

#if !defined (SIM_H8300_H)
#define SIM_H8300_H

#ifdef __cplusplus
extern "C" { //}
#endif

/* The simulator makes use of the following register information. */

  enum sim_h8300_regs
  {
    /* Registers common to all the H8 variants. */
    /* Start here: */
    SIM_H8300_R0_REGNUM,
    SIM_H8300_R1_REGNUM,
    SIM_H8300_R2_REGNUM,
    SIM_H8300_R3_REGNUM,
    SIM_H8300_R4_REGNUM,
    SIM_H8300_R5_REGNUM,
    SIM_H8300_R6_REGNUM,
    SIM_H8300_R7_REGNUM,

    SIM_H8300_CCR_REGNUM,  /* Contains processor status */
    SIM_H8300_PC_REGNUM,   /* Contains program counter */
    /* End here */

    SIM_H8300_EXR_REGNUM,  /* Contains extended processor status
                              H8S and higher */
    SIM_H8300_MACL_REGNUM, /* Lower part of MAC register (26xx only)*/
    SIM_H8300_MACH_REGNUM, /* High part of MAC register (26xx only) */

    SIM_H8300_CYCLE_REGNUM,
    SIM_H8300_INST_REGNUM,
    SIM_H8300_TICK_REGNUM
  };

  enum
  {
    SIM_H8300_ARG_FIRST_REGNUM = SIM_H8300_R0_REGNUM, /* first reg in which an arg
                                                         may be passed */
    SIM_H8300_ARG_LAST_REGNUM = SIM_H8300_R3_REGNUM,  /* last  reg in which an arg
                                                         may be passed */
    SIM_H8300_FP_REGNUM = SIM_H8300_R6_REGNUM, /* Contain address of executing
                                                  stack frame */
    SIM_H8300_SP_REGNUM = SIM_H8300_R7_REGNUM  /* Contains address of top of stack */
  };

  enum
  {
    SIM_H8300_NUM_COMMON_REGS = 10,
    SIM_H8300_S_NUM_REGS = 13,
    SIM_H8300_NUM_REGS = 16
  };

#ifdef __cplusplus
}
#endif

#endif				/* SIM_H8300_H */
