/* ptrace.c: Sparc process tracing support.
 *
 * Copyright (C) 1996 David S. Miller (davem@caipfs.rutgers.edu)
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
#include <linux/signal.h>

#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#define MAGIC_CONSTANT 0x80000000


/* Returning from ptrace is a bit tricky because the syscall return
 * low level code assumes any value returned which is negative and
 * is a valid errno will mean setting the condition codes to indicate
 * an error return.  This doesn't work, so we have this hook.
 */
static inline void pt_error_return(struct pt_regs *regs, unsigned long error)
{
	regs->u_regs[UREG_I0] = error;
	regs->psr |= PSR_C;
	regs->pc = regs->npc;
	regs->npc += 4;
}

static inline void pt_succ_return(struct pt_regs *regs, unsigned long value)
{
	regs->u_regs[UREG_I0] = value;
	regs->psr &= ~PSR_C;
	regs->pc = regs->npc;
	regs->npc += 4;
}

static void
pt_succ_return_linux(struct pt_regs *regs, unsigned long value, long __user *addr)
{
	if (put_user(value, addr)) {
		pt_error_return(regs, EFAULT);
		return;
	}
	regs->u_regs[UREG_I0] = 0;
	regs->psr &= ~PSR_C;
	regs->pc = regs->npc;
	regs->npc += 4;
}

static void
pt_os_succ_return (struct pt_regs *regs, unsigned long val, long __user *addr)
{
	if (current->personality == PER_SUNOS)
		pt_succ_return (regs, val);
	else
		pt_succ_return_linux (regs, val, addr);
}

/* Fuck me gently with a chainsaw... */
static inline void read_sunos_user(struct pt_regs *regs, unsigned long offset,
				   struct task_struct *tsk, long __user *addr)
{
	struct pt_regs *cregs = tsk->thread.kregs;
	struct thread_info *t = task_thread_info(tsk);
	int v;
	
	if(offset >= 1024)
		offset -= 1024; /* whee... */
	if(offset & ((sizeof(unsigned long) - 1))) {
		pt_error_return(regs, EIO);
		return;
	}
	if(offset >= 16 && offset < 784) {
		offset -= 16; offset >>= 2;
		pt_os_succ_return(regs, *(((unsigned long *)(&t->reg_window[0]))+offset), addr);
		return;
	}
	if(offset >= 784 && offset < 832) {
		offset -= 784; offset >>= 2;
		pt_os_succ_return(regs, *(((unsigned long *)(&t->rwbuf_stkptrs[0]))+offset), addr);
		return;
	}
	switch(offset) {
	case 0:
		v = t->ksp;
		break;
	case 4:
		v = t->kpc;
		break;
	case 8:
		v = t->kpsr;
		break;
	case 12:
		v = t->uwinmask;
		break;
	case 832:
		v = t->w_saved;
		break;
	case 896:
		v = cregs->u_regs[UREG_I0];
		break;
	case 900:
		v = cregs->u_regs[UREG_I1];
		break;
	case 904:
		v = cregs->u_regs[UREG_I2];
		break;
	case 908:
		v = cregs->u_regs[UREG_I3];
		break;
	case 912:
		v = cregs->u_regs[UREG_I4];
		break;
	case 916:
		v = cregs->u_regs[UREG_I5];
		break;
	case 920:
		v = cregs->u_regs[UREG_I6];
		break;
	case 924:
		if(tsk->thread.flags & MAGIC_CONSTANT)
			v = cregs->u_regs[UREG_G1];
		else
			v = 0;
		break;
	case 940:
		v = cregs->u_regs[UREG_I0];
		break;
	case 944:
		v = cregs->u_regs[UREG_I1];
		break;

	case 948:
		/* Isn't binary compatibility _fun_??? */
		if(cregs->psr & PSR_C)
			v = cregs->u_regs[UREG_I0] << 24;
		else
			v = 0;
		break;

		/* Rest of them are completely unsupported. */
	default:
		printk("%s [%d]: Wants to read user offset %ld\n",
		       current->comm, task_pid_nr(current), offset);
		pt_error_return(regs, EIO);
		return;
	}
	if (current->personality == PER_SUNOS)
		pt_succ_return (regs, v);
	else
		pt_succ_return_linux (regs, v, addr);
	return;
}

static inline void write_sunos_user(struct pt_regs *regs, unsigned long offset,
				    struct task_struct *tsk)
{
	struct pt_regs *cregs = tsk->thread.kregs;
	struct thread_info *t = task_thread_info(tsk);
	unsigned long value = regs->u_regs[UREG_I3];

