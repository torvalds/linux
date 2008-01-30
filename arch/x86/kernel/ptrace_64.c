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
#include <asm/prctl.h>
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

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure the single step bit is not set.
 */
void ptrace_disable(struct task_struct *child)
{ 
	user_disable_single_step(child);
}

static unsigned long *pt_regs_access(struct pt_regs *regs, unsigned long offset)
{
	BUILD_BUG_ON(offsetof(struct pt_regs, r15) != 0);
	return &regs->r15 + (offset / sizeof(regs->r15));
}

static int putreg(struct task_struct *child,
	unsigned long regno, unsigned long value)
{
	struct pt_regs *regs = task_pt_regs(child);
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
			/*
			 * When changing the segment base, use do_arch_prctl
			 * to set either thread.fs or thread.fsindex and the
			 * corresponding GDT slot.
			 */
			if (child->thread.fs != value)
				return do_arch_prctl(child, ARCH_SET_FS, value);
			return 0;
		case offsetof(struct user_regs_struct,gs_base):
			/*
			 * Exactly the same here as the %fs handling above.
			 */
			if (value >= TASK_SIZE_OF(child))
				return -EIO;
			if (child->thread.gs != value)
				return do_arch_prctl(child, ARCH_SET_GS, value);
			return 0;
		case offsetof(struct user_regs_struct,flags):
			value &= FLAG_MASK;
			/*
			 * If the user value contains TF, mark that
			 * it was not "us" (the debugger) that set it.
			 * If not, make sure it stays set if we had.
			 */
			if (value & X86_EFLAGS_TF)
				clear_tsk_thread_flag(child, TIF_FORCED_TF);
			else if (test_tsk_thread_flag(child, TIF_FORCED_TF))
				value |= X86_EFLAGS_TF;
			value |= regs->flags & ~FLAG_MASK;
			break;
		case offsetof(struct user_regs_struct,cs): 
			if ((value & 3) != 3)
				return -EIO;
			value &= 0xffff;
			break;
	}
	*pt_regs_access(regs, regno) = value;
	return 0;
}

static unsigned long getreg(struct task_struct *child, unsigned long regno)
{
	struct pt_regs *regs = task_pt_regs(child);
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
			/*
			 * do_arch_prctl may have used a GDT slot instead of
			 * the MSR.  To userland, it appears the same either
			 * way, except the %fs segment selector might not be 0.
			 */
			if (child->thread.fs != 0)
				return child->thread.fs;
			if (child->thread.fsindex != FS_TLS_SEL)
				return 0;
			return get_desc_base(&child->thread.tls_array[FS_TLS]);
		case offsetof(struct user_regs_struct, gs_base):
			/*
			 * Exactly the same here as the %fs handling above.
			 */
			if (child->thread.gs != 0)
				return child->thread.gs;
			if (child->thread.gsindex != GS_TLS_SEL)
				return 0;
			return get_desc_base(&child->thread.tls_array[GS_TLS]);
		case offsetof(struct user_regs_struct, flags):
			/*
			 * If the debugger set TF, hide it from the readout.
			 */
			val = regs->flags;
			if (test_tsk_thread_flag(child, TIF_IA32))
				val &= 0xffffffff;
			if (test_tsk_thread_flag(child, TIF_FORCED_TF))
				val &= ~X86_EFLAGS_TF;
			return val;
		default:
			val = *pt_regs_access(regs, regno);
			if (test_tsk_thread_flag(child, TIF_IA32))
				val &= 0xffffffff;
			return val;
	}

}

unsigned long ptrace_get_debugreg(struct task_struct *child, int n)
{
	switch (n) {
	case 0:		return child->thread.debugreg0;
	case 1:		return child->thread.debugreg1;
	case 2:		return child->thread.debugreg2;
	case 3:		return child->thread.debugreg3;
	case 6:		return child->thread.debugreg6;
	case 7:		return child->thread.debugreg7;
	}
	return 0;
}

