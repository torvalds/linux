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
	asm volatile(
		"       stfpc   %0\n"
		: "+Q" (*fpc));
}

static inline int restore_fp_ctl(u32 *fpc)
{
	int rc;

	asm volatile(
		"	lfpc    %1\n"
		"0:	la	%0,0\n"
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

static inline void save_vx_regs(__vector128 *vxrs)
{
	typedef struct { __vector128 _[__NUM_VXRS]; } addrtype;

	asm volatile(
		"	la	1,%0\n"
		"	.word	0xe70f,0x1000,0x003e\n"	/* vstm 0,15,0(1) */
		"	.word	0xe70f,0x1100,0x0c3e\n"	/* vstm 16,31,256(1) */
		: "=Q" (*(addrtype *) vxrs) : : "1");
}

static inline void save_vx_regs_safe(__vector128 *vxrs)
{
	unsigned long cr0, flags;

	flags = arch_local_irq_save();
	__ctl_store(cr0, 0, 0);
	__ctl_set_bit(0, 17);
	__ctl_set_bit(0, 18);
	save_vx_regs(vxrs);
	__ctl_load(cr0, 0, 0);
	arch_local_irq_restore(flags);
}

static inline void restore_vx_regs(__vector128 *vxrs)
{
	typedef struct { __vector128 _[__NUM_VXRS]; } addrtype;

	asm volatile(
		"	la	1,%0\n"
		"	.word	0xe70f,0x1000,0x0036\n"	/* vlm 0,15,0(1) */
		"	.word	0xe70f,0x1100,0x0c36\n"	/* vlm 16,31,256(1) */
		: : "Q" (*(addrtype *) vxrs) : "1");
}

static inline void save_fp_vx_regs(struct task_struct *task)
{
	if (task->thread.vxrs)
		save_vx_regs(task->thread.vxrs);
	else
		save_fp_regs(task->thread.fp_regs.fprs);
}

static inline void restore_fp_vx_regs(struct task_struct *task)
{
	if (task->thread.vxrs)
		restore_vx_regs(task->thread.vxrs);
	else
		restore_fp_regs(task->thread.fp_regs.fprs);
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
		save_fp_vx_regs(prev);					\
		save_access_regs(&prev->thread.acrs[0]);		\
		save_ri_cb(prev->thread.ri_cb);				\
	}								\
	if (next->mm) {							\
		update_cr_regs(next);					\
		restore_fp_ctl(&next->thread.fp_regs.fpc);		\
		restore_fp_vx_regs(next);				\
		restore_access_regs(&next->thread.acrs[0]);		\
		restore_ri_cb(next->thread.ri_cb, prev->thread.ri_cb);	\
	}								\
	prev = __switch_to(prev,next);					\
} while (0)

#endif /* __ASM_SWITCH_TO_H */
