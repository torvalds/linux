// SPDX-License-Identifier: GPL-2.0-only
/*
 * A fairly generic DMA-API to IOMMU-API glue layer.
 *
 * Copyright (C) 2014-2015 ARM Ltd.
 *
 * based in part on arch/arm/mm/dma-mapping.c:
 * Copyright (C) 2000-2004 Russell King
 */

#include <linux/acpi_iort.h>
#include <linux/atomic.h>
#include <linux/crash_dump.h>
#include <linux/device.h>
#include <linux/dma-direct.h>
#include <linux/dma-map-ops.h>
#include <linux/gfp.h>
#include <linux/huge_mm.h>
#include <linux/iommu.h>
#include <linux/iova.h>
#include <linux/irq.h>
#include <linux/list_sort.h>
#include <linux/memremap.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/of_iommu.h>
#include <linux/pci.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>
#include <linux/swiotlb.h>
#include <linux/vmalloc.h>
#include <trace/events/swiotlb.h>

#include "dma-iommu.h"
#include "iommu-pages.h"

struct iommu_dma_msi_page {
	struct list_head	list;
	dma_addr_t		iova;
	phys_addr_t		phys;
};

enum iommu_dma_cookie_type {
	IOMMU_DMA_IOVA_COOKIE,
	IOMMU_DMA_MSI_COOKIE,
};

enum iommu_dma_queue_type {
	IOMMU_DMA_OPTS_PER_CPU_QUEUE,
	IOMMU_DMA_OPTS_SINGLE_QUEUE,
};

struct iommu_dma_options {
	enum iommu_dma_queue_type qt;
	size_t		fq_size;
	unsigned int	fq_timeout;
};

struct iommu_dma_cookie {
	enum iommu_dma_cookie_type	type;
	union {
		/* Full allocator for IOMMU_DMA_IOVA_COOKIE */
		struct {
			struct iova_domain	iovad;
			/* Flush queue */
			union {
				struct iova_fq	*single_fq;
				struct iova_fq	__percpu *percpu_fq;
			};
			/* Number of TLB flushes that have been started */
			atomic64_t		fq_flush_start_cnt;
			/* Number of TLB flushes that have been finished */
			atomic64_t		fq_flush_finish_cnt;
			/* Timer to regularily empty the flush queues */
			struct timer_list	fq_timer;
			/* 1 when timer is active, 0 when not */
			atomic_t		fq_timer_on;
		};
		/* Trivial linear page allocator for IOMMU_DMA_MSI_COOKIE */
		dma_addr_t		msi_iova;
	};
	struct list_head		msi_page_list;

	/* Domain for flush queue callback; NULL if flush queue not in use */
	struct iommu_domain		*fq_domain;
	/* Options for dma-iommu use */
	struct iommu_dma_options	options;
	struct mutex			mutex;
};

static DEFINE_STATIC_KEY_FALSE(iommu_deferred_attach_enabled);
bool iommu_dma_forcedac __read_mostly;

static int __init iommu_dma_forcedac_setup(char *str)
{
	int ret = kstrtobool(str, &iommu_dma_forcedac);

	if (!ret && iommu_dma_forcedac)
		pr_info("Forcing DAC for PCI devices\n");
	return ret;
}
early_param("iommu.forcedac", iommu_dma_forcedac_setup);

/* Number of entries per flush queue */
#define IOVA_DEFAULT_FQ_SIZE	256
#define IOVA_SINGLE_FQ_SIZE	32768

/* Timeout (in ms) after which entries are flushed from the queue */
#define IOVA_DEFAULT_FQ_TIMEOUT	10
#define IOVA_SINGLE_FQ_TIMEOUT	1000

/* Flush queue entry for deferred flushing */
struct iova_fq_entry {
	unsigned long iova_pfn;
	unsigned long pages;
	struct list_head freelist;
	u64 counter; /* Flush counter when this entry was added */
};

/* Per-CPU flush queue structure */
struct iova_fq {
	spinlock_t lock;
	unsigned int head, tail;
	unsigned int mod_mask;
	struct iova_fq_entry entries[];
};

#define fq_ring_for_each(i, fq) \
	for ((i) = (fq)->head; (i) != (fq)->tail; (i) = ((i) + 1) & (fq)->mod_mask)

static inline bool fq_full(struct iova_fq *fq)
{
	assert_spin_locked(&fq->lock);
	return (((fq->tail + 1) & fq->mod_mask) == fq->head);
}

static inline unsigned int fq_ring_add(struct iova_fq *fq)
{
	unsigned int idx = fq->tail;

	assert_spin_locked(&fq->lock);

	fq->tail = (idx + 1) & fq->mod_mask;

	return idx;
}

static void fq_ring_free_locked(struct iommu_dma_cookie *cookie, struct iova_fq *fq)
{
	u64 counter = atomic64_read(&cookie->fq_flush_finish_cnt);
	unsigned int idx;

	assert_spin_locked(&fq->lock);

	fq_ring_for_each(idx, fq) {

		if (fq->entries[idx].counter >= counter)
			break;

		iommu_put_pages_list(&fq->entries[idx].freelist);
		free_iova_fast(&cookie->iovad,
			       fq->entries[idx].iova_pfn,
			       fq->entries[idx].pages);

		fq->head = (fq->head + 1) & fq->mod_mask;
	}
}

static void fq_ring_free(struct iommu_dma_cookie *cookie, struct iova_fq *fq)
{
	unsigned long flags;

	spin_lock_irqsave(&fq->lock, flags);
	fq_ring_free_locked(cookie, fq);
	spin_unlock_irqrestore(&fq->lock, flags);
}

static void fq_flush_iotlb(struct iommu_dma_cookie *cookie)
{
	atomic64_inc(&cookie->fq_flush_start_cnt);
	cookie->fq_domain->ops->flush_iotlb_all(cookie->fq_domain);
	atomic64_inc(&cookie->fq_flush_finish_cnt);
}

static void fq_flush_timeout(struct timer_list *t)
{
	struct iommu_dma_cookie *cookie = from_timer(cookie, t, fq_timer);
	int cpu;

	atomic_set(&cookie->fq_timer_on, 0);
	fq_flush_iotlb(cookie);

	if (cookie->options.qt == IOMMU_DMA_OPTS_SINGLE_QUEUE) {
		fq_ring_free(cookie, cookie->single_fq);
	} else {
		for_each_possible_cpu(cpu)
			fq_ring_free(cookie, per_cpu_ptr(cookie->percpu_fq, cpu));
	}
}

static void queue_iova(struct iommu_dma_cookie *cookie,
		unsigned long pfn, unsigned long pages,
		struct list_head *freelist)
{
	struct iova_fq *fq;
	unsigned long flags;
	unsigned int idx;

	/*
	 * Order against the IOMMU driver's pagetable update from unmapping
	 * @pte, to guarantee that fq_flush_iotlb() observes that if called
	 * from a different CPU before we release the lock below. Full barrier
	 * so it also pairs with iommu_dma_init_fq() to avoid seeing partially
	 * written fq state here.
	 */
	smp_mb();

	if (cookie->options.qt == IOMMU_DMA_OPTS_SINGLE_QUEUE)
		fq = cookie->single_fq;
	else
		fq = raw_cpu_ptr(cookie->percpu_fq);

	spin_lock_irqsave(&fq->lock, flags);

	/*
	 * First remove all entries from the flush queue that have already been
	 * flushed out on another CPU. This makes the fq_full() check below less
	 * likely to be true.
	 */
	fq_ring_free_locked(cookie, fq);

	if (fq_full(fq)) {
		fq_flush_iotlb(cookie);
		fq_ring_free_locked(cookie, fq);
	}

	idx = fq_ring_add(fq);

	fq->entries[idx].iova_pfn = pfn;
	fq->entries[idx].pages    = pages;
	fq->entries[idx].counter  = atomic64_read(&cookie->fq_flush_start_cnt);
	list_splice(freelist, &fq->entries[idx].freelist);

	spin_unlock_irqrestore(&fq->lock, flags);

