/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_DELAY_H
#define _ASM_IA64_DELAY_H

/*
 * Delay routines using a pre-computed "cycles/usec" value.
 *
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999 Asit Mallick <asit.k.mallick@intel.com>
 * Copyright (C) 1999 Don Dugger <don.dugger@intel.com>
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/compiler.h>

#include <asm/intrinsics.h>
#include <asm/processor.h>

static __inline__ void
ia64_set_itm (unsigned long val)
{
	ia64_setreg(_IA64_REG_CR_ITM, val);
	ia64_srlz_d();
}

static __inline__ unsigned long
ia64_get_itm (void)
{
	unsigned long result;

	result = ia64_getreg(_IA64_REG_CR_ITM);
	ia64_srlz_d();
	return result;
}

static __inline__ void
ia64_set_itv (unsigned long val)
{
	ia64_setreg(_IA64_REG_CR_ITV, val);
	ia64_srlz_d();
}

static __inline__ unsigned long
ia64_get_itv (void)
{
	return ia64_getreg(_IA64_REG_CR_ITV);
}

static __inline__ void
ia64_set_itc (unsigned long val)
{
	ia64_setreg(_IA64_REG_AR_ITC, val);
	ia64_srlz_d();
}

static __inline__ unsigned long
ia64_get_itc (void)
{
	unsigned long result;

	result = ia64_getreg(_IA64_REG_AR_ITC);
	ia64_barrier();
#ifdef CONFIG_ITANIUM
	while (unlikely((__s32) result == -1)) {
		result = ia64_getreg(_IA64_REG_AR_ITC);
		ia64_barrier();
	}
#endif
	return result;
}

extern void ia64_delay_loop (unsigned long loops);

static __inline__ void
__delay (unsigned long loops)
{
	if (unlikely(loops < 1))
		return;

	ia64_delay_loop (loops - 1);
}

extern void udelay (unsigned long usecs);

#endif /* _ASM_IA64_DELAY_H */
