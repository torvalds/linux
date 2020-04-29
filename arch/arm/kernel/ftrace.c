/*
 * Dynamic function tracing support.
 *
 * Copyright (C) 2008 Abhishek Sagar <sagar.abhishek@gmail.com>
 * Copyright (C) 2010 Rabin Vincent <rabin@rab.in>
 *
 * For licencing details, see COPYING.
 *
 * Defines low-level handling of mcount calls when the kernel
 * is compiled with the -pg flag. When using dynamic ftrace, the
 * mcount call-sites get patched with NOP till they are enabled.
 * All code mutation routines here are called under stop_machine().
 */

#include <linux/ftrace.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/stop_machine.h>

#include <asm/cacheflush.h>
#include <asm/opcodes.h>
#include <asm/ftrace.h>
#include <asm/insn.h>
#include <asm/set_memory.h>
#include <asm/patch.h>

#ifdef CONFIG_THUMB2_KERNEL
#define	NOP		0xf85deb04	/* pop.w {lr} */
#else
#define	NOP		0xe8bd4000	/* pop {lr} */
#endif

#ifdef CONFIG_DYNAMIC_FTRACE

static int __ftrace_modify_code(void *data)
{
	int *command = data;

	ftrace_modify_all_code(*command);

	return 0;
}

void arch_ftrace_update_code(int command)
{
	stop_machine(__ftrace_modify_code, &command, NULL);
}

static unsigned long ftrace_nop_replace(struct dyn_ftrace *rec)
{
	return NOP;
}

static unsigned long adjust_address(struct dyn_ftrace *rec, unsigned long addr)
{
	return addr;
}

int ftrace_arch_code_modify_prepare(void)
{
	return 0;
}

int ftrace_arch_code_modify_post_process(void)
{
	/* Make sure any TLB misses during machine stop are cleared. */
	flush_tlb_all();
	return 0;
}

static unsigned long ftrace_call_replace(unsigned long pc, unsigned long addr)
{
	return arm_gen_branch_link(pc, addr);
}

static int ftrace_modify_code(unsigned long pc, unsigned long old,
			      unsigned long new, bool validate)
{
	unsigned long replaced;

	if (IS_ENABLED(CONFIG_THUMB2_KERNEL))
		old = __opcode_to_mem_thumb32(old);
	else
		old = __opcode_to_mem_arm(old);

	if (validate) {
		if (probe_kernel_read(&replaced, (void *)pc, MCOUNT_INSN_SIZE))
			return -EFAULT;

		if (replaced != old)
			return -EINVAL;
	}

	__patch_text((void *)pc, new);

	return 0;
}

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned long pc;
	unsigned long new;
	int ret;

	pc = (unsigned long)&ftrace_call;
	new = ftrace_call_replace(pc, (unsigned long)func);

	ret = ftrace_modify_code(pc, 0, new, false);

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
	if (!ret) {
		pc = (unsigned long)&ftrace_regs_call;
		new = ftrace_call_replace(pc, (unsigned long)func);

		ret = ftrace_modify_code(pc, 0, new, false);
	}
#endif

	return ret;
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long new, old;
	unsigned long ip = rec->ip;

	old = ftrace_nop_replace(rec);

	new = ftrace_call_replace(ip, adjust_address(rec, addr));

	return ftrace_modify_code(rec->ip, old, new, true);
}

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS

int ftrace_modify_call(struct dyn_ftrace *rec, unsigned long old_addr,
				unsigned long addr)
{
	unsigned long new, old;
	unsigned long ip = rec->ip;

	old = ftrace_call_replace(ip, adjust_address(rec, old_addr));

	new = ftrace_call_replace(ip, adjust_address(rec, addr));

	return ftrace_modify_code(rec->ip, old, new, true);
}

#endif

int ftrace_make_nop(struct module *mod,
		    struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long ip = rec->ip;
	unsigned long old;
	unsigned long new;
	int ret;

	old = ftrace_call_replace(ip, adjust_address(rec, addr));
	new = ftrace_nop_replace(rec);
	ret = ftrace_modify_code(ip, old, new, true);

	return ret;
}

int __init ftrace_dyn_arch_init(void)
{
	return 0;
}
#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
void prepare_ftrace_return(unsigned long *parent, unsigned long self_addr,
			   unsigned long frame_pointer)
{
	unsigned long return_hooker = (unsigned long) &return_to_handler;
	unsigned long old;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	old = *parent;
	*parent = return_hooker;

	if (function_graph_enter(old, self_addr, frame_pointer, NULL))
		*parent = old;
}

#ifdef CONFIG_DYNAMIC_FTRACE
extern unsigned long ftrace_graph_call;
extern unsigned long ftrace_graph_call_old;
extern void ftrace_graph_caller_old(void);
extern unsigned long ftrace_graph_regs_call;
extern void ftrace_graph_regs_caller(void);

static int __ftrace_modify_caller(unsigned long *callsite,
				  void (*func) (void), bool enable)
{
	unsigned long caller_fn = (unsigned long) func;
	unsigned long pc = (unsigned long) callsite;
	unsigned long branch = arm_gen_branch(pc, caller_fn);
	unsigned long nop = 0xe1a00000;	/* mov r0, r0 */
	unsigned long old = enable ? nop : branch;
	unsigned long new = enable ? branch : nop;

	return ftrace_modify_code(pc, old, new, true);
}

static int ftrace_modify_graph_caller(bool enable)
{
	int ret;

	ret = __ftrace_modify_caller(&ftrace_graph_call,
				     ftrace_graph_caller,
				     enable);

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
	if (!ret)
		ret = __ftrace_modify_caller(&ftrace_graph_regs_call,
				     ftrace_graph_regs_caller,
				     enable);
#endif


	return ret;
}

int ftrace_enable_ftrace_graph_caller(void)
{
	return ftrace_modify_graph_caller(true);
}

int ftrace_disable_ftrace_graph_caller(void)
{
	return ftrace_modify_graph_caller(false);
}
#endif /* CONFIG_DYNAMIC_FTRACE */
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
