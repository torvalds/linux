// SPDX-License-Identifier: GPL-2.0
/*
 * Based on arch/arm64/kernel/ftrace.c
 *
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */

#include <linux/ftrace.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>

#include <asm/inst.h>
#include <asm/module.h>

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

#ifdef CONFIG_MODULES
static bool reachable_by_bl(unsigned long addr, unsigned long pc)
{
	long offset = (long)addr - (long)pc;

	return offset >= -SZ_128M && offset < SZ_128M;
}

static struct plt_entry *get_ftrace_plt(struct module *mod, unsigned long addr)
{
	struct plt_entry *plt = mod->arch.ftrace_trampolines;

	if (addr == FTRACE_ADDR)
		return &plt[FTRACE_PLT_IDX];
	if (addr == FTRACE_REGS_ADDR &&
			IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_REGS))
		return &plt[FTRACE_REGS_PLT_IDX];

	return NULL;
}

/*
 * Find the address the callsite must branch to in order to reach '*addr'.
 *
 * Due to the limited range of 'bl' instruction, modules may be placed too far
 * away to branch directly and we must use a PLT.
 *
 * Returns true when '*addr' contains a reachable target address, or has been
 * modified to contain a PLT address. Returns false otherwise.
 */
static bool ftrace_find_callable_addr(struct dyn_ftrace *rec, struct module *mod, unsigned long *addr)
{
	unsigned long pc = rec->ip + LOONGARCH_INSN_SIZE;
	struct plt_entry *plt;

	/*
	 * If a custom trampoline is unreachable, rely on the ftrace_regs_caller
	 * trampoline which knows how to indirectly reach that trampoline through
	 * ops->direct_call.
	 */
	if (*addr != FTRACE_ADDR && *addr != FTRACE_REGS_ADDR && !reachable_by_bl(*addr, pc))
		*addr = FTRACE_REGS_ADDR;

	/*
	 * When the target is within range of the 'bl' instruction, use 'addr'
	 * as-is and branch to that directly.
	 */
	if (reachable_by_bl(*addr, pc))
		return true;

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
#else /* !CONFIG_MODULES */
static bool ftrace_find_callable_addr(struct dyn_ftrace *rec, struct module *mod, unsigned long *addr)
{
	return true;
}
#endif

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
int ftrace_modify_call(struct dyn_ftrace *rec, unsigned long old_addr, unsigned long addr)
{
	u32 old, new;
	unsigned long pc;

	pc = rec->ip + LOONGARCH_INSN_SIZE;

	if (!ftrace_find_callable_addr(rec, NULL, &addr))
		return -EINVAL;

	if (!ftrace_find_callable_addr(rec, NULL, &old_addr))
		return -EINVAL;

	new = larch_insn_gen_bl(pc, addr);
	old = larch_insn_gen_bl(pc, old_addr);

	return ftrace_modify_code(pc, old, new, true);
}
#endif /* CONFIG_DYNAMIC_FTRACE_WITH_REGS */

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

	if (!ftrace_find_callable_addr(rec, NULL, &addr))
		return -EINVAL;

	old = larch_insn_gen_nop();
	new = larch_insn_gen_bl(pc, addr);

	return ftrace_modify_code(pc, old, new, true);
}

int ftrace_make_nop(struct module *mod, struct dyn_ftrace *rec, unsigned long addr)
{
	u32 old, new;
	unsigned long pc;

	pc = rec->ip + LOONGARCH_INSN_SIZE;

	if (!ftrace_find_callable_addr(rec, NULL, &addr))
		return -EINVAL;

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

	if (!function_graph_enter(old, self_addr, 0, parent))
		*parent = return_hooker;
}

#ifdef CONFIG_HAVE_DYNAMIC_FTRACE_WITH_ARGS
void ftrace_graph_func(unsigned long ip, unsigned long parent_ip,
		       struct ftrace_ops *op, struct ftrace_regs *fregs)
{
	struct pt_regs *regs = &fregs->regs;
	unsigned long *parent = (unsigned long *)&regs->regs[1];

	prepare_ftrace_return(ip, (unsigned long *)parent);
}
#else
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
#endif /* CONFIG_HAVE_DYNAMIC_FTRACE_WITH_ARGS */
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

#ifdef CONFIG_KPROBES_ON_FTRACE
/* Ftrace callback handler for kprobes -- called under preepmt disabled */
void kprobe_ftrace_handler(unsigned long ip, unsigned long parent_ip,
			   struct ftrace_ops *ops, struct ftrace_regs *fregs)
{
	int bit;
	struct pt_regs *regs;
	struct kprobe *p;
	struct kprobe_ctlblk *kcb;

	if (unlikely(kprobe_ftrace_disabled))
		return;

	bit = ftrace_test_recursion_trylock(ip, parent_ip);
	if (bit < 0)
		return;

	p = get_kprobe((kprobe_opcode_t *)ip);
	if (unlikely(!p) || kprobe_disabled(p))
		goto out;

	regs = ftrace_get_regs(fregs);
	if (!regs)
		goto out;

	kcb = get_kprobe_ctlblk();
	if (kprobe_running()) {
		kprobes_inc_nmissed_count(p);
	} else {
		unsigned long orig_ip = instruction_pointer(regs);

		instruction_pointer_set(regs, ip);

		__this_cpu_write(current_kprobe, p);
		kcb->kprobe_status = KPROBE_HIT_ACTIVE;
		if (!p->pre_handler || !p->pre_handler(p, regs)) {
			/*
			 * Emulate singlestep (and also recover regs->csr_era)
			 * as if there is a nop
			 */
			instruction_pointer_set(regs, (unsigned long)p->addr + MCOUNT_INSN_SIZE);
			if (unlikely(p->post_handler)) {
				kcb->kprobe_status = KPROBE_HIT_SSDONE;
				p->post_handler(p, regs, 0);
			}
			instruction_pointer_set(regs, orig_ip);
		}

		/*
		 * If pre_handler returns !0, it changes regs->csr_era. We have to
		 * skip emulating post_handler.
		 */
		__this_cpu_write(current_kprobe, NULL);
	}
out:
	ftrace_test_recursion_unlock(bit);
}
NOKPROBE_SYMBOL(kprobe_ftrace_handler);

int arch_prepare_kprobe_ftrace(struct kprobe *p)
{
	p->ainsn.insn = NULL;
	return 0;
}
#endif /* CONFIG_KPROBES_ON_FTRACE */
