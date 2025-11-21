/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2009 Freescale Semiconductor, Inc.
 *
 * provides masks and opcode images for use by code generation, emulation
 * and for instructions that older assemblers might not know about
 */
#ifndef _ASM_POWERPC_PPC_OPCODE_H
#define _ASM_POWERPC_PPC_OPCODE_H

#include <asm/asm-const.h>

#define	__REG_R0	0
#define	__REG_R1	1
#define	__REG_R2	2
#define	__REG_R3	3
#define	__REG_R4	4
#define	__REG_R5	5
#define	__REG_R6	6
#define	__REG_R7	7
#define	__REG_R8	8
#define	__REG_R9	9
#define	__REG_R10	10
#define	__REG_R11	11
#define	__REG_R12	12
#define	__REG_R13	13
#define	__REG_R14	14
#define	__REG_R15	15
#define	__REG_R16	16
#define	__REG_R17	17
#define	__REG_R18	18
#define	__REG_R19	19
#define	__REG_R20	20
#define	__REG_R21	21
#define	__REG_R22	22
#define	__REG_R23	23
#define	__REG_R24	24
#define	__REG_R25	25
#define	__REG_R26	26
#define	__REG_R27	27
#define	__REG_R28	28
#define	__REG_R29	29
#define	__REG_R30	30
#define	__REG_R31	31

#define	__REGA0_0	0
#define	__REGA0_R1	1
#define	__REGA0_R2	2
#define	__REGA0_R3	3
#define	__REGA0_R4	4
#define	__REGA0_R5	5
#define	__REGA0_R6	6
#define	__REGA0_R7	7
#define	__REGA0_R8	8
#define	__REGA0_R9	9
#define	__REGA0_R10	10
#define	__REGA0_R11	11
#define	__REGA0_R12	12
#define	__REGA0_R13	13
#define	__REGA0_R14	14
#define	__REGA0_R15	15
#define	__REGA0_R16	16
#define	__REGA0_R17	17
#define	__REGA0_R18	18
#define	__REGA0_R19	19
#define	__REGA0_R20	20
#define	__REGA0_R21	21
#define	__REGA0_R22	22
#define	__REGA0_R23	23
#define	__REGA0_R24	24
#define	__REGA0_R25	25
#define	__REGA0_R26	26
#define	__REGA0_R27	27
#define	__REGA0_R28	28
#define	__REGA0_R29	29
#define	__REGA0_R30	30
#define	__REGA0_R31	31

/* For use with PPC_RAW_() macros */
#define	_R0	0
#define	_R1	1
#define	_R2	2
#define	_R3	3
#define	_R4	4
#define	_R5	5
#define	_R6	6
#define	_R7	7
#define	_R8	8
#define	_R9	9
#define	_R10	10
#define	_R11	11
#define	_R12	12
#define	_R13	13
#define	_R14	14
#define	_R15	15
#define	_R16	16
#define	_R17	17
#define	_R18	18
#define	_R19	19
#define	_R20	20
#define	_R21	21
#define	_R22	22
#define	_R23	23
#define	_R24	24
#define	_R25	25
#define	_R26	26
#define	_R27	27
#define	_R28	28
#define	_R29	29
#define	_R30	30
#define	_R31	31

#define IMM_L(i)               ((uintptr_t)(i) & 0xffff)
#define IMM_DS(i)              ((uintptr_t)(i) & 0xfffc)
#define IMM_DQ(i)              ((uintptr_t)(i) & 0xfff0)
#define IMM_D0(i)              (((uintptr_t)(i) >> 16) & 0x3ffff)
#define IMM_D1(i)              IMM_L(i)

/*
 * 16-bit immediate helper macros: HA() is for use with sign-extending instrs
 * (e.g. LD, ADDI).  If the bottom 16 bits is "-ve", add another bit into the
 * top half to negate the effect (i.e. 0xffff + 1 = 0x(1)0000).
 *
 * XXX: should these mask out possible sign bits?
 */
#define IMM_H(i)                ((uintptr_t)(i)>>16)
#define IMM_HA(i)               (((uintptr_t)(i)>>16) +                       \
					(((uintptr_t)(i) & 0x8000) >> 15))

/*
 * 18-bit immediate helper for prefix 18-bit upper immediate si0 field.
 */
#define IMM_H18(i)              (((uintptr_t)(i)>>16) & 0x3ffff)


/* opcode and xopcode for instructions */
#define OP_PREFIX	1
#define OP_TRAP_64	2
#define OP_TRAP		3
#define OP_SC		17
#define OP_19		19
#define OP_31		31
#define OP_LWZ		32
#define OP_LWZU		33
#define OP_LBZ		34
#define OP_LBZU		35
#define OP_STW		36
#define OP_STWU		37
#define OP_STB		38
#define OP_STBU		39
#define OP_LHZ		40
#define OP_LHZU		41
#define OP_LHA		42
#define OP_LHAU		43
#define OP_STH		44
#define OP_STHU		45
#define OP_LMW		46
#define OP_STMW		47
#define OP_LFS		48
#define OP_LFSU		49
#define OP_LFD		50
#define OP_LFDU		51
#define OP_STFS		52
#define OP_STFSU	53
#define OP_STFD		54
#define OP_STFDU	55
#define OP_LQ		56
#define OP_LD		58
#define OP_STD		62

#define OP_19_XOP_RFID		18
#define OP_19_XOP_RFMCI		38
#define OP_19_XOP_RFDI		39
#define OP_19_XOP_RFI		50
#define OP_19_XOP_RFCI		51
#define OP_19_XOP_RFSCV		82
#define OP_19_XOP_HRFID		274
#define OP_19_XOP_URFID		306
#define OP_19_XOP_STOP		370
#define OP_19_XOP_DOZE		402
#define OP_19_XOP_NAP		434
#define OP_19_XOP_SLEEP		466
#define OP_19_XOP_RVWINKLE	498

