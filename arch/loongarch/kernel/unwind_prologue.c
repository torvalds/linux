// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */
#include <linux/cpumask.h>
#include <linux/ftrace.h>
#include <linux/kallsyms.h>

#include <asm/inst.h>
#include <asm/loongson.h>
#include <asm/ptrace.h>
#include <asm/setup.h>
#include <asm/unwind.h>

extern const int unwind_hint_ade;
extern const int unwind_hint_ale;
extern const int unwind_hint_bp;
extern const int unwind_hint_fpe;
extern const int unwind_hint_fpu;
extern const int unwind_hint_lsx;
extern const int unwind_hint_lasx;
extern const int unwind_hint_lbt;
extern const int unwind_hint_ri;
extern const int unwind_hint_watch;
extern unsigned long eentry;
#ifdef CONFIG_NUMA
extern unsigned long pcpu_handlers[NR_CPUS];
#endif

static inline bool scan_handlers(unsigned long entry_offset)
{
	int idx, offset;

	if (entry_offset >= EXCCODE_INT_START * VECSIZE)
		return false;

	idx = entry_offset / VECSIZE;
	offset = entry_offset % VECSIZE;
	switch (idx) {
	case EXCCODE_ADE:
		return offset == unwind_hint_ade;
	case EXCCODE_ALE:
		return offset == unwind_hint_ale;
	case EXCCODE_BP:
		return offset == unwind_hint_bp;
	case EXCCODE_FPE:
		return offset == unwind_hint_fpe;
	case EXCCODE_FPDIS:
		return offset == unwind_hint_fpu;
	case EXCCODE_LSXDIS:
		return offset == unwind_hint_lsx;
	case EXCCODE_LASXDIS:
		return offset == unwind_hint_lasx;
	case EXCCODE_BTDIS:
		return offset == unwind_hint_lbt;
	case EXCCODE_INE:
		return offset == unwind_hint_ri;
	case EXCCODE_WATCH:
		return offset == unwind_hint_watch;
	default:
		return false;
	}
}

static inline bool fix_exception(unsigned long pc)
{
#ifdef CONFIG_NUMA
	int cpu;

	for_each_possible_cpu(cpu) {
		if (!pcpu_handlers[cpu])
			continue;
		if (scan_handlers(pc - pcpu_handlers[cpu]))
			return true;
	}
#endif
	return scan_handlers(pc - eentry);
}

/*
 * As we meet ftrace_regs_entry, reset first flag like first doing
 * tracing. Prologue analysis will stop soon because PC is at entry.
 */
static inline bool fix_ftrace(unsigned long pc)
{
#ifdef CONFIG_DYNAMIC_FTRACE
	return pc == (unsigned long)ftrace_call + LOONGARCH_INSN_SIZE;
#else
	return false;
#endif
}

static inline bool unwind_state_fixup(struct unwind_state *state)
{
	if (!fix_exception(state->pc) && !fix_ftrace(state->pc))
		return false;

	state->reset = true;
	return true;
}

/*
 * LoongArch function prologue is like follows,
 *     [instructions not use stack var]
 *     addi.d sp, sp, -imm
 *     st.d   xx, sp, offset <- save callee saved regs and
 *     st.d   yy, sp, offset    save ra if function is nest.
 *     [others instructions]
 */
static bool unwind_by_prologue(struct unwind_state *state)
{
	long frame_ra = -1;
	unsigned long frame_size = 0;
	unsigned long size, offset, pc;
	struct pt_regs *regs;
	struct stack_info *info = &state->stack_info;
	union loongarch_instruction *ip, *ip_end;

	if (state->sp >= info->end || state->sp < info->begin)
		return false;

	if (state->reset) {
		regs = (struct pt_regs *)state->sp;
		state->first = true;
		state->reset = false;
		state->pc = regs->csr_era;
		state->ra = regs->regs[1];
		state->sp = regs->regs[3];
		return true;
	}

	/*
	 * When first is not set, the PC is a return address in the previous frame.
	 * We need to adjust its value in case overflow to the next symbol.
	 */
	pc = state->pc - (state->first ? 0 : LOONGARCH_INSN_SIZE);
	if (!kallsyms_lookup_size_offset(pc, &size, &offset))
		return false;

	ip = (union loongarch_instruction *)(pc - offset);
	ip_end = (union loongarch_instruction *)pc;

	while (ip < ip_end) {
		if (is_stack_alloc_ins(ip)) {
			frame_size = (1 << 12) - ip->reg2i12_format.immediate;
			ip++;
			break;
		}
		ip++;
	}

	/*
	 * Can't find stack alloc action, PC may be in a leaf function. Only the
	 * first being true is reasonable, otherwise indicate analysis is broken.
	 */
	if (!frame_size) {
		if (state->first)
			goto first;

		return false;
	}

	while (ip < ip_end) {
		if (is_ra_save_ins(ip)) {
			frame_ra = ip->reg2i12_format.immediate;
			break;
		}
		if (is_branch_ins(ip))
			break;
		ip++;
	}

	/* Can't find save $ra action, PC may be in a leaf function, too. */
	if (frame_ra < 0) {
		if (state->first) {
			state->sp = state->sp + frame_size;
			goto first;
		}
		return false;
	}

	state->pc = *(unsigned long *)(state->sp + frame_ra);
	state->sp = state->sp + frame_size;
	goto out;

first:
	state->pc = state->ra;

out:
	state->first = false;
	return unwind_state_fixup(state) || __kernel_text_address(state->pc);
}

static bool next_frame(struct unwind_state *state)
{
	unsigned long pc;
	struct pt_regs *regs;
	struct stack_info *info = &state->stack_info;

	if (unwind_done(state))
		return false;

	do {
		if (unwind_by_prologue(state)) {
			state->pc = unwind_graph_addr(state, state->pc, state->sp);
			return true;
		}

		if (info->type == STACK_TYPE_IRQ && info->end == state->sp) {
			regs = (struct pt_regs *)info->next_sp;
			pc = regs->csr_era;

			if (user_mode(regs) || !__kernel_text_address(pc))
				return false;

			state->first = true;
			state->pc = pc;
			state->ra = regs->regs[1];
			state->sp = regs->regs[3];
			get_stack_info(state->sp, state->task, info);

			return true;
		}

		state->sp = info->next_sp;

	} while (!get_stack_info(state->sp, state->task, info));

	return false;
}

unsigned long unwind_get_return_address(struct unwind_state *state)
{
	return __unwind_get_return_address(state);
}
EXPORT_SYMBOL_GPL(unwind_get_return_address);

void unwind_start(struct unwind_state *state, struct task_struct *task,
		    struct pt_regs *regs)
{
	__unwind_start(state, task, regs);
	state->type = UNWINDER_PROLOGUE;
	state->first = true;

	/*
	 * The current PC is not kernel text address, we cannot find its
	 * relative symbol. Thus, prologue analysis will be broken. Luckily,
	 * we can use the default_next_frame().
	 */
	if (!__kernel_text_address(state->pc)) {
		state->type = UNWINDER_GUESS;
		if (!unwind_done(state))
			unwind_next_frame(state);
	}
}
EXPORT_SYMBOL_GPL(unwind_start);

bool unwind_next_frame(struct unwind_state *state)
{
	return state->type == UNWINDER_PROLOGUE ?
			next_frame(state) : default_next_frame(state);
}
EXPORT_SYMBOL_GPL(unwind_next_frame);
