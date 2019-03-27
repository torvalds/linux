/* Print mips instructions for GDB, the GNU debugger, or for objdump.
   Copyright 1989, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2005
   Free Software Foundation, Inc.
   Contributed by Nobuyuki Hikichi(hikichi@sra.co.jp).

   This file is part of GDB, GAS, and the GNU binutils.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#include "sysdep.h"
#include "dis-asm.h"
#include "libiberty.h"
#include "opcode/mips.h"
#include "opintl.h"

/* FIXME: These are needed to figure out if the code is mips16 or
   not. The low bit of the address is often a good indicator.  No
   symbol table is available when this code runs out in an embedded
   system as when it is used for disassembler support in a monitor.  */

#if !defined(EMBEDDED_ENV)
#define SYMTAB_AVAILABLE 1
#include "elf-bfd.h"
#include "elf/mips.h"
#endif

/* Mips instructions are at maximum this many bytes long.  */
#define INSNLEN 4

/* Generate Octeon/MIPS unaligned load and store instructions. */
#ifdef INCLUDE_OCTEON_USEUN
int octeon_use_unalign = 1;
#else
int octeon_use_unalign = 0;
#endif


/* FIXME: These should be shared with gdb somehow.  */

struct mips_cp0sel_name
{
  unsigned int cp0reg;
  unsigned int sel;
  const char * const name;
};

/* The mips16 registers.  */
static const unsigned int mips16_to_32_reg_map[] =
{
  16, 17, 2, 3, 4, 5, 6, 7
};

#define mips16_reg_names(rn)	mips_gpr_names[mips16_to_32_reg_map[rn]]


static const char * const mips_gpr_names_numeric[32] =
{
  "$0",   "$1",   "$2",   "$3",   "$4",   "$5",   "$6",   "$7",
  "$8",   "$9",   "$10",  "$11",  "$12",  "$13",  "$14",  "$15",
  "$16",  "$17",  "$18",  "$19",  "$20",  "$21",  "$22",  "$23",
  "$24",  "$25",  "$26",  "$27",  "$28",  "$29",  "$30",  "$31"
};

static const char * const mips_gpr_names_oldabi[32] =
{
  "zero", "at",   "v0",   "v1",   "a0",   "a1",   "a2",   "a3",
  "t0",   "t1",   "t2",   "t3",   "t4",   "t5",   "t6",   "t7",
  "s0",   "s1",   "s2",   "s3",   "s4",   "s5",   "s6",   "s7",
  "t8",   "t9",   "k0",   "k1",   "gp",   "sp",   "s8",   "ra"
};

static const char * const mips_gpr_names_newabi[32] =
{
  "zero", "at",   "v0",   "v1",   "a0",   "a1",   "a2",   "a3",
  "a4",   "a5",   "a6",   "a7",   "t0",   "t1",   "t2",   "t3",
  "s0",   "s1",   "s2",   "s3",   "s4",   "s5",   "s6",   "s7",
  "t8",   "t9",   "k0",   "k1",   "gp",   "sp",   "s8",   "ra"
};

static const char * const mips_fpr_names_numeric[32] =
{
  "$f0",  "$f1",  "$f2",  "$f3",  "$f4",  "$f5",  "$f6",  "$f7",
  "$f8",  "$f9",  "$f10", "$f11", "$f12", "$f13", "$f14", "$f15",
  "$f16", "$f17", "$f18", "$f19", "$f20", "$f21", "$f22", "$f23",
  "$f24", "$f25", "$f26", "$f27", "$f28", "$f29", "$f30", "$f31"
};

static const char * const mips_fpr_names_32[32] =
{
  "fv0",  "fv0f", "fv1",  "fv1f", "ft0",  "ft0f", "ft1",  "ft1f",
  "ft2",  "ft2f", "ft3",  "ft3f", "fa0",  "fa0f", "fa1",  "fa1f",
  "ft4",  "ft4f", "ft5",  "ft5f", "fs0",  "fs0f", "fs1",  "fs1f",
  "fs2",  "fs2f", "fs3",  "fs3f", "fs4",  "fs4f", "fs5",  "fs5f"
};

static const char * const mips_fpr_names_n32[32] =
{
  "fv0",  "ft14", "fv1",  "ft15", "ft0",  "ft1",  "ft2",  "ft3",
  "ft4",  "ft5",  "ft6",  "ft7",  "fa0",  "fa1",  "fa2",  "fa3",
  "fa4",  "fa5",  "fa6",  "fa7",  "fs0",  "ft8",  "fs1",  "ft9",
  "fs2",  "ft10", "fs3",  "ft11", "fs4",  "ft12", "fs5",  "ft13"
};

static const char * const mips_fpr_names_64[32] =
{
  "fv0",  "ft12", "fv1",  "ft13", "ft0",  "ft1",  "ft2",  "ft3",
  "ft4",  "ft5",  "ft6",  "ft7",  "fa0",  "fa1",  "fa2",  "fa3",
  "fa4",  "fa5",  "fa6",  "fa7",  "ft8",  "ft9",  "ft10", "ft11",
  "fs0",  "fs1",  "fs2",  "fs3",  "fs4",  "fs5",  "fs6",  "fs7"
};

static const char * const mips_cp0_names_numeric[32] =
{
  "$0",   "$1",   "$2",   "$3",   "$4",   "$5",   "$6",   "$7",
  "$8",   "$9",   "$10",  "$11",  "$12",  "$13",  "$14",  "$15",
  "$16",  "$17",  "$18",  "$19",  "$20",  "$21",  "$22",  "$23",
  "$24",  "$25",  "$26",  "$27",  "$28",  "$29",  "$30",  "$31"
};

static const char * const mips_cp0_names_mips3264[32] =
{
  "c0_index",     "c0_random",    "c0_entrylo0",  "c0_entrylo1",
  "c0_context",   "c0_pagemask",  "c0_wired",     "$7",
  "c0_badvaddr",  "c0_count",     "c0_entryhi",   "c0_compare",
  "c0_status",    "c0_cause",     "c0_epc",       "c0_prid",
  "c0_config",    "c0_lladdr",    "c0_watchlo",   "c0_watchhi",
  "c0_xcontext",  "$21",          "$22",          "c0_debug",
  "c0_depc",      "c0_perfcnt",   "c0_errctl",    "c0_cacheerr",
  "c0_taglo",     "c0_taghi",     "c0_errorepc",  "c0_desave",
};

static const struct mips_cp0sel_name mips_cp0sel_names_mips3264[] =
{
  { 16, 1, "c0_config1"		},
  { 16, 2, "c0_config2"		},
  { 16, 3, "c0_config3"		},
  { 18, 1, "c0_watchlo,1"	},
  { 18, 2, "c0_watchlo,2"	},
  { 18, 3, "c0_watchlo,3"	},
  { 18, 4, "c0_watchlo,4"	},
  { 18, 5, "c0_watchlo,5"	},
  { 18, 6, "c0_watchlo,6"	},
  { 18, 7, "c0_watchlo,7"	},
  { 19, 1, "c0_watchhi,1"	},
  { 19, 2, "c0_watchhi,2"	},
  { 19, 3, "c0_watchhi,3"	},
  { 19, 4, "c0_watchhi,4"	},
  { 19, 5, "c0_watchhi,5"	},
  { 19, 6, "c0_watchhi,6"	},
  { 19, 7, "c0_watchhi,7"	},
  { 25, 1, "c0_perfcnt,1"	},
  { 25, 2, "c0_perfcnt,2"	},
  { 25, 3, "c0_perfcnt,3"	},
  { 25, 4, "c0_perfcnt,4"	},
  { 25, 5, "c0_perfcnt,5"	},
  { 25, 6, "c0_perfcnt,6"	},
  { 25, 7, "c0_perfcnt,7"	},
  { 27, 1, "c0_cacheerr,1"	},
  { 27, 2, "c0_cacheerr,2"	},
  { 27, 3, "c0_cacheerr,3"	},
  { 28, 1, "c0_datalo"		},
  { 29, 1, "c0_datahi"		}
};

static const char * const mips_cp0_names_mips3264r2[32] =
{
  "c0_index",     "c0_random",    "c0_entrylo0",  "c0_entrylo1",
  "c0_context",   "c0_pagemask",  "c0_wired",     "c0_hwrena",
  "c0_badvaddr",  "c0_count",     "c0_entryhi",   "c0_compare",
  "c0_status",    "c0_cause",     "c0_epc",       "c0_prid",
  "c0_config",    "c0_lladdr",    "c0_watchlo",   "c0_watchhi",
  "c0_xcontext",  "$21",          "$22",          "c0_debug",
  "c0_depc",      "c0_perfcnt",   "c0_errctl",    "c0_cacheerr",
  "c0_taglo",     "c0_taghi",     "c0_errorepc",  "c0_desave",
};

