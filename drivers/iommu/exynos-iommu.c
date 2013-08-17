/* linux/drivers/iommu/exynos_iommu.c
 *
 * Copyright (c) 2011-2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifdef CONFIG_EXYNOS_IOMMU_DEBUG
#define DEBUG
#endif

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/memblock.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/string.h>

#include <asm/cacheflush.h>

#include "exynos-iommu.h"

#define MODULE_NAME "exynos-sysmmu"

#define SECT_MASK (~(SECT_SIZE - 1))
#define LPAGE_MASK (~(LPAGE_SIZE - 1))
#define SPAGE_MASK (~(SPAGE_SIZE - 1))

#define lv1ent_fault(sent) (((*(sent) & 3) == 0) || ((*(sent) & 3) == 3))
#define lv1ent_page(sent) ((*(sent) & 3) == 1)
#define lv1ent_section(sent) ((*(sent) & 3) == 2)

#define lv2ent_fault(pent) ((*(pent) & 3) == 0)
#define lv2ent_small(pent) ((*(pent) & 2) == 2)
#define lv2ent_large(pent) ((*(pent) & 3) == 1)

#define section_phys(sent) (*(sent) & SECT_MASK)
#define section_offs(iova) ((iova) & 0xFFFFF)
#define lpage_phys(pent) (*(pent) & LPAGE_MASK)
#define lpage_offs(iova) ((iova) & 0xFFFF)
#define spage_phys(pent) (*(pent) & SPAGE_MASK)
#define spage_offs(iova) ((iova) & 0xFFF)

#define lv1ent_offset(iova) ((iova) >> SECT_ORDER)
#define lv2ent_offset(iova) (((iova) & 0xFF000) >> SPAGE_ORDER)

#define NUM_LV1ENTRIES 4096
#define NUM_LV2ENTRIES 256

#define LV2TABLE_SIZE (NUM_LV2ENTRIES * sizeof(long))

#define SPAGES_PER_LPAGE (LPAGE_SIZE / SPAGE_SIZE)

#define lv2table_base(sent) (*(sent) & 0xFFFFFC00)

#define mk_lv1ent_sect(pa) ((pa) | 2)
#define mk_lv1ent_page(pa) ((pa) | 1)
#define mk_lv2ent_lpage(pa) ((pa) | 1)
#define mk_lv2ent_spage(pa) ((pa) | 2)

#define CTRL_ENABLE	0x5
#define CTRL_BLOCK	0x7
#define CTRL_DISABLE	0x0

#define CFG_LRU		0x1
#define CFG_QOS(n)	((n & 0xF) << 7)
#define CFG_MASK	0x0150FFFF /* Selecting bit 0-15, 20, 22 and 24 */
#define CFG_ACGEN	(1 << 24) /* System MMU 3.3 only */
#define CFG_SYSSEL	(1 << 22) /* System MMU 3.2 only */
#define CFG_FLPDCACHE	(1 << 20) /* System MMU 3.2+ only */
#define CFG_SHAREABLE	(1 << 12) /* System MMU 3.x only */

#define REG_MMU_CTRL		0x000
#define REG_MMU_CFG		0x004
#define REG_MMU_STATUS		0x008
#define REG_MMU_FLUSH		0x00C
#define REG_MMU_FLUSH_ENTRY	0x010
#define REG_PT_BASE_ADDR	0x014
#define REG_INT_STATUS		0x018
#define REG_INT_CLEAR		0x01C
#define REG_PB_INFO		0x400
#define REG_PB_LMM		0x404
#define REG_PB_INDICATE		0x408
#define REG_PB_CFG		0x40C

#define REG_PAGE_FAULT_ADDR	0x024
#define REG_AW_FAULT_ADDR	0x028
#define REG_AR_FAULT_ADDR	0x02C
#define REG_DEFAULT_SLAVE_ADDR	0x030

#define REG_MMU_VERSION		0x034

#define MMU_MAJ_VER(reg)	(reg >> 28)
#define MMU_MIN_VER(reg)	((reg >> 21) & 0x7F)

#define MAX_NUM_PBUF		6

#define NUM_MINOR_OF_SYSMMU_V3	4

static void *sysmmu_placeholder; /* Inidcate if a device is System MMU */

#define is_sysmmu(sysmmu) (sysmmu->archdata.iommu == &sysmmu_placeholder)
#define has_sysmmu(dev)							\
	(dev->parent && dev->archdata.iommu && is_sysmmu(dev->parent))
#define for_each_sysmmu(dev, sysmmu)					\
	for (sysmmu = dev->parent; sysmmu && is_sysmmu(sysmmu);		\
			sysmmu = sysmmu->parent)
#define for_each_sysmmu_until(dev, sysmmu, until)			\
	for (sysmmu = dev->parent; sysmmu != until; sysmmu = sysmmu->parent)

static struct kmem_cache *lv2table_kmem_cache;

static unsigned long *section_entry(unsigned long *pgtable, unsigned long iova)
{
	return pgtable + lv1ent_offset(iova);
}

static unsigned long *page_entry(unsigned long *sent, unsigned long iova)
{
	return (unsigned long *)__va(lv2table_base(sent)) + lv2ent_offset(iova);
}

static unsigned short fault_reg_offset[SYSMMU_FAULTS_NUM] = {
	REG_PAGE_FAULT_ADDR,
	REG_AR_FAULT_ADDR,
	REG_AW_FAULT_ADDR,
	REG_DEFAULT_SLAVE_ADDR,
	REG_AR_FAULT_ADDR,
	REG_AR_FAULT_ADDR,
	REG_AW_FAULT_ADDR,
	REG_AW_FAULT_ADDR
};

static char *sysmmu_fault_name[SYSMMU_FAULTS_NUM] = {
	"PAGE FAULT",
	"AR MULTI-HIT FAULT",
	"AW MULTI-HIT FAULT",
	"BUS ERROR",
	"AR SECURITY PROTECTION FAULT",
	"AR ACCESS PROTECTION FAULT",
	"AW SECURITY PROTECTION FAULT",
	"AW ACCESS PROTECTION FAULT",
	"UNKNOWN FAULT"
};

/*
 * Metadata attached to each System MMU devices.
 */
struct exynos_iommu_data {
	struct exynos_iommu_owner *owner;
};

struct sysmmu_drvdata {
	struct device *sysmmu;	/* System MMU's device descriptor */
	struct device *master;	/* Client device that needs System MMU */
	char *dbgname;
	int nsfrs;
	void __iomem **sfrbases;
	struct clk *clk;
	int activations;
	struct iommu_domain *domain; /* domain given to iommu_attach_device() */
	sysmmu_fault_handler_t fault_handler;
	unsigned long pgtable;
	struct sysmmu_version ver; /* mach/sysmmu.h */
	short qos;
	spinlock_t lock;
	struct sysmmu_prefbuf pbufs[MAX_NUM_PBUF];
	int num_pbufs;
	struct dentry *debugfs_root;
	bool runtime_active;
	enum sysmmu_property prop; /* mach/sysmmu.h */
};

struct exynos_iommu_domain {
	struct list_head clients; /* list of sysmmu_drvdata.node */
	unsigned long *pgtable; /* lv1 page table, 16KB */
	short *lv2entcnt; /* free lv2 entry counter for each section */
	spinlock_t lock; /* lock for this structure */
	spinlock_t pgtablelock; /* lock for modifying page table @ pgtable */
};

static bool set_sysmmu_active(struct sysmmu_drvdata *data)
{
	/* return true if the System MMU was not active previously
	   and it needs to be initialized */
	return ++data->activations == 1;
}

