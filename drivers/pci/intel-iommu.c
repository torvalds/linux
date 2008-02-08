/*
 * Copyright (c) 2006, Intel Corporation.
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
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Copyright (C) Ashok Raj <ashok.raj@intel.com>
 * Copyright (C) Shaohua Li <shaohua.li@intel.com>
 * Copyright (C) Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 */

#include <linux/init.h>
#include <linux/bitmap.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/sysdev.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/dmar.h>
#include <linux/dma-mapping.h>
#include <linux/mempool.h>
#include "iova.h"
#include "intel-iommu.h"
#include <asm/proto.h> /* force_iommu in this header in x86-64*/
#include <asm/cacheflush.h>
#include <asm/gart.h>
#include "pci.h"

#define IS_GFX_DEVICE(pdev) ((pdev->class >> 16) == PCI_BASE_CLASS_DISPLAY)
#define IS_ISA_DEVICE(pdev) ((pdev->class >> 8) == PCI_CLASS_BRIDGE_ISA)

#define IOAPIC_RANGE_START	(0xfee00000)
#define IOAPIC_RANGE_END	(0xfeefffff)
#define IOVA_START_ADDR		(0x1000)

#define DEFAULT_DOMAIN_ADDRESS_WIDTH 48

#define DMAR_OPERATION_TIMEOUT (HZ*60) /* 1m */

#define DOMAIN_MAX_ADDR(gaw) ((((u64)1) << gaw) - 1)

static void domain_remove_dev_info(struct dmar_domain *domain);

static int dmar_disabled;
static int __initdata dmar_map_gfx = 1;
static int dmar_forcedac;

#define DUMMY_DEVICE_DOMAIN_INFO ((struct device_domain_info *)(-1))
static DEFINE_SPINLOCK(device_domain_lock);
static LIST_HEAD(device_domain_list);

static int __init intel_iommu_setup(char *str)
{
	if (!str)
		return -EINVAL;
	while (*str) {
		if (!strncmp(str, "off", 3)) {
			dmar_disabled = 1;
			printk(KERN_INFO"Intel-IOMMU: disabled\n");
		} else if (!strncmp(str, "igfx_off", 8)) {
			dmar_map_gfx = 0;
			printk(KERN_INFO
				"Intel-IOMMU: disable GFX device mapping\n");
		} else if (!strncmp(str, "forcedac", 8)) {
			printk (KERN_INFO
				"Intel-IOMMU: Forcing DAC for PCI devices\n");
			dmar_forcedac = 1;
		}

		str += strcspn(str, ",");
		while (*str == ',')
			str++;
	}
	return 0;
}
__setup("intel_iommu=", intel_iommu_setup);

static struct kmem_cache *iommu_domain_cache;
static struct kmem_cache *iommu_devinfo_cache;
static struct kmem_cache *iommu_iova_cache;

static inline void *iommu_kmem_cache_alloc(struct kmem_cache *cachep)
{
	unsigned int flags;
	void *vaddr;

	/* trying to avoid low memory issues */
	flags = current->flags & PF_MEMALLOC;
	current->flags |= PF_MEMALLOC;
	vaddr = kmem_cache_alloc(cachep, GFP_ATOMIC);
	current->flags &= (~PF_MEMALLOC | flags);
	return vaddr;
}


static inline void *alloc_pgtable_page(void)
{
	unsigned int flags;
	void *vaddr;

	/* trying to avoid low memory issues */
	flags = current->flags & PF_MEMALLOC;
	current->flags |= PF_MEMALLOC;
	vaddr = (void *)get_zeroed_page(GFP_ATOMIC);
	current->flags &= (~PF_MEMALLOC | flags);
	return vaddr;
}

static inline void free_pgtable_page(void *vaddr)
{
	free_page((unsigned long)vaddr);
}

static inline void *alloc_domain_mem(void)
{
	return iommu_kmem_cache_alloc(iommu_domain_cache);
}

static inline void free_domain_mem(void *vaddr)
{
	kmem_cache_free(iommu_domain_cache, vaddr);
}

static inline void * alloc_devinfo_mem(void)
{
	return iommu_kmem_cache_alloc(iommu_devinfo_cache);
}

static inline void free_devinfo_mem(void *vaddr)
{
	kmem_cache_free(iommu_devinfo_cache, vaddr);
}

struct iova *alloc_iova_mem(void)
{
	return iommu_kmem_cache_alloc(iommu_iova_cache);
}

void free_iova_mem(struct iova *iova)
{
	kmem_cache_free(iommu_iova_cache, iova);
}

static inline void __iommu_flush_cache(
	struct intel_iommu *iommu, void *addr, int size)
{
	if (!ecap_coherent(iommu->ecap))
		clflush_cache_range(addr, size);
}

/* Gets context entry for a given bus and devfn */
static struct context_entry * device_to_context_entry(struct intel_iommu *iommu,
		u8 bus, u8 devfn)
{
	struct root_entry *root;
	struct context_entry *context;
	unsigned long phy_addr;
	unsigned long flags;

	spin_lock_irqsave(&iommu->lock, flags);
	root = &iommu->root_entry[bus];
	context = get_context_addr_from_root(root);
	if (!context) {
		context = (struct context_entry *)alloc_pgtable_page();
		if (!context) {
			spin_unlock_irqrestore(&iommu->lock, flags);
			return NULL;
		}
		__iommu_flush_cache(iommu, (void *)context, PAGE_SIZE_4K);
		phy_addr = virt_to_phys((void *)context);
		set_root_value(root, phy_addr);
		set_root_present(root);
		__iommu_flush_cache(iommu, root, sizeof(*root));
	}
	spin_unlock_irqrestore(&iommu->lock, flags);
	return &context[devfn];
}

static int device_context_mapped(struct intel_iommu *iommu, u8 bus, u8 devfn)
{
	struct root_entry *root;
	struct context_entry *context;
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&iommu->lock, flags);
	root = &iommu->root_entry[bus];
	context = get_context_addr_from_root(root);
	if (!context) {
		ret = 0;
		goto out;
	}
	ret = context_present(context[devfn]);
out:
	spin_unlock_irqrestore(&iommu->lock, flags);
	return ret;
}

static void clear_context_table(struct intel_iommu *iommu, u8 bus, u8 devfn)
{
	struct root_entry *root;
	struct context_entry *context;
	unsigned long flags;

	spin_lock_irqsave(&iommu->lock, flags);
	root = &iommu->root_entry[bus];
	context = get_context_addr_from_root(root);
	if (context) {
		context_clear_entry(context[devfn]);
		__iommu_flush_cache(iommu, &context[devfn], \
			sizeof(*context));
	}
	spin_unlock_irqrestore(&iommu->lock, flags);
}

static void free_context_table(struct intel_iommu *iommu)
{
	struct root_entry *root;
	int i;
	unsigned long flags;
	struct context_entry *context;

	spin_lock_irqsave(&iommu->lock, flags);
	if (!iommu->root_entry) {
		goto out;
	}
	for (i = 0; i < ROOT_ENTRY_NR; i++) {
		root = &iommu->root_entry[i];
		context = get_context_addr_from_root(root);
		if (context)
			free_pgtable_page(context);
	}
	free_pgtable_page(iommu->root_entry);
	iommu->root_entry = NULL;
out:
	spin_unlock_irqrestore(&iommu->lock, flags);
}

/* page table handling */
#define LEVEL_STRIDE		(9)
#define LEVEL_MASK		(((u64)1 << LEVEL_STRIDE) - 1)

static inline int agaw_to_level(int agaw)
{
	return agaw + 2;
}

static inline int agaw_to_width(int agaw)
{
	return 30 + agaw * LEVEL_STRIDE;

}

static inline int width_to_agaw(int width)
{
	return (width - 30) / LEVEL_STRIDE;
}

static inline unsigned int level_to_offset_bits(int level)
{
	return (12 + (level - 1) * LEVEL_STRIDE);
}

static inline int address_level_offset(u64 addr, int level)
{
	return ((addr >> level_to_offset_bits(level)) & LEVEL_MASK);
}

static inline u64 level_mask(int level)
{
	return ((u64)-1 << level_to_offset_bits(level));
}

static inline u64 level_size(int level)
{
	return ((u64)1 << level_to_offset_bits(level));
}

static inline u64 align_to_level(u64 addr, int level)
{
	return ((addr + level_size(level) - 1) & level_mask(level));
}

static struct dma_pte * addr_to_dma_pte(struct dmar_domain *domain, u64 addr)
{
	int addr_width = agaw_to_width(domain->agaw);
	struct dma_pte *parent, *pte = NULL;
	int level = agaw_to_level(domain->agaw);
	int offset;
	unsigned long flags;

	BUG_ON(!domain->pgd);

	addr &= (((u64)1) << addr_width) - 1;
	parent = domain->pgd;

