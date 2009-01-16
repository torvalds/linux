/*
 * arch/arm/mach-w90x900/include/mach/map.h
 *
 * Copyright (c) 2008 Nuvoton technology corporation
 * All rights reserved.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * Based on arch/arm/mach-s3c2410/include/mach/map.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __ASM_ARCH_MAP_H
#define __ASM_ARCH_MAP_H

#ifndef __ASSEMBLY__
#define W90X900_ADDR(x)		((void __iomem *)(0xF0000000 + (x)))
#else
#define W90X900_ADDR(x)		(0xF0000000 + (x))
#endif

#define AHB_IO_BASE		0xB0000000
#define APB_IO_BASE		0xB8000000
#define CLOCKPW_BASE		(APB_IO_BASE+0x200)
#define AIC_IO_BASE		(APB_IO_BASE+0x2000)
#define TIMER_IO_BASE		(APB_IO_BASE+0x1000)

/*
 * interrupt controller is the first thing we put in, to make
 * the assembly code for the irq detection easier
 */

#define W90X900_VA_IRQ		W90X900_ADDR(0x00000000)
#define W90X900_PA_IRQ		(0xB8002000)
#define W90X900_SZ_IRQ		SZ_4K

#define W90X900_VA_GCR		W90X900_ADDR(0x08002000)
#define W90X900_PA_GCR		(0xB0000000)
#define W90X900_SZ_GCR		SZ_4K

/* Clock and Power management */

#define W90X900_VA_CLKPWR	(W90X900_VA_GCR+0x200)
#define W90X900_PA_CLKPWR	(0xB0000200)
#define W90X900_SZ_CLKPWR	SZ_4K

/* EBI management */

#define W90X900_VA_EBI		W90X900_ADDR(0x00001000)
#define W90X900_PA_EBI		(0xB0001000)
#define W90X900_SZ_EBI		SZ_4K

/* UARTs */

#define W90X900_VA_UART		W90X900_ADDR(0x08000000)
#define W90X900_PA_UART		(0xB8000000)
#define W90X900_SZ_UART		SZ_4K

/* Timers */

#define W90X900_VA_TIMER	W90X900_ADDR(0x08001000)
#define W90X900_PA_TIMER	(0xB8001000)
#define W90X900_SZ_TIMER	SZ_4K

/* GPIO ports */

#define W90X900_VA_GPIO		W90X900_ADDR(0x08003000)
#define W90X900_PA_GPIO		(0xB8003000)
#define W90X900_SZ_GPIO		SZ_4K

#endif /* __ASM_ARCH_MAP_H */
