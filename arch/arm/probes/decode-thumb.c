/*
 * arch/arm/probes/decode-thumb.c
 *
 * Copyright (C) 2011 Jon Medhurst <tixy@yxit.co.uk>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "decode.h"
#include "decode-thumb.h"


static const union decode_item t32_table_1110_100x_x0xx[] = {
	/* Load/store multiple instructions */

	/* Rn is PC		1110 100x x0xx 1111 xxxx xxxx xxxx xxxx */
	DECODE_REJECT	(0xfe4f0000, 0xe80f0000),

	/* SRS			1110 1000 00x0 xxxx xxxx xxxx xxxx xxxx */
	/* RFE			1110 1000 00x1 xxxx xxxx xxxx xxxx xxxx */
	DECODE_REJECT	(0xffc00000, 0xe8000000),
	/* SRS			1110 1001 10x0 xxxx xxxx xxxx xxxx xxxx */
	/* RFE			1110 1001 10x1 xxxx xxxx xxxx xxxx xxxx */
	DECODE_REJECT	(0xffc00000, 0xe9800000),

	/* STM Rn, {...pc}	1110 100x x0x0 xxxx 1xxx xxxx xxxx xxxx */
	DECODE_REJECT	(0xfe508000, 0xe8008000),
	/* LDM Rn, {...lr,pc}	1110 100x x0x1 xxxx 11xx xxxx xxxx xxxx */
	DECODE_REJECT	(0xfe50c000, 0xe810c000),
	/* LDM/STM Rn, {...sp}	1110 100x x0xx xxxx xx1x xxxx xxxx xxxx */
	DECODE_REJECT	(0xfe402000, 0xe8002000),

	/* STMIA		1110 1000 10x0 xxxx xxxx xxxx xxxx xxxx */
	/* LDMIA		1110 1000 10x1 xxxx xxxx xxxx xxxx xxxx */
	/* STMDB		1110 1001 00x0 xxxx xxxx xxxx xxxx xxxx */
	/* LDMDB		1110 1001 00x1 xxxx xxxx xxxx xxxx xxxx */
	DECODE_CUSTOM	(0xfe400000, 0xe8000000, PROBES_T32_LDMSTM),

	DECODE_END
};

static const union decode_item t32_table_1110_100x_x1xx[] = {
	/* Load/store dual, load/store exclusive, table branch */

	/* STRD (immediate)	1110 1000 x110 xxxx xxxx xxxx xxxx xxxx */
	/* LDRD (immediate)	1110 1000 x111 xxxx xxxx xxxx xxxx xxxx */
	DECODE_OR	(0xff600000, 0xe8600000),
	/* STRD (immediate)	1110 1001 x1x0 xxxx xxxx xxxx xxxx xxxx */
	/* LDRD (immediate)	1110 1001 x1x1 xxxx xxxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0xff400000, 0xe9400000, PROBES_T32_LDRDSTRD,
						 REGS(NOPCWB, NOSPPC, NOSPPC, 0, 0)),

	/* TBB			1110 1000 1101 xxxx xxxx xxxx 0000 xxxx */
	/* TBH			1110 1000 1101 xxxx xxxx xxxx 0001 xxxx */
	DECODE_SIMULATEX(0xfff000e0, 0xe8d00000, PROBES_T32_TABLE_BRANCH,
						 REGS(NOSP, 0, 0, 0, NOSPPC)),

	/* STREX		1110 1000 0100 xxxx xxxx xxxx xxxx xxxx */
	/* LDREX		1110 1000 0101 xxxx xxxx xxxx xxxx xxxx */
	/* STREXB		1110 1000 1100 xxxx xxxx xxxx 0100 xxxx */
	/* STREXH		1110 1000 1100 xxxx xxxx xxxx 0101 xxxx */
	/* STREXD		1110 1000 1100 xxxx xxxx xxxx 0111 xxxx */
	/* LDREXB		1110 1000 1101 xxxx xxxx xxxx 0100 xxxx */
	/* LDREXH		1110 1000 1101 xxxx xxxx xxxx 0101 xxxx */
	/* LDREXD		1110 1000 1101 xxxx xxxx xxxx 0111 xxxx */
	/* And unallocated instructions...				*/
	DECODE_END
};