static bool set_sysmmu_inactive(struct sysmmu_drvdata *data)
{
	/* return true if the System MMU is needed to be disabled */
	BUG_ON(data->activations < 1);
	return --data->activations == 0;
}

static bool is_sysmmu_active(struct sysmmu_drvdata *data)
{
	return data->activations > 0;
}

static unsigned int __sysmmu_version(struct sysmmu_drvdata *drvdata,
					int idx, unsigned int *minor)
{
	unsigned int major;

	major = readl(drvdata->sfrbases[idx] + REG_MMU_VERSION);

	if ((MMU_MAJ_VER(major) == 0) || (MMU_MAJ_VER(major) > 3)) {
		/* register MMU_VERSION is used for special purpose */
		if (drvdata->ver.major == 0) {
			/* min ver. is not important for System MMU 1 and 2 */
			major = 1;
		} else {
			if (minor)
				*minor = drvdata->ver.minor;
			major = drvdata->ver.major;
		}

		return major;
	}

	if (minor)
		*minor = MMU_MIN_VER(major);

	major = MMU_MAJ_VER(major);

	return major;
}

static bool has_sysmmu_capable_pbuf(struct sysmmu_drvdata *drvdata,
			int idx, struct sysmmu_prefbuf pbuf[], int *min)
{
	if (__sysmmu_version(drvdata, idx, min) != 3)
		return false;

	if ((pbuf[0].config & SYSMMU_PBUFCFG_WRITE) &&
		(drvdata->prop & SYSMMU_PROP_WRITE))
		return true;

	if (!(pbuf[0].config & SYSMMU_PBUFCFG_WRITE) &&
		(drvdata->prop & SYSMMU_PROP_READ))
		return true;

	return false;
}

static void sysmmu_unblock(void __iomem *sfrbase)
{
	__raw_writel(CTRL_ENABLE, sfrbase + REG_MMU_CTRL);
}

static bool sysmmu_block(void __iomem *sfrbase)
{
	int i = 120;

	__raw_writel(CTRL_BLOCK, sfrbase + REG_MMU_CTRL);
	while ((i > 0) && !(__raw_readl(sfrbase + REG_MMU_STATUS) & 1))
		--i;

	if (!(__raw_readl(sfrbase + REG_MMU_STATUS) & 1)) {
		sysmmu_unblock(sfrbase);
		return false;
	}

	return true;
}

static void __sysmmu_tlb_invalidate(void __iomem *sfrbase)
{
	__raw_writel(0x1, sfrbase + REG_MMU_FLUSH);
}

static void __sysmmu_tlb_invalidate_entry(void __iomem *sfrbase,
					  dma_addr_t iova)
{
	__raw_writel(iova | 0x1, sfrbase + REG_MMU_FLUSH_ENTRY);
}

static void __sysmmu_set_ptbase(void __iomem *sfrbase,
				       unsigned long pgd)
{
	__raw_writel(pgd, sfrbase + REG_PT_BASE_ADDR);

	__sysmmu_tlb_invalidate(sfrbase);
}

static void __sysmmu_set_prefbuf(void __iomem *pbufbase, unsigned long base,
					unsigned long size, int idx)
{
	__raw_writel(base, pbufbase + idx * 8);
	__raw_writel(size - 1 + base,  pbufbase + 4 + idx * 8);
}

/*
 * Offset of prefetch buffer setting registers are different
 * between SysMMU 3.1 and 3.2. 3.3 has a single prefetch buffer setting.
 */
static unsigned short
	pbuf_offset[NUM_MINOR_OF_SYSMMU_V3] = {0x04C, 0x04C, 0x070, 0x410};

/**
 * __sysmmu_sort_prefbuf - sort the given @prefbuf in descending order.
 * @prefbuf: array of buffer information
 * @nbufs: number of elements of @prefbuf
 * @check_size: whether to compare buffer sizes. See below description.
 *
 * return value is valid if @check_size is true. If the size of first buffer
 * in @prefbuf is larger than or equal to the sum of the sizes of the other
 * buffers, returns 1. If the size of the first buffer is smaller than the
 * sum of other sizes, returns -1. Returns 0, otherwise.
 */
static int __sysmmu_sort_prefbuf(struct sysmmu_prefbuf prefbuf[],
						int nbufs, bool check_size)
{
	int i;

	for (i = 0; i < nbufs; i++) {
		int j;
		for (j = i + 1; j < nbufs; j++)
			if (prefbuf[i].size < prefbuf[j].size)
				swap(prefbuf[i], prefbuf[j]);
	}

	if (check_size) {
		unsigned long sum = 0;
		for (i = 1; i < nbufs; i++)
			sum += prefbuf[i].size;

		if (prefbuf[0].size < sum)
			i = -1;
		else if (prefbuf[0].size >= (sum * 2))
			i = 1;
		else
			i = 0;
	}

	return i;
}

static void __exynos_sysmmu_set_pbuf_ver31(struct sysmmu_drvdata *drvdata,
			int idx, int nbufs, struct sysmmu_prefbuf prefbuf[])
{
	unsigned long cfg =
		__raw_readl(drvdata->sfrbases[idx] + REG_MMU_CFG) & CFG_MASK;

	if (nbufs > 1) {
		unsigned long base = prefbuf[1].base;
		unsigned long end = prefbuf[1].base + prefbuf[1].size;

		/* merging buffers from the second to the last */
		while (nbufs-- > 2) {
			base = min(base, prefbuf[nbufs - 1].base);
			end = max(end, prefbuf[nbufs - 1].base +
					prefbuf[nbufs - 1].size);
		}

		/* Separate PB mode */
		cfg |= 2 << 28;

		__sysmmu_set_prefbuf(drvdata->sfrbases[idx] + pbuf_offset[1],
					base, end - base, 1);

		drvdata->num_pbufs = 2;
		drvdata->pbufs[0] = prefbuf[0];
		drvdata->pbufs[1] = prefbuf[1];

	} else {
		/* Combined PB mode */
		cfg |= 3 << 28;
		drvdata->num_pbufs = 1;
		drvdata->pbufs[0] = prefbuf[0];
	}

	__raw_writel(cfg, drvdata->sfrbases[idx] + REG_MMU_CFG);

	__sysmmu_set_prefbuf(drvdata->sfrbases[idx] + pbuf_offset[1],
				prefbuf[0].base, prefbuf[0].size, 0);
}

static void __exynos_sysmmu_set_pbuf_ver32(struct sysmmu_drvdata *drvdata,
			int idx, int nbufs, struct sysmmu_prefbuf prefbuf[])
{
	int i;
	unsigned long cfg =
		__raw_readl(drvdata->sfrbases[idx] + REG_MMU_CFG) & CFG_MASK;

	cfg |= 7 << 16; /* enabling PB0 ~ PB2 */

	/* This is common to all cases below */
	drvdata->pbufs[0] = prefbuf[0];

	switch (nbufs) {
	case 1:
		/* Combined PB mode (0 ~ 2) */
		cfg |= 1 << 19;
		drvdata->num_pbufs = 1;
		break;
	case 2:
		/* Combined PB mode (0 ~ 1) */
		cfg |= 1 << 21;
		drvdata->num_pbufs = 2;
		drvdata->pbufs[1] = prefbuf[1];
		break;
	case 3:
		drvdata->num_pbufs = 3;
		drvdata->pbufs[1] = prefbuf[1];
		drvdata->pbufs[2] = prefbuf[2];

		__sysmmu_sort_prefbuf(drvdata->pbufs, 3, false);
		swap(drvdata->pbufs[0], drvdata->pbufs[2]);

		break;
	default:
		drvdata->pbufs[2].base = prefbuf[2].base;
		/* drvdata->size is used for end address temporarily */
		drvdata->pbufs[2].size = prefbuf[2].base + prefbuf[2].size;

		/* Merging all buffers from the third to the last */
		while (nbufs-- > 3) {
			drvdata->pbufs[2].base = min(drvdata->pbufs[2].base,
						prefbuf[nbufs - 1].base);
			drvdata->pbufs[2].size = max(drvdata->pbufs[2].size,
						prefbuf[nbufs - 1].base +
						prefbuf[nbufs - 1].size);
		}

		drvdata->num_pbufs = 3;
		drvdata->pbufs[1] = prefbuf[1];
		drvdata->pbufs[2].size = drvdata->pbufs[2].size -
					drvdata->pbufs[2].base;
	}

	for (i = 0; i < drvdata->num_pbufs; i++)
		__sysmmu_set_prefbuf(drvdata->sfrbases[idx] + pbuf_offset[2],
			drvdata->pbufs[i].base, drvdata->pbufs[i].size, i);

	__raw_writel(cfg, drvdata->sfrbases[idx] + REG_MMU_CFG);
}