	/* Avoid false sharing as much as possible. */
	if (!atomic_read(&cookie->fq_timer_on) &&
	    !atomic_xchg(&cookie->fq_timer_on, 1))
		mod_timer(&cookie->fq_timer,
			  jiffies + msecs_to_jiffies(cookie->options.fq_timeout));
}

static void iommu_dma_free_fq_single(struct iova_fq *fq)
{
	int idx;

	fq_ring_for_each(idx, fq)
		iommu_put_pages_list(&fq->entries[idx].freelist);
	vfree(fq);
}

static void iommu_dma_free_fq_percpu(struct iova_fq __percpu *percpu_fq)
{
	int cpu, idx;

	/* The IOVAs will be torn down separately, so just free our queued pages */
	for_each_possible_cpu(cpu) {
		struct iova_fq *fq = per_cpu_ptr(percpu_fq, cpu);

		fq_ring_for_each(idx, fq)
			iommu_put_pages_list(&fq->entries[idx].freelist);
	}

	free_percpu(percpu_fq);
}

static void iommu_dma_free_fq(struct iommu_dma_cookie *cookie)
{
	if (!cookie->fq_domain)
		return;

	del_timer_sync(&cookie->fq_timer);
	if (cookie->options.qt == IOMMU_DMA_OPTS_SINGLE_QUEUE)
		iommu_dma_free_fq_single(cookie->single_fq);
	else
		iommu_dma_free_fq_percpu(cookie->percpu_fq);
}

static void iommu_dma_init_one_fq(struct iova_fq *fq, size_t fq_size)
{
	int i;

	fq->head = 0;
	fq->tail = 0;
	fq->mod_mask = fq_size - 1;

	spin_lock_init(&fq->lock);

	for (i = 0; i < fq_size; i++)
		INIT_LIST_HEAD(&fq->entries[i].freelist);
}

static int iommu_dma_init_fq_single(struct iommu_dma_cookie *cookie)
{
	size_t fq_size = cookie->options.fq_size;
	struct iova_fq *queue;

	queue = vmalloc(struct_size(queue, entries, fq_size));
	if (!queue)
		return -ENOMEM;
	iommu_dma_init_one_fq(queue, fq_size);
	cookie->single_fq = queue;

	return 0;
}

static int iommu_dma_init_fq_percpu(struct iommu_dma_cookie *cookie)
{
	size_t fq_size = cookie->options.fq_size;
	struct iova_fq __percpu *queue;
	int cpu;

	queue = __alloc_percpu(struct_size(queue, entries, fq_size),
			       __alignof__(*queue));
	if (!queue)
		return -ENOMEM;

	for_each_possible_cpu(cpu)
		iommu_dma_init_one_fq(per_cpu_ptr(queue, cpu), fq_size);
	cookie->percpu_fq = queue;
	return 0;
}

/* sysfs updates are serialised by the mutex of the group owning @domain */
int iommu_dma_init_fq(struct iommu_domain *domain)
{
	struct iommu_dma_cookie *cookie = domain->iova_cookie;
	int rc;

	if (cookie->fq_domain)
		return 0;

	atomic64_set(&cookie->fq_flush_start_cnt,  0);
	atomic64_set(&cookie->fq_flush_finish_cnt, 0);

	if (cookie->options.qt == IOMMU_DMA_OPTS_SINGLE_QUEUE)
		rc = iommu_dma_init_fq_single(cookie);
	else
		rc = iommu_dma_init_fq_percpu(cookie);

	if (rc) {
		pr_warn("iova flush queue initialization failed\n");
		return -ENOMEM;
	}

	timer_setup(&cookie->fq_timer, fq_flush_timeout, 0);
	atomic_set(&cookie->fq_timer_on, 0);
	/*
	 * Prevent incomplete fq state being observable. Pairs with path from
	 * __iommu_dma_unmap() through iommu_dma_free_iova() to queue_iova()
	 */
	smp_wmb();
	WRITE_ONCE(cookie->fq_domain, domain);
	return 0;
}

static inline size_t cookie_msi_granule(struct iommu_dma_cookie *cookie)
{
	if (cookie->type == IOMMU_DMA_IOVA_COOKIE)
		return cookie->iovad.granule;
	return PAGE_SIZE;
}

static struct iommu_dma_cookie *cookie_alloc(enum iommu_dma_cookie_type type)
{
	struct iommu_dma_cookie *cookie;

	cookie = kzalloc(sizeof(*cookie), GFP_KERNEL);
	if (cookie) {
		INIT_LIST_HEAD(&cookie->msi_page_list);
		cookie->type = type;
	}
	return cookie;
}

/**
 * iommu_get_dma_cookie - Acquire DMA-API resources for a domain
 * @domain: IOMMU domain to prepare for DMA-API usage
 */
int iommu_get_dma_cookie(struct iommu_domain *domain)
{
	if (domain->iova_cookie)
		return -EEXIST;

	domain->iova_cookie = cookie_alloc(IOMMU_DMA_IOVA_COOKIE);
	if (!domain->iova_cookie)
		return -ENOMEM;

	mutex_init(&domain->iova_cookie->mutex);
	return 0;
}

/**
 * iommu_get_msi_cookie - Acquire just MSI remapping resources
 * @domain: IOMMU domain to prepare
 * @base: Start address of IOVA region for MSI mappings
 *
 * Users who manage their own IOVA allocation and do not want DMA API support,
 * but would still like to take advantage of automatic MSI remapping, can use
 * this to initialise their own domain appropriately. Users should reserve a
 * contiguous IOVA region, starting at @base, large enough to accommodate the
 * number of PAGE_SIZE mappings necessary to cover every MSI doorbell address
 * used by the devices attached to @domain.
 */
int iommu_get_msi_cookie(struct iommu_domain *domain, dma_addr_t base)
{
	struct iommu_dma_cookie *cookie;

	if (domain->type != IOMMU_DOMAIN_UNMANAGED)
		return -EINVAL;

	if (domain->iova_cookie)
		return -EEXIST;

	cookie = cookie_alloc(IOMMU_DMA_MSI_COOKIE);
	if (!cookie)
		return -ENOMEM;

	cookie->msi_iova = base;
	domain->iova_cookie = cookie;
	return 0;
}
EXPORT_SYMBOL(iommu_get_msi_cookie);

/**
 * iommu_put_dma_cookie - Release a domain's DMA mapping resources
 * @domain: IOMMU domain previously prepared by iommu_get_dma_cookie() or
 *          iommu_get_msi_cookie()
 */
void iommu_put_dma_cookie(struct iommu_domain *domain)
{
	struct iommu_dma_cookie *cookie = domain->iova_cookie;
	struct iommu_dma_msi_page *msi, *tmp;

	if (!cookie)
		return;

	if (cookie->type == IOMMU_DMA_IOVA_COOKIE && cookie->iovad.granule) {
		iommu_dma_free_fq(cookie);
		put_iova_domain(&cookie->iovad);
	}

	list_for_each_entry_safe(msi, tmp, &cookie->msi_page_list, list) {
		list_del(&msi->list);
		kfree(msi);
	}
	kfree(cookie);
	domain->iova_cookie = NULL;
}

/**
 * iommu_dma_get_resv_regions - Reserved region driver helper
 * @dev: Device from iommu_get_resv_regions()
 * @list: Reserved region list from iommu_get_resv_regions()
 *
 * IOMMU drivers can use this to implement their .get_resv_regions callback
 * for general non-IOMMU-specific reservations. Currently, this covers GICv3
 * ITS region reservation on ACPI based ARM platforms that may require HW MSI
 * reservation.
 */
void iommu_dma_get_resv_regions(struct device *dev, struct list_head *list)
{

	if (!is_of_node(dev_iommu_fwspec_get(dev)->iommu_fwnode))
		iort_iommu_get_resv_regions(dev, list);

	if (dev->of_node)
		of_iommu_get_resv_regions(dev, list);
}
EXPORT_SYMBOL(iommu_dma_get_resv_regions);

