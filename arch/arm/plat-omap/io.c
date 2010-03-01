/*
 * Common io.c file
 * This file is created by Russell King <rmk+kernel@arm.linux.org.uk>
 *
 * Copyright (C) 2009 Texas Instruments
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/mm.h>

#include <plat/omap7xx.h>
#include <plat/omap1510.h>
#include <plat/omap16xx.h>
#include <plat/omap24xx.h>
#include <plat/omap34xx.h>
#include <plat/omap44xx.h>

#define BETWEEN(p,st,sz)	((p) >= (st) && (p) < ((st) + (sz)))
#define XLATE(p,pst,vst)	((void __iomem *)((p) - (pst) + (vst)))

/*
 * Intercept ioremap() requests for addresses in our fixed mapping regions.
 */
void __iomem *omap_ioremap(unsigned long p, size_t size, unsigned int type)
{
#ifdef CONFIG_ARCH_OMAP1
	if (cpu_class_is_omap1()) {
		if (BETWEEN(p, OMAP1_IO_PHYS, OMAP1_IO_SIZE))
			return XLATE(p, OMAP1_IO_PHYS, OMAP1_IO_VIRT);
	}
	if (cpu_is_omap7xx()) {
		if (BETWEEN(p, OMAP7XX_DSP_BASE, OMAP7XX_DSP_SIZE))
			return XLATE(p, OMAP7XX_DSP_BASE, OMAP7XX_DSP_START);

		if (BETWEEN(p, OMAP7XX_DSPREG_BASE, OMAP7XX_DSPREG_SIZE))
			return XLATE(p, OMAP7XX_DSPREG_BASE,
					OMAP7XX_DSPREG_START);
	}
	if (cpu_is_omap15xx()) {
		if (BETWEEN(p, OMAP1510_DSP_BASE, OMAP1510_DSP_SIZE))
			return XLATE(p, OMAP1510_DSP_BASE, OMAP1510_DSP_START);

		if (BETWEEN(p, OMAP1510_DSPREG_BASE, OMAP1510_DSPREG_SIZE))
			return XLATE(p, OMAP1510_DSPREG_BASE,
					OMAP1510_DSPREG_START);
	}
	if (cpu_is_omap16xx()) {
		if (BETWEEN(p, OMAP16XX_DSP_BASE, OMAP16XX_DSP_SIZE))
			return XLATE(p, OMAP16XX_DSP_BASE, OMAP16XX_DSP_START);

		if (BETWEEN(p, OMAP16XX_DSPREG_BASE, OMAP16XX_DSPREG_SIZE))
			return XLATE(p, OMAP16XX_DSPREG_BASE,
					OMAP16XX_DSPREG_START);
	}
#endif
#ifdef CONFIG_ARCH_OMAP2
	if (cpu_is_omap24xx()) {
		if (BETWEEN(p, L3_24XX_PHYS, L3_24XX_SIZE))
			return XLATE(p, L3_24XX_PHYS, L3_24XX_VIRT);
		if (BETWEEN(p, L4_24XX_PHYS, L4_24XX_SIZE))
			return XLATE(p, L4_24XX_PHYS, L4_24XX_VIRT);
	}
	if (cpu_is_omap2420()) {
		if (BETWEEN(p, DSP_MEM_2420_PHYS, DSP_MEM_2420_SIZE))
			return XLATE(p, DSP_MEM_2420_PHYS, DSP_MEM_2420_VIRT);
		if (BETWEEN(p, DSP_IPI_2420_PHYS, DSP_IPI_2420_SIZE))
			return XLATE(p, DSP_IPI_2420_PHYS, DSP_IPI_2420_SIZE);
		if (BETWEEN(p, DSP_MMU_2420_PHYS, DSP_MMU_2420_SIZE))
			return XLATE(p, DSP_MMU_2420_PHYS, DSP_MMU_2420_VIRT);
	}
	if (cpu_is_omap2430()) {
		if (BETWEEN(p, L4_WK_243X_PHYS, L4_WK_243X_SIZE))
			return XLATE(p, L4_WK_243X_PHYS, L4_WK_243X_VIRT);
		if (BETWEEN(p, OMAP243X_GPMC_PHYS, OMAP243X_GPMC_SIZE))
			return XLATE(p, OMAP243X_GPMC_PHYS, OMAP243X_GPMC_VIRT);
		if (BETWEEN(p, OMAP243X_SDRC_PHYS, OMAP243X_SDRC_SIZE))
			return XLATE(p, OMAP243X_SDRC_PHYS, OMAP243X_SDRC_VIRT);
		if (BETWEEN(p, OMAP243X_SMS_PHYS, OMAP243X_SMS_SIZE))
			return XLATE(p, OMAP243X_SMS_PHYS, OMAP243X_SMS_VIRT);
	}
#endif
#ifdef CONFIG_ARCH_OMAP3
	if (cpu_is_omap34xx()) {
		if (BETWEEN(p, L3_34XX_PHYS, L3_34XX_SIZE))
			return XLATE(p, L3_34XX_PHYS, L3_34XX_VIRT);
		if (BETWEEN(p, L4_34XX_PHYS, L4_34XX_SIZE))
			return XLATE(p, L4_34XX_PHYS, L4_34XX_VIRT);
		if (BETWEEN(p, L4_WK_34XX_PHYS, L4_WK_34XX_SIZE))
			return XLATE(p, L4_WK_34XX_PHYS, L4_WK_34XX_VIRT);
		if (BETWEEN(p, OMAP34XX_GPMC_PHYS, OMAP34XX_GPMC_SIZE))
			return XLATE(p, OMAP34XX_GPMC_PHYS, OMAP34XX_GPMC_VIRT);
		if (BETWEEN(p, OMAP343X_SMS_PHYS, OMAP343X_SMS_SIZE))
			return XLATE(p, OMAP343X_SMS_PHYS, OMAP343X_SMS_VIRT);
		if (BETWEEN(p, OMAP343X_SDRC_PHYS, OMAP343X_SDRC_SIZE))
			return XLATE(p, OMAP343X_SDRC_PHYS, OMAP343X_SDRC_VIRT);
		if (BETWEEN(p, L4_PER_34XX_PHYS, L4_PER_34XX_SIZE))
			return XLATE(p, L4_PER_34XX_PHYS, L4_PER_34XX_VIRT);
		if (BETWEEN(p, L4_EMU_34XX_PHYS, L4_EMU_34XX_SIZE))
			return XLATE(p, L4_EMU_34XX_PHYS, L4_EMU_34XX_VIRT);
	}
#endif
#ifdef CONFIG_ARCH_OMAP4
	if (cpu_is_omap44xx()) {
		if (BETWEEN(p, L3_44XX_PHYS, L3_44XX_SIZE))
			return XLATE(p, L3_44XX_PHYS, L3_44XX_VIRT);
		if (BETWEEN(p, L4_44XX_PHYS, L4_44XX_SIZE))
			return XLATE(p, L4_44XX_PHYS, L4_44XX_VIRT);
		if (BETWEEN(p, L4_WK_44XX_PHYS, L4_WK_44XX_SIZE))
			return XLATE(p, L4_WK_44XX_PHYS, L4_WK_44XX_VIRT);
		if (BETWEEN(p, OMAP44XX_GPMC_PHYS, OMAP44XX_GPMC_SIZE))
			return XLATE(p, OMAP44XX_GPMC_PHYS, OMAP44XX_GPMC_VIRT);
		if (BETWEEN(p, OMAP44XX_EMIF1_PHYS, OMAP44XX_EMIF1_SIZE))
			return XLATE(p, OMAP44XX_EMIF1_PHYS,		\
							OMAP44XX_EMIF1_VIRT);
		if (BETWEEN(p, OMAP44XX_EMIF2_PHYS, OMAP44XX_EMIF2_SIZE))
			return XLATE(p, OMAP44XX_EMIF2_PHYS,		\
							OMAP44XX_EMIF2_VIRT);
		if (BETWEEN(p, OMAP44XX_DMM_PHYS, OMAP44XX_DMM_SIZE))
			return XLATE(p, OMAP44XX_DMM_PHYS, OMAP44XX_DMM_VIRT);
		if (BETWEEN(p, L4_PER_44XX_PHYS, L4_PER_44XX_SIZE))
			return XLATE(p, L4_PER_44XX_PHYS, L4_PER_44XX_VIRT);
		if (BETWEEN(p, L4_EMU_44XX_PHYS, L4_EMU_44XX_SIZE))
			return XLATE(p, L4_EMU_44XX_PHYS, L4_EMU_44XX_VIRT);
	}
#endif
	return __arm_ioremap_caller(p, size, type, __builtin_return_address(0));
}
EXPORT_SYMBOL(omap_ioremap);

