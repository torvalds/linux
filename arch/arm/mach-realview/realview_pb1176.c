/*
 *  linux/arch/arm/mach-realview/realview_pb1176.c
 *
 *  Copyright (C) 2008 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/sysdev.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl061.h>
#include <linux/amba/mmci.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/leds.h>
#include <asm/mach-types.h>
#include <asm/hardware/gic.h>
#include <asm/hardware/icst307.h>
#include <asm/hardware/cache-l2x0.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

#include <mach/board-pb1176.h>
#include <mach/irqs.h>

#include "core.h"
#include "clock.h"

static struct map_desc realview_pb1176_io_desc[] __initdata = {
	{
		.virtual	= IO_ADDRESS(REALVIEW_SYS_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_SYS_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_PB1176_GIC_CPU_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_PB1176_GIC_CPU_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_PB1176_GIC_DIST_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_PB1176_GIC_DIST_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_DC1176_GIC_CPU_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_DC1176_GIC_CPU_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_DC1176_GIC_DIST_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_DC1176_GIC_DIST_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_SCTL_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_SCTL_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_PB1176_TIMER0_1_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_PB1176_TIMER0_1_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_PB1176_TIMER2_3_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_PB1176_TIMER2_3_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_PB1176_L220_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_PB1176_L220_BASE),
		.length		= SZ_8K,
		.type		= MT_DEVICE,
	},
#ifdef CONFIG_DEBUG_LL
	{
		.virtual	= IO_ADDRESS(REALVIEW_PB1176_UART0_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_PB1176_UART0_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
#endif
};

static void __init realview_pb1176_map_io(void)
{
	iotable_init(realview_pb1176_io_desc, ARRAY_SIZE(realview_pb1176_io_desc));
}

static struct pl061_platform_data gpio0_plat_data = {
	.gpio_base	= 0,
	.irq_base	= -1,
};

static struct pl061_platform_data gpio1_plat_data = {
	.gpio_base	= 8,
	.irq_base	= -1,
};

static struct pl061_platform_data gpio2_plat_data = {
	.gpio_base	= 16,
	.irq_base	= -1,
};

/*
 * RealView PB1176 AMBA devices
 */
#define GPIO2_IRQ	{ IRQ_PB1176_GPIO2, NO_IRQ }
#define GPIO2_DMA	{ 0, 0 }
#define GPIO3_IRQ	{ IRQ_PB1176_GPIO3, NO_IRQ }
#define GPIO3_DMA	{ 0, 0 }
#define AACI_IRQ	{ IRQ_PB1176_AACI, NO_IRQ }
#define AACI_DMA	{ 0x80, 0x81 }
#define MMCI0_IRQ	{ IRQ_PB1176_MMCI0A, IRQ_PB1176_MMCI0B }
#define MMCI0_DMA	{ 0x84, 0 }
#define KMI0_IRQ	{ IRQ_PB1176_KMI0, NO_IRQ }
#define KMI0_DMA	{ 0, 0 }
#define KMI1_IRQ	{ IRQ_PB1176_KMI1, NO_IRQ }
#define KMI1_DMA	{ 0, 0 }
#define PB1176_SMC_IRQ	{ NO_IRQ, NO_IRQ }
#define PB1176_SMC_DMA	{ 0, 0 }
#define MPMC_IRQ	{ NO_IRQ, NO_IRQ }
#define MPMC_DMA	{ 0, 0 }
#define PB1176_CLCD_IRQ	{ IRQ_DC1176_CLCD, NO_IRQ }
#define PB1176_CLCD_DMA	{ 0, 0 }
#define DMAC_IRQ	{ IRQ_PB1176_DMAC, NO_IRQ }
#define DMAC_DMA	{ 0, 0 }
#define SCTL_IRQ	{ NO_IRQ, NO_IRQ }
#define SCTL_DMA	{ 0, 0 }
#define PB1176_WATCHDOG_IRQ	{ IRQ_DC1176_WATCHDOG, NO_IRQ }
#define PB1176_WATCHDOG_DMA	{ 0, 0 }
#define PB1176_GPIO0_IRQ	{ IRQ_PB1176_GPIO0, NO_IRQ }
#define PB1176_GPIO0_DMA	{ 0, 0 }
#define GPIO1_IRQ	{ IRQ_PB1176_GPIO1, NO_IRQ }
#define GPIO1_DMA	{ 0, 0 }
#define PB1176_RTC_IRQ	{ IRQ_DC1176_RTC, NO_IRQ }
#define PB1176_RTC_DMA	{ 0, 0 }
#define SCI_IRQ		{ IRQ_PB1176_SCI, NO_IRQ }
#define SCI_DMA		{ 7, 6 }
#define PB1176_UART0_IRQ	{ IRQ_DC1176_UART0, NO_IRQ }
#define PB1176_UART0_DMA	{ 15, 14 }
#define PB1176_UART1_IRQ	{ IRQ_DC1176_UART1, NO_IRQ }
#define PB1176_UART1_DMA	{ 13, 12 }
#define PB1176_UART2_IRQ	{ IRQ_DC1176_UART2, NO_IRQ }
#define PB1176_UART2_DMA	{ 11, 10 }
#define PB1176_UART3_IRQ	{ IRQ_DC1176_UART3, NO_IRQ }
#define PB1176_UART3_DMA	{ 0x86, 0x87 }
#define PB1176_SSP_IRQ		{ IRQ_PB1176_SSP, NO_IRQ }
#define PB1176_SSP_DMA		{ 9, 8 }

