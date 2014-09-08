/*
 * Based on arch/arm/kernel/ptrace.c
 *
 * By Ross Biro 1/23/92
 * edited by Linus Torvalds
 * ARM modifications Copyright (C) 2000 Russell King
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/audit.h>
#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/security.h>
#include <linux/init.h>
#include <linux/signal.h>
#include <linux/uaccess.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <linux/regset.h>
#include <linux/tracehook.h>
#include <linux/elf.h>

#include <asm/compat.h>
#include <asm/debug-monitors.h>
#include <asm/pgtable.h>
#include <asm/syscall.h>
#include <asm/traps.h>
#include <asm/system_misc.h>

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

/*
 * TODO: does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * Called by kernel/ptrace.c when detaching..
 */
void ptrace_disable(struct task_struct *child)
{
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
	siginfo_t info = {
		.si_signo	= SIGTRAP,
		.si_errno	= 0,
		.si_code	= TRAP_HWBKPT,
		.si_addr	= (void __user *)(bkpt->trigger),
	};

#ifdef CONFIG_COMPAT
	int i;

	if (!is_compat_task())
		goto send_sig;

	for (i = 0; i < ARM_MAX_BRP; ++i) {
		if (current->thread.debug.hbp_break[i] == bp) {
			info.si_errno = (i << 1) + 1;
			break;
		}
	}
	for (i = ARM_MAX_BRP; i < ARM_MAX_HBP_SLOTS && !bp; ++i) {
		if (current->thread.debug.hbp_watch[i] == bp) {
			info.si_errno = -((i << 1) + 1);
			break;
		}
	}

send_sig:
#endif
	force_sig_info(SIGTRAP, &info, current);
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
		if (idx < ARM_MAX_BRP)
			bp = tsk->thread.debug.hbp_break[idx];
		break;
	case NT_ARM_HW_WATCH:
		if (idx < ARM_MAX_WRP)
			bp = tsk->thread.debug.hbp_watch[idx];
		break;
	}

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
		if (idx < ARM_MAX_BRP) {
			tsk->thread.debug.hbp_break[idx] = bp;
			err = 0;
		}
		break;
	case NT_ARM_HW_WATCH:
		if (idx < ARM_MAX_WRP) {
			tsk->thread.debug.hbp_watch[idx] = bp;
			err = 0;
		}
		break;
	}

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
	int err, len, type, disabled = !ctrl.enabled;

	attr->disabled = disabled;
	if (disabled)
		return 0;

	err = arch_bp_generic_fields(ctrl, &len, &type);
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

	*addr = bp ? bp->attr.bp_addr : 0;
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
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &addr,
					 offset, offset + PTRACE_HBP_ADDR_SZ);
		if (ret)
			return ret;
		ret = ptrace_hbp_set_addr(note_type, target, idx, addr);
		if (ret)
			return ret;
		offset += PTRACE_HBP_ADDR_SZ;

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
	struct user_pt_regs newregs;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &newregs, 0, -1);
	if (ret)
		return ret;

	if (!valid_user_regs(&newregs))
		return -EINVAL;

	task_pt_regs(target)->user_regs = newregs;
	return 0;
}

/*
 * TODO: update fp accessors for lazy context switching (sync/flush hwstate)
 */
static int fpr_get(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   void *kbuf, void __user *ubuf)
{
	struct user_fpsimd_state *uregs;
	uregs = &target->thread.fpsimd_state.user_fpsimd;
	return user_regset_copyout(&pos, &count, &kbuf, &ubuf, uregs, 0, -1);
}

static int fpr_set(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	int ret;
	struct user_fpsimd_state newstate;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &newstate, 0, -1);
	if (ret)
		return ret;

	target->thread.fpsimd_state.user_fpsimd = newstate;
	fpsimd_flush_task_state(target);
	return ret;
}

static int tls_get(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   void *kbuf, void __user *ubuf)
{
	unsigned long *tls = &target->thread.tp_value;
	return user_regset_copyout(&pos, &count, &kbuf, &ubuf, tls, 0, -1);
}

static int tls_set(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	int ret;
	unsigned long tls;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &tls, 0, -1);
	if (ret)
		return ret;

	target->thread.tp_value = tls;
	return ret;
}

