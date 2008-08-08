/*
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Derived from "arch/m68k/kernel/ptrace.c"
 *  Copyright (C) 1994 by Hamish Macdonald
 *  Taken from linux/kernel/ptrace.c and modified for M680x0.
 *  linux/kernel/ptrace.c is by Ross Biro 1/23/92, edited by Linus Torvalds
 *
 * Modified by Cort Dougan (cort@hq.fsmlabs.com)
 * and Paul Mackerras (paulus@samba.org).
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file README.legal in the main directory of
 * this archive for more details.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/regset.h>
#include <linux/tracehook.h>
#include <linux/elf.h>
#include <linux/user.h>
#include <linux/security.h>
#include <linux/signal.h>
#include <linux/seccomp.h>
#include <linux/audit.h>
#ifdef CONFIG_PPC32
#include <linux/module.h>
#endif

#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * Set of msr bits that gdb can change on behalf of a process.
 */
#if defined(CONFIG_40x) || defined(CONFIG_BOOKE)
#define MSR_DEBUGCHANGE	0
#else
#define MSR_DEBUGCHANGE	(MSR_SE | MSR_BE)
#endif

/*
 * Max register writeable via put_reg
 */
#ifdef CONFIG_PPC32
#define PT_MAX_PUT_REG	PT_MQ
#else
#define PT_MAX_PUT_REG	PT_CCR
#endif

static unsigned long get_user_msr(struct task_struct *task)
{
	return task->thread.regs->msr | task->thread.fpexc_mode;
}

static int set_user_msr(struct task_struct *task, unsigned long msr)
{
	task->thread.regs->msr &= ~MSR_DEBUGCHANGE;
	task->thread.regs->msr |= msr & MSR_DEBUGCHANGE;
	return 0;
}

/*
 * We prevent mucking around with the reserved area of trap
 * which are used internally by the kernel.
 */
static int set_user_trap(struct task_struct *task, unsigned long trap)
{
	task->thread.regs->trap = trap & 0xfff0;
	return 0;
}

/*
 * Get contents of register REGNO in task TASK.
 */
unsigned long ptrace_get_reg(struct task_struct *task, int regno)
{
	if (task->thread.regs == NULL)
		return -EIO;

	if (regno == PT_MSR)
		return get_user_msr(task);

	if (regno < (sizeof(struct pt_regs) / sizeof(unsigned long)))
		return ((unsigned long *)task->thread.regs)[regno];

	return -EIO;
}

/*
 * Write contents of register REGNO in task TASK.
 */
int ptrace_put_reg(struct task_struct *task, int regno, unsigned long data)
{
	if (task->thread.regs == NULL)
		return -EIO;

	if (regno == PT_MSR)
		return set_user_msr(task, data);
	if (regno == PT_TRAP)
		return set_user_trap(task, data);

	if (regno <= PT_MAX_PUT_REG) {
		((unsigned long *)task->thread.regs)[regno] = data;
		return 0;
	}
	return -EIO;
}

static int gpr_get(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   void *kbuf, void __user *ubuf)
{
	int ret;

	if (target->thread.regs == NULL)
		return -EIO;

	CHECK_FULL_REGS(target->thread.regs);

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  target->thread.regs,
				  0, offsetof(struct pt_regs, msr));
	if (!ret) {
		unsigned long msr = get_user_msr(target);
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, &msr,
					  offsetof(struct pt_regs, msr),
					  offsetof(struct pt_regs, msr) +
					  sizeof(msr));
	}

	BUILD_BUG_ON(offsetof(struct pt_regs, orig_gpr3) !=
		     offsetof(struct pt_regs, msr) + sizeof(long));

	if (!ret)
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  &target->thread.regs->orig_gpr3,
					  offsetof(struct pt_regs, orig_gpr3),
					  sizeof(struct pt_regs));
	if (!ret)
		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					       sizeof(struct pt_regs), -1);

	return ret;
}

