/*
 * Kernel support for the ptrace() and syscall tracing interfaces.
 *
 * Copyright (C) 2000 Hewlett-Packard Co, Linuxcare Inc.
 * Copyright (C) 2000 Matthew Wilcox <matthew@wil.cx>
 * Copyright (C) 2000 David Huggins-Daines <dhd@debian.org>
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/personality.h>
#include <linux/security.h>
#include <linux/compat.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/offsets.h>

/* PSW bits we allow the debugger to modify */
#define USER_PSW_BITS	(PSW_N | PSW_V | PSW_CB)

#undef DEBUG_PTRACE

#ifdef DEBUG_PTRACE
#define DBG(x...)	printk(x)
#else
#define DBG(x...)
#endif

#ifdef __LP64__

/* This function is needed to translate 32 bit pt_regs offsets in to
 * 64 bit pt_regs offsets.  For example, a 32 bit gdb under a 64 bit kernel
 * will request offset 12 if it wants gr3, but the lower 32 bits of
 * the 64 bit kernels view of gr3 will be at offset 28 (3*8 + 4).
 * This code relies on a 32 bit pt_regs being comprised of 32 bit values
 * except for the fp registers which (a) are 64 bits, and (b) follow
 * the gr registers at the start of pt_regs.  The 32 bit pt_regs should
 * be half the size of the 64 bit pt_regs, plus 32*4 to allow for fr[]
 * being 64 bit in both cases.
 */

static long translate_usr_offset(long offset)
{
	if (offset < 0)
		return -1;
	else if (offset <= 32*4)	/* gr[0..31] */
		return offset * 2 + 4;
	else if (offset <= 32*4+32*8)	/* gr[0..31] + fr[0..31] */
		return offset + 32*4;
	else if (offset < sizeof(struct pt_regs)/2 + 32*4)
		return offset * 2 + 4 - 32*8;
	else
		return -1;
}
#endif

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *child)
{
	/* make sure the trap bits are not set */
	pa_psw(child)->r = 0;
	pa_psw(child)->t = 0;
	pa_psw(child)->h = 0;
	pa_psw(child)->l = 0;
}

