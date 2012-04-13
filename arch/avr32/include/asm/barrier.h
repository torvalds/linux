/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_BARRIER_H
#define __ASM_AVR32_BARRIER_H

#define nop()			asm volatile("nop")

#define mb()			asm volatile("" : : : "memory")
#define rmb()			mb()
#define wmb()			asm volatile("sync 0" : : : "memory")
#define read_barrier_depends()  do { } while(0)
#define set_mb(var, value)      do { var = value; mb(); } while(0)

#ifdef CONFIG_SMP
# error "The AVR32 port does not support SMP"
#else
# define smp_mb()		barrier()
# define smp_rmb()		barrier()
# define smp_wmb()		barrier()
# define smp_read_barrier_depends() do { } while(0)
#endif


#endif /* __ASM_AVR32_BARRIER_H */
