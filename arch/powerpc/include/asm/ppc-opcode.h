/*
 * Copyright 2009 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * provides masks and opcode images for use by code generation, emulation
 * and for instructions that older assemblers might not know about
 */
#ifndef _ASM_POWERPC_PPC_OPCODE_H
#define _ASM_POWERPC_PPC_OPCODE_H

#include <linux/stringify.h>
#include <asm/asm-compat.h>

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

/* opcode and xopcode for instructions */
#define OP_TRAP 3
#define OP_TRAP_64 2

#define OP_31_XOP_TRAP      4
#define OP_31_XOP_LWZX      23
#define OP_31_XOP_DCBST     54
#define OP_31_XOP_LWZUX     55
#define OP_31_XOP_TRAP_64   68
#define OP_31_XOP_DCBF      86
#define OP_31_XOP_LBZX      87
#define OP_31_XOP_STWX      151
#define OP_31_XOP_STBX      215
#define OP_31_XOP_LBZUX     119
#define OP_31_XOP_STBUX     247
#define OP_31_XOP_LHZX      279
#define OP_31_XOP_LHZUX     311
#define OP_31_XOP_MFSPR     339
#define OP_31_XOP_LHAX      343
#define OP_31_XOP_LHAUX     375
#define OP_31_XOP_STHX      407
#define OP_31_XOP_STHUX     439
#define OP_31_XOP_MTSPR     467
#define OP_31_XOP_DCBI      470
#define OP_31_XOP_LWBRX     534
#define OP_31_XOP_TLBSYNC   566
#define OP_31_XOP_STWBRX    662
#define OP_31_XOP_LHBRX     790
#define OP_31_XOP_STHBRX    918

#define OP_LWZ  32
#define OP_LD   58
#define OP_LWZU 33
#define OP_LBZ  34
#define OP_LBZU 35
#define OP_STW  36
#define OP_STWU 37
#define OP_STD  62
#define OP_STB  38
#define OP_STBU 39
#define OP_LHZ  40
#define OP_LHZU 41
#define OP_LHA  42
#define OP_LHAU 43
#define OP_STH  44
#define OP_STHU 45

/* sorted alphabetically */
#define PPC_INST_BHRBE			0x7c00025c
#define PPC_INST_CLRBHRB		0x7c00035c
#define PPC_INST_CP_ABORT		0x7c00068c
#define PPC_INST_DCBA			0x7c0005ec
#define PPC_INST_DCBA_MASK		0xfc0007fe
#define PPC_INST_DCBAL			0x7c2005ec
#define PPC_INST_DCBZL			0x7c2007ec
#define PPC_INST_ICBT			0x7c00002c
#define PPC_INST_ICSWX			0x7c00032d
#define PPC_INST_ICSWEPX		0x7c00076d
#define PPC_INST_ISEL			0x7c00001e
#define PPC_INST_ISEL_MASK		0xfc00003e
#define PPC_INST_LDARX			0x7c0000a8
#define PPC_INST_LSWI			0x7c0004aa
#define PPC_INST_LSWX			0x7c00042a
#define PPC_INST_LWARX			0x7c000028
#define PPC_INST_LWSYNC			0x7c2004ac
#define PPC_INST_SYNC			0x7c0004ac
#define PPC_INST_SYNC_MASK		0xfc0007fe
#define PPC_INST_LXVD2X			0x7c000698
#define PPC_INST_MCRXR			0x7c000400
#define PPC_INST_MCRXR_MASK		0xfc0007fe
#define PPC_INST_MFSPR_PVR		0x7c1f42a6
#define PPC_INST_MFSPR_PVR_MASK		0xfc1fffff
#define PPC_INST_MFTMR			0x7c0002dc
#define PPC_INST_MSGSND			0x7c00019c
#define PPC_INST_MSGCLR			0x7c0001dc
#define PPC_INST_MSGSNDP		0x7c00011c
#define PPC_INST_MTTMR			0x7c0003dc
#define PPC_INST_NOP			0x60000000
#define PPC_INST_POPCNTB		0x7c0000f4
#define PPC_INST_POPCNTB_MASK		0xfc0007fe
#define PPC_INST_POPCNTD		0x7c0003f4
#define PPC_INST_POPCNTW		0x7c0002f4
#define PPC_INST_RFCI			0x4c000066
#define PPC_INST_RFDI			0x4c00004e
#define PPC_INST_RFMCI			0x4c00004c
#define PPC_INST_MFSPR_DSCR		0x7c1102a6
#define PPC_INST_MFSPR_DSCR_MASK	0xfc1fffff
#define PPC_INST_MTSPR_DSCR		0x7c1103a6
#define PPC_INST_MTSPR_DSCR_MASK	0xfc1fffff
#define PPC_INST_MFSPR_DSCR_USER	0x7c0302a6
#define PPC_INST_MFSPR_DSCR_USER_MASK	0xfc1fffff
#define PPC_INST_MTSPR_DSCR_USER	0x7c0303a6
#define PPC_INST_MTSPR_DSCR_USER_MASK	0xfc1fffff
#define PPC_INST_SLBFEE			0x7c0007a7

