/* Instruction opcode table for mep.

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
#include "ansidecl.h"
#include "bfd.h"
#include "symcat.h"
#include "mep-desc.h"
#include "mep-opc.h"
#include "libiberty.h"

/* -- opc.c */
#include "elf/mep.h"

/* A mask for all ISAs executed by the core. */
CGEN_ATTR_VALUE_BITSET_TYPE mep_all_core_isas_mask = {0, 0};

void
init_mep_all_core_isas_mask (void)
{
  if (mep_all_core_isas_mask.length != 0)
    return;
  cgen_bitset_init (& mep_all_core_isas_mask, ISA_MAX);
  cgen_bitset_set (& mep_all_core_isas_mask, ISA_MEP);
  /* begin-all-core-isas */
  cgen_bitset_add (& mep_all_core_isas_mask, ISA_EXT_CORE1);
  cgen_bitset_add (& mep_all_core_isas_mask, ISA_EXT_CORE2);
  /* end-all-core-isas */
}

CGEN_ATTR_VALUE_BITSET_TYPE mep_all_cop_isas_mask = {0, 0};

void
init_mep_all_cop_isas_mask (void)
{
  if (mep_all_cop_isas_mask.length != 0)
    return;
  cgen_bitset_init (& mep_all_cop_isas_mask, ISA_MAX);
  /* begin-all-cop-isas */
  cgen_bitset_add (& mep_all_cop_isas_mask, ISA_EXT_COP2_16);
  cgen_bitset_add (& mep_all_cop_isas_mask, ISA_EXT_COP2_32);
  cgen_bitset_add (& mep_all_cop_isas_mask, ISA_EXT_COP2_48);
  cgen_bitset_add (& mep_all_cop_isas_mask, ISA_EXT_COP2_64);
  /* end-all-cop-isas */
}

int
mep_insn_supported_by_isa (const CGEN_INSN *insn, CGEN_ATTR_VALUE_BITSET_TYPE *isa_mask)
{
  CGEN_BITSET insn_isas = CGEN_INSN_BITSET_ATTR_VALUE (insn, CGEN_INSN_ISA);
  return cgen_bitset_intersect_p (& insn_isas, isa_mask);
}

#define OPTION_MASK \
	( (1 << CGEN_INSN_OPTIONAL_BIT_INSN) \
	| (1 << CGEN_INSN_OPTIONAL_MUL_INSN) \
	| (1 << CGEN_INSN_OPTIONAL_DIV_INSN) \
	| (1 << CGEN_INSN_OPTIONAL_DEBUG_INSN) \
	| (1 << CGEN_INSN_OPTIONAL_LDZ_INSN) \
	| (1 << CGEN_INSN_OPTIONAL_ABS_INSN) \
	| (1 << CGEN_INSN_OPTIONAL_AVE_INSN) \
	| (1 << CGEN_INSN_OPTIONAL_MINMAX_INSN) \
	| (1 << CGEN_INSN_OPTIONAL_CLIP_INSN) \
	| (1 << CGEN_INSN_OPTIONAL_SAT_INSN) \
	| (1 << CGEN_INSN_OPTIONAL_UCI_INSN) \
	| (1 << CGEN_INSN_OPTIONAL_DSP_INSN) \
	| (1 << CGEN_INSN_OPTIONAL_CP_INSN) \
	| (1 << CGEN_INSN_OPTIONAL_CP64_INSN) )


mep_config_map_struct mep_config_map[] =
{
  /* config-map-start */
  /* Default entry: mep core only, all options enabled. */
  { "", 0, EF_MEP_CPU_C2, 1, 0, {1,"\x0"}, {1,"\x0"}, {1,"\x0"}, {1,"\x0"}, {1,"\x0"}, {1,"\x80"}, OPTION_MASK },
  { "simple", CONFIG_SIMPLE, EF_MEP_CPU_C2, 1, 0, { 1, "\x0" }, { 1, "\x0" }, { 1, "\x0" }, { 1, "\x0" }, { 1, "\x0" }, { 1, "\xc0" },
	  0 },
  { "fmax", CONFIG_FMAX, EF_MEP_CPU_C2, 1, 0, { 1, "\x10" }, { 1, "\x8" }, { 1, "\x4" }, { 1, "\x2" }, { 1, "\x1e" }, { 1, "\xa0" },
	  0
	| (1 << CGEN_INSN_OPTIONAL_CP_INSN)
	| (1 << CGEN_INSN_OPTIONAL_MUL_INSN)
	| (1 << CGEN_INSN_OPTIONAL_DIV_INSN)
	| (1 << CGEN_INSN_OPTIONAL_BIT_INSN)
	| (1 << CGEN_INSN_OPTIONAL_LDZ_INSN)
	| (1 << CGEN_INSN_OPTIONAL_ABS_INSN)
	| (1 << CGEN_INSN_OPTIONAL_AVE_INSN)
	| (1 << CGEN_INSN_OPTIONAL_MINMAX_INSN)
	| (1 << CGEN_INSN_OPTIONAL_CLIP_INSN)
	| (1 << CGEN_INSN_OPTIONAL_SAT_INSN) },
  /* config-map-end */
  { 0, 0, 0, 0, 0, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, 0 }
};

int mep_config_index = 0;

static int
check_configured_mach (int machs)
{
  /* All base insns are supported.  */
  int mach = 1 << MACH_BASE;
  switch (MEP_CPU)
    {
    case EF_MEP_CPU_C2:
    case EF_MEP_CPU_C3:
      mach |= (1 << MACH_MEP);
      break;
    case EF_MEP_CPU_H1:
      mach |= (1 << MACH_H1);
      break;
    default:
      break;
    }
  return machs & mach;
}

int
mep_cgen_insn_supported (CGEN_CPU_DESC cd, const CGEN_INSN *insn)
{
  int iconfig = CGEN_INSN_ATTR_VALUE (insn, CGEN_INSN_CONFIG);
  int machs = CGEN_INSN_ATTR_VALUE (insn, CGEN_INSN_MACH);
  CGEN_BITSET isas = CGEN_INSN_BITSET_ATTR_VALUE (insn, CGEN_INSN_ISA);
  int ok1;
  int ok2;
  int ok3;

  /* If the insn has an option bit set that we don't want,
     reject it.  */
  if (CGEN_INSN_ATTRS (insn)->bool & OPTION_MASK & ~MEP_OMASK)
    return 0;

  /* If attributes are absent, assume no restriction. */
  if (machs == 0)
    machs = ~0;

  ok1 = ((machs & cd->machs) && cgen_bitset_intersect_p (& isas, cd->isas));
  /* If the insn is config-specific, make sure it matches.  */
  ok2 =  (iconfig == 0 || iconfig == MEP_CONFIG);
  /* Make sure the insn is supported by the configured mach  */
  ok3 = check_configured_mach (machs);

  return (ok1 && ok2 && ok3);
}
/* The hash functions are recorded here to help keep assembler code out of
   the disassembler and vice versa.  */

static int asm_hash_insn_p        (const CGEN_INSN *);
static unsigned int asm_hash_insn (const char *);
static int dis_hash_insn_p        (const CGEN_INSN *);
static unsigned int dis_hash_insn (const char *, CGEN_INSN_INT);

/* Instruction formats.  */

#if defined (__STDC__) || defined (ALMOST_STDC) || defined (HAVE_STRINGIZE)
#define F(f) & mep_cgen_ifld_table[MEP_##f]
#else
#define F(f) & mep_cgen_ifld_table[MEP_/**/f]
#endif
static const CGEN_IFMT ifmt_empty ATTRIBUTE_UNUSED = {
  0, 0, 0x0, { { 0 } }
};

static const CGEN_IFMT ifmt_sb ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_sh ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_sw ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_lbu ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_lhu ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_sw_sp ATTRIBUTE_UNUSED = {
  16, 16, 0xf083, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_8) }, { F (F_7U9A4) }, { F (F_SUB2) }, { 0 } }
};

static const CGEN_IFMT ifmt_sb_tp ATTRIBUTE_UNUSED = {
  16, 16, 0xf880, { { F (F_MAJOR) }, { F (F_4) }, { F (F_RN3) }, { F (F_8) }, { F (F_7U9) }, { 0 } }
};

