// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm64/kernel/ftrace.c
 *
 * Copyright (C) 2013 Linaro Limited
 * Author: AKASHI Takahiro <takahiro.akashi@linaro.org>
 */

#include <linux/ftrace.h>
#include <linux/module.h>
#include <linux/swab.h>
#include <linux/uaccess.h>

#include <asm/cacheflush.h>
#include <asm/debug-monitors.h>
#include <asm/ftrace.h>
#include <asm/insn.h>

#ifdef CONFIG_DYNAMIC_FTRACE
/*
 * Replace a single instruction, which may be a branch or NOP.
 * If @validate == true, a replaced instruction is checked against 'old'.
 */
static int ftrace_modify_code(unsigned long pc, u32 old, u32 new,
			      bool validate)
{
	u32 replaced;

	/*
	 * Note:
	 * We are paranoid about modifying text, as if a bug were to happen, it
	 * could cause us to read or write to someplace that could cause harm.
	 * Carefully read and modify the code with aarch64_insn_*() which uses
	 * probe_kernel_*(), and make sure what we read is what we expected it
	 * to be before modifying it.
	 */
	if (validate) {
		if (aarch64_insn_read((void *)pc, &replaced))
			return -EFAULT;

		if (replaced != old)
			return -EINVAL;
	}
	if (aarch64_insn_patch_text_nosync((void *)pc, new))
		return -EPERM;

	return 0;
}

/*
 * Replace tracer function in ftrace_caller()
 */
int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned long pc;
	u32 new;

	pc = (unsigned long)&ftrace_call;
	new = aarch64_insn_gen_branch_imm(pc, (unsigned long)func,
					  AARCH64_INSN_BRANCH_LINK);

	return ftrace_modify_code(pc, 0, new, false);
}

static struct plt_entry *get_ftrace_plt(struct module *mod, unsigned long addr)
{
#ifdef CONFIG_ARM64_MODULE_PLTS
	struct plt_entry *plt = mod->arch.ftrace_trampolines;

	if (addr == FTRACE_ADDR)
		return &plt[FTRACE_PLT_IDX];
	if (addr == FTRACE_REGS_ADDR &&
	    IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_REGS))
		return &plt[FTRACE_REGS_PLT_IDX];
#endif
	return NULL;
}

/*
 * Find the address the callsite must branch to in order to reach '*addr'.
 *
 * Due to the limited range of 'BL' instructions, modules may be placed too far
 * away to branch directly and must use a PLT.
 *
 * Returns true when '*addr' contains a reachable target address, or has been
 * modified to contain a PLT address. Returns false otherwise.
 */
static bool ftrace_find_callable_addr(struct dyn_ftrace *rec,
				      struct module *mod,
				      unsigned long *addr)
{
	unsigned long pc = rec->ip;
	long offset = (long)*addr - (long)pc;
	struct plt_entry *plt;

	/*
	 * When the target is within range of the 'BL' instruction, use 'addr'
	 * as-is and branch to that directly.
	 */
	if (offset >= -SZ_128M && offset < SZ_128M)
		return true;

	/*
	 * When the target is outside of the range of a 'BL' instruction, we
	 * must use a PLT to reach it. We can only place PLTs for modules, and
	 * only when module PLT support is built-in.
	 */
	if (!IS_ENABLED(CONFIG_ARM64_MODULE_PLTS))
		return false;

	/*
	 * 'mod' is only set at module load time, but if we end up
	 * dealing with an out-of-range condition, we can assume it
	 * is due to a module being loaded far away from the kernel.
	 *
	 * NOTE: __module_text_address() must be called with preemption
	 * disabled, but we can rely on ftrace_lock to ensure that 'mod'
	 * retains its validity throughout the remainder of this code.
	 */
	if (!mod) {
		preempt_disable();
		mod = __module_text_address(pc);
		preempt_enable();
	}

	if (WARN_ON(!mod))
		return false;

	plt = get_ftrace_plt(mod, *addr);
	if (!plt) {
		pr_err("ftrace: no module PLT for %ps\n", (void *)*addr);
		return false;
	}

	*addr = (unsigned long)plt;
	return true;
}

/*
 * Turn on the call to ftrace_caller() in instrumented function
 */
int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long pc = rec->ip;
	u32 old, new;

	if (!ftrace_find_callable_addr(rec, NULL, &addr))
		return -EINVAL;

	old = aarch64_insn_gen_nop();
	new = aarch64_insn_gen_branch_imm(pc, addr, AARCH64_INSN_BRANCH_LINK);

	return ftrace_modify_code(pc, old, new, true);
}

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
int ftrace_modify_call(struct dyn_ftrace *rec, unsigned long old_addr,
			unsigned long addr)
{
	unsigned long pc = rec->ip;
	u32 old, new;

	if (!ftrace_find_callable_addr(rec, NULL, &old_addr))
		return -EINVAL;
	if (!ftrace_find_callable_addr(rec, NULL, &addr))
		return -EINVAL;

	old = aarch64_insn_gen_branch_imm(pc, old_addr,
					  AARCH64_INSN_BRANCH_LINK);
	new = aarch64_insn_gen_branch_imm(pc, addr, AARCH64_INSN_BRANCH_LINK);

	return ftrace_modify_code(pc, old, new, true);
}

