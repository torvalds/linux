/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/include/asm/traps.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#ifndef __UNICORE_TRAP_H__
#define __UNICORE_TRAP_H__

extern void __init early_trap_init(void);
extern void dump_backtrace_entry(unsigned long where,
		unsigned long from, unsigned long frame);

extern void do_DataAbort(unsigned long addr, unsigned int fsr,
		 struct pt_regs *regs);
#endif
