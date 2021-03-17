// SPDX-License-Identifier: GPL-2.0-only
/*
 * omap iommu: tlb and pagetable primitives
 *
 * Copyright (C) 2008-2010 Nokia Corporation
 * Copyright (C) 2013-2017 Texas Instruments Incorporated - https://www.ti.com/
 *
 * Written by Hiroshi DOYU <Hiroshi.DOYU@nokia.com>,
 *		Paul Mundt and Toshihiro Kobayashi
 */

#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/iommu.h>
#include <linux/omap-iommu.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_iommu.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include <linux/platform_data/iommu-omap.h>

#include "omap-iopgtable.h"
#include "omap-iommu.h"

static const struct iommu_ops omap_iommu_ops;

#define to_iommu(dev)	((struct omap_iommu *)dev_get_drvdata(dev))

/* bitmap of the page sizes currently supported */
#define OMAP_IOMMU_PGSIZES	(SZ_4K | SZ_64K | SZ_1M | SZ_16M)

#define MMU_LOCK_BASE_SHIFT	10
#define MMU_LOCK_BASE_MASK	(0x1f << MMU_LOCK_BASE_SHIFT)
#define MMU_LOCK_BASE(x)	\
	((x & MMU_LOCK_BASE_MASK) >> MMU_LOCK_BASE_SHIFT)

#define MMU_LOCK_VICT_SHIFT	4
#define MMU_LOCK_VICT_MASK	(0x1f << MMU_LOCK_VICT_SHIFT)
#define MMU_LOCK_VICT(x)	\
	((x & MMU_LOCK_VICT_MASK) >> MMU_LOCK_VICT_SHIFT)

static struct platform_driver omap_iommu_driver;
static struct kmem_cache *iopte_cachep;

/**
 * to_omap_domain - Get struct omap_iommu_domain from generic iommu_domain
 * @dom:	generic iommu domain handle
 **/
static struct omap_iommu_domain *to_omap_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct omap_iommu_domain, domain);
}

/**
 * omap_iommu_save_ctx - Save registers for pm off-mode support
 * @dev:	client device
 *
 * This should be treated as an deprecated API. It is preserved only
 * to maintain existing functionality for OMAP3 ISP driver.
 **/
void omap_iommu_save_ctx(struct device *dev)
{
	struct omap_iommu_arch_data *arch_data = dev_iommu_priv_get(dev);
	struct omap_iommu *obj;
	u32 *p;
	int i;

	if (!arch_data)
		return;

	while (arch_data->iommu_dev) {
		obj = arch_data->iommu_dev;
		p = obj->ctx;
		for (i = 0; i < (MMU_REG_SIZE / sizeof(u32)); i++) {
			p[i] = iommu_read_reg(obj, i * sizeof(u32));
			dev_dbg(obj->dev, "%s\t[%02d] %08x\n", __func__, i,
				p[i]);
		}
		arch_data++;
	}
}
EXPORT_SYMBOL_GPL(omap_iommu_save_ctx);

/**
 * omap_iommu_restore_ctx - Restore registers for pm off-mode support
 * @dev:	client device
 *
 * This should be treated as an deprecated API. It is preserved only
 * to maintain existing functionality for OMAP3 ISP driver.
 **/
void omap_iommu_restore_ctx(struct device *dev)
{
	struct omap_iommu_arch_data *arch_data = dev_iommu_priv_get(dev);
	struct omap_iommu *obj;
	u32 *p;
	int i;

	if (!arch_data)
		return;

	while (arch_data->iommu_dev) {
		obj = arch_data->iommu_dev;
		p = obj->ctx;
		for (i = 0; i < (MMU_REG_SIZE / sizeof(u32)); i++) {
			iommu_write_reg(obj, p[i], i * sizeof(u32));
			dev_dbg(obj->dev, "%s\t[%02d] %08x\n", __func__, i,
				p[i]);
		}
		arch_data++;
	}
}
EXPORT_SYMBOL_GPL(omap_iommu_restore_ctx);

static void dra7_cfg_dspsys_mmu(struct omap_iommu *obj, bool enable)
{
	u32 val, mask;

	if (!obj->syscfg)
		return;

	mask = (1 << (obj->id * DSP_SYS_MMU_CONFIG_EN_SHIFT));
	val = enable ? mask : 0;
	regmap_update_bits(obj->syscfg, DSP_SYS_MMU_CONFIG, mask, val);
}

static void __iommu_set_twl(struct omap_iommu *obj, bool on)
{
	u32 l = iommu_read_reg(obj, MMU_CNTL);

	if (on)
		iommu_write_reg(obj, MMU_IRQ_TWL_MASK, MMU_IRQENABLE);
	else
		iommu_write_reg(obj, MMU_IRQ_TLB_MISS_MASK, MMU_IRQENABLE);

	l &= ~MMU_CNTL_MASK;
	if (on)
		l |= (MMU_CNTL_MMU_EN | MMU_CNTL_TWL_EN);
	else
		l |= (MMU_CNTL_MMU_EN);

	iommu_write_reg(obj, l, MMU_CNTL);
}

static int omap2_iommu_enable(struct omap_iommu *obj)
{
	u32 l, pa;

	if (!obj->iopgd || !IS_ALIGNED((unsigned long)obj->iopgd,  SZ_16K))
		return -EINVAL;

	pa = virt_to_phys(obj->iopgd);
	if (!IS_ALIGNED(pa, SZ_16K))
		return -EINVAL;

	l = iommu_read_reg(obj, MMU_REVISION);
	dev_info(obj->dev, "%s: version %d.%d\n", obj->name,
		 (l >> 4) & 0xf, l & 0xf);

	iommu_write_reg(obj, pa, MMU_TTB);

	dra7_cfg_dspsys_mmu(obj, true);

	if (obj->has_bus_err_back)
		iommu_write_reg(obj, MMU_GP_REG_BUS_ERR_BACK_EN, MMU_GP_REG);

	__iommu_set_twl(obj, true);

	return 0;
}

static void omap2_iommu_disable(struct omap_iommu *obj)
{
	u32 l = iommu_read_reg(obj, MMU_CNTL);

	l &= ~MMU_CNTL_MASK;
	iommu_write_reg(obj, l, MMU_CNTL);
	dra7_cfg_dspsys_mmu(obj, false);

	dev_dbg(obj->dev, "%s is shutting down\n", obj->name);
}

static int iommu_enable(struct omap_iommu *obj)
{
	int ret;

	ret = pm_runtime_get_sync(obj->dev);
	if (ret < 0)
		pm_runtime_put_noidle(obj->dev);

	return ret < 0 ? ret : 0;
}

static void iommu_disable(struct omap_iommu *obj)
{
	pm_runtime_put_sync(obj->dev);
}

/*
 *	TLB operations
 */
static u32 iotlb_cr_to_virt(struct cr_regs *cr)
{
	u32 page_size = cr->cam & MMU_CAM_PGSZ_MASK;
	u32 mask = get_cam_va_mask(cr->cam & page_size);

	return cr->cam & mask;
}

