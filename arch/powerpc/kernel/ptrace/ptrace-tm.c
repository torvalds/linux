// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/regset.h>

#include <asm/switch_to.h>
#include <asm/tm.h>
#include <asm/asm-prototypes.h>

#include "ptrace-decl.h"

void flush_tmregs_to_thread(struct task_struct *tsk)
{
	/*
	 * If task is not current, it will have been flushed already to
	 * its thread_struct during __switch_to().
	 *
	 * A reclaim flushes ALL the state or if not in TM save TM SPRs
	 * in the appropriate thread structures from live.
	 */

	if (!cpu_has_feature(CPU_FTR_TM) || tsk != current)
		return;

	if (MSR_TM_SUSPENDED(mfmsr())) {
		tm_reclaim_current(TM_CAUSE_SIGNAL);
	} else {
		tm_enable();
		tm_save_sprs(&tsk->thread);
	}
}

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
	set_trap(&task->thread.ckpt_regs, trap);
	return 0;
}

/**
 * tm_cgpr_active - get active number of registers in CGPR
 * @target:	The target task.
 * @regset:	The user regset structure.
 *
 * This function checks for the active number of available
 * regisers in transaction checkpointed GPR category.
 */
int tm_cgpr_active(struct task_struct *target, const struct user_regset *regset)
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
 * @to:		Destination of copy.
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
int tm_cgpr_get(struct task_struct *target, const struct user_regset *regset,
		struct membuf to)
{
	struct membuf to_msr = membuf_at(&to, offsetof(struct pt_regs, msr));
#ifdef CONFIG_PPC64
	struct membuf to_softe = membuf_at(&to, offsetof(struct pt_regs, softe));
#endif

	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return -ENODATA;

	flush_tmregs_to_thread(target);
	flush_fp_to_thread(target);
	flush_altivec_to_thread(target);

	membuf_write(&to, &target->thread.ckpt_regs, sizeof(struct user_pt_regs));

	membuf_store(&to_msr, get_user_ckpt_msr(target));
#ifdef CONFIG_PPC64
	membuf_store(&to_softe, 0x1ul);
#endif
	return membuf_zero(&to, ELF_NGREG * sizeof(unsigned long) -
			sizeof(struct user_pt_regs));
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
int tm_cgpr_set(struct task_struct *target, const struct user_regset *regset,
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
		user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
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
		user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
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
int tm_cfpr_active(struct task_struct *target, const struct user_regset *regset)
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
 * @to:		Destination of copy.
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
int tm_cfpr_get(struct task_struct *target, const struct user_regset *regset,
		struct membuf to)
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
	return membuf_write(&to, buf, sizeof(buf));
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
int tm_cfpr_set(struct task_struct *target, const struct user_regset *regset,
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
int tm_cvmx_active(struct task_struct *target, const struct user_regset *regset)
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
 * @to:		Destination of copy.
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
int tm_cvmx_get(struct task_struct *target, const struct user_regset *regset,
		struct membuf to)
{
	union {
		elf_vrreg_t reg;
		u32 word;
	} vrsave;
	BUILD_BUG_ON(TVSO(vscr) != TVSO(vr[32]));

	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return -ENODATA;

	/* Flush the state */
	flush_tmregs_to_thread(target);
	flush_fp_to_thread(target);
	flush_altivec_to_thread(target);

	membuf_write(&to, &target->thread.ckvr_state, 33 * sizeof(vector128));
	/*
	 * Copy out only the low-order word of vrsave.
	 */
	memset(&vrsave, 0, sizeof(vrsave));
	vrsave.word = target->thread.ckvrsave;
	return membuf_write(&to, &vrsave, sizeof(vrsave));
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
int tm_cvmx_set(struct task_struct *target, const struct user_regset *regset,
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

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &target->thread.ckvr_state,
				 0, 33 * sizeof(vector128));
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
int tm_cvsx_active(struct task_struct *target, const struct user_regset *regset)
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
 * @to:		Destination of copy.
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
int tm_cvsx_get(struct task_struct *target, const struct user_regset *regset,
		struct membuf to)
{
	u64 buf[32];
	int i;

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
	return membuf_write(&to, buf, 32 * sizeof(double));
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
int tm_cvsx_set(struct task_struct *target, const struct user_regset *regset,
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
int tm_spr_active(struct task_struct *target, const struct user_regset *regset)
{
	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	return regset->n;
}

/**
 * tm_spr_get - get the TM related SPR registers
 * @target:	The target task.
 * @regset:	The user regset structure.
 * @to:		Destination of copy.
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
int tm_spr_get(struct task_struct *target, const struct user_regset *regset,
	       struct membuf to)
{
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
	membuf_write(&to, &target->thread.tm_tfhar, sizeof(u64));
	/* TEXASR register */
	membuf_write(&to, &target->thread.tm_texasr, sizeof(u64));
	/* TFIAR register */
	return membuf_write(&to, &target->thread.tm_tfiar, sizeof(u64));
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
int tm_spr_set(struct task_struct *target, const struct user_regset *regset,
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

int tm_tar_active(struct task_struct *target, const struct user_regset *regset)
{
	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (MSR_TM_ACTIVE(target->thread.regs->msr))
		return regset->n;

	return 0;
}

int tm_tar_get(struct task_struct *target, const struct user_regset *regset,
	       struct membuf to)
{
	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return -ENODATA;

	return membuf_write(&to, &target->thread.tm_tar, sizeof(u64));
}

int tm_tar_set(struct task_struct *target, const struct user_regset *regset,
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

int tm_ppr_active(struct task_struct *target, const struct user_regset *regset)
{
	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (MSR_TM_ACTIVE(target->thread.regs->msr))
		return regset->n;

	return 0;
}


int tm_ppr_get(struct task_struct *target, const struct user_regset *regset,
	       struct membuf to)
{
	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return -ENODATA;

	return membuf_write(&to, &target->thread.tm_ppr, sizeof(u64));
}

int tm_ppr_set(struct task_struct *target, const struct user_regset *regset,
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

int tm_dscr_active(struct task_struct *target, const struct user_regset *regset)
{
	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (MSR_TM_ACTIVE(target->thread.regs->msr))
		return regset->n;

	return 0;
}

int tm_dscr_get(struct task_struct *target, const struct user_regset *regset,
		struct membuf to)
{
	if (!cpu_has_feature(CPU_FTR_TM))
		return -ENODEV;

	if (!MSR_TM_ACTIVE(target->thread.regs->msr))
		return -ENODATA;

	return membuf_write(&to, &target->thread.tm_dscr, sizeof(u64));
}

int tm_dscr_set(struct task_struct *target, const struct user_regset *regset,
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

int tm_cgpr32_get(struct task_struct *target, const struct user_regset *regset,
		  struct membuf to)
{
	gpr32_get_common(target, regset, to,
				&target->thread.ckpt_regs.gpr[0]);
	return membuf_zero(&to, ELF_NGREG * sizeof(u32));
}

int tm_cgpr32_set(struct task_struct *target, const struct user_regset *regset,
		  unsigned int pos, unsigned int count,
		  const void *kbuf, const void __user *ubuf)
{
	return gpr32_set_common(target, regset, pos, count, kbuf, ubuf,
				&target->thread.ckpt_regs.gpr[0]);
}
