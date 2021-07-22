/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_BUG_H
#define __ASM_CSKY_BUG_H

#include <linux/compiler.h>
#include <linux/const.h>
#include <linux/types.h>

#define BUG()				\
do {					\
	asm volatile ("bkpt\n");	\
	unreachable();			\
} while (0)

#define HAVE_ARCH_BUG

#include <asm-generic/bug.h>

struct pt_regs;

void die(struct pt_regs *regs, const char *str);
void do_trap(struct pt_regs *regs, int signo, int code, unsigned long addr);

void show_regs(struct pt_regs *regs);
void show_code(struct pt_regs *regs);

#endif /* __ASM_CSKY_BUG_H */