static const CGEN_IFMT ifmt_sh_tp ATTRIBUTE_UNUSED = {
  16, 16, 0xf881, { { F (F_MAJOR) }, { F (F_4) }, { F (F_RN3) }, { F (F_8) }, { F (F_7U9A2) }, { F (F_15) }, { 0 } }
};

static const CGEN_IFMT ifmt_sw_tp ATTRIBUTE_UNUSED = {
  16, 16, 0xf883, { { F (F_MAJOR) }, { F (F_4) }, { F (F_RN3) }, { F (F_8) }, { F (F_7U9A4) }, { F (F_SUB2) }, { 0 } }
};

static const CGEN_IFMT ifmt_lbu_tp ATTRIBUTE_UNUSED = {
  16, 16, 0xf880, { { F (F_MAJOR) }, { F (F_4) }, { F (F_RN3) }, { F (F_8) }, { F (F_7U9) }, { 0 } }
};

static const CGEN_IFMT ifmt_lhu_tp ATTRIBUTE_UNUSED = {
  16, 16, 0xf881, { { F (F_MAJOR) }, { F (F_4) }, { F (F_RN3) }, { F (F_8) }, { F (F_7U9A2) }, { F (F_15) }, { 0 } }
};

static const CGEN_IFMT ifmt_sb16 ATTRIBUTE_UNUSED = {
  32, 32, 0xf00f0000, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_16S16) }, { 0 } }
};

static const CGEN_IFMT ifmt_sh16 ATTRIBUTE_UNUSED = {
  32, 32, 0xf00f0000, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_16S16) }, { 0 } }
};

static const CGEN_IFMT ifmt_sw16 ATTRIBUTE_UNUSED = {
  32, 32, 0xf00f0000, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_16S16) }, { 0 } }
};

static const CGEN_IFMT ifmt_lbu16 ATTRIBUTE_UNUSED = {
  32, 32, 0xf00f0000, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_16S16) }, { 0 } }
};

static const CGEN_IFMT ifmt_lhu16 ATTRIBUTE_UNUSED = {
  32, 32, 0xf00f0000, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_16S16) }, { 0 } }
};

static const CGEN_IFMT ifmt_sw24 ATTRIBUTE_UNUSED = {
  32, 32, 0xf0030000, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_24U8A4N) }, { F (F_SUB2) }, { 0 } }
};

static const CGEN_IFMT ifmt_extb ATTRIBUTE_UNUSED = {
  16, 16, 0xf0ff, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_ssarb ATTRIBUTE_UNUSED = {
  16, 16, 0xfc0f, { { F (F_MAJOR) }, { F (F_4) }, { F (F_5) }, { F (F_2U6) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_mov ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_movi8 ATTRIBUTE_UNUSED = {
  16, 16, 0xf000, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_8S8) }, { 0 } }
};

static const CGEN_IFMT ifmt_movi16 ATTRIBUTE_UNUSED = {
  32, 32, 0xf0ff0000, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_16S16) }, { 0 } }
};

static const CGEN_IFMT ifmt_movu24 ATTRIBUTE_UNUSED = {
  32, 32, 0xf8000000, { { F (F_MAJOR) }, { F (F_4) }, { F (F_RN3) }, { F (F_24U8N) }, { 0 } }
};

static const CGEN_IFMT ifmt_movu16 ATTRIBUTE_UNUSED = {
  32, 32, 0xf0ff0000, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_16U16) }, { 0 } }
};

static const CGEN_IFMT ifmt_add3 ATTRIBUTE_UNUSED = {
  16, 16, 0xf000, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_RL) }, { 0 } }
};

static const CGEN_IFMT ifmt_add ATTRIBUTE_UNUSED = {
  16, 16, 0xf003, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_6S8) }, { F (F_SUB2) }, { 0 } }
};

static const CGEN_IFMT ifmt_add3i ATTRIBUTE_UNUSED = {
  16, 16, 0xf083, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_8) }, { F (F_7U9A4) }, { F (F_SUB2) }, { 0 } }
};

static const CGEN_IFMT ifmt_slt3i ATTRIBUTE_UNUSED = {
  16, 16, 0xf007, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_5U8) }, { F (F_SUB3) }, { 0 } }
};

static const CGEN_IFMT ifmt_add3x ATTRIBUTE_UNUSED = {
  32, 32, 0xf00f0000, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_16S16) }, { 0 } }
};

static const CGEN_IFMT ifmt_sltu3x ATTRIBUTE_UNUSED = {
  32, 32, 0xf00f0000, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_16U16) }, { 0 } }
};

static const CGEN_IFMT ifmt_bra ATTRIBUTE_UNUSED = {
  16, 16, 0xf001, { { F (F_MAJOR) }, { F (F_12S4A2) }, { F (F_15) }, { 0 } }
};

static const CGEN_IFMT ifmt_beqz ATTRIBUTE_UNUSED = {
  16, 16, 0xf001, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_8S8A2) }, { F (F_15) }, { 0 } }
};

static const CGEN_IFMT ifmt_beqi ATTRIBUTE_UNUSED = {
  32, 32, 0xf00f0000, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_4U8) }, { F (F_SUB4) }, { F (F_17S16A2) }, { 0 } }
};

static const CGEN_IFMT ifmt_beq ATTRIBUTE_UNUSED = {
  32, 32, 0xf00f0000, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_17S16A2) }, { 0 } }
};

static const CGEN_IFMT ifmt_bsr24 ATTRIBUTE_UNUSED = {
  32, 32, 0xf80f0000, { { F (F_MAJOR) }, { F (F_4) }, { F (F_24S5A2N) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_jmp ATTRIBUTE_UNUSED = {
  16, 16, 0xff0f, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_jmp24 ATTRIBUTE_UNUSED = {
  32, 32, 0xf80f0000, { { F (F_MAJOR) }, { F (F_4) }, { F (F_24U5A2N) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_ret ATTRIBUTE_UNUSED = {
  16, 16, 0xffff, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_repeat ATTRIBUTE_UNUSED = {
  32, 32, 0xf0ff0000, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_17S16A2) }, { 0 } }
};

static const CGEN_IFMT ifmt_erepeat ATTRIBUTE_UNUSED = {
  32, 32, 0xffff0000, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_17S16A2) }, { 0 } }
};

static const CGEN_IFMT ifmt_stc_lp ATTRIBUTE_UNUSED = {
  16, 16, 0xf0ff, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_CSRN_LO) }, { F (F_12) }, { F (F_13) }, { F (F_14) }, { F (F_CSRN_HI) }, { 0 } }
};

static const CGEN_IFMT ifmt_stc ATTRIBUTE_UNUSED = {
  16, 16, 0xf00e, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_CSRN) }, { F (F_12) }, { F (F_13) }, { F (F_14) }, { 0 } }
};

static const CGEN_IFMT ifmt_swi ATTRIBUTE_UNUSED = {
  16, 16, 0xffcf, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_8) }, { F (F_9) }, { F (F_2U10) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_bsetm ATTRIBUTE_UNUSED = {
  16, 16, 0xf80f, { { F (F_MAJOR) }, { F (F_4) }, { F (F_3U5) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_tas ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_cache ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_madd ATTRIBUTE_UNUSED = {
  32, 32, 0xf00fffff, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_16U16) }, { 0 } }
};

static const CGEN_IFMT ifmt_clip ATTRIBUTE_UNUSED = {
  32, 32, 0xf0ffff07, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_EXT) }, { F (F_5U24) }, { F (F_29) }, { F (F_30) }, { F (F_31) }, { 0 } }
};

static const CGEN_IFMT ifmt_swcp ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_CRN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_smcp ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_CRN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_swcp16 ATTRIBUTE_UNUSED = {
  32, 32, 0xf00f0000, { { F (F_MAJOR) }, { F (F_CRN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_16S16) }, { 0 } }
};

static const CGEN_IFMT ifmt_smcp16 ATTRIBUTE_UNUSED = {
  32, 32, 0xf00f0000, { { F (F_MAJOR) }, { F (F_CRN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_16S16) }, { 0 } }
};

