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
 * Copyright (C) 2006-2008 Intel Corporation
 * Author: Ashok Raj <ashok.raj@intel.com>
 * Author: Shaohua Li <shaohua.li@intel.com>
 * Author: Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 * Author: Fenghua Yu <fenghua.yu@intel.com>
 */

#include <linux/init.h>
#include <linux/bitmap.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/dmar.h>
#include <linux/dma-mapping.h>
#include <linux/mempool.h>
#include <linux/timer.h>
#include <linux/iova.h>
#include <linux/iommu.h>
#include <linux/intel-iommu.h>
#include <linux/sysdev.h>
#include <asm/cacheflush.h>
#include <asm/iommu.h>
#include "pci.h"

#define ROOT_SIZE		VTD_PAGE_SIZE
#define CONTEXT_SIZE		VTD_PAGE_SIZE

#define IS_GFX_DEVICE(pdev) ((pdev->class >> 16) == PCI_BASE_CLASS_DISPLAY)
#define IS_ISA_DEVICE(pdev) ((pdev->class >> 8) == PCI_CLASS_BRIDGE_ISA)

#define IOAPIC_RANGE_START	(0xfee00000)
#define IOAPIC_RANGE_END	(0xfeefffff)
#define IOVA_START_ADDR		(0x1000)

#define DEFAULT_DOMAIN_ADDRESS_WIDTH 48

#define DOMAIN_MAX_ADDR(gaw) ((((u64)1) << gaw) - 1)

#define IOVA_PFN(addr)		((addr) >> PAGE_SHIFT)
#define DMA_32BIT_PFN		IOVA_PFN(DMA_32BIT_MASK)
#define DMA_64BIT_PFN		IOVA_PFN(DMA_BIT_MASK(64))

/* global iommu list, set NULL for ignored DMAR units */
static struct intel_iommu **g_iommus;

static int rwbf_quirk;

/*
 * 0: Present
 * 1-11: Reserved
 * 12-63: Context Ptr (12 - (haw-1))
 * 64-127: Reserved
 */
struct root_entry {
	u64	val;
	u64	rsvd1;
};
#define ROOT_ENTRY_NR (VTD_PAGE_SIZE/sizeof(struct root_entry))
static inline bool root_present(struct root_entry *root)
{
	return (root->val & 1);
}
static inline void set_root_present(struct root_entry *root)
{
	root->val |= 1;
}
static inline void set_root_value(struct root_entry *root, unsigned long value)
{
	root->val |= value & VTD_PAGE_MASK;
}

static inline struct context_entry *
get_context_addr_from_root(struct root_entry *root)
{
	return (struct context_entry *)
		(root_present(root)?phys_to_virt(
		root->val & VTD_PAGE_MASK) :
		NULL);
}

/*
 * low 64 bits:
 * 0: present
 * 1: fault processing disable
 * 2-3: translation type
 * 12-63: address space root
 * high 64 bits:
 * 0-2: address width
 * 3-6: aval
 * 8-23: domain id
 */
struct context_entry {
	u64 lo;
	u64 hi;
};

static inline bool context_present(struct context_entry *context)
{
	return (context->lo & 1);
}
static inline void context_set_present(struct context_entry *context)
{
	context->lo |= 1;
}

static inline void context_set_fault_enable(struct context_entry *context)
{
	context->lo &= (((u64)-1) << 2) | 1;
}

#define CONTEXT_TT_MULTI_LEVEL 0

static inline void context_set_translation_type(struct context_entry *context,
						unsigned long value)
{
	context->lo &= (((u64)-1) << 4) | 3;
	context->lo |= (value & 3) << 2;
}

static inline void context_set_address_root(struct context_entry *context,
					    unsigned long value)
{
	context->lo |= value & VTD_PAGE_MASK;
}

static inline void context_set_address_width(struct context_entry *context,
					     unsigned long value)
{
	context->hi |= value & 7;
}

static inline void context_set_domain_id(struct context_entry *context,
					 unsigned long value)
{
	context->hi |= (value & ((1 << 16) - 1)) << 8;
}

static inline void context_clear_entry(struct context_entry *context)
{
	context->lo = 0;
	context->hi = 0;
}

/*
 * 0: readable
 * 1: writable
 * 2-6: reserved
 * 7: super page
 * 8-10: available
 * 11: snoop behavior
 * 12-63: Host physcial address
 */
struct dma_pte {
	u64 val;
};

static inline void dma_clear_pte(struct dma_pte *pte)
{
	pte->val = 0;
}

static inline void dma_set_pte_readable(struct dma_pte *pte)
{
	pte->val |= DMA_PTE_READ;
}

static inline void dma_set_pte_writable(struct dma_pte *pte)
{
	pte->val |= DMA_PTE_WRITE;
}

static inline void dma_set_pte_snp(struct dma_pte *pte)
{
	pte->val |= DMA_PTE_SNP;
}

static inline void dma_set_pte_prot(struct dma_pte *pte, unsigned long prot)
{
	pte->val = (pte->val & ~3) | (prot & 3);
}

static inline u64 dma_pte_addr(struct dma_pte *pte)
{
	return (pte->val & VTD_PAGE_MASK);
}

static inline void dma_set_pte_addr(struct dma_pte *pte, u64 addr)
{
	pte->val |= (addr & VTD_PAGE_MASK);
}

static inline bool dma_pte_present(struct dma_pte *pte)
{
	return (pte->val & 3) != 0;
}

/* devices under the same p2p bridge are owned in one domain */
#define DOMAIN_FLAG_P2P_MULTIPLE_DEVICES (1 << 0)

/* domain represents a virtual machine, more than one devices
 * across iommus may be owned in one domain, e.g. kvm guest.
 */
#define DOMAIN_FLAG_VIRTUAL_MACHINE	(1 << 1)

struct dmar_domain {
	int	id;			/* domain id */
	unsigned long iommu_bmp;	/* bitmap of iommus this domain uses*/

	struct list_head devices; 	/* all devices' list */
	struct iova_domain iovad;	/* iova's that belong to this domain */

	struct dma_pte	*pgd;		/* virtual address */
	spinlock_t	mapping_lock;	/* page table lock */
	int		gaw;		/* max guest address width */

	/* adjusted guest address width, 0 is level 2 30-bit */
	int		agaw;

	int		flags;		/* flags to find out type of domain */

	int		iommu_coherency;/* indicate coherency of iommu access */
	int		iommu_snooping; /* indicate snooping control feature*/
	int		iommu_count;	/* reference count of iommu */
	spinlock_t	iommu_lock;	/* protect iommu set in domain */
	u64		max_addr;	/* maximum mapped address */
};

/* PCI domain-device relationship */
struct device_domain_info {
	struct list_head link;	/* link to domain siblings */
	struct list_head global; /* link to global list */
	int segment;		/* PCI domain */
	u8 bus;			/* PCI bus number */
	u8 devfn;		/* PCI devfn number */
	struct pci_dev *dev; /* it's NULL for PCIE-to-PCI bridge */
	struct dmar_domain *domain; /* pointer to domain */
};

static void flush_unmaps_timeout(unsigned long data);

DEFINE_TIMER(unmap_timer,  flush_unmaps_timeout, 0, 0);

#define HIGH_WATER_MARK 250
struct deferred_flush_tables {
	int next;
	struct iova *iova[HIGH_WATER_MARK];
	struct dmar_domain *domain[HIGH_WATER_MARK];
};

static struct deferred_flush_tables *deferred_flush;

/* bitmap for indexing intel_iommus */
static int g_num_of_iommus;

static DEFINE_SPINLOCK(async_umap_flush_lock);
static LIST_HEAD(unmaps_to_do);

static int timer_on;
static long list_size;

static void domain_remove_dev_info(struct dmar_domain *domain);

#ifdef CONFIG_DMAR_DEFAULT_ON
int dmar_disabled = 0;
#else
int dmar_disabled = 1;
#endif /*CONFIG_DMAR_DEFAULT_ON*/

static int __initdata dmar_map_gfx = 1;
static int dmar_forcedac;
static int intel_iommu_strict;

#define DUMMY_DEVICE_DOMAIN_INFO ((struct device_domain_info *)(-1))
static DEFINE_SPINLOCK(device_domain_lock);
static LIST_HEAD(device_domain_list);

static struct iommu_ops intel_iommu_ops;

