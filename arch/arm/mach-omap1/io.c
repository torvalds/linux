/*
 * linux/arch/arm/mach-omap1/io.c
 *
 * OMAP1 I/O mapping code
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#include <asm/tlb.h>
#include <asm/mach/map.h>

#include <mach/mux.h>
#include <mach/tc.h>
#include <plat-omap/dma-omap.h>

#include "iomap.h"
#include "common.h"
#include "clock.h"

/*
 * The machine specific code may provide the extra mapping besides the
 * default mapping provided here.
 */
static struct map_desc omap_io_desc[] __initdata = {
	{
		.virtual	= OMAP1_IO_VIRT,
		.pfn		= __phys_to_pfn(OMAP1_IO_PHYS),
		.length		= OMAP1_IO_SIZE,
		.type		= MT_DEVICE
	}
};

#if defined (CONFIG_ARCH_OMAP730) || defined (CONFIG_ARCH_OMAP850)
static struct map_desc omap7xx_io_desc[] __initdata = {
	{
		.virtual	= OMAP7XX_DSP_BASE,
		.pfn		= __phys_to_pfn(OMAP7XX_DSP_START),
		.length		= OMAP7XX_DSP_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= OMAP7XX_DSPREG_BASE,
		.pfn		= __phys_to_pfn(OMAP7XX_DSPREG_START),
		.length		= OMAP7XX_DSPREG_SIZE,
		.type		= MT_DEVICE
	}
};
#endif

#ifdef CONFIG_ARCH_OMAP15XX
static struct map_desc omap1510_io_desc[] __initdata = {
	{
		.virtual	= OMAP1510_DSP_BASE,
		.pfn		= __phys_to_pfn(OMAP1510_DSP_START),
		.length		= OMAP1510_DSP_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= OMAP1510_DSPREG_BASE,
		.pfn		= __phys_to_pfn(OMAP1510_DSPREG_START),
		.length		= OMAP1510_DSPREG_SIZE,
		.type		= MT_DEVICE
	}
};
#endif

#if defined(CONFIG_ARCH_OMAP16XX)
static struct map_desc omap16xx_io_desc[] __initdata = {
	{
		.virtual	= OMAP16XX_DSP_BASE,
		.pfn		= __phys_to_pfn(OMAP16XX_DSP_START),
		.length		= OMAP16XX_DSP_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= OMAP16XX_DSPREG_BASE,
		.pfn		= __phys_to_pfn(OMAP16XX_DSPREG_START),
		.length		= OMAP16XX_DSPREG_SIZE,
		.type		= MT_DEVICE
	}
};
#endif

/*
 * Maps common IO regions for omap1
 */
static void __init omap1_map_common_io(void)
{
	iotable_init(omap_io_desc, ARRAY_SIZE(omap_io_desc));
}

#if defined (CONFIG_ARCH_OMAP730) || defined (CONFIG_ARCH_OMAP850)
void __init omap7xx_map_io(void)
{
	omap1_map_common_io();
	iotable_init(omap7xx_io_desc, ARRAY_SIZE(omap7xx_io_desc));
}
#endif

#ifdef CONFIG_ARCH_OMAP15XX
void __init omap15xx_map_io(void)
{
	omap1_map_common_io();
	iotable_init(omap1510_io_desc, ARRAY_SIZE(omap1510_io_desc));
}
#endif

#if defined(CONFIG_ARCH_OMAP16XX)
void __init omap16xx_map_io(void)
{
	omap1_map_common_io();
	iotable_init(omap16xx_io_desc, ARRAY_SIZE(omap16xx_io_desc));
}
#endif

/*
 * Common low-level hardware init for omap1.
 */
void __init omap1_init_early(void)
{
	omap_check_revision();

	/* REVISIT: Refer to OMAP5910 Errata, Advisory SYS_1: "Timeout Abort
	 * on a Posted Write in the TIPB Bridge".
	 */
	omap_writew(0x0, MPU_PUBLIC_TIPB_CNTL);
	omap_writew(0x0, MPU_PRIVATE_TIPB_CNTL);

	/* Must init clocks early to assure that timer interrupt works
	 */
	omap1_clk_init();
	omap1_mux_init();
}

void __init omap1_init_late(void)
{
	omap_serial_wakeup_init();
}

/*
 * NOTE: Please use ioremap + __raw_read/write where possible instead of these
 */

u8 omap_readb(u32 pa)
{
	return __raw_readb(OMAP1_IO_ADDRESS(pa));
}
EXPORT_SYMBOL(omap_readb);

u16 omap_readw(u32 pa)
{
	return __raw_readw(OMAP1_IO_ADDRESS(pa));
}
EXPORT_SYMBOL(omap_readw);

u32 omap_readl(u32 pa)
{
	return __raw_readl(OMAP1_IO_ADDRESS(pa));
}
EXPORT_SYMBOL(omap_readl);

void omap_writeb(u8 v, u32 pa)
{
	__raw_writeb(v, OMAP1_IO_ADDRESS(pa));
}
EXPORT_SYMBOL(omap_writeb);

void omap_writew(u16 v, u32 pa)
{
	__raw_writew(v, OMAP1_IO_ADDRESS(pa));
}
EXPORT_SYMBOL(omap_writew);

void omap_writel(u32 v, u32 pa)
{
	__raw_writel(v, OMAP1_IO_ADDRESS(pa));
}
EXPORT_SYMBOL(omap_writel);
