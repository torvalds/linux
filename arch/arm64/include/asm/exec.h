/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/exec.h
 *
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_EXEC_H
#define __ASM_EXEC_H

#include <linux/sched.h>

extern unsigned long arch_align_stack(unsigned long sp);
void uao_thread_switch(struct task_struct *next);

#endif	/* __ASM_EXEC_H */
