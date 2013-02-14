/*
 * omap iommu: tlb and pagetable primitives
 *
 * Copyright (C) 2008-2010 Nokia Corporation
 *
 * Written by Hiroshi DOYU <Hiroshi.DOYU@nokia.com>,
 *		Paul Mundt and Toshihiro Kobayashi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/module.h>
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

#include <asm/cacheflush.h>

#include <linux/platform_data/iommu-omap.h>

#include "omap-iopgtable.h"
#include "omap-iommu.h"

#define for_each_iotlb_cr(obj, n, __i, cr)				\
	for (__i = 0;							\
	     (__i < (n)) && (cr = __iotlb_read_cr((obj), __i), true);	\
	     __i++)

/* bitmap of the page sizes currently supported */
#define OMAP_IOMMU_PGSIZES	(SZ_4K | SZ_64K | SZ_1M | SZ_16M)

/**
 * struct omap_iommu_domain - omap iommu domain
 * @pgtable:	the page table
 * @iommu_dev:	an omap iommu device attached to this domain. only a single
 *		iommu device can be attached for now.
 * @dev:	Device using this domain.
 * @lock:	domain lock, should be taken when attaching/detaching
 */
struct omap_iommu_domain {
	u32 *pgtable;
	struct omap_iommu *iommu_dev;
	struct device *dev;
	spinlock_t lock;
};

#define MMU_LOCK_BASE_SHIFT	10
#define MMU_LOCK_BASE_MASK	(0x1f << MMU_LOCK_BASE_SHIFT)
#define MMU_LOCK_BASE(x)	\
	((x & MMU_LOCK_BASE_MASK) >> MMU_LOCK_BASE_SHIFT)

#define MMU_LOCK_VICT_SHIFT	4
#define MMU_LOCK_VICT_MASK	(0x1f << MMU_LOCK_VICT_SHIFT)
#define MMU_LOCK_VICT(x)	\
	((x & MMU_LOCK_VICT_MASK) >> MMU_LOCK_VICT_SHIFT)

struct iotlb_lock {
	short base;
	short vict;
};

/* accommodate the difference between omap1 and omap2/3 */
static const struct iommu_functions *arch_iommu;

static struct platform_driver omap_iommu_driver;
static struct kmem_cache *iopte_cachep;

/**
 * omap_install_iommu_arch - Install archtecure specific iommu functions
 * @ops:	a pointer to architecture specific iommu functions
 *
 * There are several kind of iommu algorithm(tlb, pagetable) among
 * omap series. This interface installs such an iommu algorighm.
 **/
int omap_install_iommu_arch(const struct iommu_functions *ops)
{
	if (arch_iommu)
		return -EBUSY;

	arch_iommu = ops;
	return 0;
}
EXPORT_SYMBOL_GPL(omap_install_iommu_arch);

/**
 * omap_uninstall_iommu_arch - Uninstall archtecure specific iommu functions
 * @ops:	a pointer to architecture specific iommu functions
 *
 * This interface uninstalls the iommu algorighm installed previously.
 **/
void omap_uninstall_iommu_arch(const struct iommu_functions *ops)
{
	if (arch_iommu != ops)
		pr_err("%s: not your arch\n", __func__);

	arch_iommu = NULL;
}
EXPORT_SYMBOL_GPL(omap_uninstall_iommu_arch);

/**
 * omap_iommu_save_ctx - Save registers for pm off-mode support
 * @dev:	client device
 **/
void omap_iommu_save_ctx(struct device *dev)
{
	struct omap_iommu *obj = dev_to_omap_iommu(dev);

	arch_iommu->save_ctx(obj);
}
EXPORT_SYMBOL_GPL(omap_iommu_save_ctx);

/**
 * omap_iommu_restore_ctx - Restore registers for pm off-mode support
 * @dev:	client device
 **/
