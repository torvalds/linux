// SPDX-License-Identifier: GPL-2.0
/*
 * Disassemble s390 instructions.
 *
 * Copyright IBM Corp. 2007
 * Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kallsyms.h>
#include <linux/reboot.h>
#include <linux/kprobes.h>
#include <linux/kdebug.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/io.h>
#include <asm/dis.h>
#include <asm/cpcmd.h>
#include <asm/lowcore.h>
#include <asm/debug.h>
#include <asm/irq.h>

/* Type of operand */
#define OPERAND_GPR	0x1	/* Operand printed as %rx */
#define OPERAND_FPR	0x2	/* Operand printed as %fx */
#define OPERAND_AR	0x4	/* Operand printed as %ax */
#define OPERAND_CR	0x8	/* Operand printed as %cx */
#define OPERAND_VR	0x10	/* Operand printed as %vx */
#define OPERAND_DISP	0x20	/* Operand printed as displacement */
#define OPERAND_BASE	0x40	/* Operand printed as base register */
#define OPERAND_INDEX	0x80	/* Operand printed as index register */
#define OPERAND_PCREL	0x100	/* Operand printed as pc-relative symbol */
#define OPERAND_SIGNED	0x200	/* Operand printed as signed value */
#define OPERAND_LENGTH	0x400	/* Operand printed as length (+1) */

struct s390_operand {
	unsigned char bits;	/* The number of bits in the operand. */
	unsigned char shift;	/* The number of bits to shift. */
	unsigned short flags;	/* One bit syntax flags. */
};

struct s390_insn {
	union {
		const char name[5];
		struct {
			unsigned char zero;
			unsigned int offset;
		} __packed;
	};
	unsigned char opfrag;
	unsigned char format;
};

struct s390_opcode_offset {
	unsigned char opcode;
	unsigned char mask;
	unsigned char byte;
	unsigned short offset;
	unsigned short count;
} __packed;

enum {
	UNUSED,
	A_8,	/* Access reg. starting at position 8 */
	A_12,	/* Access reg. starting at position 12 */
	A_24,	/* Access reg. starting at position 24 */
	A_28,	/* Access reg. starting at position 28 */
	B_16,	/* Base register starting at position 16 */
	B_32,	/* Base register starting at position 32 */
	C_8,	/* Control reg. starting at position 8 */
	C_12,	/* Control reg. starting at position 12 */
	D20_20, /* 20 bit displacement starting at 20 */
	D_20,	/* Displacement starting at position 20 */
	D_36,	/* Displacement starting at position 36 */
	F_8,	/* FPR starting at position 8 */
	F_12,	/* FPR starting at position 12 */
	F_16,	/* FPR starting at position 16 */
	F_24,	/* FPR starting at position 24 */
	F_28,	/* FPR starting at position 28 */
	F_32,	/* FPR starting at position 32 */
	I8_8,	/* 8 bit signed value starting at 8 */
	I8_32,	/* 8 bit signed value starting at 32 */
	I16_16, /* 16 bit signed value starting at 16 */
	I16_32, /* 16 bit signed value starting at 32 */
	I32_16, /* 32 bit signed value starting at 16 */
	J12_12, /* 12 bit PC relative offset at 12 */
	J16_16, /* 16 bit PC relative offset at 16 */
	J16_32, /* 16 bit PC relative offset at 32 */
	J24_24, /* 24 bit PC relative offset at 24 */
	J32_16, /* 32 bit PC relative offset at 16 */
	L4_8,	/* 4 bit length starting at position 8 */
	L4_12,	/* 4 bit length starting at position 12 */
	L8_8,	/* 8 bit length starting at position 8 */
	R_8,	/* GPR starting at position 8 */
	R_12,	/* GPR starting at position 12 */
	R_16,	/* GPR starting at position 16 */
	R_24,	/* GPR starting at position 24 */
	R_28,	/* GPR starting at position 28 */
	U4_8,	/* 4 bit unsigned value starting at 8 */
	U4_12,	/* 4 bit unsigned value starting at 12 */
	U4_16,	/* 4 bit unsigned value starting at 16 */
	U4_20,	/* 4 bit unsigned value starting at 20 */
	U4_24,	/* 4 bit unsigned value starting at 24 */
	U4_28,	/* 4 bit unsigned value starting at 28 */
	U4_32,	/* 4 bit unsigned value starting at 32 */
	U4_36,	/* 4 bit unsigned value starting at 36 */
	U8_8,	/* 8 bit unsigned value starting at 8 */
	U8_16,	/* 8 bit unsigned value starting at 16 */
	U8_24,	/* 8 bit unsigned value starting at 24 */
	U8_28,	/* 8 bit unsigned value starting at 28 */
	U8_32,	/* 8 bit unsigned value starting at 32 */
	U12_16, /* 12 bit unsigned value starting at 16 */
	U16_16, /* 16 bit unsigned value starting at 16 */
	U16_20, /* 16 bit unsigned value starting at 20 */
	U16_32, /* 16 bit unsigned value starting at 32 */
	U32_16, /* 32 bit unsigned value starting at 16 */
	VX_12,	/* Vector index register starting at position 12 */
	V_8,	/* Vector reg. starting at position 8 */
	V_12,	/* Vector reg. starting at position 12 */
	V_16,	/* Vector reg. starting at position 16 */
	V_32,	/* Vector reg. starting at position 32 */
	X_12,	/* Index register starting at position 12 */
};