#define OP_31_XOP_TRAP      4
#define OP_31_XOP_LDX       21
#define OP_31_XOP_LWZX      23
#define OP_31_XOP_LDUX      53
#define OP_31_XOP_DCBST     54
#define OP_31_XOP_LWZUX     55
#define OP_31_XOP_TRAP_64   68
#define OP_31_XOP_DCBF      86
#define OP_31_XOP_LBZX      87
#define OP_31_XOP_STDX      149
#define OP_31_XOP_STWX      151
#define OP_31_XOP_STDUX     181
#define OP_31_XOP_STWUX     183
#define OP_31_XOP_STBX      215
#define OP_31_XOP_LBZUX     119
#define OP_31_XOP_STBUX     247
#define OP_31_XOP_LHZX      279
#define OP_31_XOP_LHZUX     311
#define OP_31_XOP_MSGSNDP   142
#define OP_31_XOP_MSGCLRP   174
#define OP_31_XOP_MTMSR     146
#define OP_31_XOP_MTMSRD    178
#define OP_31_XOP_TLBIE     306
#define OP_31_XOP_MFSPR     339
#define OP_31_XOP_LWAX      341
#define OP_31_XOP_LHAX      343
#define OP_31_XOP_LWAUX     373
#define OP_31_XOP_LHAUX     375
#define OP_31_XOP_STHX      407
#define OP_31_XOP_STHUX     439
#define OP_31_XOP_MTSPR     467
#define OP_31_XOP_DCBI      470
#define OP_31_XOP_LDBRX     532
#define OP_31_XOP_LWBRX     534
#define OP_31_XOP_TLBSYNC   566
#define OP_31_XOP_STDBRX    660
#define OP_31_XOP_STWBRX    662
#define OP_31_XOP_STFSX	    663
#define OP_31_XOP_STFSUX    695
#define OP_31_XOP_STFDX     727
#define OP_31_XOP_HASHCHK   754
#define OP_31_XOP_STFDUX    759
#define OP_31_XOP_LHBRX     790
#define OP_31_XOP_LFIWAX    855
#define OP_31_XOP_LFIWZX    887
#define OP_31_XOP_STHBRX    918
#define OP_31_XOP_STFIWX    983

/* VSX Scalar Load Instructions */
#define OP_31_XOP_LXSDX         588
#define OP_31_XOP_LXSSPX        524
#define OP_31_XOP_LXSIWAX       76
#define OP_31_XOP_LXSIWZX       12

/* VSX Scalar Store Instructions */
#define OP_31_XOP_STXSDX        716
#define OP_31_XOP_STXSSPX       652
#define OP_31_XOP_STXSIWX       140

/* VSX Vector Load Instructions */
#define OP_31_XOP_LXVD2X        844
#define OP_31_XOP_LXVW4X        780

/* VSX Vector Load and Splat Instruction */
#define OP_31_XOP_LXVDSX        332

/* VSX Vector Store Instructions */
#define OP_31_XOP_STXVD2X       972
#define OP_31_XOP_STXVW4X       908

#define OP_31_XOP_LFSX          535
#define OP_31_XOP_LFSUX         567
#define OP_31_XOP_LFDX          599
#define OP_31_XOP_LFDUX		631

/* VMX Vector Load Instructions */
#define OP_31_XOP_LVX           103

/* VMX Vector Store Instructions */
#define OP_31_XOP_STVX          231

/* sorted alphabetically */
#define PPC_INST_BCCTR_FLUSH		0x4c400420
#define PPC_INST_COPY			0x7c20060c
#define PPC_INST_DCBA			0x7c0005ec
#define PPC_INST_DCBA_MASK		0xfc0007fe
#define PPC_INST_DSSALL			0x7e00066c
#define PPC_INST_ISEL			0x7c00001e
#define PPC_INST_ISEL_MASK		0xfc00003e
#define PPC_INST_LSWI			0x7c0004aa
#define PPC_INST_LSWX			0x7c00042a
#define PPC_INST_LWSYNC			0x7c2004ac
#define PPC_INST_SYNC			0x7c0004ac
#define PPC_INST_SYNC_MASK		0xfc0007fe
#define PPC_INST_MCRXR			0x7c000400
#define PPC_INST_MCRXR_MASK		0xfc0007fe
#define PPC_INST_MFSPR_PVR		0x7c1f42a6
#define PPC_INST_MFSPR_PVR_MASK		0xfc1ffffe
#define PPC_INST_MTMSRD			0x7c000164
#define PPC_INST_PASTE			0x7c20070d
#define PPC_INST_PASTE_MASK		0xfc2007ff
#define PPC_INST_POPCNTB		0x7c0000f4
#define PPC_INST_POPCNTB_MASK		0xfc0007fe
#define PPC_INST_RFEBB			0x4c000124
#define PPC_INST_RFID			0x4c000024
#define PPC_INST_MFSPR_DSCR		0x7c1102a6
#define PPC_INST_MFSPR_DSCR_MASK	0xfc1ffffe
#define PPC_INST_MTSPR_DSCR		0x7c1103a6
#define PPC_INST_MTSPR_DSCR_MASK	0xfc1ffffe
#define PPC_INST_MFSPR_DSCR_USER	0x7c0302a6
#define PPC_INST_MFSPR_DSCR_USER_MASK	0xfc1ffffe
#define PPC_INST_MTSPR_DSCR_USER	0x7c0303a6
#define PPC_INST_MTSPR_DSCR_USER_MASK	0xfc1ffffe
#define PPC_INST_STRING			0x7c00042a
#define PPC_INST_STRING_MASK		0xfc0007fe
#define PPC_INST_STRING_GEN_MASK	0xfc00067e
#define PPC_INST_STSWI			0x7c0005aa
#define PPC_INST_STSWX			0x7c00052a
#define PPC_INST_TRECHKPT		0x7c0007dd
#define PPC_INST_TRECLAIM		0x7c00075d
#define PPC_INST_TSR			0x7c0005dd
#define PPC_INST_BRANCH_COND		0x40800000

/* Prefixes */
#define PPC_INST_LFS			0xc0000000
#define PPC_INST_STFS			0xd0000000
#define PPC_INST_LFD			0xc8000000
#define PPC_INST_STFD			0xd8000000
#define PPC_PREFIX_MLS			0x06000000
#define PPC_PREFIX_8LS			0x04000000

/* Prefixed instructions */
#define PPC_INST_PADDI			0x38000000
#define PPC_INST_PLD			0xe4000000
#define PPC_INST_PSTD			0xf4000000

