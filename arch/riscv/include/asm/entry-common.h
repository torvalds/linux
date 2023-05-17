/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_RISCV_ENTRY_COMMON_H
#define _ASM_RISCV_ENTRY_COMMON_H

#include <asm/stacktrace.h>

void handle_page_fault(struct pt_regs *regs);
void handle_break(struct pt_regs *regs);

#endif /* _ASM_RISCV_ENTRY_COMMON_H */
