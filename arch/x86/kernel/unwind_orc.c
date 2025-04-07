// SPDX-License-Identifier: GPL-2.0-only
#include <linux/objtool.h>
#include <linux/module.h>
#include <linux/sort.h>
#include <asm/ptrace.h>
#include <asm/stacktrace.h>
#include <asm/unwind.h>
#include <asm/orc_types.h>
#include <asm/orc_lookup.h>
#include <asm/orc_header.h>

ORC_HEADER;

#define orc_warn(fmt, ...) \
	printk_deferred_once(KERN_WARNING "WARNING: " fmt, ##__VA_ARGS__)

#define orc_warn_current(args...)					\
({									\
	static bool dumped_before;					\
	if (state->task == current && !state->error) {			\
		orc_warn(args);						\
		if (unwind_debug && !dumped_before) {			\
			dumped_before = true;				\
			unwind_dump(state);				\
		}							\
	}								\
})

extern int __start_orc_unwind_ip[];
extern int __stop_orc_unwind_ip[];
extern struct orc_entry __start_orc_unwind[];
extern struct orc_entry __stop_orc_unwind[];

static bool orc_init __ro_after_init;
static bool unwind_debug __ro_after_init;
static unsigned int lookup_num_blocks __ro_after_init;

static int __init unwind_debug_cmdline(char *str)
{
	unwind_debug = true;

	return 0;
}
early_param("unwind_debug", unwind_debug_cmdline);

static void unwind_dump(struct unwind_state *state)
{
	static bool dumped_before;
	unsigned long word, *sp;
	struct stack_info stack_info = {0};
	unsigned long visit_mask = 0;

	if (dumped_before)
		return;

	dumped_before = true;

	printk_deferred("unwind stack type:%d next_sp:%p mask:0x%lx graph_idx:%d\n",
			state->stack_info.type, state->stack_info.next_sp,
			state->stack_mask, state->graph_idx);

	for (sp = __builtin_frame_address(0); sp;
	     sp = PTR_ALIGN(stack_info.next_sp, sizeof(long))) {
		if (get_stack_info(sp, state->task, &stack_info, &visit_mask))
			break;

		for (; sp < stack_info.end; sp++) {

			word = READ_ONCE_NOCHECK(*sp);

			printk_deferred("%0*lx: %0*lx (%pB)\n", BITS_PER_LONG/4,
					(unsigned long)sp, BITS_PER_LONG/4,
					word, (void *)word);
		}
	}
}

static inline unsigned long orc_ip(const int *ip)
{
	return (unsigned long)ip + *ip;
}

static struct orc_entry *__orc_find(int *ip_table, struct orc_entry *u_table,
				    unsigned int num_entries, unsigned long ip)
{
	int *first = ip_table;
	int *last = ip_table + num_entries - 1;
	int *mid, *found = first;

	if (!num_entries)
		return NULL;

	/*
	 * Do a binary range search to find the rightmost duplicate of a given
	 * starting address.  Some entries are section terminators which are
	 * "weak" entries for ensuring there are no gaps.  They should be
	 * ignored when they conflict with a real entry.
	 */
	while (first <= last) {
		mid = first + ((last - first) / 2);

		if (orc_ip(mid) <= ip) {
			found = mid;
			first = mid + 1;
		} else
			last = mid - 1;
	}

	return u_table + (found - ip_table);
}

#ifdef CONFIG_MODULES
static struct orc_entry *orc_module_find(unsigned long ip)
{
	struct module *mod;

	mod = __module_address(ip);
	if (!mod || !mod->arch.orc_unwind || !mod->arch.orc_unwind_ip)
		return NULL;
	return __orc_find(mod->arch.orc_unwind_ip, mod->arch.orc_unwind,
			  mod->arch.num_orcs, ip);
}
#else
static struct orc_entry *orc_module_find(unsigned long ip)
{
	return NULL;
}
#endif

#ifdef CONFIG_DYNAMIC_FTRACE
static struct orc_entry *orc_find(unsigned long ip);

/*
 * Ftrace dynamic trampolines do not have orc entries of their own.
 * But they are copies of the ftrace entries that are static and
 * defined in ftrace_*.S, which do have orc entries.
 *
 * If the unwinder comes across a ftrace trampoline, then find the
 * ftrace function that was used to create it, and use that ftrace
 * function's orc entry, as the placement of the return code in
 * the stack will be identical.
 */
static struct orc_entry *orc_ftrace_find(unsigned long ip)
{
	struct ftrace_ops *ops;
	unsigned long tramp_addr, offset;

	ops = ftrace_ops_trampoline(ip);
	if (!ops)
		return NULL;

	/* Set tramp_addr to the start of the code copied by the trampoline */
	if (ops->flags & FTRACE_OPS_FL_SAVE_REGS)
		tramp_addr = (unsigned long)ftrace_regs_caller;
	else
		tramp_addr = (unsigned long)ftrace_caller;

	/* Now place tramp_addr to the location within the trampoline ip is at */
	offset = ip - ops->trampoline;
	tramp_addr += offset;

	/* Prevent unlikely recursion */
	if (ip == tramp_addr)
		return NULL;

	return orc_find(tramp_addr);
}
#else
static struct orc_entry *orc_ftrace_find(unsigned long ip)
{
	return NULL;
}
#endif

/*
 * If we crash with IP==0, the last successfully executed instruction
 * was probably an indirect function call with a NULL function pointer,
 * and we don't have unwind information for NULL.
 * This hardcoded ORC entry for IP==0 allows us to unwind from a NULL function
 * pointer into its parent and then continue normally from there.
 */
static struct orc_entry null_orc_entry = {
	.sp_offset = sizeof(long),
	.sp_reg = ORC_REG_SP,
	.bp_reg = ORC_REG_UNDEFINED,
	.type = ORC_TYPE_CALL
};

/* Fake frame pointer entry -- used as a fallback for generated code */
static struct orc_entry orc_fp_entry = {
	.type		= ORC_TYPE_CALL,
	.sp_reg		= ORC_REG_BP,
	.sp_offset	= 16,
	.bp_reg		= ORC_REG_PREV_SP,
	.bp_offset	= -16,
};

static struct orc_entry *orc_find(unsigned long ip)
{
	static struct orc_entry *orc;

	if (ip == 0)
		return &null_orc_entry;

	/* For non-init vmlinux addresses, use the fast lookup table: */
	if (ip >= LOOKUP_START_IP && ip < LOOKUP_STOP_IP) {
		unsigned int idx, start, stop;

		idx = (ip - LOOKUP_START_IP) / LOOKUP_BLOCK_SIZE;

		if (unlikely((idx >= lookup_num_blocks-1))) {
			orc_warn("WARNING: bad lookup idx: idx=%u num=%u ip=%pB\n",
				 idx, lookup_num_blocks, (void *)ip);
			return NULL;
		}

		start = orc_lookup[idx];
		stop = orc_lookup[idx + 1] + 1;

		if (unlikely((__start_orc_unwind + start >= __stop_orc_unwind) ||
			     (__start_orc_unwind + stop > __stop_orc_unwind))) {
			orc_warn("WARNING: bad lookup value: idx=%u num=%u start=%u stop=%u ip=%pB\n",
				 idx, lookup_num_blocks, start, stop, (void *)ip);
			return NULL;
		}

		return __orc_find(__start_orc_unwind_ip + start,
				  __start_orc_unwind + start, stop - start, ip);
	}

	/* vmlinux .init slow lookup: */
	if (is_kernel_inittext(ip))
		return __orc_find(__start_orc_unwind_ip, __start_orc_unwind,
				  __stop_orc_unwind_ip - __start_orc_unwind_ip, ip);

	/* Module lookup: */
	orc = orc_module_find(ip);
	if (orc)
		return orc;

	return orc_ftrace_find(ip);
}

#ifdef CONFIG_MODULES

static DEFINE_MUTEX(sort_mutex);
static int *cur_orc_ip_table = __start_orc_unwind_ip;
static struct orc_entry *cur_orc_table = __start_orc_unwind;

static void orc_sort_swap(void *_a, void *_b, int size)
{
	struct orc_entry *orc_a, *orc_b;
	int *a = _a, *b = _b, tmp;
	int delta = _b - _a;

	/* Swap the .orc_unwind_ip entries: */
	tmp = *a;
	*a = *b + delta;
	*b = tmp - delta;

	/* Swap the corresponding .orc_unwind entries: */
	orc_a = cur_orc_table + (a - cur_orc_ip_table);
	orc_b = cur_orc_table + (b - cur_orc_ip_table);
	swap(*orc_a, *orc_b);
}

static int orc_sort_cmp(const void *_a, const void *_b)
{
	struct orc_entry *orc_a;
	const int *a = _a, *b = _b;
	unsigned long a_val = orc_ip(a);
	unsigned long b_val = orc_ip(b);

	if (a_val > b_val)
		return 1;
	if (a_val < b_val)
		return -1;

	/*
	 * The "weak" section terminator entries need to always be first
	 * to ensure the lookup code skips them in favor of real entries.
	 * These terminator entries exist to handle any gaps created by
	 * whitelisted .o files which didn't get objtool generation.
	 */
	orc_a = cur_orc_table + (a - cur_orc_ip_table);
	return orc_a->type == ORC_TYPE_UNDEFINED ? -1 : 1;
}

void unwind_module_init(struct module *mod, void *_orc_ip, size_t orc_ip_size,
			void *_orc, size_t orc_size)
{
	int *orc_ip = _orc_ip;
	struct orc_entry *orc = _orc;
	unsigned int num_entries = orc_ip_size / sizeof(int);

	WARN_ON_ONCE(orc_ip_size % sizeof(int) != 0 ||
		     orc_size % sizeof(*orc) != 0 ||
		     num_entries != orc_size / sizeof(*orc));

	/*
	 * The 'cur_orc_*' globals allow the orc_sort_swap() callback to
	 * associate an .orc_unwind_ip table entry with its corresponding
	 * .orc_unwind entry so they can both be swapped.
	 */
	mutex_lock(&sort_mutex);
	cur_orc_ip_table = orc_ip;
	cur_orc_table = orc;
	sort(orc_ip, num_entries, sizeof(int), orc_sort_cmp, orc_sort_swap);
	mutex_unlock(&sort_mutex);

	mod->arch.orc_unwind_ip = orc_ip;
	mod->arch.orc_unwind = orc;
	mod->arch.num_orcs = num_entries;
}
#endif

void __init unwind_init(void)
{
	size_t orc_ip_size = (void *)__stop_orc_unwind_ip - (void *)__start_orc_unwind_ip;
	size_t orc_size = (void *)__stop_orc_unwind - (void *)__start_orc_unwind;
	size_t num_entries = orc_ip_size / sizeof(int);
	struct orc_entry *orc;
	int i;

	if (!num_entries || orc_ip_size % sizeof(int) != 0 ||
	    orc_size % sizeof(struct orc_entry) != 0 ||
	    num_entries != orc_size / sizeof(struct orc_entry)) {
		orc_warn("WARNING: Bad or missing .orc_unwind table.  Disabling unwinder.\n");
		return;
	}

	/*
	 * Note, the orc_unwind and orc_unwind_ip tables were already
	 * sorted at build time via the 'sorttable' tool.
	 * It's ready for binary search straight away, no need to sort it.
	 */

	/* Initialize the fast lookup table: */
	lookup_num_blocks = orc_lookup_end - orc_lookup;
	for (i = 0; i < lookup_num_blocks-1; i++) {
		orc = __orc_find(__start_orc_unwind_ip, __start_orc_unwind,
				 num_entries,
				 LOOKUP_START_IP + (LOOKUP_BLOCK_SIZE * i));
		if (!orc) {
			orc_warn("WARNING: Corrupt .orc_unwind table.  Disabling unwinder.\n");
			return;
		}

		orc_lookup[i] = orc - __start_orc_unwind;
	}

	/* Initialize the ending block: */
	orc = __orc_find(__start_orc_unwind_ip, __start_orc_unwind, num_entries,
			 LOOKUP_STOP_IP);
	if (!orc) {
		orc_warn("WARNING: Corrupt .orc_unwind table.  Disabling unwinder.\n");
		return;
	}
	orc_lookup[lookup_num_blocks-1] = orc - __start_orc_unwind;

	orc_init = true;
}

unsigned long unwind_get_return_address(struct unwind_state *state)
{
	if (unwind_done(state))
		return 0;

	return __kernel_text_address(state->ip) ? state->ip : 0;
}
EXPORT_SYMBOL_GPL(unwind_get_return_address);

unsigned long *unwind_get_return_address_ptr(struct unwind_state *state)
{
	if (unwind_done(state))
		return NULL;

	if (state->regs)
		return &state->regs->ip;

	if (state->sp)
		return (unsigned long *)state->sp - 1;

	return NULL;
}

static bool stack_access_ok(struct unwind_state *state, unsigned long _addr,
			    size_t len)
{
	struct stack_info *info = &state->stack_info;
	void *addr = (void *)_addr;

	if (on_stack(info, addr, len))
		return true;

	return !get_stack_info(addr, state->task, info, &state->stack_mask) &&
		on_stack(info, addr, len);
}

static bool deref_stack_reg(struct unwind_state *state, unsigned long addr,
			    unsigned long *val)
{
	if (!stack_access_ok(state, addr, sizeof(long)))
		return false;

	*val = READ_ONCE_NOCHECK(*(unsigned long *)addr);
	return true;
}

static bool deref_stack_regs(struct unwind_state *state, unsigned long addr,
			     unsigned long *ip, unsigned long *sp)
{
	struct pt_regs *regs = (struct pt_regs *)addr;

	/* x86-32 support will be more complicated due to the &regs->sp hack */
	BUILD_BUG_ON(IS_ENABLED(CONFIG_X86_32));

	if (!stack_access_ok(state, addr, sizeof(struct pt_regs)))
		return false;

	*ip = READ_ONCE_NOCHECK(regs->ip);
	*sp = READ_ONCE_NOCHECK(regs->sp);
	return true;
}

static bool deref_stack_iret_regs(struct unwind_state *state, unsigned long addr,
				  unsigned long *ip, unsigned long *sp)
{
	struct pt_regs *regs = (void *)addr - IRET_FRAME_OFFSET;

	if (!stack_access_ok(state, addr, IRET_FRAME_SIZE))
		return false;

	*ip = READ_ONCE_NOCHECK(regs->ip);
	*sp = READ_ONCE_NOCHECK(regs->sp);
	return true;
}

/*
 * If state->regs is non-NULL, and points to a full pt_regs, just get the reg
 * value from state->regs.
 *
 * Otherwise, if state->regs just points to IRET regs, and the previous frame
 * had full regs, it's safe to get the value from the previous regs.  This can
 * happen when early/late IRQ entry code gets interrupted by an NMI.
 */
static bool get_reg(struct unwind_state *state, unsigned int reg_off,
		    unsigned long *val)
{
	unsigned int reg = reg_off/8;

	if (!state->regs)
		return false;

	if (state->full_regs) {
		*val = READ_ONCE_NOCHECK(((unsigned long *)state->regs)[reg]);
		return true;
	}

	if (state->prev_regs) {
		*val = READ_ONCE_NOCHECK(((unsigned long *)state->prev_regs)[reg]);
		return true;
	}

	return false;
}

bool unwind_next_frame(struct unwind_state *state)
{
	unsigned long ip_p, sp, tmp, orig_ip = state->ip, prev_sp = state->sp;
	enum stack_type prev_type = state->stack_info.type;
	struct orc_entry *orc;
	bool indirect = false;

	if (unwind_done(state))
		return false;

	/* Don't let modules unload while we're reading their ORC data. */
	guard(rcu)();

	/* End-of-stack check for user tasks: */
	if (state->regs && user_mode(state->regs))
		goto the_end;

	/*
	 * Find the orc_entry associated with the text address.
	 *
	 * For a call frame (as opposed to a signal frame), state->ip points to
	 * the instruction after the call.  That instruction's stack layout
	 * could be different from the call instruction's layout, for example
	 * if the call was to a noreturn function.  So get the ORC data for the
	 * call instruction itself.
	 */
	orc = orc_find(state->signal ? state->ip : state->ip - 1);
	if (!orc) {
		/*
		 * As a fallback, try to assume this code uses a frame pointer.
		 * This is useful for generated code, like BPF, which ORC
		 * doesn't know about.  This is just a guess, so the rest of
		 * the unwind is no longer considered reliable.
		 */
		orc = &orc_fp_entry;
		state->error = true;
	} else {
		if (orc->type == ORC_TYPE_UNDEFINED)
			goto err;

		if (orc->type == ORC_TYPE_END_OF_STACK)
			goto the_end;
	}

	state->signal = orc->signal;

	/* Find the previous frame's stack: */
	switch (orc->sp_reg) {
	case ORC_REG_SP:
		sp = state->sp + orc->sp_offset;
		break;

	case ORC_REG_BP:
		sp = state->bp + orc->sp_offset;
		break;

	case ORC_REG_SP_INDIRECT:
		sp = state->sp;
		indirect = true;
		break;

	case ORC_REG_BP_INDIRECT:
		sp = state->bp + orc->sp_offset;
		indirect = true;
		break;

	case ORC_REG_R10:
		if (!get_reg(state, offsetof(struct pt_regs, r10), &sp)) {
			orc_warn_current("missing R10 value at %pB\n",
					 (void *)state->ip);
			goto err;
		}
		break;

	case ORC_REG_R13:
		if (!get_reg(state, offsetof(struct pt_regs, r13), &sp)) {
			orc_warn_current("missing R13 value at %pB\n",
					 (void *)state->ip);
			goto err;
		}
		break;

	case ORC_REG_DI:
		if (!get_reg(state, offsetof(struct pt_regs, di), &sp)) {
			orc_warn_current("missing RDI value at %pB\n",
					 (void *)state->ip);
			goto err;
		}
		break;

	case ORC_REG_DX:
		if (!get_reg(state, offsetof(struct pt_regs, dx), &sp)) {
			orc_warn_current("missing DX value at %pB\n",
					 (void *)state->ip);
			goto err;
		}
		break;

	default:
		orc_warn("unknown SP base reg %d at %pB\n",
			 orc->sp_reg, (void *)state->ip);
		goto err;
	}

	if (indirect) {
		if (!deref_stack_reg(state, sp, &sp))
			goto err;

		if (orc->sp_reg == ORC_REG_SP_INDIRECT)
			sp += orc->sp_offset;
	}

	/* Find IP, SP and possibly regs: */
	switch (orc->type) {
	case ORC_TYPE_CALL:
		ip_p = sp - sizeof(long);

		if (!deref_stack_reg(state, ip_p, &state->ip))
			goto err;

		state->ip = unwind_recover_ret_addr(state, state->ip,
						    (unsigned long *)ip_p);
		state->sp = sp;
		state->regs = NULL;
		state->prev_regs = NULL;
		break;

	case ORC_TYPE_REGS:
		if (!deref_stack_regs(state, sp, &state->ip, &state->sp)) {
			orc_warn_current("can't access registers at %pB\n",
					 (void *)orig_ip);
			goto err;
		}
		/*
		 * There is a small chance to interrupt at the entry of
		 * arch_rethook_trampoline() where the ORC info doesn't exist.
		 * That point is right after the RET to arch_rethook_trampoline()
		 * which was modified return address.
		 * At that point, the @addr_p of the unwind_recover_rethook()
		 * (this has to point the address of the stack entry storing
		 * the modified return address) must be "SP - (a stack entry)"
		 * because SP is incremented by the RET.
		 */
		state->ip = unwind_recover_rethook(state, state->ip,
				(unsigned long *)(state->sp - sizeof(long)));
		state->regs = (struct pt_regs *)sp;
		state->prev_regs = NULL;
		state->full_regs = true;
		break;

	case ORC_TYPE_REGS_PARTIAL:
		if (!deref_stack_iret_regs(state, sp, &state->ip, &state->sp)) {
			orc_warn_current("can't access iret registers at %pB\n",
					 (void *)orig_ip);
			goto err;
		}
		/* See ORC_TYPE_REGS case comment. */
		state->ip = unwind_recover_rethook(state, state->ip,
				(unsigned long *)(state->sp - sizeof(long)));

		if (state->full_regs)
			state->prev_regs = state->regs;
		state->regs = (void *)sp - IRET_FRAME_OFFSET;
		state->full_regs = false;
		break;

	default:
		orc_warn("unknown .orc_unwind entry type %d at %pB\n",
			 orc->type, (void *)orig_ip);
		goto err;
	}

	/* Find BP: */
	switch (orc->bp_reg) {
	case ORC_REG_UNDEFINED:
		if (get_reg(state, offsetof(struct pt_regs, bp), &tmp))
			state->bp = tmp;
		break;

	case ORC_REG_PREV_SP:
		if (!deref_stack_reg(state, sp + orc->bp_offset, &state->bp))
			goto err;
		break;

	case ORC_REG_BP:
		if (!deref_stack_reg(state, state->bp + orc->bp_offset, &state->bp))
			goto err;
		break;

	default:
		orc_warn("unknown BP base reg %d for ip %pB\n",
			 orc->bp_reg, (void *)orig_ip);
		goto err;
	}

	/* Prevent a recursive loop due to bad ORC data: */
	if (state->stack_info.type == prev_type &&
	    on_stack(&state->stack_info, (void *)state->sp, sizeof(long)) &&
	    state->sp <= prev_sp) {
		orc_warn_current("stack going in the wrong direction? at %pB\n",
				 (void *)orig_ip);
		goto err;
	}

	return true;

err:
	state->error = true;

the_end:
	state->stack_info.type = STACK_TYPE_UNKNOWN;
	return false;
}
EXPORT_SYMBOL_GPL(unwind_next_frame);

void __unwind_start(struct unwind_state *state, struct task_struct *task,
		    struct pt_regs *regs, unsigned long *first_frame)
{
	memset(state, 0, sizeof(*state));
	state->task = task;

	if (!orc_init)
		goto err;

	/*
	 * Refuse to unwind the stack of a task while it's executing on another
	 * CPU.  This check is racy, but that's ok: the unwinder has other
	 * checks to prevent it from going off the rails.
	 */
	if (task_on_another_cpu(task))
		goto err;

	if (regs) {
		if (user_mode(regs))
			goto the_end;

		state->ip = regs->ip;
		state->sp = regs->sp;
		state->bp = regs->bp;
		state->regs = regs;
		state->full_regs = true;
		state->signal = true;

	} else if (task == current) {
		asm volatile("lea (%%rip), %0\n\t"
			     "mov %%rsp, %1\n\t"
			     "mov %%rbp, %2\n\t"
			     : "=r" (state->ip), "=r" (state->sp),
			       "=r" (state->bp));

	} else {
		struct inactive_task_frame *frame = (void *)task->thread.sp;

		state->sp = task->thread.sp + sizeof(*frame);
		state->bp = READ_ONCE_NOCHECK(frame->bp);
		state->ip = READ_ONCE_NOCHECK(frame->ret_addr);
		state->signal = (void *)state->ip == ret_from_fork_asm;
	}

	if (get_stack_info((unsigned long *)state->sp, state->task,
			   &state->stack_info, &state->stack_mask)) {
		/*
		 * We weren't on a valid stack.  It's possible that
		 * we overflowed a valid stack into a guard page.
		 * See if the next page up is valid so that we can
		 * generate some kind of backtrace if this happens.
		 */
		void *next_page = (void *)PAGE_ALIGN((unsigned long)state->sp);
		state->error = true;
		if (get_stack_info(next_page, state->task, &state->stack_info,
				   &state->stack_mask))
			return;
	}

	/*
	 * The caller can provide the address of the first frame directly
	 * (first_frame) or indirectly (regs->sp) to indicate which stack frame
	 * to start unwinding at.  Skip ahead until we reach it.
	 */

	/* When starting from regs, skip the regs frame: */
	if (regs) {
		unwind_next_frame(state);
		return;
	}

	/* Otherwise, skip ahead to the user-specified starting frame: */
	while (!unwind_done(state) &&
	       (!on_stack(&state->stack_info, first_frame, sizeof(long)) ||
			state->sp <= (unsigned long)first_frame))
		unwind_next_frame(state);

	return;

err:
	state->error = true;
the_end:
	state->stack_info.type = STACK_TYPE_UNKNOWN;
}
EXPORT_SYMBOL_GPL(__unwind_start);
