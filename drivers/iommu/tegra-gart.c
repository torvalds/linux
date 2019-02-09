/*
 * IOMMU API for GART in Tegra20
 *
 * Copyright (c) 2010-2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define pr_fmt(fmt)	"%s(): " fmt, __func__

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/of.h>

#include <asm/cacheflush.h>

/* bitmap of the page sizes currently supported */
#define GART_IOMMU_PGSIZES	(SZ_4K)

#define GART_REG_BASE		0x24
#define GART_CONFIG		(0x24 - GART_REG_BASE)
#define GART_ENTRY_ADDR		(0x28 - GART_REG_BASE)
#define GART_ENTRY_DATA		(0x2c - GART_REG_BASE)
#define GART_ENTRY_PHYS_ADDR_VALID	(1 << 31)

#define GART_PAGE_SHIFT		12
#define GART_PAGE_SIZE		(1 << GART_PAGE_SHIFT)
#define GART_PAGE_MASK						\
	(~(GART_PAGE_SIZE - 1) & ~GART_ENTRY_PHYS_ADDR_VALID)

struct gart_client {
	struct device		*dev;
	struct list_head	list;
};

struct gart_device {
	void __iomem		*regs;
	u32			*savedata;
	u32			page_count;	/* total remappable size */
	dma_addr_t		iovmm_base;	/* offset to vmm_area */
	spinlock_t		pte_lock;	/* for pagetable */
	struct list_head	client;
	spinlock_t		client_lock;	/* for client list */
	struct device		*dev;

	struct iommu_device	iommu;		/* IOMMU Core handle */
};

struct gart_domain {
	struct iommu_domain domain;		/* generic domain handle */
	struct gart_device *gart;		/* link to gart device   */
};

static struct gart_device *gart_handle; /* unique for a system */

static bool gart_debug;

#define GART_PTE(_pfn)						\
	(GART_ENTRY_PHYS_ADDR_VALID | ((_pfn) << PAGE_SHIFT))

static struct gart_domain *to_gart_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct gart_domain, domain);
}

/*
 * Any interaction between any block on PPSB and a block on APB or AHB
 * must have these read-back to ensure the APB/AHB bus transaction is
 * complete before initiating activity on the PPSB block.
 */
#define FLUSH_GART_REGS(gart)	((void)readl((gart)->regs + GART_CONFIG))

#define for_each_gart_pte(gart, iova)					\
	for (iova = gart->iovmm_base;					\
	     iova < gart->iovmm_base + GART_PAGE_SIZE * gart->page_count; \
	     iova += GART_PAGE_SIZE)

static inline void gart_set_pte(struct gart_device *gart,
				unsigned long offs, u32 pte)
{
	writel(offs, gart->regs + GART_ENTRY_ADDR);
	writel(pte, gart->regs + GART_ENTRY_DATA);

	dev_dbg(gart->dev, "%s %08lx:%08x\n",
		 pte ? "map" : "unmap", offs, pte & GART_PAGE_MASK);
}

static inline unsigned long gart_read_pte(struct gart_device *gart,
					  unsigned long offs)
{
	unsigned long pte;

	writel(offs, gart->regs + GART_ENTRY_ADDR);
	pte = readl(gart->regs + GART_ENTRY_DATA);

	return pte;
}

static void do_gart_setup(struct gart_device *gart, const u32 *data)
{
	unsigned long iova;

	for_each_gart_pte(gart, iova)
		gart_set_pte(gart, iova, data ? *(data++) : 0);

	writel(1, gart->regs + GART_CONFIG);
	FLUSH_GART_REGS(gart);
}

#ifdef DEBUG
static void gart_dump_table(struct gart_device *gart)
{
	unsigned long iova;
	unsigned long flags;

	spin_lock_irqsave(&gart->pte_lock, flags);
	for_each_gart_pte(gart, iova) {
		unsigned long pte;

		pte = gart_read_pte(gart, iova);

		dev_dbg(gart->dev, "%s %08lx:%08lx\n",
			(GART_ENTRY_PHYS_ADDR_VALID & pte) ? "v" : " ",
			iova, pte & GART_PAGE_MASK);
	}
	spin_unlock_irqrestore(&gart->pte_lock, flags);
}
#else
static inline void gart_dump_table(struct gart_device *gart)
{
}
#endif

