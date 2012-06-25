/*
 * Copyright 2009 Freescale Semicondutor, Inc.
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

#define	R0	0
#define	R1	1
#define	R2	2
#define	R3	3
#define	R4	4
#define	R5	5
#define	R6	6
#define	R7	7
#define	R8	8
#define	R9	9
#define	R10	10
#define	R11	11
#define	R12	12
#define	R13	13
#define	R14	14
#define	R15	15
#define	R16	16
#define	R17	17
#define	R18	18
#define	R19	19
#define	R20	20
#define	R21	21
#define	R22	22
#define	R23	23
#define	R24	24
#define	R25	25
#define	R26	26
#define	R27	27
#define	R28	28
#define	R29	29
#define	R30	30
#define	R31	31

/* sorted alphabetically */
#define PPC_INST_DCBA			0x7c0005ec
#define PPC_INST_DCBA_MASK		0xfc0007fe
#define PPC_INST_DCBAL			0x7c2005ec
#define PPC_INST_DCBZL			0x7c2007ec
#define PPC_INST_ISEL			0x7c00001e
#define PPC_INST_ISEL_MASK		0xfc00003e
#define PPC_INST_LDARX			0x7c0000a8
#define PPC_INST_LSWI			0x7c0004aa
#define PPC_INST_LSWX			0x7c00042a
#define PPC_INST_LWARX			0x7c000028
#define PPC_INST_LWSYNC			0x7c2004ac
#define PPC_INST_LXVD2X			0x7c000698
#define PPC_INST_MCRXR			0x7c000400
#define PPC_INST_MCRXR_MASK		0xfc0007fe
#define PPC_INST_MFSPR_PVR		0x7c1f42a6
#define PPC_INST_MFSPR_PVR_MASK		0xfc1fffff
#define PPC_INST_MSGSND			0x7c00019c
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

#define PPC_INST_NAP			0x4c000364
#define PPC_INST_SLEEP			0x4c0003a4

/* A2 specific instructions */
#define PPC_INST_ERATWE			0x7c0001a6
#define PPC_INST_ERATRE			0x7c000166
#define PPC_INST_ERATILX		0x7c000066
#define PPC_INST_ERATIVAX		0x7c000666
#define PPC_INST_ERATSX			0x7c000126
#define PPC_INST_ERATSX_DOT		0x7c000127

/* Misc instructions for BPF compiler */
#define PPC_INST_LD			0xe8000000
#define PPC_INST_LHZ			0xa0000000
#define PPC_INST_LWZ			0x80000000
#define PPC_INST_STD			0xf8000000
#define PPC_INST_STDU			0xf8000001
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
#define PPC_INST_DIVWU			0x7c0003d6
#define PPC_INST_RLWINM			0x54000000
#define PPC_INST_RLDICR			0x78000004
#define PPC_INST_SLW			0x7c000030
#define PPC_INST_SRW			0x7c000430
#define PPC_INST_AND			0x7c000038
#define PPC_INST_ANDDOT			0x7c000039
#define PPC_INST_OR			0x7c000378
#define PPC_INST_ANDI			0x70000000
#define PPC_INST_ORI			0x60000000
#define PPC_INST_ORIS			0x64000000
#define PPC_INST_NEG			0x7c0000d0
#define PPC_INST_BRANCH			0x48000000
#define PPC_INST_BRANCH_COND		0x40800000

/* macros to insert fields into opcodes */
#define __PPC_RA(a)	(((a) & 0x1f) << 16)
#define __PPC_RB(b)	(((b) & 0x1f) << 11)
#define __PPC_RS(s)	(((s) & 0x1f) << 21)
#define __PPC_RT(s)	__PPC_RS(s)
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
#define	PPC_DCBAL(a, b)		stringify_in_c(.long PPC_INST_DCBAL | \
					__PPC_RA(a) | __PPC_RB(b))
#define	PPC_DCBZL(a, b)		stringify_in_c(.long PPC_INST_DCBZL | \
					__PPC_RA(a) | __PPC_RB(b))