static int cookie_init_hw_msi_region(struct iommu_dma_cookie *cookie,
		phys_addr_t start, phys_addr_t end)
{
	struct iova_domain *iovad = &cookie->iovad;
	struct iommu_dma_msi_page *msi_page;
	int i, num_pages;

	start -= iova_offset(iovad, start);
	num_pages = iova_align(iovad, end - start) >> iova_shift(iovad);

	for (i = 0; i < num_pages; i++) {
		msi_page = kmalloc(sizeof(*msi_page), GFP_KERNEL);
		if (!msi_page)
			return -ENOMEM;

		msi_page->phys = start;
		msi_page->iova = start;
		INIT_LIST_HEAD(&msi_page->list);
		list_add(&msi_page->list, &cookie->msi_page_list);
		start += iovad->granule;
	}

	return 0;
}

static int iommu_dma_ranges_sort(void *priv, const struct list_head *a,
		const struct list_head *b)
{
	struct resource_entry *res_a = list_entry(a, typeof(*res_a), node);
	struct resource_entry *res_b = list_entry(b, typeof(*res_b), node);

	return res_a->res->start > res_b->res->start;
}

static int iova_reserve_pci_windows(struct pci_dev *dev,
		struct iova_domain *iovad)
{
	struct pci_host_bridge *bridge = pci_find_host_bridge(dev->bus);
	struct resource_entry *window;
	unsigned long lo, hi;
	phys_addr_t start = 0, end;

	resource_list_for_each_entry(window, &bridge->windows) {
		if (resource_type(window->res) != IORESOURCE_MEM)
			continue;

		lo = iova_pfn(iovad, window->res->start - window->offset);
		hi = iova_pfn(iovad, window->res->end - window->offset);
		reserve_iova(iovad, lo, hi);
	}

	/* Get reserved DMA windows from host bridge */
	list_sort(NULL, &bridge->dma_ranges, iommu_dma_ranges_sort);
	resource_list_for_each_entry(window, &bridge->dma_ranges) {
		end = window->res->start - window->offset;
resv_iova:
		if (end > start) {
			lo = iova_pfn(iovad, start);
			hi = iova_pfn(iovad, end);
			reserve_iova(iovad, lo, hi);
		} else if (end < start) {
			/* DMA ranges should be non-overlapping */
			dev_err(&dev->dev,
				"Failed to reserve IOVA [%pa-%pa]\n",
				&start, &end);
			return -EINVAL;
		}

		start = window->res->end - window->offset + 1;
		/* If window is last entry */
		if (window->node.next == &bridge->dma_ranges &&
		    end != ~(phys_addr_t)0) {
			end = ~(phys_addr_t)0;
			goto resv_iova;
		}
	}

	return 0;
}

static int iova_reserve_iommu_regions(struct device *dev,
		struct iommu_domain *domain)
{
	struct iommu_dma_cookie *cookie = domain->iova_cookie;
	struct iova_domain *iovad = &cookie->iovad;
	struct iommu_resv_region *region;
	LIST_HEAD(resv_regions);
	int ret = 0;

	if (dev_is_pci(dev)) {
		ret = iova_reserve_pci_windows(to_pci_dev(dev), iovad);
		if (ret)
			return ret;
	}

	iommu_get_resv_regions(dev, &resv_regions);
	list_for_each_entry(region, &resv_regions, list) {
		unsigned long lo, hi;

		/* We ARE the software that manages these! */
		if (region->type == IOMMU_RESV_SW_MSI)
			continue;

		lo = iova_pfn(iovad, region->start);
		hi = iova_pfn(iovad, region->start + region->length - 1);
		reserve_iova(iovad, lo, hi);

		if (region->type == IOMMU_RESV_MSI)
			ret = cookie_init_hw_msi_region(cookie, region->start,
					region->start + region->length);
		if (ret)
			break;
	}
	iommu_put_resv_regions(dev, &resv_regions);

	return ret;
}

static bool dev_is_untrusted(struct device *dev)
{
	return dev_is_pci(dev) && to_pci_dev(dev)->untrusted;
}

static bool dev_use_swiotlb(struct device *dev, size_t size,
			    enum dma_data_direction dir)
{
	return IS_ENABLED(CONFIG_SWIOTLB) &&
		(dev_is_untrusted(dev) ||
		 dma_kmalloc_needs_bounce(dev, size, dir));
}

static bool dev_use_sg_swiotlb(struct device *dev, struct scatterlist *sg,
			       int nents, enum dma_data_direction dir)
{
	struct scatterlist *s;
	int i;

	if (!IS_ENABLED(CONFIG_SWIOTLB))
		return false;

	if (dev_is_untrusted(dev))
		return true;

	/*
	 * If kmalloc() buffers are not DMA-safe for this device and
	 * direction, check the individual lengths in the sg list. If any
	 * element is deemed unsafe, use the swiotlb for bouncing.
	 */
	if (!dma_kmalloc_safe(dev, dir)) {
		for_each_sg(sg, s, nents, i)
			if (!dma_kmalloc_size_aligned(s->length))
				return true;
	}

	return false;
}

/**
 * iommu_dma_init_options - Initialize dma-iommu options
 * @options: The options to be initialized
 * @dev: Device the options are set for
 *
 * This allows tuning dma-iommu specific to device properties
 */
static void iommu_dma_init_options(struct iommu_dma_options *options,
				   struct device *dev)
{
	/* Shadowing IOTLB flushes do better with a single large queue */
	if (dev->iommu->shadow_on_flush) {
		options->qt = IOMMU_DMA_OPTS_SINGLE_QUEUE;
		options->fq_timeout = IOVA_SINGLE_FQ_TIMEOUT;
		options->fq_size = IOVA_SINGLE_FQ_SIZE;
	} else {
		options->qt = IOMMU_DMA_OPTS_PER_CPU_QUEUE;
		options->fq_size = IOVA_DEFAULT_FQ_SIZE;
		options->fq_timeout = IOVA_DEFAULT_FQ_TIMEOUT;
	}
}

/**
 * iommu_dma_init_domain - Initialise a DMA mapping domain
 * @domain: IOMMU domain previously prepared by iommu_get_dma_cookie()
 * @dev: Device the domain is being initialised for
 *
 * If the geometry and dma_range_map include address 0, we reserve that page
 * to ensure it is an invalid IOVA. It is safe to reinitialise a domain, but
 * any change which could make prior IOVAs invalid will fail.
 */
static int iommu_dma_init_domain(struct iommu_domain *domain, struct device *dev)
{
	struct iommu_dma_cookie *cookie = domain->iova_cookie;
	const struct bus_dma_region *map = dev->dma_range_map;
	unsigned long order, base_pfn;
	struct iova_domain *iovad;
	int ret;

	if (!cookie || cookie->type != IOMMU_DMA_IOVA_COOKIE)
		return -EINVAL;

	iovad = &cookie->iovad;

	/* Use the smallest supported page size for IOVA granularity */
	order = __ffs(domain->pgsize_bitmap);
	base_pfn = 1;

	/* Check the domain allows at least some access to the device... */
	if (map) {
		dma_addr_t base = dma_range_map_min(map);
		if (base > domain->geometry.aperture_end ||
		    dma_range_map_max(map) < domain->geometry.aperture_start) {
			pr_warn("specified DMA range outside IOMMU capability\n");
			return -EFAULT;
		}
		/* ...then finally give it a kicking to make sure it fits */
		base_pfn = max(base, domain->geometry.aperture_start) >> order;
	}

	/* start_pfn is always nonzero for an already-initialised domain */
	mutex_lock(&cookie->mutex);
	if (iovad->start_pfn) {
		if (1UL << order != iovad->granule ||
		    base_pfn != iovad->start_pfn) {
			pr_warn("Incompatible range for DMA domain\n");
			ret = -EFAULT;
			goto done_unlock;
		}

		ret = 0;
		goto done_unlock;
	}

	init_iova_domain(iovad, 1UL << order, base_pfn);
	ret = iova_domain_init_rcaches(iovad);
	if (ret)
		goto done_unlock;

	iommu_dma_init_options(&cookie->options, dev);

	/* If the FQ fails we can simply fall back to strict mode */
	if (domain->type == IOMMU_DOMAIN_DMA_FQ &&
	    (!device_iommu_capable(dev, IOMMU_CAP_DEFERRED_FLUSH) || iommu_dma_init_fq(domain)))
		domain->type = IOMMU_DOMAIN_DMA;

	ret = iova_reserve_iommu_regions(dev, domain);

done_unlock:
	mutex_unlock(&cookie->mutex);
	return ret;
}

