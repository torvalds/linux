/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 */
#ifndef _ASM_C6X_BUG_H
#define _ASM_C6X_BUG_H

#include <linux/linkage.h>
#include <asm-generic/bug.h>

struct pt_regs;

extern void die(char *str, struct pt_regs *fp, int nr);
extern asmlinkage int process_exception(struct pt_regs *regs);
extern asmlinkage void enable_exception(void);

#endif /* _ASM_C6X_BUG_H */
