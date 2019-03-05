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
#include <trace/syscall.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <linux/context_tracking.h>

#include <linux/uaccess.h>
#include <linux/pkeys.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/switch_to.h>
#include <asm/tm.h>
#include <asm/asm-prototypes.h>
#include <asm/debug.h>

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

/*
 * The parameter save area on the stack is used to store arguments being passed
 * to callee function and is located at fixed offset from stack pointer.
 */
#ifdef CONFIG_PPC32
#define PARAMETER_SAVE_AREA_OFFSET	24  /* bytes */
#else /* CONFIG_PPC32 */
#define PARAMETER_SAVE_AREA_OFFSET	48  /* bytes */
#endif

struct pt_regs_offset {
	const char *name;
	int offset;
};

#define STR(s)	#s			/* convert to string */
#define REG_OFFSET_NAME(r) {.name = #r, .offset = offsetof(struct pt_regs, r)}
#define GPR_OFFSET_NAME(num)	\
	{.name = STR(r##num), .offset = offsetof(struct pt_regs, gpr[num])}, \
	{.name = STR(gpr##num), .offset = offsetof(struct pt_regs, gpr[num])}
#define REG_OFFSET_END {.name = NULL, .offset = 0}

#define TVSO(f)	(offsetof(struct thread_vr_state, f))
#define TFSO(f)	(offsetof(struct thread_fp_state, f))
#define TSO(f)	(offsetof(struct thread_struct, f))

static const struct pt_regs_offset regoffset_table[] = {
	GPR_OFFSET_NAME(0),
	GPR_OFFSET_NAME(1),
	GPR_OFFSET_NAME(2),
	GPR_OFFSET_NAME(3),
	GPR_OFFSET_NAME(4),
	GPR_OFFSET_NAME(5),
	GPR_OFFSET_NAME(6),
	GPR_OFFSET_NAME(7),
	GPR_OFFSET_NAME(8),
	GPR_OFFSET_NAME(9),
	GPR_OFFSET_NAME(10),
	GPR_OFFSET_NAME(11),
	GPR_OFFSET_NAME(12),
	GPR_OFFSET_NAME(13),
	GPR_OFFSET_NAME(14),
	GPR_OFFSET_NAME(15),
	GPR_OFFSET_NAME(16),
	GPR_OFFSET_NAME(17),
	GPR_OFFSET_NAME(18),
	GPR_OFFSET_NAME(19),
	GPR_OFFSET_NAME(20),
	GPR_OFFSET_NAME(21),
	GPR_OFFSET_NAME(22),
	GPR_OFFSET_NAME(23),
	GPR_OFFSET_NAME(24),
	GPR_OFFSET_NAME(25),
	GPR_OFFSET_NAME(26),
	GPR_OFFSET_NAME(27),
	GPR_OFFSET_NAME(28),
	GPR_OFFSET_NAME(29),
	GPR_OFFSET_NAME(30),
	GPR_OFFSET_NAME(31),
	REG_OFFSET_NAME(nip),
	REG_OFFSET_NAME(msr),
	REG_OFFSET_NAME(ctr),
	REG_OFFSET_NAME(link),
	REG_OFFSET_NAME(xer),
	REG_OFFSET_NAME(ccr),
#ifdef CONFIG_PPC64
	REG_OFFSET_NAME(softe),
#else
	REG_OFFSET_NAME(mq),
#endif
	REG_OFFSET_NAME(trap),
	REG_OFFSET_NAME(dar),
	REG_OFFSET_NAME(dsisr),
	REG_OFFSET_END,
};

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
static void flush_tmregs_to_thread(struct task_struct *tsk)
{
	/*
	 * If task is not current, it will have been flushed already to
	 * it's thread_struct during __switch_to().
	 *
	 * A reclaim flushes ALL the state or if not in TM save TM SPRs
	 * in the appropriate thread structures from live.
	 */

	if ((!cpu_has_feature(CPU_FTR_TM)) || (tsk != current))
		return;

	if (MSR_TM_SUSPENDED(mfmsr())) {
		tm_reclaim_current(TM_CAUSE_SIGNAL);
	} else {
		tm_enable();
		tm_save_sprs(&(tsk->thread));
	}
}
#else
static inline void flush_tmregs_to_thread(struct task_struct *tsk) { }
#endif

/**
 * regs_query_register_offset() - query register offset from its name
 * @name:	the name of a register
 *
 * regs_query_register_offset() returns the offset of a register in struct
 * pt_regs from its name. If the name is invalid, this returns -EINVAL;
 */
int regs_query_register_offset(const char *name)
{
	const struct pt_regs_offset *roff;
	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (!strcmp(roff->name, name))
			return roff->offset;
	return -EINVAL;
}

/**
 * regs_query_register_name() - query register name from its offset
 * @offset:	the offset of a register in struct pt_regs.
 *
 * regs_query_register_name() returns the name of a register from its
 * offset in struct pt_regs. If the @offset is invalid, this returns NULL;
 */
const char *regs_query_register_name(unsigned int offset)
{
	const struct pt_regs_offset *roff;
	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (roff->offset == offset)
			return roff->name;
	return NULL;
}

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * Set of msr bits that gdb can change on behalf of a process.
 */
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
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

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
static unsigned long get_user_ckpt_msr(struct task_struct *task)
{
	return task->thread.ckpt_regs.msr | task->thread.fpexc_mode;
}

static int set_user_ckpt_msr(struct task_struct *task, unsigned long msr)
{
	task->thread.ckpt_regs.msr &= ~MSR_DEBUGCHANGE;
	task->thread.ckpt_regs.msr |= msr & MSR_DEBUGCHANGE;
	return 0;
}

static int set_user_ckpt_trap(struct task_struct *task, unsigned long trap)
{
	task->thread.ckpt_regs.trap = trap & 0xfff0;
	return 0;
}
#endif

#ifdef CONFIG_PPC64
static int get_user_dscr(struct task_struct *task, unsigned long *data)
{
	*data = task->thread.dscr;
	return 0;
}

static int set_user_dscr(struct task_struct *task, unsigned long dscr)
{
	task->thread.dscr = dscr;
	task->thread.dscr_inherit = 1;
	return 0;
}
#else
static int get_user_dscr(struct task_struct *task, unsigned long *data)
{
	return -EIO;
}

static int set_user_dscr(struct task_struct *task, unsigned long dscr)
{
	return -EIO;
}
#endif

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
int ptrace_get_reg(struct task_struct *task, int regno, unsigned long *data)
{
	if ((task->thread.regs == NULL) || !data)
		return -EIO;

	if (regno == PT_MSR) {
		*data = get_user_msr(task);
		return 0;
	}

	if (regno == PT_DSCR)
		return get_user_dscr(task, data);

#ifdef CONFIG_PPC64
	/*
	 * softe copies paca->irq_soft_mask variable state. Since irq_soft_mask is
	 * no more used as a flag, lets force usr to alway see the softe value as 1
	 * which means interrupts are not soft disabled.
	 */
	if (regno == PT_SOFTE) {
		*data = 1;
		return  0;
	}
#endif

	if (regno < (sizeof(struct user_pt_regs) / sizeof(unsigned long))) {
		*data = ((unsigned long *)task->thread.regs)[regno];
		return 0;
	}

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
	if (regno == PT_DSCR)
		return set_user_dscr(task, data);

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
	int i, ret;

	if (target->thread.regs == NULL)
		return -EIO;

	if (!FULL_REGS(target->thread.regs)) {
		/* We have a partial register set.  Fill 14-31 with bogus values */
		for (i = 14; i < 32; i++)
			target->thread.regs->gpr[i] = NV_REG_POISON;
	}

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
					  sizeof(struct user_pt_regs));
	if (!ret)
		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					       sizeof(struct user_pt_regs), -1);

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

/*
 * Regardless of transactions, 'fp_state' holds the current running
 * value of all FPR registers and 'ckfp_state' holds the last checkpointed
 * value of all FPR registers for the current transaction.
 *
 * Userspace interface buffer layout:
 *
 * struct data {
 *	u64	fpr[32];
 *	u64	fpscr;
 * };
 */
static int fpr_get(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   void *kbuf, void __user *ubuf)
{
#ifdef CONFIG_VSX
	u64 buf[33];
	int i;

	flush_fp_to_thread(target);

	/* copy to local buffer then write that out */
	for (i = 0; i < 32 ; i++)
		buf[i] = target->thread.TS_FPR(i);
	buf[32] = target->thread.fp_state.fpscr;
	return user_regset_copyout(&pos, &count, &kbuf, &ubuf, buf, 0, -1);
#else
	BUILD_BUG_ON(offsetof(struct thread_fp_state, fpscr) !=
		     offsetof(struct thread_fp_state, fpr[32]));

	flush_fp_to_thread(target);

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   &target->thread.fp_state, 0, -1);
#endif
}

/*
 * Regardless of transactions, 'fp_state' holds the current running
 * value of all FPR registers and 'ckfp_state' holds the last checkpointed
 * value of all FPR registers for the current transaction.
 *
 * Userspace interface buffer layout:
 *
 * struct data {
 *	u64	fpr[32];
 *	u64	fpscr;
 * };
 *
 */
static int fpr_set(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
#ifdef CONFIG_VSX
	u64 buf[33];
	int i;

	flush_fp_to_thread(target);

	for (i = 0; i < 32 ; i++)
		buf[i] = target->thread.TS_FPR(i);
	buf[32] = target->thread.fp_state.fpscr;

	/* copy to local buffer then write that out */
	i = user_regset_copyin(&pos, &count, &kbuf, &ubuf, buf, 0, -1);
	if (i)
		return i;

	for (i = 0; i < 32 ; i++)
		target->thread.TS_FPR(i) = buf[i];
	target->thread.fp_state.fpscr = buf[32];
	return 0;
#else
	BUILD_BUG_ON(offsetof(struct thread_fp_state, fpscr) !=
		     offsetof(struct thread_fp_state, fpr[32]));

	flush_fp_to_thread(target);

	return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				  &target->thread.fp_state, 0, -1);
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

/*
 * Regardless of transactions, 'vr_state' holds the current running
 * value of all the VMX registers and 'ckvr_state' holds the last
 * checkpointed value of all the VMX registers for the current
 * transaction to fall back on in case it aborts.
 *
 * Userspace interface buffer layout:
 *
 * struct data {
 *	vector128	vr[32];
 *	vector128	vscr;
 *	vector128	vrsave;
 * };
 */
static int vr_get(struct task_struct *target, const struct user_regset *regset,
		  unsigned int pos, unsigned int count,
		  void *kbuf, void __user *ubuf)
{
	int ret;

	flush_altivec_to_thread(target);

	BUILD_BUG_ON(offsetof(struct thread_vr_state, vscr) !=
		     offsetof(struct thread_vr_state, vr[32]));

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  &target->thread.vr_state, 0,
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

/*
 * Regardless of transactions, 'vr_state' holds the current running
 * value of all the VMX registers and 'ckvr_state' holds the last
 * checkpointed value of all the VMX registers for the current
 * transaction to fall back on in case it aborts.
 *
 * Userspace interface buffer layout:
 *
 * struct data {
 *	vector128	vr[32];
 *	vector128	vscr;
 *	vector128	vrsave;
 * };
 */
static int vr_set(struct task_struct *target, const struct user_regset *regset,
		  unsigned int pos, unsigned int count,
		  const void *kbuf, const void __user *ubuf)
{
	int ret;

	flush_altivec_to_thread(target);

	BUILD_BUG_ON(offsetof(struct thread_vr_state, vscr) !=
		     offsetof(struct thread_vr_state, vr[32]));

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &target->thread.vr_state, 0,
				 33 * sizeof(vector128));
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
 * the fp and VMX calls as well.  This only get/sets the lower 32
 * 128bit VSX registers.
 */

static int vsr_active(struct task_struct *target,
		      const struct user_regset *regset)
{
	flush_vsx_to_thread(target);
	return target->thread.used_vsr ? regset->n : 0;
}

/*
 * Regardless of transactions, 'fp_state' holds the current running
 * value of all FPR registers and 'ckfp_state' holds the last
 * checkpointed value of all FPR registers for the current
 * transaction.
 *
 * Userspace interface buffer layout:
 *
 * struct data {
 *	u64	vsx[32];
 * };
 */
static int vsr_get(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   void *kbuf, void __user *ubuf)
{
	u64 buf[32];
	int ret, i;

	flush_tmregs_to_thread(target);
	flush_fp_to_thread(target);
	flush_altivec_to_thread(target);
	flush_vsx_to_thread(target);

	for (i = 0; i < 32 ; i++)
		buf[i] = target->thread.fp_state.fpr[i][TS_VSRLOWOFFSET];

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  buf, 0, 32 * sizeof(double));

	return ret;
}

/*
 * Regardless of transactions, 'fp_state' holds the current running
 * value of all FPR registers and 'ckfp_state' holds the last
 * checkpointed value of all FPR registers for the current
 * transaction.
 *
 * Userspace interface buffer layout:
 *
 * struct data {
 *	u64	vsx[32];
 * };
 */
static int vsr_set(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	u64 buf[32];
	int ret,i;

	flush_tmregs_to_thread(target);
	flush_fp_to_thread(target);
	flush_altivec_to_thread(target);
	flush_vsx_to_thread(target);

	for (i = 0; i < 32 ; i++)
		buf[i] = target->thread.fp_state.fpr[i][TS_VSRLOWOFFSET];

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 buf, 0, 32 * sizeof(double));
	if (!ret)
		for (i = 0; i < 32 ; i++)
			target->thread.fp_state.fpr[i][TS_VSRLOWOFFSET] = buf[i];

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

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
/**
 * tm_cgpr_active - get active number of registers in CGPR
 * @target:	The target task.
 * @regset:	The user regset structure.
 *
 * This function checks for the active number of available
 * regisers in transaction checkpointed GPR category.
 */
static int tm_cgpr_active(struct task_struct *target,
			  const struct user_regset *regset)
{
	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return 0;

	return regset->n;
}

/**
 * tm_cgpr_get - get CGPR registers
 * @target:	The target task.
 * @regset:	The user regset structure.
 * @pos:	The buffer position.
 * @count:	Number of bytes to copy.
 * @kbuf:	Kernel buffer to copy from.
 * @ubuf:	User buffer to copy into.
 *
 * This function gets transaction checkpointed GPR registers.
 *
 * When the transaction is active, 'ckpt_regs' holds all the checkpointed
 * GPR register values for the current transaction to fall back on if it
 * aborts in between. This function gets those checkpointed GPR registers.
 * The userspace interface buffer layout is as follows.
 *
 * struct data {
 *	struct pt_regs ckpt_regs;
 * };
 */
static int tm_cgpr_get(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			void *kbuf, void __user *ubuf)
{
	int ret;

	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return -ENODATA;

	flush_tmregs_to_thread(target);
	flush_fp_to_thread(target);
	flush_altivec_to_thread(target);

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  &target->thread.ckpt_regs,
				  0, offsetof(struct pt_regs, msr));
	if (!ret) {
		unsigned long msr = get_user_ckpt_msr(target);

		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, &msr,
					  offsetof(struct pt_regs, msr),
					  offsetof(struct pt_regs, msr) +
					  sizeof(msr));
	}

	BUILD_BUG_ON(offsetof(struct pt_regs, orig_gpr3) !=
		     offsetof(struct pt_regs, msr) + sizeof(long));

	if (!ret)
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  &target->thread.ckpt_regs.orig_gpr3,
					  offsetof(struct pt_regs, orig_gpr3),
					  sizeof(struct user_pt_regs));
	if (!ret)
		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					       sizeof(struct user_pt_regs), -1);

	return ret;
}

