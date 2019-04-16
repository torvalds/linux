// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/ftrace.h>
#include <linux/uaccess.h>

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
void prepare_ftrace_return(unsigned long *parent, unsigned long self_addr,
			   unsigned long frame_pointer)
{
	unsigned long return_hooker = (unsigned long)&return_to_handler;
	unsigned long old;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	old = *parent;

	if (!function_graph_enter(old, self_addr,
			*(unsigned long *)frame_pointer, parent)) {
		/*
		 * For csky-gcc function has sub-call:
		 * subi	sp,	sp, 8
		 * stw	r8,	(sp, 0)
		 * mov	r8,	sp
		 * st.w r15,	(sp, 0x4)
		 * push	r15
		 * jl	_mcount
		 * We only need set *parent for resume
		 *
		 * For csky-gcc function has no sub-call:
		 * subi	sp,	sp, 4
		 * stw	r8,	(sp, 0)
		 * mov	r8,	sp
		 * push	r15
		 * jl	_mcount
		 * We need set *parent and *(frame_pointer + 4) for resume,
		 * because lr is resumed twice.
		 */
		*parent = return_hooker;
		frame_pointer += 4;
		if (*(unsigned long *)frame_pointer == old)
			*(unsigned long *)frame_pointer = return_hooker;
	}
}
#endif

/* _mcount is defined in abi's mcount.S */
extern void _mcount(void);
EXPORT_SYMBOL(_mcount);