static const union decode_item t32_table_1110_101x[] = {
	/* Data-processing (shifted register)				*/

	/* TST			1110 1010 0001 xxxx xxxx 1111 xxxx xxxx */
	/* TEQ			1110 1010 1001 xxxx xxxx 1111 xxxx xxxx */
	DECODE_EMULATEX	(0xff700f00, 0xea100f00, PROBES_T32_TST,
						 REGS(NOSPPC, 0, 0, 0, NOSPPC)),

	/* CMN			1110 1011 0001 xxxx xxxx 1111 xxxx xxxx */
	DECODE_OR	(0xfff00f00, 0xeb100f00),
	/* CMP			1110 1011 1011 xxxx xxxx 1111 xxxx xxxx */
	DECODE_EMULATEX	(0xfff00f00, 0xebb00f00, PROBES_T32_TST,
						 REGS(NOPC, 0, 0, 0, NOSPPC)),

	/* MOV			1110 1010 010x 1111 xxxx xxxx xxxx xxxx */
	/* MVN			1110 1010 011x 1111 xxxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0xffcf0000, 0xea4f0000, PROBES_T32_MOV,
						 REGS(0, 0, NOSPPC, 0, NOSPPC)),

	/* ???			1110 1010 101x xxxx xxxx xxxx xxxx xxxx */
	/* ???			1110 1010 111x xxxx xxxx xxxx xxxx xxxx */
	DECODE_REJECT	(0xffa00000, 0xeaa00000),
	/* ???			1110 1011 001x xxxx xxxx xxxx xxxx xxxx */
	DECODE_REJECT	(0xffe00000, 0xeb200000),
	/* ???			1110 1011 100x xxxx xxxx xxxx xxxx xxxx */
	DECODE_REJECT	(0xffe00000, 0xeb800000),
	/* ???			1110 1011 111x xxxx xxxx xxxx xxxx xxxx */
	DECODE_REJECT	(0xffe00000, 0xebe00000),

	/* ADD/SUB SP, SP, Rm, LSL #0..3				*/
	/*			1110 1011 x0xx 1101 x000 1101 xx00 xxxx */
	DECODE_EMULATEX	(0xff4f7f30, 0xeb0d0d00, PROBES_T32_ADDSUB,
						 REGS(SP, 0, SP, 0, NOSPPC)),

	/* ADD/SUB SP, SP, Rm, shift					*/
	/*			1110 1011 x0xx 1101 xxxx 1101 xxxx xxxx */
	DECODE_REJECT	(0xff4f0f00, 0xeb0d0d00),

	/* ADD/SUB Rd, SP, Rm, shift					*/
	/*			1110 1011 x0xx 1101 xxxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0xff4f0000, 0xeb0d0000, PROBES_T32_ADDSUB,
						 REGS(SP, 0, NOPC, 0, NOSPPC)),

	/* AND			1110 1010 000x xxxx xxxx xxxx xxxx xxxx */
	/* BIC			1110 1010 001x xxxx xxxx xxxx xxxx xxxx */
	/* ORR			1110 1010 010x xxxx xxxx xxxx xxxx xxxx */
	/* ORN			1110 1010 011x xxxx xxxx xxxx xxxx xxxx */
	/* EOR			1110 1010 100x xxxx xxxx xxxx xxxx xxxx */
	/* PKH			1110 1010 110x xxxx xxxx xxxx xxxx xxxx */
	/* ADD			1110 1011 000x xxxx xxxx xxxx xxxx xxxx */
	/* ADC			1110 1011 010x xxxx xxxx xxxx xxxx xxxx */
	/* SBC			1110 1011 011x xxxx xxxx xxxx xxxx xxxx */
	/* SUB			1110 1011 101x xxxx xxxx xxxx xxxx xxxx */
	/* RSB			1110 1011 110x xxxx xxxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0xfe000000, 0xea000000, PROBES_T32_LOGICAL,
						 REGS(NOSPPC, 0, NOSPPC, 0, NOSPPC)),

	DECODE_END
};

static const union decode_item t32_table_1111_0x0x___0[] = {
	/* Data-processing (modified immediate)				*/

	/* TST			1111 0x00 0001 xxxx 0xxx 1111 xxxx xxxx */
	/* TEQ			1111 0x00 1001 xxxx 0xxx 1111 xxxx xxxx */
	DECODE_EMULATEX	(0xfb708f00, 0xf0100f00, PROBES_T32_TST,
						 REGS(NOSPPC, 0, 0, 0, 0)),

	/* CMN			1111 0x01 0001 xxxx 0xxx 1111 xxxx xxxx */
	DECODE_OR	(0xfbf08f00, 0xf1100f00),
	/* CMP			1111 0x01 1011 xxxx 0xxx 1111 xxxx xxxx */
	DECODE_EMULATEX	(0xfbf08f00, 0xf1b00f00, PROBES_T32_CMP,
						 REGS(NOPC, 0, 0, 0, 0)),

	/* MOV			1111 0x00 010x 1111 0xxx xxxx xxxx xxxx */
	/* MVN			1111 0x00 011x 1111 0xxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0xfbcf8000, 0xf04f0000, PROBES_T32_MOV,
						 REGS(0, 0, NOSPPC, 0, 0)),

	/* ???			1111 0x00 101x xxxx 0xxx xxxx xxxx xxxx */
	DECODE_REJECT	(0xfbe08000, 0xf0a00000),
	/* ???			1111 0x00 110x xxxx 0xxx xxxx xxxx xxxx */
	/* ???			1111 0x00 111x xxxx 0xxx xxxx xxxx xxxx */
	DECODE_REJECT	(0xfbc08000, 0xf0c00000),
	/* ???			1111 0x01 001x xxxx 0xxx xxxx xxxx xxxx */
	DECODE_REJECT	(0xfbe08000, 0xf1200000),
	/* ???			1111 0x01 100x xxxx 0xxx xxxx xxxx xxxx */
	DECODE_REJECT	(0xfbe08000, 0xf1800000),
	/* ???			1111 0x01 111x xxxx 0xxx xxxx xxxx xxxx */
	DECODE_REJECT	(0xfbe08000, 0xf1e00000),

	/* ADD Rd, SP, #imm	1111 0x01 000x 1101 0xxx xxxx xxxx xxxx */
	/* SUB Rd, SP, #imm	1111 0x01 101x 1101 0xxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0xfb4f8000, 0xf10d0000, PROBES_T32_ADDSUB,
						 REGS(SP, 0, NOPC, 0, 0)),

	/* AND			1111 0x00 000x xxxx 0xxx xxxx xxxx xxxx */
	/* BIC			1111 0x00 001x xxxx 0xxx xxxx xxxx xxxx */
	/* ORR			1111 0x00 010x xxxx 0xxx xxxx xxxx xxxx */
	/* ORN			1111 0x00 011x xxxx 0xxx xxxx xxxx xxxx */
	/* EOR			1111 0x00 100x xxxx 0xxx xxxx xxxx xxxx */
	/* ADD			1111 0x01 000x xxxx 0xxx xxxx xxxx xxxx */
	/* ADC			1111 0x01 010x xxxx 0xxx xxxx xxxx xxxx */
	/* SBC			1111 0x01 011x xxxx 0xxx xxxx xxxx xxxx */
	/* SUB			1111 0x01 101x xxxx 0xxx xxxx xxxx xxxx */
	/* RSB			1111 0x01 110x xxxx 0xxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0xfa008000, 0xf0000000, PROBES_T32_LOGICAL,
						 REGS(NOSPPC, 0, NOSPPC, 0, 0)),

	DECODE_END
};

static const union decode_item t32_table_1111_0x1x___0[] = {
	/* Data-processing (plain binary immediate)			*/

	/* ADDW Rd, PC, #imm	1111 0x10 0000 1111 0xxx xxxx xxxx xxxx */
	DECODE_OR	(0xfbff8000, 0xf20f0000),
	/* SUBW	Rd, PC, #imm	1111 0x10 1010 1111 0xxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0xfbff8000, 0xf2af0000, PROBES_T32_ADDWSUBW_PC,
						 REGS(PC, 0, NOSPPC, 0, 0)),

	/* ADDW SP, SP, #imm	1111 0x10 0000 1101 0xxx 1101 xxxx xxxx */
	DECODE_OR	(0xfbff8f00, 0xf20d0d00),
	/* SUBW	SP, SP, #imm	1111 0x10 1010 1101 0xxx 1101 xxxx xxxx */
	DECODE_EMULATEX	(0xfbff8f00, 0xf2ad0d00, PROBES_T32_ADDWSUBW,
						 REGS(SP, 0, SP, 0, 0)),

	/* ADDW			1111 0x10 0000 xxxx 0xxx xxxx xxxx xxxx */
	DECODE_OR	(0xfbf08000, 0xf2000000),
	/* SUBW			1111 0x10 1010 xxxx 0xxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0xfbf08000, 0xf2a00000, PROBES_T32_ADDWSUBW,
						 REGS(NOPCX, 0, NOSPPC, 0, 0)),

	/* MOVW			1111 0x10 0100 xxxx 0xxx xxxx xxxx xxxx */
	/* MOVT			1111 0x10 1100 xxxx 0xxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0xfb708000, 0xf2400000, PROBES_T32_MOVW,
						 REGS(0, 0, NOSPPC, 0, 0)),

	/* SSAT16		1111 0x11 0010 xxxx 0000 xxxx 00xx xxxx */
	/* SSAT			1111 0x11 00x0 xxxx 0xxx xxxx xxxx xxxx */
	/* USAT16		1111 0x11 1010 xxxx 0000 xxxx 00xx xxxx */
	/* USAT			1111 0x11 10x0 xxxx 0xxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0xfb508000, 0xf3000000, PROBES_T32_SAT,
						 REGS(NOSPPC, 0, NOSPPC, 0, 0)),

	/* SFBX			1111 0x11 0100 xxxx 0xxx xxxx xxxx xxxx */
	/* UFBX			1111 0x11 1100 xxxx 0xxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0xfb708000, 0xf3400000, PROBES_T32_BITFIELD,
						 REGS(NOSPPC, 0, NOSPPC, 0, 0)),

	/* BFC			1111 0x11 0110 1111 0xxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0xfbff8000, 0xf36f0000, PROBES_T32_BITFIELD,
						 REGS(0, 0, NOSPPC, 0, 0)),

	/* BFI			1111 0x11 0110 xxxx 0xxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0xfbf08000, 0xf3600000, PROBES_T32_BITFIELD,
						 REGS(NOSPPCX, 0, NOSPPC, 0, 0)),

	DECODE_END
};

static const union decode_item t32_table_1111_0xxx___1[] = {
	/* Branches and miscellaneous control				*/

	/* YIELD		1111 0011 1010 xxxx 10x0 x000 0000 0001 */
	DECODE_OR	(0xfff0d7ff, 0xf3a08001),
	/* SEV			1111 0011 1010 xxxx 10x0 x000 0000 0100 */
	DECODE_EMULATE	(0xfff0d7ff, 0xf3a08004, PROBES_T32_SEV),
	/* NOP			1111 0011 1010 xxxx 10x0 x000 0000 0000 */
	/* WFE			1111 0011 1010 xxxx 10x0 x000 0000 0010 */
	/* WFI			1111 0011 1010 xxxx 10x0 x000 0000 0011 */
	DECODE_SIMULATE	(0xfff0d7fc, 0xf3a08000, PROBES_T32_WFE),

	/* MRS Rd, CPSR		1111 0011 1110 xxxx 10x0 xxxx xxxx xxxx */
	DECODE_SIMULATEX(0xfff0d000, 0xf3e08000, PROBES_T32_MRS,
						 REGS(0, 0, NOSPPC, 0, 0)),

	/*
	 * Unsupported instructions
	 *			1111 0x11 1xxx xxxx 10x0 xxxx xxxx xxxx
	 *
	 * MSR			1111 0011 100x xxxx 10x0 xxxx xxxx xxxx
	 * DBG hint		1111 0011 1010 xxxx 10x0 x000 1111 xxxx
	 * Unallocated hints	1111 0011 1010 xxxx 10x0 x000 xxxx xxxx
	 * CPS			1111 0011 1010 xxxx 10x0 xxxx xxxx xxxx
	 * CLREX/DSB/DMB/ISB	1111 0011 1011 xxxx 10x0 xxxx xxxx xxxx
	 * BXJ			1111 0011 1100 xxxx 10x0 xxxx xxxx xxxx
	 * SUBS PC,LR,#<imm8>	1111 0011 1101 xxxx 10x0 xxxx xxxx xxxx
	 * MRS Rd, SPSR		1111 0011 1111 xxxx 10x0 xxxx xxxx xxxx
	 * SMC			1111 0111 1111 xxxx 1000 xxxx xxxx xxxx
	 * UNDEFINED		1111 0111 1111 xxxx 1010 xxxx xxxx xxxx
	 * ???			1111 0111 1xxx xxxx 1010 xxxx xxxx xxxx
	 */
	DECODE_REJECT	(0xfb80d000, 0xf3808000),

	/* Bcc			1111 0xxx xxxx xxxx 10x0 xxxx xxxx xxxx */
	DECODE_CUSTOM	(0xf800d000, 0xf0008000, PROBES_T32_BRANCH_COND),

	/* BLX			1111 0xxx xxxx xxxx 11x0 xxxx xxxx xxx0 */
	DECODE_OR	(0xf800d001, 0xf000c000),
	/* B			1111 0xxx xxxx xxxx 10x1 xxxx xxxx xxxx */
	/* BL			1111 0xxx xxxx xxxx 11x1 xxxx xxxx xxxx */
	DECODE_SIMULATE	(0xf8009000, 0xf0009000, PROBES_T32_BRANCH),

	DECODE_END
};

