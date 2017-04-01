/*
 * Disassemble SuperH instructions.
 *
 * Copyright (C) 1999 kaz Kojima
 * Copyright (C) 2008 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include <asm/ptrace.h>

/*
 * Format of an instruction in memory.
 */
typedef enum {
	HEX_0, HEX_1, HEX_2, HEX_3, HEX_4, HEX_5, HEX_6, HEX_7,
	HEX_8, HEX_9, HEX_A, HEX_B, HEX_C, HEX_D, HEX_E, HEX_F,
	REG_N, REG_M, REG_NM, REG_B,
	BRANCH_12, BRANCH_8,
	DISP_8, DISP_4,
	IMM_4, IMM_4BY2, IMM_4BY4, PCRELIMM_8BY2, PCRELIMM_8BY4,
	IMM_8, IMM_8BY2, IMM_8BY4,
} sh_nibble_type;

typedef enum {
	A_END, A_BDISP12, A_BDISP8,
	A_DEC_M, A_DEC_N,
	A_DISP_GBR, A_DISP_PC, A_DISP_REG_M, A_DISP_REG_N,
	A_GBR,
	A_IMM,
	A_INC_M, A_INC_N,
	A_IND_M, A_IND_N, A_IND_R0_REG_M, A_IND_R0_REG_N,
	A_MACH, A_MACL,
	A_PR, A_R0, A_R0_GBR, A_REG_M, A_REG_N, A_REG_B,
	A_SR, A_VBR, A_SSR, A_SPC, A_SGR, A_DBR,
	F_REG_N, F_REG_M, D_REG_N, D_REG_M,
	X_REG_N, /* Only used for argument parsing */
	X_REG_M, /* Only used for argument parsing */
	DX_REG_N, DX_REG_M, V_REG_N, V_REG_M,
	FD_REG_N,
	XMTRX_M4,
	F_FR0,
	FPUL_N, FPUL_M, FPSCR_N, FPSCR_M,
} sh_arg_type;

