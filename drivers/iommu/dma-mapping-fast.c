// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
 */

#include <linux/dma-contiguous.h>
#include <linux/dma-mapping.h>
#include <linux/dma-mapping-fast.h>
#include <linux/io-pgtable.h>
#include <linux/io-pgtable-fast.h>
#include <linux/vmalloc.h>
#include <asm/cacheflush.h>
#include <asm/dma-iommu.h>
#include <linux/slab.h>
#include <linux/genalloc.h>
#include <linux/vmalloc.h>
#include <linux/pci.h>
#include <linux/dma-iommu.h>
#include <linux/iova.h>
#include <trace/events/iommu.h>

/* some redundant definitions... :( TODO: move to io-pgtable-fast.h */
#define FAST_PAGE_SHIFT		12
#define FAST_PAGE_SIZE (1UL << FAST_PAGE_SHIFT)
#define FAST_PAGE_MASK (~(PAGE_SIZE - 1))

#define DEFAULT_DMA_COHERENT_POOL_SIZE	SZ_256K
static struct gen_pool *atomic_pool __ro_after_init;

static size_t atomic_pool_size __initdata = DEFAULT_DMA_COHERENT_POOL_SIZE;

static int __init early_coherent_pool(char *p)
{
	atomic_pool_size = memparse(p, &p);
	return 0;
}
early_param("coherent_pool", early_coherent_pool);

static pgprot_t __get_dma_pgprot(unsigned long attrs, pgprot_t prot,
				 bool coherent)
{
	if (attrs & DMA_ATTR_STRONGLY_ORDERED)
		return pgprot_noncached(prot);
	else if (!coherent || (attrs & DMA_ATTR_WRITE_COMBINE))
		return pgprot_writecombine(prot);
	return prot;
}

static void *__alloc_from_pool(size_t size, struct page **ret_page, gfp_t flags)
{
	unsigned long val;
	void *ptr = NULL;

	if (!atomic_pool) {
		WARN(1, "coherent pool not initialised!\n");
		return NULL;
	}

	val = gen_pool_alloc(atomic_pool, size);
	if (val) {
		phys_addr_t phys = gen_pool_virt_to_phys(atomic_pool, val);

		*ret_page = phys_to_page(phys);
		ptr = (void *)val;
		memset(ptr, 0, size);
	}

	return ptr;
}

static phys_addr_t __atomic_get_phys(void *addr)
{
	return gen_pool_virt_to_phys(atomic_pool, (unsigned long)addr);
}

static bool __in_atomic_pool(void *start, size_t size)
{
	if (!atomic_pool)
		return false;
	return addr_in_gen_pool(atomic_pool, (unsigned long)start, size);
}

static int __free_from_pool(void *start, size_t size)
{
	if (!__in_atomic_pool(start, size))
		return 0;

	gen_pool_free(atomic_pool, (unsigned long)start, size);

	return 1;
}

static bool is_dma_coherent(struct device *dev, unsigned long attrs)
{
	bool is_coherent;

	if (attrs & DMA_ATTR_FORCE_COHERENT)
		is_coherent = true;
	else if (attrs & DMA_ATTR_FORCE_NON_COHERENT)
		is_coherent = false;
	else if (is_device_dma_coherent(dev))
		is_coherent = true;
	else
		is_coherent = false;

	return is_coherent;
}

static struct dma_fast_smmu_mapping *dev_get_mapping(struct device *dev)
{
	struct iommu_domain *domain;

	domain = iommu_get_domain_for_dev(dev);
	if (!domain)
		return ERR_PTR(-EINVAL);
	return domain->iova_cookie;
}

/*
 * Checks if the allocated range (ending at @end) covered the upcoming
 * stale bit.  We don't need to know exactly where the range starts since
 * we already know where the candidate search range started.  If, starting
 * from the beginning of the candidate search range, we had to step over
 * (or landed directly on top of) the upcoming stale bit, then we return
 * true.
 *
 * Due to wrapping, there are two scenarios we'll need to check: (1) if the
 * range [search_start, upcoming_stale] spans 0 (i.e. search_start >
 * upcoming_stale), and, (2) if the range: [search_start, upcoming_stale]
 * does *not* span 0 (i.e. search_start <= upcoming_stale).  And for each
 * of those two scenarios we need to handle three cases: (1) the bit was
 * found before wrapping or
 */
static bool __bit_covered_stale(unsigned long upcoming_stale,
				unsigned long search_start,
				unsigned long end)
{
	if (search_start > upcoming_stale) {
		if (end >= search_start) {
			/*
			 * We started searching above upcoming_stale and we
			 * didn't wrap, so we couldn't have crossed
			 * upcoming_stale.
			 */
			return false;
		}
		/*
		 * We wrapped. Did we cross (or land on top of)
		 * upcoming_stale?
		 */
		return end >= upcoming_stale;
	}

	if (search_start <= upcoming_stale) {
		if (end >= search_start) {
			/*
			 * We didn't wrap.  Did we cross (or land on top
			 * of) upcoming_stale?
			 */
			return end >= upcoming_stale;
		}
		/*
		 * We wrapped. So we must have crossed upcoming_stale
		 * (since we started searching below it).
		 */
		return true;
	}

	/* we should have covered all logical combinations... */
	WARN_ON(1);
	return true;
}

