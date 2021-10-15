/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 1994 Linus Torvalds
 *
 * Pentium III FXSR, SSE support
 * General FPU state handling cleanups
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 * x86-64 work by Andi Kleen 2002
 */

#ifndef _ASM_X86_FPU_INTERNAL_H
#define _ASM_X86_FPU_INTERNAL_H

#include <linux/compat.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <asm/user.h>
#include <asm/fpu/api.h>
#include <asm/fpu/xstate.h>
#include <asm/fpu/xcr.h>
#include <asm/cpufeature.h>
#include <asm/trace/fpu.h>

/*
 * High level FPU state handling functions:
 */
extern bool fpu__restore_sig(void __user *buf, int ia32_frame);
extern void fpu__clear_user_states(struct fpu *fpu);
extern int  fpu__exception_code(struct fpu *fpu, int trap_nr);

extern void fpu_sync_fpstate(struct fpu *fpu);

/*
 * Boot time FPU initialization functions:
 */
extern void fpu__init_cpu(void);
extern void fpu__init_system_xstate(void);
extern void fpu__init_cpu_xstate(void);
extern void fpu__init_system(struct cpuinfo_x86 *c);
extern void fpu__init_check_bugs(void);
extern void fpu__resume_cpu(void);

extern union fpregs_state init_fpstate;
extern void fpstate_init_user(union fpregs_state *state);

#ifdef CONFIG_MATH_EMULATION
extern void fpstate_init_soft(struct swregs_state *soft);
#else
static inline void fpstate_init_soft(struct swregs_state *soft) {}
#endif

extern void restore_fpregs_from_fpstate(union fpregs_state *fpstate, u64 mask);

extern bool copy_fpstate_to_sigframe(void __user *buf, void __user *fp, int size);

DECLARE_PER_CPU(struct fpu *, fpu_fpregs_owner_ctx);

#endif /* _ASM_X86_FPU_INTERNAL_H */