static struct sh_opcode_info {
	char *name;
	sh_arg_type arg[7];
	sh_nibble_type nibbles[4];
} sh_table[] = {
	{"add",{A_IMM,A_REG_N},{HEX_7,REG_N,IMM_8}},
	{"add",{ A_REG_M,A_REG_N},{HEX_3,REG_N,REG_M,HEX_C}},
	{"addc",{ A_REG_M,A_REG_N},{HEX_3,REG_N,REG_M,HEX_E}},
	{"addv",{ A_REG_M,A_REG_N},{HEX_3,REG_N,REG_M,HEX_F}},
	{"and",{A_IMM,A_R0},{HEX_C,HEX_9,IMM_8}},
	{"and",{ A_REG_M,A_REG_N},{HEX_2,REG_N,REG_M,HEX_9}},
	{"and.b",{A_IMM,A_R0_GBR},{HEX_C,HEX_D,IMM_8}},
	{"bra",{A_BDISP12},{HEX_A,BRANCH_12}},
	{"bsr",{A_BDISP12},{HEX_B,BRANCH_12}},
	{"bt",{A_BDISP8},{HEX_8,HEX_9,BRANCH_8}},
	{"bf",{A_BDISP8},{HEX_8,HEX_B,BRANCH_8}},
	{"bt.s",{A_BDISP8},{HEX_8,HEX_D,BRANCH_8}},
	{"bt/s",{A_BDISP8},{HEX_8,HEX_D,BRANCH_8}},
	{"bf.s",{A_BDISP8},{HEX_8,HEX_F,BRANCH_8}},
	{"bf/s",{A_BDISP8},{HEX_8,HEX_F,BRANCH_8}},
	{"clrmac",{0},{HEX_0,HEX_0,HEX_2,HEX_8}},
	{"clrs",{0},{HEX_0,HEX_0,HEX_4,HEX_8}},
	{"clrt",{0},{HEX_0,HEX_0,HEX_0,HEX_8}},
	{"cmp/eq",{A_IMM,A_R0},{HEX_8,HEX_8,IMM_8}},
	{"cmp/eq",{ A_REG_M,A_REG_N},{HEX_3,REG_N,REG_M,HEX_0}},
	{"cmp/ge",{ A_REG_M,A_REG_N},{HEX_3,REG_N,REG_M,HEX_3}},
	{"cmp/gt",{ A_REG_M,A_REG_N},{HEX_3,REG_N,REG_M,HEX_7}},
	{"cmp/hi",{ A_REG_M,A_REG_N},{HEX_3,REG_N,REG_M,HEX_6}},
	{"cmp/hs",{ A_REG_M,A_REG_N},{HEX_3,REG_N,REG_M,HEX_2}},
	{"cmp/pl",{A_REG_N},{HEX_4,REG_N,HEX_1,HEX_5}},
	{"cmp/pz",{A_REG_N},{HEX_4,REG_N,HEX_1,HEX_1}},
	{"cmp/str",{ A_REG_M,A_REG_N},{HEX_2,REG_N,REG_M,HEX_C}},
	{"div0s",{ A_REG_M,A_REG_N},{HEX_2,REG_N,REG_M,HEX_7}},
	{"div0u",{0},{HEX_0,HEX_0,HEX_1,HEX_9}},
	{"div1",{ A_REG_M,A_REG_N},{HEX_3,REG_N,REG_M,HEX_4}},
	{"exts.b",{ A_REG_M,A_REG_N},{HEX_6,REG_N,REG_M,HEX_E}},
	{"exts.w",{ A_REG_M,A_REG_N},{HEX_6,REG_N,REG_M,HEX_F}},
	{"extu.b",{ A_REG_M,A_REG_N},{HEX_6,REG_N,REG_M,HEX_C}},
	{"extu.w",{ A_REG_M,A_REG_N},{HEX_6,REG_N,REG_M,HEX_D}},
	{"jmp",{A_IND_N},{HEX_4,REG_N,HEX_2,HEX_B}},
	{"jsr",{A_IND_N},{HEX_4,REG_N,HEX_0,HEX_B}},
	{"ldc",{A_REG_N,A_SR},{HEX_4,REG_N,HEX_0,HEX_E}},
	{"ldc",{A_REG_N,A_GBR},{HEX_4,REG_N,HEX_1,HEX_E}},
	{"ldc",{A_REG_N,A_VBR},{HEX_4,REG_N,HEX_2,HEX_E}},
	{"ldc",{A_REG_N,A_SSR},{HEX_4,REG_N,HEX_3,HEX_E}},
	{"ldc",{A_REG_N,A_SPC},{HEX_4,REG_N,HEX_4,HEX_E}},
	{"ldc",{A_REG_N,A_DBR},{HEX_4,REG_N,HEX_7,HEX_E}},
	{"ldc",{A_REG_N,A_REG_B},{HEX_4,REG_N,REG_B,HEX_E}},
	{"ldc.l",{A_INC_N,A_SR},{HEX_4,REG_N,HEX_0,HEX_7}},
	{"ldc.l",{A_INC_N,A_GBR},{HEX_4,REG_N,HEX_1,HEX_7}},
	{"ldc.l",{A_INC_N,A_VBR},{HEX_4,REG_N,HEX_2,HEX_7}},
	{"ldc.l",{A_INC_N,A_SSR},{HEX_4,REG_N,HEX_3,HEX_7}},
	{"ldc.l",{A_INC_N,A_SPC},{HEX_4,REG_N,HEX_4,HEX_7}},
	{"ldc.l",{A_INC_N,A_DBR},{HEX_4,REG_N,HEX_7,HEX_7}},
	{"ldc.l",{A_INC_N,A_REG_B},{HEX_4,REG_N,REG_B,HEX_7}},
	{"lds",{A_REG_N,A_MACH},{HEX_4,REG_N,HEX_0,HEX_A}},
	{"lds",{A_REG_N,A_MACL},{HEX_4,REG_N,HEX_1,HEX_A}},
	{"lds",{A_REG_N,A_PR},{HEX_4,REG_N,HEX_2,HEX_A}},
	{"lds",{A_REG_M,FPUL_N},{HEX_4,REG_M,HEX_5,HEX_A}},
	{"lds",{A_REG_M,FPSCR_N},{HEX_4,REG_M,HEX_6,HEX_A}},
	{"lds.l",{A_INC_N,A_MACH},{HEX_4,REG_N,HEX_0,HEX_6}},
	{"lds.l",{A_INC_N,A_MACL},{HEX_4,REG_N,HEX_1,HEX_6}},
	{"lds.l",{A_INC_N,A_PR},{HEX_4,REG_N,HEX_2,HEX_6}},
	{"lds.l",{A_INC_M,FPUL_N},{HEX_4,REG_M,HEX_5,HEX_6}},
	{"lds.l",{A_INC_M,FPSCR_N},{HEX_4,REG_M,HEX_6,HEX_6}},
	{"ldtlb",{0},{HEX_0,HEX_0,HEX_3,HEX_8}},
	{"mac.w",{A_INC_M,A_INC_N},{HEX_4,REG_N,REG_M,HEX_F}},
	{"mov",{A_IMM,A_REG_N},{HEX_E,REG_N,IMM_8}},
	{"mov",{ A_REG_M,A_REG_N},{HEX_6,REG_N,REG_M,HEX_3}},
	{"mov.b",{ A_REG_M,A_IND_R0_REG_N},{HEX_0,REG_N,REG_M,HEX_4}},
	{"mov.b",{ A_REG_M,A_DEC_N},{HEX_2,REG_N,REG_M,HEX_4}},
	{"mov.b",{ A_REG_M,A_IND_N},{HEX_2,REG_N,REG_M,HEX_0}},
	{"mov.b",{A_DISP_REG_M,A_R0},{HEX_8,HEX_4,REG_M,IMM_4}},
	{"mov.b",{A_DISP_GBR,A_R0},{HEX_C,HEX_4,IMM_8}},
	{"mov.b",{A_IND_R0_REG_M,A_REG_N},{HEX_0,REG_N,REG_M,HEX_C}},
	{"mov.b",{A_INC_M,A_REG_N},{HEX_6,REG_N,REG_M,HEX_4}},
	{"mov.b",{A_IND_M,A_REG_N},{HEX_6,REG_N,REG_M,HEX_0}},
	{"mov.b",{A_R0,A_DISP_REG_M},{HEX_8,HEX_0,REG_M,IMM_4}},
	{"mov.b",{A_R0,A_DISP_GBR},{HEX_C,HEX_0,IMM_8}},
	{"mov.l",{ A_REG_M,A_DISP_REG_N},{HEX_1,REG_N,REG_M,IMM_4BY4}},
	{"mov.l",{ A_REG_M,A_IND_R0_REG_N},{HEX_0,REG_N,REG_M,HEX_6}},
	{"mov.l",{ A_REG_M,A_DEC_N},{HEX_2,REG_N,REG_M,HEX_6}},
	{"mov.l",{ A_REG_M,A_IND_N},{HEX_2,REG_N,REG_M,HEX_2}},
	{"mov.l",{A_DISP_REG_M,A_REG_N},{HEX_5,REG_N,REG_M,IMM_4BY4}},
	{"mov.l",{A_DISP_GBR,A_R0},{HEX_C,HEX_6,IMM_8BY4}},
	{"mov.l",{A_DISP_PC,A_REG_N},{HEX_D,REG_N,PCRELIMM_8BY4}},
	{"mov.l",{A_IND_R0_REG_M,A_REG_N},{HEX_0,REG_N,REG_M,HEX_E}},
	{"mov.l",{A_INC_M,A_REG_N},{HEX_6,REG_N,REG_M,HEX_6}},
	{"mov.l",{A_IND_M,A_REG_N},{HEX_6,REG_N,REG_M,HEX_2}},
	{"mov.l",{A_R0,A_DISP_GBR},{HEX_C,HEX_2,IMM_8BY4}},
	{"mov.w",{ A_REG_M,A_IND_R0_REG_N},{HEX_0,REG_N,REG_M,HEX_5}},
	{"mov.w",{ A_REG_M,A_DEC_N},{HEX_2,REG_N,REG_M,HEX_5}},
	{"mov.w",{ A_REG_M,A_IND_N},{HEX_2,REG_N,REG_M,HEX_1}},
	{"mov.w",{A_DISP_REG_M,A_R0},{HEX_8,HEX_5,REG_M,IMM_4BY2}},
	{"mov.w",{A_DISP_GBR,A_R0},{HEX_C,HEX_5,IMM_8BY2}},
	{"mov.w",{A_DISP_PC,A_REG_N},{HEX_9,REG_N,PCRELIMM_8BY2}},
	{"mov.w",{A_IND_R0_REG_M,A_REG_N},{HEX_0,REG_N,REG_M,HEX_D}},
	{"mov.w",{A_INC_M,A_REG_N},{HEX_6,REG_N,REG_M,HEX_5}},
	{"mov.w",{A_IND_M,A_REG_N},{HEX_6,REG_N,REG_M,HEX_1}},
	{"mov.w",{A_R0,A_DISP_REG_M},{HEX_8,HEX_1,REG_M,IMM_4BY2}},
	{"mov.w",{A_R0,A_DISP_GBR},{HEX_C,HEX_1,IMM_8BY2}},
	{"mova",{A_DISP_PC,A_R0},{HEX_C,HEX_7,PCRELIMM_8BY4}},
	{"movca.l",{A_R0,A_IND_N},{HEX_0,REG_N,HEX_C,HEX_3}},
	{"movt",{A_REG_N},{HEX_0,REG_N,HEX_2,HEX_9}},
	{"muls",{ A_REG_M,A_REG_N},{HEX_2,REG_N,REG_M,HEX_F}},
	{"mul.l",{ A_REG_M,A_REG_N},{HEX_0,REG_N,REG_M,HEX_7}},
	{"mulu",{ A_REG_M,A_REG_N},{HEX_2,REG_N,REG_M,HEX_E}},
	{"neg",{ A_REG_M,A_REG_N},{HEX_6,REG_N,REG_M,HEX_B}},
	{"negc",{ A_REG_M,A_REG_N},{HEX_6,REG_N,REG_M,HEX_A}},
	{"nop",{0},{HEX_0,HEX_0,HEX_0,HEX_9}},
	{"not",{ A_REG_M,A_REG_N},{HEX_6,REG_N,REG_M,HEX_7}},
	{"ocbi",{A_IND_N},{HEX_0,REG_N,HEX_9,HEX_3}},
	{"ocbp",{A_IND_N},{HEX_0,REG_N,HEX_A,HEX_3}},
	{"ocbwb",{A_IND_N},{HEX_0,REG_N,HEX_B,HEX_3}},
	{"or",{A_IMM,A_R0},{HEX_C,HEX_B,IMM_8}},
	{"or",{ A_REG_M,A_REG_N},{HEX_2,REG_N,REG_M,HEX_B}},
	{"or.b",{A_IMM,A_R0_GBR},{HEX_C,HEX_F,IMM_8}},
	{"pref",{A_IND_N},{HEX_0,REG_N,HEX_8,HEX_3}},
	{"rotcl",{A_REG_N},{HEX_4,REG_N,HEX_2,HEX_4}},
	{"rotcr",{A_REG_N},{HEX_4,REG_N,HEX_2,HEX_5}},
	{"rotl",{A_REG_N},{HEX_4,REG_N,HEX_0,HEX_4}},
	{"rotr",{A_REG_N},{HEX_4,REG_N,HEX_0,HEX_5}},
	{"rte",{0},{HEX_0,HEX_0,HEX_2,HEX_B}},
	{"rts",{0},{HEX_0,HEX_0,HEX_0,HEX_B}},
	{"sets",{0},{HEX_0,HEX_0,HEX_5,HEX_8}},
	{"sett",{0},{HEX_0,HEX_0,HEX_1,HEX_8}},
	{"shad",{ A_REG_M,A_REG_N},{HEX_4,REG_N,REG_M,HEX_C}},
	{"shld",{ A_REG_M,A_REG_N},{HEX_4,REG_N,REG_M,HEX_D}},
	{"shal",{A_REG_N},{HEX_4,REG_N,HEX_2,HEX_0}},
	{"shar",{A_REG_N},{HEX_4,REG_N,HEX_2,HEX_1}},
	{"shll",{A_REG_N},{HEX_4,REG_N,HEX_0,HEX_0}},
	{"shll16",{A_REG_N},{HEX_4,REG_N,HEX_2,HEX_8}},
	{"shll2",{A_REG_N},{HEX_4,REG_N,HEX_0,HEX_8}},
	{"shll8",{A_REG_N},{HEX_4,REG_N,HEX_1,HEX_8}},
	{"shlr",{A_REG_N},{HEX_4,REG_N,HEX_0,HEX_1}},
	{"shlr16",{A_REG_N},{HEX_4,REG_N,HEX_2,HEX_9}},
	{"shlr2",{A_REG_N},{HEX_4,REG_N,HEX_0,HEX_9}},
	{"shlr8",{A_REG_N},{HEX_4,REG_N,HEX_1,HEX_9}},
	{"sleep",{0},{HEX_0,HEX_0,HEX_1,HEX_B}},
	{"stc",{A_SR,A_REG_N},{HEX_0,REG_N,HEX_0,HEX_2}},
	{"stc",{A_GBR,A_REG_N},{HEX_0,REG_N,HEX_1,HEX_2}},
	{"stc",{A_VBR,A_REG_N},{HEX_0,REG_N,HEX_2,HEX_2}},
	{"stc",{A_SSR,A_REG_N},{HEX_0,REG_N,HEX_3,HEX_2}},
	{"stc",{A_SPC,A_REG_N},{HEX_0,REG_N,HEX_4,HEX_2}},
	{"stc",{A_SGR,A_REG_N},{HEX_0,REG_N,HEX_6,HEX_2}},
	{"stc",{A_DBR,A_REG_N},{HEX_0,REG_N,HEX_7,HEX_2}},
	{"stc",{A_REG_B,A_REG_N},{HEX_0,REG_N,REG_B,HEX_2}},
	{"stc.l",{A_SR,A_DEC_N},{HEX_4,REG_N,HEX_0,HEX_3}},
	{"stc.l",{A_GBR,A_DEC_N},{HEX_4,REG_N,HEX_1,HEX_3}},
	{"stc.l",{A_VBR,A_DEC_N},{HEX_4,REG_N,HEX_2,HEX_3}},
	{"stc.l",{A_SSR,A_DEC_N},{HEX_4,REG_N,HEX_3,HEX_3}},
	{"stc.l",{A_SPC,A_DEC_N},{HEX_4,REG_N,HEX_4,HEX_3}},
	{"stc.l",{A_SGR,A_DEC_N},{HEX_4,REG_N,HEX_6,HEX_3}},
	{"stc.l",{A_DBR,A_DEC_N},{HEX_4,REG_N,HEX_7,HEX_3}},
	{"stc.l",{A_REG_B,A_DEC_N},{HEX_4,REG_N,REG_B,HEX_3}},
	{"sts",{A_MACH,A_REG_N},{HEX_0,REG_N,HEX_0,HEX_A}},
	{"sts",{A_MACL,A_REG_N},{HEX_0,REG_N,HEX_1,HEX_A}},
	{"sts",{A_PR,A_REG_N},{HEX_0,REG_N,HEX_2,HEX_A}},
	{"sts",{FPUL_M,A_REG_N},{HEX_0,REG_N,HEX_5,HEX_A}},
	{"sts",{FPSCR_M,A_REG_N},{HEX_0,REG_N,HEX_6,HEX_A}},
	{"sts.l",{A_MACH,A_DEC_N},{HEX_4,REG_N,HEX_0,HEX_2}},
	{"sts.l",{A_MACL,A_DEC_N},{HEX_4,REG_N,HEX_1,HEX_2}},
	{"sts.l",{A_PR,A_DEC_N},{HEX_4,REG_N,HEX_2,HEX_2}},
	{"sts.l",{FPUL_M,A_DEC_N},{HEX_4,REG_N,HEX_5,HEX_2}},
	{"sts.l",{FPSCR_M,A_DEC_N},{HEX_4,REG_N,HEX_6,HEX_2}},
	{"sub",{ A_REG_M,A_REG_N},{HEX_3,REG_N,REG_M,HEX_8}},
	{"subc",{ A_REG_M,A_REG_N},{HEX_3,REG_N,REG_M,HEX_A}},
	{"subv",{ A_REG_M,A_REG_N},{HEX_3,REG_N,REG_M,HEX_B}},
	{"swap.b",{ A_REG_M,A_REG_N},{HEX_6,REG_N,REG_M,HEX_8}},
	{"swap.w",{ A_REG_M,A_REG_N},{HEX_6,REG_N,REG_M,HEX_9}},
	{"tas.b",{A_IND_N},{HEX_4,REG_N,HEX_1,HEX_B}},
	{"trapa",{A_IMM},{HEX_C,HEX_3,IMM_8}},
	{"tst",{A_IMM,A_R0},{HEX_C,HEX_8,IMM_8}},
	{"tst",{ A_REG_M,A_REG_N},{HEX_2,REG_N,REG_M,HEX_8}},
	{"tst.b",{A_IMM,A_R0_GBR},{HEX_C,HEX_C,IMM_8}},
	{"xor",{A_IMM,A_R0},{HEX_C,HEX_A,IMM_8}},
	{"xor",{ A_REG_M,A_REG_N},{HEX_2,REG_N,REG_M,HEX_A}},
	{"xor.b",{A_IMM,A_R0_GBR},{HEX_C,HEX_E,IMM_8}},
	{"xtrct",{ A_REG_M,A_REG_N},{HEX_2,REG_N,REG_M,HEX_D}},
	{"mul.l",{ A_REG_M,A_REG_N},{HEX_0,REG_N,REG_M,HEX_7}},
	{"dt",{A_REG_N},{HEX_4,REG_N,HEX_1,HEX_0}},
	{"dmuls.l",{ A_REG_M,A_REG_N},{HEX_3,REG_N,REG_M,HEX_D}},
	{"dmulu.l",{ A_REG_M,A_REG_N},{HEX_3,REG_N,REG_M,HEX_5}},
	{"mac.l",{A_INC_M,A_INC_N},{HEX_0,REG_N,REG_M,HEX_F}},
	{"braf",{A_REG_N},{HEX_0,REG_N,HEX_2,HEX_3}},
	{"bsrf",{A_REG_N},{HEX_0,REG_N,HEX_0,HEX_3}},
	{"fabs",{FD_REG_N},{HEX_F,REG_N,HEX_5,HEX_D}},
	{"fadd",{F_REG_M,F_REG_N},{HEX_F,REG_N,REG_M,HEX_0}},
	{"fadd",{D_REG_M,D_REG_N},{HEX_F,REG_N,REG_M,HEX_0}},
	{"fcmp/eq",{F_REG_M,F_REG_N},{HEX_F,REG_N,REG_M,HEX_4}},
	{"fcmp/eq",{D_REG_M,D_REG_N},{HEX_F,REG_N,REG_M,HEX_4}},
	{"fcmp/gt",{F_REG_M,F_REG_N},{HEX_F,REG_N,REG_M,HEX_5}},
	{"fcmp/gt",{D_REG_M,D_REG_N},{HEX_F,REG_N,REG_M,HEX_5}},
	{"fcnvds",{D_REG_N,FPUL_M},{HEX_F,REG_N,HEX_B,HEX_D}},
	{"fcnvsd",{FPUL_M,D_REG_N},{HEX_F,REG_N,HEX_A,HEX_D}},
	{"fdiv",{F_REG_M,F_REG_N},{HEX_F,REG_N,REG_M,HEX_3}},
	{"fdiv",{D_REG_M,D_REG_N},{HEX_F,REG_N,REG_M,HEX_3}},
	{"fipr",{V_REG_M,V_REG_N},{HEX_F,REG_NM,HEX_E,HEX_D}},
	{"fldi0",{F_REG_N},{HEX_F,REG_N,HEX_8,HEX_D}},
	{"fldi1",{F_REG_N},{HEX_F,REG_N,HEX_9,HEX_D}},
	{"flds",{F_REG_N,FPUL_M},{HEX_F,REG_N,HEX_1,HEX_D}},
	{"float",{FPUL_M,FD_REG_N},{HEX_F,REG_N,HEX_2,HEX_D}},
	{"fmac",{F_FR0,F_REG_M,F_REG_N},{HEX_F,REG_N,REG_M,HEX_E}},
	{"fmov",{F_REG_M,F_REG_N},{HEX_F,REG_N,REG_M,HEX_C}},
	{"fmov",{DX_REG_M,DX_REG_N},{HEX_F,REG_N,REG_M,HEX_C}},
	{"fmov",{A_IND_M,F_REG_N},{HEX_F,REG_N,REG_M,HEX_8}},
	{"fmov",{A_IND_M,DX_REG_N},{HEX_F,REG_N,REG_M,HEX_8}},
	{"fmov",{F_REG_M,A_IND_N},{HEX_F,REG_N,REG_M,HEX_A}},
	{"fmov",{DX_REG_M,A_IND_N},{HEX_F,REG_N,REG_M,HEX_A}},
	{"fmov",{A_INC_M,F_REG_N},{HEX_F,REG_N,REG_M,HEX_9}},
	{"fmov",{A_INC_M,DX_REG_N},{HEX_F,REG_N,REG_M,HEX_9}},
	{"fmov",{F_REG_M,A_DEC_N},{HEX_F,REG_N,REG_M,HEX_B}},
	{"fmov",{DX_REG_M,A_DEC_N},{HEX_F,REG_N,REG_M,HEX_B}},
	{"fmov",{A_IND_R0_REG_M,F_REG_N},{HEX_F,REG_N,REG_M,HEX_6}},
	{"fmov",{A_IND_R0_REG_M,DX_REG_N},{HEX_F,REG_N,REG_M,HEX_6}},
	{"fmov",{F_REG_M,A_IND_R0_REG_N},{HEX_F,REG_N,REG_M,HEX_7}},
	{"fmov",{DX_REG_M,A_IND_R0_REG_N},{HEX_F,REG_N,REG_M,HEX_7}},
	{"fmov.d",{A_IND_M,DX_REG_N},{HEX_F,REG_N,REG_M,HEX_8}},
	{"fmov.d",{DX_REG_M,A_IND_N},{HEX_F,REG_N,REG_M,HEX_A}},
	{"fmov.d",{A_INC_M,DX_REG_N},{HEX_F,REG_N,REG_M,HEX_9}},
	{"fmov.d",{DX_REG_M,A_DEC_N},{HEX_F,REG_N,REG_M,HEX_B}},
	{"fmov.d",{A_IND_R0_REG_M,DX_REG_N},{HEX_F,REG_N,REG_M,HEX_6}},
	{"fmov.d",{DX_REG_M,A_IND_R0_REG_N},{HEX_F,REG_N,REG_M,HEX_7}},
	{"fmov.s",{A_IND_M,F_REG_N},{HEX_F,REG_N,REG_M,HEX_8}},
	{"fmov.s",{F_REG_M,A_IND_N},{HEX_F,REG_N,REG_M,HEX_A}},
	{"fmov.s",{A_INC_M,F_REG_N},{HEX_F,REG_N,REG_M,HEX_9}},
	{"fmov.s",{F_REG_M,A_DEC_N},{HEX_F,REG_N,REG_M,HEX_B}},
	{"fmov.s",{A_IND_R0_REG_M,F_REG_N},{HEX_F,REG_N,REG_M,HEX_6}},
	{"fmov.s",{F_REG_M,A_IND_R0_REG_N},{HEX_F,REG_N,REG_M,HEX_7}},
	{"fmul",{F_REG_M,F_REG_N},{HEX_F,REG_N,REG_M,HEX_2}},
	{"fmul",{D_REG_M,D_REG_N},{HEX_F,REG_N,REG_M,HEX_2}},
	{"fneg",{FD_REG_N},{HEX_F,REG_N,HEX_4,HEX_D}},
	{"frchg",{0},{HEX_F,HEX_B,HEX_F,HEX_D}},
	{"fschg",{0},{HEX_F,HEX_3,HEX_F,HEX_D}},
	{"fsqrt",{FD_REG_N},{HEX_F,REG_N,HEX_6,HEX_D}},
	{"fsts",{FPUL_M,F_REG_N},{HEX_F,REG_N,HEX_0,HEX_D}},
	{"fsub",{F_REG_M,F_REG_N},{HEX_F,REG_N,REG_M,HEX_1}},
	{"fsub",{D_REG_M,D_REG_N},{HEX_F,REG_N,REG_M,HEX_1}},
	{"ftrc",{FD_REG_N,FPUL_M},{HEX_F,REG_N,HEX_3,HEX_D}},
	{"ftrv",{XMTRX_M4,V_REG_N},{HEX_F,REG_NM,HEX_F,HEX_D}},
	{ 0 },
};