static u32 get_iopte_attr(struct iotlb_entry *e)
{
	u32 attr;

	attr = e->mixed << 5;
	attr |= e->endian;
	attr |= e->elsz >> 3;
	attr <<= (((e->pgsz == MMU_CAM_PGSZ_4K) ||
			(e->pgsz == MMU_CAM_PGSZ_64K)) ? 0 : 6);
	return attr;
}

static u32 iommu_report_fault(struct omap_iommu *obj, u32 *da)
{
	u32 status, fault_addr;

	status = iommu_read_reg(obj, MMU_IRQSTATUS);
	status &= MMU_IRQ_MASK;
	if (!status) {
		*da = 0;
		return 0;
	}

	fault_addr = iommu_read_reg(obj, MMU_FAULT_AD);
	*da = fault_addr;

	iommu_write_reg(obj, status, MMU_IRQSTATUS);

	return status;
}

void iotlb_lock_get(struct omap_iommu *obj, struct iotlb_lock *l)
{
	u32 val;

	val = iommu_read_reg(obj, MMU_LOCK);

	l->base = MMU_LOCK_BASE(val);
	l->vict = MMU_LOCK_VICT(val);
}

void iotlb_lock_set(struct omap_iommu *obj, struct iotlb_lock *l)
{
	u32 val;

	val = (l->base << MMU_LOCK_BASE_SHIFT);
	val |= (l->vict << MMU_LOCK_VICT_SHIFT);

	iommu_write_reg(obj, val, MMU_LOCK);
}

static void iotlb_read_cr(struct omap_iommu *obj, struct cr_regs *cr)
{
	cr->cam = iommu_read_reg(obj, MMU_READ_CAM);
	cr->ram = iommu_read_reg(obj, MMU_READ_RAM);
}

static void iotlb_load_cr(struct omap_iommu *obj, struct cr_regs *cr)
{
	iommu_write_reg(obj, cr->cam | MMU_CAM_V, MMU_CAM);
	iommu_write_reg(obj, cr->ram, MMU_RAM);

	iommu_write_reg(obj, 1, MMU_FLUSH_ENTRY);
	iommu_write_reg(obj, 1, MMU_LD_TLB);
}

/* only used in iotlb iteration for-loop */
struct cr_regs __iotlb_read_cr(struct omap_iommu *obj, int n)
{
	struct cr_regs cr;
	struct iotlb_lock l;

	iotlb_lock_get(obj, &l);
	l.vict = n;
	iotlb_lock_set(obj, &l);
	iotlb_read_cr(obj, &cr);

	return cr;
}

#ifdef PREFETCH_IOTLB
static struct cr_regs *iotlb_alloc_cr(struct omap_iommu *obj,
				      struct iotlb_entry *e)
{
	struct cr_regs *cr;

	if (!e)
		return NULL;

	if (e->da & ~(get_cam_va_mask(e->pgsz))) {
		dev_err(obj->dev, "%s:\twrong alignment: %08x\n", __func__,
			e->da);
		return ERR_PTR(-EINVAL);
	}

	cr = kmalloc(sizeof(*cr), GFP_KERNEL);
	if (!cr)
		return ERR_PTR(-ENOMEM);

	cr->cam = (e->da & MMU_CAM_VATAG_MASK) | e->prsvd | e->pgsz | e->valid;
	cr->ram = e->pa | e->endian | e->elsz | e->mixed;

	return cr;
}

/**
 * load_iotlb_entry - Set an iommu tlb entry
 * @obj:	target iommu
 * @e:		an iommu tlb entry info
 **/
static int load_iotlb_entry(struct omap_iommu *obj, struct iotlb_entry *e)
{
	int err = 0;
	struct iotlb_lock l;
	struct cr_regs *cr;

	if (!obj || !obj->nr_tlb_entries || !e)
		return -EINVAL;

	pm_runtime_get_sync(obj->dev);

	iotlb_lock_get(obj, &l);
	if (l.base == obj->nr_tlb_entries) {
		dev_warn(obj->dev, "%s: preserve entries full\n", __func__);
		err = -EBUSY;
		goto out;
	}
	if (!e->prsvd) {
		int i;
		struct cr_regs tmp;

		for_each_iotlb_cr(obj, obj->nr_tlb_entries, i, tmp)
			if (!iotlb_cr_valid(&tmp))
				break;

		if (i == obj->nr_tlb_entries) {
			dev_dbg(obj->dev, "%s: full: no entry\n", __func__);
			err = -EBUSY;
			goto out;
		}

		iotlb_lock_get(obj, &l);
	} else {
		l.vict = l.base;
		iotlb_lock_set(obj, &l);
	}

	cr = iotlb_alloc_cr(obj, e);
	if (IS_ERR(cr)) {
		pm_runtime_put_sync(obj->dev);
		return PTR_ERR(cr);
	}

	iotlb_load_cr(obj, cr);
	kfree(cr);

	if (e->prsvd)
		l.base++;
	/* increment victim for next tlb load */
	if (++l.vict == obj->nr_tlb_entries)
		l.vict = l.base;
	iotlb_lock_set(obj, &l);
out:
	pm_runtime_put_sync(obj->dev);
	return err;
}

#else /* !PREFETCH_IOTLB */

static int load_iotlb_entry(struct omap_iommu *obj, struct iotlb_entry *e)
{
	return 0;
}

#endif /* !PREFETCH_IOTLB */

static int prefetch_iotlb_entry(struct omap_iommu *obj, struct iotlb_entry *e)
{
	return load_iotlb_entry(obj, e);
}

/**
 * flush_iotlb_page - Clear an iommu tlb entry
 * @obj:	target iommu
 * @da:		iommu device virtual address
 *
 * Clear an iommu tlb entry which includes 'da' address.
 **/
static void flush_iotlb_page(struct omap_iommu *obj, u32 da)
{
	int i;
	struct cr_regs cr;

	pm_runtime_get_sync(obj->dev);

	for_each_iotlb_cr(obj, obj->nr_tlb_entries, i, cr) {
		u32 start;
		size_t bytes;

		if (!iotlb_cr_valid(&cr))
			continue;

		start = iotlb_cr_to_virt(&cr);
		bytes = iopgsz_to_bytes(cr.cam & 3);

		if ((start <= da) && (da < start + bytes)) {
			dev_dbg(obj->dev, "%s: %08x<=%08x(%zx)\n",
				__func__, start, da, bytes);
			iotlb_load_cr(obj, &cr);
			iommu_write_reg(obj, 1, MMU_FLUSH_ENTRY);
			break;
		}
	}
	pm_runtime_put_sync(obj->dev);

	if (i == obj->nr_tlb_entries)
		dev_dbg(obj->dev, "%s: no page for %08x\n", __func__, da);
}

/**
 * flush_iotlb_all - Clear all iommu tlb entries
 * @obj:	target iommu
 **/
static void flush_iotlb_all(struct omap_iommu *obj)
{
	struct iotlb_lock l;

	pm_runtime_get_sync(obj->dev);

	l.base = 0;
	l.vict = 0;
	iotlb_lock_set(obj, &l);

	iommu_write_reg(obj, 1, MMU_GFLUSH);

	pm_runtime_put_sync(obj->dev);
}

