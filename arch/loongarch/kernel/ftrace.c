// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */

#include <linux/init.h>
#include <linux/ftrace.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/cacheflush.h>
#include <asm/inst.h>
#include <asm/loongarch.h>
#include <asm/syscall.h>

#include <asm-generic/sections.h>

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

/*
 * As `call _mcount` follows LoongArch psABI, ra-saved operation and
 * stack operation can be found before this insn.
 */

static int ftrace_get_parent_ra_addr(unsigned long insn_addr, int *ra_off)
{
	int limit = 32;
	union loongarch_instruction *insn;

	insn = (union loongarch_instruction *)insn_addr;

	do {
		insn--;
		limit--;

		if (is_ra_save_ins(insn))
			*ra_off = -((1 << 12) - insn->reg2i12_format.immediate);

	} while (!is_stack_alloc_ins(insn) && limit);

	if (!limit)
		return -EINVAL;

	return 0;
}

void prepare_ftrace_return(unsigned long self_addr,
		unsigned long callsite_sp, unsigned long old)
{
	int ra_off;
	unsigned long return_hooker = (unsigned long)&return_to_handler;

	if (unlikely(ftrace_graph_is_dead()))
		return;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	if (ftrace_get_parent_ra_addr(self_addr, &ra_off))
		goto out;

	if (!function_graph_enter(old, self_addr, 0, NULL))
		*(unsigned long *)(callsite_sp + ra_off) = return_hooker;

	return;

out:
	ftrace_graph_stop();
	WARN_ON(1);
}
#endif	/* CONFIG_FUNCTION_GRAPH_TRACER */
