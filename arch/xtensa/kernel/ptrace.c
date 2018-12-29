/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2007  Tensilica Inc.
 *
 * Joe Taylor	<joe@tensilica.com, joetylr@yahoo.com>
 * Chris Zankel <chris@zankel.net>
 * Scott Foehner<sfoehner@yahoo.com>,
 * Kevin Chea
 * Marc Gauthier<marc@tensilica.com> <marc@alumni.uwaterloo.ca>
 */

#include <linux/errno.h>
#include <linux/hw_breakpoint.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/perf_event.h>
#include <linux/ptrace.h>
#include <linux/regset.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/security.h>
#include <linux/signal.h>
#include <linux/smp.h>
#include <linux/tracehook.h>
#include <linux/uaccess.h>

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

#include <asm/coprocessor.h>
#include <asm/elf.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/ptrace.h>

static int gpr_get(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   void *kbuf, void __user *ubuf)
{
	struct pt_regs *regs = task_pt_regs(target);
	struct user_pt_regs newregs = {
		.pc = regs->pc,
		.ps = regs->ps & ~(1 << PS_EXCM_BIT),
		.lbeg = regs->lbeg,
		.lend = regs->lend,
		.lcount = regs->lcount,
		.sar = regs->sar,
		.threadptr = regs->threadptr,
		.windowbase = regs->windowbase,
		.windowstart = regs->windowstart,
	};

	memcpy(newregs.a,
	       regs->areg + XCHAL_NUM_AREGS - regs->windowbase * 4,
	       regs->windowbase * 16);
	memcpy(newregs.a + regs->windowbase * 4,
	       regs->areg,
	       (WSBITS - regs->windowbase) * 16);

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   &newregs, 0, -1);
}

static int gpr_set(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	int ret;
	struct user_pt_regs newregs = {0};
	struct pt_regs *regs;
	const u32 ps_mask = PS_CALLINC_MASK | PS_OWB_MASK;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &newregs, 0, -1);
	if (ret)
		return ret;

	if (newregs.windowbase >= XCHAL_NUM_AREGS / 4)
		return -EINVAL;

	regs = task_pt_regs(target);
	regs->pc = newregs.pc;
	regs->ps = (regs->ps & ~ps_mask) | (newregs.ps & ps_mask);
	regs->lbeg = newregs.lbeg;
	regs->lend = newregs.lend;
	regs->lcount = newregs.lcount;
	regs->sar = newregs.sar;
	regs->threadptr = newregs.threadptr;

	if (newregs.windowbase != regs->windowbase ||
	    newregs.windowstart != regs->windowstart) {
		u32 rotws, wmask;

		rotws = (((newregs.windowstart |
			   (newregs.windowstart << WSBITS)) >>
			  newregs.windowbase) &
			 ((1 << WSBITS) - 1)) & ~1;
		wmask = ((rotws ? WSBITS + 1 - ffs(rotws) : 0) << 4) |
			(rotws & 0xF) | 1;
		regs->windowbase = newregs.windowbase;
		regs->windowstart = newregs.windowstart;
		regs->wmask = wmask;
	}

	memcpy(regs->areg + XCHAL_NUM_AREGS - newregs.windowbase * 4,
	       newregs.a, newregs.windowbase * 16);
	memcpy(regs->areg, newregs.a + newregs.windowbase * 4,
	       (WSBITS - newregs.windowbase) * 16);

	return 0;
}

static int tie_get(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   void *kbuf, void __user *ubuf)
{
	int ret;
	struct pt_regs *regs = task_pt_regs(target);
	struct thread_info *ti = task_thread_info(target);
	elf_xtregs_t *newregs = kzalloc(sizeof(elf_xtregs_t), GFP_KERNEL);

	if (!newregs)
		return -ENOMEM;

	newregs->opt = regs->xtregs_opt;
	newregs->user = ti->xtregs_user;

#if XTENSA_HAVE_COPROCESSORS
	/* Flush all coprocessor registers to memory. */
	coprocessor_flush_all(ti);
	newregs->cp0 = ti->xtregs_cp.cp0;
	newregs->cp1 = ti->xtregs_cp.cp1;
	newregs->cp2 = ti->xtregs_cp.cp2;
	newregs->cp3 = ti->xtregs_cp.cp3;
	newregs->cp4 = ti->xtregs_cp.cp4;
	newregs->cp5 = ti->xtregs_cp.cp5;
	newregs->cp6 = ti->xtregs_cp.cp6;
	newregs->cp7 = ti->xtregs_cp.cp7;
#endif
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  newregs, 0, -1);
	kfree(newregs);
	return ret;
}