static int gpr_set(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	unsigned long reg;
	int ret;

	if (target->thread.regs == NULL)
		return -EIO;

	CHECK_FULL_REGS(target->thread.regs);

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 target->thread.regs,
				 0, PT_MSR * sizeof(reg));

	if (!ret && count > 0) {
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &reg,
					 PT_MSR * sizeof(reg),
					 (PT_MSR + 1) * sizeof(reg));
		if (!ret)
			ret = set_user_msr(target, reg);
	}

	BUILD_BUG_ON(offsetof(struct pt_regs, orig_gpr3) !=
		     offsetof(struct pt_regs, msr) + sizeof(long));

	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &target->thread.regs->orig_gpr3,
					 PT_ORIG_R3 * sizeof(reg),
					 (PT_MAX_PUT_REG + 1) * sizeof(reg));

	if (PT_MAX_PUT_REG + 1 < PT_TRAP && !ret)
		ret = user_regset_copyin_ignore(
			&pos, &count, &kbuf, &ubuf,
			(PT_MAX_PUT_REG + 1) * sizeof(reg),
			PT_TRAP * sizeof(reg));

	if (!ret && count > 0) {
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &reg,
					 PT_TRAP * sizeof(reg),
					 (PT_TRAP + 1) * sizeof(reg));
		if (!ret)
			ret = set_user_trap(target, reg);
	}

	if (!ret)
		ret = user_regset_copyin_ignore(
			&pos, &count, &kbuf, &ubuf,
			(PT_TRAP + 1) * sizeof(reg), -1);

	return ret;
}

static int fpr_get(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   void *kbuf, void __user *ubuf)
{
#ifdef CONFIG_VSX
	double buf[33];
	int i;
#endif
	flush_fp_to_thread(target);

#ifdef CONFIG_VSX
	/* copy to local buffer then write that out */
	for (i = 0; i < 32 ; i++)
		buf[i] = target->thread.TS_FPR(i);
	memcpy(&buf[32], &target->thread.fpscr, sizeof(double));
	return user_regset_copyout(&pos, &count, &kbuf, &ubuf, buf, 0, -1);

#else
	BUILD_BUG_ON(offsetof(struct thread_struct, fpscr) !=
		     offsetof(struct thread_struct, TS_FPR(32)));

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   &target->thread.fpr, 0, -1);
#endif
}

static int fpr_set(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
#ifdef CONFIG_VSX
	double buf[33];
	int i;
#endif
	flush_fp_to_thread(target);

#ifdef CONFIG_VSX
	/* copy to local buffer then write that out */
	i = user_regset_copyin(&pos, &count, &kbuf, &ubuf, buf, 0, -1);
	if (i)
		return i;
	for (i = 0; i < 32 ; i++)
		target->thread.TS_FPR(i) = buf[i];
	memcpy(&target->thread.fpscr, &buf[32], sizeof(double));
	return 0;
#else
	BUILD_BUG_ON(offsetof(struct thread_struct, fpscr) !=
		     offsetof(struct thread_struct, TS_FPR(32)));

	return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				  &target->thread.fpr, 0, -1);
#endif
}

#ifdef CONFIG_ALTIVEC
/*
 * Get/set all the altivec registers vr0..vr31, vscr, vrsave, in one go.
 * The transfer totals 34 quadword.  Quadwords 0-31 contain the
 * corresponding vector registers.  Quadword 32 contains the vscr as the
 * last word (offset 12) within that quadword.  Quadword 33 contains the
 * vrsave as the first word (offset 0) within the quadword.
 *
 * This definition of the VMX state is compatible with the current PPC32
 * ptrace interface.  This allows signal handling and ptrace to use the
 * same structures.  This also simplifies the implementation of a bi-arch
 * (combined (32- and 64-bit) gdb.
 */

static int vr_active(struct task_struct *target,
		     const struct user_regset *regset)
{
	flush_altivec_to_thread(target);
	return target->thread.used_vr ? regset->n : 0;
}