/*
 * tm_cgpr_set - set the CGPR registers
 * @target:	The target task.
 * @regset:	The user regset structure.
 * @pos:	The buffer position.
 * @count:	Number of bytes to copy.
 * @kbuf:	Kernel buffer to copy into.
 * @ubuf:	User buffer to copy from.
 *
 * This function sets in transaction checkpointed GPR registers.
 *
 * When the transaction is active, 'ckpt_regs' holds the checkpointed
 * GPR register values for the current transaction to fall back on if it
 * aborts in between. This function sets those checkpointed GPR registers.
 * The userspace interface buffer layout is as follows.
 *
 * struct data {
 *	struct pt_regs ckpt_regs;
 * };
 */
static int tm_cgpr_set(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			const void *kbuf, const void __user *ubuf)
{
	unsigned long reg;
	int ret;

	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return -ENODATA;

	flush_tmregs_to_thread(target);
	flush_fp_to_thread(target);
	flush_altivec_to_thread(target);

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &target->thread.ckpt_regs,
				 0, PT_MSR * sizeof(reg));

	if (!ret && count > 0) {
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &reg,
					 PT_MSR * sizeof(reg),
					 (PT_MSR + 1) * sizeof(reg));
		if (!ret)
			ret = set_user_ckpt_msr(target, reg);
	}

	BUILD_BUG_ON(offsetof(struct pt_regs, orig_gpr3) !=
		     offsetof(struct pt_regs, msr) + sizeof(long));

	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &target->thread.ckpt_regs.orig_gpr3,
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
			ret = set_user_ckpt_trap(target, reg);
	}

	if (!ret)
		ret = user_regset_copyin_ignore(
			&pos, &count, &kbuf, &ubuf,
			(PT_TRAP + 1) * sizeof(reg), -1);

	return ret;
}

/**
 * tm_cfpr_active - get active number of registers in CFPR
 * @target:	The target task.
 * @regset:	The user regset structure.
 *
 * This function checks for the active number of available
 * regisers in transaction checkpointed FPR category.
 */
static int tm_cfpr_active(struct task_struct *target,
				const struct user_regset *regset)
{
	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return 0;

	return regset->n;
}

/**
 * tm_cfpr_get - get CFPR registers
 * @target:	The target task.
 * @regset:	The user regset structure.
 * @pos:	The buffer position.
 * @count:	Number of bytes to copy.
 * @kbuf:	Kernel buffer to copy from.
 * @ubuf:	User buffer to copy into.
 *
 * This function gets in transaction checkpointed FPR registers.
 *
 * When the transaction is active 'ckfp_state' holds the checkpointed
 * values for the current transaction to fall back on if it aborts
 * in between. This function gets those checkpointed FPR registers.
 * The userspace interface buffer layout is as follows.
 *
 * struct data {
 *	u64	fpr[32];
 *	u64	fpscr;
 *};
 */
static int tm_cfpr_get(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			void *kbuf, void __user *ubuf)
{
	u64 buf[33];
	int i;

	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return -ENODATA;

	flush_tmregs_to_thread(target);
	flush_fp_to_thread(target);
	flush_altivec_to_thread(target);

	/* copy to local buffer then write that out */
	for (i = 0; i < 32 ; i++)
		buf[i] = target->thread.TS_CKFPR(i);
	buf[32] = target->thread.ckfp_state.fpscr;
	return user_regset_copyout(&pos, &count, &kbuf, &ubuf, buf, 0, -1);
}

/**
 * tm_cfpr_set - set CFPR registers
 * @target:	The target task.
 * @regset:	The user regset structure.
 * @pos:	The buffer position.
 * @count:	Number of bytes to copy.
 * @kbuf:	Kernel buffer to copy into.
 * @ubuf:	User buffer to copy from.
 *
 * This function sets in transaction checkpointed FPR registers.
 *
 * When the transaction is active 'ckfp_state' holds the checkpointed
 * FPR register values for the current transaction to fall back on
 * if it aborts in between. This function sets these checkpointed
 * FPR registers. The userspace interface buffer layout is as follows.
 *
 * struct data {
 *	u64	fpr[32];
 *	u64	fpscr;
 *};
 */
static int tm_cfpr_set(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			const void *kbuf, const void __user *ubuf)
{
	u64 buf[33];
	int i;

	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return -ENODATA;

	flush_tmregs_to_thread(target);
	flush_fp_to_thread(target);
	flush_altivec_to_thread(target);

	for (i = 0; i < 32; i++)
		buf[i] = target->thread.TS_CKFPR(i);
	buf[32] = target->thread.ckfp_state.fpscr;

	/* copy to local buffer then write that out */
	i = user_regset_copyin(&pos, &count, &kbuf, &ubuf, buf, 0, -1);
	if (i)
		return i;
	for (i = 0; i < 32 ; i++)
		target->thread.TS_CKFPR(i) = buf[i];
	target->thread.ckfp_state.fpscr = buf[32];
	return 0;
}

/**
 * tm_cvmx_active - get active number of registers in CVMX
 * @target:	The target task.
 * @regset:	The user regset structure.
 *
 * This function checks for the active number of available
 * regisers in checkpointed VMX category.
 */
static int tm_cvmx_active(struct task_struct *target,
				const struct user_regset *regset)
{
	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return 0;

	return regset->n;
}

/**
 * tm_cvmx_get - get CMVX registers
 * @target:	The target task.
 * @regset:	The user regset structure.
 * @pos:	The buffer position.
 * @count:	Number of bytes to copy.
 * @kbuf:	Kernel buffer to copy from.
 * @ubuf:	User buffer to copy into.
 *
 * This function gets in transaction checkpointed VMX registers.
 *
 * When the transaction is active 'ckvr_state' and 'ckvrsave' hold
 * the checkpointed values for the current transaction to fall
 * back on if it aborts in between. The userspace interface buffer
 * layout is as follows.
 *
 * struct data {
 *	vector128	vr[32];
 *	vector128	vscr;
 *	vector128	vrsave;
 *};
 */