static const struct s390_operand operands[] = {
	[UNUSED] = {  0,  0, 0 },
	[A_8]	 = {  4,  8, OPERAND_AR },
	[A_12]	 = {  4, 12, OPERAND_AR },
	[A_24]	 = {  4, 24, OPERAND_AR },
	[A_28]	 = {  4, 28, OPERAND_AR },
	[B_16]	 = {  4, 16, OPERAND_BASE | OPERAND_GPR },
	[B_32]	 = {  4, 32, OPERAND_BASE | OPERAND_GPR },
	[C_8]	 = {  4,  8, OPERAND_CR },
	[C_12]	 = {  4, 12, OPERAND_CR },
	[D20_20] = { 20, 20, OPERAND_DISP | OPERAND_SIGNED },
	[D_20]	 = { 12, 20, OPERAND_DISP },
	[D_36]	 = { 12, 36, OPERAND_DISP },
	[F_8]	 = {  4,  8, OPERAND_FPR },
	[F_12]	 = {  4, 12, OPERAND_FPR },
	[F_16]	 = {  4, 16, OPERAND_FPR },
	[F_24]	 = {  4, 24, OPERAND_FPR },
	[F_28]	 = {  4, 28, OPERAND_FPR },
	[F_32]	 = {  4, 32, OPERAND_FPR },
	[I8_8]	 = {  8,  8, OPERAND_SIGNED },
	[I8_32]	 = {  8, 32, OPERAND_SIGNED },
	[I16_16] = { 16, 16, OPERAND_SIGNED },
	[I16_32] = { 16, 32, OPERAND_SIGNED },
	[I32_16] = { 32, 16, OPERAND_SIGNED },
	[J12_12] = { 12, 12, OPERAND_PCREL },
	[J16_16] = { 16, 16, OPERAND_PCREL },
	[J16_32] = { 16, 32, OPERAND_PCREL },
	[J24_24] = { 24, 24, OPERAND_PCREL },
	[J32_16] = { 32, 16, OPERAND_PCREL },
	[L4_8]	 = {  4,  8, OPERAND_LENGTH },
	[L4_12]	 = {  4, 12, OPERAND_LENGTH },
	[L8_8]	 = {  8,  8, OPERAND_LENGTH },
	[R_8]	 = {  4,  8, OPERAND_GPR },
	[R_12]	 = {  4, 12, OPERAND_GPR },
	[R_16]	 = {  4, 16, OPERAND_GPR },
	[R_24]	 = {  4, 24, OPERAND_GPR },
	[R_28]	 = {  4, 28, OPERAND_GPR },
	[U4_8]	 = {  4,  8, 0 },
	[U4_12]	 = {  4, 12, 0 },
	[U4_16]	 = {  4, 16, 0 },
	[U4_20]	 = {  4, 20, 0 },
	[U4_24]	 = {  4, 24, 0 },
	[U4_28]	 = {  4, 28, 0 },
	[U4_32]	 = {  4, 32, 0 },
	[U4_36]	 = {  4, 36, 0 },
	[U8_8]	 = {  8,  8, 0 },
	[U8_16]	 = {  8, 16, 0 },
	[U8_24]	 = {  8, 24, 0 },
	[U8_28]	 = {  8, 28, 0 },
	[U8_32]	 = {  8, 32, 0 },
	[U12_16] = { 12, 16, 0 },
	[U16_16] = { 16, 16, 0 },
	[U16_20] = { 16, 20, 0 },
	[U16_32] = { 16, 32, 0 },
	[U32_16] = { 32, 16, 0 },
	[VX_12]	 = {  4, 12, OPERAND_INDEX | OPERAND_VR },
	[V_8]	 = {  4,  8, OPERAND_VR },
	[V_12]	 = {  4, 12, OPERAND_VR },
	[V_16]	 = {  4, 16, OPERAND_VR },
	[V_32]	 = {  4, 32, OPERAND_VR },
	[X_12]	 = {  4, 12, OPERAND_INDEX | OPERAND_GPR },
};

