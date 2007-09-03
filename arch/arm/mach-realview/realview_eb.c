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

#include <asm/hardware.h>
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

#include <asm/arch/irqs.h>

#include "core.h"
#include "clock.h"

static struct map_desc realview_eb_io_desc[] __initdata = {
	{
		.virtual	= IO_ADDRESS(REALVIEW_SYS_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_SYS_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_GIC_CPU_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_GIC_CPU_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_GIC_DIST_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_GIC_DIST_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
#ifdef CONFIG_REALVIEW_MPCORE
	{
		.virtual	= IO_ADDRESS(REALVIEW_GIC1_CPU_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_GIC1_CPU_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_GIC1_DIST_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_GIC1_DIST_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_MPCORE_L220_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_MPCORE_L220_BASE),
		.length		= SZ_8K,
		.type		= MT_DEVICE,
	},
#endif
	{
		.virtual	= IO_ADDRESS(REALVIEW_SCTL_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_SCTL_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_TIMER0_1_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_TIMER0_1_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(REALVIEW_TIMER2_3_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_TIMER2_3_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
#ifdef CONFIG_DEBUG_LL
	{
		.virtual	= IO_ADDRESS(REALVIEW_UART0_BASE),
		.pfn		= __phys_to_pfn(REALVIEW_UART0_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}
#endif
};

static void __init realview_eb_map_io(void)
{
	iotable_init(realview_eb_io_desc, ARRAY_SIZE(realview_eb_io_desc));
}

/* FPGA Primecells */
AMBA_DEVICE(aaci,  "fpga:04", AACI,     NULL);
AMBA_DEVICE(mmc0,  "fpga:05", MMCI0,    &realview_mmc0_plat_data);
AMBA_DEVICE(kmi0,  "fpga:06", KMI0,     NULL);
AMBA_DEVICE(kmi1,  "fpga:07", KMI1,     NULL);
AMBA_DEVICE(uart3, "fpga:09", UART3,    NULL);

/* DevChip Primecells */
AMBA_DEVICE(smc,   "dev:00",  SMC,      NULL);
AMBA_DEVICE(clcd,  "dev:20",  CLCD,     &clcd_plat_data);
AMBA_DEVICE(dmac,  "dev:30",  DMAC,     NULL);
AMBA_DEVICE(sctl,  "dev:e0",  SCTL,     NULL);
AMBA_DEVICE(wdog,  "dev:e1",  WATCHDOG, NULL);
AMBA_DEVICE(gpio0, "dev:e4",  GPIO0,    NULL);
AMBA_DEVICE(gpio1, "dev:e5",  GPIO1,    NULL);
AMBA_DEVICE(gpio2, "dev:e6",  GPIO2,    NULL);
AMBA_DEVICE(rtc,   "dev:e8",  RTC,      NULL);
AMBA_DEVICE(sci0,  "dev:f0",  SCI,      NULL);
AMBA_DEVICE(uart0, "dev:f1",  UART0,    NULL);
AMBA_DEVICE(uart1, "dev:f2",  UART1,    NULL);
AMBA_DEVICE(uart2, "dev:f3",  UART2,    NULL);
AMBA_DEVICE(ssp0,  "dev:f4",  SSP,      NULL);

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

static void __init gic_init_irq(void)
{
#ifdef CONFIG_REALVIEW_MPCORE
	unsigned int pldctrl;
	writel(0x0000a05f, __io_address(REALVIEW_SYS_LOCK));
	pldctrl = readl(__io_address(REALVIEW_SYS_BASE)	+ REALVIEW_MPCORE_SYS_PLD_CTRL1);
	pldctrl |= 0x00800000;	/* New irq mode */
	writel(pldctrl, __io_address(REALVIEW_SYS_BASE) + REALVIEW_MPCORE_SYS_PLD_CTRL1);
	writel(0x00000000, __io_address(REALVIEW_SYS_LOCK));
#endif
	gic_dist_init(0, __io_address(REALVIEW_GIC_DIST_BASE), 29);
	gic_cpu_init(0, __io_address(REALVIEW_GIC_CPU_BASE));
#if defined(CONFIG_REALVIEW_MPCORE) && !defined(CONFIG_REALVIEW_MPCORE_REVB)
	gic_dist_init(1, __io_address(REALVIEW_GIC1_DIST_BASE), 64);
	gic_cpu_init(1, __io_address(REALVIEW_GIC1_CPU_BASE));
	gic_cascade_irq(1, IRQ_EB_IRQ1);
#endif
}

static void __init realview_eb_init(void)
{
	int i;

#ifdef CONFIG_REALVIEW_MPCORE
	/* 1MB (128KB/way), 8-way associativity, evmon/parity/share enabled
	 * Bits:  .... ...0 0111 1001 0000 .... .... .... */
	l2x0_init(__io_address(REALVIEW_MPCORE_L220_BASE), 0x00790000, 0xfe000fff);
#endif
	clk_register(&realview_clcd_clk);

	platform_device_register(&realview_flash_device);
	platform_device_register(&realview_smc91x_device);
	platform_device_register(&realview_i2c_device);

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
	.phys_io	= REALVIEW_UART0_BASE,
	.io_pg_offst	= (IO_ADDRESS(REALVIEW_UART0_BASE) >> 18) & 0xfffc,
	.boot_params	= 0x00000100,
	.map_io		= realview_eb_map_io,
	.init_irq	= gic_init_irq,
	.timer		= &realview_timer,
	.init_machine	= realview_eb_init,
MACHINE_END