static int tm_cvmx_get(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			void *kbuf, void __user *ubuf)
{
	int ret;

	BUILD_BUG_ON(TVSO(vscr) != TVSO(vr[32]));

	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return -ENODATA;

	/* Flush the state */
	flush_tmregs_to_thread(target);
	flush_fp_to_thread(target);
	flush_altivec_to_thread(target);

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					&target->thread.ckvr_state, 0,
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
		vrsave.word = target->thread.ckvrsave;
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, &vrsave,
						33 * sizeof(vector128), -1);
	}

	return ret;
}

/**
 * tm_cvmx_set - set CMVX registers
 * @target:	The target task.
 * @regset:	The user regset structure.
 * @pos:	The buffer position.
 * @count:	Number of bytes to copy.
 * @kbuf:	Kernel buffer to copy into.
 * @ubuf:	User buffer to copy from.
 *
 * This function sets in transaction checkpointed VMX registers.
 *
 * When the transaction is active 'ckvr_state' and 'ckvrsave' hold
 * the checkpointed values for the current transaction to fall
 * back on if it aborts in between. The userspace interface buffer
 * layout is as follows.
 *
 * struct data {
 *	vector128	vr[32];
 *	vector128	vscr;
 *	vector128	vrsave;
 *};
 */
static int tm_cvmx_set(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			const void *kbuf, const void __user *ubuf)
{
	int ret;

	BUILD_BUG_ON(TVSO(vscr) != TVSO(vr[32]));

	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return -ENODATA;

	flush_tmregs_to_thread(target);
	flush_fp_to_thread(target);
	flush_altivec_to_thread(target);

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					&target->thread.ckvr_state, 0,
					33 * sizeof(vector128));
	if (!ret && count > 0) {
		/*
		 * We use only the low-order word of vrsave.
		 */
		union {
			elf_vrreg_t reg;
			u32 word;
		} vrsave;
		memset(&vrsave, 0, sizeof(vrsave));
		vrsave.word = target->thread.ckvrsave;
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &vrsave,
						33 * sizeof(vector128), -1);
		if (!ret)
			target->thread.ckvrsave = vrsave.word;
	}

	return ret;
}

/**
 * tm_cvsx_active - get active number of registers in CVSX
 * @target:	The target task.
 * @regset:	The user regset structure.
 *
 * This function checks for the active number of available
 * regisers in transaction checkpointed VSX category.
 */
static int tm_cvsx_active(struct task_struct *target,
				const struct user_regset *regset)
{
	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return 0;

	flush_vsx_to_thread(target);
	return target->thread.used_vsr ? regset->n : 0;
}

/**
 * tm_cvsx_get - get CVSX registers
 * @target:	The target task.
 * @regset:	The user regset structure.
 * @pos:	The buffer position.
 * @count:	Number of bytes to copy.
 * @kbuf:	Kernel buffer to copy from.
 * @ubuf:	User buffer to copy into.
 *
 * This function gets in transaction checkpointed VSX registers.
 *
 * When the transaction is active 'ckfp_state' holds the checkpointed
 * values for the current transaction to fall back on if it aborts
 * in between. This function gets those checkpointed VSX registers.
 * The userspace interface buffer layout is as follows.
 *
 * struct data {
 *	u64	vsx[32];
 *};
 */
static int tm_cvsx_get(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			void *kbuf, void __user *ubuf)
{
	u64 buf[32];
	int ret, i;

	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return -ENODATA;

	/* Flush the state */
	flush_tmregs_to_thread(target);
	flush_fp_to_thread(target);
	flush_altivec_to_thread(target);
	flush_vsx_to_thread(target);

	for (i = 0; i < 32 ; i++)
		buf[i] = target->thread.ckfp_state.fpr[i][TS_VSRLOWOFFSET];
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  buf, 0, 32 * sizeof(double));

	return ret;
}

/**
 * tm_cvsx_set - set CFPR registers
 * @target:	The target task.
 * @regset:	The user regset structure.
 * @pos:	The buffer position.
 * @count:	Number of bytes to copy.
 * @kbuf:	Kernel buffer to copy into.
 * @ubuf:	User buffer to copy from.
 *
 * This function sets in transaction checkpointed VSX registers.
 *
 * When the transaction is active 'ckfp_state' holds the checkpointed
 * VSX register values for the current transaction to fall back on
 * if it aborts in between. This function sets these checkpointed
 * FPR registers. The userspace interface buffer layout is as follows.
 *
 * struct data {
 *	u64	vsx[32];
 *};
 */
static int tm_cvsx_set(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			const void *kbuf, const void __user *ubuf)
{
	u64 buf[32];
	int ret, i;

	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return -ENODATA;

	/* Flush the state */
	flush_tmregs_to_thread(target);
	flush_fp_to_thread(target);
	flush_altivec_to_thread(target);
	flush_vsx_to_thread(target);

	for (i = 0; i < 32 ; i++)
		buf[i] = target->thread.ckfp_state.fpr[i][TS_VSRLOWOFFSET];

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 buf, 0, 32 * sizeof(double));
	if (!ret)
		for (i = 0; i < 32 ; i++)
			target->thread.ckfp_state.fpr[i][TS_VSRLOWOFFSET] = buf[i];

	return ret;
}

/**
 * tm_spr_active - get active number of registers in TM SPR
 * @target:	The target task.
 * @regset:	The user regset structure.
 *
 * This function checks the active number of available
 * regisers in the transactional memory SPR category.
 */
static int tm_spr_active(struct task_struct *target,
			 const struct user_regset *regset)
{
	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	return regset->n;
}

/**
 * tm_spr_get - get the TM related SPR registers
 * @target:	The target task.
 * @regset:	The user regset structure.
 * @pos:	The buffer position.
 * @count:	Number of bytes to copy.
 * @kbuf:	Kernel buffer to copy from.
 * @ubuf:	User buffer to copy into.
 *
 * This function gets transactional memory related SPR registers.
 * The userspace interface buffer layout is as follows.
 *
 * struct {
 *	u64		tm_tfhar;
 *	u64		tm_texasr;
 *	u64		tm_tfiar;
 * };
 */
static int tm_spr_get(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      void *kbuf, void __user *ubuf)
{
	int ret;

	/* Build tests */
	BUILD_BUG_ON(TSO(tm_tfhar) + sizeof(u64) != TSO(tm_texasr));
	BUILD_BUG_ON(TSO(tm_texasr) + sizeof(u64) != TSO(tm_tfiar));
	BUILD_BUG_ON(TSO(tm_tfiar) + sizeof(u64) != TSO(ckpt_regs));

	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	/* Flush the states */
	flush_tmregs_to_thread(target);
	flush_fp_to_thread(target);
	flush_altivec_to_thread(target);

	/* TFHAR register */
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				&target->thread.tm_tfhar, 0, sizeof(u64));

	/* TEXASR register */
	if (!ret)
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				&target->thread.tm_texasr, sizeof(u64),
				2 * sizeof(u64));

	/* TFIAR register */
	if (!ret)
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				&target->thread.tm_tfiar,
				2 * sizeof(u64), 3 * sizeof(u64));
	return ret;
}

/**
 * tm_spr_set - set the TM related SPR registers
 * @target:	The target task.
 * @regset:	The user regset structure.
 * @pos:	The buffer position.
 * @count:	Number of bytes to copy.
 * @kbuf:	Kernel buffer to copy into.
 * @ubuf:	User buffer to copy from.
 *
 * This function sets transactional memory related SPR registers.
 * The userspace interface buffer layout is as follows.
 *
 * struct {
 *	u64		tm_tfhar;
 *	u64		tm_texasr;
 *	u64		tm_tfiar;
 * };
 */
static int tm_spr_set(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      const void *kbuf, const void __user *ubuf)
{
	int ret;

	/* Build tests */
	BUILD_BUG_ON(TSO(tm_tfhar) + sizeof(u64) != TSO(tm_texasr));
	BUILD_BUG_ON(TSO(tm_texasr) + sizeof(u64) != TSO(tm_tfiar));
	BUILD_BUG_ON(TSO(tm_tfiar) + sizeof(u64) != TSO(ckpt_regs));

	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	/* Flush the states */
	flush_tmregs_to_thread(target);
	flush_fp_to_thread(target);
	flush_altivec_to_thread(target);

	/* TFHAR register */
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				&target->thread.tm_tfhar, 0, sizeof(u64));

	/* TEXASR register */
	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				&target->thread.tm_texasr, sizeof(u64),
				2 * sizeof(u64));

	/* TFIAR register */
	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				&target->thread.tm_tfiar,
				 2 * sizeof(u64), 3 * sizeof(u64));
	return ret;
}

static int tm_tar_active(struct task_struct *target,
			 const struct user_regset *regset)
{
	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (MSR_TM_ACTIVE(target->thread.regs->msr))
		return regset->n;

	return 0;
}

static int tm_tar_get(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      void *kbuf, void __user *ubuf)
{
	int ret;

	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return -ENODATA;

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				&target->thread.tm_tar, 0, sizeof(u64));
	return ret;
}

static int tm_tar_set(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      const void *kbuf, const void __user *ubuf)
{
	int ret;

	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return -ENODATA;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				&target->thread.tm_tar, 0, sizeof(u64));
	return ret;
}

static int tm_ppr_active(struct task_struct *target,
			 const struct user_regset *regset)
{
	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (MSR_TM_ACTIVE(target->thread.regs->msr))
		return regset->n;

	return 0;
}


static int tm_ppr_get(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      void *kbuf, void __user *ubuf)
{
	int ret;

	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return -ENODATA;

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				&target->thread.tm_ppr, 0, sizeof(u64));
	return ret;
}

static int tm_ppr_set(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      const void *kbuf, const void __user *ubuf)
{
	int ret;

	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return -ENODATA;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				&target->thread.tm_ppr, 0, sizeof(u64));
	return ret;
}

static int tm_dscr_active(struct task_struct *target,
			 const struct user_regset *regset)
{
	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (MSR_TM_ACTIVE(target->thread.regs->msr))
		return regset->n;

	return 0;
}

static int tm_dscr_get(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      void *kbuf, void __user *ubuf)
{
	int ret;

	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return -ENODATA;

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				&target->thread.tm_dscr, 0, sizeof(u64));
	return ret;
}

static int tm_dscr_set(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      const void *kbuf, const void __user *ubuf)
{
	int ret;

	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return -ENODATA;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				&target->thread.tm_dscr, 0, sizeof(u64));
	return ret;
}
#endif	/* CONFIG_PPC_TRANSACTIONAL_MEM */

#ifdef CONFIG_PPC64
static int ppr_get(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      void *kbuf, void __user *ubuf)
{
	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   &target->thread.regs->ppr, 0, sizeof(u64));
}

static int ppr_set(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      const void *kbuf, const void __user *ubuf)
{
	return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				  &target->thread.regs->ppr, 0, sizeof(u64));
}

