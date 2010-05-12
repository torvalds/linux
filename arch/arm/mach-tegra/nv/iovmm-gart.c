/*
 * arch/arm/mach-tegra/iovmm-gart.c
 *
 * Tegra I/O VMM implementation for GART devices in Tegra and Tegra 2 series
 * systems-on-a-chip.
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <asm/cacheflush.h>
#include "mach/iovmm.h"
#include "nvrm_drf.h"

#if defined(CONFIG_ARCH_TEGRA_2x_SOC)
#include "ap20/armc.h"
#elif defined(CONFIG_ARCH_TEGRA_1x_SOC)
#include "ap15/armc.h"
#else
#error "Unknown Tegra chip family!"
#endif

#define VMM_NAME "iovmm-gart"
#define DRIVER_NAME "tegra_gart"

#define GART_PAGE_SHIFT (12)
#define GART_PAGE_MASK (~((1<<GART_PAGE_SHIFT)-1))

struct gart_device {
	void __iomem		*regs;
	u32			*savedata;
	u32			page_count; /* total remappable size */
	tegra_iovmm_addr_t	iovmm_base; /* offset to apply to vmm_area */
	spinlock_t		pte_lock;
	struct tegra_iovmm_device iovmm;
	struct tegra_iovmm_domain domain;
	bool			enable;
	bool			needs_barrier; /* emulator WAR */
};

static int gart_map(struct tegra_iovmm_device *, struct tegra_iovmm_area *);
static void gart_unmap(struct tegra_iovmm_device *,
	struct tegra_iovmm_area *, bool);
static void gart_map_pfn(struct tegra_iovmm_device *,
	struct tegra_iovmm_area *, tegra_iovmm_addr_t, unsigned long);
static struct tegra_iovmm_domain *gart_alloc_domain(
	struct tegra_iovmm_device *, struct tegra_iovmm_client *);

static int __init gart_probe(struct platform_device *);
static int __devexit gart_remove(struct platform_device *);
static int gart_suspend(struct platform_device *, pm_message_t);
static int gart_resume(struct platform_device *);


static struct tegra_iovmm_device_ops tegra_iovmm_gart_ops = {
	.map		= gart_map,
	.unmap		= gart_unmap,
	.map_pfn	= gart_map_pfn,
	.alloc_domain	= gart_alloc_domain,
};

static struct platform_driver tegra_iovmm_gart_drv = {
	.probe		= gart_probe,
	.remove		= gart_remove,
	.suspend	= gart_suspend,
	.resume		= gart_resume,
	.driver		= {
		.name	= DRIVER_NAME,
	},
};

#define gpfn_to_gart(_g, _gpfn) (((_g)->iovmm_base>>GART_PAGE_SHIFT) + (_gpfn))

static int gart_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct gart_device *gart = platform_get_drvdata(pdev);
	unsigned int i;

	if (!gart)
		return -ENODEV;

	if (!gart->enable)
		return 0;

	spin_lock(&gart->pte_lock);
	for (i=0; i<gart->page_count; i++) {
		u32 reg;
		reg = NV_DRF_NUM(MC, GART_ENTRY_ADDR,
			GART_ENTRY_ADDR_TABLE_ADDR, gpfn_to_gart(gart,i));
		writel(reg, gart->regs + MC_GART_ENTRY_ADDR_0);
		gart->savedata[i] = readl(gart->regs + MC_GART_ENTRY_DATA_0);
	}
	spin_unlock(&gart->pte_lock);
	return 0;
}

static int gart_resume(struct platform_device *pdev)
{
	struct gart_device *gart = platform_get_drvdata(pdev);
	unsigned int i;
	u32 reg;

	if (!gart || (gart->enable && !gart->savedata))
		return -ENODEV;

	if (!gart->enable)
		return 0;

	spin_lock(&gart->pte_lock);
	for (i=0; i<gart->page_count; i++) {
		reg = NV_DRF_NUM(MC, GART_ENTRY_ADDR,
			GART_ENTRY_ADDR_TABLE_ADDR, gpfn_to_gart(gart, i));
		writel(reg, gart->regs + MC_GART_ENTRY_ADDR_0);
		writel(gart->savedata[i], gart->regs + MC_GART_ENTRY_DATA_0);
	}
	reg = NV_DRF_DEF(MC, GART_CONFIG, GART_ENABLE, ENABLE);
	writel(reg, gart->regs + MC_GART_CONFIG_0);
	spin_unlock(&gart->pte_lock);

	return 0;
}

