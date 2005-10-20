/* ptrace.c: Sparc process tracing support.
 *
 * Copyright (C) 1996 David S. Miller (davem@caipfs.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 * Based upon code written by Ross Biro, Linus Torvalds, Bob Manson,
 * and David Mosberger.
 *
 * Added Linux support -miguel (weird, eh?, the original code was meant
 * to emulate SunOS).
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/security.h>
#include <linux/seccomp.h>
#include <linux/audit.h>
#include <linux/signal.h>

#include <asm/asi.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/psrcompat.h>
#include <asm/visasm.h>
#include <asm/spitfire.h>
#include <asm/page.h>
#include <asm/cpudata.h>

/* Returning from ptrace is a bit tricky because the syscall return
 * low level code assumes any value returned which is negative and
 * is a valid errno will mean setting the condition codes to indicate
 * an error return.  This doesn't work, so we have this hook.
 */
static inline void pt_error_return(struct pt_regs *regs, unsigned long error)
{
	regs->u_regs[UREG_I0] = error;
	regs->tstate |= (TSTATE_ICARRY | TSTATE_XCARRY);
	regs->tpc = regs->tnpc;
	regs->tnpc += 4;
}

static inline void pt_succ_return(struct pt_regs *regs, unsigned long value)
{
	regs->u_regs[UREG_I0] = value;
	regs->tstate &= ~(TSTATE_ICARRY | TSTATE_XCARRY);
	regs->tpc = regs->tnpc;
	regs->tnpc += 4;
}

static inline void
pt_succ_return_linux(struct pt_regs *regs, unsigned long value, void __user *addr)
{
	if (test_thread_flag(TIF_32BIT)) {
		if (put_user(value, (unsigned int __user *) addr)) {
			pt_error_return(regs, EFAULT);
			return;
		}
	} else {
		if (put_user(value, (long __user *) addr)) {
			pt_error_return(regs, EFAULT);
			return;
		}
	}
	regs->u_regs[UREG_I0] = 0;
	regs->tstate &= ~(TSTATE_ICARRY | TSTATE_XCARRY);
	regs->tpc = regs->tnpc;
	regs->tnpc += 4;
}

static void
pt_os_succ_return (struct pt_regs *regs, unsigned long val, void __user *addr)
{
	if (current->personality == PER_SUNOS)
		pt_succ_return (regs, val);
	else
		pt_succ_return_linux (regs, val, addr);
}

/* #define ALLOW_INIT_TRACING */
/* #define DEBUG_PTRACE */

#ifdef DEBUG_PTRACE
char *pt_rq [] = {
	/* 0  */ "TRACEME", "PEEKTEXT", "PEEKDATA", "PEEKUSR",
	/* 4  */ "POKETEXT", "POKEDATA", "POKEUSR", "CONT",
	/* 8  */ "KILL", "SINGLESTEP", "SUNATTACH", "SUNDETACH",
	/* 12 */ "GETREGS", "SETREGS", "GETFPREGS", "SETFPREGS",
	/* 16 */ "READDATA", "WRITEDATA", "READTEXT", "WRITETEXT",
	/* 20 */ "GETFPAREGS", "SETFPAREGS", "unknown", "unknown",
	/* 24 */ "SYSCALL", ""
};
#endif

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *child)
{
	/* nothing to do */
}

/* To get the necessary page struct, access_process_vm() first calls
 * get_user_pages().  This has done a flush_dcache_page() on the
 * accessed page.  Then our caller (copy_{to,from}_user_page()) did
 * to memcpy to read/write the data from that page.
 *
 * Now, the only thing we have to do is:
 * 1) flush the D-cache if it's possible than an illegal alias
 *    has been created
 * 2) flush the I-cache if this is pre-cheetah and we did a write
 */
