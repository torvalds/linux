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
#include <linux/slab.h>

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>

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
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xfffc,
	.map_io		= ixp4xx_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer		= &ixp4xx_timer,
	.boot_params	= 0x0100,
	.init_machine	= coyote_init,
MACHINE_END
#endif

/*
 * IXDPG425 is identical to Coyote except for which serial port
 * is connected.
 */
#ifdef CONFIG_MACH_IXDPG425
MACHINE_START(IXDPG425, "Intel IXDPG425")
	/* Maintainer: MontaVista Software, Inc. */
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xfffc,
	.map_io		= ixp4xx_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer		= &ixp4xx_timer,
	.boot_params	= 0x0100,
	.init_machine	= coyote_init,
MACHINE_END
#endif

