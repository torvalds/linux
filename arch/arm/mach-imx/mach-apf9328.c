/*
 * linux/arch/arm/mach-imx/mach-apf9328.c
 *
 * Copyright (c) 2005-2011 ARMadeus systems <support@armadeus.com>
 *
 * This work is based on mach-scb9328.c which is:
 * Copyright (c) 2004 Sascha Hauer <saschahauer@web.de>
 * Copyright (c) 2006-2008 Juergen Beisert <jbeisert@netscape.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/dm9000.h>
#include <linux/gpio.h>
#include <linux/i2c.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/iomux-mx1.h>

#include "devices-imx1.h"

static const int apf9328_pins[] __initconst = {
	/* UART1 */
	PC9_PF_UART1_CTS,
	PC10_PF_UART1_RTS,
	PC11_PF_UART1_TXD,
	PC12_PF_UART1_RXD,
	/* UART2 */
	PB28_PF_UART2_CTS,
	PB29_PF_UART2_RTS,
	PB30_PF_UART2_TXD,
	PB31_PF_UART2_RXD,
	/* I2C */
	PA15_PF_I2C_SDA,
	PA16_PF_I2C_SCL,
};

/*
 * The APF9328 can have up to 32MB NOR Flash
 */
static struct resource flash_resource = {
	.start	= MX1_CS0_PHYS,
	.end	= MX1_CS0_PHYS + SZ_32M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct physmap_flash_data apf9328_flash_data = {
	.width  = 2,
};

static struct platform_device apf9328_flash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev = {
		.platform_data = &apf9328_flash_data,
	},
	.resource = &flash_resource,
	.num_resources = 1,
};

/*
 * APF9328 has a DM9000 Ethernet controller
 */
static struct dm9000_plat_data dm9000_setup = {
	.flags          = DM9000_PLATF_16BITONLY
};

static struct resource dm9000_resources[] = {
	{
		.start  = MX1_CS4_PHYS + 0x00C00000,
		.end    = MX1_CS4_PHYS + 0x00C00001,
		.flags  = IORESOURCE_MEM,
	}, {
		.start  = MX1_CS4_PHYS + 0x00C00002,
		.end    = MX1_CS4_PHYS + 0x00C00003,
		.flags  = IORESOURCE_MEM,
	}, {
		/* irq number is run-time assigned */
		.flags  = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct platform_device dm9000x_device = {
	.name		= "dm9000",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(dm9000_resources),
	.resource	= dm9000_resources,
	.dev		= {
		.platform_data = &dm9000_setup,
	}
};

static const struct imxuart_platform_data uart1_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static const struct imxi2c_platform_data apf9328_i2c_data __initconst = {
	.bitrate = 100000,
};

static struct platform_device *devices[] __initdata = {
	&apf9328_flash_device,
	&dm9000x_device,
};

static void __init apf9328_init(void)
{
	imx1_soc_init();

	mxc_gpio_setup_multiple_pins(apf9328_pins,
			ARRAY_SIZE(apf9328_pins),
			"APF9328");

	imx1_add_imx_uart0(NULL);
	imx1_add_imx_uart1(&uart1_pdata);

	imx1_add_imx_i2c(&apf9328_i2c_data);

	dm9000_resources[2].start = gpio_to_irq(IMX_GPIO_NR(2, 14));
	dm9000_resources[2].end = gpio_to_irq(IMX_GPIO_NR(2, 14));
	platform_add_devices(devices, ARRAY_SIZE(devices));
}

static void __init apf9328_timer_init(void)
{
	mx1_clocks_init(32768);
}

static struct sys_timer apf9328_timer = {
	.init	= apf9328_timer_init,
};

MACHINE_START(APF9328, "Armadeus APF9328")
	/* Maintainer: Gwenhael Goavec-Merou, ARMadeus Systems */
	.map_io       = mx1_map_io,
	.init_early   = imx1_init_early,
	.init_irq     = mx1_init_irq,
	.handle_irq   = imx1_handle_irq,
	.timer        = &apf9328_timer,
	.init_machine = apf9328_init,
	.restart	= mxc_restart,
MACHINE_END