static dma_addr_t __fast_smmu_alloc_iova(struct dma_fast_smmu_mapping *mapping,
					 unsigned long attrs,
					 size_t size)
{
	unsigned long bit, prev_search_start, nbits = size >> FAST_PAGE_SHIFT;
	unsigned long align = (1 << get_order(size)) - 1;

	bit = bitmap_find_next_zero_area(
		mapping->bitmap, mapping->num_4k_pages, mapping->next_start,
		nbits, align);
	if (unlikely(bit > mapping->num_4k_pages)) {
		/* try wrapping */
		bit = bitmap_find_next_zero_area(
			mapping->bitmap, mapping->num_4k_pages, 0, nbits,
			align);
		if (unlikely(bit > mapping->num_4k_pages))
			return DMA_ERROR_CODE;
	}

	bitmap_set(mapping->bitmap, bit, nbits);
	prev_search_start = mapping->next_start;
	mapping->next_start = bit + nbits;
	if (unlikely(mapping->next_start >= mapping->num_4k_pages))
		mapping->next_start = 0;

	/*
	 * If we just re-allocated a VA whose TLB hasn't been invalidated
	 * since it was last used and unmapped, we need to invalidate it
	 * here.  We actually invalidate the entire TLB so that we don't
	 * have to invalidate the TLB again until we wrap back around.
	 */
	if (mapping->have_stale_tlbs &&
	    __bit_covered_stale(mapping->upcoming_stale_bit,
				prev_search_start,
				bit + nbits - 1)) {
		bool skip_sync = (attrs & DMA_ATTR_SKIP_CPU_SYNC);

		iommu_tlbiall(mapping->domain);
		mapping->have_stale_tlbs = false;
		av8l_fast_clear_stale_ptes(mapping->pgtbl_ops,
				mapping->domain->geometry.aperture_start,
				mapping->base,
				mapping->base + mapping->size - 1,
				skip_sync);
	}

	return (bit << FAST_PAGE_SHIFT) + mapping->base;
}

/*
 * Checks whether the candidate bit will be allocated sooner than the
 * current upcoming stale bit.  We can say candidate will be upcoming
 * sooner than the current upcoming stale bit if it lies between the
 * starting bit of the next search range and the upcoming stale bit
 * (allowing for wrap-around).
 *
 * Stated differently, we're checking the relative ordering of three
 * unsigned numbers.  So we need to check all 6 (i.e. 3!) permutations,
 * namely:
 *
 *     0 |---A---B---C---| TOP (Case 1)
 *     0 |---A---C---B---| TOP (Case 2)
 *     0 |---B---A---C---| TOP (Case 3)
 *     0 |---B---C---A---| TOP (Case 4)
 *     0 |---C---A---B---| TOP (Case 5)
 *     0 |---C---B---A---| TOP (Case 6)
 *
 * Note that since we're allowing numbers to wrap, the following three
 * scenarios are all equivalent for Case 1:
 *
 *     0 |---A---B---C---| TOP
 *     0 |---C---A---B---| TOP (C has wrapped. This is Case 5.)
 *     0 |---B---C---A---| TOP (C and B have wrapped. This is Case 4.)
 *
 * In any of these cases, if we start searching from A, we will find B
 * before we find C.
 *
 * We can also find two equivalent cases for Case 2:
 *
 *     0 |---A---C---B---| TOP
 *     0 |---B---A---C---| TOP (B has wrapped. This is Case 3.)
 *     0 |---C---B---A---| TOP (B and C have wrapped. This is Case 6.)
 *
 * In any of these cases, if we start searching from A, we will find C
 * before we find B.
 */
static bool __bit_is_sooner(unsigned long candidate,
			    struct dma_fast_smmu_mapping *mapping)
{
	unsigned long A = mapping->next_start;
	unsigned long B = candidate;
	unsigned long C = mapping->upcoming_stale_bit;

	if ((A < B && B < C) ||	/* Case 1 */
	    (C < A && A < B) ||	/* Case 5 */
	    (B < C && C < A))	/* Case 4 */
		return true;

	if ((A < C && C < B) ||	/* Case 2 */
	    (B < A && A < C) ||	/* Case 3 */
	    (C < B && B < A))	/* Case 6 */
		return false;

	/*
	 * For simplicity, we've been ignoring the possibility of any of
	 * our three numbers being equal.  Handle those cases here (they
	 * shouldn't happen very often, (I think?)).
	 */

	/*
	 * If candidate is the next bit to be searched then it's definitely
	 * sooner.
	 */
	if (A == B)
		return true;

	/*
	 * If candidate is the next upcoming stale bit we'll return false
	 * to avoid doing `upcoming = candidate' in the caller (which would
	 * be useless since they're already equal)
	 */
	if (B == C)
		return false;

	/*
	 * If next start is the upcoming stale bit then candidate can't
	 * possibly be sooner.  The "soonest" bit is already selected.
	 */
	if (A == C)
		return false;

	/* We should have covered all logical combinations. */
	WARN(1, "Well, that's awkward. A=%ld, B=%ld, C=%ld\n", A, B, C);
	return true;
}

