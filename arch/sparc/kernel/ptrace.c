/* ptrace.c: Sparc process tracing support.
 *
 * Copyright (C) 1996, 2008 David S. Miller (davem@davemloft.net)
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
#include <linux/regset.h>
#include <linux/elf.h>

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

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *child)
{
	/* nothing to do */
}

enum sparc_regset {
	REGSET_GENERAL,
	REGSET_FP,
};

static int genregs32_get(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 void *kbuf, void __user *ubuf)
{
	const struct pt_regs *regs = target->thread.kregs;
	unsigned long __user *reg_window;
	unsigned long *k = kbuf;
	unsigned long __user *u = ubuf;
	unsigned long reg;

	if (target == current)
		flush_user_windows();

	pos /= sizeof(reg);
	count /= sizeof(reg);

	if (kbuf) {
		for (; count > 0 && pos < 16; count--)
			*k++ = regs->u_regs[pos++];

		reg_window = (unsigned long __user *) regs->u_regs[UREG_I6];
		for (; count > 0 && pos < 32; count--) {
			if (get_user(*k++, &reg_window[pos++]))
				return -EFAULT;
		}
	} else {
		for (; count > 0 && pos < 16; count--) {
			if (put_user(regs->u_regs[pos++], u++))
				return -EFAULT;
		}

		reg_window = (unsigned long __user *) regs->u_regs[UREG_I6];
		for (; count > 0 && pos < 32; count--) {
			if (get_user(reg, &reg_window[pos++]) ||
			    put_user(reg, u++))
				return -EFAULT;
		}
	}
	while (count > 0) {
		switch (pos) {
		case 32: /* PSR */
			reg = regs->psr;
			break;
		case 33: /* PC */
			reg = regs->pc;
			break;
		case 34: /* NPC */
			reg = regs->npc;
			break;
		case 35: /* Y */
			reg = regs->y;
			break;
		case 36: /* WIM */
		case 37: /* TBR */
			reg = 0;
			break;
		default:
			goto finish;
		}

		if (kbuf)
			*k++ = reg;
		else if (put_user(reg, u++))
			return -EFAULT;
		pos++;
		count--;
	}
finish:
	pos *= sizeof(reg);
	count *= sizeof(reg);

	return user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					38 * sizeof(reg), -1);
}

static int genregs32_set(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = target->thread.kregs;
	unsigned long __user *reg_window;
	const unsigned long *k = kbuf;
	const unsigned long __user *u = ubuf;
	unsigned long reg;

	if (target == current)
		flush_user_windows();

	pos /= sizeof(reg);
	count /= sizeof(reg);

	if (kbuf) {
		for (; count > 0 && pos < 16; count--)
			regs->u_regs[pos++] = *k++;

		reg_window = (unsigned long __user *) regs->u_regs[UREG_I6];
		for (; count > 0 && pos < 32; count--) {
			if (put_user(*k++, &reg_window[pos++]))
				return -EFAULT;
		}
	} else {
		for (; count > 0 && pos < 16; count--) {
			if (get_user(reg, u++))
				return -EFAULT;
			regs->u_regs[pos++] = reg;
		}

		reg_window = (unsigned long __user *) regs->u_regs[UREG_I6];
		for (; count > 0 && pos < 32; count--) {
			if (get_user(reg, u++) ||
			    put_user(reg, &reg_window[pos++]))
				return -EFAULT;
		}
	}
	while (count > 0) {
		unsigned long psr;

		if (kbuf)
			reg = *k++;
		else if (get_user(reg, u++))
			return -EFAULT;

		switch (pos) {
		case 32: /* PSR */
			psr = regs->psr;
			psr &= ~PSR_ICC;
			psr |= (reg & PSR_ICC);
			regs->psr = psr;
			break;
		case 33: /* PC */
			regs->pc = reg;
			break;
		case 34: /* NPC */
			regs->npc = reg;
			break;
		case 35: /* Y */
			regs->y = reg;
			break;
		case 36: /* WIM */
		case 37: /* TBR */
			break;
		default:
			goto finish;
		}

		pos++;
		count--;
	}
finish:
	pos *= sizeof(reg);
	count *= sizeof(reg);

	return user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					 38 * sizeof(reg), -1);
}

static int fpregs32_get(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			void *kbuf, void __user *ubuf)
{
	const unsigned long *fpregs = target->thread.float_regs;
	int ret = 0;

#if 0
	if (target == current)
		save_and_clear_fpu();
#endif

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  fpregs,
				  0, 32 * sizeof(u32));

	if (!ret)
		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					       32 * sizeof(u32),
					       33 * sizeof(u32));
	if (!ret)
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  &target->thread.fsr,
					  33 * sizeof(u32),
					  34 * sizeof(u32));

	if (!ret) {
		unsigned long val;

		val = (1 << 8) | (8 << 16);
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  &val,
					  34 * sizeof(u32),
					  35 * sizeof(u32));
	}

	if (!ret)
		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					       35 * sizeof(u32), -1);

	return ret;
}

static int fpregs32_set(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			const void *kbuf, const void __user *ubuf)
{
	unsigned long *fpregs = target->thread.float_regs;
	int ret;

#if 0
	if (target == current)
		save_and_clear_fpu();
#endif
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 fpregs,
				 0, 32 * sizeof(u32));
	if (!ret)
		user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					  32 * sizeof(u32),
					  33 * sizeof(u32));
	if (!ret && count > 0) {
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &target->thread.fsr,
					 33 * sizeof(u32),
					 34 * sizeof(u32));
	}

	if (!ret)
		ret = user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
						34 * sizeof(u32), -1);
	return ret;
}

static const struct user_regset sparc32_regsets[] = {
	/* Format is:
	 * 	G0 --> G7
	 *	O0 --> O7
	 *	L0 --> L7
	 *	I0 --> I7
	 *	PSR, PC, nPC, Y, WIM, TBR
	 */
	[REGSET_GENERAL] = {
		.core_note_type = NT_PRSTATUS,
		.n = 38 * sizeof(u32),
		.size = sizeof(u32), .align = sizeof(u32),
		.get = genregs32_get, .set = genregs32_set
	},
	/* Format is:
	 *	F0 --> F31
	 *	empty 32-bit word
	 *	FSR (32--bit word)
	 *	FPU QUEUE COUNT (8-bit char)
	 *	FPU QUEUE ENTRYSIZE (8-bit char)
	 *	FPU ENABLED (8-bit char)
	 *	empty 8-bit char
	 *	FPU QUEUE (64 32-bit ints)
	 */
	[REGSET_FP] = {
		.core_note_type = NT_PRFPREG,
		.n = 99 * sizeof(u32),
		.size = sizeof(u32), .align = sizeof(u32),
		.get = fpregs32_get, .set = fpregs32_set
	},
};

static const struct user_regset_view user_sparc32_view = {
	.name = "sparc", .e_machine = EM_SPARC,
	.regsets = sparc32_regsets, .n = ARRAY_SIZE(sparc32_regsets)
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_sparc32_view;
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
	if (current->exit_code) {
		send_sig (current->exit_code, current, 1);
		current->exit_code = 0;
	}
}
