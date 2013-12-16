/*
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Benn Huang <benn@allwinnertech.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/amba/bus.h>
#include <linux/amba/clcd.h>
#include <linux/amba/pl061.h>
#include <linux/amba/mmci.h>
#include <linux/amba/pl022.h>
#include <linux/io.h>
#include <linux/gfp.h>
#include <linux/clockchips.h>
#include <linux/memblock.h>
#include <linux/bootmem.h>
#include <linux/export.h>
#include <linux/clkdev.h>

#include <asm/arch_timer.h>
#include <asm/sched_clock.h>
#include <asm/setup.h>
#include <asm/sizes.h>
#include <asm/mach-types.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/leds.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/delay.h>
#include <asm/mach/map.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <asm/hardware/arm_timer.h>
#include <asm/hardware/gic.h>
#include <asm/hardware/icst.h>
#include <asm/hardware/vic.h>

#include <plat/core.h>
#include <plat/hardware.h>
#include <plat/memory.h>
#include <plat/platform.h>
#include <plat/system.h>
#include <plat/sys_config.h>

#include "clocksrc.h"

int arch_timer_common_register(void);
void sw_pdev_init(void);

/*
 * Only reserve certain important memory blocks if there are actually
 * drivers which use them.
 */
static unsigned long reserved_start;
static unsigned long reserved_max;

/**
 * Machine Implementations
 *
 */

/* sun4i / sun5i io-map */
static struct map_desc sw_io_desc[] __initdata = {
	{ SW_VA_SRAM_BASE, __phys_to_pfn(SW_PA_SRAM_BASE),  (SZ_128K + SZ_64K), MT_MEMORY_ITCM  },
	{ SW_VA_IO_BASE,   __phys_to_pfn(SW_PA_IO_BASE),    (SZ_1M + SZ_2M),    MT_DEVICE       },
	{ SW_VA_BROM_BASE, __phys_to_pfn(SW_PA_BROM_BASE),  (SZ_64K),           MT_MEMORY_ITCM  },
};
static void __init sw_core_map_io(void)
{
	iotable_init(sw_io_desc, ARRAY_SIZE(sw_io_desc));

	sunxi_pr_chip_id();
}

/* sun7i io-map */
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

#ifdef CONFIG_FB_SUNXI_RESERVED_MEM
/* The FB block is used by:
 *
 * - the sun4i framebuffer driver, drivers/video/sun4i/disp.
 *
 * fb_start, fb_size are used in a vast number of other places but for
 * for platform-specific drivers, so we don't have to worry about them.
 */

unsigned long fb_start;
unsigned long fb_size = SZ_32M;
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

#if IS_ENABLED(CONFIG_SUNXI_G2D)
/* The G2D block is used by:
 *
 * - the G2D engine, drivers/char/sunxi_g2d
 */

unsigned long g2d_start;
unsigned long g2d_size = SZ_1M * 16;
EXPORT_SYMBOL(g2d_start);
EXPORT_SYMBOL(g2d_size);

static int __init reserve_g2d_param(char *s)
{
	unsigned long size;
	if (kstrtoul(s, 0, &size) == 0)
		g2d_size = size * SZ_1M;
	return 0;
}
early_param("sunxi_g2d_mem_reserve", reserve_g2d_param);
#endif

#if defined CONFIG_VIDEO_DECODER_SUNXI || \
	defined CONFIG_VIDEO_DECODER_SUNXI_MODULE
/* The VE block is used by:
 *
 * - the Cedar video engine, drivers/media/video/sunxi
 */

#define RESERVE_VE_MEM 1

unsigned long ve_start;
unsigned long ve_size = (SZ_64M + SZ_16M);
EXPORT_SYMBOL(ve_start);
EXPORT_SYMBOL(ve_size);

static int __init reserve_ve_param(char *s)
{
	unsigned long size;
	if (kstrtoul(s, 0, &size) == 0)
		ve_size = size * SZ_1M;
	return 0;
}
early_param("sunxi_ve_mem_reserve", reserve_ve_param);
#endif

static void reserve_sys(void)
{
	memblock_reserve(SYS_CONFIG_MEMBASE, SYS_CONFIG_MEMSIZE);
	pr_reserve_info("SYS ", SYS_CONFIG_MEMBASE, SYS_CONFIG_MEMSIZE);
}

#if defined RESERVE_VE_MEM || defined CONFIG_FB_SUNXI_RESERVED_MEM || \
	IS_ENABLED(CONFIG_SUNXI_G2D)
static void reserve_mem(unsigned long *start, unsigned long *size,
			const char *desc)
{
	if (*size == 0) {
		*start = 0;
		return;
	}

	if ((reserved_start + *size) > reserved_max) {
		pr_warn("Not enough memory to reserve memory for %s\n", desc);
		*start = 0;
		*size = 0;
		return;
	}
	*start = reserved_start;
	memblock_reserve(*start, *size);
	pr_reserve_info(desc, *start, *size);
	reserved_start += *size;
}
#endif

