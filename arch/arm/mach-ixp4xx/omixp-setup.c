/*
 * arch/arm/mach-ixp4xx/omixp-setup.c
 *
 * omicron ixp4xx board setup
 *      Copyright (C) 2009 OMICRON electronics GmbH
 *
 * based nslu2-setup.c, ixdp425-setup.c:
 *      Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#ifdef CONFIG_LEDS_CLASS
#include <linux/leds.h>
#endif

#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>

static struct resource omixp_flash_resources[] = {
	{
		.flags	= IORESOURCE_MEM,
	}, {
		.flags	= IORESOURCE_MEM,
	},
};

static struct mtd_partition omixp_partitions[] = {
	{
		.name =		"Recovery Bootloader",
		.size =		0x00020000,
		.offset =	0,
	}, {
		.name =		"Calibration Data",
		.size =		0x00020000,
		.offset =	0x00020000,
	}, {
		.name =		"Recovery FPGA",
		.size =		0x00020000,
		.offset =	0x00040000,
	}, {
		.name =		"Release Bootloader",
		.size =		0x00020000,
		.offset =	0x00060000,
	}, {
		.name =		"Release FPGA",
		.size =		0x00020000,
		.offset =	0x00080000,
	}, {
		.name =		"Kernel",
		.size =		0x00160000,
		.offset =	0x000a0000,
	}, {
		.name =		"Filesystem",
		.size =		0x00C00000,
		.offset =	0x00200000,
	}, {
		.name =		"Persistent Storage",
		.size =		0x00200000,
		.offset =	0x00E00000,
	},
};

static struct flash_platform_data omixp_flash_data[] = {
	{
		.map_name	= "cfi_probe",
		.parts		= omixp_partitions,
		.nr_parts	= ARRAY_SIZE(omixp_partitions),
	}, {
		.map_name	= "cfi_probe",
		.parts		= NULL,
		.nr_parts	= 0,
	},
};

static struct platform_device omixp_flash_device[] = {
	{
		.name		= "IXP4XX-Flash",
		.id		= 0,
		.dev = {
			.platform_data = &omixp_flash_data[0],
		},
		.resource = &omixp_flash_resources[0],
		.num_resources = 1,
	}, {
		.name		= "IXP4XX-Flash",
		.id		= 1,
		.dev = {
			.platform_data = &omixp_flash_data[1],
		},
		.resource = &omixp_flash_resources[1],
		.num_resources = 1,
	},
};

/* Swap UART's - These boards have the console on UART2. The following
 * configuration is used:
 *      ttyS0 .. UART2
 *      ttyS1 .. UART1
 * This way standard images can be used with the kernel that expect
 * the console on ttyS0.
 */
static struct resource omixp_uart_resources[] = {
	{
		.start		= IXP4XX_UART2_BASE_PHYS,
		.end		= IXP4XX_UART2_BASE_PHYS + 0x0fff,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= IXP4XX_UART1_BASE_PHYS,
		.end		= IXP4XX_UART1_BASE_PHYS + 0x0fff,
		.flags		= IORESOURCE_MEM,
	},
};

static struct plat_serial8250_port omixp_uart_data[] = {
	{
		.mapbase	= IXP4XX_UART2_BASE_PHYS,
		.membase	= (char *)IXP4XX_UART2_BASE_VIRT + REG_OFFSET,
		.irq		= IRQ_IXP4XX_UART2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IXP4XX_UART_XTAL,
	}, {
		.mapbase	= IXP4XX_UART1_BASE_PHYS,
		.membase	= (char *)IXP4XX_UART1_BASE_VIRT + REG_OFFSET,
		.irq		= IRQ_IXP4XX_UART1,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IXP4XX_UART_XTAL,
	}, {
		/* list termination */
	}
};

static struct platform_device omixp_uart = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev.platform_data	= omixp_uart_data,
	.num_resources		= 2,
	.resource		= omixp_uart_resources,
};

static struct gpio_led mic256_led_pins[] = {
	{
		.name		= "LED-A",
		.gpio		= 7,
	},
};

