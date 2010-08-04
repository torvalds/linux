/*
 *  arch/arm/mach-realview/realview_pbx.c
 *
 *  Copyright (C) 2009 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#include <asm/irq.h>
#include <asm/leds.h>
#include <asm/mach-types.h>
#include <asm/pmu.h>
#include <asm/smp_twd.h>
#include <asm/pgtable.h>
#include <asm/hardware/gic.h>
#include <asm/hardware/cache-l2x0.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

#include <mach/hardware.h>
#include <mach/board-pbx.h>
#include <mach/irqs.h>

#include "core.h"

static struct map_desc realview_pbx_io_desc[] __initdata = {
	{
		.virtual	= IO_ADDRESS(REALVIEW_SYS_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_SYS_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_PBX_GIC_CPU_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_PBX_GIC_CPU_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_PBX_GIC_DIST_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_PBX_GIC_DIST_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_SCTL_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_SCTL_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_PBX_TIMER0_1_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_PBX_TIMER0_1_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_PBX_TIMER2_3_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_PBX_TIMER2_3_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
#ifdef CONFIG_PCI
	{
		.virtual	= PCIX_UNIT_BASE,
		.pfn		= __phys_to_pfn(REALVIEW_PBX_PCI_BASE),
		.length		= REALVIEW_PBX_PCI_BASE_SIZE,
		.type		= MT_DEVICE,
	},
#endif
#ifdef CONFIG_DEBUG_LL
	{
		.virtual	= IO_ADDRESS(REALVIEW_PBX_UART0_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_PBX_UART0_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
#endif
};

static struct map_desc realview_local_io_desc[] __initdata = {
	{
		.virtual        = IO_ADDRESS(REALVIEW_PBX_TILE_GIC_CPU_BASE),
		.pfn            = __phys_to_pfn(REALVIEW_PBX_TILE_GIC_CPU_BASE),
		.length         = SZ_4K,
		.type           = MT_DEVICE,
	}, {
		.virtual        = IO_ADDRESS(REALVIEW_PBX_TILE_GIC_DIST_BASE),
		.pfn            = __phys_to_pfn(REALVIEW_PBX_TILE_GIC_DIST_BASE),
		.length         = SZ_4K,
		.type           = MT_DEVICE,
	}, {
		.virtual        = IO_ADDRESS(REALVIEW_PBX_TILE_L220_BASE),
		.pfn            = __phys_to_pfn(REALVIEW_PBX_TILE_L220_BASE),
		.length         = SZ_8K,
		.type           = MT_DEVICE,
	}
};

static void __init realview_pbx_map_io(void)
{
	iotable_init(realview_pbx_io_desc, ARRAY_SIZE(realview_pbx_io_desc));
	if (core_tile_pbx11mp() || core_tile_pbxa9mp())
		iotable_init(realview_local_io_desc, ARRAY_SIZE(realview_local_io_desc));
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
 * RealView PBXCore AMBA devices
 */

