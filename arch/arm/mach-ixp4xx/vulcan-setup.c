// SPDX-License-Identifier: GPL-2.0
/*
 * arch/arm/mach-ixp4xx/vulcan-setup.c
 *
 * Arcom/Eurotech Vulcan board-setup
 *
 * Copyright (C) 2010 Marc Zyngier <maz@misterjones.org>
 *
 * based on fsg-setup.c:
 *	Copyright (C) 2008 Rod Whitby <rod@whitby.id.au>
 */

#include <linux/if_ether.h>
#include <linux/irq.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>
#include <linux/io.h>
#include <linux/w1-gpio.h>
#include <linux/gpio/machine.h>
#include <linux/mtd/plat-ram.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>

static struct flash_platform_data vulcan_flash_data = {
	.map_name	= "cfi_probe",
	.width		= 2,
};

static struct resource vulcan_flash_resource = {
	.flags			= IORESOURCE_MEM,
};

static struct platform_device vulcan_flash = {
	.name			= "IXP4XX-Flash",
	.id			= 0,
	.dev = {
		.platform_data	= &vulcan_flash_data,
	},
	.resource		= &vulcan_flash_resource,
	.num_resources		= 1,
};

static struct platdata_mtd_ram vulcan_sram_data = {
	.mapname	= "Vulcan SRAM",
	.bankwidth	= 1,
};

static struct resource vulcan_sram_resource = {
	.flags			= IORESOURCE_MEM,
};

static struct platform_device vulcan_sram = {
	.name			= "mtd-ram",
	.id			= 0,
	.dev = {
		.platform_data	= &vulcan_sram_data,
	},
	.resource		= &vulcan_sram_resource,
	.num_resources		= 1,
};

static struct resource vulcan_uart_resources[] = {
	[0] = {
		.start		= IXP4XX_UART1_BASE_PHYS,
		.end		= IXP4XX_UART1_BASE_PHYS + 0x0fff,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IXP4XX_UART2_BASE_PHYS,
		.end		= IXP4XX_UART2_BASE_PHYS + 0x0fff,
		.flags		= IORESOURCE_MEM,
	},
	[2] = {
		.flags		= IORESOURCE_MEM,
	},
};

static struct plat_serial8250_port vulcan_uart_data[] = {
	[0] = {
		.mapbase	= IXP4XX_UART1_BASE_PHYS,
		.membase	= (char *)IXP4XX_UART1_BASE_VIRT + REG_OFFSET,
		.irq		= IRQ_IXP4XX_UART1,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IXP4XX_UART_XTAL,
	},
	[1] = {
		.mapbase	= IXP4XX_UART2_BASE_PHYS,
		.membase	= (char *)IXP4XX_UART2_BASE_VIRT + REG_OFFSET,
		.irq		= IRQ_IXP4XX_UART2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IXP4XX_UART_XTAL,
	},
	[2] = {
		.irq		= IXP4XX_GPIO_IRQ(4),
		.irqflags	= IRQF_TRIGGER_LOW,
		.flags		= UPF_IOREMAP | UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.uartclk	= 1843200,
	},
	[3] = {
		.irq		= IXP4XX_GPIO_IRQ(4),
		.irqflags	= IRQF_TRIGGER_LOW,
		.flags		= UPF_IOREMAP | UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.uartclk	= 1843200,
	},
	{ }
};

static struct platform_device vulcan_uart = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev = {
		.platform_data	= vulcan_uart_data,
	},
	.resource		= vulcan_uart_resources,
	.num_resources		= ARRAY_SIZE(vulcan_uart_resources),
};

static struct eth_plat_info vulcan_plat_eth[] = {
	[0] = {
		.phy		= 0,
		.rxq		= 3,
		.txreadyq	= 20,
	},
	[1] = {
		.phy		= 1,
		.rxq		= 4,
		.txreadyq	= 21,
	},
};

static struct platform_device vulcan_eth[] = {
	[0] = {
		.name			= "ixp4xx_eth",
		.id			= IXP4XX_ETH_NPEB,
		.dev = {
			.platform_data	= &vulcan_plat_eth[0],
		},
	},
	[1] = {
		.name			= "ixp4xx_eth",
		.id			= IXP4XX_ETH_NPEC,
		.dev = {
			.platform_data	= &vulcan_plat_eth[1],
		},
	},
};

static struct resource vulcan_max6369_resource = {
	.flags			= IORESOURCE_MEM,
};

static struct platform_device vulcan_max6369 = {
	.name			= "max6369_wdt",
	.id			= -1,
	.resource		= &vulcan_max6369_resource,
	.num_resources		= 1,
};

static struct gpiod_lookup_table vulcan_w1_gpiod_table = {
	.dev_id = "w1-gpio",
	.table = {
		GPIO_LOOKUP_IDX("IXP4XX_GPIO_CHIP", 14, NULL, 0,
				GPIO_ACTIVE_HIGH | GPIO_OPEN_DRAIN),
	},
};