static struct gpio_led_platform_data mic256_led_data = {
	.num_leds		= ARRAY_SIZE(mic256_led_pins),
	.leds			= mic256_led_pins,
};

static struct platform_device mic256_leds = {
	.name			= "leds-gpio",
	.id			= -1,
	.dev.platform_data	= &mic256_led_data,
};

/* Built-in 10/100 Ethernet MAC interfaces */
static struct eth_plat_info ixdp425_plat_eth[] = {
	{
		.phy		= 0,
		.rxq		= 3,
		.txreadyq	= 20,
	}, {
		.phy		= 1,
		.rxq		= 4,
		.txreadyq	= 21,
	},
};

static struct platform_device ixdp425_eth[] = {
	{
		.name			= "ixp4xx_eth",
		.id			= IXP4XX_ETH_NPEB,
		.dev.platform_data	= ixdp425_plat_eth,
	}, {
		.name			= "ixp4xx_eth",
		.id			= IXP4XX_ETH_NPEC,
		.dev.platform_data	= ixdp425_plat_eth + 1,
	},
};


static struct platform_device *devixp_pldev[] __initdata = {
	&omixp_uart,
	&omixp_flash_device[0],
	&ixdp425_eth[0],
	&ixdp425_eth[1],
};

static struct platform_device *mic256_pldev[] __initdata = {
	&omixp_uart,
	&omixp_flash_device[0],
	&mic256_leds,
	&ixdp425_eth[0],
	&ixdp425_eth[1],
};

static struct platform_device *miccpt_pldev[] __initdata = {
	&omixp_uart,
	&omixp_flash_device[0],
	&omixp_flash_device[1],
	&ixdp425_eth[0],
	&ixdp425_eth[1],
};

static void __init omixp_init(void)
{
	ixp4xx_sys_init();

	/* 16MiB Boot Flash */
	omixp_flash_resources[0].start = IXP4XX_EXP_BUS_BASE(0);
	omixp_flash_resources[0].end   = IXP4XX_EXP_BUS_END(0);

	/* 32 MiB Data Flash */
	omixp_flash_resources[1].start = IXP4XX_EXP_BUS_BASE(2);
	omixp_flash_resources[1].end   = IXP4XX_EXP_BUS_END(2);

	if (machine_is_devixp())
		platform_add_devices(devixp_pldev, ARRAY_SIZE(devixp_pldev));
	else if (machine_is_miccpt())
		platform_add_devices(miccpt_pldev, ARRAY_SIZE(miccpt_pldev));
	else if (machine_is_mic256())
		platform_add_devices(mic256_pldev, ARRAY_SIZE(mic256_pldev));
}

#ifdef CONFIG_MACH_DEVIXP
MACHINE_START(DEVIXP, "Omicron DEVIXP")
	.atag_offset    = 0x100,
	.map_io		= ixp4xx_map_io,
	.init_early	= ixp4xx_init_early,
	.init_irq	= ixp4xx_init_irq,
	.timer          = &ixp4xx_timer,
	.init_machine	= omixp_init,
	.restart	= ixp4xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_MICCPT
MACHINE_START(MICCPT, "Omicron MICCPT")
	.atag_offset    = 0x100,
	.map_io		= ixp4xx_map_io,
	.init_early	= ixp4xx_init_early,
	.init_irq	= ixp4xx_init_irq,
	.timer          = &ixp4xx_timer,
	.init_machine	= omixp_init,
#if defined(CONFIG_PCI)
	.dma_zone_size	= SZ_64M,
#endif
	.restart	= ixp4xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_MIC256
MACHINE_START(MIC256, "Omicron MIC256")
	.atag_offset    = 0x100,
	.map_io		= ixp4xx_map_io,
	.init_early	= ixp4xx_init_early,
	.init_irq	= ixp4xx_init_irq,
	.timer          = &ixp4xx_timer,
	.init_machine	= omixp_init,
	.restart	= ixp4xx_restart,
MACHINE_END
#endif
