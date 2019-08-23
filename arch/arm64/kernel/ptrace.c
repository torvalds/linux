// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on arch/arm/kernel/ptrace.c
 *
 * By Ross Biro 1/23/92
 * edited by Linus Torvalds
 * ARM modifications Copyright (C) 2000 Russell King
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/audit.h>
#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/nospec.h>
#include <linux/smp.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/seccomp.h>
#include <linux/security.h>
#include <linux/init.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <linux/regset.h>
#include <linux/tracehook.h>
#include <linux/elf.h>

#include <asm/compat.h>
#include <asm/cpufeature.h>
#include <asm/debug-monitors.h>
#include <asm/fpsimd.h>
#include <asm/pgtable.h>
#include <asm/pointer_auth.h>
#include <asm/stacktrace.h>
#include <asm/syscall.h>
#include <asm/traps.h>
#include <asm/system_misc.h>

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

struct pt_regs_offset {
	const char *name;
	int offset;
};

#define REG_OFFSET_NAME(r) {.name = #r, .offset = offsetof(struct pt_regs, r)}
#define REG_OFFSET_END {.name = NULL, .offset = 0}
#define GPR_OFFSET_NAME(r) \
	{.name = "x" #r, .offset = offsetof(struct pt_regs, regs[r])}

static const struct pt_regs_offset regoffset_table[] = {
	GPR_OFFSET_NAME(0),
	GPR_OFFSET_NAME(1),
	GPR_OFFSET_NAME(2),
	GPR_OFFSET_NAME(3),
	GPR_OFFSET_NAME(4),
	GPR_OFFSET_NAME(5),
	GPR_OFFSET_NAME(6),
	GPR_OFFSET_NAME(7),
	GPR_OFFSET_NAME(8),
	GPR_OFFSET_NAME(9),
	GPR_OFFSET_NAME(10),
	GPR_OFFSET_NAME(11),
	GPR_OFFSET_NAME(12),
	GPR_OFFSET_NAME(13),
	GPR_OFFSET_NAME(14),
	GPR_OFFSET_NAME(15),
	GPR_OFFSET_NAME(16),
	GPR_OFFSET_NAME(17),
	GPR_OFFSET_NAME(18),
	GPR_OFFSET_NAME(19),
	GPR_OFFSET_NAME(20),
	GPR_OFFSET_NAME(21),
	GPR_OFFSET_NAME(22),
	GPR_OFFSET_NAME(23),
	GPR_OFFSET_NAME(24),
	GPR_OFFSET_NAME(25),
	GPR_OFFSET_NAME(26),
	GPR_OFFSET_NAME(27),
	GPR_OFFSET_NAME(28),
	GPR_OFFSET_NAME(29),
	GPR_OFFSET_NAME(30),
	{.name = "lr", .offset = offsetof(struct pt_regs, regs[30])},
	REG_OFFSET_NAME(sp),
	REG_OFFSET_NAME(pc),
	REG_OFFSET_NAME(pstate),
	REG_OFFSET_END,
};

/**
 * regs_query_register_offset() - query register offset from its name
 * @name:	the name of a register
 *
 * regs_query_register_offset() returns the offset of a register in struct
 * pt_regs from its name. If the name is invalid, this returns -EINVAL;
 */
int regs_query_register_offset(const char *name)
{
	const struct pt_regs_offset *roff;

	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (!strcmp(roff->name, name))
			return roff->offset;
	return -EINVAL;
}

/**
 * regs_within_kernel_stack() - check the address in the stack
 * @regs:      pt_regs which contains kernel stack pointer.
 * @addr:      address which is checked.
 *
 * regs_within_kernel_stack() checks @addr is within the kernel stack page(s).
 * If @addr is within the kernel stack, it returns true. If not, returns false.
 */
static bool regs_within_kernel_stack(struct pt_regs *regs, unsigned long addr)
{
	return ((addr & ~(THREAD_SIZE - 1))  ==
		(kernel_stack_pointer(regs) & ~(THREAD_SIZE - 1))) ||
		on_irq_stack(addr, NULL);
}

/**
 * regs_get_kernel_stack_nth() - get Nth entry of the stack
 * @regs:	pt_regs which contains kernel stack pointer.
 * @n:		stack entry number.
 *
 * regs_get_kernel_stack_nth() returns @n th entry of the kernel stack which
 * is specified by @regs. If the @n th entry is NOT in the kernel stack,
 * this returns 0.
 */
unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs, unsigned int n)
{
	unsigned long *addr = (unsigned long *)kernel_stack_pointer(regs);

	addr += n;
	if (regs_within_kernel_stack(regs, (unsigned long)addr))
		return *addr;
	else
		return 0;
}

/*
 * TODO: does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * Called by kernel/ptrace.c when detaching..
 */
void ptrace_disable(struct task_struct *child)
{
	/*
	 * This would be better off in core code, but PTRACE_DETACH has
	 * grown its fair share of arch-specific worts and changing it
	 * is likely to cause regressions on obscure architectures.
	 */
	user_disable_single_step(child);
}

#ifdef CONFIG_HAVE_HW_BREAKPOINT
/*
 * Handle hitting a HW-breakpoint.
 */
static void ptrace_hbptriggered(struct perf_event *bp,
				struct perf_sample_data *data,
				struct pt_regs *regs)
{
	struct arch_hw_breakpoint *bkpt = counter_arch_bp(bp);
	const char *desc = "Hardware breakpoint trap (ptrace)";

#ifdef CONFIG_COMPAT
	if (is_compat_task()) {
		int si_errno = 0;
		int i;

		for (i = 0; i < ARM_MAX_BRP; ++i) {
			if (current->thread.debug.hbp_break[i] == bp) {
				si_errno = (i << 1) + 1;
				break;
			}
		}

		for (i = 0; i < ARM_MAX_WRP; ++i) {
			if (current->thread.debug.hbp_watch[i] == bp) {
				si_errno = -((i << 1) + 1);
				break;
			}
		}
		arm64_force_sig_ptrace_errno_trap(si_errno,
						  (void __user *)bkpt->trigger,
						  desc);
	}
#endif
	arm64_force_sig_fault(SIGTRAP, TRAP_HWBKPT,
			      (void __user *)(bkpt->trigger),
			      desc);
}

/*
 * Unregister breakpoints from this task and reset the pointers in
 * the thread_struct.
 */
void flush_ptrace_hw_breakpoint(struct task_struct *tsk)
{
	int i;
	struct thread_struct *t = &tsk->thread;

	for (i = 0; i < ARM_MAX_BRP; i++) {
		if (t->debug.hbp_break[i]) {
			unregister_hw_breakpoint(t->debug.hbp_break[i]);
			t->debug.hbp_break[i] = NULL;
		}
	}

	for (i = 0; i < ARM_MAX_WRP; i++) {
		if (t->debug.hbp_watch[i]) {
			unregister_hw_breakpoint(t->debug.hbp_watch[i]);
			t->debug.hbp_watch[i] = NULL;
		}
	}
}

void ptrace_hw_copy_thread(struct task_struct *tsk)
{
	memset(&tsk->thread.debug, 0, sizeof(struct debug_info));
}