void omap_iommu_restore_ctx(struct device *dev)
{
	struct omap_iommu *obj = dev_to_omap_iommu(dev);

	arch_iommu->restore_ctx(obj);
}
EXPORT_SYMBOL_GPL(omap_iommu_restore_ctx);

/**
 * omap_iommu_arch_version - Return running iommu arch version
 **/
u32 omap_iommu_arch_version(void)
{
	return arch_iommu->version;
}
EXPORT_SYMBOL_GPL(omap_iommu_arch_version);

static int iommu_enable(struct omap_iommu *obj)
{
	int err;
	struct platform_device *pdev = to_platform_device(obj->dev);
	struct iommu_platform_data *pdata = pdev->dev.platform_data;

	if (!obj || !pdata)
		return -EINVAL;

	if (!arch_iommu)
		return -ENODEV;

	if (pdata->deassert_reset) {
		err = pdata->deassert_reset(pdev, pdata->reset_name);
		if (err) {
			dev_err(obj->dev, "deassert_reset failed: %d\n", err);
			return err;
		}
	}

	pm_runtime_get_sync(obj->dev);

	err = arch_iommu->enable(obj);

	return err;
}

static void iommu_disable(struct omap_iommu *obj)
{
	struct platform_device *pdev = to_platform_device(obj->dev);
	struct iommu_platform_data *pdata = pdev->dev.platform_data;

	if (!obj || !pdata)
		return;

	arch_iommu->disable(obj);

	pm_runtime_put_sync(obj->dev);

	if (pdata->assert_reset)
		pdata->assert_reset(pdev, pdata->reset_name);
}

/*
 *	TLB operations
 */
void omap_iotlb_cr_to_e(struct cr_regs *cr, struct iotlb_entry *e)
{
	BUG_ON(!cr || !e);

	arch_iommu->cr_to_e(cr, e);
}
EXPORT_SYMBOL_GPL(omap_iotlb_cr_to_e);

static inline int iotlb_cr_valid(struct cr_regs *cr)
{
	if (!cr)
		return -EINVAL;

	return arch_iommu->cr_valid(cr);
}

static inline struct cr_regs *iotlb_alloc_cr(struct omap_iommu *obj,
					     struct iotlb_entry *e)
{
	if (!e)
		return NULL;

	return arch_iommu->alloc_cr(obj, e);
}

static u32 iotlb_cr_to_virt(struct cr_regs *cr)
{
	return arch_iommu->cr_to_virt(cr);
}

static u32 get_iopte_attr(struct iotlb_entry *e)
{
	return arch_iommu->get_pte_attr(e);
}

static u32 iommu_report_fault(struct omap_iommu *obj, u32 *da)
{
	return arch_iommu->fault_isr(obj, da);
}

static void iotlb_lock_get(struct omap_iommu *obj, struct iotlb_lock *l)
{
	u32 val;

	val = iommu_read_reg(obj, MMU_LOCK);

	l->base = MMU_LOCK_BASE(val);
	l->vict = MMU_LOCK_VICT(val);

}

static void iotlb_lock_set(struct omap_iommu *obj, struct iotlb_lock *l)
{
	u32 val;

	val = (l->base << MMU_LOCK_BASE_SHIFT);
	val |= (l->vict << MMU_LOCK_VICT_SHIFT);

	iommu_write_reg(obj, val, MMU_LOCK);
}

static void iotlb_read_cr(struct omap_iommu *obj, struct cr_regs *cr)
{
	arch_iommu->tlb_read_cr(obj, cr);
}

static void iotlb_load_cr(struct omap_iommu *obj, struct cr_regs *cr)
{
	arch_iommu->tlb_load_cr(obj, cr);

	iommu_write_reg(obj, 1, MMU_FLUSH_ENTRY);
	iommu_write_reg(obj, 1, MMU_LD_TLB);
}

/**
 * iotlb_dump_cr - Dump an iommu tlb entry into buf
 * @obj:	target iommu
 * @cr:		contents of cam and ram register
 * @buf:	output buffer
 **/