#define GPIO2_IRQ		{ IRQ_PBX_GPIO2, NO_IRQ }
#define GPIO2_DMA		{ 0, 0 }
#define GPIO3_IRQ		{ IRQ_PBX_GPIO3, NO_IRQ }
#define GPIO3_DMA		{ 0, 0 }
#define AACI_IRQ		{ IRQ_PBX_AACI, NO_IRQ }
#define AACI_DMA		{ 0x80, 0x81 }
#define MMCI0_IRQ		{ IRQ_PBX_MMCI0A, IRQ_PBX_MMCI0B }
#define MMCI0_DMA		{ 0x84, 0 }
#define KMI0_IRQ		{ IRQ_PBX_KMI0, NO_IRQ }
#define KMI0_DMA		{ 0, 0 }
#define KMI1_IRQ		{ IRQ_PBX_KMI1, NO_IRQ }
#define KMI1_DMA		{ 0, 0 }
#define PBX_SMC_IRQ		{ NO_IRQ, NO_IRQ }
#define PBX_SMC_DMA		{ 0, 0 }
#define MPMC_IRQ		{ NO_IRQ, NO_IRQ }
#define MPMC_DMA		{ 0, 0 }
#define PBX_CLCD_IRQ		{ IRQ_PBX_CLCD, NO_IRQ }
#define PBX_CLCD_DMA		{ 0, 0 }
#define DMAC_IRQ		{ IRQ_PBX_DMAC, NO_IRQ }
#define DMAC_DMA		{ 0, 0 }
#define SCTL_IRQ		{ NO_IRQ, NO_IRQ }
#define SCTL_DMA		{ 0, 0 }
#define PBX_WATCHDOG_IRQ	{ IRQ_PBX_WATCHDOG, NO_IRQ }
#define PBX_WATCHDOG_DMA	{ 0, 0 }
#define PBX_GPIO0_IRQ		{ IRQ_PBX_GPIO0, NO_IRQ }
#define PBX_GPIO0_DMA		{ 0, 0 }
#define GPIO1_IRQ		{ IRQ_PBX_GPIO1, NO_IRQ }
#define GPIO1_DMA		{ 0, 0 }
#define PBX_RTC_IRQ		{ IRQ_PBX_RTC, NO_IRQ }
#define PBX_RTC_DMA		{ 0, 0 }
#define SCI_IRQ			{ IRQ_PBX_SCI, NO_IRQ }
#define SCI_DMA			{ 7, 6 }
#define PBX_UART0_IRQ		{ IRQ_PBX_UART0, NO_IRQ }
#define PBX_UART0_DMA		{ 15, 14 }
#define PBX_UART1_IRQ		{ IRQ_PBX_UART1, NO_IRQ }
#define PBX_UART1_DMA		{ 13, 12 }
#define PBX_UART2_IRQ		{ IRQ_PBX_UART2, NO_IRQ }
#define PBX_UART2_DMA		{ 11, 10 }
#define PBX_UART3_IRQ		{ IRQ_PBX_UART3, NO_IRQ }
#define PBX_UART3_DMA		{ 0x86, 0x87 }
#define PBX_SSP_IRQ		{ IRQ_PBX_SSP, NO_IRQ }
#define PBX_SSP_DMA		{ 9, 8 }

/* FPGA Primecells */
AMBA_DEVICE(aaci,	"fpga:aaci",	AACI,		NULL);
AMBA_DEVICE(mmc0,	"fpga:mmc0",	MMCI0,		&realview_mmc0_plat_data);
AMBA_DEVICE(kmi0,	"fpga:kmi0",	KMI0,		NULL);
AMBA_DEVICE(kmi1,	"fpga:kmi1",	KMI1,		NULL);
AMBA_DEVICE(uart3,	"fpga:uart3",	PBX_UART3,	NULL);

/* DevChip Primecells */
AMBA_DEVICE(smc,	"dev:smc",	PBX_SMC,	NULL);
AMBA_DEVICE(sctl,	"dev:sctl",	SCTL,		NULL);
AMBA_DEVICE(wdog,	"dev:wdog",	PBX_WATCHDOG, 	NULL);
AMBA_DEVICE(gpio0,	"dev:gpio0",	PBX_GPIO0,	&gpio0_plat_data);
AMBA_DEVICE(gpio1,	"dev:gpio1",	GPIO1,		&gpio1_plat_data);
AMBA_DEVICE(gpio2,	"dev:gpio2",	GPIO2,		&gpio2_plat_data);
AMBA_DEVICE(rtc,	"dev:rtc",	PBX_RTC,	NULL);
AMBA_DEVICE(sci0,	"dev:sci0",	SCI,		NULL);
AMBA_DEVICE(uart0,	"dev:uart0",	PBX_UART0,	NULL);
AMBA_DEVICE(uart1,	"dev:uart1",	PBX_UART1,	NULL);
AMBA_DEVICE(uart2,	"dev:uart2",	PBX_UART2,	NULL);
AMBA_DEVICE(ssp0,	"dev:ssp0",	PBX_SSP,	NULL);

