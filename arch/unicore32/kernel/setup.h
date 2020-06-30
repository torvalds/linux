/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/kernel/setup.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#ifndef __UNICORE_KERNEL_SETUP_H__
#define __UNICORE_KERNEL_SETUP_H__

#include <asm/hwdef-copro.h>

extern void paging_init(void);
extern void puv3_core_init(void);
extern void cpu_init(void);

extern void puv3_ps2_init(void);
extern void pci_puv3_preinit(void);
extern void __init puv3_init_gpio(void);

extern void setup_mm_for_reboot(void);

extern char __stubs_start[], __stubs_end[];
extern char __vectors_start[], __vectors_end[];

extern void kernel_thread_helper(void);

extern void __init early_signal_init(void);

extern asmlinkage void __backtrace(void);
extern asmlinkage void c_backtrace(unsigned long fp, const char *loglvl);

extern void __show_regs(struct pt_regs *);

#endif