#ifdef CONFIG_ARM64
static int __init atomic_pool_init(void)
{
	pgprot_t prot = __pgprot(PROT_NORMAL_NC);
	unsigned long nr_pages = atomic_pool_size >> PAGE_SHIFT;
	struct page *page;
	void *addr;
	unsigned int pool_size_order = get_order(atomic_pool_size);

	if (dev_get_cma_area(NULL))
		page = dma_alloc_from_contiguous(NULL, nr_pages,
						 pool_size_order, false);
	else
		page = alloc_pages(GFP_DMA32, pool_size_order);

	if (page) {
		int ret;
		void *page_addr = page_address(page);

		memset(page_addr, 0, atomic_pool_size);
		__dma_flush_area(page_addr, atomic_pool_size);

		atomic_pool = gen_pool_create(PAGE_SHIFT, -1);
		if (!atomic_pool)
			goto free_page;

		addr = dma_common_contiguous_remap(page, atomic_pool_size,
					VM_USERMAP, prot, atomic_pool_init);

		if (!addr)
			goto destroy_genpool;

		ret = gen_pool_add_virt(atomic_pool, (unsigned long)addr,
					page_to_phys(page),
					atomic_pool_size, -1);
		if (ret)
			goto remove_mapping;

		gen_pool_set_algo(atomic_pool,
				  gen_pool_first_fit_order_align,
				  NULL);

		pr_info("DMA: preallocated %zu KiB pool for atomic allocations\n",
			atomic_pool_size / 1024);
		return 0;
	}
	goto out;

remove_mapping:
	dma_common_free_remap(addr, atomic_pool_size, VM_USERMAP, false);
destroy_genpool:
	gen_pool_destroy(atomic_pool);
	atomic_pool = NULL;
free_page:
	if (!dma_release_from_contiguous(NULL, page, nr_pages))
		__free_pages(page, pool_size_order);
out:
	pr_err("DMA: failed to allocate %zu KiB pool for atomic coherent allocation\n",
		atomic_pool_size / 1024);
	return -ENOMEM;
}
arch_initcall(atomic_pool_init);
#endif

static void __fast_smmu_free_iova(struct dma_fast_smmu_mapping *mapping,
				  dma_addr_t iova, size_t size)
{
	unsigned long start_bit = (iova - mapping->base) >> FAST_PAGE_SHIFT;
	unsigned long nbits = size >> FAST_PAGE_SHIFT;

	/*
	 * We don't invalidate TLBs on unmap.  We invalidate TLBs on map
	 * when we're about to re-allocate a VA that was previously
	 * unmapped but hasn't yet been invalidated.  So we need to keep
	 * track of which bit is the closest to being re-allocated here.
	 */
	if (__bit_is_sooner(start_bit, mapping))
		mapping->upcoming_stale_bit = start_bit;

	bitmap_clear(mapping->bitmap, start_bit, nbits);
	mapping->have_stale_tlbs = true;
}


static void __fast_dma_page_cpu_to_dev(struct page *page, unsigned long off,
				       size_t size, enum dma_data_direction dir)
{
	__dma_map_area(page_address(page) + off, size, dir);
}

static void __fast_dma_page_dev_to_cpu(struct page *page, unsigned long off,
				       size_t size, enum dma_data_direction dir)
{
	__dma_unmap_area(page_address(page) + off, size, dir);

	/* TODO: WHAT IS THIS? */
	/*
	 * Mark the D-cache clean for this page to avoid extra flushing.
	 */
	if (dir != DMA_TO_DEVICE && off == 0 && size >= PAGE_SIZE)
		set_bit(PG_dcache_clean, &page->flags);
}

static dma_addr_t fast_smmu_map_page(struct device *dev, struct page *page,
				   unsigned long offset, size_t size,
				   enum dma_data_direction dir,
				   unsigned long attrs)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	dma_addr_t iova;
	unsigned long flags;
	phys_addr_t phys_plus_off = page_to_phys(page) + offset;
	phys_addr_t phys_to_map = round_down(phys_plus_off, FAST_PAGE_SIZE);
	unsigned long offset_from_phys_to_map = phys_plus_off & ~FAST_PAGE_MASK;
	size_t len = ALIGN(size + offset_from_phys_to_map, FAST_PAGE_SIZE);
	bool skip_sync = (attrs & DMA_ATTR_SKIP_CPU_SYNC);
	bool is_coherent = is_dma_coherent(dev, attrs);
	int prot = dma_info_to_prot(dir, is_coherent, attrs);

	if (!skip_sync && !is_coherent)
		__fast_dma_page_cpu_to_dev(phys_to_page(phys_to_map),
					   offset_from_phys_to_map, size, dir);

	spin_lock_irqsave(&mapping->lock, flags);

	iova = __fast_smmu_alloc_iova(mapping, attrs, len);

	if (unlikely(iova == DMA_ERROR_CODE))
		goto fail;

	if (unlikely(av8l_fast_map_public(mapping->pgtbl_ops, iova,
					  phys_to_map, len, prot)))
		goto fail_free_iova;

	spin_unlock_irqrestore(&mapping->lock, flags);

	trace_map(mapping->domain, iova, phys_to_map, len, prot);
	return iova + offset_from_phys_to_map;

fail_free_iova:
	__fast_smmu_free_iova(mapping, iova, size);
fail:
	spin_unlock_irqrestore(&mapping->lock, flags);
	return DMA_ERROR_CODE;
}

static void fast_smmu_unmap_page(struct device *dev, dma_addr_t iova,
			       size_t size, enum dma_data_direction dir,
			       unsigned long attrs)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	unsigned long flags;
	unsigned long offset = iova & ~FAST_PAGE_MASK;
	size_t len = ALIGN(size + offset, FAST_PAGE_SIZE);
	bool skip_sync = (attrs & DMA_ATTR_SKIP_CPU_SYNC);
	bool is_coherent = is_dma_coherent(dev, attrs);

	if (!skip_sync && !is_coherent) {
		phys_addr_t phys;

		phys = av8l_fast_iova_to_phys_public(mapping->pgtbl_ops, iova);
		WARN_ON(!phys);

		__fast_dma_page_dev_to_cpu(phys_to_page(phys), offset,
						size, dir);
	}

	spin_lock_irqsave(&mapping->lock, flags);
	av8l_fast_unmap_public(mapping->pgtbl_ops, iova, len);
	__fast_smmu_free_iova(mapping, iova, len);
	spin_unlock_irqrestore(&mapping->lock, flags);

	trace_unmap(mapping->domain, iova - offset, len, len);
}

