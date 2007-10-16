/* By Ross Biro 1/23/92 */
/*
 * Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
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
#include <asm/i387.h>
#include <asm/debugreg.h>
#include <asm/ldt.h>
#include <asm/desc.h>

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * Determines which flags the user has access to [1 = access, 0 = no access].
 * Prohibits changing ID(21), VIP(20), VIF(19), VM(17), NT(14), IOPL(12-13), IF(9).
 * Also masks reserved bits (31-22, 15, 5, 3, 1).
 */
#define FLAG_MASK 0x00050dd5

/* set's the trap flag. */
#define TRAP_FLAG 0x100

/*
 * Offset of eflags on child stack..
 */
#define EFL_OFFSET offsetof(struct pt_regs, eflags)

static inline struct pt_regs *get_child_regs(struct task_struct *task)
{
	void *stack_top = (void *)task->thread.esp0;
	return stack_top - sizeof(struct pt_regs);
}

/*
 * This routine will get a word off of the processes privileged stack.
 * the offset is bytes into the pt_regs structure on the stack.
 * This routine assumes that all the privileged stacks are in our
 * data space.
 */   
static inline int get_stack_long(struct task_struct *task, int offset)
{
	unsigned char *stack;

	stack = (unsigned char *)task->thread.esp0 - sizeof(struct pt_regs);
	stack += offset;
	return (*((int *)stack));
}

/*
 * This routine will put a word on the processes privileged stack.
 * the offset is bytes into the pt_regs structure on the stack.
 * This routine assumes that all the privileged stacks are in our
 * data space.
 */
static inline int put_stack_long(struct task_struct *task, int offset,
	unsigned long data)
{
	unsigned char * stack;

	stack = (unsigned char *)task->thread.esp0 - sizeof(struct pt_regs);
	stack += offset;
	*(unsigned long *) stack = data;
	return 0;
}

static int putreg(struct task_struct *child,
	unsigned long regno, unsigned long value)
{
	switch (regno >> 2) {
		case GS:
			if (value && (value & 3) != 3)
				return -EIO;
			child->thread.gs = value;
			return 0;
		case DS:
		case ES:
		case FS:
			if (value && (value & 3) != 3)
				return -EIO;
			value &= 0xffff;
			break;
		case SS:
		case CS:
			if ((value & 3) != 3)
				return -EIO;
			value &= 0xffff;
			break;
		case EFL:
			value &= FLAG_MASK;
			value |= get_stack_long(child, EFL_OFFSET) & ~FLAG_MASK;
			break;
	}
	if (regno > FS*4)
		regno -= 1*4;
	put_stack_long(child, regno, value);
	return 0;
}

static unsigned long getreg(struct task_struct *child,
	unsigned long regno)
{
	unsigned long retval = ~0UL;

	switch (regno >> 2) {
		case GS:
			retval = child->thread.gs;
			break;
		case DS:
		case ES:
		case FS:
		case SS:
		case CS:
			retval = 0xffff;
			/* fall through */
		default:
			if (regno > FS*4)
				regno -= 1*4;
			retval &= get_stack_long(child, regno);
	}
	return retval;
}

#define LDT_SEGMENT 4

static unsigned long convert_eip_to_linear(struct task_struct *child, struct pt_regs *regs)
{
	unsigned long addr, seg;

	addr = regs->eip;
	seg = regs->xcs & 0xffff;
	if (regs->eflags & VM_MASK) {
		addr = (addr & 0xffff) + (seg << 4);
		return addr;
	}

	/*
	 * We'll assume that the code segments in the GDT
	 * are all zero-based. That is largely true: the
	 * TLS segments are used for data, and the PNPBIOS
	 * and APM bios ones we just ignore here.
	 */
	if (seg & LDT_SEGMENT) {
		u32 *desc;
		unsigned long base;

		seg &= ~7UL;

		down(&child->mm->context.sem);
		if (unlikely((seg >> 3) >= child->mm->context.size))
			addr = -1L; /* bogus selector, access would fault */
		else {
			desc = child->mm->context.ldt + seg;
			base = ((desc[0] >> 16) |
				((desc[1] & 0xff) << 16) |
				(desc[1] & 0xff000000));

			/* 16-bit code segment? */
			if (!((desc[1] >> 22) & 1))
				addr &= 0xffff;
			addr += base;
		}
		up(&child->mm->context.sem);
	}
	return addr;
}

