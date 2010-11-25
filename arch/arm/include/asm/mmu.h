#ifndef __ARM_MMU_H
#define __ARM_MMU_H

#ifdef CONFIG_MMU

typedef struct {
#ifdef CONFIG_CPU_HAS_ASID
	unsigned int id;
	spinlock_t id_lock;
#endif
	unsigned int kvm_seq;
} mm_context_t;

#ifdef CONFIG_CPU_HAS_ASID
#define ASID(mm)	((mm)->context.id & 255)

/* init_mm.context.id_lock should be initialized. */
#define INIT_MM_CONTEXT(name)                                                 \
	.context.id_lock    = __SPIN_LOCK_UNLOCKED(name.context.id_lock),
#else
#define ASID(mm)	(0)
#endif

#else

/*
 * From nommu.h:
 *  Copyright (C) 2002, David McCullough <davidm@snapgear.com>
 *  modified for 2.6 by Hyok S. Choi <hyok.choi@samsung.com>
 */
typedef struct {
	unsigned long		end_brk;
} mm_context_t;

#endif

#endif