static void __exynos_sysmmu_set_pbuf_ver33(struct sysmmu_drvdata *drvdata,
			int idx, int nbufs, struct sysmmu_prefbuf prefbuf[])
{
	static char pbcfg[6][6] = {
		{7, 7, 7, 7, 7, 7}, {7, 7, 7, 7, 7, 7}, {2, 2, 3, 7, 7, 7},
		{1, 2, 3, 4, 7, 7}, {7, 7, 7, 7, 7, 7}, {2, 2, 3, 4, 5, 6}
		};
	int pbselect;
	int cmp, i;
	long num_pb = __raw_readl(drvdata->sfrbases[idx] + REG_PB_INFO) & 0xFF;

	if (nbufs > num_pb)
		nbufs = num_pb; /* ignoring the rest of buffers */

	for (i = 0; i < nbufs; i++)
		drvdata->pbufs[i] = prefbuf[i];
	drvdata->num_pbufs = nbufs;

	cmp = __sysmmu_sort_prefbuf(drvdata->pbufs, nbufs, true);

	pbselect = num_pb - nbufs;
	if (num_pb == 6) {
		if ((nbufs == 3) && (cmp == 1))
			pbselect = 4;
		else if (nbufs < 3)
			pbselect = 5;
	} else if ((num_pb == 3) && (nbufs < 3)) {
		pbselect = 1;
	}

	__raw_writel(pbselect, drvdata->sfrbases[idx] + REG_PB_LMM);

	/* Configure prefech buffers */
	for (i = 0; i < nbufs; i++) {
		__raw_writel(i, drvdata->sfrbases[idx] + REG_PB_INDICATE);
		__raw_writel(drvdata->pbufs[i].config | 1,
				drvdata->sfrbases[idx] + REG_PB_CFG);
		__sysmmu_set_prefbuf(drvdata->sfrbases[idx] + pbuf_offset[3],
			drvdata->pbufs[i].base, drvdata->pbufs[i].size, 0);
	}

	/* Disable prefetch buffers that is not set */
	for (cmp = pbcfg[num_pb - 1][nbufs - 1] - nbufs; cmp > 0; cmp--) {
		__raw_writel(cmp + nbufs - 1,
				drvdata->sfrbases[idx] + REG_PB_INDICATE);
		__raw_writel(0, drvdata->sfrbases[idx] + REG_PB_CFG);
	}
}

static void (*func_set_pbuf[NUM_MINOR_OF_SYSMMU_V3])
	(struct sysmmu_drvdata *, int, int, struct sysmmu_prefbuf []) = {
		__exynos_sysmmu_set_pbuf_ver31,
		__exynos_sysmmu_set_pbuf_ver31,
		__exynos_sysmmu_set_pbuf_ver32,
		__exynos_sysmmu_set_pbuf_ver33,
};

void exynos_sysmmu_set_pbuf(struct device *dev, int nbufs,
				struct sysmmu_prefbuf prefbuf[])
{
	struct device *sysmmu;
	int nsfrs;

	if (WARN_ON(nbufs < 1))
		return;

	for_each_sysmmu(dev, sysmmu) {
		unsigned long flags;
		struct sysmmu_drvdata *drvdata;

		drvdata = dev_get_drvdata(sysmmu);

		spin_lock_irqsave(&drvdata->lock, flags);
		if (!is_sysmmu_active(drvdata)) {
			spin_unlock_irqrestore(&drvdata->lock, flags);
			continue;
		}

		for (nsfrs = 0; nsfrs < drvdata->nsfrs; nsfrs++) {
			int min;

			if (!has_sysmmu_capable_pbuf(
					drvdata, nsfrs, prefbuf, &min))
				continue;

			if (sysmmu_block(drvdata->sfrbases[nsfrs])) {
				func_set_pbuf[min](drvdata, nsfrs,
							nbufs, prefbuf);
				sysmmu_unblock(drvdata->sfrbases[nsfrs]);
			}
		} /* while (nsfrs < drvdata->nsfrs) */
		spin_unlock_irqrestore(&drvdata->lock, flags);
	}
}

void exynos_sysmmu_set_prefbuf(struct device *dev,
				unsigned long base0, unsigned long size0,
				unsigned long base1, unsigned long size1)
{
	struct sysmmu_prefbuf pbuf[2];
	int nbufs = 1;

	pbuf[0].base = base0;
	pbuf[0].size = size0;
	if (base1) {
		pbuf[1].base = base1;
		pbuf[1].size = size1;
		nbufs = 2;
	}

	exynos_sysmmu_set_pbuf(dev, nbufs, pbuf);
}

static void __sysmmu_restore_state(struct sysmmu_drvdata *drvdata)
{
	int i, min;

	for (i = 0; i < drvdata->nsfrs; i++) {
		if (__sysmmu_version(drvdata, i, &min) == 3) {
			if (sysmmu_block(drvdata->sfrbases[i])) {
				func_set_pbuf[min](drvdata, i,
					drvdata->num_pbufs, drvdata->pbufs);
				sysmmu_unblock(drvdata->sfrbases[i]);
			}
		}
	}
}

static void __set_fault_handler(struct sysmmu_drvdata *data,
					sysmmu_fault_handler_t handler)
{
	data->fault_handler = handler;
}

void exynos_sysmmu_set_fault_handler(struct device *dev,
					sysmmu_fault_handler_t handler)
{
	struct exynos_iommu_owner *owner = dev->archdata.iommu;
	struct device *sysmmu;
	unsigned long flags;

	spin_lock_irqsave(&owner->lock, flags);

	for_each_sysmmu(dev, sysmmu)
		__set_fault_handler(dev_get_drvdata(sysmmu), handler);

	spin_unlock_irqrestore(&owner->lock, flags);
}

static int default_fault_handler(struct device *dev, const char *mmuname,
					enum exynos_sysmmu_inttype itype,
					unsigned long pgtable_base,
					unsigned long fault_addr)
{
	unsigned long *ent;

	if ((itype >= SYSMMU_FAULTS_NUM) || (itype < SYSMMU_PAGEFAULT))
		itype = SYSMMU_FAULT_UNKNOWN;

	pr_err("%s occured at 0x%lx by '%s'(Page table base: 0x%lx)\n",
		sysmmu_fault_name[itype], fault_addr, mmuname, pgtable_base);

	ent = section_entry(__va(pgtable_base), fault_addr);
	pr_err("\tLv1 entry: 0x%lx\n", *ent);

	if (lv1ent_page(ent)) {
		ent = page_entry(ent, fault_addr);
		pr_err("\t Lv2 entry: 0x%lx\n", *ent);
	}

	pr_err("Generating Kernel OOPS... because it is unrecoverable.\n");

	BUG();

	return 0;
}

