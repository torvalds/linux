/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __ABI_CSKY_PTRACE_H
#define __ABI_CSKY_PTRACE_H

struct switch_stack {
#ifdef CONFIG_CPU_HAS_HILO
	unsigned long rhi;
	unsigned long rlo;
	unsigned long cr14;
	unsigned long pad;
#endif
	unsigned long r4;
	unsigned long r5;
	unsigned long r6;
	unsigned long r7;
	unsigned long r8;
	unsigned long r9;
	unsigned long r10;
	unsigned long r11;

	unsigned long r15;
	unsigned long r16;
	unsigned long r17;
	unsigned long r26;
	unsigned long r27;
	unsigned long r28;
	unsigned long r29;
	unsigned long r30;
};
#endif /* __ABI_CSKY_PTRACE_H */
