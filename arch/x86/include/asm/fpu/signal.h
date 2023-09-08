/* SPDX-License-Identifier: GPL-2.0 */
/*
 * x86 FPU signal frame handling methods:
 */
#ifndef _ASM_X86_FPU_SIGNAL_H
#define _ASM_X86_FPU_SIGNAL_H

#include <linux/compat.h>
#include <linux/user.h>

#include <asm/fpu/types.h>

#ifdef CONFIG_X86_64
# include <uapi/asm/sigcontext.h>
# include <asm/user32.h>
#else
# define user_i387_ia32_struct	user_i387_struct
# define user32_fxsr_struct	user_fxsr_struct
#endif

extern void convert_from_fxsr(struct user_i387_ia32_struct *env,
			      struct task_struct *tsk);
extern void convert_to_fxsr(struct fxregs_state *fxsave,
			    const struct user_i387_ia32_struct *env);

unsigned long
fpu__alloc_mathframe(unsigned long sp, int ia32_frame,
		     unsigned long *buf_fx, unsigned long *size);

unsigned long fpu__get_fpstate_size(void);

extern bool copy_fpstate_to_sigframe(void __user *buf, void __user *fp, int size);
extern void fpu__clear_user_states(struct fpu *fpu);
extern bool fpu__restore_sig(void __user *buf, int ia32_frame);

extern void restore_fpregs_from_fpstate(struct fpstate *fpstate, u64 mask);
#endif /* _ASM_X86_FPU_SIGNAL_H */