void omap_iounmap(volatile void __iomem *addr)
{
	unsigned long virt = (unsigned long)addr;

	if (virt >= VMALLOC_START && virt < VMALLOC_END)
		__iounmap(addr);
}
EXPORT_SYMBOL(omap_iounmap);

/*
 * NOTE: Please use ioremap + __raw_read/write where possible instead of these
 */

u8 omap_readb(u32 pa)
{
	if (cpu_class_is_omap1())
		return __raw_readb(OMAP1_IO_ADDRESS(pa));
	else
		return __raw_readb(OMAP2_L4_IO_ADDRESS(pa));
}
EXPORT_SYMBOL(omap_readb);

u16 omap_readw(u32 pa)
{
	if (cpu_class_is_omap1())
		return __raw_readw(OMAP1_IO_ADDRESS(pa));
	else
		return __raw_readw(OMAP2_L4_IO_ADDRESS(pa));
}
EXPORT_SYMBOL(omap_readw);

u32 omap_readl(u32 pa)
{
	if (cpu_class_is_omap1())
		return __raw_readl(OMAP1_IO_ADDRESS(pa));
	else
		return __raw_readl(OMAP2_L4_IO_ADDRESS(pa));
}
EXPORT_SYMBOL(omap_readl);

void omap_writeb(u8 v, u32 pa)
{
	if (cpu_class_is_omap1())
		__raw_writeb(v, OMAP1_IO_ADDRESS(pa));
	else
		__raw_writeb(v, OMAP2_L4_IO_ADDRESS(pa));
}
EXPORT_SYMBOL(omap_writeb);

void omap_writew(u16 v, u32 pa)
{
	if (cpu_class_is_omap1())
		__raw_writew(v, OMAP1_IO_ADDRESS(pa));
	else
		__raw_writew(v, OMAP2_L4_IO_ADDRESS(pa));
}
EXPORT_SYMBOL(omap_writew);

void omap_writel(u32 v, u32 pa)
{
	if (cpu_class_is_omap1())
		__raw_writel(v, OMAP1_IO_ADDRESS(pa));
	else
		__raw_writel(v, OMAP2_L4_IO_ADDRESS(pa));
}
EXPORT_SYMBOL(omap_writel);