static int vr_get(struct task_struct *target, const struct user_regset *regset,
		  unsigned int pos, unsigned int count,
		  void *kbuf, void __user *ubuf)
{
	int ret;

	flush_altivec_to_thread(target);

	BUILD_BUG_ON(offsetof(struct thread_struct, vscr) !=
		     offsetof(struct thread_struct, vr[32]));

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  &target->thread.vr, 0,
				  33 * sizeof(vector128));
	if (!ret) {
		/*
		 * Copy out only the low-order word of vrsave.
		 */
		union {
			elf_vrreg_t reg;
			u32 word;
		} vrsave;
		memset(&vrsave, 0, sizeof(vrsave));
		vrsave.word = target->thread.vrsave;
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, &vrsave,
					  33 * sizeof(vector128), -1);
	}

	return ret;
}

static int vr_set(struct task_struct *target, const struct user_regset *regset,
		  unsigned int pos, unsigned int count,
		  const void *kbuf, const void __user *ubuf)
{
	int ret;

	flush_altivec_to_thread(target);

	BUILD_BUG_ON(offsetof(struct thread_struct, vscr) !=
		     offsetof(struct thread_struct, vr[32]));

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &target->thread.vr, 0, 33 * sizeof(vector128));
	if (!ret && count > 0) {
		/*
		 * We use only the first word of vrsave.
		 */
		union {
			elf_vrreg_t reg;
			u32 word;
		} vrsave;
		memset(&vrsave, 0, sizeof(vrsave));
		vrsave.word = target->thread.vrsave;
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &vrsave,
					 33 * sizeof(vector128), -1);
		if (!ret)
			target->thread.vrsave = vrsave.word;
	}

	return ret;
}
#endif /* CONFIG_ALTIVEC */

#ifdef CONFIG_VSX
/*
 * Currently to set and and get all the vsx state, you need to call
 * the fp and VMX calls aswell.  This only get/sets the lower 32
 * 128bit VSX registers.
 */

static int vsr_active(struct task_struct *target,
		      const struct user_regset *regset)
{
	flush_vsx_to_thread(target);
	return target->thread.used_vsr ? regset->n : 0;
}

static int vsr_get(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   void *kbuf, void __user *ubuf)
{
	double buf[32];
	int ret, i;

	flush_vsx_to_thread(target);

	for (i = 0; i < 32 ; i++)
		buf[i] = target->thread.fpr[i][TS_VSRLOWOFFSET];
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  buf, 0, 32 * sizeof(double));

	return ret;
}

static int vsr_set(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	double buf[32];
	int ret,i;

	flush_vsx_to_thread(target);

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 buf, 0, 32 * sizeof(double));
	for (i = 0; i < 32 ; i++)
		target->thread.fpr[i][TS_VSRLOWOFFSET] = buf[i];


	return ret;
}
#endif /* CONFIG_VSX */

#ifdef CONFIG_SPE

/*
 * For get_evrregs/set_evrregs functions 'data' has the following layout:
 *
 * struct {
 *   u32 evr[32];
 *   u64 acc;
 *   u32 spefscr;
 * }
 */

static int evr_active(struct task_struct *target,
		      const struct user_regset *regset)
{
	flush_spe_to_thread(target);
	return target->thread.used_spe ? regset->n : 0;
}

static int evr_get(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   void *kbuf, void __user *ubuf)
{
	int ret;

	flush_spe_to_thread(target);

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  &target->thread.evr,
				  0, sizeof(target->thread.evr));

	BUILD_BUG_ON(offsetof(struct thread_struct, acc) + sizeof(u64) !=
		     offsetof(struct thread_struct, spefscr));

	if (!ret)
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  &target->thread.acc,
					  sizeof(target->thread.evr), -1);

	return ret;
}

