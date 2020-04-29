/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __ASM_CSKY_TRAPS_H
#define __ASM_CSKY_TRAPS_H

#define VEC_RESET	0
#define VEC_ALIGN	1
#define VEC_ACCESS	2
#define VEC_ZERODIV	3
#define VEC_ILLEGAL	4
#define VEC_PRIV	5
#define VEC_TRACE	6
#define VEC_BREAKPOINT	7
#define VEC_UNRECOVER	8
#define VEC_SOFTRESET	9
#define VEC_AUTOVEC	10
#define VEC_FAUTOVEC	11
#define VEC_HWACCEL	12

#define	VEC_TLBMISS	14
#define	VEC_TLBMODIFIED	15

#define VEC_TRAP0	16
#define VEC_TRAP1	17
#define VEC_TRAP2	18
#define VEC_TRAP3	19

#define	VEC_TLBINVALIDL	20
#define	VEC_TLBINVALIDS	21

#define VEC_PRFL	29
#define VEC_FPE		30

extern void *vec_base[];

#define VEC_INIT(i, func) \
do { \
	vec_base[i] = (void *)func; \
} while (0)

void csky_alignment(struct pt_regs *regs);

#endif /* __ASM_CSKY_TRAPS_H */