void flush_ptrace_access(struct vm_area_struct *vma, struct page *page,
			 unsigned long uaddr, void *kaddr,
			 unsigned long len, int write)
{
	BUG_ON(len > PAGE_SIZE);

#ifdef DCACHE_ALIASING_POSSIBLE
	/* If bit 13 of the kernel address we used to access the
	 * user page is the same as the virtual address that page
	 * is mapped to in the user's address space, we can skip the
	 * D-cache flush.
	 */
	if ((uaddr ^ (unsigned long) kaddr) & (1UL << 13)) {
		unsigned long start = __pa(kaddr);
		unsigned long end = start + len;
		unsigned long dcache_line_size;

		dcache_line_size = local_cpu_data().dcache_line_size;

		if (tlb_type == spitfire) {
			for (; start < end; start += dcache_line_size)
				spitfire_put_dcache_tag(start & 0x3fe0, 0x0);
		} else {
			start &= ~(dcache_line_size - 1);
			for (; start < end; start += dcache_line_size)
				__asm__ __volatile__(
					"stxa %%g0, [%0] %1\n\t"
					"membar #Sync"
					: /* no outputs */
					: "r" (start),
					"i" (ASI_DCACHE_INVALIDATE));
		}
	}
#endif
	if (write && tlb_type == spitfire) {
		unsigned long start = (unsigned long) kaddr;
		unsigned long end = start + len;
		unsigned long icache_line_size;

		icache_line_size = local_cpu_data().icache_line_size;

		for (; start < end; start += icache_line_size)
			flushi(start);
	}
}