static void __init sw_core_reserve(void)
{
	pr_info("Memory Reserved:\n");
	reserve_sys();
	/* 0 - 64M is used by reserve_sys */
#ifdef CONFIG_CMA
	/* We want CMA area to be in the first 256MB of RAM (for Cedar),
	 * so move other reservations above
	 */
	reserved_start = meminfo.bank[0].start + SZ_256M;
#else
	reserved_start = meminfo.bank[0].start + SZ_64M;
#endif
	reserved_max   = meminfo.bank[0].start + meminfo.bank[0].size;
#if !defined(CONFIG_CMA) && defined(RESERVE_VE_MEM)
	reserve_mem(&ve_start, &ve_size, "VE  ");
#endif
#if IS_ENABLED(CONFIG_SUNXI_G2D)
	reserve_mem(&g2d_start, &g2d_size, "G2D ");
#endif
#ifdef CONFIG_FB_SUNXI_RESERVED_MEM
	reserve_mem(&fb_start, &fb_size, "LCD ");
#endif
	/* Ensure this is set before any arch_init funcs call script_foo */
	sunxi_script_init((void *)__va(SYS_CONFIG_MEMBASE));
}

void sw_irq_ack(struct irq_data *irqd)
{
	unsigned int irq = irqd->irq;

	if (irq < 32){
		writel(readl(SW_INT_ENABLE_REG0) & ~(1<<irq), SW_INT_ENABLE_REG0);
		writel(readl(SW_INT_MASK_REG0) | (1 << irq), SW_INT_MASK_REG0);
		writel(readl(SW_INT_IRQ_PENDING_REG0) | (1<<irq), SW_INT_IRQ_PENDING_REG0);
	} else if(irq < 64){
		irq -= 32;
		writel(readl(SW_INT_ENABLE_REG1) & ~(1<<irq), SW_INT_ENABLE_REG1);
		writel(readl(SW_INT_MASK_REG1) | (1 << irq), SW_INT_MASK_REG1);
		writel(readl(SW_INT_IRQ_PENDING_REG1) | (1<<irq), SW_INT_IRQ_PENDING_REG1);
	} else if(irq < 96){
		irq -= 64;
		writel(readl(SW_INT_ENABLE_REG2) & ~(1<<irq), SW_INT_ENABLE_REG2);
		writel(readl(SW_INT_MASK_REG2) | (1 << irq), SW_INT_MASK_REG2);
		writel(readl(SW_INT_IRQ_PENDING_REG2) | (1<<irq), SW_INT_IRQ_PENDING_REG2);
	}
}

/* Mask an IRQ line, which means disabling the IRQ line */
static void sw_irq_mask(struct irq_data *irqd)
{
	unsigned int irq = irqd->irq;

	if(irq < 32){
		writel(readl(SW_INT_ENABLE_REG0) & ~(1<<irq), SW_INT_ENABLE_REG0);
		writel(readl(SW_INT_MASK_REG0) | (1 << irq), SW_INT_MASK_REG0);
	} else if(irq < 64){
		irq -= 32;
		writel(readl(SW_INT_ENABLE_REG1) & ~(1<<irq), SW_INT_ENABLE_REG1);
		writel(readl(SW_INT_MASK_REG1) | (1 << irq), SW_INT_MASK_REG1);
	} else if(irq < 96){
		irq -= 64;
		writel(readl(SW_INT_ENABLE_REG2) & ~(1<<irq), SW_INT_ENABLE_REG2);
		writel(readl(SW_INT_MASK_REG2) | (1 << irq), SW_INT_MASK_REG2);
	}
}

static void sw_irq_unmask(struct irq_data *irqd)
{
	unsigned int irq = irqd->irq;

	if(irq < 32){
		writel(readl(SW_INT_ENABLE_REG0) | (1<<irq), SW_INT_ENABLE_REG0);
		writel(readl(SW_INT_MASK_REG0) & ~(1 << irq), SW_INT_MASK_REG0);
		if(irq == SW_INT_IRQNO_ENMI) /* must clear pending bit when enabled */
			writel((1 << SW_INT_IRQNO_ENMI), SW_INT_IRQ_PENDING_REG0);
	} else if(irq < 64){
		irq -= 32;
		writel(readl(SW_INT_ENABLE_REG1) | (1<<irq), SW_INT_ENABLE_REG1);
		writel(readl(SW_INT_MASK_REG1) & ~(1 << irq), SW_INT_MASK_REG1);
	} else if(irq < 96){
		irq -= 64;
		writel(readl(SW_INT_ENABLE_REG2) | (1<<irq), SW_INT_ENABLE_REG2);
		writel(readl(SW_INT_MASK_REG2) & ~(1 << irq), SW_INT_MASK_REG2);
	}
}