/**
 * dma_info_to_prot - Translate DMA API directions and attributes to IOMMU API
 *                    page flags.
 * @dir: Direction of DMA transfer
 * @coherent: Is the DMA master cache-coherent?
 * @attrs: DMA attributes for the mapping
 *
 * Return: corresponding IOMMU API page protection flags
 */
static int dma_info_to_prot(enum dma_data_direction dir, bool coherent,
		     unsigned long attrs)
{
	int prot = coherent ? IOMMU_CACHE : 0;

	if (attrs & DMA_ATTR_PRIVILEGED)
		prot |= IOMMU_PRIV;

	switch (dir) {
	case DMA_BIDIRECTIONAL:
		return prot | IOMMU_READ | IOMMU_WRITE;
	case DMA_TO_DEVICE:
		return prot | IOMMU_READ;
	case DMA_FROM_DEVICE:
		return prot | IOMMU_WRITE;
	default:
		return 0;
	}
}

static dma_addr_t iommu_dma_alloc_iova(struct iommu_domain *domain,
		size_t size, u64 dma_limit, struct device *dev)
{
	struct iommu_dma_cookie *cookie = domain->iova_cookie;
	struct iova_domain *iovad = &cookie->iovad;
	unsigned long shift, iova_len, iova;

	if (cookie->type == IOMMU_DMA_MSI_COOKIE) {
		cookie->msi_iova += size;
		return cookie->msi_iova - size;
	}

	shift = iova_shift(iovad);
	iova_len = size >> shift;

	dma_limit = min_not_zero(dma_limit, dev->bus_dma_limit);

	if (domain->geometry.force_aperture)
		dma_limit = min(dma_limit, (u64)domain->geometry.aperture_end);

	/*
	 * Try to use all the 32-bit PCI addresses first. The original SAC vs.
	 * DAC reasoning loses relevance with PCIe, but enough hardware and
	 * firmware bugs are still lurking out there that it's safest not to
	 * venture into the 64-bit space until necessary.
	 *
	 * If your device goes wrong after seeing the notice then likely either
	 * its driver is not setting DMA masks accurately, the hardware has
	 * some inherent bug in handling >32-bit addresses, or not all the
	 * expected address bits are wired up between the device and the IOMMU.
	 */
	if (dma_limit > DMA_BIT_MASK(32) && dev->iommu->pci_32bit_workaround) {
		iova = alloc_iova_fast(iovad, iova_len,
				       DMA_BIT_MASK(32) >> shift, false);
		if (iova)
			goto done;

		dev->iommu->pci_32bit_workaround = false;
		dev_notice(dev, "Using %d-bit DMA addresses\n", bits_per(dma_limit));
	}

	iova = alloc_iova_fast(iovad, iova_len, dma_limit >> shift, true);
done:
	return (dma_addr_t)iova << shift;
}

static void iommu_dma_free_iova(struct iommu_dma_cookie *cookie,
		dma_addr_t iova, size_t size, struct iommu_iotlb_gather *gather)
{
	struct iova_domain *iovad = &cookie->iovad;

	/* The MSI case is only ever cleaning up its most recent allocation */
	if (cookie->type == IOMMU_DMA_MSI_COOKIE)
		cookie->msi_iova -= size;
	else if (gather && gather->queued)
		queue_iova(cookie, iova_pfn(iovad, iova),
				size >> iova_shift(iovad),
				&gather->freelist);
	else
		free_iova_fast(iovad, iova_pfn(iovad, iova),
				size >> iova_shift(iovad));
}

static void __iommu_dma_unmap(struct device *dev, dma_addr_t dma_addr,
		size_t size)
{
	struct iommu_domain *domain = iommu_get_dma_domain(dev);
	struct iommu_dma_cookie *cookie = domain->iova_cookie;
	struct iova_domain *iovad = &cookie->iovad;
	size_t iova_off = iova_offset(iovad, dma_addr);
	struct iommu_iotlb_gather iotlb_gather;
	size_t unmapped;

	dma_addr -= iova_off;
	size = iova_align(iovad, size + iova_off);
	iommu_iotlb_gather_init(&iotlb_gather);
	iotlb_gather.queued = READ_ONCE(cookie->fq_domain);

	unmapped = iommu_unmap_fast(domain, dma_addr, size, &iotlb_gather);
	WARN_ON(unmapped != size);

	if (!iotlb_gather.queued)
		iommu_iotlb_sync(domain, &iotlb_gather);
	iommu_dma_free_iova(cookie, dma_addr, size, &iotlb_gather);
}

static dma_addr_t __iommu_dma_map(struct device *dev, phys_addr_t phys,
		size_t size, int prot, u64 dma_mask)
{
	struct iommu_domain *domain = iommu_get_dma_domain(dev);
	struct iommu_dma_cookie *cookie = domain->iova_cookie;
	struct iova_domain *iovad = &cookie->iovad;
	size_t iova_off = iova_offset(iovad, phys);
	dma_addr_t iova;

	if (static_branch_unlikely(&iommu_deferred_attach_enabled) &&
	    iommu_deferred_attach(dev, domain))
		return DMA_MAPPING_ERROR;

	/* If anyone ever wants this we'd need support in the IOVA allocator */
	if (dev_WARN_ONCE(dev, dma_get_min_align_mask(dev) > iova_mask(iovad),
	    "Unsupported alignment constraint\n"))
		return DMA_MAPPING_ERROR;

	size = iova_align(iovad, size + iova_off);

	iova = iommu_dma_alloc_iova(domain, size, dma_mask, dev);
	if (!iova)
		return DMA_MAPPING_ERROR;

	if (iommu_map(domain, iova, phys - iova_off, size, prot, GFP_ATOMIC)) {
		iommu_dma_free_iova(cookie, iova, size, NULL);
		return DMA_MAPPING_ERROR;
	}
	return iova + iova_off;
}

static void __iommu_dma_free_pages(struct page **pages, int count)
{
	while (count--)
		__free_page(pages[count]);
	kvfree(pages);
}