/* macros to insert fields into opcodes */
#define ___PPC_RA(a)	(((a) & 0x1f) << 16)
#define ___PPC_RB(b)	(((b) & 0x1f) << 11)
#define ___PPC_RC(c)	(((c) & 0x1f) << 6)
#define ___PPC_RS(s)	(((s) & 0x1f) << 21)
#define ___PPC_RT(t)	___PPC_RS(t)
#define ___PPC_R(r)	(((r) & 0x1) << 16)
#define ___PPC_PRS(prs)	(((prs) & 0x1) << 17)
#define ___PPC_RIC(ric)	(((ric) & 0x3) << 18)
#define __PPC_RA(a)	___PPC_RA(__REG_##a)
#define __PPC_RA0(a)	___PPC_RA(__REGA0_##a)
#define __PPC_RB(b)	___PPC_RB(__REG_##b)
#define __PPC_RS(s)	___PPC_RS(__REG_##s)
#define __PPC_RT(t)	___PPC_RT(__REG_##t)
#define __PPC_XA(a)	((((a) & 0x1f) << 16) | (((a) & 0x20) >> 3))
#define __PPC_XB(b)	((((b) & 0x1f) << 11) | (((b) & 0x20) >> 4))
#define __PPC_XS(s)	((((s) & 0x1f) << 21) | (((s) & 0x20) >> 5))
#define __PPC_XT(s)	__PPC_XS(s)
#define __PPC_XSP(s)	((((s) & 0x1e) | (((s) >> 5) & 0x1)) << 21)
#define __PPC_XTP(s)	__PPC_XSP(s)
#define __PPC_T_TLB(t)	(((t) & 0x3) << 21)
#define __PPC_PL(p)	(((p) & 0x3) << 16)
#define __PPC_WC(w)	(((w) & 0x3) << 21)
#define __PPC_WS(w)	(((w) & 0x1f) << 11)
#define __PPC_SH(s)	__PPC_WS(s)
#define __PPC_SH64(s)	(__PPC_SH(s) | (((s) & 0x20) >> 4))
#define __PPC_MB(s)	___PPC_RC(s)
#define __PPC_ME(s)	(((s) & 0x1f) << 1)
#define __PPC_MB64(s)	(__PPC_MB(s) | ((s) & 0x20))
#define __PPC_ME64(s)	__PPC_MB64(s)
#define __PPC_BI(s)	(((s) & 0x1f) << 16)
#define __PPC_CT(t)	(((t) & 0x0f) << 21)
#define __PPC_SPR(r)	((((r) & 0x1f) << 16) | ((((r) >> 5) & 0x1f) << 11))
#define __PPC_RC21	(0x1 << 10)
#define __PPC_PRFX_R(r)	(((r) & 0x1) << 20)
#define __PPC_EH(eh)	(((eh) & 0x1) << 0)

/*
 * Both low and high 16 bits are added as SIGNED additions, so if low 16 bits
 * has high bit set, high 16 bits must be adjusted. These macros do that (stolen
 * from binutils).
 */
#define PPC_LO(v)	((v) & 0xffff)
#define PPC_HI(v)	(((v) >> 16) & 0xffff)
#define PPC_HA(v)	PPC_HI((v) + 0x8000)
#define PPC_HIGHER(v)	(((v) >> 32) & 0xffff)
#define PPC_HIGHEST(v)	(((v) >> 48) & 0xffff)

/* LI Field */
#define PPC_LI_MASK	0x03fffffc
#define PPC_LI(v)	((v) & PPC_LI_MASK)

/* Base instruction encoding */
#define PPC_RAW_CP_ABORT		(0x7c00068c)
#define PPC_RAW_COPY(a, b)		(PPC_INST_COPY | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_DARN(t, l)		(0x7c0005e6 | ___PPC_RT(t) | (((l) & 0x3) << 16))
#define PPC_RAW_DCBAL(a, b)		(0x7c2005ec | __PPC_RA(a) | __PPC_RB(b))
#define PPC_RAW_DCBZL(a, b)		(0x7c2007ec | __PPC_RA(a) | __PPC_RB(b))
#define PPC_RAW_LQARX(t, a, b, eh)	(0x7c000228 | ___PPC_RT(t) | ___PPC_RA(a) | ___PPC_RB(b) | __PPC_EH(eh))
#define PPC_RAW_LDARX(t, a, b, eh)	(0x7c0000a8 | ___PPC_RT(t) | ___PPC_RA(a) | ___PPC_RB(b) | __PPC_EH(eh))
#define PPC_RAW_LWARX(t, a, b, eh)	(0x7c000028 | ___PPC_RT(t) | ___PPC_RA(a) | ___PPC_RB(b) | __PPC_EH(eh))
#define PPC_RAW_PHWSYNC			(0x7c8004ac)
#define PPC_RAW_PLWSYNC			(0x7ca004ac)
#define PPC_RAW_STQCX(t, a, b)		(0x7c00016d | ___PPC_RT(t) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_MADDHD(t, a, b, c)	(0x10000030 | ___PPC_RT(t) | ___PPC_RA(a) | ___PPC_RB(b) | ___PPC_RC(c))
#define PPC_RAW_MADDHDU(t, a, b, c)	(0x10000031 | ___PPC_RT(t) | ___PPC_RA(a) | ___PPC_RB(b) | ___PPC_RC(c))
#define PPC_RAW_MADDLD(t, a, b, c)	(0x10000033 | ___PPC_RT(t) | ___PPC_RA(a) | ___PPC_RB(b) | ___PPC_RC(c))
#define PPC_RAW_MSGSND(b)		(0x7c00019c | ___PPC_RB(b))
#define PPC_RAW_MSGSYNC			(0x7c0006ec)
#define PPC_RAW_MSGCLR(b)		(0x7c0001dc | ___PPC_RB(b))
#define PPC_RAW_MSGSNDP(b)		(0x7c00011c | ___PPC_RB(b))
#define PPC_RAW_MSGCLRP(b)		(0x7c00015c | ___PPC_RB(b))
#define PPC_RAW_PASTE(a, b)		(0x7c20070d | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_POPCNTB(a, s)		(PPC_INST_POPCNTB | __PPC_RA(a) | __PPC_RS(s))
#define PPC_RAW_POPCNTD(a, s)		(0x7c0003f4 | __PPC_RA(a) | __PPC_RS(s))
#define PPC_RAW_POPCNTW(a, s)		(0x7c0002f4 | __PPC_RA(a) | __PPC_RS(s))
#define PPC_RAW_RFCI			(0x4c000066)
#define PPC_RAW_RFDI			(0x4c00004e)
#define PPC_RAW_RFMCI			(0x4c00004c)
#define PPC_RAW_TLBILX_LPID		(0x7c000024)
#define PPC_RAW_TLBILX(t, a, b)		(0x7c000024 | __PPC_T_TLB(t) | 	__PPC_RA0(a) | __PPC_RB(b))
#define PPC_RAW_WAIT_v203		(0x7c00007c)
#define PPC_RAW_WAIT(w, p)		(0x7c00003c | __PPC_WC(w) | __PPC_PL(p))
#define PPC_RAW_TLBIE(lp, a)		(0x7c000264 | ___PPC_RB(a) | ___PPC_RS(lp))
#define PPC_RAW_TLBIE_5(rb, rs, ric, prs, r) \
	(0x7c000264 | ___PPC_RB(rb) | ___PPC_RS(rs) | ___PPC_RIC(ric) | ___PPC_PRS(prs) | ___PPC_R(r))
#define PPC_RAW_TLBIEL(rb, rs, ric, prs, r) \
	(0x7c000224 | ___PPC_RB(rb) | ___PPC_RS(rs) | ___PPC_RIC(ric) | ___PPC_PRS(prs) | ___PPC_R(r))
