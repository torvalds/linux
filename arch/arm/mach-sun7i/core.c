/*
 *  arch/arm/mach-sun7i/core.c
 *
 *  Copyright (C) 2012 - 2016 Allwinner Limited
 *  Benn Huang (benn@allwinnertech.com)
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
#include <linux/memblock.h>
#include <linux/amba/pl022.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/clockchips.h>
#include <linux/module.h>
#include <asm/io.h>
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

#include <plat/sys_config.h>
#include <mach/includes.h>
#include <mach/timer.h>

#include "core.h"

void sw_pdev_init(void);
int arch_timer_common_register(void);

static struct map_desc sun7i_io_desc[] __initdata = {
	{IO_ADDRESS(SW_PA_IO_BASE), __phys_to_pfn(SW_PA_IO_BASE),  SW_IO_SIZE, MT_DEVICE_NONSHARED},
	{IO_ADDRESS(SW_PA_SRAM_A1_BASE), __phys_to_pfn(SW_PA_SRAM_A1_BASE),  SW_SRAM_A1_SIZE, MT_MEMORY_ITCM},
	{IO_ADDRESS(SW_PA_SRAM_A2_BASE), __phys_to_pfn(SW_PA_SRAM_A2_BASE),  SW_SRAM_A2_SIZE, MT_MEMORY_ITCM},
	{IO_ADDRESS(SW_PA_SRAM_A3_BASE), __phys_to_pfn(SW_PA_SRAM_A3_BASE),  SW_SRAM_A3_SIZE + SW_SRAM_A4_SIZE, MT_MEMORY_ITCM},
	//{IO_ADDRESS(SW_PA_SRAM_A4_BASE), __phys_to_pfn(SW_PA_SRAM_A4_BASE),  SW_SRAM_A4_SIZE, MT_MEMORY_ITCM}, /* not page align, cause sun7i_map_io err,2013-1-10 */
	{IO_ADDRESS(SW_PA_BROM_START), __phys_to_pfn(SW_PA_BROM_START), SW_BROM_SIZE, MT_DEVICE_NONSHARED},
};

static void __init sun7i_map_io(void)
{
	iotable_init(sun7i_io_desc, ARRAY_SIZE(sun7i_io_desc));
	sunxi_pr_chip_id();
}

static void __init gic_init_irq(void)
{
	gic_init(0, 29, (void *)IO_ADDRESS(AW_GIC_DIST_BASE), (void *)IO_ADDRESS(AW_GIC_CPU_BASE));
}

static void __init sun7i_timer_init(void)
{
	aw_clkevt_init();
	/* to fix, 2013-1-14 */
	aw_clksrc_init();
	arch_timer_common_register();
}

static struct sys_timer sun7i_timer = {
	.init		= sun7i_timer_init,
};

static void sun7i_fixup(struct tag *tags, char **from,
			struct meminfo *meminfo)
{
	pr_debug("[%s] enter\n", __func__);
	meminfo->bank[0].start = PLAT_PHYS_OFFSET;
	meminfo->bank[0].size = PLAT_MEM_SIZE;
	meminfo->nr_banks = 1;
}

#ifdef CONFIG_FB_SUNXI_RESERVED_MEM
/* The FB block is used by:
 *
 * - the sun4i framebuffer driver, drivers/video/sun4i/disp.
 *
 * fb_start, fb_size are used in a vast number of other places but for
 * for platform-specific drivers, so we don't have to worry about them.
 */

unsigned long fb_start = SW_FB_MEM_BASE;
unsigned long fb_size = SW_FB_MEM_SIZE;
EXPORT_SYMBOL(fb_start);
EXPORT_SYMBOL(fb_size);

static int __init reserve_fb_param(char *s)
{
	unsigned long size;
	if (kstrtoul(s, 0, &size) == 0)
		fb_size = size * SZ_1M;
	return 0;
}
early_param("sunxi_fb_mem_reserve", reserve_fb_param);
#endif

static void __init sun7i_reserve(void)
{
	pr_info("memblock reserve %lx(%x) for sysconfig, ret=%d\n", SYS_CONFIG_MEMBASE, SYS_CONFIG_MEMSIZE,
		memblock_reserve(SYS_CONFIG_MEMBASE, SYS_CONFIG_MEMSIZE));

	pr_info("memblock reserve %x(%x) for standby, ret=%d\n", SUPER_STANDBY_BASE, SUPER_STANDBY_SIZE,
		memblock_reserve(SUPER_STANDBY_BASE, SUPER_STANDBY_SIZE));

#ifdef CONFIG_FB_SUNXI_RESERVED_MEM
	pr_info("memblock reserve %lx(%lx) for fb, ret=%d\n", fb_start, fb_size,
		memblock_reserve(fb_start, fb_size));
#endif

	/* Ensure this is set before any arch_init funcs call script_foo */
	sunxi_script_init((void *)__va(SYS_CONFIG_MEMBASE));
}

static void sun7i_restart(char mode, const char *cmd)
{
	pr_err("%s: to check\n", __func__);

	/* use watch-dog to reset system */
	*(volatile unsigned int *)WDOG_MODE_REG = 0;
	__delay(100000);
	*(volatile unsigned int *)WDOG_MODE_REG |= 2;
	while(1) {
		__delay(100);
		*(volatile unsigned int *)WDOG_MODE_REG |= 1;
	}
}

static void __init sun7i_init(void)
{
	pr_info("%s: enter\n", __func__);
	sw_pdev_init();
	/* Register platform devices here!! */
}

void __init sun7i_init_early(void)
{
	pr_info("%s: enter\n", __func__);
}

MACHINE_START(SUN7I, "sun7i")
	.atag_offset	= 0x100,
	.fixup		= sun7i_fixup,
	.reserve        = sun7i_reserve,
	.map_io		= sun7i_map_io,
	.init_early	= sun7i_init_early,
	.init_irq	= gic_init_irq,
	.timer		= &sun7i_timer,
	.handle_irq	= gic_handle_irq,
	.init_machine	= sun7i_init,
#ifdef CONFIG_ZONE_DMA
	.dma_zone_size	= SZ_256M,
#endif
	.restart	= sun7i_restart,
MACHINE_END
