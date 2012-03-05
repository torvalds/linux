// TODO some minor issues
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

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/smp.h>
#include <linux/security.h>
#include <linux/signal.h>

#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/ptrace.h>
#include <asm/elf.h>
#include <asm/coprocessor.h>


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

int ptrace_getregs(struct task_struct *child, void __user *uregs)
{
	struct pt_regs *regs = task_pt_regs(child);
	xtensa_gregset_t __user *gregset = uregs;
	unsigned long wm = regs->wmask;
	unsigned long wb = regs->windowbase;
	int live, i;

	if (!access_ok(VERIFY_WRITE, uregs, sizeof(xtensa_gregset_t)))
		return -EIO;

	__put_user(regs->pc, &gregset->pc);
	__put_user(regs->ps & ~(1 << PS_EXCM_BIT), &gregset->ps);
	__put_user(regs->lbeg, &gregset->lbeg);
	__put_user(regs->lend, &gregset->lend);
	__put_user(regs->lcount, &gregset->lcount);
	__put_user(regs->windowstart, &gregset->windowstart);
	__put_user(regs->windowbase, &gregset->windowbase);

	live = (wm & 2) ? 4 : (wm & 4) ? 8 : (wm & 8) ? 12 : 16;

	for (i = 0; i < live; i++)
		__put_user(regs->areg[i],gregset->a+((wb*4+i)%XCHAL_NUM_AREGS));
	for (i = XCHAL_NUM_AREGS - (wm >> 4) * 4; i < XCHAL_NUM_AREGS; i++)
		__put_user(regs->areg[i],gregset->a+((wb*4+i)%XCHAL_NUM_AREGS));

	return 0;
}

int ptrace_setregs(struct task_struct *child, void __user *uregs)
{
	struct pt_regs *regs = task_pt_regs(child);
	xtensa_gregset_t *gregset = uregs;
	const unsigned long ps_mask = PS_CALLINC_MASK | PS_OWB_MASK;
	unsigned long ps;
	unsigned long wb;

	if (!access_ok(VERIFY_WRITE, uregs, sizeof(xtensa_gregset_t)))
		return -EIO;

	__get_user(regs->pc, &gregset->pc);
	__get_user(ps, &gregset->ps);
	__get_user(regs->lbeg, &gregset->lbeg);
	__get_user(regs->lend, &gregset->lend);
	__get_user(regs->lcount, &gregset->lcount);
	__get_user(regs->windowstart, &gregset->windowstart);
	__get_user(wb, &gregset->windowbase);

	regs->ps = (regs->ps & ~ps_mask) | (ps & ps_mask) | (1 << PS_EXCM_BIT);

	if (wb >= XCHAL_NUM_AREGS / 4)
		return -EFAULT;

	regs->windowbase = wb;

	if (wb != 0 &&  __copy_from_user(regs->areg + XCHAL_NUM_AREGS - wb * 4,
					 gregset->a, wb * 16))
		return -EFAULT;

	if (__copy_from_user(regs->areg, gregset->a + wb*4, (WSBITS-wb) * 16))
		return -EFAULT;

	return 0;
}


int ptrace_getxregs(struct task_struct *child, void __user *uregs)
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

int ptrace_setxregs(struct task_struct *child, void __user *uregs)
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

int ptrace_peekusr(struct task_struct *child, long regno, long __user *ret)
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
			/* Note:  PS.EXCM is not set while user task is running;
			 * its being set in regs is for exception handling
			 * convenience.  */
			tmp = (regs->ps & ~(1 << PS_EXCM_BIT));
			break;

		case REG_WB:
			break;		/* tmp = 0 */

		case REG_WS:
		{
			unsigned long wb = regs->windowbase;
			unsigned long ws = regs->windowstart;
			tmp = ((ws>>wb) | (ws<<(WSBITS-wb))) & ((1<<WSBITS)-1);
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

int ptrace_pokeusr(struct task_struct *child, long regno, long val)
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

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

void do_syscall_trace(void)
{
	/*
	 * The 0x80 provides a way for the tracing parent to distinguish
	 * between a syscall stop and SIGTRAP delivery
	 */
	ptrace_notify(SIGTRAP|((current->ptrace & PT_TRACESYSGOOD) ? 0x80 : 0));

	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}

void do_syscall_trace_enter(struct pt_regs *regs)
{
	if (test_thread_flag(TIF_SYSCALL_TRACE)
			&& (current->ptrace & PT_PTRACED))
		do_syscall_trace();

#if 0
	audit_syscall_entry(current, AUDIT_ARCH_XTENSA..);
#endif
}

void do_syscall_trace_leave(struct pt_regs *regs)
{
	if ((test_thread_flag(TIF_SYSCALL_TRACE))
			&& (current->ptrace & PT_PTRACED))
		do_syscall_trace();
}

