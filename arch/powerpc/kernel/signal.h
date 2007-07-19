/*
 *    Copyright (c) 2007 Benjamin Herrenschmidt, IBM Coproration
 *    Extracted from signal_32.c and signal_64.c
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file README.legal in the main directory of
 * this archive for more details.
 */

#ifndef _POWERPC_ARCH_SIGNAL_H
#define _POWERPC_ARCH_SIGNAL_H

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

extern void __user * get_sigframe(struct k_sigaction *ka, struct pt_regs *regs,
				  size_t frame_size);
extern void restore_sigmask(sigset_t *set);

extern int handle_signal32(unsigned long sig, struct k_sigaction *ka,
			   siginfo_t *info, sigset_t *oldset,
			   struct pt_regs *regs);

extern int handle_rt_signal32(unsigned long sig, struct k_sigaction *ka,
			      siginfo_t *info, sigset_t *oldset,
			      struct pt_regs *regs);


#ifdef CONFIG_PPC64

static inline int is_32bit_task(void)
{
	return test_thread_flag(TIF_32BIT);
}

extern int handle_rt_signal64(int signr, struct k_sigaction *ka,
			      siginfo_t *info, sigset_t *set,
			      struct pt_regs *regs);

#else /* CONFIG_PPC64 */

static inline int is_32bit_task(void)
{
	return 1;
}

static inline int handle_rt_signal64(int signr, struct k_sigaction *ka,
				     siginfo_t *info, sigset_t *set,
				     struct pt_regs *regs)
{
	return -EFAULT;
}

#endif /* !defined(CONFIG_PPC64) */

#endif  /* _POWERPC_ARCH_SIGNAL_H */
