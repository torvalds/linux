/* CPU data for mep.

THIS FILE IS MACHINE GENERATED WITH CGEN.

Copyright 1996-2005 Free Software Foundation, Inc.

This file is part of the GNU Binutils and/or GDB, the GNU debugger.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include "sysdep.h"
#include <stdio.h>
#include <stdarg.h>
#include "ansidecl.h"
#include "bfd.h"
#include "symcat.h"
#include "mep-desc.h"
#include "mep-opc.h"
#include "opintl.h"
#include "libiberty.h"
#include "xregex.h"

/* Attributes.  */

static const CGEN_ATTR_ENTRY bool_attr[] =
{
  { "#f", 0 },
  { "#t", 1 },
  { 0, 0 }
};

static const CGEN_ATTR_ENTRY MACH_attr[] ATTRIBUTE_UNUSED =
{
  { "base", MACH_BASE },
  { "mep", MACH_MEP },
  { "h1", MACH_H1 },
  { "max", MACH_MAX },
  { 0, 0 }
};

static const CGEN_ATTR_ENTRY ISA_attr[] ATTRIBUTE_UNUSED =
{
  { "mep", ISA_MEP },
  { "ext_core1", ISA_EXT_CORE1 },
  { "ext_core2", ISA_EXT_CORE2 },
  { "ext_cop2_16", ISA_EXT_COP2_16 },
  { "ext_cop2_32", ISA_EXT_COP2_32 },
  { "ext_cop2_48", ISA_EXT_COP2_48 },
  { "ext_cop2_64", ISA_EXT_COP2_64 },
  { "max", ISA_MAX },
  { 0, 0 }
};

static const CGEN_ATTR_ENTRY CDATA_attr[] ATTRIBUTE_UNUSED =
{
  { "LABEL", CDATA_LABEL },
  { "REGNUM", CDATA_REGNUM },
  { "FMAX_FLOAT", CDATA_FMAX_FLOAT },
  { "FMAX_INT", CDATA_FMAX_INT },
  { "POINTER", CDATA_POINTER },
  { "LONG", CDATA_LONG },
  { "ULONG", CDATA_ULONG },
  { "SHORT", CDATA_SHORT },
  { "USHORT", CDATA_USHORT },
  { "CHAR", CDATA_CHAR },
  { "UCHAR", CDATA_UCHAR },
  { "CP_DATA_BUS_INT", CDATA_CP_DATA_BUS_INT },
  { 0, 0 }
};

static const CGEN_ATTR_ENTRY ALIGN_attr [] ATTRIBUTE_UNUSED = 
{
  {"integer", 1},
  { 0, 0 }
};

static const CGEN_ATTR_ENTRY LATENCY_attr [] ATTRIBUTE_UNUSED = 
{
  {"integer", 0},
  { 0, 0 }
};

static const CGEN_ATTR_ENTRY CONFIG_attr[] ATTRIBUTE_UNUSED =
{
  { "NONE", CONFIG_NONE },
  { "simple", CONFIG_SIMPLE },
  { "fmax", CONFIG_FMAX },
  { 0, 0 }
};

const CGEN_ATTR_TABLE mep_cgen_ifield_attr_table[] =
{
  { "MACH", & MACH_attr[0], & MACH_attr[0] },
  { "ISA", & ISA_attr[0], & ISA_attr[0] },
  { "VIRTUAL", &bool_attr[0], &bool_attr[0] },
  { "PCREL-ADDR", &bool_attr[0], &bool_attr[0] },
  { "ABS-ADDR", &bool_attr[0], &bool_attr[0] },
  { "RESERVED", &bool_attr[0], &bool_attr[0] },
  { "SIGN-OPT", &bool_attr[0], &bool_attr[0] },
  { "SIGNED", &bool_attr[0], &bool_attr[0] },
  { 0, 0, 0 }
};

const CGEN_ATTR_TABLE mep_cgen_hardware_attr_table[] =
{
  { "MACH", & MACH_attr[0], & MACH_attr[0] },
  { "ISA", & ISA_attr[0], & ISA_attr[0] },
  { "VIRTUAL", &bool_attr[0], &bool_attr[0] },
  { "CACHE-ADDR", &bool_attr[0], &bool_attr[0] },
  { "PC", &bool_attr[0], &bool_attr[0] },
  { "PROFILE", &bool_attr[0], &bool_attr[0] },
  { "IS_FLOAT", &bool_attr[0], &bool_attr[0] },
  { 0, 0, 0 }
};

const CGEN_ATTR_TABLE mep_cgen_operand_attr_table[] =
{
  { "MACH", & MACH_attr[0], & MACH_attr[0] },
  { "ISA", & ISA_attr[0], & ISA_attr[0] },
  { "CDATA", & CDATA_attr[0], & CDATA_attr[0] },
  { "ALIGN", & ALIGN_attr[0], & ALIGN_attr[0] },
  { "VIRTUAL", &bool_attr[0], &bool_attr[0] },
  { "PCREL-ADDR", &bool_attr[0], &bool_attr[0] },
  { "ABS-ADDR", &bool_attr[0], &bool_attr[0] },
  { "SIGN-OPT", &bool_attr[0], &bool_attr[0] },
  { "SIGNED", &bool_attr[0], &bool_attr[0] },
  { "NEGATIVE", &bool_attr[0], &bool_attr[0] },
  { "RELAX", &bool_attr[0], &bool_attr[0] },
  { "SEM-ONLY", &bool_attr[0], &bool_attr[0] },
  { "RELOC_IMPLIES_OVERFLOW", &bool_attr[0], &bool_attr[0] },
  { 0, 0, 0 }
};

const CGEN_ATTR_TABLE mep_cgen_insn_attr_table[] =
{
  { "MACH", & MACH_attr[0], & MACH_attr[0] },
  { "ISA", & ISA_attr[0], & ISA_attr[0] },
  { "LATENCY", & LATENCY_attr[0], & LATENCY_attr[0] },
  { "CONFIG", & CONFIG_attr[0], & CONFIG_attr[0] },
  { "ALIAS", &bool_attr[0], &bool_attr[0] },
  { "VIRTUAL", &bool_attr[0], &bool_attr[0] },
  { "UNCOND-CTI", &bool_attr[0], &bool_attr[0] },
  { "COND-CTI", &bool_attr[0], &bool_attr[0] },
  { "SKIP-CTI", &bool_attr[0], &bool_attr[0] },
  { "DELAY-SLOT", &bool_attr[0], &bool_attr[0] },
  { "RELAXABLE", &bool_attr[0], &bool_attr[0] },
  { "RELAXED", &bool_attr[0], &bool_attr[0] },
  { "NO-DIS", &bool_attr[0], &bool_attr[0] },
  { "PBB", &bool_attr[0], &bool_attr[0] },
  { "OPTIONAL_BIT_INSN", &bool_attr[0], &bool_attr[0] },
  { "OPTIONAL_MUL_INSN", &bool_attr[0], &bool_attr[0] },
  { "OPTIONAL_DIV_INSN", &bool_attr[0], &bool_attr[0] },
  { "OPTIONAL_DEBUG_INSN", &bool_attr[0], &bool_attr[0] },
  { "OPTIONAL_LDZ_INSN", &bool_attr[0], &bool_attr[0] },
  { "OPTIONAL_ABS_INSN", &bool_attr[0], &bool_attr[0] },
  { "OPTIONAL_AVE_INSN", &bool_attr[0], &bool_attr[0] },
  { "OPTIONAL_MINMAX_INSN", &bool_attr[0], &bool_attr[0] },
  { "OPTIONAL_CLIP_INSN", &bool_attr[0], &bool_attr[0] },
  { "OPTIONAL_SAT_INSN", &bool_attr[0], &bool_attr[0] },
  { "OPTIONAL_UCI_INSN", &bool_attr[0], &bool_attr[0] },
  { "OPTIONAL_DSP_INSN", &bool_attr[0], &bool_attr[0] },
  { "OPTIONAL_CP_INSN", &bool_attr[0], &bool_attr[0] },
  { "OPTIONAL_CP64_INSN", &bool_attr[0], &bool_attr[0] },
  { "OPTIONAL_VLIW64", &bool_attr[0], &bool_attr[0] },
  { "MAY_TRAP", &bool_attr[0], &bool_attr[0] },
  { "VLIW_ALONE", &bool_attr[0], &bool_attr[0] },
  { "VLIW_NO_CORE_NOP", &bool_attr[0], &bool_attr[0] },
  { "VLIW_NO_COP_NOP", &bool_attr[0], &bool_attr[0] },
  { "VLIW64_NO_MATCHING_NOP", &bool_attr[0], &bool_attr[0] },
  { "VLIW32_NO_MATCHING_NOP", &bool_attr[0], &bool_attr[0] },
  { "VOLATILE", &bool_attr[0], &bool_attr[0] },
  { 0, 0, 0 }
};

/* Instruction set variants.  */

static const CGEN_ISA mep_cgen_isa_table[] = {
  { "mep", 32, 32, 16, 32 },
  { "ext_core1", 32, 32, 16, 32 },
  { "ext_core2", 32, 32, 16, 32 },
  { "ext_cop2_16", 32, 32, 65535, 0 },
  { "ext_cop2_32", 32, 32, 65535, 0 },
  { "ext_cop2_48", 32, 32, 65535, 0 },
  { "ext_cop2_64", 32, 32, 65535, 0 },
  { 0, 0, 0, 0, 0 }
};

/* Machine variants.  */

static const CGEN_MACH mep_cgen_mach_table[] = {
  { "mep", "mep", MACH_MEP, 16 },
  { "h1", "h1", MACH_H1, 16 },
  { 0, 0, 0, 0 }
};

static CGEN_KEYWORD_ENTRY mep_cgen_opval_h_gpr_entries[] =
{
  { "$0", 0, {0, {{{0, 0}}}}, 0, 0 },
  { "$1", 1, {0, {{{0, 0}}}}, 0, 0 },
  { "$2", 2, {0, {{{0, 0}}}}, 0, 0 },
  { "$3", 3, {0, {{{0, 0}}}}, 0, 0 },
  { "$4", 4, {0, {{{0, 0}}}}, 0, 0 },
  { "$5", 5, {0, {{{0, 0}}}}, 0, 0 },
  { "$6", 6, {0, {{{0, 0}}}}, 0, 0 },
  { "$7", 7, {0, {{{0, 0}}}}, 0, 0 },
  { "$8", 8, {0, {{{0, 0}}}}, 0, 0 },
  { "$9", 9, {0, {{{0, 0}}}}, 0, 0 },
  { "$10", 10, {0, {{{0, 0}}}}, 0, 0 },
  { "$11", 11, {0, {{{0, 0}}}}, 0, 0 },
  { "$fp", 8, {0, {{{0, 0}}}}, 0, 0 },
  { "$tp", 13, {0, {{{0, 0}}}}, 0, 0 },
  { "$gp", 14, {0, {{{0, 0}}}}, 0, 0 },
  { "$sp", 15, {0, {{{0, 0}}}}, 0, 0 },
  { "$12", 12, {0, {{{0, 0}}}}, 0, 0 },
  { "$13", 13, {0, {{{0, 0}}}}, 0, 0 },
  { "$14", 14, {0, {{{0, 0}}}}, 0, 0 },
  { "$15", 15, {0, {{{0, 0}}}}, 0, 0 }
};

CGEN_KEYWORD mep_cgen_opval_h_gpr =
{
  & mep_cgen_opval_h_gpr_entries[0],
  20,
  0, 0, 0, 0, ""
};