/*
 *	H/W pagetable operations
 */
static void flush_iopte_range(struct device *dev, dma_addr_t dma,
			      unsigned long offset, int num_entries)
{
	size_t size = num_entries * sizeof(u32);

	dma_sync_single_range_for_device(dev, dma, offset, size, DMA_TO_DEVICE);
}

static void iopte_free(struct omap_iommu *obj, u32 *iopte, bool dma_valid)
{
	dma_addr_t pt_dma;

	/* Note: freed iopte's must be clean ready for re-use */
	if (iopte) {
		if (dma_valid) {
			pt_dma = virt_to_phys(iopte);
			dma_unmap_single(obj->dev, pt_dma, IOPTE_TABLE_SIZE,
					 DMA_TO_DEVICE);
		}

		kmem_cache_free(iopte_cachep, iopte);
	}
}

static u32 *iopte_alloc(struct omap_iommu *obj, u32 *iopgd,
			dma_addr_t *pt_dma, u32 da)
{
	u32 *iopte;
	unsigned long offset = iopgd_index(da) * sizeof(da);

	/* a table has already existed */
	if (*iopgd)
		goto pte_ready;

	/*
	 * do the allocation outside the page table lock
	 */
	spin_unlock(&obj->page_table_lock);
	iopte = kmem_cache_zalloc(iopte_cachep, GFP_KERNEL);
	spin_lock(&obj->page_table_lock);

	if (!*iopgd) {
		if (!iopte)
			return ERR_PTR(-ENOMEM);

		*pt_dma = dma_map_single(obj->dev, iopte, IOPTE_TABLE_SIZE,
					 DMA_TO_DEVICE);
		if (dma_mapping_error(obj->dev, *pt_dma)) {
			dev_err(obj->dev, "DMA map error for L2 table\n");
			iopte_free(obj, iopte, false);
			return ERR_PTR(-ENOMEM);
		}

		/*
		 * we rely on dma address and the physical address to be
		 * the same for mapping the L2 table
		 */
		if (WARN_ON(*pt_dma != virt_to_phys(iopte))) {
			dev_err(obj->dev, "DMA translation error for L2 table\n");
			dma_unmap_single(obj->dev, *pt_dma, IOPTE_TABLE_SIZE,
					 DMA_TO_DEVICE);
			iopte_free(obj, iopte, false);
			return ERR_PTR(-ENOMEM);
		}

		*iopgd = virt_to_phys(iopte) | IOPGD_TABLE;

		flush_iopte_range(obj->dev, obj->pd_dma, offset, 1);
		dev_vdbg(obj->dev, "%s: a new pte:%p\n", __func__, iopte);
	} else {
		/* We raced, free the reduniovant table */
		iopte_free(obj, iopte, false);
	}

pte_ready:
	iopte = iopte_offset(iopgd, da);
	*pt_dma = iopgd_page_paddr(iopgd);
	dev_vdbg(obj->dev,
		 "%s: da:%08x pgd:%p *pgd:%08x pte:%p *pte:%08x\n",
		 __func__, da, iopgd, *iopgd, iopte, *iopte);

	return iopte;
}

static int iopgd_alloc_section(struct omap_iommu *obj, u32 da, u32 pa, u32 prot)
{
	u32 *iopgd = iopgd_offset(obj, da);
	unsigned long offset = iopgd_index(da) * sizeof(da);

	if ((da | pa) & ~IOSECTION_MASK) {
		dev_err(obj->dev, "%s: %08x:%08x should aligned on %08lx\n",
			__func__, da, pa, IOSECTION_SIZE);
		return -EINVAL;
	}

	*iopgd = (pa & IOSECTION_MASK) | prot | IOPGD_SECTION;
	flush_iopte_range(obj->dev, obj->pd_dma, offset, 1);
	return 0;
}

static int iopgd_alloc_super(struct omap_iommu *obj, u32 da, u32 pa, u32 prot)
{
	u32 *iopgd = iopgd_offset(obj, da);
	unsigned long offset = iopgd_index(da) * sizeof(da);
	int i;

	if ((da | pa) & ~IOSUPER_MASK) {
		dev_err(obj->dev, "%s: %08x:%08x should aligned on %08lx\n",
			__func__, da, pa, IOSUPER_SIZE);
		return -EINVAL;
	}

	for (i = 0; i < 16; i++)
		*(iopgd + i) = (pa & IOSUPER_MASK) | prot | IOPGD_SUPER;
	flush_iopte_range(obj->dev, obj->pd_dma, offset, 16);
	return 0;
}

static int iopte_alloc_page(struct omap_iommu *obj, u32 da, u32 pa, u32 prot)
{
	u32 *iopgd = iopgd_offset(obj, da);
	dma_addr_t pt_dma;
	u32 *iopte = iopte_alloc(obj, iopgd, &pt_dma, da);
	unsigned long offset = iopte_index(da) * sizeof(da);

	if (IS_ERR(iopte))
		return PTR_ERR(iopte);

	*iopte = (pa & IOPAGE_MASK) | prot | IOPTE_SMALL;
	flush_iopte_range(obj->dev, pt_dma, offset, 1);

	dev_vdbg(obj->dev, "%s: da:%08x pa:%08x pte:%p *pte:%08x\n",
		 __func__, da, pa, iopte, *iopte);

	return 0;
}

static int iopte_alloc_large(struct omap_iommu *obj, u32 da, u32 pa, u32 prot)
{
	u32 *iopgd = iopgd_offset(obj, da);
	dma_addr_t pt_dma;
	u32 *iopte = iopte_alloc(obj, iopgd, &pt_dma, da);
	unsigned long offset = iopte_index(da) * sizeof(da);
	int i;

	if ((da | pa) & ~IOLARGE_MASK) {
		dev_err(obj->dev, "%s: %08x:%08x should aligned on %08lx\n",
			__func__, da, pa, IOLARGE_SIZE);
		return -EINVAL;
	}

	if (IS_ERR(iopte))
		return PTR_ERR(iopte);

	for (i = 0; i < 16; i++)
		*(iopte + i) = (pa & IOLARGE_MASK) | prot | IOPTE_LARGE;
	flush_iopte_range(obj->dev, pt_dma, offset, 16);
	return 0;
}

static int
iopgtable_store_entry_core(struct omap_iommu *obj, struct iotlb_entry *e)
{
	int (*fn)(struct omap_iommu *, u32, u32, u32);
	u32 prot;
	int err;

	if (!obj || !e)
		return -EINVAL;

	switch (e->pgsz) {
	case MMU_CAM_PGSZ_16M:
		fn = iopgd_alloc_super;
		break;
	case MMU_CAM_PGSZ_1M:
		fn = iopgd_alloc_section;
		break;
	case MMU_CAM_PGSZ_64K:
		fn = iopte_alloc_large;
		break;
	case MMU_CAM_PGSZ_4K:
		fn = iopte_alloc_page;
		break;
	default:
		fn = NULL;
		break;
	}

	if (WARN_ON(!fn))
		return -EINVAL;

	prot = get_iopte_attr(e);

	spin_lock(&obj->page_table_lock);
	err = fn(obj, e->da, e->pa, prot);
	spin_unlock(&obj->page_table_lock);

	return err;
}

