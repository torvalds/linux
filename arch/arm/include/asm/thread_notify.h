/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/thread_yestify.h
 *
 *  Copyright (C) 2006 Russell King.
 */
#ifndef ASMARM_THREAD_NOTIFY_H
#define ASMARM_THREAD_NOTIFY_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

#include <linux/yestifier.h>
#include <asm/thread_info.h>

static inline int thread_register_yestifier(struct yestifier_block *n)
{
	extern struct atomic_yestifier_head thread_yestify_head;
	return atomic_yestifier_chain_register(&thread_yestify_head, n);
}

static inline void thread_unregister_yestifier(struct yestifier_block *n)
{
	extern struct atomic_yestifier_head thread_yestify_head;
	atomic_yestifier_chain_unregister(&thread_yestify_head, n);
}

static inline void thread_yestify(unsigned long rc, struct thread_info *thread)
{
	extern struct atomic_yestifier_head thread_yestify_head;
	atomic_yestifier_call_chain(&thread_yestify_head, rc, thread);
}

#endif

/*
 * These are the reason codes for the thread yestifier.
 */
#define THREAD_NOTIFY_FLUSH	0
#define THREAD_NOTIFY_EXIT	1
#define THREAD_NOTIFY_SWITCH	2
#define THREAD_NOTIFY_COPY	3

#endif
#endif
