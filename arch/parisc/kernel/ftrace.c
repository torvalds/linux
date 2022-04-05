// SPDX-License-Identifier: GPL-2.0
/*
 * Code for tracing calls in Linux kernel.
 * Copyright (C) 2009-2016 Helge Deller <deller@gmx.de>
 *
 * based on code for x86 which is:
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 *
 * future possible enhancements:
 *	- add CONFIG_STACK_TRACER
 */

#include <linux/init.h>
#include <linux/ftrace.h>
#include <linux/uaccess.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>

#include <asm/assembly.h>
#include <asm/sections.h>
#include <asm/ftrace.h>
#include <asm/patch.h>

#define __hot __attribute__ ((__section__ (".text.hot")))

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
/*
 * Hook the return address and push it in the stack of return addrs
 * in current thread info.
 */
static void __hot prepare_ftrace_return(unsigned long *parent,
					unsigned long self_addr)
{
	unsigned long old;
	extern int parisc_return_to_handler;

	if (unlikely(ftrace_graph_is_dead()))
		return;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	old = *parent;

	if (!function_graph_enter(old, self_addr, 0, NULL))
		/* activate parisc_return_to_handler() as return point */
		*parent = (unsigned long) &parisc_return_to_handler;
}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

void notrace __hot ftrace_function_trampoline(unsigned long parent,
				unsigned long self_addr,
				unsigned long org_sp_gr3,
				struct pt_regs *regs)
{
#ifndef CONFIG_DYNAMIC_FTRACE
	extern ftrace_func_t ftrace_trace_function;
#endif
	extern struct ftrace_ops *function_trace_op;

	if (function_trace_op->flags & FTRACE_OPS_FL_ENABLED &&
	    ftrace_trace_function != ftrace_stub)
		ftrace_trace_function(self_addr, parent,
				function_trace_op, regs);

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	if (ftrace_graph_return != (trace_func_graph_ret_t) ftrace_stub ||
	    ftrace_graph_entry != ftrace_graph_entry_stub) {
		unsigned long *parent_rp;

		/* calculate pointer to %rp in stack */
		parent_rp = (unsigned long *) (org_sp_gr3 - RP_OFFSET);
		/* sanity check: parent_rp should hold parent */
		if (*parent_rp != parent)
			return;

		prepare_ftrace_return(parent_rp, self_addr);
		return;
	}
#endif
}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
int ftrace_enable_ftrace_graph_caller(void)
{
	return 0;
}

int ftrace_disable_ftrace_graph_caller(void)
{
	return 0;
}
#endif

#ifdef CONFIG_DYNAMIC_FTRACE

int __init ftrace_dyn_arch_init(void)
{
	return 0;
}
int ftrace_update_ftrace_func(ftrace_func_t func)
{
	return 0;
}

int ftrace_modify_call(struct dyn_ftrace *rec, unsigned long old_addr,
			unsigned long addr)
{
	return 0;
}

unsigned long ftrace_call_adjust(unsigned long addr)
{
	return addr+(FTRACE_PATCHABLE_FUNCTION_SIZE-1)*4;
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	u32 insn[FTRACE_PATCHABLE_FUNCTION_SIZE];
	u32 *tramp;
	int size, ret, i;
	void *ip;

#ifdef CONFIG_64BIT
	unsigned long addr2 =
		(unsigned long)dereference_function_descriptor((void *)addr);

	u32 ftrace_trampoline[] = {
		0x73c10208, /* std,ma r1,100(sp) */
		0x0c2110c1, /* ldd -10(r1),r1 */
		0xe820d002, /* bve,n (r1) */
		addr2 >> 32,
		addr2 & 0xffffffff,
		0xe83f1fd7, /* b,l,n .-14,r1 */
	};

	u32 ftrace_trampoline_unaligned[] = {
		addr2 >> 32,
		addr2 & 0xffffffff,
		0x37de0200, /* ldo 100(sp),sp */
		0x73c13e01, /* std r1,-100(sp) */
		0x34213ff9, /* ldo -4(r1),r1 */
		0x50213fc1, /* ldd -20(r1),r1 */
		0xe820d002, /* bve,n (r1) */
		0xe83f1fcf, /* b,l,n .-20,r1 */
	};

	BUILD_BUG_ON(ARRAY_SIZE(ftrace_trampoline_unaligned) >
				FTRACE_PATCHABLE_FUNCTION_SIZE);
#else
	u32 ftrace_trampoline[] = {
		(u32)addr,
		0x6fc10080, /* stw,ma r1,40(sp) */
		0x48213fd1, /* ldw -18(r1),r1 */
		0xe820c002, /* bv,n r0(r1) */
		0xe83f1fdf, /* b,l,n .-c,r1 */
	};
#endif

	BUILD_BUG_ON(ARRAY_SIZE(ftrace_trampoline) >
				FTRACE_PATCHABLE_FUNCTION_SIZE);

	size = sizeof(ftrace_trampoline);
	tramp = ftrace_trampoline;

#ifdef CONFIG_64BIT
	if (rec->ip & 0x4) {
		size = sizeof(ftrace_trampoline_unaligned);
		tramp = ftrace_trampoline_unaligned;
	}
#endif

	ip = (void *)(rec->ip + 4 - size);

	ret = probe_kernel_read(insn, ip, size);
	if (ret)
		return ret;

	for (i = 0; i < size / 4; i++) {
		if (insn[i] != INSN_NOP)
			return -EINVAL;
	}

	__patch_text_multiple(ip, tramp, size);
	return 0;
}

int ftrace_make_nop(struct module *mod, struct dyn_ftrace *rec,
		    unsigned long addr)
{
	u32 insn[FTRACE_PATCHABLE_FUNCTION_SIZE];
	int i;

	for (i = 0; i < ARRAY_SIZE(insn); i++)
		insn[i] = INSN_NOP;

	__patch_text((void *)rec->ip, INSN_NOP);
	__patch_text_multiple((void *)rec->ip + 4 - sizeof(insn),
			      insn, sizeof(insn)-4);
	return 0;
}
#endif

#ifdef CONFIG_KPROBES_ON_FTRACE
void kprobe_ftrace_handler(unsigned long ip, unsigned long parent_ip,
			   struct ftrace_ops *ops, struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb;
	struct kprobe *p = get_kprobe((kprobe_opcode_t *)ip);

	if (unlikely(!p) || kprobe_disabled(p))
		return;

	if (kprobe_running()) {
		kprobes_inc_nmissed_count(p);
		return;
	}

	__this_cpu_write(current_kprobe, p);

	kcb = get_kprobe_ctlblk();
	kcb->kprobe_status = KPROBE_HIT_ACTIVE;

	regs->iaoq[0] = ip;
	regs->iaoq[1] = ip + 4;

	if (!p->pre_handler || !p->pre_handler(p, regs)) {
		regs->iaoq[0] = ip + 4;
		regs->iaoq[1] = ip + 8;

		if (unlikely(p->post_handler)) {
			kcb->kprobe_status = KPROBE_HIT_SSDONE;
			p->post_handler(p, regs, 0);
		}
	}
	__this_cpu_write(current_kprobe, NULL);
}
NOKPROBE_SYMBOL(kprobe_ftrace_handler);

int arch_prepare_kprobe_ftrace(struct kprobe *p)
{
	p->ainsn.insn = NULL;
	return 0;
}
#endif
