/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_CFI_H
#define _ASM_RISCV_CFI_H

/*
 * Clang Control Flow Integrity (CFI) support.
 *
 * Copyright (C) 2023 Google LLC
 */
#include <linux/bug.h>

struct pt_regs;

#ifdef CONFIG_CFI
enum bug_trap_type handle_cfi_failure(struct pt_regs *regs);
#define __bpfcall
#else
static inline enum bug_trap_type handle_cfi_failure(struct pt_regs *regs)
{
	return BUG_TRAP_TYPE_NONE;
}
#endif /* CONFIG_CFI */

#endif /* _ASM_RISCV_CFI_H */