static int evr_set(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	int ret;

	flush_spe_to_thread(target);

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &target->thread.evr,
				 0, sizeof(target->thread.evr));

	BUILD_BUG_ON(offsetof(struct thread_struct, acc) + sizeof(u64) !=
		     offsetof(struct thread_struct, spefscr));

	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &target->thread.acc,
					 sizeof(target->thread.evr), -1);

	return ret;
}
#endif /* CONFIG_SPE */


/*
 * These are our native regset flavors.
 */
enum powerpc_regset {
	REGSET_GPR,
	REGSET_FPR,
#ifdef CONFIG_ALTIVEC
	REGSET_VMX,
#endif
#ifdef CONFIG_VSX
	REGSET_VSX,
#endif
#ifdef CONFIG_SPE
	REGSET_SPE,
#endif
};

static const struct user_regset native_regsets[] = {
	[REGSET_GPR] = {
		.core_note_type = NT_PRSTATUS, .n = ELF_NGREG,
		.size = sizeof(long), .align = sizeof(long),
		.get = gpr_get, .set = gpr_set
	},
	[REGSET_FPR] = {
		.core_note_type = NT_PRFPREG, .n = ELF_NFPREG,
		.size = sizeof(double), .align = sizeof(double),
		.get = fpr_get, .set = fpr_set
	},
#ifdef CONFIG_ALTIVEC
	[REGSET_VMX] = {
		.core_note_type = NT_PPC_VMX, .n = 34,
		.size = sizeof(vector128), .align = sizeof(vector128),
		.active = vr_active, .get = vr_get, .set = vr_set
	},
#endif
#ifdef CONFIG_VSX
	[REGSET_VSX] = {
		.core_note_type = NT_PPC_VSX, .n = 32,
		.size = sizeof(double), .align = sizeof(double),
		.active = vsr_active, .get = vsr_get, .set = vsr_set
	},
#endif
#ifdef CONFIG_SPE
	[REGSET_SPE] = {
		.n = 35,
		.size = sizeof(u32), .align = sizeof(u32),
		.active = evr_active, .get = evr_get, .set = evr_set
	},
#endif
};

static const struct user_regset_view user_ppc_native_view = {
	.name = UTS_MACHINE, .e_machine = ELF_ARCH, .ei_osabi = ELF_OSABI,
	.regsets = native_regsets, .n = ARRAY_SIZE(native_regsets)
};

#ifdef CONFIG_PPC64
#include <linux/compat.h>

static int gpr32_get(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     void *kbuf, void __user *ubuf)
{
	const unsigned long *regs = &target->thread.regs->gpr[0];
	compat_ulong_t *k = kbuf;
	compat_ulong_t __user *u = ubuf;
	compat_ulong_t reg;

	if (target->thread.regs == NULL)
		return -EIO;

	CHECK_FULL_REGS(target->thread.regs);

	pos /= sizeof(reg);
	count /= sizeof(reg);

	if (kbuf)
		for (; count > 0 && pos < PT_MSR; --count)
			*k++ = regs[pos++];
	else
		for (; count > 0 && pos < PT_MSR; --count)
			if (__put_user((compat_ulong_t) regs[pos++], u++))
				return -EFAULT;

	if (count > 0 && pos == PT_MSR) {
		reg = get_user_msr(target);
		if (kbuf)
			*k++ = reg;
		else if (__put_user(reg, u++))
			return -EFAULT;
		++pos;
		--count;
	}

	if (kbuf)
		for (; count > 0 && pos < PT_REGS_COUNT; --count)
			*k++ = regs[pos++];
	else
		for (; count > 0 && pos < PT_REGS_COUNT; --count)
			if (__put_user((compat_ulong_t) regs[pos++], u++))
				return -EFAULT;

	kbuf = k;
	ubuf = u;
	pos *= sizeof(reg);
	count *= sizeof(reg);
	return user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					PT_REGS_COUNT * sizeof(reg), -1);
}