static inline ssize_t iotlb_dump_cr(struct omap_iommu *obj, struct cr_regs *cr,
				    char *buf)
{
	BUG_ON(!cr || !buf);

	return arch_iommu->dump_cr(obj, cr, buf);
}

/* only used in iotlb iteration for-loop */
static struct cr_regs __iotlb_read_cr(struct omap_iommu *obj, int n)
{
	struct cr_regs cr;
	struct iotlb_lock l;

	iotlb_lock_get(obj, &l);
	l.vict = n;
	iotlb_lock_set(obj, &l);
	iotlb_read_cr(obj, &cr);

	return cr;
}

/**
 * load_iotlb_entry - Set an iommu tlb entry
 * @obj:	target iommu
 * @e:		an iommu tlb entry info
 **/
#ifdef PREFETCH_IOTLB
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
			dev_dbg(obj->dev, "%s: %08x<=%08x(%x)\n",
				__func__, start, da, bytes);
			iotlb_load_cr(obj, &cr);
			iommu_write_reg(obj, 1, MMU_FLUSH_ENTRY);
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

#if defined(CONFIG_OMAP_IOMMU_DEBUG) || defined(CONFIG_OMAP_IOMMU_DEBUG_MODULE)

ssize_t omap_iommu_dump_ctx(struct omap_iommu *obj, char *buf, ssize_t bytes)
{
	if (!obj || !buf)
		return -EINVAL;

	pm_runtime_get_sync(obj->dev);

	bytes = arch_iommu->dump_ctx(obj, buf, bytes);

	pm_runtime_put_sync(obj->dev);

	return bytes;
}
EXPORT_SYMBOL_GPL(omap_iommu_dump_ctx);

static int
__dump_tlb_entries(struct omap_iommu *obj, struct cr_regs *crs, int num)
{
	int i;
	struct iotlb_lock saved;
	struct cr_regs tmp;
	struct cr_regs *p = crs;

	pm_runtime_get_sync(obj->dev);
	iotlb_lock_get(obj, &saved);

	for_each_iotlb_cr(obj, num, i, tmp) {
		if (!iotlb_cr_valid(&tmp))
			continue;
		*p++ = tmp;
	}

	iotlb_lock_set(obj, &saved);
	pm_runtime_put_sync(obj->dev);

	return  p - crs;
}

/**
 * omap_dump_tlb_entries - dump cr arrays to given buffer
 * @obj:	target iommu
 * @buf:	output buffer
 **/
size_t omap_dump_tlb_entries(struct omap_iommu *obj, char *buf, ssize_t bytes)
{
	int i, num;
	struct cr_regs *cr;
	char *p = buf;

	num = bytes / sizeof(*cr);
	num = min(obj->nr_tlb_entries, num);

	cr = kcalloc(num, sizeof(*cr), GFP_KERNEL);
	if (!cr)
		return 0;

	num = __dump_tlb_entries(obj, cr, num);
	for (i = 0; i < num; i++)
		p += iotlb_dump_cr(obj, cr + i, p);
	kfree(cr);

	return p - buf;
}
EXPORT_SYMBOL_GPL(omap_dump_tlb_entries);

int omap_foreach_iommu_device(void *data, int (*fn)(struct device *, void *))
{
	return driver_for_each_device(&omap_iommu_driver.driver,
				      NULL, data, fn);
}
EXPORT_SYMBOL_GPL(omap_foreach_iommu_device);

#endif /* CONFIG_OMAP_IOMMU_DEBUG_MODULE */

/*
 *	H/W pagetable operations
 */
static void flush_iopgd_range(u32 *first, u32 *last)
{
	/* FIXME: L2 cache should be taken care of if it exists */
	do {
		asm("mcr	p15, 0, %0, c7, c10, 1 @ flush_pgd"
		    : : "r" (first));
		first += L1_CACHE_BYTES / sizeof(*first);
	} while (first <= last);
}

