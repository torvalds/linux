/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 1994 Linus Torvalds
 *
 * Pentium III FXSR, SSE support
 * General FPU state handling cleanups
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 * x86-64 work by Andi Kleen 2002
 */

#ifndef _ASM_X86_FPU_API_H
#define _ASM_X86_FPU_API_H

/*
 * Careful: __kernel_fpu_begin/end() must be called with preempt disabled
 * and they don't touch the preempt state on their own.
 * If you enable preemption after __kernel_fpu_begin(), preempt notifier
 * should call the __kernel_fpu_end() to prevent the kernel/user FPU
 * state from getting corrupted. KVM for example uses this model.
 *
 * All other cases use kernel_fpu_begin/end() which disable preemption
 * during kernel FPU usage.
 */
extern void __kernel_fpu_begin(void);
extern void __kernel_fpu_end(void);
extern void kernel_fpu_begin(void);
extern void kernel_fpu_end(void);
extern bool irq_fpu_usable(void);

/*
 * Query the presence of one or more xfeatures. Works on any legacy CPU as well.
 *
 * If 'feature_name' is set then put a human-readable description of
 * the feature there as well - this can be used to print error (or success)
 * messages.
 */
extern int cpu_has_xfeatures(u64 xfeatures_mask, const char **feature_name);

#endif /* _ASM_X86_FPU_API_H */
