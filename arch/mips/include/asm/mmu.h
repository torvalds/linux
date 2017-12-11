/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MMU_H
#define __ASM_MMU_H

#include <linux/atomic.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

typedef struct {
	unsigned long asid[NR_CPUS];
	void *vdso;
	atomic_t fp_mode_switching;

	/* lock to be held whilst modifying fp_bd_emupage_allocmap */
	spinlock_t bd_emupage_lock;
	/* bitmap tracking allocation of fp_bd_emupage */
	unsigned long *bd_emupage_allocmap;
	/* wait queue for threads requiring an emuframe */
	wait_queue_head_t bd_emupage_queue;
} mm_context_t;

#endif /* __ASM_MMU_H */
