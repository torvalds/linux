/* Target-dependent code for SPARC.

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

#ifndef SPARC_TDEP_H
#define SPARC_TDEP_H 1

struct frame_info;
struct gdbarch;
struct regcache;
struct regset;
struct trad_frame_saved_reg;

/* Register offsets for the general-purpose register set.  */

struct sparc_gregset
{
  int r_psr_offset;
  int r_pc_offset;
  int r_npc_offset;
  int r_y_offset;
  int r_wim_offset;
  int r_tbr_offset;
  int r_g1_offset;
  int r_l0_offset;
  int r_y_size;
};

/* SPARC architecture-specific information.  */

struct gdbarch_tdep
{
  /* Register numbers for the PN and nPC registers.  The definitions
     for (64-bit) UltraSPARC differ from the (32-bit) SPARC
     definitions.  */
  int pc_regnum;
  int npc_regnum;

  /* Register sets.  */
  struct regset *gregset;
  size_t sizeof_gregset;
  struct regset *fpregset;
  size_t sizeof_fpregset;

  /* Offset of saved PC in jmp_buf.  */
  int jb_pc_offset;

  /* Size of an Procedure Linkage Table (PLT) entry, 0 if we shouldn't
     treat the PLT special when doing prologue analysis.  */
  size_t plt_entry_size;
};

/* Register numbers of various important registers.  */

enum sparc_regnum
{
  SPARC_G0_REGNUM,		/* %g0 */
  SPARC_G1_REGNUM,
  SPARC_G2_REGNUM,
  SPARC_G3_REGNUM,
  SPARC_G4_REGNUM,
  SPARC_G5_REGNUM,
  SPARC_G6_REGNUM,
  SPARC_G7_REGNUM,		/* %g7 */
  SPARC_O0_REGNUM,		/* %o0 */
  SPARC_O1_REGNUM,
  SPARC_O2_REGNUM,
  SPARC_O3_REGNUM,
  SPARC_O4_REGNUM,
  SPARC_O5_REGNUM,
  SPARC_SP_REGNUM,		/* %sp (%o6) */
  SPARC_O7_REGNUM,		/* %o7 */
  SPARC_L0_REGNUM,		/* %l0 */
  SPARC_L1_REGNUM,
  SPARC_L2_REGNUM,
  SPARC_L3_REGNUM,
  SPARC_L4_REGNUM,
  SPARC_L5_REGNUM,
  SPARC_L6_REGNUM,
  SPARC_L7_REGNUM,		/* %l7 */
  SPARC_I0_REGNUM,		/* %i0 */
  SPARC_I1_REGNUM,
  SPARC_I2_REGNUM,
  SPARC_I3_REGNUM,
  SPARC_I4_REGNUM,
  SPARC_I5_REGNUM,
  SPARC_FP_REGNUM,		/* %fp (%i6) */
  SPARC_I7_REGNUM,		/* %i7 */
  SPARC_F0_REGNUM,		/* %f0 */
  SPARC_F1_REGNUM,
  SPARC_F31_REGNUM		/* %f31 */
  = SPARC_F0_REGNUM + 31
};

enum sparc32_regnum
{
  SPARC32_Y_REGNUM		/* %y */
  = SPARC_F31_REGNUM + 1,
  SPARC32_PSR_REGNUM,		/* %psr */
  SPARC32_WIM_REGNUM,		/* %wim */
  SPARC32_TBR_REGNUM,		/* %tbr */
  SPARC32_PC_REGNUM,		/* %pc */
  SPARC32_NPC_REGNUM,		/* %npc */
  SPARC32_FSR_REGNUM,		/* %fsr */
  SPARC32_CSR_REGNUM,		/* %csr */

  /* Pseudo registers.  */
  SPARC32_D0_REGNUM,		/* %d0 */
  SPARC32_D30_REGNUM		/* %d30 */
  = SPARC32_D0_REGNUM + 15
};


struct sparc_frame_cache
{
  /* Base address.  */
  CORE_ADDR base;
  CORE_ADDR pc;

  /* Do we have a frame?  */
  int frameless_p;

  /* Do we have a Structure, Union or Quad-Precision return value?.  */
  int struct_return_p;

  /* Table of saved registers.  */
  struct trad_frame_saved_reg *saved_regs;
};

/* Fetch the instruction at PC.  */
extern unsigned long sparc_fetch_instruction (CORE_ADDR pc);

/* Fetch StackGhost Per-Process XOR cookie.  */
extern ULONGEST sparc_fetch_wcookie (void);

extern CORE_ADDR sparc_analyze_prologue (CORE_ADDR pc, CORE_ADDR current_pc,
					 struct sparc_frame_cache *cache);

extern struct sparc_frame_cache *
  sparc_frame_cache (struct frame_info *next_frame, void **this_cache);

extern struct sparc_frame_cache *
  sparc32_frame_cache (struct frame_info *next_frame, void **this_cache);



extern void sparc_software_single_step (enum target_signal sig,
					int insert_breakpoints_p);

extern void sparc_supply_rwindow (struct regcache *regcache,
				  CORE_ADDR sp, int regnum);
extern void sparc_collect_rwindow (const struct regcache *regcache,
				   CORE_ADDR sp, int regnum);

/* Register offsets for SunOS 4.  */
extern const struct sparc_gregset sparc32_sunos4_gregset;

extern void sparc32_supply_gregset (const struct sparc_gregset *gregset,
				    struct regcache *regcache,
				    int regnum, const void *gregs);
extern void sparc32_collect_gregset (const struct sparc_gregset *gregset,
				     const struct regcache *regcache,
				     int regnum, void *gregs);
extern void sparc32_supply_fpregset (struct regcache *regcache,
				     int regnum, const void *fpregs);
extern void sparc32_collect_fpregset (const struct regcache *regcache,
				      int regnum, void *fpregs);

/* Functions and variables exported from sparc-sol2-tdep.c.  */

/* Register offsets for Solaris 2.  */
extern const struct sparc_gregset sparc32_sol2_gregset;

extern int sparc_sol2_pc_in_sigtramp (CORE_ADDR pc, char *name);

extern void sparc32_sol2_init_abi (struct gdbarch_info info,
				   struct gdbarch *gdbarch);

/* Functions and variables exported from sparcnbsd-tdep.c.  */

/* Register offsets for NetBSD.  */
extern const struct sparc_gregset sparc32nbsd_gregset;

extern struct trad_frame_saved_reg *
  sparc32nbsd_sigcontext_saved_regs (struct frame_info *next_frame);

#endif /* sparc-tdep.h */