static const struct mips_cp0sel_name mips_cp0sel_names_mips3264r2[] =
{
  {  4, 1, "c0_contextconfig"	},
  {  0, 1, "c0_mvpcontrol"	},
  {  0, 2, "c0_mvpconf0"	},
  {  0, 3, "c0_mvpconf1"	},
  {  1, 1, "c0_vpecontrol"	},
  {  1, 2, "c0_vpeconf0"	},
  {  1, 3, "c0_vpeconf1"	},
  {  1, 4, "c0_yqmask"		},
  {  1, 5, "c0_vpeschedule"	},
  {  1, 6, "c0_vpeschefback"	},
  {  2, 1, "c0_tcstatus"	},
  {  2, 2, "c0_tcbind"		},
  {  2, 3, "c0_tcrestart"	},
  {  2, 4, "c0_tchalt"		},
  {  2, 5, "c0_tccontext"	},
  {  2, 6, "c0_tcschedule"	},
  {  2, 7, "c0_tcschefback"	},
  {  5, 1, "c0_pagegrain"	},
  {  6, 1, "c0_srsconf0"	},
  {  6, 2, "c0_srsconf1"	},
  {  6, 3, "c0_srsconf2"	},
  {  6, 4, "c0_srsconf3"	},
  {  6, 5, "c0_srsconf4"	},
  { 12, 1, "c0_intctl"		},
  { 12, 2, "c0_srsctl"		},
  { 12, 3, "c0_srsmap"		},
  { 15, 1, "c0_ebase"		},
  { 16, 1, "c0_config1"		},
  { 16, 2, "c0_config2"		},
  { 16, 3, "c0_config3"		},
  { 18, 1, "c0_watchlo,1"	},
  { 18, 2, "c0_watchlo,2"	},
  { 18, 3, "c0_watchlo,3"	},
  { 18, 4, "c0_watchlo,4"	},
  { 18, 5, "c0_watchlo,5"	},
  { 18, 6, "c0_watchlo,6"	},
  { 18, 7, "c0_watchlo,7"	},
  { 19, 1, "c0_watchhi,1"	},
  { 19, 2, "c0_watchhi,2"	},
  { 19, 3, "c0_watchhi,3"	},
  { 19, 4, "c0_watchhi,4"	},
  { 19, 5, "c0_watchhi,5"	},
  { 19, 6, "c0_watchhi,6"	},
  { 19, 7, "c0_watchhi,7"	},
  { 23, 1, "c0_tracecontrol"	},
  { 23, 2, "c0_tracecontrol2"	},
  { 23, 3, "c0_usertracedata"	},
  { 23, 4, "c0_tracebpc"	},
  { 25, 1, "c0_perfcnt,1"	},
  { 25, 2, "c0_perfcnt,2"	},
  { 25, 3, "c0_perfcnt,3"	},
  { 25, 4, "c0_perfcnt,4"	},
  { 25, 5, "c0_perfcnt,5"	},
  { 25, 6, "c0_perfcnt,6"	},
  { 25, 7, "c0_perfcnt,7"	},
  { 27, 1, "c0_cacheerr,1"	},
  { 27, 2, "c0_cacheerr,2"	},
  { 27, 3, "c0_cacheerr,3"	},
  { 28, 1, "c0_datalo"		},
  { 28, 2, "c0_taglo1"		},
  { 28, 3, "c0_datalo1"		},
  { 28, 4, "c0_taglo2"		},
  { 28, 5, "c0_datalo2"		},
  { 28, 6, "c0_taglo3"		},
  { 28, 7, "c0_datalo3"		},
  { 29, 1, "c0_datahi"		},
  { 29, 2, "c0_taghi1"		},
  { 29, 3, "c0_datahi1"		},
  { 29, 4, "c0_taghi2"		},
  { 29, 5, "c0_datahi2"		},
  { 29, 6, "c0_taghi3"		},
  { 29, 7, "c0_datahi3"		},
};

/* SB-1: MIPS64 (mips_cp0_names_mips3264) with minor mods.  */
static const char * const mips_cp0_names_sb1[32] =
{
  "c0_index",     "c0_random",    "c0_entrylo0",  "c0_entrylo1",
  "c0_context",   "c0_pagemask",  "c0_wired",     "$7",
  "c0_badvaddr",  "c0_count",     "c0_entryhi",   "c0_compare",
  "c0_status",    "c0_cause",     "c0_epc",       "c0_prid",
  "c0_config",    "c0_lladdr",    "c0_watchlo",   "c0_watchhi",
  "c0_xcontext",  "$21",          "$22",          "c0_debug",
  "c0_depc",      "c0_perfcnt",   "c0_errctl",    "c0_cacheerr_i",
  "c0_taglo_i",   "c0_taghi_i",   "c0_errorepc",  "c0_desave",
};

static const struct mips_cp0sel_name mips_cp0sel_names_sb1[] =
{
  { 16, 1, "c0_config1"		},
  { 18, 1, "c0_watchlo,1"	},
  { 19, 1, "c0_watchhi,1"	},
  { 22, 0, "c0_perftrace"	},
  { 23, 3, "c0_edebug"		},
  { 25, 1, "c0_perfcnt,1"	},
  { 25, 2, "c0_perfcnt,2"	},
  { 25, 3, "c0_perfcnt,3"	},
  { 25, 4, "c0_perfcnt,4"	},
  { 25, 5, "c0_perfcnt,5"	},
  { 25, 6, "c0_perfcnt,6"	},
  { 25, 7, "c0_perfcnt,7"	},
  { 26, 1, "c0_buserr_pa"	},
  { 27, 1, "c0_cacheerr_d"	},
  { 27, 3, "c0_cacheerr_d_pa"	},
  { 28, 1, "c0_datalo_i"	},
  { 28, 2, "c0_taglo_d"		},
  { 28, 3, "c0_datalo_d"	},
  { 29, 1, "c0_datahi_i"	},
  { 29, 2, "c0_taghi_d"		},
  { 29, 3, "c0_datahi_d"	},
};

static const char * const mips_cp0_names_octeon[32] = {
  "c0_index",     "c0_random",    "c0_entrylo0",  "c0_entrylo1",
  "c0_context",   "c0_pagemask",  "c0_wired",     "c0_hwrena",
  "c0_badvaddr",  "c0_count",     "c0_entryhi",   "c0_compare",
  "c0_status",    "c0_cause",     "c0_epc",       "c0_prid",
  "c0_config",    "$17",          "c0_watchlo",   "c0_watchhi",
  "c0_xcontext",  "$21",          "c0_mdebug",    "c0_debug",
  "c0_depc",      "c0_perfcnt",   "$26",          "c0_cacheerr",
  "c0_taglo",     "c0_taghi",     "c0_errorepc",  "c0_desave",
};

static const struct mips_cp0sel_name mips_cp0sel_names_octeon[] = {
  { 5,  1, "c0_pagegrain"               },
  { 9,  6, "c0_cvmcount"                },
  { 9,  7, "c0_cvmctl"                  },
  { 11, 7, "c0_cvmmemctl"               },
  { 12, 1, "c0_intctl"                  },
  { 12, 2, "c0_srsctl"                  },
  { 15, 1, "c0_ebase"                   },
  { 16, 1, "c0_config1",                },
  { 16, 2, "c0_config2",                },
  { 16, 3, "c0_config3",                },
  { 18, 1, "c0_watchlo,1"               },
  { 19, 1, "c0_watchhi,1"               },
  { 25, 2, "c0_perfcnt,2"               },
  { 27, 1, "c0_cacheerr,1"              },
  { 28, 3, "c0_datalo"                  },
  { 29, 3, "c0_datahi"                  },
};

static const char * const mips_hwr_names_numeric[32] =
{
  "$0",   "$1",   "$2",   "$3",   "$4",   "$5",   "$6",   "$7",
  "$8",   "$9",   "$10",  "$11",  "$12",  "$13",  "$14",  "$15",
  "$16",  "$17",  "$18",  "$19",  "$20",  "$21",  "$22",  "$23",
  "$24",  "$25",  "$26",  "$27",  "$28",  "$29",  "$30",  "$31"
};

static const char * const mips_hwr_names_mips3264r2[32] =
{
  "hwr_cpunum",   "hwr_synci_step", "hwr_cc",     "hwr_ccres",
  "$4",          "$5",            "$6",           "$7",
  "$8",   "$9",   "$10",  "$11",  "$12",  "$13",  "$14",  "$15",
  "$16",  "$17",  "$18",  "$19",  "$20",  "$21",  "$22",  "$23",
  "$24",  "$25",  "$26",  "$27",  "$28",  "$29",  "$30",  "$31"
};

struct mips_abi_choice
{
  const char * name;
  const char * const *gpr_names;
  const char * const *fpr_names;
};

struct mips_abi_choice mips_abi_choices[] =
{
  { "numeric", mips_gpr_names_numeric, mips_fpr_names_numeric },
  { "32", mips_gpr_names_oldabi, mips_fpr_names_32 },
  { "n32", mips_gpr_names_newabi, mips_fpr_names_n32 },
  { "64", mips_gpr_names_newabi, mips_fpr_names_64 },
};

struct mips_arch_choice
{
  const char *name;
  int bfd_mach_valid;
  unsigned long bfd_mach;
  int processor;
  int isa;
  const char * const *cp0_names;
  const struct mips_cp0sel_name *cp0sel_names;
  unsigned int cp0sel_names_len;
  const char * const *hwr_names;
};

