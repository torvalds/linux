/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _POWERPC_PERF_CALLCHAIN_H
#define _POWERPC_PERF_CALLCHAIN_H

int read_user_stack_slow(void __user *ptr, void *buf, int nb);
void perf_callchain_user_64(struct perf_callchain_entry_ctx *entry,
			    struct pt_regs *regs);
void perf_callchain_user_32(struct perf_callchain_entry_ctx *entry,
			    struct pt_regs *regs);

static inline bool invalid_user_sp(unsigned long sp)
{
	unsigned long mask = is_32bit_task() ? 3 : 7;
	unsigned long top = STACK_TOP - (is_32bit_task() ? 16 : 32);

	return (!sp || (sp & mask) || (sp > top));
}

#endif /* _POWERPC_PERF_CALLCHAIN_H */