static inline bool gart_iova_range_valid(struct gart_device *gart,
					 unsigned long iova, size_t bytes)
{
	unsigned long iova_start, iova_end, gart_start, gart_end;

	iova_start = iova;
	iova_end = iova_start + bytes - 1;
	gart_start = gart->iovmm_base;
	gart_end = gart_start + gart->page_count * GART_PAGE_SIZE - 1;

	if (iova_start < gart_start)
		return false;
	if (iova_end > gart_end)
		return false;
	return true;
}

static int gart_iommu_attach_dev(struct iommu_domain *domain,
				 struct device *dev)
{
	struct gart_domain *gart_domain = to_gart_domain(domain);
	struct gart_device *gart = gart_domain->gart;
	struct gart_client *client, *c;
	int err = 0;

	client = devm_kzalloc(gart->dev, sizeof(*c), GFP_KERNEL);
	if (!client)
		return -ENOMEM;
	client->dev = dev;

	spin_lock(&gart->client_lock);
	list_for_each_entry(c, &gart->client, list) {
		if (c->dev == dev) {
			dev_err(gart->dev,
				"%s is already attached\n", dev_name(dev));
			err = -EINVAL;
			goto fail;
		}
	}
	list_add(&client->list, &gart->client);
	spin_unlock(&gart->client_lock);
	dev_dbg(gart->dev, "Attached %s\n", dev_name(dev));
	return 0;

fail:
	devm_kfree(gart->dev, client);
	spin_unlock(&gart->client_lock);
	return err;
}

static void gart_iommu_detach_dev(struct iommu_domain *domain,
				  struct device *dev)
{
	struct gart_domain *gart_domain = to_gart_domain(domain);
	struct gart_device *gart = gart_domain->gart;
	struct gart_client *c;

	spin_lock(&gart->client_lock);

	list_for_each_entry(c, &gart->client, list) {
		if (c->dev == dev) {
			list_del(&c->list);
			devm_kfree(gart->dev, c);
			dev_dbg(gart->dev, "Detached %s\n", dev_name(dev));
			goto out;
		}
	}
	dev_err(gart->dev, "Couldn't find\n");
out:
	spin_unlock(&gart->client_lock);
}

static struct iommu_domain *gart_iommu_domain_alloc(unsigned type)
{
	struct gart_domain *gart_domain;
	struct gart_device *gart;

	if (type != IOMMU_DOMAIN_UNMANAGED)
		return NULL;

	gart = gart_handle;
	if (!gart)
		return NULL;

	gart_domain = kzalloc(sizeof(*gart_domain), GFP_KERNEL);
	if (!gart_domain)
		return NULL;

	gart_domain->gart = gart;
	gart_domain->domain.geometry.aperture_start = gart->iovmm_base;
	gart_domain->domain.geometry.aperture_end = gart->iovmm_base +
					gart->page_count * GART_PAGE_SIZE - 1;
	gart_domain->domain.geometry.force_aperture = true;

	return &gart_domain->domain;
}

static void gart_iommu_domain_free(struct iommu_domain *domain)
{
	struct gart_domain *gart_domain = to_gart_domain(domain);
	struct gart_device *gart = gart_domain->gart;

	if (gart) {
		spin_lock(&gart->client_lock);
		if (!list_empty(&gart->client)) {
			struct gart_client *c;

			list_for_each_entry(c, &gart->client, list)
				gart_iommu_detach_dev(domain, c->dev);
		}
		spin_unlock(&gart->client_lock);
	}

	kfree(gart_domain);
}