static void fast_smmu_sync_single_for_cpu(struct device *dev,
		dma_addr_t iova, size_t size, enum dma_data_direction dir)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	unsigned long offset = iova & ~FAST_PAGE_MASK;

	if (!av8l_fast_iova_coherent_public(mapping->pgtbl_ops, iova)) {
		phys_addr_t phys;

		phys = av8l_fast_iova_to_phys_public(mapping->pgtbl_ops, iova);
		WARN_ON(!phys);

		__fast_dma_page_dev_to_cpu(phys_to_page(phys), offset,
						size, dir);
	}
}

static void fast_smmu_sync_single_for_device(struct device *dev,
		dma_addr_t iova, size_t size, enum dma_data_direction dir)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	unsigned long offset = iova & ~FAST_PAGE_MASK;

	if (!av8l_fast_iova_coherent_public(mapping->pgtbl_ops, iova)) {
		phys_addr_t phys;

		phys = av8l_fast_iova_to_phys_public(mapping->pgtbl_ops, iova);
		WARN_ON(!phys);

		__fast_dma_page_cpu_to_dev(phys_to_page(phys), offset,
						size, dir);
	}
}

static void fast_smmu_sync_sg_for_cpu(struct device *dev,
				    struct scatterlist *sgl, int nelems,
				    enum dma_data_direction dir)
{
	struct scatterlist *sg;
	dma_addr_t iova = sg_dma_address(sgl);
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	int i;

	if (av8l_fast_iova_coherent_public(mapping->pgtbl_ops, iova))
		return;

	for_each_sg(sgl, sg, nelems, i)
		__dma_unmap_area(sg_virt(sg), sg->length, dir);
}

static void fast_smmu_sync_sg_for_device(struct device *dev,
				       struct scatterlist *sgl, int nelems,
				       enum dma_data_direction dir)
{
	struct scatterlist *sg;
	dma_addr_t iova = sg_dma_address(sgl);
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	int i;

	if (av8l_fast_iova_coherent_public(mapping->pgtbl_ops, iova))
		return;

	for_each_sg(sgl, sg, nelems, i)
		__dma_map_area(sg_virt(sg), sg->length, dir);
}

static int fast_smmu_map_sg(struct device *dev, struct scatterlist *sg,
			    int nents, enum dma_data_direction dir,
			    unsigned long attrs)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	size_t iova_len;
	bool is_coherent = is_dma_coherent(dev, attrs);
	int prot = dma_info_to_prot(dir, is_coherent, attrs);
	int ret;
	dma_addr_t iova;
	unsigned long flags;
	size_t unused;

	iova_len = iommu_dma_prepare_map_sg(dev, mapping->iovad, sg, nents);

	spin_lock_irqsave(&mapping->lock, flags);
	iova = __fast_smmu_alloc_iova(mapping, attrs, iova_len);
	spin_unlock_irqrestore(&mapping->lock, flags);

	if (unlikely(iova == DMA_ERROR_CODE))
		goto fail;

	av8l_fast_map_sg_public(mapping->pgtbl_ops, iova, sg, nents, prot,
				&unused);

	ret = iommu_dma_finalise_sg(dev, sg, nents, iova);

	if ((attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0)
		fast_smmu_sync_sg_for_device(dev, sg, nents, dir);

	return ret;
fail:
	iommu_dma_invalidate_sg(sg, nents);
	return 0;
}

static void fast_smmu_unmap_sg(struct device *dev,
			       struct scatterlist *sg, int nelems,
			       enum dma_data_direction dir,
			       unsigned long attrs)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	unsigned long flags;
	dma_addr_t start;
	size_t len;
	struct scatterlist *tmp;
	int i;

	if ((attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0)
		fast_smmu_sync_sg_for_cpu(dev, sg, nelems, dir);

	/*
	 * The scatterlist segments are mapped into a single
	 * contiguous IOVA allocation, so this is incredibly easy.
	 */
	start = sg_dma_address(sg);
	for_each_sg(sg_next(sg), tmp, nelems - 1, i) {
		if (sg_dma_len(tmp) == 0)
			break;
		sg = tmp;
	}
	len = sg_dma_address(sg) + sg_dma_len(sg) - start;

	av8l_fast_unmap_public(mapping->pgtbl_ops, start, len);

	spin_lock_irqsave(&mapping->lock, flags);
	__fast_smmu_free_iova(mapping, start, len);
	spin_unlock_irqrestore(&mapping->lock, flags);
}

static void __fast_smmu_free_pages(struct page **pages, int count)
{
	int i;

	if (!pages)
		return;
	for (i = 0; i < count; i++)
		__free_page(pages[i]);
	kvfree(pages);
}