	spin_lock_irqsave(&domain->mapping_lock, flags);
	while (level > 0) {
		void *tmp_page;

		offset = address_level_offset(addr, level);
		pte = &parent[offset];
		if (level == 1)
			break;

		if (!dma_pte_present(*pte)) {
			tmp_page = alloc_pgtable_page();

			if (!tmp_page) {
				spin_unlock_irqrestore(&domain->mapping_lock,
					flags);
				return NULL;
			}
			__iommu_flush_cache(domain->iommu, tmp_page,
					PAGE_SIZE_4K);
			dma_set_pte_addr(*pte, virt_to_phys(tmp_page));
			/*
			 * high level table always sets r/w, last level page
			 * table control read/write
			 */
			dma_set_pte_readable(*pte);
			dma_set_pte_writable(*pte);
			__iommu_flush_cache(domain->iommu, pte, sizeof(*pte));
		}
		parent = phys_to_virt(dma_pte_addr(*pte));
		level--;
	}

	spin_unlock_irqrestore(&domain->mapping_lock, flags);
	return pte;
}

/* return address's pte at specific level */
static struct dma_pte *dma_addr_level_pte(struct dmar_domain *domain, u64 addr,
		int level)
{
	struct dma_pte *parent, *pte = NULL;
	int total = agaw_to_level(domain->agaw);
	int offset;

	parent = domain->pgd;
	while (level <= total) {
		offset = address_level_offset(addr, total);
		pte = &parent[offset];
		if (level == total)
			return pte;

		if (!dma_pte_present(*pte))
			break;
		parent = phys_to_virt(dma_pte_addr(*pte));
		total--;
	}
	return NULL;
}

/* clear one page's page table */
static void dma_pte_clear_one(struct dmar_domain *domain, u64 addr)
{
	struct dma_pte *pte = NULL;

	/* get last level pte */
	pte = dma_addr_level_pte(domain, addr, 1);

	if (pte) {
		dma_clear_pte(*pte);
		__iommu_flush_cache(domain->iommu, pte, sizeof(*pte));
	}
}

/* clear last level pte, a tlb flush should be followed */
static void dma_pte_clear_range(struct dmar_domain *domain, u64 start, u64 end)
{
	int addr_width = agaw_to_width(domain->agaw);

	start &= (((u64)1) << addr_width) - 1;
	end &= (((u64)1) << addr_width) - 1;
	/* in case it's partial page */
	start = PAGE_ALIGN_4K(start);
	end &= PAGE_MASK_4K;

	/* we don't need lock here, nobody else touches the iova range */
	while (start < end) {
		dma_pte_clear_one(domain, start);
		start += PAGE_SIZE_4K;
	}
}

/* free page table pages. last level pte should already be cleared */
static void dma_pte_free_pagetable(struct dmar_domain *domain,
	u64 start, u64 end)
{
	int addr_width = agaw_to_width(domain->agaw);
	struct dma_pte *pte;
	int total = agaw_to_level(domain->agaw);
	int level;
	u64 tmp;

	start &= (((u64)1) << addr_width) - 1;
	end &= (((u64)1) << addr_width) - 1;

	/* we don't need lock here, nobody else touches the iova range */
	level = 2;
	while (level <= total) {
		tmp = align_to_level(start, level);
		if (tmp >= end || (tmp + level_size(level) > end))
			return;

		while (tmp < end) {
			pte = dma_addr_level_pte(domain, tmp, level);
			if (pte) {
				free_pgtable_page(
					phys_to_virt(dma_pte_addr(*pte)));
				dma_clear_pte(*pte);
				__iommu_flush_cache(domain->iommu,
						pte, sizeof(*pte));
			}
			tmp += level_size(level);
		}
		level++;
	}
	/* free pgd */
	if (start == 0 && end >= ((((u64)1) << addr_width) - 1)) {
		free_pgtable_page(domain->pgd);
		domain->pgd = NULL;
	}
}

/* iommu handling */
static int iommu_alloc_root_entry(struct intel_iommu *iommu)
{
	struct root_entry *root;
	unsigned long flags;

	root = (struct root_entry *)alloc_pgtable_page();
	if (!root)
		return -ENOMEM;

	__iommu_flush_cache(iommu, root, PAGE_SIZE_4K);

	spin_lock_irqsave(&iommu->lock, flags);
	iommu->root_entry = root;
	spin_unlock_irqrestore(&iommu->lock, flags);

	return 0;
}

#define IOMMU_WAIT_OP(iommu, offset, op, cond, sts) \
{\
	unsigned long start_time = jiffies;\
	while (1) {\
		sts = op (iommu->reg + offset);\
		if (cond)\
			break;\
		if (time_after(jiffies, start_time + DMAR_OPERATION_TIMEOUT))\
			panic("DMAR hardware is malfunctioning\n");\
		cpu_relax();\
	}\
}

static void iommu_set_root_entry(struct intel_iommu *iommu)
{
	void *addr;
	u32 cmd, sts;
	unsigned long flag;

	addr = iommu->root_entry;

	spin_lock_irqsave(&iommu->register_lock, flag);
	dmar_writeq(iommu->reg + DMAR_RTADDR_REG, virt_to_phys(addr));

	cmd = iommu->gcmd | DMA_GCMD_SRTP;
	writel(cmd, iommu->reg + DMAR_GCMD_REG);

	/* Make sure hardware complete it */
	IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG,
		readl, (sts & DMA_GSTS_RTPS), sts);

	spin_unlock_irqrestore(&iommu->register_lock, flag);
}

static void iommu_flush_write_buffer(struct intel_iommu *iommu)
{
	u32 val;
	unsigned long flag;

	if (!cap_rwbf(iommu->cap))
		return;
	val = iommu->gcmd | DMA_GCMD_WBF;

	spin_lock_irqsave(&iommu->register_lock, flag);
	writel(val, iommu->reg + DMAR_GCMD_REG);

	/* Make sure hardware complete it */
	IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG,
			readl, (!(val & DMA_GSTS_WBFS)), val);

	spin_unlock_irqrestore(&iommu->register_lock, flag);
}

/* return value determine if we need a write buffer flush */
static int __iommu_flush_context(struct intel_iommu *iommu,
	u16 did, u16 source_id, u8 function_mask, u64 type,
	int non_present_entry_flush)
{
	u64 val = 0;
	unsigned long flag;

	/*
	 * In the non-present entry flush case, if hardware doesn't cache
	 * non-present entry we do nothing and if hardware cache non-present
	 * entry, we flush entries of domain 0 (the domain id is used to cache
	 * any non-present entries)
	 */
	if (non_present_entry_flush) {
		if (!cap_caching_mode(iommu->cap))
			return 1;
		else
			did = 0;
	}

	switch (type) {
	case DMA_CCMD_GLOBAL_INVL:
		val = DMA_CCMD_GLOBAL_INVL;
		break;
	case DMA_CCMD_DOMAIN_INVL:
		val = DMA_CCMD_DOMAIN_INVL|DMA_CCMD_DID(did);
		break;
	case DMA_CCMD_DEVICE_INVL:
		val = DMA_CCMD_DEVICE_INVL|DMA_CCMD_DID(did)
			| DMA_CCMD_SID(source_id) | DMA_CCMD_FM(function_mask);
		break;
	default:
		BUG();
	}
	val |= DMA_CCMD_ICC;

	spin_lock_irqsave(&iommu->register_lock, flag);
	dmar_writeq(iommu->reg + DMAR_CCMD_REG, val);

	/* Make sure hardware complete it */
	IOMMU_WAIT_OP(iommu, DMAR_CCMD_REG,
		dmar_readq, (!(val & DMA_CCMD_ICC)), val);

	spin_unlock_irqrestore(&iommu->register_lock, flag);

	/* flush context entry will implictly flush write buffer */
	return 0;
}

static int inline iommu_flush_context_global(struct intel_iommu *iommu,
	int non_present_entry_flush)
{
	return __iommu_flush_context(iommu, 0, 0, 0, DMA_CCMD_GLOBAL_INVL,
		non_present_entry_flush);
}

static int inline iommu_flush_context_domain(struct intel_iommu *iommu, u16 did,
	int non_present_entry_flush)
{
	return __iommu_flush_context(iommu, did, 0, 0, DMA_CCMD_DOMAIN_INVL,
		non_present_entry_flush);
}

static int inline iommu_flush_context_device(struct intel_iommu *iommu,
	u16 did, u16 source_id, u8 function_mask, int non_present_entry_flush)
{
	return __iommu_flush_context(iommu, did, source_id, function_mask,
		DMA_CCMD_DEVICE_INVL, non_present_entry_flush);
}