#define PPC_RAW_TLBIEL_v205(rb, l)	(0x7c000224 | ___PPC_RB(rb) | (l << 21))
#define PPC_RAW_TLBSRX_DOT(a, b)	(0x7c0006a5 | __PPC_RA0(a) | __PPC_RB(b))
#define PPC_RAW_TLBIVAX(a, b)		(0x7c000624 | __PPC_RA0(a) | __PPC_RB(b))
#define PPC_RAW_ERATWE(s, a, w)		(0x7c0001a6 | __PPC_RS(s) | __PPC_RA(a) | __PPC_WS(w))
#define PPC_RAW_ERATRE(s, a, w)		(0x7c000166 | __PPC_RS(s) | __PPC_RA(a) | __PPC_WS(w))
#define PPC_RAW_ERATILX(t, a, b)	(0x7c000066 | __PPC_T_TLB(t) | __PPC_RA0(a) | __PPC_RB(b))
#define PPC_RAW_ERATIVAX(s, a, b)	(0x7c000666 | __PPC_RS(s) | __PPC_RA0(a) | __PPC_RB(b))
#define PPC_RAW_ERATSX(t, a, w)		(0x7c000126 | __PPC_RS(t) | __PPC_RA0(a) | __PPC_RB(b))
#define PPC_RAW_ERATSX_DOT(t, a, w)	(0x7c000127 | __PPC_RS(t) | __PPC_RA0(a) | __PPC_RB(b))
#define PPC_RAW_SLBFEE_DOT(t, b)	(0x7c0007a7 | __PPC_RT(t) | __PPC_RB(b))
#define __PPC_RAW_SLBFEE_DOT(t, b)	(0x7c0007a7 | ___PPC_RT(t) | ___PPC_RB(b))
#define PPC_RAW_ICBT(c, a, b)		(0x7c00002c | __PPC_CT(c) | __PPC_RA0(a) | __PPC_RB(b))
#define PPC_RAW_LBZCIX(t, a, b)		(0x7c0006aa | __PPC_RT(t) | __PPC_RA(a) | __PPC_RB(b))
#define PPC_RAW_STBCIX(s, a, b)		(0x7c0007aa | __PPC_RS(s) | __PPC_RA(a) | __PPC_RB(b))
#define PPC_RAW_DCBFPS(a, b)		(0x7c0000ac | ___PPC_RA(a) | ___PPC_RB(b) | (4 << 21))
#define PPC_RAW_DCBSTPS(a, b)		(0x7c0000ac | ___PPC_RA(a) | ___PPC_RB(b) | (6 << 21))
#define PPC_RAW_SC()			(0x44000002)
#define PPC_RAW_SYNC()			(0x7c0004ac)
#define PPC_RAW_ISYNC()			(0x4c00012c)
#define PPC_RAW_LWSYNC()		(0x7c2004ac)

/*
 * Define what the VSX XX1 form instructions will look like, then add
 * the 128 bit load store instructions based on that.
 */
#define VSX_XX1(s, a, b)		(__PPC_XS(s) | __PPC_RA(a) | __PPC_RB(b))
#define VSX_XX3(t, a, b)		(__PPC_XT(t) | __PPC_XA(a) | __PPC_XB(b))
#define PPC_RAW_STXVD2X(s, a, b)	(0x7c000798 | VSX_XX1((s), a, b))
#define PPC_RAW_LXVD2X(s, a, b)		(0x7c000698 | VSX_XX1((s), a, b))
#define PPC_RAW_MFVRD(a, t)		(0x7c000066 | VSX_XX1((t) + 32, a, R0))
#define PPC_RAW_MTVRD(t, a)		(0x7c000166 | VSX_XX1((t) + 32, a, R0))
#define PPC_RAW_VPMSUMW(t, a, b)	(0x10000488 | VSX_XX3((t), a, b))
#define PPC_RAW_VPMSUMD(t, a, b)	(0x100004c8 | VSX_XX3((t), a, b))
#define PPC_RAW_XXLOR(t, a, b)		(0xf0000490 | VSX_XX3((t), a, b))
#define PPC_RAW_XXSWAPD(t, a)		(0xf0000250 | VSX_XX3((t), a, a))
#define PPC_RAW_XVCPSGNDP(t, a, b)	((0xf0000780 | VSX_XX3((t), (a), (b))))
#define PPC_RAW_VPERMXOR(vrt, vra, vrb, vrc) \
	((0x1000002d | ___PPC_RT(vrt) | ___PPC_RA(vra) | ___PPC_RB(vrb) | (((vrc) & 0x1f) << 6)))
#define PPC_RAW_LXVP(xtp, a, i)		(0x18000000 | __PPC_XTP(xtp) | ___PPC_RA(a) | IMM_DQ(i))
#define PPC_RAW_STXVP(xsp, a, i)	(0x18000001 | __PPC_XSP(xsp) | ___PPC_RA(a) | IMM_DQ(i))
#define PPC_RAW_LXVPX(xtp, a, b)	(0x7c00029a | __PPC_XTP(xtp) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_STXVPX(xsp, a, b)	(0x7c00039a | __PPC_XSP(xsp) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_PLXVP_P(xtp, i, a, pr)	(PPC_PREFIX_8LS | __PPC_PRFX_R(pr) | IMM_D0(i))
#define PPC_RAW_PLXVP_S(xtp, i, a, pr)	(0xe8000000 | __PPC_XTP(xtp) | ___PPC_RA(a) | IMM_D1(i))
#define PPC_RAW_PSTXVP_P(xsp, i, a, pr)	(PPC_PREFIX_8LS | __PPC_PRFX_R(pr) | IMM_D0(i))
#define PPC_RAW_PSTXVP_S(xsp, i, a, pr)	(0xf8000000 | __PPC_XSP(xsp) | ___PPC_RA(a) | IMM_D1(i))
#define PPC_RAW_NAP			(0x4c000364)
#define PPC_RAW_SLEEP			(0x4c0003a4)
#define PPC_RAW_WINKLE			(0x4c0003e4)
#define PPC_RAW_STOP			(0x4c0002e4)
#define PPC_RAW_CLRBHRB			(0x7c00035c)
#define PPC_RAW_MFBHRBE(r, n)		(0x7c00025c | __PPC_RT(r) | (((n) & 0x3ff) << 11))
#define PPC_RAW_TRECHKPT		(PPC_INST_TRECHKPT)
#define PPC_RAW_TRECLAIM(r)		(PPC_INST_TRECLAIM | __PPC_RA(r))
#define PPC_RAW_TABORT(r)		(0x7c00071d | __PPC_RA(r))
#define TMRN(x)				((((x) & 0x1f) << 16) | (((x) & 0x3e0) << 6))
#define PPC_RAW_MTTMR(tmr, r)		(0x7c0003dc | TMRN(tmr) | ___PPC_RS(r))
#define PPC_RAW_MFTMR(tmr, r)		(0x7c0002dc | TMRN(tmr) | ___PPC_RT(r))
#define PPC_RAW_ICSWX(s, a, b)		(0x7c00032d | ___PPC_RS(s) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_ICSWEPX(s, a, b)	(0x7c00076d | ___PPC_RS(s) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_SLBIA(IH)		(0x7c0003e4 | (((IH) & 0x7) << 21))
#define PPC_RAW_VCMPEQUD_RC(vrt, vra, vrb) \
	(0x100000c7 | ___PPC_RT(vrt) | ___PPC_RA(vra) | ___PPC_RB(vrb) | __PPC_RC21)
