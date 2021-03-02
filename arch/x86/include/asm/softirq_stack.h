/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SOFTIRQ_STACK_H
#define _ASM_X86_SOFTIRQ_STACK_H

#ifdef CONFIG_X86_64
# include <asm/irq_stack.h>
#else
# include <asm-generic/softirq_stack.h>
#endif

#endif