static const unsigned char formats[][6] = {
	[INSTR_E]	     = { 0, 0, 0, 0, 0, 0 },
	[INSTR_IE_UU]	     = { U4_24, U4_28, 0, 0, 0, 0 },
	[INSTR_MII_UPP]	     = { U4_8, J12_12, J24_24 },
	[INSTR_RIE_R0IU]     = { R_8, I16_16, U4_32, 0, 0, 0 },
	[INSTR_RIE_R0UU]     = { R_8, U16_16, U4_32, 0, 0, 0 },
	[INSTR_RIE_RRI0]     = { R_8, R_12, I16_16, 0, 0, 0 },
	[INSTR_RIE_RRP]	     = { R_8, R_12, J16_16, 0, 0, 0 },
	[INSTR_RIE_RRPU]     = { R_8, R_12, U4_32, J16_16, 0, 0 },
	[INSTR_RIE_RRUUU]    = { R_8, R_12, U8_16, U8_24, U8_32, 0 },
	[INSTR_RIE_RUI0]     = { R_8, I16_16, U4_12, 0, 0, 0 },
	[INSTR_RIE_RUPI]     = { R_8, I8_32, U4_12, J16_16, 0, 0 },
	[INSTR_RIE_RUPU]     = { R_8, U8_32, U4_12, J16_16, 0, 0 },
	[INSTR_RIL_RI]	     = { R_8, I32_16, 0, 0, 0, 0 },
	[INSTR_RIL_RP]	     = { R_8, J32_16, 0, 0, 0, 0 },
	[INSTR_RIL_RU]	     = { R_8, U32_16, 0, 0, 0, 0 },
	[INSTR_RIL_UP]	     = { U4_8, J32_16, 0, 0, 0, 0 },
	[INSTR_RIS_RURDI]    = { R_8, I8_32, U4_12, D_20, B_16, 0 },
	[INSTR_RIS_RURDU]    = { R_8, U8_32, U4_12, D_20, B_16, 0 },
	[INSTR_RI_RI]	     = { R_8, I16_16, 0, 0, 0, 0 },
	[INSTR_RI_RP]	     = { R_8, J16_16, 0, 0, 0, 0 },
	[INSTR_RI_RU]	     = { R_8, U16_16, 0, 0, 0, 0 },
	[INSTR_RI_UP]	     = { U4_8, J16_16, 0, 0, 0, 0 },
	[INSTR_RRE_00]	     = { 0, 0, 0, 0, 0, 0 },
	[INSTR_RRE_AA]	     = { A_24, A_28, 0, 0, 0, 0 },
	[INSTR_RRE_AR]	     = { A_24, R_28, 0, 0, 0, 0 },
	[INSTR_RRE_F0]	     = { F_24, 0, 0, 0, 0, 0 },
	[INSTR_RRE_FF]	     = { F_24, F_28, 0, 0, 0, 0 },
	[INSTR_RRE_FR]	     = { F_24, R_28, 0, 0, 0, 0 },
	[INSTR_RRE_R0]	     = { R_24, 0, 0, 0, 0, 0 },
	[INSTR_RRE_RA]	     = { R_24, A_28, 0, 0, 0, 0 },
	[INSTR_RRE_RF]	     = { R_24, F_28, 0, 0, 0, 0 },
	[INSTR_RRE_RR]	     = { R_24, R_28, 0, 0, 0, 0 },
	[INSTR_RRF_0UFF]     = { F_24, F_28, U4_20, 0, 0, 0 },
	[INSTR_RRF_0URF]     = { R_24, F_28, U4_20, 0, 0, 0 },
	[INSTR_RRF_F0FF]     = { F_16, F_24, F_28, 0, 0, 0 },
	[INSTR_RRF_F0FF2]    = { F_24, F_16, F_28, 0, 0, 0 },
	[INSTR_RRF_F0FR]     = { F_24, F_16, R_28, 0, 0, 0 },
	[INSTR_RRF_FFRU]     = { F_24, F_16, R_28, U4_20, 0, 0 },
	[INSTR_RRF_FUFF]     = { F_24, F_16, F_28, U4_20, 0, 0 },
	[INSTR_RRF_FUFF2]    = { F_24, F_28, F_16, U4_20, 0, 0 },
	[INSTR_RRF_R0RR]     = { R_24, R_16, R_28, 0, 0, 0 },
	[INSTR_RRF_R0RR2]    = { R_24, R_28, R_16, 0, 0, 0 },
	[INSTR_RRF_RURR]     = { R_24, R_28, R_16, U4_20, 0, 0 },
	[INSTR_RRF_RURR2]    = { R_24, R_16, R_28, U4_20, 0, 0 },
	[INSTR_RRF_U0FF]     = { F_24, U4_16, F_28, 0, 0, 0 },
	[INSTR_RRF_U0RF]     = { R_24, U4_16, F_28, 0, 0, 0 },
	[INSTR_RRF_U0RR]     = { R_24, R_28, U4_16, 0, 0, 0 },
	[INSTR_RRF_URR]	     = { R_24, R_28, U8_16, 0, 0, 0 },
	[INSTR_RRF_UUFF]     = { F_24, U4_16, F_28, U4_20, 0, 0 },
	[INSTR_RRF_UUFR]     = { F_24, U4_16, R_28, U4_20, 0, 0 },
	[INSTR_RRF_UURF]     = { R_24, U4_16, F_28, U4_20, 0, 0 },
	[INSTR_RRS_RRRDU]    = { R_8, R_12, U4_32, D_20, B_16 },
	[INSTR_RR_FF]	     = { F_8, F_12, 0, 0, 0, 0 },
	[INSTR_RR_R0]	     = { R_8,  0, 0, 0, 0, 0 },
	[INSTR_RR_RR]	     = { R_8, R_12, 0, 0, 0, 0 },
	[INSTR_RR_U0]	     = { U8_8,	0, 0, 0, 0, 0 },
	[INSTR_RR_UR]	     = { U4_8, R_12, 0, 0, 0, 0 },
	[INSTR_RSI_RRP]	     = { R_8, R_12, J16_16, 0, 0, 0 },
	[INSTR_RSL_LRDFU]    = { F_32, D_20, L8_8, B_16, U4_36, 0 },
	[INSTR_RSL_R0RD]     = { D_20, L4_8, B_16, 0, 0, 0 },
	[INSTR_RSY_AARD]     = { A_8, A_12, D20_20, B_16, 0, 0 },
	[INSTR_RSY_CCRD]     = { C_8, C_12, D20_20, B_16, 0, 0 },
	[INSTR_RSY_RRRD]     = { R_8, R_12, D20_20, B_16, 0, 0 },
	[INSTR_RSY_RURD]     = { R_8, U4_12, D20_20, B_16, 0, 0 },
	[INSTR_RSY_RURD2]    = { R_8, D20_20, B_16, U4_12, 0, 0 },
	[INSTR_RS_AARD]	     = { A_8, A_12, D_20, B_16, 0, 0 },
	[INSTR_RS_CCRD]	     = { C_8, C_12, D_20, B_16, 0, 0 },
	[INSTR_RS_R0RD]	     = { R_8, D_20, B_16, 0, 0, 0 },
	[INSTR_RS_RRRD]	     = { R_8, R_12, D_20, B_16, 0, 0 },
	[INSTR_RS_RURD]	     = { R_8, U4_12, D_20, B_16, 0, 0 },
	[INSTR_RXE_FRRD]     = { F_8, D_20, X_12, B_16, 0, 0 },
	[INSTR_RXE_RRRDU]    = { R_8, D_20, X_12, B_16, U4_32, 0 },
	[INSTR_RXF_FRRDF]    = { F_32, F_8, D_20, X_12, B_16, 0 },
	[INSTR_RXY_FRRD]     = { F_8, D20_20, X_12, B_16, 0, 0 },
	[INSTR_RXY_RRRD]     = { R_8, D20_20, X_12, B_16, 0, 0 },
	[INSTR_RXY_URRD]     = { U4_8, D20_20, X_12, B_16, 0, 0 },
	[INSTR_RX_FRRD]	     = { F_8, D_20, X_12, B_16, 0, 0 },
	[INSTR_RX_RRRD]	     = { R_8, D_20, X_12, B_16, 0, 0 },
	[INSTR_RX_URRD]	     = { U4_8, D_20, X_12, B_16, 0, 0 },
	[INSTR_SIL_RDI]	     = { D_20, B_16, I16_32, 0, 0, 0 },
	[INSTR_SIL_RDU]	     = { D_20, B_16, U16_32, 0, 0, 0 },
	[INSTR_SIY_IRD]	     = { D20_20, B_16, I8_8, 0, 0, 0 },
	[INSTR_SIY_RD]	     = { D20_20, B_16, 0, 0, 0, 0 },
	[INSTR_SIY_URD]	     = { D20_20, B_16, U8_8, 0, 0, 0 },
	[INSTR_SI_RD]	     = { D_20, B_16, 0, 0, 0, 0 },
	[INSTR_SI_URD]	     = { D_20, B_16, U8_8, 0, 0, 0 },
	[INSTR_SMI_U0RDP]    = { U4_8, J16_32, D_20, B_16, 0, 0 },
	[INSTR_SSE_RDRD]     = { D_20, B_16, D_36, B_32, 0, 0 },
	[INSTR_SSF_RRDRD]    = { D_20, B_16, D_36, B_32, R_8, 0 },
	[INSTR_SSF_RRDRD2]   = { R_8, D_20, B_16, D_36, B_32, 0 },
	[INSTR_SS_L0RDRD]    = { D_20, L8_8, B_16, D_36, B_32, 0 },
	[INSTR_SS_L2RDRD]    = { D_20, B_16, D_36, L8_8, B_32, 0 },
	[INSTR_SS_LIRDRD]    = { D_20, L4_8, B_16, D_36, B_32, U4_12 },
	[INSTR_SS_LLRDRD]    = { D_20, L4_8, B_16, D_36, L4_12, B_32 },
	[INSTR_SS_RRRDRD]    = { D_20, R_8, B_16, D_36, B_32, R_12 },
	[INSTR_SS_RRRDRD2]   = { R_8, D_20, B_16, R_12, D_36, B_32 },
	[INSTR_SS_RRRDRD3]   = { R_8, R_12, D_20, B_16, D_36, B_32 },
	[INSTR_S_00]	     = { 0, 0, 0, 0, 0, 0 },
	[INSTR_S_RD]	     = { D_20, B_16, 0, 0, 0, 0 },
	[INSTR_VRI_V0IU]     = { V_8, I16_16, U4_32, 0, 0, 0 },
	[INSTR_VRI_V0U]	     = { V_8, U16_16, 0, 0, 0, 0 },
	[INSTR_VRI_V0UU2]    = { V_8, U16_16, U4_32, 0, 0, 0 },
	[INSTR_VRI_V0UUU]    = { V_8, U8_16, U8_24, U4_32, 0, 0 },
	[INSTR_VRI_VR0UU]    = { V_8, R_12, U8_28, U4_24, 0, 0 },
	[INSTR_VRI_VV0UU]    = { V_8, V_12, U8_28, U4_24, 0, 0 },
	[INSTR_VRI_VVUU]     = { V_8, V_12, U16_16, U4_32, 0, 0 },
	[INSTR_VRI_VVUUU]    = { V_8, V_12, U12_16, U4_32, U4_28, 0 },
	[INSTR_VRI_VVUUU2]   = { V_8, V_12, U8_28, U8_16, U4_24, 0 },
	[INSTR_VRI_VVV0U]    = { V_8, V_12, V_16, U8_24, 0, 0 },
	[INSTR_VRI_VVV0UU]   = { V_8, V_12, V_16, U8_24, U4_32, 0 },
	[INSTR_VRI_VVV0UU2]  = { V_8, V_12, V_16, U8_28, U4_24, 0 },
	[INSTR_VRI_VVV0UV]   = { V_8, V_12, V_16, V_32, U8_24, 0 },
	[INSTR_VRR_0V0U]     = { V_12, U16_20, 0, 0, 0, 0 },
	[INSTR_VRR_0VV0U]    = { V_12, V_16, U4_24, 0, 0, 0 },
	[INSTR_VRR_0VVU]     = { V_12, V_16, U16_20, 0, 0, 0 },
	[INSTR_VRR_RV0UU]    = { R_8, V_12, U4_24, U4_28, 0, 0 },
	[INSTR_VRR_VRR]	     = { V_8, R_12, R_16, 0, 0, 0 },
	[INSTR_VRR_VV]	     = { V_8, V_12, 0, 0, 0, 0 },
	[INSTR_VRR_VV0U]     = { V_8, V_12, U4_32, 0, 0, 0 },
	[INSTR_VRR_VV0U0U]   = { V_8, V_12, U4_32, U4_24, 0, 0 },
	[INSTR_VRR_VV0U2]    = { V_8, V_12, U4_24, 0, 0, 0 },
	[INSTR_VRR_VV0UU2]   = { V_8, V_12, U4_32, U4_28, 0, 0 },
	[INSTR_VRR_VV0UUU]   = { V_8, V_12, U4_32, U4_28, U4_24, 0 },
	[INSTR_VRR_VVV]	     = { V_8, V_12, V_16, 0, 0, 0 },
	[INSTR_VRR_VVV0U]    = { V_8, V_12, V_16, U4_32, 0, 0 },
	[INSTR_VRR_VVV0U0]   = { V_8, V_12, V_16, U4_24, 0, 0 },
	[INSTR_VRR_VVV0U0U]  = { V_8, V_12, V_16, U4_32, U4_24, 0 },
	[INSTR_VRR_VVV0UU]   = { V_8, V_12, V_16, U4_32, U4_28, 0 },
	[INSTR_VRR_VVV0UUU]  = { V_8, V_12, V_16, U4_32, U4_28, U4_24 },
	[INSTR_VRR_VVV0V]    = { V_8, V_12, V_16, V_32, 0, 0 },
	[INSTR_VRR_VVVU0UV]  = { V_8, V_12, V_16, V_32, U4_28, U4_20 },
	[INSTR_VRR_VVVU0V]   = { V_8, V_12, V_16, V_32, U4_20, 0 },
	[INSTR_VRR_VVVUU0V]  = { V_8, V_12, V_16, V_32, U4_20, U4_24 },
	[INSTR_VRS_RRDV]     = { V_32, R_12, D_20, B_16, 0, 0 },
	[INSTR_VRS_RVRDU]    = { R_8, V_12, D_20, B_16, U4_32, 0 },
	[INSTR_VRS_VRRD]     = { V_8, R_12, D_20, B_16, 0, 0 },
	[INSTR_VRS_VRRDU]    = { V_8, R_12, D_20, B_16, U4_32, 0 },
	[INSTR_VRS_VVRDU]    = { V_8, V_12, D_20, B_16, U4_32, 0 },
	[INSTR_VRV_VVXRDU]   = { V_8, D_20, VX_12, B_16, U4_32, 0 },
	[INSTR_VRX_VRRDU]    = { V_8, D_20, X_12, B_16, U4_32, 0 },
	[INSTR_VRX_VV]	     = { V_8, V_12, 0, 0, 0, 0 },
	[INSTR_VSI_URDV]     = { V_32, D_20, B_16, U8_8, 0, 0 },
};

