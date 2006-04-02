/*
 * linux/arch/arm/mach-omap2/devices.c
 *
 * OMAP2 platform device setup/initialization
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/mach-types.h>
#include <asm/mach/map.h>

#include <asm/arch/tc.h>
#include <asm/arch/board.h>
#include <asm/arch/mux.h>
#include <asm/arch/gpio.h>

extern void omap_nop_release(struct device *dev);

/*-------------------------------------------------------------------------*/

#if 	defined(CONFIG_I2C_OMAP) || defined(CONFIG_I2C_OMAP_MODULE)

#define OMAP2_I2C_BASE2		0x48072000
#define OMAP2_I2C_INT2		57

static struct resource i2c_resources2[] = {
	{
		.start		= OMAP2_I2C_BASE2,
		.end		= OMAP2_I2C_BASE2 + 0x3f,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= OMAP2_I2C_INT2,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device omap_i2c_device2 = {
        .name           = "i2c_omap",
        .id             = 2,
        .dev = {
                .release        = omap_nop_release,
        },
	.num_resources	= ARRAY_SIZE(i2c_resources2),
	.resource	= i2c_resources2,
};

/* See also arch/arm/plat-omap/devices.c for first I2C on 24xx */
static void omap_init_i2c(void)
{
	/* REVISIT: Second I2C not in use on H4? */
	if (machine_is_omap_h4())
		return;

	omap_cfg_reg(J15_24XX_I2C2_SCL);
	omap_cfg_reg(H19_24XX_I2C2_SDA);
	(void) platform_device_register(&omap_i2c_device2);
}

#else

static void omap_init_i2c(void) {}

#endif

#if defined(CONFIG_OMAP_STI)

#define OMAP2_STI_BASE		IO_ADDRESS(0x48068000)
#define OMAP2_STI_CHANNEL_BASE	0x54000000
#define OMAP2_STI_IRQ		4

static struct resource sti_resources[] = {
	{
		.start		= OMAP2_STI_BASE,
		.end		= OMAP2_STI_BASE + 0x7ff,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= OMAP2_STI_CHANNEL_BASE,
		.end		= OMAP2_STI_CHANNEL_BASE + SZ_64K - 1,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= OMAP2_STI_IRQ,
		.flags		= IORESOURCE_IRQ,
	}
};

static struct platform_device sti_device = {
	.name		= "sti",
	.id		= -1,
	.dev = {
		.release	= omap_nop_release,
	},
	.num_resources	= ARRAY_SIZE(sti_resources),
	.resource	= sti_resources,
};

static inline void omap_init_sti(void)
{
	platform_device_register(&sti_device);
}
#else
static inline void omap_init_sti(void) {}
#endif

/*-------------------------------------------------------------------------*/

static int __init omap2_init_devices(void)
{
	/* please keep these calls, and their implementations above,
	 * in alphabetical order so they're easier to sort through.
	 */
	omap_init_i2c();
	omap_init_sti();

	return 0;
}
arch_initcall(omap2_init_devices);