static const union decode_item t32_table_1111_100x_x0x1__1111[] = {
	/* Memory hints							*/

	/* PLD (literal)	1111 1000 x001 1111 1111 xxxx xxxx xxxx */
	/* PLI (literal)	1111 1001 x001 1111 1111 xxxx xxxx xxxx */
	DECODE_SIMULATE	(0xfe7ff000, 0xf81ff000, PROBES_T32_PLDI),

	/* PLD{W} (immediate)	1111 1000 10x1 xxxx 1111 xxxx xxxx xxxx */
	DECODE_OR	(0xffd0f000, 0xf890f000),
	/* PLD{W} (immediate)	1111 1000 00x1 xxxx 1111 1100 xxxx xxxx */
	DECODE_OR	(0xffd0ff00, 0xf810fc00),
	/* PLI (immediate)	1111 1001 1001 xxxx 1111 xxxx xxxx xxxx */
	DECODE_OR	(0xfff0f000, 0xf990f000),
	/* PLI (immediate)	1111 1001 0001 xxxx 1111 1100 xxxx xxxx */
	DECODE_SIMULATEX(0xfff0ff00, 0xf910fc00, PROBES_T32_PLDI,
						 REGS(NOPCX, 0, 0, 0, 0)),

	/* PLD{W} (register)	1111 1000 00x1 xxxx 1111 0000 00xx xxxx */
	DECODE_OR	(0xffd0ffc0, 0xf810f000),
	/* PLI (register)	1111 1001 0001 xxxx 1111 0000 00xx xxxx */
	DECODE_SIMULATEX(0xfff0ffc0, 0xf910f000, PROBES_T32_PLDI,
						 REGS(NOPCX, 0, 0, 0, NOSPPC)),

	/* Other unallocated instructions...				*/
	DECODE_END
};

