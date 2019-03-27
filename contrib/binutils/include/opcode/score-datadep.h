/* score-datadep.h -- Score Instructions data dependency table
   Copyright 2006 Free Software Foundation, Inc.
   Contributed by: 
   Mei Ligang (ligang@sunnorth.com.cn)
   Pei-Lin Tsai (pltsai@sunplus.com)

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
   Boston, MA 02110-1301, USA.  */

#ifndef SCORE_DATA_DEPENDENCY_H
#define SCORE_DATA_DEPENDENCY_H

#define INSN_NAME_LEN 16

enum insn_type_for_dependency
{
  D_pce,
  D_cond_br,
  D_cond_mv,
  D_cached,
  D_cachei,
  D_ldst,
  D_ldcombine,
  D_mtcr,
  D_mfcr,
  D_mfsr,
  D_mftlb,
  D_mtptlb,
  D_mtrtlb,
  D_stlb,
  D_all_insn
};

struct insn_to_dependency
{
  char *insn_name;
  enum insn_type_for_dependency type;
};

struct data_dependency
{
  enum insn_type_for_dependency pre_insn_type;
  char pre_reg[6];
  enum insn_type_for_dependency cur_insn_type;
  char cur_reg[6];
  int bubblenum_7;
  int bubblenum_5;
  int warn_or_error;           /* warning - 0; error - 1  */
};

static const struct insn_to_dependency insn_to_dependency_table[] =
{
  /* pce instruction.  */
  {"pce",       D_pce},
  /* conditional branch instruction.  */
  {"bcs",       D_cond_br},
  {"bcc",       D_cond_br},
  {"bgtu",      D_cond_br},
  {"bleu",      D_cond_br},
  {"beq",       D_cond_br},
  {"bne",       D_cond_br},
  {"bgt",       D_cond_br},
  {"ble",       D_cond_br},
  {"bge",       D_cond_br},
  {"blt",       D_cond_br},
  {"bmi",       D_cond_br},
  {"bpl",       D_cond_br},
  {"bvs",       D_cond_br},
  {"bvc",       D_cond_br},
  {"bcsl",      D_cond_br},
  {"bccl",      D_cond_br},
  {"bgtul",     D_cond_br},
  {"bleul",     D_cond_br},
  {"beql",      D_cond_br},
  {"bnel",      D_cond_br},
  {"bgtl",      D_cond_br},
  {"blel",      D_cond_br},
  {"bgel",      D_cond_br},
  {"bltl",      D_cond_br},
  {"bmil",      D_cond_br},
  {"bpll",      D_cond_br},
  {"bvsl",      D_cond_br},
  {"bvcl",      D_cond_br},
  {"bcs!",      D_cond_br},
  {"bcc!",      D_cond_br},
  {"bgtu!",     D_cond_br},
  {"bleu!",     D_cond_br},
  {"beq!",      D_cond_br},
  {"bne!",      D_cond_br},
  {"bgt!",      D_cond_br},
  {"ble!",      D_cond_br},
  {"bge!",      D_cond_br},
  {"blt!",      D_cond_br},
  {"bmi!",      D_cond_br},
  {"bpl!",      D_cond_br},
  {"bvs!",      D_cond_br},
  {"bvc!",      D_cond_br},
  {"brcs",      D_cond_br},
  {"brcc",      D_cond_br},
  {"brgtu",     D_cond_br},
  {"brleu",     D_cond_br},
  {"breq",      D_cond_br},
  {"brne",      D_cond_br},
  {"brgt",      D_cond_br},
  {"brle",      D_cond_br},
  {"brge",      D_cond_br},
  {"brlt",      D_cond_br},
  {"brmi",      D_cond_br},
  {"brpl",      D_cond_br},
  {"brvs",      D_cond_br},
  {"brvc",      D_cond_br},
  {"brcsl",     D_cond_br},
  {"brccl",     D_cond_br},
  {"brgtul",    D_cond_br},
  {"brleul",    D_cond_br},
  {"breql",     D_cond_br},
  {"brnel",     D_cond_br},
  {"brgtl",     D_cond_br},
  {"brlel",     D_cond_br},
  {"brgel",     D_cond_br},
  {"brltl",     D_cond_br},
  {"brmil",     D_cond_br},
  {"brpll",     D_cond_br},
  {"brvsl",     D_cond_br},
  {"brvcl",     D_cond_br},
  {"brcs!",     D_cond_br},
  {"brcc!",     D_cond_br},
  {"brgtu!",    D_cond_br},
  {"brleu!",    D_cond_br},
  {"breq!",     D_cond_br},
  {"brne!",     D_cond_br},
  {"brgt!",     D_cond_br},
  {"brle!",     D_cond_br},
  {"brge!",     D_cond_br},
  {"brlt!",     D_cond_br},
  {"brmi!",     D_cond_br},
  {"brpl!",     D_cond_br},
  {"brvs!",     D_cond_br},
  {"brvc!",     D_cond_br},
  {"brcsl!",    D_cond_br},
  {"brccl!",    D_cond_br},
  {"brgtul!",   D_cond_br},
  {"brleul!",   D_cond_br},
  {"breql!",    D_cond_br},
  {"brnel!",    D_cond_br},
  {"brgtl!",    D_cond_br},
  {"brlel!",    D_cond_br},
  {"brgel!",    D_cond_br},
  {"brltl!",    D_cond_br},
  {"brmil!",    D_cond_br},
  {"brpll!",    D_cond_br},
  {"brvsl!",    D_cond_br},
  {"brvcl!",    D_cond_br},
  /* conditional move instruction.  */
  {"mvcs",      D_cond_mv},
  {"mvcc",      D_cond_mv},
  {"mvgtu",     D_cond_mv},
  {"mvleu",     D_cond_mv},
  {"mveq",      D_cond_mv},
  {"mvne",      D_cond_mv},
  {"mvgt",      D_cond_mv},
  {"mvle",      D_cond_mv},
  {"mvge",      D_cond_mv},
  {"mvlt",      D_cond_mv},
  {"mvmi",      D_cond_mv},
  {"mvpl",      D_cond_mv},
  {"mvvs",      D_cond_mv},
  {"mvvc",      D_cond_mv},
  /* move spectial instruction.  */
  {"mtcr",      D_mtcr},
  {"mftlb",     D_mftlb},
  {"mtptlb",    D_mtptlb},
  {"mtrtlb",    D_mtrtlb},
  {"stlb",      D_stlb},
  {"mfcr",      D_mfcr},
  {"mfsr",      D_mfsr},
  /* cache instruction.  */
  {"cache 8",   D_cached},
  {"cache 9",   D_cached},
  {"cache 10",  D_cached},
  {"cache 11",  D_cached},
  {"cache 12",  D_cached},
  {"cache 13",  D_cached},
  {"cache 14",  D_cached},
  {"cache 24",  D_cached},
  {"cache 26",  D_cached},
  {"cache 27",  D_cached},
  {"cache 29",  D_cached},
  {"cache 30",  D_cached},
  {"cache 31",  D_cached},
  {"cache 0",   D_cachei},
  {"cache 1",   D_cachei},
  {"cache 2",   D_cachei},
  {"cache 3",   D_cachei},
  {"cache 4",   D_cachei},
  {"cache 16",  D_cachei},
  {"cache 17",  D_cachei},
  /* load/store instruction.  */
  {"lb",        D_ldst},
  {"lbu",       D_ldst},
  {"lbu!",      D_ldst},
  {"lbup!",     D_ldst},
  {"lh",        D_ldst},
  {"lhu",       D_ldst},
  {"lh!",       D_ldst},
  {"lhp!",      D_ldst},
  {"lw",        D_ldst},
  {"lw!",       D_ldst},
  {"lwp!",      D_ldst},
  {"sb",        D_ldst},
  {"sb!",       D_ldst},
  {"sbp!",      D_ldst},
  {"sh",        D_ldst},
  {"sh!",       D_ldst},
  {"shp!",      D_ldst},
  {"sw",        D_ldst},
  {"sw!",       D_ldst},
  {"swp!",      D_ldst},
  {"alw",       D_ldst},
  {"asw",       D_ldst},
  {"push!",     D_ldst},
  {"pushhi!",   D_ldst},
  {"pop!",      D_ldst},
  {"pophi!",    D_ldst},
  {"ldc1",      D_ldst},
  {"ldc2",      D_ldst},
  {"ldc3",      D_ldst},
  {"stc1",      D_ldst},
  {"stc2",      D_ldst},
  {"stc3",      D_ldst},
  {"scb",       D_ldst},
  {"scw",       D_ldst},
  {"sce",       D_ldst},
  /* load combine instruction.  */
  {"lcb",       D_ldcombine},
  {"lcw",       D_ldcombine},
  {"lce",       D_ldcombine},
};