static char long_insn_name[][7] = LONG_INSN_INITIALIZER;
static struct s390_insn opcode[] = OPCODE_TABLE_INITIALIZER;
static struct s390_opcode_offset opcode_offset[] = OPCODE_OFFSET_INITIALIZER;

/* Extracts an operand value from an instruction.  */
static unsigned int extract_operand(unsigned char *code,
				    const struct s390_operand *operand)
{
	unsigned char *cp;
	unsigned int val;
	int bits;

	/* Extract fragments of the operand byte for byte.  */
	cp = code + operand->shift / 8;
	bits = (operand->shift & 7) + operand->bits;
	val = 0;
	do {
		val <<= 8;
		val |= (unsigned int) *cp++;
		bits -= 8;
	} while (bits > 0);
	val >>= -bits;
	val &= ((1U << (operand->bits - 1)) << 1) - 1;

	/* Check for special long displacement case.  */
	if (operand->bits == 20 && operand->shift == 20)
		val = (val & 0xff) << 12 | (val & 0xfff00) >> 8;

	/* Check for register extensions bits for vector registers. */
	if (operand->flags & OPERAND_VR) {
		if (operand->shift == 8)
			val |= (code[4] & 8) << 1;
		else if (operand->shift == 12)
			val |= (code[4] & 4) << 2;
		else if (operand->shift == 16)
			val |= (code[4] & 2) << 3;
		else if (operand->shift == 32)
			val |= (code[4] & 1) << 4;
	}

	/* Sign extend value if the operand is signed or pc relative.  */
	if ((operand->flags & (OPERAND_SIGNED | OPERAND_PCREL)) &&
	    (val & (1U << (operand->bits - 1))))
		val |= (-1U << (operand->bits - 1)) << 1;

	/* Double value if the operand is pc relative.	*/
	if (operand->flags & OPERAND_PCREL)
		val <<= 1;

	/* Length x in an instructions has real length x + 1.  */
	if (operand->flags & OPERAND_LENGTH)
		val++;
	return val;
}