static void *fast_smmu_alloc_atomic(struct dma_fast_smmu_mapping *mapping,
				    size_t size, gfp_t gfp, unsigned long attrs,
				    dma_addr_t *handle, bool coherent)
{
	void *addr;
	unsigned long flags;
	struct page *page;
	dma_addr_t dma_addr;
	int prot = dma_info_to_prot(DMA_BIDIRECTIONAL, coherent, attrs);

	if (coherent) {
		page = alloc_pages(gfp, get_order(size));
		addr = page ? page_address(page) : NULL;
	} else
		addr = __alloc_from_pool(size, &page, gfp);
	if (!addr)
		return NULL;

	spin_lock_irqsave(&mapping->lock, flags);
	dma_addr = __fast_smmu_alloc_iova(mapping, attrs, size);
	if (dma_addr == DMA_ERROR_CODE) {
		dev_err(mapping->dev, "no iova\n");
		spin_unlock_irqrestore(&mapping->lock, flags);
		goto out_free_page;
	}
	if (unlikely(av8l_fast_map_public(mapping->pgtbl_ops, dma_addr,
					  page_to_phys(page), size, prot))) {
		dev_err(mapping->dev, "no map public\n");
		goto out_free_iova;
	}
	spin_unlock_irqrestore(&mapping->lock, flags);
	*handle = dma_addr;
	return addr;

out_free_iova:
	__fast_smmu_free_iova(mapping, dma_addr, size);
	spin_unlock_irqrestore(&mapping->lock, flags);
out_free_page:
	coherent ? __free_pages(page, get_order(size)) :
		   __free_from_pool(addr, size);
	return NULL;
}

static struct page **__fast_smmu_alloc_pages(unsigned int count, gfp_t gfp)
{
	struct page **pages;
	unsigned int i = 0, array_size = count * sizeof(*pages);

	if (array_size <= PAGE_SIZE)
		pages = kzalloc(array_size, GFP_KERNEL);
	else
		pages = vzalloc(array_size);
	if (!pages)
		return NULL;

	/* IOMMU can map any pages, so himem can also be used here */
	gfp |= __GFP_NOWARN | __GFP_HIGHMEM;

	for (i = 0; i < count; ++i) {
		struct page *page = alloc_page(gfp);

		if (!page) {
			__fast_smmu_free_pages(pages, i);
			return NULL;
		}
		pages[i] = page;
	}
	return pages;
}

static void *__fast_smmu_alloc_contiguous(struct device *dev, size_t size,
			dma_addr_t *handle, gfp_t gfp, unsigned long attrs)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	bool is_coherent = is_dma_coherent(dev, attrs);
	int prot = dma_info_to_prot(DMA_BIDIRECTIONAL, is_coherent, attrs);
	pgprot_t remap_prot = __get_dma_pgprot(attrs, PAGE_KERNEL, is_coherent);
	struct page *page;
	dma_addr_t iova;
	unsigned long flags;
	void *coherent_addr;

	page = dma_alloc_from_contiguous(dev, size >> PAGE_SHIFT,
					get_order(size), gfp & __GFP_NOWARN);
	if (!page)
		return NULL;


	spin_lock_irqsave(&mapping->lock, flags);
	iova = __fast_smmu_alloc_iova(mapping, attrs, size);
	spin_unlock_irqrestore(&mapping->lock, flags);
	if (iova == DMA_ERROR_CODE)
		goto release_page;

	if (av8l_fast_map_public(mapping->pgtbl_ops, iova, page_to_phys(page),
				 size, prot))
		goto release_iova;

	coherent_addr = dma_common_contiguous_remap(page, size, VM_USERMAP,
				remap_prot, __fast_smmu_alloc_contiguous);
	if (!coherent_addr)
		goto release_mapping;

	if (!is_coherent)
		__dma_flush_area(page_to_virt(page), size);

	*handle = iova;
	return coherent_addr;

release_mapping:
	av8l_fast_unmap_public(mapping->pgtbl_ops, iova, size);
release_iova:
	__fast_smmu_free_iova(mapping, iova, size);
release_page:
	dma_release_from_contiguous(dev, page, size >> PAGE_SHIFT);
	return NULL;
}

static void *fast_smmu_alloc(struct device *dev, size_t size,
			     dma_addr_t *handle, gfp_t gfp,
			     unsigned long attrs)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	struct sg_table sgt;
	dma_addr_t dma_addr, iova_iter;
	void *addr;
	unsigned long flags;
	struct sg_mapping_iter miter;
	size_t count = ALIGN(size, SZ_4K) >> PAGE_SHIFT;
	bool is_coherent = is_dma_coherent(dev, attrs);
	int prot = dma_info_to_prot(DMA_BIDIRECTIONAL, is_coherent, attrs);
	pgprot_t remap_prot = __get_dma_pgprot(attrs, PAGE_KERNEL, is_coherent);
	struct page **pages;

	/*
	 * sg_alloc_table_from_pages accepts unsigned int value for count
	 * so check count doesn't exceed UINT_MAX.
	 */

	if (count > UINT_MAX) {
		dev_err(dev, "count: %zx exceeds UNIT_MAX\n", count);
		return NULL;
	}

	*handle = DMA_ERROR_CODE;
	size = ALIGN(size, SZ_4K);

	if (atomic_pool && !gfpflags_allow_blocking(gfp))
		return fast_smmu_alloc_atomic(mapping, size, gfp, attrs, handle,
					      is_coherent);
	else if (attrs & DMA_ATTR_FORCE_CONTIGUOUS)
		return __fast_smmu_alloc_contiguous(dev, size, handle, gfp,
						    attrs);

	pages = __fast_smmu_alloc_pages(count, gfp);
	if (!pages) {
		dev_err(dev, "no pages\n");
		return NULL;
	}

	if (sg_alloc_table_from_pages(&sgt, pages, count, 0, size, gfp)) {
		dev_err(dev, "no sg tablen\n");
		goto out_free_pages;
	}

	if (!is_coherent) {
		/*
		 * The CPU-centric flushing implied by SG_MITER_TO_SG isn't
		 * sufficient here, so skip it by using the "wrong" direction.
		 */
		sg_miter_start(&miter, sgt.sgl, sgt.orig_nents,
			       SG_MITER_FROM_SG);
		while (sg_miter_next(&miter))
			__dma_flush_area(miter.addr, miter.length);
		sg_miter_stop(&miter);
	}

	spin_lock_irqsave(&mapping->lock, flags);
	dma_addr = __fast_smmu_alloc_iova(mapping, attrs, size);
	if (dma_addr == DMA_ERROR_CODE) {
		dev_err(dev, "no iova\n");
		spin_unlock_irqrestore(&mapping->lock, flags);
		goto out_free_sg;
	}
	iova_iter = dma_addr;
	sg_miter_start(&miter, sgt.sgl, sgt.orig_nents,
		       SG_MITER_FROM_SG | SG_MITER_ATOMIC);
	while (sg_miter_next(&miter)) {
		if (unlikely(av8l_fast_map_public(
				     mapping->pgtbl_ops, iova_iter,
				     page_to_phys(miter.page),
				     miter.length, prot))) {
			dev_err(dev, "no map public\n");
			/* TODO: unwind previously successful mappings */
			goto out_free_iova;
		}
		iova_iter += miter.length;
	}
	sg_miter_stop(&miter);
	spin_unlock_irqrestore(&mapping->lock, flags);

	addr = dma_common_pages_remap(pages, size, VM_USERMAP, remap_prot,
				      __builtin_return_address(0));
	if (!addr) {
		dev_err(dev, "no common pages\n");
		goto out_unmap;
	}

	*handle = dma_addr;
	sg_free_table(&sgt);
	return addr;