asmlinkage void do_ptrace(struct pt_regs *regs)
{
	int request = regs->u_regs[UREG_I0];
	pid_t pid = regs->u_regs[UREG_I1];
	unsigned long addr = regs->u_regs[UREG_I2];
	unsigned long data = regs->u_regs[UREG_I3];
	unsigned long addr2 = regs->u_regs[UREG_I4];
	struct task_struct *child;
	int ret;

	if (test_thread_flag(TIF_32BIT)) {
		addr &= 0xffffffffUL;
		data &= 0xffffffffUL;
		addr2 &= 0xffffffffUL;
	}
	lock_kernel();
#ifdef DEBUG_PTRACE
	{
		char *s;

		if ((request >= 0) && (request <= 24))
			s = pt_rq [request];
		else
			s = "unknown";

		if (request == PTRACE_POKEDATA && data == 0x91d02001){
			printk ("do_ptrace: breakpoint pid=%d, addr=%016lx addr2=%016lx\n",
				pid, addr, addr2);
		} else 
			printk("do_ptrace: rq=%s(%d) pid=%d addr=%016lx data=%016lx addr2=%016lx\n",
			       s, request, pid, addr, data, addr2);
	}
#endif
	if (request == PTRACE_TRACEME) {
		int ret;

		/* are we already being traced? */
		if (current->ptrace & PT_PTRACED) {
			pt_error_return(regs, EPERM);
			goto out;
		}
		ret = security_ptrace(current->parent, current);
		if (ret) {
			pt_error_return(regs, -ret);
			goto out;
		}

		/* set the ptrace bit in the process flags. */
		current->ptrace |= PT_PTRACED;
		pt_succ_return(regs, 0);
		goto out;
	}
#ifndef ALLOW_INIT_TRACING
	if (pid == 1) {
		/* Can't dork with init. */
		pt_error_return(regs, EPERM);
		goto out;
	}
#endif
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	if (child)
		get_task_struct(child);
	read_unlock(&tasklist_lock);

	if (!child) {
		pt_error_return(regs, ESRCH);
		goto out;
	}

	if ((current->personality == PER_SUNOS && request == PTRACE_SUNATTACH)
	    || (current->personality != PER_SUNOS && request == PTRACE_ATTACH)) {
		if (ptrace_attach(child)) {
			pt_error_return(regs, EPERM);
			goto out_tsk;
		}
		pt_succ_return(regs, 0);
		goto out_tsk;
	}

	ret = ptrace_check_attach(child, request == PTRACE_KILL);
	if (ret < 0) {
		pt_error_return(regs, -ret);
		goto out_tsk;
	}

	if (!(test_thread_flag(TIF_32BIT))	&&
	    ((request == PTRACE_READDATA64)		||
	     (request == PTRACE_WRITEDATA64)		||
	     (request == PTRACE_READTEXT64)		||
	     (request == PTRACE_WRITETEXT64)		||
	     (request == PTRACE_PEEKTEXT64)		||
	     (request == PTRACE_POKETEXT64)		||
	     (request == PTRACE_PEEKDATA64)		||
	     (request == PTRACE_POKEDATA64))) {
		addr = regs->u_regs[UREG_G2];
		addr2 = regs->u_regs[UREG_G3];
		request -= 30; /* wheee... */
	}

	switch(request) {
	case PTRACE_PEEKTEXT: /* read word at location addr. */ 
	case PTRACE_PEEKDATA: {
		unsigned long tmp64;
		unsigned int tmp32;
		int res, copied;

		res = -EIO;
		if (test_thread_flag(TIF_32BIT)) {
			copied = access_process_vm(child, addr,
						   &tmp32, sizeof(tmp32), 0);
			tmp64 = (unsigned long) tmp32;
			if (copied == sizeof(tmp32))
				res = 0;
		} else {
			copied = access_process_vm(child, addr,
						   &tmp64, sizeof(tmp64), 0);
			if (copied == sizeof(tmp64))
				res = 0;
		}
		if (res < 0)
			pt_error_return(regs, -res);
		else
			pt_os_succ_return(regs, tmp64, (void __user *) data);
		goto out_tsk;
	}

	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA: {
		unsigned long tmp64;
		unsigned int tmp32;
		int copied, res = -EIO;

		if (test_thread_flag(TIF_32BIT)) {
			tmp32 = data;
			copied = access_process_vm(child, addr,
						   &tmp32, sizeof(tmp32), 1);
			if (copied == sizeof(tmp32))
				res = 0;
		} else {
			tmp64 = data;
			copied = access_process_vm(child, addr,
						   &tmp64, sizeof(tmp64), 1);
			if (copied == sizeof(tmp64))
				res = 0;
		}
		if (res < 0)
			pt_error_return(regs, -res);
		else
			pt_succ_return(regs, res);
		goto out_tsk;
	}

	case PTRACE_GETREGS: {
		struct pt_regs32 __user *pregs =
			(struct pt_regs32 __user *) addr;
		struct pt_regs *cregs = child->thread_info->kregs;
		int rval;

		if (__put_user(tstate_to_psr(cregs->tstate), (&pregs->psr)) ||
		    __put_user(cregs->tpc, (&pregs->pc)) ||
		    __put_user(cregs->tnpc, (&pregs->npc)) ||
		    __put_user(cregs->y, (&pregs->y))) {
			pt_error_return(regs, EFAULT);
			goto out_tsk;
		}
		for (rval = 1; rval < 16; rval++)
			if (__put_user(cregs->u_regs[rval], (&pregs->u_regs[rval - 1]))) {
				pt_error_return(regs, EFAULT);
				goto out_tsk;
			}
		pt_succ_return(regs, 0);
#ifdef DEBUG_PTRACE
		printk ("PC=%lx nPC=%lx o7=%lx\n", cregs->tpc, cregs->tnpc, cregs->u_regs [15]);
#endif
		goto out_tsk;
	}

	case PTRACE_GETREGS64: {
		struct pt_regs __user *pregs = (struct pt_regs __user *) addr;
		struct pt_regs *cregs = child->thread_info->kregs;
		unsigned long tpc = cregs->tpc;
		int rval;

		if ((child->thread_info->flags & _TIF_32BIT) != 0)
			tpc &= 0xffffffff;
		if (__put_user(cregs->tstate, (&pregs->tstate)) ||
		    __put_user(tpc, (&pregs->tpc)) ||
		    __put_user(cregs->tnpc, (&pregs->tnpc)) ||
		    __put_user(cregs->y, (&pregs->y))) {
			pt_error_return(regs, EFAULT);
			goto out_tsk;
		}
		for (rval = 1; rval < 16; rval++)
			if (__put_user(cregs->u_regs[rval], (&pregs->u_regs[rval - 1]))) {
				pt_error_return(regs, EFAULT);
				goto out_tsk;
			}
		pt_succ_return(regs, 0);
#ifdef DEBUG_PTRACE
		printk ("PC=%lx nPC=%lx o7=%lx\n", cregs->tpc, cregs->tnpc, cregs->u_regs [15]);
#endif
		goto out_tsk;
	}

	case PTRACE_SETREGS: {
		struct pt_regs32 __user *pregs =
			(struct pt_regs32 __user *) addr;
		struct pt_regs *cregs = child->thread_info->kregs;
		unsigned int psr, pc, npc, y;
		int i;

		/* Must be careful, tracing process can only set certain
		 * bits in the psr.
		 */
		if (__get_user(psr, (&pregs->psr)) ||
		    __get_user(pc, (&pregs->pc)) ||
		    __get_user(npc, (&pregs->npc)) ||
		    __get_user(y, (&pregs->y))) {
			pt_error_return(regs, EFAULT);
			goto out_tsk;
		}
		cregs->tstate &= ~(TSTATE_ICC);
		cregs->tstate |= psr_to_tstate_icc(psr);
               	if (!((pc | npc) & 3)) {
			cregs->tpc = pc;
			cregs->tnpc = npc;
		}
		cregs->y = y;
		for (i = 1; i < 16; i++) {
			if (__get_user(cregs->u_regs[i], (&pregs->u_regs[i-1]))) {
				pt_error_return(regs, EFAULT);
				goto out_tsk;
			}
		}
		pt_succ_return(regs, 0);
		goto out_tsk;
	}

	case PTRACE_SETREGS64: {
		struct pt_regs __user *pregs = (struct pt_regs __user *) addr;
		struct pt_regs *cregs = child->thread_info->kregs;
		unsigned long tstate, tpc, tnpc, y;
		int i;

		/* Must be careful, tracing process can only set certain
		 * bits in the psr.
		 */
		if (__get_user(tstate, (&pregs->tstate)) ||
		    __get_user(tpc, (&pregs->tpc)) ||
		    __get_user(tnpc, (&pregs->tnpc)) ||
		    __get_user(y, (&pregs->y))) {
			pt_error_return(regs, EFAULT);
			goto out_tsk;
		}
		if ((child->thread_info->flags & _TIF_32BIT) != 0) {
			tpc &= 0xffffffff;
			tnpc &= 0xffffffff;
		}
		tstate &= (TSTATE_ICC | TSTATE_XCC);
		cregs->tstate &= ~(TSTATE_ICC | TSTATE_XCC);
		cregs->tstate |= tstate;
		if (!((tpc | tnpc) & 3)) {
			cregs->tpc = tpc;
			cregs->tnpc = tnpc;
		}
		cregs->y = y;
		for (i = 1; i < 16; i++) {
			if (__get_user(cregs->u_regs[i], (&pregs->u_regs[i-1]))) {
				pt_error_return(regs, EFAULT);
				goto out_tsk;
			}
		}
		pt_succ_return(regs, 0);
		goto out_tsk;
	}

	case PTRACE_GETFPREGS: {
		struct fps {
			unsigned int regs[32];
			unsigned int fsr;
			unsigned int flags;
			unsigned int extra;
			unsigned int fpqd;
			struct fq {
				unsigned int insnaddr;
				unsigned int insn;
			} fpq[16];
		};
		struct fps __user *fps = (struct fps __user *) addr;
		unsigned long *fpregs = child->thread_info->fpregs;

		if (copy_to_user(&fps->regs[0], fpregs,
				 (32 * sizeof(unsigned int))) ||
		    __put_user(child->thread_info->xfsr[0], (&fps->fsr)) ||
		    __put_user(0, (&fps->fpqd)) ||
		    __put_user(0, (&fps->flags)) ||
		    __put_user(0, (&fps->extra)) ||
		    clear_user(&fps->fpq[0], 32 * sizeof(unsigned int))) {
			pt_error_return(regs, EFAULT);
			goto out_tsk;
		}
		pt_succ_return(regs, 0);
		goto out_tsk;
	}

	case PTRACE_GETFPREGS64: {
		struct fps {
			unsigned int regs[64];
			unsigned long fsr;
		};
		struct fps __user *fps = (struct fps __user *) addr;
		unsigned long *fpregs = child->thread_info->fpregs;

		if (copy_to_user(&fps->regs[0], fpregs,
				 (64 * sizeof(unsigned int))) ||
		    __put_user(child->thread_info->xfsr[0], (&fps->fsr))) {
			pt_error_return(regs, EFAULT);
			goto out_tsk;
		}
		pt_succ_return(regs, 0);
		goto out_tsk;
	}

	case PTRACE_SETFPREGS: {
		struct fps {
			unsigned int regs[32];
			unsigned int fsr;
			unsigned int flags;
			unsigned int extra;
			unsigned int fpqd;
			struct fq {
				unsigned int insnaddr;
				unsigned int insn;
			} fpq[16];
		};
		struct fps __user *fps = (struct fps __user *) addr;
		unsigned long *fpregs = child->thread_info->fpregs;
		unsigned fsr;

		if (copy_from_user(fpregs, &fps->regs[0],
				   (32 * sizeof(unsigned int))) ||
		    __get_user(fsr, (&fps->fsr))) {
			pt_error_return(regs, EFAULT);
			goto out_tsk;
		}
		child->thread_info->xfsr[0] &= 0xffffffff00000000UL;
		child->thread_info->xfsr[0] |= fsr;
		if (!(child->thread_info->fpsaved[0] & FPRS_FEF))
			child->thread_info->gsr[0] = 0;
		child->thread_info->fpsaved[0] |= (FPRS_FEF | FPRS_DL);
		pt_succ_return(regs, 0);
		goto out_tsk;
	}

	case PTRACE_SETFPREGS64: {
		struct fps {
			unsigned int regs[64];
			unsigned long fsr;
		};
		struct fps __user *fps = (struct fps __user *) addr;
		unsigned long *fpregs = child->thread_info->fpregs;

		if (copy_from_user(fpregs, &fps->regs[0],
				   (64 * sizeof(unsigned int))) ||
		    __get_user(child->thread_info->xfsr[0], (&fps->fsr))) {
			pt_error_return(regs, EFAULT);
			goto out_tsk;
		}
		if (!(child->thread_info->fpsaved[0] & FPRS_FEF))
			child->thread_info->gsr[0] = 0;
		child->thread_info->fpsaved[0] |= (FPRS_FEF | FPRS_DL | FPRS_DU);
		pt_succ_return(regs, 0);
		goto out_tsk;
	}

	case PTRACE_READTEXT:
	case PTRACE_READDATA: {
		int res = ptrace_readdata(child, addr,
					  (char __user *)addr2, data);
		if (res == data) {
			pt_succ_return(regs, 0);
			goto out_tsk;
		}
		if (res >= 0)
			res = -EIO;
		pt_error_return(regs, -res);
		goto out_tsk;
	}

	case PTRACE_WRITETEXT:
	case PTRACE_WRITEDATA: {
		int res = ptrace_writedata(child, (char __user *) addr2,
					   addr, data);
		if (res == data) {
			pt_succ_return(regs, 0);
			goto out_tsk;
		}
		if (res >= 0)
			res = -EIO;
		pt_error_return(regs, -res);
		goto out_tsk;
	}
	case PTRACE_SYSCALL: /* continue and stop at (return from) syscall */
		addr = 1;

	case PTRACE_CONT: { /* restart after signal. */
		if (!valid_signal(data)) {
			pt_error_return(regs, EIO);
			goto out_tsk;
		}

		if (request == PTRACE_SYSCALL) {
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		} else {
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		}

		child->exit_code = data;
#ifdef DEBUG_PTRACE
		printk("CONT: %s [%d]: set exit_code = %x %lx %lx\n", child->comm,
			child->pid, child->exit_code,
			child->thread_info->kregs->tpc,
			child->thread_info->kregs->tnpc);
		       
#endif
		wake_up_process(child);
		pt_succ_return(regs, 0);
		goto out_tsk;
	}

/*
 * make the child exit.  Best I can do is send it a sigkill. 
 * perhaps it should be put in the status that it wants to 
 * exit.
 */
	case PTRACE_KILL: {
		if (child->exit_state == EXIT_ZOMBIE) {	/* already dead */
			pt_succ_return(regs, 0);
			goto out_tsk;
		}
		child->exit_code = SIGKILL;
		wake_up_process(child);
		pt_succ_return(regs, 0);
		goto out_tsk;
	}

	case PTRACE_SUNDETACH: { /* detach a process that was attached. */
		int error = ptrace_detach(child, data);
		if (error) {
			pt_error_return(regs, EIO);
			goto out_tsk;
		}
		pt_succ_return(regs, 0);
		goto out_tsk;
	}

	/* PTRACE_DUMPCORE unsupported... */

	default: {
		int err = ptrace_request(child, request, addr, data);
		if (err)
			pt_error_return(regs, -err);
		else
			pt_succ_return(regs, 0);
		goto out_tsk;
	}
	}
out_tsk:
	if (child)
		put_task_struct(child);
out:
	unlock_kernel();
}

asmlinkage void syscall_trace(struct pt_regs *regs, int syscall_exit_p)
{
	/* do the secure computing check first */
	secure_computing(regs->u_regs[UREG_G1]);

	if (unlikely(current->audit_context) && syscall_exit_p) {
		unsigned long tstate = regs->tstate;
		int result = AUDITSC_SUCCESS;

		if (unlikely(tstate & (TSTATE_XCARRY | TSTATE_ICARRY)))
			result = AUDITSC_FAILURE;

		audit_syscall_exit(current, result, regs->u_regs[UREG_I0]);
	}

	if (!(current->ptrace & PT_PTRACED))
		goto out;

	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		goto out;

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

out:
	if (unlikely(current->audit_context) && !syscall_exit_p)
		audit_syscall_entry(current,
				    (test_thread_flag(TIF_32BIT) ?
				     AUDIT_ARCH_SPARC :
				     AUDIT_ARCH_SPARC64),
				    regs->u_regs[UREG_G1],
				    regs->u_regs[UREG_I0],
				    regs->u_regs[UREG_I1],
				    regs->u_regs[UREG_I2],
				    regs->u_regs[UREG_I3]);
}
