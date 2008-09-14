/*
 *  linux/arch/arm/mach-realview/realview_eb.c
 *
 *  Copyright (C) 2004 ARM Limited
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

#include <mach/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/leds.h>
#include <asm/mach-types.h>
#include <asm/hardware/gic.h>
#include <asm/hardware/icst307.h>
#include <asm/hardware/cache-l2x0.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/mmc.h>
#include <asm/mach/time.h>

#include <mach/board-eb.h>
#include <mach/irqs.h>

#include "core.h"
#include "clock.h"

static struct map_desc realview_eb_io_desc[] __initdata = {
	{
		.virtual	= IO_ADDRESS(REALVIEW_SYS_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_SYS_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_EB_GIC_CPU_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_EB_GIC_CPU_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_EB_GIC_DIST_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_EB_GIC_DIST_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_SCTL_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_SCTL_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_EB_TIMER0_1_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_EB_TIMER0_1_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_EB_TIMER2_3_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_EB_TIMER2_3_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
#ifdef CONFIG_DEBUG_LL
	{
		.virtual	= IO_ADDRESS(REALVIEW_EB_UART0_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_EB_UART0_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}
#endif
};

static struct map_desc realview_eb11mp_io_desc[] __initdata = {
	{
		.virtual	= IO_ADDRESS(REALVIEW_EB11MP_GIC_CPU_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_EB11MP_GIC_CPU_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_EB11MP_GIC_DIST_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_EB11MP_GIC_DIST_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_EB11MP_L220_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_EB11MP_L220_BASE),
		.length		= SZ_8K,
		.type		= MT_DEVICE,
	}
};

static void __init realview_eb_map_io(void)
{
	iotable_init(realview_eb_io_desc, ARRAY_SIZE(realview_eb_io_desc));
	if (core_tile_eb11mp())
		iotable_init(realview_eb11mp_io_desc, ARRAY_SIZE(realview_eb11mp_io_desc));
}

/*
 * RealView EB AMBA devices
 */

/*
 * These devices are connected via the core APB bridge
 */
#define GPIO2_IRQ	{ IRQ_EB_GPIO2, NO_IRQ }
#define GPIO2_DMA	{ 0, 0 }
#define GPIO3_IRQ	{ IRQ_EB_GPIO3, NO_IRQ }
#define GPIO3_DMA	{ 0, 0 }

#define AACI_IRQ	{ IRQ_EB_AACI, NO_IRQ }
#define AACI_DMA	{ 0x80, 0x81 }
#define MMCI0_IRQ	{ IRQ_EB_MMCI0A, IRQ_EB_MMCI0B }
#define MMCI0_DMA	{ 0x84, 0 }
#define KMI0_IRQ	{ IRQ_EB_KMI0, NO_IRQ }
#define KMI0_DMA	{ 0, 0 }
#define KMI1_IRQ	{ IRQ_EB_KMI1, NO_IRQ }
#define KMI1_DMA	{ 0, 0 }

/*
 * These devices are connected directly to the multi-layer AHB switch
 */
#define EB_SMC_IRQ	{ NO_IRQ, NO_IRQ }
#define EB_SMC_DMA	{ 0, 0 }
#define MPMC_IRQ	{ NO_IRQ, NO_IRQ }
#define MPMC_DMA	{ 0, 0 }
#define EB_CLCD_IRQ	{ IRQ_EB_CLCD, NO_IRQ }
#define EB_CLCD_DMA	{ 0, 0 }
#define DMAC_IRQ	{ IRQ_EB_DMA, NO_IRQ }
#define DMAC_DMA	{ 0, 0 }

/*
 * These devices are connected via the core APB bridge
 */
#define SCTL_IRQ	{ NO_IRQ, NO_IRQ }
#define SCTL_DMA	{ 0, 0 }
#define EB_WATCHDOG_IRQ	{ IRQ_EB_WDOG, NO_IRQ }
#define EB_WATCHDOG_DMA	{ 0, 0 }
#define EB_GPIO0_IRQ	{ IRQ_EB_GPIO0, NO_IRQ }
#define EB_GPIO0_DMA	{ 0, 0 }
#define GPIO1_IRQ	{ IRQ_EB_GPIO1, NO_IRQ }
#define GPIO1_DMA	{ 0, 0 }
#define EB_RTC_IRQ	{ IRQ_EB_RTC, NO_IRQ }
#define EB_RTC_DMA	{ 0, 0 }

/*
 * These devices are connected via the DMA APB bridge
 */
#define SCI_IRQ		{ IRQ_EB_SCI, NO_IRQ }
#define SCI_DMA		{ 7, 6 }
#define EB_UART0_IRQ	{ IRQ_EB_UART0, NO_IRQ }
#define EB_UART0_DMA	{ 15, 14 }
#define EB_UART1_IRQ	{ IRQ_EB_UART1, NO_IRQ }
#define EB_UART1_DMA	{ 13, 12 }
#define EB_UART2_IRQ	{ IRQ_EB_UART2, NO_IRQ }
#define EB_UART2_DMA	{ 11, 10 }
#define EB_UART3_IRQ	{ IRQ_EB_UART3, NO_IRQ }
#define EB_UART3_DMA	{ 0x86, 0x87 }
#define EB_SSP_IRQ	{ IRQ_EB_SSP, NO_IRQ }
#define EB_SSP_DMA	{ 9, 8 }

