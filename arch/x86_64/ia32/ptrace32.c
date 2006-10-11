/* 
 * 32bit ptrace for x86-64.
 *
 * Copyright 2001,2002 Andi Kleen, SuSE Labs.
 * Some parts copied from arch/i386/kernel/ptrace.c. See that file for earlier 
 * copyright.
 * 
 * This allows to access 64bit processes too; but there is no way to see the extended 
 * register contents.
 */ 

#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/mm.h>
#include <linux/ptrace.h>
#include <asm/ptrace.h>
#include <asm/compat.h>
#include <asm/uaccess.h>
#include <asm/user32.h>
#include <asm/user.h>
#include <asm/errno.h>
#include <asm/debugreg.h>
#include <asm/i387.h>
#include <asm/fpu32.h>
#include <asm/ia32.h>

/*
 * Determines which flags the user has access to [1 = access, 0 = no access].
 * Prohibits changing ID(21), VIP(20), VIF(19), VM(17), IOPL(12-13), IF(9).
 * Also masks reserved bits (31-22, 15, 5, 3, 1).
 */
#define FLAG_MASK 0x54dd5UL

#define R32(l,q) \
	case offsetof(struct user32, regs.l): stack[offsetof(struct pt_regs, q)/8] = val; break

static int putreg32(struct task_struct *child, unsigned regno, u32 val)
{
	int i;
	__u64 *stack = (__u64 *)task_pt_regs(child);

	switch (regno) {
	case offsetof(struct user32, regs.fs):
		if (val && (val & 3) != 3) return -EIO; 
		child->thread.fsindex = val & 0xffff;
		break;
	case offsetof(struct user32, regs.gs):
		if (val && (val & 3) != 3) return -EIO; 
		child->thread.gsindex = val & 0xffff;
		break;
	case offsetof(struct user32, regs.ds):
		if (val && (val & 3) != 3) return -EIO; 
		child->thread.ds = val & 0xffff;
		break;
	case offsetof(struct user32, regs.es):
		child->thread.es = val & 0xffff;
		break;
	case offsetof(struct user32, regs.ss): 
		if ((val & 3) != 3) return -EIO;
        	stack[offsetof(struct pt_regs, ss)/8] = val & 0xffff;
		break;
	case offsetof(struct user32, regs.cs): 
		if ((val & 3) != 3) return -EIO;
		stack[offsetof(struct pt_regs, cs)/8] = val & 0xffff;
		break;

	R32(ebx, rbx); 
	R32(ecx, rcx);
	R32(edx, rdx);
	R32(edi, rdi);
	R32(esi, rsi);
	R32(ebp, rbp);
	R32(eax, rax);
	R32(orig_eax, orig_rax);
	R32(eip, rip);
	R32(esp, rsp);

	case offsetof(struct user32, regs.eflags): {
		__u64 *flags = &stack[offsetof(struct pt_regs, eflags)/8];
		val &= FLAG_MASK;
		*flags = val | (*flags & ~FLAG_MASK);
		break;
	}

	case offsetof(struct user32, u_debugreg[4]): 
	case offsetof(struct user32, u_debugreg[5]):
		return -EIO;

	case offsetof(struct user32, u_debugreg[0]):
		child->thread.debugreg0 = val;
		break;

	case offsetof(struct user32, u_debugreg[1]):
		child->thread.debugreg1 = val;
		break;

	case offsetof(struct user32, u_debugreg[2]):
		child->thread.debugreg2 = val;
		break;

	case offsetof(struct user32, u_debugreg[3]):
		child->thread.debugreg3 = val;
		break;

	case offsetof(struct user32, u_debugreg[6]):
		child->thread.debugreg6 = val;
		break; 

	case offsetof(struct user32, u_debugreg[7]):
		val &= ~DR_CONTROL_RESERVED;
		/* See arch/i386/kernel/ptrace.c for an explanation of
		 * this awkward check.*/
		for(i=0; i<4; i++)
			if ((0x5454 >> ((val >> (16 + 4*i)) & 0xf)) & 1)
			       return -EIO;
		child->thread.debugreg7 = val; 
		if (val)
			set_tsk_thread_flag(child, TIF_DEBUG);
		else
			clear_tsk_thread_flag(child, TIF_DEBUG);
		break; 
		    
	default:
		if (regno > sizeof(struct user32) || (regno & 3))
			return -EIO;
	       
		/* Other dummy fields in the virtual user structure are ignored */ 
		break; 		
	}
	return 0;
}

