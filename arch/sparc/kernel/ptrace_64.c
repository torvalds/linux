/* ptrace.c: Sparc process tracing support.
 *
 * Copyright (C) 1996, 2008 David S. Miller (davem@davemloft.net)
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
#include <linux/security.h>
#include <linux/seccomp.h>
#include <linux/audit.h>
#include <linux/signal.h>
#include <linux/regset.h>
#include <linux/tracehook.h>
#include <trace/syscall.h>
#include <linux/compat.h>
#include <linux/elf.h>

#include <asm/asi.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/psrcompat.h>
#include <asm/visasm.h>
#include <asm/spitfire.h>
#include <asm/page.h>
#include <asm/cpudata.h>
#include <asm/cacheflush.h>

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

#include "entry.h"

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

	if (tlb_type == hypervisor)
		return;

	preempt_disable();

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

	preempt_enable();
}

static int get_from_target(struct task_struct *target, unsigned long uaddr,
			   void *kbuf, int len)
{
	if (target == current) {
		if (copy_from_user(kbuf, (void __user *) uaddr, len))
			return -EFAULT;
	} else {
		int len2 = access_process_vm(target, uaddr, kbuf, len, 0);
		if (len2 != len)
			return -EFAULT;
	}
	return 0;
}

static int set_to_target(struct task_struct *target, unsigned long uaddr,
			 void *kbuf, int len)
{
	if (target == current) {
		if (copy_to_user((void __user *) uaddr, kbuf, len))
			return -EFAULT;
	} else {
		int len2 = access_process_vm(target, uaddr, kbuf, len, 1);
		if (len2 != len)
			return -EFAULT;
	}
	return 0;
}

static int regwindow64_get(struct task_struct *target,
			   const struct pt_regs *regs,
			   struct reg_window *wbuf)
{
	unsigned long rw_addr = regs->u_regs[UREG_I6];

	if (test_tsk_thread_flag(current, TIF_32BIT)) {
		struct reg_window32 win32;
		int i;

		if (get_from_target(target, rw_addr, &win32, sizeof(win32)))
			return -EFAULT;
		for (i = 0; i < 8; i++)
			wbuf->locals[i] = win32.locals[i];
		for (i = 0; i < 8; i++)
			wbuf->ins[i] = win32.ins[i];
	} else {
		rw_addr += STACK_BIAS;
		if (get_from_target(target, rw_addr, wbuf, sizeof(*wbuf)))
			return -EFAULT;
	}

	return 0;
}

static int regwindow64_set(struct task_struct *target,
			   const struct pt_regs *regs,
			   struct reg_window *wbuf)
{
	unsigned long rw_addr = regs->u_regs[UREG_I6];

	if (test_tsk_thread_flag(current, TIF_32BIT)) {
		struct reg_window32 win32;
		int i;

		for (i = 0; i < 8; i++)
			win32.locals[i] = wbuf->locals[i];
		for (i = 0; i < 8; i++)
			win32.ins[i] = wbuf->ins[i];

		if (set_to_target(target, rw_addr, &win32, sizeof(win32)))
			return -EFAULT;
	} else {
		rw_addr += STACK_BIAS;
		if (set_to_target(target, rw_addr, wbuf, sizeof(*wbuf)))
			return -EFAULT;
	}

	return 0;
}

enum sparc_regset {
	REGSET_GENERAL,
	REGSET_FP,
};

static int genregs64_get(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 void *kbuf, void __user *ubuf)
{
	const struct pt_regs *regs = task_pt_regs(target);
	int ret;

	if (target == current)
		flushw_user();

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  regs->u_regs,
				  0, 16 * sizeof(u64));
	if (!ret && count && pos < (32 * sizeof(u64))) {
		struct reg_window window;

		if (regwindow64_get(target, regs, &window))
			return -EFAULT;
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  &window,
					  16 * sizeof(u64),
					  32 * sizeof(u64));
	}

	if (!ret) {
		/* TSTATE, TPC, TNPC */
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  &regs->tstate,
					  32 * sizeof(u64),
					  35 * sizeof(u64));
	}

	if (!ret) {
		unsigned long y = regs->y;

		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  &y,
					  35 * sizeof(u64),
					  36 * sizeof(u64));
	}

	if (!ret) {
		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					       36 * sizeof(u64), -1);

	}
	return ret;
}