const struct mips_arch_choice mips_arch_choices[] =
{
  { "numeric",	0, 0, 0, 0,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },

  { "r3000",	1, bfd_mach_mips3000, CPU_R3000, ISA_MIPS1,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r3900",	1, bfd_mach_mips3900, CPU_R3900, ISA_MIPS1,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r4000",	1, bfd_mach_mips4000, CPU_R4000, ISA_MIPS3,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r4010",	1, bfd_mach_mips4010, CPU_R4010, ISA_MIPS2,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "vr4100",	1, bfd_mach_mips4100, CPU_VR4100, ISA_MIPS3,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "vr4111",	1, bfd_mach_mips4111, CPU_R4111, ISA_MIPS3,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "vr4120",	1, bfd_mach_mips4120, CPU_VR4120, ISA_MIPS3,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r4300",	1, bfd_mach_mips4300, CPU_R4300, ISA_MIPS3,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r4400",	1, bfd_mach_mips4400, CPU_R4400, ISA_MIPS3,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r4600",	1, bfd_mach_mips4600, CPU_R4600, ISA_MIPS3,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r4650",	1, bfd_mach_mips4650, CPU_R4650, ISA_MIPS3,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r5000",	1, bfd_mach_mips5000, CPU_R5000, ISA_MIPS4,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "vr5400",	1, bfd_mach_mips5400, CPU_VR5400, ISA_MIPS4,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "vr5500",	1, bfd_mach_mips5500, CPU_VR5500, ISA_MIPS4,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r6000",	1, bfd_mach_mips6000, CPU_R6000, ISA_MIPS2,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "rm7000",	1, bfd_mach_mips7000, CPU_RM7000, ISA_MIPS4,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "rm9000",	1, bfd_mach_mips7000, CPU_RM7000, ISA_MIPS4,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r8000",	1, bfd_mach_mips8000, CPU_R8000, ISA_MIPS4,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r10000",	1, bfd_mach_mips10000, CPU_R10000, ISA_MIPS4,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r12000",	1, bfd_mach_mips12000, CPU_R12000, ISA_MIPS4,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "mips5",	1, bfd_mach_mips5, CPU_MIPS5, ISA_MIPS5,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },

  /* For stock MIPS32, disassemble all applicable MIPS-specified ASEs.
     Note that MIPS-3D and MDMX are not applicable to MIPS32.  (See
     _MIPS32 Architecture For Programmers Volume I: Introduction to the
     MIPS32 Architecture_ (MIPS Document Number MD00082, Revision 0.95),
     page 1.  */
  { "mips32",	1, bfd_mach_mipsisa32, CPU_MIPS32,
    ISA_MIPS32 | INSN_MIPS16 | INSN_SMARTMIPS,
    mips_cp0_names_mips3264,
    mips_cp0sel_names_mips3264, ARRAY_SIZE (mips_cp0sel_names_mips3264),
    mips_hwr_names_numeric },

  { "mips32r2",	1, bfd_mach_mipsisa32r2, CPU_MIPS32R2,
    (ISA_MIPS32R2 | INSN_MIPS16 | INSN_SMARTMIPS | INSN_DSP | INSN_DSPR2
     | INSN_MIPS3D | INSN_MT),
    mips_cp0_names_mips3264r2,
    mips_cp0sel_names_mips3264r2, ARRAY_SIZE (mips_cp0sel_names_mips3264r2),
    mips_hwr_names_mips3264r2 },

  /* For stock MIPS64, disassemble all applicable MIPS-specified ASEs.  */
  { "mips64",	1, bfd_mach_mipsisa64, CPU_MIPS64,
    ISA_MIPS64 | INSN_MIPS16 | INSN_MIPS3D | INSN_MDMX,
    mips_cp0_names_mips3264,
    mips_cp0sel_names_mips3264, ARRAY_SIZE (mips_cp0sel_names_mips3264),
    mips_hwr_names_numeric },

  { "mips64r2",	1, bfd_mach_mipsisa64r2, CPU_MIPS64R2,
    (ISA_MIPS64R2 | INSN_MIPS16 | INSN_MIPS3D | INSN_DSP | INSN_DSPR2
     | INSN_DSP64 | INSN_MT | INSN_MDMX),
    mips_cp0_names_mips3264r2,
    mips_cp0sel_names_mips3264r2, ARRAY_SIZE (mips_cp0sel_names_mips3264r2),
    mips_hwr_names_mips3264r2 },

  { "sb1",	1, bfd_mach_mips_sb1, CPU_SB1,
    ISA_MIPS64 | INSN_MIPS3D | INSN_SB1,
    mips_cp0_names_sb1,
    mips_cp0sel_names_sb1, ARRAY_SIZE (mips_cp0sel_names_sb1),
    mips_hwr_names_numeric },

  { "octeon",   1, bfd_mach_mips_octeon, CPU_OCTEON,
    ISA_MIPS64R2 | INSN_OCTEON, mips_cp0_names_octeon,
    mips_cp0sel_names_octeon, ARRAY_SIZE (mips_cp0sel_names_octeon),
    mips_hwr_names_numeric },

  /* This entry, mips16, is here only for ISA/processor selection; do
     not print its name.  */
  { "",		1, bfd_mach_mips16, CPU_MIPS16, ISA_MIPS3 | INSN_MIPS16,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
};

/* ISA and processor type to disassemble for, and register names to use.
   set_default_mips_dis_options and parse_mips_dis_options fill in these
   values.  */
static int mips_processor;
static int mips_isa;
static const char * const *mips_gpr_names;
static const char * const *mips_fpr_names;
static const char * const *mips_cp0_names;
static const struct mips_cp0sel_name *mips_cp0sel_names;
static int mips_cp0sel_names_len;
static const char * const *mips_hwr_names;

/* Other options */
static int no_aliases;	/* If set disassemble as most general inst.  */

static const struct mips_abi_choice *
choose_abi_by_name (const char *name, unsigned int namelen)
{
  const struct mips_abi_choice *c;
  unsigned int i;

  for (i = 0, c = NULL; i < ARRAY_SIZE (mips_abi_choices) && c == NULL; i++)
    if (strncmp (mips_abi_choices[i].name, name, namelen) == 0
	&& strlen (mips_abi_choices[i].name) == namelen)
      c = &mips_abi_choices[i];

  return c;
}

static const struct mips_arch_choice *
choose_arch_by_name (const char *name, unsigned int namelen)
{
  const struct mips_arch_choice *c = NULL;
  unsigned int i;

  for (i = 0, c = NULL; i < ARRAY_SIZE (mips_arch_choices) && c == NULL; i++)
    if (strncmp (mips_arch_choices[i].name, name, namelen) == 0
	&& strlen (mips_arch_choices[i].name) == namelen)
      c = &mips_arch_choices[i];

  return c;
}

static const struct mips_arch_choice *
choose_arch_by_number (unsigned long mach)
{
  static unsigned long hint_bfd_mach;
  static const struct mips_arch_choice *hint_arch_choice;
  const struct mips_arch_choice *c;
  unsigned int i;

  /* We optimize this because even if the user specifies no
     flags, this will be done for every instruction!  */
  if (hint_bfd_mach == mach
      && hint_arch_choice != NULL
      && hint_arch_choice->bfd_mach == hint_bfd_mach)
    return hint_arch_choice;

  for (i = 0, c = NULL; i < ARRAY_SIZE (mips_arch_choices) && c == NULL; i++)
    {
      if (mips_arch_choices[i].bfd_mach_valid
	  && mips_arch_choices[i].bfd_mach == mach)
	{
	  c = &mips_arch_choices[i];
	  hint_bfd_mach = mach;
	  hint_arch_choice = c;
	}
    }
  return c;
}

/* Check if the object uses NewABI conventions.  */

static int
is_newabi (Elf_Internal_Ehdr *header)
{
  /* There are no old-style ABIs which use 64-bit ELF.  */
  if (header->e_ident[EI_CLASS] == ELFCLASS64)
    return 1;

  /* If a 32-bit ELF file, n32 is a new-style ABI.  */
  if ((header->e_flags & EF_MIPS_ABI2) != 0)
    return 1;

  return 0;
}

static void
set_default_mips_dis_options (struct disassemble_info *info)
{
  const struct mips_arch_choice *chosen_arch;

  /* Defaults: mipsIII/r3000 (?!), (o)32-style ("oldabi") GPR names,
     and numeric FPR, CP0 register, and HWR names.  */
  mips_isa = ISA_MIPS3;
  mips_processor =  CPU_R3000;
  mips_gpr_names = mips_gpr_names_oldabi;
  mips_fpr_names = mips_fpr_names_numeric;
  mips_cp0_names = mips_cp0_names_numeric;
  mips_cp0sel_names = NULL;
  mips_cp0sel_names_len = 0;
  mips_hwr_names = mips_hwr_names_numeric;
  no_aliases = 0;

  /* If an ELF "newabi" binary, use the n32/(n)64 GPR names.  */
  if (info->flavour == bfd_target_elf_flavour && info->section != NULL)
    {
      Elf_Internal_Ehdr *header;

      header = elf_elfheader (info->section->owner);
      if (is_newabi (header))
	mips_gpr_names = mips_gpr_names_newabi;
    }

  /* Set ISA, architecture, and cp0 register names as best we can.  */
#if ! SYMTAB_AVAILABLE
  /* This is running out on a target machine, not in a host tool.
     FIXME: Where does mips_target_info come from?  */
  target_processor = mips_target_info.processor;
  mips_isa = mips_target_info.isa;
#else
  chosen_arch = choose_arch_by_number (info->mach);
  if (chosen_arch != NULL)
    {
      mips_processor = chosen_arch->processor;
      mips_isa = chosen_arch->isa;
      mips_cp0_names = chosen_arch->cp0_names;
      mips_cp0sel_names = chosen_arch->cp0sel_names;
      mips_cp0sel_names_len = chosen_arch->cp0sel_names_len;
      mips_hwr_names = chosen_arch->hwr_names;
    }
#endif
}

static void
parse_mips_dis_option (const char *option, unsigned int len)
{
  unsigned int i, optionlen, vallen;
  const char *val;
  const struct mips_abi_choice *chosen_abi;
  const struct mips_arch_choice *chosen_arch;

  if (strcmp ("octeon-useun", option) == 0)
    {
      octeon_use_unalign = 1;
      return;
    }
  if (strcmp ("no-octeon-useun", option) == 0)
    {
      octeon_use_unalign = 0;
      return;
    }

  /* Try to match options that are simple flags */
  if (CONST_STRNEQ (option, "no-aliases"))
    {
      no_aliases = 1;
      return;
    }
  
  /* Look for the = that delimits the end of the option name.  */
  for (i = 0; i < len; i++)
    if (option[i] == '=')
      break;

  if (i == 0)		/* Invalid option: no name before '='.  */
    return;
  if (i == len)		/* Invalid option: no '='.  */
    return;
  if (i == (len - 1))	/* Invalid option: no value after '='.  */
    return;

  optionlen = i;
  val = option + (optionlen + 1);
  vallen = len - (optionlen + 1);

  if (strncmp ("gpr-names", option, optionlen) == 0
      && strlen ("gpr-names") == optionlen)
    {
      chosen_abi = choose_abi_by_name (val, vallen);
      if (chosen_abi != NULL)
	mips_gpr_names = chosen_abi->gpr_names;
      return;
    }

  if (strncmp ("fpr-names", option, optionlen) == 0
      && strlen ("fpr-names") == optionlen)
    {
      chosen_abi = choose_abi_by_name (val, vallen);
      if (chosen_abi != NULL)
	mips_fpr_names = chosen_abi->fpr_names;
      return;
    }

  if (strncmp ("cp0-names", option, optionlen) == 0
      && strlen ("cp0-names") == optionlen)
    {
      chosen_arch = choose_arch_by_name (val, vallen);
      if (chosen_arch != NULL)
	{
	  mips_cp0_names = chosen_arch->cp0_names;
	  mips_cp0sel_names = chosen_arch->cp0sel_names;
	  mips_cp0sel_names_len = chosen_arch->cp0sel_names_len;
	}
      return;
    }

  if (strncmp ("hwr-names", option, optionlen) == 0
      && strlen ("hwr-names") == optionlen)
    {
      chosen_arch = choose_arch_by_name (val, vallen);
      if (chosen_arch != NULL)
	mips_hwr_names = chosen_arch->hwr_names;
      return;
    }

  if (strncmp ("reg-names", option, optionlen) == 0
      && strlen ("reg-names") == optionlen)
    {
      /* We check both ABI and ARCH here unconditionally, so
	 that "numeric" will do the desirable thing: select
	 numeric register names for all registers.  Other than
	 that, a given name probably won't match both.  */
      chosen_abi = choose_abi_by_name (val, vallen);
      if (chosen_abi != NULL)
	{
	  mips_gpr_names = chosen_abi->gpr_names;
	  mips_fpr_names = chosen_abi->fpr_names;
	}
      chosen_arch = choose_arch_by_name (val, vallen);
      if (chosen_arch != NULL)
	{
	  mips_cp0_names = chosen_arch->cp0_names;
	  mips_cp0sel_names = chosen_arch->cp0sel_names;
	  mips_cp0sel_names_len = chosen_arch->cp0sel_names_len;
	  mips_hwr_names = chosen_arch->hwr_names;
	}
      return;
    }

  /* Invalid option.  */
}

static void
parse_mips_dis_options (const char *options)
{
  const char *option_end;

  if (options == NULL)
    return;

  while (*options != '\0')
    {
      /* Skip empty options.  */
      if (*options == ',')
	{
	  options++;
	  continue;
	}

      /* We know that *options is neither NUL or a comma.  */
      option_end = options + 1;
      while (*option_end != ',' && *option_end != '\0')
	option_end++;

      parse_mips_dis_option (options, option_end - options);

      /* Go on to the next one.  If option_end points to a comma, it
	 will be skipped above.  */
      options = option_end;
    }
}

static const struct mips_cp0sel_name *
lookup_mips_cp0sel_name (const struct mips_cp0sel_name *names,
			 unsigned int len,
			 unsigned int cp0reg,
			 unsigned int sel)
{
  unsigned int i;

  for (i = 0; i < len; i++)
    if (names[i].cp0reg == cp0reg && names[i].sel == sel)
      return &names[i];
  return NULL;
}

/* Print insn arguments for 32/64-bit code.  */

static void
print_insn_args (const char *d,
		 register unsigned long int l,
		 bfd_vma pc,
		 struct disassemble_info *info,
		 const struct mips_opcode *opp)
{
  int op, delta;
  unsigned int lsb, msb, msbd;

  lsb = 0;

  for (; *d != '\0'; d++)
    {
      switch (*d)
	{
	case ',':
	case '(':
	case ')':
	case '[':
	case ']':
	  (*info->fprintf_func) (info->stream, "%c", *d);
	  break;

	case '+':
	  /* Extension character; switch for second char.  */
	  d++;
	  switch (*d)
	    {
	    case '\0':
	      /* xgettext:c-format */
	      (*info->fprintf_func) (info->stream,
				     _("# internal error, incomplete extension sequence (+)"));
	      return;

	    case 'A':
	      lsb = (l >> OP_SH_SHAMT) & OP_MASK_SHAMT;
	      (*info->fprintf_func) (info->stream, "0x%x", lsb);
	      break;
	
	    case 'B':
	      msb = (l >> OP_SH_INSMSB) & OP_MASK_INSMSB;
	      (*info->fprintf_func) (info->stream, "0x%x", msb - lsb + 1);
	      break;

	    case '1':
	      (*info->fprintf_func) (info->stream, "0x%lx",
				     (l >> OP_SH_UDI1) & OP_MASK_UDI1);
	      break;
	      
	    case '2':
	      (*info->fprintf_func) (info->stream, "0x%lx",
				     (l >> OP_SH_UDI2) & OP_MASK_UDI2);
	      break;
	      
	    case '3':
	      (*info->fprintf_func) (info->stream, "0x%lx",
				     (l >> OP_SH_UDI3) & OP_MASK_UDI3);
	      break;
      
	    case '4':
	      (*info->fprintf_func) (info->stream, "0x%lx",
				     (l >> OP_SH_UDI4) & OP_MASK_UDI4);
	      break;
	      
	    case 'C':
	    case 'H':
	      msbd = (l >> OP_SH_EXTMSBD) & OP_MASK_EXTMSBD;
	      (*info->fprintf_func) (info->stream, "0x%x", msbd + 1);
	      break;

	    case 'D':
	      {
		const struct mips_cp0sel_name *n;
		unsigned int cp0reg, sel;

		cp0reg = (l >> OP_SH_RD) & OP_MASK_RD;
		sel = (l >> OP_SH_SEL) & OP_MASK_SEL;

		/* CP0 register including 'sel' code for mtcN (et al.), to be
		   printed textually if known.  If not known, print both
		   CP0 register name and sel numerically since CP0 register
		   with sel 0 may have a name unrelated to register being
		   printed.  */
		n = lookup_mips_cp0sel_name(mips_cp0sel_names,
					    mips_cp0sel_names_len, cp0reg, sel);
		if (n != NULL)
		  (*info->fprintf_func) (info->stream, "%s", n->name);
		else
		  (*info->fprintf_func) (info->stream, "$%d,%d", cp0reg, sel);
		break;
	      }

	    case 'E':
	      lsb = ((l >> OP_SH_SHAMT) & OP_MASK_SHAMT) + 32;
	      (*info->fprintf_func) (info->stream, "0x%x", lsb);
	      break;
	
	    case 'F':
	      msb = ((l >> OP_SH_INSMSB) & OP_MASK_INSMSB) + 32;
	      (*info->fprintf_func) (info->stream, "0x%x", msb - lsb + 1);
	      break;

	    case 'G':
	      msbd = ((l >> OP_SH_EXTMSBD) & OP_MASK_EXTMSBD) + 32;
	      (*info->fprintf_func) (info->stream, "0x%x", msbd + 1);
	      break;

	    case 't': /* Coprocessor 0 reg name */
	      (*info->fprintf_func) (info->stream, "%s",
				     mips_cp0_names[(l >> OP_SH_RT) &
						     OP_MASK_RT]);
	      break;

	    case 'T': /* Coprocessor 0 reg name */
	      {
		const struct mips_cp0sel_name *n;
		unsigned int cp0reg, sel;

		cp0reg = (l >> OP_SH_RT) & OP_MASK_RT;
		sel = (l >> OP_SH_SEL) & OP_MASK_SEL;

		/* CP0 register including 'sel' code for mftc0, to be
		   printed textually if known.  If not known, print both
		   CP0 register name and sel numerically since CP0 register
		   with sel 0 may have a name unrelated to register being
		   printed.  */
		n = lookup_mips_cp0sel_name(mips_cp0sel_names,
					    mips_cp0sel_names_len, cp0reg, sel);
		if (n != NULL)
		  (*info->fprintf_func) (info->stream, "%s", n->name);
		else
		  (*info->fprintf_func) (info->stream, "$%d,%d", cp0reg, sel);
		break;
	      }

	    default:
	      /* xgettext:c-format */
	      (*info->fprintf_func) (info->stream,
				     _("# internal error, undefined extension sequence (+%c)"),
				     *d);
	      return;
	    }
	  break;

	case '2':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_BP) & OP_MASK_BP);
	  break;

	case '3':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_SA3) & OP_MASK_SA3);
	  break;

	case '4':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_SA4) & OP_MASK_SA4);
	  break;

	case '5':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_IMM8) & OP_MASK_IMM8);
	  break;

	case '6':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_RS) & OP_MASK_RS);
	  break;

	case '7':
	  (*info->fprintf_func) (info->stream, "$ac%ld",
				 (l >> OP_SH_DSPACC) & OP_MASK_DSPACC);
	  break;

	case '8':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_WRDSP) & OP_MASK_WRDSP);
	  break;

	case '9':
	  (*info->fprintf_func) (info->stream, "$ac%ld",
				 (l >> OP_SH_DSPACC_S) & OP_MASK_DSPACC_S);
	  break;

	case '0': /* dsp 6-bit signed immediate in bit 20 */
	  delta = ((l >> OP_SH_DSPSFT) & OP_MASK_DSPSFT);
	  if (delta & 0x20) /* test sign bit */
	    delta |= ~OP_MASK_DSPSFT;
	  (*info->fprintf_func) (info->stream, "%d", delta);
	  break;

	case ':': /* dsp 7-bit signed immediate in bit 19 */
	  delta = ((l >> OP_SH_DSPSFT_7) & OP_MASK_DSPSFT_7);
	  if (delta & 0x40) /* test sign bit */
	    delta |= ~OP_MASK_DSPSFT_7;
	  (*info->fprintf_func) (info->stream, "%d", delta);
	  break;

	case '\'':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_RDDSP) & OP_MASK_RDDSP);
	  break;

	case '@': /* dsp 10-bit signed immediate in bit 16 */
	  delta = ((l >> OP_SH_IMM10) & OP_MASK_IMM10);
	  if (delta & 0x200) /* test sign bit */
	    delta |= ~OP_MASK_IMM10;
	  (*info->fprintf_func) (info->stream, "%d", delta);
	  break;

	case '!':
	  (*info->fprintf_func) (info->stream, "%ld",
				 (l >> OP_SH_MT_U) & OP_MASK_MT_U);
	  break;

	case '$':
	  (*info->fprintf_func) (info->stream, "%ld",
				 (l >> OP_SH_MT_H) & OP_MASK_MT_H);
	  break;

	case '*':
	  (*info->fprintf_func) (info->stream, "$ac%ld",
				 (l >> OP_SH_MTACC_T) & OP_MASK_MTACC_T);
	  break;

	case '&':
	  (*info->fprintf_func) (info->stream, "$ac%ld",
				 (l >> OP_SH_MTACC_D) & OP_MASK_MTACC_D);
	  break;

	case 'g':
	  /* Coprocessor register for CTTC1, MTTC2, MTHC2, CTTC2.  */
	  (*info->fprintf_func) (info->stream, "$%ld",
				 (l >> OP_SH_RD) & OP_MASK_RD);
	  break;

	case 's':
	case 'b':
	case 'r':
	case 'v':
	  (*info->fprintf_func) (info->stream, "%s",
				 mips_gpr_names[(l >> OP_SH_RS) & OP_MASK_RS]);
	  break;

	case 't':
	case 'w':
	  (*info->fprintf_func) (info->stream, "%s",
				 mips_gpr_names[(l >> OP_SH_RT) & OP_MASK_RT]);
	  break;

	case 'i':
	case 'u':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_IMMEDIATE) & OP_MASK_IMMEDIATE);
	  break;

	case 'j': /* Same as i, but sign-extended.  */
	case 'o':
	  delta = (l >> OP_SH_DELTA) & OP_MASK_DELTA;
	  if (delta & 0x8000)
	    delta |= ~0xffff;
	  (*info->fprintf_func) (info->stream, "%d",
				 delta);
	  break;

	case 'h':
	  (*info->fprintf_func) (info->stream, "0x%x",
				 (unsigned int) ((l >> OP_SH_PREFX)
						 & OP_MASK_PREFX));
	  break;

	case 'k':
	  (*info->fprintf_func) (info->stream, "0x%x",
				 (unsigned int) ((l >> OP_SH_CACHE)
						 & OP_MASK_CACHE));
	  break;

	case 'a':
	  info->target = (((pc + 4) & ~(bfd_vma) 0x0fffffff)
			  | (((l >> OP_SH_TARGET) & OP_MASK_TARGET) << 2));
	  /* For gdb disassembler, force odd address on jalx.  */
	  if (info->flavour == bfd_target_unknown_flavour
	      && strcmp (opp->name, "jalx") == 0)
	    info->target |= 1;
	  (*info->print_address_func) (info->target, info);
	  break;

	case 'p':
	  /* Sign extend the displacement.  */
	  delta = (l >> OP_SH_DELTA) & OP_MASK_DELTA;
	  if (delta & 0x8000)
	    delta |= ~0xffff;
	  info->target = (delta << 2) + pc + INSNLEN;
	  (*info->print_address_func) (info->target, info);
	  break;

	case 'd':
	  (*info->fprintf_func) (info->stream, "%s",
				 mips_gpr_names[(l >> OP_SH_RD) & OP_MASK_RD]);
	  break;

	case 'U':
	  {
	    /* First check for both rd and rt being equal.  */
	    unsigned int reg = (l >> OP_SH_RD) & OP_MASK_RD;
	    if (reg == ((l >> OP_SH_RT) & OP_MASK_RT))
	      (*info->fprintf_func) (info->stream, "%s",
				     mips_gpr_names[reg]);
	    else
	      {
		/* If one is zero use the other.  */
		if (reg == 0)
		  (*info->fprintf_func) (info->stream, "%s",
					 mips_gpr_names[(l >> OP_SH_RT) & OP_MASK_RT]);
		else if (((l >> OP_SH_RT) & OP_MASK_RT) == 0)
		  (*info->fprintf_func) (info->stream, "%s",
					 mips_gpr_names[reg]);
		else /* Bogus, result depends on processor.  */
		  (*info->fprintf_func) (info->stream, "%s or %s",
					 mips_gpr_names[reg],
					 mips_gpr_names[(l >> OP_SH_RT) & OP_MASK_RT]);
	      }
	  }
	  break;

	case 'z':
	  (*info->fprintf_func) (info->stream, "%s", mips_gpr_names[0]);
	  break;

	case '<':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_SHAMT) & OP_MASK_SHAMT);
	  break;

	case 'c':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_CODE) & OP_MASK_CODE);
	  break;

	case 'q':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_CODE2) & OP_MASK_CODE2);
	  break;

	/* Display 5 bits of bbit0/1 bit index amount. */
	case '^':
          (*info->fprintf_func) (info->stream, "0x%lx",
                                 (l >> OP_SH_BITIND) & OP_MASK_BITIND);
          break;

	/* Display 10 bits signed constant from seqi/snei instruction. */
	case 'y':
          {
            int imm = (l >> OP_SH_CODE2) & OP_MASK_CODE2;
            imm <<= 22;
            imm >>= 22;
            (*info->fprintf_func) (info->stream, "%d", imm);
          }
          break;

	case 'C':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_COPZ) & OP_MASK_COPZ);
	  break;

	case 'B':
	  (*info->fprintf_func) (info->stream, "0x%lx",

				 (l >> OP_SH_CODE20) & OP_MASK_CODE20);
	  break;

	case 'J':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_CODE19) & OP_MASK_CODE19);
	  break;

	case 'S':
	case 'V':
	  (*info->fprintf_func) (info->stream, "%s",
				 mips_fpr_names[(l >> OP_SH_FS) & OP_MASK_FS]);
	  break;

	case 'T':
	case 'W':
	  (*info->fprintf_func) (info->stream, "%s",
				 mips_fpr_names[(l >> OP_SH_FT) & OP_MASK_FT]);
	  break;

	case 'D':
	  (*info->fprintf_func) (info->stream, "%s",
				 mips_fpr_names[(l >> OP_SH_FD) & OP_MASK_FD]);
	  break;

	case 'R':
	  (*info->fprintf_func) (info->stream, "%s",
				 mips_fpr_names[(l >> OP_SH_FR) & OP_MASK_FR]);
	  break;

	case 'E':
	  /* Coprocessor register for lwcN instructions, et al.

	     Note that there is no load/store cp0 instructions, and
	     that FPU (cp1) instructions disassemble this field using
	     'T' format.  Therefore, until we gain understanding of
	     cp2 register names, we can simply print the register
	     numbers.  */
	  (*info->fprintf_func) (info->stream, "$%ld",
				 (l >> OP_SH_RT) & OP_MASK_RT);
	  break;

	case 'G':
	  /* Coprocessor register for mtcN instructions, et al.  Note
	     that FPU (cp1) instructions disassemble this field using
	     'S' format.  Therefore, we only need to worry about cp0,
	     cp2, and cp3.  */
	  op = (l >> OP_SH_OP) & OP_MASK_OP;
	  if (op == OP_OP_COP0)
	    (*info->fprintf_func) (info->stream, "%s",
				   mips_cp0_names[(l >> OP_SH_RD) & OP_MASK_RD]);
	  else
	    (*info->fprintf_func) (info->stream, "$%ld",
				   (l >> OP_SH_RD) & OP_MASK_RD);
	  break;

	case 'K':
	  (*info->fprintf_func) (info->stream, "%s",
				 mips_hwr_names[(l >> OP_SH_RD) & OP_MASK_RD]);
	  break;

	case 'N':
	  (*info->fprintf_func) (info->stream,
				 ((opp->pinfo & (FP_D | FP_S)) != 0
				  ? "$fcc%ld" : "$cc%ld"),
				 (l >> OP_SH_BCC) & OP_MASK_BCC);
	  break;

	case 'M':
	  (*info->fprintf_func) (info->stream, "$fcc%ld",
				 (l >> OP_SH_CCC) & OP_MASK_CCC);
	  break;

	case 'P':
	  (*info->fprintf_func) (info->stream, "%ld",
				 (l >> OP_SH_PERFREG) & OP_MASK_PERFREG);
	  break;

	case 'e':
	  (*info->fprintf_func) (info->stream, "%ld",
				 (l >> OP_SH_VECBYTE) & OP_MASK_VECBYTE);
	  break;

	case '%':
	  (*info->fprintf_func) (info->stream, "%ld",
				 (l >> OP_SH_VECALIGN) & OP_MASK_VECALIGN);
	  break;

	case 'H':
	  (*info->fprintf_func) (info->stream, "%ld",
				 (l >> OP_SH_SEL) & OP_MASK_SEL);
	  break;

	case 'O':
	  (*info->fprintf_func) (info->stream, "%ld",
				 (l >> OP_SH_ALN) & OP_MASK_ALN);
	  break;

	case 'Q':
	  {
	    unsigned int vsel = (l >> OP_SH_VSEL) & OP_MASK_VSEL;

	    if ((vsel & 0x10) == 0)
	      {
		int fmt;

		vsel &= 0x0f;
		for (fmt = 0; fmt < 3; fmt++, vsel >>= 1)
		  if ((vsel & 1) == 0)
		    break;
		(*info->fprintf_func) (info->stream, "$v%ld[%d]",
				       (l >> OP_SH_FT) & OP_MASK_FT,
				       vsel >> 1);
	      }
	    else if ((vsel & 0x08) == 0)
	      {
		(*info->fprintf_func) (info->stream, "$v%ld",
				       (l >> OP_SH_FT) & OP_MASK_FT);
	      }
	    else
	      {
		(*info->fprintf_func) (info->stream, "0x%lx",
				       (l >> OP_SH_FT) & OP_MASK_FT);
	      }
	  }
	  break;

	case 'X':
	  (*info->fprintf_func) (info->stream, "$v%ld",
				 (l >> OP_SH_FD) & OP_MASK_FD);
	  break;

	case 'Y':
	  (*info->fprintf_func) (info->stream, "$v%ld",
				 (l >> OP_SH_FS) & OP_MASK_FS);
	  break;

	case 'Z':
	  (*info->fprintf_func) (info->stream, "$v%ld",
				 (l >> OP_SH_FT) & OP_MASK_FT);
	  break;

	default:
	  /* xgettext:c-format */
	  (*info->fprintf_func) (info->stream,
				 _("# internal error, undefined modifier(%c)"),
				 *d);
	  return;
	}
    }
}