static const union decode_item t32_table_1111_100x[] = {
	/* Store/Load single data item					*/

	/* ???			1111 100x x11x xxxx xxxx xxxx xxxx xxxx */
	DECODE_REJECT	(0xfe600000, 0xf8600000),

	/* ???			1111 1001 0101 xxxx xxxx xxxx xxxx xxxx */
	DECODE_REJECT	(0xfff00000, 0xf9500000),

	/* ???			1111 100x 0xxx xxxx xxxx 10x0 xxxx xxxx */
	DECODE_REJECT	(0xfe800d00, 0xf8000800),

	/* STRBT		1111 1000 0000 xxxx xxxx 1110 xxxx xxxx */
	/* STRHT		1111 1000 0010 xxxx xxxx 1110 xxxx xxxx */
	/* STRT			1111 1000 0100 xxxx xxxx 1110 xxxx xxxx */
	/* LDRBT		1111 1000 0001 xxxx xxxx 1110 xxxx xxxx */
	/* LDRSBT		1111 1001 0001 xxxx xxxx 1110 xxxx xxxx */
	/* LDRHT		1111 1000 0011 xxxx xxxx 1110 xxxx xxxx */
	/* LDRSHT		1111 1001 0011 xxxx xxxx 1110 xxxx xxxx */
	/* LDRT			1111 1000 0101 xxxx xxxx 1110 xxxx xxxx */
	DECODE_REJECT	(0xfe800f00, 0xf8000e00),

	/* STR{,B,H} Rn,[PC...]	1111 1000 xxx0 1111 xxxx xxxx xxxx xxxx */
	DECODE_REJECT	(0xff1f0000, 0xf80f0000),

	/* STR{,B,H} PC,[Rn...]	1111 1000 xxx0 xxxx 1111 xxxx xxxx xxxx */
	DECODE_REJECT	(0xff10f000, 0xf800f000),

	/* LDR (literal)	1111 1000 x101 1111 xxxx xxxx xxxx xxxx */
	DECODE_SIMULATEX(0xff7f0000, 0xf85f0000, PROBES_T32_LDR_LIT,
						 REGS(PC, ANY, 0, 0, 0)),

	/* STR (immediate)	1111 1000 0100 xxxx xxxx 1xxx xxxx xxxx */
	/* LDR (immediate)	1111 1000 0101 xxxx xxxx 1xxx xxxx xxxx */
	DECODE_OR	(0xffe00800, 0xf8400800),
	/* STR (immediate)	1111 1000 1100 xxxx xxxx xxxx xxxx xxxx */
	/* LDR (immediate)	1111 1000 1101 xxxx xxxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0xffe00000, 0xf8c00000, PROBES_T32_LDRSTR,
						 REGS(NOPCX, ANY, 0, 0, 0)),

	/* STR (register)	1111 1000 0100 xxxx xxxx 0000 00xx xxxx */
	/* LDR (register)	1111 1000 0101 xxxx xxxx 0000 00xx xxxx */
	DECODE_EMULATEX	(0xffe00fc0, 0xf8400000, PROBES_T32_LDRSTR,
						 REGS(NOPCX, ANY, 0, 0, NOSPPC)),

	/* LDRB (literal)	1111 1000 x001 1111 xxxx xxxx xxxx xxxx */
	/* LDRSB (literal)	1111 1001 x001 1111 xxxx xxxx xxxx xxxx */
	/* LDRH (literal)	1111 1000 x011 1111 xxxx xxxx xxxx xxxx */
	/* LDRSH (literal)	1111 1001 x011 1111 xxxx xxxx xxxx xxxx */
	DECODE_SIMULATEX(0xfe5f0000, 0xf81f0000, PROBES_T32_LDR_LIT,
						 REGS(PC, NOSPPCX, 0, 0, 0)),

	/* STRB (immediate)	1111 1000 0000 xxxx xxxx 1xxx xxxx xxxx */
	/* STRH (immediate)	1111 1000 0010 xxxx xxxx 1xxx xxxx xxxx */
	/* LDRB (immediate)	1111 1000 0001 xxxx xxxx 1xxx xxxx xxxx */
	/* LDRSB (immediate)	1111 1001 0001 xxxx xxxx 1xxx xxxx xxxx */
	/* LDRH (immediate)	1111 1000 0011 xxxx xxxx 1xxx xxxx xxxx */
	/* LDRSH (immediate)	1111 1001 0011 xxxx xxxx 1xxx xxxx xxxx */
	DECODE_OR	(0xfec00800, 0xf8000800),
	/* STRB (immediate)	1111 1000 1000 xxxx xxxx xxxx xxxx xxxx */
	/* STRH (immediate)	1111 1000 1010 xxxx xxxx xxxx xxxx xxxx */
	/* LDRB (immediate)	1111 1000 1001 xxxx xxxx xxxx xxxx xxxx */
	/* LDRSB (immediate)	1111 1001 1001 xxxx xxxx xxxx xxxx xxxx */
	/* LDRH (immediate)	1111 1000 1011 xxxx xxxx xxxx xxxx xxxx */
	/* LDRSH (immediate)	1111 1001 1011 xxxx xxxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0xfec00000, 0xf8800000, PROBES_T32_LDRSTR,
						 REGS(NOPCX, NOSPPCX, 0, 0, 0)),

	/* STRB (register)	1111 1000 0000 xxxx xxxx 0000 00xx xxxx */
	/* STRH (register)	1111 1000 0010 xxxx xxxx 0000 00xx xxxx */
	/* LDRB (register)	1111 1000 0001 xxxx xxxx 0000 00xx xxxx */
	/* LDRSB (register)	1111 1001 0001 xxxx xxxx 0000 00xx xxxx */
	/* LDRH (register)	1111 1000 0011 xxxx xxxx 0000 00xx xxxx */
	/* LDRSH (register)	1111 1001 0011 xxxx xxxx 0000 00xx xxxx */
	DECODE_EMULATEX	(0xfe800fc0, 0xf8000000, PROBES_T32_LDRSTR,
						 REGS(NOPCX, NOSPPCX, 0, 0, NOSPPC)),

	/* Other unallocated instructions...				*/
	DECODE_END
};