static int tie_set(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	int ret;
	struct pt_regs *regs = task_pt_regs(target);
	struct thread_info *ti = task_thread_info(target);
	elf_xtregs_t *newregs = kzalloc(sizeof(elf_xtregs_t), GFP_KERNEL);

	if (!newregs)
		return -ENOMEM;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 newregs, 0, -1);

	if (ret)
		goto exit;
	regs->xtregs_opt = newregs->opt;
	ti->xtregs_user = newregs->user;

#if XTENSA_HAVE_COPROCESSORS
	/* Flush all coprocessors before we overwrite them. */
	coprocessor_flush_all(ti);
	coprocessor_release_all(ti);
	ti->xtregs_cp.cp0 = newregs->cp0;
	ti->xtregs_cp.cp1 = newregs->cp1;
	ti->xtregs_cp.cp2 = newregs->cp2;
	ti->xtregs_cp.cp3 = newregs->cp3;
	ti->xtregs_cp.cp4 = newregs->cp4;
	ti->xtregs_cp.cp5 = newregs->cp5;
	ti->xtregs_cp.cp6 = newregs->cp6;
	ti->xtregs_cp.cp7 = newregs->cp7;
#endif
exit:
	kfree(newregs);
	return ret;
}

enum xtensa_regset {
	REGSET_GPR,
	REGSET_TIE,
};

static const struct user_regset xtensa_regsets[] = {
	[REGSET_GPR] = {
		.core_note_type = NT_PRSTATUS,
		.n = sizeof(struct user_pt_regs) / sizeof(u32),
		.size = sizeof(u32),
		.align = sizeof(u32),
		.get = gpr_get,
		.set = gpr_set,
	},
	[REGSET_TIE] = {
		.core_note_type = NT_PRFPREG,
		.n = sizeof(elf_xtregs_t) / sizeof(u32),
		.size = sizeof(u32),
		.align = sizeof(u32),
		.get = tie_get,
		.set = tie_set,
	},
};

static const struct user_regset_view user_xtensa_view = {
	.name = "xtensa",
	.e_machine = EM_XTENSA,
	.regsets = xtensa_regsets,
	.n = ARRAY_SIZE(xtensa_regsets)
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_xtensa_view;
}

void user_enable_single_step(struct task_struct *child)
{
	child->ptrace |= PT_SINGLESTEP;
}

void user_disable_single_step(struct task_struct *child)
{
	child->ptrace &= ~PT_SINGLESTEP;
}

/*
 * Called by kernel/ptrace.c when detaching to disable single stepping.
 */

void ptrace_disable(struct task_struct *child)
{
	/* Nothing to do.. */
}

static int ptrace_getregs(struct task_struct *child, void __user *uregs)
{
	return copy_regset_to_user(child, &user_xtensa_view, REGSET_GPR,
				   0, sizeof(xtensa_gregset_t), uregs);
}

static int ptrace_setregs(struct task_struct *child, void __user *uregs)
{
	return copy_regset_from_user(child, &user_xtensa_view, REGSET_GPR,
				     0, sizeof(xtensa_gregset_t), uregs);
}

static int ptrace_getxregs(struct task_struct *child, void __user *uregs)
{
	return copy_regset_to_user(child, &user_xtensa_view, REGSET_TIE,
				   0, sizeof(elf_xtregs_t), uregs);
}

static int ptrace_setxregs(struct task_struct *child, void __user *uregs)
{
	return copy_regset_from_user(child, &user_xtensa_view, REGSET_TIE,
				     0, sizeof(elf_xtregs_t), uregs);
}