/* Print the mips instruction at address MEMADDR in debugged memory,
   on using INFO.  Returns length of the instruction, in bytes, which is
   always INSNLEN.  BIGENDIAN must be 1 if this is big-endian code, 0 if
   this is little-endian code.  */

static int
print_insn_mips (bfd_vma memaddr,
		 unsigned long int word,
		 struct disassemble_info *info)
{
  const struct mips_opcode *op;
  static bfd_boolean init = 0;
  static const struct mips_opcode *mips_hash[OP_MASK_OP + 1];

  /* Build a hash table to shorten the search time.  */
  if (! init)
    {
      unsigned int i;

      for (i = 0; i <= OP_MASK_OP; i++)
	{
	  for (op = mips_opcodes; op < &mips_opcodes[NUMOPCODES]; op++)
	    {
	      if (op->pinfo == INSN_MACRO
		  || (no_aliases && (op->pinfo2 & INSN2_ALIAS)))
		continue;
	      if (i == ((op->match >> OP_SH_OP) & OP_MASK_OP))
		{
		  mips_hash[i] = op;
		  break;
		}
	    }
	}

      init = 1;
    }

  info->bytes_per_chunk = INSNLEN;
  info->display_endian = info->endian;
  info->insn_info_valid = 1;
  info->branch_delay_insns = 0;
  info->data_size = 0;
  info->insn_type = dis_nonbranch;
  info->target = 0;
  info->target2 = 0;

