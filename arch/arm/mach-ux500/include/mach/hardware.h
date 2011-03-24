/*
 * Copyright (C) 2009 ST-Ericsson.
 *
 * U8500 hardware definitions
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef __MACH_HARDWARE_H
#define __MACH_HARDWARE_H

/* macros to get at IO space when running virtually
 * We dont map all the peripherals, let ioremap do
 * this for us. We map only very basic peripherals here.
 */
#define U8500_IO_VIRTUAL	0xf0000000
#define U8500_IO_PHYSICAL	0xa0000000

/* this macro is used in assembly, so no cast */
#define IO_ADDRESS(x)           \
	(((x) & 0x0fffffff) + (((x) >> 4) & 0x0f000000) + U8500_IO_VIRTUAL)

/* typesafe io address */
#define __io_address(n)		__io(IO_ADDRESS(n))
/* used by some plat-nomadik code */
#define io_p2v(n)		__io_address(n)

#include <mach/db8500-regs.h>
#include <mach/db5500-regs.h>

#ifndef __ASSEMBLY__

#include <mach/id.h>
extern void __iomem *_PRCMU_BASE;

#define ARRAY_AND_SIZE(x)	(x), ARRAY_SIZE(x)

#endif

#endif				/* __MACH_HARDWARE_H */
