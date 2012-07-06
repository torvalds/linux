/*
 * Copyright (C) 2008-2009 ST-Ericsson SA
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
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/mfd/abx500/ab8500.h>

#include <asm/mach/map.h>
#include <asm/pmu.h>
#include <plat/gpio-nomadik.h>
#include <mach/hardware.h>
#include <mach/setup.h>
#include <mach/devices.h>
#include <mach/usb.h>
#include <mach/db8500-regs.h>

#include "devices-db8500.h"
#include "ste-dma40-db8500.h"

/* minimum static i/o mapping required to boot U8500 platforms */
static struct map_desc u8500_uart_io_desc[] __initdata = {
	__IO_DEV_DESC(U8500_UART0_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_UART2_BASE, SZ_4K),
};
/*  U8500 and U9540 common io_desc */
static struct map_desc u8500_common_io_desc[] __initdata = {
	/* SCU base also covers GIC CPU BASE and TWD with its 4K page */
	__IO_DEV_DESC(U8500_SCU_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_GIC_DIST_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_L2CC_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_MTU0_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_BACKUPRAM0_BASE, SZ_8K),

	__IO_DEV_DESC(U8500_CLKRST1_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_CLKRST2_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_CLKRST3_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_CLKRST5_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_CLKRST6_BASE, SZ_4K),

	__IO_DEV_DESC(U8500_GPIO0_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_GPIO1_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_GPIO2_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_GPIO3_BASE, SZ_4K),
};

/* U8500 IO map specific description */
static struct map_desc u8500_io_desc[] __initdata = {
	__IO_DEV_DESC(U8500_PRCMU_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_PRCMU_TCDM_BASE, SZ_4K),

};

/* U9540 IO map specific description */
static struct map_desc u9540_io_desc[] __initdata = {
	__IO_DEV_DESC(U8500_PRCMU_BASE, SZ_4K + SZ_8K),
	__IO_DEV_DESC(U8500_PRCMU_TCDM_BASE, SZ_4K + SZ_8K),
};

void __init u8500_map_io(void)
{
	/*
	 * Map the UARTs early so that the DEBUG_LL stuff continues to work.
	 */
	iotable_init(u8500_uart_io_desc, ARRAY_SIZE(u8500_uart_io_desc));

	ux500_map_io();

	iotable_init(u8500_common_io_desc, ARRAY_SIZE(u8500_common_io_desc));

	if (cpu_is_u9540())
		iotable_init(u9540_io_desc, ARRAY_SIZE(u9540_io_desc));
	else
		iotable_init(u8500_io_desc, ARRAY_SIZE(u8500_io_desc));

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

struct arm_pmu_platdata db8500_pmu_platdata = {
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

static struct platform_device *of_platform_devs[] __initdata = {
	&u8500_dma40_device,
	&db8500_pmu_device,
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

static void __init db8500_add_gpios(struct device *parent)
{
	struct nmk_gpio_platform_data pdata = {
		.supports_sleepmode = true,
	};

	dbx500_add_gpios(parent, ARRAY_AND_SIZE(db8500_gpio_base),
			 IRQ_DB8500_GPIO0, &pdata);
	dbx500_add_pinctrl(parent, "pinctrl-db8500");
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

static const char *db8500_read_soc_id(void)
{
	void __iomem *uid = __io_address(U8500_BB_UID_BASE);

	return kasprintf(GFP_KERNEL, "%08x%08x%08x%08x%08x",
			 readl((u32 *)uid+1),
			 readl((u32 *)uid+1), readl((u32 *)uid+2),
			 readl((u32 *)uid+3), readl((u32 *)uid+4));
}

static struct device * __init db8500_soc_device_init(void)
{
	const char *soc_id = db8500_read_soc_id();

	return ux500_soc_device_init(soc_id);
}

/*
 * This function is called from the board init
 */
struct device * __init u8500_init_devices(struct ab8500_platform_data *ab8500)
{
	struct device *parent;
	int i;

	parent = db8500_soc_device_init();

	db8500_add_rtc(parent);
	db8500_add_gpios(parent);
	db8500_add_usb(parent, usb_db8500_rx_dma_cfg, usb_db8500_tx_dma_cfg);

	platform_device_register_data(parent,
		"cpufreq-u8500", -1, NULL, 0);

	for (i = 0; i < ARRAY_SIZE(platform_devs); i++)
		platform_devs[i]->dev.parent = parent;

	db8500_prcmu_device.dev.platform_data = ab8500;

	platform_add_devices(platform_devs, ARRAY_SIZE(platform_devs));

	return parent;
}

/* TODO: Once all pieces are DT:ed, remove completely. */
struct device * __init u8500_of_init_devices(void)
{
	struct device *parent;
	int i;

	parent = db8500_soc_device_init();

	db8500_add_rtc(parent);
	db8500_add_usb(parent, usb_db8500_rx_dma_cfg, usb_db8500_tx_dma_cfg);

	platform_device_register_data(parent,
		"cpufreq-u8500", -1, NULL, 0);

	for (i = 0; i < ARRAY_SIZE(of_platform_devs); i++)
		of_platform_devs[i]->dev.parent = parent;

	/*
	 * Devices to be DT:ed:
	 *   u8500_dma40_device  = todo
	 *   db8500_pmu_device   = todo
	 *   db8500_prcmu_device = done
	 */
	platform_add_devices(of_platform_devs, ARRAY_SIZE(of_platform_devs));

	return parent;
}