  op = mips_hash[(word >> OP_SH_OP) & OP_MASK_OP];
  if (op != NULL)
    {
      for (; op < &mips_opcodes[NUMOPCODES]; op++)
	{
	  if (op->pinfo != INSN_MACRO 
	      && !(no_aliases && (op->pinfo2 & INSN2_ALIAS))
	      && (word & op->mask) == op->match)
	    {
	      const char *d;

	      /* We always allow to disassemble the jalx instruction.  */
	      if (! OPCODE_IS_MEMBER (op, mips_isa, mips_processor)
		  && strcmp (op->name, "jalx"))
		continue;

	      if (info->mach == CPU_OCTEON && octeon_use_unalign)
                {
                  if (strcmp (op->name, "lwl") == 0
                      || strcmp (op->name, "ldl") == 0
                      || strcmp (op->name, "swl") == 0
                      || strcmp (op->name, "sdl") == 0
                      || strcmp (op->name, "lcache") == 0
                      || strcmp (op->name, "scache") == 0
                      || strcmp (op->name, "flush") == 0)
                    continue;
                                                                                
                  if (strcmp (op->name, "ldr") == 0
                       || strcmp (op->name, "lwr") == 0
                       || strcmp (op->name, "swr") == 0
                       || strcmp (op->name, "sdr") == 0)
                    {
                      (*info->fprintf_func) (info->stream, "nop");
                      return INSNLEN;
                    }
                }

	      /* Figure out instruction type and branch delay information.  */
	      if ((op->pinfo & INSN_UNCOND_BRANCH_DELAY) != 0)
	        {
		  if ((info->insn_type & INSN_WRITE_GPR_31) != 0)
		    info->insn_type = dis_jsr;
		  else
		    info->insn_type = dis_branch;
		  info->branch_delay_insns = 1;
		}
	      else if ((op->pinfo & (INSN_COND_BRANCH_DELAY
				     | INSN_COND_BRANCH_LIKELY)) != 0)
		{
		  if ((info->insn_type & INSN_WRITE_GPR_31) != 0)
		    info->insn_type = dis_condjsr;
		  else
		    info->insn_type = dis_condbranch;
		  info->branch_delay_insns = 1;
		}
	      else if ((op->pinfo & (INSN_STORE_MEMORY
				     | INSN_LOAD_MEMORY_DELAY)) != 0)
		info->insn_type = dis_dref;

	      (*info->fprintf_func) (info->stream, "%s", op->name);

	      d = op->args;
	      if (d != NULL && *d != '\0')
		{
		  (*info->fprintf_func) (info->stream, "\t");
		  print_insn_args (d, word, memaddr, info, op);
		}

	      return INSNLEN;
	    }
	}
    }

