/* MN10300 FPU definitions
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * Derived from include/asm-i386/i387.h: Copyright (C) 1994 Linus Torvalds
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_FPU_H
#define _ASM_FPU_H

#ifndef __ASSEMBLY__

#include <linux/sched.h>
#include <asm/exceptions.h>
#include <asm/sigcontext.h>

#ifdef __KERNEL__

extern asmlinkage void fpu_disabled(void);

#ifdef CONFIG_FPU

#ifdef CONFIG_LAZY_SAVE_FPU
/* the task that currently owns the FPU state */
extern struct task_struct *fpu_state_owner;
#endif

#if (THREAD_USING_FPU & ~0xff)
#error THREAD_USING_FPU must be smaller than 0x100.
#endif

static inline void set_using_fpu(struct task_struct *tsk)
{
	asm volatile(
		"bset %0,(0,%1)"
		:
		: "i"(THREAD_USING_FPU), "a"(&tsk->thread.fpu_flags)
		: "memory", "cc");
}

static inline void clear_using_fpu(struct task_struct *tsk)
{
	asm volatile(
		"bclr %0,(0,%1)"
		:
		: "i"(THREAD_USING_FPU), "a"(&tsk->thread.fpu_flags)
		: "memory", "cc");
}

#define is_using_fpu(tsk) ((tsk)->thread.fpu_flags & THREAD_USING_FPU)

extern asmlinkage void fpu_kill_state(struct task_struct *);
extern asmlinkage void fpu_exception(struct pt_regs *, enum exception_code);
extern asmlinkage void fpu_init_state(void);
extern asmlinkage void fpu_save(struct fpu_state_struct *);
extern int fpu_setup_sigcontext(struct fpucontext *buf);
extern int fpu_restore_sigcontext(struct fpucontext *buf);

static inline void unlazy_fpu(struct task_struct *tsk)
{
	preempt_disable();
#ifndef CONFIG_LAZY_SAVE_FPU
	if (tsk->thread.fpu_flags & THREAD_HAS_FPU) {
		fpu_save(&tsk->thread.fpu_state);
		tsk->thread.fpu_flags &= ~THREAD_HAS_FPU;
		tsk->thread.uregs->epsw &= ~EPSW_FE;
	}
#else
	if (fpu_state_owner == tsk)
		fpu_save(&tsk->thread.fpu_state);
#endif
	preempt_enable();
}

static inline void exit_fpu(void)
{
#ifdef CONFIG_LAZY_SAVE_FPU
	struct task_struct *tsk = current;

	preempt_disable();
	if (fpu_state_owner == tsk)
		fpu_state_owner = NULL;
	preempt_enable();
#endif
}

static inline void flush_fpu(void)
{
	struct task_struct *tsk = current;

	preempt_disable();
#ifndef CONFIG_LAZY_SAVE_FPU
	if (tsk->thread.fpu_flags & THREAD_HAS_FPU) {
		tsk->thread.fpu_flags &= ~THREAD_HAS_FPU;
		tsk->thread.uregs->epsw &= ~EPSW_FE;
	}
#else
	if (fpu_state_owner == tsk) {
		fpu_state_owner = NULL;
		tsk->thread.uregs->epsw &= ~EPSW_FE;
	}
#endif
	preempt_enable();
	clear_using_fpu(tsk);
}

#else /* CONFIG_FPU */

extern asmlinkage
void unexpected_fpu_exception(struct pt_regs *, enum exception_code);
#define fpu_exception unexpected_fpu_exception

struct task_struct;
struct fpu_state_struct;
static inline bool is_using_fpu(struct task_struct *tsk) { return false; }
static inline void set_using_fpu(struct task_struct *tsk) {}
static inline void clear_using_fpu(struct task_struct *tsk) {}
static inline void fpu_init_state(void) {}
static inline void fpu_save(struct fpu_state_struct *s) {}
static inline void fpu_kill_state(struct task_struct *tsk) {}
static inline void unlazy_fpu(struct task_struct *tsk) {}
static inline void exit_fpu(void) {}
static inline void flush_fpu(void) {}
static inline int fpu_setup_sigcontext(struct fpucontext *buf) { return 0; }
static inline int fpu_restore_sigcontext(struct fpucontext *buf) { return 0; }
#endif /* CONFIG_FPU  */

#endif /* __KERNEL__ */
#endif /* !__ASSEMBLY__ */
#endif /* _ASM_FPU_H */
