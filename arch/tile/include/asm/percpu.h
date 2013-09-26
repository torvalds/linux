/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_PERCPU_H
#define _ASM_TILE_PERCPU_H

register unsigned long my_cpu_offset_reg asm("tp");

#ifdef CONFIG_PREEMPT
/*
 * For full preemption, we can't just use the register variable
 * directly, since we need barrier() to hazard against it, causing the
 * compiler to reload anything computed from a previous "tp" value.
 * But we also don't want to use volatile asm, since we'd like the
 * compiler to be able to cache the value across multiple percpu reads.
 * So we use a fake stack read as a hazard against barrier().
 * The 'U' constraint is like 'm' but disallows postincrement.
 */
static inline unsigned long __my_cpu_offset(void)
{
	unsigned long tp;
	register unsigned long *sp asm("sp");
	asm("move %0, tp" : "=r" (tp) : "U" (*sp));
	return tp;
}
#define __my_cpu_offset __my_cpu_offset()
#else
/*
 * We don't need to hazard against barrier() since "tp" doesn't ever
 * change with PREEMPT_NONE, and with PREEMPT_VOLUNTARY it only
 * changes at function call points, at which we are already re-reading
 * the value of "tp" due to "my_cpu_offset_reg" being a global variable.
 */
#define __my_cpu_offset my_cpu_offset_reg
#endif

#define set_my_cpu_offset(tp) (my_cpu_offset_reg = (tp))

#include <asm-generic/percpu.h>

#endif /* _ASM_TILE_PERCPU_H */
