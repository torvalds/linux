/*
 * arch/arm/mach-imx/mx1ads.c
 *
 * Initially based on:
 *	linux-2.6.7-imx/arch/arm/mach-imx/scb9328.c
 *	Copyright (c) 2004 Sascha Hauer <sascha@saschahauer.de>
 *
 * 2004 (c) MontaVista Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include <mach/irqs.h>
#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx1-mx2.h>
#include "devices.h"

/*
 * UARTs platform data
 */
static int mxc_uart1_pins[] = {
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

static int uart1_mxc_exit(struct platform_device *pdev)
{
	mxc_gpio_release_multiple_pins(mxc_uart1_pins,
			ARRAY_SIZE(mxc_uart1_pins));
	return 0;
}

static int mxc_uart2_pins[] = {
	PB28_PF_UART2_CTS,
	PB29_PF_UART2_RTS,
	PB30_PF_UART2_TXD,
	PB31_PF_UART2_RXD,
};

static int uart2_mxc_init(struct platform_device *pdev)
{
	return mxc_gpio_setup_multiple_pins(mxc_uart2_pins,
			ARRAY_SIZE(mxc_uart2_pins), "UART2");
}

static int uart2_mxc_exit(struct platform_device *pdev)
{
	mxc_gpio_release_multiple_pins(mxc_uart2_pins,
			ARRAY_SIZE(mxc_uart2_pins));
	return 0;
}

static struct imxuart_platform_data uart_pdata[] = {
	{
		.init = uart1_mxc_init,
		.exit = uart1_mxc_exit,
		.flags = IMXUART_HAVE_RTSCTS,
	}, {
		.init = uart2_mxc_init,
		.exit = uart2_mxc_exit,
		.flags = IMXUART_HAVE_RTSCTS,
	},
};

/*
 * Physmap flash
 */

static struct physmap_flash_data mx1ads_flash_data = {
	.width		= 4,		/* bankwidth in bytes */
};

static struct resource flash_resource = {
	.start	= IMX_CS0_PHYS,
	.end	= IMX_CS0_PHYS + SZ_32M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device flash_device = {
	.name	= "physmap-flash",
	.id	= 0,
	.resource = &flash_resource,
	.num_resources = 1,
};

/*
 * Board init
 */
static void __init mx1ads_init(void)
{
	/* UART */
	mxc_register_device(&imx_uart1_device, &uart_pdata[0]);
	mxc_register_device(&imx_uart2_device, &uart_pdata[1]);

	/* Physmap flash */
	mxc_register_device(&flash_device, &mx1ads_flash_data);
}

static void __init mx1ads_timer_init(void)
{
	mxc_clocks_init(32000);
	mxc_timer_init("gpt_clk");
}

struct sys_timer mx1ads_timer = {
	.init	= mx1ads_timer_init,
};

MACHINE_START(MX1ADS, "Freescale MX1ADS")
	/* Maintainer: Sascha Hauer, Pengutronix */
	.phys_io	= IMX_IO_PHYS,
	.io_pg_offst	= (IMX_IO_BASE >> 18) & 0xfffc,
	.boot_params	= PHYS_OFFSET + 0x100,
	.map_io		= mxc_map_io,
	.init_irq	= mxc_init_irq,
	.timer		= &mx1ads_timer,
	.init_machine	= mx1ads_init,
MACHINE_END

MACHINE_START(MXLADS, "Freescale MXLADS")
	.phys_io	= IMX_IO_PHYS,
	.io_pg_offst	= (IMX_IO_BASE >> 18) & 0xfffc,
	.boot_params	= PHYS_OFFSET + 0x100,
	.map_io		= mxc_map_io,
	.init_irq	= mxc_init_irq,
	.timer		= &mx1ads_timer,
	.init_machine	= mx1ads_init,
MACHINE_END
