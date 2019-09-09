/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _ASM_ERROR_INJECTION_H
#define _ASM_ERROR_INJECTION_H

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <asm/ptrace.h>
#include <asm-generic/error-injection.h>

void override_function_with_return(struct pt_regs *regs);

#endif /* _ASM_ERROR_INJECTION_H */