/**
 * omap_iopgtable_store_entry - Make an iommu pte entry
 * @obj:	target iommu
 * @e:		an iommu tlb entry info
 **/
static int
omap_iopgtable_store_entry(struct omap_iommu *obj, struct iotlb_entry *e)
{
	int err;

	flush_iotlb_page(obj, e->da);
	err = iopgtable_store_entry_core(obj, e);
	if (!err)
		prefetch_iotlb_entry(obj, e);
	return err;
}

/**
 * iopgtable_lookup_entry - Lookup an iommu pte entry
 * @obj:	target iommu
 * @da:		iommu device virtual address
 * @ppgd:	iommu pgd entry pointer to be returned
 * @ppte:	iommu pte entry pointer to be returned
 **/
static void
iopgtable_lookup_entry(struct omap_iommu *obj, u32 da, u32 **ppgd, u32 **ppte)
{
	u32 *iopgd, *iopte = NULL;

	iopgd = iopgd_offset(obj, da);
	if (!*iopgd)
		goto out;

	if (iopgd_is_table(*iopgd))
		iopte = iopte_offset(iopgd, da);
out:
	*ppgd = iopgd;
	*ppte = iopte;
}

static size_t iopgtable_clear_entry_core(struct omap_iommu *obj, u32 da)
{
	size_t bytes;
	u32 *iopgd = iopgd_offset(obj, da);
	int nent = 1;
	dma_addr_t pt_dma;
	unsigned long pd_offset = iopgd_index(da) * sizeof(da);
	unsigned long pt_offset = iopte_index(da) * sizeof(da);

	if (!*iopgd)
		return 0;

	if (iopgd_is_table(*iopgd)) {
		int i;
		u32 *iopte = iopte_offset(iopgd, da);

		bytes = IOPTE_SIZE;
		if (*iopte & IOPTE_LARGE) {
			nent *= 16;
			/* rewind to the 1st entry */
			iopte = iopte_offset(iopgd, (da & IOLARGE_MASK));
		}
		bytes *= nent;
		memset(iopte, 0, nent * sizeof(*iopte));
		pt_dma = iopgd_page_paddr(iopgd);
		flush_iopte_range(obj->dev, pt_dma, pt_offset, nent);

		/*
		 * do table walk to check if this table is necessary or not
		 */
		iopte = iopte_offset(iopgd, 0);
		for (i = 0; i < PTRS_PER_IOPTE; i++)
			if (iopte[i])
				goto out;

		iopte_free(obj, iopte, true);
		nent = 1; /* for the next L1 entry */
	} else {
		bytes = IOPGD_SIZE;
		if ((*iopgd & IOPGD_SUPER) == IOPGD_SUPER) {
			nent *= 16;
			/* rewind to the 1st entry */
			iopgd = iopgd_offset(obj, (da & IOSUPER_MASK));
		}
		bytes *= nent;
	}
	memset(iopgd, 0, nent * sizeof(*iopgd));
	flush_iopte_range(obj->dev, obj->pd_dma, pd_offset, nent);
out:
	return bytes;
}

/**
 * iopgtable_clear_entry - Remove an iommu pte entry
 * @obj:	target iommu
 * @da:		iommu device virtual address
 **/
static size_t iopgtable_clear_entry(struct omap_iommu *obj, u32 da)
{
	size_t bytes;

	spin_lock(&obj->page_table_lock);

	bytes = iopgtable_clear_entry_core(obj, da);
	flush_iotlb_page(obj, da);

	spin_unlock(&obj->page_table_lock);

	return bytes;
}

static void iopgtable_clear_entry_all(struct omap_iommu *obj)
{
	unsigned long offset;
	int i;

	spin_lock(&obj->page_table_lock);

	for (i = 0; i < PTRS_PER_IOPGD; i++) {
		u32 da;
		u32 *iopgd;

		da = i << IOPGD_SHIFT;
		iopgd = iopgd_offset(obj, da);
		offset = iopgd_index(da) * sizeof(da);

		if (!*iopgd)
			continue;

		if (iopgd_is_table(*iopgd))
			iopte_free(obj, iopte_offset(iopgd, 0), true);

		*iopgd = 0;
		flush_iopte_range(obj->dev, obj->pd_dma, offset, 1);
	}

	flush_iotlb_all(obj);

	spin_unlock(&obj->page_table_lock);
}

/*
 *	Device IOMMU generic operations
 */
static irqreturn_t iommu_fault_handler(int irq, void *data)
{
	u32 da, errs;
	u32 *iopgd, *iopte;
	struct omap_iommu *obj = data;
	struct iommu_domain *domain = obj->domain;
	struct omap_iommu_domain *omap_domain = to_omap_domain(domain);

	if (!omap_domain->dev)
		return IRQ_NONE;

	errs = iommu_report_fault(obj, &da);
	if (errs == 0)
		return IRQ_HANDLED;

	/* Fault callback or TLB/PTE Dynamic loading */
	if (!report_iommu_fault(domain, obj->dev, da, 0))
		return IRQ_HANDLED;

	iommu_write_reg(obj, 0, MMU_IRQENABLE);

	iopgd = iopgd_offset(obj, da);

	if (!iopgd_is_table(*iopgd)) {
		dev_err(obj->dev, "%s: errs:0x%08x da:0x%08x pgd:0x%p *pgd:px%08x\n",
			obj->name, errs, da, iopgd, *iopgd);
		return IRQ_NONE;
	}

	iopte = iopte_offset(iopgd, da);

	dev_err(obj->dev, "%s: errs:0x%08x da:0x%08x pgd:0x%p *pgd:0x%08x pte:0x%p *pte:0x%08x\n",
		obj->name, errs, da, iopgd, *iopgd, iopte, *iopte);

	return IRQ_NONE;
}

/**
 * omap_iommu_attach() - attach iommu device to an iommu domain
 * @obj:	target omap iommu device
 * @iopgd:	page table
 **/
static int omap_iommu_attach(struct omap_iommu *obj, u32 *iopgd)
{
	int err;

	spin_lock(&obj->iommu_lock);

	obj->pd_dma = dma_map_single(obj->dev, iopgd, IOPGD_TABLE_SIZE,
				     DMA_TO_DEVICE);
	if (dma_mapping_error(obj->dev, obj->pd_dma)) {
		dev_err(obj->dev, "DMA map error for L1 table\n");
		err = -ENOMEM;
		goto out_err;
	}

	obj->iopgd = iopgd;
	err = iommu_enable(obj);
	if (err)
		goto out_err;
	flush_iotlb_all(obj);

	spin_unlock(&obj->iommu_lock);

	dev_dbg(obj->dev, "%s: %s\n", __func__, obj->name);

	return 0;

out_err:
	spin_unlock(&obj->iommu_lock);

	return err;
}

/**
 * omap_iommu_detach - release iommu device
 * @obj:	target iommu
 **/