static irqreturn_t exynos_sysmmu_irq(int irq, void *dev_id)
{
	/* SYSMMU is in blocked when interrupt occurred. */
	struct sysmmu_drvdata *drvdata = dev_id;
	struct exynos_iommu_owner *owner = NULL;
	enum exynos_sysmmu_inttype itype;
	unsigned long addr = -1;
	const char *mmuname = NULL;
	int i, ret = -ENOSYS;

	if (drvdata->master)
		owner = drvdata->master->archdata.iommu;

	if (owner)
		spin_lock(&owner->lock);

	WARN_ON(!is_sysmmu_active(drvdata));

	for (i = 0; i < drvdata->nsfrs; i++) {
		struct resource *irqres;
		irqres = platform_get_resource(
				to_platform_device(drvdata->sysmmu),
				IORESOURCE_IRQ, i);
		if (irqres && ((int)irqres->start == irq)) {
			mmuname = irqres->name;
			break;
		}
	}

	if (i == drvdata->nsfrs) {
		itype = SYSMMU_FAULT_UNKNOWN;
	} else {
		itype = (enum exynos_sysmmu_inttype)
			__ffs(__raw_readl(
					drvdata->sfrbases[i] + REG_INT_STATUS));
		if (WARN_ON(!((itype >= 0) && (itype < SYSMMU_FAULT_UNKNOWN))))
			itype = SYSMMU_FAULT_UNKNOWN;
		else
			addr = __raw_readl(
				drvdata->sfrbases[i] + fault_reg_offset[itype]);
	}

	if (drvdata->domain) /* owner is always set if drvdata->domain exists */
		ret = report_iommu_fault(drvdata->domain,
					owner->dev, addr, itype);

	if ((ret == -ENOSYS) && drvdata->fault_handler) {
		unsigned long base = drvdata->pgtable;
		if (itype != SYSMMU_FAULT_UNKNOWN)
			base = __raw_readl(
				drvdata->sfrbases[i] + REG_PT_BASE_ADDR);
		ret = drvdata->fault_handler(
					owner ? owner->dev : drvdata->sysmmu,
					mmuname ? mmuname : drvdata->dbgname,
					itype, base, addr);
	}

	if (!ret && (itype != SYSMMU_FAULT_UNKNOWN))
		__raw_writel(1 << itype, drvdata->sfrbases[i] + REG_INT_CLEAR);
	else
		dev_dbg(owner ? owner->dev : drvdata->sysmmu,
				"%s is not handled by %s\n",
				sysmmu_fault_name[itype], drvdata->dbgname);

	sysmmu_unblock(drvdata->sfrbases[i]);

	if (owner)
		spin_unlock(&owner->lock);

	return IRQ_HANDLED;
}

static void __sysmmu_disable_nocount(struct sysmmu_drvdata *drvdata)
{
		int i;

		for (i = 0; i < drvdata->nsfrs; i++)
			__raw_writel(CTRL_DISABLE,
				drvdata->sfrbases[i] + REG_MMU_CTRL);

		clk_disable(drvdata->clk);
}

static bool __sysmmu_disable(struct sysmmu_drvdata *drvdata)
{
	bool disabled;
	unsigned long flags;

	spin_lock_irqsave(&drvdata->lock, flags);

	disabled = set_sysmmu_inactive(drvdata);

	if (disabled) {
		drvdata->pgtable = 0;
		drvdata->domain = NULL;

		if (drvdata->runtime_active)
			__sysmmu_disable_nocount(drvdata);

		dev_dbg(drvdata->sysmmu, "Disabled %s\n", drvdata->dbgname);
	} else  {
		dev_dbg(drvdata->sysmmu, "%d times left to disable %s\n",
					drvdata->activations, drvdata->dbgname);
	}

	spin_unlock_irqrestore(&drvdata->lock, flags);

	return disabled;
}

static void __sysmmu_init_config(struct sysmmu_drvdata *drvdata, int idx)
{
	unsigned long cfg = CFG_LRU | CFG_QOS(drvdata->qos);
	int maj, min = 0;

	maj = __sysmmu_version(drvdata, idx, &min);
	if ((maj == 1) || (maj == 2))
		goto set_cfg;

	BUG_ON(maj != 3);

	cfg |= CFG_SHAREABLE;
	if (min < 2)
		goto set_cfg;

	BUG_ON(min > 3);

	cfg |= CFG_FLPDCACHE;
	cfg |= (min == 2) ? CFG_SYSSEL : CFG_ACGEN;

	func_set_pbuf[min](drvdata, idx, drvdata->num_pbufs, drvdata->pbufs);
set_cfg:
	cfg |= __raw_readl(drvdata->sfrbases[idx] + REG_MMU_CFG) & ~CFG_MASK;
	__raw_writel(cfg, drvdata->sfrbases[idx] + REG_MMU_CFG);
}

static void __sysmmu_enable_nocount(struct sysmmu_drvdata *drvdata)
{
	int i;

	clk_enable(drvdata->clk);

	for (i = 0; i < drvdata->nsfrs; i++) {
		BUG_ON(__raw_readl(drvdata->sfrbases[i] + REG_MMU_CTRL)
								& CTRL_ENABLE);

		__sysmmu_init_config(drvdata, i);

		__sysmmu_set_ptbase(drvdata->sfrbases[i], drvdata->pgtable);

		__raw_writel(CTRL_ENABLE, drvdata->sfrbases[i] + REG_MMU_CTRL);
	}
}

static int __sysmmu_enable(struct sysmmu_drvdata *drvdata,
			unsigned long pgtable, struct iommu_domain *domain)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&drvdata->lock, flags);
	if (set_sysmmu_active(drvdata)) {
		drvdata->pgtable = pgtable;
		drvdata->domain = domain;

		if (drvdata->runtime_active)
			__sysmmu_enable_nocount(drvdata);

		dev_dbg(drvdata->sysmmu, "Enabled %s\n", drvdata->dbgname);
	} else {
		ret = (pgtable == drvdata->pgtable) ? 1 : -EBUSY;

		dev_dbg(drvdata->sysmmu, "%s is already enabled\n",
							drvdata->dbgname);
	}

	if (WARN_ON(ret < 0))
		set_sysmmu_inactive(drvdata); /* decrement count */

	spin_unlock_irqrestore(&drvdata->lock, flags);

	return ret;
}

/* __exynos_sysmmu_enable: Enables System MMU
 *
 * returns -error if an error occurred and System MMU is not enabled,
 * 0 if the System MMU has been just enabled and 1 if System MMU was already
 * enabled before.
 */
static int __exynos_sysmmu_enable(struct device *dev, unsigned long pgtable,
				struct iommu_domain *domain)
{
	int ret = 0;
	unsigned long flags;
	struct exynos_iommu_owner *owner = dev->archdata.iommu;
	struct device *sysmmu;

	BUG_ON(!has_sysmmu(dev));

	spin_lock_irqsave(&owner->lock, flags);

	for_each_sysmmu(dev, sysmmu) {
		struct sysmmu_drvdata *drvdata = dev_get_drvdata(sysmmu);
		ret = __sysmmu_enable(drvdata, pgtable, domain);
		if (ret < 0) {
			struct device *iter;
			for_each_sysmmu_until(dev, iter, sysmmu) {
				drvdata = dev_get_drvdata(iter);
				__sysmmu_disable(drvdata);
			}
		} else {
			drvdata->master = dev;
		}
	}

	spin_unlock_irqrestore(&owner->lock, flags);

	return ret;
}