struct s390_insn *find_insn(unsigned char *code)
{
	struct s390_opcode_offset *entry;
	struct s390_insn *insn;
	unsigned char opfrag;
	int i;

	/* Search the opcode offset table to find an entry which
	 * matches the beginning of the opcode. If there is no match
	 * the last entry will be used, which is the default entry for
	 * unknown instructions as well as 1-byte opcode instructions.
	 */
	for (i = 0; i < ARRAY_SIZE(opcode_offset); i++) {
		entry = &opcode_offset[i];
		if (entry->opcode == code[0])
			break;
	}

	opfrag = *(code + entry->byte) & entry->mask;

	insn = &opcode[entry->offset];
	for (i = 0; i < entry->count; i++) {
		if (insn->opfrag == opfrag)
			return insn;
		insn++;
	}
	return NULL;
}

static int print_insn(char *buffer, unsigned char *code, unsigned long addr)
{
	struct s390_insn *insn;
	const unsigned char *ops;
	const struct s390_operand *operand;
	unsigned int value;
	char separator;
	char *ptr;
	int i;

	ptr = buffer;
	insn = find_insn(code);
	if (insn) {
		if (insn->zero == 0)
			ptr += sprintf(ptr, "%.7s\t",
				       long_insn_name[insn->offset]);
		else
			ptr += sprintf(ptr, "%.5s\t", insn->name);
		/* Extract the operands. */
		separator = 0;
		for (ops = formats[insn->format], i = 0;
		     *ops != 0 && i < 6; ops++, i++) {
			operand = operands + *ops;
			value = extract_operand(code, operand);
			if ((operand->flags & OPERAND_INDEX)  && value == 0)
				continue;
			if ((operand->flags & OPERAND_BASE) &&
			    value == 0 && separator == '(') {
				separator = ',';
				continue;
			}
			if (separator)
				ptr += sprintf(ptr, "%c", separator);
			if (operand->flags & OPERAND_GPR)
				ptr += sprintf(ptr, "%%r%u", value);
			else if (operand->flags & OPERAND_FPR)
				ptr += sprintf(ptr, "%%f%u", value);
			else if (operand->flags & OPERAND_AR)
				ptr += sprintf(ptr, "%%a%u", value);
			else if (operand->flags & OPERAND_CR)
				ptr += sprintf(ptr, "%%c%u", value);
			else if (operand->flags & OPERAND_VR)
				ptr += sprintf(ptr, "%%v%u", value);
			else if (operand->flags & OPERAND_PCREL) {
				void *pcrel = (void *)((int)value + addr);

				ptr += sprintf(ptr, "%px", pcrel);
			} else if (operand->flags & OPERAND_SIGNED)
				ptr += sprintf(ptr, "%i", (int)value);
			else
				ptr += sprintf(ptr, "%u", value);
			if (operand->flags & OPERAND_DISP)
				separator = '(';
			else if (operand->flags & OPERAND_BASE) {
				ptr += sprintf(ptr, ")");
				separator = ',';
			} else
				separator = ',';
		}
	} else
		ptr += sprintf(ptr, "unknown");
	return (int) (ptr - buffer);
}