static const CGEN_IFMT ifmt_sbcpa ATTRIBUTE_UNUSED = {
  32, 32, 0xf00fff00, { { F (F_MAJOR) }, { F (F_CRN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_EXT) }, { F (F_8S24) }, { 0 } }
};

static const CGEN_IFMT ifmt_shcpa ATTRIBUTE_UNUSED = {
  32, 32, 0xf00fff01, { { F (F_MAJOR) }, { F (F_CRN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_EXT) }, { F (F_8S24A2) }, { F (F_31) }, { 0 } }
};

static const CGEN_IFMT ifmt_swcpa ATTRIBUTE_UNUSED = {
  32, 32, 0xf00fff03, { { F (F_MAJOR) }, { F (F_CRN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_EXT) }, { F (F_8S24A4) }, { F (F_30) }, { F (F_31) }, { 0 } }
};

static const CGEN_IFMT ifmt_smcpa ATTRIBUTE_UNUSED = {
  32, 32, 0xf00fff07, { { F (F_MAJOR) }, { F (F_CRN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_EXT) }, { F (F_8S24A8) }, { F (F_29) }, { F (F_30) }, { F (F_31) }, { 0 } }
};

static const CGEN_IFMT ifmt_bcpeq ATTRIBUTE_UNUSED = {
  32, 32, 0xff0f0000, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { F (F_17S16A2) }, { 0 } }
};

static const CGEN_IFMT ifmt_sim_syscall ATTRIBUTE_UNUSED = {
  16, 16, 0xf8ef, { { F (F_MAJOR) }, { F (F_4) }, { F (F_CALLNUM) }, { F (F_8) }, { F (F_9) }, { F (F_10) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_fadds ATTRIBUTE_UNUSED = {
  32, 32, 0xf0fff001, { { F (F_FMAX_0_4) }, { F (F_FMAX_FRD) }, { F (F_FMAX_8_4) }, { F (F_FMAX_12_4) }, { F (F_FMAX_16_4) }, { F (F_FMAX_FRN) }, { F (F_FMAX_FRM) }, { F (F_FMAX_31_1) }, { 0 } }
};

static const CGEN_IFMT ifmt_fsqrts ATTRIBUTE_UNUSED = {
  32, 32, 0xf0fff0f3, { { F (F_FMAX_0_4) }, { F (F_FMAX_FRD) }, { F (F_FMAX_8_4) }, { F (F_FMAX_12_4) }, { F (F_FMAX_16_4) }, { F (F_FMAX_FRN) }, { F (F_FMAX_24_4) }, { F (F_FMAX_30_1) }, { F (F_FMAX_31_1) }, { 0 } }
};

static const CGEN_IFMT ifmt_froundws ATTRIBUTE_UNUSED = {
  32, 32, 0xf0fff0f3, { { F (F_FMAX_0_4) }, { F (F_FMAX_FRD) }, { F (F_FMAX_8_4) }, { F (F_FMAX_12_4) }, { F (F_FMAX_16_4) }, { F (F_FMAX_FRN) }, { F (F_FMAX_24_4) }, { F (F_FMAX_30_1) }, { F (F_FMAX_31_1) }, { 0 } }
};

static const CGEN_IFMT ifmt_fcvtsw ATTRIBUTE_UNUSED = {
  32, 32, 0xf0fff0f3, { { F (F_FMAX_0_4) }, { F (F_FMAX_FRD) }, { F (F_FMAX_8_4) }, { F (F_FMAX_12_4) }, { F (F_FMAX_16_4) }, { F (F_FMAX_FRN) }, { F (F_FMAX_24_4) }, { F (F_FMAX_30_1) }, { F (F_FMAX_31_1) }, { 0 } }
};

static const CGEN_IFMT ifmt_fcmpfs ATTRIBUTE_UNUSED = {
  32, 32, 0xfffff009, { { F (F_FMAX_0_4) }, { F (F_FMAX_4_4) }, { F (F_FMAX_8_4) }, { F (F_FMAX_12_4) }, { F (F_FMAX_16_4) }, { F (F_FMAX_FRN) }, { F (F_FMAX_FRM) }, { F (F_FMAX_28_1) }, { F (F_FMAX_31_1) }, { 0 } }
};

static const CGEN_IFMT ifmt_cmov_frn_rm ATTRIBUTE_UNUSED = {
  32, 32, 0xf00ffff7, { { F (F_FMAX_0_4) }, { F (F_FMAX_FRD) }, { F (F_FMAX_RM) }, { F (F_FMAX_12_4) }, { F (F_FMAX_16_4) }, { F (F_FMAX_20_4) }, { F (F_FMAX_24_4) }, { F (F_FMAX_29_1) }, { F (F_FMAX_30_1) }, { F (F_FMAX_31_1) }, { 0 } }
};

static const CGEN_IFMT ifmt_cmovc_ccrn_rm ATTRIBUTE_UNUSED = {
  32, 32, 0xf00fffff, { { F (F_FMAX_0_4) }, { F (F_FMAX_4_4) }, { F (F_FMAX_RM) }, { F (F_FMAX_12_4) }, { F (F_FMAX_16_4) }, { F (F_FMAX_20_4) }, { F (F_FMAX_24_4) }, { F (F_FMAX_28_1) }, { F (F_FMAX_29_1) }, { F (F_FMAX_30_1) }, { F (F_FMAX_31_1) }, { 0 } }
};

#undef F

#if defined (__STDC__) || defined (ALMOST_STDC) || defined (HAVE_STRINGIZE)
#define A(a) (1 << CGEN_INSN_##a)
#else
#define A(a) (1 << CGEN_INSN_/**/a)
#endif
#if defined (__STDC__) || defined (ALMOST_STDC) || defined (HAVE_STRINGIZE)
#define OPERAND(op) MEP_OPERAND_##op
#else
#define OPERAND(op) MEP_OPERAND_/**/op
#endif
#define MNEM CGEN_SYNTAX_MNEMONIC /* syntax value for mnemonic */
#define OP(field) CGEN_SYNTAX_MAKE_FIELD (OPERAND (field))

/* The instruction table.  */

static const CGEN_OPCODE mep_cgen_insn_opcode_table[MAX_INSNS] =
{
  /* Special null first entry.
     A `num' value of zero is thus invalid.
     Also, the special `invalid' insn resides here.  */
  { { 0, 0, 0, 0 }, {{0}}, 0, {0}},
/* sb $rnc,($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNC), ',', '(', OP (RMA), ')', 0 } },
    & ifmt_sb, { 0x8 }
  },
/* sh $rns,($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNS), ',', '(', OP (RMA), ')', 0 } },
    & ifmt_sh, { 0x9 }
  },
/* sw $rnl,($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNL), ',', '(', OP (RMA), ')', 0 } },
    & ifmt_sw, { 0xa }
  },
/* lb $rnc,($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNC), ',', '(', OP (RMA), ')', 0 } },
    & ifmt_sb, { 0xc }
  },
/* lh $rns,($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNS), ',', '(', OP (RMA), ')', 0 } },
    & ifmt_sh, { 0xd }
  },
/* lw $rnl,($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNL), ',', '(', OP (RMA), ')', 0 } },
    & ifmt_sw, { 0xe }
  },
/* lbu $rnuc,($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNUC), ',', '(', OP (RMA), ')', 0 } },
    & ifmt_lbu, { 0xb }
  },
/* lhu $rnus,($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNUS), ',', '(', OP (RMA), ')', 0 } },
    & ifmt_lhu, { 0xf }
  },
/* sw $rnl,$udisp7a4($spr) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNL), ',', OP (UDISP7A4), '(', OP (SPR), ')', 0 } },
    & ifmt_sw_sp, { 0x4002 }
  },
/* lw $rnl,$udisp7a4($spr) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNL), ',', OP (UDISP7A4), '(', OP (SPR), ')', 0 } },
    & ifmt_sw_sp, { 0x4003 }
  },
/* sb $rn3c,$udisp7($tpr) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN3C), ',', OP (UDISP7), '(', OP (TPR), ')', 0 } },
    & ifmt_sb_tp, { 0x8000 }
  },
/* sh $rn3s,$udisp7a2($tpr) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN3S), ',', OP (UDISP7A2), '(', OP (TPR), ')', 0 } },
    & ifmt_sh_tp, { 0x8080 }
  },
/* sw $rn3l,$udisp7a4($tpr) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN3L), ',', OP (UDISP7A4), '(', OP (TPR), ')', 0 } },
    & ifmt_sw_tp, { 0x4082 }
  },
/* lb $rn3c,$udisp7($tpr) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN3C), ',', OP (UDISP7), '(', OP (TPR), ')', 0 } },
    & ifmt_sb_tp, { 0x8800 }
  },
/* lh $rn3s,$udisp7a2($tpr) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN3S), ',', OP (UDISP7A2), '(', OP (TPR), ')', 0 } },
    & ifmt_sh_tp, { 0x8880 }
  },
/* lw $rn3l,$udisp7a4($tpr) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN3L), ',', OP (UDISP7A4), '(', OP (TPR), ')', 0 } },
    & ifmt_sw_tp, { 0x4083 }
  },
/* lbu $rn3uc,$udisp7($tpr) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN3UC), ',', OP (UDISP7), '(', OP (TPR), ')', 0 } },
    & ifmt_lbu_tp, { 0x4880 }
  },
/* lhu $rn3us,$udisp7a2($tpr) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN3US), ',', OP (UDISP7A2), '(', OP (TPR), ')', 0 } },
    & ifmt_lhu_tp, { 0x8881 }
  },
/* sb $rnc,$sdisp16($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNC), ',', OP (SDISP16), '(', OP (RMA), ')', 0 } },
    & ifmt_sb16, { 0xc0080000 }
  },
/* sh $rns,$sdisp16($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNS), ',', OP (SDISP16), '(', OP (RMA), ')', 0 } },
    & ifmt_sh16, { 0xc0090000 }
  },
/* sw $rnl,$sdisp16($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNL), ',', OP (SDISP16), '(', OP (RMA), ')', 0 } },
    & ifmt_sw16, { 0xc00a0000 }
  },
/* lb $rnc,$sdisp16($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNC), ',', OP (SDISP16), '(', OP (RMA), ')', 0 } },
    & ifmt_sb16, { 0xc00c0000 }
  },
/* lh $rns,$sdisp16($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNS), ',', OP (SDISP16), '(', OP (RMA), ')', 0 } },
    & ifmt_sh16, { 0xc00d0000 }
  },
/* lw $rnl,$sdisp16($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNL), ',', OP (SDISP16), '(', OP (RMA), ')', 0 } },
    & ifmt_sw16, { 0xc00e0000 }
  },
/* lbu $rnuc,$sdisp16($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNUC), ',', OP (SDISP16), '(', OP (RMA), ')', 0 } },
    & ifmt_lbu16, { 0xc00b0000 }
  },
/* lhu $rnus,$sdisp16($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNUS), ',', OP (SDISP16), '(', OP (RMA), ')', 0 } },
    & ifmt_lhu16, { 0xc00f0000 }
  },
/* sw $rnl,($addr24a4) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNL), ',', '(', OP (ADDR24A4), ')', 0 } },
    & ifmt_sw24, { 0xe0020000 }
  },
/* lw $rnl,($addr24a4) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNL), ',', '(', OP (ADDR24A4), ')', 0 } },
    & ifmt_sw24, { 0xe0030000 }
  },
/* extb $rn */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), 0 } },
    & ifmt_extb, { 0x100d }
  },