/* FPGA Primecells */
AMBA_DEVICE(aaci,  "fpga:04", AACI,     NULL);
AMBA_DEVICE(mmc0,  "fpga:05", MMCI0,    &realview_mmc0_plat_data);
AMBA_DEVICE(kmi0,  "fpga:06", KMI0,     NULL);
AMBA_DEVICE(kmi1,  "fpga:07", KMI1,     NULL);
AMBA_DEVICE(uart3, "fpga:09", EB_UART3, NULL);

/* DevChip Primecells */
AMBA_DEVICE(smc,   "dev:00",  EB_SMC,   NULL);
AMBA_DEVICE(clcd,  "dev:20",  EB_CLCD,  &clcd_plat_data);
AMBA_DEVICE(dmac,  "dev:30",  DMAC,     NULL);
AMBA_DEVICE(sctl,  "dev:e0",  SCTL,     NULL);
AMBA_DEVICE(wdog,  "dev:e1",  EB_WATCHDOG, NULL);
AMBA_DEVICE(gpio0, "dev:e4",  EB_GPIO0, NULL);
AMBA_DEVICE(gpio1, "dev:e5",  GPIO1,    NULL);
AMBA_DEVICE(gpio2, "dev:e6",  GPIO2,    NULL);
AMBA_DEVICE(rtc,   "dev:e8",  EB_RTC,   NULL);
AMBA_DEVICE(sci0,  "dev:f0",  SCI,      NULL);
AMBA_DEVICE(uart0, "dev:f1",  EB_UART0, NULL);
AMBA_DEVICE(uart1, "dev:f2",  EB_UART1, NULL);
AMBA_DEVICE(uart2, "dev:f3",  EB_UART2, NULL);
AMBA_DEVICE(ssp0,  "dev:f4",  EB_SSP,   NULL);