static int copy_from_regs(struct pt_regs *regs, void *dst, void *src, int len)
{
	if (user_mode(regs)) {
		if (copy_from_user(dst, (char __user *)src, len))
			return -EFAULT;
	} else {
		if (copy_from_kernel_nofault(dst, src, len))
			return -EFAULT;
	}
	return 0;
}

void show_code(struct pt_regs *regs)
{
	char *mode = user_mode(regs) ? "User" : "Krnl";
	unsigned char code[64];
	char buffer[128], *ptr;
	unsigned long addr;
	int start, end, opsize, hops, i;

	/* Get a snapshot of the 64 bytes surrounding the fault address. */
	for (start = 32; start && regs->psw.addr >= 34 - start; start -= 2) {
		addr = regs->psw.addr - 34 + start;
		if (copy_from_regs(regs, code + start - 2, (void *)addr, 2))
			break;
	}
	for (end = 32; end < 64; end += 2) {
		addr = regs->psw.addr + end - 32;
		if (copy_from_regs(regs, code + end, (void *)addr, 2))
			break;
	}
	/* Code snapshot usable ? */
	if ((regs->psw.addr & 1) || start >= end) {
		printk("%s Code: Bad PSW.\n", mode);
		return;
	}
	/* Find a starting point for the disassembly. */
	while (start < 32) {
		for (i = 0, hops = 0; start + i < 32 && hops < 3; hops++) {
			if (!find_insn(code + start + i))
				break;
			i += insn_length(code[start + i]);
		}
		if (start + i == 32)
			/* Looks good, sequence ends at PSW. */
			break;
		start += 2;
	}
	/* Decode the instructions. */
	ptr = buffer;
	ptr += sprintf(ptr, "%s Code:", mode);
	hops = 0;
	while (start < end && hops < 8) {
		opsize = insn_length(code[start]);
		if  (start + opsize == 32)
			*ptr++ = '#';
		else if (start == 32)
			*ptr++ = '>';
		else
			*ptr++ = ' ';
		addr = regs->psw.addr + start - 32;
		ptr += sprintf(ptr, "%px: ", (void *)addr);
		if (start + opsize >= end)
			break;
		for (i = 0; i < opsize; i++)
			ptr += sprintf(ptr, "%02x", code[start + i]);
		*ptr++ = '\t';
		if (i < 6)
			*ptr++ = '\t';
		ptr += print_insn(ptr, code + start, addr);
		start += opsize;
		pr_cont("%s", buffer);
		ptr = buffer;
		ptr += sprintf(ptr, "\n          ");
		hops++;
	}
	pr_cont("\n");
}

void print_fn_code(unsigned char *code, unsigned long len)
{
	char buffer[128], *ptr;
	int opsize, i;

	while (len) {
		ptr = buffer;
		opsize = insn_length(*code);
		if (opsize > len)
			break;
		ptr += sprintf(ptr, "%px: ", code);
		for (i = 0; i < opsize; i++)
			ptr += sprintf(ptr, "%02x", code[i]);
		*ptr++ = '\t';
		if (i < 4)
			*ptr++ = '\t';
		ptr += print_insn(ptr, code, (unsigned long) code);
		*ptr++ = '\n';
		*ptr++ = 0;
		printk("%s", buffer);
		code += opsize;
		len -= opsize;
	}
}