#undef R32

#define R32(l,q) \
	case offsetof(struct user32, regs.l): *val = stack[offsetof(struct pt_regs, q)/8]; break

static int getreg32(struct task_struct *child, unsigned regno, u32 *val)
{
	__u64 *stack = (__u64 *)task_pt_regs(child);

	switch (regno) {
	case offsetof(struct user32, regs.fs):
	        *val = child->thread.fsindex;
		break;
	case offsetof(struct user32, regs.gs):
		*val = child->thread.gsindex;
		break;
	case offsetof(struct user32, regs.ds):
		*val = child->thread.ds;
		break;
	case offsetof(struct user32, regs.es):
		*val = child->thread.es;
		break;

	R32(cs, cs);
	R32(ss, ss);
	R32(ebx, rbx); 
	R32(ecx, rcx);
	R32(edx, rdx);
	R32(edi, rdi);
	R32(esi, rsi);
	R32(ebp, rbp);
	R32(eax, rax);
	R32(orig_eax, orig_rax);
	R32(eip, rip);
	R32(eflags, eflags);
	R32(esp, rsp);

	case offsetof(struct user32, u_debugreg[0]): 
		*val = child->thread.debugreg0; 
		break; 
	case offsetof(struct user32, u_debugreg[1]): 
		*val = child->thread.debugreg1; 
		break; 
	case offsetof(struct user32, u_debugreg[2]): 
		*val = child->thread.debugreg2; 
		break; 
	case offsetof(struct user32, u_debugreg[3]): 
		*val = child->thread.debugreg3; 
		break; 
	case offsetof(struct user32, u_debugreg[6]): 
		*val = child->thread.debugreg6; 
		break; 
	case offsetof(struct user32, u_debugreg[7]): 
		*val = child->thread.debugreg7; 
		break; 
		    
	default:
		if (regno > sizeof(struct user32) || (regno & 3))
			return -EIO;

		/* Other dummy fields in the virtual user structure are ignored */ 
		*val = 0;
		break; 		
	}
	return 0;
}

#undef R32

static long ptrace32_siginfo(unsigned request, u32 pid, u32 addr, u32 data)
{
	int ret;
	compat_siginfo_t __user *si32 = compat_ptr(data);
	siginfo_t ssi; 
	siginfo_t __user *si = compat_alloc_user_space(sizeof(siginfo_t));
	if (request == PTRACE_SETSIGINFO) {
		memset(&ssi, 0, sizeof(siginfo_t));
		ret = copy_siginfo_from_user32(&ssi, si32);
		if (ret)
			return ret;
		if (copy_to_user(si, &ssi, sizeof(siginfo_t)))
			return -EFAULT;
	}
	ret = sys_ptrace(request, pid, addr, (unsigned long)si);
	if (ret)
		return ret;
	if (request == PTRACE_GETSIGINFO) {
		if (copy_from_user(&ssi, si, sizeof(siginfo_t)))
			return -EFAULT;
		ret = copy_siginfo_to_user32(si32, &ssi);
	}
	return ret;
}