#define PPC_RAW_VCMPEQUB_RC(vrt, vra, vrb) \
	(0x10000006 | ___PPC_RT(vrt) | ___PPC_RA(vra) | ___PPC_RB(vrb) | __PPC_RC21)
#define PPC_RAW_LD(r, base, i)		(0xe8000000 | ___PPC_RT(r) | ___PPC_RA(base) | IMM_DS(i))
#define PPC_RAW_LWA(r, base, i)		(0xe8000002 | ___PPC_RT(r) | ___PPC_RA(base) | IMM_DS(i))
#define PPC_RAW_LWZ(r, base, i)		(0x80000000 | ___PPC_RT(r) | ___PPC_RA(base) | IMM_L(i))
#define PPC_RAW_LWZX(t, a, b)		(0x7c00002e | ___PPC_RT(t) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_STD(r, base, i)		(0xf8000000 | ___PPC_RS(r) | ___PPC_RA(base) | IMM_DS(i))
#define PPC_RAW_STDCX(s, a, b)		(0x7c0001ad | ___PPC_RS(s) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_LFSX(t, a, b)		(0x7c00042e | ___PPC_RT(t) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_STFSX(s, a, b)		(0x7c00052e | ___PPC_RS(s) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_LFDX(t, a, b)		(0x7c0004ae | ___PPC_RT(t) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_STFDX(s, a, b)		(0x7c0005ae | ___PPC_RS(s) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_LVX(t, a, b)		(0x7c0000ce | ___PPC_RT(t) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_STVX(s, a, b)		(0x7c0001ce | ___PPC_RS(s) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_ADDE(t, a, b)		(0x7c000114 | ___PPC_RT(t) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_ADDZE(t, a)		(0x7c000194 | ___PPC_RT(t) | ___PPC_RA(a))
#define PPC_RAW_ADDME(t, a)		(0x7c0001d4 | ___PPC_RT(t) | ___PPC_RA(a))
#define PPC_RAW_ADD(t, a, b)		(0x7c000214 | ___PPC_RT(t) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_ADD_DOT(t, a, b)	(0x7c000214 | ___PPC_RT(t) | ___PPC_RA(a) | ___PPC_RB(b) | 0x1)
#define PPC_RAW_ADDC(t, a, b)		(0x7c000014 | ___PPC_RT(t) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_ADDC_DOT(t, a, b)	(0x7c000014 | ___PPC_RT(t) | ___PPC_RA(a) | ___PPC_RB(b) | 0x1)
#define PPC_RAW_NOP()			PPC_RAW_ORI(0, 0, 0)
#define PPC_RAW_BLR()			(0x4e800020)
#define PPC_RAW_BLRL()			(0x4e800021)
#define PPC_RAW_MTLR(r)			(0x7c0803a6 | ___PPC_RT(r))
#define PPC_RAW_MFLR(t)			(0x7c0802a6 | ___PPC_RT(t))
#define PPC_RAW_BCTR()			(0x4e800420)
#define PPC_RAW_BCTRL()			(0x4e800421)
#define PPC_RAW_MTCTR(r)		(0x7c0903a6 | ___PPC_RT(r))
#define PPC_RAW_ADDI(d, a, i)		(0x38000000 | ___PPC_RT(d) | ___PPC_RA(a) | IMM_L(i))
#define PPC_RAW_LI(r, i)		PPC_RAW_ADDI(r, 0, i)
#define PPC_RAW_ADDIS(d, a, i)		(0x3c000000 | ___PPC_RT(d) | ___PPC_RA(a) | IMM_L(i))
#define PPC_RAW_ADDIC(d, a, i)		(0x30000000 | ___PPC_RT(d) | ___PPC_RA(a) | IMM_L(i))
#define PPC_RAW_ADDIC_DOT(d, a, i)	(0x34000000 | ___PPC_RT(d) | ___PPC_RA(a) | IMM_L(i))
#define PPC_RAW_LIS(r, i)		PPC_RAW_ADDIS(r, 0, i)
#define PPC_RAW_STDX(r, base, b)	(0x7c00012a | ___PPC_RS(r) | ___PPC_RA(base) | ___PPC_RB(b))
#define PPC_RAW_STDU(r, base, i)	(0xf8000001 | ___PPC_RS(r) | ___PPC_RA(base) | ((i) & 0xfffc))
#define PPC_RAW_STW(r, base, i)		(0x90000000 | ___PPC_RS(r) | ___PPC_RA(base) | IMM_L(i))
#define PPC_RAW_STWU(r, base, i)	(0x94000000 | ___PPC_RS(r) | ___PPC_RA(base) | IMM_L(i))
#define PPC_RAW_STH(r, base, i)		(0xb0000000 | ___PPC_RS(r) | ___PPC_RA(base) | IMM_L(i))
#define PPC_RAW_STB(r, base, i)		(0x98000000 | ___PPC_RS(r) | ___PPC_RA(base) | IMM_L(i))
#define PPC_RAW_LBZ(r, base, i)		(0x88000000 | ___PPC_RT(r) | ___PPC_RA(base) | IMM_L(i))
#define PPC_RAW_LDX(r, base, b)		(0x7c00002a | ___PPC_RT(r) | ___PPC_RA(base) | ___PPC_RB(b))
#define PPC_RAW_LHA(r, base, i)		(0xa8000000 | ___PPC_RT(r) | ___PPC_RA(base) | IMM_L(i))
#define PPC_RAW_LHZ(r, base, i)		(0xa0000000 | ___PPC_RT(r) | ___PPC_RA(base) | IMM_L(i))
#define PPC_RAW_LHBRX(r, base, b)	(0x7c00062c | ___PPC_RT(r) | ___PPC_RA(base) | ___PPC_RB(b))
#define PPC_RAW_LWBRX(r, base, b)	(0x7c00042c | ___PPC_RT(r) | ___PPC_RA(base) | ___PPC_RB(b))
#define PPC_RAW_LDBRX(r, base, b)	(0x7c000428 | ___PPC_RT(r) | ___PPC_RA(base) | ___PPC_RB(b))
#define PPC_RAW_STWCX(s, a, b)		(0x7c00012d | ___PPC_RS(s) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_CMPWI(a, i)		(0x2c000000 | ___PPC_RA(a) | IMM_L(i))
#define PPC_RAW_CMPDI(a, i)		(0x2c200000 | ___PPC_RA(a) | IMM_L(i))
#define PPC_RAW_CMPW(a, b)		(0x7c000000 | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_CMPD(a, b)		(0x7c200000 | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_CMPLWI(a, i)		(0x28000000 | ___PPC_RA(a) | IMM_L(i))
#define PPC_RAW_CMPLDI(a, i)		(0x28200000 | ___PPC_RA(a) | IMM_L(i))
#define PPC_RAW_CMPLW(a, b)		(0x7c000040 | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_CMPLD(a, b)		(0x7c200040 | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_SUB(d, a, b)		(0x7c000050 | ___PPC_RT(d) | ___PPC_RB(a) | ___PPC_RA(b))
#define PPC_RAW_SUBFC(d, a, b)		(0x7c000010 | ___PPC_RT(d) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_SUBFE(d, a, b)		(0x7c000110 | ___PPC_RT(d) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_SUBFIC(d, a, i)		(0x20000000 | ___PPC_RT(d) | ___PPC_RA(a) | IMM_L(i))
#define PPC_RAW_SUBFZE(d, a)		(0x7c000190 | ___PPC_RT(d) | ___PPC_RA(a))
#define PPC_RAW_MULD(d, a, b)		(0x7c0001d2 | ___PPC_RT(d) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_MULW(d, a, b)		(0x7c0001d6 | ___PPC_RT(d) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_MULHWU(d, a, b)		(0x7c000016 | ___PPC_RT(d) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_MULI(d, a, i)		(0x1c000000 | ___PPC_RT(d) | ___PPC_RA(a) | IMM_L(i))
#define PPC_RAW_DIVW(d, a, b)		(0x7c0003d6 | ___PPC_RT(d) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_DIVWU(d, a, b)		(0x7c000396 | ___PPC_RT(d) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_DIVD(d, a, b)		(0x7c0003d2 | ___PPC_RT(d) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_DIVDU(d, a, b)		(0x7c000392 | ___PPC_RT(d) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_DIVDE(t, a, b)		(0x7c000352 | ___PPC_RT(t) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_DIVDE_DOT(t, a, b)	(0x7c000352 | ___PPC_RT(t) | ___PPC_RA(a) | ___PPC_RB(b) | 0x1)
#define PPC_RAW_DIVDEU(t, a, b)		(0x7c000312 | ___PPC_RT(t) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_DIVDEU_DOT(t, a, b)	(0x7c000312 | ___PPC_RT(t) | ___PPC_RA(a) | ___PPC_RB(b) | 0x1)
#define PPC_RAW_AND(d, a, b)		(0x7c000038 | ___PPC_RA(d) | ___PPC_RS(a) | ___PPC_RB(b))
#define PPC_RAW_ANDI(d, a, i)		(0x70000000 | ___PPC_RA(d) | ___PPC_RS(a) | IMM_L(i))
#define PPC_RAW_ANDIS(d, a, i)		(0x74000000 | ___PPC_RA(d) | ___PPC_RS(a) | IMM_L(i))
#define PPC_RAW_AND_DOT(d, a, b)	(0x7c000039 | ___PPC_RA(d) | ___PPC_RS(a) | ___PPC_RB(b))
#define PPC_RAW_OR(d, a, b)		(0x7c000378 | ___PPC_RA(d) | ___PPC_RS(a) | ___PPC_RB(b))
#define PPC_RAW_MR(d, a)		PPC_RAW_OR(d, a, a)
#define PPC_RAW_ORI(d, a, i)		(0x60000000 | ___PPC_RA(d) | ___PPC_RS(a) | IMM_L(i))
#define PPC_RAW_ORIS(d, a, i)		(0x64000000 | ___PPC_RA(d) | ___PPC_RS(a) | IMM_L(i))
#define PPC_RAW_NOR(d, a, b)		(0x7c0000f8 | ___PPC_RA(d) | ___PPC_RS(a) | ___PPC_RB(b))
#define PPC_RAW_XOR(d, a, b)		(0x7c000278 | ___PPC_RA(d) | ___PPC_RS(a) | ___PPC_RB(b))
#define PPC_RAW_XORI(d, a, i)		(0x68000000 | ___PPC_RA(d) | ___PPC_RS(a) | IMM_L(i))
#define PPC_RAW_XORIS(d, a, i)		(0x6c000000 | ___PPC_RA(d) | ___PPC_RS(a) | IMM_L(i))
#define PPC_RAW_EXTSB(d, a)		(0x7c000774 | ___PPC_RA(d) | ___PPC_RS(a))
#define PPC_RAW_EXTSH(d, a)		(0x7c000734 | ___PPC_RA(d) | ___PPC_RS(a))
#define PPC_RAW_EXTSW(d, a)		(0x7c0007b4 | ___PPC_RA(d) | ___PPC_RS(a))
#define PPC_RAW_SLW(d, a, s)		(0x7c000030 | ___PPC_RA(d) | ___PPC_RS(a) | ___PPC_RB(s))
#define PPC_RAW_SLD(d, a, s)		(0x7c000036 | ___PPC_RA(d) | ___PPC_RS(a) | ___PPC_RB(s))
#define PPC_RAW_SRW(d, a, s)		(0x7c000430 | ___PPC_RA(d) | ___PPC_RS(a) | ___PPC_RB(s))
#define PPC_RAW_SRAW(d, a, s)		(0x7c000630 | ___PPC_RA(d) | ___PPC_RS(a) | ___PPC_RB(s))
#define PPC_RAW_SRAWI(d, a, i)		(0x7c000670 | ___PPC_RA(d) | ___PPC_RS(a) | __PPC_SH(i))
#define PPC_RAW_SRD(d, a, s)		(0x7c000436 | ___PPC_RA(d) | ___PPC_RS(a) | ___PPC_RB(s))
#define PPC_RAW_SRAD(d, a, s)		(0x7c000634 | ___PPC_RA(d) | ___PPC_RS(a) | ___PPC_RB(s))
#define PPC_RAW_SRADI(d, a, i)		(0x7c000674 | ___PPC_RA(d) | ___PPC_RS(a) | __PPC_SH64(i))
#define PPC_RAW_RLWINM(d, a, i, mb, me)	(0x54000000 | ___PPC_RA(d) | ___PPC_RS(a) | __PPC_SH(i) | __PPC_MB(mb) | __PPC_ME(me))
#define PPC_RAW_RLWINM_DOT(d, a, i, mb, me) \
					(0x54000001 | ___PPC_RA(d) | ___PPC_RS(a) | __PPC_SH(i) | __PPC_MB(mb) | __PPC_ME(me))
