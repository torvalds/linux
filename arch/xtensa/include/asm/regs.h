/*
 * Copyright (c) 2006 Tensilica, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2.1 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307,
 * USA.
 */

#ifndef _XTENSA_REGS_H
#define _XTENSA_REGS_H

/*  Special registers.  */

#define SREG_MR			32
#define SREG_IBREAKENABLE	96
#define SREG_IBREAKA		128
#define SREG_DBREAKA		144
#define SREG_DBREAKC		160
#define SREG_EPC		176
#define SREG_EPS		192
#define SREG_EXCSAVE		208
#define SREG_CCOMPARE		240
#define SREG_MISC		244

/*  EXCCAUSE register fields  */

#define EXCCAUSE_EXCCAUSE_SHIFT	0
#define EXCCAUSE_EXCCAUSE_MASK	0x3F

#define EXCCAUSE_ILLEGAL_INSTRUCTION		0
#define EXCCAUSE_SYSTEM_CALL			1
#define EXCCAUSE_INSTRUCTION_FETCH_ERROR	2
#define EXCCAUSE_LOAD_STORE_ERROR		3
#define EXCCAUSE_LEVEL1_INTERRUPT		4
#define EXCCAUSE_ALLOCA				5
#define EXCCAUSE_INTEGER_DIVIDE_BY_ZERO		6
#define EXCCAUSE_SPECULATION			7
#define EXCCAUSE_PRIVILEGED			8
#define EXCCAUSE_UNALIGNED			9
#define EXCCAUSE_INSTR_DATA_ERROR		12
#define EXCCAUSE_LOAD_STORE_DATA_ERROR		13
#define EXCCAUSE_INSTR_ADDR_ERROR		14
#define EXCCAUSE_LOAD_STORE_ADDR_ERROR		15
#define EXCCAUSE_ITLB_MISS			16
#define EXCCAUSE_ITLB_MULTIHIT			17
#define EXCCAUSE_ITLB_PRIVILEGE			18
#define EXCCAUSE_ITLB_SIZE_RESTRICTION		19
#define EXCCAUSE_FETCH_CACHE_ATTRIBUTE		20
#define EXCCAUSE_DTLB_MISS			24
#define EXCCAUSE_DTLB_MULTIHIT			25
#define EXCCAUSE_DTLB_PRIVILEGE			26
#define EXCCAUSE_DTLB_SIZE_RESTRICTION		27
#define EXCCAUSE_LOAD_CACHE_ATTRIBUTE		28
#define EXCCAUSE_STORE_CACHE_ATTRIBUTE		29
#define EXCCAUSE_COPROCESSOR0_DISABLED		32
#define EXCCAUSE_COPROCESSOR1_DISABLED		33
#define EXCCAUSE_COPROCESSOR2_DISABLED		34
#define EXCCAUSE_COPROCESSOR3_DISABLED		35
#define EXCCAUSE_COPROCESSOR4_DISABLED		36
#define EXCCAUSE_COPROCESSOR5_DISABLED		37
#define EXCCAUSE_COPROCESSOR6_DISABLED		38
#define EXCCAUSE_COPROCESSOR7_DISABLED		39

/*  PS register fields.  */

#define PS_WOE_BIT		18
#define PS_CALLINC_SHIFT	16
#define PS_CALLINC_MASK		0x00030000
#define PS_OWB_SHIFT		8
#define PS_OWB_WIDTH		4
#define PS_OWB_MASK		0x00000F00
#define PS_RING_SHIFT		6
#define PS_RING_MASK		0x000000C0
#define PS_UM_BIT		5
#define PS_EXCM_BIT		4
#define PS_INTLEVEL_SHIFT	0
#define PS_INTLEVEL_WIDTH	4
#define PS_INTLEVEL_MASK	0x0000000F

/*  DBREAKCn register fields.  */

#define DBREAKC_MASK_BIT		0
#define DBREAKC_MASK_MASK		0x0000003F
#define DBREAKC_LOAD_BIT		30
#define DBREAKC_LOAD_MASK		0x40000000
#define DBREAKC_STOR_BIT		31
#define DBREAKC_STOR_MASK		0x80000000

/*  DEBUGCAUSE register fields.  */

#define DEBUGCAUSE_DBNUM_MASK		0xf00
#define DEBUGCAUSE_DBNUM_SHIFT		8	/* First bit of DBNUM field */
#define DEBUGCAUSE_DEBUGINT_BIT		5	/* External debug interrupt */
#define DEBUGCAUSE_BREAKN_BIT		4	/* BREAK.N instruction */
#define DEBUGCAUSE_BREAK_BIT		3	/* BREAK instruction */
#define DEBUGCAUSE_DBREAK_BIT		2	/* DBREAK match */
#define DEBUGCAUSE_IBREAK_BIT		1	/* IBREAK match */
#define DEBUGCAUSE_ICOUNT_BIT		0	/* ICOUNT would incr. to zero */

#endif /* _XTENSA_SPECREG_H */