static struct perf_event *ptrace_hbp_get_event(unsigned int note_type,
					       struct task_struct *tsk,
					       unsigned long idx)
{
	struct perf_event *bp = ERR_PTR(-EINVAL);

	switch (note_type) {
	case NT_ARM_HW_BREAK:
		if (idx >= ARM_MAX_BRP)
			goto out;
		idx = array_index_nospec(idx, ARM_MAX_BRP);
		bp = tsk->thread.debug.hbp_break[idx];
		break;
	case NT_ARM_HW_WATCH:
		if (idx >= ARM_MAX_WRP)
			goto out;
		idx = array_index_nospec(idx, ARM_MAX_WRP);
		bp = tsk->thread.debug.hbp_watch[idx];
		break;
	}

out:
	return bp;
}

static int ptrace_hbp_set_event(unsigned int note_type,
				struct task_struct *tsk,
				unsigned long idx,
				struct perf_event *bp)
{
	int err = -EINVAL;

	switch (note_type) {
	case NT_ARM_HW_BREAK:
		if (idx >= ARM_MAX_BRP)
			goto out;
		idx = array_index_nospec(idx, ARM_MAX_BRP);
		tsk->thread.debug.hbp_break[idx] = bp;
		err = 0;
		break;
	case NT_ARM_HW_WATCH:
		if (idx >= ARM_MAX_WRP)
			goto out;
		idx = array_index_nospec(idx, ARM_MAX_WRP);
		tsk->thread.debug.hbp_watch[idx] = bp;
		err = 0;
		break;
	}

out:
	return err;
}

static struct perf_event *ptrace_hbp_create(unsigned int note_type,
					    struct task_struct *tsk,
					    unsigned long idx)
{
	struct perf_event *bp;
	struct perf_event_attr attr;
	int err, type;

	switch (note_type) {
	case NT_ARM_HW_BREAK:
		type = HW_BREAKPOINT_X;
		break;
	case NT_ARM_HW_WATCH:
		type = HW_BREAKPOINT_RW;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	ptrace_breakpoint_init(&attr);

	/*
	 * Initialise fields to sane defaults
	 * (i.e. values that will pass validation).
	 */
	attr.bp_addr	= 0;
	attr.bp_len	= HW_BREAKPOINT_LEN_4;
	attr.bp_type	= type;
	attr.disabled	= 1;

	bp = register_user_hw_breakpoint(&attr, ptrace_hbptriggered, NULL, tsk);
	if (IS_ERR(bp))
		return bp;

	err = ptrace_hbp_set_event(note_type, tsk, idx, bp);
	if (err)
		return ERR_PTR(err);

	return bp;
}

static int ptrace_hbp_fill_attr_ctrl(unsigned int note_type,
				     struct arch_hw_breakpoint_ctrl ctrl,
				     struct perf_event_attr *attr)
{
	int err, len, type, offset, disabled = !ctrl.enabled;

	attr->disabled = disabled;
	if (disabled)
		return 0;

	err = arch_bp_generic_fields(ctrl, &len, &type, &offset);
	if (err)
		return err;

	switch (note_type) {
	case NT_ARM_HW_BREAK:
		if ((type & HW_BREAKPOINT_X) != type)
			return -EINVAL;
		break;
	case NT_ARM_HW_WATCH:
		if ((type & HW_BREAKPOINT_RW) != type)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	attr->bp_len	= len;
	attr->bp_type	= type;
	attr->bp_addr	+= offset;

	return 0;
}

static int ptrace_hbp_get_resource_info(unsigned int note_type, u32 *info)
{
	u8 num;
	u32 reg = 0;

	switch (note_type) {
	case NT_ARM_HW_BREAK:
		num = hw_breakpoint_slots(TYPE_INST);
		break;
	case NT_ARM_HW_WATCH:
		num = hw_breakpoint_slots(TYPE_DATA);
		break;
	default:
		return -EINVAL;
	}

	reg |= debug_monitors_arch();
	reg <<= 8;
	reg |= num;

	*info = reg;
	return 0;
}

static int ptrace_hbp_get_ctrl(unsigned int note_type,
			       struct task_struct *tsk,
			       unsigned long idx,
			       u32 *ctrl)
{
	struct perf_event *bp = ptrace_hbp_get_event(note_type, tsk, idx);

	if (IS_ERR(bp))
		return PTR_ERR(bp);

	*ctrl = bp ? encode_ctrl_reg(counter_arch_bp(bp)->ctrl) : 0;
	return 0;
}

static int ptrace_hbp_get_addr(unsigned int note_type,
			       struct task_struct *tsk,
			       unsigned long idx,
			       u64 *addr)
{
	struct perf_event *bp = ptrace_hbp_get_event(note_type, tsk, idx);

	if (IS_ERR(bp))
		return PTR_ERR(bp);

	*addr = bp ? counter_arch_bp(bp)->address : 0;
	return 0;
}

static struct perf_event *ptrace_hbp_get_initialised_bp(unsigned int note_type,
							struct task_struct *tsk,
							unsigned long idx)
{
	struct perf_event *bp = ptrace_hbp_get_event(note_type, tsk, idx);

	if (!bp)
		bp = ptrace_hbp_create(note_type, tsk, idx);

	return bp;
}

static int ptrace_hbp_set_ctrl(unsigned int note_type,
			       struct task_struct *tsk,
			       unsigned long idx,
			       u32 uctrl)
{
	int err;
	struct perf_event *bp;
	struct perf_event_attr attr;
	struct arch_hw_breakpoint_ctrl ctrl;

	bp = ptrace_hbp_get_initialised_bp(note_type, tsk, idx);
	if (IS_ERR(bp)) {
		err = PTR_ERR(bp);
		return err;
	}

	attr = bp->attr;
	decode_ctrl_reg(uctrl, &ctrl);
	err = ptrace_hbp_fill_attr_ctrl(note_type, ctrl, &attr);
	if (err)
		return err;

	return modify_user_hw_breakpoint(bp, &attr);
}

static int ptrace_hbp_set_addr(unsigned int note_type,
			       struct task_struct *tsk,
			       unsigned long idx,
			       u64 addr)
{
	int err;
	struct perf_event *bp;
	struct perf_event_attr attr;

	bp = ptrace_hbp_get_initialised_bp(note_type, tsk, idx);
	if (IS_ERR(bp)) {
		err = PTR_ERR(bp);
		return err;
	}

	attr = bp->attr;
	attr.bp_addr = addr;
	err = modify_user_hw_breakpoint(bp, &attr);
	return err;
}

#define PTRACE_HBP_ADDR_SZ	sizeof(u64)
#define PTRACE_HBP_CTRL_SZ	sizeof(u32)
#define PTRACE_HBP_PAD_SZ	sizeof(u32)

static int hw_break_get(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			void *kbuf, void __user *ubuf)
{
	unsigned int note_type = regset->core_note_type;
	int ret, idx = 0, offset, limit;
	u32 info, ctrl;
	u64 addr;

	/* Resource info */
	ret = ptrace_hbp_get_resource_info(note_type, &info);
	if (ret)
		return ret;

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, &info, 0,
				  sizeof(info));
	if (ret)
		return ret;

	/* Pad */
	offset = offsetof(struct user_hwdebug_state, pad);
	ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf, offset,
				       offset + PTRACE_HBP_PAD_SZ);
	if (ret)
		return ret;

	/* (address, ctrl) registers */
	offset = offsetof(struct user_hwdebug_state, dbg_regs);
	limit = regset->n * regset->size;
	while (count && offset < limit) {
		ret = ptrace_hbp_get_addr(note_type, target, idx, &addr);
		if (ret)
			return ret;
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, &addr,
					  offset, offset + PTRACE_HBP_ADDR_SZ);
		if (ret)
			return ret;
		offset += PTRACE_HBP_ADDR_SZ;

		ret = ptrace_hbp_get_ctrl(note_type, target, idx, &ctrl);
		if (ret)
			return ret;
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, &ctrl,
					  offset, offset + PTRACE_HBP_CTRL_SZ);
		if (ret)
			return ret;
		offset += PTRACE_HBP_CTRL_SZ;

		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					       offset,
					       offset + PTRACE_HBP_PAD_SZ);
		if (ret)
			return ret;
		offset += PTRACE_HBP_PAD_SZ;
		idx++;
	}

	return 0;
}