#define PPC_INST_STRING			0x7c00042a
#define PPC_INST_STRING_MASK		0xfc0007fe
#define PPC_INST_STRING_GEN_MASK	0xfc00067e

#define PPC_INST_STSWI			0x7c0005aa
#define PPC_INST_STSWX			0x7c00052a
#define PPC_INST_STXVD2X		0x7c000798
#define PPC_INST_TLBIE			0x7c000264
#define PPC_INST_TLBILX			0x7c000024
#define PPC_INST_WAIT			0x7c00007c
#define PPC_INST_TLBIVAX		0x7c000624
#define PPC_INST_TLBSRX_DOT		0x7c0006a5
#define PPC_INST_XXLOR			0xf0000510
#define PPC_INST_XXSWAPD		0xf0000250
#define PPC_INST_XVCPSGNDP		0xf0000780
#define PPC_INST_TRECHKPT		0x7c0007dd
#define PPC_INST_TRECLAIM		0x7c00075d
#define PPC_INST_TABORT			0x7c00071d

#define PPC_INST_NAP			0x4c000364
#define PPC_INST_SLEEP			0x4c0003a4
#define PPC_INST_WINKLE			0x4c0003e4

/* A2 specific instructions */
#define PPC_INST_ERATWE			0x7c0001a6
#define PPC_INST_ERATRE			0x7c000166
#define PPC_INST_ERATILX		0x7c000066
#define PPC_INST_ERATIVAX		0x7c000666
#define PPC_INST_ERATSX			0x7c000126
#define PPC_INST_ERATSX_DOT		0x7c000127

