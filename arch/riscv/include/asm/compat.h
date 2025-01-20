/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_COMPAT_H
#define __ASM_COMPAT_H

#define COMPAT_UTS_MACHINE	"riscv\0\0"

/*
 * Architecture specific compatibility types
 */
#include <linux/types.h>
#include <linux/sched.h>
#include <asm-generic/compat.h>

static inline int is_compat_task(void)
{
	if (!IS_ENABLED(CONFIG_COMPAT))
		return 0;

	return test_thread_flag(TIF_32BIT);
}

static inline int is_compat_thread(struct thread_info *thread)
{
	if (!IS_ENABLED(CONFIG_COMPAT))
		return 0;

	return test_ti_thread_flag(thread, TIF_32BIT);
}

static inline void set_compat_task(bool is_compat)
{
	if (is_compat)
		set_thread_flag(TIF_32BIT);
	else
		clear_thread_flag(TIF_32BIT);
}

struct compat_user_regs_struct {
	compat_ulong_t pc;
	compat_ulong_t ra;
	compat_ulong_t sp;
	compat_ulong_t gp;
	compat_ulong_t tp;
	compat_ulong_t t0;
	compat_ulong_t t1;
	compat_ulong_t t2;
	compat_ulong_t s0;
	compat_ulong_t s1;
	compat_ulong_t a0;
	compat_ulong_t a1;
	compat_ulong_t a2;
	compat_ulong_t a3;
	compat_ulong_t a4;
	compat_ulong_t a5;
	compat_ulong_t a6;
	compat_ulong_t a7;
	compat_ulong_t s2;
	compat_ulong_t s3;
	compat_ulong_t s4;
	compat_ulong_t s5;
	compat_ulong_t s6;
	compat_ulong_t s7;
	compat_ulong_t s8;
	compat_ulong_t s9;
	compat_ulong_t s10;
	compat_ulong_t s11;
	compat_ulong_t t3;
	compat_ulong_t t4;
	compat_ulong_t t5;
	compat_ulong_t t6;
};

static inline void regs_to_cregs(struct compat_user_regs_struct *cregs,
				 struct pt_regs *regs)
{
	cregs->pc	= (compat_ulong_t) regs->epc;
	cregs->ra	= (compat_ulong_t) regs->ra;
	cregs->sp	= (compat_ulong_t) regs->sp;
	cregs->gp	= (compat_ulong_t) regs->gp;
	cregs->tp	= (compat_ulong_t) regs->tp;
	cregs->t0	= (compat_ulong_t) regs->t0;
	cregs->t1	= (compat_ulong_t) regs->t1;
	cregs->t2	= (compat_ulong_t) regs->t2;
	cregs->s0	= (compat_ulong_t) regs->s0;
	cregs->s1	= (compat_ulong_t) regs->s1;
	cregs->a0	= (compat_ulong_t) regs->a0;
	cregs->a1	= (compat_ulong_t) regs->a1;
	cregs->a2	= (compat_ulong_t) regs->a2;
	cregs->a3	= (compat_ulong_t) regs->a3;
	cregs->a4	= (compat_ulong_t) regs->a4;
	cregs->a5	= (compat_ulong_t) regs->a5;
	cregs->a6	= (compat_ulong_t) regs->a6;
	cregs->a7	= (compat_ulong_t) regs->a7;
	cregs->s2	= (compat_ulong_t) regs->s2;
	cregs->s3	= (compat_ulong_t) regs->s3;
	cregs->s4	= (compat_ulong_t) regs->s4;
	cregs->s5	= (compat_ulong_t) regs->s5;
	cregs->s6	= (compat_ulong_t) regs->s6;
	cregs->s7	= (compat_ulong_t) regs->s7;
	cregs->s8	= (compat_ulong_t) regs->s8;
	cregs->s9	= (compat_ulong_t) regs->s9;
	cregs->s10	= (compat_ulong_t) regs->s10;
	cregs->s11	= (compat_ulong_t) regs->s11;
	cregs->t3	= (compat_ulong_t) regs->t3;
	cregs->t4	= (compat_ulong_t) regs->t4;
	cregs->t5	= (compat_ulong_t) regs->t5;
	cregs->t6	= (compat_ulong_t) regs->t6;
};

static inline void cregs_to_regs(struct compat_user_regs_struct *cregs,
				 struct pt_regs *regs)
{
	regs->epc	= (unsigned long) cregs->pc;
	regs->ra	= (unsigned long) cregs->ra;
	regs->sp	= (unsigned long) cregs->sp;
	regs->gp	= (unsigned long) cregs->gp;
	regs->tp	= (unsigned long) cregs->tp;
	regs->t0	= (unsigned long) cregs->t0;
	regs->t1	= (unsigned long) cregs->t1;
	regs->t2	= (unsigned long) cregs->t2;
	regs->s0	= (unsigned long) cregs->s0;
	regs->s1	= (unsigned long) cregs->s1;
	regs->a0	= (unsigned long) cregs->a0;
	regs->a1	= (unsigned long) cregs->a1;
	regs->a2	= (unsigned long) cregs->a2;
	regs->a3	= (unsigned long) cregs->a3;
	regs->a4	= (unsigned long) cregs->a4;
	regs->a5	= (unsigned long) cregs->a5;
	regs->a6	= (unsigned long) cregs->a6;
	regs->a7	= (unsigned long) cregs->a7;
	regs->s2	= (unsigned long) cregs->s2;
	regs->s3	= (unsigned long) cregs->s3;
	regs->s4	= (unsigned long) cregs->s4;
	regs->s5	= (unsigned long) cregs->s5;
	regs->s6	= (unsigned long) cregs->s6;
	regs->s7	= (unsigned long) cregs->s7;
	regs->s8	= (unsigned long) cregs->s8;
	regs->s9	= (unsigned long) cregs->s9;
	regs->s10	= (unsigned long) cregs->s10;
	regs->s11	= (unsigned long) cregs->s11;
	regs->t3	= (unsigned long) cregs->t3;
	regs->t4	= (unsigned long) cregs->t4;
	regs->t5	= (unsigned long) cregs->t5;
	regs->t6	= (unsigned long) cregs->t6;
};

#endif /* __ASM_COMPAT_H */