static int hw_break_set(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			const void *kbuf, const void __user *ubuf)
{
	unsigned int note_type = regset->core_note_type;
	int ret, idx = 0, offset, limit;
	u32 ctrl;
	u64 addr;

	/* Resource info and pad */
	offset = offsetof(struct user_hwdebug_state, dbg_regs);
	ret = user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf, 0, offset);
	if (ret)
		return ret;

	/* (address, ctrl) registers */
	limit = regset->n * regset->size;
	while (count && offset < limit) {
		if (count < PTRACE_HBP_ADDR_SZ)
			return -EINVAL;
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &addr,
					 offset, offset + PTRACE_HBP_ADDR_SZ);
		if (ret)
			return ret;
		ret = ptrace_hbp_set_addr(note_type, target, idx, addr);
		if (ret)
			return ret;
		offset += PTRACE_HBP_ADDR_SZ;

		if (!count)
			break;
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &ctrl,
					 offset, offset + PTRACE_HBP_CTRL_SZ);
		if (ret)
			return ret;
		ret = ptrace_hbp_set_ctrl(note_type, target, idx, ctrl);
		if (ret)
			return ret;
		offset += PTRACE_HBP_CTRL_SZ;

		ret = user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
						offset,
						offset + PTRACE_HBP_PAD_SZ);
		if (ret)
			return ret;
		offset += PTRACE_HBP_PAD_SZ;
		idx++;
	}

	return 0;
}
#endif	/* CONFIG_HAVE_HW_BREAKPOINT */

static int gpr_get(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   void *kbuf, void __user *ubuf)
{
	struct user_pt_regs *uregs = &task_pt_regs(target)->user_regs;
	return user_regset_copyout(&pos, &count, &kbuf, &ubuf, uregs, 0, -1);
}

static int gpr_set(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	int ret;
	struct user_pt_regs newregs = task_pt_regs(target)->user_regs;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &newregs, 0, -1);
	if (ret)
		return ret;

	if (!valid_user_regs(&newregs, target))
		return -EINVAL;

	task_pt_regs(target)->user_regs = newregs;
	return 0;
}

/*
 * TODO: update fp accessors for lazy context switching (sync/flush hwstate)
 */
static int __fpr_get(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     void *kbuf, void __user *ubuf, unsigned int start_pos)
{
	struct user_fpsimd_state *uregs;

	sve_sync_to_fpsimd(target);

	uregs = &target->thread.uw.fpsimd_state;

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf, uregs,
				   start_pos, start_pos + sizeof(*uregs));
}

static int fpr_get(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   void *kbuf, void __user *ubuf)
{
	if (target == current)
		fpsimd_preserve_current_state();

	return __fpr_get(target, regset, pos, count, kbuf, ubuf, 0);
}

static int __fpr_set(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     const void *kbuf, const void __user *ubuf,
		     unsigned int start_pos)
{
	int ret;
	struct user_fpsimd_state newstate;

	/*
	 * Ensure target->thread.uw.fpsimd_state is up to date, so that a
	 * short copyin can't resurrect stale data.
	 */
	sve_sync_to_fpsimd(target);

	newstate = target->thread.uw.fpsimd_state;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &newstate,
				 start_pos, start_pos + sizeof(newstate));
	if (ret)
		return ret;

	target->thread.uw.fpsimd_state = newstate;

	return ret;
}

static int fpr_set(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	int ret;

	ret = __fpr_set(target, regset, pos, count, kbuf, ubuf, 0);
	if (ret)
		return ret;

	sve_sync_from_fpsimd_zeropad(target);
	fpsimd_flush_task_state(target);

	return ret;
}

static int tls_get(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   void *kbuf, void __user *ubuf)
{
	unsigned long *tls = &target->thread.uw.tp_value;

	if (target == current)
		tls_preserve_current_state();

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf, tls, 0, -1);
}

static int tls_set(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	int ret;
	unsigned long tls = target->thread.uw.tp_value;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &tls, 0, -1);
	if (ret)
		return ret;

	target->thread.uw.tp_value = tls;
	return ret;
}

static int system_call_get(struct task_struct *target,
			   const struct user_regset *regset,
			   unsigned int pos, unsigned int count,
			   void *kbuf, void __user *ubuf)
{
	int syscallno = task_pt_regs(target)->syscallno;

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   &syscallno, 0, -1);
}

static int system_call_set(struct task_struct *target,
			   const struct user_regset *regset,
			   unsigned int pos, unsigned int count,
			   const void *kbuf, const void __user *ubuf)
{
	int syscallno = task_pt_regs(target)->syscallno;
	int ret;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &syscallno, 0, -1);
	if (ret)
		return ret;

	task_pt_regs(target)->syscallno = syscallno;
	return ret;
}

#ifdef CONFIG_ARM64_SVE

static void sve_init_header_from_task(struct user_sve_header *header,
				      struct task_struct *target)
{
	unsigned int vq;

	memset(header, 0, sizeof(*header));

	header->flags = test_tsk_thread_flag(target, TIF_SVE) ?
		SVE_PT_REGS_SVE : SVE_PT_REGS_FPSIMD;
	if (test_tsk_thread_flag(target, TIF_SVE_VL_INHERIT))
		header->flags |= SVE_PT_VL_INHERIT;

	header->vl = target->thread.sve_vl;
	vq = sve_vq_from_vl(header->vl);

	header->max_vl = sve_max_vl;
	header->size = SVE_PT_SIZE(vq, header->flags);
	header->max_size = SVE_PT_SIZE(sve_vq_from_vl(header->max_vl),
				      SVE_PT_REGS_SVE);
}

static unsigned int sve_size_from_header(struct user_sve_header const *header)
{
	return ALIGN(header->size, SVE_VQ_BYTES);
}

static unsigned int sve_get_size(struct task_struct *target,
				 const struct user_regset *regset)
{
	struct user_sve_header header;

	if (!system_supports_sve())
		return 0;

	sve_init_header_from_task(&header, target);
	return sve_size_from_header(&header);
}

