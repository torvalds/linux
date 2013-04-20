/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_ARC_UNWIND_H
#define _ASM_ARC_UNWIND_H

#ifdef CONFIG_ARC_DW2_UNWIND

#include <linux/sched.h>

struct arc700_regs {
	unsigned long r0;
	unsigned long r1;
	unsigned long r2;
	unsigned long r3;
	unsigned long r4;
	unsigned long r5;
	unsigned long r6;
	unsigned long r7;
	unsigned long r8;
	unsigned long r9;
	unsigned long r10;
	unsigned long r11;
	unsigned long r12;
	unsigned long r13;
	unsigned long r14;
	unsigned long r15;
	unsigned long r16;
	unsigned long r17;
	unsigned long r18;
	unsigned long r19;
	unsigned long r20;
	unsigned long r21;
	unsigned long r22;
	unsigned long r23;
	unsigned long r24;
	unsigned long r25;
	unsigned long r26;
	unsigned long r27;	/* fp */
	unsigned long r28;	/* sp */
	unsigned long r29;
	unsigned long r30;
	unsigned long r31;	/* blink */
	unsigned long r63;	/* pc */
};

struct unwind_frame_info {
	struct arc700_regs regs;
	struct task_struct *task;
	unsigned call_frame:1;
};

#define UNW_PC(frame)		((frame)->regs.r63)
#define UNW_SP(frame)		((frame)->regs.r28)
#define UNW_BLINK(frame)	((frame)->regs.r31)

/* Rajesh FIXME */
#ifdef CONFIG_FRAME_POINTER
#define UNW_FP(frame)		((frame)->regs.r27)
#define FRAME_RETADDR_OFFSET	4
#define FRAME_LINK_OFFSET	0
#define STACK_BOTTOM_UNW(tsk)	STACK_LIMIT((tsk)->thread.ksp)
#define STACK_TOP_UNW(tsk)	((tsk)->thread.ksp)
#else
#define UNW_FP(frame)		((void)(frame), 0)
#endif

#define STACK_LIMIT(ptr)	(((ptr) - 1) & ~(THREAD_SIZE - 1))

#define UNW_REGISTER_INFO \
	PTREGS_INFO(r0), \
	PTREGS_INFO(r1), \
	PTREGS_INFO(r2), \
	PTREGS_INFO(r3), \
	PTREGS_INFO(r4), \
	PTREGS_INFO(r5), \
	PTREGS_INFO(r6), \
	PTREGS_INFO(r7), \
	PTREGS_INFO(r8), \
	PTREGS_INFO(r9), \
	PTREGS_INFO(r10), \
	PTREGS_INFO(r11), \
	PTREGS_INFO(r12), \
	PTREGS_INFO(r13), \
	PTREGS_INFO(r14), \
	PTREGS_INFO(r15), \
	PTREGS_INFO(r16), \
	PTREGS_INFO(r17), \
	PTREGS_INFO(r18), \
	PTREGS_INFO(r19), \
	PTREGS_INFO(r20), \
	PTREGS_INFO(r21), \
	PTREGS_INFO(r22), \
	PTREGS_INFO(r23), \
	PTREGS_INFO(r24), \
	PTREGS_INFO(r25), \
	PTREGS_INFO(r26), \
	PTREGS_INFO(r27), \
	PTREGS_INFO(r28), \
	PTREGS_INFO(r29), \
	PTREGS_INFO(r30), \
	PTREGS_INFO(r31), \
	PTREGS_INFO(r63)

#define UNW_DEFAULT_RA(raItem, dataAlign) \
	((raItem).where == Memory && !((raItem).value * (dataAlign) + 4))

extern int arc_unwind(struct unwind_frame_info *frame);
extern void arc_unwind_init(void);
extern void arc_unwind_setup(void);
extern void *unwind_add_table(struct module *module, const void *table_start,
			      unsigned long table_size);
extern void unwind_remove_table(void *handle, int init_only);

static inline int
arch_unwind_init_running(struct unwind_frame_info *info,
			 int (*callback) (struct unwind_frame_info *info,
					  void *arg),
			 void *arg)
{
	return 0;
}

static inline int arch_unw_user_mode(const struct unwind_frame_info *info)
{
	return 0;
}

static inline void arch_unw_init_blocked(struct unwind_frame_info *info)
{
	return;
}

static inline void arch_unw_init_frame_info(struct unwind_frame_info *info,
					    struct pt_regs *regs)
{
	return;
}

#else

#define UNW_PC(frame) ((void)(frame), 0)
#define UNW_SP(frame) ((void)(frame), 0)
#define UNW_FP(frame) ((void)(frame), 0)

static inline void arc_unwind_init(void)
{
}

static inline void arc_unwind_setup(void)
{
}
#define unwind_add_table(a, b, c)
#define unwind_remove_table(a, b)

#endif /* CONFIG_ARC_DW2_UNWIND */

#endif /* _ASM_ARC_UNWIND_H */
