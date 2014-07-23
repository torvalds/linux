/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 Ross Biro
 * Copyright (C) Linus Torvalds
 * Copyright (C) 1994, 95, 96, 97, 98, 2000 Ralf Baechle
 * Copyright (C) 1996 David S. Miller
 * Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999 MIPS Technologies, Inc.
 * Copyright (C) 2000 Ulf Carlsson
 *
 * At this time Linux/MIPS64 only supports syscall tracing, even for 32-bit
 * binaries.
 */
#include <linux/compiler.h>
#include <linux/context_tracking.h>
#include <linux/elf.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/regset.h>
#include <linux/smp.h>
#include <linux/user.h>
#include <linux/security.h>
#include <linux/tracehook.h>
#include <linux/audit.h>
#include <linux/seccomp.h>
#include <linux/ftrace.h>

#include <asm/byteorder.h>
#include <asm/cpu.h>
#include <asm/dsp.h>
#include <asm/fpu.h>
#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/syscall.h>
#include <asm/uaccess.h>
#include <asm/bootinfo.h>
#include <asm/reg.h>

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *child)
{
	/* Don't load the watchpoint registers for the ex-child. */
	clear_tsk_thread_flag(child, TIF_LOAD_WATCH);
}

/*
 * Read a general register set.	 We always use the 64-bit format, even
 * for 32-bit kernels and for 32-bit processes on a 64-bit kernel.
 * Registers are sign extended to fill the available space.
 */
int ptrace_getregs(struct task_struct *child, __s64 __user *data)
{
	struct pt_regs *regs;
	int i;

	if (!access_ok(VERIFY_WRITE, data, 38 * 8))
		return -EIO;

	regs = task_pt_regs(child);

	for (i = 0; i < 32; i++)
		__put_user((long)regs->regs[i], data + i);
	__put_user((long)regs->lo, data + EF_LO - EF_R0);
	__put_user((long)regs->hi, data + EF_HI - EF_R0);
	__put_user((long)regs->cp0_epc, data + EF_CP0_EPC - EF_R0);
	__put_user((long)regs->cp0_badvaddr, data + EF_CP0_BADVADDR - EF_R0);
	__put_user((long)regs->cp0_status, data + EF_CP0_STATUS - EF_R0);
	__put_user((long)regs->cp0_cause, data + EF_CP0_CAUSE - EF_R0);

	return 0;
}

/*
 * Write a general register set.  As for PTRACE_GETREGS, we always use
 * the 64-bit format.  On a 32-bit kernel only the lower order half
 * (according to endianness) will be used.
 */
int ptrace_setregs(struct task_struct *child, __s64 __user *data)
{
	struct pt_regs *regs;
	int i;

	if (!access_ok(VERIFY_READ, data, 38 * 8))
		return -EIO;

	regs = task_pt_regs(child);

	for (i = 0; i < 32; i++)
		__get_user(regs->regs[i], data + i);
	__get_user(regs->lo, data + EF_LO - EF_R0);
	__get_user(regs->hi, data + EF_HI - EF_R0);
	__get_user(regs->cp0_epc, data + EF_CP0_EPC - EF_R0);

	/* badvaddr, status, and cause may not be written.  */

	return 0;
}

int ptrace_getfpregs(struct task_struct *child, __u32 __user *data)
{
	int i;

	if (!access_ok(VERIFY_WRITE, data, 33 * 8))
		return -EIO;

	if (tsk_used_math(child)) {
		union fpureg *fregs = get_fpu_regs(child);
		for (i = 0; i < 32; i++)
			__put_user(get_fpr64(&fregs[i], 0),
				   i + (__u64 __user *)data);
	} else {
		for (i = 0; i < 32; i++)
			__put_user((__u64) -1, i + (__u64 __user *) data);
	}

	__put_user(child->thread.fpu.fcr31, data + 64);
	__put_user(boot_cpu_data.fpu_id, data + 65);

	return 0;
}

int ptrace_setfpregs(struct task_struct *child, __u32 __user *data)
{
	union fpureg *fregs;
	u64 fpr_val;
	int i;

	if (!access_ok(VERIFY_READ, data, 33 * 8))
		return -EIO;

	fregs = get_fpu_regs(child);

	for (i = 0; i < 32; i++) {
		__get_user(fpr_val, i + (__u64 __user *)data);
		set_fpr64(&fregs[i], 0, fpr_val);
	}

	__get_user(child->thread.fpu.fcr31, data + 64);

	/* FIR may not be written.  */

	return 0;
}

int ptrace_get_watch_regs(struct task_struct *child,
			  struct pt_watch_regs __user *addr)
{
	enum pt_watch_style style;
	int i;