static int gpr32_set(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     const void *kbuf, const void __user *ubuf)
{
	unsigned long *regs = &target->thread.regs->gpr[0];
	const compat_ulong_t *k = kbuf;
	const compat_ulong_t __user *u = ubuf;
	compat_ulong_t reg;

	if (target->thread.regs == NULL)
		return -EIO;

	CHECK_FULL_REGS(target->thread.regs);

	pos /= sizeof(reg);
	count /= sizeof(reg);

	if (kbuf)
		for (; count > 0 && pos < PT_MSR; --count)
			regs[pos++] = *k++;
	else
		for (; count > 0 && pos < PT_MSR; --count) {
			if (__get_user(reg, u++))
				return -EFAULT;
			regs[pos++] = reg;
		}


	if (count > 0 && pos == PT_MSR) {
		if (kbuf)
			reg = *k++;
		else if (__get_user(reg, u++))
			return -EFAULT;
		set_user_msr(target, reg);
		++pos;
		--count;
	}

	if (kbuf) {
		for (; count > 0 && pos <= PT_MAX_PUT_REG; --count)
			regs[pos++] = *k++;
		for (; count > 0 && pos < PT_TRAP; --count, ++pos)
			++k;
	} else {
		for (; count > 0 && pos <= PT_MAX_PUT_REG; --count) {
			if (__get_user(reg, u++))
				return -EFAULT;
			regs[pos++] = reg;
		}
		for (; count > 0 && pos < PT_TRAP; --count, ++pos)
			if (__get_user(reg, u++))
				return -EFAULT;
	}

	if (count > 0 && pos == PT_TRAP) {
		if (kbuf)
			reg = *k++;
		else if (__get_user(reg, u++))
			return -EFAULT;
		set_user_trap(target, reg);
		++pos;
		--count;
	}

	kbuf = k;
	ubuf = u;
	pos *= sizeof(reg);
	count *= sizeof(reg);
	return user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					 (PT_TRAP + 1) * sizeof(reg), -1);
}

/*
 * These are the regset flavors matching the CONFIG_PPC32 native set.
 */
static const struct user_regset compat_regsets[] = {
	[REGSET_GPR] = {
		.core_note_type = NT_PRSTATUS, .n = ELF_NGREG,
		.size = sizeof(compat_long_t), .align = sizeof(compat_long_t),
		.get = gpr32_get, .set = gpr32_set
	},
	[REGSET_FPR] = {
		.core_note_type = NT_PRFPREG, .n = ELF_NFPREG,
		.size = sizeof(double), .align = sizeof(double),
		.get = fpr_get, .set = fpr_set
	},
#ifdef CONFIG_ALTIVEC
	[REGSET_VMX] = {
		.core_note_type = NT_PPC_VMX, .n = 34,
		.size = sizeof(vector128), .align = sizeof(vector128),
		.active = vr_active, .get = vr_get, .set = vr_set
	},
#endif
#ifdef CONFIG_SPE
	[REGSET_SPE] = {
		.core_note_type = NT_PPC_SPE, .n = 35,
		.size = sizeof(u32), .align = sizeof(u32),
		.active = evr_active, .get = evr_get, .set = evr_set
	},
#endif
};

static const struct user_regset_view user_ppc_compat_view = {
	.name = "ppc", .e_machine = EM_PPC, .ei_osabi = ELF_OSABI,
	.regsets = compat_regsets, .n = ARRAY_SIZE(compat_regsets)
};
#endif	/* CONFIG_PPC64 */

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
#ifdef CONFIG_PPC64
	if (test_tsk_thread_flag(task, TIF_32BIT))
		return &user_ppc_compat_view;
#endif
	return &user_ppc_native_view;
}


void user_enable_single_step(struct task_struct *task)
{
	struct pt_regs *regs = task->thread.regs;

	if (regs != NULL) {
#if defined(CONFIG_40x) || defined(CONFIG_BOOKE)
		task->thread.dbcr0 |= DBCR0_IDM | DBCR0_IC;
		regs->msr |= MSR_DE;
#else
		regs->msr |= MSR_SE;
#endif
	}
	set_tsk_thread_flag(task, TIF_SINGLESTEP);
}

