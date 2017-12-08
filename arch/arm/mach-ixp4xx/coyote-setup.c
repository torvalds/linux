// SPDX-License-Identifier: GPL-2.0
/*
 * arch/arm/mach-ixp4xx/coyote-setup.c
 *
 * Board setup for ADI Engineering and IXDGP425 boards
 *
 * Copyright (C) 2003-2005 MontaVista Software, Inc.
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_8250.h>

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>

#define COYOTE_IDE_BASE_PHYS	IXP4XX_EXP_BUS_BASE(3)
#define COYOTE_IDE_BASE_VIRT	0xFFFE1000
#define COYOTE_IDE_REGION_SIZE	0x1000

#define COYOTE_IDE_DATA_PORT	0xFFFE10E0
#define COYOTE_IDE_CTRL_PORT	0xFFFE10FC
#define COYOTE_IDE_ERROR_PORT	0xFFFE10E2
#define IRQ_COYOTE_IDE		IRQ_IXP4XX_GPIO5

static struct flash_platform_data coyote_flash_data = {
	.map_name	= "cfi_probe",
	.width		= 2,
};

static struct resource coyote_flash_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platform_device coyote_flash = {
	.name		= "IXP4XX-Flash",
	.id		= 0,
	.dev		= {
		.platform_data = &coyote_flash_data,
	},
	.num_resources	= 1,
	.resource	= &coyote_flash_resource,
};

static struct resource coyote_uart_resource = {
	.start	= IXP4XX_UART2_BASE_PHYS,
	.end	= IXP4XX_UART2_BASE_PHYS + 0x0fff,
	.flags	= IORESOURCE_MEM,
};

static struct plat_serial8250_port coyote_uart_data[] = {
	{
		.mapbase	= IXP4XX_UART2_BASE_PHYS,
		.membase	= (char *)IXP4XX_UART2_BASE_VIRT + REG_OFFSET,
		.irq		= IRQ_IXP4XX_UART2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IXP4XX_UART_XTAL,
	},
	{ },
};

static struct platform_device coyote_uart = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= coyote_uart_data,
	},
	.num_resources	= 1,
	.resource	= &coyote_uart_resource,
};

static struct platform_device *coyote_devices[] __initdata = {
	&coyote_flash,
	&coyote_uart
};

static void __init coyote_init(void)
{
	ixp4xx_sys_init();

	coyote_flash_resource.start = IXP4XX_EXP_BUS_BASE(0);
	coyote_flash_resource.end = IXP4XX_EXP_BUS_BASE(0) + SZ_32M - 1;

	*IXP4XX_EXP_CS0 |= IXP4XX_FLASH_WRITABLE;
	*IXP4XX_EXP_CS1 = *IXP4XX_EXP_CS0;

	if (machine_is_ixdpg425()) {
		coyote_uart_data[0].membase =
			(char*)(IXP4XX_UART1_BASE_VIRT + REG_OFFSET);
		coyote_uart_data[0].mapbase = IXP4XX_UART1_BASE_PHYS;
		coyote_uart_data[0].irq = IRQ_IXP4XX_UART1;
	}

	platform_add_devices(coyote_devices, ARRAY_SIZE(coyote_devices));
}

#ifdef CONFIG_ARCH_ADI_COYOTE
MACHINE_START(ADI_COYOTE, "ADI Engineering Coyote")
	/* Maintainer: MontaVista Software, Inc. */
	.map_io		= ixp4xx_map_io,
	.init_early	= ixp4xx_init_early,
	.init_irq	= ixp4xx_init_irq,
	.init_time	= ixp4xx_timer_init,
	.atag_offset	= 0x100,
	.init_machine	= coyote_init,
#if defined(CONFIG_PCI)
	.dma_zone_size	= SZ_64M,
#endif
	.restart	= ixp4xx_restart,
MACHINE_END
#endif

/*
 * IXDPG425 is identical to Coyote except for which serial port
 * is connected.
 */
#ifdef CONFIG_MACH_IXDPG425
MACHINE_START(IXDPG425, "Intel IXDPG425")
	/* Maintainer: MontaVista Software, Inc. */
	.map_io		= ixp4xx_map_io,
	.init_early	= ixp4xx_init_early,
	.init_irq	= ixp4xx_init_irq,
	.init_time	= ixp4xx_timer_init,
	.atag_offset	= 0x100,
	.init_machine	= coyote_init,
	.restart	= ixp4xx_restart,
MACHINE_END
#endif

