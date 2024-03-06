/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_OPENRISC_BUG_H
#define __ASM_OPENRISC_BUG_H

#include <asm-generic/bug.h>

struct pt_regs;

void __noreturn die(const char *str, struct pt_regs *regs, long err);

#endif /* __ASM_OPENRISC_BUG_H */