void user_disable_single_step(struct task_struct *task)
{
	struct pt_regs *regs = task->thread.regs;


#if defined(CONFIG_BOOKE)
	/* If DAC then do not single step, skip */
	if (task->thread.dabr)
		return;
#endif

	if (regs != NULL) {
#if defined(CONFIG_40x) || defined(CONFIG_BOOKE)
		task->thread.dbcr0 &= ~(DBCR0_IC | DBCR0_IDM);
		regs->msr &= ~MSR_DE;
#else
		regs->msr &= ~MSR_SE;
#endif
	}
	clear_tsk_thread_flag(task, TIF_SINGLESTEP);
}

int ptrace_set_debugreg(struct task_struct *task, unsigned long addr,
			       unsigned long data)
{
	/* For ppc64 we support one DABR and no IABR's at the moment (ppc64).
	 *  For embedded processors we support one DAC and no IAC's at the
	 *  moment.
	 */
	if (addr > 0)
		return -EINVAL;

	/* The bottom 3 bits in dabr are flags */
	if ((data & ~0x7UL) >= TASK_SIZE)
		return -EIO;

#ifndef CONFIG_BOOKE

	/* For processors using DABR (i.e. 970), the bottom 3 bits are flags.
	 *  It was assumed, on previous implementations, that 3 bits were
	 *  passed together with the data address, fitting the design of the
	 *  DABR register, as follows:
	 *
	 *  bit 0: Read flag
	 *  bit 1: Write flag
	 *  bit 2: Breakpoint translation
	 *
	 *  Thus, we use them here as so.
	 */

	/* Ensure breakpoint translation bit is set */
	if (data && !(data & DABR_TRANSLATION))
		return -EIO;

	/* Move contents to the DABR register */
	task->thread.dabr = data;

#endif
#if defined(CONFIG_BOOKE)

	/* As described above, it was assumed 3 bits were passed with the data
	 *  address, but we will assume only the mode bits will be passed
	 *  as to not cause alignment restrictions for DAC-based processors.
	 */

	/* DAC's hold the whole address without any mode flags */
	task->thread.dabr = data & ~0x3UL;

	if (task->thread.dabr == 0) {
		task->thread.dbcr0 &= ~(DBSR_DAC1R | DBSR_DAC1W | DBCR0_IDM);
		task->thread.regs->msr &= ~MSR_DE;
		return 0;
	}

	/* Read or Write bits must be set */

	if (!(data & 0x3UL))
		return -EINVAL;

	/* Set the Internal Debugging flag (IDM bit 1) for the DBCR0
	   register */
	task->thread.dbcr0 = DBCR0_IDM;

	/* Check for write and read flags and set DBCR0
	   accordingly */
	if (data & 0x1UL)
		task->thread.dbcr0 |= DBSR_DAC1R;
	if (data & 0x2UL)
		task->thread.dbcr0 |= DBSR_DAC1W;

	task->thread.regs->msr |= MSR_DE;
#endif
	return 0;
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *child)
{
	/* make sure the single step bit is not set. */
	user_disable_single_step(child);
}

/*
 * Here are the old "legacy" powerpc specific getregs/setregs ptrace calls,
 * we mark them as obsolete now, they will be removed in a future version
 */