/* return value determine if we need a write buffer flush */
static int __iommu_flush_iotlb(struct intel_iommu *iommu, u16 did,
	u64 addr, unsigned int size_order, u64 type,
	int non_present_entry_flush)
{
	int tlb_offset = ecap_iotlb_offset(iommu->ecap);
	u64 val = 0, val_iva = 0;
	unsigned long flag;

	/*
	 * In the non-present entry flush case, if hardware doesn't cache
	 * non-present entry we do nothing and if hardware cache non-present
	 * entry, we flush entries of domain 0 (the domain id is used to cache
	 * any non-present entries)
	 */
	if (non_present_entry_flush) {
		if (!cap_caching_mode(iommu->cap))
			return 1;
		else
			did = 0;
	}

	switch (type) {
	case DMA_TLB_GLOBAL_FLUSH:
		/* global flush doesn't need set IVA_REG */
		val = DMA_TLB_GLOBAL_FLUSH|DMA_TLB_IVT;
		break;
	case DMA_TLB_DSI_FLUSH:
		val = DMA_TLB_DSI_FLUSH|DMA_TLB_IVT|DMA_TLB_DID(did);
		break;
	case DMA_TLB_PSI_FLUSH:
		val = DMA_TLB_PSI_FLUSH|DMA_TLB_IVT|DMA_TLB_DID(did);
		/* Note: always flush non-leaf currently */
		val_iva = size_order | addr;
		break;
	default:
		BUG();
	}
	/* Note: set drain read/write */
#if 0
	/*
	 * This is probably to be super secure.. Looks like we can
	 * ignore it without any impact.
	 */
	if (cap_read_drain(iommu->cap))
		val |= DMA_TLB_READ_DRAIN;
#endif
	if (cap_write_drain(iommu->cap))
		val |= DMA_TLB_WRITE_DRAIN;

	spin_lock_irqsave(&iommu->register_lock, flag);
	/* Note: Only uses first TLB reg currently */
	if (val_iva)
		dmar_writeq(iommu->reg + tlb_offset, val_iva);
	dmar_writeq(iommu->reg + tlb_offset + 8, val);

	/* Make sure hardware complete it */
	IOMMU_WAIT_OP(iommu, tlb_offset + 8,
		dmar_readq, (!(val & DMA_TLB_IVT)), val);

	spin_unlock_irqrestore(&iommu->register_lock, flag);

	/* check IOTLB invalidation granularity */
	if (DMA_TLB_IAIG(val) == 0)
		printk(KERN_ERR"IOMMU: flush IOTLB failed\n");
	if (DMA_TLB_IAIG(val) != DMA_TLB_IIRG(type))
		pr_debug("IOMMU: tlb flush request %Lx, actual %Lx\n",
			DMA_TLB_IIRG(type), DMA_TLB_IAIG(val));
	/* flush context entry will implictly flush write buffer */
	return 0;
}

static int inline iommu_flush_iotlb_global(struct intel_iommu *iommu,
	int non_present_entry_flush)
{
	return __iommu_flush_iotlb(iommu, 0, 0, 0, DMA_TLB_GLOBAL_FLUSH,
		non_present_entry_flush);
}

static int inline iommu_flush_iotlb_dsi(struct intel_iommu *iommu, u16 did,
	int non_present_entry_flush)
{
	return __iommu_flush_iotlb(iommu, did, 0, 0, DMA_TLB_DSI_FLUSH,
		non_present_entry_flush);
}

static int iommu_flush_iotlb_psi(struct intel_iommu *iommu, u16 did,
	u64 addr, unsigned int pages, int non_present_entry_flush)
{
	unsigned int mask;

	BUG_ON(addr & (~PAGE_MASK_4K));
	BUG_ON(pages == 0);

	/* Fallback to domain selective flush if no PSI support */
	if (!cap_pgsel_inv(iommu->cap))
		return iommu_flush_iotlb_dsi(iommu, did,
			non_present_entry_flush);

	/*
	 * PSI requires page size to be 2 ^ x, and the base address is naturally
	 * aligned to the size
	 */
	mask = ilog2(__roundup_pow_of_two(pages));
	/* Fallback to domain selective flush if size is too big */
	if (mask > cap_max_amask_val(iommu->cap))
		return iommu_flush_iotlb_dsi(iommu, did,
			non_present_entry_flush);

	return __iommu_flush_iotlb(iommu, did, addr, mask,
		DMA_TLB_PSI_FLUSH, non_present_entry_flush);
}

static void iommu_disable_protect_mem_regions(struct intel_iommu *iommu)
{
	u32 pmen;
	unsigned long flags;

	spin_lock_irqsave(&iommu->register_lock, flags);
	pmen = readl(iommu->reg + DMAR_PMEN_REG);
	pmen &= ~DMA_PMEN_EPM;
	writel(pmen, iommu->reg + DMAR_PMEN_REG);

	/* wait for the protected region status bit to clear */
	IOMMU_WAIT_OP(iommu, DMAR_PMEN_REG,
		readl, !(pmen & DMA_PMEN_PRS), pmen);

	spin_unlock_irqrestore(&iommu->register_lock, flags);
}

static int iommu_enable_translation(struct intel_iommu *iommu)
{
	u32 sts;
	unsigned long flags;

	spin_lock_irqsave(&iommu->register_lock, flags);
	writel(iommu->gcmd|DMA_GCMD_TE, iommu->reg + DMAR_GCMD_REG);

	/* Make sure hardware complete it */
	IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG,
		readl, (sts & DMA_GSTS_TES), sts);

	iommu->gcmd |= DMA_GCMD_TE;
	spin_unlock_irqrestore(&iommu->register_lock, flags);
	return 0;
}

static int iommu_disable_translation(struct intel_iommu *iommu)
{
	u32 sts;
	unsigned long flag;

	spin_lock_irqsave(&iommu->register_lock, flag);
	iommu->gcmd &= ~DMA_GCMD_TE;
	writel(iommu->gcmd, iommu->reg + DMAR_GCMD_REG);

	/* Make sure hardware complete it */
	IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG,
		readl, (!(sts & DMA_GSTS_TES)), sts);

	spin_unlock_irqrestore(&iommu->register_lock, flag);
	return 0;
}

/* iommu interrupt handling. Most stuff are MSI-like. */

static char *fault_reason_strings[] =
{
	"Software",
	"Present bit in root entry is clear",
	"Present bit in context entry is clear",
	"Invalid context entry",
	"Access beyond MGAW",
	"PTE Write access is not set",
	"PTE Read access is not set",
	"Next page table ptr is invalid",
	"Root table address invalid",
	"Context table ptr is invalid",
	"non-zero reserved fields in RTP",
	"non-zero reserved fields in CTP",
	"non-zero reserved fields in PTE",
	"Unknown"
};
#define MAX_FAULT_REASON_IDX 	(ARRAY_SIZE(fault_reason_strings) - 1)

char *dmar_get_fault_reason(u8 fault_reason)
{
	if (fault_reason >= MAX_FAULT_REASON_IDX)
		return fault_reason_strings[MAX_FAULT_REASON_IDX - 1];
	else
		return fault_reason_strings[fault_reason];
}

void dmar_msi_unmask(unsigned int irq)
{
	struct intel_iommu *iommu = get_irq_data(irq);
	unsigned long flag;

	/* unmask it */
	spin_lock_irqsave(&iommu->register_lock, flag);
	writel(0, iommu->reg + DMAR_FECTL_REG);
	/* Read a reg to force flush the post write */
	readl(iommu->reg + DMAR_FECTL_REG);
	spin_unlock_irqrestore(&iommu->register_lock, flag);
}

void dmar_msi_mask(unsigned int irq)
{
	unsigned long flag;
	struct intel_iommu *iommu = get_irq_data(irq);

	/* mask it */
	spin_lock_irqsave(&iommu->register_lock, flag);
	writel(DMA_FECTL_IM, iommu->reg + DMAR_FECTL_REG);
	/* Read a reg to force flush the post write */
	readl(iommu->reg + DMAR_FECTL_REG);
	spin_unlock_irqrestore(&iommu->register_lock, flag);
}

void dmar_msi_write(int irq, struct msi_msg *msg)
{
	struct intel_iommu *iommu = get_irq_data(irq);
	unsigned long flag;

	spin_lock_irqsave(&iommu->register_lock, flag);
	writel(msg->data, iommu->reg + DMAR_FEDATA_REG);
	writel(msg->address_lo, iommu->reg + DMAR_FEADDR_REG);
	writel(msg->address_hi, iommu->reg + DMAR_FEUADDR_REG);
	spin_unlock_irqrestore(&iommu->register_lock, flag);
}

void dmar_msi_read(int irq, struct msi_msg *msg)
{
	struct intel_iommu *iommu = get_irq_data(irq);
	unsigned long flag;

	spin_lock_irqsave(&iommu->register_lock, flag);
	msg->data = readl(iommu->reg + DMAR_FEDATA_REG);
	msg->address_lo = readl(iommu->reg + DMAR_FEADDR_REG);
	msg->address_hi = readl(iommu->reg + DMAR_FEUADDR_REG);
	spin_unlock_irqrestore(&iommu->register_lock, flag);
}

static int iommu_page_fault_do_one(struct intel_iommu *iommu, int type,
		u8 fault_reason, u16 source_id, u64 addr)
{
	char *reason;

	reason = dmar_get_fault_reason(fault_reason);

	printk(KERN_ERR
		"DMAR:[%s] Request device [%02x:%02x.%d] "
		"fault addr %llx \n"
		"DMAR:[fault reason %02d] %s\n",
		(type ? "DMA Read" : "DMA Write"),
		(source_id >> 8), PCI_SLOT(source_id & 0xFF),
		PCI_FUNC(source_id & 0xFF), addr, fault_reason, reason);
	return 0;
}