static CGEN_KEYWORD_ENTRY mep_cgen_opval_h_csr_entries[] =
{
  { "$pc", 0, {0, {{{0, 0}}}}, 0, 0 },
  { "$lp", 1, {0, {{{0, 0}}}}, 0, 0 },
  { "$sar", 2, {0, {{{0, 0}}}}, 0, 0 },
  { "$rpb", 4, {0, {{{0, 0}}}}, 0, 0 },
  { "$rpe", 5, {0, {{{0, 0}}}}, 0, 0 },
  { "$rpc", 6, {0, {{{0, 0}}}}, 0, 0 },
  { "$hi", 7, {0, {{{0, 0}}}}, 0, 0 },
  { "$lo", 8, {0, {{{0, 0}}}}, 0, 0 },
  { "$mb0", 12, {0, {{{0, 0}}}}, 0, 0 },
  { "$me0", 13, {0, {{{0, 0}}}}, 0, 0 },
  { "$mb1", 14, {0, {{{0, 0}}}}, 0, 0 },
  { "$me1", 15, {0, {{{0, 0}}}}, 0, 0 },
  { "$psw", 16, {0, {{{0, 0}}}}, 0, 0 },
  { "$id", 17, {0, {{{0, 0}}}}, 0, 0 },
  { "$tmp", 18, {0, {{{0, 0}}}}, 0, 0 },
  { "$epc", 19, {0, {{{0, 0}}}}, 0, 0 },
  { "$exc", 20, {0, {{{0, 0}}}}, 0, 0 },
  { "$cfg", 21, {0, {{{0, 0}}}}, 0, 0 },
  { "$npc", 23, {0, {{{0, 0}}}}, 0, 0 },
  { "$dbg", 24, {0, {{{0, 0}}}}, 0, 0 },
  { "$depc", 25, {0, {{{0, 0}}}}, 0, 0 },
  { "$opt", 26, {0, {{{0, 0}}}}, 0, 0 },
  { "$rcfg", 27, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccfg", 28, {0, {{{0, 0}}}}, 0, 0 }
};

CGEN_KEYWORD mep_cgen_opval_h_csr =
{
  & mep_cgen_opval_h_csr_entries[0],
  24,
  0, 0, 0, 0, ""
};

static CGEN_KEYWORD_ENTRY mep_cgen_opval_h_cr64_entries[] =
{
  { "$c0", 0, {0, {{{0, 0}}}}, 0, 0 },
  { "$c1", 1, {0, {{{0, 0}}}}, 0, 0 },
  { "$c2", 2, {0, {{{0, 0}}}}, 0, 0 },
  { "$c3", 3, {0, {{{0, 0}}}}, 0, 0 },
  { "$c4", 4, {0, {{{0, 0}}}}, 0, 0 },
  { "$c5", 5, {0, {{{0, 0}}}}, 0, 0 },
  { "$c6", 6, {0, {{{0, 0}}}}, 0, 0 },
  { "$c7", 7, {0, {{{0, 0}}}}, 0, 0 },
  { "$c8", 8, {0, {{{0, 0}}}}, 0, 0 },
  { "$c9", 9, {0, {{{0, 0}}}}, 0, 0 },
  { "$c10", 10, {0, {{{0, 0}}}}, 0, 0 },
  { "$c11", 11, {0, {{{0, 0}}}}, 0, 0 },
  { "$c12", 12, {0, {{{0, 0}}}}, 0, 0 },
  { "$c13", 13, {0, {{{0, 0}}}}, 0, 0 },
  { "$c14", 14, {0, {{{0, 0}}}}, 0, 0 },
  { "$c15", 15, {0, {{{0, 0}}}}, 0, 0 },
  { "$c16", 16, {0, {{{0, 0}}}}, 0, 0 },
  { "$c17", 17, {0, {{{0, 0}}}}, 0, 0 },
  { "$c18", 18, {0, {{{0, 0}}}}, 0, 0 },
  { "$c19", 19, {0, {{{0, 0}}}}, 0, 0 },
  { "$c20", 20, {0, {{{0, 0}}}}, 0, 0 },
  { "$c21", 21, {0, {{{0, 0}}}}, 0, 0 },
  { "$c22", 22, {0, {{{0, 0}}}}, 0, 0 },
  { "$c23", 23, {0, {{{0, 0}}}}, 0, 0 },
  { "$c24", 24, {0, {{{0, 0}}}}, 0, 0 },
  { "$c25", 25, {0, {{{0, 0}}}}, 0, 0 },
  { "$c26", 26, {0, {{{0, 0}}}}, 0, 0 },
  { "$c27", 27, {0, {{{0, 0}}}}, 0, 0 },
  { "$c28", 28, {0, {{{0, 0}}}}, 0, 0 },
  { "$c29", 29, {0, {{{0, 0}}}}, 0, 0 },
  { "$c30", 30, {0, {{{0, 0}}}}, 0, 0 },
  { "$c31", 31, {0, {{{0, 0}}}}, 0, 0 }
};

CGEN_KEYWORD mep_cgen_opval_h_cr64 =
{
  & mep_cgen_opval_h_cr64_entries[0],
  32,
  0, 0, 0, 0, ""
};

static CGEN_KEYWORD_ENTRY mep_cgen_opval_h_cr_entries[] =
{
  { "$c0", 0, {0, {{{0, 0}}}}, 0, 0 },
  { "$c1", 1, {0, {{{0, 0}}}}, 0, 0 },
  { "$c2", 2, {0, {{{0, 0}}}}, 0, 0 },
  { "$c3", 3, {0, {{{0, 0}}}}, 0, 0 },
  { "$c4", 4, {0, {{{0, 0}}}}, 0, 0 },
  { "$c5", 5, {0, {{{0, 0}}}}, 0, 0 },
  { "$c6", 6, {0, {{{0, 0}}}}, 0, 0 },
  { "$c7", 7, {0, {{{0, 0}}}}, 0, 0 },
  { "$c8", 8, {0, {{{0, 0}}}}, 0, 0 },
  { "$c9", 9, {0, {{{0, 0}}}}, 0, 0 },
  { "$c10", 10, {0, {{{0, 0}}}}, 0, 0 },
  { "$c11", 11, {0, {{{0, 0}}}}, 0, 0 },
  { "$c12", 12, {0, {{{0, 0}}}}, 0, 0 },
  { "$c13", 13, {0, {{{0, 0}}}}, 0, 0 },
  { "$c14", 14, {0, {{{0, 0}}}}, 0, 0 },
  { "$c15", 15, {0, {{{0, 0}}}}, 0, 0 },
  { "$c16", 16, {0, {{{0, 0}}}}, 0, 0 },
  { "$c17", 17, {0, {{{0, 0}}}}, 0, 0 },
  { "$c18", 18, {0, {{{0, 0}}}}, 0, 0 },
  { "$c19", 19, {0, {{{0, 0}}}}, 0, 0 },
  { "$c20", 20, {0, {{{0, 0}}}}, 0, 0 },
  { "$c21", 21, {0, {{{0, 0}}}}, 0, 0 },
  { "$c22", 22, {0, {{{0, 0}}}}, 0, 0 },
  { "$c23", 23, {0, {{{0, 0}}}}, 0, 0 },
  { "$c24", 24, {0, {{{0, 0}}}}, 0, 0 },
  { "$c25", 25, {0, {{{0, 0}}}}, 0, 0 },
  { "$c26", 26, {0, {{{0, 0}}}}, 0, 0 },
  { "$c27", 27, {0, {{{0, 0}}}}, 0, 0 },
  { "$c28", 28, {0, {{{0, 0}}}}, 0, 0 },
  { "$c29", 29, {0, {{{0, 0}}}}, 0, 0 },
  { "$c30", 30, {0, {{{0, 0}}}}, 0, 0 },
  { "$c31", 31, {0, {{{0, 0}}}}, 0, 0 }
};

CGEN_KEYWORD mep_cgen_opval_h_cr =
{
  & mep_cgen_opval_h_cr_entries[0],
  32,
  0, 0, 0, 0, ""
};

static CGEN_KEYWORD_ENTRY mep_cgen_opval_h_ccr_entries[] =
{
  { "$ccr0", 0, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr1", 1, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr2", 2, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr3", 3, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr4", 4, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr5", 5, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr6", 6, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr7", 7, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr8", 8, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr9", 9, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr10", 10, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr11", 11, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr12", 12, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr13", 13, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr14", 14, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr15", 15, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr16", 16, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr17", 17, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr18", 18, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr19", 19, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr20", 20, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr21", 21, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr22", 22, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr23", 23, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr24", 24, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr25", 25, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr26", 26, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr27", 27, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr28", 28, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr29", 29, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr30", 30, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr31", 31, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr32", 32, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr33", 33, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr34", 34, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr35", 35, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr36", 36, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr37", 37, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr38", 38, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr39", 39, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr40", 40, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr41", 41, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr42", 42, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr43", 43, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr44", 44, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr45", 45, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr46", 46, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr47", 47, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr48", 48, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr49", 49, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr50", 50, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr51", 51, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr52", 52, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr53", 53, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr54", 54, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr55", 55, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr56", 56, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr57", 57, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr58", 58, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr59", 59, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr60", 60, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr61", 61, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr62", 62, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr63", 63, {0, {{{0, 0}}}}, 0, 0 }
};

CGEN_KEYWORD mep_cgen_opval_h_ccr =
{
  & mep_cgen_opval_h_ccr_entries[0],
  64,
  0, 0, 0, 0, ""
};

static CGEN_KEYWORD_ENTRY mep_cgen_opval_h_cr_fmax_entries[] =
{
  { "$fr0", 0, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr1", 1, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr2", 2, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr3", 3, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr4", 4, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr5", 5, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr6", 6, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr7", 7, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr8", 8, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr9", 9, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr10", 10, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr11", 11, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr12", 12, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr13", 13, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr14", 14, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr15", 15, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr16", 16, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr17", 17, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr18", 18, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr19", 19, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr20", 20, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr21", 21, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr22", 22, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr23", 23, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr24", 24, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr25", 25, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr26", 26, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr27", 27, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr28", 28, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr29", 29, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr30", 30, {0, {{{0, 0}}}}, 0, 0 },
  { "$fr31", 31, {0, {{{0, 0}}}}, 0, 0 },
  { "$c0", 0, {0, {{{0, 0}}}}, 0, 0 },
  { "$c1", 1, {0, {{{0, 0}}}}, 0, 0 },
  { "$c2", 2, {0, {{{0, 0}}}}, 0, 0 },
  { "$c3", 3, {0, {{{0, 0}}}}, 0, 0 },
  { "$c4", 4, {0, {{{0, 0}}}}, 0, 0 },
  { "$c5", 5, {0, {{{0, 0}}}}, 0, 0 },
  { "$c6", 6, {0, {{{0, 0}}}}, 0, 0 },
  { "$c7", 7, {0, {{{0, 0}}}}, 0, 0 },
  { "$c8", 8, {0, {{{0, 0}}}}, 0, 0 },
  { "$c9", 9, {0, {{{0, 0}}}}, 0, 0 },
  { "$c10", 10, {0, {{{0, 0}}}}, 0, 0 },
  { "$c11", 11, {0, {{{0, 0}}}}, 0, 0 },
  { "$c12", 12, {0, {{{0, 0}}}}, 0, 0 },
  { "$c13", 13, {0, {{{0, 0}}}}, 0, 0 },
  { "$c14", 14, {0, {{{0, 0}}}}, 0, 0 },
  { "$c15", 15, {0, {{{0, 0}}}}, 0, 0 },
  { "$c16", 16, {0, {{{0, 0}}}}, 0, 0 },
  { "$c17", 17, {0, {{{0, 0}}}}, 0, 0 },
  { "$c18", 18, {0, {{{0, 0}}}}, 0, 0 },
  { "$c19", 19, {0, {{{0, 0}}}}, 0, 0 },
  { "$c20", 20, {0, {{{0, 0}}}}, 0, 0 },
  { "$c21", 21, {0, {{{0, 0}}}}, 0, 0 },
  { "$c22", 22, {0, {{{0, 0}}}}, 0, 0 },
  { "$c23", 23, {0, {{{0, 0}}}}, 0, 0 },
  { "$c24", 24, {0, {{{0, 0}}}}, 0, 0 },
  { "$c25", 25, {0, {{{0, 0}}}}, 0, 0 },
  { "$c26", 26, {0, {{{0, 0}}}}, 0, 0 },
  { "$c27", 27, {0, {{{0, 0}}}}, 0, 0 },
  { "$c28", 28, {0, {{{0, 0}}}}, 0, 0 },
  { "$c29", 29, {0, {{{0, 0}}}}, 0, 0 },
  { "$c30", 30, {0, {{{0, 0}}}}, 0, 0 },
  { "$c31", 31, {0, {{{0, 0}}}}, 0, 0 }
};

CGEN_KEYWORD mep_cgen_opval_h_cr_fmax =
{
  & mep_cgen_opval_h_cr_fmax_entries[0],
  64,
  0, 0, 0, 0, ""
};

static CGEN_KEYWORD_ENTRY mep_cgen_opval_h_ccr_fmax_entries[] =
{
  { "$cirr", 0, {0, {{{0, 0}}}}, 0, 0 },
  { "$fcr0", 0, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr0", 0, {0, {{{0, 0}}}}, 0, 0 },
  { "$cbcr", 1, {0, {{{0, 0}}}}, 0, 0 },
  { "$fcr1", 1, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr1", 1, {0, {{{0, 0}}}}, 0, 0 },
  { "$cerr", 15, {0, {{{0, 0}}}}, 0, 0 },
  { "$fcr15", 15, {0, {{{0, 0}}}}, 0, 0 },
  { "$ccr15", 15, {0, {{{0, 0}}}}, 0, 0 }
};

CGEN_KEYWORD mep_cgen_opval_h_ccr_fmax =
{
  & mep_cgen_opval_h_ccr_fmax_entries[0],
  9,
  0, 0, 0, 0, ""
};


/* The hardware table.  */

#if defined (__STDC__) || defined (ALMOST_STDC) || defined (HAVE_STRINGIZE)
#define A(a) (1 << CGEN_HW_##a)
#else
#define A(a) (1 << CGEN_HW_/**/a)
#endif

const CGEN_HW_ENTRY mep_cgen_hw_table[] =
{
  { "h-memory", HW_H_MEMORY, CGEN_ASM_NONE, 0, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } } },
  { "h-sint", HW_H_SINT, CGEN_ASM_NONE, 0, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } } },
  { "h-uint", HW_H_UINT, CGEN_ASM_NONE, 0, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } } },
  { "h-addr", HW_H_ADDR, CGEN_ASM_NONE, 0, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } } },
  { "h-iaddr", HW_H_IADDR, CGEN_ASM_NONE, 0, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } } },
  { "h-pc", HW_H_PC, CGEN_ASM_NONE, 0, { 0|A(PROFILE)|A(PC), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } } },
  { "h-gpr", HW_H_GPR, CGEN_ASM_KEYWORD, (PTR) & mep_cgen_opval_h_gpr, { 0|A(PROFILE)|A(CACHE_ADDR), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } } },
  { "h-csr", HW_H_CSR, CGEN_ASM_KEYWORD, (PTR) & mep_cgen_opval_h_csr, { 0|A(PROFILE), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } } },
  { "h-cr64", HW_H_CR64, CGEN_ASM_KEYWORD, (PTR) & mep_cgen_opval_h_cr64, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } } },
  { "h-cr", HW_H_CR, CGEN_ASM_KEYWORD, (PTR) & mep_cgen_opval_h_cr, { 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } } },
  { "h-ccr", HW_H_CCR, CGEN_ASM_KEYWORD, (PTR) & mep_cgen_opval_h_ccr, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } } },
  { "h-cr-fmax", HW_H_CR_FMAX, CGEN_ASM_KEYWORD, (PTR) & mep_cgen_opval_h_cr_fmax, { 0|A(IS_FLOAT)|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } } } } },
  { "h-ccr-fmax", HW_H_CCR_FMAX, CGEN_ASM_KEYWORD, (PTR) & mep_cgen_opval_h_ccr_fmax, { 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } } } } },
  { "h-fmax-compare-i-p", HW_H_FMAX_COMPARE_I_P, CGEN_ASM_NONE, 0, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } } } } },
  { 0, 0, CGEN_ASM_NONE, 0, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x80" } } } } }
};