static struct page **__iommu_dma_alloc_pages(struct device *dev,
		unsigned int count, unsigned long order_mask, gfp_t gfp)
{
	struct page **pages;
	unsigned int i = 0, nid = dev_to_node(dev);

	order_mask &= GENMASK(MAX_PAGE_ORDER, 0);
	if (!order_mask)
		return NULL;

	pages = kvcalloc(count, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return NULL;

	/* IOMMU can map any pages, so himem can also be used here */
	gfp |= __GFP_NOWARN | __GFP_HIGHMEM;

	while (count) {
		struct page *page = NULL;
		unsigned int order_size;

		/*
		 * Higher-order allocations are a convenience rather
		 * than a necessity, hence using __GFP_NORETRY until
		 * falling back to minimum-order allocations.
		 */
		for (order_mask &= GENMASK(__fls(count), 0);
		     order_mask; order_mask &= ~order_size) {
			unsigned int order = __fls(order_mask);
			gfp_t alloc_flags = gfp;

			order_size = 1U << order;
			if (order_mask > order_size)
				alloc_flags |= __GFP_NORETRY;
			page = alloc_pages_node(nid, alloc_flags, order);
			if (!page)
				continue;
			if (order)
				split_page(page, order);
			break;
		}
		if (!page) {
			__iommu_dma_free_pages(pages, i);
			return NULL;
		}
		count -= order_size;
		while (order_size--)
			pages[i++] = page++;
	}
	return pages;
}

/*
 * If size is less than PAGE_SIZE, then a full CPU page will be allocated,
 * but an IOMMU which supports smaller pages might not map the whole thing.
 */
static struct page **__iommu_dma_alloc_noncontiguous(struct device *dev,
		size_t size, struct sg_table *sgt, gfp_t gfp, pgprot_t prot,
		unsigned long attrs)
{
	struct iommu_domain *domain = iommu_get_dma_domain(dev);
	struct iommu_dma_cookie *cookie = domain->iova_cookie;
	struct iova_domain *iovad = &cookie->iovad;
	bool coherent = dev_is_dma_coherent(dev);
	int ioprot = dma_info_to_prot(DMA_BIDIRECTIONAL, coherent, attrs);
	unsigned int count, min_size, alloc_sizes = domain->pgsize_bitmap;
	struct page **pages;
	dma_addr_t iova;
	ssize_t ret;

	if (static_branch_unlikely(&iommu_deferred_attach_enabled) &&
	    iommu_deferred_attach(dev, domain))
		return NULL;

	min_size = alloc_sizes & -alloc_sizes;
	if (min_size < PAGE_SIZE) {
		min_size = PAGE_SIZE;
		alloc_sizes |= PAGE_SIZE;
	} else {
		size = ALIGN(size, min_size);
	}
	if (attrs & DMA_ATTR_ALLOC_SINGLE_PAGES)
		alloc_sizes = min_size;

	count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	pages = __iommu_dma_alloc_pages(dev, count, alloc_sizes >> PAGE_SHIFT,
					gfp);
	if (!pages)
		return NULL;

	size = iova_align(iovad, size);
	iova = iommu_dma_alloc_iova(domain, size, dev->coherent_dma_mask, dev);
	if (!iova)
		goto out_free_pages;

	/*
	 * Remove the zone/policy flags from the GFP - these are applied to the
	 * __iommu_dma_alloc_pages() but are not used for the supporting
	 * internal allocations that follow.
	 */
	gfp &= ~(__GFP_DMA | __GFP_DMA32 | __GFP_HIGHMEM | __GFP_COMP);

	if (sg_alloc_table_from_pages(sgt, pages, count, 0, size, gfp))
		goto out_free_iova;

	if (!(ioprot & IOMMU_CACHE)) {
		struct scatterlist *sg;
		int i;

		for_each_sg(sgt->sgl, sg, sgt->orig_nents, i)
			arch_dma_prep_coherent(sg_page(sg), sg->length);
	}

	ret = iommu_map_sg(domain, iova, sgt->sgl, sgt->orig_nents, ioprot,
			   gfp);
	if (ret < 0 || ret < size)
		goto out_free_sg;

	sgt->sgl->dma_address = iova;
	sgt->sgl->dma_length = size;
	return pages;

out_free_sg:
	sg_free_table(sgt);
out_free_iova:
	iommu_dma_free_iova(cookie, iova, size, NULL);
out_free_pages:
	__iommu_dma_free_pages(pages, count);
	return NULL;
}

static void *iommu_dma_alloc_remap(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp, pgprot_t prot,
		unsigned long attrs)
{
	struct page **pages;
	struct sg_table sgt;
	void *vaddr;

	pages = __iommu_dma_alloc_noncontiguous(dev, size, &sgt, gfp, prot,
						attrs);
	if (!pages)
		return NULL;
	*dma_handle = sgt.sgl->dma_address;
	sg_free_table(&sgt);
	vaddr = dma_common_pages_remap(pages, size, prot,
			__builtin_return_address(0));
	if (!vaddr)
		goto out_unmap;
	return vaddr;

out_unmap:
	__iommu_dma_unmap(dev, *dma_handle, size);
	__iommu_dma_free_pages(pages, PAGE_ALIGN(size) >> PAGE_SHIFT);
	return NULL;
}

static struct sg_table *iommu_dma_alloc_noncontiguous(struct device *dev,
		size_t size, enum dma_data_direction dir, gfp_t gfp,
		unsigned long attrs)
{
	struct dma_sgt_handle *sh;

	sh = kmalloc(sizeof(*sh), gfp);
	if (!sh)
		return NULL;

	sh->pages = __iommu_dma_alloc_noncontiguous(dev, size, &sh->sgt, gfp,
						    PAGE_KERNEL, attrs);
	if (!sh->pages) {
		kfree(sh);
		return NULL;
	}
	return &sh->sgt;
}

static void iommu_dma_free_noncontiguous(struct device *dev, size_t size,
		struct sg_table *sgt, enum dma_data_direction dir)
{
	struct dma_sgt_handle *sh = sgt_handle(sgt);

	__iommu_dma_unmap(dev, sgt->sgl->dma_address, size);
	__iommu_dma_free_pages(sh->pages, PAGE_ALIGN(size) >> PAGE_SHIFT);
	sg_free_table(&sh->sgt);
	kfree(sh);
}

static void iommu_dma_sync_single_for_cpu(struct device *dev,
		dma_addr_t dma_handle, size_t size, enum dma_data_direction dir)
{
	phys_addr_t phys;

	if (dev_is_dma_coherent(dev) && !dev_use_swiotlb(dev, size, dir))
		return;

	phys = iommu_iova_to_phys(iommu_get_dma_domain(dev), dma_handle);
	if (!dev_is_dma_coherent(dev))
		arch_sync_dma_for_cpu(phys, size, dir);

	if (is_swiotlb_buffer(dev, phys))
		swiotlb_sync_single_for_cpu(dev, phys, size, dir);
}

static void iommu_dma_sync_single_for_device(struct device *dev,
		dma_addr_t dma_handle, size_t size, enum dma_data_direction dir)
{
	phys_addr_t phys;

	if (dev_is_dma_coherent(dev) && !dev_use_swiotlb(dev, size, dir))
		return;

	phys = iommu_iova_to_phys(iommu_get_dma_domain(dev), dma_handle);
	if (is_swiotlb_buffer(dev, phys))
		swiotlb_sync_single_for_device(dev, phys, size, dir);

	if (!dev_is_dma_coherent(dev))
		arch_sync_dma_for_device(phys, size, dir);
}

static void iommu_dma_sync_sg_for_cpu(struct device *dev,
		struct scatterlist *sgl, int nelems,
		enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	if (sg_dma_is_swiotlb(sgl))
		for_each_sg(sgl, sg, nelems, i)
			iommu_dma_sync_single_for_cpu(dev, sg_dma_address(sg),
						      sg->length, dir);
	else if (!dev_is_dma_coherent(dev))
		for_each_sg(sgl, sg, nelems, i)
			arch_sync_dma_for_cpu(sg_phys(sg), sg->length, dir);
}

static void iommu_dma_sync_sg_for_device(struct device *dev,
		struct scatterlist *sgl, int nelems,
		enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	if (sg_dma_is_swiotlb(sgl))
		for_each_sg(sgl, sg, nelems, i)
			iommu_dma_sync_single_for_device(dev,
							 sg_dma_address(sg),
							 sg->length, dir);
	else if (!dev_is_dma_coherent(dev))
		for_each_sg(sgl, sg, nelems, i)
			arch_sync_dma_for_device(sg_phys(sg), sg->length, dir);
}

static dma_addr_t iommu_dma_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size, enum dma_data_direction dir,
		unsigned long attrs)
{
	phys_addr_t phys = page_to_phys(page) + offset;
	bool coherent = dev_is_dma_coherent(dev);
	int prot = dma_info_to_prot(dir, coherent, attrs);
	struct iommu_domain *domain = iommu_get_dma_domain(dev);
	struct iommu_dma_cookie *cookie = domain->iova_cookie;
	struct iova_domain *iovad = &cookie->iovad;
	dma_addr_t iova, dma_mask = dma_get_mask(dev);

	/*
	 * If both the physical buffer start address and size are
	 * page aligned, we don't need to use a bounce page.
	 */
	if (dev_use_swiotlb(dev, size, dir) &&
	    iova_offset(iovad, phys | size)) {
		if (!is_swiotlb_active(dev)) {
			dev_warn_once(dev, "DMA bounce buffers are inactive, unable to map unaligned transaction.\n");
			return DMA_MAPPING_ERROR;
		}

		trace_swiotlb_bounced(dev, phys, size);

		phys = swiotlb_tbl_map_single(dev, phys, size,
					      iova_mask(iovad), dir, attrs);

		if (phys == DMA_MAPPING_ERROR)
			return DMA_MAPPING_ERROR;

		/*
		 * Untrusted devices should not see padding areas with random
		 * leftover kernel data, so zero the pre- and post-padding.
		 * swiotlb_tbl_map_single() has initialized the bounce buffer
		 * proper to the contents of the original memory buffer.
		 */
		if (dev_is_untrusted(dev)) {
			size_t start, virt = (size_t)phys_to_virt(phys);

			/* Pre-padding */
			start = iova_align_down(iovad, virt);
			memset((void *)start, 0, virt - start);

			/* Post-padding */
			start = virt + size;
			memset((void *)start, 0,
			       iova_align(iovad, start) - start);
		}
	}

	if (!coherent && !(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		arch_sync_dma_for_device(phys, size, dir);

	iova = __iommu_dma_map(dev, phys, size, prot, dma_mask);
	if (iova == DMA_MAPPING_ERROR && is_swiotlb_buffer(dev, phys))
		swiotlb_tbl_unmap_single(dev, phys, size, dir, attrs);
	return iova;
}

static void iommu_dma_unmap_page(struct device *dev, dma_addr_t dma_handle,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	struct iommu_domain *domain = iommu_get_dma_domain(dev);
	phys_addr_t phys;

	phys = iommu_iova_to_phys(domain, dma_handle);
	if (WARN_ON(!phys))
		return;

	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC) && !dev_is_dma_coherent(dev))
		arch_sync_dma_for_cpu(phys, size, dir);

	__iommu_dma_unmap(dev, dma_handle, size);

	if (unlikely(is_swiotlb_buffer(dev, phys)))
		swiotlb_tbl_unmap_single(dev, phys, size, dir, attrs);
}

