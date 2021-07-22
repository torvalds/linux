/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _POWERPC_PERF_CALLCHAIN_H
#define _POWERPC_PERF_CALLCHAIN_H

int read_user_stack_slow(const void __user *ptr, void *buf, int nb);
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

/*
 * On 32-bit we just access the address and let hash_page create a
 * HPTE if necessary, so there is no need to fall back to reading
 * the page tables.  Since this is called at interrupt level,
 * do_page_fault() won't treat a DSI as a page fault.
 */
static inline int __read_user_stack(const void __user *ptr, void *ret,
				    size_t size)
{
	unsigned long addr = (unsigned long)ptr;
	int rc;

	if (addr > TASK_SIZE - size || (addr & (size - 1)))
		return -EFAULT;

	rc = copy_from_user_nofault(ret, ptr, size);

	if (IS_ENABLED(CONFIG_PPC64) && !radix_enabled() && rc)
		return read_user_stack_slow(ptr, ret, size);

	return rc;
}

#endif /* _POWERPC_PERF_CALLCHAIN_H */
