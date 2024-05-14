/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Stack trace functions.
 *
 * Copyright 2018, Murilo Opsfelder Araujo, IBM Corporation.
 */

#ifndef _ASM_POWERPC_STACKTRACE_H
#define _ASM_POWERPC_STACKTRACE_H

void show_user_instructions(struct pt_regs *regs);

#endif /* _ASM_POWERPC_STACKTRACE_H */