static void flush_iopte_range(u32 *first, u32 *last)
{
	/* FIXME: L2 cache should be taken care of if it exists */
	do {
		asm("mcr	p15, 0, %0, c7, c10, 1 @ flush_pte"
		    : : "r" (first));
		first += L1_CACHE_BYTES / sizeof(*first);
	} while (first <= last);
}

static void iopte_free(u32 *iopte)
{
	/* Note: freed iopte's must be clean ready for re-use */
	kmem_cache_free(iopte_cachep, iopte);
}

static u32 *iopte_alloc(struct omap_iommu *obj, u32 *iopgd, u32 da)
{
	u32 *iopte;

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

		*iopgd = virt_to_phys(iopte) | IOPGD_TABLE;
		flush_iopgd_range(iopgd, iopgd);

		dev_vdbg(obj->dev, "%s: a new pte:%p\n", __func__, iopte);
	} else {
		/* We raced, free the reduniovant table */
		iopte_free(iopte);
	}

pte_ready:
	iopte = iopte_offset(iopgd, da);

	dev_vdbg(obj->dev,
		 "%s: da:%08x pgd:%p *pgd:%08x pte:%p *pte:%08x\n",
		 __func__, da, iopgd, *iopgd, iopte, *iopte);

	return iopte;
}

static int iopgd_alloc_section(struct omap_iommu *obj, u32 da, u32 pa, u32 prot)
{
	u32 *iopgd = iopgd_offset(obj, da);

	if ((da | pa) & ~IOSECTION_MASK) {
		dev_err(obj->dev, "%s: %08x:%08x should aligned on %08lx\n",
			__func__, da, pa, IOSECTION_SIZE);
		return -EINVAL;
	}

	*iopgd = (pa & IOSECTION_MASK) | prot | IOPGD_SECTION;
	flush_iopgd_range(iopgd, iopgd);
	return 0;
}

static int iopgd_alloc_super(struct omap_iommu *obj, u32 da, u32 pa, u32 prot)
{
	u32 *iopgd = iopgd_offset(obj, da);
	int i;

	if ((da | pa) & ~IOSUPER_MASK) {
		dev_err(obj->dev, "%s: %08x:%08x should aligned on %08lx\n",
			__func__, da, pa, IOSUPER_SIZE);
		return -EINVAL;
	}

	for (i = 0; i < 16; i++)
		*(iopgd + i) = (pa & IOSUPER_MASK) | prot | IOPGD_SUPER;
	flush_iopgd_range(iopgd, iopgd + 15);
	return 0;
}

static int iopte_alloc_page(struct omap_iommu *obj, u32 da, u32 pa, u32 prot)
{
	u32 *iopgd = iopgd_offset(obj, da);
	u32 *iopte = iopte_alloc(obj, iopgd, da);

	if (IS_ERR(iopte))
		return PTR_ERR(iopte);

	*iopte = (pa & IOPAGE_MASK) | prot | IOPTE_SMALL;
	flush_iopte_range(iopte, iopte);

	dev_vdbg(obj->dev, "%s: da:%08x pa:%08x pte:%p *pte:%08x\n",
		 __func__, da, pa, iopte, *iopte);

	return 0;
}