static struct w1_gpio_platform_data vulcan_w1_gpio_pdata = {
	/* Intentionally left blank */
};

static struct platform_device vulcan_w1_gpio = {
	.name			= "w1-gpio",
	.id			= 0,
	.dev			= {
		.platform_data	= &vulcan_w1_gpio_pdata,
	},
};

static struct platform_device *vulcan_devices[] __initdata = {
	&vulcan_uart,
	&vulcan_flash,
	&vulcan_sram,
	&vulcan_max6369,
	&vulcan_eth[0],
	&vulcan_eth[1],
	&vulcan_w1_gpio,
};

static void __init vulcan_init(void)
{
	ixp4xx_sys_init();

	/* Flash is spread over both CS0 and CS1 */
	vulcan_flash_resource.start	 = IXP4XX_EXP_BUS_BASE(0);
	vulcan_flash_resource.end	 = IXP4XX_EXP_BUS_BASE(0) + SZ_32M - 1;
	*IXP4XX_EXP_CS0 = IXP4XX_EXP_BUS_CS_EN		|
			  IXP4XX_EXP_BUS_STROBE_T(3)	|
			  IXP4XX_EXP_BUS_SIZE(0xF)	|
			  IXP4XX_EXP_BUS_BYTE_RD16	|
			  IXP4XX_EXP_BUS_WR_EN;
	*IXP4XX_EXP_CS1 = *IXP4XX_EXP_CS0;

	/* SRAM on CS2, (256kB, 8bit, writable) */
	vulcan_sram_resource.start	= IXP4XX_EXP_BUS_BASE(2);
	vulcan_sram_resource.end	= IXP4XX_EXP_BUS_BASE(2) + SZ_256K - 1;
	*IXP4XX_EXP_CS2 = IXP4XX_EXP_BUS_CS_EN		|
			  IXP4XX_EXP_BUS_STROBE_T(1)	|
			  IXP4XX_EXP_BUS_HOLD_T(2)	|
			  IXP4XX_EXP_BUS_SIZE(9)	|
			  IXP4XX_EXP_BUS_SPLT_EN	|
			  IXP4XX_EXP_BUS_WR_EN		|
			  IXP4XX_EXP_BUS_BYTE_EN;

	/* XR16L2551 on CS3 (Moto style, 512 bytes, 8bits, writable) */
	vulcan_uart_resources[2].start	= IXP4XX_EXP_BUS_BASE(3);
	vulcan_uart_resources[2].end	= IXP4XX_EXP_BUS_BASE(3) + 16 - 1;
	vulcan_uart_data[2].mapbase	= vulcan_uart_resources[2].start;
	vulcan_uart_data[3].mapbase	= vulcan_uart_data[2].mapbase + 8;
	*IXP4XX_EXP_CS3 = IXP4XX_EXP_BUS_CS_EN		|
			  IXP4XX_EXP_BUS_STROBE_T(3)	|
			  IXP4XX_EXP_BUS_CYCLES(IXP4XX_EXP_BUS_CYCLES_MOTOROLA)|
			  IXP4XX_EXP_BUS_WR_EN		|
			  IXP4XX_EXP_BUS_BYTE_EN;

	/* GPIOS on CS4 (512 bytes, 8bits, writable) */
	*IXP4XX_EXP_CS4 = IXP4XX_EXP_BUS_CS_EN		|
			  IXP4XX_EXP_BUS_WR_EN		|
			  IXP4XX_EXP_BUS_BYTE_EN;

	/* max6369 on CS5 (512 bytes, 8bits, writable) */
	vulcan_max6369_resource.start	= IXP4XX_EXP_BUS_BASE(5);
	vulcan_max6369_resource.end	= IXP4XX_EXP_BUS_BASE(5);
	*IXP4XX_EXP_CS5 = IXP4XX_EXP_BUS_CS_EN		|
			  IXP4XX_EXP_BUS_WR_EN		|
			  IXP4XX_EXP_BUS_BYTE_EN;

	gpiod_add_lookup_table(&vulcan_w1_gpiod_table);
	platform_add_devices(vulcan_devices, ARRAY_SIZE(vulcan_devices));
}

MACHINE_START(ARCOM_VULCAN, "Arcom/Eurotech Vulcan")
	/* Maintainer: Marc Zyngier <maz@misterjones.org> */
	.map_io		= ixp4xx_map_io,
	.init_early	= ixp4xx_init_early,
	.init_irq	= ixp4xx_init_irq,
	.init_time	= ixp4xx_timer_init,
	.atag_offset	= 0x100,
	.init_machine	= vulcan_init,
#if defined(CONFIG_PCI)
	.dma_zone_size	= SZ_64M,
#endif
	.restart	= ixp4xx_restart,
MACHINE_END