static int ptrace_peekusr(struct task_struct *child, long regno,
			  long __user *ret)
{
	struct pt_regs *regs;
	unsigned long tmp;

	regs = task_pt_regs(child);
	tmp = 0;  /* Default return value. */

	switch(regno) {
	case REG_AR_BASE ... REG_AR_BASE + XCHAL_NUM_AREGS - 1:
		tmp = regs->areg[regno - REG_AR_BASE];
		break;

	case REG_A_BASE ... REG_A_BASE + 15:
		tmp = regs->areg[regno - REG_A_BASE];
		break;

	case REG_PC:
		tmp = regs->pc;
		break;

	case REG_PS:
		/* Note: PS.EXCM is not set while user task is running;
		 * its being set in regs is for exception handling
		 * convenience.
		 */
		tmp = (regs->ps & ~(1 << PS_EXCM_BIT));
		break;

	case REG_WB:
		break;		/* tmp = 0 */

	case REG_WS:
		{
			unsigned long wb = regs->windowbase;
			unsigned long ws = regs->windowstart;
			tmp = ((ws >> wb) | (ws << (WSBITS - wb))) &
				((1 << WSBITS) - 1);
			break;
		}
	case REG_LBEG:
		tmp = regs->lbeg;
		break;

	case REG_LEND:
		tmp = regs->lend;
		break;

	case REG_LCOUNT:
		tmp = regs->lcount;
		break;

	case REG_SAR:
		tmp = regs->sar;
		break;

	case SYSCALL_NR:
		tmp = regs->syscall;
		break;

	default:
		return -EIO;
	}
	return put_user(tmp, ret);
}

static int ptrace_pokeusr(struct task_struct *child, long regno, long val)
{
	struct pt_regs *regs;
	regs = task_pt_regs(child);

	switch (regno) {
	case REG_AR_BASE ... REG_AR_BASE + XCHAL_NUM_AREGS - 1:
		regs->areg[regno - REG_AR_BASE] = val;
		break;

	case REG_A_BASE ... REG_A_BASE + 15:
		regs->areg[regno - REG_A_BASE] = val;
		break;

	case REG_PC:
		regs->pc = val;
		break;

	case SYSCALL_NR:
		regs->syscall = val;
		break;

	default:
		return -EIO;
	}
	return 0;
}

#ifdef CONFIG_HAVE_HW_BREAKPOINT
static void ptrace_hbptriggered(struct perf_event *bp,
				struct perf_sample_data *data,
				struct pt_regs *regs)
{
	int i;
	struct arch_hw_breakpoint *bkpt = counter_arch_bp(bp);

	if (bp->attr.bp_type & HW_BREAKPOINT_X) {
		for (i = 0; i < XCHAL_NUM_IBREAK; ++i)
			if (current->thread.ptrace_bp[i] == bp)
				break;
		i <<= 1;
	} else {
		for (i = 0; i < XCHAL_NUM_DBREAK; ++i)
			if (current->thread.ptrace_wp[i] == bp)
				break;
		i = (i << 1) | 1;
	}

	force_sig_ptrace_errno_trap(i, (void __user *)bkpt->address);
}

static struct perf_event *ptrace_hbp_create(struct task_struct *tsk, int type)
{
	struct perf_event_attr attr;

	ptrace_breakpoint_init(&attr);

	/* Initialise fields to sane defaults. */
	attr.bp_addr	= 0;
	attr.bp_len	= 1;
	attr.bp_type	= type;
	attr.disabled	= 1;

	return register_user_hw_breakpoint(&attr, ptrace_hbptriggered, NULL,
					   tsk);
}

/*
 * Address bit 0 choose instruction (0) or data (1) break register, bits
 * 31..1 are the register number.
 * Both PTRACE_GETHBPREGS and PTRACE_SETHBPREGS transfer two 32-bit words:
 * address (0) and control (1).
 * Instruction breakpoint contorl word is 0 to clear breakpoint, 1 to set.
 * Data breakpoint control word bit 31 is 'trigger on store', bit 30 is
 * 'trigger on load, bits 29..0 are length. Length 0 is used to clear a
 * breakpoint. To set a breakpoint length must be a power of 2 in the range
 * 1..64 and the address must be length-aligned.
 */

