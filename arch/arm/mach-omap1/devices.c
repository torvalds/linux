/*
 * linux/arch/arm/mach-omap1/devices.c
 *
 * OMAP1 platform device setup/initialization
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

#if	defined(CONFIG_OMAP1610_IR) || defined(CONFIG_OMAP161O_IR_MODULE)

static u64 irda_dmamask = 0xffffffff;

static struct platform_device omap1610ir_device = {
	.name = "omap1610-ir",
	.id = -1,
	.dev = {
		.dma_mask	= &irda_dmamask,
	},
};

static void omap_init_irda(void)
{
	/* FIXME define and use a boot tag, members something like:
	 *  u8		uart;		// uart1, or uart3
	 * ... but driver only handles uart3 for now
	 *  s16		fir_sel;	// gpio for SIR vs FIR
	 * ... may prefer a callback for SIR/MIR/FIR mode select;
	 * while h2 uses a GPIO, H3 uses a gpio expander
	 */
	if (machine_is_omap_h2()
			|| machine_is_omap_h3())
		(void) platform_device_register(&omap1610ir_device);
}
#else
static inline void omap_init_irda(void) {}
#endif

/*-------------------------------------------------------------------------*/

#if	defined(CONFIG_OMAP_RTC) || defined(CONFIG_OMAP_RTC)

#define	OMAP_RTC_BASE		0xfffb4800

static struct resource rtc_resources[] = {
	{
		.start		= OMAP_RTC_BASE,
		.end		= OMAP_RTC_BASE + 0x5f,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= INT_RTC_TIMER,
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= INT_RTC_ALARM,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device omap_rtc_device = {
	.name           = "omap_rtc",
	.id             = -1,
	.num_resources	= ARRAY_SIZE(rtc_resources),
	.resource	= rtc_resources,
};

static void omap_init_rtc(void)
{
	(void) platform_device_register(&omap_rtc_device);
}
#else
static inline void omap_init_rtc(void) {}
#endif

#if defined(CONFIG_OMAP_STI)

#define OMAP1_STI_BASE		IO_ADDRESS(0xfffea000)
#define OMAP1_STI_CHANNEL_BASE	(OMAP1_STI_BASE + 0x400)

static struct resource sti_resources[] = {
	{
		.start		= OMAP1_STI_BASE,
		.end		= OMAP1_STI_BASE + SZ_1K - 1,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= OMAP1_STI_CHANNEL_BASE,
		.end		= OMAP1_STI_CHANNEL_BASE + SZ_1K - 1,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= INT_1610_STI,
		.flags		= IORESOURCE_IRQ,
	}
};

static struct platform_device sti_device = {
	.name		= "sti",
	.id		= -1,
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

/*
 * This gets called after board-specific INIT_MACHINE, and initializes most
 * on-chip peripherals accessible on this board (except for few like USB):
 *
 *  (a) Does any "standard config" pin muxing needed.  Board-specific
 *	code will have muxed GPIO pins and done "nonstandard" setup;
 *	that code could live in the boot loader.
 *  (b) Populating board-specific platform_data with the data drivers
 *	rely on to handle wiring variations.
 *  (c) Creating platform devices as meaningful on this board and
 *	with this kernel configuration.
 *
 * Claiming GPIOs, and setting their direction and initial values, is the
 * responsibility of the device drivers.  So is responding to probe().
 *
 * Board-specific knowlege like creating devices or pin setup is to be
 * kept out of drivers as much as possible.  In particular, pin setup
 * may be handled by the boot loader, and drivers should expect it will
 * normally have been done by the time they're probed.
 */
static int __init omap1_init_devices(void)
{
	/* please keep these calls, and their implementations above,
	 * in alphabetical order so they're easier to sort through.
	 */
	omap_init_irda();
	omap_init_rtc();
	omap_init_sti();

	return 0;
}
arch_initcall(omap1_init_devices);