static const union decode_item t32_table_1111_1010___1111[] = {
	/* Data-processing (register)					*/

	/* ???			1111 1010 011x xxxx 1111 xxxx 1xxx xxxx */
	DECODE_REJECT	(0xffe0f080, 0xfa60f080),

	/* SXTH			1111 1010 0000 1111 1111 xxxx 1xxx xxxx */
	/* UXTH			1111 1010 0001 1111 1111 xxxx 1xxx xxxx */
	/* SXTB16		1111 1010 0010 1111 1111 xxxx 1xxx xxxx */
	/* UXTB16		1111 1010 0011 1111 1111 xxxx 1xxx xxxx */
	/* SXTB			1111 1010 0100 1111 1111 xxxx 1xxx xxxx */
	/* UXTB			1111 1010 0101 1111 1111 xxxx 1xxx xxxx */
	DECODE_EMULATEX	(0xff8ff080, 0xfa0ff080, PROBES_T32_SIGN_EXTEND,
						 REGS(0, 0, NOSPPC, 0, NOSPPC)),


	/* ???			1111 1010 1xxx xxxx 1111 xxxx 0x11 xxxx */
	DECODE_REJECT	(0xff80f0b0, 0xfa80f030),
	/* ???			1111 1010 1x11 xxxx 1111 xxxx 0xxx xxxx */
	DECODE_REJECT	(0xffb0f080, 0xfab0f000),

	/* SADD16		1111 1010 1001 xxxx 1111 xxxx 0000 xxxx */
	/* SASX			1111 1010 1010 xxxx 1111 xxxx 0000 xxxx */
	/* SSAX			1111 1010 1110 xxxx 1111 xxxx 0000 xxxx */
	/* SSUB16		1111 1010 1101 xxxx 1111 xxxx 0000 xxxx */
	/* SADD8		1111 1010 1000 xxxx 1111 xxxx 0000 xxxx */
	/* SSUB8		1111 1010 1100 xxxx 1111 xxxx 0000 xxxx */

	/* QADD16		1111 1010 1001 xxxx 1111 xxxx 0001 xxxx */
	/* QASX			1111 1010 1010 xxxx 1111 xxxx 0001 xxxx */
	/* QSAX			1111 1010 1110 xxxx 1111 xxxx 0001 xxxx */
	/* QSUB16		1111 1010 1101 xxxx 1111 xxxx 0001 xxxx */
	/* QADD8		1111 1010 1000 xxxx 1111 xxxx 0001 xxxx */
	/* QSUB8		1111 1010 1100 xxxx 1111 xxxx 0001 xxxx */

	/* SHADD16		1111 1010 1001 xxxx 1111 xxxx 0010 xxxx */
	/* SHASX		1111 1010 1010 xxxx 1111 xxxx 0010 xxxx */
	/* SHSAX		1111 1010 1110 xxxx 1111 xxxx 0010 xxxx */
	/* SHSUB16		1111 1010 1101 xxxx 1111 xxxx 0010 xxxx */
	/* SHADD8		1111 1010 1000 xxxx 1111 xxxx 0010 xxxx */
	/* SHSUB8		1111 1010 1100 xxxx 1111 xxxx 0010 xxxx */

	/* UADD16		1111 1010 1001 xxxx 1111 xxxx 0100 xxxx */
	/* UASX			1111 1010 1010 xxxx 1111 xxxx 0100 xxxx */
	/* USAX			1111 1010 1110 xxxx 1111 xxxx 0100 xxxx */
	/* USUB16		1111 1010 1101 xxxx 1111 xxxx 0100 xxxx */
	/* UADD8		1111 1010 1000 xxxx 1111 xxxx 0100 xxxx */
	/* USUB8		1111 1010 1100 xxxx 1111 xxxx 0100 xxxx */

	/* UQADD16		1111 1010 1001 xxxx 1111 xxxx 0101 xxxx */
	/* UQASX		1111 1010 1010 xxxx 1111 xxxx 0101 xxxx */
	/* UQSAX		1111 1010 1110 xxxx 1111 xxxx 0101 xxxx */
	/* UQSUB16		1111 1010 1101 xxxx 1111 xxxx 0101 xxxx */
	/* UQADD8		1111 1010 1000 xxxx 1111 xxxx 0101 xxxx */
	/* UQSUB8		1111 1010 1100 xxxx 1111 xxxx 0101 xxxx */

	/* UHADD16		1111 1010 1001 xxxx 1111 xxxx 0110 xxxx */
	/* UHASX		1111 1010 1010 xxxx 1111 xxxx 0110 xxxx */
	/* UHSAX		1111 1010 1110 xxxx 1111 xxxx 0110 xxxx */
	/* UHSUB16		1111 1010 1101 xxxx 1111 xxxx 0110 xxxx */
	/* UHADD8		1111 1010 1000 xxxx 1111 xxxx 0110 xxxx */
	/* UHSUB8		1111 1010 1100 xxxx 1111 xxxx 0110 xxxx */
	DECODE_OR	(0xff80f080, 0xfa80f000),

	/* SXTAH		1111 1010 0000 xxxx 1111 xxxx 1xxx xxxx */
	/* UXTAH		1111 1010 0001 xxxx 1111 xxxx 1xxx xxxx */
	/* SXTAB16		1111 1010 0010 xxxx 1111 xxxx 1xxx xxxx */
	/* UXTAB16		1111 1010 0011 xxxx 1111 xxxx 1xxx xxxx */
	/* SXTAB		1111 1010 0100 xxxx 1111 xxxx 1xxx xxxx */
	/* UXTAB		1111 1010 0101 xxxx 1111 xxxx 1xxx xxxx */
	DECODE_OR	(0xff80f080, 0xfa00f080),

	/* QADD			1111 1010 1000 xxxx 1111 xxxx 1000 xxxx */
	/* QDADD		1111 1010 1000 xxxx 1111 xxxx 1001 xxxx */
	/* QSUB			1111 1010 1000 xxxx 1111 xxxx 1010 xxxx */
	/* QDSUB		1111 1010 1000 xxxx 1111 xxxx 1011 xxxx */
	DECODE_OR	(0xfff0f0c0, 0xfa80f080),

	/* SEL			1111 1010 1010 xxxx 1111 xxxx 1000 xxxx */
	DECODE_OR	(0xfff0f0f0, 0xfaa0f080),

	/* LSL			1111 1010 000x xxxx 1111 xxxx 0000 xxxx */
	/* LSR			1111 1010 001x xxxx 1111 xxxx 0000 xxxx */
	/* ASR			1111 1010 010x xxxx 1111 xxxx 0000 xxxx */
	/* ROR			1111 1010 011x xxxx 1111 xxxx 0000 xxxx */
	DECODE_EMULATEX	(0xff80f0f0, 0xfa00f000, PROBES_T32_MEDIA,
						 REGS(NOSPPC, 0, NOSPPC, 0, NOSPPC)),

	/* CLZ			1111 1010 1010 xxxx 1111 xxxx 1000 xxxx */
	DECODE_OR	(0xfff0f0f0, 0xfab0f080),

	/* REV			1111 1010 1001 xxxx 1111 xxxx 1000 xxxx */
	/* REV16		1111 1010 1001 xxxx 1111 xxxx 1001 xxxx */
	/* RBIT			1111 1010 1001 xxxx 1111 xxxx 1010 xxxx */
	/* REVSH		1111 1010 1001 xxxx 1111 xxxx 1011 xxxx */
	DECODE_EMULATEX	(0xfff0f0c0, 0xfa90f080, PROBES_T32_REVERSE,
						 REGS(NOSPPC, 0, NOSPPC, 0, SAMEAS16)),

	/* Other unallocated instructions...				*/
	DECODE_END
};

