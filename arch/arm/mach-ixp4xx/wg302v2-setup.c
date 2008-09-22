/*
 * arch/arm/mach-ixp4xx/wg302-setup.c
 *
 * Board setup for the Netgear WG302 v2 and WAG302 v2
 *
 * Copyright (C) 2007 Imre Kaloz <Kaloz@openwrt.org>
 *
 * based on coyote-setup.c:
 *      Copyright (C) 2003-2005 MontaVista Software, Inc.
 *
 * Author: Imre Kaloz <kaloz@openwrt.org>
 *
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

static struct flash_platform_data wg302v2_flash_data = {
	.map_name	= "cfi_probe",
	.width		= 2,
};

static struct resource wg302v2_flash_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platform_device wg302v2_flash = {
	.name		= "IXP4XX-Flash",
	.id		= 0,
	.dev		= {
		.platform_data = &wg302v2_flash_data,
	},
	.num_resources	= 1,
	.resource	= &wg302v2_flash_resource,
};

static struct resource wg302v2_uart_resource = {
	.start	= IXP4XX_UART2_BASE_PHYS,
	.end	= IXP4XX_UART2_BASE_PHYS + 0x0fff,
	.flags	= IORESOURCE_MEM,
};

static struct plat_serial8250_port wg302v2_uart_data[] = {
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

static struct platform_device wg302v2_uart = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= wg302v2_uart_data,
	},
	.num_resources	= 1,
	.resource	= &wg302v2_uart_resource,
};

static struct platform_device *wg302v2_devices[] __initdata = {
	&wg302v2_flash,
	&wg302v2_uart,
};

static void __init wg302v2_init(void)
{
	ixp4xx_sys_init();

	wg302v2_flash_resource.start = IXP4XX_EXP_BUS_BASE(0);
	wg302v2_flash_resource.end = IXP4XX_EXP_BUS_BASE(0) + SZ_32M - 1;

	*IXP4XX_EXP_CS0 |= IXP4XX_FLASH_WRITABLE;
	*IXP4XX_EXP_CS1 = *IXP4XX_EXP_CS0;

	platform_add_devices(wg302v2_devices, ARRAY_SIZE(wg302v2_devices));
}

#ifdef CONFIG_MACH_WG302V2
MACHINE_START(WG302V2, "Netgear WG302 v2 / WAG302 v2")
	/* Maintainer: Imre Kaloz <kaloz@openwrt.org> */
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xfffc,
	.map_io		= ixp4xx_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer		= &ixp4xx_timer,
	.boot_params	= 0x0100,
	.init_machine	= wg302v2_init,
MACHINE_END
#endif