static int iopte_alloc_large(struct omap_iommu *obj, u32 da, u32 pa, u32 prot)
{
	u32 *iopgd = iopgd_offset(obj, da);
	u32 *iopte = iopte_alloc(obj, iopgd, da);
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
	flush_iopte_range(iopte, iopte + 15);
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
		BUG();
		break;
	}

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
int omap_iopgtable_store_entry(struct omap_iommu *obj, struct iotlb_entry *e)
{
	int err;

	flush_iotlb_page(obj, e->da);
	err = iopgtable_store_entry_core(obj, e);
	if (!err)
		prefetch_iotlb_entry(obj, e);
	return err;
}
EXPORT_SYMBOL_GPL(omap_iopgtable_store_entry);

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
		flush_iopte_range(iopte, iopte + (nent - 1) * sizeof(*iopte));

		/*
		 * do table walk to check if this table is necessary or not
		 */
		iopte = iopte_offset(iopgd, 0);
		for (i = 0; i < PTRS_PER_IOPTE; i++)
			if (iopte[i])
				goto out;

		iopte_free(iopte);
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
	flush_iopgd_range(iopgd, iopgd + (nent - 1) * sizeof(*iopgd));
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
	int i;

	spin_lock(&obj->page_table_lock);

	for (i = 0; i < PTRS_PER_IOPGD; i++) {
		u32 da;
		u32 *iopgd;

		da = i << IOPGD_SHIFT;
		iopgd = iopgd_offset(obj, da);

		if (!*iopgd)
			continue;

		if (iopgd_is_table(*iopgd))
			iopte_free(iopte_offset(iopgd, 0));

		*iopgd = 0;
		flush_iopgd_range(iopgd, iopgd);
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

	if (!obj->refcount)
		return IRQ_NONE;

	errs = iommu_report_fault(obj, &da);
	if (errs == 0)
		return IRQ_HANDLED;

	/* Fault callback or TLB/PTE Dynamic loading */
	if (!report_iommu_fault(domain, obj->dev, da, 0))
		return IRQ_HANDLED;

	iommu_disable(obj);

	iopgd = iopgd_offset(obj, da);

	if (!iopgd_is_table(*iopgd)) {
		dev_err(obj->dev, "%s: errs:0x%08x da:0x%08x pgd:0x%p "
			"*pgd:px%08x\n", obj->name, errs, da, iopgd, *iopgd);
		return IRQ_NONE;
	}

	iopte = iopte_offset(iopgd, da);

	dev_err(obj->dev, "%s: errs:0x%08x da:0x%08x pgd:0x%p *pgd:0x%08x "
		"pte:0x%p *pte:0x%08x\n", obj->name, errs, da, iopgd, *iopgd,
		iopte, *iopte);

	return IRQ_NONE;
}

static int device_match_by_alias(struct device *dev, void *data)
{
	struct omap_iommu *obj = to_iommu(dev);
	const char *name = data;

	pr_debug("%s: %s %s\n", __func__, obj->name, name);

	return strcmp(obj->name, name) == 0;
}

/**
 * omap_iommu_attach() - attach iommu device to an iommu domain
 * @name:	name of target omap iommu device
 * @iopgd:	page table
 **/
static struct omap_iommu *omap_iommu_attach(const char *name, u32 *iopgd)
{
	int err = -ENOMEM;
	struct device *dev;
	struct omap_iommu *obj;

	dev = driver_find_device(&omap_iommu_driver.driver, NULL,
				(void *)name,
				device_match_by_alias);
	if (!dev)
		return NULL;

	obj = to_iommu(dev);

	spin_lock(&obj->iommu_lock);

	/* an iommu device can only be attached once */
	if (++obj->refcount > 1) {
		dev_err(dev, "%s: already attached!\n", obj->name);
		err = -EBUSY;
		goto err_enable;
	}

	obj->iopgd = iopgd;
	err = iommu_enable(obj);
	if (err)
		goto err_enable;
	flush_iotlb_all(obj);

	if (!try_module_get(obj->owner))
		goto err_module;

	spin_unlock(&obj->iommu_lock);

	dev_dbg(obj->dev, "%s: %s\n", __func__, obj->name);
	return obj;

err_module:
	if (obj->refcount == 1)
		iommu_disable(obj);
err_enable:
	obj->refcount--;
	spin_unlock(&obj->iommu_lock);
	return ERR_PTR(err);
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

	if (--obj->refcount == 0)
		iommu_disable(obj);

	module_put(obj->owner);

	obj->iopgd = NULL;

	spin_unlock(&obj->iommu_lock);