long sys_ptrace(long request, pid_t pid, long addr, long data)
{
	struct task_struct *child;
	long ret;
#ifdef DEBUG_PTRACE
	long oaddr=addr, odata=data;
#endif

	lock_kernel();
	ret = -EPERM;
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->ptrace & PT_PTRACED)
			goto out;

		ret = security_ptrace(current->parent, current);
		if (ret) 
			goto out;

		/* set the ptrace bit in the process flags. */
		current->ptrace |= PT_PTRACED;
		ret = 0;
		goto out;
	}

	ret = -ESRCH;
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	if (child)
		get_task_struct(child);
	read_unlock(&tasklist_lock);
	if (!child)
		goto out;
	ret = -EPERM;
	if (pid == 1)		/* no messing around with init! */
		goto out_tsk;

	if (request == PTRACE_ATTACH) {
		ret = ptrace_attach(child);
		goto out_tsk;
	}

	ret = ptrace_check_attach(child, request == PTRACE_KILL);
	if (ret < 0)
		goto out_tsk;

	switch (request) {
	case PTRACE_PEEKTEXT: /* read word at location addr. */ 
	case PTRACE_PEEKDATA: {
		int copied;

#ifdef __LP64__
		if (is_compat_task(child)) {
			unsigned int tmp;

			addr &= 0xffffffffL;
			copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
			ret = -EIO;
			if (copied != sizeof(tmp))
				goto out_tsk;
			ret = put_user(tmp,(unsigned int *) data);
			DBG("sys_ptrace(PEEK%s, %d, %lx, %lx) returning %ld, data %x\n",
				request == PTRACE_PEEKTEXT ? "TEXT" : "DATA",
				pid, oaddr, odata, ret, tmp);
		}
		else
#endif
		{
			unsigned long tmp;

			copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
			ret = -EIO;
			if (copied != sizeof(tmp))
				goto out_tsk;
			ret = put_user(tmp,(unsigned long *) data);
		}
		goto out_tsk;
	}

	/* when I and D space are separate, this will have to be fixed. */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = 0;
#ifdef __LP64__
		if (is_compat_task(child)) {
			unsigned int tmp = (unsigned int)data;
			DBG("sys_ptrace(POKE%s, %d, %lx, %lx)\n",
				request == PTRACE_POKETEXT ? "TEXT" : "DATA",
				pid, oaddr, odata);
			addr &= 0xffffffffL;
			if (access_process_vm(child, addr, &tmp, sizeof(tmp), 1) == sizeof(tmp))
				goto out_tsk;
		}
		else
#endif
		{
			if (access_process_vm(child, addr, &data, sizeof(data), 1) == sizeof(data))
				goto out_tsk;
		}
		ret = -EIO;
		goto out_tsk;

	/* Read the word at location addr in the USER area.  For ptraced
	   processes, the kernel saves all regs on a syscall. */
	case PTRACE_PEEKUSR: {
		ret = -EIO;
#ifdef __LP64__
		if (is_compat_task(child)) {
			unsigned int tmp;

			if (addr & (sizeof(int)-1))
				goto out_tsk;
			if ((addr = translate_usr_offset(addr)) < 0)
				goto out_tsk;

			tmp = *(unsigned int *) ((char *) task_regs(child) + addr);
			ret = put_user(tmp, (unsigned int *) data);
			DBG("sys_ptrace(PEEKUSR, %d, %lx, %lx) returning %ld, addr %lx, data %x\n",
				pid, oaddr, odata, ret, addr, tmp);
		}
		else
#endif
		{
			unsigned long tmp;

			if ((addr & (sizeof(long)-1)) || (unsigned long) addr >= sizeof(struct pt_regs))
				goto out_tsk;
			tmp = *(unsigned long *) ((char *) task_regs(child) + addr);
			ret = put_user(tmp, (unsigned long *) data);
		}
		goto out_tsk;
	}

	/* Write the word at location addr in the USER area.  This will need
	   to change when the kernel no longer saves all regs on a syscall.
	   FIXME.  There is a problem at the moment in that r3-r18 are only
	   saved if the process is ptraced on syscall entry, and even then
	   those values are overwritten by actual register values on syscall
	   exit. */
	case PTRACE_POKEUSR:
		ret = -EIO;
		/* Some register values written here may be ignored in
		 * entry.S:syscall_restore_rfi; e.g. iaoq is written with
		 * r31/r31+4, and not with the values in pt_regs.
		 */
		 /* PT_PSW=0, so this is valid for 32 bit processes under 64
		 * bit kernels.
		 */
		if (addr == PT_PSW) {
			/* PT_PSW=0, so this is valid for 32 bit processes
			 * under 64 bit kernels.
			 *
			 * Allow writing to Nullify, Divide-step-correction,
			 * and carry/borrow bits.
			 * BEWARE, if you set N, and then single step, it won't
			 * stop on the nullified instruction.
			 */
			DBG("sys_ptrace(POKEUSR, %d, %lx, %lx)\n",
				pid, oaddr, odata);
			data &= USER_PSW_BITS;
			task_regs(child)->gr[0] &= ~USER_PSW_BITS;
			task_regs(child)->gr[0] |= data;
			ret = 0;
			goto out_tsk;
		}
#ifdef __LP64__
		if (is_compat_task(child)) {
			if (addr & (sizeof(int)-1))
				goto out_tsk;
			if ((addr = translate_usr_offset(addr)) < 0)
				goto out_tsk;
			DBG("sys_ptrace(POKEUSR, %d, %lx, %lx) addr %lx\n",
				pid, oaddr, odata, addr);
			if (addr >= PT_FR0 && addr <= PT_FR31 + 4) {
				/* Special case, fp regs are 64 bits anyway */
				*(unsigned int *) ((char *) task_regs(child) + addr) = data;
				ret = 0;
			}
			else if ((addr >= PT_GR1+4 && addr <= PT_GR31+4) ||
					addr == PT_IAOQ0+4 || addr == PT_IAOQ1+4 ||
					addr == PT_SAR+4) {
				/* Zero the top 32 bits */
				*(unsigned int *) ((char *) task_regs(child) + addr - 4) = 0;
				*(unsigned int *) ((char *) task_regs(child) + addr) = data;
				ret = 0;
			}
			goto out_tsk;
		}
		else
#endif
		{
			if ((addr & (sizeof(long)-1)) || (unsigned long) addr >= sizeof(struct pt_regs))
				goto out_tsk;
			if ((addr >= PT_GR1 && addr <= PT_GR31) ||
					addr == PT_IAOQ0 || addr == PT_IAOQ1 ||
					(addr >= PT_FR0 && addr <= PT_FR31 + 4) ||
					addr == PT_SAR) {
				*(unsigned long *) ((char *) task_regs(child) + addr) = data;
				ret = 0;
			}
			goto out_tsk;
		}

	case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
	case PTRACE_CONT:
		ret = -EIO;
		DBG("sys_ptrace(%s)\n",
			request == PTRACE_SYSCALL ? "SYSCALL" : "CONT");
		if ((unsigned long) data > _NSIG)
			goto out_tsk;
		child->ptrace &= ~(PT_SINGLESTEP|PT_BLOCKSTEP);
		if (request == PTRACE_SYSCALL) {
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		} else {
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		}		
		child->exit_code = data;
		goto out_wake_notrap;

	case PTRACE_KILL:
		/*
		 * make the child exit.  Best I can do is send it a
		 * sigkill.  perhaps it should be put in the status
		 * that it wants to exit.
		 */
		DBG("sys_ptrace(KILL)\n");
		if (child->exit_state == EXIT_ZOMBIE)	/* already dead */
			goto out_tsk;
		child->exit_code = SIGKILL;
		goto out_wake_notrap;

	case PTRACE_SINGLEBLOCK:
		DBG("sys_ptrace(SINGLEBLOCK)\n");
		ret = -EIO;
		if ((unsigned long) data > _NSIG)
			goto out_tsk;
		clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		child->ptrace &= ~PT_SINGLESTEP;
		child->ptrace |= PT_BLOCKSTEP;
		child->exit_code = data;

		/* Enable taken branch trap. */
		pa_psw(child)->r = 0;
		pa_psw(child)->t = 1;
		pa_psw(child)->h = 0;
		pa_psw(child)->l = 0;
		goto out_wake;

	case PTRACE_SINGLESTEP:
		DBG("sys_ptrace(SINGLESTEP)\n");
		ret = -EIO;
		if ((unsigned long) data > _NSIG)
			goto out_tsk;

		clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		child->ptrace &= ~PT_BLOCKSTEP;
		child->ptrace |= PT_SINGLESTEP;
		child->exit_code = data;

		if (pa_psw(child)->n) {
			struct siginfo si;

			/* Nullified, just crank over the queue. */
			task_regs(child)->iaoq[0] = task_regs(child)->iaoq[1];
			task_regs(child)->iasq[0] = task_regs(child)->iasq[1];
			task_regs(child)->iaoq[1] = task_regs(child)->iaoq[0] + 4;
			pa_psw(child)->n = 0;
			pa_psw(child)->x = 0;
			pa_psw(child)->y = 0;
			pa_psw(child)->z = 0;
			pa_psw(child)->b = 0;
			ptrace_disable(child);
			/* Don't wake up the child, but let the
			   parent know something happened. */
			si.si_code = TRAP_TRACE;
			si.si_addr = (void __user *) (task_regs(child)->iaoq[0] & ~3);
			si.si_signo = SIGTRAP;
			si.si_errno = 0;
			force_sig_info(SIGTRAP, &si, child);
			//notify_parent(child, SIGCHLD);
			//ret = 0;
			goto out_wake;
		}

		/* Enable recovery counter traps.  The recovery counter
		 * itself will be set to zero on a task switch.  If the
		 * task is suspended on a syscall then the syscall return
		 * path will overwrite the recovery counter with a suitable
		 * value such that it traps once back in user space.  We
		 * disable interrupts in the childs PSW here also, to avoid
		 * interrupts while the recovery counter is decrementing.
		 */
		pa_psw(child)->r = 1;
		pa_psw(child)->t = 0;
		pa_psw(child)->h = 0;
		pa_psw(child)->l = 0;
		/* give it a chance to run. */
		goto out_wake;

	case PTRACE_DETACH:
		ret = ptrace_detach(child, data);
		goto out_tsk;

	case PTRACE_GETEVENTMSG:
                ret = put_user(child->ptrace_message, (unsigned int __user *) data);
		goto out_tsk;

	default:
		ret = ptrace_request(child, request, addr, data);
		goto out_tsk;
	}

out_wake_notrap:
	ptrace_disable(child);
out_wake:
	wake_up_process(child);
	ret = 0;
out_tsk:
	put_task_struct(child);
out:
	unlock_kernel();
	DBG("sys_ptrace(%ld, %d, %lx, %lx) returning %ld\n",
		request, pid, oaddr, odata, ret);
	return ret;
}

void syscall_trace(void)
{
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;
	if (!(current->ptrace & PT_PTRACED))
		return;
	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
				 ? 0x80 : 0));
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
