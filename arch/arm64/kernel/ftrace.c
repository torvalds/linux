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
#include <asm/text-patching.h>

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_ARGS
struct fregs_offset {
	const char *name;
	int offset;
};

#define FREGS_OFFSET(n, field)					\
{								\
	.name = n,						\
	.offset = offsetof(struct __arch_ftrace_regs, field),	\
}

static const struct fregs_offset fregs_offsets[] = {
	FREGS_OFFSET("x0", regs[0]),
	FREGS_OFFSET("x1", regs[1]),
	FREGS_OFFSET("x2", regs[2]),
	FREGS_OFFSET("x3", regs[3]),
	FREGS_OFFSET("x4", regs[4]),
	FREGS_OFFSET("x5", regs[5]),
	FREGS_OFFSET("x6", regs[6]),
	FREGS_OFFSET("x7", regs[7]),
	FREGS_OFFSET("x8", regs[8]),

	FREGS_OFFSET("x29", fp),
	FREGS_OFFSET("x30", lr),
	FREGS_OFFSET("lr", lr),

	FREGS_OFFSET("sp", sp),
	FREGS_OFFSET("pc", pc),
};

int ftrace_regs_query_register_offset(const char *name)
{
	for (int i = 0; i < ARRAY_SIZE(fregs_offsets); i++) {
		const struct fregs_offset *roff = &fregs_offsets[i];
		if (!strcmp(roff->name, name))
			return roff->offset;
	}

	return -EINVAL;
}
#endif

unsigned long ftrace_call_adjust(unsigned long addr)
{
	/*
	 * When using mcount, addr is the address of the mcount call
	 * instruction, and no adjustment is necessary.
	 */
	if (!IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_ARGS))
		return addr;

	/*
	 * When using patchable-function-entry without pre-function NOPS, addr
	 * is the address of the first NOP after the function entry point.
	 *
	 * The compiler has either generated:
	 *
	 * addr+00:	func:	NOP		// To be patched to MOV X9, LR
	 * addr+04:		NOP		// To be patched to BL <caller>
	 *
	 * Or:
	 *
	 * addr-04:		BTI	C
	 * addr+00:	func:	NOP		// To be patched to MOV X9, LR
	 * addr+04:		NOP		// To be patched to BL <caller>
	 *
	 * We must adjust addr to the address of the NOP which will be patched
	 * to `BL <caller>`, which is at `addr + 4` bytes in either case.
	 *
	 */
	if (!IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_CALL_OPS))
		return addr + AARCH64_INSN_SIZE;

	/*
	 * When using patchable-function-entry with pre-function NOPs, addr is
	 * the address of the first pre-function NOP.
	 *
	 * Starting from an 8-byte aligned base, the compiler has either
	 * generated:
	 *
	 * addr+00:		NOP		// Literal (first 32 bits)
	 * addr+04:		NOP		// Literal (last 32 bits)
	 * addr+08:	func:	NOP		// To be patched to MOV X9, LR
	 * addr+12:		NOP		// To be patched to BL <caller>
	 *
	 * Or:
	 *
	 * addr+00:		NOP		// Literal (first 32 bits)
	 * addr+04:		NOP		// Literal (last 32 bits)
	 * addr+08:	func:	BTI	C
	 * addr+12:		NOP		// To be patched to MOV X9, LR
	 * addr+16:		NOP		// To be patched to BL <caller>
	 *
	 * We must adjust addr to the address of the NOP which will be patched
	 * to `BL <caller>`, which is at either addr+12 or addr+16 depending on
	 * whether there is a BTI.
	 */

	if (!IS_ALIGNED(addr, sizeof(unsigned long))) {
		WARN_RATELIMIT(1, "Misaligned patch-site %pS\n",
			       (void *)(addr + 8));
		return 0;
	}

	/* Skip the NOPs placed before the function entry point */
	addr += 2 * AARCH64_INSN_SIZE;

	/* Skip any BTI */
	if (IS_ENABLED(CONFIG_ARM64_BTI_KERNEL)) {
		u32 insn = le32_to_cpu(*(__le32 *)addr);

		if (aarch64_insn_is_bti(insn)) {
			addr += AARCH64_INSN_SIZE;
		} else if (insn != aarch64_insn_gen_nop()) {
			WARN_RATELIMIT(1, "unexpected insn in patch-site %pS: 0x%08x\n",
				       (void *)addr, insn);
		}
	}

	/* Skip the first NOP after function entry */
	addr += AARCH64_INSN_SIZE;

	return addr;
}

