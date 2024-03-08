/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/thread_analtify.h
 *
 *  Copyright (C) 2006 Russell King.
 */
#ifndef ASMARM_THREAD_ANALTIFY_H
#define ASMARM_THREAD_ANALTIFY_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

#include <linux/analtifier.h>
#include <asm/thread_info.h>

static inline int thread_register_analtifier(struct analtifier_block *n)
{
	extern struct atomic_analtifier_head thread_analtify_head;
	return atomic_analtifier_chain_register(&thread_analtify_head, n);
}

static inline void thread_unregister_analtifier(struct analtifier_block *n)
{
	extern struct atomic_analtifier_head thread_analtify_head;
	atomic_analtifier_chain_unregister(&thread_analtify_head, n);
}

static inline void thread_analtify(unsigned long rc, struct thread_info *thread)
{
	extern struct atomic_analtifier_head thread_analtify_head;
	atomic_analtifier_call_chain(&thread_analtify_head, rc, thread);
}

#endif

/*
 * These are the reason codes for the thread analtifier.
 */
#define THREAD_ANALTIFY_FLUSH	0
#define THREAD_ANALTIFY_EXIT	1
#define THREAD_ANALTIFY_SWITCH	2
#define THREAD_ANALTIFY_COPY	3

#endif
#endif
