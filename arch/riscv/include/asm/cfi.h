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

#ifdef CONFIG_CFI_CLANG
enum bug_trap_type handle_cfi_failure(struct pt_regs *regs);
#define __bpfcall
static inline int cfi_get_offset(void)
{
	return 4;
}

#define cfi_get_offset cfi_get_offset
extern u32 cfi_bpf_hash;
extern u32 cfi_bpf_subprog_hash;
extern u32 cfi_get_func_hash(void *func);
#else
static inline enum bug_trap_type handle_cfi_failure(struct pt_regs *regs)
{
	return BUG_TRAP_TYPE_NONE;
}

#define cfi_bpf_hash 0U
#define cfi_bpf_subprog_hash 0U
static inline u32 cfi_get_func_hash(void *func)
{
	return 0;
}
#endif /* CONFIG_CFI_CLANG */

#endif /* _ASM_RISCV_CFI_H */
