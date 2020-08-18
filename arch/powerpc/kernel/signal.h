/*
 *    Copyright (c) 2007 Benjamin Herrenschmidt, IBM Corporation
 *    Extracted from signal_32.c and signal_64.c
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file README.legal in the main directory of
 * this archive for more details.
 */

#ifndef _POWERPC_ARCH_SIGNAL_H
#define _POWERPC_ARCH_SIGNAL_H

void __user *get_sigframe(struct ksignal *ksig, struct task_struct *tsk,
			  size_t frame_size, int is_32);

extern int handle_signal32(struct ksignal *ksig, sigset_t *oldset,
			   struct task_struct *tsk);

extern int handle_rt_signal32(struct ksignal *ksig, sigset_t *oldset,
			      struct task_struct *tsk);

#ifdef CONFIG_VSX
extern unsigned long copy_vsx_to_user(void __user *to,
				      struct task_struct *task);
extern unsigned long copy_ckvsx_to_user(void __user *to,
					       struct task_struct *task);
extern unsigned long copy_vsx_from_user(struct task_struct *task,
					void __user *from);
extern unsigned long copy_ckvsx_from_user(struct task_struct *task,
						 void __user *from);
unsigned long copy_fpr_to_user(void __user *to, struct task_struct *task);
unsigned long copy_ckfpr_to_user(void __user *to, struct task_struct *task);
unsigned long copy_fpr_from_user(struct task_struct *task, void __user *from);
unsigned long copy_ckfpr_from_user(struct task_struct *task, void __user *from);
#elif defined(CONFIG_PPC_FPU_REGS)
static inline unsigned long
copy_fpr_to_user(void __user *to, struct task_struct *task)
{
	return __copy_to_user(to, task->thread.fp_state.fpr,
			      ELF_NFPREG * sizeof(double));
}

static inline unsigned long
copy_fpr_from_user(struct task_struct *task, void __user *from)
{
	return __copy_from_user(task->thread.fp_state.fpr, from,
			      ELF_NFPREG * sizeof(double));
}

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
inline unsigned long copy_ckfpr_to_user(void __user *to, struct task_struct *task)
{
	return __copy_to_user(to, task->thread.ckfp_state.fpr,
			      ELF_NFPREG * sizeof(double));
}

static inline unsigned long
copy_ckfpr_from_user(struct task_struct *task, void __user *from)
{
	return __copy_from_user(task->thread.ckfp_state.fpr, from,
				ELF_NFPREG * sizeof(double));
}
#endif /* CONFIG_PPC_TRANSACTIONAL_MEM */
#else
static inline unsigned long
copy_fpr_to_user(void __user *to, struct task_struct *task)
{
	return 0;
}

static inline unsigned long
copy_fpr_from_user(struct task_struct *task, void __user *from)
{
	return 0;
}
#endif

#ifdef CONFIG_PPC64

extern int handle_rt_signal64(struct ksignal *ksig, sigset_t *set,
			      struct task_struct *tsk);

#else /* CONFIG_PPC64 */

extern long sys_rt_sigreturn(void);
extern long sys_sigreturn(void);

static inline int handle_rt_signal64(struct ksignal *ksig, sigset_t *set,
				     struct task_struct *tsk)
{
	return -EFAULT;
}

#endif /* !defined(CONFIG_PPC64) */

void signal_fault(struct task_struct *tsk, struct pt_regs *regs,
		  const char *where, void __user *ptr);

#endif  /* _POWERPC_ARCH_SIGNAL_H */