#define PRIMARY_FAULT_REG_LEN (16)
static irqreturn_t iommu_page_fault(int irq, void *dev_id)
{
	struct intel_iommu *iommu = dev_id;
	int reg, fault_index;
	u32 fault_status;
	unsigned long flag;

	spin_lock_irqsave(&iommu->register_lock, flag);
	fault_status = readl(iommu->reg + DMAR_FSTS_REG);

	/* TBD: ignore advanced fault log currently */
	if (!(fault_status & DMA_FSTS_PPF))
		goto clear_overflow;

	fault_index = dma_fsts_fault_record_index(fault_status);
	reg = cap_fault_reg_offset(iommu->cap);
	while (1) {
		u8 fault_reason;
		u16 source_id;
		u64 guest_addr;
		int type;
		u32 data;

		/* highest 32 bits */
		data = readl(iommu->reg + reg +
				fault_index * PRIMARY_FAULT_REG_LEN + 12);
		if (!(data & DMA_FRCD_F))
			break;

		fault_reason = dma_frcd_fault_reason(data);
		type = dma_frcd_type(data);

		data = readl(iommu->reg + reg +
				fault_index * PRIMARY_FAULT_REG_LEN + 8);
		source_id = dma_frcd_source_id(data);

		guest_addr = dmar_readq(iommu->reg + reg +
				fault_index * PRIMARY_FAULT_REG_LEN);
		guest_addr = dma_frcd_page_addr(guest_addr);
		/* clear the fault */
		writel(DMA_FRCD_F, iommu->reg + reg +
			fault_index * PRIMARY_FAULT_REG_LEN + 12);

		spin_unlock_irqrestore(&iommu->register_lock, flag);

		iommu_page_fault_do_one(iommu, type, fault_reason,
				source_id, guest_addr);

		fault_index++;
		if (fault_index > cap_num_fault_regs(iommu->cap))
			fault_index = 0;
		spin_lock_irqsave(&iommu->register_lock, flag);
	}
clear_overflow:
	/* clear primary fault overflow */
	fault_status = readl(iommu->reg + DMAR_FSTS_REG);
	if (fault_status & DMA_FSTS_PFO)
		writel(DMA_FSTS_PFO, iommu->reg + DMAR_FSTS_REG);

	spin_unlock_irqrestore(&iommu->register_lock, flag);
	return IRQ_HANDLED;
}

int dmar_set_interrupt(struct intel_iommu *iommu)
{
	int irq, ret;

	irq = create_irq();
	if (!irq) {
		printk(KERN_ERR "IOMMU: no free vectors\n");
		return -EINVAL;
	}

	set_irq_data(irq, iommu);
	iommu->irq = irq;

	ret = arch_setup_dmar_msi(irq);
	if (ret) {
		set_irq_data(irq, NULL);
		iommu->irq = 0;
		destroy_irq(irq);
		return 0;
	}

	/* Force fault register is cleared */
	iommu_page_fault(irq, iommu);

	ret = request_irq(irq, iommu_page_fault, 0, iommu->name, iommu);
	if (ret)
		printk(KERN_ERR "IOMMU: can't request irq\n");
	return ret;
}

static int iommu_init_domains(struct intel_iommu *iommu)
{
	unsigned long ndomains;
	unsigned long nlongs;

	ndomains = cap_ndoms(iommu->cap);
	pr_debug("Number of Domains supportd <%ld>\n", ndomains);
	nlongs = BITS_TO_LONGS(ndomains);

	/* TBD: there might be 64K domains,
	 * consider other allocation for future chip
	 */
	iommu->domain_ids = kcalloc(nlongs, sizeof(unsigned long), GFP_KERNEL);
	if (!iommu->domain_ids) {
		printk(KERN_ERR "Allocating domain id array failed\n");
		return -ENOMEM;
	}
	iommu->domains = kcalloc(ndomains, sizeof(struct dmar_domain *),
			GFP_KERNEL);
	if (!iommu->domains) {
		printk(KERN_ERR "Allocating domain array failed\n");
		kfree(iommu->domain_ids);
		return -ENOMEM;
	}

	/*
	 * if Caching mode is set, then invalid translations are tagged
	 * with domainid 0. Hence we need to pre-allocate it.
	 */
	if (cap_caching_mode(iommu->cap))
		set_bit(0, iommu->domain_ids);
	return 0;
}

static struct intel_iommu *alloc_iommu(struct dmar_drhd_unit *drhd)
{
	struct intel_iommu *iommu;
	int ret;
	int map_size;
	u32 ver;

	iommu = kzalloc(sizeof(*iommu), GFP_KERNEL);
	if (!iommu)
		return NULL;
	iommu->reg = ioremap(drhd->reg_base_addr, PAGE_SIZE_4K);
	if (!iommu->reg) {
		printk(KERN_ERR "IOMMU: can't map the region\n");
		goto error;
	}
	iommu->cap = dmar_readq(iommu->reg + DMAR_CAP_REG);
	iommu->ecap = dmar_readq(iommu->reg + DMAR_ECAP_REG);

	/* the registers might be more than one page */
	map_size = max_t(int, ecap_max_iotlb_offset(iommu->ecap),
		cap_max_fault_reg_offset(iommu->cap));
	map_size = PAGE_ALIGN_4K(map_size);
	if (map_size > PAGE_SIZE_4K) {
		iounmap(iommu->reg);
		iommu->reg = ioremap(drhd->reg_base_addr, map_size);
		if (!iommu->reg) {
			printk(KERN_ERR "IOMMU: can't map the region\n");
			goto error;
		}
	}

	ver = readl(iommu->reg + DMAR_VER_REG);
	pr_debug("IOMMU %llx: ver %d:%d cap %llx ecap %llx\n",
		drhd->reg_base_addr, DMAR_VER_MAJOR(ver), DMAR_VER_MINOR(ver),
		iommu->cap, iommu->ecap);
	ret = iommu_init_domains(iommu);
	if (ret)
		goto error_unmap;
	spin_lock_init(&iommu->lock);
	spin_lock_init(&iommu->register_lock);

	drhd->iommu = iommu;
	return iommu;
error_unmap:
	iounmap(iommu->reg);
error:
	kfree(iommu);
	return NULL;
}

static void domain_exit(struct dmar_domain *domain);
static void free_iommu(struct intel_iommu *iommu)
{
	struct dmar_domain *domain;
	int i;

	if (!iommu)
		return;

	i = find_first_bit(iommu->domain_ids, cap_ndoms(iommu->cap));
	for (; i < cap_ndoms(iommu->cap); ) {
		domain = iommu->domains[i];
		clear_bit(i, iommu->domain_ids);
		domain_exit(domain);
		i = find_next_bit(iommu->domain_ids,
			cap_ndoms(iommu->cap), i+1);
	}

	if (iommu->gcmd & DMA_GCMD_TE)
		iommu_disable_translation(iommu);

	if (iommu->irq) {
		set_irq_data(iommu->irq, NULL);
		/* This will mask the irq */
		free_irq(iommu->irq, iommu);
		destroy_irq(iommu->irq);
	}

	kfree(iommu->domains);
	kfree(iommu->domain_ids);

	/* free context mapping */
	free_context_table(iommu);

	if (iommu->reg)
		iounmap(iommu->reg);
	kfree(iommu);
}

static struct dmar_domain * iommu_alloc_domain(struct intel_iommu *iommu)
{
	unsigned long num;
	unsigned long ndomains;
	struct dmar_domain *domain;
	unsigned long flags;

	domain = alloc_domain_mem();
	if (!domain)
		return NULL;

	ndomains = cap_ndoms(iommu->cap);

	spin_lock_irqsave(&iommu->lock, flags);
	num = find_first_zero_bit(iommu->domain_ids, ndomains);
	if (num >= ndomains) {
		spin_unlock_irqrestore(&iommu->lock, flags);
		free_domain_mem(domain);
		printk(KERN_ERR "IOMMU: no free domain ids\n");
		return NULL;
	}

	set_bit(num, iommu->domain_ids);
	domain->id = num;
	domain->iommu = iommu;
	iommu->domains[num] = domain;
	spin_unlock_irqrestore(&iommu->lock, flags);

	return domain;
}

static void iommu_free_domain(struct dmar_domain *domain)
{
	unsigned long flags;

	spin_lock_irqsave(&domain->iommu->lock, flags);
	clear_bit(domain->id, domain->iommu->domain_ids);
	spin_unlock_irqrestore(&domain->iommu->lock, flags);
}

static struct iova_domain reserved_iova_list;