/* Misc instructions for BPF compiler */
#define PPC_INST_LBZ			0x88000000
#define PPC_INST_LD			0xe8000000
#define PPC_INST_LHZ			0xa0000000
#define PPC_INST_LHBRX			0x7c00062c
#define PPC_INST_LWZ			0x80000000
#define PPC_INST_STD			0xf8000000
#define PPC_INST_STDU			0xf8000001
#define PPC_INST_STW			0x90000000
#define PPC_INST_STWU			0x94000000
#define PPC_INST_MFLR			0x7c0802a6
#define PPC_INST_MTLR			0x7c0803a6
#define PPC_INST_CMPWI			0x2c000000
#define PPC_INST_CMPDI			0x2c200000
#define PPC_INST_CMPLW			0x7c000040
#define PPC_INST_CMPLWI			0x28000000
#define PPC_INST_ADDI			0x38000000
#define PPC_INST_ADDIS			0x3c000000
#define PPC_INST_ADD			0x7c000214
#define PPC_INST_SUB			0x7c000050
#define PPC_INST_BLR			0x4e800020
#define PPC_INST_BLRL			0x4e800021
#define PPC_INST_MULLW			0x7c0001d6
#define PPC_INST_MULHWU			0x7c000016
#define PPC_INST_MULLI			0x1c000000
#define PPC_INST_DIVWU			0x7c000396
#define PPC_INST_RLWINM			0x54000000
#define PPC_INST_RLDICR			0x78000004
#define PPC_INST_SLW			0x7c000030
#define PPC_INST_SRW			0x7c000430
#define PPC_INST_AND			0x7c000038
#define PPC_INST_ANDDOT			0x7c000039
#define PPC_INST_OR			0x7c000378
#define PPC_INST_XOR			0x7c000278
#define PPC_INST_ANDI			0x70000000
#define PPC_INST_ORI			0x60000000
#define PPC_INST_ORIS			0x64000000
#define PPC_INST_XORI			0x68000000
#define PPC_INST_XORIS			0x6c000000
#define PPC_INST_NEG			0x7c0000d0
#define PPC_INST_BRANCH			0x48000000
#define PPC_INST_BRANCH_COND		0x40800000
#define PPC_INST_LBZCIX			0x7c0006aa
#define PPC_INST_STBCIX			0x7c0007aa

/* macros to insert fields into opcodes */
#define ___PPC_RA(a)	(((a) & 0x1f) << 16)
#define ___PPC_RB(b)	(((b) & 0x1f) << 11)
#define ___PPC_RS(s)	(((s) & 0x1f) << 21)
#define ___PPC_RT(t)	___PPC_RS(t)
#define __PPC_RA(a)	___PPC_RA(__REG_##a)
#define __PPC_RA0(a)	___PPC_RA(__REGA0_##a)
#define __PPC_RB(b)	___PPC_RB(__REG_##b)
#define __PPC_RS(s)	___PPC_RS(__REG_##s)
#define __PPC_RT(t)	___PPC_RT(__REG_##t)
#define __PPC_XA(a)	((((a) & 0x1f) << 16) | (((a) & 0x20) >> 3))
#define __PPC_XB(b)	((((b) & 0x1f) << 11) | (((b) & 0x20) >> 4))
#define __PPC_XS(s)	((((s) & 0x1f) << 21) | (((s) & 0x20) >> 5))
#define __PPC_XT(s)	__PPC_XS(s)
#define __PPC_T_TLB(t)	(((t) & 0x3) << 21)
#define __PPC_WC(w)	(((w) & 0x3) << 21)
#define __PPC_WS(w)	(((w) & 0x1f) << 11)
#define __PPC_SH(s)	__PPC_WS(s)
#define __PPC_MB(s)	(((s) & 0x1f) << 6)
#define __PPC_ME(s)	(((s) & 0x1f) << 1)
#define __PPC_BI(s)	(((s) & 0x1f) << 16)
#define __PPC_CT(t)	(((t) & 0x0f) << 21)

/*
 * Only use the larx hint bit on 64bit CPUs. e500v1/v2 based CPUs will treat a
 * larx with EH set as an illegal instruction.
 */
#ifdef CONFIG_PPC64
#define __PPC_EH(eh)	(((eh) & 0x1) << 0)
#else
#define __PPC_EH(eh)	0
#endif

/* Deal with instructions that older assemblers aren't aware of */
#define	PPC_CP_ABORT		stringify_in_c(.long PPC_INST_CP_ABORT)
#define	PPC_DCBAL(a, b)		stringify_in_c(.long PPC_INST_DCBAL | \
					__PPC_RA(a) | __PPC_RB(b))
#define	PPC_DCBZL(a, b)		stringify_in_c(.long PPC_INST_DCBZL | \
					__PPC_RA(a) | __PPC_RB(b))
#define PPC_LDARX(t, a, b, eh)	stringify_in_c(.long PPC_INST_LDARX | \
					___PPC_RT(t) | ___PPC_RA(a) | \
					___PPC_RB(b) | __PPC_EH(eh))