/*
 * Prepare a successfully-mapped scatterlist to give back to the caller.
 *
 * At this point the segments are already laid out by iommu_dma_map_sg() to
 * avoid individually crossing any boundaries, so we merely need to check a
 * segment's start address to avoid concatenating across one.
 */
static int __finalise_sg(struct device *dev, struct scatterlist *sg, int nents,
		dma_addr_t dma_addr)
{
	struct scatterlist *s, *cur = sg;
	unsigned long seg_mask = dma_get_seg_boundary(dev);
	unsigned int cur_len = 0, max_len = dma_get_max_seg_size(dev);
	int i, count = 0;

	for_each_sg(sg, s, nents, i) {
		/* Restore this segment's original unaligned fields first */
		dma_addr_t s_dma_addr = sg_dma_address(s);
		unsigned int s_iova_off = sg_dma_address(s);
		unsigned int s_length = sg_dma_len(s);
		unsigned int s_iova_len = s->length;

		sg_dma_address(s) = DMA_MAPPING_ERROR;
		sg_dma_len(s) = 0;

		if (sg_dma_is_bus_address(s)) {
			if (i > 0)
				cur = sg_next(cur);

			sg_dma_unmark_bus_address(s);
			sg_dma_address(cur) = s_dma_addr;
			sg_dma_len(cur) = s_length;
			sg_dma_mark_bus_address(cur);
			count++;
			cur_len = 0;
			continue;
		}

		s->offset += s_iova_off;
		s->length = s_length;

		/*
		 * Now fill in the real DMA data. If...
		 * - there is a valid output segment to append to
		 * - and this segment starts on an IOVA page boundary
		 * - but doesn't fall at a segment boundary
		 * - and wouldn't make the resulting output segment too long
		 */
		if (cur_len && !s_iova_off && (dma_addr & seg_mask) &&
		    (max_len - cur_len >= s_length)) {
			/* ...then concatenate it with the previous one */
			cur_len += s_length;
		} else {
			/* Otherwise start the next output segment */
			if (i > 0)
				cur = sg_next(cur);
			cur_len = s_length;
			count++;

			sg_dma_address(cur) = dma_addr + s_iova_off;
		}

		sg_dma_len(cur) = cur_len;
		dma_addr += s_iova_len;

		if (s_length + s_iova_off < s_iova_len)
			cur_len = 0;
	}
	return count;
}

/*
 * If mapping failed, then just restore the original list,
 * but making sure the DMA fields are invalidated.
 */
static void __invalidate_sg(struct scatterlist *sg, int nents)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i) {
		if (sg_dma_is_bus_address(s)) {
			sg_dma_unmark_bus_address(s);
		} else {
			if (sg_dma_address(s) != DMA_MAPPING_ERROR)
				s->offset += sg_dma_address(s);
			if (sg_dma_len(s))
				s->length = sg_dma_len(s);
		}
		sg_dma_address(s) = DMA_MAPPING_ERROR;
		sg_dma_len(s) = 0;
	}
}

static void iommu_dma_unmap_sg_swiotlb(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction dir, unsigned long attrs)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i)
		iommu_dma_unmap_page(dev, sg_dma_address(s),
				sg_dma_len(s), dir, attrs);
}

static int iommu_dma_map_sg_swiotlb(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction dir, unsigned long attrs)
{
	struct scatterlist *s;
	int i;

	sg_dma_mark_swiotlb(sg);

	for_each_sg(sg, s, nents, i) {
		sg_dma_address(s) = iommu_dma_map_page(dev, sg_page(s),
				s->offset, s->length, dir, attrs);
		if (sg_dma_address(s) == DMA_MAPPING_ERROR)
			goto out_unmap;
		sg_dma_len(s) = s->length;
	}

	return nents;

out_unmap:
	iommu_dma_unmap_sg_swiotlb(dev, sg, i, dir, attrs | DMA_ATTR_SKIP_CPU_SYNC);
	return -EIO;
}

/*
 * The DMA API client is passing in a scatterlist which could describe
 * any old buffer layout, but the IOMMU API requires everything to be
 * aligned to IOMMU pages. Hence the need for this complicated bit of
 * impedance-matching, to be able to hand off a suitably-aligned list,
 * but still preserve the original offsets and sizes for the caller.
 */