static int sve_get(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   void *kbuf, void __user *ubuf)
{
	int ret;
	struct user_sve_header header;
	unsigned int vq;
	unsigned long start, end;

	if (!system_supports_sve())
		return -EINVAL;

	/* Header */
	sve_init_header_from_task(&header, target);
	vq = sve_vq_from_vl(header.vl);

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, &header,
				  0, sizeof(header));
	if (ret)
		return ret;

	if (target == current)
		fpsimd_preserve_current_state();

	/* Registers: FPSIMD-only case */

	BUILD_BUG_ON(SVE_PT_FPSIMD_OFFSET != sizeof(header));
	if ((header.flags & SVE_PT_REGS_MASK) == SVE_PT_REGS_FPSIMD)
		return __fpr_get(target, regset, pos, count, kbuf, ubuf,
				 SVE_PT_FPSIMD_OFFSET);

	/* Otherwise: full SVE case */

	BUILD_BUG_ON(SVE_PT_SVE_OFFSET != sizeof(header));
	start = SVE_PT_SVE_OFFSET;
	end = SVE_PT_SVE_FFR_OFFSET(vq) + SVE_PT_SVE_FFR_SIZE(vq);
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  target->thread.sve_state,
				  start, end);
	if (ret)
		return ret;

	start = end;
	end = SVE_PT_SVE_FPSR_OFFSET(vq);
	ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
				       start, end);
	if (ret)
		return ret;

	/*
	 * Copy fpsr, and fpcr which must follow contiguously in
	 * struct fpsimd_state:
	 */
	start = end;
	end = SVE_PT_SVE_FPCR_OFFSET(vq) + SVE_PT_SVE_FPCR_SIZE;
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  &target->thread.uw.fpsimd_state.fpsr,
				  start, end);
	if (ret)
		return ret;

	start = end;
	end = sve_size_from_header(&header);
	return user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					start, end);
}

static int sve_set(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	int ret;
	struct user_sve_header header;
	unsigned int vq;
	unsigned long start, end;

	if (!system_supports_sve())
		return -EINVAL;

	/* Header */
	if (count < sizeof(header))
		return -EINVAL;
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &header,
				 0, sizeof(header));
	if (ret)
		goto out;

	/*
	 * Apart from PT_SVE_REGS_MASK, all PT_SVE_* flags are consumed by
	 * sve_set_vector_length(), which will also validate them for us:
	 */
	ret = sve_set_vector_length(target, header.vl,
		((unsigned long)header.flags & ~SVE_PT_REGS_MASK) << 16);
	if (ret)
		goto out;

	/* Actual VL set may be less than the user asked for: */
	vq = sve_vq_from_vl(target->thread.sve_vl);

	/* Registers: FPSIMD-only case */

	BUILD_BUG_ON(SVE_PT_FPSIMD_OFFSET != sizeof(header));
	if ((header.flags & SVE_PT_REGS_MASK) == SVE_PT_REGS_FPSIMD) {
		ret = __fpr_set(target, regset, pos, count, kbuf, ubuf,
				SVE_PT_FPSIMD_OFFSET);
		clear_tsk_thread_flag(target, TIF_SVE);
		goto out;
	}

	/* Otherwise: full SVE case */

	/*
	 * If setting a different VL from the requested VL and there is
	 * register data, the data layout will be wrong: don't even
	 * try to set the registers in this case.
	 */
	if (count && vq != sve_vq_from_vl(header.vl)) {
		ret = -EIO;
		goto out;
	}

	sve_alloc(target);

	/*
	 * Ensure target->thread.sve_state is up to date with target's
	 * FPSIMD regs, so that a short copyin leaves trailing registers
	 * unmodified.
	 */
	fpsimd_sync_to_sve(target);
	set_tsk_thread_flag(target, TIF_SVE);

	BUILD_BUG_ON(SVE_PT_SVE_OFFSET != sizeof(header));
	start = SVE_PT_SVE_OFFSET;
	end = SVE_PT_SVE_FFR_OFFSET(vq) + SVE_PT_SVE_FFR_SIZE(vq);
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 target->thread.sve_state,
				 start, end);
	if (ret)
		goto out;

	start = end;
	end = SVE_PT_SVE_FPSR_OFFSET(vq);
	ret = user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					start, end);
	if (ret)
		goto out;

	/*
	 * Copy fpsr, and fpcr which must follow contiguously in
	 * struct fpsimd_state:
	 */
	start = end;
	end = SVE_PT_SVE_FPCR_OFFSET(vq) + SVE_PT_SVE_FPCR_SIZE;
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &target->thread.uw.fpsimd_state.fpsr,
				 start, end);

out:
	fpsimd_flush_task_state(target);
	return ret;
}

#endif /* CONFIG_ARM64_SVE */

#ifdef CONFIG_ARM64_PTR_AUTH
static int pac_mask_get(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			void *kbuf, void __user *ubuf)
{
	/*
	 * The PAC bits can differ across data and instruction pointers
	 * depending on TCR_EL1.TBID*, which we may make use of in future, so
	 * we expose separate masks.
	 */
	unsigned long mask = ptrauth_user_pac_mask();
	struct user_pac_mask uregs = {
		.data_mask = mask,
		.insn_mask = mask,
	};

	if (!system_supports_address_auth())
		return -EINVAL;

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf, &uregs, 0, -1);
}

#ifdef CONFIG_CHECKPOINT_RESTORE
static __uint128_t pac_key_to_user(const struct ptrauth_key *key)
{
	return (__uint128_t)key->hi << 64 | key->lo;
}

static struct ptrauth_key pac_key_from_user(__uint128_t ukey)
{
	struct ptrauth_key key = {
		.lo = (unsigned long)ukey,
		.hi = (unsigned long)(ukey >> 64),
	};

	return key;
}

static void pac_address_keys_to_user(struct user_pac_address_keys *ukeys,
				     const struct ptrauth_keys *keys)
{
	ukeys->apiakey = pac_key_to_user(&keys->apia);
	ukeys->apibkey = pac_key_to_user(&keys->apib);
	ukeys->apdakey = pac_key_to_user(&keys->apda);
	ukeys->apdbkey = pac_key_to_user(&keys->apdb);
}

static void pac_address_keys_from_user(struct ptrauth_keys *keys,
				       const struct user_pac_address_keys *ukeys)
{
	keys->apia = pac_key_from_user(ukeys->apiakey);
	keys->apib = pac_key_from_user(ukeys->apibkey);
	keys->apda = pac_key_from_user(ukeys->apdakey);
	keys->apdb = pac_key_from_user(ukeys->apdbkey);
}

static int pac_address_keys_get(struct task_struct *target,
				const struct user_regset *regset,
				unsigned int pos, unsigned int count,
				void *kbuf, void __user *ubuf)
{
	struct ptrauth_keys *keys = &target->thread.keys_user;
	struct user_pac_address_keys user_keys;

	if (!system_supports_address_auth())
		return -EINVAL;

	pac_address_keys_to_user(&user_keys, keys);

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   &user_keys, 0, -1);
}