#define PPC_LWARX(t, a, b, eh)	stringify_in_c(.long PPC_INST_LWARX | \
					___PPC_RT(t) | ___PPC_RA(a) | \
					___PPC_RB(b) | __PPC_EH(eh))
#define PPC_MSGSND(b)		stringify_in_c(.long PPC_INST_MSGSND | \
					___PPC_RB(b))
#define PPC_MSGCLR(b)		stringify_in_c(.long PPC_INST_MSGCLR | \
					___PPC_RB(b))
#define PPC_MSGSNDP(b)		stringify_in_c(.long PPC_INST_MSGSNDP | \
					___PPC_RB(b))
#define PPC_POPCNTB(a, s)	stringify_in_c(.long PPC_INST_POPCNTB | \
					__PPC_RA(a) | __PPC_RS(s))
#define PPC_POPCNTD(a, s)	stringify_in_c(.long PPC_INST_POPCNTD | \
					__PPC_RA(a) | __PPC_RS(s))
#define PPC_POPCNTW(a, s)	stringify_in_c(.long PPC_INST_POPCNTW | \
					__PPC_RA(a) | __PPC_RS(s))
#define PPC_RFCI		stringify_in_c(.long PPC_INST_RFCI)
#define PPC_RFDI		stringify_in_c(.long PPC_INST_RFDI)
#define PPC_RFMCI		stringify_in_c(.long PPC_INST_RFMCI)
#define PPC_TLBILX(t, a, b)	stringify_in_c(.long PPC_INST_TLBILX | \
					__PPC_T_TLB(t) | __PPC_RA0(a) | __PPC_RB(b))
#define PPC_TLBILX_ALL(a, b)	PPC_TLBILX(0, a, b)
#define PPC_TLBILX_PID(a, b)	PPC_TLBILX(1, a, b)
#define PPC_TLBILX_VA(a, b)	PPC_TLBILX(3, a, b)
#define PPC_WAIT(w)		stringify_in_c(.long PPC_INST_WAIT | \
					__PPC_WC(w))
#define PPC_TLBIE(lp,a) 	stringify_in_c(.long PPC_INST_TLBIE | \
					       ___PPC_RB(a) | ___PPC_RS(lp))
#define PPC_TLBSRX_DOT(a,b)	stringify_in_c(.long PPC_INST_TLBSRX_DOT | \
					__PPC_RA0(a) | __PPC_RB(b))
#define PPC_TLBIVAX(a,b)	stringify_in_c(.long PPC_INST_TLBIVAX | \
					__PPC_RA0(a) | __PPC_RB(b))

#define PPC_ERATWE(s, a, w)	stringify_in_c(.long PPC_INST_ERATWE | \
					__PPC_RS(s) | __PPC_RA(a) | __PPC_WS(w))
#define PPC_ERATRE(s, a, w)	stringify_in_c(.long PPC_INST_ERATRE | \
					__PPC_RS(s) | __PPC_RA(a) | __PPC_WS(w))
#define PPC_ERATILX(t, a, b)	stringify_in_c(.long PPC_INST_ERATILX | \
					__PPC_T_TLB(t) | __PPC_RA0(a) | \
					__PPC_RB(b))
#define PPC_ERATIVAX(s, a, b)	stringify_in_c(.long PPC_INST_ERATIVAX | \
					__PPC_RS(s) | __PPC_RA0(a) | __PPC_RB(b))
#define PPC_ERATSX(t, a, w)	stringify_in_c(.long PPC_INST_ERATSX | \
					__PPC_RS(t) | __PPC_RA0(a) | __PPC_RB(b))
#define PPC_ERATSX_DOT(t, a, w)	stringify_in_c(.long PPC_INST_ERATSX_DOT | \
					__PPC_RS(t) | __PPC_RA0(a) | __PPC_RB(b))
