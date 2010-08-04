/*
 * arch/arm/mach-nuc93x/cpu.h
 *
 * Copyright (c) 2008 Nuvoton technology corporation
 * All rights reserved.
 *
 * Header file for NUC93X CPU support
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define IODESC_ENT(y)					\
{							\
	.virtual = (unsigned long)NUC93X_VA_##y,	\
	.pfn     = __phys_to_pfn(NUC93X_PA_##y),	\
	.length  = NUC93X_SZ_##y,			\
	.type    = MT_DEVICE,				\
}

#define NUC93X_8250PORT(name)					\
{								\
	.membase	= name##_BA,				\
	.mapbase	= name##_PA,				\
	.irq		= IRQ_##name,				\
	.uartclk	= 57139200,				\
	.regshift	= 2,					\
	.iotype		= UPIO_MEM,				\
	.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,	\
}

/*Cpu identifier register*/

#define NUC93XPDID	NUC93X_VA_GCR
#define NUC932_CPUID	0x29550091

/* extern file from cpu.c */

extern void nuc93x_clock_source(struct device *dev, unsigned char *src);
extern void nuc93x_init_clocks(void);
extern void nuc93x_map_io(struct map_desc *mach_desc, int mach_size);
extern void nuc93x_board_init(struct platform_device **device, int size);
extern struct platform_device nuc93x_serial_device;