/* exth $rn */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), 0 } },
    & ifmt_extb, { 0x102d }
  },
/* extub $rn */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), 0 } },
    & ifmt_extb, { 0x108d }
  },
/* extuh $rn */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), 0 } },
    & ifmt_extb, { 0x10ad }
  },
/* ssarb $udisp2($rm) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (UDISP2), '(', OP (RM), ')', 0 } },
    & ifmt_ssarb, { 0x100c }
  },
/* mov $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x0 }
  },
/* mov $rn,$simm8 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (SIMM8), 0 } },
    & ifmt_movi8, { 0x5000 }
  },
/* mov $rn,$simm16 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (SIMM16), 0 } },
    & ifmt_movi16, { 0xc0010000 }
  },
/* movu $rn3,$uimm24 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN3), ',', OP (UIMM24), 0 } },
    & ifmt_movu24, { 0xd0000000 }
  },
/* movu $rn,$uimm16 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (UIMM16), 0 } },
    & ifmt_movu16, { 0xc0110000 }
  },
/* movh $rn,$uimm16 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (UIMM16), 0 } },
    & ifmt_movu16, { 0xc0210000 }
  },
/* add3 $rl,$rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RL), ',', OP (RN), ',', OP (RM), 0 } },
    & ifmt_add3, { 0x9000 }
  },
/* add $rn,$simm6 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (SIMM6), 0 } },
    & ifmt_add, { 0x6000 }
  },
/* add3 $rn,$spr,$uimm7a4 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (SPR), ',', OP (UIMM7A4), 0 } },
    & ifmt_add3i, { 0x4000 }
  },
/* advck3 \$0,$rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', '$', '0', ',', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x7 }
  },
/* sub $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x4 }
  },
/* sbvck3 \$0,$rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', '$', '0', ',', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x5 }
  },
/* neg $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x1 }
  },
/* slt3 \$0,$rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', '$', '0', ',', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x2 }
  },
/* sltu3 \$0,$rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', '$', '0', ',', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x3 }
  },
/* slt3 \$0,$rn,$uimm5 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', '$', '0', ',', OP (RN), ',', OP (UIMM5), 0 } },
    & ifmt_slt3i, { 0x6001 }
  },
/* sltu3 \$0,$rn,$uimm5 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', '$', '0', ',', OP (RN), ',', OP (UIMM5), 0 } },
    & ifmt_slt3i, { 0x6005 }
  },
/* sl1ad3 \$0,$rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', '$', '0', ',', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x2006 }
  },
/* sl2ad3 \$0,$rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', '$', '0', ',', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x2007 }
  },
/* add3 $rn,$rm,$simm16 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), ',', OP (SIMM16), 0 } },
    & ifmt_add3x, { 0xc0000000 }
  },
/* slt3 $rn,$rm,$simm16 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), ',', OP (SIMM16), 0 } },
    & ifmt_add3x, { 0xc0020000 }
  },
/* sltu3 $rn,$rm,$uimm16 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), ',', OP (UIMM16), 0 } },
    & ifmt_sltu3x, { 0xc0030000 }
  },
/* or $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x1000 }
  },
/* and $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x1001 }
  },
/* xor $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x1002 }
  },
/* nor $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x1003 }
  },
/* or3 $rn,$rm,$uimm16 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), ',', OP (UIMM16), 0 } },
    & ifmt_sltu3x, { 0xc0040000 }
  },
/* and3 $rn,$rm,$uimm16 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), ',', OP (UIMM16), 0 } },
    & ifmt_sltu3x, { 0xc0050000 }
  },
/* xor3 $rn,$rm,$uimm16 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), ',', OP (UIMM16), 0 } },
    & ifmt_sltu3x, { 0xc0060000 }
  },
/* sra $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x200d }
  },
/* srl $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x200c }
  },
/* sll $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x200e }
  },
/* sra $rn,$uimm5 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (UIMM5), 0 } },
    & ifmt_slt3i, { 0x6003 }
  },
/* srl $rn,$uimm5 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (UIMM5), 0 } },
    & ifmt_slt3i, { 0x6002 }
  },
/* sll $rn,$uimm5 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (UIMM5), 0 } },
    & ifmt_slt3i, { 0x6006 }
  },
/* sll3 \$0,$rn,$uimm5 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', '$', '0', ',', OP (RN), ',', OP (UIMM5), 0 } },
    & ifmt_slt3i, { 0x6007 }
  },
/* fsft $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x200f }
  },
/* bra $pcrel12a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (PCREL12A2), 0 } },
    & ifmt_bra, { 0xb000 }
  },
/* beqz $rn,$pcrel8a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (PCREL8A2), 0 } },
    & ifmt_beqz, { 0xa000 }
  },
/* bnez $rn,$pcrel8a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (PCREL8A2), 0 } },
    & ifmt_beqz, { 0xa001 }
  },
/* beqi $rn,$uimm4,$pcrel17a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (UIMM4), ',', OP (PCREL17A2), 0 } },
    & ifmt_beqi, { 0xe0000000 }
  },
/* bnei $rn,$uimm4,$pcrel17a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (UIMM4), ',', OP (PCREL17A2), 0 } },
    & ifmt_beqi, { 0xe0040000 }
  },
/* blti $rn,$uimm4,$pcrel17a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (UIMM4), ',', OP (PCREL17A2), 0 } },
    & ifmt_beqi, { 0xe00c0000 }
  },
/* bgei $rn,$uimm4,$pcrel17a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (UIMM4), ',', OP (PCREL17A2), 0 } },
    & ifmt_beqi, { 0xe0080000 }
  },
/* beq $rn,$rm,$pcrel17a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), ',', OP (PCREL17A2), 0 } },
    & ifmt_beq, { 0xe0010000 }
  },
/* bne $rn,$rm,$pcrel17a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), ',', OP (PCREL17A2), 0 } },
    & ifmt_beq, { 0xe0050000 }
  },
/* bsr $pcrel12a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (PCREL12A2), 0 } },
    & ifmt_bra, { 0xb001 }
  },
/* bsr $pcrel24a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (PCREL24A2), 0 } },
    & ifmt_bsr24, { 0xd8090000 }
  },
/* jmp $rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RM), 0 } },
    & ifmt_jmp, { 0x100e }
  },
/* jmp $pcabs24a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (PCABS24A2), 0 } },
    & ifmt_jmp24, { 0xd8080000 }
  },
/* jsr $rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RM), 0 } },
    & ifmt_jmp, { 0x100f }
  },
/* ret */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_ret, { 0x7002 }
  },