	dev_dbg(obj->dev, "%s: %s\n", __func__, obj->name);
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
	struct iommu_platform_data *pdata = pdev->dev.platform_data;

	obj = kzalloc(sizeof(*obj) + MMU_REG_SIZE, GFP_KERNEL);
	if (!obj)
		return -ENOMEM;

	obj->nr_tlb_entries = pdata->nr_tlb_entries;
	obj->name = pdata->name;
	obj->dev = &pdev->dev;
	obj->ctx = (void *)obj + sizeof(*obj);
	obj->da_start = pdata->da_start;
	obj->da_end = pdata->da_end;

	spin_lock_init(&obj->iommu_lock);
	mutex_init(&obj->mmap_lock);
	spin_lock_init(&obj->page_table_lock);
	INIT_LIST_HEAD(&obj->mmap);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		err = -ENODEV;
		goto err_mem;
	}

	res = request_mem_region(res->start, resource_size(res),
				 dev_name(&pdev->dev));
	if (!res) {
		err = -EIO;
		goto err_mem;
	}

	obj->regbase = ioremap(res->start, resource_size(res));
	if (!obj->regbase) {
		err = -ENOMEM;
		goto err_ioremap;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		err = -ENODEV;
		goto err_irq;
	}
	err = request_irq(irq, iommu_fault_handler, IRQF_SHARED,
			  dev_name(&pdev->dev), obj);
	if (err < 0)
		goto err_irq;
	platform_set_drvdata(pdev, obj);

	pm_runtime_irq_safe(obj->dev);
	pm_runtime_enable(obj->dev);

	dev_info(&pdev->dev, "%s registered\n", obj->name);
	return 0;

err_irq:
	iounmap(obj->regbase);
err_ioremap:
	release_mem_region(res->start, resource_size(res));
err_mem:
	kfree(obj);
	return err;
}

static int omap_iommu_remove(struct platform_device *pdev)
{
	int irq;
	struct resource *res;
	struct omap_iommu *obj = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	iopgtable_clear_entry_all(obj);

	irq = platform_get_irq(pdev, 0);
	free_irq(irq, obj);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));
	iounmap(obj->regbase);

	pm_runtime_disable(obj->dev);

	dev_info(&pdev->dev, "%s removed\n", obj->name);
	kfree(obj);
	return 0;
}

static struct platform_driver omap_iommu_driver = {
	.probe	= omap_iommu_probe,
	.remove	= omap_iommu_remove,
	.driver	= {
		.name	= "omap-iommu",
	},
};

static void iopte_cachep_ctor(void *iopte)
{
	clean_dcache_area(iopte, IOPTE_TABLE_SIZE);
}

static u32 iotlb_init_entry(struct iotlb_entry *e, u32 da, u32 pa,
				   u32 flags)
{
	memset(e, 0, sizeof(*e));

	e->da		= da;
	e->pa		= pa;
	e->valid	= 1;
	/* FIXME: add OMAP1 support */
	e->pgsz		= flags & MMU_CAM_PGSZ_MASK;
	e->endian	= flags & MMU_RAM_ENDIAN_MASK;
	e->elsz		= flags & MMU_RAM_ELSZ_MASK;
	e->mixed	= flags & MMU_RAM_MIXED_MASK;

	return iopgsz_to_bytes(e->pgsz);
}

static int omap_iommu_map(struct iommu_domain *domain, unsigned long da,
			 phys_addr_t pa, size_t bytes, int prot)
{
	struct omap_iommu_domain *omap_domain = domain->priv;
	struct omap_iommu *oiommu = omap_domain->iommu_dev;
	struct device *dev = oiommu->dev;
	struct iotlb_entry e;
	int omap_pgsz;
	u32 ret, flags;

	/* we only support mapping a single iommu page for now */
	omap_pgsz = bytes_to_iopgsz(bytes);
	if (omap_pgsz < 0) {
		dev_err(dev, "invalid size to map: %d\n", bytes);
		return -EINVAL;
	}

	dev_dbg(dev, "mapping da 0x%lx to pa 0x%x size 0x%x\n", da, pa, bytes);

	flags = omap_pgsz | prot;

	iotlb_init_entry(&e, da, pa, flags);

	ret = omap_iopgtable_store_entry(oiommu, &e);
	if (ret)
		dev_err(dev, "omap_iopgtable_store_entry failed: %d\n", ret);

	return ret;
}