#define PPC_RAW_RLWIMI(d, a, i, mb, me) (0x50000000 | ___PPC_RA(d) | ___PPC_RS(a) | __PPC_SH(i) | __PPC_MB(mb) | __PPC_ME(me))
#define PPC_RAW_RLDICL(d, a, i, mb)     (0x78000000 | ___PPC_RA(d) | ___PPC_RS(a) | __PPC_SH64(i) | __PPC_MB64(mb))
#define PPC_RAW_RLDICL_DOT(d, a, i, mb) (0x78000000 | ___PPC_RA(d) | ___PPC_RS(a) | __PPC_SH64(i) | __PPC_MB64(mb) | 0x1)
#define PPC_RAW_RLDICR(d, a, i, me)     (0x78000004 | ___PPC_RA(d) | ___PPC_RS(a) | __PPC_SH64(i) | __PPC_ME64(me))

/* slwi = rlwinm Rx, Ry, n, 0, 31-n */
#define PPC_RAW_SLWI(d, a, i)		PPC_RAW_RLWINM(d, a, i, 0, 31-(i))
/* srwi = rlwinm Rx, Ry, 32-n, n, 31 */
#define PPC_RAW_SRWI(d, a, i)		PPC_RAW_RLWINM(d, a, 32-(i), i, 31)
/* sldi = rldicr Rx, Ry, n, 63-n */
#define PPC_RAW_SLDI(d, a, i)		PPC_RAW_RLDICR(d, a, i, 63-(i))
/* sldi = rldicl Rx, Ry, 64-n, n */
#define PPC_RAW_SRDI(d, a, i)		PPC_RAW_RLDICL(d, a, 64-(i), i)