static long arch_ptrace_old(struct task_struct *child, long request, long addr,
			    long data)
{
	switch (request) {
	case PPC_PTRACE_GETREGS:	/* Get GPRs 0 - 31. */
		return copy_regset_to_user(child, &user_ppc_native_view,
					   REGSET_GPR, 0, 32 * sizeof(long),
					   (void __user *) data);

	case PPC_PTRACE_SETREGS:	/* Set GPRs 0 - 31. */
		return copy_regset_from_user(child, &user_ppc_native_view,
					     REGSET_GPR, 0, 32 * sizeof(long),
					     (const void __user *) data);

	case PPC_PTRACE_GETFPREGS:	/* Get FPRs 0 - 31. */
		return copy_regset_to_user(child, &user_ppc_native_view,
					   REGSET_FPR, 0, 32 * sizeof(double),
					   (void __user *) data);

	case PPC_PTRACE_SETFPREGS:	/* Set FPRs 0 - 31. */
		return copy_regset_from_user(child, &user_ppc_native_view,
					     REGSET_FPR, 0, 32 * sizeof(double),
					     (const void __user *) data);
	}

	return -EPERM;
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	int ret = -EPERM;

	switch (request) {
	/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		unsigned long index, tmp;

		ret = -EIO;
		/* convert to index and check */
#ifdef CONFIG_PPC32
		index = (unsigned long) addr >> 2;
		if ((addr & 3) || (index > PT_FPSCR)
		    || (child->thread.regs == NULL))
#else
		index = (unsigned long) addr >> 3;
		if ((addr & 7) || (index > PT_FPSCR))
#endif
			break;

		CHECK_FULL_REGS(child->thread.regs);
		if (index < PT_FPR0) {
			tmp = ptrace_get_reg(child, (int) index);
		} else {
			flush_fp_to_thread(child);
			tmp = ((unsigned long *)child->thread.fpr)
				[TS_FPRWIDTH * (index - PT_FPR0)];
		}
		ret = put_user(tmp,(unsigned long __user *) data);
		break;
	}

	/* write the word at location addr in the USER area */
	case PTRACE_POKEUSR: {
		unsigned long index;

		ret = -EIO;
		/* convert to index and check */
#ifdef CONFIG_PPC32
		index = (unsigned long) addr >> 2;
		if ((addr & 3) || (index > PT_FPSCR)
		    || (child->thread.regs == NULL))
#else
		index = (unsigned long) addr >> 3;
		if ((addr & 7) || (index > PT_FPSCR))
#endif
			break;

		CHECK_FULL_REGS(child->thread.regs);
		if (index < PT_FPR0) {
			ret = ptrace_put_reg(child, index, data);
		} else {
			flush_fp_to_thread(child);
			((unsigned long *)child->thread.fpr)
				[TS_FPRWIDTH * (index - PT_FPR0)] = data;
			ret = 0;
		}
		break;
	}

	case PTRACE_GET_DEBUGREG: {
		ret = -EINVAL;
		/* We only support one DABR and no IABRS at the moment */
		if (addr > 0)
			break;
		ret = put_user(child->thread.dabr,
			       (unsigned long __user *)data);
		break;
	}

	case PTRACE_SET_DEBUGREG:
		ret = ptrace_set_debugreg(child, addr, data);
		break;

#ifdef CONFIG_PPC64
	case PTRACE_GETREGS64:
#endif
	case PTRACE_GETREGS:	/* Get all pt_regs from the child. */
		return copy_regset_to_user(child, &user_ppc_native_view,
					   REGSET_GPR,
					   0, sizeof(struct pt_regs),
					   (void __user *) data);

#ifdef CONFIG_PPC64
	case PTRACE_SETREGS64:
#endif
	case PTRACE_SETREGS:	/* Set all gp regs in the child. */
		return copy_regset_from_user(child, &user_ppc_native_view,
					     REGSET_GPR,
					     0, sizeof(struct pt_regs),
					     (const void __user *) data);

	case PTRACE_GETFPREGS: /* Get the child FPU state (FPR0...31 + FPSCR) */
		return copy_regset_to_user(child, &user_ppc_native_view,
					   REGSET_FPR,
					   0, sizeof(elf_fpregset_t),
					   (void __user *) data);

	case PTRACE_SETFPREGS: /* Set the child FPU state (FPR0...31 + FPSCR) */
		return copy_regset_from_user(child, &user_ppc_native_view,
					     REGSET_FPR,
					     0, sizeof(elf_fpregset_t),
					     (const void __user *) data);

#ifdef CONFIG_ALTIVEC
	case PTRACE_GETVRREGS:
		return copy_regset_to_user(child, &user_ppc_native_view,
					   REGSET_VMX,
					   0, (33 * sizeof(vector128) +
					       sizeof(u32)),
					   (void __user *) data);

	case PTRACE_SETVRREGS:
		return copy_regset_from_user(child, &user_ppc_native_view,
					     REGSET_VMX,
					     0, (33 * sizeof(vector128) +
						 sizeof(u32)),
					     (const void __user *) data);
#endif
#ifdef CONFIG_VSX
	case PTRACE_GETVSRREGS:
		return copy_regset_to_user(child, &user_ppc_native_view,
					   REGSET_VSX,
					   0, 32 * sizeof(double),
					   (void __user *) data);

	case PTRACE_SETVSRREGS:
		return copy_regset_from_user(child, &user_ppc_native_view,
					     REGSET_VSX,
					     0, 32 * sizeof(double),
					     (const void __user *) data);
#endif
#ifdef CONFIG_SPE
	case PTRACE_GETEVRREGS:
		/* Get the child spe register state. */
		return copy_regset_to_user(child, &user_ppc_native_view,
					   REGSET_SPE, 0, 35 * sizeof(u32),
					   (void __user *) data);

	case PTRACE_SETEVRREGS:
		/* Set the child spe register state. */
		return copy_regset_from_user(child, &user_ppc_native_view,
					     REGSET_SPE, 0, 35 * sizeof(u32),
					     (const void __user *) data);
#endif

	/* Old reverse args ptrace callss */
	case PPC_PTRACE_GETREGS: /* Get GPRs 0 - 31. */
	case PPC_PTRACE_SETREGS: /* Set GPRs 0 - 31. */
	case PPC_PTRACE_GETFPREGS: /* Get FPRs 0 - 31. */
	case PPC_PTRACE_SETFPREGS: /* Get FPRs 0 - 31. */
		ret = arch_ptrace_old(child, request, addr, data);
		break;

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}
	return ret;
}

