/*
 * Copyright IBM Corp. 1999, 2009
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef __ASM_SWITCH_TO_H
#define __ASM_SWITCH_TO_H

#include <linux/thread_info.h>
#include <asm/ptrace.h>

extern struct task_struct *__switch_to(void *, void *);
extern void update_cr_regs(struct task_struct *task);

static inline int test_fp_ctl(u32 fpc)
{
	u32 orig_fpc;
	int rc;

	if (!MACHINE_HAS_IEEE)
		return 0;

	asm volatile(
		"	efpc    %1\n"
		"	sfpc	%2\n"
		"0:	sfpc	%1\n"
		"	la	%0,0\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "=d" (rc), "=d" (orig_fpc)
		: "d" (fpc), "0" (-EINVAL));
	return rc;
}

static inline void save_fp_ctl(u32 *fpc)
{
	if (!MACHINE_HAS_IEEE)
		return;

	asm volatile(
		"       stfpc   %0\n"
		: "+Q" (*fpc));
}

static inline int restore_fp_ctl(u32 *fpc)
{
	int rc;

	if (!MACHINE_HAS_IEEE)
		return 0;

	asm volatile(
		"0:	lfpc    %1\n"
		"	la	%0,0\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "=d" (rc) : "Q" (*fpc), "0" (-EINVAL));
	return rc;
}

static inline void save_fp_regs(freg_t *fprs)
{
	asm volatile("std 0,%0" : "=Q" (fprs[0]));
	asm volatile("std 2,%0" : "=Q" (fprs[2]));
	asm volatile("std 4,%0" : "=Q" (fprs[4]));
	asm volatile("std 6,%0" : "=Q" (fprs[6]));
	if (!MACHINE_HAS_IEEE)
		return;
	asm volatile("std 1,%0" : "=Q" (fprs[1]));
	asm volatile("std 3,%0" : "=Q" (fprs[3]));
	asm volatile("std 5,%0" : "=Q" (fprs[5]));
	asm volatile("std 7,%0" : "=Q" (fprs[7]));
	asm volatile("std 8,%0" : "=Q" (fprs[8]));
	asm volatile("std 9,%0" : "=Q" (fprs[9]));
	asm volatile("std 10,%0" : "=Q" (fprs[10]));
	asm volatile("std 11,%0" : "=Q" (fprs[11]));
	asm volatile("std 12,%0" : "=Q" (fprs[12]));
	asm volatile("std 13,%0" : "=Q" (fprs[13]));
	asm volatile("std 14,%0" : "=Q" (fprs[14]));
	asm volatile("std 15,%0" : "=Q" (fprs[15]));
}

static inline void restore_fp_regs(freg_t *fprs)
{
	asm volatile("ld 0,%0" : : "Q" (fprs[0]));
	asm volatile("ld 2,%0" : : "Q" (fprs[2]));
	asm volatile("ld 4,%0" : : "Q" (fprs[4]));
	asm volatile("ld 6,%0" : : "Q" (fprs[6]));
	if (!MACHINE_HAS_IEEE)
		return;
	asm volatile("ld 1,%0" : : "Q" (fprs[1]));
	asm volatile("ld 3,%0" : : "Q" (fprs[3]));
	asm volatile("ld 5,%0" : : "Q" (fprs[5]));
	asm volatile("ld 7,%0" : : "Q" (fprs[7]));
	asm volatile("ld 8,%0" : : "Q" (fprs[8]));
	asm volatile("ld 9,%0" : : "Q" (fprs[9]));
	asm volatile("ld 10,%0" : : "Q" (fprs[10]));
	asm volatile("ld 11,%0" : : "Q" (fprs[11]));
	asm volatile("ld 12,%0" : : "Q" (fprs[12]));
	asm volatile("ld 13,%0" : : "Q" (fprs[13]));
	asm volatile("ld 14,%0" : : "Q" (fprs[14]));
	asm volatile("ld 15,%0" : : "Q" (fprs[15]));
}

static inline void save_access_regs(unsigned int *acrs)
{
	typedef struct { int _[NUM_ACRS]; } acrstype;

	asm volatile("stam 0,15,%0" : "=Q" (*(acrstype *)acrs));
}

static inline void restore_access_regs(unsigned int *acrs)
{
	typedef struct { int _[NUM_ACRS]; } acrstype;

	asm volatile("lam 0,15,%0" : : "Q" (*(acrstype *)acrs));
}

#define switch_to(prev,next,last) do {					\
	if (prev->mm) {							\
		save_fp_ctl(&prev->thread.fp_regs.fpc);			\
		save_fp_regs(prev->thread.fp_regs.fprs);		\
		save_access_regs(&prev->thread.acrs[0]);		\
		save_ri_cb(prev->thread.ri_cb);				\
	}								\
	if (next->mm) {							\
		restore_fp_ctl(&next->thread.fp_regs.fpc);		\
		restore_fp_regs(next->thread.fp_regs.fprs);		\
		restore_access_regs(&next->thread.acrs[0]);		\
		restore_ri_cb(next->thread.ri_cb, prev->thread.ri_cb);	\
		update_cr_regs(next);					\
	}								\
	prev = __switch_to(prev,next);					\
} while (0)

#define finish_arch_switch(prev) do {					     \
	set_fs(current->thread.mm_segment);				     \
} while (0)

#endif /* __ASM_SWITCH_TO_H */