	if (!cpu_has_watch || boot_cpu_data.watch_reg_use_cnt == 0)
		return -EIO;
	if (!access_ok(VERIFY_WRITE, addr, sizeof(struct pt_watch_regs)))
		return -EIO;

#ifdef CONFIG_32BIT
	style = pt_watch_style_mips32;
#define WATCH_STYLE mips32
#else
	style = pt_watch_style_mips64;
#define WATCH_STYLE mips64
#endif

	__put_user(style, &addr->style);
	__put_user(boot_cpu_data.watch_reg_use_cnt,
		   &addr->WATCH_STYLE.num_valid);
	for (i = 0; i < boot_cpu_data.watch_reg_use_cnt; i++) {
		__put_user(child->thread.watch.mips3264.watchlo[i],
			   &addr->WATCH_STYLE.watchlo[i]);
		__put_user(child->thread.watch.mips3264.watchhi[i] & 0xfff,
			   &addr->WATCH_STYLE.watchhi[i]);
		__put_user(boot_cpu_data.watch_reg_masks[i],
			   &addr->WATCH_STYLE.watch_masks[i]);
	}
	for (; i < 8; i++) {
		__put_user(0, &addr->WATCH_STYLE.watchlo[i]);
		__put_user(0, &addr->WATCH_STYLE.watchhi[i]);
		__put_user(0, &addr->WATCH_STYLE.watch_masks[i]);
	}

	return 0;
}

int ptrace_set_watch_regs(struct task_struct *child,
			  struct pt_watch_regs __user *addr)
{
	int i;
	int watch_active = 0;
	unsigned long lt[NUM_WATCH_REGS];
	u16 ht[NUM_WATCH_REGS];

	if (!cpu_has_watch || boot_cpu_data.watch_reg_use_cnt == 0)
		return -EIO;
	if (!access_ok(VERIFY_READ, addr, sizeof(struct pt_watch_regs)))
		return -EIO;
	/* Check the values. */
	for (i = 0; i < boot_cpu_data.watch_reg_use_cnt; i++) {
		__get_user(lt[i], &addr->WATCH_STYLE.watchlo[i]);
#ifdef CONFIG_32BIT
		if (lt[i] & __UA_LIMIT)
			return -EINVAL;
#else
		if (test_tsk_thread_flag(child, TIF_32BIT_ADDR)) {
			if (lt[i] & 0xffffffff80000000UL)
				return -EINVAL;
		} else {
			if (lt[i] & __UA_LIMIT)
				return -EINVAL;
		}
#endif
		__get_user(ht[i], &addr->WATCH_STYLE.watchhi[i]);
		if (ht[i] & ~0xff8)
			return -EINVAL;
	}
	/* Install them. */
	for (i = 0; i < boot_cpu_data.watch_reg_use_cnt; i++) {
		if (lt[i] & 7)
			watch_active = 1;
		child->thread.watch.mips3264.watchlo[i] = lt[i];
		/* Set the G bit. */
		child->thread.watch.mips3264.watchhi[i] = ht[i];
	}

	if (watch_active)
		set_tsk_thread_flag(child, TIF_LOAD_WATCH);
	else
		clear_tsk_thread_flag(child, TIF_LOAD_WATCH);

	return 0;
}

/* regset get/set implementations */

static int gpr_get(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   void *kbuf, void __user *ubuf)
{
	struct pt_regs *regs = task_pt_regs(target);

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   regs, 0, sizeof(*regs));
}

static int gpr_set(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	struct pt_regs newregs;
	int ret;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &newregs,
				 0, sizeof(newregs));
	if (ret)
		return ret;

	*task_pt_regs(target) = newregs;

	return 0;
}

static int fpr_get(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   void *kbuf, void __user *ubuf)
{
	unsigned i;
	int err;
	u64 fpr_val;

	/* XXX fcr31  */

	if (sizeof(target->thread.fpu.fpr[i]) == sizeof(elf_fpreg_t))
		return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					   &target->thread.fpu,
					   0, sizeof(elf_fpregset_t));

	for (i = 0; i < NUM_FPU_REGS; i++) {
		fpr_val = get_fpr64(&target->thread.fpu.fpr[i], 0);
		err = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  &fpr_val, i * sizeof(elf_fpreg_t),
					  (i + 1) * sizeof(elf_fpreg_t));
		if (err)
			return err;
	}

	return 0;
}

static int fpr_set(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	unsigned i;
	int err;
	u64 fpr_val;

	/* XXX fcr31  */

	if (sizeof(target->thread.fpu.fpr[i]) == sizeof(elf_fpreg_t))
		return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					  &target->thread.fpu,
					  0, sizeof(elf_fpregset_t));

	for (i = 0; i < NUM_FPU_REGS; i++) {
		err = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &fpr_val, i * sizeof(elf_fpreg_t),
					 (i + 1) * sizeof(elf_fpreg_t));
		if (err)
			return err;
		set_fpr64(&target->thread.fpu.fpr[i], 0, fpr_val);
	}

	return 0;
}