/*
 * We must return the syscall number to actually look up in the table.
 * This can be -1L to skip running any syscall at all.
 */
long do_syscall_trace_enter(struct pt_regs *regs)
{
	long ret = 0;

	secure_computing(regs->gpr[0]);

	if (test_thread_flag(TIF_SYSCALL_TRACE) &&
	    tracehook_report_syscall_entry(regs))
		/*
		 * Tracing decided this syscall should not happen.
		 * We'll return a bogus call number to get an ENOSYS
		 * error, but leave the original number in regs->gpr[0].
		 */
		ret = -1L;

	if (unlikely(current->audit_context)) {
#ifdef CONFIG_PPC64
		if (!test_thread_flag(TIF_32BIT))
			audit_syscall_entry(AUDIT_ARCH_PPC64,
					    regs->gpr[0],
					    regs->gpr[3], regs->gpr[4],
					    regs->gpr[5], regs->gpr[6]);
		else
#endif
			audit_syscall_entry(AUDIT_ARCH_PPC,
					    regs->gpr[0],
					    regs->gpr[3] & 0xffffffff,
					    regs->gpr[4] & 0xffffffff,
					    regs->gpr[5] & 0xffffffff,
					    regs->gpr[6] & 0xffffffff);
	}

	return ret ?: regs->gpr[0];
}

void do_syscall_trace_leave(struct pt_regs *regs)
{
	int step;

	if (unlikely(current->audit_context))
		audit_syscall_exit((regs->ccr&0x10000000)?AUDITSC_FAILURE:AUDITSC_SUCCESS,
				   regs->result);

	step = test_thread_flag(TIF_SINGLESTEP);
	if (step || test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, step);
}