/* repeat $rn,$pcrel17a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (PCREL17A2), 0 } },
    & ifmt_repeat, { 0xe0090000 }
  },
/* erepeat $pcrel17a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (PCREL17A2), 0 } },
    & ifmt_erepeat, { 0xe0190000 }
  },
/* stc $rn,\$lp */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', '$', 'l', 'p', 0 } },
    & ifmt_stc_lp, { 0x7018 }
  },
/* stc $rn,\$hi */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', '$', 'h', 'i', 0 } },
    & ifmt_stc_lp, { 0x7078 }
  },
/* stc $rn,\$lo */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', '$', 'l', 'o', 0 } },
    & ifmt_stc_lp, { 0x7088 }
  },
/* stc $rn,$csrn */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (CSRN), 0 } },
    & ifmt_stc, { 0x7008 }
  },
/* ldc $rn,\$lp */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', '$', 'l', 'p', 0 } },
    & ifmt_stc_lp, { 0x701a }
  },
/* ldc $rn,\$hi */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', '$', 'h', 'i', 0 } },
    & ifmt_stc_lp, { 0x707a }
  },
/* ldc $rn,\$lo */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', '$', 'l', 'o', 0 } },
    & ifmt_stc_lp, { 0x708a }
  },
/* ldc $rn,$csrn */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (CSRN), 0 } },
    & ifmt_stc, { 0x700a }
  },
/* di */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_ret, { 0x7000 }
  },
/* ei */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_ret, { 0x7010 }
  },
/* reti */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_ret, { 0x7012 }
  },
/* halt */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_ret, { 0x7022 }
  },
/* sleep */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_ret, { 0x7062 }
  },
/* swi $uimm2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (UIMM2), 0 } },
    & ifmt_swi, { 0x7006 }
  },
/* break */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_ret, { 0x7032 }
  },
/* syncm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_ret, { 0x7011 }
  },
/* stcb $rn,$uimm16 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (UIMM16), 0 } },
    & ifmt_movu16, { 0xf0040000 }
  },
/* ldcb $rn,$uimm16 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (UIMM16), 0 } },
    & ifmt_movu16, { 0xf0140000 }
  },
/* bsetm ($rma),$uimm3 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', '(', OP (RMA), ')', ',', OP (UIMM3), 0 } },
    & ifmt_bsetm, { 0x2000 }
  },
/* bclrm ($rma),$uimm3 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', '(', OP (RMA), ')', ',', OP (UIMM3), 0 } },
    & ifmt_bsetm, { 0x2001 }
  },
/* bnotm ($rma),$uimm3 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', '(', OP (RMA), ')', ',', OP (UIMM3), 0 } },
    & ifmt_bsetm, { 0x2002 }
  },
/* btstm \$0,($rma),$uimm3 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', '$', '0', ',', '(', OP (RMA), ')', ',', OP (UIMM3), 0 } },
    & ifmt_bsetm, { 0x2003 }
  },
/* tas $rn,($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', '(', OP (RMA), ')', 0 } },
    & ifmt_tas, { 0x2004 }
  },
/* cache $cimm4,($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CIMM4), ',', '(', OP (RMA), ')', 0 } },
    & ifmt_cache, { 0x7004 }
  },
/* mul $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x1004 }
  },
/* mulu $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x1005 }
  },
/* mulr $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x1006 }
  },
/* mulru $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x1007 }
  },
/* madd $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_madd, { 0xf0013004 }
  },
/* maddu $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_madd, { 0xf0013005 }
  },
/* maddr $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_madd, { 0xf0013006 }
  },
/* maddru $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_madd, { 0xf0013007 }
  },
/* div $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x1008 }
  },
/* divu $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_mov, { 0x1009 }
  },
/* dret */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_ret, { 0x7013 }
  },
/* dbreak */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_ret, { 0x7033 }
  },