static int iommu_dma_map_sg(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction dir, unsigned long attrs)
{
	struct iommu_domain *domain = iommu_get_dma_domain(dev);
	struct iommu_dma_cookie *cookie = domain->iova_cookie;
	struct iova_domain *iovad = &cookie->iovad;
	struct scatterlist *s, *prev = NULL;
	int prot = dma_info_to_prot(dir, dev_is_dma_coherent(dev), attrs);
	struct pci_p2pdma_map_state p2pdma_state = {};
	enum pci_p2pdma_map_type map;
	dma_addr_t iova;
	size_t iova_len = 0;
	unsigned long mask = dma_get_seg_boundary(dev);
	ssize_t ret;
	int i;

	if (static_branch_unlikely(&iommu_deferred_attach_enabled)) {
		ret = iommu_deferred_attach(dev, domain);
		if (ret)
			goto out;
	}

	if (dev_use_sg_swiotlb(dev, sg, nents, dir))
		return iommu_dma_map_sg_swiotlb(dev, sg, nents, dir, attrs);

	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		iommu_dma_sync_sg_for_device(dev, sg, nents, dir);

	/*
	 * Work out how much IOVA space we need, and align the segments to
	 * IOVA granules for the IOMMU driver to handle. With some clever
	 * trickery we can modify the list in-place, but reversibly, by
	 * stashing the unaligned parts in the as-yet-unused DMA fields.
	 */
	for_each_sg(sg, s, nents, i) {
		size_t s_iova_off = iova_offset(iovad, s->offset);
		size_t s_length = s->length;
		size_t pad_len = (mask - iova_len + 1) & mask;

		if (is_pci_p2pdma_page(sg_page(s))) {
			map = pci_p2pdma_map_segment(&p2pdma_state, dev, s);
			switch (map) {
			case PCI_P2PDMA_MAP_BUS_ADDR:
				/*
				 * iommu_map_sg() will skip this segment as
				 * it is marked as a bus address,
				 * __finalise_sg() will copy the dma address
				 * into the output segment.
				 */
				continue;
			case PCI_P2PDMA_MAP_THRU_HOST_BRIDGE:
				/*
				 * Mapping through host bridge should be
				 * mapped with regular IOVAs, thus we
				 * do nothing here and continue below.
				 */
				break;
			default:
				ret = -EREMOTEIO;
				goto out_restore_sg;
			}
		}

		sg_dma_address(s) = s_iova_off;
		sg_dma_len(s) = s_length;
		s->offset -= s_iova_off;
		s_length = iova_align(iovad, s_length + s_iova_off);
		s->length = s_length;

		/*
		 * Due to the alignment of our single IOVA allocation, we can
		 * depend on these assumptions about the segment boundary mask:
		 * - If mask size >= IOVA size, then the IOVA range cannot
		 *   possibly fall across a boundary, so we don't care.
		 * - If mask size < IOVA size, then the IOVA range must start
		 *   exactly on a boundary, therefore we can lay things out
		 *   based purely on segment lengths without needing to know
		 *   the actual addresses beforehand.
		 * - The mask must be a power of 2, so pad_len == 0 if
		 *   iova_len == 0, thus we cannot dereference prev the first
		 *   time through here (i.e. before it has a meaningful value).
		 */
		if (pad_len && pad_len < s_length - 1) {
			prev->length += pad_len;
			iova_len += pad_len;
		}

		iova_len += s_length;
		prev = s;
	}

	if (!iova_len)
		return __finalise_sg(dev, sg, nents, 0);

	iova = iommu_dma_alloc_iova(domain, iova_len, dma_get_mask(dev), dev);
	if (!iova) {
		ret = -ENOMEM;
		goto out_restore_sg;
	}

	/*
	 * We'll leave any physical concatenation to the IOMMU driver's
	 * implementation - it knows better than we do.
	 */
	ret = iommu_map_sg(domain, iova, sg, nents, prot, GFP_ATOMIC);
	if (ret < 0 || ret < iova_len)
		goto out_free_iova;

	return __finalise_sg(dev, sg, nents, iova);

out_free_iova:
	iommu_dma_free_iova(cookie, iova, iova_len, NULL);
out_restore_sg:
	__invalidate_sg(sg, nents);
out:
	if (ret != -ENOMEM && ret != -EREMOTEIO)
		return -EINVAL;
	return ret;
}

static void iommu_dma_unmap_sg(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction dir, unsigned long attrs)
{
	dma_addr_t end = 0, start;
	struct scatterlist *tmp;
	int i;

	if (sg_dma_is_swiotlb(sg)) {
		iommu_dma_unmap_sg_swiotlb(dev, sg, nents, dir, attrs);
		return;
	}

	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		iommu_dma_sync_sg_for_cpu(dev, sg, nents, dir);

	/*
	 * The scatterlist segments are mapped into a single
	 * contiguous IOVA allocation, the start and end points
	 * just have to be determined.
	 */
	for_each_sg(sg, tmp, nents, i) {
		if (sg_dma_is_bus_address(tmp)) {
			sg_dma_unmark_bus_address(tmp);
			continue;
		}

		if (sg_dma_len(tmp) == 0)
			break;

		start = sg_dma_address(tmp);
		break;
	}

	nents -= i;
	for_each_sg(tmp, tmp, nents, i) {
		if (sg_dma_is_bus_address(tmp)) {
			sg_dma_unmark_bus_address(tmp);
			continue;
		}

		if (sg_dma_len(tmp) == 0)
			break;

		end = sg_dma_address(tmp) + sg_dma_len(tmp);
	}

	if (end)
		__iommu_dma_unmap(dev, start, end - start);
}

static dma_addr_t iommu_dma_map_resource(struct device *dev, phys_addr_t phys,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	return __iommu_dma_map(dev, phys, size,
			dma_info_to_prot(dir, false, attrs) | IOMMU_MMIO,
			dma_get_mask(dev));
}

static void iommu_dma_unmap_resource(struct device *dev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	__iommu_dma_unmap(dev, handle, size);
}

static void __iommu_dma_free(struct device *dev, size_t size, void *cpu_addr)
{
	size_t alloc_size = PAGE_ALIGN(size);
	int count = alloc_size >> PAGE_SHIFT;
	struct page *page = NULL, **pages = NULL;

	/* Non-coherent atomic allocation? Easy */
	if (IS_ENABLED(CONFIG_DMA_DIRECT_REMAP) &&
	    dma_free_from_pool(dev, cpu_addr, alloc_size))
		return;

	if (is_vmalloc_addr(cpu_addr)) {
		/*
		 * If it the address is remapped, then it's either non-coherent
		 * or highmem CMA, or an iommu_dma_alloc_remap() construction.
		 */
		pages = dma_common_find_pages(cpu_addr);
		if (!pages)
			page = vmalloc_to_page(cpu_addr);
		dma_common_free_remap(cpu_addr, alloc_size);
	} else {
		/* Lowmem means a coherent atomic or CMA allocation */
		page = virt_to_page(cpu_addr);
	}

	if (pages)
		__iommu_dma_free_pages(pages, count);
	if (page)
		dma_free_contiguous(dev, page, alloc_size);
}

static void iommu_dma_free(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t handle, unsigned long attrs)
{
	__iommu_dma_unmap(dev, handle, size);
	__iommu_dma_free(dev, size, cpu_addr);
}

static void *iommu_dma_alloc_pages(struct device *dev, size_t size,
		struct page **pagep, gfp_t gfp, unsigned long attrs)
{
	bool coherent = dev_is_dma_coherent(dev);
	size_t alloc_size = PAGE_ALIGN(size);
	int node = dev_to_node(dev);
	struct page *page = NULL;
	void *cpu_addr;

	page = dma_alloc_contiguous(dev, alloc_size, gfp);
	if (!page)
		page = alloc_pages_node(node, gfp, get_order(alloc_size));
	if (!page)
		return NULL;

	if (!coherent || PageHighMem(page)) {
		pgprot_t prot = dma_pgprot(dev, PAGE_KERNEL, attrs);

		cpu_addr = dma_common_contiguous_remap(page, alloc_size,
				prot, __builtin_return_address(0));
		if (!cpu_addr)
			goto out_free_pages;

		if (!coherent)
			arch_dma_prep_coherent(page, size);
	} else {
		cpu_addr = page_address(page);
	}

	*pagep = page;
	memset(cpu_addr, 0, alloc_size);
	return cpu_addr;
out_free_pages:
	dma_free_contiguous(dev, page, alloc_size);
	return NULL;
}

static void *iommu_dma_alloc(struct device *dev, size_t size,
		dma_addr_t *handle, gfp_t gfp, unsigned long attrs)
{
	bool coherent = dev_is_dma_coherent(dev);
	int ioprot = dma_info_to_prot(DMA_BIDIRECTIONAL, coherent, attrs);
	struct page *page = NULL;
	void *cpu_addr;

	gfp |= __GFP_ZERO;

	if (gfpflags_allow_blocking(gfp) &&
	    !(attrs & DMA_ATTR_FORCE_CONTIGUOUS)) {
		return iommu_dma_alloc_remap(dev, size, handle, gfp,
				dma_pgprot(dev, PAGE_KERNEL, attrs), attrs);
	}

	if (IS_ENABLED(CONFIG_DMA_DIRECT_REMAP) &&
	    !gfpflags_allow_blocking(gfp) && !coherent)
		page = dma_alloc_from_pool(dev, PAGE_ALIGN(size), &cpu_addr,
					       gfp, NULL);
	else
		cpu_addr = iommu_dma_alloc_pages(dev, size, &page, gfp, attrs);
	if (!cpu_addr)
		return NULL;

	*handle = __iommu_dma_map(dev, page_to_phys(page), size, ioprot,
			dev->coherent_dma_mask);
	if (*handle == DMA_MAPPING_ERROR) {
		__iommu_dma_free(dev, size, cpu_addr);
		return NULL;
	}

	return cpu_addr;
}

