/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef __UAPI_ASM_BREAK_H
#define __UAPI_ASM_BREAK_H

#define BRK_DEFAULT		0	/* Used as default */
#define BRK_BUG			1	/* Used by BUG() */
#define BRK_KDB			2	/* Used in KDB_ENTER() */
#define BRK_MATHEMU		3	/* Used by FPU emulator */
#define BRK_USERBP		4	/* User bp (used by debuggers) */
#define BRK_SSTEPBP		5	/* User bp (used by debuggers) */
#define BRK_OVERFLOW		6	/* Overflow check */
#define BRK_DIVZERO		7	/* Divide by zero check */
#define BRK_RANGE		8	/* Range error check */
#define BRK_MULOVFL		9	/* Multiply overflow */
#define BRK_KPROBE_BP		10	/* Kprobe break */
#define BRK_KPROBE_SSTEPBP	11	/* Kprobe single step break */
#define BRK_UPROBE_BP		12	/* See <asm/uprobes.h> */
#define BRK_UPROBE_XOLBP	13	/* See <asm/uprobes.h> */

#endif /* __UAPI_ASM_BREAK_H */