  /* Handle undefined instructions.  */
  info->insn_type = dis_noninsn;
  (*info->fprintf_func) (info->stream, "0x%lx", word);
  return INSNLEN;
}

/* Disassemble an operand for a mips16 instruction.  */

static void
print_mips16_insn_arg (char type,
		       const struct mips_opcode *op,
		       int l,
		       bfd_boolean use_extend,
		       int extend,
		       bfd_vma memaddr,
		       struct disassemble_info *info)
{
  switch (type)
    {
    case ',':
    case '(':
    case ')':
      (*info->fprintf_func) (info->stream, "%c", type);
      break;

    case 'y':
    case 'w':
      (*info->fprintf_func) (info->stream, "%s",
			     mips16_reg_names(((l >> MIPS16OP_SH_RY)
					       & MIPS16OP_MASK_RY)));
      break;

    case 'x':
    case 'v':
      (*info->fprintf_func) (info->stream, "%s",
			     mips16_reg_names(((l >> MIPS16OP_SH_RX)
					       & MIPS16OP_MASK_RX)));
      break;

    case 'z':
      (*info->fprintf_func) (info->stream, "%s",
			     mips16_reg_names(((l >> MIPS16OP_SH_RZ)
					       & MIPS16OP_MASK_RZ)));
      break;

    case 'Z':
      (*info->fprintf_func) (info->stream, "%s",
			     mips16_reg_names(((l >> MIPS16OP_SH_MOVE32Z)
					       & MIPS16OP_MASK_MOVE32Z)));
      break;

    case '0':
      (*info->fprintf_func) (info->stream, "%s", mips_gpr_names[0]);
      break;

    case 'S':
      (*info->fprintf_func) (info->stream, "%s", mips_gpr_names[29]);
      break;

    case 'P':
      (*info->fprintf_func) (info->stream, "$pc");
      break;

    case 'R':
      (*info->fprintf_func) (info->stream, "%s", mips_gpr_names[31]);
      break;

    case 'X':
      (*info->fprintf_func) (info->stream, "%s",
			     mips_gpr_names[((l >> MIPS16OP_SH_REGR32)
					    & MIPS16OP_MASK_REGR32)]);
      break;

    case 'Y':
      (*info->fprintf_func) (info->stream, "%s",
			     mips_gpr_names[MIPS16OP_EXTRACT_REG32R (l)]);
      break;

    case '<':
    case '>':
    case '[':
    case ']':
    case '4':
    case '5':
    case 'H':
    case 'W':
    case 'D':
    case 'j':
    case '6':
    case '8':
    case 'V':
    case 'C':
    case 'U':
    case 'k':
    case 'K':
    case 'p':
    case 'q':
    case 'A':
    case 'B':
    case 'E':
      {
	int immed, nbits, shift, signedp, extbits, pcrel, extu, branch;

	shift = 0;
	signedp = 0;
	extbits = 16;
	pcrel = 0;
	extu = 0;
	branch = 0;
	switch (type)
	  {
	  case '<':
	    nbits = 3;
	    immed = (l >> MIPS16OP_SH_RZ) & MIPS16OP_MASK_RZ;
	    extbits = 5;
	    extu = 1;
	    break;
	  case '>':
	    nbits = 3;
	    immed = (l >> MIPS16OP_SH_RX) & MIPS16OP_MASK_RX;
	    extbits = 5;
	    extu = 1;
	    break;
	  case '[':
	    nbits = 3;
	    immed = (l >> MIPS16OP_SH_RZ) & MIPS16OP_MASK_RZ;
	    extbits = 6;
	    extu = 1;
	    break;
	  case ']':
	    nbits = 3;
	    immed = (l >> MIPS16OP_SH_RX) & MIPS16OP_MASK_RX;
	    extbits = 6;
	    extu = 1;
	    break;
	  case '4':
	    nbits = 4;
	    immed = (l >> MIPS16OP_SH_IMM4) & MIPS16OP_MASK_IMM4;
	    signedp = 1;
	    extbits = 15;
	    break;
	  case '5':
	    nbits = 5;
	    immed = (l >> MIPS16OP_SH_IMM5) & MIPS16OP_MASK_IMM5;
	    info->insn_type = dis_dref;
	    info->data_size = 1;
	    break;
	  case 'H':
	    nbits = 5;
	    shift = 1;
	    immed = (l >> MIPS16OP_SH_IMM5) & MIPS16OP_MASK_IMM5;
	    info->insn_type = dis_dref;
	    info->data_size = 2;
	    break;
	  case 'W':
	    nbits = 5;
	    shift = 2;
	    immed = (l >> MIPS16OP_SH_IMM5) & MIPS16OP_MASK_IMM5;
	    if ((op->pinfo & MIPS16_INSN_READ_PC) == 0
		&& (op->pinfo & MIPS16_INSN_READ_SP) == 0)
	      {
		info->insn_type = dis_dref;
		info->data_size = 4;
	      }
	    break;
	  case 'D':
	    nbits = 5;
	    shift = 3;
	    immed = (l >> MIPS16OP_SH_IMM5) & MIPS16OP_MASK_IMM5;
	    info->insn_type = dis_dref;
	    info->data_size = 8;
	    break;
	  case 'j':
	    nbits = 5;
	    immed = (l >> MIPS16OP_SH_IMM5) & MIPS16OP_MASK_IMM5;
	    signedp = 1;
	    break;
	  case '6':
	    nbits = 6;
	    immed = (l >> MIPS16OP_SH_IMM6) & MIPS16OP_MASK_IMM6;
	    break;
	  case '8':
	    nbits = 8;
	    immed = (l >> MIPS16OP_SH_IMM8) & MIPS16OP_MASK_IMM8;
	    break;
	  case 'V':
	    nbits = 8;
	    shift = 2;
	    immed = (l >> MIPS16OP_SH_IMM8) & MIPS16OP_MASK_IMM8;
	    /* FIXME: This might be lw, or it might be addiu to $sp or
               $pc.  We assume it's load.  */
	    info->insn_type = dis_dref;
	    info->data_size = 4;
	    break;
	  case 'C':
	    nbits = 8;
	    shift = 3;
	    immed = (l >> MIPS16OP_SH_IMM8) & MIPS16OP_MASK_IMM8;
	    info->insn_type = dis_dref;
	    info->data_size = 8;
	    break;
	  case 'U':
	    nbits = 8;
	    immed = (l >> MIPS16OP_SH_IMM8) & MIPS16OP_MASK_IMM8;
	    extu = 1;
	    break;
	  case 'k':
	    nbits = 8;
	    immed = (l >> MIPS16OP_SH_IMM8) & MIPS16OP_MASK_IMM8;
	    signedp = 1;
	    break;
	  case 'K':
	    nbits = 8;
	    shift = 3;
	    immed = (l >> MIPS16OP_SH_IMM8) & MIPS16OP_MASK_IMM8;
	    signedp = 1;
	    break;
	  case 'p':
	    nbits = 8;
	    immed = (l >> MIPS16OP_SH_IMM8) & MIPS16OP_MASK_IMM8;
	    signedp = 1;
	    pcrel = 1;
	    branch = 1;
	    info->insn_type = dis_condbranch;
	    break;
	  case 'q':
	    nbits = 11;
	    immed = (l >> MIPS16OP_SH_IMM11) & MIPS16OP_MASK_IMM11;
	    signedp = 1;
	    pcrel = 1;
	    branch = 1;
	    info->insn_type = dis_branch;
	    break;
	  case 'A':
	    nbits = 8;
	    shift = 2;
	    immed = (l >> MIPS16OP_SH_IMM8) & MIPS16OP_MASK_IMM8;
	    pcrel = 1;
	    /* FIXME: This can be lw or la.  We assume it is lw.  */
	    info->insn_type = dis_dref;
	    info->data_size = 4;
	    break;
	  case 'B':
	    nbits = 5;
	    shift = 3;
	    immed = (l >> MIPS16OP_SH_IMM5) & MIPS16OP_MASK_IMM5;
	    pcrel = 1;
	    info->insn_type = dis_dref;
	    info->data_size = 8;
	    break;
	  case 'E':
	    nbits = 5;
	    shift = 2;
	    immed = (l >> MIPS16OP_SH_IMM5) & MIPS16OP_MASK_IMM5;
	    pcrel = 1;
	    break;
	  default:
	    abort ();
	  }

	if (! use_extend)
	  {
	    if (signedp && immed >= (1 << (nbits - 1)))
	      immed -= 1 << nbits;
	    immed <<= shift;
	    if ((type == '<' || type == '>' || type == '[' || type == ']')
		&& immed == 0)
	      immed = 8;
	  }
	else
	  {
	    if (extbits == 16)
	      immed |= ((extend & 0x1f) << 11) | (extend & 0x7e0);
	    else if (extbits == 15)
	      immed |= ((extend & 0xf) << 11) | (extend & 0x7f0);
	    else
	      immed = ((extend >> 6) & 0x1f) | (extend & 0x20);
	    immed &= (1 << extbits) - 1;
	    if (! extu && immed >= (1 << (extbits - 1)))
	      immed -= 1 << extbits;
	  }

	if (! pcrel)
	  (*info->fprintf_func) (info->stream, "%d", immed);
	else
	  {
	    bfd_vma baseaddr;

	    if (branch)
	      {
		immed *= 2;
		baseaddr = memaddr + 2;
	      }
	    else if (use_extend)
	      baseaddr = memaddr - 2;
	    else
	      {
		int status;
		bfd_byte buffer[2];

		baseaddr = memaddr;

		/* If this instruction is in the delay slot of a jr
                   instruction, the base address is the address of the
                   jr instruction.  If it is in the delay slot of jalr
                   instruction, the base address is the address of the
                   jalr instruction.  This test is unreliable: we have
                   no way of knowing whether the previous word is
                   instruction or data.  */
		status = (*info->read_memory_func) (memaddr - 4, buffer, 2,
						    info);
		if (status == 0
		    && (((info->endian == BFD_ENDIAN_BIG
			  ? bfd_getb16 (buffer)
			  : bfd_getl16 (buffer))
			 & 0xf800) == 0x1800))
		  baseaddr = memaddr - 4;
		else
		  {
		    status = (*info->read_memory_func) (memaddr - 2, buffer,
							2, info);
		    if (status == 0
			&& (((info->endian == BFD_ENDIAN_BIG
			      ? bfd_getb16 (buffer)
			      : bfd_getl16 (buffer))
			     & 0xf81f) == 0xe800))
		      baseaddr = memaddr - 2;
		  }
	      }
	    info->target = (baseaddr & ~((1 << shift) - 1)) + immed;
	    if (pcrel && branch
		&& info->flavour == bfd_target_unknown_flavour)
	      /* For gdb disassembler, maintain odd address.  */
	      info->target |= 1;
	    (*info->print_address_func) (info->target, info);
	  }
      }
      break;

    case 'a':
      {
	int jalx = l & 0x400;

	if (! use_extend)
	  extend = 0;
	l = ((l & 0x1f) << 23) | ((l & 0x3e0) << 13) | (extend << 2);
	if (!jalx && info->flavour == bfd_target_unknown_flavour)
	  /* For gdb disassembler, maintain odd address.  */
	  l |= 1;
      }
      info->target = ((memaddr + 4) & ~(bfd_vma) 0x0fffffff) | l;
      (*info->print_address_func) (info->target, info);
      info->insn_type = dis_jsr;
      info->branch_delay_insns = 1;
      break;

    case 'l':
    case 'L':
      {
	int need_comma, amask, smask;

	need_comma = 0;

	l = (l >> MIPS16OP_SH_IMM6) & MIPS16OP_MASK_IMM6;

	amask = (l >> 3) & 7;

	if (amask > 0 && amask < 5)
	  {
	    (*info->fprintf_func) (info->stream, "%s", mips_gpr_names[4]);
	    if (amask > 1)
	      (*info->fprintf_func) (info->stream, "-%s",
				     mips_gpr_names[amask + 3]);
	    need_comma = 1;
	  }

	smask = (l >> 1) & 3;
	if (smask == 3)
	  {
	    (*info->fprintf_func) (info->stream, "%s??",
				   need_comma ? "," : "");
	    need_comma = 1;
	  }
	else if (smask > 0)
	  {
	    (*info->fprintf_func) (info->stream, "%s%s",
				   need_comma ? "," : "",
				   mips_gpr_names[16]);
	    if (smask > 1)
	      (*info->fprintf_func) (info->stream, "-%s",
				     mips_gpr_names[smask + 15]);
	    need_comma = 1;
	  }

	if (l & 1)
	  {
	    (*info->fprintf_func) (info->stream, "%s%s",
				   need_comma ? "," : "",
				   mips_gpr_names[31]);
	    need_comma = 1;
	  }

	if (amask == 5 || amask == 6)
	  {
	    (*info->fprintf_func) (info->stream, "%s$f0",
				   need_comma ? "," : "");
	    if (amask == 6)
	      (*info->fprintf_func) (info->stream, "-$f1");
	  }
      }
      break;

    case 'm':
    case 'M':
      /* MIPS16e save/restore.  */
      {
      int need_comma = 0;
      int amask, args, statics;
      int nsreg, smask;
      int framesz;
      int i, j;

      l = l & 0x7f;
      if (use_extend)
        l |= extend << 16;

      amask = (l >> 16) & 0xf;
      if (amask == MIPS16_ALL_ARGS)
        {
          args = 4;
          statics = 0;
        }
      else if (amask == MIPS16_ALL_STATICS)
        {
          args = 0;
          statics = 4;
        }
      else
        {
          args = amask >> 2;
          statics = amask & 3;
        }

      if (args > 0) {
          (*info->fprintf_func) (info->stream, "%s", mips_gpr_names[4]);
          if (args > 1)
            (*info->fprintf_func) (info->stream, "-%s",
                                   mips_gpr_names[4 + args - 1]);
          need_comma = 1;
      }

      framesz = (((l >> 16) & 0xf0) | (l & 0x0f)) * 8;
      if (framesz == 0 && !use_extend)
        framesz = 128;

      (*info->fprintf_func) (info->stream, "%s%d", 
                             need_comma ? "," : "",
                             framesz);

      if (l & 0x40)                   /* $ra */
        (*info->fprintf_func) (info->stream, ",%s", mips_gpr_names[31]);

      nsreg = (l >> 24) & 0x7;
      smask = 0;
      if (l & 0x20)                   /* $s0 */
        smask |= 1 << 0;
      if (l & 0x10)                   /* $s1 */
        smask |= 1 << 1;
      if (nsreg > 0)                  /* $s2-$s8 */
        smask |= ((1 << nsreg) - 1) << 2;

      /* Find first set static reg bit.  */
      for (i = 0; i < 9; i++)
        {
          if (smask & (1 << i))
            {
              (*info->fprintf_func) (info->stream, ",%s",
                                     mips_gpr_names[i == 8 ? 30 : (16 + i)]);
              /* Skip over string of set bits.  */
              for (j = i; smask & (2 << j); j++)
                continue;
              if (j > i)
                (*info->fprintf_func) (info->stream, "-%s",
                                       mips_gpr_names[j == 8 ? 30 : (16 + j)]);
              i = j + 1;
            }
        }

      /* Statics $ax - $a3.  */
      if (statics == 1)
        (*info->fprintf_func) (info->stream, ",%s", mips_gpr_names[7]);
      else if (statics > 0) 
        (*info->fprintf_func) (info->stream, ",%s-%s", 
                               mips_gpr_names[7 - statics + 1],
                               mips_gpr_names[7]);
      }
      break;

    default:
      /* xgettext:c-format */
      (*info->fprintf_func)
	(info->stream,
	 _("# internal disassembler error, unrecognised modifier (%c)"),
	 type);
      abort ();
    }
}