int exynos_sysmmu_enable(struct device *dev, unsigned long pgtable)
{
	int ret;

	BUG_ON(!memblock_is_memory(pgtable));

	ret = __exynos_sysmmu_enable(dev, pgtable, NULL);

	return ret;
}

bool exynos_sysmmu_disable(struct device *dev)
{
	unsigned long flags;
	bool disabled = true;
	struct exynos_iommu_owner *owner = dev->archdata.iommu;
	struct device *sysmmu;

	BUG_ON(!has_sysmmu(dev));

	spin_lock_irqsave(&owner->lock, flags);

	/* Every call to __sysmmu_disable() must return same result */
	for_each_sysmmu(dev, sysmmu) {
		struct sysmmu_drvdata *drvdata = dev_get_drvdata(sysmmu);
		disabled = __sysmmu_disable(drvdata);
		if (disabled)
			drvdata->master = NULL;
	}

	spin_unlock_irqrestore(&owner->lock, flags);

	return disabled;
}

void exynos_sysmmu_tlb_invalidate(struct device *dev)
{
	struct device *sysmmu;

	for_each_sysmmu(dev, sysmmu) {
		unsigned long flags;
		struct sysmmu_drvdata *drvdata;

		drvdata = dev_get_drvdata(sysmmu);

		spin_lock_irqsave(&drvdata->lock, flags);
		if (is_sysmmu_active(drvdata) &&
				drvdata->runtime_active) {
			int i;
			for (i = 0; i < drvdata->nsfrs; i++) {
				if (sysmmu_block(drvdata->sfrbases[i])) {
					__sysmmu_tlb_invalidate(
							drvdata->sfrbases[i]);
					sysmmu_unblock(drvdata->sfrbases[i]);
				}
			}
		} else {
			dev_dbg(dev,
				"%s is disabled. Skipping TLB invalidation\n",
				drvdata->dbgname);
		}
		spin_unlock_irqrestore(&drvdata->lock, flags);
	}
}

static void sysmmu_tlb_invalidate_entry(struct device *dev, dma_addr_t iova)
{
	struct device *sysmmu;

	for_each_sysmmu(dev, sysmmu) {
		unsigned long flags;
		struct sysmmu_drvdata *drvdata;

		drvdata = dev_get_drvdata(sysmmu);

		spin_lock_irqsave(&drvdata->lock, flags);
		if (is_sysmmu_active(drvdata) &&
				drvdata->runtime_active) {
			int i;
			for (i = 0; i < drvdata->nsfrs; i++)
				__sysmmu_tlb_invalidate_entry(
						drvdata->sfrbases[i], iova);
		} else {
			dev_dbg(dev,
			"%s is disabled. Skipping TLB invalidation @ %#x\n",
			drvdata->dbgname, iova);
		}
		spin_unlock_irqrestore(&drvdata->lock, flags);
	}
}

static int __init __sysmmu_init_clock(struct device *sysmmu,
					struct sysmmu_drvdata *drvdata,
					struct device *master)
{
	struct sysmmu_platform_data *platdata = dev_get_platdata(sysmmu);
	char *conid;
	struct clk *parent_clk;
	int ret;

	drvdata->clk = clk_get(sysmmu, "sysmmu");
	if (IS_ERR(drvdata->clk)) {
		dev_dbg(sysmmu, "No gating clock found.\n");
		drvdata->clk = NULL;
		return 0;
	}

	if (!master)
		return 0;

	conid = platdata->clockname;
	if (!conid) {
		dev_dbg(sysmmu, "No parent clock specified.\n");
		return 0;
	}

	parent_clk = clk_get(master, conid);
	if (IS_ERR(parent_clk)) {
		parent_clk = clk_get(NULL, conid);
		if (IS_ERR(parent_clk)) {
			clk_put(drvdata->clk);
			dev_err(sysmmu, "No parent clock '%s,%s' found\n",
				dev_name(master), conid);
			return PTR_ERR(parent_clk);
		}
	}

	ret = clk_set_parent(drvdata->clk, parent_clk);
	if (ret) {
		clk_put(drvdata->clk);
		dev_err(sysmmu, "Failed to set parent clock '%s,%s'\n",
				dev_name(master), conid);
	}

	clk_put(parent_clk);

	return ret;
}

#define has_more_master(dev) ((unsigned long)dev->archdata.iommu & 1)
#define master_initialized(dev) (!((unsigned long)dev->archdata.iommu & 1) \
				&& ((unsigned long)dev->archdata.iommu & ~1))

static struct device * __init __sysmmu_init_master(
				struct device *sysmmu, struct device *dev) {
	struct exynos_iommu_owner *owner;
	struct device *master = (struct device *)((unsigned long)dev & ~1);
	int ret;

	if (!master)
		return NULL;

	/*
	 * has_more_master() call to the main master device returns false while
	 * the same call to the other master devices (shared master devices)
	 * return true.
	 * Shared master devices are moved after 'sysmmu' in the DPM list while
	 * 'sysmmu' is moved before the master device not to break the order of
	 * suspend/resume.
	 */
	if (has_more_master(master)) {
		void *pret;
		pret = __sysmmu_init_master(sysmmu, master->archdata.iommu);
		if (IS_ERR(pret))
			return pret;

		ret = device_move(master, sysmmu, DPM_ORDER_DEV_AFTER_PARENT);
		if (ret)
			return ERR_PTR(ret);
	} else {
		struct device *child = master;
		/* Finding the topmost System MMU in the hierarchy of master. */
		while (child && child->parent && is_sysmmu(child->parent))
			child = child->parent;

		ret = device_move(child, sysmmu, DPM_ORDER_PARENT_BEFORE_DEV);
		if (ret)
			return ERR_PTR(ret);

		if (master_initialized(master)) {
			dev_dbg(sysmmu,
				"Assigned initialized master device %s.\n",
							dev_name(master));
			return master;
		}
	}

	/*
	 * There must not be a master device which is initialized and
	 * has a link to another master device.
	 */
	BUG_ON(master_initialized(master));

	owner = devm_kzalloc(sysmmu, sizeof(*owner), GFP_KERNEL);
	if (!owner) {
		dev_err(sysmmu, "Failed to allcoate iommu data.\n");
		return ERR_PTR(-ENOMEM);
	}

	INIT_LIST_HEAD(&owner->client);
	owner->dev = master;
	spin_lock_init(&owner->lock);

	master->archdata.iommu = owner;

	ret = exynos_create_iovmm(master);
	if (ret) {
		dev_err(sysmmu, "Failed create IOVMM context.\n");
		return ERR_PTR(ret);
	}

	dev_dbg(sysmmu, "Assigned master device %s.\n", dev_name(master));

	return master;
}

static int __init __sysmmu_setup(struct device *sysmmu,
				struct sysmmu_drvdata *drvdata)
{
	struct sysmmu_platform_data *platdata = dev_get_platdata(sysmmu);
	struct device *master;
	int ret;

	drvdata->ver = platdata->ver;
	drvdata->dbgname = platdata->dbgname;
	drvdata->qos = platdata->qos;
	drvdata->prop = platdata->prop;
	if ((drvdata->qos < 0) || (drvdata->qos > 15))
		drvdata->qos = 15;
	drvdata->pbufs[0].base = IOVA_START;
	drvdata->pbufs[0].size = IOVM_SIZE;
	drvdata->pbufs[0].config = (drvdata->prop & SYSMMU_PROP_READ) ?
				SYSMMU_PBUFCFG_READ : SYSMMU_PBUFCFG_WRITE;
	drvdata->pbufs[0].config |= SYSMMU_PBUFCFG_TLB_UPDATE |
			SYSMMU_PBUFCFG_ASCENDING | SYSMMU_PBUFCFG_PREFETCH;
	drvdata->num_pbufs = 1;

