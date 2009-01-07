/*   -*- linux-c -*-
 *   include/asm-blackfin/_baseipipe.h
 *
 *   Copyright (C) 2007 Philippe Gerum.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __ASM_BLACKFIN_IPIPE_BASE_H
#define __ASM_BLACKFIN_IPIPE_BASE_H

#ifdef CONFIG_IPIPE

#define IPIPE_NR_XIRQS		NR_IRQS
#define IPIPE_IRQ_ISHIFT	5	/* 2^5 for 32bits arch. */

/* Blackfin-specific, global domain flags */
#define IPIPE_ROOTLOCK_FLAG	1	/* Lock pipeline for root */

 /* Blackfin traps -- i.e. exception vector numbers */
#define IPIPE_NR_FAULTS		52 /* We leave a gap after VEC_ILL_RES. */
/* Pseudo-vectors used for kernel events */
#define IPIPE_FIRST_EVENT	IPIPE_NR_FAULTS
#define IPIPE_EVENT_SYSCALL	(IPIPE_FIRST_EVENT)
#define IPIPE_EVENT_SCHEDULE	(IPIPE_FIRST_EVENT + 1)
#define IPIPE_EVENT_SIGWAKE	(IPIPE_FIRST_EVENT + 2)
#define IPIPE_EVENT_SETSCHED	(IPIPE_FIRST_EVENT + 3)
#define IPIPE_EVENT_INIT	(IPIPE_FIRST_EVENT + 4)
#define IPIPE_EVENT_EXIT	(IPIPE_FIRST_EVENT + 5)
#define IPIPE_EVENT_CLEANUP	(IPIPE_FIRST_EVENT + 6)
#define IPIPE_LAST_EVENT	IPIPE_EVENT_CLEANUP
#define IPIPE_NR_EVENTS		(IPIPE_LAST_EVENT + 1)

#define IPIPE_TIMER_IRQ		IRQ_CORETMR

#ifndef __ASSEMBLY__

#include <linux/bitops.h>

extern int test_bit(int nr, const void *addr);


extern unsigned long __ipipe_root_status; /* Alias to ipipe_root_cpudom_var(status) */

static inline void __ipipe_stall_root(void)
{
	volatile unsigned long *p = &__ipipe_root_status;
	set_bit(0, p);
}

static inline unsigned long __ipipe_test_and_stall_root(void)
{
	volatile unsigned long *p = &__ipipe_root_status;
	return test_and_set_bit(0, p);
}

static inline unsigned long __ipipe_test_root(void)
{
	const unsigned long *p = &__ipipe_root_status;
	return test_bit(0, p);
}

#endif /* !__ASSEMBLY__ */

#endif /* CONFIG_IPIPE */

#endif /* !__ASM_BLACKFIN_IPIPE_BASE_H */