enum aarch64_regset {
	REGSET_GPR,
	REGSET_FPR,
	REGSET_TLS,
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	REGSET_HW_BREAK,
	REGSET_HW_WATCH,
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
};

static const struct user_regset_view user_aarch64_view = {
	.name = "aarch64", .e_machine = EM_AARCH64,
	.regsets = aarch64_regsets, .n = ARRAY_SIZE(aarch64_regsets)
};

#ifdef CONFIG_COMPAT
#include <linux/compat.h>

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
			if (ret)
				break;

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
			if (ret)
				return ret;

			ubuf += sizeof(reg);
		}

		switch (idx) {
		case 15:
			newregs.pc = reg;
			break;
		case 16:
			newregs.pstate = reg;
			break;
		case 17:
			newregs.orig_x0 = reg;
			break;
		default:
			newregs.regs[idx] = reg;
		}

	}

	if (valid_user_regs(&newregs.user_regs))
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
	int ret;

	uregs = &target->thread.fpsimd_state.user_fpsimd;

	/*
	 * The VFP registers are packed into the fpsimd_state, so they all sit
	 * nicely together for us. We just need to create the fpscr separately.
	 */
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, uregs, 0,
				  VFP_STATE_SIZE - sizeof(compat_ulong_t));

	if (count && !ret) {
		fpscr = (uregs->fpsr & VFP_FPSCR_STAT_MASK) |
			(uregs->fpcr & VFP_FPSCR_CTRL_MASK);
		ret = put_user(fpscr, (compat_ulong_t *)ubuf);
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
	int ret;

	if (pos + count > VFP_STATE_SIZE)
		return -EIO;

	uregs = &target->thread.fpsimd_state.user_fpsimd;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, uregs, 0,
				 VFP_STATE_SIZE - sizeof(compat_ulong_t));

	if (count && !ret) {
		ret = get_user(fpscr, (compat_ulong_t *)ubuf);
		uregs->fpsr = fpscr & VFP_FPSCR_STAT_MASK;
		uregs->fpcr = fpscr & VFP_FPSCR_CTRL_MASK;
	}

	fpsimd_flush_task_state(target);
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

	int err, idx = compat_ptrace_hbp_num_to_idx(num);;

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
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
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
	set_fs(old_fs);

	if (!ret)
		ret = put_user(kdata, data);

	return ret;
}

static int compat_ptrace_sethbpregs(struct task_struct *tsk, compat_long_t num,
				    compat_ulong_t __user *data)
{
	int ret;
	u32 kdata = 0;
	mm_segment_t old_fs = get_fs();

	if (num == 0)
		return 0;

	ret = get_user(kdata, data);
	if (ret)
		return ret;

	set_fs(KERNEL_DS);
	if (num < 0)
		ret = compat_ptrace_hbp_set(NT_ARM_HW_WATCH, tsk, num, &kdata);
	else
		ret = compat_ptrace_hbp_set(NT_ARM_HW_BREAK, tsk, num, &kdata);
	set_fs(old_fs);

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
			ret = put_user((compat_ulong_t)child->thread.tp_value,
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
	if (is_compat_thread(task_thread_info(task)))
		return &user_aarch32_view;
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
		regs->syscallno = ~0UL;

	regs->regs[regno] = saved_reg;
}

asmlinkage int syscall_trace_enter(struct pt_regs *regs)
{
	if (test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall(regs, PTRACE_SYSCALL_ENTER);

	if (test_thread_flag(TIF_SYSCALL_TRACEPOINT))
		trace_sys_enter(regs, regs->syscallno);

	audit_syscall_entry(syscall_get_arch(), regs->syscallno,
		regs->orig_x0, regs->regs[1], regs->regs[2], regs->regs[3]);

	return regs->syscallno;
}

asmlinkage void syscall_trace_exit(struct pt_regs *regs)
{
	audit_syscall_exit(regs);

	if (test_thread_flag(TIF_SYSCALL_TRACEPOINT))
		trace_sys_exit(regs, regs_return_value(regs));

	if (test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall(regs, PTRACE_SYSCALL_EXIT);
}
