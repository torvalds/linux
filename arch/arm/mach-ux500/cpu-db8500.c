/*
 * Copyright (C) 2008-2009 ST-Ericsson
 *
 * Author: Srinidhi KASAGAR <srinidhi.kasagar@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <asm/mach/map.h>
#include <asm/pmu.h>
#include <mach/hardware.h>
#include <mach/setup.h>
#include <mach/devices.h>
#include <mach/usb.h>

#include "devices-db8500.h"
#include "ste-dma40-db8500.h"

/* minimum static i/o mapping required to boot U8500 platforms */
static struct map_desc u8500_uart_io_desc[] __initdata = {
	__IO_DEV_DESC(U8500_UART0_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_UART2_BASE, SZ_4K),
};

static struct map_desc u8500_io_desc[] __initdata = {
	__IO_DEV_DESC(U8500_GIC_CPU_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_GIC_DIST_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_L2CC_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_TWD_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_MTU0_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_SCU_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_BACKUPRAM0_BASE, SZ_8K),

	__IO_DEV_DESC(U8500_CLKRST1_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_CLKRST2_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_CLKRST3_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_CLKRST5_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_CLKRST6_BASE, SZ_4K),

	__IO_DEV_DESC(U8500_PRCMU_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_GPIO0_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_GPIO1_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_GPIO2_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_GPIO3_BASE, SZ_4K),
};

static struct map_desc u8500_ed_io_desc[] __initdata = {
	__IO_DEV_DESC(U8500_MTU0_BASE_ED, SZ_4K),
	__IO_DEV_DESC(U8500_CLKRST7_BASE_ED, SZ_8K),
};

static struct map_desc u8500_v1_io_desc[] __initdata = {
	__IO_DEV_DESC(U8500_MTU0_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_PRCMU_TCDM_BASE_V1, SZ_4K),
};

static struct map_desc u8500_v2_io_desc[] __initdata = {
	__IO_DEV_DESC(U8500_PRCMU_TCDM_BASE, SZ_4K),
};

void __init u8500_map_io(void)
{
	/*
	 * Map the UARTs early so that the DEBUG_LL stuff continues to work.
	 */
	iotable_init(u8500_uart_io_desc, ARRAY_SIZE(u8500_uart_io_desc));

	ux500_map_io();

	iotable_init(u8500_io_desc, ARRAY_SIZE(u8500_io_desc));

	if (cpu_is_u8500ed())
		iotable_init(u8500_ed_io_desc, ARRAY_SIZE(u8500_ed_io_desc));
	else if (cpu_is_u8500v1())
		iotable_init(u8500_v1_io_desc, ARRAY_SIZE(u8500_v1_io_desc));
	else if (cpu_is_u8500v2())
		iotable_init(u8500_v2_io_desc, ARRAY_SIZE(u8500_v2_io_desc));

	_PRCMU_BASE = __io_address(U8500_PRCMU_BASE);
}

static struct resource db8500_pmu_resources[] = {
	[0] = {
		.start		= IRQ_DB8500_PMU,
		.end		= IRQ_DB8500_PMU,
		.flags		= IORESOURCE_IRQ,
	},
};

/*
 * The PMU IRQ lines of two cores are wired together into a single interrupt.
 * Bounce the interrupt to the other core if it's not ours.
 */
static irqreturn_t db8500_pmu_handler(int irq, void *dev, irq_handler_t handler)
{
	irqreturn_t ret = handler(irq, dev);
	int other = !smp_processor_id();

	if (ret == IRQ_NONE && cpu_online(other))
		irq_set_affinity(irq, cpumask_of(other));

	/*
	 * We should be able to get away with the amount of IRQ_NONEs we give,
	 * while still having the spurious IRQ detection code kick in if the
	 * interrupt really starts hitting spuriously.
	 */
	return ret;
}

static struct arm_pmu_platdata db8500_pmu_platdata = {
	.handle_irq		= db8500_pmu_handler,
};

static struct platform_device db8500_pmu_device = {
	.name			= "arm-pmu",
	.id			= ARM_PMU_DEVICE_CPU,
	.num_resources		= ARRAY_SIZE(db8500_pmu_resources),
	.resource		= db8500_pmu_resources,
	.dev.platform_data	= &db8500_pmu_platdata,
};

static struct platform_device db8500_prcmu_device = {
	.name			= "db8500-prcmu",
};

static struct platform_device *platform_devs[] __initdata = {
	&u8500_dma40_device,
	&db8500_pmu_device,
	&db8500_prcmu_device,
};

static resource_size_t __initdata db8500_gpio_base[] = {
	U8500_GPIOBANK0_BASE,
	U8500_GPIOBANK1_BASE,
	U8500_GPIOBANK2_BASE,
	U8500_GPIOBANK3_BASE,
	U8500_GPIOBANK4_BASE,
	U8500_GPIOBANK5_BASE,
	U8500_GPIOBANK6_BASE,
	U8500_GPIOBANK7_BASE,
	U8500_GPIOBANK8_BASE,
};

static void __init db8500_add_gpios(void)
{
	struct nmk_gpio_platform_data pdata = {
		/* No custom data yet */
	};

	if (cpu_is_u8500v2())
		pdata.supports_sleepmode = true;

	dbx500_add_gpios(ARRAY_AND_SIZE(db8500_gpio_base),
			 IRQ_DB8500_GPIO0, &pdata);
}

static int usb_db8500_rx_dma_cfg[] = {
	DB8500_DMA_DEV38_USB_OTG_IEP_1_9,
	DB8500_DMA_DEV37_USB_OTG_IEP_2_10,
	DB8500_DMA_DEV36_USB_OTG_IEP_3_11,
	DB8500_DMA_DEV19_USB_OTG_IEP_4_12,
	DB8500_DMA_DEV18_USB_OTG_IEP_5_13,
	DB8500_DMA_DEV17_USB_OTG_IEP_6_14,
	DB8500_DMA_DEV16_USB_OTG_IEP_7_15,
	DB8500_DMA_DEV39_USB_OTG_IEP_8
};

static int usb_db8500_tx_dma_cfg[] = {
	DB8500_DMA_DEV38_USB_OTG_OEP_1_9,
	DB8500_DMA_DEV37_USB_OTG_OEP_2_10,
	DB8500_DMA_DEV36_USB_OTG_OEP_3_11,
	DB8500_DMA_DEV19_USB_OTG_OEP_4_12,
	DB8500_DMA_DEV18_USB_OTG_OEP_5_13,
	DB8500_DMA_DEV17_USB_OTG_OEP_6_14,
	DB8500_DMA_DEV16_USB_OTG_OEP_7_15,
	DB8500_DMA_DEV39_USB_OTG_OEP_8
};

/*
 * This function is called from the board init
 */
void __init u8500_init_devices(void)
{
	if (cpu_is_u8500ed())
		dma40_u8500ed_fixup();

	db8500_add_rtc();
	db8500_add_gpios();
	db8500_add_usb(usb_db8500_rx_dma_cfg, usb_db8500_tx_dma_cfg);

	platform_device_register_simple("cpufreq-u8500", -1, NULL, 0);
	platform_add_devices(platform_devs, ARRAY_SIZE(platform_devs));

	return ;
}