static size_t omap_iommu_unmap(struct iommu_domain *domain, unsigned long da,
			    size_t size)
{
	struct omap_iommu_domain *omap_domain = domain->priv;
	struct omap_iommu *oiommu = omap_domain->iommu_dev;
	struct device *dev = oiommu->dev;

	dev_dbg(dev, "unmapping da 0x%lx size %u\n", da, size);

	return iopgtable_clear_entry(oiommu, da);
}

static int
omap_iommu_attach_dev(struct iommu_domain *domain, struct device *dev)
{
	struct omap_iommu_domain *omap_domain = domain->priv;
	struct omap_iommu *oiommu;
	struct omap_iommu_arch_data *arch_data = dev->archdata.iommu;
	int ret = 0;

	spin_lock(&omap_domain->lock);

	/* only a single device is supported per domain for now */
	if (omap_domain->iommu_dev) {
		dev_err(dev, "iommu domain is already attached\n");
		ret = -EBUSY;
		goto out;
	}

	/* get a handle to and enable the omap iommu */
	oiommu = omap_iommu_attach(arch_data->name, omap_domain->pgtable);
	if (IS_ERR(oiommu)) {
		ret = PTR_ERR(oiommu);
		dev_err(dev, "can't get omap iommu: %d\n", ret);
		goto out;
	}

	omap_domain->iommu_dev = arch_data->iommu_dev = oiommu;
	omap_domain->dev = dev;
	oiommu->domain = domain;

out:
	spin_unlock(&omap_domain->lock);
	return ret;
}

static void _omap_iommu_detach_dev(struct omap_iommu_domain *omap_domain,
			struct device *dev)
{
	struct omap_iommu *oiommu = dev_to_omap_iommu(dev);
	struct omap_iommu_arch_data *arch_data = dev->archdata.iommu;

	/* only a single device is supported per domain for now */
	if (omap_domain->iommu_dev != oiommu) {
		dev_err(dev, "invalid iommu device\n");
		return;
	}

	iopgtable_clear_entry_all(oiommu);

	omap_iommu_detach(oiommu);

	omap_domain->iommu_dev = arch_data->iommu_dev = NULL;
	omap_domain->dev = NULL;
}

static void omap_iommu_detach_dev(struct iommu_domain *domain,
				 struct device *dev)
{
	struct omap_iommu_domain *omap_domain = domain->priv;

	spin_lock(&omap_domain->lock);
	_omap_iommu_detach_dev(omap_domain, dev);
	spin_unlock(&omap_domain->lock);
}

static int omap_iommu_domain_init(struct iommu_domain *domain)
{
	struct omap_iommu_domain *omap_domain;

	omap_domain = kzalloc(sizeof(*omap_domain), GFP_KERNEL);
	if (!omap_domain) {
		pr_err("kzalloc failed\n");
		goto out;
	}

	omap_domain->pgtable = kzalloc(IOPGD_TABLE_SIZE, GFP_KERNEL);
	if (!omap_domain->pgtable) {
		pr_err("kzalloc failed\n");
		goto fail_nomem;
	}

	/*
	 * should never fail, but please keep this around to ensure
	 * we keep the hardware happy
	 */
	BUG_ON(!IS_ALIGNED((long)omap_domain->pgtable, IOPGD_TABLE_SIZE));

	clean_dcache_area(omap_domain->pgtable, IOPGD_TABLE_SIZE);
	spin_lock_init(&omap_domain->lock);

	domain->priv = omap_domain;

	domain->geometry.aperture_start = 0;
	domain->geometry.aperture_end   = (1ULL << 32) - 1;
	domain->geometry.force_aperture = true;

	return 0;

fail_nomem:
	kfree(omap_domain);
out:
	return -ENOMEM;
}