static int __devexit gart_remove(struct platform_device *pdev)
{
	struct gart_device *gart = platform_get_drvdata(pdev);

	if (!gart)
		return 0;

	if (gart->enable) {
		u32 reg;
		reg = NV_DRF_DEF(MC, GART_CONFIG, GART_ENABLE, DISABLE);
		writel(reg, gart->regs + MC_GART_CONFIG_0);
		gart->enable = 0;
	}
	platform_set_drvdata(pdev, NULL);
	tegra_iovmm_unregister(&gart->iovmm);
	if (gart->savedata)
		vfree(gart->savedata);
	if (gart->regs)
		iounmap(gart->regs);
	kfree(gart);
	return 0;
}

static int __init gart_probe(struct platform_device *pdev)
{
	struct gart_device *gart = NULL;
	struct resource *res, *res_remap;
	void __iomem *gart_regs = NULL;
	u32 reg;
	unsigned int i;
	int e;

	if (!pdev) {
		pr_err(DRIVER_NAME ": platform_device required\n");
		return -ENODEV;
	}

	if (PAGE_SHIFT != GART_PAGE_SHIFT) {
		pr_err(DRIVER_NAME ": GART and CPU page size must match\n");
		return -ENXIO;
	}

	/* the GART memory aperture is required */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	res_remap = platform_get_resource(pdev, IORESOURCE_MEM, 1);

	if (!res || !res_remap) {
		pr_err(DRIVER_NAME ": GART memory aperture expected\n");
		return -ENXIO;
	}
	gart = kzalloc(sizeof(*gart), GFP_KERNEL);
	if (!gart) {
		pr_err(DRIVER_NAME ": failed to allocate tegra_iovmm_device\n");
		e = -ENOMEM;
		goto fail;
	}

	gart_regs = ioremap_wc(res->start, res->end - res->start + 1);
	if (!gart_regs) {
		pr_err(DRIVER_NAME ": failed to remap GART registers\n");
		e = -ENXIO;
		goto fail;
	}

	gart->iovmm.name = VMM_NAME;
	gart->iovmm.ops = &tegra_iovmm_gart_ops;
	gart->iovmm.pgsize_bits = GART_PAGE_SHIFT;
	spin_lock_init(&gart->pte_lock);

	platform_set_drvdata(pdev, gart);

	e = tegra_iovmm_register(&gart->iovmm);
	if (e) goto fail;

	e = tegra_iovmm_domain_init(&gart->domain, &gart->iovmm,
		(tegra_iovmm_addr_t)res_remap->start,
		(tegra_iovmm_addr_t)res_remap->end+1);
	if (e) goto fail;

	gart->regs = gart_regs;
	gart->iovmm_base = (tegra_iovmm_addr_t)res_remap->start;
	gart->page_count = res_remap->end - res_remap->start + 1;
	gart->page_count >>= GART_PAGE_SHIFT;

	gart->savedata = vmalloc(sizeof(u32)*gart->page_count);
	if (!gart->savedata) {
		pr_err(DRIVER_NAME ": failed to allocate context save area\n");
		e = -ENOMEM;
		goto fail;
	}

	spin_lock(&gart->pte_lock);
	for (i=0; i<gart->page_count; i++) {
		reg = NV_DRF_NUM(MC, GART_ENTRY_ADDR,
			GART_ENTRY_ADDR_TABLE_ADDR, gpfn_to_gart(gart, i));
		writel(reg, gart->regs + MC_GART_ENTRY_ADDR_0);
		writel(0, gart->regs + MC_GART_ENTRY_DATA_0);
	}
	reg = NV_DRF_DEF(MC, GART_CONFIG, GART_ENABLE, ENABLE);
	writel(reg, gart->regs + MC_GART_CONFIG_0);
	spin_unlock(&gart->pte_lock);
	gart->enable = 1;
	return 0;

fail:
	if (gart_regs)
		iounmap(gart_regs);
	if (gart && gart->savedata)
		vfree(gart->savedata);
	if (gart)
		kfree(gart);
	return e;
}

static int __devinit gart_init(void)
{
	return platform_driver_register(&tegra_iovmm_gart_drv);
}

static void __exit gart_exit(void)
{
	return platform_driver_unregister(&tegra_iovmm_gart_drv);
}

