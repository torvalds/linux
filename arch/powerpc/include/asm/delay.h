#ifndef _ASM_POWERPC_DELAY_H
#define _ASM_POWERPC_DELAY_H
#ifdef __KERNEL__

/*
 * Copyright 1996, Paul Mackerras.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * PPC64 Support added by Dave Engebretsen, Todd Inglett, Mike Corrigan,
 * Anton Blanchard.
 */

extern void __delay(unsigned long loops);
extern void udelay(unsigned long usecs);

/*
 * On shared processor machines the generic implementation of mdelay can
 * result in large errors. While each iteration of the loop inside mdelay
 * is supposed to take 1ms, the hypervisor could sleep our partition for
 * longer (eg 10ms). With the right timing these errors can add up.
 *
 * Since there is no 32bit overflow issue on 64bit kernels, just call
 * udelay directly.
 */
#ifdef CONFIG_PPC64
#define mdelay(n)	udelay((n) * 1000)
#endif

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_DELAY_H */