static int dscr_get(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      void *kbuf, void __user *ubuf)
{
	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   &target->thread.dscr, 0, sizeof(u64));
}
static int dscr_set(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      const void *kbuf, const void __user *ubuf)
{
	return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				  &target->thread.dscr, 0, sizeof(u64));
}
#endif
#ifdef CONFIG_PPC_BOOK3S_64
static int tar_get(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      void *kbuf, void __user *ubuf)
{
	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   &target->thread.tar, 0, sizeof(u64));
}
static int tar_set(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      const void *kbuf, const void __user *ubuf)
{
	return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				  &target->thread.tar, 0, sizeof(u64));
}

static int ebb_active(struct task_struct *target,
			 const struct user_regset *regset)
{
	if (!cpu_has_feature(CPU_FTR_ARCH_207S))
		return -ENODEV;

	if (target->thread.used_ebb)
		return regset->n;

	return 0;
}

static int ebb_get(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      void *kbuf, void __user *ubuf)
{
	/* Build tests */
	BUILD_BUG_ON(TSO(ebbrr) + sizeof(unsigned long) != TSO(ebbhr));
	BUILD_BUG_ON(TSO(ebbhr) + sizeof(unsigned long) != TSO(bescr));

	if (!cpu_has_feature(CPU_FTR_ARCH_207S))
		return -ENODEV;

	if (!target->thread.used_ebb)
		return -ENODATA;

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
			&target->thread.ebbrr, 0, 3 * sizeof(unsigned long));
}

static int ebb_set(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      const void *kbuf, const void __user *ubuf)
{
	int ret = 0;

	/* Build tests */
	BUILD_BUG_ON(TSO(ebbrr) + sizeof(unsigned long) != TSO(ebbhr));
	BUILD_BUG_ON(TSO(ebbhr) + sizeof(unsigned long) != TSO(bescr));

	if (!cpu_has_feature(CPU_FTR_ARCH_207S))
		return -ENODEV;

	if (target->thread.used_ebb)
		return -ENODATA;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
			&target->thread.ebbrr, 0, sizeof(unsigned long));

	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
			&target->thread.ebbhr, sizeof(unsigned long),
			2 * sizeof(unsigned long));

	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
			&target->thread.bescr,
			2 * sizeof(unsigned long), 3 * sizeof(unsigned long));

	return ret;
}
static int pmu_active(struct task_struct *target,
			 const struct user_regset *regset)
{
	if (!cpu_has_feature(CPU_FTR_ARCH_207S))
		return -ENODEV;

	return regset->n;
}

static int pmu_get(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      void *kbuf, void __user *ubuf)
{
	/* Build tests */
	BUILD_BUG_ON(TSO(siar) + sizeof(unsigned long) != TSO(sdar));
	BUILD_BUG_ON(TSO(sdar) + sizeof(unsigned long) != TSO(sier));
	BUILD_BUG_ON(TSO(sier) + sizeof(unsigned long) != TSO(mmcr2));
	BUILD_BUG_ON(TSO(mmcr2) + sizeof(unsigned long) != TSO(mmcr0));

	if (!cpu_has_feature(CPU_FTR_ARCH_207S))
		return -ENODEV;

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
			&target->thread.siar, 0,
			5 * sizeof(unsigned long));
}

static int pmu_set(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      const void *kbuf, const void __user *ubuf)
{
	int ret = 0;

	/* Build tests */
	BUILD_BUG_ON(TSO(siar) + sizeof(unsigned long) != TSO(sdar));
	BUILD_BUG_ON(TSO(sdar) + sizeof(unsigned long) != TSO(sier));
	BUILD_BUG_ON(TSO(sier) + sizeof(unsigned long) != TSO(mmcr2));
	BUILD_BUG_ON(TSO(mmcr2) + sizeof(unsigned long) != TSO(mmcr0));

	if (!cpu_has_feature(CPU_FTR_ARCH_207S))
		return -ENODEV;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
			&target->thread.siar, 0,
			sizeof(unsigned long));

	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
			&target->thread.sdar, sizeof(unsigned long),
			2 * sizeof(unsigned long));

	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
			&target->thread.sier, 2 * sizeof(unsigned long),
			3 * sizeof(unsigned long));

	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
			&target->thread.mmcr2, 3 * sizeof(unsigned long),
			4 * sizeof(unsigned long));

	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
			&target->thread.mmcr0, 4 * sizeof(unsigned long),
			5 * sizeof(unsigned long));
	return ret;
}
#endif

#ifdef CONFIG_PPC_MEM_KEYS
static int pkey_active(struct task_struct *target,
		       const struct user_regset *regset)
{
	if (!arch_pkeys_enabled())
		return -ENODEV;

	return regset->n;
}

static int pkey_get(struct task_struct *target,
		    const struct user_regset *regset,
		    unsigned int pos, unsigned int count,
		    void *kbuf, void __user *ubuf)
{
	BUILD_BUG_ON(TSO(amr) + sizeof(unsigned long) != TSO(iamr));
	BUILD_BUG_ON(TSO(iamr) + sizeof(unsigned long) != TSO(uamor));

	if (!arch_pkeys_enabled())
		return -ENODEV;

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   &target->thread.amr, 0,
				   ELF_NPKEY * sizeof(unsigned long));
}

static int pkey_set(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      const void *kbuf, const void __user *ubuf)
{
	u64 new_amr;
	int ret;

	if (!arch_pkeys_enabled())
		return -ENODEV;

	/* Only the AMR can be set from userspace */
	if (pos != 0 || count != sizeof(new_amr))
		return -EINVAL;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &new_amr, 0, sizeof(new_amr));
	if (ret)
		return ret;

	/* UAMOR determines which bits of the AMR can be set from userspace. */
	target->thread.amr = (new_amr & target->thread.uamor) |
		(target->thread.amr & ~target->thread.uamor);

	return 0;
}
#endif /* CONFIG_PPC_MEM_KEYS */

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
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	REGSET_TM_CGPR,		/* TM checkpointed GPR registers */
	REGSET_TM_CFPR,		/* TM checkpointed FPR registers */
	REGSET_TM_CVMX,		/* TM checkpointed VMX registers */
	REGSET_TM_CVSX,		/* TM checkpointed VSX registers */
	REGSET_TM_SPR,		/* TM specific SPR registers */
	REGSET_TM_CTAR,		/* TM checkpointed TAR register */
	REGSET_TM_CPPR,		/* TM checkpointed PPR register */
	REGSET_TM_CDSCR,	/* TM checkpointed DSCR register */
#endif
#ifdef CONFIG_PPC64
	REGSET_PPR,		/* PPR register */
	REGSET_DSCR,		/* DSCR register */
#endif
#ifdef CONFIG_PPC_BOOK3S_64
	REGSET_TAR,		/* TAR register */
	REGSET_EBB,		/* EBB registers */
	REGSET_PMR,		/* Performance Monitor Registers */
#endif
#ifdef CONFIG_PPC_MEM_KEYS
	REGSET_PKEY,		/* AMR register */
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
		.core_note_type = NT_PPC_SPE, .n = 35,
		.size = sizeof(u32), .align = sizeof(u32),
		.active = evr_active, .get = evr_get, .set = evr_set
	},
#endif
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	[REGSET_TM_CGPR] = {
		.core_note_type = NT_PPC_TM_CGPR, .n = ELF_NGREG,
		.size = sizeof(long), .align = sizeof(long),
		.active = tm_cgpr_active, .get = tm_cgpr_get, .set = tm_cgpr_set
	},
	[REGSET_TM_CFPR] = {
		.core_note_type = NT_PPC_TM_CFPR, .n = ELF_NFPREG,
		.size = sizeof(double), .align = sizeof(double),
		.active = tm_cfpr_active, .get = tm_cfpr_get, .set = tm_cfpr_set
	},
	[REGSET_TM_CVMX] = {
		.core_note_type = NT_PPC_TM_CVMX, .n = ELF_NVMX,
		.size = sizeof(vector128), .align = sizeof(vector128),
		.active = tm_cvmx_active, .get = tm_cvmx_get, .set = tm_cvmx_set
	},
	[REGSET_TM_CVSX] = {
		.core_note_type = NT_PPC_TM_CVSX, .n = ELF_NVSX,
		.size = sizeof(double), .align = sizeof(double),
		.active = tm_cvsx_active, .get = tm_cvsx_get, .set = tm_cvsx_set
	},
	[REGSET_TM_SPR] = {
		.core_note_type = NT_PPC_TM_SPR, .n = ELF_NTMSPRREG,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = tm_spr_active, .get = tm_spr_get, .set = tm_spr_set
	},
	[REGSET_TM_CTAR] = {
		.core_note_type = NT_PPC_TM_CTAR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = tm_tar_active, .get = tm_tar_get, .set = tm_tar_set
	},
	[REGSET_TM_CPPR] = {
		.core_note_type = NT_PPC_TM_CPPR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = tm_ppr_active, .get = tm_ppr_get, .set = tm_ppr_set
	},
	[REGSET_TM_CDSCR] = {
		.core_note_type = NT_PPC_TM_CDSCR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = tm_dscr_active, .get = tm_dscr_get, .set = tm_dscr_set
	},
#endif
#ifdef CONFIG_PPC64
	[REGSET_PPR] = {
		.core_note_type = NT_PPC_PPR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.get = ppr_get, .set = ppr_set
	},
	[REGSET_DSCR] = {
		.core_note_type = NT_PPC_DSCR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.get = dscr_get, .set = dscr_set
	},
#endif
#ifdef CONFIG_PPC_BOOK3S_64
	[REGSET_TAR] = {
		.core_note_type = NT_PPC_TAR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.get = tar_get, .set = tar_set
	},
	[REGSET_EBB] = {
		.core_note_type = NT_PPC_EBB, .n = ELF_NEBB,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = ebb_active, .get = ebb_get, .set = ebb_set
	},
	[REGSET_PMR] = {
		.core_note_type = NT_PPC_PMU, .n = ELF_NPMU,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = pmu_active, .get = pmu_get, .set = pmu_set
	},
#endif
#ifdef CONFIG_PPC_MEM_KEYS
	[REGSET_PKEY] = {
		.core_note_type = NT_PPC_PKEY, .n = ELF_NPKEY,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = pkey_active, .get = pkey_get, .set = pkey_set
	},
#endif
};

static const struct user_regset_view user_ppc_native_view = {
	.name = UTS_MACHINE, .e_machine = ELF_ARCH, .ei_osabi = ELF_OSABI,
	.regsets = native_regsets, .n = ARRAY_SIZE(native_regsets)
};

#ifdef CONFIG_PPC64
#include <linux/compat.h>