	master = __sysmmu_init_master(sysmmu, sysmmu->archdata.iommu);
	if (!master) {
		dev_dbg(sysmmu, "No master device is assigned\n");
	} else if (IS_ERR(master)) {
		dev_err(sysmmu, "Failed to initialize master device.\n");
		return PTR_ERR(master);
	}

	ret = __sysmmu_init_clock(sysmmu, drvdata, master);
	if (ret)
		dev_err(sysmmu, "Failed to initialize gating clocks\n");

	return ret;
}

static void __create_debugfs_entry(struct sysmmu_drvdata *drvdata);

static int __init exynos_sysmmu_probe(struct platform_device *pdev)
{
	int i, ret;
	struct device *dev = &pdev->dev;
	struct sysmmu_drvdata *data;

	data = devm_kzalloc(dev,
			sizeof(*data) + sizeof(*data->sfrbases) *
				(pdev->num_resources / 2),
			GFP_KERNEL);
	if (!data) {
		dev_err(dev, "Not enough memory\n");
		return -ENOMEM;
	}

	data->nsfrs = pdev->num_resources / 2;
	data->sfrbases = (void __iomem **)(data + 1);

	for (i = 0; i < data->nsfrs; i++) {
		struct resource *res;
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res) {
			dev_err(dev, "Unable to find IOMEM region\n");
			return -ENOENT;
		}

		data->sfrbases[i] = devm_request_and_ioremap(dev, res);
		if (!data->sfrbases[i]) {
			dev_err(dev, "Unable to map IOMEM @ PA:%#x\n",
							res->start);
			return -EBUSY;
		}
	}

	for (i = 0; i < data->nsfrs; i++) {
		ret = platform_get_irq(pdev, i);
		if (ret <= 0) {
			dev_err(dev, "Unable to find IRQ resource\n");
			return ret;
		}

		ret = devm_request_irq(dev, ret, exynos_sysmmu_irq, 0,
					dev_name(dev), data);
		if (ret) {
			dev_err(dev, "Unabled to register interrupt handler\n");
			return ret;
		}
	}

	pm_runtime_enable(dev);

	ret = __sysmmu_setup(dev, data);
	if (!ret) {
		data->runtime_active = !pm_runtime_enabled(dev);
		data->sysmmu = dev;
		spin_lock_init(&data->lock);

		__set_fault_handler(data, &default_fault_handler);

		__create_debugfs_entry(data);

		platform_set_drvdata(pdev, data);

		dev->archdata.iommu = &sysmmu_placeholder;
		dev_dbg(dev, "(%s) Initialized\n", data->dbgname);
	}

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int sysmmu_suspend(struct device *dev)
{
	struct sysmmu_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned long flags;
	spin_lock_irqsave(&drvdata->lock, flags);
	if (is_sysmmu_active(drvdata) &&
		(!pm_runtime_enabled(dev) || drvdata->runtime_active))
		__sysmmu_disable_nocount(drvdata);
	spin_unlock_irqrestore(&drvdata->lock, flags);
	return 0;
}

static int sysmmu_resume(struct device *dev)
{
	struct sysmmu_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned long flags;
	spin_lock_irqsave(&drvdata->lock, flags);
	if (is_sysmmu_active(drvdata) &&
		(!pm_runtime_enabled(dev) || drvdata->runtime_active)) {
		__sysmmu_enable_nocount(drvdata);
		__sysmmu_restore_state(drvdata);
	}
	spin_unlock_irqrestore(&drvdata->lock, flags);
	return 0;
}
#endif

#ifdef CONFIG_PM_RUNTIME
static int sysmmu_runtime_suspend(struct device *dev)
{
	struct sysmmu_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned long flags;
	spin_lock_irqsave(&drvdata->lock, flags);
	if (is_sysmmu_active(drvdata))
		__sysmmu_disable_nocount(drvdata);
	drvdata->runtime_active = false;
	spin_unlock_irqrestore(&drvdata->lock, flags);
	return 0;
}

static int sysmmu_runtime_resume(struct device *dev)
{
	struct sysmmu_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned long flags;
	spin_lock_irqsave(&drvdata->lock, flags);
	drvdata->runtime_active = true;
	if (is_sysmmu_active(drvdata))
		__sysmmu_enable_nocount(drvdata);
	spin_unlock_irqrestore(&drvdata->lock, flags);
	return 0;
}
#endif

static const struct dev_pm_ops __pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sysmmu_suspend, sysmmu_resume)
	SET_RUNTIME_PM_OPS(sysmmu_runtime_suspend, sysmmu_runtime_resume, NULL)
};

static struct platform_driver exynos_sysmmu_driver __refdata = {
	.probe		= exynos_sysmmu_probe,
	.driver		= {
		.owner		= THIS_MODULE,
		.name		= MODULE_NAME,
		.pm		= &__pm_ops,
	}
};

static inline void pgtable_flush(void *vastart, void *vaend)
{
	dmac_flush_range(vastart, vaend);
	outer_flush_range(virt_to_phys(vastart),
				virt_to_phys(vaend));
}

static int exynos_iommu_domain_init(struct iommu_domain *domain)
{
	struct exynos_iommu_domain *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->pgtable = (unsigned long *)__get_free_pages(
						GFP_KERNEL | __GFP_ZERO, 2);
	if (!priv->pgtable)
		goto err_pgtable;

	priv->lv2entcnt = (short *)__get_free_pages(
						GFP_KERNEL | __GFP_ZERO, 1);
	if (!priv->lv2entcnt)
		goto err_counter;

	pgtable_flush(priv->pgtable, priv->pgtable + NUM_LV1ENTRIES);

	spin_lock_init(&priv->lock);
	spin_lock_init(&priv->pgtablelock);
	INIT_LIST_HEAD(&priv->clients);

	domain->priv = priv;
	return 0;

err_counter:
	free_pages((unsigned long)priv->pgtable, 2);
err_pgtable:
	kfree(priv);
	return -ENOMEM;
}