out_unmap:
	/* need to take the lock again for page tables and iova */
	spin_lock_irqsave(&mapping->lock, flags);
	av8l_fast_unmap_public(mapping->pgtbl_ops, dma_addr, size);
out_free_iova:
	__fast_smmu_free_iova(mapping, dma_addr, size);
	spin_unlock_irqrestore(&mapping->lock, flags);
out_free_sg:
	sg_free_table(&sgt);
out_free_pages:
	__fast_smmu_free_pages(pages, count);
	return NULL;
}

static void fast_smmu_free(struct device *dev, size_t size,
			   void *cpu_addr, dma_addr_t dma_handle,
			   unsigned long attrs)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	struct vm_struct *area;
	unsigned long flags;

	size = ALIGN(size, FAST_PAGE_SIZE);

	spin_lock_irqsave(&mapping->lock, flags);
	av8l_fast_unmap_public(mapping->pgtbl_ops, dma_handle, size);
	__fast_smmu_free_iova(mapping, dma_handle, size);
	spin_unlock_irqrestore(&mapping->lock, flags);

	area = find_vm_area(cpu_addr);
	if (area && area->pages) {
		struct page **pages = area->pages;

		dma_common_free_remap(cpu_addr, size, VM_USERMAP, false);
		__fast_smmu_free_pages(pages, size >> FAST_PAGE_SHIFT);
	} else if (attrs & DMA_ATTR_FORCE_CONTIGUOUS) {
		struct page *page = vmalloc_to_page(cpu_addr);

		dma_common_free_remap(cpu_addr, size, VM_USERMAP, false);
		dma_release_from_contiguous(dev, page, size >> PAGE_SHIFT);
	} else if (!is_vmalloc_addr(cpu_addr)) {
		__free_pages(virt_to_page(cpu_addr), get_order(size));
	} else if (__in_atomic_pool(cpu_addr, size)) {
		// Keep remap
		__free_from_pool(cpu_addr, size);
	}
}

/* __swiotlb_mmap_pfn is not currently exported. */
static int fast_smmu_mmap_pfn(struct vm_area_struct *vma, unsigned long pfn,
			     size_t size)
{
	int ret = -ENXIO;
	unsigned long nr_vma_pages = vma_pages(vma);
	unsigned long nr_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned long off = vma->vm_pgoff;

	if (off < nr_pages && nr_vma_pages <= (nr_pages - off)) {
		ret = remap_pfn_range(vma, vma->vm_start, pfn + off,
				      vma->vm_end - vma->vm_start,
				      vma->vm_page_prot);
	}

	return ret;
}

static int fast_smmu_mmap_attrs(struct device *dev, struct vm_area_struct *vma,
				void *cpu_addr, dma_addr_t dma_addr,
				size_t size, unsigned long attrs)
{
	struct vm_struct *area;
	bool coherent = is_dma_coherent(dev, attrs);
	unsigned long pfn = 0;

	vma->vm_page_prot = __get_dma_pgprot(attrs, vma->vm_page_prot,
					     coherent);
	area = find_vm_area(cpu_addr);
	if (area && area->pages)
		return iommu_dma_mmap(area->pages, size, vma);
	else if (attrs & DMA_ATTR_FORCE_CONTIGUOUS)
		pfn = vmalloc_to_pfn(cpu_addr);
	else if (!is_vmalloc_addr(cpu_addr))
		pfn = page_to_pfn(virt_to_page(cpu_addr));
	else if (__in_atomic_pool(cpu_addr, size))
		pfn = __atomic_get_phys(cpu_addr) >> PAGE_SHIFT;


	if (pfn)
		return fast_smmu_mmap_pfn(vma, pfn, size);

	return -EINVAL;
}