/* Disassemble mips16 instructions.  */

static int
print_insn_mips16 (bfd_vma memaddr, struct disassemble_info *info)
{
  int status;
  bfd_byte buffer[2];
  int length;
  int insn;
  bfd_boolean use_extend;
  int extend = 0;
  const struct mips_opcode *op, *opend;

  info->bytes_per_chunk = 2;
  info->display_endian = info->endian;
  info->insn_info_valid = 1;
  info->branch_delay_insns = 0;
  info->data_size = 0;
  info->insn_type = dis_nonbranch;
  info->target = 0;
  info->target2 = 0;

  status = (*info->read_memory_func) (memaddr, buffer, 2, info);
  if (status != 0)
    {
      (*info->memory_error_func) (status, memaddr, info);
      return -1;
    }

  length = 2;

  if (info->endian == BFD_ENDIAN_BIG)
    insn = bfd_getb16 (buffer);
  else
    insn = bfd_getl16 (buffer);

  /* Handle the extend opcode specially.  */
  use_extend = FALSE;
  if ((insn & 0xf800) == 0xf000)
    {
      use_extend = TRUE;
      extend = insn & 0x7ff;

      memaddr += 2;

      status = (*info->read_memory_func) (memaddr, buffer, 2, info);
      if (status != 0)
	{
	  (*info->fprintf_func) (info->stream, "extend 0x%x",
				 (unsigned int) extend);
	  (*info->memory_error_func) (status, memaddr, info);
	  return -1;
	}

      if (info->endian == BFD_ENDIAN_BIG)
	insn = bfd_getb16 (buffer);
      else
	insn = bfd_getl16 (buffer);

      /* Check for an extend opcode followed by an extend opcode.  */
      if ((insn & 0xf800) == 0xf000)
	{
	  (*info->fprintf_func) (info->stream, "extend 0x%x",
				 (unsigned int) extend);
	  info->insn_type = dis_noninsn;
	  return length;
	}

      length += 2;
    }

  /* FIXME: Should probably use a hash table on the major opcode here.  */

  opend = mips16_opcodes + bfd_mips16_num_opcodes;
  for (op = mips16_opcodes; op < opend; op++)
    {
      if (op->pinfo != INSN_MACRO
	  && !(no_aliases && (op->pinfo2 & INSN2_ALIAS))
	  && (insn & op->mask) == op->match)
	{
	  const char *s;

	  if (strchr (op->args, 'a') != NULL)
	    {
	      if (use_extend)
		{
		  (*info->fprintf_func) (info->stream, "extend 0x%x",
					 (unsigned int) extend);
		  info->insn_type = dis_noninsn;
		  return length - 2;
		}

	      use_extend = FALSE;

	      memaddr += 2;

	      status = (*info->read_memory_func) (memaddr, buffer, 2,
						  info);
	      if (status == 0)
		{
		  use_extend = TRUE;
		  if (info->endian == BFD_ENDIAN_BIG)
		    extend = bfd_getb16 (buffer);
		  else
		    extend = bfd_getl16 (buffer);
		  length += 2;
		}
	    }

	  (*info->fprintf_func) (info->stream, "%s", op->name);
	  if (op->args[0] != '\0')
	    (*info->fprintf_func) (info->stream, "\t");

	  for (s = op->args; *s != '\0'; s++)
	    {
	      if (*s == ','
		  && s[1] == 'w'
		  && (((insn >> MIPS16OP_SH_RX) & MIPS16OP_MASK_RX)
		      == ((insn >> MIPS16OP_SH_RY) & MIPS16OP_MASK_RY)))
		{
		  /* Skip the register and the comma.  */
		  ++s;
		  continue;
		}
	      if (*s == ','
		  && s[1] == 'v'
		  && (((insn >> MIPS16OP_SH_RZ) & MIPS16OP_MASK_RZ)
		      == ((insn >> MIPS16OP_SH_RX) & MIPS16OP_MASK_RX)))
		{
		  /* Skip the register and the comma.  */
		  ++s;
		  continue;
		}
	      print_mips16_insn_arg (*s, op, insn, use_extend, extend, memaddr,
				     info);
	    }

	  if ((op->pinfo & INSN_UNCOND_BRANCH_DELAY) != 0)
	    {
	      info->branch_delay_insns = 1;
	      if (info->insn_type != dis_jsr)
		info->insn_type = dis_branch;
	    }

	  return length;
	}
    }

  if (use_extend)
    (*info->fprintf_func) (info->stream, "0x%x", extend | 0xf000);
  (*info->fprintf_func) (info->stream, "0x%x", insn);
  info->insn_type = dis_noninsn;

  return length;
}