#define GART_PTE(_valid, _pfn) \
	(NV_DRF_NUM(MC,GART_ENTRY_DATA,GART_ENTRY_DATA_PHYS_ADDR_VALID,(_valid))|\
	 NV_DRF_NUM(MC,GART_ENTRY_DATA,GART_ENTRY_DATA_PHYS_ADDR,\
		((_pfn)<<PAGE_SHIFT)>>GART_PAGE_SHIFT))


static int gart_map(struct tegra_iovmm_device *dev,
	struct tegra_iovmm_area *iovma)
{
	struct gart_device *gart = container_of(dev, struct gart_device, iovmm);
	u32 gart_page, count;
	unsigned int i;

	gart_page = iovma->iovm_start >> GART_PAGE_SHIFT;
	count = iovma->iovm_length >> GART_PAGE_SHIFT;

	for (i=0; i<count; i++) {
		unsigned long pfn;
		u32 reg;

		pfn = iovma->ops->lock_makeresident(iovma, i<<PAGE_SHIFT);
		if (!pfn_valid(pfn))
			goto fail;

		spin_lock(&gart->pte_lock);
		/* gpfn translation not needed, since iovm_start already
		 * includes the offset */
		reg = NV_DRF_NUM(MC, GART_ENTRY_ADDR,
			GART_ENTRY_ADDR_TABLE_ADDR, gart_page + i);
		writel(reg, gart->regs + MC_GART_ENTRY_ADDR_0);
		reg = GART_PTE(1,pfn);
		writel(reg, gart->regs + MC_GART_ENTRY_DATA_0);
		if (unlikely(gart->needs_barrier))
			reg = readl(gart->regs + MC_GART_ENTRY_DATA_0);
		spin_unlock(&gart->pte_lock);
	}

	dmb();
	outer_sync();
	return 0;

fail:
	while (i--) {
		u32 reg;
		iovma->ops->release(iovma, i<<PAGE_SHIFT);
		spin_lock(&gart->pte_lock);
		reg = NV_DRF_NUM(MC, GART_ENTRY_ADDR,
			GART_ENTRY_ADDR_TABLE_ADDR, gart_page + i);
		writel(reg, gart->regs + MC_GART_ENTRY_ADDR_0);
		writel(0, gart->regs + MC_GART_ENTRY_DATA_0);
		spin_unlock(&gart->pte_lock);
	}

	return -ENOMEM;
}

static void gart_unmap(struct tegra_iovmm_device *dev,
	struct tegra_iovmm_area *iovma, bool decommit)
{
	struct gart_device *gart = container_of(dev, struct gart_device, iovmm);
	u32 gart_page;
	unsigned int i;

	spin_lock(&gart->pte_lock);
	for (i=0, gart_page=iovma->iovm_start;
	    gart_page<iovma->iovm_start + iovma->iovm_length;
	    gart_page+=(1<<GART_PAGE_SHIFT), i++) {
		if (iovma->ops && iovma->ops->release)
			iovma->ops->release(iovma, i<<PAGE_SHIFT);

		writel(gart_page, gart->regs + MC_GART_ENTRY_ADDR_0);
		writel(0, gart->regs + MC_GART_ENTRY_DATA_0);
	}
	spin_unlock(&gart->pte_lock);
	dmb();
	outer_sync();
}

static void gart_map_pfn(struct tegra_iovmm_device *dev,
	struct tegra_iovmm_area *iovma, tegra_iovmm_addr_t offs,
	unsigned long pfn)
{
	struct gart_device *gart = container_of(dev, struct gart_device, iovmm);

	BUG_ON((pfn<<PAGE_SHIFT)>=0x40000000ul);
	spin_lock(&gart->pte_lock);
	writel(offs, gart->regs + MC_GART_ENTRY_ADDR_0);
	writel(GART_PTE(1,pfn), gart->regs + MC_GART_ENTRY_DATA_0);
	(void)readl(gart->regs + MC_GART_ENTRY_DATA_0);
	spin_unlock(&gart->pte_lock);
	dmb();
	outer_sync();
}

static struct tegra_iovmm_domain *gart_alloc_domain(
	struct tegra_iovmm_device *dev, struct tegra_iovmm_client *client)
{
	struct gart_device *gart = container_of(dev, struct gart_device, iovmm);
	return &gart->domain;
}

module_init(gart_init);
module_exit(gart_exit);