static long ptrace_gethbpregs(struct task_struct *child, long addr,
			      long __user *datap)
{
	struct perf_event *bp;
	u32 user_data[2] = {0};
	bool dbreak = addr & 1;
	unsigned idx = addr >> 1;

	if ((!dbreak && idx >= XCHAL_NUM_IBREAK) ||
	    (dbreak && idx >= XCHAL_NUM_DBREAK))
		return -EINVAL;

	if (dbreak)
		bp = child->thread.ptrace_wp[idx];
	else
		bp = child->thread.ptrace_bp[idx];

	if (bp) {
		user_data[0] = bp->attr.bp_addr;
		user_data[1] = bp->attr.disabled ? 0 : bp->attr.bp_len;
		if (dbreak) {
			if (bp->attr.bp_type & HW_BREAKPOINT_R)
				user_data[1] |= DBREAKC_LOAD_MASK;
			if (bp->attr.bp_type & HW_BREAKPOINT_W)
				user_data[1] |= DBREAKC_STOR_MASK;
		}
	}

	if (copy_to_user(datap, user_data, sizeof(user_data)))
		return -EFAULT;

	return 0;
}

static long ptrace_sethbpregs(struct task_struct *child, long addr,
			      long __user *datap)
{
	struct perf_event *bp;
	struct perf_event_attr attr;
	u32 user_data[2];
	bool dbreak = addr & 1;
	unsigned idx = addr >> 1;
	int bp_type = 0;

	if ((!dbreak && idx >= XCHAL_NUM_IBREAK) ||
	    (dbreak && idx >= XCHAL_NUM_DBREAK))
		return -EINVAL;

	if (copy_from_user(user_data, datap, sizeof(user_data)))
		return -EFAULT;

	if (dbreak) {
		bp = child->thread.ptrace_wp[idx];
		if (user_data[1] & DBREAKC_LOAD_MASK)
			bp_type |= HW_BREAKPOINT_R;
		if (user_data[1] & DBREAKC_STOR_MASK)
			bp_type |= HW_BREAKPOINT_W;
	} else {
		bp = child->thread.ptrace_bp[idx];
		bp_type = HW_BREAKPOINT_X;
	}

	if (!bp) {
		bp = ptrace_hbp_create(child,
				       bp_type ? bp_type : HW_BREAKPOINT_RW);
		if (IS_ERR(bp))
			return PTR_ERR(bp);
		if (dbreak)
			child->thread.ptrace_wp[idx] = bp;
		else
			child->thread.ptrace_bp[idx] = bp;
	}

	attr = bp->attr;
	attr.bp_addr = user_data[0];
	attr.bp_len = user_data[1] & ~(DBREAKC_LOAD_MASK | DBREAKC_STOR_MASK);
	attr.bp_type = bp_type;
	attr.disabled = !attr.bp_len;

	return modify_user_hw_breakpoint(bp, &attr);
}
#endif

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	int ret = -EPERM;
	void __user *datap = (void __user *) data;

	switch (request) {
	case PTRACE_PEEKUSR:	/* read register specified by addr. */
		ret = ptrace_peekusr(child, addr, datap);
		break;

	case PTRACE_POKEUSR:	/* write register specified by addr. */
		ret = ptrace_pokeusr(child, addr, data);
		break;

	case PTRACE_GETREGS:
		ret = ptrace_getregs(child, datap);
		break;

	case PTRACE_SETREGS:
		ret = ptrace_setregs(child, datap);
		break;

	case PTRACE_GETXTREGS:
		ret = ptrace_getxregs(child, datap);
		break;

	case PTRACE_SETXTREGS:
		ret = ptrace_setxregs(child, datap);
		break;
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	case PTRACE_GETHBPREGS:
		ret = ptrace_gethbpregs(child, addr, datap);
		break;

	case PTRACE_SETHBPREGS:
		ret = ptrace_sethbpregs(child, addr, datap);
		break;
#endif
	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

void do_syscall_trace_enter(struct pt_regs *regs)
{
	if (test_thread_flag(TIF_SYSCALL_TRACE) &&
	    tracehook_report_syscall_entry(regs))
		regs->syscall = NO_SYSCALL;

	if (test_thread_flag(TIF_SYSCALL_TRACEPOINT))
		trace_sys_enter(regs, syscall_get_nr(current, regs));
}

void do_syscall_trace_leave(struct pt_regs *regs)
{
	int step;

	if (test_thread_flag(TIF_SYSCALL_TRACEPOINT))
		trace_sys_exit(regs, regs_return_value(regs));

	step = test_thread_flag(TIF_SINGLESTEP);

	if (step || test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, step);
}
