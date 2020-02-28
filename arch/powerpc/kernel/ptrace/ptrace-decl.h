/* SPDX-License-Identifier: GPL-2.0-or-later */

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

#define TVSO(f)	(offsetof(struct thread_vr_state, f))
#define TFSO(f)	(offsetof(struct thread_fp_state, f))
#define TSO(f)	(offsetof(struct thread_struct, f))

/* ptrace-(no)vsx */

int fpr_get(struct task_struct *target, const struct user_regset *regset,
	    unsigned int pos, unsigned int count, void *kbuf, void __user *ubuf);
int fpr_set(struct task_struct *target, const struct user_regset *regset,
	    unsigned int pos, unsigned int count,
	    const void *kbuf, const void __user *ubuf);

/* ptrace-vsx */

int vsr_active(struct task_struct *target, const struct user_regset *regset);
int vsr_get(struct task_struct *target, const struct user_regset *regset,
	    unsigned int pos, unsigned int count, void *kbuf, void __user *ubuf);
int vsr_set(struct task_struct *target, const struct user_regset *regset,
	    unsigned int pos, unsigned int count,
	    const void *kbuf, const void __user *ubuf);

/* ptrace-altivec */

int vr_active(struct task_struct *target, const struct user_regset *regset);
int vr_get(struct task_struct *target, const struct user_regset *regset,
	   unsigned int pos, unsigned int count, void *kbuf, void __user *ubuf);
int vr_set(struct task_struct *target, const struct user_regset *regset,
	   unsigned int pos, unsigned int count,
	   const void *kbuf, const void __user *ubuf);

/* ptrace-spe */

int evr_active(struct task_struct *target, const struct user_regset *regset);
int evr_get(struct task_struct *target, const struct user_regset *regset,
	    unsigned int pos, unsigned int count, void *kbuf, void __user *ubuf);
int evr_set(struct task_struct *target, const struct user_regset *regset,
	    unsigned int pos, unsigned int count,
	    const void *kbuf, const void __user *ubuf);

/* ptrace */

int gpr32_get_common(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
			    void *kbuf, void __user *ubuf,
			    unsigned long *regs);
int gpr32_set_common(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     const void *kbuf, const void __user *ubuf,
		     unsigned long *regs);

/* ptrace-tm */

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
void flush_tmregs_to_thread(struct task_struct *tsk);
#else
static inline void flush_tmregs_to_thread(struct task_struct *tsk) { }
#endif

int tm_cgpr_active(struct task_struct *target, const struct user_regset *regset);
int tm_cgpr_get(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count, void *kbuf, void __user *ubuf);
int tm_cgpr_set(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		const void *kbuf, const void __user *ubuf);
int tm_cfpr_active(struct task_struct *target, const struct user_regset *regset);
int tm_cfpr_get(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count, void *kbuf, void __user *ubuf);
int tm_cfpr_set(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		const void *kbuf, const void __user *ubuf);
int tm_cvmx_active(struct task_struct *target, const struct user_regset *regset);
int tm_cvmx_get(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count, void *kbuf, void __user *ubuf);
int tm_cvmx_set(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		const void *kbuf, const void __user *ubuf);
int tm_cvsx_active(struct task_struct *target, const struct user_regset *regset);
int tm_cvsx_get(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count, void *kbuf, void __user *ubuf);
int tm_cvsx_set(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		const void *kbuf, const void __user *ubuf);
int tm_spr_active(struct task_struct *target, const struct user_regset *regset);
int tm_spr_get(struct task_struct *target, const struct user_regset *regset,
	       unsigned int pos, unsigned int count, void *kbuf, void __user *ubuf);
int tm_spr_set(struct task_struct *target, const struct user_regset *regset,
	       unsigned int pos, unsigned int count,
	       const void *kbuf, const void __user *ubuf);
int tm_tar_active(struct task_struct *target, const struct user_regset *regset);
int tm_tar_get(struct task_struct *target, const struct user_regset *regset,
	       unsigned int pos, unsigned int count, void *kbuf, void __user *ubuf);
int tm_tar_set(struct task_struct *target, const struct user_regset *regset,
	       unsigned int pos, unsigned int count,
	       const void *kbuf, const void __user *ubuf);
int tm_ppr_active(struct task_struct *target, const struct user_regset *regset);
int tm_ppr_get(struct task_struct *target, const struct user_regset *regset,
	       unsigned int pos, unsigned int count, void *kbuf, void __user *ubuf);
int tm_ppr_set(struct task_struct *target, const struct user_regset *regset,
	       unsigned int pos, unsigned int count,
	       const void *kbuf, const void __user *ubuf);
int tm_dscr_active(struct task_struct *target, const struct user_regset *regset);
int tm_dscr_get(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count, void *kbuf, void __user *ubuf);
int tm_dscr_set(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		const void *kbuf, const void __user *ubuf);
int tm_cgpr32_get(struct task_struct *target, const struct user_regset *regset,
		  unsigned int pos, unsigned int count, void *kbuf, void __user *ubuf);
int tm_cgpr32_set(struct task_struct *target, const struct user_regset *regset,
		  unsigned int pos, unsigned int count,
		  const void *kbuf, const void __user *ubuf);
