/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Transactional memory support routines to reclaim and recheckpoint
 * transactional process state.
 *
 * Copyright 2012 Matt Evans & Michael Neuling, IBM Corporation.
 */

#include <uapi/asm/tm.h>

#ifndef __ASSEMBLY__

extern void tm_enable(void);
extern void tm_reclaim(struct thread_struct *thread,
		       unsigned long orig_msr, uint8_t cause);
extern void tm_reclaim_current(uint8_t cause);
extern void tm_recheckpoint(struct thread_struct *thread,
			    unsigned long orig_msr);
extern void tm_abort(uint8_t cause);
extern void tm_save_sprs(struct thread_struct *thread);
extern void tm_restore_sprs(struct thread_struct *thread);

#endif /* __ASSEMBLY__ */