#undef A


/* The instruction field table.  */

#if defined (__STDC__) || defined (ALMOST_STDC) || defined (HAVE_STRINGIZE)
#define A(a) (1 << CGEN_IFLD_##a)
#else
#define A(a) (1 << CGEN_IFLD_/**/a)
#endif

const CGEN_IFLD mep_cgen_ifld_table[] =
{
  { MEP_F_NIL, "f-nil", 0, 0, 0, 0, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x80" } } } }  },
  { MEP_F_ANYOF, "f-anyof", 0, 0, 0, 0, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x80" } } } }  },
  { MEP_F_MAJOR, "f-major", 0, 32, 0, 4, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_RN, "f-rn", 0, 32, 4, 4, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_RN3, "f-rn3", 0, 32, 5, 3, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_RM, "f-rm", 0, 32, 8, 4, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_RL, "f-rl", 0, 32, 12, 4, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_SUB2, "f-sub2", 0, 32, 14, 2, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_SUB3, "f-sub3", 0, 32, 13, 3, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_SUB4, "f-sub4", 0, 32, 12, 4, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_EXT, "f-ext", 0, 32, 16, 8, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_CRN, "f-crn", 0, 32, 4, 4, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_CSRN_HI, "f-csrn-hi", 0, 32, 15, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_CSRN_LO, "f-csrn-lo", 0, 32, 8, 4, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_CSRN, "f-csrn", 0, 0, 0, 0,{ 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_CRNX_HI, "f-crnx-hi", 0, 32, 28, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_CRNX_LO, "f-crnx-lo", 0, 32, 4, 4, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_CRNX, "f-crnx", 0, 0, 0, 0,{ 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_0, "f-0", 0, 32, 0, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_1, "f-1", 0, 32, 1, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_2, "f-2", 0, 32, 2, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_3, "f-3", 0, 32, 3, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_4, "f-4", 0, 32, 4, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_5, "f-5", 0, 32, 5, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_6, "f-6", 0, 32, 6, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_7, "f-7", 0, 32, 7, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_8, "f-8", 0, 32, 8, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_9, "f-9", 0, 32, 9, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_10, "f-10", 0, 32, 10, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_11, "f-11", 0, 32, 11, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_12, "f-12", 0, 32, 12, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_13, "f-13", 0, 32, 13, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_14, "f-14", 0, 32, 14, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_15, "f-15", 0, 32, 15, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_16, "f-16", 0, 32, 16, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_17, "f-17", 0, 32, 17, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_18, "f-18", 0, 32, 18, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_19, "f-19", 0, 32, 19, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_20, "f-20", 0, 32, 20, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_21, "f-21", 0, 32, 21, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_22, "f-22", 0, 32, 22, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_23, "f-23", 0, 32, 23, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_24, "f-24", 0, 32, 24, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_25, "f-25", 0, 32, 25, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_26, "f-26", 0, 32, 26, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_27, "f-27", 0, 32, 27, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_28, "f-28", 0, 32, 28, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_29, "f-29", 0, 32, 29, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_30, "f-30", 0, 32, 30, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_31, "f-31", 0, 32, 31, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } } } }  },
  { MEP_F_8S8A2, "f-8s8a2", 0, 32, 8, 7, { 0|A(PCREL_ADDR), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_12S4A2, "f-12s4a2", 0, 32, 4, 11, { 0|A(PCREL_ADDR), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_17S16A2, "f-17s16a2", 0, 32, 16, 16, { 0|A(PCREL_ADDR), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_24S5A2N_HI, "f-24s5a2n-hi", 0, 32, 16, 16, { 0|A(PCREL_ADDR), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_24S5A2N_LO, "f-24s5a2n-lo", 0, 32, 5, 7, { 0|A(PCREL_ADDR), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_24S5A2N, "f-24s5a2n", 0, 0, 0, 0,{ 0|A(PCREL_ADDR)|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_24U5A2N_HI, "f-24u5a2n-hi", 0, 32, 16, 16, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_24U5A2N_LO, "f-24u5a2n-lo", 0, 32, 5, 7, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_24U5A2N, "f-24u5a2n", 0, 0, 0, 0,{ 0|A(ABS_ADDR)|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_2U6, "f-2u6", 0, 32, 6, 2, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_7U9, "f-7u9", 0, 32, 9, 7, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_7U9A2, "f-7u9a2", 0, 32, 9, 6, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_7U9A4, "f-7u9a4", 0, 32, 9, 5, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_16S16, "f-16s16", 0, 32, 16, 16, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_2U10, "f-2u10", 0, 32, 10, 2, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_3U5, "f-3u5", 0, 32, 5, 3, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_4U8, "f-4u8", 0, 32, 8, 4, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_5U8, "f-5u8", 0, 32, 8, 5, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_5U24, "f-5u24", 0, 32, 24, 5, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_6S8, "f-6s8", 0, 32, 8, 6, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_8S8, "f-8s8", 0, 32, 8, 8, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_16U16, "f-16u16", 0, 32, 16, 16, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_12U16, "f-12u16", 0, 32, 16, 12, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_3U29, "f-3u29", 0, 32, 29, 3, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_8S24, "f-8s24", 0, 32, 24, 8, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_8S24A2, "f-8s24a2", 0, 32, 24, 7, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_8S24A4, "f-8s24a4", 0, 32, 24, 6, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_8S24A8, "f-8s24a8", 0, 32, 24, 5, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_24U8A4N_HI, "f-24u8a4n-hi", 0, 32, 16, 16, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_24U8A4N_LO, "f-24u8a4n-lo", 0, 32, 8, 6, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_24U8A4N, "f-24u8a4n", 0, 0, 0, 0,{ 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_24U8N_HI, "f-24u8n-hi", 0, 32, 16, 16, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_24U8N_LO, "f-24u8n-lo", 0, 32, 8, 8, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_24U8N, "f-24u8n", 0, 0, 0, 0,{ 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_24U4N_HI, "f-24u4n-hi", 0, 32, 4, 8, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_24U4N_LO, "f-24u4n-lo", 0, 32, 16, 16, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_24U4N, "f-24u4n", 0, 0, 0, 0,{ 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_CALLNUM, "f-callnum", 0, 0, 0, 0,{ 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_CCRN_HI, "f-ccrn-hi", 0, 32, 28, 2, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_CCRN_LO, "f-ccrn-lo", 0, 32, 4, 4, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_CCRN, "f-ccrn", 0, 0, 0, 0,{ 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } } } }  },
  { MEP_F_FMAX_0_4, "f-fmax-0-4", 0, 32, 0, 4, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } } } }  },
  { MEP_F_FMAX_4_4, "f-fmax-4-4", 0, 32, 4, 4, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } } } }  },
  { MEP_F_FMAX_8_4, "f-fmax-8-4", 0, 32, 8, 4, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } } } }  },
  { MEP_F_FMAX_12_4, "f-fmax-12-4", 0, 32, 12, 4, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } } } }  },
  { MEP_F_FMAX_16_4, "f-fmax-16-4", 0, 32, 16, 4, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } } } }  },
  { MEP_F_FMAX_20_4, "f-fmax-20-4", 0, 32, 20, 4, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } } } }  },
  { MEP_F_FMAX_24_4, "f-fmax-24-4", 0, 32, 24, 4, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } } } }  },
  { MEP_F_FMAX_28_1, "f-fmax-28-1", 0, 32, 28, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } } } }  },
  { MEP_F_FMAX_29_1, "f-fmax-29-1", 0, 32, 29, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } } } }  },
  { MEP_F_FMAX_30_1, "f-fmax-30-1", 0, 32, 30, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } } } }  },
  { MEP_F_FMAX_31_1, "f-fmax-31-1", 0, 32, 31, 1, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } } } }  },
  { MEP_F_FMAX_FRD, "f-fmax-frd", 0, 0, 0, 0,{ 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } } } }  },
  { MEP_F_FMAX_FRN, "f-fmax-frn", 0, 0, 0, 0,{ 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } } } }  },
  { MEP_F_FMAX_FRM, "f-fmax-frm", 0, 0, 0, 0,{ 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } } } }  },
  { MEP_F_FMAX_RM, "f-fmax-rm", 0, 32, 8, 4, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } } } }  },
  { 0, 0, 0, 0, 0, 0, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x80" } } } } }
};

#undef A



/* multi ifield declarations */

const CGEN_MAYBE_MULTI_IFLD MEP_F_CSRN_MULTI_IFIELD [];
const CGEN_MAYBE_MULTI_IFLD MEP_F_CRNX_MULTI_IFIELD [];
const CGEN_MAYBE_MULTI_IFLD MEP_F_24S5A2N_MULTI_IFIELD [];
const CGEN_MAYBE_MULTI_IFLD MEP_F_24U5A2N_MULTI_IFIELD [];
const CGEN_MAYBE_MULTI_IFLD MEP_F_24U8A4N_MULTI_IFIELD [];
const CGEN_MAYBE_MULTI_IFLD MEP_F_24U8N_MULTI_IFIELD [];
const CGEN_MAYBE_MULTI_IFLD MEP_F_24U4N_MULTI_IFIELD [];
const CGEN_MAYBE_MULTI_IFLD MEP_F_CALLNUM_MULTI_IFIELD [];
const CGEN_MAYBE_MULTI_IFLD MEP_F_CCRN_MULTI_IFIELD [];
const CGEN_MAYBE_MULTI_IFLD MEP_F_FMAX_FRD_MULTI_IFIELD [];
const CGEN_MAYBE_MULTI_IFLD MEP_F_FMAX_FRN_MULTI_IFIELD [];
const CGEN_MAYBE_MULTI_IFLD MEP_F_FMAX_FRM_MULTI_IFIELD [];


/* multi ifield definitions */