enum mips_regset {
	REGSET_GPR,
	REGSET_FPR,
};

static const struct user_regset mips_regsets[] = {
	[REGSET_GPR] = {
		.core_note_type	= NT_PRSTATUS,
		.n		= ELF_NGREG,
		.size		= sizeof(unsigned int),
		.align		= sizeof(unsigned int),
		.get		= gpr_get,
		.set		= gpr_set,
	},
	[REGSET_FPR] = {
		.core_note_type	= NT_PRFPREG,
		.n		= ELF_NFPREG,
		.size		= sizeof(elf_fpreg_t),
		.align		= sizeof(elf_fpreg_t),
		.get		= fpr_get,
		.set		= fpr_set,
	},
};

static const struct user_regset_view user_mips_view = {
	.name		= "mips",
	.e_machine	= ELF_ARCH,
	.ei_osabi	= ELF_OSABI,
	.regsets	= mips_regsets,
	.n		= ARRAY_SIZE(mips_regsets),
};

static const struct user_regset mips64_regsets[] = {
	[REGSET_GPR] = {
		.core_note_type	= NT_PRSTATUS,
		.n		= ELF_NGREG,
		.size		= sizeof(unsigned long),
		.align		= sizeof(unsigned long),
		.get		= gpr_get,
		.set		= gpr_set,
	},
	[REGSET_FPR] = {
		.core_note_type	= NT_PRFPREG,
		.n		= ELF_NFPREG,
		.size		= sizeof(elf_fpreg_t),
		.align		= sizeof(elf_fpreg_t),
		.get		= fpr_get,
		.set		= fpr_set,
	},
};

