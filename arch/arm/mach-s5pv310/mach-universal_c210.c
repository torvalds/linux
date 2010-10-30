/* linux/arch/arm/mach-s5pv310/mach-universal_c210.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/serial_core.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>
#include <asm/hardware/cache-l2x0.h>

#include <plat/regs-serial.h>
#include <plat/s5pv310.h>
#include <plat/cpu.h>

#include <mach/map.h>

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define UNIVERSAL_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define UNIVERSAL_ULCON_DEFAULT	S3C2410_LCON_CS8

#define UNIVERSAL_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG256 |	\
				 S5PV210_UFCON_RXTRIG256)

static struct s3c2410_uartcfg universal_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.ucon		= UNIVERSAL_UCON_DEFAULT,
		.ulcon		= UNIVERSAL_ULCON_DEFAULT,
		.ufcon		= UNIVERSAL_UFCON_DEFAULT,
	},
	[1] = {
		.hwport		= 1,
		.ucon		= UNIVERSAL_UCON_DEFAULT,
		.ulcon		= UNIVERSAL_ULCON_DEFAULT,
		.ufcon		= UNIVERSAL_UFCON_DEFAULT,
	},
	[2] = {
		.hwport		= 2,
		.ucon		= UNIVERSAL_UCON_DEFAULT,
		.ulcon		= UNIVERSAL_ULCON_DEFAULT,
		.ufcon		= UNIVERSAL_UFCON_DEFAULT,
	},
	[3] = {
		.hwport		= 3,
		.ucon		= UNIVERSAL_UCON_DEFAULT,
		.ulcon		= UNIVERSAL_ULCON_DEFAULT,
		.ufcon		= UNIVERSAL_UFCON_DEFAULT,
	},
};

static void __init universal_map_io(void)
{
	s5p_init_io(NULL, 0, S5P_VA_CHIPID);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(universal_uartcfgs, ARRAY_SIZE(universal_uartcfgs));
}

static void __init universal_machine_init(void)
{
#ifdef CONFIG_CACHE_L2X0
	l2x0_init(S5P_VA_L2CC, 1 << 28, 0xffffffff);
#endif
}

MACHINE_START(UNIVERSAL_C210, "UNIVERSAL_C210")
	/* Maintainer: Kyungmin Park <kyungmin.park@samsung.com> */
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= s5pv310_init_irq,
	.map_io		= universal_map_io,
	.init_machine	= universal_machine_init,
	.timer		= &s5pv310_timer,
MACHINE_END