static int pac_address_keys_set(struct task_struct *target,
				const struct user_regset *regset,
				unsigned int pos, unsigned int count,
				const void *kbuf, const void __user *ubuf)
{
	struct ptrauth_keys *keys = &target->thread.keys_user;
	struct user_pac_address_keys user_keys;
	int ret;

	if (!system_supports_address_auth())
		return -EINVAL;

	pac_address_keys_to_user(&user_keys, keys);
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &user_keys, 0, -1);
	if (ret)
		return ret;
	pac_address_keys_from_user(keys, &user_keys);

	return 0;
}

static void pac_generic_keys_to_user(struct user_pac_generic_keys *ukeys,
				     const struct ptrauth_keys *keys)
{
	ukeys->apgakey = pac_key_to_user(&keys->apga);
}

static void pac_generic_keys_from_user(struct ptrauth_keys *keys,
				       const struct user_pac_generic_keys *ukeys)
{
	keys->apga = pac_key_from_user(ukeys->apgakey);
}

static int pac_generic_keys_get(struct task_struct *target,
				const struct user_regset *regset,
				unsigned int pos, unsigned int count,
				void *kbuf, void __user *ubuf)
{
	struct ptrauth_keys *keys = &target->thread.keys_user;
	struct user_pac_generic_keys user_keys;

	if (!system_supports_generic_auth())
		return -EINVAL;

	pac_generic_keys_to_user(&user_keys, keys);

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   &user_keys, 0, -1);
}

static int pac_generic_keys_set(struct task_struct *target,
				const struct user_regset *regset,
				unsigned int pos, unsigned int count,
				const void *kbuf, const void __user *ubuf)
{
	struct ptrauth_keys *keys = &target->thread.keys_user;
	struct user_pac_generic_keys user_keys;
	int ret;

	if (!system_supports_generic_auth())
		return -EINVAL;

	pac_generic_keys_to_user(&user_keys, keys);
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &user_keys, 0, -1);
	if (ret)
		return ret;
	pac_generic_keys_from_user(keys, &user_keys);

	return 0;
}
#endif /* CONFIG_CHECKPOINT_RESTORE */
#endif /* CONFIG_ARM64_PTR_AUTH */

enum aarch64_regset {
	REGSET_GPR,
	REGSET_FPR,
	REGSET_TLS,
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	REGSET_HW_BREAK,
	REGSET_HW_WATCH,
#endif
	REGSET_SYSTEM_CALL,
#ifdef CONFIG_ARM64_SVE
	REGSET_SVE,
#endif
#ifdef CONFIG_ARM64_PTR_AUTH
	REGSET_PAC_MASK,
#ifdef CONFIG_CHECKPOINT_RESTORE
	REGSET_PACA_KEYS,
	REGSET_PACG_KEYS,
#endif
#endif
};

static const struct user_regset aarch64_regsets[] = {
	[REGSET_GPR] = {
		.core_note_type = NT_PRSTATUS,
		.n = sizeof(struct user_pt_regs) / sizeof(u64),
		.size = sizeof(u64),
		.align = sizeof(u64),
		.get = gpr_get,
		.set = gpr_set
	},
	[REGSET_FPR] = {
		.core_note_type = NT_PRFPREG,
		.n = sizeof(struct user_fpsimd_state) / sizeof(u32),
		/*
		 * We pretend we have 32-bit registers because the fpsr and
		 * fpcr are 32-bits wide.
		 */
		.size = sizeof(u32),
		.align = sizeof(u32),
		.get = fpr_get,
		.set = fpr_set
	},
	[REGSET_TLS] = {
		.core_note_type = NT_ARM_TLS,
		.n = 1,
		.size = sizeof(void *),
		.align = sizeof(void *),
		.get = tls_get,
		.set = tls_set,
	},
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	[REGSET_HW_BREAK] = {
		.core_note_type = NT_ARM_HW_BREAK,
		.n = sizeof(struct user_hwdebug_state) / sizeof(u32),
		.size = sizeof(u32),
		.align = sizeof(u32),
		.get = hw_break_get,
		.set = hw_break_set,
	},
	[REGSET_HW_WATCH] = {
		.core_note_type = NT_ARM_HW_WATCH,
		.n = sizeof(struct user_hwdebug_state) / sizeof(u32),
		.size = sizeof(u32),
		.align = sizeof(u32),
		.get = hw_break_get,
		.set = hw_break_set,
	},
#endif
	[REGSET_SYSTEM_CALL] = {
		.core_note_type = NT_ARM_SYSTEM_CALL,
		.n = 1,
		.size = sizeof(int),
		.align = sizeof(int),
		.get = system_call_get,
		.set = system_call_set,
	},
#ifdef CONFIG_ARM64_SVE
	[REGSET_SVE] = { /* Scalable Vector Extension */
		.core_note_type = NT_ARM_SVE,
		.n = DIV_ROUND_UP(SVE_PT_SIZE(SVE_VQ_MAX, SVE_PT_REGS_SVE),
				  SVE_VQ_BYTES),
		.size = SVE_VQ_BYTES,
		.align = SVE_VQ_BYTES,
		.get = sve_get,
		.set = sve_set,
		.get_size = sve_get_size,
	},
#endif
#ifdef CONFIG_ARM64_PTR_AUTH
	[REGSET_PAC_MASK] = {
		.core_note_type = NT_ARM_PAC_MASK,
		.n = sizeof(struct user_pac_mask) / sizeof(u64),
		.size = sizeof(u64),
		.align = sizeof(u64),
		.get = pac_mask_get,
		/* this cannot be set dynamically */
	},
#ifdef CONFIG_CHECKPOINT_RESTORE
	[REGSET_PACA_KEYS] = {
		.core_note_type = NT_ARM_PACA_KEYS,
		.n = sizeof(struct user_pac_address_keys) / sizeof(__uint128_t),
		.size = sizeof(__uint128_t),
		.align = sizeof(__uint128_t),
		.get = pac_address_keys_get,
		.set = pac_address_keys_set,
	},
	[REGSET_PACG_KEYS] = {
		.core_note_type = NT_ARM_PACG_KEYS,
		.n = sizeof(struct user_pac_generic_keys) / sizeof(__uint128_t),
		.size = sizeof(__uint128_t),
		.align = sizeof(__uint128_t),
		.get = pac_generic_keys_get,
		.set = pac_generic_keys_set,
	},
#endif
#endif
};

static const struct user_regset_view user_aarch64_view = {
	.name = "aarch64", .e_machine = EM_AARCH64,
	.regsets = aarch64_regsets, .n = ARRAY_SIZE(aarch64_regsets)
};

#ifdef CONFIG_COMPAT
enum compat_regset {
	REGSET_COMPAT_GPR,
	REGSET_COMPAT_VFP,
};

static int compat_gpr_get(struct task_struct *target,
			  const struct user_regset *regset,
			  unsigned int pos, unsigned int count,
			  void *kbuf, void __user *ubuf)
{
	int ret = 0;
	unsigned int i, start, num_regs;

	/* Calculate the number of AArch32 registers contained in count */
	num_regs = count / regset->size;

	/* Convert pos into an register number */
	start = pos / regset->size;

	if (start + num_regs > regset->n)
		return -EIO;

	for (i = 0; i < num_regs; ++i) {
		unsigned int idx = start + i;
		compat_ulong_t reg;

		switch (idx) {
		case 15:
			reg = task_pt_regs(target)->pc;
			break;
		case 16:
			reg = task_pt_regs(target)->pstate;
			reg = pstate_to_compat_psr(reg);
			break;
		case 17:
			reg = task_pt_regs(target)->orig_x0;
			break;
		default:
			reg = task_pt_regs(target)->regs[idx];
		}

		if (kbuf) {
			memcpy(kbuf, &reg, sizeof(reg));
			kbuf += sizeof(reg);
		} else {
			ret = copy_to_user(ubuf, &reg, sizeof(reg));
			if (ret) {
				ret = -EFAULT;
				break;
			}

			ubuf += sizeof(reg);
		}
	}

	return ret;
}