/*
 * The compiler has inserted two NOPs before the regular function prologue.
 * All instrumented functions follow the AAPCS, so x0-x8 and x19-x30 are live,
 * and x9-x18 are free for our use.
 *
 * At runtime we want to be able to swing a single NOP <-> BL to enable or
 * disable the ftrace call. The BL requires us to save the original LR value,
 * so here we insert a <MOV X9, LR> over the first NOP so the instructions
 * before the regular prologue are:
 *
 * | Compiled | Disabled   | Enabled    |
 * +----------+------------+------------+
 * | NOP      | MOV X9, LR | MOV X9, LR |
 * | NOP      | NOP        | BL <entry> |
 *
 * The LR value will be recovered by ftrace_regs_entry, and restored into LR
 * before returning to the regular function prologue. When a function is not
 * being traced, the MOV is not harmful given x9 is not live per the AAPCS.
 *
 * Note: ftrace_process_locs() has pre-adjusted rec->ip to be the address of
 * the BL.
 */
int ftrace_init_nop(struct module *mod, struct dyn_ftrace *rec)
{
	unsigned long pc = rec->ip - AARCH64_INSN_SIZE;
	u32 old, new;

	old = aarch64_insn_gen_nop();
	new = aarch64_insn_gen_move_reg(AARCH64_INSN_REG_9,
					AARCH64_INSN_REG_LR,
					AARCH64_INSN_VARIANT_64BIT);
	return ftrace_modify_code(pc, old, new, true);
}
#endif

/*
 * Turn off the call to ftrace_caller() in instrumented function
 */
int ftrace_make_nop(struct module *mod, struct dyn_ftrace *rec,
		    unsigned long addr)
{
	unsigned long pc = rec->ip;
	u32 old = 0, new;

	new = aarch64_insn_gen_nop();

	/*
	 * When using mcount, callsites in modules may have been initalized to
	 * call an arbitrary module PLT (which redirects to the _mcount stub)
	 * rather than the ftrace PLT we'll use at runtime (which redirects to
	 * the ftrace trampoline). We can ignore the old PLT when initializing
	 * the callsite.
	 *
	 * Note: 'mod' is only set at module load time.
	 */
	if (!IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_REGS) &&
	    IS_ENABLED(CONFIG_ARM64_MODULE_PLTS) && mod) {
		return aarch64_insn_patch_text_nosync((void *)pc, new);
	}

	if (!ftrace_find_callable_addr(rec, mod, &addr))
		return -EINVAL;

	old = aarch64_insn_gen_branch_imm(pc, addr, AARCH64_INSN_BRANCH_LINK);

	return ftrace_modify_code(pc, old, new, true);
}

void arch_ftrace_update_code(int command)
{
	command |= FTRACE_MAY_SLEEP;
	ftrace_modify_all_code(command);
}

int __init ftrace_dyn_arch_init(void)
{
	return 0;
}
#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
/*
 * function_graph tracer expects ftrace_return_to_handler() to be called
 * on the way back to parent. For this purpose, this function is called
 * in _mcount() or ftrace_caller() to replace return address (*parent) on
 * the call stack to return_to_handler.
 *
 * Note that @frame_pointer is used only for sanity check later.
 */
void prepare_ftrace_return(unsigned long self_addr, unsigned long *parent,
			   unsigned long frame_pointer)
{
	unsigned long return_hooker = (unsigned long)&return_to_handler;
	unsigned long old;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	/*
	 * Note:
	 * No protection against faulting at *parent, which may be seen
	 * on other archs. It's unlikely on AArch64.
	 */
	old = *parent;

	if (!function_graph_enter(old, self_addr, frame_pointer, NULL))
		*parent = return_hooker;
}

#ifdef CONFIG_DYNAMIC_FTRACE
/*
 * Turn on/off the call to ftrace_graph_caller() in ftrace_caller()
 * depending on @enable.
 */
static int ftrace_modify_graph_caller(bool enable)
{
	unsigned long pc = (unsigned long)&ftrace_graph_call;
	u32 branch, nop;

	branch = aarch64_insn_gen_branch_imm(pc,
					     (unsigned long)ftrace_graph_caller,
					     AARCH64_INSN_BRANCH_NOLINK);
	nop = aarch64_insn_gen_nop();

	if (enable)
		return ftrace_modify_code(pc, nop, branch, true);
	else
		return ftrace_modify_code(pc, branch, nop, true);
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
