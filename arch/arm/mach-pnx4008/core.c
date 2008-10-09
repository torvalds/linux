/*
 * arch/arm/mach-pnx4008/core.c
 *
 * PNX4008 core startup code
 *
 * Authors: Vitaly Wool, Dmitry Chigirev,
 * Grigory Tolstolytkin, Dmitry Pervushin <source@mvista.com>
 *
 * Based on reference code received from Philips:
 * Copyright (C) 2003 Philips Semiconductors
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/serial_8250.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/system.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

#include <mach/irq.h>
#include <mach/clock.h>
#include <mach/dma.h>

struct resource spipnx_0_resources[] = {
	{
		.start = PNX4008_SPI1_BASE,
		.end = PNX4008_SPI1_BASE + SZ_4K,
		.flags = IORESOURCE_MEM,
	}, {
		.start = PER_SPI1_REC_XMIT,
		.flags = IORESOURCE_DMA,
	}, {
		.start = SPI1_INT,
		.flags = IORESOURCE_IRQ,
	}, {
		.flags = 0,
	},
};

struct resource spipnx_1_resources[] = {
	{
		.start = PNX4008_SPI2_BASE,
		.end = PNX4008_SPI2_BASE + SZ_4K,
		.flags = IORESOURCE_MEM,
	}, {
		.start = PER_SPI2_REC_XMIT,
		.flags = IORESOURCE_DMA,
	}, {
		.start = SPI2_INT,
		.flags = IORESOURCE_IRQ,
	}, {
		.flags = 0,
	}
};

static struct spi_board_info spi_board_info[] __initdata = {
	{
		.modalias	= "m25p80",
		.max_speed_hz	= 1000000,
		.bus_num	= 1,
		.chip_select	= 0,
	},
};

static struct platform_device spipnx_1 = {
	.name = "spipnx",
	.id = 1,
	.num_resources = ARRAY_SIZE(spipnx_0_resources),
	.resource = spipnx_0_resources,
	.dev = {
		.coherent_dma_mask = 0xFFFFFFFF,
		},
};

static struct platform_device spipnx_2 = {
	.name = "spipnx",
	.id = 2,
	.num_resources = ARRAY_SIZE(spipnx_1_resources),
	.resource = spipnx_1_resources,
	.dev = {
		.coherent_dma_mask = 0xFFFFFFFF,
		},
};

static struct plat_serial8250_port platform_serial_ports[] = {
	{
		.membase = (void *)__iomem(IO_ADDRESS(PNX4008_UART5_BASE)),
		.mapbase = (unsigned long)PNX4008_UART5_BASE,
		.irq = IIR5_INT,
		.uartclk = PNX4008_UART_CLK,
		.regshift = 2,
		.iotype = UPIO_MEM,
		.flags = UPF_BOOT_AUTOCONF | UPF_BUGGY_UART | UPF_SKIP_TEST,
	},
	{
		.membase = (void *)__iomem(IO_ADDRESS(PNX4008_UART3_BASE)),
		.mapbase = (unsigned long)PNX4008_UART3_BASE,
		.irq = IIR3_INT,
		.uartclk = PNX4008_UART_CLK,
		.regshift = 2,
		.iotype = UPIO_MEM,
		.flags = UPF_BOOT_AUTOCONF | UPF_BUGGY_UART | UPF_SKIP_TEST,
	 },
	{}
};

static struct platform_device serial_device = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM,
	.dev = {
		.platform_data = &platform_serial_ports,
	},
};

static struct platform_device nand_flash_device = {
	.name = "pnx4008-flash",
	.id = -1,
	.dev = {
		.coherent_dma_mask = 0xFFFFFFFF,
	},
};

/* The dmamask must be set for OHCI to work */
static u64 ohci_dmamask = ~(u32) 0;