static void dmar_init_reserved_ranges(void)
{
	struct pci_dev *pdev = NULL;
	struct iova *iova;
	int i;
	u64 addr, size;

	init_iova_domain(&reserved_iova_list, DMA_32BIT_PFN);

	/* IOAPIC ranges shouldn't be accessed by DMA */
	iova = reserve_iova(&reserved_iova_list, IOVA_PFN(IOAPIC_RANGE_START),
		IOVA_PFN(IOAPIC_RANGE_END));
	if (!iova)
		printk(KERN_ERR "Reserve IOAPIC range failed\n");

	/* Reserve all PCI MMIO to avoid peer-to-peer access */
	for_each_pci_dev(pdev) {
		struct resource *r;

		for (i = 0; i < PCI_NUM_RESOURCES; i++) {
			r = &pdev->resource[i];
			if (!r->flags || !(r->flags & IORESOURCE_MEM))
				continue;
			addr = r->start;
			addr &= PAGE_MASK_4K;
			size = r->end - addr;
			size = PAGE_ALIGN_4K(size);
			iova = reserve_iova(&reserved_iova_list, IOVA_PFN(addr),
				IOVA_PFN(size + addr) - 1);
			if (!iova)
				printk(KERN_ERR "Reserve iova failed\n");
		}
	}

}

static void domain_reserve_special_ranges(struct dmar_domain *domain)
{
	copy_reserved_iova(&reserved_iova_list, &domain->iovad);
}

static inline int guestwidth_to_adjustwidth(int gaw)
{
	int agaw;
	int r = (gaw - 12) % 9;

	if (r == 0)
		agaw = gaw;
	else
		agaw = gaw + 9 - r;
	if (agaw > 64)
		agaw = 64;
	return agaw;
}

static int domain_init(struct dmar_domain *domain, int guest_width)
{
	struct intel_iommu *iommu;
	int adjust_width, agaw;
	unsigned long sagaw;

	init_iova_domain(&domain->iovad, DMA_32BIT_PFN);
	spin_lock_init(&domain->mapping_lock);

	domain_reserve_special_ranges(domain);

	/* calculate AGAW */
	iommu = domain->iommu;
	if (guest_width > cap_mgaw(iommu->cap))
		guest_width = cap_mgaw(iommu->cap);
	domain->gaw = guest_width;
	adjust_width = guestwidth_to_adjustwidth(guest_width);
	agaw = width_to_agaw(adjust_width);
	sagaw = cap_sagaw(iommu->cap);
	if (!test_bit(agaw, &sagaw)) {
		/* hardware doesn't support it, choose a bigger one */
		pr_debug("IOMMU: hardware doesn't support agaw %d\n", agaw);
		agaw = find_next_bit(&sagaw, 5, agaw);
		if (agaw >= 5)
			return -ENODEV;
	}
	domain->agaw = agaw;
	INIT_LIST_HEAD(&domain->devices);

	/* always allocate the top pgd */
	domain->pgd = (struct dma_pte *)alloc_pgtable_page();
	if (!domain->pgd)
		return -ENOMEM;
	__iommu_flush_cache(iommu, domain->pgd, PAGE_SIZE_4K);
	return 0;
}

static void domain_exit(struct dmar_domain *domain)
{
	u64 end;

	/* Domain 0 is reserved, so dont process it */
	if (!domain)
		return;

	domain_remove_dev_info(domain);
	/* destroy iovas */
	put_iova_domain(&domain->iovad);
	end = DOMAIN_MAX_ADDR(domain->gaw);
	end = end & (~PAGE_MASK_4K);

	/* clear ptes */
	dma_pte_clear_range(domain, 0, end);

	/* free page tables */
	dma_pte_free_pagetable(domain, 0, end);

	iommu_free_domain(domain);
	free_domain_mem(domain);
}

static int domain_context_mapping_one(struct dmar_domain *domain,
		u8 bus, u8 devfn)
{
	struct context_entry *context;
	struct intel_iommu *iommu = domain->iommu;
	unsigned long flags;

	pr_debug("Set context mapping for %02x:%02x.%d\n",
		bus, PCI_SLOT(devfn), PCI_FUNC(devfn));
	BUG_ON(!domain->pgd);
	context = device_to_context_entry(iommu, bus, devfn);
	if (!context)
		return -ENOMEM;
	spin_lock_irqsave(&iommu->lock, flags);
	if (context_present(*context)) {
		spin_unlock_irqrestore(&iommu->lock, flags);
		return 0;
	}

	context_set_domain_id(*context, domain->id);
	context_set_address_width(*context, domain->agaw);
	context_set_address_root(*context, virt_to_phys(domain->pgd));
	context_set_translation_type(*context, CONTEXT_TT_MULTI_LEVEL);
	context_set_fault_enable(*context);
	context_set_present(*context);
	__iommu_flush_cache(iommu, context, sizeof(*context));

	/* it's a non-present to present mapping */
	if (iommu_flush_context_device(iommu, domain->id,
			(((u16)bus) << 8) | devfn, DMA_CCMD_MASK_NOBIT, 1))
		iommu_flush_write_buffer(iommu);
	else
		iommu_flush_iotlb_dsi(iommu, 0, 0);
	spin_unlock_irqrestore(&iommu->lock, flags);
	return 0;
}

static int
domain_context_mapping(struct dmar_domain *domain, struct pci_dev *pdev)
{
	int ret;
	struct pci_dev *tmp, *parent;

	ret = domain_context_mapping_one(domain, pdev->bus->number,
		pdev->devfn);
	if (ret)
		return ret;

	/* dependent device mapping */
	tmp = pci_find_upstream_pcie_bridge(pdev);
	if (!tmp)
		return 0;
	/* Secondary interface's bus number and devfn 0 */
	parent = pdev->bus->self;
	while (parent != tmp) {
		ret = domain_context_mapping_one(domain, parent->bus->number,
			parent->devfn);
		if (ret)
			return ret;
		parent = parent->bus->self;
	}
	if (tmp->is_pcie) /* this is a PCIE-to-PCI bridge */
		return domain_context_mapping_one(domain,
			tmp->subordinate->number, 0);
	else /* this is a legacy PCI bridge */
		return domain_context_mapping_one(domain,
			tmp->bus->number, tmp->devfn);
}

static int domain_context_mapped(struct dmar_domain *domain,
	struct pci_dev *pdev)
{
	int ret;
	struct pci_dev *tmp, *parent;

	ret = device_context_mapped(domain->iommu,
		pdev->bus->number, pdev->devfn);
	if (!ret)
		return ret;
	/* dependent device mapping */
	tmp = pci_find_upstream_pcie_bridge(pdev);
	if (!tmp)
		return ret;
	/* Secondary interface's bus number and devfn 0 */
	parent = pdev->bus->self;
	while (parent != tmp) {
		ret = device_context_mapped(domain->iommu, parent->bus->number,
			parent->devfn);
		if (!ret)
			return ret;
		parent = parent->bus->self;
	}
	if (tmp->is_pcie)
		return device_context_mapped(domain->iommu,
			tmp->subordinate->number, 0);
	else
		return device_context_mapped(domain->iommu,
			tmp->bus->number, tmp->devfn);
}

static int
domain_page_mapping(struct dmar_domain *domain, dma_addr_t iova,
			u64 hpa, size_t size, int prot)
{
	u64 start_pfn, end_pfn;
	struct dma_pte *pte;
	int index;

	if ((prot & (DMA_PTE_READ|DMA_PTE_WRITE)) == 0)
		return -EINVAL;
	iova &= PAGE_MASK_4K;
	start_pfn = ((u64)hpa) >> PAGE_SHIFT_4K;
	end_pfn = (PAGE_ALIGN_4K(((u64)hpa) + size)) >> PAGE_SHIFT_4K;
	index = 0;
	while (start_pfn < end_pfn) {
		pte = addr_to_dma_pte(domain, iova + PAGE_SIZE_4K * index);
		if (!pte)
			return -ENOMEM;
		/* We don't need lock here, nobody else
		 * touches the iova range
		 */
		BUG_ON(dma_pte_addr(*pte));
		dma_set_pte_addr(*pte, start_pfn << PAGE_SHIFT_4K);
		dma_set_pte_prot(*pte, prot);
		__iommu_flush_cache(domain->iommu, pte, sizeof(*pte));
		start_pfn++;
		index++;
	}
	return 0;
}

static void detach_domain_for_dev(struct dmar_domain *domain, u8 bus, u8 devfn)
{
	clear_context_table(domain->iommu, bus, devfn);
	iommu_flush_context_global(domain->iommu, 0);
	iommu_flush_iotlb_global(domain->iommu, 0);
}

static void domain_remove_dev_info(struct dmar_domain *domain)
{
	struct device_domain_info *info;
	unsigned long flags;

	spin_lock_irqsave(&device_domain_lock, flags);
	while (!list_empty(&domain->devices)) {
		info = list_entry(domain->devices.next,
			struct device_domain_info, link);
		list_del(&info->link);
		list_del(&info->global);
		if (info->dev)
			info->dev->dev.archdata.iommu = NULL;
		spin_unlock_irqrestore(&device_domain_lock, flags);

		detach_domain_for_dev(info->domain, info->bus, info->devfn);
		free_devinfo_mem(info);

		spin_lock_irqsave(&device_domain_lock, flags);
	}
	spin_unlock_irqrestore(&device_domain_lock, flags);
}

