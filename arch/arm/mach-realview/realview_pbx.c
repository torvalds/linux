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
#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl061.h>
#include <linux/amba/mmci.h>
#include <linux/amba/pl022.h>
#include <linux/io.h>

#include <asm/irq.h>
#include <asm/leds.h>
#include <asm/mach-types.h>
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
		.virtual        = IO_ADDRESS(REALVIEW_PBX_TILE_SCU_BASE),
		.pfn            = __phys_to_pfn(REALVIEW_PBX_TILE_SCU_BASE),
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
};

static struct pl061_platform_data gpio1_plat_data = {
	.gpio_base	= 8,
};

static struct pl061_platform_data gpio2_plat_data = {
	.gpio_base	= 16,
};

static struct pl022_ssp_controller ssp0_plat_data = {
	.bus_id = 0,
	.enable_dma = 0,
	.num_chipselect = 1,
};

/*
 * RealView PBXCore AMBA devices
 */

#define GPIO2_IRQ		{ IRQ_PBX_GPIO2 }
#define GPIO3_IRQ		{ IRQ_PBX_GPIO3 }
#define AACI_IRQ		{ IRQ_PBX_AACI }
#define MMCI0_IRQ		{ IRQ_PBX_MMCI0A, IRQ_PBX_MMCI0B }
#define KMI0_IRQ		{ IRQ_PBX_KMI0 }
#define KMI1_IRQ		{ IRQ_PBX_KMI1 }
#define PBX_SMC_IRQ		{ }
#define MPMC_IRQ		{ }
#define PBX_CLCD_IRQ		{ IRQ_PBX_CLCD }
#define DMAC_IRQ		{ IRQ_PBX_DMAC }
#define SCTL_IRQ		{ }
#define PBX_WATCHDOG_IRQ	{ IRQ_PBX_WATCHDOG }
#define PBX_GPIO0_IRQ		{ IRQ_PBX_GPIO0 }
#define GPIO1_IRQ		{ IRQ_PBX_GPIO1 }
#define PBX_RTC_IRQ		{ IRQ_PBX_RTC }
#define SCI_IRQ			{ IRQ_PBX_SCI }
#define PBX_UART0_IRQ		{ IRQ_PBX_UART0 }
#define PBX_UART1_IRQ		{ IRQ_PBX_UART1 }
#define PBX_UART2_IRQ		{ IRQ_PBX_UART2 }
#define PBX_UART3_IRQ		{ IRQ_PBX_UART3 }
#define PBX_SSP_IRQ		{ IRQ_PBX_SSP }

/* FPGA Primecells */
APB_DEVICE(aaci,	"fpga:aaci",	AACI,		NULL);
APB_DEVICE(mmc0,	"fpga:mmc0",	MMCI0,		&realview_mmc0_plat_data);
APB_DEVICE(kmi0,	"fpga:kmi0",	KMI0,		NULL);
APB_DEVICE(kmi1,	"fpga:kmi1",	KMI1,		NULL);
APB_DEVICE(uart3,	"fpga:uart3",	PBX_UART3,	NULL);

/* DevChip Primecells */
AHB_DEVICE(smc,	"dev:smc",	PBX_SMC,	NULL);
AHB_DEVICE(sctl,	"dev:sctl",	SCTL,		NULL);
APB_DEVICE(wdog,	"dev:wdog",	PBX_WATCHDOG, 	NULL);
APB_DEVICE(gpio0,	"dev:gpio0",	PBX_GPIO0,	&gpio0_plat_data);
APB_DEVICE(gpio1,	"dev:gpio1",	GPIO1,		&gpio1_plat_data);
APB_DEVICE(gpio2,	"dev:gpio2",	GPIO2,		&gpio2_plat_data);
APB_DEVICE(rtc,		"dev:rtc",	PBX_RTC,	NULL);
APB_DEVICE(sci0,	"dev:sci0",	SCI,		NULL);
APB_DEVICE(uart0,	"dev:uart0",	PBX_UART0,	NULL);
APB_DEVICE(uart1,	"dev:uart1",	PBX_UART1,	NULL);
APB_DEVICE(uart2,	"dev:uart2",	PBX_UART2,	NULL);
APB_DEVICE(ssp0,	"dev:ssp0",	PBX_SSP,	&ssp0_plat_data);

/* Primecells on the NEC ISSP chip */
AHB_DEVICE(clcd,	"issp:clcd",	PBX_CLCD,	&clcd_plat_data);
AHB_DEVICE(dmac,	"issp:dmac",	DMAC,		NULL);

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
	.id			= -1,
	.num_resources		= ARRAY_SIZE(pmu_resources),
	.resource		= pmu_resources,
};

static void __init gic_init_irq(void)
{
	/* ARM PBX on-board GIC */
	if (core_tile_pbx11mp() || core_tile_pbxa9mp()) {
		gic_init(0, 29, __io_address(REALVIEW_PBX_TILE_GIC_DIST_BASE),
			 __io_address(REALVIEW_PBX_TILE_GIC_CPU_BASE));
	} else {
		gic_init(0, IRQ_PBX_GIC_START,
			 __io_address(REALVIEW_PBX_GIC_DIST_BASE),
			 __io_address(REALVIEW_PBX_GIC_CPU_BASE));
	}
}

#ifdef CONFIG_HAVE_ARM_TWD
static DEFINE_TWD_LOCAL_TIMER(twd_local_timer,
			      REALVIEW_PBX_TILE_TWD_BASE,
			      IRQ_LOCALTIMER);

static void __init realview_pbx_twd_init(void)
{
	int err = twd_local_timer_register(&twd_local_timer);
	if (err)
		pr_err("twd_local_timer_register failed %d\n", err);
}
#else
#define realview_pbx_twd_init()	do { } while(0)
#endif

static void __init realview_pbx_timer_init(void)
{
	timer0_va_base = __io_address(REALVIEW_PBX_TIMER0_1_BASE);
	timer1_va_base = __io_address(REALVIEW_PBX_TIMER0_1_BASE) + 0x20;
	timer2_va_base = __io_address(REALVIEW_PBX_TIMER2_3_BASE);
	timer3_va_base = __io_address(REALVIEW_PBX_TIMER2_3_BASE) + 0x20;

	realview_timer_init(IRQ_PBX_TIMER0_1);
	realview_pbx_twd_init();
}

static struct sys_timer realview_pbx_timer = {
	.init		= realview_pbx_timer_init,
};

static void realview_pbx_fixup(struct tag *tags, char **from,
			       struct meminfo *meminfo)
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
	realview_fixup(tags, from, meminfo);
#endif
}

static void realview_pbx_restart(char mode, const char *cmd)
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
	dsb();
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
}

MACHINE_START(REALVIEW_PBX, "ARM-RealView PBX")
	/* Maintainer: ARM Ltd/Deep Blue Solutions Ltd */
	.atag_offset	= 0x100,
	.fixup		= realview_pbx_fixup,
	.map_io		= realview_pbx_map_io,
	.init_early	= realview_init_early,
	.init_irq	= gic_init_irq,
	.timer		= &realview_pbx_timer,
	.handle_irq	= gic_handle_irq,
	.init_machine	= realview_pbx_init,
#ifdef CONFIG_ZONE_DMA
	.dma_zone_size	= SZ_256M,
#endif
	.restart	= realview_pbx_restart,
MACHINE_END