/* FPGA Primecells */
AMBA_DEVICE(aaci,	"fpga:aaci",	AACI,		NULL);
AMBA_DEVICE(mmc0,	"fpga:mmc0",	MMCI0,		&realview_mmc0_plat_data);
AMBA_DEVICE(kmi0,	"fpga:kmi0",	KMI0,		NULL);
AMBA_DEVICE(kmi1,	"fpga:kmi1",	KMI1,		NULL);
AMBA_DEVICE(uart3,	"fpga:uart3",	PB1176_UART3,	NULL);

/* DevChip Primecells */
AMBA_DEVICE(smc,	"dev:smc",	PB1176_SMC,	NULL);
AMBA_DEVICE(sctl,	"dev:sctl",	SCTL,		NULL);
AMBA_DEVICE(wdog,	"dev:wdog",	PB1176_WATCHDOG,	NULL);
AMBA_DEVICE(gpio0,	"dev:gpio0",	PB1176_GPIO0,	&gpio0_plat_data);
AMBA_DEVICE(gpio1,	"dev:gpio1",	GPIO1,		&gpio1_plat_data);
AMBA_DEVICE(gpio2,	"dev:gpio2",	GPIO2,		&gpio2_plat_data);
AMBA_DEVICE(rtc,	"dev:rtc",	PB1176_RTC,	NULL);
AMBA_DEVICE(sci0,	"dev:sci0",	SCI,		NULL);
AMBA_DEVICE(uart0,	"dev:uart0",	PB1176_UART0,	NULL);
AMBA_DEVICE(uart1,	"dev:uart1",	PB1176_UART1,	NULL);
AMBA_DEVICE(uart2,	"dev:uart2",	PB1176_UART2,	NULL);
AMBA_DEVICE(ssp0,	"dev:ssp0",	PB1176_SSP,	NULL);

/* Primecells on the NEC ISSP chip */
AMBA_DEVICE(clcd,	"issp:clcd",	PB1176_CLCD,	&clcd_plat_data);
//AMBA_DEVICE(dmac,	"issp:dmac",	PB1176_DMAC,	NULL);

static struct amba_device *amba_devs[] __initdata = {
//	&dmac_device,
	&uart0_device,
	&uart1_device,
	&uart2_device,
	&uart3_device,
	&smc_device,
	&clcd_device,
	&sctl_device,
	&wdog_device,
	&gpio0_device,
	&gpio1_device,
	&gpio2_device,
	&rtc_device,
	&sci0_device,
	&ssp0_device,
	&aaci_device,
	&mmc0_device,
	&kmi0_device,
	&kmi1_device,
};

/*
 * RealView PB1176 platform devices
 */
static struct resource realview_pb1176_flash_resources[] = {
	[0] = {
		.start		= REALVIEW_PB1176_FLASH_BASE,
		.end		= REALVIEW_PB1176_FLASH_BASE + REALVIEW_PB1176_FLASH_SIZE - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= REALVIEW_PB1176_SEC_FLASH_BASE,
		.end		= REALVIEW_PB1176_SEC_FLASH_BASE + REALVIEW_PB1176_SEC_FLASH_SIZE - 1,
		.flags		= IORESOURCE_MEM,
	},
};
#ifdef CONFIG_REALVIEW_PB1176_SECURE_FLASH
#define PB1176_FLASH_BLOCKS	2
#else
#define PB1176_FLASH_BLOCKS	1
#endif