const CGEN_MAYBE_MULTI_IFLD MEP_F_CSRN_MULTI_IFIELD [] =
{
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_CSRN_HI] } },
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_CSRN_LO] } },
    { 0, { (const PTR) 0 } }
};
const CGEN_MAYBE_MULTI_IFLD MEP_F_CRNX_MULTI_IFIELD [] =
{
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_CRNX_HI] } },
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_CRNX_LO] } },
    { 0, { (const PTR) 0 } }
};
const CGEN_MAYBE_MULTI_IFLD MEP_F_24S5A2N_MULTI_IFIELD [] =
{
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_24S5A2N_HI] } },
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_24S5A2N_LO] } },
    { 0, { (const PTR) 0 } }
};
const CGEN_MAYBE_MULTI_IFLD MEP_F_24U5A2N_MULTI_IFIELD [] =
{
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_24U5A2N_HI] } },
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_24U5A2N_LO] } },
    { 0, { (const PTR) 0 } }
};
const CGEN_MAYBE_MULTI_IFLD MEP_F_24U8A4N_MULTI_IFIELD [] =
{
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_24U8A4N_HI] } },
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_24U8A4N_LO] } },
    { 0, { (const PTR) 0 } }
};
const CGEN_MAYBE_MULTI_IFLD MEP_F_24U8N_MULTI_IFIELD [] =
{
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_24U8N_HI] } },
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_24U8N_LO] } },
    { 0, { (const PTR) 0 } }
};
const CGEN_MAYBE_MULTI_IFLD MEP_F_24U4N_MULTI_IFIELD [] =
{
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_24U4N_HI] } },
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_24U4N_LO] } },
    { 0, { (const PTR) 0 } }
};
const CGEN_MAYBE_MULTI_IFLD MEP_F_CALLNUM_MULTI_IFIELD [] =
{
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_5] } },
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_6] } },
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_7] } },
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_11] } },
    { 0, { (const PTR) 0 } }
};
const CGEN_MAYBE_MULTI_IFLD MEP_F_CCRN_MULTI_IFIELD [] =
{
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_CCRN_HI] } },
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_CCRN_LO] } },
    { 0, { (const PTR) 0 } }
};
const CGEN_MAYBE_MULTI_IFLD MEP_F_FMAX_FRD_MULTI_IFIELD [] =
{
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_FMAX_28_1] } },
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_FMAX_4_4] } },
    { 0, { (const PTR) 0 } }
};
const CGEN_MAYBE_MULTI_IFLD MEP_F_FMAX_FRN_MULTI_IFIELD [] =
{
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_FMAX_29_1] } },
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_FMAX_20_4] } },
    { 0, { (const PTR) 0 } }
};
const CGEN_MAYBE_MULTI_IFLD MEP_F_FMAX_FRM_MULTI_IFIELD [] =
{
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_FMAX_30_1] } },
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_FMAX_24_4] } },
    { 0, { (const PTR) 0 } }
};

/* The operand table.  */

#if defined (__STDC__) || defined (ALMOST_STDC) || defined (HAVE_STRINGIZE)
#define A(a) (1 << CGEN_OPERAND_##a)
#else
#define A(a) (1 << CGEN_OPERAND_/**/a)
#endif
#if defined (__STDC__) || defined (ALMOST_STDC) || defined (HAVE_STRINGIZE)
#define OPERAND(op) MEP_OPERAND_##op
#else
#define OPERAND(op) MEP_OPERAND_/**/op
#endif

const CGEN_OPERAND mep_cgen_operand_table[] =
{
/* pc: program counter */
  { "pc", MEP_OPERAND_PC, HW_H_PC, 0, 0,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_NIL] } }, 
    { 0|A(SEM_ONLY), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x80" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* r0: register 0 */
  { "r0", MEP_OPERAND_R0, HW_H_GPR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* rn: register Rn */
  { "rn", MEP_OPERAND_RN, HW_H_GPR, 4, 4,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_RN] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* rm: register Rm */
  { "rm", MEP_OPERAND_RM, HW_H_GPR, 8, 4,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_RM] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* rl: register Rl */
  { "rl", MEP_OPERAND_RL, HW_H_GPR, 12, 4,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_RL] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* rn3: register 0-7 */
  { "rn3", MEP_OPERAND_RN3, HW_H_GPR, 5, 3,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_RN3] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* rma: register Rm holding pointer */
  { "rma", MEP_OPERAND_RMA, HW_H_GPR, 8, 4,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_RM] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_POINTER, 0 } }, { { 1, 0 } } } }  },
/* rnc: register Rn holding char */
  { "rnc", MEP_OPERAND_RNC, HW_H_GPR, 4, 4,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_RN] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_CHAR, 0 } }, { { 1, 0 } } } }  },
/* rnuc: register Rn holding unsigned char */
  { "rnuc", MEP_OPERAND_RNUC, HW_H_GPR, 4, 4,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_RN] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_UCHAR, 0 } }, { { 1, 0 } } } }  },
/* rns: register Rn holding short */
  { "rns", MEP_OPERAND_RNS, HW_H_GPR, 4, 4,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_RN] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_SHORT, 0 } }, { { 1, 0 } } } }  },
/* rnus: register Rn holding unsigned short */
  { "rnus", MEP_OPERAND_RNUS, HW_H_GPR, 4, 4,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_RN] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_USHORT, 0 } }, { { 1, 0 } } } }  },
/* rnl: register Rn holding long */
  { "rnl", MEP_OPERAND_RNL, HW_H_GPR, 4, 4,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_RN] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* rnul: register Rn holding unsigned  long */
  { "rnul", MEP_OPERAND_RNUL, HW_H_GPR, 4, 4,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_RN] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_ULONG, 0 } }, { { 1, 0 } } } }  },
/* rn3c: register 0-7 holding unsigned char */
  { "rn3c", MEP_OPERAND_RN3C, HW_H_GPR, 5, 3,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_RN3] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_CHAR, 0 } }, { { 1, 0 } } } }  },
/* rn3uc: register 0-7 holding byte */
  { "rn3uc", MEP_OPERAND_RN3UC, HW_H_GPR, 5, 3,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_RN3] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_UCHAR, 0 } }, { { 1, 0 } } } }  },
/* rn3s: register 0-7 holding unsigned short */
  { "rn3s", MEP_OPERAND_RN3S, HW_H_GPR, 5, 3,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_RN3] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_SHORT, 0 } }, { { 1, 0 } } } }  },
/* rn3us: register 0-7 holding short */
  { "rn3us", MEP_OPERAND_RN3US, HW_H_GPR, 5, 3,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_RN3] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_USHORT, 0 } }, { { 1, 0 } } } }  },
/* rn3l: register 0-7 holding unsigned long */
  { "rn3l", MEP_OPERAND_RN3L, HW_H_GPR, 5, 3,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_RN3] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* rn3ul: register 0-7 holding long */
  { "rn3ul", MEP_OPERAND_RN3UL, HW_H_GPR, 5, 3,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_RN3] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_ULONG, 0 } }, { { 1, 0 } } } }  },
/* lp: link pointer */
  { "lp", MEP_OPERAND_LP, HW_H_CSR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* sar: shift amount register */
  { "sar", MEP_OPERAND_SAR, HW_H_CSR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* hi: high result */
  { "hi", MEP_OPERAND_HI, HW_H_CSR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* lo: low result */
  { "lo", MEP_OPERAND_LO, HW_H_CSR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* mb0: modulo begin register 0 */
  { "mb0", MEP_OPERAND_MB0, HW_H_CSR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* me0: modulo end register 0 */
  { "me0", MEP_OPERAND_ME0, HW_H_CSR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* mb1: modulo begin register 1 */
  { "mb1", MEP_OPERAND_MB1, HW_H_CSR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* me1: modulo end register 1 */
  { "me1", MEP_OPERAND_ME1, HW_H_CSR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* psw: program status word */
  { "psw", MEP_OPERAND_PSW, HW_H_CSR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* epc: exception prog counter */
  { "epc", MEP_OPERAND_EPC, HW_H_CSR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* exc: exception cause */
  { "exc", MEP_OPERAND_EXC, HW_H_CSR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* npc: nmi program counter */
  { "npc", MEP_OPERAND_NPC, HW_H_CSR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* dbg: debug register */
  { "dbg", MEP_OPERAND_DBG, HW_H_CSR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* depc: debug exception pc */
  { "depc", MEP_OPERAND_DEPC, HW_H_CSR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* opt: option register */
  { "opt", MEP_OPERAND_OPT, HW_H_CSR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* r1: register 1 */
  { "r1", MEP_OPERAND_R1, HW_H_GPR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* tp: tiny data area pointer */
  { "tp", MEP_OPERAND_TP, HW_H_GPR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* sp: stack pointer */
  { "sp", MEP_OPERAND_SP, HW_H_GPR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* tpr: comment */
  { "tpr", MEP_OPERAND_TPR, HW_H_GPR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* spr: comment */
  { "spr", MEP_OPERAND_SPR, HW_H_GPR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* csrn: control/special register */
  { "csrn", MEP_OPERAND_CSRN, HW_H_CSR, 8, 5,
    { 2, { (const PTR) &MEP_F_CSRN_MULTI_IFIELD[0] } }, 
    { 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_REGNUM, 0 } }, { { 1, 0 } } } }  },
/* csrn-idx: control/special reg idx */
  { "csrn-idx", MEP_OPERAND_CSRN_IDX, HW_H_UINT, 8, 5,
    { 2, { (const PTR) &MEP_F_CSRN_MULTI_IFIELD[0] } }, 
    { 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* crn64: copro Rn (64-bit) */
  { "crn64", MEP_OPERAND_CRN64, HW_H_CR64, 4, 4,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_CRN] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_CP_DATA_BUS_INT, 0 } }, { { 1, 0 } } } }  },
/* crn: copro Rn (32-bit) */
  { "crn", MEP_OPERAND_CRN, HW_H_CR, 4, 4,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_CRN] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_CP_DATA_BUS_INT, 0 } }, { { 1, 0 } } } }  },
/* crnx64: copro Rn (0-31, 64-bit) */
  { "crnx64", MEP_OPERAND_CRNX64, HW_H_CR64, 4, 5,
    { 2, { (const PTR) &MEP_F_CRNX_MULTI_IFIELD[0] } }, 
    { 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_CP_DATA_BUS_INT, 0 } }, { { 1, 0 } } } }  },
/* crnx: copro Rn (0-31, 32-bit) */
  { "crnx", MEP_OPERAND_CRNX, HW_H_CR, 4, 5,
    { 2, { (const PTR) &MEP_F_CRNX_MULTI_IFIELD[0] } }, 
    { 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_CP_DATA_BUS_INT, 0 } }, { { 1, 0 } } } }  },
/* ccrn: copro control reg CCRn */
  { "ccrn", MEP_OPERAND_CCRN, HW_H_CCR, 4, 6,
    { 2, { (const PTR) &MEP_F_CCRN_MULTI_IFIELD[0] } }, 
    { 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_REGNUM, 0 } }, { { 1, 0 } } } }  },
/* cccc: copro flags */
  { "cccc", MEP_OPERAND_CCCC, HW_H_UINT, 8, 4,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_RM] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* pcrel8a2: comment */
  { "pcrel8a2", MEP_OPERAND_PCREL8A2, HW_H_SINT, 8, 7,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_8S8A2] } }, 
    { 0|A(RELAX)|A(PCREL_ADDR), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LABEL, 0 } }, { { 1, 0 } } } }  },
/* pcrel12a2: comment */
  { "pcrel12a2", MEP_OPERAND_PCREL12A2, HW_H_SINT, 4, 11,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_12S4A2] } }, 
    { 0|A(RELAX)|A(PCREL_ADDR), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LABEL, 0 } }, { { 1, 0 } } } }  },
/* pcrel17a2: comment */
  { "pcrel17a2", MEP_OPERAND_PCREL17A2, HW_H_SINT, 16, 16,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_17S16A2] } }, 
    { 0|A(RELAX)|A(PCREL_ADDR), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LABEL, 0 } }, { { 1, 0 } } } }  },
/* pcrel24a2: comment */
  { "pcrel24a2", MEP_OPERAND_PCREL24A2, HW_H_SINT, 5, 23,
    { 2, { (const PTR) &MEP_F_24S5A2N_MULTI_IFIELD[0] } }, 
    { 0|A(PCREL_ADDR)|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LABEL, 0 } }, { { 1, 0 } } } }  },
/* pcabs24a2: comment */
  { "pcabs24a2", MEP_OPERAND_PCABS24A2, HW_H_UINT, 5, 23,
    { 2, { (const PTR) &MEP_F_24U5A2N_MULTI_IFIELD[0] } }, 
    { 0|A(ABS_ADDR)|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LABEL, 0 } }, { { 1, 0 } } } }  },
/* sdisp16: comment */
  { "sdisp16", MEP_OPERAND_SDISP16, HW_H_SINT, 16, 16,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_16S16] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* simm16: comment */
  { "simm16", MEP_OPERAND_SIMM16, HW_H_SINT, 16, 16,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_16S16] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* uimm16: comment */
  { "uimm16", MEP_OPERAND_UIMM16, HW_H_UINT, 16, 16,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_16U16] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* code16: uci/dsp code (16 bits) */
  { "code16", MEP_OPERAND_CODE16, HW_H_UINT, 16, 16,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_16U16] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* udisp2: SSARB addend (2 bits) */
  { "udisp2", MEP_OPERAND_UDISP2, HW_H_SINT, 6, 2,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_2U6] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* uimm2: interrupt (2 bits) */
  { "uimm2", MEP_OPERAND_UIMM2, HW_H_UINT, 10, 2,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_2U10] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* simm6: add const (6 bits) */
  { "simm6", MEP_OPERAND_SIMM6, HW_H_SINT, 8, 6,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_6S8] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* simm8: mov const (8 bits) */
  { "simm8", MEP_OPERAND_SIMM8, HW_H_SINT, 8, 8,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_8S8] } }, 
    { 0|A(RELOC_IMPLIES_OVERFLOW), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* addr24a4: comment */
  { "addr24a4", MEP_OPERAND_ADDR24A4, HW_H_UINT, 8, 22,
    { 2, { (const PTR) &MEP_F_24U8A4N_MULTI_IFIELD[0] } }, 
    { 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 4, 0 } } } }  },