/*
 * find_domain
 * Note: we use struct pci_dev->dev.archdata.iommu stores the info
 */
struct dmar_domain *
find_domain(struct pci_dev *pdev)
{
	struct device_domain_info *info;

	/* No lock here, assumes no domain exit in normal case */
	info = pdev->dev.archdata.iommu;
	if (info)
		return info->domain;
	return NULL;
}

static int dmar_pci_device_match(struct pci_dev *devices[], int cnt,
     struct pci_dev *dev)
{
	int index;

	while (dev) {
		for (index = 0; index < cnt; index ++)
			if (dev == devices[index])
				return 1;

		/* Check our parent */
		dev = dev->bus->self;
	}

	return 0;
}

static struct dmar_drhd_unit *
dmar_find_matched_drhd_unit(struct pci_dev *dev)
{
	struct dmar_drhd_unit *drhd = NULL;

	list_for_each_entry(drhd, &dmar_drhd_units, list) {
		if (drhd->include_all || dmar_pci_device_match(drhd->devices,
						drhd->devices_cnt, dev))
			return drhd;
	}

	return NULL;
}

/* domain is initialized */
static struct dmar_domain *get_domain_for_dev(struct pci_dev *pdev, int gaw)
{
	struct dmar_domain *domain, *found = NULL;
	struct intel_iommu *iommu;
	struct dmar_drhd_unit *drhd;
	struct device_domain_info *info, *tmp;
	struct pci_dev *dev_tmp;
	unsigned long flags;
	int bus = 0, devfn = 0;

	domain = find_domain(pdev);
	if (domain)
		return domain;

	dev_tmp = pci_find_upstream_pcie_bridge(pdev);
	if (dev_tmp) {
		if (dev_tmp->is_pcie) {
			bus = dev_tmp->subordinate->number;
			devfn = 0;
		} else {
			bus = dev_tmp->bus->number;
			devfn = dev_tmp->devfn;
		}
		spin_lock_irqsave(&device_domain_lock, flags);
		list_for_each_entry(info, &device_domain_list, global) {
			if (info->bus == bus && info->devfn == devfn) {
				found = info->domain;
				break;
			}
		}
		spin_unlock_irqrestore(&device_domain_lock, flags);
		/* pcie-pci bridge already has a domain, uses it */
		if (found) {
			domain = found;
			goto found_domain;
		}
	}

	/* Allocate new domain for the device */
	drhd = dmar_find_matched_drhd_unit(pdev);
	if (!drhd) {
		printk(KERN_ERR "IOMMU: can't find DMAR for device %s\n",
			pci_name(pdev));
		return NULL;
	}
	iommu = drhd->iommu;

	domain = iommu_alloc_domain(iommu);
	if (!domain)
		goto error;

	if (domain_init(domain, gaw)) {
		domain_exit(domain);
		goto error;
	}

	/* register pcie-to-pci device */
	if (dev_tmp) {
		info = alloc_devinfo_mem();
		if (!info) {
			domain_exit(domain);
			goto error;
		}
		info->bus = bus;
		info->devfn = devfn;
		info->dev = NULL;
		info->domain = domain;
		/* This domain is shared by devices under p2p bridge */
		domain->flags |= DOMAIN_FLAG_MULTIPLE_DEVICES;

		/* pcie-to-pci bridge already has a domain, uses it */
		found = NULL;
		spin_lock_irqsave(&device_domain_lock, flags);
		list_for_each_entry(tmp, &device_domain_list, global) {
			if (tmp->bus == bus && tmp->devfn == devfn) {
				found = tmp->domain;
				break;
			}
		}
		if (found) {
			free_devinfo_mem(info);
			domain_exit(domain);
			domain = found;
		} else {
			list_add(&info->link, &domain->devices);
			list_add(&info->global, &device_domain_list);
		}
		spin_unlock_irqrestore(&device_domain_lock, flags);
	}

found_domain:
	info = alloc_devinfo_mem();
	if (!info)
		goto error;
	info->bus = pdev->bus->number;
	info->devfn = pdev->devfn;
	info->dev = pdev;
	info->domain = domain;
	spin_lock_irqsave(&device_domain_lock, flags);
	/* somebody is fast */
	found = find_domain(pdev);
	if (found != NULL) {
		spin_unlock_irqrestore(&device_domain_lock, flags);
		if (found != domain) {
			domain_exit(domain);
			domain = found;
		}
		free_devinfo_mem(info);
		return domain;
	}
	list_add(&info->link, &domain->devices);
	list_add(&info->global, &device_domain_list);
	pdev->dev.archdata.iommu = info;
	spin_unlock_irqrestore(&device_domain_lock, flags);
	return domain;
error:
	/* recheck it here, maybe others set it */
	return find_domain(pdev);
}

static int iommu_prepare_identity_map(struct pci_dev *pdev, u64 start, u64 end)
{
	struct dmar_domain *domain;
	unsigned long size;
	u64 base;
	int ret;

	printk(KERN_INFO
		"IOMMU: Setting identity map for device %s [0x%Lx - 0x%Lx]\n",
		pci_name(pdev), start, end);
	/* page table init */
	domain = get_domain_for_dev(pdev, DEFAULT_DOMAIN_ADDRESS_WIDTH);
	if (!domain)
		return -ENOMEM;

	/* The address might not be aligned */
	base = start & PAGE_MASK_4K;
	size = end - base;
	size = PAGE_ALIGN_4K(size);
	if (!reserve_iova(&domain->iovad, IOVA_PFN(base),
			IOVA_PFN(base + size) - 1)) {
		printk(KERN_ERR "IOMMU: reserve iova failed\n");
		ret = -ENOMEM;
		goto error;
	}

	pr_debug("Mapping reserved region %lx@%llx for %s\n",
		size, base, pci_name(pdev));
	/*
	 * RMRR range might have overlap with physical memory range,
	 * clear it first
	 */
	dma_pte_clear_range(domain, base, base + size);

	ret = domain_page_mapping(domain, base, base, size,
		DMA_PTE_READ|DMA_PTE_WRITE);
	if (ret)
		goto error;

	/* context entry init */
	ret = domain_context_mapping(domain, pdev);
	if (!ret)
		return 0;
error:
	domain_exit(domain);
	return ret;

}

static inline int iommu_prepare_rmrr_dev(struct dmar_rmrr_unit *rmrr,
	struct pci_dev *pdev)
{
	if (pdev->dev.archdata.iommu == DUMMY_DEVICE_DOMAIN_INFO)
		return 0;
	return iommu_prepare_identity_map(pdev, rmrr->base_address,
		rmrr->end_address + 1);
}

#ifdef CONFIG_DMAR_GFX_WA
extern int arch_get_ram_range(int slot, u64 *addr, u64 *size);
static void __init iommu_prepare_gfx_mapping(void)
{
	struct pci_dev *pdev = NULL;
	u64 base, size;
	int slot;
	int ret;

	for_each_pci_dev(pdev) {
		if (pdev->dev.archdata.iommu == DUMMY_DEVICE_DOMAIN_INFO ||
				!IS_GFX_DEVICE(pdev))
			continue;
		printk(KERN_INFO "IOMMU: gfx device %s 1-1 mapping\n",
			pci_name(pdev));
		slot = arch_get_ram_range(0, &base, &size);
		while (slot >= 0) {
			ret = iommu_prepare_identity_map(pdev,
					base, base + size);
			if (ret)
				goto error;
			slot = arch_get_ram_range(slot, &base, &size);
		}
		continue;
error:
		printk(KERN_ERR "IOMMU: mapping reserved region failed\n");
	}
}
#endif

#ifdef CONFIG_DMAR_FLOPPY_WA
static inline void iommu_prepare_isa(void)
{
	struct pci_dev *pdev;
	int ret;

	pdev = pci_get_class(PCI_CLASS_BRIDGE_ISA << 8, NULL);
	if (!pdev)
		return;

	printk(KERN_INFO "IOMMU: Prepare 0-16M unity mapping for LPC\n");
	ret = iommu_prepare_identity_map(pdev, 0, 16*1024*1024);

	if (ret)
		printk("IOMMU: Failed to create 0-64M identity map, "
			"floppy might not work\n");

}
#else
static inline void iommu_prepare_isa(void)
{
	return;
}
#endif /* !CONFIG_DMAR_FLPY_WA */