static int fast_smmu_get_sgtable(struct device *dev, struct sg_table *sgt,
				void *cpu_addr, dma_addr_t dma_addr,
				size_t size, unsigned long attrs)
{
	unsigned int n_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
	struct vm_struct *area;
	struct page *page = NULL;
	int ret = -ENXIO;

	area = find_vm_area(cpu_addr);
	if (area && area->pages)
		return sg_alloc_table_from_pages(sgt, area->pages, n_pages, 0,
						 size, GFP_KERNEL);
	else if (attrs & DMA_ATTR_FORCE_CONTIGUOUS)
		page = vmalloc_to_page(cpu_addr);
	else if (!is_vmalloc_addr(cpu_addr))
		page = virt_to_page(cpu_addr);
	else if (__in_atomic_pool(cpu_addr, size))
		page = phys_to_page(__atomic_get_phys(cpu_addr));

	if (page) {
		ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
		if (!ret)
			sg_set_page(sgt->sgl, page, PAGE_ALIGN(size), 0);
	}

	return ret;
}

static dma_addr_t fast_smmu_dma_map_resource(
			struct device *dev, phys_addr_t phys_addr,
			size_t size, enum dma_data_direction dir,
			unsigned long attrs)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	size_t offset = phys_addr & ~FAST_PAGE_MASK;
	size_t len = round_up(size + offset, FAST_PAGE_SIZE);
	dma_addr_t dma_addr;
	int prot;
	unsigned long flags;

	spin_lock_irqsave(&mapping->lock, flags);
	dma_addr = __fast_smmu_alloc_iova(mapping, attrs, len);
	spin_unlock_irqrestore(&mapping->lock, flags);

	if (dma_addr == DMA_ERROR_CODE)
		return dma_addr;

	prot = dma_info_to_prot(dir, false, attrs);
	prot |= IOMMU_MMIO;

	if (iommu_map(mapping->domain, dma_addr, phys_addr - offset,
			len, prot)) {
		spin_lock_irqsave(&mapping->lock, flags);
		__fast_smmu_free_iova(mapping, dma_addr, len);
		spin_unlock_irqrestore(&mapping->lock, flags);
		return DMA_ERROR_CODE;
	}
	return dma_addr + offset;
}

static void fast_smmu_dma_unmap_resource(
			struct device *dev, dma_addr_t addr,
			size_t size, enum dma_data_direction dir,
			unsigned long attrs)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	size_t offset = addr & ~FAST_PAGE_MASK;
	size_t len = round_up(size + offset, FAST_PAGE_SIZE);
	unsigned long flags;

	iommu_unmap(mapping->domain, addr - offset, len);
	spin_lock_irqsave(&mapping->lock, flags);
	__fast_smmu_free_iova(mapping, addr, len);
	spin_unlock_irqrestore(&mapping->lock, flags);
}

static int fast_smmu_mapping_error(struct device *dev,
				   dma_addr_t dma_addr)
{
	return dma_addr == DMA_ERROR_CODE;
}

static void __fast_smmu_mapped_over_stale(struct dma_fast_smmu_mapping *fast,
					  void *data)
{
	av8l_fast_iopte *pmds, *ptep = data;
	dma_addr_t iova;
	unsigned long bitmap_idx;
	struct io_pgtable *tbl;

	tbl  = container_of(fast->pgtbl_ops, struct io_pgtable, ops);
	pmds = tbl->cfg.av8l_fast_cfg.pmds;

	bitmap_idx = (unsigned long)(ptep - pmds);
	iova = bitmap_idx << FAST_PAGE_SHIFT;
	dev_err(fast->dev, "Mapped over stale tlb at %pa\n", &iova);
	dev_err(fast->dev, "bitmap (failure at idx %lu):\n", bitmap_idx);
	dev_err(fast->dev, "ptep: %p pmds: %p diff: %lu\n", ptep,
		pmds, bitmap_idx);
	print_hex_dump(KERN_ERR, "bmap: ", DUMP_PREFIX_ADDRESS,
		       32, 8, fast->bitmap, fast->bitmap_size, false);
}

static int fast_smmu_notify(struct notifier_block *self,
			    unsigned long action, void *data)
{
	struct dma_fast_smmu_mapping *fast = container_of(
		self, struct dma_fast_smmu_mapping, notifier);

	switch (action) {
	case MAPPED_OVER_STALE_TLB:
		__fast_smmu_mapped_over_stale(fast, data);
		return NOTIFY_OK;
	default:
		WARN(1, "Unhandled notifier action");
		return NOTIFY_DONE;
	}
}

static const struct dma_map_ops fast_smmu_dma_ops = {
	.alloc = fast_smmu_alloc,
	.free = fast_smmu_free,
	.mmap = fast_smmu_mmap_attrs,
	.get_sgtable = fast_smmu_get_sgtable,
	.map_page = fast_smmu_map_page,
	.unmap_page = fast_smmu_unmap_page,
	.sync_single_for_cpu = fast_smmu_sync_single_for_cpu,
	.sync_single_for_device = fast_smmu_sync_single_for_device,
	.map_sg = fast_smmu_map_sg,
	.unmap_sg = fast_smmu_unmap_sg,
	.sync_sg_for_cpu = fast_smmu_sync_sg_for_cpu,
	.sync_sg_for_device = fast_smmu_sync_sg_for_device,
	.map_resource = fast_smmu_dma_map_resource,
	.unmap_resource = fast_smmu_dma_unmap_resource,
	.mapping_error = fast_smmu_mapping_error,
};

