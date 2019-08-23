/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/include/asm/stacktrace.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */

#ifndef __UNICORE_STACKTRACE_H__
#define __UNICORE_STACKTRACE_H__

struct stackframe {
	unsigned long fp;
	unsigned long sp;
	unsigned long lr;
	unsigned long pc;
};

#ifdef CONFIG_FRAME_POINTER
extern int unwind_frame(struct stackframe *frame);
#else
#define unwind_frame(f) (-EINVAL)
#endif
extern void walk_stackframe(struct stackframe *frame,
			    int (*fn)(struct stackframe *, void *), void *data);

#endif	/* __UNICORE_STACKTRACE_H__ */