/* ldz $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_madd, { 0xf0010000 }
  },
/* abs $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_madd, { 0xf0010003 }
  },
/* ave $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_madd, { 0xf0010002 }
  },
/* min $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_madd, { 0xf0010004 }
  },
/* max $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_madd, { 0xf0010005 }
  },
/* minu $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_madd, { 0xf0010006 }
  },
/* maxu $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_madd, { 0xf0010007 }
  },
/* clip $rn,$cimm5 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (CIMM5), 0 } },
    & ifmt_clip, { 0xf0011000 }
  },
/* clipu $rn,$cimm5 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (CIMM5), 0 } },
    & ifmt_clip, { 0xf0011001 }
  },
/* sadd $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_madd, { 0xf0010008 }
  },
/* ssub $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_madd, { 0xf001000a }
  },
/* saddu $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_madd, { 0xf0010009 }
  },
/* ssubu $rn,$rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RN), ',', OP (RM), 0 } },
    & ifmt_madd, { 0xf001000b }
  },
/* swcp $crn,($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), ')', 0 } },
    & ifmt_swcp, { 0x3008 }
  },
/* lwcp $crn,($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), ')', 0 } },
    & ifmt_swcp, { 0x3009 }
  },
/* smcp $crn64,($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN64), ',', '(', OP (RMA), ')', 0 } },
    & ifmt_smcp, { 0x300a }
  },
/* lmcp $crn64,($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN64), ',', '(', OP (RMA), ')', 0 } },
    & ifmt_smcp, { 0x300b }
  },
/* swcpi $crn,($rma+) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), '+', ')', 0 } },
    & ifmt_swcp, { 0x3000 }
  },
/* lwcpi $crn,($rma+) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), '+', ')', 0 } },
    & ifmt_swcp, { 0x3001 }
  },
/* smcpi $crn64,($rma+) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN64), ',', '(', OP (RMA), '+', ')', 0 } },
    & ifmt_smcp, { 0x3002 }
  },
/* lmcpi $crn64,($rma+) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN64), ',', '(', OP (RMA), '+', ')', 0 } },
    & ifmt_smcp, { 0x3003 }
  },
/* swcp $crn,$sdisp16($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', OP (SDISP16), '(', OP (RMA), ')', 0 } },
    & ifmt_swcp16, { 0xf00c0000 }
  },
/* lwcp $crn,$sdisp16($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', OP (SDISP16), '(', OP (RMA), ')', 0 } },
    & ifmt_swcp16, { 0xf00d0000 }
  },
/* smcp $crn64,$sdisp16($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN64), ',', OP (SDISP16), '(', OP (RMA), ')', 0 } },
    & ifmt_smcp16, { 0xf00e0000 }
  },
/* lmcp $crn64,$sdisp16($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN64), ',', OP (SDISP16), '(', OP (RMA), ')', 0 } },
    & ifmt_smcp16, { 0xf00f0000 }
  },
/* sbcpa $crn,($rma+),$cdisp8 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8), 0 } },
    & ifmt_sbcpa, { 0xf0050000 }
  },
/* lbcpa $crn,($rma+),$cdisp8 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8), 0 } },
    & ifmt_sbcpa, { 0xf0054000 }
  },
/* shcpa $crn,($rma+),$cdisp8a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8A2), 0 } },
    & ifmt_shcpa, { 0xf0051000 }
  },
/* lhcpa $crn,($rma+),$cdisp8a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8A2), 0 } },
    & ifmt_shcpa, { 0xf0055000 }
  },
/* swcpa $crn,($rma+),$cdisp8a4 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8A4), 0 } },
    & ifmt_swcpa, { 0xf0052000 }
  },
/* lwcpa $crn,($rma+),$cdisp8a4 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8A4), 0 } },
    & ifmt_swcpa, { 0xf0056000 }
  },
/* smcpa $crn64,($rma+),$cdisp8a8 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN64), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8A8), 0 } },
    & ifmt_smcpa, { 0xf0053000 }
  },
/* lmcpa $crn64,($rma+),$cdisp8a8 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN64), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8A8), 0 } },
    & ifmt_smcpa, { 0xf0057000 }
  },
/* sbcpm0 $crn,($rma+),$cdisp8 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8), 0 } },
    & ifmt_sbcpa, { 0xf0050800 }
  },
/* lbcpm0 $crn,($rma+),$cdisp8 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8), 0 } },
    & ifmt_sbcpa, { 0xf0054800 }
  },
/* shcpm0 $crn,($rma+),$cdisp8a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8A2), 0 } },
    & ifmt_shcpa, { 0xf0051800 }
  },
/* lhcpm0 $crn,($rma+),$cdisp8a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8A2), 0 } },
    & ifmt_shcpa, { 0xf0055800 }
  },
/* swcpm0 $crn,($rma+),$cdisp8a4 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8A4), 0 } },
    & ifmt_swcpa, { 0xf0052800 }
  },
/* lwcpm0 $crn,($rma+),$cdisp8a4 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8A4), 0 } },
    & ifmt_swcpa, { 0xf0056800 }
  },
/* smcpm0 $crn64,($rma+),$cdisp8a8 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN64), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8A8), 0 } },
    & ifmt_smcpa, { 0xf0053800 }
  },
/* lmcpm0 $crn64,($rma+),$cdisp8a8 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN64), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8A8), 0 } },
    & ifmt_smcpa, { 0xf0057800 }
  },
/* sbcpm1 $crn,($rma+),$cdisp8 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8), 0 } },
    & ifmt_sbcpa, { 0xf0050c00 }
  },
/* lbcpm1 $crn,($rma+),$cdisp8 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8), 0 } },
    & ifmt_sbcpa, { 0xf0054c00 }
  },
/* shcpm1 $crn,($rma+),$cdisp8a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8A2), 0 } },
    & ifmt_shcpa, { 0xf0051c00 }
  },
/* lhcpm1 $crn,($rma+),$cdisp8a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8A2), 0 } },
    & ifmt_shcpa, { 0xf0055c00 }
  },
/* swcpm1 $crn,($rma+),$cdisp8a4 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8A4), 0 } },
    & ifmt_swcpa, { 0xf0052c00 }
  },
/* lwcpm1 $crn,($rma+),$cdisp8a4 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8A4), 0 } },
    & ifmt_swcpa, { 0xf0056c00 }
  },
/* smcpm1 $crn64,($rma+),$cdisp8a8 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN64), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8A8), 0 } },
    & ifmt_smcpa, { 0xf0053c00 }
  },
/* lmcpm1 $crn64,($rma+),$cdisp8a8 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN64), ',', '(', OP (RMA), '+', ')', ',', OP (CDISP8A8), 0 } },
    & ifmt_smcpa, { 0xf0057c00 }
  },
/* bcpeq $cccc,$pcrel17a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CCCC), ',', OP (PCREL17A2), 0 } },
    & ifmt_bcpeq, { 0xd8040000 }
  },
/* bcpne $cccc,$pcrel17a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CCCC), ',', OP (PCREL17A2), 0 } },
    & ifmt_bcpeq, { 0xd8050000 }
  },
/* bcpat $cccc,$pcrel17a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CCCC), ',', OP (PCREL17A2), 0 } },
    & ifmt_bcpeq, { 0xd8060000 }
  },
/* bcpaf $cccc,$pcrel17a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CCCC), ',', OP (PCREL17A2), 0 } },
    & ifmt_bcpeq, { 0xd8070000 }
  },
/* synccp */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_ret, { 0x7021 }
  },
/* jsrv $rm */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RM), 0 } },
    & ifmt_jmp, { 0x180f }
  },
/* bsrv $pcrel24a2 */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (PCREL24A2), 0 } },
    & ifmt_bsr24, { 0xd80b0000 }
  },
/* --unused-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_sim_syscall, { 0x7800 }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x6 }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x100a }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x100b }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x2005 }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x2008 }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x2009 }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x200a }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x200b }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x3004 }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x3005 }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x3006 }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x3007 }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x300c }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x300d }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x300e }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x300f }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x7007 }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x700e }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x700f }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0xc007 }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0xe00d }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0xf003 }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0xf006 }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0xf008 }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x7005 }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x700c }
  },
/* --reserved-- */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_mov, { 0x700d }
  },