/**
 * __fast_smmu_create_mapping_sized
 * @base: bottom of the VA range
 * @size: size of the VA range in bytes
 *
 * Creates a mapping structure which holds information about used/unused IO
 * address ranges, which is required to perform mapping with IOMMU aware
 * functions. The only VA range supported is [0, 4GB].
 *
 * The client device need to be attached to the mapping with
 * fast_smmu_attach_device function.
 */
static struct dma_fast_smmu_mapping *__fast_smmu_create_mapping_sized(
	dma_addr_t base, u64 size)
{
	struct dma_fast_smmu_mapping *fast;

	fast = kzalloc(sizeof(struct dma_fast_smmu_mapping), GFP_KERNEL);
	if (!fast)
		goto err;

	fast->base = base;
	fast->size = size;
	fast->num_4k_pages = size >> FAST_PAGE_SHIFT;
	fast->bitmap_size = BITS_TO_LONGS(fast->num_4k_pages) * sizeof(long);

	fast->bitmap = kzalloc(fast->bitmap_size, GFP_KERNEL | __GFP_NOWARN |
								__GFP_NORETRY);
	if (!fast->bitmap)
		fast->bitmap = vzalloc(fast->bitmap_size);

	if (!fast->bitmap)
		goto err2;

	spin_lock_init(&fast->lock);

	fast->iovad = kzalloc(sizeof(*fast->iovad), GFP_KERNEL);
	if (!fast->iovad)
		goto err_free_bitmap;
	init_iova_domain(fast->iovad, FAST_PAGE_SIZE,
			base >> FAST_PAGE_SHIFT);

	return fast;

err_free_bitmap:
	kvfree(fast->bitmap);
err2:
	kfree(fast);
err:
	return ERR_PTR(-ENOMEM);
}

/*
 * Based off of similar code from dma-iommu.c, but modified to use a different
 * iova allocator
 */
static void fast_smmu_reserve_pci_windows(struct device *dev,
			    struct dma_fast_smmu_mapping *mapping)
{
	struct pci_host_bridge *bridge;
	struct resource_entry *window;
	phys_addr_t start, end;
	struct pci_dev *pci_dev;
	unsigned long flags;

	if (!dev_is_pci(dev))
		return;

	pci_dev = to_pci_dev(dev);
	bridge = pci_find_host_bridge(pci_dev->bus);

	spin_lock_irqsave(&mapping->lock, flags);
	resource_list_for_each_entry(window, &bridge->windows) {
		if (resource_type(window->res) != IORESOURCE_MEM &&
		    resource_type(window->res) != IORESOURCE_IO)
			continue;

		start = round_down(window->res->start - window->offset,
				FAST_PAGE_SIZE);
		end = round_up(window->res->end - window->offset,
				FAST_PAGE_SIZE);
		start = max_t(unsigned long, mapping->base, start);
		end = min_t(unsigned long, mapping->base + mapping->size, end);
		if (start >= end)
			continue;

		dev_dbg(dev, "iova allocator reserved 0x%pa-0x%pa\n",
				&start, &end);

		start = (start - mapping->base) >> FAST_PAGE_SHIFT;
		end = (end - mapping->base) >> FAST_PAGE_SHIFT;
		bitmap_set(mapping->bitmap, start, end - start);
	}
	spin_unlock_irqrestore(&mapping->lock, flags);
}

void fast_smmu_put_dma_cookie(struct iommu_domain *domain)
{
	struct dma_fast_smmu_mapping *fast = domain->iova_cookie;

	if (!fast)
		return;

	if (fast->iovad) {
		put_iova_domain(fast->iovad);
		kfree(fast->iovad);
	}

	if (fast->bitmap)
		kvfree(fast->bitmap);

	kfree(fast);
	domain->iova_cookie = NULL;
}
EXPORT_SYMBOL_GPL(fast_smmu_put_dma_cookie);

/**
 * fast_smmu_init_mapping
 * @dev: valid struct device pointer
 * @mapping: io address space mapping structure (returned from
 *	arm_iommu_create_mapping)
 *
 * Called the first time a device is attached to this mapping.
 * Not for dma client use.
 */
int fast_smmu_init_mapping(struct device *dev,
			    struct dma_iommu_mapping *mapping)
{
	int err = 0;
	struct iommu_domain *domain = mapping->domain;
	struct iommu_pgtbl_info info;
	u64 size = (u64)mapping->bits << PAGE_SHIFT;
	struct dma_fast_smmu_mapping *fast;

	if (domain->iova_cookie) {
		fast = domain->iova_cookie;
		goto finish;
	}

	if (mapping->base + size > (SZ_1G * 4ULL)) {
		dev_err(dev, "Iova end address too large\n");
		return -EINVAL;
	}

	fast = __fast_smmu_create_mapping_sized(mapping->base, size);
	if (IS_ERR(fast))
		return -ENOMEM;

	fast->domain = domain;
	fast->dev = dev;
	domain->iova_cookie = fast;

	domain->geometry.aperture_start = mapping->base;
	domain->geometry.aperture_end = mapping->base + size - 1;

	if (iommu_domain_get_attr(domain, DOMAIN_ATTR_PGTBL_INFO,
				  &info)) {
		dev_err(dev, "Couldn't get page table info\n");
		err = -EINVAL;
		goto release_mapping;
	}
	fast->pgtbl_ops = (struct io_pgtable_ops *)info.ops;

	fast->notifier.notifier_call = fast_smmu_notify;
	av8l_register_notify(&fast->notifier);

finish:
	fast_smmu_reserve_pci_windows(dev, fast);
	mapping->ops = &fast_smmu_dma_ops;
	return 0;

release_mapping:
	fast_smmu_put_dma_cookie(domain);
	return err;
}