static int compat_gpr_set(struct task_struct *target,
			  const struct user_regset *regset,
			  unsigned int pos, unsigned int count,
			  const void *kbuf, const void __user *ubuf)
{
	struct pt_regs newregs;
	int ret = 0;
	unsigned int i, start, num_regs;

	/* Calculate the number of AArch32 registers contained in count */
	num_regs = count / regset->size;

	/* Convert pos into an register number */
	start = pos / regset->size;

	if (start + num_regs > regset->n)
		return -EIO;

	newregs = *task_pt_regs(target);

	for (i = 0; i < num_regs; ++i) {
		unsigned int idx = start + i;
		compat_ulong_t reg;

		if (kbuf) {
			memcpy(&reg, kbuf, sizeof(reg));
			kbuf += sizeof(reg);
		} else {
			ret = copy_from_user(&reg, ubuf, sizeof(reg));
			if (ret) {
				ret = -EFAULT;
				break;
			}

			ubuf += sizeof(reg);
		}

		switch (idx) {
		case 15:
			newregs.pc = reg;
			break;
		case 16:
			reg = compat_psr_to_pstate(reg);
			newregs.pstate = reg;
			break;
		case 17:
			newregs.orig_x0 = reg;
			break;
		default:
			newregs.regs[idx] = reg;
		}

	}

	if (valid_user_regs(&newregs.user_regs, target))
		*task_pt_regs(target) = newregs;
	else
		ret = -EINVAL;

	return ret;
}

static int compat_vfp_get(struct task_struct *target,
			  const struct user_regset *regset,
			  unsigned int pos, unsigned int count,
			  void *kbuf, void __user *ubuf)
{
	struct user_fpsimd_state *uregs;
	compat_ulong_t fpscr;
	int ret, vregs_end_pos;

	uregs = &target->thread.uw.fpsimd_state;

	if (target == current)
		fpsimd_preserve_current_state();

	/*
	 * The VFP registers are packed into the fpsimd_state, so they all sit
	 * nicely together for us. We just need to create the fpscr separately.
	 */
	vregs_end_pos = VFP_STATE_SIZE - sizeof(compat_ulong_t);
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, uregs,
				  0, vregs_end_pos);

	if (count && !ret) {
		fpscr = (uregs->fpsr & VFP_FPSCR_STAT_MASK) |
			(uregs->fpcr & VFP_FPSCR_CTRL_MASK);

		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, &fpscr,
					  vregs_end_pos, VFP_STATE_SIZE);
	}

	return ret;
}

static int compat_vfp_set(struct task_struct *target,
			  const struct user_regset *regset,
			  unsigned int pos, unsigned int count,
			  const void *kbuf, const void __user *ubuf)
{
	struct user_fpsimd_state *uregs;
	compat_ulong_t fpscr;
	int ret, vregs_end_pos;

	uregs = &target->thread.uw.fpsimd_state;

	vregs_end_pos = VFP_STATE_SIZE - sizeof(compat_ulong_t);
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, uregs, 0,
				 vregs_end_pos);

	if (count && !ret) {
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &fpscr,
					 vregs_end_pos, VFP_STATE_SIZE);
		if (!ret) {
			uregs->fpsr = fpscr & VFP_FPSCR_STAT_MASK;
			uregs->fpcr = fpscr & VFP_FPSCR_CTRL_MASK;
		}
	}

	fpsimd_flush_task_state(target);
	return ret;
}

static int compat_tls_get(struct task_struct *target,
			  const struct user_regset *regset, unsigned int pos,
			  unsigned int count, void *kbuf, void __user *ubuf)
{
	compat_ulong_t tls = (compat_ulong_t)target->thread.uw.tp_value;
	return user_regset_copyout(&pos, &count, &kbuf, &ubuf, &tls, 0, -1);
}

static int compat_tls_set(struct task_struct *target,
			  const struct user_regset *regset, unsigned int pos,
			  unsigned int count, const void *kbuf,
			  const void __user *ubuf)
{
	int ret;
	compat_ulong_t tls = target->thread.uw.tp_value;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &tls, 0, -1);
	if (ret)
		return ret;

	target->thread.uw.tp_value = tls;
	return ret;
}

static const struct user_regset aarch32_regsets[] = {
	[REGSET_COMPAT_GPR] = {
		.core_note_type = NT_PRSTATUS,
		.n = COMPAT_ELF_NGREG,
		.size = sizeof(compat_elf_greg_t),
		.align = sizeof(compat_elf_greg_t),
		.get = compat_gpr_get,
		.set = compat_gpr_set
	},
	[REGSET_COMPAT_VFP] = {
		.core_note_type = NT_ARM_VFP,
		.n = VFP_STATE_SIZE / sizeof(compat_ulong_t),
		.size = sizeof(compat_ulong_t),
		.align = sizeof(compat_ulong_t),
		.get = compat_vfp_get,
		.set = compat_vfp_set
	},
};

static const struct user_regset_view user_aarch32_view = {
	.name = "aarch32", .e_machine = EM_ARM,
	.regsets = aarch32_regsets, .n = ARRAY_SIZE(aarch32_regsets)
};

static const struct user_regset aarch32_ptrace_regsets[] = {
	[REGSET_GPR] = {
		.core_note_type = NT_PRSTATUS,
		.n = COMPAT_ELF_NGREG,
		.size = sizeof(compat_elf_greg_t),
		.align = sizeof(compat_elf_greg_t),
		.get = compat_gpr_get,
		.set = compat_gpr_set
	},
	[REGSET_FPR] = {
		.core_note_type = NT_ARM_VFP,
		.n = VFP_STATE_SIZE / sizeof(compat_ulong_t),
		.size = sizeof(compat_ulong_t),
		.align = sizeof(compat_ulong_t),
		.get = compat_vfp_get,
		.set = compat_vfp_set
	},
	[REGSET_TLS] = {
		.core_note_type = NT_ARM_TLS,
		.n = 1,
		.size = sizeof(compat_ulong_t),
		.align = sizeof(compat_ulong_t),
		.get = compat_tls_get,
		.set = compat_tls_set,
	},
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	[REGSET_HW_BREAK] = {
		.core_note_type = NT_ARM_HW_BREAK,
		.n = sizeof(struct user_hwdebug_state) / sizeof(u32),
		.size = sizeof(u32),
		.align = sizeof(u32),
		.get = hw_break_get,
		.set = hw_break_set,
	},
	[REGSET_HW_WATCH] = {
		.core_note_type = NT_ARM_HW_WATCH,
		.n = sizeof(struct user_hwdebug_state) / sizeof(u32),
		.size = sizeof(u32),
		.align = sizeof(u32),
		.get = hw_break_get,
		.set = hw_break_set,
	},
#endif
	[REGSET_SYSTEM_CALL] = {
		.core_note_type = NT_ARM_SYSTEM_CALL,
		.n = 1,
		.size = sizeof(int),
		.align = sizeof(int),
		.get = system_call_get,
		.set = system_call_set,
	},
};

