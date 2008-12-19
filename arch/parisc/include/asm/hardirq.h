/* hardirq.h: PA-RISC hard IRQ support.
 *
 * Copyright (C) 2001 Matthew Wilcox <matthew@wil.cx>
 *
 * The locking is really quite interesting.  There's a cpu-local
 * count of how many interrupts are being handled, and a global
 * lock.  An interrupt can only be serviced if the global lock
 * is free.  You can't be sure no more interrupts are being
 * serviced until you've acquired the lock and then checked
 * all the per-cpu interrupt counts are all zero.  It's a specialised
 * br_lock, and that's exactly how Sparc does it.  We don't because
 * it's more locking for us.  This way is lock-free in the interrupt path.
 */

#ifndef _PARISC_HARDIRQ_H
#define _PARISC_HARDIRQ_H

#include <linux/threads.h>
#include <linux/irq.h>

typedef struct {
	unsigned long __softirq_pending; /* set_bit is used on this */
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

void ack_bad_irq(unsigned int irq);

#endif /* _PARISC_HARDIRQ_H */