int ptrace_set_debugreg(struct task_struct *child, int n, unsigned long data)
{
	int i;

	if (n < 4) {
		int dsize = test_tsk_thread_flag(child, TIF_IA32) ? 3 : 7;
		if (unlikely(data >= TASK_SIZE_OF(child) - dsize))
			return -EIO;
	}

	switch (n) {
	case 0:		child->thread.debugreg0 = data; break;
	case 1:		child->thread.debugreg1 = data; break;
	case 2:		child->thread.debugreg2 = data; break;
	case 3:		child->thread.debugreg3 = data; break;

	case 6:
		if (data >> 32)
			return -EIO;
		child->thread.debugreg6 = data;
		break;

	case 7:
		/*
		 * See ptrace_32.c for an explanation of this awkward check.
		 */
		data &= ~DR_CONTROL_RESERVED;
		for (i = 0; i < 4; i++)
			if ((0x5554 >> ((data >> (16 + 4*i)) & 0xf)) & 1)
				return -EIO;
		child->thread.debugreg7 = data;
		if (data)
			set_tsk_thread_flag(child, TIF_DEBUG);
		else
			clear_tsk_thread_flag(child, TIF_DEBUG);
		break;
	}

	return 0;
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	long ret;
	unsigned ui;

	switch (request) {
	/* when I and D space are separate, these will need to be fixed. */
	case PTRACE_PEEKTEXT: /* read word at location addr. */ 
	case PTRACE_PEEKDATA:
		ret = generic_ptrace_peekdata(child, addr, data);
		break;

	/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		unsigned long tmp;

		ret = -EIO;
		if ((addr & 7) ||
		    addr > sizeof(struct user) - 7)
			break;

		tmp = 0;
		if (addr < sizeof(struct user_regs_struct))
			tmp = getreg(child, addr);
		else if (addr >= offsetof(struct user, u_debugreg[0])) {
			addr -= offsetof(struct user, u_debugreg[0]);
			tmp = ptrace_get_debugreg(child, addr / sizeof(long));
		}

		ret = put_user(tmp,(unsigned long __user *) data);
		break;
	}

	/* when I and D space are separate, this will have to be fixed. */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = generic_ptrace_pokedata(child, addr, data);
		break;

	case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
		ret = -EIO;
		if ((addr & 7) ||
		    addr > sizeof(struct user) - 7)
			break;

		if (addr < sizeof(struct user_regs_struct))
			ret = putreg(child, addr, data);
		else if (addr >= offsetof(struct user, u_debugreg[0])) {
			addr -= offsetof(struct user, u_debugreg[0]);
			ret = ptrace_set_debugreg(child,
						  addr / sizeof(long), data);
		}
		break;

#ifdef CONFIG_IA32_EMULATION
		/* This makes only sense with 32bit programs. Allow a
		   64bit debugger to fully examine them too. Better
		   don't use it against 64bit processes, use
		   PTRACE_ARCH_PRCTL instead. */
	case PTRACE_GET_THREAD_AREA:
		if (addr < 0)
			return -EIO;
		ret = do_get_thread_area(child, addr,
					 (struct user_desc __user *) data);

		break;
	case PTRACE_SET_THREAD_AREA:
		if (addr < 0)
			return -EIO;
		ret = do_set_thread_area(child, addr,
					 (struct user_desc __user *) data, 0);
		break;
#endif
		/* normal 64bit interface to access TLS data. 
		   Works just like arch_prctl, except that the arguments
		   are reversed. */
	case PTRACE_ARCH_PRCTL: 
		ret = do_arch_prctl(child, data, addr);
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
			ret = __get_user(tmp, (unsigned long __user *) data);
			if (ret)
				break;
			ret = putreg(child, ui, tmp);
			if (ret)
				break;
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
	printk("trace %s ip %lx sp %lx ax %d origrax %d caller %lx tiflags %x ptrace %x\n",
	       current->comm,
	       regs->ip, regs->sp, regs->ax, regs->orig_ax, __builtin_return_address(0),
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
	secure_computing(regs->orig_ax);

	if (test_thread_flag(TIF_SYSCALL_TRACE)
	    && (current->ptrace & PT_PTRACED))
		syscall_trace(regs);

	if (unlikely(current->audit_context)) {
		if (test_thread_flag(TIF_IA32)) {
			audit_syscall_entry(AUDIT_ARCH_I386,
					    regs->orig_ax,
					    regs->bx, regs->cx,
					    regs->dx, regs->si);
		} else {
			audit_syscall_entry(AUDIT_ARCH_X86_64,
					    regs->orig_ax,
					    regs->di, regs->si,
					    regs->dx, regs->r10);
		}
	}
}

asmlinkage void syscall_trace_leave(struct pt_regs *regs)
{
	if (unlikely(current->audit_context))
		audit_syscall_exit(AUDITSC_RESULT(regs->ax), regs->ax);

	if ((test_thread_flag(TIF_SYSCALL_TRACE)
	     || test_thread_flag(TIF_SINGLESTEP))
	    && (current->ptrace & PT_PTRACED))
		syscall_trace(regs);
}
