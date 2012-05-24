/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef _ASM_C6X_BARRIER_H
#define _ASM_C6X_BARRIER_H

#define nop()                    asm("NOP\n");

#define mb()                     barrier()
#define rmb()                    barrier()
#define wmb()                    barrier()
#define set_mb(var, value)       do { var = value;  mb(); } while (0)
#define set_wmb(var, value)      do { var = value; wmb(); } while (0)

#define smp_mb()	         barrier()
#define smp_rmb()	         barrier()
#define smp_wmb()	         barrier()
#define smp_read_barrier_depends()	do { } while (0)

#endif /* _ASM_C6X_BARRIER_H */