static const struct user_regset_view user_aarch32_ptrace_view = {
	.name = "aarch32", .e_machine = EM_ARM,
	.regsets = aarch32_ptrace_regsets, .n = ARRAY_SIZE(aarch32_ptrace_regsets)
};

static int compat_ptrace_read_user(struct task_struct *tsk, compat_ulong_t off,
				   compat_ulong_t __user *ret)
{
	compat_ulong_t tmp;

	if (off & 3)
		return -EIO;

	if (off == COMPAT_PT_TEXT_ADDR)
		tmp = tsk->mm->start_code;
	else if (off == COMPAT_PT_DATA_ADDR)
		tmp = tsk->mm->start_data;
	else if (off == COMPAT_PT_TEXT_END_ADDR)
		tmp = tsk->mm->end_code;
	else if (off < sizeof(compat_elf_gregset_t))
		return copy_regset_to_user(tsk, &user_aarch32_view,
					   REGSET_COMPAT_GPR, off,
					   sizeof(compat_ulong_t), ret);
	else if (off >= COMPAT_USER_SZ)
		return -EIO;
	else
		tmp = 0;

	return put_user(tmp, ret);
}

static int compat_ptrace_write_user(struct task_struct *tsk, compat_ulong_t off,
				    compat_ulong_t val)
{
	int ret;
	mm_segment_t old_fs = get_fs();

	if (off & 3 || off >= COMPAT_USER_SZ)
		return -EIO;

	if (off >= sizeof(compat_elf_gregset_t))
		return 0;

	set_fs(KERNEL_DS);
	ret = copy_regset_from_user(tsk, &user_aarch32_view,
				    REGSET_COMPAT_GPR, off,
				    sizeof(compat_ulong_t),
				    &val);
	set_fs(old_fs);

	return ret;
}

#ifdef CONFIG_HAVE_HW_BREAKPOINT

/*
 * Convert a virtual register number into an index for a thread_info
 * breakpoint array. Breakpoints are identified using positive numbers
 * whilst watchpoints are negative. The registers are laid out as pairs
 * of (address, control), each pair mapping to a unique hw_breakpoint struct.
 * Register 0 is reserved for describing resource information.
 */
static int compat_ptrace_hbp_num_to_idx(compat_long_t num)
{
	return (abs(num) - 1) >> 1;
}

static int compat_ptrace_hbp_get_resource_info(u32 *kdata)
{
	u8 num_brps, num_wrps, debug_arch, wp_len;
	u32 reg = 0;

	num_brps	= hw_breakpoint_slots(TYPE_INST);
	num_wrps	= hw_breakpoint_slots(TYPE_DATA);

	debug_arch	= debug_monitors_arch();
	wp_len		= 8;
	reg		|= debug_arch;
	reg		<<= 8;
	reg		|= wp_len;
	reg		<<= 8;
	reg		|= num_wrps;
	reg		<<= 8;
	reg		|= num_brps;

	*kdata = reg;
	return 0;
}

static int compat_ptrace_hbp_get(unsigned int note_type,
				 struct task_struct *tsk,
				 compat_long_t num,
				 u32 *kdata)
{
	u64 addr = 0;
	u32 ctrl = 0;

	int err, idx = compat_ptrace_hbp_num_to_idx(num);

	if (num & 1) {
		err = ptrace_hbp_get_addr(note_type, tsk, idx, &addr);
		*kdata = (u32)addr;
	} else {
		err = ptrace_hbp_get_ctrl(note_type, tsk, idx, &ctrl);
		*kdata = ctrl;
	}

	return err;
}

static int compat_ptrace_hbp_set(unsigned int note_type,
				 struct task_struct *tsk,
				 compat_long_t num,
				 u32 *kdata)
{
	u64 addr;
	u32 ctrl;

	int err, idx = compat_ptrace_hbp_num_to_idx(num);

	if (num & 1) {
		addr = *kdata;
		err = ptrace_hbp_set_addr(note_type, tsk, idx, addr);
	} else {
		ctrl = *kdata;
		err = ptrace_hbp_set_ctrl(note_type, tsk, idx, ctrl);
	}

	return err;
}

static int compat_ptrace_gethbpregs(struct task_struct *tsk, compat_long_t num,
				    compat_ulong_t __user *data)
{
	int ret;
	u32 kdata;

	/* Watchpoint */
	if (num < 0) {
		ret = compat_ptrace_hbp_get(NT_ARM_HW_WATCH, tsk, num, &kdata);
	/* Resource info */
	} else if (num == 0) {
		ret = compat_ptrace_hbp_get_resource_info(&kdata);
	/* Breakpoint */
	} else {
		ret = compat_ptrace_hbp_get(NT_ARM_HW_BREAK, tsk, num, &kdata);
	}

	if (!ret)
		ret = put_user(kdata, data);

	return ret;
}

static int compat_ptrace_sethbpregs(struct task_struct *tsk, compat_long_t num,
				    compat_ulong_t __user *data)
{
	int ret;
	u32 kdata = 0;

	if (num == 0)
		return 0;

	ret = get_user(kdata, data);
	if (ret)
		return ret;

	if (num < 0)
		ret = compat_ptrace_hbp_set(NT_ARM_HW_WATCH, tsk, num, &kdata);
	else
		ret = compat_ptrace_hbp_set(NT_ARM_HW_BREAK, tsk, num, &kdata);

	return ret;
}
#endif	/* CONFIG_HAVE_HW_BREAKPOINT */