int __init init_dmars(void)
{
	struct dmar_drhd_unit *drhd;
	struct dmar_rmrr_unit *rmrr;
	struct pci_dev *pdev;
	struct intel_iommu *iommu;
	int ret, unit = 0;

	/*
	 * for each drhd
	 *    allocate root
	 *    initialize and program root entry to not present
	 * endfor
	 */
	for_each_drhd_unit(drhd) {
		if (drhd->ignored)
			continue;
		iommu = alloc_iommu(drhd);
		if (!iommu) {
			ret = -ENOMEM;
			goto error;
		}

		/*
		 * TBD:
		 * we could share the same root & context tables
		 * amoung all IOMMU's. Need to Split it later.
		 */
		ret = iommu_alloc_root_entry(iommu);
		if (ret) {
			printk(KERN_ERR "IOMMU: allocate root entry failed\n");
			goto error;
		}
	}

	/*
	 * For each rmrr
	 *   for each dev attached to rmrr
	 *   do
	 *     locate drhd for dev, alloc domain for dev
	 *     allocate free domain
	 *     allocate page table entries for rmrr
	 *     if context not allocated for bus
	 *           allocate and init context
	 *           set present in root table for this bus
	 *     init context with domain, translation etc
	 *    endfor
	 * endfor
	 */
	for_each_rmrr_units(rmrr) {
		int i;
		for (i = 0; i < rmrr->devices_cnt; i++) {
			pdev = rmrr->devices[i];
			/* some BIOS lists non-exist devices in DMAR table */
			if (!pdev)
				continue;
			ret = iommu_prepare_rmrr_dev(rmrr, pdev);
			if (ret)
				printk(KERN_ERR
				 "IOMMU: mapping reserved region failed\n");
		}
	}

	iommu_prepare_gfx_mapping();

	iommu_prepare_isa();

	/*
	 * for each drhd
	 *   enable fault log
	 *   global invalidate context cache
	 *   global invalidate iotlb
	 *   enable translation
	 */
	for_each_drhd_unit(drhd) {
		if (drhd->ignored)
			continue;
		iommu = drhd->iommu;
		sprintf (iommu->name, "dmar%d", unit++);

		iommu_flush_write_buffer(iommu);

		ret = dmar_set_interrupt(iommu);
		if (ret)
			goto error;

		iommu_set_root_entry(iommu);

		iommu_flush_context_global(iommu, 0);
		iommu_flush_iotlb_global(iommu, 0);

		iommu_disable_protect_mem_regions(iommu);

		ret = iommu_enable_translation(iommu);
		if (ret)
			goto error;
	}

	return 0;
error:
	for_each_drhd_unit(drhd) {
		if (drhd->ignored)
			continue;
		iommu = drhd->iommu;
		free_iommu(iommu);
	}
	return ret;
}

static inline u64 aligned_size(u64 host_addr, size_t size)
{
	u64 addr;
	addr = (host_addr & (~PAGE_MASK_4K)) + size;
	return PAGE_ALIGN_4K(addr);
}

struct iova *
iommu_alloc_iova(struct dmar_domain *domain, size_t size, u64 end)
{
	struct iova *piova;

	/* Make sure it's in range */
	end = min_t(u64, DOMAIN_MAX_ADDR(domain->gaw), end);
	if (!size || (IOVA_START_ADDR + size > end))
		return NULL;

	piova = alloc_iova(&domain->iovad,
			size >> PAGE_SHIFT_4K, IOVA_PFN(end), 1);
	return piova;
}

static struct iova *
__intel_alloc_iova(struct device *dev, struct dmar_domain *domain,
		size_t size)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct iova *iova = NULL;

	if ((pdev->dma_mask <= DMA_32BIT_MASK) || (dmar_forcedac)) {
		iova = iommu_alloc_iova(domain, size, pdev->dma_mask);
	} else  {
		/*
		 * First try to allocate an io virtual address in
		 * DMA_32BIT_MASK and if that fails then try allocating
		 * from higher range
		 */
		iova = iommu_alloc_iova(domain, size, DMA_32BIT_MASK);
		if (!iova)
			iova = iommu_alloc_iova(domain, size, pdev->dma_mask);
	}

	if (!iova) {
		printk(KERN_ERR"Allocating iova for %s failed", pci_name(pdev));
		return NULL;
	}

	return iova;
}

static struct dmar_domain *
get_valid_domain_for_dev(struct pci_dev *pdev)
{
	struct dmar_domain *domain;
	int ret;

	domain = get_domain_for_dev(pdev,
			DEFAULT_DOMAIN_ADDRESS_WIDTH);
	if (!domain) {
		printk(KERN_ERR
			"Allocating domain for %s failed", pci_name(pdev));
		return NULL;
	}

	/* make sure context mapping is ok */
	if (unlikely(!domain_context_mapped(domain, pdev))) {
		ret = domain_context_mapping(domain, pdev);
		if (ret) {
			printk(KERN_ERR
				"Domain context map for %s failed",
				pci_name(pdev));
			return NULL;
		}
	}

	return domain;
}

static dma_addr_t intel_map_single(struct device *hwdev, void *addr,
	size_t size, int dir)
{
	struct pci_dev *pdev = to_pci_dev(hwdev);
	int ret;
	struct dmar_domain *domain;
	unsigned long start_addr;
	struct iova *iova;
	int prot = 0;

	BUG_ON(dir == DMA_NONE);
	if (pdev->dev.archdata.iommu == DUMMY_DEVICE_DOMAIN_INFO)
		return virt_to_bus(addr);

	domain = get_valid_domain_for_dev(pdev);
	if (!domain)
		return 0;

	addr = (void *)virt_to_phys(addr);
	size = aligned_size((u64)addr, size);

	iova = __intel_alloc_iova(hwdev, domain, size);
	if (!iova)
		goto error;

	start_addr = iova->pfn_lo << PAGE_SHIFT_4K;

	/*
	 * Check if DMAR supports zero-length reads on write only
	 * mappings..
	 */
	if (dir == DMA_TO_DEVICE || dir == DMA_BIDIRECTIONAL || \
			!cap_zlr(domain->iommu->cap))
		prot |= DMA_PTE_READ;
	if (dir == DMA_FROM_DEVICE || dir == DMA_BIDIRECTIONAL)
		prot |= DMA_PTE_WRITE;
	/*
	 * addr - (addr + size) might be partial page, we should map the whole
	 * page.  Note: if two part of one page are separately mapped, we
	 * might have two guest_addr mapping to the same host addr, but this
	 * is not a big problem
	 */
	ret = domain_page_mapping(domain, start_addr,
		((u64)addr) & PAGE_MASK_4K, size, prot);
	if (ret)
		goto error;

	pr_debug("Device %s request: %lx@%llx mapping: %lx@%llx, dir %d\n",
		pci_name(pdev), size, (u64)addr,
		size, (u64)start_addr, dir);

	/* it's a non-present to present mapping */
	ret = iommu_flush_iotlb_psi(domain->iommu, domain->id,
			start_addr, size >> PAGE_SHIFT_4K, 1);
	if (ret)
		iommu_flush_write_buffer(domain->iommu);

	return (start_addr + ((u64)addr & (~PAGE_MASK_4K)));

error:
	if (iova)
		__free_iova(&domain->iovad, iova);
	printk(KERN_ERR"Device %s request: %lx@%llx dir %d --- failed\n",
		pci_name(pdev), size, (u64)addr, dir);
	return 0;
}

static void intel_unmap_single(struct device *dev, dma_addr_t dev_addr,
	size_t size, int dir)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct dmar_domain *domain;
	unsigned long start_addr;
	struct iova *iova;

	if (pdev->dev.archdata.iommu == DUMMY_DEVICE_DOMAIN_INFO)
		return;
	domain = find_domain(pdev);
	BUG_ON(!domain);

	iova = find_iova(&domain->iovad, IOVA_PFN(dev_addr));
	if (!iova)
		return;

	start_addr = iova->pfn_lo << PAGE_SHIFT_4K;
	size = aligned_size((u64)dev_addr, size);

	pr_debug("Device %s unmapping: %lx@%llx\n",
		pci_name(pdev), size, (u64)start_addr);

	/*  clear the whole page */
	dma_pte_clear_range(domain, start_addr, start_addr + size);
	/* free page tables */
	dma_pte_free_pagetable(domain, start_addr, start_addr + size);

	if (iommu_flush_iotlb_psi(domain->iommu, domain->id, start_addr,
			size >> PAGE_SHIFT_4K, 0))
		iommu_flush_write_buffer(domain->iommu);

	/* free iova */
	__free_iova(&domain->iovad, iova);
}

static void * intel_alloc_coherent(struct device *hwdev, size_t size,
		       dma_addr_t *dma_handle, gfp_t flags)
{
	void *vaddr;
	int order;

	size = PAGE_ALIGN_4K(size);
	order = get_order(size);
	flags &= ~(GFP_DMA | GFP_DMA32);

	vaddr = (void *)__get_free_pages(flags, order);
	if (!vaddr)
		return NULL;
	memset(vaddr, 0, size);

	*dma_handle = intel_map_single(hwdev, vaddr, size, DMA_BIDIRECTIONAL);
	if (*dma_handle)
		return vaddr;
	free_pages((unsigned long)vaddr, order);
	return NULL;
}

static void intel_free_coherent(struct device *hwdev, size_t size,
	void *vaddr, dma_addr_t dma_handle)
{
	int order;

	size = PAGE_ALIGN_4K(size);
	order = get_order(size);

	intel_unmap_single(hwdev, dma_handle, size, DMA_BIDIRECTIONAL);
	free_pages((unsigned long)vaddr, order);
}

