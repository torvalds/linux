/*
 * arch/arm/mach-sa1100/include/mach/hardware.h
 *
 * Copyright (C) 1998 Nicolas Pitre <nico@fluxnic.net>
 *
 * This file contains the hardware definitions for SA1100 architecture
 *
 * 2000/05/23 John Dorsey <john+@cs.cmu.edu>
 *      Definitions for SA1111 added.
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H


#define UNCACHEABLE_ADDR	0xfa050000	/* ICIP */


/*
 * SA1100 internal I/O mappings
 *
 * We have the following mapping:
 *      phys            virt
 *      80000000        f8000000
 *      90000000        fa000000
 *      a0000000        fc000000
 *      b0000000        fe000000
 */

#define VIO_BASE        0xf8000000	/* virtual start of IO space */
#define VIO_SHIFT       3		/* x = IO space shrink power */
#define PIO_START       0x80000000	/* physical start of IO space */

#define io_p2v( x )             \
   IOMEM( (((x)&0x00ffffff) | (((x)&0x30000000)>>VIO_SHIFT)) + VIO_BASE )
#define io_v2p( x )             \
   ( (((x)&0x00ffffff) | (((x)&(0x30000000>>VIO_SHIFT))<<VIO_SHIFT)) + PIO_START )

#define __MREG(x)	IOMEM(io_p2v(x))

#ifndef __ASSEMBLY__

# define __REG(x)	(*((volatile unsigned long __iomem *)io_p2v(x)))
# define __PREG(x)	(io_v2p((unsigned long)&(x)))

static inline unsigned long get_clock_tick_rate(void)
{
	return 3686400;
}
#else

# define __REG(x)	io_p2v(x)
# define __PREG(x)	io_v2p(x)

#endif

#include "SA-1100.h"

#ifdef CONFIG_SA1101
#include "SA-1101.h"
#endif

#endif  /* _ASM_ARCH_HARDWARE_H */