static struct amba_device *amba_devs[] __initdata = {
	&dmac_device,
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
 * RealView EB platform devices
 */
static struct resource realview_eb_flash_resource = {
	.start			= REALVIEW_EB_FLASH_BASE,
	.end			= REALVIEW_EB_FLASH_BASE + REALVIEW_EB_FLASH_SIZE - 1,
	.flags			= IORESOURCE_MEM,
};

static struct resource realview_eb_eth_resources[] = {
	[0] = {
		.start		= REALVIEW_EB_ETH_BASE,
		.end		= REALVIEW_EB_ETH_BASE + SZ_64K - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IRQ_EB_ETH,
		.end		= IRQ_EB_ETH,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device realview_eb_eth_device = {
	.id		= 0,
	.num_resources	= ARRAY_SIZE(realview_eb_eth_resources),
	.resource	= realview_eb_eth_resources,
};

/*
 * Detect and register the correct Ethernet device. RealView/EB rev D
 * platforms use the newer SMSC LAN9118 Ethernet chip
 */
static int eth_device_register(void)
{
	void __iomem *eth_addr = ioremap(REALVIEW_EB_ETH_BASE, SZ_4K);
	u32 idrev;

	if (!eth_addr)
		return -ENOMEM;

	idrev = readl(eth_addr + 0x50);
	if ((idrev & 0xFFFF0000) == 0x01180000)
		/* SMSC LAN9118 chip present */
		realview_eb_eth_device.name = "smc911x";
	else
		/* SMSC 91C111 chip present */
		realview_eb_eth_device.name = "smc91x";

	iounmap(eth_addr);
	return platform_device_register(&realview_eb_eth_device);
}

static void __init gic_init_irq(void)
{
	if (core_tile_eb11mp()) {
		unsigned int pldctrl;

		/* new irq mode */
		writel(0x0000a05f, __io_address(REALVIEW_SYS_LOCK));
		pldctrl = readl(__io_address(REALVIEW_SYS_BASE)	+ REALVIEW_EB11MP_SYS_PLD_CTRL1);
		pldctrl |= 0x00800000;
		writel(pldctrl, __io_address(REALVIEW_SYS_BASE) + REALVIEW_EB11MP_SYS_PLD_CTRL1);
		writel(0x00000000, __io_address(REALVIEW_SYS_LOCK));

		/* core tile GIC, primary */
		gic_cpu_base_addr = __io_address(REALVIEW_EB11MP_GIC_CPU_BASE);
		gic_dist_init(0, __io_address(REALVIEW_EB11MP_GIC_DIST_BASE), 29);
		gic_cpu_init(0, gic_cpu_base_addr);

#ifndef CONFIG_REALVIEW_EB_ARM11MP_REVB
		/* board GIC, secondary */
		gic_dist_init(1, __io_address(REALVIEW_EB_GIC_DIST_BASE), 64);
		gic_cpu_init(1, __io_address(REALVIEW_EB_GIC_CPU_BASE));
		gic_cascade_irq(1, IRQ_EB11MP_EB_IRQ1);
#endif
	} else {
		/* board GIC, primary */
		gic_cpu_base_addr = __io_address(REALVIEW_EB_GIC_CPU_BASE);
		gic_dist_init(0, __io_address(REALVIEW_EB_GIC_DIST_BASE), 29);
		gic_cpu_init(0, gic_cpu_base_addr);
	}
}

/*
 * Fix up the IRQ numbers for the RealView EB/ARM11MPCore tile
 */
static void realview_eb11mp_fixup(void)
{
	/* AMBA devices */
	dmac_device.irq[0]	= IRQ_EB11MP_DMA;
	uart0_device.irq[0]	= IRQ_EB11MP_UART0;
	uart1_device.irq[0]	= IRQ_EB11MP_UART1;
	uart2_device.irq[0]	= IRQ_EB11MP_UART2;
	uart3_device.irq[0]	= IRQ_EB11MP_UART3;
	clcd_device.irq[0]	= IRQ_EB11MP_CLCD;
	wdog_device.irq[0]	= IRQ_EB11MP_WDOG;
	gpio0_device.irq[0]	= IRQ_EB11MP_GPIO0;
	gpio1_device.irq[0]	= IRQ_EB11MP_GPIO1;
	gpio2_device.irq[0]	= IRQ_EB11MP_GPIO2;
	rtc_device.irq[0]	= IRQ_EB11MP_RTC;
	sci0_device.irq[0]	= IRQ_EB11MP_SCI;
	ssp0_device.irq[0]	= IRQ_EB11MP_SSP;
	aaci_device.irq[0]	= IRQ_EB11MP_AACI;
	mmc0_device.irq[0]	= IRQ_EB11MP_MMCI0A;
	mmc0_device.irq[1]	= IRQ_EB11MP_MMCI0B;
	kmi0_device.irq[0]	= IRQ_EB11MP_KMI0;
	kmi1_device.irq[0]	= IRQ_EB11MP_KMI1;

	/* platform devices */
	realview_eb_eth_resources[1].start	= IRQ_EB11MP_ETH;
	realview_eb_eth_resources[1].end	= IRQ_EB11MP_ETH;
}

static void __init realview_eb_timer_init(void)
{
	unsigned int timer_irq;

	timer0_va_base = __io_address(REALVIEW_EB_TIMER0_1_BASE);
	timer1_va_base = __io_address(REALVIEW_EB_TIMER0_1_BASE) + 0x20;
	timer2_va_base = __io_address(REALVIEW_EB_TIMER2_3_BASE);
	timer3_va_base = __io_address(REALVIEW_EB_TIMER2_3_BASE) + 0x20;

	if (core_tile_eb11mp()) {
#ifdef CONFIG_LOCAL_TIMERS
		twd_base_addr = __io_address(REALVIEW_EB11MP_TWD_BASE);
		twd_size = REALVIEW_EB11MP_TWD_SIZE;
#endif
		timer_irq = IRQ_EB11MP_TIMER0_1;
	} else
		timer_irq = IRQ_EB_TIMER0_1;

	realview_timer_init(timer_irq);
}

static struct sys_timer realview_eb_timer = {
	.init		= realview_eb_timer_init,
};

static void __init realview_eb_init(void)
{
	int i;

	if (core_tile_eb11mp()) {
		realview_eb11mp_fixup();

#ifdef CONFIG_CACHE_L2X0
		/* 1MB (128KB/way), 8-way associativity, evmon/parity/share enabled
		 * Bits:  .... ...0 0111 1001 0000 .... .... .... */
		l2x0_init(__io_address(REALVIEW_EB11MP_L220_BASE), 0x00790000, 0xfe000fff);
#endif
	}

	clk_register(&realview_clcd_clk);

	realview_flash_register(&realview_eb_flash_resource, 1);
	platform_device_register(&realview_i2c_device);
	eth_device_register();

	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		struct amba_device *d = amba_devs[i];
		amba_device_register(d, &iomem_resource);
	}

#ifdef CONFIG_LEDS
	leds_event = realview_leds_event;
#endif
}

MACHINE_START(REALVIEW_EB, "ARM-RealView EB")
	/* Maintainer: ARM Ltd/Deep Blue Solutions Ltd */
	.phys_io	= REALVIEW_EB_UART0_BASE,
	.io_pg_offst	= (IO_ADDRESS(REALVIEW_EB_UART0_BASE) >> 18) & 0xfffc,
	.boot_params	= 0x00000100,
	.map_io		= realview_eb_map_io,
	.init_irq	= gic_init_irq,
	.timer		= &realview_eb_timer,
	.init_machine	= realview_eb_init,
MACHINE_END