/* Primecells on the NEC ISSP chip */
AMBA_DEVICE(clcd,	"issp:clcd",	PBX_CLCD,	&clcd_plat_data);
AMBA_DEVICE(dmac,	"issp:dmac",	DMAC,		NULL);

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
 * RealView PB-X platform devices
 */
static struct resource realview_pbx_flash_resources[] = {
	[0] = {
		.start          = REALVIEW_PBX_FLASH0_BASE,
		.end            = REALVIEW_PBX_FLASH0_BASE + REALVIEW_PBX_FLASH0_SIZE - 1,
		.flags          = IORESOURCE_MEM,
	},
	[1] = {
		.start          = REALVIEW_PBX_FLASH1_BASE,
		.end            = REALVIEW_PBX_FLASH1_BASE + REALVIEW_PBX_FLASH1_SIZE - 1,
		.flags          = IORESOURCE_MEM,
	},
};

static struct resource realview_pbx_smsc911x_resources[] = {
	[0] = {
		.start		= REALVIEW_PBX_ETH_BASE,
		.end		= REALVIEW_PBX_ETH_BASE + SZ_64K - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IRQ_PBX_ETH,
		.end		= IRQ_PBX_ETH,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct resource realview_pbx_isp1761_resources[] = {
	[0] = {
		.start		= REALVIEW_PBX_USB_BASE,
		.end		= REALVIEW_PBX_USB_BASE + SZ_128K - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IRQ_PBX_USB,
		.end		= IRQ_PBX_USB,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct resource pmu_resources[] = {
	[0] = {
		.start		= IRQ_PBX_PMU_CPU0,
		.end		= IRQ_PBX_PMU_CPU0,
		.flags		= IORESOURCE_IRQ,
	},
	[1] = {
		.start		= IRQ_PBX_PMU_CPU1,
		.end		= IRQ_PBX_PMU_CPU1,
		.flags		= IORESOURCE_IRQ,
	},
	[2] = {
		.start		= IRQ_PBX_PMU_CPU2,
		.end		= IRQ_PBX_PMU_CPU2,
		.flags		= IORESOURCE_IRQ,
	},
	[3] = {
		.start		= IRQ_PBX_PMU_CPU3,
		.end		= IRQ_PBX_PMU_CPU3,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device pmu_device = {
	.name			= "arm-pmu",
	.id			= ARM_PMU_DEVICE_CPU,
	.num_resources		= ARRAY_SIZE(pmu_resources),
	.resource		= pmu_resources,
};

static void __init gic_init_irq(void)
{
	/* ARM PBX on-board GIC */
	if (core_tile_pbx11mp() || core_tile_pbxa9mp()) {
		gic_cpu_base_addr = __io_address(REALVIEW_PBX_TILE_GIC_CPU_BASE);
		gic_dist_init(0, __io_address(REALVIEW_PBX_TILE_GIC_DIST_BASE),
			      29);
		gic_cpu_init(0, __io_address(REALVIEW_PBX_TILE_GIC_CPU_BASE));
	} else {
		gic_cpu_base_addr = __io_address(REALVIEW_PBX_GIC_CPU_BASE);
		gic_dist_init(0, __io_address(REALVIEW_PBX_GIC_DIST_BASE),
			      IRQ_PBX_GIC_START);
		gic_cpu_init(0, __io_address(REALVIEW_PBX_GIC_CPU_BASE));
	}
}

static void __init realview_pbx_timer_init(void)
{
	timer0_va_base = __io_address(REALVIEW_PBX_TIMER0_1_BASE);
	timer1_va_base = __io_address(REALVIEW_PBX_TIMER0_1_BASE) + 0x20;
	timer2_va_base = __io_address(REALVIEW_PBX_TIMER2_3_BASE);
	timer3_va_base = __io_address(REALVIEW_PBX_TIMER2_3_BASE) + 0x20;

#ifdef CONFIG_LOCAL_TIMERS
	if (core_tile_pbx11mp() || core_tile_pbxa9mp())
		twd_base = __io_address(REALVIEW_PBX_TILE_TWD_BASE);
#endif
	realview_timer_init(IRQ_PBX_TIMER0_1);
}

static struct sys_timer realview_pbx_timer = {
	.init		= realview_pbx_timer_init,
};

static void realview_pbx_fixup(struct machine_desc *mdesc, struct tag *tags,
			       char **from, struct meminfo *meminfo)
{
#ifdef CONFIG_SPARSEMEM
	/*
	 * Memory configuration with SPARSEMEM enabled on RealView PBX (see
	 * asm/mach/memory.h for more information).
	 */
	meminfo->bank[0].start = 0;
	meminfo->bank[0].size = SZ_256M;
	meminfo->bank[1].start = 0x20000000;
	meminfo->bank[1].size = SZ_512M;
	meminfo->bank[2].start = 0x80000000;
	meminfo->bank[2].size = SZ_256M;
	meminfo->nr_banks = 3;
#else
	realview_fixup(mdesc, tags, from, meminfo);
#endif
}

static void realview_pbx_reset(char mode)
{
	void __iomem *reset_ctrl = __io_address(REALVIEW_SYS_RESETCTL);
	void __iomem *lock_ctrl = __io_address(REALVIEW_SYS_LOCK);

	/*
	 * To reset, we hit the on-board reset register
	 * in the system FPGA
	 */
	__raw_writel(REALVIEW_SYS_LOCK_VAL, lock_ctrl);
	__raw_writel(0x00F0, reset_ctrl);
	__raw_writel(0x00F4, reset_ctrl);
}

static void __init realview_pbx_init(void)
{
	int i;

#ifdef CONFIG_CACHE_L2X0
	if (core_tile_pbxa9mp()) {
		void __iomem *l2x0_base =
			__io_address(REALVIEW_PBX_TILE_L220_BASE);

		/* set RAM latencies to 1 cycle for eASIC */
		writel(0, l2x0_base + L2X0_TAG_LATENCY_CTRL);
		writel(0, l2x0_base + L2X0_DATA_LATENCY_CTRL);

		/* 16KB way size, 8-way associativity, parity disabled
		 * Bits:  .. 0 0 0 0 1 00 1 0 1 001 0 000 0 .... .... .... */
		l2x0_init(l2x0_base, 0x02520000, 0xc0000fff);
		platform_device_register(&pmu_device);
	}
#endif

	realview_flash_register(realview_pbx_flash_resources,
				ARRAY_SIZE(realview_pbx_flash_resources));
	realview_eth_register(NULL, realview_pbx_smsc911x_resources);
	platform_device_register(&realview_i2c_device);
	platform_device_register(&realview_cf_device);
	realview_usb_register(realview_pbx_isp1761_resources);

	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		struct amba_device *d = amba_devs[i];
		amba_device_register(d, &iomem_resource);
	}

#ifdef CONFIG_LEDS
	leds_event = realview_leds_event;
#endif
	realview_reset = realview_pbx_reset;
}

MACHINE_START(REALVIEW_PBX, "ARM-RealView PBX")
	/* Maintainer: ARM Ltd/Deep Blue Solutions Ltd */
	.phys_io	= REALVIEW_PBX_UART0_BASE & SECTION_MASK,
	.io_pg_offst	= (IO_ADDRESS(REALVIEW_PBX_UART0_BASE) >> 18) & 0xfffc,
	.boot_params	= PHYS_OFFSET + 0x00000100,
	.fixup		= realview_pbx_fixup,
	.map_io		= realview_pbx_map_io,
	.init_irq	= gic_init_irq,
	.timer		= &realview_pbx_timer,
	.init_machine	= realview_pbx_init,
MACHINE_END