#define PPC_SLBFEE_DOT(t, b)	stringify_in_c(.long PPC_INST_SLBFEE | \
					__PPC_RT(t) | __PPC_RB(b))
#define PPC_ICBT(c,a,b)		stringify_in_c(.long PPC_INST_ICBT | \
				       __PPC_CT(c) | __PPC_RA0(a) | __PPC_RB(b))
/* PASemi instructions */
#define LBZCIX(t,a,b)		stringify_in_c(.long PPC_INST_LBZCIX | \
				       __PPC_RT(t) | __PPC_RA(a) | __PPC_RB(b))
#define STBCIX(s,a,b)		stringify_in_c(.long PPC_INST_STBCIX | \
				       __PPC_RS(s) | __PPC_RA(a) | __PPC_RB(b))

/*
 * Define what the VSX XX1 form instructions will look like, then add
 * the 128 bit load store instructions based on that.
 */
#define VSX_XX1(s, a, b)	(__PPC_XS(s) | __PPC_RA(a) | __PPC_RB(b))
#define VSX_XX3(t, a, b)	(__PPC_XT(t) | __PPC_XA(a) | __PPC_XB(b))
#define STXVD2X(s, a, b)	stringify_in_c(.long PPC_INST_STXVD2X | \
					       VSX_XX1((s), a, b))
#define LXVD2X(s, a, b)		stringify_in_c(.long PPC_INST_LXVD2X | \
					       VSX_XX1((s), a, b))
#define XXLOR(t, a, b)		stringify_in_c(.long PPC_INST_XXLOR | \
					       VSX_XX3((t), a, b))
#define XXSWAPD(t, a)		stringify_in_c(.long PPC_INST_XXSWAPD | \
					       VSX_XX3((t), a, a))
#define XVCPSGNDP(t, a, b)	stringify_in_c(.long (PPC_INST_XVCPSGNDP | \
					       VSX_XX3((t), (a), (b))))

#define PPC_NAP			stringify_in_c(.long PPC_INST_NAP)
#define PPC_SLEEP		stringify_in_c(.long PPC_INST_SLEEP)
#define PPC_WINKLE		stringify_in_c(.long PPC_INST_WINKLE)

/* BHRB instructions */
#define PPC_CLRBHRB		stringify_in_c(.long PPC_INST_CLRBHRB)
#define PPC_MFBHRBE(r, n)	stringify_in_c(.long PPC_INST_BHRBE | \
						__PPC_RT(r) | \
							(((n) & 0x3ff) << 11))

/* Transactional memory instructions */
#define TRECHKPT		stringify_in_c(.long PPC_INST_TRECHKPT)
#define TRECLAIM(r)		stringify_in_c(.long PPC_INST_TRECLAIM \
					       | __PPC_RA(r))
#define TABORT(r)		stringify_in_c(.long PPC_INST_TABORT \
					       | __PPC_RA(r))

/* book3e thread control instructions */
#define TMRN(x)			((((x) & 0x1f) << 16) | (((x) & 0x3e0) << 6))
#define MTTMR(tmr, r)		stringify_in_c(.long PPC_INST_MTTMR | \
					       TMRN(tmr) | ___PPC_RS(r))
#define MFTMR(tmr, r)		stringify_in_c(.long PPC_INST_MFTMR | \
					       TMRN(tmr) | ___PPC_RT(r))

/* Coprocessor instructions */
#define PPC_ICSWX(s, a, b)	stringify_in_c(.long PPC_INST_ICSWX |	\
					       ___PPC_RS(s) |		\
					       ___PPC_RA(a) |		\
					       ___PPC_RB(b))
#define PPC_ICSWEPX(s, a, b)	stringify_in_c(.long PPC_INST_ICSWEPX | \
					       ___PPC_RS(s) |		\
					       ___PPC_RA(a) |		\
					       ___PPC_RB(b))


#endif /* _ASM_POWERPC_PPC_OPCODE_H */