/* fadds ${fmax-FRd},${fmax-FRn},${fmax-FRm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRD), ',', OP (FMAX_FRN), ',', OP (FMAX_FRM), 0 } },
    & ifmt_fadds, { 0xf0070000 }
  },
/* fsubs ${fmax-FRd},${fmax-FRn},${fmax-FRm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRD), ',', OP (FMAX_FRN), ',', OP (FMAX_FRM), 0 } },
    & ifmt_fadds, { 0xf0170000 }
  },
/* fmuls ${fmax-FRd},${fmax-FRn},${fmax-FRm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRD), ',', OP (FMAX_FRN), ',', OP (FMAX_FRM), 0 } },
    & ifmt_fadds, { 0xf0270000 }
  },
/* fdivs ${fmax-FRd},${fmax-FRn},${fmax-FRm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRD), ',', OP (FMAX_FRN), ',', OP (FMAX_FRM), 0 } },
    & ifmt_fadds, { 0xf0370000 }
  },
/* fsqrts ${fmax-FRd},${fmax-FRn} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRD), ',', OP (FMAX_FRN), 0 } },
    & ifmt_fsqrts, { 0xf0470000 }
  },
/* fabss ${fmax-FRd},${fmax-FRn} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRD), ',', OP (FMAX_FRN), 0 } },
    & ifmt_fsqrts, { 0xf0570000 }
  },
/* fnegs ${fmax-FRd},${fmax-FRn} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRD), ',', OP (FMAX_FRN), 0 } },
    & ifmt_fsqrts, { 0xf0770000 }
  },
/* fmovs ${fmax-FRd},${fmax-FRn} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRD), ',', OP (FMAX_FRN), 0 } },
    & ifmt_fsqrts, { 0xf0670000 }
  },
/* froundws ${fmax-FRd-int},${fmax-FRn} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRD_INT), ',', OP (FMAX_FRN), 0 } },
    & ifmt_froundws, { 0xf0c70000 }
  },
/* ftruncws ${fmax-FRd-int},${fmax-FRn} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRD_INT), ',', OP (FMAX_FRN), 0 } },
    & ifmt_froundws, { 0xf0d70000 }
  },
/* fceilws ${fmax-FRd-int},${fmax-FRn} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRD_INT), ',', OP (FMAX_FRN), 0 } },
    & ifmt_froundws, { 0xf0e70000 }
  },
/* ffloorws ${fmax-FRd-int},${fmax-FRn} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRD_INT), ',', OP (FMAX_FRN), 0 } },
    & ifmt_froundws, { 0xf0f70000 }
  },
/* fcvtws ${fmax-FRd-int},${fmax-FRn} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRD_INT), ',', OP (FMAX_FRN), 0 } },
    & ifmt_froundws, { 0xf0471000 }
  },
/* fcvtsw ${fmax-FRd},${fmax-FRn-int} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRD), ',', OP (FMAX_FRN_INT), 0 } },
    & ifmt_fcvtsw, { 0xf0079000 }
  },
/* fcmpfs ${fmax-FRn},${fmax-FRm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRN), ',', OP (FMAX_FRM), 0 } },
    & ifmt_fcmpfs, { 0xf0072000 }
  },
/* fcmpus ${fmax-FRn},${fmax-FRm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRN), ',', OP (FMAX_FRM), 0 } },
    & ifmt_fcmpfs, { 0xf0172000 }
  },
/* fcmpes ${fmax-FRn},${fmax-FRm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRN), ',', OP (FMAX_FRM), 0 } },
    & ifmt_fcmpfs, { 0xf0272000 }
  },
/* fcmpues ${fmax-FRn},${fmax-FRm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRN), ',', OP (FMAX_FRM), 0 } },
    & ifmt_fcmpfs, { 0xf0372000 }
  },
/* fcmpls ${fmax-FRn},${fmax-FRm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRN), ',', OP (FMAX_FRM), 0 } },
    & ifmt_fcmpfs, { 0xf0472000 }
  },
/* fcmpuls ${fmax-FRn},${fmax-FRm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRN), ',', OP (FMAX_FRM), 0 } },
    & ifmt_fcmpfs, { 0xf0572000 }
  },
/* fcmples ${fmax-FRn},${fmax-FRm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRN), ',', OP (FMAX_FRM), 0 } },
    & ifmt_fcmpfs, { 0xf0672000 }
  },
/* fcmpules ${fmax-FRn},${fmax-FRm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRN), ',', OP (FMAX_FRM), 0 } },
    & ifmt_fcmpfs, { 0xf0772000 }
  },
/* fcmpfis ${fmax-FRn},${fmax-FRm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRN), ',', OP (FMAX_FRM), 0 } },
    & ifmt_fcmpfs, { 0xf0872000 }
  },
/* fcmpuis ${fmax-FRn},${fmax-FRm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRN), ',', OP (FMAX_FRM), 0 } },
    & ifmt_fcmpfs, { 0xf0972000 }
  },
/* fcmpeis ${fmax-FRn},${fmax-FRm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRN), ',', OP (FMAX_FRM), 0 } },
    & ifmt_fcmpfs, { 0xf0a72000 }
  },
/* fcmpueis ${fmax-FRn},${fmax-FRm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRN), ',', OP (FMAX_FRM), 0 } },
    & ifmt_fcmpfs, { 0xf0b72000 }
  },
/* fcmplis ${fmax-FRn},${fmax-FRm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRN), ',', OP (FMAX_FRM), 0 } },
    & ifmt_fcmpfs, { 0xf0c72000 }
  },
/* fcmpulis ${fmax-FRn},${fmax-FRm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRN), ',', OP (FMAX_FRM), 0 } },
    & ifmt_fcmpfs, { 0xf0d72000 }
  },
/* fcmpleis ${fmax-FRn},${fmax-FRm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRN), ',', OP (FMAX_FRM), 0 } },
    & ifmt_fcmpfs, { 0xf0e72000 }
  },
/* fcmpuleis ${fmax-FRn},${fmax-FRm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRN), ',', OP (FMAX_FRM), 0 } },
    & ifmt_fcmpfs, { 0xf0f72000 }
  },
/* cmov ${fmax-FRd-int},${fmax-Rm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_FRD_INT), ',', OP (FMAX_RM), 0 } },
    & ifmt_cmov_frn_rm, { 0xf007f000 }
  },
/* cmov ${fmax-Rm},${fmax-FRd-int} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_RM), ',', OP (FMAX_FRD_INT), 0 } },
    & ifmt_cmov_frn_rm, { 0xf007f001 }
  },
/* cmovc ${fmax-CCRn},${fmax-Rm} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_CCRN), ',', OP (FMAX_RM), 0 } },
    & ifmt_cmovc_ccrn_rm, { 0xf007f002 }
  },
/* cmovc ${fmax-Rm},${fmax-CCRn} */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (FMAX_RM), ',', OP (FMAX_CCRN), 0 } },
    & ifmt_cmovc_ccrn_rm, { 0xf007f003 }
  },
};

#undef A
#undef OPERAND
#undef MNEM
#undef OP

/* Formats for ALIAS macro-insns.  */

