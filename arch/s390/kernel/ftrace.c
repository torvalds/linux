// SPDX-License-Identifier: GPL-2.0
/*
 * Dynamic function tracer architecture backend.
 *
 * Copyright IBM Corp. 2009,2014
 *
 *   Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>,
 *		Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/moduleloader.h>
#include <linux/hardirq.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/kprobes.h>
#include <trace/syscall.h>
#include <asm/asm-offsets.h>
#include <asm/cacheflush.h>
#include <asm/set_memory.h>
#include "entry.h"

/*
 * To generate function prologue either gcc's hotpatch feature (since gcc 4.8)
 * or a combination of -pg -mrecord-mcount -mnop-mcount -mfentry flags
 * (since gcc 9 / clang 10) is used.
 * In both cases the original and also the disabled function prologue contains
 * only a single six byte instruction and looks like this:
 * >	brcl	0,0			# offset 0
 * To enable ftrace the code gets patched like above and afterwards looks
 * like this:
 * >	brasl	%r0,ftrace_caller	# offset 0
 *
 * The instruction will be patched by ftrace_make_call / ftrace_make_nop.
 * The ftrace function gets called with a non-standard C function call ABI
 * where r0 contains the return address. It is also expected that the called
 * function only clobbers r0 and r1, but restores r2-r15.
 * For module code we can't directly jump to ftrace caller, but need a
 * trampoline (ftrace_plt), which clobbers also r1.
 */

void *ftrace_func __read_mostly = ftrace_stub;
unsigned long ftrace_plt;

int ftrace_modify_call(struct dyn_ftrace *rec, unsigned long old_addr,
		       unsigned long addr)
{
	return 0;
}

int ftrace_make_nop(struct module *mod, struct dyn_ftrace *rec,
		    unsigned long addr)
{
	struct ftrace_insn orig, new, old;

	if (copy_from_kernel_nofault(&old, (void *) rec->ip, sizeof(old)))
		return -EFAULT;
	/* Replace ftrace call with a nop. */
	ftrace_generate_call_insn(&orig, rec->ip);
	ftrace_generate_nop_insn(&new);

	/* Verify that the to be replaced code matches what we expect. */
	if (memcmp(&orig, &old, sizeof(old)))
		return -EINVAL;
	s390_kernel_write((void *) rec->ip, &new, sizeof(new));
	return 0;
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	struct ftrace_insn orig, new, old;

	if (copy_from_kernel_nofault(&old, (void *) rec->ip, sizeof(old)))
		return -EFAULT;
	/* Replace nop with an ftrace call. */
	ftrace_generate_nop_insn(&orig);
	ftrace_generate_call_insn(&new, rec->ip);

	/* Verify that the to be replaced code matches what we expect. */
	if (memcmp(&orig, &old, sizeof(old)))
		return -EINVAL;
	s390_kernel_write((void *) rec->ip, &new, sizeof(new));
	return 0;
}

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	ftrace_func = func;
	return 0;
}

int __init ftrace_dyn_arch_init(void)
{
	return 0;
}

#ifdef CONFIG_MODULES

static int __init ftrace_plt_init(void)
{
	unsigned int *ip;

	ftrace_plt = (unsigned long) module_alloc(PAGE_SIZE);
	if (!ftrace_plt)
		panic("cannot allocate ftrace plt\n");
	ip = (unsigned int *) ftrace_plt;
	ip[0] = 0x0d10e310; /* basr 1,0; lg 1,10(1); br 1 */
	ip[1] = 0x100a0004;
	ip[2] = 0x07f10000;
	ip[3] = FTRACE_ADDR >> 32;
	ip[4] = FTRACE_ADDR & 0xffffffff;
	set_memory_ro(ftrace_plt, 1);
	return 0;
}
device_initcall(ftrace_plt_init);

#endif /* CONFIG_MODULES */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
/*
 * Hook the return address and push it in the stack of return addresses
 * in current thread info.
 */
unsigned long prepare_ftrace_return(unsigned long ra, unsigned long sp,
				    unsigned long ip)
{
	if (unlikely(ftrace_graph_is_dead()))
		goto out;
	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		goto out;
	ip -= MCOUNT_INSN_SIZE;
	if (!function_graph_enter(ra, ip, 0, (void *) sp))
		ra = (unsigned long) return_to_handler;
out:
	return ra;
}
NOKPROBE_SYMBOL(prepare_ftrace_return);

/*
 * Patch the kernel code at ftrace_graph_caller location. The instruction
 * there is branch relative on condition. To enable the ftrace graph code
 * block, we simply patch the mask field of the instruction to zero and
 * turn the instruction into a nop.
 * To disable the ftrace graph code the mask field will be patched to
 * all ones, which turns the instruction into an unconditional branch.
 */
int ftrace_enable_ftrace_graph_caller(void)
{
	u8 op = 0x04; /* set mask field to zero */

	s390_kernel_write(__va(ftrace_graph_caller)+1, &op, sizeof(op));
	return 0;
}

int ftrace_disable_ftrace_graph_caller(void)
{
	u8 op = 0xf4; /* set mask field to all ones */

	s390_kernel_write(__va(ftrace_graph_caller)+1, &op, sizeof(op));
	return 0;
}

#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

#ifdef CONFIG_KPROBES_ON_FTRACE
void kprobe_ftrace_handler(unsigned long ip, unsigned long parent_ip,
		struct ftrace_ops *ops, struct ftrace_regs *fregs)
{
	struct kprobe_ctlblk *kcb;
	struct pt_regs *regs;
	struct kprobe *p;
	int bit;

	bit = ftrace_test_recursion_trylock(ip, parent_ip);
	if (bit < 0)
		return;

	regs = ftrace_get_regs(fregs);
	preempt_disable_notrace();
	p = get_kprobe((kprobe_opcode_t *)ip);
	if (unlikely(!p) || kprobe_disabled(p))
		goto out;

	if (kprobe_running()) {
		kprobes_inc_nmissed_count(p);
		goto out;
	}

	__this_cpu_write(current_kprobe, p);

	kcb = get_kprobe_ctlblk();
	kcb->kprobe_status = KPROBE_HIT_ACTIVE;

	instruction_pointer_set(regs, ip);

	if (!p->pre_handler || !p->pre_handler(p, regs)) {

		instruction_pointer_set(regs, ip + MCOUNT_INSN_SIZE);

		if (unlikely(p->post_handler)) {
			kcb->kprobe_status = KPROBE_HIT_SSDONE;
			p->post_handler(p, regs, 0);
		}
	}
	__this_cpu_write(current_kprobe, NULL);
out:
	preempt_enable_notrace();
	ftrace_test_recursion_unlock(bit);
}
NOKPROBE_SYMBOL(kprobe_ftrace_handler);

int arch_prepare_kprobe_ftrace(struct kprobe *p)
{
	p->ainsn.insn = NULL;
	return 0;
}
#endif