static const struct user_regset_view user_mips64_view = {
	.name		= "mips",
	.e_machine	= ELF_ARCH,
	.ei_osabi	= ELF_OSABI,
	.regsets	= mips64_regsets,
	.n		= ARRAY_SIZE(mips_regsets),
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
#ifdef CONFIG_32BIT
	return &user_mips_view;
#endif

#ifdef CONFIG_MIPS32_O32
		if (test_tsk_thread_flag(task, TIF_32BIT_REGS))
			return &user_mips_view;
#endif

	return &user_mips64_view;
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	int ret;
	void __user *addrp = (void __user *) addr;
	void __user *datavp = (void __user *) data;
	unsigned long __user *datalp = (void __user *) data;

	switch (request) {
	/* when I and D space are separate, these will need to be fixed. */
	case PTRACE_PEEKTEXT: /* read word at location addr. */
	case PTRACE_PEEKDATA:
		ret = generic_ptrace_peekdata(child, addr, data);
		break;

	/* Read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		struct pt_regs *regs;
		union fpureg *fregs;
		unsigned long tmp = 0;

		regs = task_pt_regs(child);
		ret = 0;  /* Default return value. */

		switch (addr) {
		case 0 ... 31:
			tmp = regs->regs[addr];
			break;
		case FPR_BASE ... FPR_BASE + 31:
			if (!tsk_used_math(child)) {
				/* FP not yet used */
				tmp = -1;
				break;
			}
			fregs = get_fpu_regs(child);

#ifdef CONFIG_32BIT
			if (test_thread_flag(TIF_32BIT_FPREGS)) {
				/*
				 * The odd registers are actually the high
				 * order bits of the values stored in the even
				 * registers - unless we're using r2k_switch.S.
				 */
				tmp = get_fpr32(&fregs[(addr & ~1) - FPR_BASE],
						addr & 1);
				break;
			}
#endif
			tmp = get_fpr32(&fregs[addr - FPR_BASE], 0);
			break;
		case PC:
			tmp = regs->cp0_epc;
			break;
		case CAUSE:
			tmp = regs->cp0_cause;
			break;
		case BADVADDR:
			tmp = regs->cp0_badvaddr;
			break;
		case MMHI:
			tmp = regs->hi;
			break;
		case MMLO:
			tmp = regs->lo;
			break;
#ifdef CONFIG_CPU_HAS_SMARTMIPS
		case ACX:
			tmp = regs->acx;
			break;
#endif
		case FPC_CSR:
			tmp = child->thread.fpu.fcr31;
			break;
		case FPC_EIR:
			/* implementation / version register */
			tmp = boot_cpu_data.fpu_id;
			break;
		case DSP_BASE ... DSP_BASE + 5: {
			dspreg_t *dregs;

			if (!cpu_has_dsp) {
				tmp = 0;
				ret = -EIO;
				goto out;
			}
			dregs = __get_dsp_regs(child);
			tmp = (unsigned long) (dregs[addr - DSP_BASE]);
			break;
		}
		case DSP_CONTROL:
			if (!cpu_has_dsp) {
				tmp = 0;
				ret = -EIO;
				goto out;
			}
			tmp = child->thread.dsp.dspcontrol;
			break;
		default:
			tmp = 0;
			ret = -EIO;
			goto out;
		}
		ret = put_user(tmp, datalp);
		break;
	}

	/* when I and D space are separate, this will have to be fixed. */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = generic_ptrace_pokedata(child, addr, data);
		break;

	case PTRACE_POKEUSR: {
		struct pt_regs *regs;
		ret = 0;
		regs = task_pt_regs(child);

		switch (addr) {
		case 0 ... 31:
			regs->regs[addr] = data;
			break;
		case FPR_BASE ... FPR_BASE + 31: {
			union fpureg *fregs = get_fpu_regs(child);

			if (!tsk_used_math(child)) {
				/* FP not yet used  */
				memset(&child->thread.fpu, ~0,
				       sizeof(child->thread.fpu));
				child->thread.fpu.fcr31 = 0;
			}
#ifdef CONFIG_32BIT
			if (test_thread_flag(TIF_32BIT_FPREGS)) {
				/*
				 * The odd registers are actually the high
				 * order bits of the values stored in the even
				 * registers - unless we're using r2k_switch.S.
				 */
				set_fpr32(&fregs[(addr & ~1) - FPR_BASE],
					  addr & 1, data);
				break;
			}
#endif
			set_fpr64(&fregs[addr - FPR_BASE], 0, data);
			break;
		}
		case PC:
			regs->cp0_epc = data;
			break;
		case MMHI:
			regs->hi = data;
			break;
		case MMLO:
			regs->lo = data;
			break;
#ifdef CONFIG_CPU_HAS_SMARTMIPS
		case ACX:
			regs->acx = data;
			break;
#endif
		case FPC_CSR:
			child->thread.fpu.fcr31 = data;
			break;
		case DSP_BASE ... DSP_BASE + 5: {
			dspreg_t *dregs;

			if (!cpu_has_dsp) {
				ret = -EIO;
				break;
			}

			dregs = __get_dsp_regs(child);
			dregs[addr - DSP_BASE] = data;
			break;
		}
		case DSP_CONTROL:
			if (!cpu_has_dsp) {
				ret = -EIO;
				break;
			}
			child->thread.dsp.dspcontrol = data;
			break;
		default:
			/* The rest are not allowed. */
			ret = -EIO;
			break;
		}
		break;
		}

	case PTRACE_GETREGS:
		ret = ptrace_getregs(child, datavp);
		break;

	case PTRACE_SETREGS:
		ret = ptrace_setregs(child, datavp);
		break;

	case PTRACE_GETFPREGS:
		ret = ptrace_getfpregs(child, datavp);
		break;

	case PTRACE_SETFPREGS:
		ret = ptrace_setfpregs(child, datavp);
		break;

	case PTRACE_GET_THREAD_AREA:
		ret = put_user(task_thread_info(child)->tp_value, datalp);
		break;

	case PTRACE_GET_WATCH_REGS:
		ret = ptrace_get_watch_regs(child, addrp);
		break;

	case PTRACE_SET_WATCH_REGS:
		ret = ptrace_set_watch_regs(child, addrp);
		break;

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}
 out:
	return ret;
}

/*
 * Notification of system call entry/exit
 * - triggered by current->work.syscall_trace
 */
asmlinkage long syscall_trace_enter(struct pt_regs *regs, long syscall)
{
	long ret = 0;
	user_exit();

	if (secure_computing(syscall) == -1)
		return -1;

	if (test_thread_flag(TIF_SYSCALL_TRACE) &&
	    tracehook_report_syscall_entry(regs))
		ret = -1;

	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_enter(regs, regs->regs[2]);

	audit_syscall_entry(syscall_get_arch(),
			    syscall,
			    regs->regs[4], regs->regs[5],
			    regs->regs[6], regs->regs[7]);
	return syscall;
}

/*
 * Notification of system call entry/exit
 * - triggered by current->work.syscall_trace
 */
asmlinkage void syscall_trace_leave(struct pt_regs *regs)
{
        /*
	 * We may come here right after calling schedule_user()
	 * or do_notify_resume(), in which case we can be in RCU
	 * user mode.
	 */
	user_exit();

	audit_syscall_exit(regs);

	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_exit(regs, regs->regs[2]);

	if (test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, 0);

	user_enter();
}