static int genregs64_set(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = task_pt_regs(target);
	int ret;

	if (target == current)
		flushw_user();

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 regs->u_regs,
				 0, 16 * sizeof(u64));
	if (!ret && count && pos < (32 * sizeof(u64))) {
		struct reg_window window;

		if (regwindow64_get(target, regs, &window))
			return -EFAULT;

		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &window,
					 16 * sizeof(u64),
					 32 * sizeof(u64));

		if (!ret &&
		    regwindow64_set(target, regs, &window))
			return -EFAULT;
	}

	if (!ret && count > 0) {
		unsigned long tstate;

		/* TSTATE */
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &tstate,
					 32 * sizeof(u64),
					 33 * sizeof(u64));
		if (!ret) {
			/* Only the condition codes and the "in syscall"
			 * state can be modified in the %tstate register.
			 */
			tstate &= (TSTATE_ICC | TSTATE_XCC | TSTATE_SYSCALL);
			regs->tstate &= ~(TSTATE_ICC | TSTATE_XCC | TSTATE_SYSCALL);
			regs->tstate |= tstate;
		}
	}

	if (!ret) {
		/* TPC, TNPC */
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &regs->tpc,
					 33 * sizeof(u64),
					 35 * sizeof(u64));
	}

	if (!ret) {
		unsigned long y;

		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &y,
					 35 * sizeof(u64),
					 36 * sizeof(u64));
		if (!ret)
			regs->y = y;
	}

	if (!ret)
		ret = user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
						36 * sizeof(u64), -1);

	return ret;
}

static int fpregs64_get(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			void *kbuf, void __user *ubuf)
{
	const unsigned long *fpregs = task_thread_info(target)->fpregs;
	unsigned long fprs, fsr, gsr;
	int ret;

	if (target == current)
		save_and_clear_fpu();

	fprs = task_thread_info(target)->fpsaved[0];

	if (fprs & FPRS_DL)
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  fpregs,
					  0, 16 * sizeof(u64));
	else
		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					       0,
					       16 * sizeof(u64));

	if (!ret) {
		if (fprs & FPRS_DU)
			ret = user_regset_copyout(&pos, &count,
						  &kbuf, &ubuf,
						  fpregs + 16,
						  16 * sizeof(u64),
						  32 * sizeof(u64));
		else
			ret = user_regset_copyout_zero(&pos, &count,
						       &kbuf, &ubuf,
						       16 * sizeof(u64),
						       32 * sizeof(u64));
	}

	if (fprs & FPRS_FEF) {
		fsr = task_thread_info(target)->xfsr[0];
		gsr = task_thread_info(target)->gsr[0];
	} else {
		fsr = gsr = 0;
	}

	if (!ret)
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  &fsr,
					  32 * sizeof(u64),
					  33 * sizeof(u64));
	if (!ret)
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  &gsr,
					  33 * sizeof(u64),
					  34 * sizeof(u64));
	if (!ret)
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  &fprs,
					  34 * sizeof(u64),
					  35 * sizeof(u64));

	if (!ret)
		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					       35 * sizeof(u64), -1);

	return ret;
}

static int fpregs64_set(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			const void *kbuf, const void __user *ubuf)
{
	unsigned long *fpregs = task_thread_info(target)->fpregs;
	unsigned long fprs;
	int ret;

	if (target == current)
		save_and_clear_fpu();

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 fpregs,
				 0, 32 * sizeof(u64));
	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 task_thread_info(target)->xfsr,
					 32 * sizeof(u64),
					 33 * sizeof(u64));
	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 task_thread_info(target)->gsr,
					 33 * sizeof(u64),
					 34 * sizeof(u64));

	fprs = task_thread_info(target)->fpsaved[0];
	if (!ret && count > 0) {
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &fprs,
					 34 * sizeof(u64),
					 35 * sizeof(u64));
	}

	fprs |= (FPRS_FEF | FPRS_DL | FPRS_DU);
	task_thread_info(target)->fpsaved[0] = fprs;

	if (!ret)
		ret = user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
						35 * sizeof(u64), -1);
	return ret;
}

