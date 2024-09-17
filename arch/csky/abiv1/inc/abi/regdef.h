/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_REGDEF_H
#define __ASM_CSKY_REGDEF_H

#ifdef __ASSEMBLY__
#define syscallid	r1
#else
#define syscallid	"r1"
#endif

#define regs_syscallid(regs) regs->regs[9]
#define regs_fp(regs) regs->regs[2]

/*
 * PSR format:
 * | 31 | 30-24 | 23-16 | 15 14 | 13-0 |
 *   S     CPID     VEC     TM
 *
 *    S: Super Mode
 * CPID: Coprocessor id, only 15 for MMU
 *  VEC: Exception Number
 *   TM: Trace Mode
 */
#define DEFAULT_PSR_VALUE	0x8f000000

#define SYSTRACE_SAVENUM	2

#define TRAP0_SIZE		2

#endif /* __ASM_CSKY_REGDEF_H */
