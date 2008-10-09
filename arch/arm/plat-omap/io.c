#include <linux/module.h>
#include <linux/io.h>
#include <linux/mm.h>

#include <mach/omap730.h>
#include <mach/omap1510.h>
#include <mach/omap16xx.h>
#include <mach/omap24xx.h>
#include <mach/omap34xx.h>

#define BETWEEN(p,st,sz)	((p) >= (st) && (p) < ((st) + (sz)))
#define XLATE(p,pst,vst)	((void __iomem *)((p) - (pst) + (vst)))

/*
 * Intercept ioremap() requests for addresses in our fixed mapping regions.
 */
void __iomem *omap_ioremap(unsigned long p, size_t size, unsigned int type)
{
#ifdef CONFIG_ARCH_OMAP1
	if (cpu_class_is_omap1()) {
		if (BETWEEN(p, IO_PHYS, IO_SIZE))
			return XLATE(p, IO_PHYS, IO_VIRT);
	}
	if (cpu_is_omap730()) {
		if (BETWEEN(p, OMAP730_DSP_BASE, OMAP730_DSP_SIZE))
			return XLATE(p, OMAP730_DSP_BASE, OMAP730_DSP_START);

		if (BETWEEN(p, OMAP730_DSPREG_BASE, OMAP730_DSPREG_SIZE))
			return XLATE(p, OMAP730_DSPREG_BASE,
					OMAP730_DSPREG_START);
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
		if (BETWEEN(p, DSP_MEM_24XX_PHYS, DSP_MEM_24XX_SIZE))
			return XLATE(p, DSP_MEM_24XX_PHYS, DSP_MEM_24XX_VIRT);
		if (BETWEEN(p, DSP_IPI_24XX_PHYS, DSP_IPI_24XX_SIZE))
			return XLATE(p, DSP_IPI_24XX_PHYS, DSP_IPI_24XX_SIZE);
		if (BETWEEN(p, DSP_MMU_24XX_PHYS, DSP_MMU_24XX_SIZE))
			return XLATE(p, DSP_MMU_24XX_PHYS, DSP_MMU_24XX_VIRT);
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

	return __arm_ioremap(p, size, type);
}
EXPORT_SYMBOL(omap_ioremap);

void omap_iounmap(volatile void __iomem *addr)
{
	unsigned long virt = (unsigned long)addr;

	if (virt >= VMALLOC_START && virt < VMALLOC_END)
		__iounmap(addr);
}
EXPORT_SYMBOL(omap_iounmap);
