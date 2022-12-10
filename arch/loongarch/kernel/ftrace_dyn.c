// SPDX-License-Identifier: GPL-2.0
/*
 * Based on arch/arm64/kernel/ftrace.c
 *
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */

#include <linux/ftrace.h>
#include <linux/uaccess.h>

#include <asm/inst.h>

static int ftrace_modify_code(unsigned long pc, u32 old, u32 new, bool validate)
{
	u32 replaced;

	if (validate) {
		if (larch_insn_read((void *)pc, &replaced))
			return -EFAULT;

		if (replaced != old)
			return -EINVAL;
	}

	if (larch_insn_patch_text((void *)pc, new))
		return -EPERM;

	return 0;
}

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	u32 new;
	unsigned long pc;

	pc = (unsigned long)&ftrace_call;
	new = larch_insn_gen_bl(pc, (unsigned long)func);

	return ftrace_modify_code(pc, 0, new, false);
}

/*
 * The compiler has inserted 2 NOPs before the regular function prologue.
 * T series registers are available and safe because of LoongArch's psABI.
 *
 * At runtime, we can replace nop with bl to enable ftrace call and replace bl
 * with nop to disable ftrace call. The bl requires us to save the original RA
 * value, so it saves RA at t0 here.
 *
 * Details are:
 *
 * | Compiled   |       Disabled         |        Enabled         |
 * +------------+------------------------+------------------------+
 * | nop        | move     t0, ra        | move     t0, ra        |
 * | nop        | nop                    | bl       ftrace_caller |
 * | func_body  | func_body              | func_body              |
 *
 * The RA value will be recovered by ftrace_regs_entry, and restored into RA
 * before returning to the regular function prologue. When a function is not
 * being traced, the "move t0, ra" is not harmful.
 */

int ftrace_init_nop(struct module *mod, struct dyn_ftrace *rec)
{
	u32 old, new;
	unsigned long pc;

	pc = rec->ip;
	old = larch_insn_gen_nop();
	new = larch_insn_gen_move(LOONGARCH_GPR_T0, LOONGARCH_GPR_RA);

	return ftrace_modify_code(pc, old, new, true);
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	u32 old, new;
	unsigned long pc;

	pc = rec->ip + LOONGARCH_INSN_SIZE;

	old = larch_insn_gen_nop();
	new = larch_insn_gen_bl(pc, addr);

	return ftrace_modify_code(pc, old, new, true);
}

int ftrace_make_nop(struct module *mod, struct dyn_ftrace *rec, unsigned long addr)
{
	u32 old, new;
	unsigned long pc;

	pc = rec->ip + LOONGARCH_INSN_SIZE;

	new = larch_insn_gen_nop();
	old = larch_insn_gen_bl(pc, addr);

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

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
void prepare_ftrace_return(unsigned long self_addr, unsigned long *parent)
{
	unsigned long old;
	unsigned long return_hooker = (unsigned long)&return_to_handler;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	old = *parent;

	if (!function_graph_enter(old, self_addr, 0, NULL))
		*parent = return_hooker;
}

static int ftrace_modify_graph_caller(bool enable)
{
	u32 branch, nop;
	unsigned long pc, func;
	extern void ftrace_graph_call(void);

	pc = (unsigned long)&ftrace_graph_call;
	func = (unsigned long)&ftrace_graph_caller;

	nop = larch_insn_gen_nop();
	branch = larch_insn_gen_b(pc, func);

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
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