static int gart_iommu_map(struct iommu_domain *domain, unsigned long iova,
			  phys_addr_t pa, size_t bytes, int prot)
{
	struct gart_domain *gart_domain = to_gart_domain(domain);
	struct gart_device *gart = gart_domain->gart;
	unsigned long flags;
	unsigned long pfn;
	unsigned long pte;

	if (!gart_iova_range_valid(gart, iova, bytes))
		return -EINVAL;

	spin_lock_irqsave(&gart->pte_lock, flags);
	pfn = __phys_to_pfn(pa);
	if (!pfn_valid(pfn)) {
		dev_err(gart->dev, "Invalid page: %pa\n", &pa);
		spin_unlock_irqrestore(&gart->pte_lock, flags);
		return -EINVAL;
	}
	if (gart_debug) {
		pte = gart_read_pte(gart, iova);
		if (pte & GART_ENTRY_PHYS_ADDR_VALID) {
			spin_unlock_irqrestore(&gart->pte_lock, flags);
			dev_err(gart->dev, "Page entry is in-use\n");
			return -EBUSY;
		}
	}
	gart_set_pte(gart, iova, GART_PTE(pfn));
	FLUSH_GART_REGS(gart);
	spin_unlock_irqrestore(&gart->pte_lock, flags);
	return 0;
}

static size_t gart_iommu_unmap(struct iommu_domain *domain, unsigned long iova,
			       size_t bytes)
{
	struct gart_domain *gart_domain = to_gart_domain(domain);
	struct gart_device *gart = gart_domain->gart;
	unsigned long flags;

	if (!gart_iova_range_valid(gart, iova, bytes))
		return 0;

	spin_lock_irqsave(&gart->pte_lock, flags);
	gart_set_pte(gart, iova, 0);
	FLUSH_GART_REGS(gart);
	spin_unlock_irqrestore(&gart->pte_lock, flags);
	return bytes;
}

static phys_addr_t gart_iommu_iova_to_phys(struct iommu_domain *domain,
					   dma_addr_t iova)
{
	struct gart_domain *gart_domain = to_gart_domain(domain);
	struct gart_device *gart = gart_domain->gart;
	unsigned long pte;
	phys_addr_t pa;
	unsigned long flags;

	if (!gart_iova_range_valid(gart, iova, 0))
		return -EINVAL;

	spin_lock_irqsave(&gart->pte_lock, flags);
	pte = gart_read_pte(gart, iova);
	spin_unlock_irqrestore(&gart->pte_lock, flags);

	pa = (pte & GART_PAGE_MASK);
	if (!pfn_valid(__phys_to_pfn(pa))) {
		dev_err(gart->dev, "No entry for %08llx:%pa\n",
			 (unsigned long long)iova, &pa);
		gart_dump_table(gart);
		return -EINVAL;
	}
	return pa;
}

static bool gart_iommu_capable(enum iommu_cap cap)
{
	return false;
}

static int gart_iommu_add_device(struct device *dev)
{
	struct iommu_group *group = iommu_group_get_for_dev(dev);

	if (IS_ERR(group))
		return PTR_ERR(group);

	iommu_group_put(group);

	iommu_device_link(&gart_handle->iommu, dev);

	return 0;
}

static void gart_iommu_remove_device(struct device *dev)
{
	iommu_group_remove_device(dev);
	iommu_device_unlink(&gart_handle->iommu, dev);
}

static const struct iommu_ops gart_iommu_ops = {
	.capable	= gart_iommu_capable,
	.domain_alloc	= gart_iommu_domain_alloc,
	.domain_free	= gart_iommu_domain_free,
	.attach_dev	= gart_iommu_attach_dev,
	.detach_dev	= gart_iommu_detach_dev,
	.add_device	= gart_iommu_add_device,
	.remove_device	= gart_iommu_remove_device,
	.device_group	= generic_device_group,
	.map		= gart_iommu_map,
	.unmap		= gart_iommu_unmap,
	.iova_to_phys	= gart_iommu_iova_to_phys,
	.pgsize_bitmap	= GART_IOMMU_PGSIZES,
};

static int tegra_gart_suspend(struct device *dev)
{
	struct gart_device *gart = dev_get_drvdata(dev);
	unsigned long iova;
	u32 *data = gart->savedata;
	unsigned long flags;

	spin_lock_irqsave(&gart->pte_lock, flags);
	for_each_gart_pte(gart, iova)
		*(data++) = gart_read_pte(gart, iova);
	spin_unlock_irqrestore(&gart->pte_lock, flags);
	return 0;
}