static void omap_iommu_detach(struct omap_iommu *obj)
{
	if (!obj || IS_ERR(obj))
		return;

	spin_lock(&obj->iommu_lock);

	dma_unmap_single(obj->dev, obj->pd_dma, IOPGD_TABLE_SIZE,
			 DMA_TO_DEVICE);
	obj->pd_dma = 0;
	obj->iopgd = NULL;
	iommu_disable(obj);

	spin_unlock(&obj->iommu_lock);

	dev_dbg(obj->dev, "%s: %s\n", __func__, obj->name);
}

static void omap_iommu_save_tlb_entries(struct omap_iommu *obj)
{
	struct iotlb_lock lock;
	struct cr_regs cr;
	struct cr_regs *tmp;
	int i;

	/* check if there are any locked tlbs to save */
	iotlb_lock_get(obj, &lock);
	obj->num_cr_ctx = lock.base;
	if (!obj->num_cr_ctx)
		return;

	tmp = obj->cr_ctx;
	for_each_iotlb_cr(obj, obj->num_cr_ctx, i, cr)
		* tmp++ = cr;
}

static void omap_iommu_restore_tlb_entries(struct omap_iommu *obj)
{
	struct iotlb_lock l;
	struct cr_regs *tmp;
	int i;

	/* no locked tlbs to restore */
	if (!obj->num_cr_ctx)
		return;

	l.base = 0;
	tmp = obj->cr_ctx;
	for (i = 0; i < obj->num_cr_ctx; i++, tmp++) {
		l.vict = i;
		iotlb_lock_set(obj, &l);
		iotlb_load_cr(obj, tmp);
	}
	l.base = obj->num_cr_ctx;
	l.vict = i;
	iotlb_lock_set(obj, &l);
}

/**
 * omap_iommu_domain_deactivate - deactivate attached iommu devices
 * @domain: iommu domain attached to the target iommu device
 *
 * This API allows the client devices of IOMMU devices to suspend
 * the IOMMUs they control at runtime, after they are idled and
 * suspended all activity. System Suspend will leverage the PM
 * driver late callbacks.
 **/
