/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 SiFive, Inc.
 */
#ifndef __ASM_HEAD_H
#define __ASM_HEAD_H

#include <linux/linkage.h>
#include <linux/init.h>

extern atomic_t hart_lottery;

asmlinkage void do_page_fault(struct pt_regs *regs);
asmlinkage void __init setup_vm(uintptr_t dtb_pa);

extern void *__cpu_up_stack_pointer[];
extern void *__cpu_up_task_pointer[];

void __init parse_dtb(void);

#endif /* __ASM_HEAD_H */