static const struct data_dependency data_dependency_table[] =
{
  /* Condition register.  */
  {D_mtcr, "cr1", D_pce, "", 2, 1, 1},
  {D_mtcr, "cr1", D_cond_br, "", 1, 0, 1},
  {D_mtcr, "cr1", D_cond_mv, "", 1, 0, 1},
  /* Status regiser.  */
  {D_mtcr, "cr0", D_all_insn, "", 5, 4, 0},
  /* CCR regiser.  */
  {D_mtcr, "cr4", D_all_insn, "", 6, 5, 0},
  /* EntryHi/EntryLo register.  */
  {D_mftlb, "", D_mtptlb, "", 1, 1, 1},
  {D_mftlb, "", D_mtrtlb, "", 1, 1, 1},
  {D_mftlb, "", D_stlb, "", 1, 1,1},
  {D_mftlb, "", D_mfcr, "cr11", 1, 1, 1},
  {D_mftlb, "", D_mfcr, "cr12", 1, 1, 1},
  /* Index register.  */
  {D_stlb, "", D_mtptlb, "", 1, 1, 1},
  {D_stlb, "", D_mftlb, "", 1, 1, 1},
  {D_stlb, "", D_mfcr, "cr8", 2, 2, 1},
  /* Cache.  */
  {D_cached, "", D_ldst, "", 1, 1, 0},
  {D_cached, "", D_ldcombine, "", 1, 1, 0},
  {D_cachei, "", D_all_insn, "", 5, 4, 0},
  /* Load combine.  */
  {D_ldcombine, "", D_mfsr, "sr1", 3, 3, 1},
};

#endif