int omap_iommu_domain_deactivate(struct iommu_domain *domain)
{
	struct omap_iommu_domain *omap_domain = to_omap_domain(domain);
	struct omap_iommu_device *iommu;
	struct omap_iommu *oiommu;
	int i;

	if (!omap_domain->dev)
		return 0;

	iommu = omap_domain->iommus;
	iommu += (omap_domain->num_iommus - 1);
	for (i = 0; i < omap_domain->num_iommus; i++, iommu--) {
		oiommu = iommu->iommu_dev;
		pm_runtime_put_sync(oiommu->dev);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(omap_iommu_domain_deactivate);

/**
 * omap_iommu_domain_activate - activate attached iommu devices
 * @domain: iommu domain attached to the target iommu device
 *
 * This API allows the client devices of IOMMU devices to resume the
 * IOMMUs they control at runtime, before they can resume operations.
 * System Resume will leverage the PM driver late callbacks.
 **/
int omap_iommu_domain_activate(struct iommu_domain *domain)
{
	struct omap_iommu_domain *omap_domain = to_omap_domain(domain);
	struct omap_iommu_device *iommu;
	struct omap_iommu *oiommu;
	int i;

	if (!omap_domain->dev)
		return 0;

	iommu = omap_domain->iommus;
	for (i = 0; i < omap_domain->num_iommus; i++, iommu++) {
		oiommu = iommu->iommu_dev;
		pm_runtime_get_sync(oiommu->dev);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(omap_iommu_domain_activate);

/**
 * omap_iommu_runtime_suspend - disable an iommu device
 * @dev:	iommu device
 *
 * This function performs all that is necessary to disable an
 * IOMMU device, either during final detachment from a client
 * device, or during system/runtime suspend of the device. This
 * includes programming all the appropriate IOMMU registers, and
 * managing the associated omap_hwmod's state and the device's
 * reset line. This function also saves the context of any
 * locked TLBs if suspending.
 **/
static __maybe_unused int omap_iommu_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct iommu_platform_data *pdata = dev_get_platdata(dev);
	struct omap_iommu *obj = to_iommu(dev);
	int ret;

	/* save the TLBs only during suspend, and not for power down */
	if (obj->domain && obj->iopgd)
		omap_iommu_save_tlb_entries(obj);

	omap2_iommu_disable(obj);

	if (pdata && pdata->device_idle)
		pdata->device_idle(pdev);

	if (pdata && pdata->assert_reset)
		pdata->assert_reset(pdev, pdata->reset_name);

	if (pdata && pdata->set_pwrdm_constraint) {
		ret = pdata->set_pwrdm_constraint(pdev, false, &obj->pwrst);
		if (ret) {
			dev_warn(obj->dev, "pwrdm_constraint failed to be reset, status = %d\n",
				 ret);
		}
	}

	return 0;
}

/**
 * omap_iommu_runtime_resume - enable an iommu device
 * @dev:	iommu device
 *
 * This function performs all that is necessary to enable an
 * IOMMU device, either during initial attachment to a client
 * device, or during system/runtime resume of the device. This
 * includes programming all the appropriate IOMMU registers, and
 * managing the associated omap_hwmod's state and the device's
 * reset line. The function also restores any locked TLBs if
 * resuming after a suspend.
 **/
static __maybe_unused int omap_iommu_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct iommu_platform_data *pdata = dev_get_platdata(dev);
	struct omap_iommu *obj = to_iommu(dev);
	int ret = 0;

	if (pdata && pdata->set_pwrdm_constraint) {
		ret = pdata->set_pwrdm_constraint(pdev, true, &obj->pwrst);
		if (ret) {
			dev_warn(obj->dev, "pwrdm_constraint failed to be set, status = %d\n",
				 ret);
		}
	}

	if (pdata && pdata->deassert_reset) {
		ret = pdata->deassert_reset(pdev, pdata->reset_name);
		if (ret) {
			dev_err(dev, "deassert_reset failed: %d\n", ret);
			return ret;
		}
	}

	if (pdata && pdata->device_enable)
		pdata->device_enable(pdev);

	/* restore the TLBs only during resume, and not for power up */
	if (obj->domain)
		omap_iommu_restore_tlb_entries(obj);

	ret = omap2_iommu_enable(obj);

	return ret;
}

/**
 * omap_iommu_suspend_prepare - prepare() dev_pm_ops implementation
 * @dev:	iommu device
 *
 * This function performs the necessary checks to determine if the IOMMU
 * device needs suspending or not. The function checks if the runtime_pm
 * status of the device is suspended, and returns 1 in that case. This
 * results in the PM core to skip invoking any of the Sleep PM callbacks
 * (suspend, suspend_late, resume, resume_early etc).
 */
static int omap_iommu_prepare(struct device *dev)
{
	if (pm_runtime_status_suspended(dev))
		return 1;
	return 0;
}

static bool omap_iommu_can_register(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;

	if (!of_device_is_compatible(np, "ti,dra7-dsp-iommu"))
		return true;

	/*
	 * restrict IOMMU core registration only for processor-port MDMA MMUs
	 * on DRA7 DSPs
	 */
	if ((!strcmp(dev_name(&pdev->dev), "40d01000.mmu")) ||
	    (!strcmp(dev_name(&pdev->dev), "41501000.mmu")))
		return true;

	return false;
}

static int omap_iommu_dra7_get_dsp_system_cfg(struct platform_device *pdev,
					      struct omap_iommu *obj)
{
	struct device_node *np = pdev->dev.of_node;
	int ret;

	if (!of_device_is_compatible(np, "ti,dra7-dsp-iommu"))
		return 0;

	if (!of_property_read_bool(np, "ti,syscon-mmuconfig")) {
		dev_err(&pdev->dev, "ti,syscon-mmuconfig property is missing\n");
		return -EINVAL;
	}

	obj->syscfg =
		syscon_regmap_lookup_by_phandle(np, "ti,syscon-mmuconfig");
	if (IS_ERR(obj->syscfg)) {
		/* can fail with -EPROBE_DEFER */
		ret = PTR_ERR(obj->syscfg);
		return ret;
	}

	if (of_property_read_u32_index(np, "ti,syscon-mmuconfig", 1,
				       &obj->id)) {
		dev_err(&pdev->dev, "couldn't get the IOMMU instance id within subsystem\n");
		return -EINVAL;
	}

	if (obj->id != 0 && obj->id != 1) {
		dev_err(&pdev->dev, "invalid IOMMU instance id\n");
		return -EINVAL;
	}

	return 0;
}

/*
 *	OMAP Device MMU(IOMMU) detection
 */
static int omap_iommu_probe(struct platform_device *pdev)
{
	int err = -ENODEV;
	int irq;
	struct omap_iommu *obj;
	struct resource *res;
	struct device_node *of = pdev->dev.of_node;

	if (!of) {
		pr_err("%s: only DT-based devices are supported\n", __func__);
		return -ENODEV;
	}

	obj = devm_kzalloc(&pdev->dev, sizeof(*obj) + MMU_REG_SIZE, GFP_KERNEL);
	if (!obj)
		return -ENOMEM;

	/*
	 * self-manage the ordering dependencies between omap_device_enable/idle
	 * and omap_device_assert/deassert_hardreset API
	 */
	if (pdev->dev.pm_domain) {
		dev_dbg(&pdev->dev, "device pm_domain is being reset\n");
		pdev->dev.pm_domain = NULL;
	}

	obj->name = dev_name(&pdev->dev);
	obj->nr_tlb_entries = 32;
	err = of_property_read_u32(of, "ti,#tlb-entries", &obj->nr_tlb_entries);
	if (err && err != -EINVAL)
		return err;
	if (obj->nr_tlb_entries != 32 && obj->nr_tlb_entries != 8)
		return -EINVAL;
	if (of_find_property(of, "ti,iommu-bus-err-back", NULL))
		obj->has_bus_err_back = MMU_GP_REG_BUS_ERR_BACK_EN;

	obj->dev = &pdev->dev;
	obj->ctx = (void *)obj + sizeof(*obj);
	obj->cr_ctx = devm_kzalloc(&pdev->dev,
				   sizeof(*obj->cr_ctx) * obj->nr_tlb_entries,
				   GFP_KERNEL);
	if (!obj->cr_ctx)
		return -ENOMEM;

	spin_lock_init(&obj->iommu_lock);
	spin_lock_init(&obj->page_table_lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	obj->regbase = devm_ioremap_resource(obj->dev, res);
	if (IS_ERR(obj->regbase))
		return PTR_ERR(obj->regbase);

	err = omap_iommu_dra7_get_dsp_system_cfg(pdev, obj);
	if (err)
		return err;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENODEV;

	err = devm_request_irq(obj->dev, irq, iommu_fault_handler, IRQF_SHARED,
			       dev_name(obj->dev), obj);
	if (err < 0)
		return err;
	platform_set_drvdata(pdev, obj);

	if (omap_iommu_can_register(pdev)) {
		obj->group = iommu_group_alloc();
		if (IS_ERR(obj->group))
			return PTR_ERR(obj->group);

		err = iommu_device_sysfs_add(&obj->iommu, obj->dev, NULL,
					     obj->name);
		if (err)
			goto out_group;

		iommu_device_set_ops(&obj->iommu, &omap_iommu_ops);
		iommu_device_set_fwnode(&obj->iommu, &of->fwnode);

		err = iommu_device_register(&obj->iommu);
		if (err)
			goto out_sysfs;
	}

	pm_runtime_enable(obj->dev);

	omap_iommu_debugfs_add(obj);

	dev_info(&pdev->dev, "%s registered\n", obj->name);

	/* Re-probe bus to probe device attached to this IOMMU */
	bus_iommu_probe(&platform_bus_type);

	return 0;

out_sysfs:
	iommu_device_sysfs_remove(&obj->iommu);
out_group:
	iommu_group_put(obj->group);
	return err;
}

static int omap_iommu_remove(struct platform_device *pdev)
{
	struct omap_iommu *obj = platform_get_drvdata(pdev);

	if (obj->group) {
		iommu_group_put(obj->group);
		obj->group = NULL;

		iommu_device_sysfs_remove(&obj->iommu);
		iommu_device_unregister(&obj->iommu);
	}

	omap_iommu_debugfs_remove(obj);

	pm_runtime_disable(obj->dev);

	dev_info(&pdev->dev, "%s removed\n", obj->name);
	return 0;
}

static const struct dev_pm_ops omap_iommu_pm_ops = {
	.prepare = omap_iommu_prepare,
	SET_LATE_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				     pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(omap_iommu_runtime_suspend,
			   omap_iommu_runtime_resume, NULL)
};

static const struct of_device_id omap_iommu_of_match[] = {
	{ .compatible = "ti,omap2-iommu" },
	{ .compatible = "ti,omap4-iommu" },
	{ .compatible = "ti,dra7-iommu"	},
	{ .compatible = "ti,dra7-dsp-iommu" },
	{},
};

static struct platform_driver omap_iommu_driver = {
	.probe	= omap_iommu_probe,
	.remove	= omap_iommu_remove,
	.driver	= {
		.name	= "omap-iommu",
		.pm	= &omap_iommu_pm_ops,
		.of_match_table = of_match_ptr(omap_iommu_of_match),
	},
};

static u32 iotlb_init_entry(struct iotlb_entry *e, u32 da, u32 pa, int pgsz)
{
	memset(e, 0, sizeof(*e));

	e->da		= da;
	e->pa		= pa;
	e->valid	= MMU_CAM_V;
	e->pgsz		= pgsz;
	e->endian	= MMU_RAM_ENDIAN_LITTLE;
	e->elsz		= MMU_RAM_ELSZ_8;
	e->mixed	= 0;

	return iopgsz_to_bytes(e->pgsz);
}

static int omap_iommu_map(struct iommu_domain *domain, unsigned long da,
			  phys_addr_t pa, size_t bytes, int prot, gfp_t gfp)
{
	struct omap_iommu_domain *omap_domain = to_omap_domain(domain);
	struct device *dev = omap_domain->dev;
	struct omap_iommu_device *iommu;
	struct omap_iommu *oiommu;
	struct iotlb_entry e;
	int omap_pgsz;
	u32 ret = -EINVAL;
	int i;

	omap_pgsz = bytes_to_iopgsz(bytes);
	if (omap_pgsz < 0) {
		dev_err(dev, "invalid size to map: %zu\n", bytes);
		return -EINVAL;
	}

	dev_dbg(dev, "mapping da 0x%lx to pa %pa size 0x%zx\n", da, &pa, bytes);

	iotlb_init_entry(&e, da, pa, omap_pgsz);

	iommu = omap_domain->iommus;
	for (i = 0; i < omap_domain->num_iommus; i++, iommu++) {
		oiommu = iommu->iommu_dev;
		ret = omap_iopgtable_store_entry(oiommu, &e);
		if (ret) {
			dev_err(dev, "omap_iopgtable_store_entry failed: %d\n",
				ret);
			break;
		}
	}

	if (ret) {
		while (i--) {
			iommu--;
			oiommu = iommu->iommu_dev;
			iopgtable_clear_entry(oiommu, da);
		}
	}

	return ret;
}

static size_t omap_iommu_unmap(struct iommu_domain *domain, unsigned long da,
			       size_t size, struct iommu_iotlb_gather *gather)
{
	struct omap_iommu_domain *omap_domain = to_omap_domain(domain);
	struct device *dev = omap_domain->dev;
	struct omap_iommu_device *iommu;
	struct omap_iommu *oiommu;
	bool error = false;
	size_t bytes = 0;
	int i;

	dev_dbg(dev, "unmapping da 0x%lx size %zu\n", da, size);

	iommu = omap_domain->iommus;
	for (i = 0; i < omap_domain->num_iommus; i++, iommu++) {
		oiommu = iommu->iommu_dev;
		bytes = iopgtable_clear_entry(oiommu, da);
		if (!bytes)
			error = true;
	}

	/*
	 * simplify return - we are only checking if any of the iommus
	 * reported an error, but not if all of them are unmapping the
	 * same number of entries. This should not occur due to the
	 * mirror programming.
	 */
	return error ? 0 : bytes;
}

static int omap_iommu_count(struct device *dev)
{
	struct omap_iommu_arch_data *arch_data = dev_iommu_priv_get(dev);
	int count = 0;

	while (arch_data->iommu_dev) {
		count++;
		arch_data++;
	}

	return count;
}

/* caller should call cleanup if this function fails */
static int omap_iommu_attach_init(struct device *dev,
				  struct omap_iommu_domain *odomain)
{
	struct omap_iommu_device *iommu;
	int i;

	odomain->num_iommus = omap_iommu_count(dev);
	if (!odomain->num_iommus)
		return -EINVAL;

	odomain->iommus = kcalloc(odomain->num_iommus, sizeof(*iommu),
				  GFP_ATOMIC);
	if (!odomain->iommus)
		return -ENOMEM;

	iommu = odomain->iommus;
	for (i = 0; i < odomain->num_iommus; i++, iommu++) {
		iommu->pgtable = kzalloc(IOPGD_TABLE_SIZE, GFP_ATOMIC);
		if (!iommu->pgtable)
			return -ENOMEM;

		/*
		 * should never fail, but please keep this around to ensure
		 * we keep the hardware happy
		 */
		if (WARN_ON(!IS_ALIGNED((long)iommu->pgtable,
					IOPGD_TABLE_SIZE)))
			return -EINVAL;
	}

	return 0;
}

static void omap_iommu_detach_fini(struct omap_iommu_domain *odomain)
{
	int i;
	struct omap_iommu_device *iommu = odomain->iommus;

	for (i = 0; iommu && i < odomain->num_iommus; i++, iommu++)
		kfree(iommu->pgtable);

	kfree(odomain->iommus);
	odomain->num_iommus = 0;
	odomain->iommus = NULL;
}

static int
omap_iommu_attach_dev(struct iommu_domain *domain, struct device *dev)
{
	struct omap_iommu_arch_data *arch_data = dev_iommu_priv_get(dev);
	struct omap_iommu_domain *omap_domain = to_omap_domain(domain);
	struct omap_iommu_device *iommu;
	struct omap_iommu *oiommu;
	int ret = 0;
	int i;

	if (!arch_data || !arch_data->iommu_dev) {
		dev_err(dev, "device doesn't have an associated iommu\n");
		return -EINVAL;
	}

	spin_lock(&omap_domain->lock);

	/* only a single client device can be attached to a domain */
	if (omap_domain->dev) {
		dev_err(dev, "iommu domain is already attached\n");
		ret = -EBUSY;
		goto out;
	}

	ret = omap_iommu_attach_init(dev, omap_domain);
	if (ret) {
		dev_err(dev, "failed to allocate required iommu data %d\n",
			ret);
		goto init_fail;
	}

	iommu = omap_domain->iommus;
	for (i = 0; i < omap_domain->num_iommus; i++, iommu++, arch_data++) {
		/* configure and enable the omap iommu */
		oiommu = arch_data->iommu_dev;
		ret = omap_iommu_attach(oiommu, iommu->pgtable);
		if (ret) {
			dev_err(dev, "can't get omap iommu: %d\n", ret);
			goto attach_fail;
		}

		oiommu->domain = domain;
		iommu->iommu_dev = oiommu;
	}

	omap_domain->dev = dev;

	goto out;

attach_fail:
	while (i--) {
		iommu--;
		arch_data--;
		oiommu = iommu->iommu_dev;
		omap_iommu_detach(oiommu);
		iommu->iommu_dev = NULL;
		oiommu->domain = NULL;
	}
init_fail:
	omap_iommu_detach_fini(omap_domain);
out:
	spin_unlock(&omap_domain->lock);
	return ret;
}

static void _omap_iommu_detach_dev(struct omap_iommu_domain *omap_domain,
				   struct device *dev)
{
	struct omap_iommu_arch_data *arch_data = dev_iommu_priv_get(dev);
	struct omap_iommu_device *iommu = omap_domain->iommus;
	struct omap_iommu *oiommu;
	int i;

	if (!omap_domain->dev) {
		dev_err(dev, "domain has no attached device\n");
		return;
	}

	/* only a single device is supported per domain for now */
	if (omap_domain->dev != dev) {
		dev_err(dev, "invalid attached device\n");
		return;
	}

	/*
	 * cleanup in the reverse order of attachment - this addresses
	 * any h/w dependencies between multiple instances, if any
	 */
	iommu += (omap_domain->num_iommus - 1);
	arch_data += (omap_domain->num_iommus - 1);
	for (i = 0; i < omap_domain->num_iommus; i++, iommu--, arch_data--) {
		oiommu = iommu->iommu_dev;
		iopgtable_clear_entry_all(oiommu);

		omap_iommu_detach(oiommu);
		iommu->iommu_dev = NULL;
		oiommu->domain = NULL;
	}

	omap_iommu_detach_fini(omap_domain);

	omap_domain->dev = NULL;
}

static void omap_iommu_detach_dev(struct iommu_domain *domain,
				  struct device *dev)
{
	struct omap_iommu_domain *omap_domain = to_omap_domain(domain);

	spin_lock(&omap_domain->lock);
	_omap_iommu_detach_dev(omap_domain, dev);
	spin_unlock(&omap_domain->lock);
}

static struct iommu_domain *omap_iommu_domain_alloc(unsigned type)
{
	struct omap_iommu_domain *omap_domain;

	if (type != IOMMU_DOMAIN_UNMANAGED)
		return NULL;

	omap_domain = kzalloc(sizeof(*omap_domain), GFP_KERNEL);
	if (!omap_domain)
		return NULL;

	spin_lock_init(&omap_domain->lock);

	omap_domain->domain.geometry.aperture_start = 0;
	omap_domain->domain.geometry.aperture_end   = (1ULL << 32) - 1;
	omap_domain->domain.geometry.force_aperture = true;

	return &omap_domain->domain;
}

static void omap_iommu_domain_free(struct iommu_domain *domain)
{
	struct omap_iommu_domain *omap_domain = to_omap_domain(domain);

	/*
	 * An iommu device is still attached
	 * (currently, only one device can be attached) ?
	 */
	if (omap_domain->dev)
		_omap_iommu_detach_dev(omap_domain, omap_domain->dev);

	kfree(omap_domain);
}

static phys_addr_t omap_iommu_iova_to_phys(struct iommu_domain *domain,
					   dma_addr_t da)
{
	struct omap_iommu_domain *omap_domain = to_omap_domain(domain);
	struct omap_iommu_device *iommu = omap_domain->iommus;
	struct omap_iommu *oiommu = iommu->iommu_dev;
	struct device *dev = oiommu->dev;
	u32 *pgd, *pte;
	phys_addr_t ret = 0;

	/*
	 * all the iommus within the domain will have identical programming,
	 * so perform the lookup using just the first iommu
	 */
	iopgtable_lookup_entry(oiommu, da, &pgd, &pte);

	if (pte) {
		if (iopte_is_small(*pte))
			ret = omap_iommu_translate(*pte, da, IOPTE_MASK);
		else if (iopte_is_large(*pte))
			ret = omap_iommu_translate(*pte, da, IOLARGE_MASK);
		else
			dev_err(dev, "bogus pte 0x%x, da 0x%llx", *pte,
				(unsigned long long)da);
	} else {
		if (iopgd_is_section(*pgd))
			ret = omap_iommu_translate(*pgd, da, IOSECTION_MASK);
		else if (iopgd_is_super(*pgd))
			ret = omap_iommu_translate(*pgd, da, IOSUPER_MASK);
		else
			dev_err(dev, "bogus pgd 0x%x, da 0x%llx", *pgd,
				(unsigned long long)da);
	}

	return ret;
}

static struct iommu_device *omap_iommu_probe_device(struct device *dev)
{
	struct omap_iommu_arch_data *arch_data, *tmp;
	struct platform_device *pdev;
	struct omap_iommu *oiommu;
	struct device_node *np;
	int num_iommus, i;

	/*
	 * Allocate the per-device iommu structure for DT-based devices.
	 *
	 * TODO: Simplify this when removing non-DT support completely from the
	 * IOMMU users.
	 */
	if (!dev->of_node)
		return ERR_PTR(-ENODEV);

	/*
	 * retrieve the count of IOMMU nodes using phandle size as element size
	 * since #iommu-cells = 0 for OMAP
	 */
	num_iommus = of_property_count_elems_of_size(dev->of_node, "iommus",
						     sizeof(phandle));
	if (num_iommus < 0)
		return 0;

	arch_data = kcalloc(num_iommus + 1, sizeof(*arch_data), GFP_KERNEL);
	if (!arch_data)
		return ERR_PTR(-ENOMEM);

	for (i = 0, tmp = arch_data; i < num_iommus; i++, tmp++) {
		np = of_parse_phandle(dev->of_node, "iommus", i);
		if (!np) {
			kfree(arch_data);
			return ERR_PTR(-EINVAL);
		}

		pdev = of_find_device_by_node(np);
		if (!pdev) {
			of_node_put(np);
			kfree(arch_data);
			return ERR_PTR(-ENODEV);
		}

		oiommu = platform_get_drvdata(pdev);
		if (!oiommu) {
			of_node_put(np);
			kfree(arch_data);
			return ERR_PTR(-EINVAL);
		}

		tmp->iommu_dev = oiommu;
		tmp->dev = &pdev->dev;

		of_node_put(np);
	}

	dev_iommu_priv_set(dev, arch_data);

	/*
	 * use the first IOMMU alone for the sysfs device linking.
	 * TODO: Evaluate if a single iommu_group needs to be
	 * maintained for both IOMMUs
	 */
	oiommu = arch_data->iommu_dev;

	return &oiommu->iommu;
}

static void omap_iommu_release_device(struct device *dev)
{
	struct omap_iommu_arch_data *arch_data = dev_iommu_priv_get(dev);

	if (!dev->of_node || !arch_data)
		return;

	dev_iommu_priv_set(dev, NULL);
	kfree(arch_data);

}

static struct iommu_group *omap_iommu_device_group(struct device *dev)
{
	struct omap_iommu_arch_data *arch_data = dev_iommu_priv_get(dev);
	struct iommu_group *group = ERR_PTR(-EINVAL);

	if (!arch_data)
		return ERR_PTR(-ENODEV);

	if (arch_data->iommu_dev)
		group = iommu_group_ref_get(arch_data->iommu_dev->group);

	return group;
}

static const struct iommu_ops omap_iommu_ops = {
	.domain_alloc	= omap_iommu_domain_alloc,
	.domain_free	= omap_iommu_domain_free,
	.attach_dev	= omap_iommu_attach_dev,
	.detach_dev	= omap_iommu_detach_dev,
	.map		= omap_iommu_map,
	.unmap		= omap_iommu_unmap,
	.iova_to_phys	= omap_iommu_iova_to_phys,
	.probe_device	= omap_iommu_probe_device,
	.release_device	= omap_iommu_release_device,
	.device_group	= omap_iommu_device_group,
	.pgsize_bitmap	= OMAP_IOMMU_PGSIZES,
};

static int __init omap_iommu_init(void)
{
	struct kmem_cache *p;
	const slab_flags_t flags = SLAB_HWCACHE_ALIGN;
	size_t align = 1 << 10; /* L2 pagetable alignement */
	struct device_node *np;
	int ret;

	np = of_find_matching_node(NULL, omap_iommu_of_match);
	if (!np)
		return 0;

	of_node_put(np);

	p = kmem_cache_create("iopte_cache", IOPTE_TABLE_SIZE, align, flags,
			      NULL);
	if (!p)
		return -ENOMEM;
	iopte_cachep = p;

	omap_iommu_debugfs_init();

	ret = platform_driver_register(&omap_iommu_driver);
	if (ret) {
		pr_err("%s: failed to register driver\n", __func__);
		goto fail_driver;
	}

	ret = bus_set_iommu(&platform_bus_type, &omap_iommu_ops);
	if (ret)
		goto fail_bus;

	return 0;

fail_bus:
	platform_driver_unregister(&omap_iommu_driver);
fail_driver:
	kmem_cache_destroy(iopte_cachep);
	return ret;
}
subsys_initcall(omap_iommu_init);
/* must be ready before omap3isp is probed */
