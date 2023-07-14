// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm/mach-omap1/io.c
 *
 * OMAP1 I/O mapping code
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/omap-dma.h>

#include <asm/tlb.h>
#include <asm/mach/map.h>

#include "tc.h"
#include "iomap.h"
#include "common.h"

/*
 * The machine specific code may provide the extra mapping besides the
 * default mapping provided here.
 */
static struct map_desc omap1_io_desc[] __initdata = {
	{
		.virtual	= OMAP1_IO_VIRT,
		.pfn		= __phys_to_pfn(OMAP1_IO_PHYS),
		.length		= OMAP1_IO_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= OMAP1_DSP_BASE,
		.pfn		= __phys_to_pfn(OMAP1_DSP_START),
		.length		= OMAP1_DSP_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= OMAP1_DSPREG_BASE,
		.pfn		= __phys_to_pfn(OMAP1_DSPREG_START),
		.length		= OMAP1_DSPREG_SIZE,
		.type		= MT_DEVICE
	}
};

/*
 * Maps common IO regions for omap1
 */
void __init omap1_map_io(void)
{
	iotable_init(omap1_io_desc, ARRAY_SIZE(omap1_io_desc));
}

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