/* Convert fentry_ip to the symbol address without kallsyms */
unsigned long arch_ftrace_get_symaddr(unsigned long fentry_ip)
{
	u32 insn;

	/*
	 * When using patchable-function-entry without pre-function NOPS, ftrace
	 * entry is the address of the first NOP after the function entry point.
	 *
	 * The compiler has either generated:
	 *
	 * func+00:	func:	NOP		// To be patched to MOV X9, LR
	 * func+04:		NOP		// To be patched to BL <caller>
	 *
	 * Or:
	 *
	 * func-04:		BTI	C
	 * func+00:	func:	NOP		// To be patched to MOV X9, LR
	 * func+04:		NOP		// To be patched to BL <caller>
	 *
	 * The fentry_ip is the address of `BL <caller>` which is at `func + 4`
	 * bytes in either case.
	 */
	if (!IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_CALL_OPS))
		return fentry_ip - AARCH64_INSN_SIZE;

	/*
	 * When using patchable-function-entry with pre-function NOPs, BTI is
	 * a bit different.
	 *
	 * func+00:	func:	NOP		// To be patched to MOV X9, LR
	 * func+04:		NOP		// To be patched to BL <caller>
	 *
	 * Or:
	 *
	 * func+00:	func:	BTI	C
	 * func+04:		NOP		// To be patched to MOV X9, LR
	 * func+08:		NOP		// To be patched to BL <caller>
	 *
	 * The fentry_ip is the address of `BL <caller>` which is at either
	 * `func + 4` or `func + 8` depends on whether there is a BTI.
	 */

	/* If there is no BTI, the func address should be one instruction before. */
	if (!IS_ENABLED(CONFIG_ARM64_BTI_KERNEL))
		return fentry_ip - AARCH64_INSN_SIZE;

	/* We want to be extra safe in case entry ip is on the page edge,
	 * but otherwise we need to avoid get_kernel_nofault()'s overhead.
	 */
	if ((fentry_ip & ~PAGE_MASK) < AARCH64_INSN_SIZE * 2) {
		if (get_kernel_nofault(insn, (u32 *)(fentry_ip - AARCH64_INSN_SIZE * 2)))
			return 0;
	} else {
		insn = *(u32 *)(fentry_ip - AARCH64_INSN_SIZE * 2);
	}

	if (aarch64_insn_is_bti(le32_to_cpu((__le32)insn)))
		return fentry_ip - AARCH64_INSN_SIZE * 2;

	return fentry_ip - AARCH64_INSN_SIZE;
}

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

	/*
	 * When using CALL_OPS, the function to call is associated with the
	 * call site, and we don't have a global function pointer to update.
	 */
	if (IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_CALL_OPS))
		return 0;

	pc = (unsigned long)ftrace_call;
	new = aarch64_insn_gen_branch_imm(pc, (unsigned long)func,
					  AARCH64_INSN_BRANCH_LINK);

	return ftrace_modify_code(pc, 0, new, false);
}

static struct plt_entry *get_ftrace_plt(struct module *mod)
{
#ifdef CONFIG_MODULES
	struct plt_entry *plt = mod->arch.ftrace_trampolines;

	return &plt[FTRACE_PLT_IDX];
#else
	return NULL;
#endif
}

static bool reachable_by_bl(unsigned long addr, unsigned long pc)
{
	long offset = (long)addr - (long)pc;

	return offset >= -SZ_128M && offset < SZ_128M;
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
	struct plt_entry *plt;

	/*
	 * If a custom trampoline is unreachable, rely on the ftrace_caller
	 * trampoline which knows how to indirectly reach that trampoline
	 * through ops->direct_call.
	 */
	if (*addr != FTRACE_ADDR && !reachable_by_bl(*addr, pc))
		*addr = FTRACE_ADDR;

	/*
	 * When the target is within range of the 'BL' instruction, use 'addr'
	 * as-is and branch to that directly.
	 */
	if (reachable_by_bl(*addr, pc))
		return true;

	/*
	 * When the target is outside of the range of a 'BL' instruction, we
	 * must use a PLT to reach it. We can only place PLTs for modules, and
	 * only when module PLT support is built-in.
	 */
	if (!IS_ENABLED(CONFIG_MODULES))
		return false;

	/*
	 * 'mod' is only set at module load time, but if we end up
	 * dealing with an out-of-range condition, we can assume it
	 * is due to a module being loaded far away from the kernel.
	 *
	 * NOTE: __module_text_address() must be called within a RCU read
	 * section, but we can rely on ftrace_lock to ensure that 'mod'
	 * retains its validity throughout the remainder of this code.
	 */
	if (!mod) {
		guard(rcu)();
		mod = __module_text_address(pc);
	}

	if (WARN_ON(!mod))
		return false;

	plt = get_ftrace_plt(mod);
	if (!plt) {
		pr_err("ftrace: no module PLT for %ps\n", (void *)*addr);
		return false;
	}

	*addr = (unsigned long)plt;
	return true;
}

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_CALL_OPS
static const struct ftrace_ops *arm64_rec_get_ops(struct dyn_ftrace *rec)
{
	const struct ftrace_ops *ops = NULL;

	if (rec->flags & FTRACE_FL_CALL_OPS_EN) {
		ops = ftrace_find_unique_ops(rec);
		WARN_ON_ONCE(!ops);
	}

	if (!ops)
		ops = &ftrace_list_ops;

	return ops;
}

