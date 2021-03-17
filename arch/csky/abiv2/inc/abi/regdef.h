/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __ASM_CSKY_REGDEF_H
#define __ASM_CSKY_REGDEF_H

#define syscallid	r7
#define regs_syscallid(regs) regs->regs[3]
#define regs_fp(regs) regs->regs[4]

/*
 * PSR format:
 * | 31 | 30-24 | 23-16 | 15 14 | 13-10 | 9 | 8-0 |
 *   S              VEC     TM            MM
 *
 *   S: Super Mode
 * VEC: Exception Number
 *  TM: Trace Mode
 *  MM: Memory unaligned addr access
 */
#define DEFAULT_PSR_VALUE	0x80000200

#define SYSTRACE_SAVENUM	5

#define TRAP0_SIZE		4

#endif /* __ASM_CSKY_REGDEF_H */
