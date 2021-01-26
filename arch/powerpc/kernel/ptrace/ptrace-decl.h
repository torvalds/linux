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

/* ptrace-(no)vsx */

user_regset_get2_fn fpr_get;
int fpr_set(struct task_struct *target, const struct user_regset *regset,
	    unsigned int pos, unsigned int count,
	    const void *kbuf, const void __user *ubuf);

/* ptrace-vsx */

int vsr_active(struct task_struct *target, const struct user_regset *regset);
user_regset_get2_fn vsr_get;
int vsr_set(struct task_struct *target, const struct user_regset *regset,
	    unsigned int pos, unsigned int count,
	    const void *kbuf, const void __user *ubuf);

/* ptrace-altivec */

int vr_active(struct task_struct *target, const struct user_regset *regset);
user_regset_get2_fn vr_get;
int vr_set(struct task_struct *target, const struct user_regset *regset,
	   unsigned int pos, unsigned int count,
	   const void *kbuf, const void __user *ubuf);

/* ptrace-spe */

int evr_active(struct task_struct *target, const struct user_regset *regset);
user_regset_get2_fn evr_get;
int evr_set(struct task_struct *target, const struct user_regset *regset,
	    unsigned int pos, unsigned int count,
	    const void *kbuf, const void __user *ubuf);

/* ptrace */

int gpr32_get_common(struct task_struct *target,
		     const struct user_regset *regset,
		     struct membuf to,
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
user_regset_get2_fn tm_cgpr_get;
int tm_cgpr_set(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		const void *kbuf, const void __user *ubuf);
int tm_cfpr_active(struct task_struct *target, const struct user_regset *regset);
user_regset_get2_fn tm_cfpr_get;
int tm_cfpr_set(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		const void *kbuf, const void __user *ubuf);
int tm_cvmx_active(struct task_struct *target, const struct user_regset *regset);
user_regset_get2_fn tm_cvmx_get;
int tm_cvmx_set(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		const void *kbuf, const void __user *ubuf);
int tm_cvsx_active(struct task_struct *target, const struct user_regset *regset);
user_regset_get2_fn tm_cvsx_get;
int tm_cvsx_set(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		const void *kbuf, const void __user *ubuf);
int tm_spr_active(struct task_struct *target, const struct user_regset *regset);
user_regset_get2_fn tm_spr_get;
int tm_spr_set(struct task_struct *target, const struct user_regset *regset,
	       unsigned int pos, unsigned int count,
	       const void *kbuf, const void __user *ubuf);
int tm_tar_active(struct task_struct *target, const struct user_regset *regset);
user_regset_get2_fn tm_tar_get;
int tm_tar_set(struct task_struct *target, const struct user_regset *regset,
	       unsigned int pos, unsigned int count,
	       const void *kbuf, const void __user *ubuf);
int tm_ppr_active(struct task_struct *target, const struct user_regset *regset);
user_regset_get2_fn tm_ppr_get;
int tm_ppr_set(struct task_struct *target, const struct user_regset *regset,
	       unsigned int pos, unsigned int count,
	       const void *kbuf, const void __user *ubuf);
int tm_dscr_active(struct task_struct *target, const struct user_regset *regset);
user_regset_get2_fn tm_dscr_get;
int tm_dscr_set(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		const void *kbuf, const void __user *ubuf);
user_regset_get2_fn tm_cgpr32_get;
int tm_cgpr32_set(struct task_struct *target, const struct user_regset *regset,
		  unsigned int pos, unsigned int count,
		  const void *kbuf, const void __user *ubuf);

/* ptrace-view */

extern const struct user_regset_view user_ppc_native_view;

/* ptrace-(no)adv */
void ppc_gethwdinfo(struct ppc_debug_info *dbginfo);
int ptrace_get_debugreg(struct task_struct *child, unsigned long addr,
			unsigned long __user *datalp);
int ptrace_set_debugreg(struct task_struct *task, unsigned long addr, unsigned long data);
long ppc_set_hwdebug(struct task_struct *child, struct ppc_hw_breakpoint *bp_info);
long ppc_del_hwdebug(struct task_struct *child, long data);