static struct resource realview_pb1176_smsc911x_resources[] = {
	[0] = {
		.start		= REALVIEW_PB1176_ETH_BASE,
		.end		= REALVIEW_PB1176_ETH_BASE + SZ_64K - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IRQ_PB1176_ETH,
		.end		= IRQ_PB1176_ETH,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct resource realview_pb1176_isp1761_resources[] = {
	[0] = {
		.start		= REALVIEW_PB1176_USB_BASE,
		.end		= REALVIEW_PB1176_USB_BASE + SZ_128K - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IRQ_PB1176_USB,
		.end		= IRQ_PB1176_USB,
		.flags		= IORESOURCE_IRQ,
	},
};

static void __init gic_init_irq(void)
{
	/* ARM1176 DevChip GIC, primary */
	gic_cpu_base_addr = __io_address(REALVIEW_DC1176_GIC_CPU_BASE);
	gic_dist_init(0, __io_address(REALVIEW_DC1176_GIC_DIST_BASE), IRQ_DC1176_GIC_START);
	gic_cpu_init(0, gic_cpu_base_addr);

	/* board GIC, secondary */
	gic_dist_init(1, __io_address(REALVIEW_PB1176_GIC_DIST_BASE), IRQ_PB1176_GIC_START);
	gic_cpu_init(1, __io_address(REALVIEW_PB1176_GIC_CPU_BASE));
	gic_cascade_irq(1, IRQ_DC1176_PB_IRQ1);
}

static void __init realview_pb1176_timer_init(void)
{
	timer0_va_base = __io_address(REALVIEW_PB1176_TIMER0_1_BASE);
	timer1_va_base = __io_address(REALVIEW_PB1176_TIMER0_1_BASE) + 0x20;
	timer2_va_base = __io_address(REALVIEW_PB1176_TIMER2_3_BASE);
	timer3_va_base = __io_address(REALVIEW_PB1176_TIMER2_3_BASE) + 0x20;

	realview_timer_init(IRQ_DC1176_TIMER0);
}

static struct sys_timer realview_pb1176_timer = {
	.init		= realview_pb1176_timer_init,
};

static void realview_pb1176_reset(char mode)
{
	void __iomem *hdr_ctrl = __io_address(REALVIEW_SYS_BASE) +
		REALVIEW_SYS_RESETCTL_OFFSET;
	void __iomem *rst_hdr_ctrl = __io_address(REALVIEW_SYS_BASE) +
		REALVIEW_SYS_LOCK_OFFSET;
	__raw_writel(REALVIEW_SYS_LOCKVAL_MASK, rst_hdr_ctrl);
	__raw_writel(REALVIEW_PB1176_SYS_LOCKVAL_RSTCTL, hdr_ctrl);
}

static void __init realview_pb1176_init(void)
{
	int i;

#ifdef CONFIG_CACHE_L2X0
	/* 128Kb (16Kb/way) 8-way associativity. evmon/parity/share enabled. */
	l2x0_init(__io_address(REALVIEW_PB1176_L220_BASE), 0x00730000, 0xfe000fff);
#endif

	realview_flash_register(realview_pb1176_flash_resources,
				PB1176_FLASH_BLOCKS);
	realview_eth_register(NULL, realview_pb1176_smsc911x_resources);
	platform_device_register(&realview_i2c_device);
	realview_usb_register(realview_pb1176_isp1761_resources);

	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		struct amba_device *d = amba_devs[i];
		amba_device_register(d, &iomem_resource);
	}

#ifdef CONFIG_LEDS
	leds_event = realview_leds_event;
#endif
	realview_reset = realview_pb1176_reset;
}

MACHINE_START(REALVIEW_PB1176, "ARM-RealView PB1176")
	/* Maintainer: ARM Ltd/Deep Blue Solutions Ltd */
	.phys_io	= REALVIEW_PB1176_UART0_BASE,
	.io_pg_offst	= (IO_ADDRESS(REALVIEW_PB1176_UART0_BASE) >> 18) & 0xfffc,
	.boot_params	= PHYS_OFFSET + 0x00000100,
	.map_io		= realview_pb1176_map_io,
	.init_irq	= gic_init_irq,
	.timer		= &realview_pb1176_timer,
	.init_machine	= realview_pb1176_init,
MACHINE_END
