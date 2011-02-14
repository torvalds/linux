/*
 * linux/arch/arm/mach-mx1/mach-scb9328.c
 *
 * Copyright (c) 2004 Sascha Hauer <saschahauer@web.de>
 * Copyright (c) 2006-2008 Juergen Beisert <jbeisert@netscape.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/interrupt.h>
#include <linux/dm9000.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/iomux-mx1.h>

#include "devices-imx1.h"

/*
 * This scb9328 has a 32MiB flash
 */
static struct resource flash_resource = {
	.start	= MX1_CS0_PHYS,
	.end	= MX1_CS0_PHYS + (32 * 1024 * 1024) - 1,
	.flags	= IORESOURCE_MEM,
};

static struct physmap_flash_data scb_flash_data = {
	.width  = 2,
};

static struct platform_device scb_flash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev = {
		.platform_data = &scb_flash_data,
	},
	.resource = &flash_resource,
	.num_resources = 1,
};

/*
 * scb9328 has a DM9000 network controller
 * connected to CS5, with 16 bit data path
 * and interrupt connected to GPIO 3
 */

/*
 * internal datapath is fixed 16 bit
 */
static struct dm9000_plat_data dm9000_platdata = {
	.flags	= DM9000_PLATF_16BITONLY,
};

/*
 * the DM9000 drivers wants two defined address spaces
 * to gain access to address latch registers and the data path.
 */
static struct resource dm9000x_resources[] = {
	{
		.name	= "address area",
		.start	= MX1_CS5_PHYS,
		.end	= MX1_CS5_PHYS + 1,
		.flags	= IORESOURCE_MEM,	/* address access */
	}, {
		.name	= "data area",
		.start	= MX1_CS5_PHYS + 4,
		.end	= MX1_CS5_PHYS + 5,
		.flags	= IORESOURCE_MEM,	/* data access */
	}, {
		.start	= IRQ_GPIOC(3),
		.end	= IRQ_GPIOC(3),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct platform_device dm9000x_device = {
	.name		= "dm9000",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(dm9000x_resources),
	.resource	= dm9000x_resources,
	.dev		= {
		.platform_data = &dm9000_platdata,
	}
};

static const int mxc_uart1_pins[] = {
	PC9_PF_UART1_CTS,
	PC10_PF_UART1_RTS,
	PC11_PF_UART1_TXD,
	PC12_PF_UART1_RXD,
};

static int uart1_mxc_init(struct platform_device *pdev)
{
	return mxc_gpio_setup_multiple_pins(mxc_uart1_pins,
			ARRAY_SIZE(mxc_uart1_pins), "UART1");
}

static void uart1_mxc_exit(struct platform_device *pdev)
{
	mxc_gpio_release_multiple_pins(mxc_uart1_pins,
			ARRAY_SIZE(mxc_uart1_pins));
}

static const struct imxuart_platform_data uart_pdata __initconst = {
	.init = uart1_mxc_init,
	.exit = uart1_mxc_exit,
	.flags = IMXUART_HAVE_RTSCTS,
};

static struct platform_device *devices[] __initdata = {
	&scb_flash_device,
	&dm9000x_device,
};

/*
 * scb9328_init - Init the CPU card itself
 */
static void __init scb9328_init(void)
{
	imx1_add_imx_uart0(&uart_pdata);

	printk(KERN_INFO"Scb9328: Adding devices\n");
	platform_add_devices(devices, ARRAY_SIZE(devices));
}

static void __init scb9328_timer_init(void)
{
	mx1_clocks_init(32000);
}

static struct sys_timer scb9328_timer = {
	.init	= scb9328_timer_init,
};

MACHINE_START(SCB9328, "Synertronixx scb9328")
    /* Sascha Hauer */
	.boot_params	= 0x08000100,
	.map_io		= mx1_map_io,
	.init_irq	= mx1_init_irq,
	.timer		= &scb9328_timer,
	.init_machine	= scb9328_init,
MACHINE_END