#define PPC_RAW_NEG(d, a)		(0x7c0000d0 | ___PPC_RT(d) | ___PPC_RA(a))

#define PPC_RAW_MFSPR(d, spr)		(0x7c0002a6 | ___PPC_RT(d) | __PPC_SPR(spr))
#define PPC_RAW_MTSPR(spr, d)		(0x7c0003a6 | ___PPC_RS(d) | __PPC_SPR(spr))
#define PPC_RAW_EIEIO()			(0x7c0006ac)

/* bcl 20,31,$+4 */
#define PPC_RAW_BCL4()			(0x429f0005)
#define PPC_RAW_BRANCH(offset)		(0x48000000 | PPC_LI(offset))
#define PPC_RAW_BL(offset)		(0x48000001 | PPC_LI(offset))
#define PPC_RAW_TW(t0, a, b)		(0x7c000008 | ___PPC_RS(t0) | ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_RAW_TRAP()			PPC_RAW_TW(31, 0, 0)
#define PPC_RAW_SETB(t, bfa)		(0x7c000100 | ___PPC_RT(t) | ___PPC_RA((bfa) << 2))

#ifdef CONFIG_PPC32
#define PPC_RAW_STL		PPC_RAW_STW
#define PPC_RAW_STLU		PPC_RAW_STWU
#define PPC_RAW_LL		PPC_RAW_LWZ
#define PPC_RAW_CMPLI		PPC_RAW_CMPWI
#else
#define PPC_RAW_STL		PPC_RAW_STD
#define PPC_RAW_STLU		PPC_RAW_STDU
#define PPC_RAW_LL		PPC_RAW_LD
#define PPC_RAW_CMPLI		PPC_RAW_CMPDI
#endif

/* Deal with instructions that older assemblers aren't aware of */
#define	PPC_BCCTR_FLUSH		stringify_in_c(.long PPC_INST_BCCTR_FLUSH)
#define	PPC_CP_ABORT		stringify_in_c(.long PPC_RAW_CP_ABORT)
#define	PPC_COPY(a, b)		stringify_in_c(.long PPC_RAW_COPY(a, b))
#define PPC_DARN(t, l)		stringify_in_c(.long PPC_RAW_DARN(t, l))
#define	PPC_DCBAL(a, b)		stringify_in_c(.long PPC_RAW_DCBAL(a, b))
#define	PPC_DCBZL(a, b)		stringify_in_c(.long PPC_RAW_DCBZL(a, b))
#define	PPC_DIVDE(t, a, b)	stringify_in_c(.long PPC_RAW_DIVDE(t, a, b))
#define	PPC_DIVDEU(t, a, b)	stringify_in_c(.long PPC_RAW_DIVDEU(t, a, b))
#define PPC_DSSALL		stringify_in_c(.long PPC_INST_DSSALL)
#define PPC_LQARX(t, a, b, eh)	stringify_in_c(.long PPC_RAW_LQARX(t, a, b, eh))
#define PPC_STQCX(t, a, b)	stringify_in_c(.long PPC_RAW_STQCX(t, a, b))
#define PPC_MADDHD(t, a, b, c)	stringify_in_c(.long PPC_RAW_MADDHD(t, a, b, c))
#define PPC_MADDHDU(t, a, b, c)	stringify_in_c(.long PPC_RAW_MADDHDU(t, a, b, c))
#define PPC_MADDLD(t, a, b, c)	stringify_in_c(.long PPC_RAW_MADDLD(t, a, b, c))
#define PPC_MSGSND(b)		stringify_in_c(.long PPC_RAW_MSGSND(b))
#define PPC_MSGSYNC		stringify_in_c(.long PPC_RAW_MSGSYNC)
#define PPC_MSGCLR(b)		stringify_in_c(.long PPC_RAW_MSGCLR(b))
#define PPC_MSGSNDP(b)		stringify_in_c(.long PPC_RAW_MSGSNDP(b))
#define PPC_MSGCLRP(b)		stringify_in_c(.long PPC_RAW_MSGCLRP(b))
#define PPC_PASTE(a, b)		stringify_in_c(.long PPC_RAW_PASTE(a, b))
#define PPC_POPCNTB(a, s)	stringify_in_c(.long PPC_RAW_POPCNTB(a, s))
#define PPC_POPCNTD(a, s)	stringify_in_c(.long PPC_RAW_POPCNTD(a, s))
#define PPC_POPCNTW(a, s)	stringify_in_c(.long PPC_RAW_POPCNTW(a, s))
#define PPC_RFCI		stringify_in_c(.long PPC_RAW_RFCI)
#define PPC_RFDI		stringify_in_c(.long PPC_RAW_RFDI)
#define PPC_RFMCI		stringify_in_c(.long PPC_RAW_RFMCI)
#define PPC_TLBILX(t, a, b)	stringify_in_c(.long PPC_RAW_TLBILX(t, a, b))
#define PPC_TLBILX_ALL(a, b)	PPC_TLBILX(0, a, b)
#define PPC_TLBILX_PID(a, b)	PPC_TLBILX(1, a, b)
#define PPC_TLBILX_LPID		stringify_in_c(.long PPC_RAW_TLBILX_LPID)
#define PPC_TLBILX_VA(a, b)	PPC_TLBILX(3, a, b)
#define PPC_WAIT_v203		stringify_in_c(.long PPC_RAW_WAIT_v203)
#define PPC_WAIT(w, p)		stringify_in_c(.long PPC_RAW_WAIT(w, p))
#define PPC_TLBIE(lp, a) 	stringify_in_c(.long PPC_RAW_TLBIE(lp, a))
#define	PPC_TLBIE_5(rb, rs, ric, prs, r) \
				stringify_in_c(.long PPC_RAW_TLBIE_5(rb, rs, ric, prs, r))
