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

#include <linux/device.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <asm/system.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/page.h>

#include <asm/mach/map.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/arch/mmc.h>
#include <asm/arch/imx-uart.h>
#include <linux/interrupt.h>
#include "generic.h"

static struct resource cs89x0_resources[] = {
	[0] = {
		.start	= IMX_CS4_PHYS + 0x300,
		.end	= IMX_CS4_PHYS + 0x300 + 16,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_GPIOC(17),
		.end	= IRQ_GPIOC(17),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device cs89x0_device = {
	.name		= "cirrus-cs89x0",
	.num_resources	= ARRAY_SIZE(cs89x0_resources),
	.resource	= cs89x0_resources,
};

static struct imxuart_platform_data uart_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static struct resource imx_uart1_resources[] = {
	[0] = {
		.start	= 0x00206000,
		.end	= 0x002060FF,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= (UART1_MINT_RX),
		.end	= (UART1_MINT_RX),
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= (UART1_MINT_TX),
		.end	= (UART1_MINT_TX),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device imx_uart1_device = {
	.name		= "imx-uart",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(imx_uart1_resources),
	.resource	= imx_uart1_resources,
	.dev = {
		.platform_data = &uart_pdata,
	}
};

static struct resource imx_uart2_resources[] = {
	[0] = {
		.start	= 0x00207000,
		.end	= 0x002070FF,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= (UART2_MINT_RX),
		.end	= (UART2_MINT_RX),
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= (UART2_MINT_TX),
		.end	= (UART2_MINT_TX),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device imx_uart2_device = {
	.name		= "imx-uart",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(imx_uart2_resources),
	.resource	= imx_uart2_resources,
	.dev = {
		.platform_data = &uart_pdata,
	}
};

static struct platform_device *devices[] __initdata = {
	&cs89x0_device,
	&imx_uart1_device,
	&imx_uart2_device,
};

#ifdef CONFIG_MMC_IMX
static int mx1ads_mmc_card_present(void)
{
	/* MMC/SD Card Detect is PB 20 on MX1ADS V1.0.7 */
	return (SSR(1) & (1 << 20) ? 0 : 1);
}

static struct imxmmc_platform_data mx1ads_mmc_info = {
       .card_present = mx1ads_mmc_card_present,
};
#endif

static void __init
mx1ads_init(void)
{
#ifdef CONFIG_LEDS
	imx_gpio_mode(GPIO_PORTA | GPIO_OUT | 2);
#endif
#ifdef CONFIG_MMC_IMX
	/* SD/MMC card detect */
	imx_gpio_mode(GPIO_PORTB | GPIO_GIUS | GPIO_IN | 20);
	imx_set_mmc_info(&mx1ads_mmc_info);
#endif

	imx_gpio_mode(PC9_PF_UART1_CTS);
	imx_gpio_mode(PC10_PF_UART1_RTS);
	imx_gpio_mode(PC11_PF_UART1_TXD);
	imx_gpio_mode(PC12_PF_UART1_RXD);

	imx_gpio_mode(PB28_PF_UART2_CTS);
	imx_gpio_mode(PB29_PF_UART2_RTS);
	imx_gpio_mode(PB30_PF_UART2_TXD);
	imx_gpio_mode(PB31_PF_UART2_RXD);

	platform_add_devices(devices, ARRAY_SIZE(devices));
}

static void __init
mx1ads_map_io(void)
{
	imx_map_io();
}

MACHINE_START(MX1ADS, "Motorola MX1ADS")
	/* Maintainer: Sascha Hauer, Pengutronix */
	.phys_io	= 0x00200000,
	.io_pg_offst	= ((0xe0000000) >> 18) & 0xfffc,
	.boot_params	= 0x08000100,
	.map_io		= mx1ads_map_io,
	.init_irq	= imx_init_irq,
	.timer		= &imx_timer,
	.init_machine	= mx1ads_init,
MACHINE_END