static int iommu_dma_mmap(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs)
{
	unsigned long nr_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned long pfn, off = vma->vm_pgoff;
	int ret;

	vma->vm_page_prot = dma_pgprot(dev, vma->vm_page_prot, attrs);

	if (dma_mmap_from_dev_coherent(dev, vma, cpu_addr, size, &ret))
		return ret;

	if (off >= nr_pages || vma_pages(vma) > nr_pages - off)
		return -ENXIO;

	if (is_vmalloc_addr(cpu_addr)) {
		struct page **pages = dma_common_find_pages(cpu_addr);

		if (pages)
			return vm_map_pages(vma, pages, nr_pages);
		pfn = vmalloc_to_pfn(cpu_addr);
	} else {
		pfn = page_to_pfn(virt_to_page(cpu_addr));
	}

	return remap_pfn_range(vma, vma->vm_start, pfn + off,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot);
}

static int iommu_dma_get_sgtable(struct device *dev, struct sg_table *sgt,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs)
{
	struct page *page;
	int ret;

	if (is_vmalloc_addr(cpu_addr)) {
		struct page **pages = dma_common_find_pages(cpu_addr);

		if (pages) {
			return sg_alloc_table_from_pages(sgt, pages,
					PAGE_ALIGN(size) >> PAGE_SHIFT,
					0, size, GFP_KERNEL);
		}

		page = vmalloc_to_page(cpu_addr);
	} else {
		page = virt_to_page(cpu_addr);
	}

	ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
	if (!ret)
		sg_set_page(sgt->sgl, page, PAGE_ALIGN(size), 0);
	return ret;
}

static unsigned long iommu_dma_get_merge_boundary(struct device *dev)
{
	struct iommu_domain *domain = iommu_get_dma_domain(dev);

	return (1UL << __ffs(domain->pgsize_bitmap)) - 1;
}

static size_t iommu_dma_opt_mapping_size(void)
{
	return iova_rcache_range();
}

static size_t iommu_dma_max_mapping_size(struct device *dev)
{
	if (dev_is_untrusted(dev))
		return swiotlb_max_mapping_size(dev);

	return SIZE_MAX;
}

static const struct dma_map_ops iommu_dma_ops = {
	.flags			= DMA_F_PCI_P2PDMA_SUPPORTED |
				  DMA_F_CAN_SKIP_SYNC,
	.alloc			= iommu_dma_alloc,
	.free			= iommu_dma_free,
	.alloc_pages_op		= dma_common_alloc_pages,
	.free_pages		= dma_common_free_pages,
	.alloc_noncontiguous	= iommu_dma_alloc_noncontiguous,
	.free_noncontiguous	= iommu_dma_free_noncontiguous,
	.mmap			= iommu_dma_mmap,
	.get_sgtable		= iommu_dma_get_sgtable,
	.map_page		= iommu_dma_map_page,
	.unmap_page		= iommu_dma_unmap_page,
	.map_sg			= iommu_dma_map_sg,
	.unmap_sg		= iommu_dma_unmap_sg,
	.sync_single_for_cpu	= iommu_dma_sync_single_for_cpu,
	.sync_single_for_device	= iommu_dma_sync_single_for_device,
	.sync_sg_for_cpu	= iommu_dma_sync_sg_for_cpu,
	.sync_sg_for_device	= iommu_dma_sync_sg_for_device,
	.map_resource		= iommu_dma_map_resource,
	.unmap_resource		= iommu_dma_unmap_resource,
	.get_merge_boundary	= iommu_dma_get_merge_boundary,
	.opt_mapping_size	= iommu_dma_opt_mapping_size,
	.max_mapping_size       = iommu_dma_max_mapping_size,
};

void iommu_setup_dma_ops(struct device *dev)
{
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);

	if (dev_is_pci(dev))
		dev->iommu->pci_32bit_workaround = !iommu_dma_forcedac;

	if (iommu_is_dma_domain(domain)) {
		if (iommu_dma_init_domain(domain, dev))
			goto out_err;
		dev->dma_ops = &iommu_dma_ops;
	} else if (dev->dma_ops == &iommu_dma_ops) {
		/* Clean up if we've switched *from* a DMA domain */
		dev->dma_ops = NULL;
	}

	return;
out_err:
	 pr_warn("Failed to set up IOMMU for device %s; retaining platform DMA ops\n",
		 dev_name(dev));
}

static struct iommu_dma_msi_page *iommu_dma_get_msi_page(struct device *dev,
		phys_addr_t msi_addr, struct iommu_domain *domain)
{
	struct iommu_dma_cookie *cookie = domain->iova_cookie;
	struct iommu_dma_msi_page *msi_page;
	dma_addr_t iova;
	int prot = IOMMU_WRITE | IOMMU_NOEXEC | IOMMU_MMIO;
	size_t size = cookie_msi_granule(cookie);

	msi_addr &= ~(phys_addr_t)(size - 1);
	list_for_each_entry(msi_page, &cookie->msi_page_list, list)
		if (msi_page->phys == msi_addr)
			return msi_page;

	msi_page = kzalloc(sizeof(*msi_page), GFP_KERNEL);
	if (!msi_page)
		return NULL;

	iova = iommu_dma_alloc_iova(domain, size, dma_get_mask(dev), dev);
	if (!iova)
		goto out_free_page;

	if (iommu_map(domain, iova, msi_addr, size, prot, GFP_KERNEL))
		goto out_free_iova;

	INIT_LIST_HEAD(&msi_page->list);
	msi_page->phys = msi_addr;
	msi_page->iova = iova;
	list_add(&msi_page->list, &cookie->msi_page_list);
	return msi_page;

out_free_iova:
	iommu_dma_free_iova(cookie, iova, size, NULL);
out_free_page:
	kfree(msi_page);
	return NULL;
}

/**
 * iommu_dma_prepare_msi() - Map the MSI page in the IOMMU domain
 * @desc: MSI descriptor, will store the MSI page
 * @msi_addr: MSI target address to be mapped
 *
 * Return: 0 on success or negative error code if the mapping failed.
 */
int iommu_dma_prepare_msi(struct msi_desc *desc, phys_addr_t msi_addr)
{
	struct device *dev = msi_desc_to_dev(desc);
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	struct iommu_dma_msi_page *msi_page;
	static DEFINE_MUTEX(msi_prepare_lock); /* see below */

	if (!domain || !domain->iova_cookie) {
		desc->iommu_cookie = NULL;
		return 0;
	}

	/*
	 * In fact the whole prepare operation should already be serialised by
	 * irq_domain_mutex further up the callchain, but that's pretty subtle
	 * on its own, so consider this locking as failsafe documentation...
	 */
	mutex_lock(&msi_prepare_lock);
	msi_page = iommu_dma_get_msi_page(dev, msi_addr, domain);
	mutex_unlock(&msi_prepare_lock);

	msi_desc_set_iommu_cookie(desc, msi_page);

	if (!msi_page)
		return -ENOMEM;
	return 0;
}

/**
 * iommu_dma_compose_msi_msg() - Apply translation to an MSI message
 * @desc: MSI descriptor prepared by iommu_dma_prepare_msi()
 * @msg: MSI message containing target physical address
 */
void iommu_dma_compose_msi_msg(struct msi_desc *desc, struct msi_msg *msg)
{
	struct device *dev = msi_desc_to_dev(desc);
	const struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	const struct iommu_dma_msi_page *msi_page;

	msi_page = msi_desc_get_iommu_cookie(desc);

	if (!domain || !domain->iova_cookie || WARN_ON(!msi_page))
		return;

	msg->address_hi = upper_32_bits(msi_page->iova);
	msg->address_lo &= cookie_msi_granule(domain->iova_cookie) - 1;
	msg->address_lo += lower_32_bits(msi_page->iova);
}

static int iommu_dma_init(void)
{
	if (is_kdump_kernel())
		static_branch_enable(&iommu_deferred_attach_enabled);

	return iova_cache_get();
}
arch_initcall(iommu_dma_init);
