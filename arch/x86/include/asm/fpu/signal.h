/*
 * x86 FPU signal frame handling methods:
 */
#ifndef _ASM_X86_FPU_SIGNAL_H
#define _ASM_X86_FPU_SIGNAL_H

#ifdef CONFIG_X86_64
# include <uapi/asm/sigcontext.h>
# include <asm/user32.h>
struct ksignal;
int ia32_setup_rt_frame(int sig, struct ksignal *ksig,
			compat_sigset_t *set, struct pt_regs *regs);
int ia32_setup_frame(int sig, struct ksignal *ksig,
		     compat_sigset_t *set, struct pt_regs *regs);
#else
# define user_i387_ia32_struct	user_i387_struct
# define user32_fxsr_struct	user_fxsr_struct
# define ia32_setup_frame	__setup_frame
# define ia32_setup_rt_frame	__setup_rt_frame
#endif

#ifdef CONFIG_COMPAT
int __copy_siginfo_to_user32(compat_siginfo_t __user *to,
		const siginfo_t *from, bool x32_ABI);
#endif


extern void convert_from_fxsr(struct user_i387_ia32_struct *env,
			      struct task_struct *tsk);
extern void convert_to_fxsr(struct task_struct *tsk,
			    const struct user_i387_ia32_struct *env);

unsigned long
fpu__alloc_mathframe(unsigned long sp, int ia32_frame,
		     unsigned long *buf_fx, unsigned long *size);

extern void fpu__init_prepare_fx_sw_frame(void);

#endif /* _ASM_X86_FPU_SIGNAL_H */
