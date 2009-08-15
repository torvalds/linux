/*
 * arch/arm/mach-w90x900/cpu.h
 *
 * Based on linux/include/asm-arm/plat-s3c24xx/cpu.h by Ben Dooks
 *
 * Copyright (c) 2008 Nuvoton technology corporation
 * All rights reserved.
 *
 * Header file for W90X900 CPU support
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define IODESC_ENT(y)                                  \
{                                                      \
       .virtual = (unsigned long)W90X900_VA_##y,       \
       .pfn     = __phys_to_pfn(W90X900_PA_##y),       \
       .length  = W90X900_SZ_##y,                      \
       .type    = MT_DEVICE,                           \
}

/*Cpu identifier register*/

#define W90X900PDID	W90X900_VA_GCR
#define W90P910_CPUID	0x02900910
#define W90P920_CPUID	0x02900920
#define W90P950_CPUID	0x02900950
#define W90N960_CPUID	0x02900960

struct w90x900_uartcfg;
struct map_desc;
struct sys_timer;

/* core initialisation functions */

extern void w90x900_init_irq(void);
extern void w90p910_init_io(struct map_desc *mach_desc, int size);
extern void w90p910_init_uarts(struct w90x900_uartcfg *cfg, int no);
extern void w90p910_init_clocks(void);
extern void w90p910_map_io(struct map_desc *mach_desc, int size);
extern struct platform_device w90p910_serial_device;
extern struct sys_timer w90x900_timer;

#define W90X900_8250PORT(name)					\
{								\
	.membase	= name##_BA,				\
	.mapbase	= name##_PA,				\
	.irq		= IRQ_##name,				\
	.uartclk	= 11313600,				\
	.regshift	= 2,					\
	.iotype		= UPIO_MEM,				\
	.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,	\
}
