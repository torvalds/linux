/* MN10300 memory barrier definitions
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_BARRIER_H
#define _ASM_BARRIER_H

#define nop()	asm volatile ("nop")

#define mb()	asm volatile ("": : :"memory")
#define rmb()	mb()
#define wmb()	asm volatile ("": : :"memory")

#ifdef CONFIG_SMP
#define smp_mb()	mb()
#define smp_rmb()	rmb()
#define smp_wmb()	wmb()
#define set_mb(var, value)  do { xchg(&var, value); } while (0)
#else  /* CONFIG_SMP */
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define set_mb(var, value)  do { var = value;  mb(); } while (0)
#endif /* CONFIG_SMP */

#define set_wmb(var, value) do { var = value; wmb(); } while (0)

#define read_barrier_depends()		do {} while (0)
#define smp_read_barrier_depends()	do {} while (0)

#endif /* _ASM_BARRIER_H */