static int ftrace_rec_set_ops(const struct dyn_ftrace *rec,
			      const struct ftrace_ops *ops)
{
	unsigned long literal = ALIGN_DOWN(rec->ip - 12, 8);
	return aarch64_insn_write_literal_u64((void *)literal,
					      (unsigned long)ops);
}

static int ftrace_rec_set_nop_ops(struct dyn_ftrace *rec)
{
	return ftrace_rec_set_ops(rec, &ftrace_nop_ops);
}

static int ftrace_rec_update_ops(struct dyn_ftrace *rec)
{
	return ftrace_rec_set_ops(rec, arm64_rec_get_ops(rec));
}
#else
static int ftrace_rec_set_nop_ops(struct dyn_ftrace *rec) { return 0; }
static int ftrace_rec_update_ops(struct dyn_ftrace *rec) { return 0; }
#endif

/*
 * Turn on the call to ftrace_caller() in instrumented function
 */
int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long pc = rec->ip;
	u32 old, new;
	int ret;

	ret = ftrace_rec_update_ops(rec);
	if (ret)
		return ret;

	if (!ftrace_find_callable_addr(rec, NULL, &addr))
		return -EINVAL;

	old = aarch64_insn_gen_nop();
	new = aarch64_insn_gen_branch_imm(pc, addr, AARCH64_INSN_BRANCH_LINK);

	return ftrace_modify_code(pc, old, new, true);
}

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_CALL_OPS
int ftrace_modify_call(struct dyn_ftrace *rec, unsigned long old_addr,
		       unsigned long addr)
{
	unsigned long pc = rec->ip;
	u32 old, new;
	int ret;

	ret = ftrace_rec_set_ops(rec, arm64_rec_get_ops(rec));
	if (ret)
		return ret;

	if (!ftrace_find_callable_addr(rec, NULL, &old_addr))
		return -EINVAL;
	if (!ftrace_find_callable_addr(rec, NULL, &addr))
		return -EINVAL;

	old = aarch64_insn_gen_branch_imm(pc, old_addr,
					  AARCH64_INSN_BRANCH_LINK);
	new = aarch64_insn_gen_branch_imm(pc, addr, AARCH64_INSN_BRANCH_LINK);

	return ftrace_modify_code(pc, old, new, true);
}
#endif

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_ARGS
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
 * The LR value will be recovered by ftrace_caller, and restored into LR
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
	int ret;

	ret = ftrace_rec_set_nop_ops(rec);
	if (ret)
		return ret;

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
	int ret;

	new = aarch64_insn_gen_nop();

	ret = ftrace_rec_set_nop_ops(rec);
	if (ret)
		return ret;

	/*
	 * When using mcount, callsites in modules may have been initalized to
	 * call an arbitrary module PLT (which redirects to the _mcount stub)
	 * rather than the ftrace PLT we'll use at runtime (which redirects to
	 * the ftrace trampoline). We can ignore the old PLT when initializing
	 * the callsite.
	 *
	 * Note: 'mod' is only set at module load time.
	 */
	if (!IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_ARGS) && mod)
		return aarch64_insn_patch_text_nosync((void *)pc, new);

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

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
/*
 * function_graph tracer expects ftrace_return_to_handler() to be called
 * on the way back to parent. For this purpose, this function is called
 * in _mcount() or ftrace_caller() to replace return address (*parent) on
 * the call stack to return_to_handler.
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

	if (!function_graph_enter(old, self_addr, frame_pointer,
	    (void *)frame_pointer)) {
		*parent = return_hooker;
	}
}

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_ARGS
void ftrace_graph_func(unsigned long ip, unsigned long parent_ip,
		       struct ftrace_ops *op, struct ftrace_regs *fregs)
{
	unsigned long return_hooker = (unsigned long)&return_to_handler;
	unsigned long frame_pointer = arch_ftrace_regs(fregs)->fp;
	unsigned long *parent = &arch_ftrace_regs(fregs)->lr;
	unsigned long old;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	old = *parent;

	if (!function_graph_enter_regs(old, ip, frame_pointer,
				       (void *)frame_pointer, fregs)) {
		*parent = return_hooker;
	}
}
#else
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
#endif /* CONFIG_DYNAMIC_FTRACE_WITH_ARGS */
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