/* code24: coprocessor code */
  { "code24", MEP_OPERAND_CODE24, HW_H_UINT, 4, 24,
    { 2, { (const PTR) &MEP_F_24U4N_MULTI_IFIELD[0] } }, 
    { 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* callnum: system call number */
  { "callnum", MEP_OPERAND_CALLNUM, HW_H_UINT, 5, 4,
    { 4, { (const PTR) &MEP_F_CALLNUM_MULTI_IFIELD[0] } }, 
    { 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* uimm3: bit immediate (3 bits) */
  { "uimm3", MEP_OPERAND_UIMM3, HW_H_UINT, 5, 3,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_3U5] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* uimm4: bCC const (4 bits) */
  { "uimm4", MEP_OPERAND_UIMM4, HW_H_UINT, 8, 4,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_4U8] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* uimm5: bit/shift val (5 bits) */
  { "uimm5", MEP_OPERAND_UIMM5, HW_H_UINT, 8, 5,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_5U8] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* udisp7: comment */
  { "udisp7", MEP_OPERAND_UDISP7, HW_H_UINT, 9, 7,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_7U9] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* udisp7a2: comment */
  { "udisp7a2", MEP_OPERAND_UDISP7A2, HW_H_UINT, 9, 6,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_7U9A2] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 2, 0 } } } }  },
/* udisp7a4: comment */
  { "udisp7a4", MEP_OPERAND_UDISP7A4, HW_H_UINT, 9, 5,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_7U9A4] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 4, 0 } } } }  },
/* uimm7a4: comment */
  { "uimm7a4", MEP_OPERAND_UIMM7A4, HW_H_UINT, 9, 5,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_7U9A4] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 4, 0 } } } }  },
/* uimm24: immediate (24 bits) */
  { "uimm24", MEP_OPERAND_UIMM24, HW_H_UINT, 8, 24,
    { 2, { (const PTR) &MEP_F_24U8N_MULTI_IFIELD[0] } }, 
    { 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* cimm4: cache immed'te (4 bits) */
  { "cimm4", MEP_OPERAND_CIMM4, HW_H_UINT, 4, 4,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_RN] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* cimm5: clip immediate (5 bits) */
  { "cimm5", MEP_OPERAND_CIMM5, HW_H_UINT, 24, 5,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_5U24] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* cdisp8: copro addend (8 bits) */
  { "cdisp8", MEP_OPERAND_CDISP8, HW_H_SINT, 24, 8,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_8S24] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* cdisp8a2: comment */
  { "cdisp8a2", MEP_OPERAND_CDISP8A2, HW_H_SINT, 24, 7,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_8S24A2] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 2, 0 } } } }  },
/* cdisp8a4: comment */
  { "cdisp8a4", MEP_OPERAND_CDISP8A4, HW_H_SINT, 24, 6,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_8S24A4] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 4, 0 } } } }  },
/* cdisp8a8: comment */
  { "cdisp8a8", MEP_OPERAND_CDISP8A8, HW_H_SINT, 24, 5,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_8S24A8] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 8, 0 } } } }  },
/* zero: Zero operand */
  { "zero", MEP_OPERAND_ZERO, HW_H_SINT, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* cp_flag: branch condition register */
  { "cp_flag", MEP_OPERAND_CP_FLAG, HW_H_CCR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xfe" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* fmax-FRd: FRd */
  { "fmax-FRd", MEP_OPERAND_FMAX_FRD, HW_H_CR, 4, 5,
    { 2, { (const PTR) &MEP_F_FMAX_FRD_MULTI_IFIELD[0] } }, 
    { 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { CDATA_FMAX_FLOAT, 0 } }, { { 1, 0 } } } }  },
/* fmax-FRn: FRn */
  { "fmax-FRn", MEP_OPERAND_FMAX_FRN, HW_H_CR, 20, 5,
    { 2, { (const PTR) &MEP_F_FMAX_FRN_MULTI_IFIELD[0] } }, 
    { 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { CDATA_FMAX_FLOAT, 0 } }, { { 1, 0 } } } }  },
/* fmax-FRm: FRm */
  { "fmax-FRm", MEP_OPERAND_FMAX_FRM, HW_H_CR, 24, 5,
    { 2, { (const PTR) &MEP_F_FMAX_FRM_MULTI_IFIELD[0] } }, 
    { 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { CDATA_FMAX_FLOAT, 0 } }, { { 1, 0 } } } }  },
/* fmax-FRd-int: FRd as an integer */
  { "fmax-FRd-int", MEP_OPERAND_FMAX_FRD_INT, HW_H_CR, 4, 5,
    { 2, { (const PTR) &MEP_F_FMAX_FRD_MULTI_IFIELD[0] } }, 
    { 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { CDATA_FMAX_INT, 0 } }, { { 1, 0 } } } }  },
/* fmax-FRn-int: FRn as an integer */
  { "fmax-FRn-int", MEP_OPERAND_FMAX_FRN_INT, HW_H_CR, 20, 5,
    { 2, { (const PTR) &MEP_F_FMAX_FRN_MULTI_IFIELD[0] } }, 
    { 0|A(VIRTUAL), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { CDATA_FMAX_INT, 0 } }, { { 1, 0 } } } }  },
/* fmax-CCRn: CCRn */
  { "fmax-CCRn", MEP_OPERAND_FMAX_CCRN, HW_H_CCR, 4, 4,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_FMAX_4_4] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { CDATA_REGNUM, 0 } }, { { 1, 0 } } } }  },