static int gpr32_get_common(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
			    void *kbuf, void __user *ubuf,
			    unsigned long *regs)
{
	compat_ulong_t *k = kbuf;
	compat_ulong_t __user *u = ubuf;
	compat_ulong_t reg;

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

static int gpr32_set_common(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     const void *kbuf, const void __user *ubuf,
		     unsigned long *regs)
{
	const compat_ulong_t *k = kbuf;
	const compat_ulong_t __user *u = ubuf;
	compat_ulong_t reg;

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

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
static int tm_cgpr32_get(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     void *kbuf, void __user *ubuf)
{
	return gpr32_get_common(target, regset, pos, count, kbuf, ubuf,
			&target->thread.ckpt_regs.gpr[0]);
}

static int tm_cgpr32_set(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     const void *kbuf, const void __user *ubuf)
{
	return gpr32_set_common(target, regset, pos, count, kbuf, ubuf,
			&target->thread.ckpt_regs.gpr[0]);
}
#endif /* CONFIG_PPC_TRANSACTIONAL_MEM */

static int gpr32_get(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     void *kbuf, void __user *ubuf)
{
	int i;

	if (target->thread.regs == NULL)
		return -EIO;

	if (!FULL_REGS(target->thread.regs)) {
		/*
		 * We have a partial register set.
		 * Fill 14-31 with bogus values.
		 */
		for (i = 14; i < 32; i++)
			target->thread.regs->gpr[i] = NV_REG_POISON;
	}
	return gpr32_get_common(target, regset, pos, count, kbuf, ubuf,
			&target->thread.regs->gpr[0]);
}

static int gpr32_set(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     const void *kbuf, const void __user *ubuf)
{
	if (target->thread.regs == NULL)
		return -EIO;

	CHECK_FULL_REGS(target->thread.regs);
	return gpr32_set_common(target, regset, pos, count, kbuf, ubuf,
			&target->thread.regs->gpr[0]);
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
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	[REGSET_TM_CGPR] = {
		.core_note_type = NT_PPC_TM_CGPR, .n = ELF_NGREG,
		.size = sizeof(long), .align = sizeof(long),
		.active = tm_cgpr_active,
		.get = tm_cgpr32_get, .set = tm_cgpr32_set
	},
	[REGSET_TM_CFPR] = {
		.core_note_type = NT_PPC_TM_CFPR, .n = ELF_NFPREG,
		.size = sizeof(double), .align = sizeof(double),
		.active = tm_cfpr_active, .get = tm_cfpr_get, .set = tm_cfpr_set
	},
	[REGSET_TM_CVMX] = {
		.core_note_type = NT_PPC_TM_CVMX, .n = ELF_NVMX,
		.size = sizeof(vector128), .align = sizeof(vector128),
		.active = tm_cvmx_active, .get = tm_cvmx_get, .set = tm_cvmx_set
	},
	[REGSET_TM_CVSX] = {
		.core_note_type = NT_PPC_TM_CVSX, .n = ELF_NVSX,
		.size = sizeof(double), .align = sizeof(double),
		.active = tm_cvsx_active, .get = tm_cvsx_get, .set = tm_cvsx_set
	},
	[REGSET_TM_SPR] = {
		.core_note_type = NT_PPC_TM_SPR, .n = ELF_NTMSPRREG,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = tm_spr_active, .get = tm_spr_get, .set = tm_spr_set
	},
	[REGSET_TM_CTAR] = {
		.core_note_type = NT_PPC_TM_CTAR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = tm_tar_active, .get = tm_tar_get, .set = tm_tar_set
	},
	[REGSET_TM_CPPR] = {
		.core_note_type = NT_PPC_TM_CPPR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = tm_ppr_active, .get = tm_ppr_get, .set = tm_ppr_set
	},
	[REGSET_TM_CDSCR] = {
		.core_note_type = NT_PPC_TM_CDSCR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = tm_dscr_active, .get = tm_dscr_get, .set = tm_dscr_set
	},
#endif
#ifdef CONFIG_PPC64
	[REGSET_PPR] = {
		.core_note_type = NT_PPC_PPR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.get = ppr_get, .set = ppr_set
	},
	[REGSET_DSCR] = {
		.core_note_type = NT_PPC_DSCR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.get = dscr_get, .set = dscr_set
	},
#endif
#ifdef CONFIG_PPC_BOOK3S_64
	[REGSET_TAR] = {
		.core_note_type = NT_PPC_TAR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.get = tar_get, .set = tar_set
	},
	[REGSET_EBB] = {
		.core_note_type = NT_PPC_EBB, .n = ELF_NEBB,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = ebb_active, .get = ebb_get, .set = ebb_set
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
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
		task->thread.debug.dbcr0 &= ~DBCR0_BT;
		task->thread.debug.dbcr0 |= DBCR0_IDM | DBCR0_IC;
		regs->msr |= MSR_DE;
#else
		regs->msr &= ~MSR_BE;
		regs->msr |= MSR_SE;
#endif
	}
	set_tsk_thread_flag(task, TIF_SINGLESTEP);
}

void user_enable_block_step(struct task_struct *task)
{
	struct pt_regs *regs = task->thread.regs;

	if (regs != NULL) {
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
		task->thread.debug.dbcr0 &= ~DBCR0_IC;
		task->thread.debug.dbcr0 = DBCR0_IDM | DBCR0_BT;
		regs->msr |= MSR_DE;
#else
		regs->msr &= ~MSR_SE;
		regs->msr |= MSR_BE;
#endif
	}
	set_tsk_thread_flag(task, TIF_SINGLESTEP);
}

void user_disable_single_step(struct task_struct *task)
{
	struct pt_regs *regs = task->thread.regs;

	if (regs != NULL) {
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
		/*
		 * The logic to disable single stepping should be as
		 * simple as turning off the Instruction Complete flag.
		 * And, after doing so, if all debug flags are off, turn
		 * off DBCR0(IDM) and MSR(DE) .... Torez
		 */
		task->thread.debug.dbcr0 &= ~(DBCR0_IC|DBCR0_BT);
		/*
		 * Test to see if any of the DBCR_ACTIVE_EVENTS bits are set.
		 */
		if (!DBCR_ACTIVE_EVENTS(task->thread.debug.dbcr0,
					task->thread.debug.dbcr1)) {
			/*
			 * All debug events were off.....
			 */
			task->thread.debug.dbcr0 &= ~DBCR0_IDM;
			regs->msr &= ~MSR_DE;
		}
#else
		regs->msr &= ~(MSR_SE | MSR_BE);
#endif
	}
	clear_tsk_thread_flag(task, TIF_SINGLESTEP);
}

#ifdef CONFIG_HAVE_HW_BREAKPOINT
void ptrace_triggered(struct perf_event *bp,
		      struct perf_sample_data *data, struct pt_regs *regs)
{
	struct perf_event_attr attr;

	/*
	 * Disable the breakpoint request here since ptrace has defined a
	 * one-shot behaviour for breakpoint exceptions in PPC64.
	 * The SIGTRAP signal is generated automatically for us in do_dabr().
	 * We don't have to do anything about that here
	 */
	attr = bp->attr;
	attr.disabled = true;
	modify_user_hw_breakpoint(bp, &attr);
}
#endif /* CONFIG_HAVE_HW_BREAKPOINT */

static int ptrace_set_debugreg(struct task_struct *task, unsigned long addr,
			       unsigned long data)
{
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	int ret;
	struct thread_struct *thread = &(task->thread);
	struct perf_event *bp;
	struct perf_event_attr attr;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */
#ifndef CONFIG_PPC_ADV_DEBUG_REGS
	bool set_bp = true;
	struct arch_hw_breakpoint hw_brk;
#endif

	/* For ppc64 we support one DABR and no IABR's at the moment (ppc64).
	 *  For embedded processors we support one DAC and no IAC's at the
	 *  moment.
	 */
	if (addr > 0)
		return -EINVAL;

	/* The bottom 3 bits in dabr are flags */
	if ((data & ~0x7UL) >= TASK_SIZE)
		return -EIO;

#ifndef CONFIG_PPC_ADV_DEBUG_REGS
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
	if (data && !(data & HW_BRK_TYPE_TRANSLATE))
		return -EIO;
	hw_brk.address = data & (~HW_BRK_TYPE_DABR);
	hw_brk.type = (data & HW_BRK_TYPE_DABR) | HW_BRK_TYPE_PRIV_ALL;
	hw_brk.len = 8;
	set_bp = (data) && (hw_brk.type & HW_BRK_TYPE_RDWR);
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	bp = thread->ptrace_bps[0];
	if (!set_bp) {
		if (bp) {
			unregister_hw_breakpoint(bp);
			thread->ptrace_bps[0] = NULL;
		}
		return 0;
	}
	if (bp) {
		attr = bp->attr;
		attr.bp_addr = hw_brk.address;
		arch_bp_generic_fields(hw_brk.type, &attr.bp_type);

		/* Enable breakpoint */
		attr.disabled = false;

		ret =  modify_user_hw_breakpoint(bp, &attr);
		if (ret) {
			return ret;
		}
		thread->ptrace_bps[0] = bp;
		thread->hw_brk = hw_brk;
		return 0;
	}

	/* Create a new breakpoint request if one doesn't exist already */
	hw_breakpoint_init(&attr);
	attr.bp_addr = hw_brk.address;
	attr.bp_len = 8;
	arch_bp_generic_fields(hw_brk.type,
			       &attr.bp_type);

	thread->ptrace_bps[0] = bp = register_user_hw_breakpoint(&attr,
					       ptrace_triggered, NULL, task);
	if (IS_ERR(bp)) {
		thread->ptrace_bps[0] = NULL;
		return PTR_ERR(bp);
	}

#else /* !CONFIG_HAVE_HW_BREAKPOINT */
	if (set_bp && (!ppc_breakpoint_available()))
		return -ENODEV;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */
	task->thread.hw_brk = hw_brk;
#else /* CONFIG_PPC_ADV_DEBUG_REGS */
	/* As described above, it was assumed 3 bits were passed with the data
	 *  address, but we will assume only the mode bits will be passed
	 *  as to not cause alignment restrictions for DAC-based processors.
	 */

	/* DAC's hold the whole address without any mode flags */
	task->thread.debug.dac1 = data & ~0x3UL;

	if (task->thread.debug.dac1 == 0) {
		dbcr_dac(task) &= ~(DBCR_DAC1R | DBCR_DAC1W);
		if (!DBCR_ACTIVE_EVENTS(task->thread.debug.dbcr0,
					task->thread.debug.dbcr1)) {
			task->thread.regs->msr &= ~MSR_DE;
			task->thread.debug.dbcr0 &= ~DBCR0_IDM;
		}
		return 0;
	}

	/* Read or Write bits must be set */

	if (!(data & 0x3UL))
		return -EINVAL;

	/* Set the Internal Debugging flag (IDM bit 1) for the DBCR0
	   register */
	task->thread.debug.dbcr0 |= DBCR0_IDM;

	/* Check for write and read flags and set DBCR0
	   accordingly */
	dbcr_dac(task) &= ~(DBCR_DAC1R|DBCR_DAC1W);
	if (data & 0x1UL)
		dbcr_dac(task) |= DBCR_DAC1R;
	if (data & 0x2UL)
		dbcr_dac(task) |= DBCR_DAC1W;
	task->thread.regs->msr |= MSR_DE;
#endif /* CONFIG_PPC_ADV_DEBUG_REGS */
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
	clear_tsk_thread_flag(child, TIF_SYSCALL_EMU);
}

#ifdef CONFIG_PPC_ADV_DEBUG_REGS
static long set_instruction_bp(struct task_struct *child,
			      struct ppc_hw_breakpoint *bp_info)
{
	int slot;
	int slot1_in_use = ((child->thread.debug.dbcr0 & DBCR0_IAC1) != 0);
	int slot2_in_use = ((child->thread.debug.dbcr0 & DBCR0_IAC2) != 0);
	int slot3_in_use = ((child->thread.debug.dbcr0 & DBCR0_IAC3) != 0);
	int slot4_in_use = ((child->thread.debug.dbcr0 & DBCR0_IAC4) != 0);

	if (dbcr_iac_range(child) & DBCR_IAC12MODE)
		slot2_in_use = 1;
	if (dbcr_iac_range(child) & DBCR_IAC34MODE)
		slot4_in_use = 1;

	if (bp_info->addr >= TASK_SIZE)
		return -EIO;

	if (bp_info->addr_mode != PPC_BREAKPOINT_MODE_EXACT) {

		/* Make sure range is valid. */
		if (bp_info->addr2 >= TASK_SIZE)
			return -EIO;

		/* We need a pair of IAC regsisters */
		if ((!slot1_in_use) && (!slot2_in_use)) {
			slot = 1;
			child->thread.debug.iac1 = bp_info->addr;
			child->thread.debug.iac2 = bp_info->addr2;
			child->thread.debug.dbcr0 |= DBCR0_IAC1;
			if (bp_info->addr_mode ==
					PPC_BREAKPOINT_MODE_RANGE_EXCLUSIVE)
				dbcr_iac_range(child) |= DBCR_IAC12X;
			else
				dbcr_iac_range(child) |= DBCR_IAC12I;
#if CONFIG_PPC_ADV_DEBUG_IACS > 2
		} else if ((!slot3_in_use) && (!slot4_in_use)) {
			slot = 3;
			child->thread.debug.iac3 = bp_info->addr;
			child->thread.debug.iac4 = bp_info->addr2;
			child->thread.debug.dbcr0 |= DBCR0_IAC3;
			if (bp_info->addr_mode ==
					PPC_BREAKPOINT_MODE_RANGE_EXCLUSIVE)
				dbcr_iac_range(child) |= DBCR_IAC34X;
			else
				dbcr_iac_range(child) |= DBCR_IAC34I;
#endif
		} else
			return -ENOSPC;
	} else {
		/* We only need one.  If possible leave a pair free in
		 * case a range is needed later
		 */
		if (!slot1_in_use) {
			/*
			 * Don't use iac1 if iac1-iac2 are free and either
			 * iac3 or iac4 (but not both) are free
			 */
			if (slot2_in_use || (slot3_in_use == slot4_in_use)) {
				slot = 1;
				child->thread.debug.iac1 = bp_info->addr;
				child->thread.debug.dbcr0 |= DBCR0_IAC1;
				goto out;
			}
		}
		if (!slot2_in_use) {
			slot = 2;
			child->thread.debug.iac2 = bp_info->addr;
			child->thread.debug.dbcr0 |= DBCR0_IAC2;
#if CONFIG_PPC_ADV_DEBUG_IACS > 2
		} else if (!slot3_in_use) {
			slot = 3;
			child->thread.debug.iac3 = bp_info->addr;
			child->thread.debug.dbcr0 |= DBCR0_IAC3;
		} else if (!slot4_in_use) {
			slot = 4;
			child->thread.debug.iac4 = bp_info->addr;
			child->thread.debug.dbcr0 |= DBCR0_IAC4;
#endif
		} else
			return -ENOSPC;
	}
out:
	child->thread.debug.dbcr0 |= DBCR0_IDM;
	child->thread.regs->msr |= MSR_DE;

	return slot;
}

static int del_instruction_bp(struct task_struct *child, int slot)
{
	switch (slot) {
	case 1:
		if ((child->thread.debug.dbcr0 & DBCR0_IAC1) == 0)
			return -ENOENT;

		if (dbcr_iac_range(child) & DBCR_IAC12MODE) {
			/* address range - clear slots 1 & 2 */
			child->thread.debug.iac2 = 0;
			dbcr_iac_range(child) &= ~DBCR_IAC12MODE;
		}
		child->thread.debug.iac1 = 0;
		child->thread.debug.dbcr0 &= ~DBCR0_IAC1;
		break;
	case 2:
		if ((child->thread.debug.dbcr0 & DBCR0_IAC2) == 0)
			return -ENOENT;

		if (dbcr_iac_range(child) & DBCR_IAC12MODE)
			/* used in a range */
			return -EINVAL;
		child->thread.debug.iac2 = 0;
		child->thread.debug.dbcr0 &= ~DBCR0_IAC2;
		break;
#if CONFIG_PPC_ADV_DEBUG_IACS > 2
	case 3:
		if ((child->thread.debug.dbcr0 & DBCR0_IAC3) == 0)
			return -ENOENT;

		if (dbcr_iac_range(child) & DBCR_IAC34MODE) {
			/* address range - clear slots 3 & 4 */
			child->thread.debug.iac4 = 0;
			dbcr_iac_range(child) &= ~DBCR_IAC34MODE;
		}
		child->thread.debug.iac3 = 0;
		child->thread.debug.dbcr0 &= ~DBCR0_IAC3;
		break;
	case 4:
		if ((child->thread.debug.dbcr0 & DBCR0_IAC4) == 0)
			return -ENOENT;

		if (dbcr_iac_range(child) & DBCR_IAC34MODE)
			/* Used in a range */
			return -EINVAL;
		child->thread.debug.iac4 = 0;
		child->thread.debug.dbcr0 &= ~DBCR0_IAC4;
		break;
#endif
	default:
		return -EINVAL;
	}
	return 0;
}

static int set_dac(struct task_struct *child, struct ppc_hw_breakpoint *bp_info)
{
	int byte_enable =
		(bp_info->condition_mode >> PPC_BREAKPOINT_CONDITION_BE_SHIFT)
		& 0xf;
	int condition_mode =
		bp_info->condition_mode & PPC_BREAKPOINT_CONDITION_MODE;
	int slot;

	if (byte_enable && (condition_mode == 0))
		return -EINVAL;

	if (bp_info->addr >= TASK_SIZE)
		return -EIO;

	if ((dbcr_dac(child) & (DBCR_DAC1R | DBCR_DAC1W)) == 0) {
		slot = 1;
		if (bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_READ)
			dbcr_dac(child) |= DBCR_DAC1R;
		if (bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_WRITE)
			dbcr_dac(child) |= DBCR_DAC1W;
		child->thread.debug.dac1 = (unsigned long)bp_info->addr;
#if CONFIG_PPC_ADV_DEBUG_DVCS > 0
		if (byte_enable) {
			child->thread.debug.dvc1 =
				(unsigned long)bp_info->condition_value;
			child->thread.debug.dbcr2 |=
				((byte_enable << DBCR2_DVC1BE_SHIFT) |
				 (condition_mode << DBCR2_DVC1M_SHIFT));
		}
#endif
#ifdef CONFIG_PPC_ADV_DEBUG_DAC_RANGE
	} else if (child->thread.debug.dbcr2 & DBCR2_DAC12MODE) {
		/* Both dac1 and dac2 are part of a range */
		return -ENOSPC;
#endif
	} else if ((dbcr_dac(child) & (DBCR_DAC2R | DBCR_DAC2W)) == 0) {
		slot = 2;
		if (bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_READ)
			dbcr_dac(child) |= DBCR_DAC2R;
		if (bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_WRITE)
			dbcr_dac(child) |= DBCR_DAC2W;
		child->thread.debug.dac2 = (unsigned long)bp_info->addr;
#if CONFIG_PPC_ADV_DEBUG_DVCS > 0
		if (byte_enable) {
			child->thread.debug.dvc2 =
				(unsigned long)bp_info->condition_value;
			child->thread.debug.dbcr2 |=
				((byte_enable << DBCR2_DVC2BE_SHIFT) |
				 (condition_mode << DBCR2_DVC2M_SHIFT));
		}
#endif
	} else
		return -ENOSPC;
	child->thread.debug.dbcr0 |= DBCR0_IDM;
	child->thread.regs->msr |= MSR_DE;

	return slot + 4;
}

static int del_dac(struct task_struct *child, int slot)
{
	if (slot == 1) {
		if ((dbcr_dac(child) & (DBCR_DAC1R | DBCR_DAC1W)) == 0)
			return -ENOENT;

		child->thread.debug.dac1 = 0;
		dbcr_dac(child) &= ~(DBCR_DAC1R | DBCR_DAC1W);
#ifdef CONFIG_PPC_ADV_DEBUG_DAC_RANGE
		if (child->thread.debug.dbcr2 & DBCR2_DAC12MODE) {
			child->thread.debug.dac2 = 0;
			child->thread.debug.dbcr2 &= ~DBCR2_DAC12MODE;
		}
		child->thread.debug.dbcr2 &= ~(DBCR2_DVC1M | DBCR2_DVC1BE);
#endif
#if CONFIG_PPC_ADV_DEBUG_DVCS > 0
		child->thread.debug.dvc1 = 0;
#endif
	} else if (slot == 2) {
		if ((dbcr_dac(child) & (DBCR_DAC2R | DBCR_DAC2W)) == 0)
			return -ENOENT;

#ifdef CONFIG_PPC_ADV_DEBUG_DAC_RANGE
		if (child->thread.debug.dbcr2 & DBCR2_DAC12MODE)
			/* Part of a range */
			return -EINVAL;
		child->thread.debug.dbcr2 &= ~(DBCR2_DVC2M | DBCR2_DVC2BE);
#endif
#if CONFIG_PPC_ADV_DEBUG_DVCS > 0
		child->thread.debug.dvc2 = 0;
#endif
		child->thread.debug.dac2 = 0;
		dbcr_dac(child) &= ~(DBCR_DAC2R | DBCR_DAC2W);
	} else
		return -EINVAL;

	return 0;
}
#endif /* CONFIG_PPC_ADV_DEBUG_REGS */

#ifdef CONFIG_PPC_ADV_DEBUG_DAC_RANGE
static int set_dac_range(struct task_struct *child,
			 struct ppc_hw_breakpoint *bp_info)
{
	int mode = bp_info->addr_mode & PPC_BREAKPOINT_MODE_MASK;

	/* We don't allow range watchpoints to be used with DVC */
	if (bp_info->condition_mode)
		return -EINVAL;

	/*
	 * Best effort to verify the address range.  The user/supervisor bits
	 * prevent trapping in kernel space, but let's fail on an obvious bad
	 * range.  The simple test on the mask is not fool-proof, and any
	 * exclusive range will spill over into kernel space.
	 */
	if (bp_info->addr >= TASK_SIZE)
		return -EIO;
	if (mode == PPC_BREAKPOINT_MODE_MASK) {
		/*
		 * dac2 is a bitmask.  Don't allow a mask that makes a
		 * kernel space address from a valid dac1 value
		 */
		if (~((unsigned long)bp_info->addr2) >= TASK_SIZE)
			return -EIO;
	} else {
		/*
		 * For range breakpoints, addr2 must also be a valid address
		 */
		if (bp_info->addr2 >= TASK_SIZE)
			return -EIO;
	}

	if (child->thread.debug.dbcr0 &
	    (DBCR0_DAC1R | DBCR0_DAC1W | DBCR0_DAC2R | DBCR0_DAC2W))
		return -ENOSPC;

	if (bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_READ)
		child->thread.debug.dbcr0 |= (DBCR0_DAC1R | DBCR0_IDM);
	if (bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_WRITE)
		child->thread.debug.dbcr0 |= (DBCR0_DAC1W | DBCR0_IDM);
	child->thread.debug.dac1 = bp_info->addr;
	child->thread.debug.dac2 = bp_info->addr2;
	if (mode == PPC_BREAKPOINT_MODE_RANGE_INCLUSIVE)
		child->thread.debug.dbcr2  |= DBCR2_DAC12M;
	else if (mode == PPC_BREAKPOINT_MODE_RANGE_EXCLUSIVE)
		child->thread.debug.dbcr2  |= DBCR2_DAC12MX;
	else	/* PPC_BREAKPOINT_MODE_MASK */
		child->thread.debug.dbcr2  |= DBCR2_DAC12MM;
	child->thread.regs->msr |= MSR_DE;

	return 5;
}
#endif /* CONFIG_PPC_ADV_DEBUG_DAC_RANGE */

static long ppc_set_hwdebug(struct task_struct *child,
		     struct ppc_hw_breakpoint *bp_info)
{
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	int len = 0;
	struct thread_struct *thread = &(child->thread);
	struct perf_event *bp;
	struct perf_event_attr attr;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */
#ifndef CONFIG_PPC_ADV_DEBUG_REGS
	struct arch_hw_breakpoint brk;
#endif

	if (bp_info->version != 1)
		return -ENOTSUPP;
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
	/*
	 * Check for invalid flags and combinations
	 */
	if ((bp_info->trigger_type == 0) ||
	    (bp_info->trigger_type & ~(PPC_BREAKPOINT_TRIGGER_EXECUTE |
				       PPC_BREAKPOINT_TRIGGER_RW)) ||
	    (bp_info->addr_mode & ~PPC_BREAKPOINT_MODE_MASK) ||
	    (bp_info->condition_mode &
	     ~(PPC_BREAKPOINT_CONDITION_MODE |
	       PPC_BREAKPOINT_CONDITION_BE_ALL)))
		return -EINVAL;
#if CONFIG_PPC_ADV_DEBUG_DVCS == 0
	if (bp_info->condition_mode != PPC_BREAKPOINT_CONDITION_NONE)
		return -EINVAL;
#endif

	if (bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_EXECUTE) {
		if ((bp_info->trigger_type != PPC_BREAKPOINT_TRIGGER_EXECUTE) ||
		    (bp_info->condition_mode != PPC_BREAKPOINT_CONDITION_NONE))
			return -EINVAL;
		return set_instruction_bp(child, bp_info);
	}
	if (bp_info->addr_mode == PPC_BREAKPOINT_MODE_EXACT)
		return set_dac(child, bp_info);

#ifdef CONFIG_PPC_ADV_DEBUG_DAC_RANGE
	return set_dac_range(child, bp_info);
#else
	return -EINVAL;
#endif
#else /* !CONFIG_PPC_ADV_DEBUG_DVCS */
	/*
	 * We only support one data breakpoint
	 */
	if ((bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_RW) == 0 ||
	    (bp_info->trigger_type & ~PPC_BREAKPOINT_TRIGGER_RW) != 0 ||
	    bp_info->condition_mode != PPC_BREAKPOINT_CONDITION_NONE)
		return -EINVAL;

	if ((unsigned long)bp_info->addr >= TASK_SIZE)
		return -EIO;

	brk.address = bp_info->addr & ~7UL;
	brk.type = HW_BRK_TYPE_TRANSLATE;
	brk.len = 8;
	if (bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_READ)
		brk.type |= HW_BRK_TYPE_READ;
	if (bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_WRITE)
		brk.type |= HW_BRK_TYPE_WRITE;
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	/*
	 * Check if the request is for 'range' breakpoints. We can
	 * support it if range < 8 bytes.
	 */
	if (bp_info->addr_mode == PPC_BREAKPOINT_MODE_RANGE_INCLUSIVE)
		len = bp_info->addr2 - bp_info->addr;
	else if (bp_info->addr_mode == PPC_BREAKPOINT_MODE_EXACT)
		len = 1;
	else
		return -EINVAL;
	bp = thread->ptrace_bps[0];
	if (bp)
		return -ENOSPC;

	/* Create a new breakpoint request if one doesn't exist already */
	hw_breakpoint_init(&attr);
	attr.bp_addr = (unsigned long)bp_info->addr & ~HW_BREAKPOINT_ALIGN;
	attr.bp_len = len;
	arch_bp_generic_fields(brk.type, &attr.bp_type);

	thread->ptrace_bps[0] = bp = register_user_hw_breakpoint(&attr,
					       ptrace_triggered, NULL, child);
	if (IS_ERR(bp)) {
		thread->ptrace_bps[0] = NULL;
		return PTR_ERR(bp);
	}

	return 1;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */

	if (bp_info->addr_mode != PPC_BREAKPOINT_MODE_EXACT)
		return -EINVAL;

	if (child->thread.hw_brk.address)
		return -ENOSPC;

	if (!ppc_breakpoint_available())
		return -ENODEV;

	child->thread.hw_brk = brk;

	return 1;
#endif /* !CONFIG_PPC_ADV_DEBUG_DVCS */
}

static long ppc_del_hwdebug(struct task_struct *child, long data)
{
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	int ret = 0;
	struct thread_struct *thread = &(child->thread);
	struct perf_event *bp;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
	int rc;

	if (data <= 4)
		rc = del_instruction_bp(child, (int)data);
	else
		rc = del_dac(child, (int)data - 4);

	if (!rc) {
		if (!DBCR_ACTIVE_EVENTS(child->thread.debug.dbcr0,
					child->thread.debug.dbcr1)) {
			child->thread.debug.dbcr0 &= ~DBCR0_IDM;
			child->thread.regs->msr &= ~MSR_DE;
		}
	}
	return rc;
#else
	if (data != 1)
		return -EINVAL;

#ifdef CONFIG_HAVE_HW_BREAKPOINT
	bp = thread->ptrace_bps[0];
	if (bp) {
		unregister_hw_breakpoint(bp);
		thread->ptrace_bps[0] = NULL;
	} else
		ret = -ENOENT;
	return ret;
#else /* CONFIG_HAVE_HW_BREAKPOINT */
	if (child->thread.hw_brk.address == 0)
		return -ENOENT;

	child->thread.hw_brk.address = 0;
	child->thread.hw_brk.type = 0;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */

	return 0;
#endif
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	int ret = -EPERM;
	void __user *datavp = (void __user *) data;
	unsigned long __user *datalp = datavp;

	switch (request) {
	/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		unsigned long index, tmp;

		ret = -EIO;
		/* convert to index and check */
#ifdef CONFIG_PPC32
		index = addr >> 2;
		if ((addr & 3) || (index > PT_FPSCR)
		    || (child->thread.regs == NULL))
#else
		index = addr >> 3;
		if ((addr & 7) || (index > PT_FPSCR))
#endif
			break;

		CHECK_FULL_REGS(child->thread.regs);
		if (index < PT_FPR0) {
			ret = ptrace_get_reg(child, (int) index, &tmp);
			if (ret)
				break;
		} else {
			unsigned int fpidx = index - PT_FPR0;

			flush_fp_to_thread(child);
			if (fpidx < (PT_FPSCR - PT_FPR0))
				memcpy(&tmp, &child->thread.TS_FPR(fpidx),
				       sizeof(long));
			else
				tmp = child->thread.fp_state.fpscr;
		}
		ret = put_user(tmp, datalp);
		break;
	}

	/* write the word at location addr in the USER area */
	case PTRACE_POKEUSR: {
		unsigned long index;

		ret = -EIO;
		/* convert to index and check */
#ifdef CONFIG_PPC32
		index = addr >> 2;
		if ((addr & 3) || (index > PT_FPSCR)
		    || (child->thread.regs == NULL))
#else
		index = addr >> 3;
		if ((addr & 7) || (index > PT_FPSCR))
#endif
			break;

		CHECK_FULL_REGS(child->thread.regs);
		if (index < PT_FPR0) {
			ret = ptrace_put_reg(child, index, data);
		} else {
			unsigned int fpidx = index - PT_FPR0;

			flush_fp_to_thread(child);
			if (fpidx < (PT_FPSCR - PT_FPR0))
				memcpy(&child->thread.TS_FPR(fpidx), &data,
				       sizeof(long));
			else
				child->thread.fp_state.fpscr = data;
			ret = 0;
		}
		break;
	}

	case PPC_PTRACE_GETHWDBGINFO: {
		struct ppc_debug_info dbginfo;

		dbginfo.version = 1;
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
		dbginfo.num_instruction_bps = CONFIG_PPC_ADV_DEBUG_IACS;
		dbginfo.num_data_bps = CONFIG_PPC_ADV_DEBUG_DACS;
		dbginfo.num_condition_regs = CONFIG_PPC_ADV_DEBUG_DVCS;
		dbginfo.data_bp_alignment = 4;
		dbginfo.sizeof_condition = 4;
		dbginfo.features = PPC_DEBUG_FEATURE_INSN_BP_RANGE |
				   PPC_DEBUG_FEATURE_INSN_BP_MASK;
#ifdef CONFIG_PPC_ADV_DEBUG_DAC_RANGE
		dbginfo.features |=
				   PPC_DEBUG_FEATURE_DATA_BP_RANGE |
				   PPC_DEBUG_FEATURE_DATA_BP_MASK;
#endif
#else /* !CONFIG_PPC_ADV_DEBUG_REGS */
		dbginfo.num_instruction_bps = 0;
		if (ppc_breakpoint_available())
			dbginfo.num_data_bps = 1;
		else
			dbginfo.num_data_bps = 0;
		dbginfo.num_condition_regs = 0;
#ifdef CONFIG_PPC64
		dbginfo.data_bp_alignment = 8;
#else
		dbginfo.data_bp_alignment = 4;
#endif
		dbginfo.sizeof_condition = 0;
#ifdef CONFIG_HAVE_HW_BREAKPOINT
		dbginfo.features = PPC_DEBUG_FEATURE_DATA_BP_RANGE;
		if (cpu_has_feature(CPU_FTR_DAWR))
			dbginfo.features |= PPC_DEBUG_FEATURE_DATA_BP_DAWR;
#else
		dbginfo.features = 0;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */
#endif /* CONFIG_PPC_ADV_DEBUG_REGS */

		if (copy_to_user(datavp, &dbginfo,
				 sizeof(struct ppc_debug_info)))
			return -EFAULT;
		return 0;
	}

	case PPC_PTRACE_SETHWDEBUG: {
		struct ppc_hw_breakpoint bp_info;

		if (copy_from_user(&bp_info, datavp,
				   sizeof(struct ppc_hw_breakpoint)))
			return -EFAULT;
		return ppc_set_hwdebug(child, &bp_info);
	}

	case PPC_PTRACE_DELHWDEBUG: {
		ret = ppc_del_hwdebug(child, data);
		break;
	}

	case PTRACE_GET_DEBUGREG: {
#ifndef CONFIG_PPC_ADV_DEBUG_REGS
		unsigned long dabr_fake;
#endif
		ret = -EINVAL;
		/* We only support one DABR and no IABRS at the moment */
		if (addr > 0)
			break;
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
		ret = put_user(child->thread.debug.dac1, datalp);
#else
		dabr_fake = ((child->thread.hw_brk.address & (~HW_BRK_TYPE_DABR)) |
			     (child->thread.hw_brk.type & HW_BRK_TYPE_DABR));
		ret = put_user(dabr_fake, datalp);
#endif
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
					   0, sizeof(struct user_pt_regs),
					   datavp);

#ifdef CONFIG_PPC64
	case PTRACE_SETREGS64:
#endif
	case PTRACE_SETREGS:	/* Set all gp regs in the child. */
		return copy_regset_from_user(child, &user_ppc_native_view,
					     REGSET_GPR,
					     0, sizeof(struct user_pt_regs),
					     datavp);

	case PTRACE_GETFPREGS: /* Get the child FPU state (FPR0...31 + FPSCR) */
		return copy_regset_to_user(child, &user_ppc_native_view,
					   REGSET_FPR,
					   0, sizeof(elf_fpregset_t),
					   datavp);

	case PTRACE_SETFPREGS: /* Set the child FPU state (FPR0...31 + FPSCR) */
		return copy_regset_from_user(child, &user_ppc_native_view,
					     REGSET_FPR,
					     0, sizeof(elf_fpregset_t),
					     datavp);

#ifdef CONFIG_ALTIVEC
	case PTRACE_GETVRREGS:
		return copy_regset_to_user(child, &user_ppc_native_view,
					   REGSET_VMX,
					   0, (33 * sizeof(vector128) +
					       sizeof(u32)),
					   datavp);

	case PTRACE_SETVRREGS:
		return copy_regset_from_user(child, &user_ppc_native_view,
					     REGSET_VMX,
					     0, (33 * sizeof(vector128) +
						 sizeof(u32)),
					     datavp);
#endif
#ifdef CONFIG_VSX
	case PTRACE_GETVSRREGS:
		return copy_regset_to_user(child, &user_ppc_native_view,
					   REGSET_VSX,
					   0, 32 * sizeof(double),
					   datavp);

	case PTRACE_SETVSRREGS:
		return copy_regset_from_user(child, &user_ppc_native_view,
					     REGSET_VSX,
					     0, 32 * sizeof(double),
					     datavp);
#endif
#ifdef CONFIG_SPE
	case PTRACE_GETEVRREGS:
		/* Get the child spe register state. */
		return copy_regset_to_user(child, &user_ppc_native_view,
					   REGSET_SPE, 0, 35 * sizeof(u32),
					   datavp);

	case PTRACE_SETEVRREGS:
		/* Set the child spe register state. */
		return copy_regset_from_user(child, &user_ppc_native_view,
					     REGSET_SPE, 0, 35 * sizeof(u32),
					     datavp);
#endif

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}
	return ret;
}

#ifdef CONFIG_SECCOMP
static int do_seccomp(struct pt_regs *regs)
{
	if (!test_thread_flag(TIF_SECCOMP))
		return 0;

	/*
	 * The ABI we present to seccomp tracers is that r3 contains
	 * the syscall return value and orig_gpr3 contains the first
	 * syscall parameter. This is different to the ptrace ABI where
	 * both r3 and orig_gpr3 contain the first syscall parameter.
	 */
	regs->gpr[3] = -ENOSYS;

	/*
	 * We use the __ version here because we have already checked
	 * TIF_SECCOMP. If this fails, there is nothing left to do, we
	 * have already loaded -ENOSYS into r3, or seccomp has put
	 * something else in r3 (via SECCOMP_RET_ERRNO/TRACE).
	 */
	if (__secure_computing(NULL))
		return -1;

	/*
	 * The syscall was allowed by seccomp, restore the register
	 * state to what audit expects.
	 * Note that we use orig_gpr3, which means a seccomp tracer can
	 * modify the first syscall parameter (in orig_gpr3) and also
	 * allow the syscall to proceed.
	 */
	regs->gpr[3] = regs->orig_gpr3;

	return 0;
}
#else
static inline int do_seccomp(struct pt_regs *regs) { return 0; }
#endif /* CONFIG_SECCOMP */

/**
 * do_syscall_trace_enter() - Do syscall tracing on kernel entry.
 * @regs: the pt_regs of the task to trace (current)
 *
 * Performs various types of tracing on syscall entry. This includes seccomp,
 * ptrace, syscall tracepoints and audit.
 *
 * The pt_regs are potentially visible to userspace via ptrace, so their
 * contents is ABI.
 *
 * One or more of the tracers may modify the contents of pt_regs, in particular
 * to modify arguments or even the syscall number itself.
 *
 * It's also possible that a tracer can choose to reject the system call. In
 * that case this function will return an illegal syscall number, and will put
 * an appropriate return value in regs->r3.
 *
 * Return: the (possibly changed) syscall number.
 */
long do_syscall_trace_enter(struct pt_regs *regs)
{
	u32 flags;

	user_exit();

	flags = READ_ONCE(current_thread_info()->flags) &
		(_TIF_SYSCALL_EMU | _TIF_SYSCALL_TRACE);

	if (flags) {
		int rc = tracehook_report_syscall_entry(regs);

		if (unlikely(flags & _TIF_SYSCALL_EMU)) {
			/*
			 * A nonzero return code from
			 * tracehook_report_syscall_entry() tells us to prevent
			 * the syscall execution, but we are not going to
			 * execute it anyway.
			 *
			 * Returning -1 will skip the syscall execution. We want
			 * to avoid clobbering any registers, so we don't goto
			 * the skip label below.
			 */
			return -1;
		}

		if (rc) {
			/*
			 * The tracer decided to abort the syscall. Note that
			 * the tracer may also just change regs->gpr[0] to an
			 * invalid syscall number, that is handled below on the
			 * exit path.
			 */
			goto skip;
		}
	}

	/* Run seccomp after ptrace; allow it to set gpr[3]. */
	if (do_seccomp(regs))
		return -1;

	/* Avoid trace and audit when syscall is invalid. */
	if (regs->gpr[0] >= NR_syscalls)
		goto skip;

	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_enter(regs, regs->gpr[0]);

#ifdef CONFIG_PPC64
	if (!is_32bit_task())
		audit_syscall_entry(regs->gpr[0], regs->gpr[3], regs->gpr[4],
				    regs->gpr[5], regs->gpr[6]);
	else
#endif
		audit_syscall_entry(regs->gpr[0],
				    regs->gpr[3] & 0xffffffff,
				    regs->gpr[4] & 0xffffffff,
				    regs->gpr[5] & 0xffffffff,
				    regs->gpr[6] & 0xffffffff);

	/* Return the possibly modified but valid syscall number */
	return regs->gpr[0];

skip:
	/*
	 * If we are aborting explicitly, or if the syscall number is
	 * now invalid, set the return value to -ENOSYS.
	 */
	regs->gpr[3] = -ENOSYS;
	return -1;
}

void do_syscall_trace_leave(struct pt_regs *regs)
{
	int step;

	audit_syscall_exit(regs);

	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_exit(regs, regs->result);

	step = test_thread_flag(TIF_SINGLESTEP);
	if (step || test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, step);

	user_enter();
}

void __init pt_regs_check(void)
{
	BUILD_BUG_ON(offsetof(struct pt_regs, gpr) !=
		     offsetof(struct user_pt_regs, gpr));
	BUILD_BUG_ON(offsetof(struct pt_regs, nip) !=
		     offsetof(struct user_pt_regs, nip));
	BUILD_BUG_ON(offsetof(struct pt_regs, msr) !=
		     offsetof(struct user_pt_regs, msr));
	BUILD_BUG_ON(offsetof(struct pt_regs, msr) !=
		     offsetof(struct user_pt_regs, msr));
	BUILD_BUG_ON(offsetof(struct pt_regs, orig_gpr3) !=
		     offsetof(struct user_pt_regs, orig_gpr3));
	BUILD_BUG_ON(offsetof(struct pt_regs, ctr) !=
		     offsetof(struct user_pt_regs, ctr));
	BUILD_BUG_ON(offsetof(struct pt_regs, link) !=
		     offsetof(struct user_pt_regs, link));
	BUILD_BUG_ON(offsetof(struct pt_regs, xer) !=
		     offsetof(struct user_pt_regs, xer));
	BUILD_BUG_ON(offsetof(struct pt_regs, ccr) !=
		     offsetof(struct user_pt_regs, ccr));
#ifdef __powerpc64__
	BUILD_BUG_ON(offsetof(struct pt_regs, softe) !=
		     offsetof(struct user_pt_regs, softe));
#else
	BUILD_BUG_ON(offsetof(struct pt_regs, mq) !=
		     offsetof(struct user_pt_regs, mq));
#endif
	BUILD_BUG_ON(offsetof(struct pt_regs, trap) !=
		     offsetof(struct user_pt_regs, trap));
	BUILD_BUG_ON(offsetof(struct pt_regs, dar) !=
		     offsetof(struct user_pt_regs, dar));
	BUILD_BUG_ON(offsetof(struct pt_regs, dsisr) !=
		     offsetof(struct user_pt_regs, dsisr));
	BUILD_BUG_ON(offsetof(struct pt_regs, result) !=
		     offsetof(struct user_pt_regs, result));

	BUILD_BUG_ON(sizeof(struct user_pt_regs) > sizeof(struct pt_regs));
}
