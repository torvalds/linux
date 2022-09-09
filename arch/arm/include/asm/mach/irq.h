/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/mach/irq.h
 *
 *  Copyright (C) 1995-2000 Russell King.
 */
#ifndef __ASM_ARM_MACH_IRQ_H
#define __ASM_ARM_MACH_IRQ_H

#include <linux/irq.h>

struct seq_file;

/*
 * This is internal.  Do not use it.
 */
extern void init_FIQ(int);
extern int show_fiq_list(struct seq_file *, int);

/*
 * This is for easy migration, but should be changed in the source
 */
#define do_bad_IRQ(desc)				\
do {							\
	raw_spin_lock(&desc->lock);			\
	handle_bad_irq(desc);				\
	raw_spin_unlock(&desc->lock);			\
} while(0)

#endif
