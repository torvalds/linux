/*
 * Memory barrier implementations for PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2012 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __UNICORE_BARRIER_H__
#define __UNICORE_BARRIER_H__

#define isb() __asm__ __volatile__ ("" : : : "memory")
#define dsb() __asm__ __volatile__ ("" : : : "memory")
#define dmb() __asm__ __volatile__ ("" : : : "memory")

#define mb()				barrier()
#define rmb()				barrier()
#define wmb()				barrier()
#define smp_mb()			barrier()
#define smp_rmb()			barrier()
#define smp_wmb()			barrier()
#define read_barrier_depends()		do { } while (0)
#define smp_read_barrier_depends()	do { } while (0)

#define set_mb(var, value)		do { var = value; smp_mb(); } while (0)

#endif /* __UNICORE_BARRIER_H__ */