asmlinkage long sys32_ptrace(long request, u32 pid, u32 addr, u32 data)
{
	struct task_struct *child;
	struct pt_regs *childregs; 
	void __user *datap = compat_ptr(data);
	int ret;
	__u32 val;

	switch (request) { 
	case PTRACE_TRACEME:
	case PTRACE_ATTACH:
	case PTRACE_KILL:
	case PTRACE_CONT:
	case PTRACE_SINGLESTEP:
	case PTRACE_DETACH:
	case PTRACE_SYSCALL:
	case PTRACE_SETOPTIONS:
		return sys_ptrace(request, pid, addr, data); 

	default:
		return -EINVAL;

	case PTRACE_PEEKTEXT:
	case PTRACE_PEEKDATA:
	case PTRACE_POKEDATA:
	case PTRACE_POKETEXT:
	case PTRACE_POKEUSR:       
	case PTRACE_PEEKUSR:
	case PTRACE_GETREGS:
	case PTRACE_SETREGS:
	case PTRACE_SETFPREGS:
	case PTRACE_GETFPREGS:
	case PTRACE_SETFPXREGS:
	case PTRACE_GETFPXREGS:
	case PTRACE_GETEVENTMSG:
		break;

	case PTRACE_SETSIGINFO:
	case PTRACE_GETSIGINFO:
		return ptrace32_siginfo(request, pid, addr, data);
	}

	child = ptrace_get_task_struct(pid);
	if (IS_ERR(child))
		return PTR_ERR(child);

	ret = ptrace_check_attach(child, request == PTRACE_KILL);
	if (ret < 0)
		goto out;

	childregs = task_pt_regs(child);

	switch (request) {
	case PTRACE_PEEKDATA:
	case PTRACE_PEEKTEXT:
		ret = 0;
		if (access_process_vm(child, addr, &val, sizeof(u32), 0)!=sizeof(u32))
			ret = -EIO;
		else
			ret = put_user(val, (unsigned int __user *)datap); 
		break; 

	case PTRACE_POKEDATA:
	case PTRACE_POKETEXT:
		ret = 0;
		if (access_process_vm(child, addr, &data, sizeof(u32), 1)!=sizeof(u32))
			ret = -EIO; 
		break;

	case PTRACE_PEEKUSR:
		ret = getreg32(child, addr, &val);
		if (ret == 0)
			ret = put_user(val, (__u32 __user *)datap);
		break;

	case PTRACE_POKEUSR:
		ret = putreg32(child, addr, data);
		break;

	case PTRACE_GETREGS: { /* Get all gp regs from the child. */
		int i;
	  	if (!access_ok(VERIFY_WRITE, datap, 16*4)) {
			ret = -EIO;
			break;
		}
		ret = 0;
		for ( i = 0; i <= 16*4 ; i += sizeof(__u32) ) {
			getreg32(child, i, &val);
			ret |= __put_user(val,(u32 __user *)datap);
			datap += sizeof(u32);
		}
		break;
	}

	case PTRACE_SETREGS: { /* Set all gp regs in the child. */
		unsigned long tmp;
		int i;
	  	if (!access_ok(VERIFY_READ, datap, 16*4)) {
			ret = -EIO;
			break;
		}
		ret = 0; 
		for ( i = 0; i <= 16*4; i += sizeof(u32) ) {
			ret |= __get_user(tmp, (u32 __user *)datap);
			putreg32(child, i, tmp);
			datap += sizeof(u32);
		}
		break;
	}

	case PTRACE_GETFPREGS:
		ret = -EIO; 
		if (!access_ok(VERIFY_READ, compat_ptr(data), 
			       sizeof(struct user_i387_struct)))
			break;
		save_i387_ia32(child, datap, childregs, 1);
		ret = 0; 
			break;

	case PTRACE_SETFPREGS:
		ret = -EIO;
		if (!access_ok(VERIFY_WRITE, datap, 
			       sizeof(struct user_i387_struct)))
			break;
		ret = 0;
		/* don't check EFAULT to be bug-to-bug compatible to i386 */
		restore_i387_ia32(child, datap, 1);
		break;

	case PTRACE_GETFPXREGS: { 
		struct user32_fxsr_struct __user *u = datap;
		init_fpu(child); 
		ret = -EIO;
		if (!access_ok(VERIFY_WRITE, u, sizeof(*u)))
			break;
			ret = -EFAULT;
		if (__copy_to_user(u, &child->thread.i387.fxsave, sizeof(*u)))
			break;
		ret = __put_user(childregs->cs, &u->fcs);
		ret |= __put_user(child->thread.ds, &u->fos); 
		break; 
	} 
	case PTRACE_SETFPXREGS: { 
		struct user32_fxsr_struct __user *u = datap;
		unlazy_fpu(child);
		ret = -EIO;
		if (!access_ok(VERIFY_READ, u, sizeof(*u)))
			break;
		/* no checking to be bug-to-bug compatible with i386. */
		/* but silence warning */
		if (__copy_from_user(&child->thread.i387.fxsave, u, sizeof(*u)))
			;
		set_stopped_child_used_math(child);
		child->thread.i387.fxsave.mxcsr &= mxcsr_feature_mask;
		ret = 0; 
		break;
	}

	case PTRACE_GETEVENTMSG:
		ret = put_user(child->ptrace_message,(unsigned int __user *)compat_ptr(data));
		break;

	default:
		BUG();
	}

 out:
	put_task_struct(child);
	return ret;
}