#define	PPC_TLBIEL(rb,rs,ric,prs,r) \
				stringify_in_c(.long PPC_RAW_TLBIEL(rb, rs, ric, prs, r))
#define PPC_TLBIEL_v205(rb, l)	stringify_in_c(.long PPC_RAW_TLBIEL_v205(rb, l))
#define PPC_TLBSRX_DOT(a, b)	stringify_in_c(.long PPC_RAW_TLBSRX_DOT(a, b))
#define PPC_TLBIVAX(a, b)	stringify_in_c(.long PPC_RAW_TLBIVAX(a, b))

#define PPC_ERATWE(s, a, w)	stringify_in_c(.long PPC_RAW_ERATWE(s, a, w))
#define PPC_ERATRE(s, a, w)	stringify_in_c(.long PPC_RAW_ERATRE(a, a, w))
#define PPC_ERATILX(t, a, b)	stringify_in_c(.long PPC_RAW_ERATILX(t, a, b))
#define PPC_ERATIVAX(s, a, b)	stringify_in_c(.long PPC_RAW_ERATIVAX(s, a, b))
#define PPC_ERATSX(t, a, w)	stringify_in_c(.long PPC_RAW_ERATSX(t, a, w))
#define PPC_ERATSX_DOT(t, a, w)	stringify_in_c(.long PPC_RAW_ERATSX_DOT(t, a, w))
#define PPC_SLBFEE_DOT(t, b)	stringify_in_c(.long PPC_RAW_SLBFEE_DOT(t, b))
#define __PPC_SLBFEE_DOT(t, b)	stringify_in_c(.long __PPC_RAW_SLBFEE_DOT(t, b))
#define PPC_ICBT(c, a, b)	stringify_in_c(.long PPC_RAW_ICBT(c, a, b))
/* PASemi instructions */
#define LBZCIX(t, a, b)		stringify_in_c(.long PPC_RAW_LBZCIX(t, a, b))
#define STBCIX(s, a, b)		stringify_in_c(.long PPC_RAW_STBCIX(s, a, b))
#define PPC_DCBFPS(a, b)	stringify_in_c(.long PPC_RAW_DCBFPS(a, b))
#define PPC_DCBSTPS(a, b)	stringify_in_c(.long PPC_RAW_DCBSTPS(a, b))
#define PPC_PHWSYNC		stringify_in_c(.long PPC_RAW_PHWSYNC)
#define PPC_PLWSYNC		stringify_in_c(.long PPC_RAW_PLWSYNC)
#define STXVD2X(s, a, b)	stringify_in_c(.long PPC_RAW_STXVD2X(s, a, b))
#define LXVD2X(s, a, b)		stringify_in_c(.long PPC_RAW_LXVD2X(s, a, b))
#define MFVRD(a, t)		stringify_in_c(.long PPC_RAW_MFVRD(a, t))
#define MTVRD(t, a)		stringify_in_c(.long PPC_RAW_MTVRD(t, a))
#define VPMSUMW(t, a, b)	stringify_in_c(.long PPC_RAW_VPMSUMW(t, a, b))
#define VPMSUMD(t, a, b)	stringify_in_c(.long PPC_RAW_VPMSUMD(t, a, b))
#define XXLOR(t, a, b)		stringify_in_c(.long PPC_RAW_XXLOR(t, a, b))
#define XXSWAPD(t, a)		stringify_in_c(.long PPC_RAW_XXSWAPD(t, a))
#define XVCPSGNDP(t, a, b)	stringify_in_c(.long (PPC_RAW_XVCPSGNDP(t, a, b)))

#define VPERMXOR(vrt, vra, vrb, vrc)				\
	stringify_in_c(.long (PPC_RAW_VPERMXOR(vrt, vra, vrb, vrc)))

#define PPC_NAP			stringify_in_c(.long PPC_RAW_NAP)
#define PPC_SLEEP		stringify_in_c(.long PPC_RAW_SLEEP)
#define PPC_WINKLE		stringify_in_c(.long PPC_RAW_WINKLE)

#define PPC_STOP		stringify_in_c(.long PPC_RAW_STOP)

/* BHRB instructions */
#define PPC_CLRBHRB		stringify_in_c(.long PPC_RAW_CLRBHRB)
#define PPC_MFBHRBE(r, n)	stringify_in_c(.long PPC_RAW_MFBHRBE(r, n))

/* Transactional memory instructions */
#define TRECHKPT		stringify_in_c(.long PPC_RAW_TRECHKPT)
#define TRECLAIM(r)		stringify_in_c(.long PPC_RAW_TRECLAIM(r))
#define TABORT(r)		stringify_in_c(.long PPC_RAW_TABORT(r))

/* book3e thread control instructions */
#define MTTMR(tmr, r)		stringify_in_c(.long PPC_RAW_MTTMR(tmr, r))
#define MFTMR(tmr, r)		stringify_in_c(.long PPC_RAW_MFTMR(tmr, r))

/* Coprocessor instructions */
#define PPC_ICSWX(s, a, b)	stringify_in_c(.long PPC_RAW_ICSWX(s, a, b))
#define PPC_ICSWEPX(s, a, b)	stringify_in_c(.long PPC_RAW_ICSWEPX(s, a, b))

#define PPC_SLBIA(IH)	stringify_in_c(.long PPC_RAW_SLBIA(IH))

/*
 * These may only be used on ISA v3.0 or later (aka. CPU_FTR_ARCH_300, radix
 * implies CPU_FTR_ARCH_300). USER/GUEST invalidates may only be used by radix
 * mode (on HPT these would also invalidate various SLBEs which may not be
 * desired).
 */
#define PPC_ISA_3_0_INVALIDATE_ERAT	PPC_SLBIA(7)
#define PPC_RADIX_INVALIDATE_ERAT_USER	PPC_SLBIA(3)
#define PPC_RADIX_INVALIDATE_ERAT_GUEST	PPC_SLBIA(6)

#define VCMPEQUD_RC(vrt, vra, vrb)	stringify_in_c(.long PPC_RAW_VCMPEQUD_RC(vrt, vra, vrb))

#define VCMPEQUB_RC(vrt, vra, vrb)	stringify_in_c(.long PPC_RAW_VCMPEQUB_RC(vrt, vra, vrb))

#endif /* _ASM_POWERPC_PPC_OPCODE_H */
