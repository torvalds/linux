/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 */
#ifndef __ASM_MACH_PXA_REGS_H
#define __ASM_MACH_PXA_REGS_H

/*
 * Workarounds for at least 2 errata so far require this.
 * The mapping is set in mach-pxa/generic.c.
 */
#define UNCACHED_PHYS_0		0xfe000000
#define UNCACHED_PHYS_0_SIZE	0x00100000

/*
 * Intel PXA2xx internal register mapping:
 *
 * 0x40000000 - 0x41ffffff <--> 0xf2000000 - 0xf3ffffff
 * 0x44000000 - 0x45ffffff <--> 0xf4000000 - 0xf5ffffff
 * 0x48000000 - 0x49ffffff <--> 0xf6000000 - 0xf7ffffff
 * 0x4c000000 - 0x4dffffff <--> 0xf8000000 - 0xf9ffffff
 * 0x50000000 - 0x51ffffff <--> 0xfa000000 - 0xfbffffff
 * 0x54000000 - 0x55ffffff <--> 0xfc000000 - 0xfdffffff
 * 0x58000000 - 0x59ffffff <--> 0xfe000000 - 0xffffffff
 *
 * Note that not all PXA2xx chips implement all those addresses, and the
 * kernel only maps the minimum needed range of this mapping.
 */
#define io_v2p(x) (0x3c000000 + ((x) & 0x01ffffff) + (((x) & 0x0e000000) << 1))
#define io_p2v(x) IOMEM(0xf2000000 + ((x) & 0x01ffffff) + (((x) & 0x1c000000) >> 1))

#ifndef __ASSEMBLY__
# define __REG(x)	(*((volatile u32 __iomem *)io_p2v(x)))

/* With indexed regs we don't want to feed the index through io_p2v()
   especially if it is a variable, otherwise horrible code will result. */
# define __REG2(x,y)	\
	(*(volatile u32 __iomem*)((u32)&__REG(x) + (y)))

# define __PREG(x)	(io_v2p((u32)&(x)))

#else

# define __REG(x)	io_p2v(x)
# define __PREG(x)	io_v2p(x)

#endif


#endif