static struct resource ohci_resources[] = {
	{
		.start = IO_ADDRESS(PNX4008_USB_CONFIG_BASE),
		.end = IO_ADDRESS(PNX4008_USB_CONFIG_BASE + 0x100),
		.flags = IORESOURCE_MEM,
	}, {
		.start = USB_HOST_INT,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device ohci_device = {
	.name = "pnx4008-usb-ohci",
	.id = -1,
	.dev = {
		.dma_mask = &ohci_dmamask,
		.coherent_dma_mask = 0xffffffff,
		},
	.num_resources = ARRAY_SIZE(ohci_resources),
	.resource = ohci_resources,
};

static struct platform_device sdum_device = {
	.name = "pnx4008-sdum",
	.id = 0,
	.dev = {
		.coherent_dma_mask = 0xffffffff,
	},
};

static struct platform_device rgbfb_device = {
	.name = "pnx4008-rgbfb",
	.id = 0,
	.dev = {
		.coherent_dma_mask = 0xffffffff,
	}
};

struct resource watchdog_resources[] = {
	{
		.start = PNX4008_WDOG_BASE,
		.end = PNX4008_WDOG_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device watchdog_device = {
	.name = "pnx4008-watchdog",
	.id = -1,
	.num_resources = ARRAY_SIZE(watchdog_resources),
	.resource = watchdog_resources,
};

static struct platform_device *devices[] __initdata = {
	&spipnx_1,
	&spipnx_2,
	&serial_device,
	&ohci_device,
	&nand_flash_device,
	&sdum_device,
	&rgbfb_device,
	&watchdog_device,
};


extern void pnx4008_uart_init(void);

static void __init pnx4008_init(void)
{
	/*disable all START interrupt sources,
	   and clear all START interrupt flags */
	__raw_writel(0, START_INT_ER_REG(SE_PIN_BASE_INT));
	__raw_writel(0, START_INT_ER_REG(SE_INT_BASE_INT));
	__raw_writel(0xffffffff, START_INT_RSR_REG(SE_PIN_BASE_INT));
	__raw_writel(0xffffffff, START_INT_RSR_REG(SE_INT_BASE_INT));

	platform_add_devices(devices, ARRAY_SIZE(devices));
	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));
	/* Switch on the UART clocks */
	pnx4008_uart_init();
}

static struct map_desc pnx4008_io_desc[] __initdata = {
	{
		.virtual 	= IO_ADDRESS(PNX4008_IRAM_BASE),
		.pfn 		= __phys_to_pfn(PNX4008_IRAM_BASE),
		.length		= SZ_64K,
		.type 		= MT_DEVICE,
	}, {
		.virtual 	= IO_ADDRESS(PNX4008_NDF_FLASH_BASE),
		.pfn 		= __phys_to_pfn(PNX4008_NDF_FLASH_BASE),
		.length		= SZ_1M - SZ_128K,
		.type 		= MT_DEVICE,
	}, {
		.virtual 	= IO_ADDRESS(PNX4008_JPEG_CONFIG_BASE),
		.pfn 		= __phys_to_pfn(PNX4008_JPEG_CONFIG_BASE),
		.length		= SZ_128K * 3,
		.type 		= MT_DEVICE,
	}, {
		.virtual 	= IO_ADDRESS(PNX4008_DMA_CONFIG_BASE),
		.pfn 		= __phys_to_pfn(PNX4008_DMA_CONFIG_BASE),
		.length		= SZ_1M,
		.type 		= MT_DEVICE,
	}, {
		.virtual 	= IO_ADDRESS(PNX4008_AHB2FAB_BASE),
		.pfn 		= __phys_to_pfn(PNX4008_AHB2FAB_BASE),
		.length		= SZ_1M,
		.type 		= MT_DEVICE,
	},
};

void __init pnx4008_map_io(void)
{
	iotable_init(pnx4008_io_desc, ARRAY_SIZE(pnx4008_io_desc));
}

extern struct sys_timer pnx4008_timer;

MACHINE_START(PNX4008, "Philips PNX4008")
	/* Maintainer: MontaVista Software Inc. */
	.phys_io 		= 0x40090000,
	.io_pg_offst 		= (0xf4090000 >> 18) & 0xfffc,
	.boot_params		= 0x80000100,
	.map_io 		= pnx4008_map_io,
	.init_irq 		= pnx4008_init_irq,
	.init_machine 		= pnx4008_init,
	.timer 			= &pnx4008_timer,
MACHINE_END