static const union decode_item t32_table_1111_1011_0[] = {
	/* Multiply, multiply accumulate, and absolute difference	*/

	/* ???			1111 1011 0000 xxxx 1111 xxxx 0001 xxxx */
	DECODE_REJECT	(0xfff0f0f0, 0xfb00f010),
	/* ???			1111 1011 0111 xxxx 1111 xxxx 0001 xxxx */
	DECODE_REJECT	(0xfff0f0f0, 0xfb70f010),

	/* SMULxy		1111 1011 0001 xxxx 1111 xxxx 00xx xxxx */
	DECODE_OR	(0xfff0f0c0, 0xfb10f000),
	/* MUL			1111 1011 0000 xxxx 1111 xxxx 0000 xxxx */
	/* SMUAD{X}		1111 1011 0010 xxxx 1111 xxxx 000x xxxx */
	/* SMULWy		1111 1011 0011 xxxx 1111 xxxx 000x xxxx */
	/* SMUSD{X}		1111 1011 0100 xxxx 1111 xxxx 000x xxxx */
	/* SMMUL{R}		1111 1011 0101 xxxx 1111 xxxx 000x xxxx */
	/* USAD8		1111 1011 0111 xxxx 1111 xxxx 0000 xxxx */
	DECODE_EMULATEX	(0xff80f0e0, 0xfb00f000, PROBES_T32_MUL_ADD,
						 REGS(NOSPPC, 0, NOSPPC, 0, NOSPPC)),

	/* ???			1111 1011 0111 xxxx xxxx xxxx 0001 xxxx */
	DECODE_REJECT	(0xfff000f0, 0xfb700010),

	/* SMLAxy		1111 1011 0001 xxxx xxxx xxxx 00xx xxxx */
	DECODE_OR	(0xfff000c0, 0xfb100000),
	/* MLA			1111 1011 0000 xxxx xxxx xxxx 0000 xxxx */
	/* MLS			1111 1011 0000 xxxx xxxx xxxx 0001 xxxx */
	/* SMLAD{X}		1111 1011 0010 xxxx xxxx xxxx 000x xxxx */
	/* SMLAWy		1111 1011 0011 xxxx xxxx xxxx 000x xxxx */
	/* SMLSD{X}		1111 1011 0100 xxxx xxxx xxxx 000x xxxx */
	/* SMMLA{R}		1111 1011 0101 xxxx xxxx xxxx 000x xxxx */
	/* SMMLS{R}		1111 1011 0110 xxxx xxxx xxxx 000x xxxx */
	/* USADA8		1111 1011 0111 xxxx xxxx xxxx 0000 xxxx */
	DECODE_EMULATEX	(0xff8000c0, 0xfb000000,  PROBES_T32_MUL_ADD2,
						 REGS(NOSPPC, NOSPPCX, NOSPPC, 0, NOSPPC)),

	/* Other unallocated instructions...				*/
	DECODE_END
};

static const union decode_item t32_table_1111_1011_1[] = {
	/* Long multiply, long multiply accumulate, and divide		*/

	/* UMAAL		1111 1011 1110 xxxx xxxx xxxx 0110 xxxx */
	DECODE_OR	(0xfff000f0, 0xfbe00060),
	/* SMLALxy		1111 1011 1100 xxxx xxxx xxxx 10xx xxxx */
	DECODE_OR	(0xfff000c0, 0xfbc00080),
	/* SMLALD{X}		1111 1011 1100 xxxx xxxx xxxx 110x xxxx */
	/* SMLSLD{X}		1111 1011 1101 xxxx xxxx xxxx 110x xxxx */
	DECODE_OR	(0xffe000e0, 0xfbc000c0),
	/* SMULL		1111 1011 1000 xxxx xxxx xxxx 0000 xxxx */
	/* UMULL		1111 1011 1010 xxxx xxxx xxxx 0000 xxxx */
	/* SMLAL		1111 1011 1100 xxxx xxxx xxxx 0000 xxxx */
	/* UMLAL		1111 1011 1110 xxxx xxxx xxxx 0000 xxxx */
	DECODE_EMULATEX	(0xff9000f0, 0xfb800000, PROBES_T32_MUL_ADD_LONG,
						 REGS(NOSPPC, NOSPPC, NOSPPC, 0, NOSPPC)),

	/* SDIV			1111 1011 1001 xxxx xxxx xxxx 1111 xxxx */
	/* UDIV			1111 1011 1011 xxxx xxxx xxxx 1111 xxxx */
	/* Other unallocated instructions...				*/
	DECODE_END
};

