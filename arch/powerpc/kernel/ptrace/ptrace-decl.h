/* SPDX-License-Identifier: GPL-2.0-or-later */

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

/* ptrace */

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
void flush_tmregs_to_thread(struct task_struct *tsk);
#else
static inline void flush_tmregs_to_thread(struct task_struct *tsk) { }
#endif
