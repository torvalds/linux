/* ptrace.c */
/* By Ross Biro 1/23/92 */
/*
 * Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 * 
 * x86-64 port 2000-2002 Andi Kleen
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/security.h>
#include <linux/audit.h>
#include <linux/seccomp.h>
#include <linux/signal.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/i387.h>
#include <asm/debugreg.h>
#include <asm/ldt.h>
#include <asm/desc.h>
#include <asm/proto.h>
#include <asm/ia32.h>

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * Determines which flags the user has access to [1 = access, 0 = no access].
 * Prohibits changing ID(21), VIP(20), VIF(19), VM(17), IOPL(12-13), IF(9).
 * Also masks reserved bits (63-22, 15, 5, 3, 1).
 */
#define FLAG_MASK 0x54dd5UL

/* set's the trap flag. */
#define TRAP_FLAG 0x100UL

/*
 * eflags and offset of eflags on child stack..
 */
#define EFLAGS offsetof(struct pt_regs, eflags)
#define EFL_OFFSET ((int)(EFLAGS-sizeof(struct pt_regs)))

/*
 * this routine will get a word off of the processes privileged stack. 
 * the offset is how far from the base addr as stored in the TSS.  
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */   
static inline unsigned long get_stack_long(struct task_struct *task, int offset)
{
	unsigned char *stack;

	stack = (unsigned char *)task->thread.rsp0;
	stack += offset;
	return (*((unsigned long *)stack));
}

/*
 * this routine will put a word on the processes privileged stack. 
 * the offset is how far from the base addr as stored in the TSS.  
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */
static inline long put_stack_long(struct task_struct *task, int offset,
	unsigned long data)
{
	unsigned char * stack;

	stack = (unsigned char *) task->thread.rsp0;
	stack += offset;
	*(unsigned long *) stack = data;
	return 0;
}

#define LDT_SEGMENT 4

unsigned long convert_rip_to_linear(struct task_struct *child, struct pt_regs *regs)
{
	unsigned long addr, seg;

	addr = regs->rip;
	seg = regs->cs & 0xffff;

	/*
	 * We'll assume that the code segments in the GDT
	 * are all zero-based. That is largely true: the
	 * TLS segments are used for data, and the PNPBIOS
	 * and APM bios ones we just ignore here.
	 */
	if (seg & LDT_SEGMENT) {
		u32 *desc;
		unsigned long base;

		down(&child->mm->context.sem);
		desc = child->mm->context.ldt + (seg & ~7);
		base = (desc[0] >> 16) | ((desc[1] & 0xff) << 16) | (desc[1] & 0xff000000);

		/* 16-bit code segment? */
		if (!((desc[1] >> 22) & 1))
			addr &= 0xffff;
		addr += base;
		up(&child->mm->context.sem);
	}
	return addr;
}

static int is_at_popf(struct task_struct *child, struct pt_regs *regs)
{
	int i, copied;
	unsigned char opcode[16];
	unsigned long addr = convert_rip_to_linear(child, regs);

	copied = access_process_vm(child, addr, opcode, sizeof(opcode), 0);
	for (i = 0; i < copied; i++) {
		switch (opcode[i]) {
		/* popf */
		case 0x9d:
			return 1;

			/* CHECKME: 64 65 */

		/* opcode and address size prefixes */
		case 0x66: case 0x67:
			continue;
		/* irrelevant prefixes (segment overrides and repeats) */
		case 0x26: case 0x2e:
		case 0x36: case 0x3e:
		case 0x64: case 0x65:
		case 0xf0: case 0xf2: case 0xf3:
			continue;

		/* REX prefixes */
		case 0x40 ... 0x4f:
			continue;

			/* CHECKME: f0, f2, f3 */

		/*
		 * pushf: NOTE! We should probably not let
		 * the user see the TF bit being set. But
		 * it's more pain than it's worth to avoid
		 * it, and a debugger could emulate this
		 * all in user space if it _really_ cares.
		 */
		case 0x9c:
		default:
			return 0;
		}
	}
	return 0;
}

static void set_singlestep(struct task_struct *child)
{
	struct pt_regs *regs = task_pt_regs(child);

	/*
	 * Always set TIF_SINGLESTEP - this guarantees that
	 * we single-step system calls etc..  This will also
	 * cause us to set TF when returning to user mode.
	 */
	set_tsk_thread_flag(child, TIF_SINGLESTEP);

	/*
	 * If TF was already set, don't do anything else
	 */
	if (regs->eflags & TRAP_FLAG)
		return;

	/* Set TF on the kernel stack.. */
	regs->eflags |= TRAP_FLAG;

	/*
	 * ..but if TF is changed by the instruction we will trace,
	 * don't mark it as being "us" that set it, so that we
	 * won't clear it by hand later.
	 *
	 * AK: this is not enough, LAHF and IRET can change TF in user space too.
	 */
	if (is_at_popf(child, regs))
		return;

	child->ptrace |= PT_DTRACE;
}