static void print_sh_insn(u32 memaddr, u16 insn)
{
	int relmask = ~0;
	int nibs[4] = { (insn >> 12) & 0xf, (insn >> 8) & 0xf, (insn >> 4) & 0xf, insn & 0xf};
	int lastsp;
	struct sh_opcode_info *op = sh_table;

	for (; op->name; op++) {
		int n;
		int imm = 0;
		int rn = 0;
		int rm = 0;
		int rb = 0;
		int disp_pc;
		int disp_pc_addr = 0;

		for (n = 0; n < 4; n++) {
			int i = op->nibbles[n];

			if (i < 16) {
				if (nibs[n] == i)
					continue;
				goto fail;
			}
			switch (i) {
			case BRANCH_8:
				imm = (nibs[2] << 4) | (nibs[3]);
				if (imm & 0x80)
					imm |= ~0xff;
				imm = ((char)imm) * 2 + 4 ;
				goto ok;
			case BRANCH_12:
				imm = ((nibs[1]) << 8) | (nibs[2] << 4) | (nibs[3]);
				if (imm & 0x800)
					imm |= ~0xfff;
				imm = imm * 2 + 4;
				goto ok;
			case IMM_4:
				imm = nibs[3];
				goto ok;
			case IMM_4BY2:
				imm = nibs[3] <<1;
				goto ok;
			case IMM_4BY4:
				imm = nibs[3] <<2;
				goto ok;
			case IMM_8:
				imm = (nibs[2] << 4) | nibs[3];
				goto ok;
			case PCRELIMM_8BY2:
				imm = ((nibs[2] << 4) | nibs[3]) <<1;
				relmask = ~1;
				goto ok;
			case PCRELIMM_8BY4:
				imm = ((nibs[2] << 4) | nibs[3]) <<2;
				relmask = ~3;
				goto ok;
			case IMM_8BY2:
				imm = ((nibs[2] << 4) | nibs[3]) <<1;
				goto ok;
			case IMM_8BY4:
				imm = ((nibs[2] << 4) | nibs[3]) <<2;
				goto ok;
			case DISP_8:
				imm = (nibs[2] << 4) | (nibs[3]);
				goto ok;
			case DISP_4:
				imm = nibs[3];
				goto ok;
			case REG_N:
				rn = nibs[n];
				break;
			case REG_M:
				rm = nibs[n];
				break;
			case REG_NM:
				rn = (nibs[n] & 0xc) >> 2;
				rm = (nibs[n] & 0x3);
				break;
			case REG_B:
				rb = nibs[n] & 0x07;
				break;
			default:
				return;
			}
		}

	ok:
		printk("%-8s  ", op->name);
		lastsp = (op->arg[0] == A_END);
		disp_pc = 0;
		for (n = 0; n < 6 && op->arg[n] != A_END; n++) {
			if (n && op->arg[1] != A_END)
				printk(", ");
			switch (op->arg[n]) {
			case A_IMM:
				printk("#%d", (char)(imm));
				break;
			case A_R0:
				printk("r0");
				break;
			case A_REG_N:
				printk("r%d", rn);
				break;
			case A_INC_N:
				printk("@r%d+", rn);
				break;
			case A_DEC_N:
				printk("@-r%d", rn);
				break;
			case A_IND_N:
				printk("@r%d", rn);
				break;
			case A_DISP_REG_N:
				printk("@(%d,r%d)", imm, rn);
				break;
			case A_REG_M:
				printk("r%d", rm);
				break;
			case A_INC_M:
				printk("@r%d+", rm);
				break;
			case A_DEC_M:
				printk("@-r%d", rm);
				break;
			case A_IND_M:
				printk("@r%d", rm);
				break;
			case A_DISP_REG_M:
				printk("@(%d,r%d)", imm, rm);
				break;
			case A_REG_B:
				printk("r%d_bank", rb);
				break;
			case A_DISP_PC:
				disp_pc = 1;
				disp_pc_addr = imm + 4 + (memaddr & relmask);
				printk("%08x <%pS>", disp_pc_addr,
				       (void *)disp_pc_addr);
				break;
			case A_IND_R0_REG_N:
				printk("@(r0,r%d)", rn);
				break;
			case A_IND_R0_REG_M:
				printk("@(r0,r%d)", rm);
				break;
			case A_DISP_GBR:
				printk("@(%d,gbr)",imm);
				break;
			case A_R0_GBR:
				printk("@(r0,gbr)");
				break;
			case A_BDISP12:
			case A_BDISP8:
				printk("%08x", imm + memaddr);
				break;
			case A_SR:
				printk("sr");
				break;
			case A_GBR:
				printk("gbr");
				break;
			case A_VBR:
				printk("vbr");
				break;
			case A_SSR:
				printk("ssr");
				break;
			case A_SPC:
				printk("spc");
				break;
			case A_MACH:
				printk("mach");
				break;
			case A_MACL:
				printk("macl");
				break;
			case A_PR:
				printk("pr");
				break;
			case A_SGR:
				printk("sgr");
				break;
			case A_DBR:
				printk("dbr");
				break;
			case FD_REG_N:
				if (0)
					goto d_reg_n;
			case F_REG_N:
				printk("fr%d", rn);
				break;
			case F_REG_M:
				printk("fr%d", rm);
				break;
			case DX_REG_N:
				if (rn & 1) {
					printk("xd%d", rn & ~1);
					break;
				}
			d_reg_n:
			case D_REG_N:
				printk("dr%d", rn);
				break;
			case DX_REG_M:
				if (rm & 1) {
					printk("xd%d", rm & ~1);
					break;
				}
			case D_REG_M:
				printk("dr%d", rm);
				break;
			case FPSCR_M:
			case FPSCR_N:
				printk("fpscr");
				break;
			case FPUL_M:
			case FPUL_N:
				printk("fpul");
				break;
			case F_FR0:
				printk("fr0");
				break;
			case V_REG_N:
				printk("fv%d", rn*4);
				break;
			case V_REG_M:
				printk("fv%d", rm*4);
				break;
			case XMTRX_M4:
				printk("xmtrx");
				break;
			default:
				return;
			}
		}

		if (disp_pc && strcmp(op->name, "mova") != 0) {
			u32 val;

			if (relmask == ~1)
				__get_user(val, (u16 *)disp_pc_addr);
			else
				__get_user(val, (u32 *)disp_pc_addr);

			printk("  ! %08x <%pS>", val, (void *)val);
		}

		return;
	fail:
		;

	}

	printk(".word 0x%x%x%x%x", nibs[0], nibs[1], nibs[2], nibs[3]);
}

void show_code(struct pt_regs *regs)
{
	unsigned short *pc = (unsigned short *)regs->pc;
	long i;

	if (regs->pc & 0x1)
		return;

	printk("Code:\n");

	for (i = -3 ; i < 6 ; i++) {
		unsigned short insn;

		if (__get_user(insn, pc + i)) {
			printk(" (Bad address in pc)\n");
			break;
		}

		printk("%s%08lx:  ", (i ? "  ": "->"), (unsigned long)(pc + i));
		print_sh_insn((unsigned long)(pc + i), insn);
		printk("\n");
	}

	printk("\n");
}
