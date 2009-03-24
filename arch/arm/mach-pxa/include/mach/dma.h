/*
 *  arch/arm/mach-pxa/include/mach/dma.h
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

#include <mach/hardware.h>

/* DMA Controller Registers Definitions */
#define DMAC_REGS_VIRT	io_p2v(0x40000000)

#include <plat/dma.h>
#endif /* _ASM_ARCH_DMA_H */