static void clear_singlestep(struct task_struct *child)
{
	/* Always clear TIF_SINGLESTEP... */
	clear_tsk_thread_flag(child, TIF_SINGLESTEP);

	/* But touch TF only if it was set by us.. */
	if (child->ptrace & PT_DTRACE) {
		struct pt_regs *regs = task_pt_regs(child);
		regs->eflags &= ~TRAP_FLAG;
		child->ptrace &= ~PT_DTRACE;
	}
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure the single step bit is not set.
 */
void ptrace_disable(struct task_struct *child)
{ 
	clear_singlestep(child);
}

static int putreg(struct task_struct *child,
	unsigned long regno, unsigned long value)
{
	unsigned long tmp; 
	
	/* Some code in the 64bit emulation may not be 64bit clean.
	   Don't take any chances. */
	if (test_tsk_thread_flag(child, TIF_IA32))
		value &= 0xffffffff;
	switch (regno) {
		case offsetof(struct user_regs_struct,fs):
			if (value && (value & 3) != 3)
				return -EIO;
			child->thread.fsindex = value & 0xffff; 
			return 0;
		case offsetof(struct user_regs_struct,gs):
			if (value && (value & 3) != 3)
				return -EIO;
			child->thread.gsindex = value & 0xffff;
			return 0;
		case offsetof(struct user_regs_struct,ds):
			if (value && (value & 3) != 3)
				return -EIO;
			child->thread.ds = value & 0xffff;
			return 0;
		case offsetof(struct user_regs_struct,es): 
			if (value && (value & 3) != 3)
				return -EIO;
			child->thread.es = value & 0xffff;
			return 0;
		case offsetof(struct user_regs_struct,ss):
			if ((value & 3) != 3)
				return -EIO;
			value &= 0xffff;
			return 0;
		case offsetof(struct user_regs_struct,fs_base):
			if (value >= TASK_SIZE_OF(child))
				return -EIO;
			child->thread.fs = value;
			return 0;
		case offsetof(struct user_regs_struct,gs_base):
			if (value >= TASK_SIZE_OF(child))
				return -EIO;
			child->thread.gs = value;
			return 0;
		case offsetof(struct user_regs_struct, eflags):
			value &= FLAG_MASK;
			tmp = get_stack_long(child, EFL_OFFSET); 
			tmp &= ~FLAG_MASK; 
			value |= tmp;
			break;
		case offsetof(struct user_regs_struct,cs): 
			if ((value & 3) != 3)
				return -EIO;
			value &= 0xffff;
			break;
	}
	put_stack_long(child, regno - sizeof(struct pt_regs), value);
	return 0;
}

static unsigned long getreg(struct task_struct *child, unsigned long regno)
{
	unsigned long val;
	switch (regno) {
		case offsetof(struct user_regs_struct, fs):
			return child->thread.fsindex;
		case offsetof(struct user_regs_struct, gs):
			return child->thread.gsindex;
		case offsetof(struct user_regs_struct, ds):
			return child->thread.ds;
		case offsetof(struct user_regs_struct, es):
			return child->thread.es; 
		case offsetof(struct user_regs_struct, fs_base):
			return child->thread.fs;
		case offsetof(struct user_regs_struct, gs_base):
			return child->thread.gs;
		default:
			regno = regno - sizeof(struct pt_regs);
			val = get_stack_long(child, regno);
			if (test_tsk_thread_flag(child, TIF_IA32))
				val &= 0xffffffff;
			return val;
	}

}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	long i, ret;
	unsigned ui;

	switch (request) {
	/* when I and D space are separate, these will need to be fixed. */
	case PTRACE_PEEKTEXT: /* read word at location addr. */ 
	case PTRACE_PEEKDATA: {
		unsigned long tmp;
		int copied;

		copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
		ret = -EIO;
		if (copied != sizeof(tmp))
			break;
		ret = put_user(tmp,(unsigned long __user *) data);
		break;
	}

	/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		unsigned long tmp;

		ret = -EIO;
		if ((addr & 7) ||
		    addr > sizeof(struct user) - 7)
			break;

		switch (addr) { 
		case 0 ... sizeof(struct user_regs_struct) - sizeof(long):
			tmp = getreg(child, addr);
			break;
		case offsetof(struct user, u_debugreg[0]):
			tmp = child->thread.debugreg0;
			break;
		case offsetof(struct user, u_debugreg[1]):
			tmp = child->thread.debugreg1;
			break;
		case offsetof(struct user, u_debugreg[2]):
			tmp = child->thread.debugreg2;
			break;
		case offsetof(struct user, u_debugreg[3]):
			tmp = child->thread.debugreg3;
			break;
		case offsetof(struct user, u_debugreg[6]):
			tmp = child->thread.debugreg6;
			break;
		case offsetof(struct user, u_debugreg[7]):
			tmp = child->thread.debugreg7;
			break;
		default:
			tmp = 0;
			break;
		}
		ret = put_user(tmp,(unsigned long __user *) data);
		break;
	}

	/* when I and D space are separate, this will have to be fixed. */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = 0;
		if (access_process_vm(child, addr, &data, sizeof(data), 1) == sizeof(data))
			break;
		ret = -EIO;
		break;

	case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
	{
		int dsize = test_tsk_thread_flag(child, TIF_IA32) ? 3 : 7;
		ret = -EIO;
		if ((addr & 7) ||
		    addr > sizeof(struct user) - 7)
			break;

		switch (addr) { 
		case 0 ... sizeof(struct user_regs_struct) - sizeof(long):
			ret = putreg(child, addr, data);
			break;
		/* Disallows to set a breakpoint into the vsyscall */
		case offsetof(struct user, u_debugreg[0]):
			if (data >= TASK_SIZE_OF(child) - dsize) break;
			child->thread.debugreg0 = data;
			ret = 0;
			break;
		case offsetof(struct user, u_debugreg[1]):
			if (data >= TASK_SIZE_OF(child) - dsize) break;
			child->thread.debugreg1 = data;
			ret = 0;
			break;
		case offsetof(struct user, u_debugreg[2]):
			if (data >= TASK_SIZE_OF(child) - dsize) break;
			child->thread.debugreg2 = data;
			ret = 0;
			break;
		case offsetof(struct user, u_debugreg[3]):
			if (data >= TASK_SIZE_OF(child) - dsize) break;
			child->thread.debugreg3 = data;
			ret = 0;
			break;
		case offsetof(struct user, u_debugreg[6]):
				  if (data >> 32)
				break; 
			child->thread.debugreg6 = data;
			ret = 0;
			break;
		case offsetof(struct user, u_debugreg[7]):
			/* See arch/i386/kernel/ptrace.c for an explanation of
			 * this awkward check.*/
			data &= ~DR_CONTROL_RESERVED;
			for(i=0; i<4; i++)
				if ((0x5554 >> ((data >> (16 + 4*i)) & 0xf)) & 1)
					break;
			if (i == 4) {
				child->thread.debugreg7 = data;
			  ret = 0;
		  }
		  break;
		}
		break;
	}
	case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
	case PTRACE_CONT:    /* restart after signal. */

		ret = -EIO;
		if (!valid_signal(data))
			break;
		if (request == PTRACE_SYSCALL)
			set_tsk_thread_flag(child,TIF_SYSCALL_TRACE);
		else
			clear_tsk_thread_flag(child,TIF_SYSCALL_TRACE);
		clear_tsk_thread_flag(child, TIF_SINGLESTEP);
		child->exit_code = data;
		/* make sure the single step bit is not set. */
		clear_singlestep(child);
		wake_up_process(child);
		ret = 0;
		break;

#ifdef CONFIG_IA32_EMULATION
		/* This makes only sense with 32bit programs. Allow a
		   64bit debugger to fully examine them too. Better
		   don't use it against 64bit processes, use
		   PTRACE_ARCH_PRCTL instead. */
	case PTRACE_SET_THREAD_AREA: {
		struct user_desc __user *p;
		int old; 
		p = (struct user_desc __user *)data;
		get_user(old,  &p->entry_number); 
		put_user(addr, &p->entry_number);
		ret = do_set_thread_area(&child->thread, p);
		put_user(old,  &p->entry_number); 
		break;
	case PTRACE_GET_THREAD_AREA:
		p = (struct user_desc __user *)data;
		get_user(old,  &p->entry_number); 
		put_user(addr, &p->entry_number);
		ret = do_get_thread_area(&child->thread, p);
		put_user(old,  &p->entry_number); 
		break;
	} 
#endif
		/* normal 64bit interface to access TLS data. 
		   Works just like arch_prctl, except that the arguments
		   are reversed. */
	case PTRACE_ARCH_PRCTL: 
		ret = do_arch_prctl(child, data, addr);
		break;

/*
 * make the child exit.  Best I can do is send it a sigkill. 
 * perhaps it should be put in the status that it wants to 
 * exit.
 */
	case PTRACE_KILL:
		ret = 0;
		if (child->exit_state == EXIT_ZOMBIE)	/* already dead */
			break;
		clear_tsk_thread_flag(child, TIF_SINGLESTEP);
		child->exit_code = SIGKILL;
		/* make sure the single step bit is not set. */
		clear_singlestep(child);
		wake_up_process(child);
		break;

	case PTRACE_SINGLESTEP:    /* set the trap flag. */
		ret = -EIO;
		if (!valid_signal(data))
			break;
		clear_tsk_thread_flag(child,TIF_SYSCALL_TRACE);
		set_singlestep(child);
		child->exit_code = data;
		/* give it a chance to run. */
		wake_up_process(child);
		ret = 0;
		break;

	case PTRACE_DETACH:
		/* detach a process that was attached. */
		ret = ptrace_detach(child, data);
		break;

	case PTRACE_GETREGS: { /* Get all gp regs from the child. */
	  	if (!access_ok(VERIFY_WRITE, (unsigned __user *)data,
			       sizeof(struct user_regs_struct))) {
			ret = -EIO;
			break;
		}
		ret = 0;
		for (ui = 0; ui < sizeof(struct user_regs_struct); ui += sizeof(long)) {
			ret |= __put_user(getreg(child, ui),(unsigned long __user *) data);
			data += sizeof(long);
		}
		break;
	}

	case PTRACE_SETREGS: { /* Set all gp regs in the child. */
		unsigned long tmp;
	  	if (!access_ok(VERIFY_READ, (unsigned __user *)data,
			       sizeof(struct user_regs_struct))) {
			ret = -EIO;
			break;
		}
		ret = 0;
		for (ui = 0; ui < sizeof(struct user_regs_struct); ui += sizeof(long)) {
			ret |= __get_user(tmp, (unsigned long __user *) data);
			putreg(child, ui, tmp);
			data += sizeof(long);
		}
		break;
	}

	case PTRACE_GETFPREGS: { /* Get the child extended FPU state. */
		if (!access_ok(VERIFY_WRITE, (unsigned __user *)data,
			       sizeof(struct user_i387_struct))) {
			ret = -EIO;
			break;
		}
		ret = get_fpregs((struct user_i387_struct __user *)data, child);
		break;
	}

	case PTRACE_SETFPREGS: { /* Set the child extended FPU state. */
		if (!access_ok(VERIFY_READ, (unsigned __user *)data,
			       sizeof(struct user_i387_struct))) {
			ret = -EIO;
			break;
		}
		set_stopped_child_used_math(child);
		ret = set_fpregs(child, (struct user_i387_struct __user *)data);
		break;
	}

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}
	return ret;
}