#define SG_ENT_VIRT_ADDRESS(sg)	(sg_virt((sg)))
static void intel_unmap_sg(struct device *hwdev, struct scatterlist *sglist,
	int nelems, int dir)
{
	int i;
	struct pci_dev *pdev = to_pci_dev(hwdev);
	struct dmar_domain *domain;
	unsigned long start_addr;
	struct iova *iova;
	size_t size = 0;
	void *addr;
	struct scatterlist *sg;

	if (pdev->dev.archdata.iommu == DUMMY_DEVICE_DOMAIN_INFO)
		return;

	domain = find_domain(pdev);

	iova = find_iova(&domain->iovad, IOVA_PFN(sglist[0].dma_address));
	if (!iova)
		return;
	for_each_sg(sglist, sg, nelems, i) {
		addr = SG_ENT_VIRT_ADDRESS(sg);
		size += aligned_size((u64)addr, sg->length);
	}

	start_addr = iova->pfn_lo << PAGE_SHIFT_4K;

	/*  clear the whole page */
	dma_pte_clear_range(domain, start_addr, start_addr + size);
	/* free page tables */
	dma_pte_free_pagetable(domain, start_addr, start_addr + size);

	if (iommu_flush_iotlb_psi(domain->iommu, domain->id, start_addr,
			size >> PAGE_SHIFT_4K, 0))
		iommu_flush_write_buffer(domain->iommu);

	/* free iova */
	__free_iova(&domain->iovad, iova);
}

static int intel_nontranslate_map_sg(struct device *hddev,
	struct scatterlist *sglist, int nelems, int dir)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sglist, sg, nelems, i) {
		BUG_ON(!sg_page(sg));
		sg->dma_address = virt_to_bus(SG_ENT_VIRT_ADDRESS(sg));
		sg->dma_length = sg->length;
	}
	return nelems;
}

static int intel_map_sg(struct device *hwdev, struct scatterlist *sglist,
				int nelems, int dir)
{
	void *addr;
	int i;
	struct pci_dev *pdev = to_pci_dev(hwdev);
	struct dmar_domain *domain;
	size_t size = 0;
	int prot = 0;
	size_t offset = 0;
	struct iova *iova = NULL;
	int ret;
	struct scatterlist *sg;
	unsigned long start_addr;

	BUG_ON(dir == DMA_NONE);
	if (pdev->dev.archdata.iommu == DUMMY_DEVICE_DOMAIN_INFO)
		return intel_nontranslate_map_sg(hwdev, sglist, nelems, dir);

	domain = get_valid_domain_for_dev(pdev);
	if (!domain)
		return 0;

	for_each_sg(sglist, sg, nelems, i) {
		addr = SG_ENT_VIRT_ADDRESS(sg);
		addr = (void *)virt_to_phys(addr);
		size += aligned_size((u64)addr, sg->length);
	}

	iova = __intel_alloc_iova(hwdev, domain, size);
	if (!iova) {
		sglist->dma_length = 0;
		return 0;
	}

	/*
	 * Check if DMAR supports zero-length reads on write only
	 * mappings..
	 */
	if (dir == DMA_TO_DEVICE || dir == DMA_BIDIRECTIONAL || \
			!cap_zlr(domain->iommu->cap))
		prot |= DMA_PTE_READ;
	if (dir == DMA_FROM_DEVICE || dir == DMA_BIDIRECTIONAL)
		prot |= DMA_PTE_WRITE;

	start_addr = iova->pfn_lo << PAGE_SHIFT_4K;
	offset = 0;
	for_each_sg(sglist, sg, nelems, i) {
		addr = SG_ENT_VIRT_ADDRESS(sg);
		addr = (void *)virt_to_phys(addr);
		size = aligned_size((u64)addr, sg->length);
		ret = domain_page_mapping(domain, start_addr + offset,
			((u64)addr) & PAGE_MASK_4K,
			size, prot);
		if (ret) {
			/*  clear the page */
			dma_pte_clear_range(domain, start_addr,
				  start_addr + offset);
			/* free page tables */
			dma_pte_free_pagetable(domain, start_addr,
				  start_addr + offset);
			/* free iova */
			__free_iova(&domain->iovad, iova);
			return 0;
		}
		sg->dma_address = start_addr + offset +
				((u64)addr & (~PAGE_MASK_4K));
		sg->dma_length = sg->length;
		offset += size;
	}

	/* it's a non-present to present mapping */
	if (iommu_flush_iotlb_psi(domain->iommu, domain->id,
			start_addr, offset >> PAGE_SHIFT_4K, 1))
		iommu_flush_write_buffer(domain->iommu);
	return nelems;
}

static struct dma_mapping_ops intel_dma_ops = {
	.alloc_coherent = intel_alloc_coherent,
	.free_coherent = intel_free_coherent,
	.map_single = intel_map_single,
	.unmap_single = intel_unmap_single,
	.map_sg = intel_map_sg,
	.unmap_sg = intel_unmap_sg,
};

static inline int iommu_domain_cache_init(void)
{
	int ret = 0;

	iommu_domain_cache = kmem_cache_create("iommu_domain",
					 sizeof(struct dmar_domain),
					 0,
					 SLAB_HWCACHE_ALIGN,

					 NULL);
	if (!iommu_domain_cache) {
		printk(KERN_ERR "Couldn't create iommu_domain cache\n");
		ret = -ENOMEM;
	}

	return ret;
}

static inline int iommu_devinfo_cache_init(void)
{
	int ret = 0;

	iommu_devinfo_cache = kmem_cache_create("iommu_devinfo",
					 sizeof(struct device_domain_info),
					 0,
					 SLAB_HWCACHE_ALIGN,

					 NULL);
	if (!iommu_devinfo_cache) {
		printk(KERN_ERR "Couldn't create devinfo cache\n");
		ret = -ENOMEM;
	}

	return ret;
}

static inline int iommu_iova_cache_init(void)
{
	int ret = 0;

	iommu_iova_cache = kmem_cache_create("iommu_iova",
					 sizeof(struct iova),
					 0,
					 SLAB_HWCACHE_ALIGN,

					 NULL);
	if (!iommu_iova_cache) {
		printk(KERN_ERR "Couldn't create iova cache\n");
		ret = -ENOMEM;
	}

	return ret;
}

static int __init iommu_init_mempool(void)
{
	int ret;
	ret = iommu_iova_cache_init();
	if (ret)
		return ret;

	ret = iommu_domain_cache_init();
	if (ret)
		goto domain_error;

	ret = iommu_devinfo_cache_init();
	if (!ret)
		return ret;

	kmem_cache_destroy(iommu_domain_cache);
domain_error:
	kmem_cache_destroy(iommu_iova_cache);

	return -ENOMEM;
}

static void __init iommu_exit_mempool(void)
{
	kmem_cache_destroy(iommu_devinfo_cache);
	kmem_cache_destroy(iommu_domain_cache);
	kmem_cache_destroy(iommu_iova_cache);

}

void __init detect_intel_iommu(void)
{
	if (swiotlb || no_iommu || iommu_detected || dmar_disabled)
		return;
	if (early_dmar_detect()) {
		iommu_detected = 1;
	}
}

static void __init init_no_remapping_devices(void)
{
	struct dmar_drhd_unit *drhd;

	for_each_drhd_unit(drhd) {
		if (!drhd->include_all) {
			int i;
			for (i = 0; i < drhd->devices_cnt; i++)
				if (drhd->devices[i] != NULL)
					break;
			/* ignore DMAR unit if no pci devices exist */
			if (i == drhd->devices_cnt)
				drhd->ignored = 1;
		}
	}

	if (dmar_map_gfx)
		return;

	for_each_drhd_unit(drhd) {
		int i;
		if (drhd->ignored || drhd->include_all)
			continue;

		for (i = 0; i < drhd->devices_cnt; i++)
			if (drhd->devices[i] &&
				!IS_GFX_DEVICE(drhd->devices[i]))
				break;

		if (i < drhd->devices_cnt)
			continue;

		/* bypass IOMMU if it is just for gfx devices */
		drhd->ignored = 1;
		for (i = 0; i < drhd->devices_cnt; i++) {
			if (!drhd->devices[i])
				continue;
			drhd->devices[i]->dev.archdata.iommu = DUMMY_DEVICE_DOMAIN_INFO;
		}
	}
}

int __init intel_iommu_init(void)
{
	int ret = 0;

	if (no_iommu || swiotlb || dmar_disabled)
		return -ENODEV;

	if (dmar_table_init())
		return 	-ENODEV;

	iommu_init_mempool();
	dmar_init_reserved_ranges();

	init_no_remapping_devices();

	ret = init_dmars();
	if (ret) {
		printk(KERN_ERR "IOMMU: dmar init failed\n");
		put_iova_domain(&reserved_iova_list);
		iommu_exit_mempool();
		return ret;
	}
	printk(KERN_INFO
	"PCI-DMA: Intel(R) Virtualization Technology for Directed I/O\n");

	force_iommu = 1;
	dma_ops = &intel_dma_ops;
	return 0;
}

