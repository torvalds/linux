/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */

#ifndef __ASM_BRK_IMM_H
#define __ASM_BRK_IMM_H

/*
 * #imm16 values used for BRK instruction generation
 * 0x004: for installing kprobes
 * 0x005: for installing uprobes
 * 0x006: for kprobe software single-step
 * Allowed values for kgdb are 0x400 - 0x7ff
 * 0x100: for triggering a fault on purpose (reserved)
 * 0x400: for dynamic BRK instruction
 * 0x401: for compile time BRK instruction
 * 0x800: kernel-mode BUG() and WARN() traps
 * 0x9xx: tag-based KASAN trap (allowed values 0x900 - 0x9ff)
 * 0x8xxx: Control-Flow Integrity traps
 */
#define KPROBES_BRK_IMM			0x004
#define UPROBES_BRK_IMM			0x005
#define KPROBES_BRK_SS_IMM		0x006
#define FAULT_BRK_IMM			0x100
#define KGDB_DYN_DBG_BRK_IMM		0x400
#define KGDB_COMPILED_DBG_BRK_IMM	0x401
#define BUG_BRK_IMM			0x800
#define KASAN_BRK_IMM			0x900
#define KASAN_BRK_MASK			0x0ff

#define CFI_BRK_IMM_TARGET		GENMASK(4, 0)
#define CFI_BRK_IMM_TYPE		GENMASK(9, 5)
#define CFI_BRK_IMM_BASE		0x8000
#define CFI_BRK_IMM_MASK		(CFI_BRK_IMM_TARGET | CFI_BRK_IMM_TYPE)

#endif
