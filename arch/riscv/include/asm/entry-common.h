/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_RISCV_ENTRY_COMMON_H
#define _ASM_RISCV_ENTRY_COMMON_H

#include <asm/stacktrace.h>

void handle_page_fault(struct pt_regs *regs);
void handle_break(struct pt_regs *regs);

#ifdef CONFIG_RISCV_MISALIGNED
int handle_misaligned_load(struct pt_regs *regs);
int handle_misaligned_store(struct pt_regs *regs);
#else
static inline int handle_misaligned_load(struct pt_regs *regs)
{
	return -1;
}
static inline int handle_misaligned_store(struct pt_regs *regs)
{
	return -1;
}
#endif

#endif /* _ASM_RISCV_ENTRY_COMMON_H */