static struct irq_chip sw_vic_chip = {
	.name       = "sw_vic",
	.irq_ack    = sw_irq_ack,
	.irq_mask   = sw_irq_mask,
	.irq_unmask = sw_irq_unmask,
};

void __init sw_core_init_irq(void)
{
	u32 i = 0;

	/* Disable & clear all interrupts */
	writel(0, SW_INT_ENABLE_REG0);
	writel(0, SW_INT_ENABLE_REG1);
	writel(0, SW_INT_ENABLE_REG2);

	writel(0xffffffff, SW_INT_MASK_REG0);
	writel(0xffffffff, SW_INT_MASK_REG1);
	writel(0xffffffff, SW_INT_MASK_REG2);

	writel(0xffffffff, SW_INT_IRQ_PENDING_REG0);
	writel(0xffffffff, SW_INT_IRQ_PENDING_REG1);
	writel(0xffffffff, SW_INT_IRQ_PENDING_REG2);
	writel(0xffffffff, SW_INT_FIQ_PENDING_REG0);
	writel(0xffffffff, SW_INT_FIQ_PENDING_REG1);
	writel(0xffffffff, SW_INT_FIQ_PENDING_REG2);

	/*enable protection mode*/
	writel(0x01, SW_INT_PROTECTION_REG);
	/*config the external interrupt source type*/
	writel(0x00, SW_INT_NMI_CTRL_REG);

	for (i = SW_INT_START; i < SW_INT_END; i++) {
		irq_set_chip(i, &sw_vic_chip);
		irq_set_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
	}
}

static void __init gic_init_irq(void)
{
/*
 * HdG: note to anyone trying to make it possible to build a single sunxi image
 * for all of sun4i/sun5i/sun7i, selecting CONFIG_ARM_GIC automatically selects
 * CONFIG_MULTI_IRQ_HANDLER, at which point we need to provide a handle_irq
 * function for the sun4i and sun5i machine definitions, vic_handle_irq is
 * probably a good start for this / maybe we can even use all of the common
 * vic handling code?
 */
#ifdef CONFIG_ARM_GIC
	gic_init(0, 29, (void *)IO_ADDRESS(AW_GIC_DIST_BASE),
		 (void *)IO_ADDRESS(AW_GIC_CPU_BASE));
#endif
}


/*
 * Global vars definitions
 *
 */
static void sun4i_restart(char mode, const char *cmd)
{
	/* use watch-dog to reset system */
	#define WATCH_DOG_CTRL_REG  (SW_VA_TIMERC_IO_BASE + 0x0090)
	#define WATCH_DOG_MODE_REG  (SW_VA_TIMERC_IO_BASE + 0x0094)

	*(volatile unsigned int *)WATCH_DOG_MODE_REG = 0;
	__delay(100000);
	*(volatile unsigned int *)WATCH_DOG_MODE_REG |= 2;
	while(1) {
		__delay(100);
		*(volatile unsigned int *)WATCH_DOG_MODE_REG |= 1;
	}
}

static void __init sw_timer_init(void)
{
	aw_clkevt_init();
	aw_clksrc_init();
#ifdef CONFIG_ARM_ARCH_TIMER
	if (sunxi_is_sun7i()) {
		arch_timer_common_register();
		arch_timer_sched_clock_init();
	} else
#endif
	setup_sched_clock(aw_sched_clock_read, 32, AW_HPET_CLOCK_SOURCE_HZ);
}

struct sys_timer sw_sys_timer = {
	.init = sw_timer_init,
};

void __init sw_core_init(void)
{
	sw_pdev_init();
}

MACHINE_START(SUN4I, "sun4i")
	.atag_offset	= 0x100,
	.timer          = &sw_sys_timer,
	.map_io         = sw_core_map_io,
	.init_early     = NULL,
	.init_irq       = sw_core_init_irq,
	.init_machine   = sw_core_init,
	.reserve        = sw_core_reserve,
	.restart	= sun4i_restart,
MACHINE_END

MACHINE_START(SUN5I, "sun5i")
	.atag_offset	= 0x100,
	.timer          = &sw_sys_timer,
	.map_io         = sw_core_map_io,
	.init_early     = NULL,
	.init_irq       = sw_core_init_irq,
	.init_machine   = sw_core_init,
	.reserve        = sw_core_reserve,
	.restart	= sun4i_restart,
MACHINE_END

MACHINE_START(SUN7I, "sun7i")
	.atag_offset	= 0x100,
	.timer          = &sw_sys_timer,
	.map_io         = sun7i_map_io,
	.init_early     = NULL,
	.init_irq	= gic_init_irq,
	.init_machine   = sw_core_init,
	.reserve        = sw_core_reserve,
	.restart	= sun4i_restart,
#ifdef CONFIG_MULTI_IRQ_HANDLER
	.handle_irq	= gic_handle_irq,
#endif
#ifdef CONFIG_ZONE_DMA
	.dma_zone_size	= SZ_256M,
#endif
MACHINE_END