const union decode_item probes_decode_thumb32_table[] = {

	/*
	 * Load/store multiple instructions
	 *			1110 100x x0xx xxxx xxxx xxxx xxxx xxxx
	 */
	DECODE_TABLE	(0xfe400000, 0xe8000000, t32_table_1110_100x_x0xx),

	/*
	 * Load/store dual, load/store exclusive, table branch
	 *			1110 100x x1xx xxxx xxxx xxxx xxxx xxxx
	 */
	DECODE_TABLE	(0xfe400000, 0xe8400000, t32_table_1110_100x_x1xx),

	/*
	 * Data-processing (shifted register)
	 *			1110 101x xxxx xxxx xxxx xxxx xxxx xxxx
	 */
	DECODE_TABLE	(0xfe000000, 0xea000000, t32_table_1110_101x),

	/*
	 * Coprocessor instructions
	 *			1110 11xx xxxx xxxx xxxx xxxx xxxx xxxx
	 */
	DECODE_REJECT	(0xfc000000, 0xec000000),

	/*
	 * Data-processing (modified immediate)
	 *			1111 0x0x xxxx xxxx 0xxx xxxx xxxx xxxx
	 */
	DECODE_TABLE	(0xfa008000, 0xf0000000, t32_table_1111_0x0x___0),

	/*
	 * Data-processing (plain binary immediate)
	 *			1111 0x1x xxxx xxxx 0xxx xxxx xxxx xxxx
	 */
	DECODE_TABLE	(0xfa008000, 0xf2000000, t32_table_1111_0x1x___0),

	/*
	 * Branches and miscellaneous control
	 *			1111 0xxx xxxx xxxx 1xxx xxxx xxxx xxxx
	 */
	DECODE_TABLE	(0xf8008000, 0xf0008000, t32_table_1111_0xxx___1),

	/*
	 * Advanced SIMD element or structure load/store instructions
	 *			1111 1001 xxx0 xxxx xxxx xxxx xxxx xxxx
	 */
	DECODE_REJECT	(0xff100000, 0xf9000000),

	/*
	 * Memory hints
	 *			1111 100x x0x1 xxxx 1111 xxxx xxxx xxxx
	 */
	DECODE_TABLE	(0xfe50f000, 0xf810f000, t32_table_1111_100x_x0x1__1111),

	/*
	 * Store single data item
	 *			1111 1000 xxx0 xxxx xxxx xxxx xxxx xxxx
	 * Load single data items
	 *			1111 100x xxx1 xxxx xxxx xxxx xxxx xxxx
	 */
	DECODE_TABLE	(0xfe000000, 0xf8000000, t32_table_1111_100x),

	/*
	 * Data-processing (register)
	 *			1111 1010 xxxx xxxx 1111 xxxx xxxx xxxx
	 */
	DECODE_TABLE	(0xff00f000, 0xfa00f000, t32_table_1111_1010___1111),

	/*
	 * Multiply, multiply accumulate, and absolute difference
	 *			1111 1011 0xxx xxxx xxxx xxxx xxxx xxxx
	 */
	DECODE_TABLE	(0xff800000, 0xfb000000, t32_table_1111_1011_0),

	/*
	 * Long multiply, long multiply accumulate, and divide
	 *			1111 1011 1xxx xxxx xxxx xxxx xxxx xxxx
	 */
	DECODE_TABLE	(0xff800000, 0xfb800000, t32_table_1111_1011_1),

	/*
	 * Coprocessor instructions
	 *			1111 11xx xxxx xxxx xxxx xxxx xxxx xxxx
	 */
	DECODE_END
};
#ifdef CONFIG_ARM_KPROBES_TEST_MODULE
EXPORT_SYMBOL_GPL(probes_decode_thumb32_table);
#endif

static const union decode_item t16_table_1011[] = {
	/* Miscellaneous 16-bit instructions		    */

	/* ADD (SP plus immediate)	1011 0000 0xxx xxxx */
	/* SUB (SP minus immediate)	1011 0000 1xxx xxxx */
	DECODE_SIMULATE	(0xff00, 0xb000, PROBES_T16_ADD_SP),

	/* CBZ				1011 00x1 xxxx xxxx */
	/* CBNZ				1011 10x1 xxxx xxxx */
	DECODE_SIMULATE	(0xf500, 0xb100, PROBES_T16_CBZ),

	/* SXTH				1011 0010 00xx xxxx */
	/* SXTB				1011 0010 01xx xxxx */
	/* UXTH				1011 0010 10xx xxxx */
	/* UXTB				1011 0010 11xx xxxx */
	/* REV				1011 1010 00xx xxxx */
	/* REV16			1011 1010 01xx xxxx */
	/* ???				1011 1010 10xx xxxx */
	/* REVSH			1011 1010 11xx xxxx */
	DECODE_REJECT	(0xffc0, 0xba80),
	DECODE_EMULATE	(0xf500, 0xb000, PROBES_T16_SIGN_EXTEND),

	/* PUSH				1011 010x xxxx xxxx */
	DECODE_CUSTOM	(0xfe00, 0xb400, PROBES_T16_PUSH),
	/* POP				1011 110x xxxx xxxx */
	DECODE_CUSTOM	(0xfe00, 0xbc00, PROBES_T16_POP),

	/*
	 * If-Then, and hints
	 *				1011 1111 xxxx xxxx
	 */

	/* YIELD			1011 1111 0001 0000 */
	DECODE_OR	(0xffff, 0xbf10),
	/* SEV				1011 1111 0100 0000 */
	DECODE_EMULATE	(0xffff, 0xbf40, PROBES_T16_SEV),
	/* NOP				1011 1111 0000 0000 */
	/* WFE				1011 1111 0010 0000 */
	/* WFI				1011 1111 0011 0000 */
	DECODE_SIMULATE	(0xffcf, 0xbf00, PROBES_T16_WFE),
	/* Unassigned hints		1011 1111 xxxx 0000 */
	DECODE_REJECT	(0xff0f, 0xbf00),
	/* IT				1011 1111 xxxx xxxx */
	DECODE_CUSTOM	(0xff00, 0xbf00, PROBES_T16_IT),

	/* SETEND			1011 0110 010x xxxx */
	/* CPS				1011 0110 011x xxxx */
	/* BKPT				1011 1110 xxxx xxxx */
	/* And unallocated instructions...		    */
	DECODE_END
};

