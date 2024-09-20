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
#include <asm/stacktrace.h>
#include <asm/patch.h>

/*
 * The compiler emitted profiling hook consists of
 *
 *   PUSH    {LR}
 *   BL	     __gnu_mcount_nc
 *
 * To turn this combined sequence into a NOP, we need to restore the value of
 * SP before the PUSH. Let's use an ADD rather than a POP into LR, as LR is not
 * modified anyway, and reloading LR from memory is highly likely to be less
 * efficient.
 */
#ifdef CONFIG_THUMB2_KERNEL
#define	NOP		0xf10d0d04	/* add.w sp, sp, #4 */
#else
#define	NOP		0xe28dd004	/* add   sp, sp, #4 */
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

void ftrace_caller_from_init(void);
void ftrace_regs_caller_from_init(void);

static unsigned long __ref adjust_address(struct dyn_ftrace *rec,
					  unsigned long addr)
{
	if (!IS_ENABLED(CONFIG_DYNAMIC_FTRACE) ||
	    system_state >= SYSTEM_FREEING_INITMEM ||
	    likely(!is_kernel_inittext(rec->ip)))
		return addr;
	if (!IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_REGS) ||
	    addr == (unsigned long)&ftrace_caller)
		return (unsigned long)&ftrace_caller_from_init;
	return (unsigned long)&ftrace_regs_caller_from_init;
}

void ftrace_arch_code_modify_prepare(void)
{
}

void ftrace_arch_code_modify_post_process(void)
{
	/* Make sure any TLB misses during machine stop are cleared. */
	flush_tlb_all();
}

static unsigned long ftrace_call_replace(unsigned long pc, unsigned long addr,
					 bool warn)
{
	return arm_gen_branch_link(pc, addr, warn);
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
		if (copy_from_kernel_nofault(&replaced, (void *)pc,
				MCOUNT_INSN_SIZE))
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
	new = ftrace_call_replace(pc, (unsigned long)func, true);

	ret = ftrace_modify_code(pc, 0, new, false);

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
	if (!ret) {
		pc = (unsigned long)&ftrace_regs_call;
		new = ftrace_call_replace(pc, (unsigned long)func, true);

		ret = ftrace_modify_code(pc, 0, new, false);
	}
#endif

	return ret;
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long new, old;
	unsigned long ip = rec->ip;
	unsigned long aaddr = adjust_address(rec, addr);
	struct module *mod = NULL;

#ifdef CONFIG_ARM_MODULE_PLTS
	mod = rec->arch.mod;
#endif

	old = ftrace_nop_replace(rec);

	new = ftrace_call_replace(ip, aaddr, !mod);
#ifdef CONFIG_ARM_MODULE_PLTS
	if (!new && mod) {
		aaddr = get_module_plt(mod, ip, aaddr);
		new = ftrace_call_replace(ip, aaddr, true);
	}
#endif

	return ftrace_modify_code(rec->ip, old, new, true);
}

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS

int ftrace_modify_call(struct dyn_ftrace *rec, unsigned long old_addr,
				unsigned long addr)
{
	unsigned long new, old;
	unsigned long ip = rec->ip;

	old = ftrace_call_replace(ip, adjust_address(rec, old_addr), true);

	new = ftrace_call_replace(ip, adjust_address(rec, addr), true);

	return ftrace_modify_code(rec->ip, old, new, true);
}

#endif

int ftrace_make_nop(struct module *mod,
		    struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long aaddr = adjust_address(rec, addr);
	unsigned long ip = rec->ip;
	unsigned long old;
	unsigned long new;
	int ret;

#ifdef CONFIG_ARM_MODULE_PLTS
	/* mod is only supplied during module loading */
	if (!mod)
		mod = rec->arch.mod;
	else
		rec->arch.mod = mod;
#endif

	old = ftrace_call_replace(ip, aaddr,
				  !IS_ENABLED(CONFIG_ARM_MODULE_PLTS) || !mod);
#ifdef CONFIG_ARM_MODULE_PLTS
	if (!old && mod) {
		aaddr = get_module_plt(mod, ip, aaddr);
		old = ftrace_call_replace(ip, aaddr, true);
	}
#endif

	new = ftrace_nop_replace(rec);
	/*
	 * Locations in .init.text may call __gnu_mcount_mc via a linker
	 * emitted veneer if they are too far away from its implementation, and
	 * so validation may fail spuriously in such cases. Let's work around
	 * this by omitting those from validation.
	 */
	ret = ftrace_modify_code(ip, old, new, !is_kernel_inittext(ip));

	return ret;
}
#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
asmlinkage
void prepare_ftrace_return(unsigned long *parent, unsigned long self_addr,
			   unsigned long frame_pointer,
			   unsigned long stack_pointer)
{
	unsigned long return_hooker = (unsigned long) &return_to_handler;
	unsigned long old;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
err_out:
		return;

	if (IS_ENABLED(CONFIG_UNWINDER_FRAME_POINTER)) {
		/*
		 * Usually, the stack frames are contiguous in memory but cases
		 * have been observed where the next stack frame does not live
		 * at 'frame_pointer + 4' as this code used to assume.
		 *
		 * Instead, dereference the field in the stack frame that
		 * stores the SP of the calling frame: to avoid unbounded
		 * recursion, this cannot involve any ftrace instrumented
		 * functions, so use the __get_kernel_nofault() primitive
		 * directly.
		 */
		__get_kernel_nofault(&frame_pointer,
				     (unsigned long *)(frame_pointer - 8),
				     unsigned long, err_out);
	} else {
		struct stackframe frame = {
			.fp = frame_pointer,
			.sp = stack_pointer,
			.lr = self_addr,
			.pc = self_addr,
		};
		if (unwind_frame(&frame) < 0)
			return;
		if (frame.lr != self_addr)
			parent = frame.lr_addr;
		frame_pointer = frame.sp;
	}

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
	unsigned long nop = arm_gen_nop();
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