/* In an environment where we do not know the symbol type of the
   instruction we are forced to assume that the low order bit of the
   instructions' address may mark it as a mips16 instruction.  If we
   are single stepping, or the pc is within the disassembled function,
   this works.  Otherwise, we need a clue.  Sometimes.  */

static int
_print_insn_mips (bfd_vma memaddr,
		  struct disassemble_info *info,
		  enum bfd_endian endianness)
{
  bfd_byte buffer[INSNLEN];
  int status;

  set_default_mips_dis_options (info);
  parse_mips_dis_options (info->disassembler_options);

#if 1
  /* FIXME: If odd address, this is CLEARLY a mips 16 instruction.  */
  /* Only a few tools will work this way.  */
  if (memaddr & 0x01)
    return print_insn_mips16 (memaddr, info);
#endif

#if SYMTAB_AVAILABLE
  if (info->mach == bfd_mach_mips16
      || (info->flavour == bfd_target_elf_flavour
	  && info->symbols != NULL
	  && ((*(elf_symbol_type **) info->symbols)->internal_elf_sym.st_other
	      == STO_MIPS16)))
    return print_insn_mips16 (memaddr, info);
#endif

  status = (*info->read_memory_func) (memaddr, buffer, INSNLEN, info);
  if (status == 0)
    {
      unsigned long insn;

      if (endianness == BFD_ENDIAN_BIG)
	insn = (unsigned long) bfd_getb32 (buffer);
      else
	insn = (unsigned long) bfd_getl32 (buffer);

      return print_insn_mips (memaddr, insn, info);
    }
  else
    {
      (*info->memory_error_func) (status, memaddr, info);
      return -1;
    }
}

int
print_insn_big_mips (bfd_vma memaddr, struct disassemble_info *info)
{
  return _print_insn_mips (memaddr, info, BFD_ENDIAN_BIG);
}

int
print_insn_little_mips (bfd_vma memaddr, struct disassemble_info *info)
{
  return _print_insn_mips (memaddr, info, BFD_ENDIAN_LITTLE);
}

void
print_mips_disassembler_options (FILE *stream)
{
  unsigned int i;

  fprintf (stream, _("\n\
The following MIPS specific disassembler options are supported for use\n\
with the -M switch (multiple options should be separated by commas):\n"));

  fprintf (stream, _("\n\
  octeon-useun             Disassemble Octeon unaligned load/store instructions.\n"));

  fprintf (stream, _("\n\
  no-octeon-useun          Disassemble mips unaligned load/store instructions.\n"));

  fprintf (stream, _("\n\
  gpr-names=ABI            Print GPR names according to  specified ABI.\n\
                           Default: based on binary being disassembled.\n"));

  fprintf (stream, _("\n\
  fpr-names=ABI            Print FPR names according to specified ABI.\n\
                           Default: numeric.\n"));

  fprintf (stream, _("\n\
  cp0-names=ARCH           Print CP0 register names according to\n\
                           specified architecture.\n\
                           Default: based on binary being disassembled.\n"));

  fprintf (stream, _("\n\
  hwr-names=ARCH           Print HWR names according to specified \n\
			   architecture.\n\
                           Default: based on binary being disassembled.\n"));

  fprintf (stream, _("\n\
  reg-names=ABI            Print GPR and FPR names according to\n\
                           specified ABI.\n"));

  fprintf (stream, _("\n\
  reg-names=ARCH           Print CP0 register and HWR names according to\n\
                           specified architecture.\n"));

  fprintf (stream, _("\n\
  For the options above, the following values are supported for \"ABI\":\n\
   "));
  for (i = 0; i < ARRAY_SIZE (mips_abi_choices); i++)
    fprintf (stream, " %s", mips_abi_choices[i].name);
  fprintf (stream, _("\n"));

  fprintf (stream, _("\n\
  For the options above, The following values are supported for \"ARCH\":\n\
   "));
  for (i = 0; i < ARRAY_SIZE (mips_arch_choices); i++)
    if (*mips_arch_choices[i].name != '\0')
      fprintf (stream, " %s", mips_arch_choices[i].name);
  fprintf (stream, _("\n"));

  fprintf (stream, _("\n"));
}
