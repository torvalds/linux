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
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/security.h>
#include <linux/signal.h>
#include <linux/smp.h>
#include <linux/tracehook.h>
#include <linux/uaccess.h>

#include <asm/coprocessor.h>
#include <asm/elf.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/ptrace.h>


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
	struct pt_regs *regs = task_pt_regs(child);
	xtensa_gregset_t __user *gregset = uregs;
	unsigned long wb = regs->windowbase;
	int i;

	if (!access_ok(VERIFY_WRITE, uregs, sizeof(xtensa_gregset_t)))
		return -EIO;

	__put_user(regs->pc, &gregset->pc);
	__put_user(regs->ps & ~(1 << PS_EXCM_BIT), &gregset->ps);
	__put_user(regs->lbeg, &gregset->lbeg);
	__put_user(regs->lend, &gregset->lend);
	__put_user(regs->lcount, &gregset->lcount);
	__put_user(regs->windowstart, &gregset->windowstart);
	__put_user(regs->windowbase, &gregset->windowbase);
	__put_user(regs->threadptr, &gregset->threadptr);

	for (i = 0; i < XCHAL_NUM_AREGS; i++)
		__put_user(regs->areg[i],
			   gregset->a + ((wb * 4 + i) % XCHAL_NUM_AREGS));

	return 0;
}

static int ptrace_setregs(struct task_struct *child, void __user *uregs)
{
	struct pt_regs *regs = task_pt_regs(child);
	xtensa_gregset_t *gregset = uregs;
	const unsigned long ps_mask = PS_CALLINC_MASK | PS_OWB_MASK;
	unsigned long ps;
	unsigned long wb, ws;

	if (!access_ok(VERIFY_WRITE, uregs, sizeof(xtensa_gregset_t)))
		return -EIO;

	__get_user(regs->pc, &gregset->pc);
	__get_user(ps, &gregset->ps);
	__get_user(regs->lbeg, &gregset->lbeg);
	__get_user(regs->lend, &gregset->lend);
	__get_user(regs->lcount, &gregset->lcount);
	__get_user(ws, &gregset->windowstart);
	__get_user(wb, &gregset->windowbase);
	__get_user(regs->threadptr, &gregset->threadptr);

	regs->ps = (regs->ps & ~ps_mask) | (ps & ps_mask) | (1 << PS_EXCM_BIT);

	if (wb >= XCHAL_NUM_AREGS / 4)
		return -EFAULT;

	if (wb != regs->windowbase || ws != regs->windowstart) {
		unsigned long rotws, wmask;

		rotws = (((ws | (ws << WSBITS)) >> wb) &
			 ((1 << WSBITS) - 1)) & ~1;
		wmask = ((rotws ? WSBITS + 1 - ffs(rotws) : 0) << 4) |
			(rotws & 0xF) | 1;
		regs->windowbase = wb;
		regs->windowstart = ws;
		regs->wmask = wmask;
	}

	if (wb != 0 && __copy_from_user(regs->areg + XCHAL_NUM_AREGS - wb * 4,
					gregset->a, wb * 16))
		return -EFAULT;

	if (__copy_from_user(regs->areg, gregset->a + wb * 4,
			     (WSBITS - wb) * 16))
		return -EFAULT;

	return 0;
}


static int ptrace_getxregs(struct task_struct *child, void __user *uregs)
{
	struct pt_regs *regs = task_pt_regs(child);
	struct thread_info *ti = task_thread_info(child);
	elf_xtregs_t __user *xtregs = uregs;
	int ret = 0;

	if (!access_ok(VERIFY_WRITE, uregs, sizeof(elf_xtregs_t)))
		return -EIO;

#if XTENSA_HAVE_COPROCESSORS
	/* Flush all coprocessor registers to memory. */
	coprocessor_flush_all(ti);
	ret |= __copy_to_user(&xtregs->cp0, &ti->xtregs_cp,
			      sizeof(xtregs_coprocessor_t));
#endif
	ret |= __copy_to_user(&xtregs->opt, &regs->xtregs_opt,
			      sizeof(xtregs->opt));
	ret |= __copy_to_user(&xtregs->user,&ti->xtregs_user,
			      sizeof(xtregs->user));

	return ret ? -EFAULT : 0;
}

static int ptrace_setxregs(struct task_struct *child, void __user *uregs)
{
	struct thread_info *ti = task_thread_info(child);
	struct pt_regs *regs = task_pt_regs(child);
	elf_xtregs_t *xtregs = uregs;
	int ret = 0;

	if (!access_ok(VERIFY_READ, uregs, sizeof(elf_xtregs_t)))
		return -EFAULT;

#if XTENSA_HAVE_COPROCESSORS
	/* Flush all coprocessors before we overwrite them. */
	coprocessor_flush_all(ti);
	coprocessor_release_all(ti);

	ret |= __copy_from_user(&ti->xtregs_cp, &xtregs->cp0,
				sizeof(xtregs_coprocessor_t));
#endif
	ret |= __copy_from_user(&regs->xtregs_opt, &xtregs->opt,
				sizeof(xtregs->opt));
	ret |= __copy_from_user(&ti->xtregs_user, &xtregs->user,
				sizeof(xtregs->user));

	return ret ? -EFAULT : 0;
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
	case PTRACE_PEEKTEXT:	/* read word at location addr. */
	case PTRACE_PEEKDATA:
		ret = generic_ptrace_peekdata(child, addr, data);
		break;

	case PTRACE_PEEKUSR:	/* read register specified by addr. */
		ret = ptrace_peekusr(child, addr, datap);
		break;

	case PTRACE_POKETEXT:	/* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = generic_ptrace_pokedata(child, addr, data);
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

unsigned long do_syscall_trace_enter(struct pt_regs *regs)
{
	if (test_thread_flag(TIF_SYSCALL_TRACE) &&
	    tracehook_report_syscall_entry(regs))
		return -1;

	return regs->areg[2];
}

void do_syscall_trace_leave(struct pt_regs *regs)
{
	int step;

	step = test_thread_flag(TIF_SINGLESTEP);

	if (step || test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, step);
}