static void omap_iommu_domain_destroy(struct iommu_domain *domain)
{
	struct omap_iommu_domain *omap_domain = domain->priv;

	domain->priv = NULL;

	/*
	 * An iommu device is still attached
	 * (currently, only one device can be attached) ?
	 */
	if (omap_domain->iommu_dev)
		_omap_iommu_detach_dev(omap_domain, omap_domain->dev);

	kfree(omap_domain->pgtable);
	kfree(omap_domain);
}

static phys_addr_t omap_iommu_iova_to_phys(struct iommu_domain *domain,
					  unsigned long da)
{
	struct omap_iommu_domain *omap_domain = domain->priv;
	struct omap_iommu *oiommu = omap_domain->iommu_dev;
	struct device *dev = oiommu->dev;
	u32 *pgd, *pte;
	phys_addr_t ret = 0;

	iopgtable_lookup_entry(oiommu, da, &pgd, &pte);

	if (pte) {
		if (iopte_is_small(*pte))
			ret = omap_iommu_translate(*pte, da, IOPTE_MASK);
		else if (iopte_is_large(*pte))
			ret = omap_iommu_translate(*pte, da, IOLARGE_MASK);
		else
			dev_err(dev, "bogus pte 0x%x, da 0x%lx", *pte, da);
	} else {
		if (iopgd_is_section(*pgd))
			ret = omap_iommu_translate(*pgd, da, IOSECTION_MASK);
		else if (iopgd_is_super(*pgd))
			ret = omap_iommu_translate(*pgd, da, IOSUPER_MASK);
		else
			dev_err(dev, "bogus pgd 0x%x, da 0x%lx", *pgd, da);
	}

	return ret;
}

static int omap_iommu_domain_has_cap(struct iommu_domain *domain,
				    unsigned long cap)
{
	return 0;
}

static struct iommu_ops omap_iommu_ops = {
	.domain_init	= omap_iommu_domain_init,
	.domain_destroy	= omap_iommu_domain_destroy,
	.attach_dev	= omap_iommu_attach_dev,
	.detach_dev	= omap_iommu_detach_dev,
	.map		= omap_iommu_map,
	.unmap		= omap_iommu_unmap,
	.iova_to_phys	= omap_iommu_iova_to_phys,
	.domain_has_cap	= omap_iommu_domain_has_cap,
	.pgsize_bitmap	= OMAP_IOMMU_PGSIZES,
};

static int __init omap_iommu_init(void)
{
	struct kmem_cache *p;
	const unsigned long flags = SLAB_HWCACHE_ALIGN;
	size_t align = 1 << 10; /* L2 pagetable alignement */

	p = kmem_cache_create("iopte_cache", IOPTE_TABLE_SIZE, align, flags,
			      iopte_cachep_ctor);
	if (!p)
		return -ENOMEM;
	iopte_cachep = p;

	bus_set_iommu(&platform_bus_type, &omap_iommu_ops);

	return platform_driver_register(&omap_iommu_driver);
}
/* must be ready before omap3isp is probed */
subsys_initcall(omap_iommu_init);

static void __exit omap_iommu_exit(void)
{
	kmem_cache_destroy(iopte_cachep);

	platform_driver_unregister(&omap_iommu_driver);
}
module_exit(omap_iommu_exit);

MODULE_DESCRIPTION("omap iommu: tlb and pagetable primitives");
MODULE_ALIAS("platform:omap-iommu");
MODULE_AUTHOR("Hiroshi DOYU, Paul Mundt and Toshihiro Kobayashi");
MODULE_LICENSE("GPL v2");