static int __init intel_iommu_setup(char *str)
{
	if (!str)
		return -EINVAL;
	while (*str) {
		if (!strncmp(str, "on", 2)) {
			dmar_disabled = 0;
			printk(KERN_INFO "Intel-IOMMU: enabled\n");
		} else if (!strncmp(str, "off", 3)) {
			dmar_disabled = 1;
			printk(KERN_INFO "Intel-IOMMU: disabled\n");
		} else if (!strncmp(str, "igfx_off", 8)) {
			dmar_map_gfx = 0;
			printk(KERN_INFO
				"Intel-IOMMU: disable GFX device mapping\n");
		} else if (!strncmp(str, "forcedac", 8)) {
			printk(KERN_INFO
				"Intel-IOMMU: Forcing DAC for PCI devices\n");
			dmar_forcedac = 1;
		} else if (!strncmp(str, "strict", 6)) {
			printk(KERN_INFO
				"Intel-IOMMU: disable batched IOTLB flush\n");
			intel_iommu_strict = 1;
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

static void free_domain_mem(void *vaddr)
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


static inline int width_to_agaw(int width);

/* calculate agaw for each iommu.
 * "SAGAW" may be different across iommus, use a default agaw, and
 * get a supported less agaw for iommus that don't support the default agaw.
 */
int iommu_calculate_agaw(struct intel_iommu *iommu)
{
	unsigned long sagaw;
	int agaw = -1;

	sagaw = cap_sagaw(iommu->cap);
	for (agaw = width_to_agaw(DEFAULT_DOMAIN_ADDRESS_WIDTH);
	     agaw >= 0; agaw--) {
		if (test_bit(agaw, &sagaw))
			break;
	}

	return agaw;
}

/* in native case, each domain is related to only one iommu */
static struct intel_iommu *domain_get_iommu(struct dmar_domain *domain)
{
	int iommu_id;

	BUG_ON(domain->flags & DOMAIN_FLAG_VIRTUAL_MACHINE);

	iommu_id = find_first_bit(&domain->iommu_bmp, g_num_of_iommus);
	if (iommu_id < 0 || iommu_id >= g_num_of_iommus)
		return NULL;

	return g_iommus[iommu_id];
}

static void domain_update_iommu_coherency(struct dmar_domain *domain)
{
	int i;

	domain->iommu_coherency = 1;

	i = find_first_bit(&domain->iommu_bmp, g_num_of_iommus);
	for (; i < g_num_of_iommus; ) {
		if (!ecap_coherent(g_iommus[i]->ecap)) {
			domain->iommu_coherency = 0;
			break;
		}
		i = find_next_bit(&domain->iommu_bmp, g_num_of_iommus, i+1);
	}
}

static void domain_update_iommu_snooping(struct dmar_domain *domain)
{
	int i;

	domain->iommu_snooping = 1;

	i = find_first_bit(&domain->iommu_bmp, g_num_of_iommus);
	for (; i < g_num_of_iommus; ) {
		if (!ecap_sc_support(g_iommus[i]->ecap)) {
			domain->iommu_snooping = 0;
			break;
		}
		i = find_next_bit(&domain->iommu_bmp, g_num_of_iommus, i+1);
	}
}

/* Some capabilities may be different across iommus */
static void domain_update_iommu_cap(struct dmar_domain *domain)
{
	domain_update_iommu_coherency(domain);
	domain_update_iommu_snooping(domain);
}

static struct intel_iommu *device_to_iommu(int segment, u8 bus, u8 devfn)
{
	struct dmar_drhd_unit *drhd = NULL;
	int i;

	for_each_drhd_unit(drhd) {
		if (drhd->ignored)
			continue;
		if (segment != drhd->segment)
			continue;

		for (i = 0; i < drhd->devices_cnt; i++) {
			if (drhd->devices[i] &&
			    drhd->devices[i]->bus->number == bus &&
			    drhd->devices[i]->devfn == devfn)
				return drhd->iommu;
			if (drhd->devices[i] &&
			    drhd->devices[i]->subordinate &&
			    drhd->devices[i]->subordinate->number <= bus &&
			    drhd->devices[i]->subordinate->subordinate >= bus)
				return drhd->iommu;
		}

		if (drhd->include_all)
			return drhd->iommu;
	}

	return NULL;
}

static void domain_flush_cache(struct dmar_domain *domain,
			       void *addr, int size)
{
	if (!domain->iommu_coherency)
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
		__iommu_flush_cache(iommu, (void *)context, CONTEXT_SIZE);
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
	ret = context_present(&context[devfn]);
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
		context_clear_entry(&context[devfn]);
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

		if (!dma_pte_present(pte)) {
			tmp_page = alloc_pgtable_page();

			if (!tmp_page) {
				spin_unlock_irqrestore(&domain->mapping_lock,
					flags);
				return NULL;
			}
			domain_flush_cache(domain, tmp_page, PAGE_SIZE);
			dma_set_pte_addr(pte, virt_to_phys(tmp_page));
			/*
			 * high level table always sets r/w, last level page
			 * table control read/write
			 */
			dma_set_pte_readable(pte);
			dma_set_pte_writable(pte);
			domain_flush_cache(domain, pte, sizeof(*pte));
		}
		parent = phys_to_virt(dma_pte_addr(pte));
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

		if (!dma_pte_present(pte))
			break;
		parent = phys_to_virt(dma_pte_addr(pte));
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
		dma_clear_pte(pte);
		domain_flush_cache(domain, pte, sizeof(*pte));
	}
}

/* clear last level pte, a tlb flush should be followed */
static void dma_pte_clear_range(struct dmar_domain *domain, u64 start, u64 end)
{
	int addr_width = agaw_to_width(domain->agaw);
	int npages;

	start &= (((u64)1) << addr_width) - 1;
	end &= (((u64)1) << addr_width) - 1;
	/* in case it's partial page */
	start = PAGE_ALIGN(start);
	end &= PAGE_MASK;
	npages = (end - start) / VTD_PAGE_SIZE;

	/* we don't need lock here, nobody else touches the iova range */
	while (npages--) {
		dma_pte_clear_one(domain, start);
		start += VTD_PAGE_SIZE;
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
					phys_to_virt(dma_pte_addr(pte)));
				dma_clear_pte(pte);
				domain_flush_cache(domain, pte, sizeof(*pte));
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

	__iommu_flush_cache(iommu, root, ROOT_SIZE);

	spin_lock_irqsave(&iommu->lock, flags);
	iommu->root_entry = root;
	spin_unlock_irqrestore(&iommu->lock, flags);

	return 0;
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

	if (!rwbf_quirk && !cap_rwbf(iommu->cap))
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

	/* flush context entry will implicitly flush write buffer */
	return 0;
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
			(unsigned long long)DMA_TLB_IIRG(type),
			(unsigned long long)DMA_TLB_IAIG(val));
	/* flush iotlb entry will implicitly flush write buffer */
	return 0;
}

static int iommu_flush_iotlb_psi(struct intel_iommu *iommu, u16 did,
	u64 addr, unsigned int pages, int non_present_entry_flush)
{
	unsigned int mask;

	BUG_ON(addr & (~VTD_PAGE_MASK));
	BUG_ON(pages == 0);

	/* Fallback to domain selective flush if no PSI support */
	if (!cap_pgsel_inv(iommu->cap))
		return iommu->flush.flush_iotlb(iommu, did, 0, 0,
						DMA_TLB_DSI_FLUSH,
						non_present_entry_flush);

	/*
	 * PSI requires page size to be 2 ^ x, and the base address is naturally
	 * aligned to the size
	 */
	mask = ilog2(__roundup_pow_of_two(pages));
	/* Fallback to domain selective flush if size is too big */
	if (mask > cap_max_amask_val(iommu->cap))
		return iommu->flush.flush_iotlb(iommu, did, 0, 0,
			DMA_TLB_DSI_FLUSH, non_present_entry_flush);

	return iommu->flush.flush_iotlb(iommu, did, addr, mask,
					DMA_TLB_PSI_FLUSH,
					non_present_entry_flush);
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

	spin_lock_init(&iommu->lock);

	/*
	 * if Caching mode is set, then invalid translations are tagged
	 * with domainid 0. Hence we need to pre-allocate it.
	 */
	if (cap_caching_mode(iommu->cap))
		set_bit(0, iommu->domain_ids);
	return 0;
}


static void domain_exit(struct dmar_domain *domain);
static void vm_domain_exit(struct dmar_domain *domain);

void free_dmar_iommu(struct intel_iommu *iommu)
{
	struct dmar_domain *domain;
	int i;
	unsigned long flags;

	i = find_first_bit(iommu->domain_ids, cap_ndoms(iommu->cap));
	for (; i < cap_ndoms(iommu->cap); ) {
		domain = iommu->domains[i];
		clear_bit(i, iommu->domain_ids);

		spin_lock_irqsave(&domain->iommu_lock, flags);
		if (--domain->iommu_count == 0) {
			if (domain->flags & DOMAIN_FLAG_VIRTUAL_MACHINE)
				vm_domain_exit(domain);
			else
				domain_exit(domain);
		}
		spin_unlock_irqrestore(&domain->iommu_lock, flags);

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

	g_iommus[iommu->seq_id] = NULL;

	/* if all iommus are freed, free g_iommus */
	for (i = 0; i < g_num_of_iommus; i++) {
		if (g_iommus[i])
			break;
	}

	if (i == g_num_of_iommus)
		kfree(g_iommus);

	/* free context mapping */
	free_context_table(iommu);
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
	memset(&domain->iommu_bmp, 0, sizeof(unsigned long));
	set_bit(iommu->seq_id, &domain->iommu_bmp);
	domain->flags = 0;
	iommu->domains[num] = domain;
	spin_unlock_irqrestore(&iommu->lock, flags);

	return domain;
}

static void iommu_free_domain(struct dmar_domain *domain)
{
	unsigned long flags;
	struct intel_iommu *iommu;

	iommu = domain_get_iommu(domain);

	spin_lock_irqsave(&iommu->lock, flags);
	clear_bit(domain->id, iommu->domain_ids);
	spin_unlock_irqrestore(&iommu->lock, flags);
}

static struct iova_domain reserved_iova_list;
static struct lock_class_key reserved_alloc_key;
static struct lock_class_key reserved_rbtree_key;

static void dmar_init_reserved_ranges(void)
{
	struct pci_dev *pdev = NULL;
	struct iova *iova;
	int i;
	u64 addr, size;

	init_iova_domain(&reserved_iova_list, DMA_32BIT_PFN);

	lockdep_set_class(&reserved_iova_list.iova_alloc_lock,
		&reserved_alloc_key);
	lockdep_set_class(&reserved_iova_list.iova_rbtree_lock,
		&reserved_rbtree_key);

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
			addr &= PAGE_MASK;
			size = r->end - addr;
			size = PAGE_ALIGN(size);
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
	spin_lock_init(&domain->iommu_lock);

	domain_reserve_special_ranges(domain);

	/* calculate AGAW */
	iommu = domain_get_iommu(domain);
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

	if (ecap_coherent(iommu->ecap))
		domain->iommu_coherency = 1;
	else
		domain->iommu_coherency = 0;

	if (ecap_sc_support(iommu->ecap))
		domain->iommu_snooping = 1;
	else
		domain->iommu_snooping = 0;

	domain->iommu_count = 1;

	/* always allocate the top pgd */
	domain->pgd = (struct dma_pte *)alloc_pgtable_page();
	if (!domain->pgd)
		return -ENOMEM;
	__iommu_flush_cache(iommu, domain->pgd, PAGE_SIZE);
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
	end = end & (~PAGE_MASK);

	/* clear ptes */
	dma_pte_clear_range(domain, 0, end);

	/* free page tables */
	dma_pte_free_pagetable(domain, 0, end);

	iommu_free_domain(domain);
	free_domain_mem(domain);
}

static int domain_context_mapping_one(struct dmar_domain *domain,
				      int segment, u8 bus, u8 devfn)
{
	struct context_entry *context;
	unsigned long flags;
	struct intel_iommu *iommu;
	struct dma_pte *pgd;
	unsigned long num;
	unsigned long ndomains;
	int id;
	int agaw;

	pr_debug("Set context mapping for %02x:%02x.%d\n",
		bus, PCI_SLOT(devfn), PCI_FUNC(devfn));
	BUG_ON(!domain->pgd);

	iommu = device_to_iommu(segment, bus, devfn);
	if (!iommu)
		return -ENODEV;

	context = device_to_context_entry(iommu, bus, devfn);
	if (!context)
		return -ENOMEM;
	spin_lock_irqsave(&iommu->lock, flags);
	if (context_present(context)) {
		spin_unlock_irqrestore(&iommu->lock, flags);
		return 0;
	}

	id = domain->id;
	pgd = domain->pgd;

	if (domain->flags & DOMAIN_FLAG_VIRTUAL_MACHINE) {
		int found = 0;

		/* find an available domain id for this device in iommu */
		ndomains = cap_ndoms(iommu->cap);
		num = find_first_bit(iommu->domain_ids, ndomains);
		for (; num < ndomains; ) {
			if (iommu->domains[num] == domain) {
				id = num;
				found = 1;
				break;
			}
			num = find_next_bit(iommu->domain_ids,
					    cap_ndoms(iommu->cap), num+1);
		}

		if (found == 0) {
			num = find_first_zero_bit(iommu->domain_ids, ndomains);
			if (num >= ndomains) {
				spin_unlock_irqrestore(&iommu->lock, flags);
				printk(KERN_ERR "IOMMU: no free domain ids\n");
				return -EFAULT;
			}

			set_bit(num, iommu->domain_ids);
			iommu->domains[num] = domain;
			id = num;
		}

		/* Skip top levels of page tables for
		 * iommu which has less agaw than default.
		 */
		for (agaw = domain->agaw; agaw != iommu->agaw; agaw--) {
			pgd = phys_to_virt(dma_pte_addr(pgd));
			if (!dma_pte_present(pgd)) {
				spin_unlock_irqrestore(&iommu->lock, flags);
				return -ENOMEM;
			}
		}
	}

	context_set_domain_id(context, id);
	context_set_address_width(context, iommu->agaw);
	context_set_address_root(context, virt_to_phys(pgd));
	context_set_translation_type(context, CONTEXT_TT_MULTI_LEVEL);
	context_set_fault_enable(context);
	context_set_present(context);
	domain_flush_cache(domain, context, sizeof(*context));

	/* it's a non-present to present mapping */
	if (iommu->flush.flush_context(iommu, domain->id,
		(((u16)bus) << 8) | devfn, DMA_CCMD_MASK_NOBIT,
		DMA_CCMD_DEVICE_INVL, 1))
		iommu_flush_write_buffer(iommu);
	else
		iommu->flush.flush_iotlb(iommu, 0, 0, 0, DMA_TLB_DSI_FLUSH, 0);

	spin_unlock_irqrestore(&iommu->lock, flags);

	spin_lock_irqsave(&domain->iommu_lock, flags);
	if (!test_and_set_bit(iommu->seq_id, &domain->iommu_bmp)) {
		domain->iommu_count++;
		domain_update_iommu_cap(domain);
	}
	spin_unlock_irqrestore(&domain->iommu_lock, flags);
	return 0;
}

static int
domain_context_mapping(struct dmar_domain *domain, struct pci_dev *pdev)
{
	int ret;
	struct pci_dev *tmp, *parent;

	ret = domain_context_mapping_one(domain, pci_domain_nr(pdev->bus),
					 pdev->bus->number, pdev->devfn);
	if (ret)
		return ret;

	/* dependent device mapping */
	tmp = pci_find_upstream_pcie_bridge(pdev);
	if (!tmp)
		return 0;
	/* Secondary interface's bus number and devfn 0 */
	parent = pdev->bus->self;
	while (parent != tmp) {
		ret = domain_context_mapping_one(domain,
						 pci_domain_nr(parent->bus),
						 parent->bus->number,
						 parent->devfn);
		if (ret)
			return ret;
		parent = parent->bus->self;
	}
	if (tmp->is_pcie) /* this is a PCIE-to-PCI bridge */
		return domain_context_mapping_one(domain,
					pci_domain_nr(tmp->subordinate),
					tmp->subordinate->number, 0);
	else /* this is a legacy PCI bridge */
		return domain_context_mapping_one(domain,
						  pci_domain_nr(tmp->bus),
						  tmp->bus->number,
						  tmp->devfn);
}

static int domain_context_mapped(struct pci_dev *pdev)
{
	int ret;
	struct pci_dev *tmp, *parent;
	struct intel_iommu *iommu;

	iommu = device_to_iommu(pci_domain_nr(pdev->bus), pdev->bus->number,
				pdev->devfn);
	if (!iommu)
		return -ENODEV;

	ret = device_context_mapped(iommu, pdev->bus->number, pdev->devfn);
	if (!ret)
		return ret;
	/* dependent device mapping */
	tmp = pci_find_upstream_pcie_bridge(pdev);
	if (!tmp)
		return ret;
	/* Secondary interface's bus number and devfn 0 */
	parent = pdev->bus->self;
	while (parent != tmp) {
		ret = device_context_mapped(iommu, parent->bus->number,
					    parent->devfn);
		if (!ret)
			return ret;
		parent = parent->bus->self;
	}
	if (tmp->is_pcie)
		return device_context_mapped(iommu, tmp->subordinate->number,
					     0);
	else
		return device_context_mapped(iommu, tmp->bus->number,
					     tmp->devfn);
}

static int
domain_page_mapping(struct dmar_domain *domain, dma_addr_t iova,
			u64 hpa, size_t size, int prot)
{
	u64 start_pfn, end_pfn;
	struct dma_pte *pte;
	int index;
	int addr_width = agaw_to_width(domain->agaw);

	hpa &= (((u64)1) << addr_width) - 1;

	if ((prot & (DMA_PTE_READ|DMA_PTE_WRITE)) == 0)
		return -EINVAL;
	iova &= PAGE_MASK;
	start_pfn = ((u64)hpa) >> VTD_PAGE_SHIFT;
	end_pfn = (VTD_PAGE_ALIGN(((u64)hpa) + size)) >> VTD_PAGE_SHIFT;
	index = 0;
	while (start_pfn < end_pfn) {
		pte = addr_to_dma_pte(domain, iova + VTD_PAGE_SIZE * index);
		if (!pte)
			return -ENOMEM;
		/* We don't need lock here, nobody else
		 * touches the iova range
		 */
		BUG_ON(dma_pte_addr(pte));
		dma_set_pte_addr(pte, start_pfn << VTD_PAGE_SHIFT);
		dma_set_pte_prot(pte, prot);
		if (prot & DMA_PTE_SNP)
			dma_set_pte_snp(pte);
		domain_flush_cache(domain, pte, sizeof(*pte));
		start_pfn++;
		index++;
	}
	return 0;
}

static void iommu_detach_dev(struct intel_iommu *iommu, u8 bus, u8 devfn)
{
	if (!iommu)
		return;

	clear_context_table(iommu, bus, devfn);
	iommu->flush.flush_context(iommu, 0, 0, 0,
					   DMA_CCMD_GLOBAL_INVL, 0);
	iommu->flush.flush_iotlb(iommu, 0, 0, 0,
					 DMA_TLB_GLOBAL_FLUSH, 0);
}

static void domain_remove_dev_info(struct dmar_domain *domain)
{
	struct device_domain_info *info;
	unsigned long flags;
	struct intel_iommu *iommu;

	spin_lock_irqsave(&device_domain_lock, flags);
	while (!list_empty(&domain->devices)) {
		info = list_entry(domain->devices.next,
			struct device_domain_info, link);
		list_del(&info->link);
		list_del(&info->global);
		if (info->dev)
			info->dev->dev.archdata.iommu = NULL;
		spin_unlock_irqrestore(&device_domain_lock, flags);

		iommu = device_to_iommu(info->segment, info->bus, info->devfn);
		iommu_detach_dev(iommu, info->bus, info->devfn);
		free_devinfo_mem(info);

		spin_lock_irqsave(&device_domain_lock, flags);
	}
	spin_unlock_irqrestore(&device_domain_lock, flags);
}

/*
 * find_domain
 * Note: we use struct pci_dev->dev.archdata.iommu stores the info
 */
static struct dmar_domain *
find_domain(struct pci_dev *pdev)
{
	struct device_domain_info *info;

	/* No lock here, assumes no domain exit in normal case */
	info = pdev->dev.archdata.iommu;
	if (info)
		return info->domain;
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
	int segment;

	domain = find_domain(pdev);
	if (domain)
		return domain;

	segment = pci_domain_nr(pdev->bus);

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
			if (info->segment == segment &&
			    info->bus == bus && info->devfn == devfn) {
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
		info->segment = segment;
		info->bus = bus;
		info->devfn = devfn;
		info->dev = NULL;
		info->domain = domain;
		/* This domain is shared by devices under p2p bridge */
		domain->flags |= DOMAIN_FLAG_P2P_MULTIPLE_DEVICES;

		/* pcie-to-pci bridge already has a domain, uses it */
		found = NULL;
		spin_lock_irqsave(&device_domain_lock, flags);
		list_for_each_entry(tmp, &device_domain_list, global) {
			if (tmp->segment == segment &&
			    tmp->bus == bus && tmp->devfn == devfn) {
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
	info->segment = segment;
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

static int iommu_prepare_identity_map(struct pci_dev *pdev,
				      unsigned long long start,
				      unsigned long long end)
{
	struct dmar_domain *domain;
	unsigned long size;
	unsigned long long base;
	int ret;

	printk(KERN_INFO
		"IOMMU: Setting identity map for device %s [0x%Lx - 0x%Lx]\n",
		pci_name(pdev), start, end);
	/* page table init */
	domain = get_domain_for_dev(pdev, DEFAULT_DOMAIN_ADDRESS_WIDTH);
	if (!domain)
		return -ENOMEM;

	/* The address might not be aligned */
	base = start & PAGE_MASK;
	size = end - base;
	size = PAGE_ALIGN(size);
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
struct iommu_prepare_data {
	struct pci_dev *pdev;
	int ret;
};

static int __init iommu_prepare_work_fn(unsigned long start_pfn,
					 unsigned long end_pfn, void *datax)
{
	struct iommu_prepare_data *data;

	data = (struct iommu_prepare_data *)datax;

	data->ret = iommu_prepare_identity_map(data->pdev,
				start_pfn<<PAGE_SHIFT, end_pfn<<PAGE_SHIFT);
	return data->ret;

}

static int __init iommu_prepare_with_active_regions(struct pci_dev *pdev)
{
	int nid;
	struct iommu_prepare_data data;

	data.pdev = pdev;
	data.ret = 0;

	for_each_online_node(nid) {
		work_with_active_regions(nid, iommu_prepare_work_fn, &data);
		if (data.ret)
			return data.ret;
	}
	return data.ret;
}

static void __init iommu_prepare_gfx_mapping(void)
{
	struct pci_dev *pdev = NULL;
	int ret;

	for_each_pci_dev(pdev) {
		if (pdev->dev.archdata.iommu == DUMMY_DEVICE_DOMAIN_INFO ||
				!IS_GFX_DEVICE(pdev))
			continue;
		printk(KERN_INFO "IOMMU: gfx device %s 1-1 mapping\n",
			pci_name(pdev));
		ret = iommu_prepare_with_active_regions(pdev);
		if (ret)
			printk(KERN_ERR "IOMMU: mapping reserved region failed\n");
	}
}
#else /* !CONFIG_DMAR_GFX_WA */
static inline void iommu_prepare_gfx_mapping(void)
{
	return;
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
		printk(KERN_ERR "IOMMU: Failed to create 0-64M identity map, "
			"floppy might not work\n");

}
#else
static inline void iommu_prepare_isa(void)
{
	return;
}
#endif /* !CONFIG_DMAR_FLPY_WA */

static int __init init_dmars(void)
{
	struct dmar_drhd_unit *drhd;
	struct dmar_rmrr_unit *rmrr;
	struct pci_dev *pdev;
	struct intel_iommu *iommu;
	int i, ret;

	/*
	 * for each drhd
	 *    allocate root
	 *    initialize and program root entry to not present
	 * endfor
	 */
	for_each_drhd_unit(drhd) {
		g_num_of_iommus++;
		/*
		 * lock not needed as this is only incremented in the single
		 * threaded kernel __init code path all other access are read
		 * only
		 */
	}

	g_iommus = kcalloc(g_num_of_iommus, sizeof(struct intel_iommu *),
			GFP_KERNEL);
	if (!g_iommus) {
		printk(KERN_ERR "Allocating global iommu array failed\n");
		ret = -ENOMEM;
		goto error;
	}

	deferred_flush = kzalloc(g_num_of_iommus *
		sizeof(struct deferred_flush_tables), GFP_KERNEL);
	if (!deferred_flush) {
		kfree(g_iommus);
		ret = -ENOMEM;
		goto error;
	}

	for_each_drhd_unit(drhd) {
		if (drhd->ignored)
			continue;

		iommu = drhd->iommu;
		g_iommus[iommu->seq_id] = iommu;

		ret = iommu_init_domains(iommu);
		if (ret)
			goto error;

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
	 * Start from the sane iommu hardware state.
	 */
	for_each_drhd_unit(drhd) {
		if (drhd->ignored)
			continue;

		iommu = drhd->iommu;

		/*
		 * If the queued invalidation is already initialized by us
		 * (for example, while enabling interrupt-remapping) then
		 * we got the things already rolling from a sane state.
		 */
		if (iommu->qi)
			continue;

		/*
		 * Clear any previous faults.
		 */
		dmar_fault(-1, iommu);
		/*
		 * Disable queued invalidation if supported and already enabled
		 * before OS handover.
		 */
		dmar_disable_qi(iommu);
	}

	for_each_drhd_unit(drhd) {
		if (drhd->ignored)
			continue;

		iommu = drhd->iommu;

		if (dmar_enable_qi(iommu)) {
			/*
			 * Queued Invalidate not enabled, use Register Based
			 * Invalidate
			 */
			iommu->flush.flush_context = __iommu_flush_context;
			iommu->flush.flush_iotlb = __iommu_flush_iotlb;
			printk(KERN_INFO "IOMMU 0x%Lx: using Register based "
			       "invalidation\n",
			       (unsigned long long)drhd->reg_base_addr);
		} else {
			iommu->flush.flush_context = qi_flush_context;
			iommu->flush.flush_iotlb = qi_flush_iotlb;
			printk(KERN_INFO "IOMMU 0x%Lx: using Queued "
			       "invalidation\n",
			       (unsigned long long)drhd->reg_base_addr);
		}
	}

#ifdef CONFIG_INTR_REMAP
	if (!intr_remapping_enabled) {
		ret = enable_intr_remapping(0);
		if (ret)
			printk(KERN_ERR
			       "IOMMU: enable interrupt remapping failed\n");
	}
#endif

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

		iommu_flush_write_buffer(iommu);

		ret = dmar_set_interrupt(iommu);
		if (ret)
			goto error;

		iommu_set_root_entry(iommu);

		iommu->flush.flush_context(iommu, 0, 0, 0, DMA_CCMD_GLOBAL_INVL,
					   0);
		iommu->flush.flush_iotlb(iommu, 0, 0, 0, DMA_TLB_GLOBAL_FLUSH,
					 0);
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
	kfree(g_iommus);
	return ret;
}

static inline u64 aligned_size(u64 host_addr, size_t size)
{
	u64 addr;
	addr = (host_addr & (~PAGE_MASK)) + size;
	return PAGE_ALIGN(addr);
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
			size >> PAGE_SHIFT, IOVA_PFN(end), 1);
	return piova;
}

static struct iova *
__intel_alloc_iova(struct device *dev, struct dmar_domain *domain,
		   size_t size, u64 dma_mask)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct iova *iova = NULL;

	if (dma_mask <= DMA_32BIT_MASK || dmar_forcedac)
		iova = iommu_alloc_iova(domain, size, dma_mask);
	else {
		/*
		 * First try to allocate an io virtual address in
		 * DMA_32BIT_MASK and if that fails then try allocating
		 * from higher range
		 */
		iova = iommu_alloc_iova(domain, size, DMA_32BIT_MASK);
		if (!iova)
			iova = iommu_alloc_iova(domain, size, dma_mask);
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
	if (unlikely(!domain_context_mapped(pdev))) {
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

static dma_addr_t __intel_map_single(struct device *hwdev, phys_addr_t paddr,
				     size_t size, int dir, u64 dma_mask)
{
	struct pci_dev *pdev = to_pci_dev(hwdev);
	struct dmar_domain *domain;
	phys_addr_t start_paddr;
	struct iova *iova;
	int prot = 0;
	int ret;
	struct intel_iommu *iommu;

	BUG_ON(dir == DMA_NONE);
	if (pdev->dev.archdata.iommu == DUMMY_DEVICE_DOMAIN_INFO)
		return paddr;

	domain = get_valid_domain_for_dev(pdev);
	if (!domain)
		return 0;

	iommu = domain_get_iommu(domain);
	size = aligned_size((u64)paddr, size);

	iova = __intel_alloc_iova(hwdev, domain, size, pdev->dma_mask);
	if (!iova)
		goto error;

	start_paddr = (phys_addr_t)iova->pfn_lo << PAGE_SHIFT;

	/*
	 * Check if DMAR supports zero-length reads on write only
	 * mappings..
	 */
	if (dir == DMA_TO_DEVICE || dir == DMA_BIDIRECTIONAL || \
			!cap_zlr(iommu->cap))
		prot |= DMA_PTE_READ;
	if (dir == DMA_FROM_DEVICE || dir == DMA_BIDIRECTIONAL)
		prot |= DMA_PTE_WRITE;
	/*
	 * paddr - (paddr + size) might be partial page, we should map the whole
	 * page.  Note: if two part of one page are separately mapped, we
	 * might have two guest_addr mapping to the same host paddr, but this
	 * is not a big problem
	 */
	ret = domain_page_mapping(domain, start_paddr,
		((u64)paddr) & PAGE_MASK, size, prot);
	if (ret)
		goto error;

	/* it's a non-present to present mapping */
	ret = iommu_flush_iotlb_psi(iommu, domain->id,
			start_paddr, size >> VTD_PAGE_SHIFT, 1);
	if (ret)
		iommu_flush_write_buffer(iommu);

	return start_paddr + ((u64)paddr & (~PAGE_MASK));

error:
	if (iova)
		__free_iova(&domain->iovad, iova);
	printk(KERN_ERR"Device %s request: %zx@%llx dir %d --- failed\n",
		pci_name(pdev), size, (unsigned long long)paddr, dir);
	return 0;
}

static dma_addr_t intel_map_page(struct device *dev, struct page *page,
				 unsigned long offset, size_t size,
				 enum dma_data_direction dir,
				 struct dma_attrs *attrs)
{
	return __intel_map_single(dev, page_to_phys(page) + offset, size,
				  dir, to_pci_dev(dev)->dma_mask);
}

static void flush_unmaps(void)
{
	int i, j;

	timer_on = 0;

	/* just flush them all */
	for (i = 0; i < g_num_of_iommus; i++) {
		struct intel_iommu *iommu = g_iommus[i];
		if (!iommu)
			continue;

		if (deferred_flush[i].next) {
			iommu->flush.flush_iotlb(iommu, 0, 0, 0,
						 DMA_TLB_GLOBAL_FLUSH, 0);
			for (j = 0; j < deferred_flush[i].next; j++) {
				__free_iova(&deferred_flush[i].domain[j]->iovad,
						deferred_flush[i].iova[j]);
			}
			deferred_flush[i].next = 0;
		}
	}

	list_size = 0;
}

static void flush_unmaps_timeout(unsigned long data)
{
	unsigned long flags;

	spin_lock_irqsave(&async_umap_flush_lock, flags);
	flush_unmaps();
	spin_unlock_irqrestore(&async_umap_flush_lock, flags);
}

static void add_unmap(struct dmar_domain *dom, struct iova *iova)
{
	unsigned long flags;
	int next, iommu_id;
	struct intel_iommu *iommu;

	spin_lock_irqsave(&async_umap_flush_lock, flags);
	if (list_size == HIGH_WATER_MARK)
		flush_unmaps();

	iommu = domain_get_iommu(dom);
	iommu_id = iommu->seq_id;

	next = deferred_flush[iommu_id].next;
	deferred_flush[iommu_id].domain[next] = dom;
	deferred_flush[iommu_id].iova[next] = iova;
	deferred_flush[iommu_id].next++;

	if (!timer_on) {
		mod_timer(&unmap_timer, jiffies + msecs_to_jiffies(10));
		timer_on = 1;
	}
	list_size++;
	spin_unlock_irqrestore(&async_umap_flush_lock, flags);
}

static void intel_unmap_page(struct device *dev, dma_addr_t dev_addr,
			     size_t size, enum dma_data_direction dir,
			     struct dma_attrs *attrs)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct dmar_domain *domain;
	unsigned long start_addr;
	struct iova *iova;
	struct intel_iommu *iommu;

	if (pdev->dev.archdata.iommu == DUMMY_DEVICE_DOMAIN_INFO)
		return;
	domain = find_domain(pdev);
	BUG_ON(!domain);

	iommu = domain_get_iommu(domain);

	iova = find_iova(&domain->iovad, IOVA_PFN(dev_addr));
	if (!iova)
		return;

	start_addr = iova->pfn_lo << PAGE_SHIFT;
	size = aligned_size((u64)dev_addr, size);

	pr_debug("Device %s unmapping: %zx@%llx\n",
		pci_name(pdev), size, (unsigned long long)start_addr);

	/*  clear the whole page */
	dma_pte_clear_range(domain, start_addr, start_addr + size);
	/* free page tables */
	dma_pte_free_pagetable(domain, start_addr, start_addr + size);
	if (intel_iommu_strict) {
		if (iommu_flush_iotlb_psi(iommu,
			domain->id, start_addr, size >> VTD_PAGE_SHIFT, 0))
			iommu_flush_write_buffer(iommu);
		/* free iova */
		__free_iova(&domain->iovad, iova);
	} else {
		add_unmap(domain, iova);
		/*
		 * queue up the release of the unmap to save the 1/6th of the
		 * cpu used up by the iotlb flush operation...
		 */
	}
}

static void intel_unmap_single(struct device *dev, dma_addr_t dev_addr, size_t size,
			       int dir)
{
	intel_unmap_page(dev, dev_addr, size, dir, NULL);
}

static void *intel_alloc_coherent(struct device *hwdev, size_t size,
				  dma_addr_t *dma_handle, gfp_t flags)
{
	void *vaddr;
	int order;

	size = PAGE_ALIGN(size);
	order = get_order(size);
	flags &= ~(GFP_DMA | GFP_DMA32);

	vaddr = (void *)__get_free_pages(flags, order);
	if (!vaddr)
		return NULL;
	memset(vaddr, 0, size);

	*dma_handle = __intel_map_single(hwdev, virt_to_bus(vaddr), size,
					 DMA_BIDIRECTIONAL,
					 hwdev->coherent_dma_mask);
	if (*dma_handle)
		return vaddr;
	free_pages((unsigned long)vaddr, order);
	return NULL;
}

static void intel_free_coherent(struct device *hwdev, size_t size, void *vaddr,
				dma_addr_t dma_handle)
{
	int order;

	size = PAGE_ALIGN(size);
	order = get_order(size);

	intel_unmap_single(hwdev, dma_handle, size, DMA_BIDIRECTIONAL);
	free_pages((unsigned long)vaddr, order);
}

static void intel_unmap_sg(struct device *hwdev, struct scatterlist *sglist,
			   int nelems, enum dma_data_direction dir,
			   struct dma_attrs *attrs)
{
	int i;
	struct pci_dev *pdev = to_pci_dev(hwdev);
	struct dmar_domain *domain;
	unsigned long start_addr;
	struct iova *iova;
	size_t size = 0;
	phys_addr_t addr;
	struct scatterlist *sg;
	struct intel_iommu *iommu;

	if (pdev->dev.archdata.iommu == DUMMY_DEVICE_DOMAIN_INFO)
		return;

	domain = find_domain(pdev);
	BUG_ON(!domain);

	iommu = domain_get_iommu(domain);

	iova = find_iova(&domain->iovad, IOVA_PFN(sglist[0].dma_address));
	if (!iova)
		return;
	for_each_sg(sglist, sg, nelems, i) {
		addr = page_to_phys(sg_page(sg)) + sg->offset;
		size += aligned_size((u64)addr, sg->length);
	}

	start_addr = iova->pfn_lo << PAGE_SHIFT;

	/*  clear the whole page */
	dma_pte_clear_range(domain, start_addr, start_addr + size);
	/* free page tables */
	dma_pte_free_pagetable(domain, start_addr, start_addr + size);

	if (iommu_flush_iotlb_psi(iommu, domain->id, start_addr,
			size >> VTD_PAGE_SHIFT, 0))
		iommu_flush_write_buffer(iommu);

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
		sg->dma_address = page_to_phys(sg_page(sg)) + sg->offset;
		sg->dma_length = sg->length;
	}
	return nelems;
}

static int intel_map_sg(struct device *hwdev, struct scatterlist *sglist, int nelems,
			enum dma_data_direction dir, struct dma_attrs *attrs)
{
	phys_addr_t addr;
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
	struct intel_iommu *iommu;

	BUG_ON(dir == DMA_NONE);
	if (pdev->dev.archdata.iommu == DUMMY_DEVICE_DOMAIN_INFO)
		return intel_nontranslate_map_sg(hwdev, sglist, nelems, dir);

	domain = get_valid_domain_for_dev(pdev);
	if (!domain)
		return 0;

	iommu = domain_get_iommu(domain);

	for_each_sg(sglist, sg, nelems, i) {
		addr = page_to_phys(sg_page(sg)) + sg->offset;
		size += aligned_size((u64)addr, sg->length);
	}

	iova = __intel_alloc_iova(hwdev, domain, size, pdev->dma_mask);
	if (!iova) {
		sglist->dma_length = 0;
		return 0;
	}

	/*
	 * Check if DMAR supports zero-length reads on write only
	 * mappings..
	 */
	if (dir == DMA_TO_DEVICE || dir == DMA_BIDIRECTIONAL || \
			!cap_zlr(iommu->cap))
		prot |= DMA_PTE_READ;
	if (dir == DMA_FROM_DEVICE || dir == DMA_BIDIRECTIONAL)
		prot |= DMA_PTE_WRITE;

	start_addr = iova->pfn_lo << PAGE_SHIFT;
	offset = 0;
	for_each_sg(sglist, sg, nelems, i) {
		addr = page_to_phys(sg_page(sg)) + sg->offset;
		size = aligned_size((u64)addr, sg->length);
		ret = domain_page_mapping(domain, start_addr + offset,
			((u64)addr) & PAGE_MASK,
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
				((u64)addr & (~PAGE_MASK));
		sg->dma_length = sg->length;
		offset += size;
	}

	/* it's a non-present to present mapping */
	if (iommu_flush_iotlb_psi(iommu, domain->id,
			start_addr, offset >> VTD_PAGE_SHIFT, 1))
		iommu_flush_write_buffer(iommu);
	return nelems;
}

static int intel_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return !dma_addr;
}

struct dma_map_ops intel_dma_ops = {
	.alloc_coherent = intel_alloc_coherent,
	.free_coherent = intel_free_coherent,
	.map_sg = intel_map_sg,
	.unmap_sg = intel_unmap_sg,
	.map_page = intel_map_page,
	.unmap_page = intel_unmap_page,
	.mapping_error = intel_mapping_error,
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

#ifdef CONFIG_SUSPEND
static int init_iommu_hw(void)
{
	struct dmar_drhd_unit *drhd;
	struct intel_iommu *iommu = NULL;

	for_each_active_iommu(iommu, drhd)
		if (iommu->qi)
			dmar_reenable_qi(iommu);

	for_each_active_iommu(iommu, drhd) {
		iommu_flush_write_buffer(iommu);

		iommu_set_root_entry(iommu);

		iommu->flush.flush_context(iommu, 0, 0, 0,
						DMA_CCMD_GLOBAL_INVL, 0);
		iommu->flush.flush_iotlb(iommu, 0, 0, 0,
						DMA_TLB_GLOBAL_FLUSH, 0);
		iommu_disable_protect_mem_regions(iommu);
		iommu_enable_translation(iommu);
	}

	return 0;
}

static void iommu_flush_all(void)
{
	struct dmar_drhd_unit *drhd;
	struct intel_iommu *iommu;

	for_each_active_iommu(iommu, drhd) {
		iommu->flush.flush_context(iommu, 0, 0, 0,
						DMA_CCMD_GLOBAL_INVL, 0);
		iommu->flush.flush_iotlb(iommu, 0, 0, 0,
						DMA_TLB_GLOBAL_FLUSH, 0);
	}
}

static int iommu_suspend(struct sys_device *dev, pm_message_t state)
{
	struct dmar_drhd_unit *drhd;
	struct intel_iommu *iommu = NULL;
	unsigned long flag;

	for_each_active_iommu(iommu, drhd) {
		iommu->iommu_state = kzalloc(sizeof(u32) * MAX_SR_DMAR_REGS,
						 GFP_ATOMIC);
		if (!iommu->iommu_state)
			goto nomem;
	}

	iommu_flush_all();

	for_each_active_iommu(iommu, drhd) {
		iommu_disable_translation(iommu);

		spin_lock_irqsave(&iommu->register_lock, flag);

		iommu->iommu_state[SR_DMAR_FECTL_REG] =
			readl(iommu->reg + DMAR_FECTL_REG);
		iommu->iommu_state[SR_DMAR_FEDATA_REG] =
			readl(iommu->reg + DMAR_FEDATA_REG);
		iommu->iommu_state[SR_DMAR_FEADDR_REG] =
			readl(iommu->reg + DMAR_FEADDR_REG);
		iommu->iommu_state[SR_DMAR_FEUADDR_REG] =
			readl(iommu->reg + DMAR_FEUADDR_REG);

		spin_unlock_irqrestore(&iommu->register_lock, flag);
	}
	return 0;

nomem:
	for_each_active_iommu(iommu, drhd)
		kfree(iommu->iommu_state);

	return -ENOMEM;
}

static int iommu_resume(struct sys_device *dev)
{
	struct dmar_drhd_unit *drhd;
	struct intel_iommu *iommu = NULL;
	unsigned long flag;

	if (init_iommu_hw()) {
		WARN(1, "IOMMU setup failed, DMAR can not resume!\n");
		return -EIO;
	}

	for_each_active_iommu(iommu, drhd) {

		spin_lock_irqsave(&iommu->register_lock, flag);

		writel(iommu->iommu_state[SR_DMAR_FECTL_REG],
			iommu->reg + DMAR_FECTL_REG);
		writel(iommu->iommu_state[SR_DMAR_FEDATA_REG],
			iommu->reg + DMAR_FEDATA_REG);
		writel(iommu->iommu_state[SR_DMAR_FEADDR_REG],
			iommu->reg + DMAR_FEADDR_REG);
		writel(iommu->iommu_state[SR_DMAR_FEUADDR_REG],
			iommu->reg + DMAR_FEUADDR_REG);

		spin_unlock_irqrestore(&iommu->register_lock, flag);
	}

	for_each_active_iommu(iommu, drhd)
		kfree(iommu->iommu_state);

	return 0;
}

static struct sysdev_class iommu_sysclass = {
	.name		= "iommu",
	.resume		= iommu_resume,
	.suspend	= iommu_suspend,
};

static struct sys_device device_iommu = {
	.cls	= &iommu_sysclass,
};

static int __init init_iommu_sysfs(void)
{
	int error;

	error = sysdev_class_register(&iommu_sysclass);
	if (error)
		return error;

	error = sysdev_register(&device_iommu);
	if (error)
		sysdev_class_unregister(&iommu_sysclass);

	return error;
}

#else
static int __init init_iommu_sysfs(void)
{
	return 0;
}
#endif	/* CONFIG_PM */

int __init intel_iommu_init(void)
{
	int ret = 0;

	if (dmar_table_init())
		return 	-ENODEV;

	if (dmar_dev_scope_init())
		return 	-ENODEV;

	/*
	 * Check the need for DMA-remapping initialization now.
	 * Above initialization will also be used by Interrupt-remapping.
	 */
	if (no_iommu || swiotlb || dmar_disabled)
		return -ENODEV;

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

	init_timer(&unmap_timer);
	force_iommu = 1;
	dma_ops = &intel_dma_ops;
	init_iommu_sysfs();

	register_iommu(&intel_iommu_ops);

	return 0;
}

static int vm_domain_add_dev_info(struct dmar_domain *domain,
				  struct pci_dev *pdev)
{
	struct device_domain_info *info;
	unsigned long flags;

	info = alloc_devinfo_mem();
	if (!info)
		return -ENOMEM;

	info->segment = pci_domain_nr(pdev->bus);
	info->bus = pdev->bus->number;
	info->devfn = pdev->devfn;
	info->dev = pdev;
	info->domain = domain;

	spin_lock_irqsave(&device_domain_lock, flags);
	list_add(&info->link, &domain->devices);
	list_add(&info->global, &device_domain_list);
	pdev->dev.archdata.iommu = info;
	spin_unlock_irqrestore(&device_domain_lock, flags);

	return 0;
}

static void iommu_detach_dependent_devices(struct intel_iommu *iommu,
					   struct pci_dev *pdev)
{
	struct pci_dev *tmp, *parent;

	if (!iommu || !pdev)
		return;

	/* dependent device detach */
	tmp = pci_find_upstream_pcie_bridge(pdev);
	/* Secondary interface's bus number and devfn 0 */
	if (tmp) {
		parent = pdev->bus->self;
		while (parent != tmp) {
			iommu_detach_dev(iommu, parent->bus->number,
					 parent->devfn);
			parent = parent->bus->self;
		}
		if (tmp->is_pcie) /* this is a PCIE-to-PCI bridge */
			iommu_detach_dev(iommu,
				tmp->subordinate->number, 0);
		else /* this is a legacy PCI bridge */
			iommu_detach_dev(iommu, tmp->bus->number,
					 tmp->devfn);
	}
}

static void vm_domain_remove_one_dev_info(struct dmar_domain *domain,
					  struct pci_dev *pdev)
{
	struct device_domain_info *info;
	struct intel_iommu *iommu;
	unsigned long flags;
	int found = 0;
	struct list_head *entry, *tmp;

	iommu = device_to_iommu(pci_domain_nr(pdev->bus), pdev->bus->number,
				pdev->devfn);
	if (!iommu)
		return;

	spin_lock_irqsave(&device_domain_lock, flags);
	list_for_each_safe(entry, tmp, &domain->devices) {
		info = list_entry(entry, struct device_domain_info, link);
		/* No need to compare PCI domain; it has to be the same */
		if (info->bus == pdev->bus->number &&
		    info->devfn == pdev->devfn) {
			list_del(&info->link);
			list_del(&info->global);
			if (info->dev)
				info->dev->dev.archdata.iommu = NULL;
			spin_unlock_irqrestore(&device_domain_lock, flags);

			iommu_detach_dev(iommu, info->bus, info->devfn);
			iommu_detach_dependent_devices(iommu, pdev);
			free_devinfo_mem(info);

			spin_lock_irqsave(&device_domain_lock, flags);

			if (found)
				break;
			else
				continue;
		}

		/* if there is no other devices under the same iommu
		 * owned by this domain, clear this iommu in iommu_bmp
		 * update iommu count and coherency
		 */
		if (iommu == device_to_iommu(info->segment, info->bus,
					    info->devfn))
			found = 1;
	}

	if (found == 0) {
		unsigned long tmp_flags;
		spin_lock_irqsave(&domain->iommu_lock, tmp_flags);
		clear_bit(iommu->seq_id, &domain->iommu_bmp);
		domain->iommu_count--;
		domain_update_iommu_cap(domain);
		spin_unlock_irqrestore(&domain->iommu_lock, tmp_flags);
	}

	spin_unlock_irqrestore(&device_domain_lock, flags);
}

static void vm_domain_remove_all_dev_info(struct dmar_domain *domain)
{
	struct device_domain_info *info;
	struct intel_iommu *iommu;
	unsigned long flags1, flags2;

	spin_lock_irqsave(&device_domain_lock, flags1);
	while (!list_empty(&domain->devices)) {
		info = list_entry(domain->devices.next,
			struct device_domain_info, link);
		list_del(&info->link);
		list_del(&info->global);
		if (info->dev)
			info->dev->dev.archdata.iommu = NULL;

		spin_unlock_irqrestore(&device_domain_lock, flags1);

		iommu = device_to_iommu(info->segment, info->bus, info->devfn);
		iommu_detach_dev(iommu, info->bus, info->devfn);
		iommu_detach_dependent_devices(iommu, info->dev);

		/* clear this iommu in iommu_bmp, update iommu count
		 * and capabilities
		 */
		spin_lock_irqsave(&domain->iommu_lock, flags2);
		if (test_and_clear_bit(iommu->seq_id,
				       &domain->iommu_bmp)) {
			domain->iommu_count--;
			domain_update_iommu_cap(domain);
		}
		spin_unlock_irqrestore(&domain->iommu_lock, flags2);

		free_devinfo_mem(info);
		spin_lock_irqsave(&device_domain_lock, flags1);
	}
	spin_unlock_irqrestore(&device_domain_lock, flags1);
}

/* domain id for virtual machine, it won't be set in context */
static unsigned long vm_domid;

static int vm_domain_min_agaw(struct dmar_domain *domain)
{
	int i;
	int min_agaw = domain->agaw;

	i = find_first_bit(&domain->iommu_bmp, g_num_of_iommus);
	for (; i < g_num_of_iommus; ) {
		if (min_agaw > g_iommus[i]->agaw)
			min_agaw = g_iommus[i]->agaw;

		i = find_next_bit(&domain->iommu_bmp, g_num_of_iommus, i+1);
	}

	return min_agaw;
}

static struct dmar_domain *iommu_alloc_vm_domain(void)
{
	struct dmar_domain *domain;

	domain = alloc_domain_mem();
	if (!domain)
		return NULL;

	domain->id = vm_domid++;
	memset(&domain->iommu_bmp, 0, sizeof(unsigned long));
	domain->flags = DOMAIN_FLAG_VIRTUAL_MACHINE;

	return domain;
}

static int vm_domain_init(struct dmar_domain *domain, int guest_width)
{
	int adjust_width;

	init_iova_domain(&domain->iovad, DMA_32BIT_PFN);
	spin_lock_init(&domain->mapping_lock);
	spin_lock_init(&domain->iommu_lock);

	domain_reserve_special_ranges(domain);

	/* calculate AGAW */
	domain->gaw = guest_width;
	adjust_width = guestwidth_to_adjustwidth(guest_width);
	domain->agaw = width_to_agaw(adjust_width);

	INIT_LIST_HEAD(&domain->devices);

	domain->iommu_count = 0;
	domain->iommu_coherency = 0;
	domain->max_addr = 0;

	/* always allocate the top pgd */
	domain->pgd = (struct dma_pte *)alloc_pgtable_page();
	if (!domain->pgd)
		return -ENOMEM;
	domain_flush_cache(domain, domain->pgd, PAGE_SIZE);
	return 0;
}

static void iommu_free_vm_domain(struct dmar_domain *domain)
{
	unsigned long flags;
	struct dmar_drhd_unit *drhd;
	struct intel_iommu *iommu;
	unsigned long i;
	unsigned long ndomains;

	for_each_drhd_unit(drhd) {
		if (drhd->ignored)
			continue;
		iommu = drhd->iommu;

		ndomains = cap_ndoms(iommu->cap);
		i = find_first_bit(iommu->domain_ids, ndomains);
		for (; i < ndomains; ) {
			if (iommu->domains[i] == domain) {
				spin_lock_irqsave(&iommu->lock, flags);
				clear_bit(i, iommu->domain_ids);
				iommu->domains[i] = NULL;
				spin_unlock_irqrestore(&iommu->lock, flags);
				break;
			}
			i = find_next_bit(iommu->domain_ids, ndomains, i+1);
		}
	}
}

static void vm_domain_exit(struct dmar_domain *domain)
{
	u64 end;

	/* Domain 0 is reserved, so dont process it */
	if (!domain)
		return;

	vm_domain_remove_all_dev_info(domain);
	/* destroy iovas */
	put_iova_domain(&domain->iovad);
	end = DOMAIN_MAX_ADDR(domain->gaw);
	end = end & (~VTD_PAGE_MASK);

	/* clear ptes */
	dma_pte_clear_range(domain, 0, end);

	/* free page tables */
	dma_pte_free_pagetable(domain, 0, end);

	iommu_free_vm_domain(domain);
	free_domain_mem(domain);
}

static int intel_iommu_domain_init(struct iommu_domain *domain)
{
	struct dmar_domain *dmar_domain;

	dmar_domain = iommu_alloc_vm_domain();
	if (!dmar_domain) {
		printk(KERN_ERR
			"intel_iommu_domain_init: dmar_domain == NULL\n");
		return -ENOMEM;
	}
	if (vm_domain_init(dmar_domain, DEFAULT_DOMAIN_ADDRESS_WIDTH)) {
		printk(KERN_ERR
			"intel_iommu_domain_init() failed\n");
		vm_domain_exit(dmar_domain);
		return -ENOMEM;
	}
	domain->priv = dmar_domain;

	return 0;
}

static void intel_iommu_domain_destroy(struct iommu_domain *domain)
{
	struct dmar_domain *dmar_domain = domain->priv;

	domain->priv = NULL;
	vm_domain_exit(dmar_domain);
}

static int intel_iommu_attach_device(struct iommu_domain *domain,
				     struct device *dev)
{
	struct dmar_domain *dmar_domain = domain->priv;
	struct pci_dev *pdev = to_pci_dev(dev);
	struct intel_iommu *iommu;
	int addr_width;
	u64 end;
	int ret;

	/* normally pdev is not mapped */
	if (unlikely(domain_context_mapped(pdev))) {
		struct dmar_domain *old_domain;

		old_domain = find_domain(pdev);
		if (old_domain) {
			if (dmar_domain->flags & DOMAIN_FLAG_VIRTUAL_MACHINE)
				vm_domain_remove_one_dev_info(old_domain, pdev);
			else
				domain_remove_dev_info(old_domain);
		}
	}

	iommu = device_to_iommu(pci_domain_nr(pdev->bus), pdev->bus->number,
				pdev->devfn);
	if (!iommu)
		return -ENODEV;

	/* check if this iommu agaw is sufficient for max mapped address */
	addr_width = agaw_to_width(iommu->agaw);
	end = DOMAIN_MAX_ADDR(addr_width);
	end = end & VTD_PAGE_MASK;
	if (end < dmar_domain->max_addr) {
		printk(KERN_ERR "%s: iommu agaw (%d) is not "
		       "sufficient for the mapped address (%llx)\n",
		       __func__, iommu->agaw, dmar_domain->max_addr);
		return -EFAULT;
	}

	ret = domain_context_mapping(dmar_domain, pdev);
	if (ret)
		return ret;

	ret = vm_domain_add_dev_info(dmar_domain, pdev);
	return ret;
}

static void intel_iommu_detach_device(struct iommu_domain *domain,
				      struct device *dev)
{
	struct dmar_domain *dmar_domain = domain->priv;
	struct pci_dev *pdev = to_pci_dev(dev);

	vm_domain_remove_one_dev_info(dmar_domain, pdev);
}

static int intel_iommu_map_range(struct iommu_domain *domain,
				 unsigned long iova, phys_addr_t hpa,
				 size_t size, int iommu_prot)
{
	struct dmar_domain *dmar_domain = domain->priv;
	u64 max_addr;
	int addr_width;
	int prot = 0;
	int ret;

	if (iommu_prot & IOMMU_READ)
		prot |= DMA_PTE_READ;
	if (iommu_prot & IOMMU_WRITE)
		prot |= DMA_PTE_WRITE;
	if ((iommu_prot & IOMMU_CACHE) && dmar_domain->iommu_snooping)
		prot |= DMA_PTE_SNP;

	max_addr = (iova & VTD_PAGE_MASK) + VTD_PAGE_ALIGN(size);
	if (dmar_domain->max_addr < max_addr) {
		int min_agaw;
		u64 end;

		/* check if minimum agaw is sufficient for mapped address */
		min_agaw = vm_domain_min_agaw(dmar_domain);
		addr_width = agaw_to_width(min_agaw);
		end = DOMAIN_MAX_ADDR(addr_width);
		end = end & VTD_PAGE_MASK;
		if (end < max_addr) {
			printk(KERN_ERR "%s: iommu agaw (%d) is not "
			       "sufficient for the mapped address (%llx)\n",
			       __func__, min_agaw, max_addr);
			return -EFAULT;
		}
		dmar_domain->max_addr = max_addr;
	}

	ret = domain_page_mapping(dmar_domain, iova, hpa, size, prot);
	return ret;
}

static void intel_iommu_unmap_range(struct iommu_domain *domain,
				    unsigned long iova, size_t size)
{
	struct dmar_domain *dmar_domain = domain->priv;
	dma_addr_t base;

	/* The address might not be aligned */
	base = iova & VTD_PAGE_MASK;
	size = VTD_PAGE_ALIGN(size);
	dma_pte_clear_range(dmar_domain, base, base + size);

	if (dmar_domain->max_addr == base + size)
		dmar_domain->max_addr = base;
}

static phys_addr_t intel_iommu_iova_to_phys(struct iommu_domain *domain,
					    unsigned long iova)
{
	struct dmar_domain *dmar_domain = domain->priv;
	struct dma_pte *pte;
	u64 phys = 0;

	pte = addr_to_dma_pte(dmar_domain, iova);
	if (pte)
		phys = dma_pte_addr(pte);

	return phys;
}

static int intel_iommu_domain_has_cap(struct iommu_domain *domain,
				      unsigned long cap)
{
	struct dmar_domain *dmar_domain = domain->priv;

	if (cap == IOMMU_CAP_CACHE_COHERENCY)
		return dmar_domain->iommu_snooping;

	return 0;
}

static struct iommu_ops intel_iommu_ops = {
	.domain_init	= intel_iommu_domain_init,
	.domain_destroy = intel_iommu_domain_destroy,
	.attach_dev	= intel_iommu_attach_device,
	.detach_dev	= intel_iommu_detach_device,
	.map		= intel_iommu_map_range,
	.unmap		= intel_iommu_unmap_range,
	.iova_to_phys	= intel_iommu_iova_to_phys,
	.domain_has_cap = intel_iommu_domain_has_cap,
};

static void __devinit quirk_iommu_rwbf(struct pci_dev *dev)
{
	/*
	 * Mobile 4 Series Chipset neglects to set RWBF capability,
	 * but needs it:
	 */
	printk(KERN_INFO "DMAR: Forcing write-buffer flush capability\n");
	rwbf_quirk = 1;
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x2a40, quirk_iommu_rwbf);