/* fmax-CIRR: CIRR */
  { "fmax-CIRR", MEP_OPERAND_FMAX_CIRR, HW_H_CCR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0|A(SEM_ONLY), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* fmax-CBCR: CBCR */
  { "fmax-CBCR", MEP_OPERAND_FMAX_CBCR, HW_H_CCR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0|A(SEM_ONLY), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* fmax-CERR: CERR */
  { "fmax-CERR", MEP_OPERAND_FMAX_CERR, HW_H_CCR, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0|A(SEM_ONLY), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* fmax-Rm: Rm */
  { "fmax-Rm", MEP_OPERAND_FMAX_RM, HW_H_GPR, 8, 4,
    { 0, { (const PTR) &mep_cgen_ifld_table[MEP_F_FMAX_RM] } }, 
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* fmax-Compare-i-p: flag */
  { "fmax-Compare-i-p", MEP_OPERAND_FMAX_COMPARE_I_P, HW_H_FMAX_COMPARE_I_P, 0, 0,
    { 0, { (const PTR) 0 } }, 
    { 0|A(SEM_ONLY), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } }  },
/* sentinel */
  { 0, 0, 0, 0, 0,
    { 0, { (const PTR) 0 } },
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x80" } }, { { CDATA_LONG, 0 } }, { { 1, 0 } } } } }
};

#undef A


/* The instruction table.  */

#define OP(field) CGEN_SYNTAX_MAKE_FIELD (OPERAND (field))
#if defined (__STDC__) || defined (ALMOST_STDC) || defined (HAVE_STRINGIZE)
#define A(a) (1 << CGEN_INSN_##a)
#else
#define A(a) (1 << CGEN_INSN_/**/a)
#endif

static const CGEN_IBASE mep_cgen_insn_table[MAX_INSNS] =
{
  /* Special null first entry.
     A `num' value of zero is thus invalid.
     Also, the special `invalid' insn resides here.  */
  { 0, 0, 0, 0, { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x80" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } } },
/* sb $rnc,($rma) */
  {
    MEP_INSN_SB, "sb", "sb", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sh $rns,($rma) */
  {
    MEP_INSN_SH, "sh", "sh", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sw $rnl,($rma) */
  {
    MEP_INSN_SW, "sw", "sw", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lb $rnc,($rma) */
  {
    MEP_INSN_LB, "lb", "lb", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 2, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lh $rns,($rma) */
  {
    MEP_INSN_LH, "lh", "lh", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 2, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lw $rnl,($rma) */
  {
    MEP_INSN_LW, "lw", "lw", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 2, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lbu $rnuc,($rma) */
  {
    MEP_INSN_LBU, "lbu", "lbu", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 2, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lhu $rnus,($rma) */
  {
    MEP_INSN_LHU, "lhu", "lhu", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 2, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sw $rnl,$udisp7a4($spr) */
  {
    MEP_INSN_SW_SP, "sw-sp", "sw", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lw $rnl,$udisp7a4($spr) */
  {
    MEP_INSN_LW_SP, "lw-sp", "lw", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 2, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sb $rn3c,$udisp7($tpr) */
  {
    MEP_INSN_SB_TP, "sb-tp", "sb", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sh $rn3s,$udisp7a2($tpr) */
  {
    MEP_INSN_SH_TP, "sh-tp", "sh", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sw $rn3l,$udisp7a4($tpr) */
  {
    MEP_INSN_SW_TP, "sw-tp", "sw", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lb $rn3c,$udisp7($tpr) */
  {
    MEP_INSN_LB_TP, "lb-tp", "lb", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 2, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lh $rn3s,$udisp7a2($tpr) */
  {
    MEP_INSN_LH_TP, "lh-tp", "lh", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 2, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lw $rn3l,$udisp7a4($tpr) */
  {
    MEP_INSN_LW_TP, "lw-tp", "lw", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 2, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lbu $rn3uc,$udisp7($tpr) */
  {
    MEP_INSN_LBU_TP, "lbu-tp", "lbu", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 2, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lhu $rn3us,$udisp7a2($tpr) */
  {
    MEP_INSN_LHU_TP, "lhu-tp", "lhu", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 2, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sb $rnc,$sdisp16($rma) */
  {
    MEP_INSN_SB16, "sb16", "sb", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sh $rns,$sdisp16($rma) */
  {
    MEP_INSN_SH16, "sh16", "sh", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sw $rnl,$sdisp16($rma) */
  {
    MEP_INSN_SW16, "sw16", "sw", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lb $rnc,$sdisp16($rma) */
  {
    MEP_INSN_LB16, "lb16", "lb", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 2, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lh $rns,$sdisp16($rma) */
  {
    MEP_INSN_LH16, "lh16", "lh", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 2, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lw $rnl,$sdisp16($rma) */
  {
    MEP_INSN_LW16, "lw16", "lw", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 2, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lbu $rnuc,$sdisp16($rma) */
  {
    MEP_INSN_LBU16, "lbu16", "lbu", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 2, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lhu $rnus,$sdisp16($rma) */
  {
    MEP_INSN_LHU16, "lhu16", "lhu", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 2, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sw $rnl,($addr24a4) */
  {
    MEP_INSN_SW24, "sw24", "sw", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lw $rnl,($addr24a4) */
  {
    MEP_INSN_LW24, "lw24", "lw", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 2, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* extb $rn */
  {
    MEP_INSN_EXTB, "extb", "extb", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* exth $rn */
  {
    MEP_INSN_EXTH, "exth", "exth", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* extub $rn */
  {
    MEP_INSN_EXTUB, "extub", "extub", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* extuh $rn */
  {
    MEP_INSN_EXTUH, "extuh", "extuh", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* ssarb $udisp2($rm) */
  {
    MEP_INSN_SSARB, "ssarb", "ssarb", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* mov $rn,$rm */
  {
    MEP_INSN_MOV, "mov", "mov", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* mov $rn,$simm8 */
  {
    MEP_INSN_MOVI8, "movi8", "mov", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* mov $rn,$simm16 */
  {
    MEP_INSN_MOVI16, "movi16", "mov", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* movu $rn3,$uimm24 */
  {
    MEP_INSN_MOVU24, "movu24", "movu", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* movu $rn,$uimm16 */
  {
    MEP_INSN_MOVU16, "movu16", "movu", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* movh $rn,$uimm16 */
  {
    MEP_INSN_MOVH, "movh", "movh", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* add3 $rl,$rn,$rm */
  {
    MEP_INSN_ADD3, "add3", "add3", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* add $rn,$simm6 */
  {
    MEP_INSN_ADD, "add", "add", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* add3 $rn,$spr,$uimm7a4 */
  {
    MEP_INSN_ADD3I, "add3i", "add3", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* advck3 \$0,$rn,$rm */
  {
    MEP_INSN_ADVCK3, "advck3", "advck3", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sub $rn,$rm */
  {
    MEP_INSN_SUB, "sub", "sub", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sbvck3 \$0,$rn,$rm */
  {
    MEP_INSN_SBVCK3, "sbvck3", "sbvck3", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* neg $rn,$rm */
  {
    MEP_INSN_NEG, "neg", "neg", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* slt3 \$0,$rn,$rm */
  {
    MEP_INSN_SLT3, "slt3", "slt3", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sltu3 \$0,$rn,$rm */
  {
    MEP_INSN_SLTU3, "sltu3", "sltu3", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* slt3 \$0,$rn,$uimm5 */
  {
    MEP_INSN_SLT3I, "slt3i", "slt3", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sltu3 \$0,$rn,$uimm5 */
  {
    MEP_INSN_SLTU3I, "sltu3i", "sltu3", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sl1ad3 \$0,$rn,$rm */
  {
    MEP_INSN_SL1AD3, "sl1ad3", "sl1ad3", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sl2ad3 \$0,$rn,$rm */
  {
    MEP_INSN_SL2AD3, "sl2ad3", "sl2ad3", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* add3 $rn,$rm,$simm16 */
  {
    MEP_INSN_ADD3X, "add3x", "add3", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* slt3 $rn,$rm,$simm16 */
  {
    MEP_INSN_SLT3X, "slt3x", "slt3", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sltu3 $rn,$rm,$uimm16 */
  {
    MEP_INSN_SLTU3X, "sltu3x", "sltu3", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* or $rn,$rm */
  {
    MEP_INSN_OR, "or", "or", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* and $rn,$rm */
  {
    MEP_INSN_AND, "and", "and", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* xor $rn,$rm */
  {
    MEP_INSN_XOR, "xor", "xor", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* nor $rn,$rm */
  {
    MEP_INSN_NOR, "nor", "nor", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* or3 $rn,$rm,$uimm16 */
  {
    MEP_INSN_OR3, "or3", "or3", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* and3 $rn,$rm,$uimm16 */
  {
    MEP_INSN_AND3, "and3", "and3", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* xor3 $rn,$rm,$uimm16 */
  {
    MEP_INSN_XOR3, "xor3", "xor3", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sra $rn,$rm */
  {
    MEP_INSN_SRA, "sra", "sra", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* srl $rn,$rm */
  {
    MEP_INSN_SRL, "srl", "srl", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sll $rn,$rm */
  {
    MEP_INSN_SLL, "sll", "sll", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sra $rn,$uimm5 */
  {
    MEP_INSN_SRAI, "srai", "sra", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* srl $rn,$uimm5 */
  {
    MEP_INSN_SRLI, "srli", "srl", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sll $rn,$uimm5 */
  {
    MEP_INSN_SLLI, "slli", "sll", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sll3 \$0,$rn,$uimm5 */
  {
    MEP_INSN_SLL3, "sll3", "sll3", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fsft $rn,$rm */
  {
    MEP_INSN_FSFT, "fsft", "fsft", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* bra $pcrel12a2 */
  {
    MEP_INSN_BRA, "bra", "bra", 16,
    { 0|A(RELAXABLE)|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* beqz $rn,$pcrel8a2 */
  {
    MEP_INSN_BEQZ, "beqz", "beqz", 16,
    { 0|A(RELAXABLE)|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* bnez $rn,$pcrel8a2 */
  {
    MEP_INSN_BNEZ, "bnez", "bnez", 16,
    { 0|A(RELAXABLE)|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* beqi $rn,$uimm4,$pcrel17a2 */
  {
    MEP_INSN_BEQI, "beqi", "beqi", 32,
    { 0|A(RELAXABLE)|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* bnei $rn,$uimm4,$pcrel17a2 */
  {
    MEP_INSN_BNEI, "bnei", "bnei", 32,
    { 0|A(RELAXABLE)|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* blti $rn,$uimm4,$pcrel17a2 */
  {
    MEP_INSN_BLTI, "blti", "blti", 32,
    { 0|A(RELAXABLE)|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* bgei $rn,$uimm4,$pcrel17a2 */
  {
    MEP_INSN_BGEI, "bgei", "bgei", 32,
    { 0|A(RELAXABLE)|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* beq $rn,$rm,$pcrel17a2 */
  {
    MEP_INSN_BEQ, "beq", "beq", 32,
    { 0|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* bne $rn,$rm,$pcrel17a2 */
  {
    MEP_INSN_BNE, "bne", "bne", 32,
    { 0|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* bsr $pcrel12a2 */
  {
    MEP_INSN_BSR12, "bsr12", "bsr", 16,
    { 0|A(RELAXABLE)|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* bsr $pcrel24a2 */
  {
    MEP_INSN_BSR24, "bsr24", "bsr", 32,
    { 0|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* jmp $rm */
  {
    MEP_INSN_JMP, "jmp", "jmp", 16,
    { 0|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* jmp $pcabs24a2 */
  {
    MEP_INSN_JMP24, "jmp24", "jmp", 32,
    { 0|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* jsr $rm */
  {
    MEP_INSN_JSR, "jsr", "jsr", 16,
    { 0|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* ret */
  {
    MEP_INSN_RET, "ret", "ret", 16,
    { 0|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* repeat $rn,$pcrel17a2 */
  {
    MEP_INSN_REPEAT, "repeat", "repeat", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* erepeat $pcrel17a2 */
  {
    MEP_INSN_EREPEAT, "erepeat", "erepeat", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* stc $rn,\$lp */
  {
    MEP_INSN_STC_LP, "stc_lp", "stc", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* stc $rn,\$hi */
  {
    MEP_INSN_STC_HI, "stc_hi", "stc", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* stc $rn,\$lo */
  {
    MEP_INSN_STC_LO, "stc_lo", "stc", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* stc $rn,$csrn */
  {
    MEP_INSN_STC, "stc", "stc", 16,
    { 0|A(VOLATILE), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* ldc $rn,\$lp */
  {
    MEP_INSN_LDC_LP, "ldc_lp", "ldc", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* ldc $rn,\$hi */
  {
    MEP_INSN_LDC_HI, "ldc_hi", "ldc", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* ldc $rn,\$lo */
  {
    MEP_INSN_LDC_LO, "ldc_lo", "ldc", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* ldc $rn,$csrn */
  {
    MEP_INSN_LDC, "ldc", "ldc", 16,
    { 0|A(VOLATILE), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 2, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* di */
  {
    MEP_INSN_DI, "di", "di", 16,
    { 0|A(VOLATILE), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* ei */
  {
    MEP_INSN_EI, "ei", "ei", 16,
    { 0|A(VOLATILE), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* reti */
  {
    MEP_INSN_RETI, "reti", "reti", 16,
    { 0|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* halt */
  {
    MEP_INSN_HALT, "halt", "halt", 16,
    { 0|A(VOLATILE), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sleep */
  {
    MEP_INSN_SLEEP, "sleep", "sleep", 16,
    { 0|A(VOLATILE), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* swi $uimm2 */
  {
    MEP_INSN_SWI, "swi", "swi", 16,
    { 0|A(VOLATILE)|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* break */
  {
    MEP_INSN_BREAK, "break", "break", 16,
    { 0|A(VOLATILE)|A(MAY_TRAP)|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* syncm */
  {
    MEP_INSN_SYNCM, "syncm", "syncm", 16,
    { 0|A(VOLATILE), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* stcb $rn,$uimm16 */
  {
    MEP_INSN_STCB, "stcb", "stcb", 32,
    { 0|A(VOLATILE), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* ldcb $rn,$uimm16 */
  {
    MEP_INSN_LDCB, "ldcb", "ldcb", 32,
    { 0|A(VOLATILE), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 3, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* bsetm ($rma),$uimm3 */
  {
    MEP_INSN_BSETM, "bsetm", "bsetm", 16,
    { 0|A(OPTIONAL_BIT_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* bclrm ($rma),$uimm3 */
  {
    MEP_INSN_BCLRM, "bclrm", "bclrm", 16,
    { 0|A(OPTIONAL_BIT_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* bnotm ($rma),$uimm3 */
  {
    MEP_INSN_BNOTM, "bnotm", "bnotm", 16,
    { 0|A(OPTIONAL_BIT_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* btstm \$0,($rma),$uimm3 */
  {
    MEP_INSN_BTSTM, "btstm", "btstm", 16,
    { 0|A(OPTIONAL_BIT_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* tas $rn,($rma) */
  {
    MEP_INSN_TAS, "tas", "tas", 16,
    { 0|A(OPTIONAL_BIT_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* cache $cimm4,($rma) */
  {
    MEP_INSN_CACHE, "cache", "cache", 16,
    { 0|A(VOLATILE), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* mul $rn,$rm */
  {
    MEP_INSN_MUL, "mul", "mul", 16,
    { 0|A(OPTIONAL_MUL_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* mulu $rn,$rm */
  {
    MEP_INSN_MULU, "mulu", "mulu", 16,
    { 0|A(OPTIONAL_MUL_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* mulr $rn,$rm */
  {
    MEP_INSN_MULR, "mulr", "mulr", 16,
    { 0|A(OPTIONAL_MUL_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 3, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* mulru $rn,$rm */
  {
    MEP_INSN_MULRU, "mulru", "mulru", 16,
    { 0|A(OPTIONAL_MUL_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 3, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* madd $rn,$rm */
  {
    MEP_INSN_MADD, "madd", "madd", 32,
    { 0|A(OPTIONAL_MUL_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* maddu $rn,$rm */
  {
    MEP_INSN_MADDU, "maddu", "maddu", 32,
    { 0|A(OPTIONAL_MUL_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* maddr $rn,$rm */
  {
    MEP_INSN_MADDR, "maddr", "maddr", 32,
    { 0|A(OPTIONAL_MUL_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 3, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* maddru $rn,$rm */
  {
    MEP_INSN_MADDRU, "maddru", "maddru", 32,
    { 0|A(OPTIONAL_MUL_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 3, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* div $rn,$rm */
  {
    MEP_INSN_DIV, "div", "div", 16,
    { 0|A(MAY_TRAP)|A(OPTIONAL_DIV_INSN)|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 34, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* divu $rn,$rm */
  {
    MEP_INSN_DIVU, "divu", "divu", 16,
    { 0|A(MAY_TRAP)|A(OPTIONAL_DIV_INSN)|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 34, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* dret */
  {
    MEP_INSN_DRET, "dret", "dret", 16,
    { 0|A(OPTIONAL_DEBUG_INSN)|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* dbreak */
  {
    MEP_INSN_DBREAK, "dbreak", "dbreak", 16,
    { 0|A(VOLATILE)|A(MAY_TRAP)|A(OPTIONAL_DEBUG_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* ldz $rn,$rm */
  {
    MEP_INSN_LDZ, "ldz", "ldz", 32,
    { 0|A(OPTIONAL_LDZ_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* abs $rn,$rm */
  {
    MEP_INSN_ABS, "abs", "abs", 32,
    { 0|A(OPTIONAL_ABS_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* ave $rn,$rm */
  {
    MEP_INSN_AVE, "ave", "ave", 32,
    { 0|A(OPTIONAL_AVE_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* min $rn,$rm */
  {
    MEP_INSN_MIN, "min", "min", 32,
    { 0|A(OPTIONAL_MINMAX_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* max $rn,$rm */
  {
    MEP_INSN_MAX, "max", "max", 32,
    { 0|A(OPTIONAL_MINMAX_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* minu $rn,$rm */
  {
    MEP_INSN_MINU, "minu", "minu", 32,
    { 0|A(OPTIONAL_MINMAX_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* maxu $rn,$rm */
  {
    MEP_INSN_MAXU, "maxu", "maxu", 32,
    { 0|A(OPTIONAL_MINMAX_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* clip $rn,$cimm5 */
  {
    MEP_INSN_CLIP, "clip", "clip", 32,
    { 0|A(OPTIONAL_CLIP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* clipu $rn,$cimm5 */
  {
    MEP_INSN_CLIPU, "clipu", "clipu", 32,
    { 0|A(OPTIONAL_CLIP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sadd $rn,$rm */
  {
    MEP_INSN_SADD, "sadd", "sadd", 32,
    { 0|A(OPTIONAL_SAT_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* ssub $rn,$rm */
  {
    MEP_INSN_SSUB, "ssub", "ssub", 32,
    { 0|A(OPTIONAL_SAT_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* saddu $rn,$rm */
  {
    MEP_INSN_SADDU, "saddu", "saddu", 32,
    { 0|A(OPTIONAL_SAT_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* ssubu $rn,$rm */
  {
    MEP_INSN_SSUBU, "ssubu", "ssubu", 32,
    { 0|A(OPTIONAL_SAT_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* swcp $crn,($rma) */
  {
    MEP_INSN_SWCP, "swcp", "swcp", 16,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lwcp $crn,($rma) */
  {
    MEP_INSN_LWCP, "lwcp", "lwcp", 16,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* smcp $crn64,($rma) */
  {
    MEP_INSN_SMCP, "smcp", "smcp", 16,
    { 0|A(OPTIONAL_CP64_INSN)|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lmcp $crn64,($rma) */
  {
    MEP_INSN_LMCP, "lmcp", "lmcp", 16,
    { 0|A(OPTIONAL_CP64_INSN)|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* swcpi $crn,($rma+) */
  {
    MEP_INSN_SWCPI, "swcpi", "swcpi", 16,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lwcpi $crn,($rma+) */
  {
    MEP_INSN_LWCPI, "lwcpi", "lwcpi", 16,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* smcpi $crn64,($rma+) */
  {
    MEP_INSN_SMCPI, "smcpi", "smcpi", 16,
    { 0|A(OPTIONAL_CP64_INSN)|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lmcpi $crn64,($rma+) */
  {
    MEP_INSN_LMCPI, "lmcpi", "lmcpi", 16,
    { 0|A(OPTIONAL_CP64_INSN)|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* swcp $crn,$sdisp16($rma) */
  {
    MEP_INSN_SWCP16, "swcp16", "swcp", 32,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lwcp $crn,$sdisp16($rma) */
  {
    MEP_INSN_LWCP16, "lwcp16", "lwcp", 32,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* smcp $crn64,$sdisp16($rma) */
  {
    MEP_INSN_SMCP16, "smcp16", "smcp", 32,
    { 0|A(OPTIONAL_CP64_INSN)|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lmcp $crn64,$sdisp16($rma) */
  {
    MEP_INSN_LMCP16, "lmcp16", "lmcp", 32,
    { 0|A(OPTIONAL_CP64_INSN)|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sbcpa $crn,($rma+),$cdisp8 */
  {
    MEP_INSN_SBCPA, "sbcpa", "sbcpa", 32,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lbcpa $crn,($rma+),$cdisp8 */
  {
    MEP_INSN_LBCPA, "lbcpa", "lbcpa", 32,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* shcpa $crn,($rma+),$cdisp8a2 */
  {
    MEP_INSN_SHCPA, "shcpa", "shcpa", 32,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lhcpa $crn,($rma+),$cdisp8a2 */
  {
    MEP_INSN_LHCPA, "lhcpa", "lhcpa", 32,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* swcpa $crn,($rma+),$cdisp8a4 */
  {
    MEP_INSN_SWCPA, "swcpa", "swcpa", 32,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lwcpa $crn,($rma+),$cdisp8a4 */
  {
    MEP_INSN_LWCPA, "lwcpa", "lwcpa", 32,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* smcpa $crn64,($rma+),$cdisp8a8 */
  {
    MEP_INSN_SMCPA, "smcpa", "smcpa", 32,
    { 0|A(OPTIONAL_CP64_INSN)|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lmcpa $crn64,($rma+),$cdisp8a8 */
  {
    MEP_INSN_LMCPA, "lmcpa", "lmcpa", 32,
    { 0|A(OPTIONAL_CP64_INSN)|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sbcpm0 $crn,($rma+),$cdisp8 */
  {
    MEP_INSN_SBCPM0, "sbcpm0", "sbcpm0", 32,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lbcpm0 $crn,($rma+),$cdisp8 */
  {
    MEP_INSN_LBCPM0, "lbcpm0", "lbcpm0", 32,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* shcpm0 $crn,($rma+),$cdisp8a2 */
  {
    MEP_INSN_SHCPM0, "shcpm0", "shcpm0", 32,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lhcpm0 $crn,($rma+),$cdisp8a2 */
  {
    MEP_INSN_LHCPM0, "lhcpm0", "lhcpm0", 32,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* swcpm0 $crn,($rma+),$cdisp8a4 */
  {
    MEP_INSN_SWCPM0, "swcpm0", "swcpm0", 32,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lwcpm0 $crn,($rma+),$cdisp8a4 */
  {
    MEP_INSN_LWCPM0, "lwcpm0", "lwcpm0", 32,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* smcpm0 $crn64,($rma+),$cdisp8a8 */
  {
    MEP_INSN_SMCPM0, "smcpm0", "smcpm0", 32,
    { 0|A(OPTIONAL_CP64_INSN)|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lmcpm0 $crn64,($rma+),$cdisp8a8 */
  {
    MEP_INSN_LMCPM0, "lmcpm0", "lmcpm0", 32,
    { 0|A(OPTIONAL_CP64_INSN)|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sbcpm1 $crn,($rma+),$cdisp8 */
  {
    MEP_INSN_SBCPM1, "sbcpm1", "sbcpm1", 32,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lbcpm1 $crn,($rma+),$cdisp8 */
  {
    MEP_INSN_LBCPM1, "lbcpm1", "lbcpm1", 32,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* shcpm1 $crn,($rma+),$cdisp8a2 */
  {
    MEP_INSN_SHCPM1, "shcpm1", "shcpm1", 32,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lhcpm1 $crn,($rma+),$cdisp8a2 */
  {
    MEP_INSN_LHCPM1, "lhcpm1", "lhcpm1", 32,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* swcpm1 $crn,($rma+),$cdisp8a4 */
  {
    MEP_INSN_SWCPM1, "swcpm1", "swcpm1", 32,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lwcpm1 $crn,($rma+),$cdisp8a4 */
  {
    MEP_INSN_LWCPM1, "lwcpm1", "lwcpm1", 32,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* smcpm1 $crn64,($rma+),$cdisp8a8 */
  {
    MEP_INSN_SMCPM1, "smcpm1", "smcpm1", 32,
    { 0|A(OPTIONAL_CP64_INSN)|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lmcpm1 $crn64,($rma+),$cdisp8a8 */
  {
    MEP_INSN_LMCPM1, "lmcpm1", "lmcpm1", 32,
    { 0|A(OPTIONAL_CP64_INSN)|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* bcpeq $cccc,$pcrel17a2 */
  {
    MEP_INSN_BCPEQ, "bcpeq", "bcpeq", 32,
    { 0|A(RELAXABLE)|A(OPTIONAL_CP_INSN)|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* bcpne $cccc,$pcrel17a2 */
  {
    MEP_INSN_BCPNE, "bcpne", "bcpne", 32,
    { 0|A(RELAXABLE)|A(OPTIONAL_CP_INSN)|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* bcpat $cccc,$pcrel17a2 */
  {
    MEP_INSN_BCPAT, "bcpat", "bcpat", 32,
    { 0|A(RELAXABLE)|A(OPTIONAL_CP_INSN)|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* bcpaf $cccc,$pcrel17a2 */
  {
    MEP_INSN_BCPAF, "bcpaf", "bcpaf", 32,
    { 0|A(RELAXABLE)|A(OPTIONAL_CP_INSN)|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* synccp */
  {
    MEP_INSN_SYNCCP, "synccp", "synccp", 16,
    { 0|A(OPTIONAL_CP_INSN), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* jsrv $rm */
  {
    MEP_INSN_JSRV, "jsrv", "jsrv", 16,
    { 0|A(OPTIONAL_CP_INSN)|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* bsrv $pcrel24a2 */
  {
    MEP_INSN_BSRV, "bsrv", "bsrv", 32,
    { 0|A(OPTIONAL_CP_INSN)|A(COND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --unused-- */
  {
    MEP_INSN_SIM_SYSCALL, "sim-syscall", "--unused--", 16,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_0, "ri-0", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_1, "ri-1", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_2, "ri-2", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_3, "ri-3", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_4, "ri-4", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_5, "ri-5", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_6, "ri-6", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_7, "ri-7", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_8, "ri-8", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_9, "ri-9", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_10, "ri-10", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_11, "ri-11", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_12, "ri-12", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_13, "ri-13", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_14, "ri-14", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_15, "ri-15", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_17, "ri-17", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_20, "ri-20", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_21, "ri-21", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_22, "ri-22", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_23, "ri-23", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_24, "ri-24", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_25, "ri-25", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_26, "ri-26", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_16, "ri-16", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_18, "ri-18", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* --reserved-- */
  {
    MEP_INSN_RI_19, "ri-19", "--reserved--", 16,
    { 0|A(UNCOND_CTI), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fadds ${fmax-FRd},${fmax-FRn},${fmax-FRm} */
  {
    MEP_INSN_FADDS, "fadds", "fadds", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fsubs ${fmax-FRd},${fmax-FRn},${fmax-FRm} */
  {
    MEP_INSN_FSUBS, "fsubs", "fsubs", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fmuls ${fmax-FRd},${fmax-FRn},${fmax-FRm} */
  {
    MEP_INSN_FMULS, "fmuls", "fmuls", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fdivs ${fmax-FRd},${fmax-FRn},${fmax-FRm} */
  {
    MEP_INSN_FDIVS, "fdivs", "fdivs", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fsqrts ${fmax-FRd},${fmax-FRn} */
  {
    MEP_INSN_FSQRTS, "fsqrts", "fsqrts", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fabss ${fmax-FRd},${fmax-FRn} */
  {
    MEP_INSN_FABSS, "fabss", "fabss", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fnegs ${fmax-FRd},${fmax-FRn} */
  {
    MEP_INSN_FNEGS, "fnegs", "fnegs", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fmovs ${fmax-FRd},${fmax-FRn} */
  {
    MEP_INSN_FMOVS, "fmovs", "fmovs", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* froundws ${fmax-FRd-int},${fmax-FRn} */
  {
    MEP_INSN_FROUNDWS, "froundws", "froundws", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* ftruncws ${fmax-FRd-int},${fmax-FRn} */
  {
    MEP_INSN_FTRUNCWS, "ftruncws", "ftruncws", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fceilws ${fmax-FRd-int},${fmax-FRn} */
  {
    MEP_INSN_FCEILWS, "fceilws", "fceilws", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* ffloorws ${fmax-FRd-int},${fmax-FRn} */
  {
    MEP_INSN_FFLOORWS, "ffloorws", "ffloorws", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fcvtws ${fmax-FRd-int},${fmax-FRn} */
  {
    MEP_INSN_FCVTWS, "fcvtws", "fcvtws", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fcvtsw ${fmax-FRd},${fmax-FRn-int} */
  {
    MEP_INSN_FCVTSW, "fcvtsw", "fcvtsw", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fcmpfs ${fmax-FRn},${fmax-FRm} */
  {
    MEP_INSN_FCMPFS, "fcmpfs", "fcmpfs", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fcmpus ${fmax-FRn},${fmax-FRm} */
  {
    MEP_INSN_FCMPUS, "fcmpus", "fcmpus", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fcmpes ${fmax-FRn},${fmax-FRm} */
  {
    MEP_INSN_FCMPES, "fcmpes", "fcmpes", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fcmpues ${fmax-FRn},${fmax-FRm} */
  {
    MEP_INSN_FCMPUES, "fcmpues", "fcmpues", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fcmpls ${fmax-FRn},${fmax-FRm} */
  {
    MEP_INSN_FCMPLS, "fcmpls", "fcmpls", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fcmpuls ${fmax-FRn},${fmax-FRm} */
  {
    MEP_INSN_FCMPULS, "fcmpuls", "fcmpuls", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fcmples ${fmax-FRn},${fmax-FRm} */
  {
    MEP_INSN_FCMPLES, "fcmples", "fcmples", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fcmpules ${fmax-FRn},${fmax-FRm} */
  {
    MEP_INSN_FCMPULES, "fcmpules", "fcmpules", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fcmpfis ${fmax-FRn},${fmax-FRm} */
  {
    MEP_INSN_FCMPFIS, "fcmpfis", "fcmpfis", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fcmpuis ${fmax-FRn},${fmax-FRm} */
  {
    MEP_INSN_FCMPUIS, "fcmpuis", "fcmpuis", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fcmpeis ${fmax-FRn},${fmax-FRm} */
  {
    MEP_INSN_FCMPEIS, "fcmpeis", "fcmpeis", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fcmpueis ${fmax-FRn},${fmax-FRm} */
  {
    MEP_INSN_FCMPUEIS, "fcmpueis", "fcmpueis", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fcmplis ${fmax-FRn},${fmax-FRm} */
  {
    MEP_INSN_FCMPLIS, "fcmplis", "fcmplis", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fcmpulis ${fmax-FRn},${fmax-FRm} */
  {
    MEP_INSN_FCMPULIS, "fcmpulis", "fcmpulis", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fcmpleis ${fmax-FRn},${fmax-FRm} */
  {
    MEP_INSN_FCMPLEIS, "fcmpleis", "fcmpleis", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* fcmpuleis ${fmax-FRn},${fmax-FRm} */
  {
    MEP_INSN_FCMPULEIS, "fcmpuleis", "fcmpuleis", 32,
    { 0|A(MAY_TRAP), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* cmov ${fmax-FRd-int},${fmax-Rm} */
  {
    MEP_INSN_CMOV_FRN_RM, "cmov-frn-rm", "cmov", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* cmov ${fmax-Rm},${fmax-FRd-int} */
  {
    MEP_INSN_CMOV_RM_FRN, "cmov-rm-frn", "cmov", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* cmovc ${fmax-CCRn},${fmax-Rm} */
  {
    MEP_INSN_CMOVC_CCRN_RM, "cmovc-ccrn-rm", "cmovc", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* cmovc ${fmax-Rm},${fmax-CCRn} */
  {
    MEP_INSN_CMOVC_RM_CCRN, "cmovc-rm-ccrn", "cmovc", 32,
    { 0, { { { (1<<MACH_BASE), 0 } }, { { 1, "\x20" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
};

#undef OP
#undef A

/* Initialize anything needed to be done once, before any cpu_open call.  */

static void
init_tables (void)
{
}

static const CGEN_MACH * lookup_mach_via_bfd_name (const CGEN_MACH *, const char *);
static void build_hw_table      (CGEN_CPU_TABLE *);
static void build_ifield_table  (CGEN_CPU_TABLE *);
static void build_operand_table (CGEN_CPU_TABLE *);
static void build_insn_table    (CGEN_CPU_TABLE *);
static void mep_cgen_rebuild_tables (CGEN_CPU_TABLE *);

/* Subroutine of mep_cgen_cpu_open to look up a mach via its bfd name.  */

static const CGEN_MACH *
lookup_mach_via_bfd_name (const CGEN_MACH *table, const char *name)
{
  while (table->name)
    {
      if (strcmp (name, table->bfd_name) == 0)
	return table;
      ++table;
    }
  abort ();
}

/* Subroutine of mep_cgen_cpu_open to build the hardware table.  */

static void
build_hw_table (CGEN_CPU_TABLE *cd)
{
  int i;
  int machs = cd->machs;
  const CGEN_HW_ENTRY *init = & mep_cgen_hw_table[0];
  /* MAX_HW is only an upper bound on the number of selected entries.
     However each entry is indexed by it's enum so there can be holes in
     the table.  */
  const CGEN_HW_ENTRY **selected =
    (const CGEN_HW_ENTRY **) xmalloc (MAX_HW * sizeof (CGEN_HW_ENTRY *));

  cd->hw_table.init_entries = init;
  cd->hw_table.entry_size = sizeof (CGEN_HW_ENTRY);
  memset (selected, 0, MAX_HW * sizeof (CGEN_HW_ENTRY *));
  /* ??? For now we just use machs to determine which ones we want.  */
  for (i = 0; init[i].name != NULL; ++i)
    if (CGEN_HW_ATTR_VALUE (&init[i], CGEN_HW_MACH)
	& machs)
      selected[init[i].type] = &init[i];
  cd->hw_table.entries = selected;
  cd->hw_table.num_entries = MAX_HW;
}

/* Subroutine of mep_cgen_cpu_open to build the hardware table.  */

static void
build_ifield_table (CGEN_CPU_TABLE *cd)
{
  cd->ifld_table = & mep_cgen_ifld_table[0];
}

/* Subroutine of mep_cgen_cpu_open to build the hardware table.  */

static void
build_operand_table (CGEN_CPU_TABLE *cd)
{
  int i;
  int machs = cd->machs;
  const CGEN_OPERAND *init = & mep_cgen_operand_table[0];
  /* MAX_OPERANDS is only an upper bound on the number of selected entries.
     However each entry is indexed by it's enum so there can be holes in
     the table.  */
  const CGEN_OPERAND **selected = xmalloc (MAX_OPERANDS * sizeof (* selected));

  cd->operand_table.init_entries = init;
  cd->operand_table.entry_size = sizeof (CGEN_OPERAND);
  memset (selected, 0, MAX_OPERANDS * sizeof (CGEN_OPERAND *));
  /* ??? For now we just use mach to determine which ones we want.  */
  for (i = 0; init[i].name != NULL; ++i)
    if (CGEN_OPERAND_ATTR_VALUE (&init[i], CGEN_OPERAND_MACH)
	& machs)
      selected[init[i].type] = &init[i];
  cd->operand_table.entries = selected;
  cd->operand_table.num_entries = MAX_OPERANDS;
}

/* Subroutine of mep_cgen_cpu_open to build the hardware table.
   ??? This could leave out insns not supported by the specified mach/isa,
   but that would cause errors like "foo only supported by bar" to become
   "unknown insn", so for now we include all insns and require the app to
   do the checking later.
   ??? On the other hand, parsing of such insns may require their hardware or
   operand elements to be in the table [which they mightn't be].  */

static void
build_insn_table (CGEN_CPU_TABLE *cd)
{
  int i;
  const CGEN_IBASE *ib = & mep_cgen_insn_table[0];
  CGEN_INSN *insns = xmalloc (MAX_INSNS * sizeof (CGEN_INSN));

  memset (insns, 0, MAX_INSNS * sizeof (CGEN_INSN));
  for (i = 0; i < MAX_INSNS; ++i)
    insns[i].base = &ib[i];
  cd->insn_table.init_entries = insns;
  cd->insn_table.entry_size = sizeof (CGEN_IBASE);
  cd->insn_table.num_init_entries = MAX_INSNS;
}

/* Subroutine of mep_cgen_cpu_open to rebuild the tables.  */

static void
mep_cgen_rebuild_tables (CGEN_CPU_TABLE *cd)
{
  int i;
  CGEN_BITSET *isas = cd->isas;
  unsigned int machs = cd->machs;

  cd->int_insn_p = CGEN_INT_INSN_P;

  /* Data derived from the isa spec.  */
#define UNSET (CGEN_SIZE_UNKNOWN + 1)
  cd->default_insn_bitsize = UNSET;
  cd->base_insn_bitsize = UNSET;
  cd->min_insn_bitsize = 65535; /* Some ridiculously big number.  */
  cd->max_insn_bitsize = 0;
  for (i = 0; i < MAX_ISAS; ++i)
    if (cgen_bitset_contains (isas, i))
      {
	const CGEN_ISA *isa = & mep_cgen_isa_table[i];

	/* Default insn sizes of all selected isas must be
	   equal or we set the result to 0, meaning "unknown".  */
	if (cd->default_insn_bitsize == UNSET)
	  cd->default_insn_bitsize = isa->default_insn_bitsize;
	else if (isa->default_insn_bitsize == cd->default_insn_bitsize)
	  ; /* This is ok.  */
	else
	  cd->default_insn_bitsize = CGEN_SIZE_UNKNOWN;

	/* Base insn sizes of all selected isas must be equal
	   or we set the result to 0, meaning "unknown".  */
	if (cd->base_insn_bitsize == UNSET)
	  cd->base_insn_bitsize = isa->base_insn_bitsize;
	else if (isa->base_insn_bitsize == cd->base_insn_bitsize)
	  ; /* This is ok.  */
	else
	  cd->base_insn_bitsize = CGEN_SIZE_UNKNOWN;

	/* Set min,max insn sizes.  */
	if (isa->min_insn_bitsize < cd->min_insn_bitsize)
	  cd->min_insn_bitsize = isa->min_insn_bitsize;
	if (isa->max_insn_bitsize > cd->max_insn_bitsize)
	  cd->max_insn_bitsize = isa->max_insn_bitsize;
      }

  /* Data derived from the mach spec.  */
  for (i = 0; i < MAX_MACHS; ++i)
    if (((1 << i) & machs) != 0)
      {
	const CGEN_MACH *mach = & mep_cgen_mach_table[i];

	if (mach->insn_chunk_bitsize != 0)
	{
	  if (cd->insn_chunk_bitsize != 0 && cd->insn_chunk_bitsize != mach->insn_chunk_bitsize)
	    {
	      fprintf (stderr, "mep_cgen_rebuild_tables: conflicting insn-chunk-bitsize values: `%d' vs. `%d'\n",
		       cd->insn_chunk_bitsize, mach->insn_chunk_bitsize);
	      abort ();
	    }

 	  cd->insn_chunk_bitsize = mach->insn_chunk_bitsize;
	}
      }

  /* Determine which hw elements are used by MACH.  */
  build_hw_table (cd);

  /* Build the ifield table.  */
  build_ifield_table (cd);

  /* Determine which operands are used by MACH/ISA.  */
  build_operand_table (cd);

  /* Build the instruction table.  */
  build_insn_table (cd);
}

/* Initialize a cpu table and return a descriptor.
   It's much like opening a file, and must be the first function called.
   The arguments are a set of (type/value) pairs, terminated with
   CGEN_CPU_OPEN_END.

   Currently supported values:
   CGEN_CPU_OPEN_ISAS:    bitmap of values in enum isa_attr
   CGEN_CPU_OPEN_MACHS:   bitmap of values in enum mach_attr
   CGEN_CPU_OPEN_BFDMACH: specify 1 mach using bfd name
   CGEN_CPU_OPEN_ENDIAN:  specify endian choice
   CGEN_CPU_OPEN_END:     terminates arguments

   ??? Simultaneous multiple isas might not make sense, but it's not (yet)
   precluded.

   ??? We only support ISO C stdargs here, not K&R.
   Laziness, plus experiment to see if anything requires K&R - eventually
   K&R will no longer be supported - e.g. GDB is currently trying this.  */

CGEN_CPU_DESC
mep_cgen_cpu_open (enum cgen_cpu_open_arg arg_type, ...)
{
  CGEN_CPU_TABLE *cd = (CGEN_CPU_TABLE *) xmalloc (sizeof (CGEN_CPU_TABLE));
  static int init_p;
  CGEN_BITSET *isas = 0;  /* 0 = "unspecified" */
  unsigned int machs = 0; /* 0 = "unspecified" */
  enum cgen_endian endian = CGEN_ENDIAN_UNKNOWN;
  va_list ap;

  if (! init_p)
    {
      init_tables ();
      init_p = 1;
    }

  memset (cd, 0, sizeof (*cd));

  va_start (ap, arg_type);
  while (arg_type != CGEN_CPU_OPEN_END)
    {
      switch (arg_type)
	{
	case CGEN_CPU_OPEN_ISAS :
	  isas = va_arg (ap, CGEN_BITSET *);
	  break;
	case CGEN_CPU_OPEN_MACHS :
	  machs = va_arg (ap, unsigned int);
	  break;
	case CGEN_CPU_OPEN_BFDMACH :
	  {
	    const char *name = va_arg (ap, const char *);
	    const CGEN_MACH *mach =
	      lookup_mach_via_bfd_name (mep_cgen_mach_table, name);

	    machs |= 1 << mach->num;
	    break;
	  }
	case CGEN_CPU_OPEN_ENDIAN :
	  endian = va_arg (ap, enum cgen_endian);
	  break;
	default :
	  fprintf (stderr, "mep_cgen_cpu_open: unsupported argument `%d'\n",
		   arg_type);
	  abort (); /* ??? return NULL? */
	}
      arg_type = va_arg (ap, enum cgen_cpu_open_arg);
    }
  va_end (ap);

  /* Mach unspecified means "all".  */
  if (machs == 0)
    machs = (1 << MAX_MACHS) - 1;
  /* Base mach is always selected.  */
  machs |= 1;
  if (endian == CGEN_ENDIAN_UNKNOWN)
    {
      /* ??? If target has only one, could have a default.  */
      fprintf (stderr, "mep_cgen_cpu_open: no endianness specified\n");
      abort ();
    }

  cd->isas = cgen_bitset_copy (isas);
  cd->machs = machs;
  cd->endian = endian;
  /* FIXME: for the sparc case we can determine insn-endianness statically.
     The worry here is where both data and insn endian can be independently
     chosen, in which case this function will need another argument.
     Actually, will want to allow for more arguments in the future anyway.  */
  cd->insn_endian = endian;

  /* Table (re)builder.  */
  cd->rebuild_tables = mep_cgen_rebuild_tables;
  mep_cgen_rebuild_tables (cd);

  /* Default to not allowing signed overflow.  */
  cd->signed_overflow_ok_p = 0;
  
  return (CGEN_CPU_DESC) cd;
}

/* Cover fn to mep_cgen_cpu_open to handle the simple case of 1 isa, 1 mach.
   MACH_NAME is the bfd name of the mach.  */

CGEN_CPU_DESC
mep_cgen_cpu_open_1 (const char *mach_name, enum cgen_endian endian)
{
  return mep_cgen_cpu_open (CGEN_CPU_OPEN_BFDMACH, mach_name,
			       CGEN_CPU_OPEN_ENDIAN, endian,
			       CGEN_CPU_OPEN_END);
}

/* Close a cpu table.
   ??? This can live in a machine independent file, but there's currently
   no place to put this file (there's no libcgen).  libopcodes is the wrong
   place as some simulator ports use this but they don't use libopcodes.  */

void
mep_cgen_cpu_close (CGEN_CPU_DESC cd)
{
  unsigned int i;
  const CGEN_INSN *insns;

  if (cd->macro_insn_table.init_entries)
    {
      insns = cd->macro_insn_table.init_entries;
      for (i = 0; i < cd->macro_insn_table.num_init_entries; ++i, ++insns)
	if (CGEN_INSN_RX ((insns)))
	  regfree (CGEN_INSN_RX (insns));
    }

  if (cd->insn_table.init_entries)
    {
      insns = cd->insn_table.init_entries;
      for (i = 0; i < cd->insn_table.num_init_entries; ++i, ++insns)
	if (CGEN_INSN_RX (insns))
	  regfree (CGEN_INSN_RX (insns));
    }  

  if (cd->macro_insn_table.init_entries)
    free ((CGEN_INSN *) cd->macro_insn_table.init_entries);

  if (cd->insn_table.init_entries)
    free ((CGEN_INSN *) cd->insn_table.init_entries);

  if (cd->hw_table.entries)
    free ((CGEN_HW_ENTRY *) cd->hw_table.entries);

  if (cd->operand_table.entries)
    free ((CGEN_HW_ENTRY *) cd->operand_table.entries);

  free (cd);
}