static const struct user_regset sparc64_regsets[] = {
	/* Format is:
	 * 	G0 --> G7
	 *	O0 --> O7
	 *	L0 --> L7
	 *	I0 --> I7
	 *	TSTATE, TPC, TNPC, Y
	 */
	[REGSET_GENERAL] = {
		.core_note_type = NT_PRSTATUS,
		.n = 36,
		.size = sizeof(u64), .align = sizeof(u64),
		.get = genregs64_get, .set = genregs64_set
	},
	/* Format is:
	 *	F0 --> F63
	 *	FSR
	 *	GSR
	 *	FPRS
	 */
	[REGSET_FP] = {
		.core_note_type = NT_PRFPREG,
		.n = 35,
		.size = sizeof(u64), .align = sizeof(u64),
		.get = fpregs64_get, .set = fpregs64_set
	},
};

static const struct user_regset_view user_sparc64_view = {
	.name = "sparc64", .e_machine = EM_SPARCV9,
	.regsets = sparc64_regsets, .n = ARRAY_SIZE(sparc64_regsets)
};

#ifdef CONFIG_COMPAT
static int genregs32_get(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 void *kbuf, void __user *ubuf)
{
	const struct pt_regs *regs = task_pt_regs(target);
	compat_ulong_t __user *reg_window;
	compat_ulong_t *k = kbuf;
	compat_ulong_t __user *u = ubuf;
	compat_ulong_t reg;

	if (target == current)
		flushw_user();

	pos /= sizeof(reg);
	count /= sizeof(reg);

	if (kbuf) {
		for (; count > 0 && pos < 16; count--)
			*k++ = regs->u_regs[pos++];

		reg_window = (compat_ulong_t __user *) regs->u_regs[UREG_I6];
		reg_window -= 16;
		if (target == current) {
			for (; count > 0 && pos < 32; count--) {
				if (get_user(*k++, &reg_window[pos++]))
					return -EFAULT;
			}
		} else {
			for (; count > 0 && pos < 32; count--) {
				if (access_process_vm(target,
						      (unsigned long)
						      &reg_window[pos],
						      k, sizeof(*k), 0)
				    != sizeof(*k))
					return -EFAULT;
				k++;
				pos++;
			}
		}
	} else {
		for (; count > 0 && pos < 16; count--) {
			if (put_user((compat_ulong_t) regs->u_regs[pos++], u++))
				return -EFAULT;
		}

		reg_window = (compat_ulong_t __user *) regs->u_regs[UREG_I6];
		reg_window -= 16;
		if (target == current) {
			for (; count > 0 && pos < 32; count--) {
				if (get_user(reg, &reg_window[pos++]) ||
				    put_user(reg, u++))
					return -EFAULT;
			}
		} else {
			for (; count > 0 && pos < 32; count--) {
				if (access_process_vm(target,
						      (unsigned long)
						      &reg_window[pos],
						      &reg, sizeof(reg), 0)
				    != sizeof(reg))
					return -EFAULT;
				if (access_process_vm(target,
						      (unsigned long) u,
						      &reg, sizeof(reg), 1)
				    != sizeof(reg))
					return -EFAULT;
				pos++;
				u++;
			}
		}
	}
	while (count > 0) {
		switch (pos) {
		case 32: /* PSR */
			reg = tstate_to_psr(regs->tstate);
			break;
		case 33: /* PC */
			reg = regs->tpc;
			break;
		case 34: /* NPC */
			reg = regs->tnpc;
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
	struct pt_regs *regs = task_pt_regs(target);
	compat_ulong_t __user *reg_window;
	const compat_ulong_t *k = kbuf;
	const compat_ulong_t __user *u = ubuf;
	compat_ulong_t reg;

	if (target == current)
		flushw_user();

	pos /= sizeof(reg);
	count /= sizeof(reg);

	if (kbuf) {
		for (; count > 0 && pos < 16; count--)
			regs->u_regs[pos++] = *k++;

		reg_window = (compat_ulong_t __user *) regs->u_regs[UREG_I6];
		reg_window -= 16;
		if (target == current) {
			for (; count > 0 && pos < 32; count--) {
				if (put_user(*k++, &reg_window[pos++]))
					return -EFAULT;
			}
		} else {
			for (; count > 0 && pos < 32; count--) {
				if (access_process_vm(target,
						      (unsigned long)
						      &reg_window[pos],
						      (void *) k,
						      sizeof(*k), 1)
				    != sizeof(*k))
					return -EFAULT;
				k++;
				pos++;
			}
		}
	} else {
		for (; count > 0 && pos < 16; count--) {
			if (get_user(reg, u++))
				return -EFAULT;
			regs->u_regs[pos++] = reg;
		}

		reg_window = (compat_ulong_t __user *) regs->u_regs[UREG_I6];
		reg_window -= 16;
		if (target == current) {
			for (; count > 0 && pos < 32; count--) {
				if (get_user(reg, u++) ||
				    put_user(reg, &reg_window[pos++]))
					return -EFAULT;
			}
		} else {
			for (; count > 0 && pos < 32; count--) {
				if (access_process_vm(target,
						      (unsigned long)
						      u,
						      &reg, sizeof(reg), 0)
				    != sizeof(reg))
					return -EFAULT;
				if (access_process_vm(target,
						      (unsigned long)
						      &reg_window[pos],
						      &reg, sizeof(reg), 1)
				    != sizeof(reg))
					return -EFAULT;
				pos++;
				u++;
			}
		}
	}
	while (count > 0) {
		unsigned long tstate;

		if (kbuf)
			reg = *k++;
		else if (get_user(reg, u++))
			return -EFAULT;

		switch (pos) {
		case 32: /* PSR */
			tstate = regs->tstate;
			tstate &= ~(TSTATE_ICC | TSTATE_XCC | TSTATE_SYSCALL);
			tstate |= psr_to_tstate_icc(reg);
			if (reg & PSR_SYSCALL)
				tstate |= TSTATE_SYSCALL;
			regs->tstate = tstate;
			break;
		case 33: /* PC */
			regs->tpc = reg;
			break;
		case 34: /* NPC */
			regs->tnpc = reg;
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
	const unsigned long *fpregs = task_thread_info(target)->fpregs;
	compat_ulong_t enabled;
	unsigned long fprs;
	compat_ulong_t fsr;
	int ret = 0;

	if (target == current)
		save_and_clear_fpu();

	fprs = task_thread_info(target)->fpsaved[0];
	if (fprs & FPRS_FEF) {
		fsr = task_thread_info(target)->xfsr[0];
		enabled = 1;
	} else {
		fsr = 0;
		enabled = 0;
	}

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  fpregs,
				  0, 32 * sizeof(u32));

	if (!ret)
		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					       32 * sizeof(u32),
					       33 * sizeof(u32));
	if (!ret)
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  &fsr,
					  33 * sizeof(u32),
					  34 * sizeof(u32));

	if (!ret) {
		compat_ulong_t val;

		val = (enabled << 8) | (8 << 16);
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
	unsigned long *fpregs = task_thread_info(target)->fpregs;
	unsigned long fprs;
	int ret;

	if (target == current)
		save_and_clear_fpu();

	fprs = task_thread_info(target)->fpsaved[0];

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 fpregs,
				 0, 32 * sizeof(u32));
	if (!ret)
		user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					  32 * sizeof(u32),
					  33 * sizeof(u32));
	if (!ret && count > 0) {
		compat_ulong_t fsr;
		unsigned long val;

		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &fsr,
					 33 * sizeof(u32),
					 34 * sizeof(u32));
		if (!ret) {
			val = task_thread_info(target)->xfsr[0];
			val &= 0xffffffff00000000UL;
			val |= fsr;
			task_thread_info(target)->xfsr[0] = val;
		}
	}

	fprs |= (FPRS_FEF | FPRS_DL);
	task_thread_info(target)->fpsaved[0] = fprs;

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
		.n = 38,
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
		.n = 99,
		.size = sizeof(u32), .align = sizeof(u32),
		.get = fpregs32_get, .set = fpregs32_set
	},
};

