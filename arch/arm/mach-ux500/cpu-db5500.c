/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/platform_device.h>
#include <linux/amba/bus.h>
#include <linux/io.h>
#include <linux/irq.h>

#include <asm/mach/map.h>

#include <plat/gpio.h>

#include <mach/hardware.h>
#include <mach/devices.h>
#include <mach/setup.h>
#include <mach/irqs.h>

#include "devices-db5500.h"

static struct map_desc u5500_uart_io_desc[] __initdata = {
	__IO_DEV_DESC(U5500_UART0_BASE, SZ_4K),
	__IO_DEV_DESC(U5500_UART2_BASE, SZ_4K),
};

static struct map_desc u5500_io_desc[] __initdata = {
	__IO_DEV_DESC(U5500_GIC_CPU_BASE, SZ_4K),
	__IO_DEV_DESC(U5500_GIC_DIST_BASE, SZ_4K),
	__IO_DEV_DESC(U5500_L2CC_BASE, SZ_4K),
	__IO_DEV_DESC(U5500_TWD_BASE, SZ_4K),
	__IO_DEV_DESC(U5500_MTU0_BASE, SZ_4K),
	__IO_DEV_DESC(U5500_SCU_BASE, SZ_4K),
	__IO_DEV_DESC(U5500_BACKUPRAM0_BASE, SZ_8K),

	__IO_DEV_DESC(U5500_GPIO0_BASE, SZ_4K),
	__IO_DEV_DESC(U5500_GPIO1_BASE, SZ_4K),
	__IO_DEV_DESC(U5500_GPIO2_BASE, SZ_4K),
	__IO_DEV_DESC(U5500_GPIO3_BASE, SZ_4K),
	__IO_DEV_DESC(U5500_GPIO4_BASE, SZ_4K),
	__IO_DEV_DESC(U5500_PRCMU_BASE, SZ_4K),
};

static struct resource mbox0_resources[] = {
	{
		.name = "mbox_peer",
		.start = U5500_MBOX0_PEER_START,
		.end = U5500_MBOX0_PEER_END,
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "mbox_local",
		.start = U5500_MBOX0_LOCAL_START,
		.end = U5500_MBOX0_LOCAL_END,
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "mbox_irq",
		.start = MBOX_PAIR0_VIRT_IRQ,
		.end = MBOX_PAIR0_VIRT_IRQ,
		.flags = IORESOURCE_IRQ,
	}
};

static struct resource mbox1_resources[] = {
	{
		.name = "mbox_peer",
		.start = U5500_MBOX1_PEER_START,
		.end = U5500_MBOX1_PEER_END,
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "mbox_local",
		.start = U5500_MBOX1_LOCAL_START,
		.end = U5500_MBOX1_LOCAL_END,
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "mbox_irq",
		.start = MBOX_PAIR1_VIRT_IRQ,
		.end = MBOX_PAIR1_VIRT_IRQ,
		.flags = IORESOURCE_IRQ,
	}
};

static struct resource mbox2_resources[] = {
	{
		.name = "mbox_peer",
		.start = U5500_MBOX2_PEER_START,
		.end = U5500_MBOX2_PEER_END,
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "mbox_local",
		.start = U5500_MBOX2_LOCAL_START,
		.end = U5500_MBOX2_LOCAL_END,
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "mbox_irq",
		.start = MBOX_PAIR2_VIRT_IRQ,
		.end = MBOX_PAIR2_VIRT_IRQ,
		.flags = IORESOURCE_IRQ,
	}
};

static struct platform_device mbox0_device = {
	.id = 0,
	.name = "mbox",
	.resource = mbox0_resources,
	.num_resources = ARRAY_SIZE(mbox0_resources),
};

static struct platform_device mbox1_device = {
	.id = 1,
	.name = "mbox",
	.resource = mbox1_resources,
	.num_resources = ARRAY_SIZE(mbox1_resources),
};

static struct platform_device mbox2_device = {
	.id = 2,
	.name = "mbox",
	.resource = mbox2_resources,
	.num_resources = ARRAY_SIZE(mbox2_resources),
};

static struct platform_device *u5500_platform_devs[] __initdata = {
	&mbox0_device,
	&mbox1_device,
	&mbox2_device,
};

static resource_size_t __initdata db5500_gpio_base[] = {
	U5500_GPIOBANK0_BASE,
	U5500_GPIOBANK1_BASE,
	U5500_GPIOBANK2_BASE,
	U5500_GPIOBANK3_BASE,
	U5500_GPIOBANK4_BASE,
	U5500_GPIOBANK5_BASE,
	U5500_GPIOBANK6_BASE,
	U5500_GPIOBANK7_BASE,
};

static void __init db5500_add_gpios(void)
{
	struct nmk_gpio_platform_data pdata = {
		/* No custom data yet */
	};

	dbx500_add_gpios(ARRAY_AND_SIZE(db5500_gpio_base),
			 IRQ_DB5500_GPIO0, &pdata);
}

void __init u5500_map_io(void)
{
	/*
	 * Map the UARTs early so that the DEBUG_LL stuff continues to work.
	 */
	iotable_init(u5500_uart_io_desc, ARRAY_SIZE(u5500_uart_io_desc));

	ux500_map_io();

	iotable_init(u5500_io_desc, ARRAY_SIZE(u5500_io_desc));
}

void __init u5500_init_devices(void)
{
	db5500_add_gpios();
	db5500_dma_init();
	db5500_add_rtc();

	platform_add_devices(u5500_platform_devs,
			     ARRAY_SIZE(u5500_platform_devs));
}