	if(offset >= 1024)
		offset -= 1024; /* whee... */
	if(offset & ((sizeof(unsigned long) - 1)))
		goto failure;
	if(offset >= 16 && offset < 784) {
		offset -= 16; offset >>= 2;
		*(((unsigned long *)(&t->reg_window[0]))+offset) = value;
		goto success;
	}
	if(offset >= 784 && offset < 832) {
		offset -= 784; offset >>= 2;
		*(((unsigned long *)(&t->rwbuf_stkptrs[0]))+offset) = value;
		goto success;
	}
	switch(offset) {
	case 896:
		cregs->u_regs[UREG_I0] = value;
		break;
	case 900:
		cregs->u_regs[UREG_I1] = value;
		break;
	case 904:
		cregs->u_regs[UREG_I2] = value;
		break;
	case 908:
		cregs->u_regs[UREG_I3] = value;
		break;
	case 912:
		cregs->u_regs[UREG_I4] = value;
		break;
	case 916:
		cregs->u_regs[UREG_I5] = value;
		break;
	case 920:
		cregs->u_regs[UREG_I6] = value;
		break;
	case 924:
		cregs->u_regs[UREG_I7] = value;
		break;
	case 940:
		cregs->u_regs[UREG_I0] = value;
		break;
	case 944:
		cregs->u_regs[UREG_I1] = value;
		break;

		/* Rest of them are completely unsupported or "no-touch". */
	default:
		printk("%s [%d]: Wants to write user offset %ld\n",
		       current->comm, task_pid_nr(current), offset);
		goto failure;
	}
success:
	pt_succ_return(regs, 0);
	return;
failure:
	pt_error_return(regs, EIO);
	return;
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

asmlinkage void do_ptrace(struct pt_regs *regs)
{
	unsigned long request = regs->u_regs[UREG_I0];
	unsigned long pid = regs->u_regs[UREG_I1];
	unsigned long addr = regs->u_regs[UREG_I2];
	unsigned long data = regs->u_regs[UREG_I3];
	unsigned long addr2 = regs->u_regs[UREG_I4];
	struct task_struct *child;
	int ret;

	lock_kernel();
#ifdef DEBUG_PTRACE
	{
		char *s;

		if ((request >= 0) && (request <= 24))
			s = pt_rq [request];
		else
			s = "unknown";

		if (request == PTRACE_POKEDATA && data == 0x91d02001){
			printk ("do_ptrace: breakpoint pid=%d, addr=%08lx addr2=%08lx\n",
				pid, addr, addr2);
		} else 
			printk("do_ptrace: rq=%s(%d) pid=%d addr=%08lx data=%08lx addr2=%08lx\n",
			       s, (int) request, (int) pid, addr, data, addr2);
	}
#endif

	if (request == PTRACE_TRACEME) {
		ret = ptrace_traceme();
		if (ret < 0)
			pt_error_return(regs, -ret);
		else
			pt_succ_return(regs, 0);
		goto out;
	}

	child = ptrace_get_task_struct(pid);
	if (IS_ERR(child)) {
		ret = PTR_ERR(child);
		pt_error_return(regs, -ret);
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

	switch(request) {
	case PTRACE_PEEKTEXT: /* read word at location addr. */ 
	case PTRACE_PEEKDATA: {
		unsigned long tmp;

		if (access_process_vm(child, addr,
				      &tmp, sizeof(tmp), 0) == sizeof(tmp))
			pt_os_succ_return(regs, tmp, (long __user *)data);
		else
			pt_error_return(regs, EIO);
		goto out_tsk;
	}

	case PTRACE_PEEKUSR:
		read_sunos_user(regs, addr, child, (long __user *) data);
		goto out_tsk;

	case PTRACE_POKEUSR:
		write_sunos_user(regs, addr, child);
		goto out_tsk;

	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA: {
		if (access_process_vm(child, addr,
				      &data, sizeof(data), 1) == sizeof(data))
			pt_succ_return(regs, 0);
		else
			pt_error_return(regs, EIO);
		goto out_tsk;
	}

	case PTRACE_GETREGS: {
		struct pt_regs __user *pregs = (struct pt_regs __user *) addr;
		struct pt_regs *cregs = child->thread.kregs;
		int rval;

		if (!access_ok(VERIFY_WRITE, pregs, sizeof(struct pt_regs))) {
			rval = -EFAULT;
			pt_error_return(regs, -rval);
			goto out_tsk;
		}
		__put_user(cregs->psr, (&pregs->psr));
		__put_user(cregs->pc, (&pregs->pc));
		__put_user(cregs->npc, (&pregs->npc));
		__put_user(cregs->y, (&pregs->y));
		for(rval = 1; rval < 16; rval++)
			__put_user(cregs->u_regs[rval], (&pregs->u_regs[rval - 1]));
		pt_succ_return(regs, 0);
#ifdef DEBUG_PTRACE
		printk ("PC=%x nPC=%x o7=%x\n", cregs->pc, cregs->npc, cregs->u_regs [15]);
#endif
		goto out_tsk;
	}

	case PTRACE_SETREGS: {
		struct pt_regs __user *pregs = (struct pt_regs __user *) addr;
		struct pt_regs *cregs = child->thread.kregs;
		unsigned long psr, pc, npc, y;
		int i;

		/* Must be careful, tracing process can only set certain
		 * bits in the psr.
		 */
		if (!access_ok(VERIFY_READ, pregs, sizeof(struct pt_regs))) {
			pt_error_return(regs, EFAULT);
			goto out_tsk;
		}
		__get_user(psr, (&pregs->psr));
		__get_user(pc, (&pregs->pc));
		__get_user(npc, (&pregs->npc));
		__get_user(y, (&pregs->y));
		psr &= PSR_ICC;
		cregs->psr &= ~PSR_ICC;
		cregs->psr |= psr;
		if (!((pc | npc) & 3)) {
			cregs->pc = pc;
			cregs->npc =npc;
		}
		cregs->y = y;
		for(i = 1; i < 16; i++)
			__get_user(cregs->u_regs[i], (&pregs->u_regs[i-1]));
		pt_succ_return(regs, 0);
		goto out_tsk;
	}

	case PTRACE_GETFPREGS: {
		struct fps {
			unsigned long regs[32];
			unsigned long fsr;
			unsigned long flags;
			unsigned long extra;
			unsigned long fpqd;
			struct fq {
				unsigned long *insnaddr;
				unsigned long insn;
			} fpq[16];
		};
		struct fps __user *fps = (struct fps __user *) addr;
		int i;

		if (!access_ok(VERIFY_WRITE, fps, sizeof(struct fps))) {
			i = -EFAULT;
			pt_error_return(regs, -i);
			goto out_tsk;
		}
		for(i = 0; i < 32; i++)
			__put_user(child->thread.float_regs[i], (&fps->regs[i]));
		__put_user(child->thread.fsr, (&fps->fsr));
		__put_user(child->thread.fpqdepth, (&fps->fpqd));
		__put_user(0, (&fps->flags));
		__put_user(0, (&fps->extra));
		for(i = 0; i < 16; i++) {
			__put_user(child->thread.fpqueue[i].insn_addr,
				   (&fps->fpq[i].insnaddr));
			__put_user(child->thread.fpqueue[i].insn, (&fps->fpq[i].insn));
		}
		pt_succ_return(regs, 0);
		goto out_tsk;
	}

	case PTRACE_SETFPREGS: {
		struct fps {
			unsigned long regs[32];
			unsigned long fsr;
			unsigned long flags;
			unsigned long extra;
			unsigned long fpqd;
			struct fq {
				unsigned long *insnaddr;
				unsigned long insn;
			} fpq[16];
		};
		struct fps __user *fps = (struct fps __user *) addr;
		int i;

		if (!access_ok(VERIFY_READ, fps, sizeof(struct fps))) {
			i = -EFAULT;
			pt_error_return(regs, -i);
			goto out_tsk;
		}
		copy_from_user(&child->thread.float_regs[0], &fps->regs[0], (32 * sizeof(unsigned long)));
		__get_user(child->thread.fsr, (&fps->fsr));
		__get_user(child->thread.fpqdepth, (&fps->fpqd));
		for(i = 0; i < 16; i++) {
			__get_user(child->thread.fpqueue[i].insn_addr,
				   (&fps->fpq[i].insnaddr));
			__get_user(child->thread.fpqueue[i].insn, (&fps->fpq[i].insn));
		}
		pt_succ_return(regs, 0);
		goto out_tsk;
	}

	case PTRACE_READTEXT:
	case PTRACE_READDATA: {
		int res = ptrace_readdata(child, addr,
					  (void __user *) addr2, data);

		if (res == data) {
			pt_succ_return(regs, 0);
			goto out_tsk;
		}
		/* Partial read is an IO failure */
		if (res >= 0)
			res = -EIO;
		pt_error_return(regs, -res);
		goto out_tsk;
	}

	case PTRACE_WRITETEXT:
	case PTRACE_WRITEDATA: {
		int res = ptrace_writedata(child, (void __user *) addr2,
					   addr, data);

		if (res == data) {
			pt_succ_return(regs, 0);
			goto out_tsk;
		}
		/* Partial write is an IO failure */
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

		if (request == PTRACE_SYSCALL)
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		else
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);

		child->exit_code = data;
#ifdef DEBUG_PTRACE
		printk("CONT: %s [%d]: set exit_code = %x %lx %lx\n",
			child->comm, child->pid, child->exit_code,
			child->thread.kregs->pc,
			child->thread.kregs->npc);
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
		wake_up_process(child);
		child->exit_code = SIGKILL;
		pt_succ_return(regs, 0);
		goto out_tsk;
	}

	case PTRACE_SUNDETACH: { /* detach a process that was attached. */
		int err = ptrace_detach(child, data);
		if (err) {
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

asmlinkage void syscall_trace(void)
{
#ifdef DEBUG_PTRACE
	printk("%s [%d]: syscall_trace\n", current->comm, current->pid);
#endif
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;
	if (!(current->ptrace & PT_PTRACED))
		return;
	current->thread.flags ^= MAGIC_CONSTANT;
	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
				 ? 0x80 : 0));
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
#ifdef DEBUG_PTRACE
	printk("%s [%d]: syscall_trace exit= %x\n", current->comm,
		current->pid, current->exit_code);
#endif
	if (current->exit_code) {
		send_sig (current->exit_code, current, 1);
		current->exit_code = 0;
	}
}