static const struct user_regset_view user_sparc32_view = {
	.name = "sparc", .e_machine = EM_SPARC,
	.regsets = sparc32_regsets, .n = ARRAY_SIZE(sparc32_regsets)
};
#endif /* CONFIG_COMPAT */

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
#ifdef CONFIG_COMPAT
	if (test_tsk_thread_flag(task, TIF_32BIT))
		return &user_sparc32_view;
#endif
	return &user_sparc64_view;
}

#ifdef CONFIG_COMPAT
struct compat_fps {
	unsigned int regs[32];
	unsigned int fsr;
	unsigned int flags;
	unsigned int extra;
	unsigned int fpqd;
	struct compat_fq {
		unsigned int insnaddr;
		unsigned int insn;
	} fpq[16];
};

long compat_arch_ptrace(struct task_struct *child, compat_long_t request,
			compat_ulong_t caddr, compat_ulong_t cdata)
{
	const struct user_regset_view *view = task_user_regset_view(current);
	compat_ulong_t caddr2 = task_pt_regs(current)->u_regs[UREG_I4];
	struct pt_regs32 __user *pregs;
	struct compat_fps __user *fps;
	unsigned long addr2 = caddr2;
	unsigned long addr = caddr;
	unsigned long data = cdata;
	int ret;

	pregs = (struct pt_regs32 __user *) addr;
	fps = (struct compat_fps __user *) addr;

	switch (request) {
	case PTRACE_PEEKUSR:
		ret = (addr != 0) ? -EIO : 0;
		break;

	case PTRACE_GETREGS:
		ret = copy_regset_to_user(child, view, REGSET_GENERAL,
					  32 * sizeof(u32),
					  4 * sizeof(u32),
					  &pregs->psr);
		if (!ret)
			ret = copy_regset_to_user(child, view, REGSET_GENERAL,
						  1 * sizeof(u32),
						  15 * sizeof(u32),
						  &pregs->u_regs[0]);
		break;

	case PTRACE_SETREGS:
		ret = copy_regset_from_user(child, view, REGSET_GENERAL,
					    32 * sizeof(u32),
					    4 * sizeof(u32),
					    &pregs->psr);
		if (!ret)
			ret = copy_regset_from_user(child, view, REGSET_GENERAL,
						    1 * sizeof(u32),
						    15 * sizeof(u32),
						    &pregs->u_regs[0]);
		break;

	case PTRACE_GETFPREGS:
		ret = copy_regset_to_user(child, view, REGSET_FP,
					  0 * sizeof(u32),
					  32 * sizeof(u32),
					  &fps->regs[0]);
		if (!ret)
			ret = copy_regset_to_user(child, view, REGSET_FP,
						  33 * sizeof(u32),
						  1 * sizeof(u32),
						  &fps->fsr);
		if (!ret) {
			if (__put_user(0, &fps->flags) ||
			    __put_user(0, &fps->extra) ||
			    __put_user(0, &fps->fpqd) ||
			    clear_user(&fps->fpq[0], 32 * sizeof(unsigned int)))
				ret = -EFAULT;
		}
		break;

	case PTRACE_SETFPREGS:
		ret = copy_regset_from_user(child, view, REGSET_FP,
					    0 * sizeof(u32),
					    32 * sizeof(u32),
					    &fps->regs[0]);
		if (!ret)
			ret = copy_regset_from_user(child, view, REGSET_FP,
						    33 * sizeof(u32),
						    1 * sizeof(u32),
						    &fps->fsr);
		break;

	case PTRACE_READTEXT:
	case PTRACE_READDATA:
		ret = ptrace_readdata(child, addr,
				      (char __user *)addr2, data);
		if (ret == data)
			ret = 0;
		else if (ret >= 0)
			ret = -EIO;
		break;

	case PTRACE_WRITETEXT:
	case PTRACE_WRITEDATA:
		ret = ptrace_writedata(child, (char __user *) addr2,
				       addr, data);
		if (ret == data)
			ret = 0;
		else if (ret >= 0)
			ret = -EIO;
		break;

	default:
		if (request == PTRACE_SPARC_DETACH)
			request = PTRACE_DETACH;
		ret = compat_ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}
#endif /* CONFIG_COMPAT */

struct fps {
	unsigned int regs[64];
	unsigned long fsr;
};

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	const struct user_regset_view *view = task_user_regset_view(current);
	unsigned long addr2 = task_pt_regs(current)->u_regs[UREG_I4];
	struct pt_regs __user *pregs;
	struct fps __user *fps;
	void __user *addr2p;
	int ret;

	pregs = (struct pt_regs __user *) addr;
	fps = (struct fps __user *) addr;
	addr2p = (void __user *) addr2;

	switch (request) {
	case PTRACE_PEEKUSR:
		ret = (addr != 0) ? -EIO : 0;
		break;

	case PTRACE_GETREGS64:
		ret = copy_regset_to_user(child, view, REGSET_GENERAL,
					  1 * sizeof(u64),
					  15 * sizeof(u64),
					  &pregs->u_regs[0]);
		if (!ret) {
			/* XXX doesn't handle 'y' register correctly XXX */
			ret = copy_regset_to_user(child, view, REGSET_GENERAL,
						  32 * sizeof(u64),
						  4 * sizeof(u64),
						  &pregs->tstate);
		}
		break;

	case PTRACE_SETREGS64:
		ret = copy_regset_from_user(child, view, REGSET_GENERAL,
					    1 * sizeof(u64),
					    15 * sizeof(u64),
					    &pregs->u_regs[0]);
		if (!ret) {
			/* XXX doesn't handle 'y' register correctly XXX */
			ret = copy_regset_from_user(child, view, REGSET_GENERAL,
						    32 * sizeof(u64),
						    4 * sizeof(u64),
						    &pregs->tstate);
		}
		break;

	case PTRACE_GETFPREGS64:
		ret = copy_regset_to_user(child, view, REGSET_FP,
					  0 * sizeof(u64),
					  33 * sizeof(u64),
					  fps);
		break;

	case PTRACE_SETFPREGS64:
		ret = copy_regset_from_user(child, view, REGSET_FP,
					  0 * sizeof(u64),
					  33 * sizeof(u64),
					  fps);
		break;

	case PTRACE_READTEXT:
	case PTRACE_READDATA:
		ret = ptrace_readdata(child, addr, addr2p, data);
		if (ret == data)
			ret = 0;
		else if (ret >= 0)
			ret = -EIO;
		break;

	case PTRACE_WRITETEXT:
	case PTRACE_WRITEDATA:
		ret = ptrace_writedata(child, addr2p, addr, data);
		if (ret == data)
			ret = 0;
		else if (ret >= 0)
			ret = -EIO;
		break;

	default:
		if (request == PTRACE_SPARC_DETACH)
			request = PTRACE_DETACH;
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

asmlinkage int syscall_trace_enter(struct pt_regs *regs)
{
	int ret = 0;

	/* do the secure computing check first */
	secure_computing(regs->u_regs[UREG_G1]);

	if (test_thread_flag(TIF_SYSCALL_TRACE))
		ret = tracehook_report_syscall_entry(regs);

	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_enter(regs, regs->u_regs[UREG_G1]);

	audit_syscall_entry((test_thread_flag(TIF_32BIT) ?
			     AUDIT_ARCH_SPARC :
			     AUDIT_ARCH_SPARC64),
			    regs->u_regs[UREG_G1],
			    regs->u_regs[UREG_I0],
			    regs->u_regs[UREG_I1],
			    regs->u_regs[UREG_I2],
			    regs->u_regs[UREG_I3]);

	return ret;
}

asmlinkage void syscall_trace_leave(struct pt_regs *regs)
{
	audit_syscall_exit(regs);

	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_exit(regs, regs->u_regs[UREG_G1]);

	if (test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, 0);
}