static void exynos_iommu_domain_destroy(struct iommu_domain *domain)
{
	struct exynos_iommu_domain *priv = domain->priv;
	struct exynos_iommu_owner *owner, *n;
	unsigned long flags;
	int i;

	WARN_ON(!list_empty(&priv->clients));

	spin_lock_irqsave(&priv->lock, flags);

	list_for_each_entry_safe(owner, n, &priv->clients, client) {
		while (!exynos_sysmmu_disable(owner->dev))
			; /* until System MMU is actually disabled */
		list_del_init(&owner->client);
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	for (i = 0; i < NUM_LV1ENTRIES; i++)
		if (lv1ent_page(priv->pgtable + i))
			kmem_cache_free(lv2table_kmem_cache,
					__va(lv2table_base(priv->pgtable + i)));

	free_pages((unsigned long)priv->pgtable, 2);
	free_pages((unsigned long)priv->lv2entcnt, 1);
	kfree(domain->priv);
	domain->priv = NULL;
}

static int exynos_iommu_attach_device(struct iommu_domain *domain,
				   struct device *dev)
{
	struct exynos_iommu_owner *owner = dev->archdata.iommu;
	struct exynos_iommu_domain *priv = domain->priv;
	unsigned long flags;
	int ret;

	if (WARN_ON(!list_empty(&owner->client))) {
		bool found = false;
		struct exynos_iommu_owner *tmpowner;

		spin_lock_irqsave(&priv->lock, flags);
		list_for_each_entry(tmpowner, &priv->clients, client) {
			if (tmpowner == owner) {
				found = true;
				break;
			}
		}
		spin_unlock_irqrestore(&priv->lock, flags);

		if (!found) {
			dev_err(dev, "%s: Already attached to another domain\n",
								__func__);
			return -EBUSY;
		}

		dev_dbg(dev, "%s: Already attached to this domain\n", __func__);
		return 0;
	}

	spin_lock_irqsave(&priv->lock, flags);

	ret = __exynos_sysmmu_enable(dev, __pa(priv->pgtable), domain);

	/*
	 * __exynos_sysmmu_enable() returns 1
	 * if the System MMU of dev is already enabled
	 */
	BUG_ON(ret > 0);

	list_add_tail(&owner->client, &priv->clients);

	spin_unlock_irqrestore(&priv->lock, flags);

	if (ret < 0)
		dev_err(dev, "%s: Failed to attach IOMMU with pgtable %#lx\n",
				__func__, __pa(priv->pgtable));
	else
		dev_dbg(dev, "%s: Attached new IOMMU with pgtable 0x%lx\n",
					__func__, __pa(priv->pgtable));

	return ret;
}

static void exynos_iommu_detach_device(struct iommu_domain *domain,
				    struct device *dev)
{
	struct exynos_iommu_owner *owner, *n;
	struct exynos_iommu_domain *priv = domain->priv;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	list_for_each_entry_safe(owner, n, &priv->clients, client) {
		if (owner == dev->archdata.iommu) {
			if (exynos_sysmmu_disable(dev))
				list_del_init(&owner->client);
			else
				BUG();
			break;
		}
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	if (owner == dev->archdata.iommu)
		dev_dbg(dev, "%s: Detached IOMMU with pgtable %#lx\n",
					__func__, __pa(priv->pgtable));
	else
		dev_dbg(dev, "%s: No IOMMU is attached\n", __func__);
}

static unsigned long *alloc_lv2entry(unsigned long *sent, unsigned long iova,
					short *pgcounter)
{
	if (lv1ent_fault(sent)) {
		unsigned long *pent;

		pent = kmem_cache_zalloc(lv2table_kmem_cache, GFP_ATOMIC);
		BUG_ON((unsigned long)pent & (LV2TABLE_SIZE - 1));
		if (!pent)
			return ERR_PTR(-ENOMEM);

		*sent = mk_lv1ent_page(__pa(pent));
		kmemleak_ignore(pent);
		*pgcounter = NUM_LV2ENTRIES;
		pgtable_flush(pent, pent + NUM_LV2ENTRIES);
		pgtable_flush(sent, sent + 1);
	} else if (lv1ent_section(sent)) {
		return ERR_PTR(-EADDRINUSE);
	}

	return page_entry(sent, iova);
}

static int lv1set_section(unsigned long *sent, phys_addr_t paddr, short *pgcnt)
{
	if (lv1ent_section(sent))
		return -EADDRINUSE;

	if (lv1ent_page(sent)) {
		if (*pgcnt != NUM_LV2ENTRIES)
			return -EADDRINUSE;

		kmem_cache_free(lv2table_kmem_cache, page_entry(sent, 0));

		*pgcnt = 0;
	}

	*sent = mk_lv1ent_sect(paddr);

	pgtable_flush(sent, sent + 1);

	return 0;
}

static int lv2set_page(unsigned long *pent, phys_addr_t paddr, size_t size,
								short *pgcnt)
{
	if (size == SPAGE_SIZE) {
		if (!lv2ent_fault(pent))
			return -EADDRINUSE;

		*pent = mk_lv2ent_spage(paddr);
		pgtable_flush(pent, pent + 1);
		*pgcnt -= 1;
	} else { /* size == LPAGE_SIZE */
		int i;
		for (i = 0; i < SPAGES_PER_LPAGE; i++, pent++) {
			if (!lv2ent_fault(pent)) {
				memset(pent, 0, sizeof(*pent) * i);
				return -EADDRINUSE;
			}

			*pent = mk_lv2ent_lpage(paddr);
		}
		pgtable_flush(pent - SPAGES_PER_LPAGE, pent);
		*pgcnt -= SPAGES_PER_LPAGE;
	}

	return 0;
}

static int exynos_iommu_map(struct iommu_domain *domain, unsigned long iova,
			 phys_addr_t paddr, size_t size, int prot)
{
	struct exynos_iommu_domain *priv = domain->priv;
	unsigned long *entry;
	unsigned long flags;
	int ret = -ENOMEM;

	BUG_ON(priv->pgtable == NULL);

	spin_lock_irqsave(&priv->pgtablelock, flags);

	entry = section_entry(priv->pgtable, iova);

	if (size == SECT_SIZE) {
		ret = lv1set_section(entry, paddr,
					&priv->lv2entcnt[lv1ent_offset(iova)]);
	} else {
		unsigned long *pent;

		pent = alloc_lv2entry(entry, iova,
					&priv->lv2entcnt[lv1ent_offset(iova)]);

		if (IS_ERR(pent))
			ret = PTR_ERR(pent);
		else
			ret = lv2set_page(pent, paddr, size,
					&priv->lv2entcnt[lv1ent_offset(iova)]);
	}

	if (ret)
		pr_err("%s: Failed(%d) to map %#x bytes @ %#lx\n",
			__func__, ret, size, iova);

	spin_unlock_irqrestore(&priv->pgtablelock, flags);

	return ret;
}

static size_t exynos_iommu_unmap(struct iommu_domain *domain,
					       unsigned long iova, size_t size)
{
	struct exynos_iommu_domain *priv = domain->priv;
	struct exynos_iommu_owner *owner;
	unsigned long flags;
	unsigned long *ent;
	size_t err_page;

	BUG_ON(priv->pgtable == NULL);

	spin_lock_irqsave(&priv->pgtablelock, flags);

	ent = section_entry(priv->pgtable, iova);

	if (lv1ent_section(ent)) {
		if (WARN_ON(size < SECT_SIZE))
			goto err;

		*ent = 0;
		pgtable_flush(ent, ent + 1);
		size = SECT_SIZE;
		goto done;
	}

	if (unlikely(lv1ent_fault(ent))) {
		if (size > SECT_SIZE)
			size = SECT_SIZE;
		goto done;
	}

	/* lv1ent_page(sent) == true here */

	ent = page_entry(ent, iova);

	if (unlikely(lv2ent_fault(ent))) {
		size = SPAGE_SIZE;
		goto done;
	}

	if (lv2ent_small(ent)) {
		*ent = 0;
		size = SPAGE_SIZE;
		pgtable_flush(ent, ent + 1);
		priv->lv2entcnt[lv1ent_offset(iova)] += 1;
		goto done;
	}

	/* lv1ent_large(ent) == true here */
	if (WARN_ON(size < LPAGE_SIZE))
		goto err;

	memset(ent, 0, sizeof(*ent) * SPAGES_PER_LPAGE);
	pgtable_flush(ent, ent + SPAGES_PER_LPAGE);

	size = LPAGE_SIZE;
	priv->lv2entcnt[lv1ent_offset(iova)] += SPAGES_PER_LPAGE;
done:
	spin_unlock_irqrestore(&priv->pgtablelock, flags);

	spin_lock_irqsave(&priv->lock, flags);
	list_for_each_entry(owner, &priv->clients, client)
		sysmmu_tlb_invalidate_entry(owner->dev, iova);
	spin_unlock_irqrestore(&priv->lock, flags);

	return size;
err:
	spin_unlock_irqrestore(&priv->pgtablelock, flags);

	err_page = (
		((unsigned long)ent - (unsigned long)priv->pgtable)
					< (NUM_LV1ENTRIES * sizeof(long))
			) ?  SECT_SIZE : LPAGE_SIZE;

	pr_err("%s: Failed due to size(%#lx) @ %#x is"\
		" smaller than page size %#x\n",
		__func__, iova, size, err_page);

	return 0;
}

static phys_addr_t exynos_iommu_iova_to_phys(struct iommu_domain *domain,
					  unsigned long iova)
{
	struct exynos_iommu_domain *priv = domain->priv;
	unsigned long *entry;
	unsigned long flags;
	phys_addr_t phys = 0;

	spin_lock_irqsave(&priv->pgtablelock, flags);

	entry = section_entry(priv->pgtable, iova);

	if (lv1ent_section(entry)) {
		phys = section_phys(entry) + section_offs(iova);
	} else if (lv1ent_page(entry)) {
		entry = page_entry(entry, iova);

		if (lv2ent_large(entry))
			phys = lpage_phys(entry) + lpage_offs(iova);
		else if (lv2ent_small(entry))
			phys = spage_phys(entry) + spage_offs(iova);
	}

	spin_unlock_irqrestore(&priv->pgtablelock, flags);

	return phys;
}

static struct iommu_ops exynos_iommu_ops = {
	.domain_init = &exynos_iommu_domain_init,
	.domain_destroy = &exynos_iommu_domain_destroy,
	.attach_dev = &exynos_iommu_attach_device,
	.detach_dev = &exynos_iommu_detach_device,
	.map = &exynos_iommu_map,
	.unmap = &exynos_iommu_unmap,
	.iova_to_phys = &exynos_iommu_iova_to_phys,
	.pgsize_bitmap = SECT_SIZE | LPAGE_SIZE | SPAGE_SIZE,
};

static int __init exynos_bus_set_iommu(void)
{
	int ret;

	lv2table_kmem_cache = kmem_cache_create("exynos-iommu-lv2table",
		LV2TABLE_SIZE, LV2TABLE_SIZE, 0, NULL);
	if (!lv2table_kmem_cache) {
		pr_err("%s: failed to create kmem cache\n", __func__);
		return -ENOMEM;
	}

	ret = bus_set_iommu(&platform_bus_type, &exynos_iommu_ops);
	if (ret) {
		kmem_cache_destroy(lv2table_kmem_cache);
		pr_err("%s: Failed to register IOMMU ops\n", __func__);
	}

	return ret;
}
arch_initcall(exynos_bus_set_iommu);

static struct dentry *sysmmu_debugfs_root; /* /sys/kernel/debug/sysmmu */

static int __init exynos_iommu_init(void)
{
	sysmmu_debugfs_root = debugfs_create_dir("sysmmu", NULL);
	if (!sysmmu_debugfs_root)
		pr_err("%s: Failed to create debugfs entry, 'sysmmu'\n",
							__func__);
	if (IS_ERR(sysmmu_debugfs_root))
		sysmmu_debugfs_root = NULL;
	return platform_driver_register(&exynos_sysmmu_driver);
}
subsys_initcall(exynos_iommu_init);

static int debug_string_show(struct seq_file *s, void *unused)
{
	char *str = s->private;

	seq_printf(s, "%s\n", str);

	return 0;
}

static int debug_sysmmu_list_show(struct seq_file *s, void *unused)
{
	struct sysmmu_drvdata *drvdata = s->private;
	struct platform_device *pdev = to_platform_device(drvdata->sysmmu);
	int idx, maj, min, ret;

	seq_printf(s, "SysMMU Name | Ver | SFR Base\n");

	if (pm_runtime_enabled(drvdata->sysmmu)) {
		ret = pm_runtime_get_sync(drvdata->sysmmu);
		if (ret < 0)
			return ret;
	}

	for (idx = 0; idx < drvdata->nsfrs; idx++) {
		struct resource *res;

		res = platform_get_resource(pdev, IORESOURCE_MEM, idx);
		if (!res)
			break;

		maj = __sysmmu_version(drvdata, idx, &min);

		if (maj == 0)
			seq_printf(s, "%11.s | N/A | 0x%08x\n",
					res->name, res->start);
		else
			seq_printf(s, "%11.s | %d.%d | 0x%08x\n",
					res->name, maj, min, res->start);
	}

	if (pm_runtime_enabled(drvdata->sysmmu))
		pm_runtime_put(drvdata->sysmmu);

	return 0;
}

static int debug_next_sibling_show(struct seq_file *s, void *unused)
{
	struct device *dev = s->private;

	if (dev->parent &&
		!strncmp(dev_name(dev->parent),
			MODULE_NAME, strlen(MODULE_NAME)))
		seq_printf(s, "%s\n", dev_name(dev->parent));
	return 0;
}

static int __show_master(struct device *dev, void *data)
{
	struct seq_file *s = data;

	if (strncmp(dev_name(dev), MODULE_NAME, strlen(MODULE_NAME)))
		seq_printf(s, "%s\n", dev_name(dev));
	return 0;
}

static int debug_master_show(struct seq_file *s, void *unused)
{
	struct device *dev = s->private;

	device_for_each_child(dev, s, __show_master);
	return 0;
}

static int debug_string_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_string_show, inode->i_private);
}