static int tegra_gart_resume(struct device *dev)
{
	struct gart_device *gart = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&gart->pte_lock, flags);
	do_gart_setup(gart, gart->savedata);
	spin_unlock_irqrestore(&gart->pte_lock, flags);
	return 0;
}

static int tegra_gart_probe(struct platform_device *pdev)
{
	struct gart_device *gart;
	struct resource *res, *res_remap;
	void __iomem *gart_regs;
	struct device *dev = &pdev->dev;
	int ret;

	if (gart_handle)
		return -EIO;

	BUILD_BUG_ON(PAGE_SHIFT != GART_PAGE_SHIFT);

	/* the GART memory aperture is required */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	res_remap = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res || !res_remap) {
		dev_err(dev, "GART memory aperture expected\n");
		return -ENXIO;
	}

	gart = devm_kzalloc(dev, sizeof(*gart), GFP_KERNEL);
	if (!gart) {
		dev_err(dev, "failed to allocate gart_device\n");
		return -ENOMEM;
	}

	gart_regs = devm_ioremap(dev, res->start, resource_size(res));
	if (!gart_regs) {
		dev_err(dev, "failed to remap GART registers\n");
		return -ENXIO;
	}

	ret = iommu_device_sysfs_add(&gart->iommu, &pdev->dev, NULL,
				     dev_name(&pdev->dev));
	if (ret) {
		dev_err(dev, "Failed to register IOMMU in sysfs\n");
		return ret;
	}

	iommu_device_set_ops(&gart->iommu, &gart_iommu_ops);

	ret = iommu_device_register(&gart->iommu);
	if (ret) {
		dev_err(dev, "Failed to register IOMMU\n");
		iommu_device_sysfs_remove(&gart->iommu);
		return ret;
	}

	gart->dev = &pdev->dev;
	spin_lock_init(&gart->pte_lock);
	spin_lock_init(&gart->client_lock);
	INIT_LIST_HEAD(&gart->client);
	gart->regs = gart_regs;
	gart->iovmm_base = (dma_addr_t)res_remap->start;
	gart->page_count = (resource_size(res_remap) >> GART_PAGE_SHIFT);

	gart->savedata = vmalloc(array_size(sizeof(u32), gart->page_count));
	if (!gart->savedata) {
		dev_err(dev, "failed to allocate context save area\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, gart);
	do_gart_setup(gart, NULL);

	gart_handle = gart;

	return 0;
}

static int tegra_gart_remove(struct platform_device *pdev)
{
	struct gart_device *gart = platform_get_drvdata(pdev);

	iommu_device_unregister(&gart->iommu);
	iommu_device_sysfs_remove(&gart->iommu);

	writel(0, gart->regs + GART_CONFIG);
	if (gart->savedata)
		vfree(gart->savedata);
	gart_handle = NULL;
	return 0;
}

static const struct dev_pm_ops tegra_gart_pm_ops = {
	.suspend	= tegra_gart_suspend,
	.resume		= tegra_gart_resume,
};

static const struct of_device_id tegra_gart_of_match[] = {
	{ .compatible = "nvidia,tegra20-gart", },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_gart_of_match);

static struct platform_driver tegra_gart_driver = {
	.probe		= tegra_gart_probe,
	.remove		= tegra_gart_remove,
	.driver = {
		.name	= "tegra-gart",
		.pm	= &tegra_gart_pm_ops,
		.of_match_table = tegra_gart_of_match,
	},
};

static int tegra_gart_init(void)
{
	return platform_driver_register(&tegra_gart_driver);
}

static void __exit tegra_gart_exit(void)
{
	platform_driver_unregister(&tegra_gart_driver);
}

subsys_initcall(tegra_gart_init);
module_exit(tegra_gart_exit);
module_param(gart_debug, bool, 0644);

MODULE_PARM_DESC(gart_debug, "Enable GART debugging");
MODULE_DESCRIPTION("IOMMU API for GART in Tegra20");
MODULE_AUTHOR("Hiroshi DOYU <hdoyu@nvidia.com>");
MODULE_ALIAS("platform:tegra-gart");
MODULE_LICENSE("GPL v2");
