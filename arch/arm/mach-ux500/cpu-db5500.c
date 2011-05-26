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
#include <asm/pmu.h>

#include <plat/gpio.h>

#include <mach/hardware.h>
#include <mach/devices.h>
#include <mach/setup.h>
#include <mach/irqs.h>
#include <mach/usb.h>

#include "devices-db5500.h"
#include "ste-dma40-db5500.h"

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

static struct resource db5500_pmu_resources[] = {
	[0] = {
		.start		= IRQ_DB5500_PMU0,
		.end		= IRQ_DB5500_PMU0,
		.flags		= IORESOURCE_IRQ,
	},
	[1] = {
		.start		= IRQ_DB5500_PMU1,
		.end		= IRQ_DB5500_PMU1,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device db5500_pmu_device = {
	.name			= "arm-pmu",
	.id			= ARM_PMU_DEVICE_CPU,
	.num_resources		= ARRAY_SIZE(db5500_pmu_resources),
	.resource		= db5500_pmu_resources,
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

static struct platform_device *db5500_platform_devs[] __initdata = {
	&db5500_pmu_device,
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

	_PRCMU_BASE = __io_address(U5500_PRCMU_BASE);
}

static int usb_db5500_rx_dma_cfg[] = {
	DB5500_DMA_DEV4_USB_OTG_IEP_1_9,
	DB5500_DMA_DEV5_USB_OTG_IEP_2_10,
	DB5500_DMA_DEV6_USB_OTG_IEP_3_11,
	DB5500_DMA_DEV20_USB_OTG_IEP_4_12,
	DB5500_DMA_DEV21_USB_OTG_IEP_5_13,
	DB5500_DMA_DEV22_USB_OTG_IEP_6_14,
	DB5500_DMA_DEV23_USB_OTG_IEP_7_15,
	DB5500_DMA_DEV38_USB_OTG_IEP_8
};

static int usb_db5500_tx_dma_cfg[] = {
	DB5500_DMA_DEV4_USB_OTG_OEP_1_9,
	DB5500_DMA_DEV5_USB_OTG_OEP_2_10,
	DB5500_DMA_DEV6_USB_OTG_OEP_3_11,
	DB5500_DMA_DEV20_USB_OTG_OEP_4_12,
	DB5500_DMA_DEV21_USB_OTG_OEP_5_13,
	DB5500_DMA_DEV22_USB_OTG_OEP_6_14,
	DB5500_DMA_DEV23_USB_OTG_OEP_7_15,
	DB5500_DMA_DEV38_USB_OTG_OEP_8
};

void __init u5500_init_devices(void)
{
	db5500_add_gpios();
	db5500_dma_init();
	db5500_add_rtc();
	db5500_add_usb(usb_db5500_rx_dma_cfg, usb_db5500_tx_dma_cfg);

	platform_add_devices(db5500_platform_devs,
			     ARRAY_SIZE(db5500_platform_devs));
}