static void syscall_trace(struct pt_regs *regs)
{

#if 0
	printk("trace %s rip %lx rsp %lx rax %d origrax %d caller %lx tiflags %x ptrace %x\n",
	       current->comm,
	       regs->rip, regs->rsp, regs->rax, regs->orig_rax, __builtin_return_address(0),
	       current_thread_info()->flags, current->ptrace); 
#endif

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

asmlinkage void syscall_trace_enter(struct pt_regs *regs)
{
	/* do the secure computing check first */
	secure_computing(regs->orig_rax);

	if (test_thread_flag(TIF_SYSCALL_TRACE)
	    && (current->ptrace & PT_PTRACED))
		syscall_trace(regs);

	if (unlikely(current->audit_context)) {
		if (test_thread_flag(TIF_IA32)) {
			audit_syscall_entry(current, AUDIT_ARCH_I386,
					    regs->orig_rax,
					    regs->rbx, regs->rcx,
					    regs->rdx, regs->rsi);
		} else {
			audit_syscall_entry(current, AUDIT_ARCH_X86_64,
					    regs->orig_rax,
					    regs->rdi, regs->rsi,
					    regs->rdx, regs->r10);
		}
	}
}

asmlinkage void syscall_trace_leave(struct pt_regs *regs)
{
	if (unlikely(current->audit_context))
		audit_syscall_exit(current, AUDITSC_RESULT(regs->rax), regs->rax);

	if ((test_thread_flag(TIF_SYSCALL_TRACE)
	     || test_thread_flag(TIF_SINGLESTEP))
	    && (current->ptrace & PT_PTRACED))
		syscall_trace(regs);
}