static int debug_sysmmu_list_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_sysmmu_list_show, inode->i_private);
}

static int debug_next_sibling_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_next_sibling_show, inode->i_private);
}

static int debug_master_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_master_show, inode->i_private);
}

static const struct file_operations debug_string_fops = {
	.open = debug_string_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations debug_sysmmu_list_fops = {
	.open = debug_sysmmu_list_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations debug_next_sibling_fops = {
	.open = debug_next_sibling_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations debug_master_fops = {
	.open = debug_master_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void __init __create_debugfs_entry(struct sysmmu_drvdata *drvdata)
{
	if (!sysmmu_debugfs_root)
		return;

	drvdata->debugfs_root = debugfs_create_dir(dev_name(drvdata->sysmmu),
							sysmmu_debugfs_root);
	if (!drvdata->debugfs_root)
		dev_err(drvdata->sysmmu, "Failed to create debugfs dentry\n");
	if (IS_ERR(drvdata->debugfs_root))
		drvdata->debugfs_root = NULL;

	if (!drvdata->debugfs_root)
		return;

	if (!debugfs_create_u32("enable", 0664, drvdata->debugfs_root,
						&drvdata->activations))
		dev_err(drvdata->sysmmu,
				"Failed to create debugfs file 'enable'\n");

	if (!debugfs_create_x32("pagetable", 0664, drvdata->debugfs_root,
						(u32 *)&drvdata->pgtable))
		dev_err(drvdata->sysmmu,
				"Failed to create debugfs file 'pagetable'\n");

	if (!debugfs_create_file("name", 0444, drvdata->debugfs_root,
					drvdata->dbgname, &debug_string_fops))
		dev_err(drvdata->sysmmu,
				"Failed to create debugfs file 'name'\n");

	if (!debugfs_create_file("sysmmu_list", 0444, drvdata->debugfs_root,
					drvdata, &debug_sysmmu_list_fops))
		dev_err(drvdata->sysmmu,
			"Failed to create debugfs file 'sysmmu_list'\n");

	if (!debugfs_create_file("next_sibling", 0x444, drvdata->debugfs_root,
				drvdata->sysmmu, &debug_next_sibling_fops))
		dev_err(drvdata->sysmmu,
			"Failed to create debugfs file 'next_siblings'\n");

	if (!debugfs_create_file("master", 0x444, drvdata->debugfs_root,
				drvdata->sysmmu, &debug_master_fops))
		dev_err(drvdata->sysmmu,
			"Failed to create debugfs file 'next_siblings'\n");
}