long compat_arch_ptrace(struct task_struct *child, compat_long_t request,
			compat_ulong_t caddr, compat_ulong_t cdata)
{
	unsigned long addr = caddr;
	unsigned long data = cdata;
	void __user *datap = compat_ptr(data);
	int ret;

	switch (request) {
		case PTRACE_PEEKUSR:
			ret = compat_ptrace_read_user(child, addr, datap);
			break;

		case PTRACE_POKEUSR:
			ret = compat_ptrace_write_user(child, addr, data);
			break;

		case COMPAT_PTRACE_GETREGS:
			ret = copy_regset_to_user(child,
						  &user_aarch32_view,
						  REGSET_COMPAT_GPR,
						  0, sizeof(compat_elf_gregset_t),
						  datap);
			break;

		case COMPAT_PTRACE_SETREGS:
			ret = copy_regset_from_user(child,
						    &user_aarch32_view,
						    REGSET_COMPAT_GPR,
						    0, sizeof(compat_elf_gregset_t),
						    datap);
			break;

		case COMPAT_PTRACE_GET_THREAD_AREA:
			ret = put_user((compat_ulong_t)child->thread.uw.tp_value,
				       (compat_ulong_t __user *)datap);
			break;

		case COMPAT_PTRACE_SET_SYSCALL:
			task_pt_regs(child)->syscallno = data;
			ret = 0;
			break;

		case COMPAT_PTRACE_GETVFPREGS:
			ret = copy_regset_to_user(child,
						  &user_aarch32_view,
						  REGSET_COMPAT_VFP,
						  0, VFP_STATE_SIZE,
						  datap);
			break;

		case COMPAT_PTRACE_SETVFPREGS:
			ret = copy_regset_from_user(child,
						    &user_aarch32_view,
						    REGSET_COMPAT_VFP,
						    0, VFP_STATE_SIZE,
						    datap);
			break;

#ifdef CONFIG_HAVE_HW_BREAKPOINT
		case COMPAT_PTRACE_GETHBPREGS:
			ret = compat_ptrace_gethbpregs(child, addr, datap);
			break;

		case COMPAT_PTRACE_SETHBPREGS:
			ret = compat_ptrace_sethbpregs(child, addr, datap);
			break;
#endif

		default:
			ret = compat_ptrace_request(child, request, addr,
						    data);
			break;
	}

	return ret;
}
#endif /* CONFIG_COMPAT */

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
#ifdef CONFIG_COMPAT
	/*
	 * Core dumping of 32-bit tasks or compat ptrace requests must use the
	 * user_aarch32_view compatible with arm32. Native ptrace requests on
	 * 32-bit children use an extended user_aarch32_ptrace_view to allow
	 * access to the TLS register.
	 */
	if (is_compat_task())
		return &user_aarch32_view;
	else if (is_compat_thread(task_thread_info(task)))
		return &user_aarch32_ptrace_view;
#endif
	return &user_aarch64_view;
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	return ptrace_request(child, request, addr, data);
}

enum ptrace_syscall_dir {
	PTRACE_SYSCALL_ENTER = 0,
	PTRACE_SYSCALL_EXIT,
};

static void tracehook_report_syscall(struct pt_regs *regs,
				     enum ptrace_syscall_dir dir)
{
	int regno;
	unsigned long saved_reg;

	/*
	 * A scratch register (ip(r12) on AArch32, x7 on AArch64) is
	 * used to denote syscall entry/exit:
	 */
	regno = (is_compat_task() ? 12 : 7);
	saved_reg = regs->regs[regno];
	regs->regs[regno] = dir;

	if (dir == PTRACE_SYSCALL_EXIT)
		tracehook_report_syscall_exit(regs, 0);
	else if (tracehook_report_syscall_entry(regs))
		forget_syscall(regs);

	regs->regs[regno] = saved_reg;
}

int syscall_trace_enter(struct pt_regs *regs)
{
	if (test_thread_flag(TIF_SYSCALL_TRACE) ||
		test_thread_flag(TIF_SYSCALL_EMU)) {
		tracehook_report_syscall(regs, PTRACE_SYSCALL_ENTER);
		if (!in_syscall(regs) || test_thread_flag(TIF_SYSCALL_EMU))
			return -1;
	}

	/* Do the secure computing after ptrace; failures should be fast. */
	if (secure_computing(NULL) == -1)
		return -1;

	if (test_thread_flag(TIF_SYSCALL_TRACEPOINT))
		trace_sys_enter(regs, regs->syscallno);

	audit_syscall_entry(regs->syscallno, regs->orig_x0, regs->regs[1],
			    regs->regs[2], regs->regs[3]);

	return regs->syscallno;
}

void syscall_trace_exit(struct pt_regs *regs)
{
	audit_syscall_exit(regs);

	if (test_thread_flag(TIF_SYSCALL_TRACEPOINT))
		trace_sys_exit(regs, regs_return_value(regs));

	if (test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall(regs, PTRACE_SYSCALL_EXIT);

	rseq_syscall(regs);
}

/*
 * SPSR_ELx bits which are always architecturally RES0 per ARM DDI 0487D.a.
 * We permit userspace to set SSBS (AArch64 bit 12, AArch32 bit 23) which is
 * not described in ARM DDI 0487D.a.
 * We treat PAN and UAO as RES0 bits, as they are meaningless at EL0, and may
 * be allocated an EL0 meaning in future.
 * Userspace cannot use these until they have an architectural meaning.
 * Note that this follows the SPSR_ELx format, not the AArch32 PSR format.
 * We also reserve IL for the kernel; SS is handled dynamically.
 */
#define SPSR_EL1_AARCH64_RES0_BITS \
	(GENMASK_ULL(63, 32) | GENMASK_ULL(27, 25) | GENMASK_ULL(23, 22) | \
	 GENMASK_ULL(20, 13) | GENMASK_ULL(11, 10) | GENMASK_ULL(5, 5))
#define SPSR_EL1_AARCH32_RES0_BITS \
	(GENMASK_ULL(63, 32) | GENMASK_ULL(22, 22) | GENMASK_ULL(20, 20))

static int valid_compat_regs(struct user_pt_regs *regs)
{
	regs->pstate &= ~SPSR_EL1_AARCH32_RES0_BITS;

	if (!system_supports_mixed_endian_el0()) {
		if (IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
			regs->pstate |= PSR_AA32_E_BIT;
		else
			regs->pstate &= ~PSR_AA32_E_BIT;
	}

	if (user_mode(regs) && (regs->pstate & PSR_MODE32_BIT) &&
	    (regs->pstate & PSR_AA32_A_BIT) == 0 &&
	    (regs->pstate & PSR_AA32_I_BIT) == 0 &&
	    (regs->pstate & PSR_AA32_F_BIT) == 0) {
		return 1;
	}

	/*
	 * Force PSR to a valid 32-bit EL0t, preserving the same bits as
	 * arch/arm.
	 */
	regs->pstate &= PSR_AA32_N_BIT | PSR_AA32_Z_BIT |
			PSR_AA32_C_BIT | PSR_AA32_V_BIT |
			PSR_AA32_Q_BIT | PSR_AA32_IT_MASK |
			PSR_AA32_GE_MASK | PSR_AA32_E_BIT |
			PSR_AA32_T_BIT;
	regs->pstate |= PSR_MODE32_BIT;

	return 0;
}

static int valid_native_regs(struct user_pt_regs *regs)
{
	regs->pstate &= ~SPSR_EL1_AARCH64_RES0_BITS;

	if (user_mode(regs) && !(regs->pstate & PSR_MODE32_BIT) &&
	    (regs->pstate & PSR_D_BIT) == 0 &&
	    (regs->pstate & PSR_A_BIT) == 0 &&
	    (regs->pstate & PSR_I_BIT) == 0 &&
	    (regs->pstate & PSR_F_BIT) == 0) {
		return 1;
	}

	/* Force PSR to a valid 64-bit EL0t */
	regs->pstate &= PSR_N_BIT | PSR_Z_BIT | PSR_C_BIT | PSR_V_BIT;

	return 0;
}

/*
 * Are the current registers suitable for user mode? (used to maintain
 * security in signal handlers)
 */
int valid_user_regs(struct user_pt_regs *regs, struct task_struct *task)
{
	if (!test_tsk_thread_flag(task, TIF_SINGLESTEP))
		regs->pstate &= ~DBG_SPSR_SS;

	if (is_compat_thread(task_thread_info(task)))
		return valid_compat_regs(regs);
	else
		return valid_native_regs(regs);
}
