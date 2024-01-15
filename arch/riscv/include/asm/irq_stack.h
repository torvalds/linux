/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_RISCV_IRQ_STACK_H
#define _ASM_RISCV_IRQ_STACK_H

#include <linux/bug.h>
#include <linux/gfp.h>
#include <linux/kconfig.h>
#include <linux/vmalloc.h>
#include <linux/pgtable.h>
#include <asm/thread_info.h>

DECLARE_PER_CPU(ulong *, irq_stack_ptr);

asmlinkage void call_on_irq_stack(struct pt_regs *regs,
				  void (*func)(struct pt_regs *));

#ifdef CONFIG_VMAP_STACK
/*
 * To ensure that VMAP'd stack overflow detection works correctly, all VMAP'd
 * stacks need to have the same alignment.
 */
static inline unsigned long *arch_alloc_vmap_stack(size_t stack_size, int node)
{
	void *p;

	p = __vmalloc_node(stack_size, THREAD_ALIGN, THREADINFO_GFP, node,
			__builtin_return_address(0));
	return kasan_reset_tag(p);
}
#endif /* CONFIG_VMAP_STACK */

#endif /* _ASM_RISCV_IRQ_STACK_H */