#define PPC_LDARX(t, a, b, eh)	stringify_in_c(.long PPC_INST_LDARX | \
					__PPC_RT(t) | __PPC_RA(a) | \
					__PPC_RB(b) | __PPC_EH(eh))
#define PPC_LWARX(t, a, b, eh)	stringify_in_c(.long PPC_INST_LWARX | \
					__PPC_RT(t) | __PPC_RA(a) | \
					__PPC_RB(b) | __PPC_EH(eh))
#define PPC_MSGSND(b)		stringify_in_c(.long PPC_INST_MSGSND | \
					__PPC_RB(b))
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
					__PPC_T_TLB(t) | __PPC_RA(a) | __PPC_RB(b))
#define PPC_TLBILX_ALL(a, b)	PPC_TLBILX(0, a, b)
#define PPC_TLBILX_PID(a, b)	PPC_TLBILX(1, a, b)
#define PPC_TLBILX_VA(a, b)	PPC_TLBILX(3, a, b)
#define PPC_WAIT(w)		stringify_in_c(.long PPC_INST_WAIT | \
					__PPC_WC(w))
#define PPC_TLBIE(lp,a) 	stringify_in_c(.long PPC_INST_TLBIE | \
					       __PPC_RB(a) | __PPC_RS(lp))
#define PPC_TLBSRX_DOT(a,b)	stringify_in_c(.long PPC_INST_TLBSRX_DOT | \
					__PPC_RA(a) | __PPC_RB(b))
#define PPC_TLBIVAX(a,b)	stringify_in_c(.long PPC_INST_TLBIVAX | \
					__PPC_RA(a) | __PPC_RB(b))

#define PPC_ERATWE(s, a, w)	stringify_in_c(.long PPC_INST_ERATWE | \
					__PPC_RS(s) | __PPC_RA(a) | __PPC_WS(w))
#define PPC_ERATRE(s, a, w)	stringify_in_c(.long PPC_INST_ERATRE | \
					__PPC_RS(s) | __PPC_RA(a) | __PPC_WS(w))
#define PPC_ERATILX(t, a, b)	stringify_in_c(.long PPC_INST_ERATILX | \
					__PPC_T_TLB(t) | __PPC_RA(a) | \
					__PPC_RB(b))
#define PPC_ERATIVAX(s, a, b)	stringify_in_c(.long PPC_INST_ERATIVAX | \
					__PPC_RS(s) | __PPC_RA(a) | __PPC_RB(b))
#define PPC_ERATSX(t, a, w)	stringify_in_c(.long PPC_INST_ERATSX | \
					__PPC_RS(t) | __PPC_RA(a) | __PPC_RB(b))
#define PPC_ERATSX_DOT(t, a, w)	stringify_in_c(.long PPC_INST_ERATSX_DOT | \
					__PPC_RS(t) | __PPC_RA(a) | __PPC_RB(b))
#define PPC_SLBFEE_DOT(t, b)	stringify_in_c(.long PPC_INST_SLBFEE | \
					__PPC_RT(t) | __PPC_RB(b))

/*
 * Define what the VSX XX1 form instructions will look like, then add
 * the 128 bit load store instructions based on that.
 */
#define VSX_XX1(s, a, b)	(__PPC_XS(s) | __PPC_RA(a) | __PPC_RB(b))
#define VSX_XX3(t, a, b)	(__PPC_XT(t) | __PPC_XA(a) | __PPC_XB(b))
#define STXVD2X(s, a, b)	stringify_in_c(.long PPC_INST_STXVD2X | \
					       VSX_XX1((s), (a), (b)))
#define LXVD2X(s, a, b)		stringify_in_c(.long PPC_INST_LXVD2X | \
					       VSX_XX1((s), (a), (b)))
#define XXLOR(t, a, b)		stringify_in_c(.long PPC_INST_XXLOR | \
					       VSX_XX3((t), (a), (b)))

#define PPC_NAP			stringify_in_c(.long PPC_INST_NAP)
#define PPC_SLEEP		stringify_in_c(.long PPC_INST_SLEEP)

#endif /* _ASM_POWERPC_PPC_OPCODE_H */