static inline int is_setting_trap_flag(struct task_struct *child, struct pt_regs *regs)
{
	int i, copied;
	unsigned char opcode[15];
	unsigned long addr = convert_eip_to_linear(child, regs);

	copied = access_process_vm(child, addr, opcode, sizeof(opcode), 0);
	for (i = 0; i < copied; i++) {
		switch (opcode[i]) {
		/* popf and iret */
		case 0x9d: case 0xcf:
			return 1;
		/* opcode and address size prefixes */
		case 0x66: case 0x67:
			continue;
		/* irrelevant prefixes (segment overrides and repeats) */
		case 0x26: case 0x2e:
		case 0x36: case 0x3e:
		case 0x64: case 0x65:
		case 0xf0: case 0xf2: case 0xf3:
			continue;

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
	struct pt_regs *regs = get_child_regs(child);

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
	 */
	if (is_setting_trap_flag(child, regs))
		return;
	
	child->ptrace |= PT_DTRACE;
}

static void clear_singlestep(struct task_struct *child)
{
	/* Always clear TIF_SINGLESTEP... */
	clear_tsk_thread_flag(child, TIF_SINGLESTEP);

	/* But touch TF only if it was set by us.. */
	if (child->ptrace & PT_DTRACE) {
		struct pt_regs *regs = get_child_regs(child);
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
	clear_tsk_thread_flag(child, TIF_SYSCALL_EMU);
}

/*
 * Perform get_thread_area on behalf of the traced child.
 */
static int
ptrace_get_thread_area(struct task_struct *child,
		       int idx, struct user_desc __user *user_desc)
{
	struct user_desc info;
	struct desc_struct *desc;

/*
 * Get the current Thread-Local Storage area:
 */

#define GET_BASE(desc) ( \
	(((desc)->a >> 16) & 0x0000ffff) | \
	(((desc)->b << 16) & 0x00ff0000) | \
	( (desc)->b        & 0xff000000)   )

#define GET_LIMIT(desc) ( \
	((desc)->a & 0x0ffff) | \
	 ((desc)->b & 0xf0000) )

#define GET_32BIT(desc)		(((desc)->b >> 22) & 1)
#define GET_CONTENTS(desc)	(((desc)->b >> 10) & 3)
#define GET_WRITABLE(desc)	(((desc)->b >>  9) & 1)
#define GET_LIMIT_PAGES(desc)	(((desc)->b >> 23) & 1)
#define GET_PRESENT(desc)	(((desc)->b >> 15) & 1)
#define GET_USEABLE(desc)	(((desc)->b >> 20) & 1)

	if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
		return -EINVAL;

	desc = child->thread.tls_array + idx - GDT_ENTRY_TLS_MIN;

	info.entry_number = idx;
	info.base_addr = GET_BASE(desc);
	info.limit = GET_LIMIT(desc);
	info.seg_32bit = GET_32BIT(desc);
	info.contents = GET_CONTENTS(desc);
	info.read_exec_only = !GET_WRITABLE(desc);
	info.limit_in_pages = GET_LIMIT_PAGES(desc);
	info.seg_not_present = !GET_PRESENT(desc);
	info.useable = GET_USEABLE(desc);

	if (copy_to_user(user_desc, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

/*
 * Perform set_thread_area on behalf of the traced child.
 */
static int
ptrace_set_thread_area(struct task_struct *child,
		       int idx, struct user_desc __user *user_desc)
{
	struct user_desc info;
	struct desc_struct *desc;

	if (copy_from_user(&info, user_desc, sizeof(info)))
		return -EFAULT;

	if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
		return -EINVAL;

	desc = child->thread.tls_array + idx - GDT_ENTRY_TLS_MIN;
	if (LDT_empty(&info)) {
		desc->a = 0;
		desc->b = 0;
	} else {
		desc->a = LDT_entry_a(&info);
		desc->b = LDT_entry_b(&info);
	}

	return 0;
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	struct user * dummy = NULL;
	int i, ret;
	unsigned long __user *datap = (unsigned long __user *)data;

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
		if ((addr & 3) || addr < 0 || 
		    addr > sizeof(struct user) - 3)
			break;

		tmp = 0;  /* Default return condition */
		if(addr < FRAME_SIZE*sizeof(long))
			tmp = getreg(child, addr);
		if(addr >= (long) &dummy->u_debugreg[0] &&
		   addr <= (long) &dummy->u_debugreg[7]){
			addr -= (long) &dummy->u_debugreg[0];
			addr = addr >> 2;
			tmp = child->thread.debugreg[addr];
		}
		ret = put_user(tmp, datap);
		break;
	}

	/* when I and D space are separate, this will have to be fixed. */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = generic_ptrace_pokedata(child, addr, data);
		break;

	case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
		ret = -EIO;
		if ((addr & 3) || addr < 0 || 
		    addr > sizeof(struct user) - 3)
			break;

		if (addr < FRAME_SIZE*sizeof(long)) {
			ret = putreg(child, addr, data);
			break;
		}
		/* We need to be very careful here.  We implicitly
		   want to modify a portion of the task_struct, and we
		   have to be selective about what portions we allow someone
		   to modify. */

		  ret = -EIO;
		  if(addr >= (long) &dummy->u_debugreg[0] &&
		     addr <= (long) &dummy->u_debugreg[7]){

			  if(addr == (long) &dummy->u_debugreg[4]) break;
			  if(addr == (long) &dummy->u_debugreg[5]) break;
			  if(addr < (long) &dummy->u_debugreg[4] &&
			     ((unsigned long) data) >= TASK_SIZE-3) break;
			  
			  /* Sanity-check data. Take one half-byte at once with
			   * check = (val >> (16 + 4*i)) & 0xf. It contains the
			   * R/Wi and LENi bits; bits 0 and 1 are R/Wi, and bits
			   * 2 and 3 are LENi. Given a list of invalid values,
			   * we do mask |= 1 << invalid_value, so that
			   * (mask >> check) & 1 is a correct test for invalid
			   * values.
			   *
			   * R/Wi contains the type of the breakpoint /
			   * watchpoint, LENi contains the length of the watched
			   * data in the watchpoint case.
			   *
			   * The invalid values are:
			   * - LENi == 0x10 (undefined), so mask |= 0x0f00.
			   * - R/Wi == 0x10 (break on I/O reads or writes), so
			   *   mask |= 0x4444.
			   * - R/Wi == 0x00 && LENi != 0x00, so we have mask |=
			   *   0x1110.
			   *
			   * Finally, mask = 0x0f00 | 0x4444 | 0x1110 == 0x5f54.
			   *
			   * See the Intel Manual "System Programming Guide",
			   * 15.2.4
			   *
			   * Note that LENi == 0x10 is defined on x86_64 in long
			   * mode (i.e. even for 32-bit userspace software, but
			   * 64-bit kernel), so the x86_64 mask value is 0x5454.
			   * See the AMD manual no. 24593 (AMD64 System
			   * Programming)*/

			  if(addr == (long) &dummy->u_debugreg[7]) {
				  data &= ~DR_CONTROL_RESERVED;
				  for(i=0; i<4; i++)
					  if ((0x5f54 >> ((data >> (16 + 4*i)) & 0xf)) & 1)
						  goto out_tsk;
				  if (data)
					  set_tsk_thread_flag(child, TIF_DEBUG);
				  else
					  clear_tsk_thread_flag(child, TIF_DEBUG);
			  }
			  addr -= (long) &dummy->u_debugreg;
			  addr = addr >> 2;
			  child->thread.debugreg[addr] = data;
			  ret = 0;
		  }
		  break;

	case PTRACE_SYSEMU: /* continue and stop at next syscall, which will not be executed */
	case PTRACE_SYSCALL:	/* continue and stop at next (return from) syscall */
	case PTRACE_CONT:	/* restart after signal. */
		ret = -EIO;
		if (!valid_signal(data))
			break;
		if (request == PTRACE_SYSEMU) {
			set_tsk_thread_flag(child, TIF_SYSCALL_EMU);
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		} else if (request == PTRACE_SYSCALL) {
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
			clear_tsk_thread_flag(child, TIF_SYSCALL_EMU);
		} else {
			clear_tsk_thread_flag(child, TIF_SYSCALL_EMU);
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		}
		child->exit_code = data;
		/* make sure the single step bit is not set. */
		clear_singlestep(child);
		wake_up_process(child);
		ret = 0;
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
		child->exit_code = SIGKILL;
		/* make sure the single step bit is not set. */
		clear_singlestep(child);
		wake_up_process(child);
		break;

	case PTRACE_SYSEMU_SINGLESTEP: /* Same as SYSEMU, but singlestep if not syscall */
	case PTRACE_SINGLESTEP:	/* set the trap flag. */
		ret = -EIO;
		if (!valid_signal(data))
			break;

		if (request == PTRACE_SYSEMU_SINGLESTEP)
			set_tsk_thread_flag(child, TIF_SYSCALL_EMU);
		else
			clear_tsk_thread_flag(child, TIF_SYSCALL_EMU);

		clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		set_singlestep(child);
		child->exit_code = data;
		/* give it a chance to run. */
		wake_up_process(child);
		ret = 0;
		break;

	case PTRACE_GETREGS: { /* Get all gp regs from the child. */
	  	if (!access_ok(VERIFY_WRITE, datap, FRAME_SIZE*sizeof(long))) {
			ret = -EIO;
			break;
		}
		for ( i = 0; i < FRAME_SIZE*sizeof(long); i += sizeof(long) ) {
			__put_user(getreg(child, i), datap);
			datap++;
		}
		ret = 0;
		break;
	}

	case PTRACE_SETREGS: { /* Set all gp regs in the child. */
		unsigned long tmp;
	  	if (!access_ok(VERIFY_READ, datap, FRAME_SIZE*sizeof(long))) {
			ret = -EIO;
			break;
		}
		for ( i = 0; i < FRAME_SIZE*sizeof(long); i += sizeof(long) ) {
			__get_user(tmp, datap);
			putreg(child, i, tmp);
			datap++;
		}
		ret = 0;
		break;
	}

	case PTRACE_GETFPREGS: { /* Get the child FPU state. */
		if (!access_ok(VERIFY_WRITE, datap,
			       sizeof(struct user_i387_struct))) {
			ret = -EIO;
			break;
		}
		ret = 0;
		if (!tsk_used_math(child))
			init_fpu(child);
		get_fpregs((struct user_i387_struct __user *)data, child);
		break;
	}

	case PTRACE_SETFPREGS: { /* Set the child FPU state. */
		if (!access_ok(VERIFY_READ, datap,
			       sizeof(struct user_i387_struct))) {
			ret = -EIO;
			break;
		}
		set_stopped_child_used_math(child);
		set_fpregs(child, (struct user_i387_struct __user *)data);
		ret = 0;
		break;
	}

	case PTRACE_GETFPXREGS: { /* Get the child extended FPU state. */
		if (!access_ok(VERIFY_WRITE, datap,
			       sizeof(struct user_fxsr_struct))) {
			ret = -EIO;
			break;
		}
		if (!tsk_used_math(child))
			init_fpu(child);
		ret = get_fpxregs((struct user_fxsr_struct __user *)data, child);
		break;
	}

	case PTRACE_SETFPXREGS: { /* Set the child extended FPU state. */
		if (!access_ok(VERIFY_READ, datap,
			       sizeof(struct user_fxsr_struct))) {
			ret = -EIO;
			break;
		}
		set_stopped_child_used_math(child);
		ret = set_fpxregs(child, (struct user_fxsr_struct __user *)data);
		break;
	}

	case PTRACE_GET_THREAD_AREA:
		ret = ptrace_get_thread_area(child, addr,
					(struct user_desc __user *) data);
		break;

	case PTRACE_SET_THREAD_AREA:
		ret = ptrace_set_thread_area(child, addr,
					(struct user_desc __user *) data);
		break;

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}
 out_tsk:
	return ret;
}

void send_sigtrap(struct task_struct *tsk, struct pt_regs *regs, int error_code)
{
	struct siginfo info;

	tsk->thread.trap_no = 1;
	tsk->thread.error_code = error_code;

	memset(&info, 0, sizeof(info));
	info.si_signo = SIGTRAP;
	info.si_code = TRAP_BRKPT;

	/* User-mode eip? */
	info.si_addr = user_mode_vm(regs) ? (void __user *) regs->eip : NULL;

	/* Send us the fakey SIGTRAP */
	force_sig_info(SIGTRAP, &info, tsk);
}

/* notification of system call entry/exit
 * - triggered by current->work.syscall_trace
 */
__attribute__((regparm(3)))
int do_syscall_trace(struct pt_regs *regs, int entryexit)
{
	int is_sysemu = test_thread_flag(TIF_SYSCALL_EMU);
	/*
	 * With TIF_SYSCALL_EMU set we want to ignore TIF_SINGLESTEP for syscall
	 * interception
	 */
	int is_singlestep = !is_sysemu && test_thread_flag(TIF_SINGLESTEP);
	int ret = 0;

	/* do the secure computing check first */
	if (!entryexit)
		secure_computing(regs->orig_eax);

	if (unlikely(current->audit_context)) {
		if (entryexit)
			audit_syscall_exit(AUDITSC_RESULT(regs->eax),
						regs->eax);
		/* Debug traps, when using PTRACE_SINGLESTEP, must be sent only
		 * on the syscall exit path. Normally, when TIF_SYSCALL_AUDIT is
		 * not used, entry.S will call us only on syscall exit, not
		 * entry; so when TIF_SYSCALL_AUDIT is used we must avoid
		 * calling send_sigtrap() on syscall entry.
		 *
		 * Note that when PTRACE_SYSEMU_SINGLESTEP is used,
		 * is_singlestep is false, despite his name, so we will still do
		 * the correct thing.
		 */
		else if (is_singlestep)
			goto out;
	}

	if (!(current->ptrace & PT_PTRACED))
		goto out;

	/* If a process stops on the 1st tracepoint with SYSCALL_TRACE
	 * and then is resumed with SYSEMU_SINGLESTEP, it will come in
	 * here. We have to check this and return */
	if (is_sysemu && entryexit)
		return 0;

	/* Fake a debug trap */
	if (is_singlestep)
		send_sigtrap(current, regs, 0);

 	if (!test_thread_flag(TIF_SYSCALL_TRACE) && !is_sysemu)
		goto out;

	/* the 0x80 provides a way for the tracing parent to distinguish
	   between a syscall stop and SIGTRAP delivery */
	/* Note that the debugger could change the result of test_thread_flag!*/
	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD) ? 0x80:0));

	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
	ret = is_sysemu;
out:
	if (unlikely(current->audit_context) && !entryexit)
		audit_syscall_entry(AUDIT_ARCH_I386, regs->orig_eax,
				    regs->ebx, regs->ecx, regs->edx, regs->esi);
	if (ret == 0)
		return 0;

	regs->orig_eax = -1; /* force skip of syscall restarting */
	if (unlikely(current->audit_context))
		audit_syscall_exit(AUDITSC_RESULT(regs->eax), regs->eax);
	return 1;
}