#if defined (__STDC__) || defined (ALMOST_STDC) || defined (HAVE_STRINGIZE)
#define F(f) & mep_cgen_ifld_table[MEP_##f]
#else
#define F(f) & mep_cgen_ifld_table[MEP_/**/f]
#endif
static const CGEN_IFMT ifmt_nop ATTRIBUTE_UNUSED = {
  16, 16, 0xffff, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_sb16_0 ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_sh16_0 ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_sw16_0 ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_lb16_0 ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_lh16_0 ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_lw16_0 ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_lbu16_0 ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_lhu16_0 ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_RN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_swcp16_0 ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_CRN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_lwcp16_0 ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_CRN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_smcp16_0 ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_CRN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

static const CGEN_IFMT ifmt_lmcp16_0 ATTRIBUTE_UNUSED = {
  16, 16, 0xf00f, { { F (F_MAJOR) }, { F (F_CRN) }, { F (F_RM) }, { F (F_SUB4) }, { 0 } }
};

#undef F

/* Each non-simple macro entry points to an array of expansion possibilities.  */

#if defined (__STDC__) || defined (ALMOST_STDC) || defined (HAVE_STRINGIZE)
#define A(a) (1 << CGEN_INSN_##a)
#else
#define A(a) (1 << CGEN_INSN_/**/a)
#endif
#if defined (__STDC__) || defined (ALMOST_STDC) || defined (HAVE_STRINGIZE)
#define OPERAND(op) MEP_OPERAND_##op
#else
#define OPERAND(op) MEP_OPERAND_/**/op
#endif
#define MNEM CGEN_SYNTAX_MNEMONIC /* syntax value for mnemonic */
#define OP(field) CGEN_SYNTAX_MAKE_FIELD (OPERAND (field))

/* The macro instruction table.  */

static const CGEN_IBASE mep_cgen_macro_insn_table[] =
{
/* nop */
  {
    -1, "nop", "nop", 16,
    { 0|A(ALIAS), { { { (1<<MACH_BASE), 0 } }, { { 1, "\x80" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sb $rnc,$zero($rma) */
  {
    -1, "sb16-0", "sb", 16,
    { 0|A(NO_DIS)|A(ALIAS), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sh $rns,$zero($rma) */
  {
    -1, "sh16-0", "sh", 16,
    { 0|A(NO_DIS)|A(ALIAS), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* sw $rnl,$zero($rma) */
  {
    -1, "sw16-0", "sw", 16,
    { 0|A(NO_DIS)|A(ALIAS), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lb $rnc,$zero($rma) */
  {
    -1, "lb16-0", "lb", 16,
    { 0|A(NO_DIS)|A(ALIAS), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lh $rns,$zero($rma) */
  {
    -1, "lh16-0", "lh", 16,
    { 0|A(NO_DIS)|A(ALIAS), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lw $rnl,$zero($rma) */
  {
    -1, "lw16-0", "lw", 16,
    { 0|A(NO_DIS)|A(ALIAS), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lbu $rnuc,$zero($rma) */
  {
    -1, "lbu16-0", "lbu", 16,
    { 0|A(NO_DIS)|A(ALIAS), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lhu $rnus,$zero($rma) */
  {
    -1, "lhu16-0", "lhu", 16,
    { 0|A(NO_DIS)|A(ALIAS), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* swcp $crn,$zero($rma) */
  {
    -1, "swcp16-0", "swcp", 16,
    { 0|A(NO_DIS)|A(OPTIONAL_CP_INSN)|A(ALIAS), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lwcp $crn,$zero($rma) */
  {
    -1, "lwcp16-0", "lwcp", 16,
    { 0|A(NO_DIS)|A(OPTIONAL_CP_INSN)|A(ALIAS), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* smcp $crn64,$zero($rma) */
  {
    -1, "smcp16-0", "smcp", 16,
    { 0|A(NO_DIS)|A(OPTIONAL_CP64_INSN)|A(OPTIONAL_CP_INSN)|A(ALIAS), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
/* lmcp $crn64,$zero($rma) */
  {
    -1, "lmcp16-0", "lmcp", 16,
    { 0|A(NO_DIS)|A(OPTIONAL_CP64_INSN)|A(OPTIONAL_CP_INSN)|A(ALIAS), { { { (1<<MACH_BASE), 0 } }, { { 1, "\xe0" } }, { { 0, 0 } }, { { CONFIG_NONE, 0 } } } }
  },
};

/* The macro instruction opcode table.  */

static const CGEN_OPCODE mep_cgen_macro_insn_opcode_table[] =
{
/* nop */
  {
    { 0, 0, 0, 0 },
    { { MNEM, 0 } },
    & ifmt_nop, { 0x0 }
  },
/* sb $rnc,$zero($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNC), ',', OP (ZERO), '(', OP (RMA), ')', 0 } },
    & ifmt_sb16_0, { 0x8 }
  },
/* sh $rns,$zero($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNS), ',', OP (ZERO), '(', OP (RMA), ')', 0 } },
    & ifmt_sh16_0, { 0x9 }
  },
/* sw $rnl,$zero($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNL), ',', OP (ZERO), '(', OP (RMA), ')', 0 } },
    & ifmt_sw16_0, { 0xa }
  },
/* lb $rnc,$zero($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNC), ',', OP (ZERO), '(', OP (RMA), ')', 0 } },
    & ifmt_lb16_0, { 0xc }
  },
/* lh $rns,$zero($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNS), ',', OP (ZERO), '(', OP (RMA), ')', 0 } },
    & ifmt_lh16_0, { 0xd }
  },
/* lw $rnl,$zero($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNL), ',', OP (ZERO), '(', OP (RMA), ')', 0 } },
    & ifmt_lw16_0, { 0xe }
  },
/* lbu $rnuc,$zero($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNUC), ',', OP (ZERO), '(', OP (RMA), ')', 0 } },
    & ifmt_lbu16_0, { 0xb }
  },
/* lhu $rnus,$zero($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (RNUS), ',', OP (ZERO), '(', OP (RMA), ')', 0 } },
    & ifmt_lhu16_0, { 0xf }
  },
/* swcp $crn,$zero($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', OP (ZERO), '(', OP (RMA), ')', 0 } },
    & ifmt_swcp16_0, { 0x3008 }
  },
/* lwcp $crn,$zero($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN), ',', OP (ZERO), '(', OP (RMA), ')', 0 } },
    & ifmt_lwcp16_0, { 0x3009 }
  },
/* smcp $crn64,$zero($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN64), ',', OP (ZERO), '(', OP (RMA), ')', 0 } },
    & ifmt_smcp16_0, { 0x300a }
  },
/* lmcp $crn64,$zero($rma) */
  {
    { 0, 0, 0, 0 },
    { { MNEM, ' ', OP (CRN64), ',', OP (ZERO), '(', OP (RMA), ')', 0 } },
    & ifmt_lmcp16_0, { 0x300b }
  },
};

#undef A
#undef OPERAND
#undef MNEM
#undef OP

#ifndef CGEN_ASM_HASH_P
#define CGEN_ASM_HASH_P(insn) 1
#endif

#ifndef CGEN_DIS_HASH_P
#define CGEN_DIS_HASH_P(insn) 1
#endif

/* Return non-zero if INSN is to be added to the hash table.
   Targets are free to override CGEN_{ASM,DIS}_HASH_P in the .opc file.  */

static int
asm_hash_insn_p (insn)
     const CGEN_INSN *insn ATTRIBUTE_UNUSED;
{
  return CGEN_ASM_HASH_P (insn);
}

static int
dis_hash_insn_p (insn)
     const CGEN_INSN *insn;
{
  /* If building the hash table and the NO-DIS attribute is present,
     ignore.  */
  if (CGEN_INSN_ATTR_VALUE (insn, CGEN_INSN_NO_DIS))
    return 0;
  return CGEN_DIS_HASH_P (insn);
}

#ifndef CGEN_ASM_HASH
#define CGEN_ASM_HASH_SIZE 127
#ifdef CGEN_MNEMONIC_OPERANDS
#define CGEN_ASM_HASH(mnem) (*(unsigned char *) (mnem) % CGEN_ASM_HASH_SIZE)
#else
#define CGEN_ASM_HASH(mnem) (*(unsigned char *) (mnem) % CGEN_ASM_HASH_SIZE) /*FIXME*/
#endif
#endif

/* It doesn't make much sense to provide a default here,
   but while this is under development we do.
   BUFFER is a pointer to the bytes of the insn, target order.
   VALUE is the first base_insn_bitsize bits as an int in host order.  */

#ifndef CGEN_DIS_HASH
#define CGEN_DIS_HASH_SIZE 256
#define CGEN_DIS_HASH(buf, value) (*(unsigned char *) (buf))
#endif

/* The result is the hash value of the insn.
   Targets are free to override CGEN_{ASM,DIS}_HASH in the .opc file.  */

static unsigned int
asm_hash_insn (mnem)
     const char * mnem;
{
  return CGEN_ASM_HASH (mnem);
}

/* BUF is a pointer to the bytes of the insn, target order.
   VALUE is the first base_insn_bitsize bits as an int in host order.  */

static unsigned int
dis_hash_insn (buf, value)
     const char * buf ATTRIBUTE_UNUSED;
     CGEN_INSN_INT value ATTRIBUTE_UNUSED;
{
  return CGEN_DIS_HASH (buf, value);
}

/* Set the recorded length of the insn in the CGEN_FIELDS struct.  */

static void
set_fields_bitsize (CGEN_FIELDS *fields, int size)
{
  CGEN_FIELDS_BITSIZE (fields) = size;
}

/* Function to call before using the operand instance table.
   This plugs the opcode entries and macro instructions into the cpu table.  */

void
mep_cgen_init_opcode_table (CGEN_CPU_DESC cd)
{
  int i;
  int num_macros = (sizeof (mep_cgen_macro_insn_table) /
		    sizeof (mep_cgen_macro_insn_table[0]));
  const CGEN_IBASE *ib = & mep_cgen_macro_insn_table[0];
  const CGEN_OPCODE *oc = & mep_cgen_macro_insn_opcode_table[0];
  CGEN_INSN *insns = xmalloc (num_macros * sizeof (CGEN_INSN));

  memset (insns, 0, num_macros * sizeof (CGEN_INSN));
  for (i = 0; i < num_macros; ++i)
    {
      insns[i].base = &ib[i];
      insns[i].opcode = &oc[i];
      mep_cgen_build_insn_regex (& insns[i]);
    }
  cd->macro_insn_table.init_entries = insns;
  cd->macro_insn_table.entry_size = sizeof (CGEN_IBASE);
  cd->macro_insn_table.num_init_entries = num_macros;

  oc = & mep_cgen_insn_opcode_table[0];
  insns = (CGEN_INSN *) cd->insn_table.init_entries;
  for (i = 0; i < MAX_INSNS; ++i)
    {
      insns[i].opcode = &oc[i];
      mep_cgen_build_insn_regex (& insns[i]);
    }

  cd->sizeof_fields = sizeof (CGEN_FIELDS);
  cd->set_fields_bitsize = set_fields_bitsize;

  cd->asm_hash_p = asm_hash_insn_p;
  cd->asm_hash = asm_hash_insn;
  cd->asm_hash_size = CGEN_ASM_HASH_SIZE;

  cd->dis_hash_p = dis_hash_insn_p;
  cd->dis_hash = dis_hash_insn;
  cd->dis_hash_size = CGEN_DIS_HASH_SIZE;
}