const union decode_item probes_decode_thumb16_table[] = {

	/*
	 * Shift (immediate), add, subtract, move, and compare
	 *				00xx xxxx xxxx xxxx
	 */

	/* CMP (immediate)		0010 1xxx xxxx xxxx */
	DECODE_EMULATE	(0xf800, 0x2800, PROBES_T16_CMP),

	/* ADD (register)		0001 100x xxxx xxxx */
	/* SUB (register)		0001 101x xxxx xxxx */
	/* LSL (immediate)		0000 0xxx xxxx xxxx */
	/* LSR (immediate)		0000 1xxx xxxx xxxx */
	/* ASR (immediate)		0001 0xxx xxxx xxxx */
	/* ADD (immediate, Thumb)	0001 110x xxxx xxxx */
	/* SUB (immediate, Thumb)	0001 111x xxxx xxxx */
	/* MOV (immediate)		0010 0xxx xxxx xxxx */
	/* ADD (immediate, Thumb)	0011 0xxx xxxx xxxx */
	/* SUB (immediate, Thumb)	0011 1xxx xxxx xxxx */
	DECODE_EMULATE	(0xc000, 0x0000, PROBES_T16_ADDSUB),

	/*
	 * 16-bit Thumb data-processing instructions
	 *				0100 00xx xxxx xxxx
	 */

	/* TST (register)		0100 0010 00xx xxxx */
	DECODE_EMULATE	(0xffc0, 0x4200, PROBES_T16_CMP),
	/* CMP (register)		0100 0010 10xx xxxx */
	/* CMN (register)		0100 0010 11xx xxxx */
	DECODE_EMULATE	(0xff80, 0x4280, PROBES_T16_CMP),
	/* AND (register)		0100 0000 00xx xxxx */
	/* EOR (register)		0100 0000 01xx xxxx */
	/* LSL (register)		0100 0000 10xx xxxx */
	/* LSR (register)		0100 0000 11xx xxxx */
	/* ASR (register)		0100 0001 00xx xxxx */
	/* ADC (register)		0100 0001 01xx xxxx */
	/* SBC (register)		0100 0001 10xx xxxx */
	/* ROR (register)		0100 0001 11xx xxxx */
	/* RSB (immediate)		0100 0010 01xx xxxx */
	/* ORR (register)		0100 0011 00xx xxxx */
	/* MUL				0100 0011 00xx xxxx */
	/* BIC (register)		0100 0011 10xx xxxx */
	/* MVN (register)		0100 0011 10xx xxxx */
	DECODE_EMULATE	(0xfc00, 0x4000, PROBES_T16_LOGICAL),

	/*
	 * Special data instructions and branch and exchange
	 *				0100 01xx xxxx xxxx
	 */

	/* BLX pc			0100 0111 1111 1xxx */
	DECODE_REJECT	(0xfff8, 0x47f8),

	/* BX (register)		0100 0111 0xxx xxxx */
	/* BLX (register)		0100 0111 1xxx xxxx */
	DECODE_SIMULATE (0xff00, 0x4700, PROBES_T16_BLX),

	/* ADD pc, pc			0100 0100 1111 1111 */
	DECODE_REJECT	(0xffff, 0x44ff),

	/* ADD (register)		0100 0100 xxxx xxxx */
	/* CMP (register)		0100 0101 xxxx xxxx */
	/* MOV (register)		0100 0110 xxxx xxxx */
	DECODE_CUSTOM	(0xfc00, 0x4400, PROBES_T16_HIREGOPS),

	/*
	 * Load from Literal Pool
	 * LDR (literal)		0100 1xxx xxxx xxxx
	 */
	DECODE_SIMULATE	(0xf800, 0x4800, PROBES_T16_LDR_LIT),

	/*
	 * 16-bit Thumb Load/store instructions
	 *				0101 xxxx xxxx xxxx
	 *				011x xxxx xxxx xxxx
	 *				100x xxxx xxxx xxxx
	 */

	/* STR (register)		0101 000x xxxx xxxx */
	/* STRH (register)		0101 001x xxxx xxxx */
	/* STRB (register)		0101 010x xxxx xxxx */
	/* LDRSB (register)		0101 011x xxxx xxxx */
	/* LDR (register)		0101 100x xxxx xxxx */
	/* LDRH (register)		0101 101x xxxx xxxx */
	/* LDRB (register)		0101 110x xxxx xxxx */
	/* LDRSH (register)		0101 111x xxxx xxxx */
	/* STR (immediate, Thumb)	0110 0xxx xxxx xxxx */
	/* LDR (immediate, Thumb)	0110 1xxx xxxx xxxx */
	/* STRB (immediate, Thumb)	0111 0xxx xxxx xxxx */
	/* LDRB (immediate, Thumb)	0111 1xxx xxxx xxxx */
	DECODE_EMULATE	(0xc000, 0x4000, PROBES_T16_LDRHSTRH),
	/* STRH (immediate, Thumb)	1000 0xxx xxxx xxxx */
	/* LDRH (immediate, Thumb)	1000 1xxx xxxx xxxx */
	DECODE_EMULATE	(0xf000, 0x8000, PROBES_T16_LDRHSTRH),
	/* STR (immediate, Thumb)	1001 0xxx xxxx xxxx */
	/* LDR (immediate, Thumb)	1001 1xxx xxxx xxxx */
	DECODE_SIMULATE	(0xf000, 0x9000, PROBES_T16_LDRSTR),

	/*
	 * Generate PC-/SP-relative address
	 * ADR (literal)		1010 0xxx xxxx xxxx
	 * ADD (SP plus immediate)	1010 1xxx xxxx xxxx
	 */
	DECODE_SIMULATE	(0xf000, 0xa000, PROBES_T16_ADR),

	/*
	 * Miscellaneous 16-bit instructions
	 *				1011 xxxx xxxx xxxx
	 */
	DECODE_TABLE	(0xf000, 0xb000, t16_table_1011),

	/* STM				1100 0xxx xxxx xxxx */
	/* LDM				1100 1xxx xxxx xxxx */
	DECODE_EMULATE	(0xf000, 0xc000, PROBES_T16_LDMSTM),

	/*
	 * Conditional branch, and Supervisor Call
	 */

	/* Permanently UNDEFINED	1101 1110 xxxx xxxx */
	/* SVC				1101 1111 xxxx xxxx */
	DECODE_REJECT	(0xfe00, 0xde00),

	/* Conditional branch		1101 xxxx xxxx xxxx */
	DECODE_CUSTOM	(0xf000, 0xd000, PROBES_T16_BRANCH_COND),

	/*
	 * Unconditional branch
	 * B				1110 0xxx xxxx xxxx
	 */
	DECODE_SIMULATE	(0xf800, 0xe000, PROBES_T16_BRANCH),

	DECODE_END
};
#ifdef CONFIG_ARM_KPROBES_TEST_MODULE
EXPORT_SYMBOL_GPL(probes_decode_thumb16_table);
#endif

static unsigned long __kprobes thumb_check_cc(unsigned long cpsr)
{
	if (unlikely(in_it_block(cpsr)))
		return probes_condition_checks[current_cond(cpsr)](cpsr);
	return true;
}

static void __kprobes thumb16_singlestep(probes_opcode_t opcode,
		struct arch_probes_insn *asi,
		struct pt_regs *regs)
{
	regs->ARM_pc += 2;
	asi->insn_handler(opcode, asi, regs);
	regs->ARM_cpsr = it_advance(regs->ARM_cpsr);
}

static void __kprobes thumb32_singlestep(probes_opcode_t opcode,
		struct arch_probes_insn *asi,
		struct pt_regs *regs)
{
	regs->ARM_pc += 4;
	asi->insn_handler(opcode, asi, regs);
	regs->ARM_cpsr = it_advance(regs->ARM_cpsr);
}

enum probes_insn __kprobes
thumb16_probes_decode_insn(probes_opcode_t insn, struct arch_probes_insn *asi,
			   bool emulate, const union decode_action *actions,
			   const struct decode_checker *checkers[])
{
	asi->insn_singlestep = thumb16_singlestep;
	asi->insn_check_cc = thumb_check_cc;
	return probes_decode_insn(insn, asi, probes_decode_thumb16_table, true,
				  emulate, actions, checkers);
}

enum probes_insn __kprobes
thumb32_probes_decode_insn(probes_opcode_t insn, struct arch_probes_insn *asi,
			   bool emulate, const union decode_action *actions,
			   const struct decode_checker *checkers[])
{
	asi->insn_singlestep = thumb32_singlestep;
	asi->insn_check_cc = thumb_check_cc;
	return probes_decode_insn(insn, asi, probes_decode_thumb32_table, true,
				  emulate, actions, checkers);
}
