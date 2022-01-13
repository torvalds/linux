// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2007, Intel Corporation.
 * All Rights Reserved.
 *
 * Authors: Thomas Hellstrom <thomas-at-tungstengraphics.com>
 *	    Alan Cox <alan@linux.intel.com>
 */

#include "gem.h" /* TODO: for struct psb_gem_object, see psb_gtt_restore() */
#include "psb_drv.h"


/*
 *	GTT resource allocator - manage page mappings in GTT space
 */

int psb_gtt_allocate_resource(struct drm_psb_private *pdev, struct resource *res,
			      const char *name, resource_size_t size, resource_size_t align,
			      bool stolen, u32 *offset)
{
	struct resource *root = pdev->gtt_mem;
	resource_size_t start, end;
	int ret;

	if (stolen) {
		/* The start of the GTT is backed by stolen pages. */
		start = root->start;
		end = root->start + pdev->gtt.stolen_size - 1;
	} else {
		/* The rest is backed by system pages. */
		start = root->start + pdev->gtt.stolen_size;
		end = root->end;
	}

	res->name = name;
	ret = allocate_resource(root, res, size, start, end, align, NULL, NULL);
	if (ret)
		return ret;
	*offset = res->start - root->start;

	return 0;
}

/**
 *	psb_gtt_mask_pte	-	generate GTT pte entry
 *	@pfn: page number to encode
 *	@type: type of memory in the GTT
 *
 *	Set the GTT entry for the appropriate memory type.
 */
static inline uint32_t psb_gtt_mask_pte(uint32_t pfn, int type)
{
	uint32_t mask = PSB_PTE_VALID;

	/* Ensure we explode rather than put an invalid low mapping of
	   a high mapping page into the gtt */
	BUG_ON(pfn & ~(0xFFFFFFFF >> PAGE_SHIFT));

	if (type & PSB_MMU_CACHED_MEMORY)
		mask |= PSB_PTE_CACHED;
	if (type & PSB_MMU_RO_MEMORY)
		mask |= PSB_PTE_RO;
	if (type & PSB_MMU_WO_MEMORY)
		mask |= PSB_PTE_WO;

	return (pfn << PAGE_SHIFT) | mask;
}

static u32 __iomem *psb_gtt_entry(struct drm_psb_private *pdev, const struct resource *res)
{
	unsigned long offset = res->start - pdev->gtt_mem->start;

	return pdev->gtt_map + (offset >> PAGE_SHIFT);
}

/*
 * Take our preallocated GTT range and insert the GEM object into
 * the GTT. This is protected via the gtt mutex which the caller
 * must hold.
 */
void psb_gtt_insert_pages(struct drm_psb_private *pdev, const struct resource *res,
			  struct page **pages)
{
	resource_size_t npages, i;
	u32 __iomem *gtt_slot;
	u32 pte;

	/* Write our page entries into the GTT itself */

	npages = resource_size(res) >> PAGE_SHIFT;
	gtt_slot = psb_gtt_entry(pdev, res);

	for (i = 0; i < npages; ++i, ++gtt_slot) {
		pte = psb_gtt_mask_pte(page_to_pfn(pages[i]), PSB_MMU_CACHED_MEMORY);
		iowrite32(pte, gtt_slot);
	}

	/* Make sure all the entries are set before we return */
	ioread32(gtt_slot - 1);
}

/*
 * Remove a preallocated GTT range from the GTT. Overwrite all the
 * page table entries with the dummy page. This is protected via the gtt
 * mutex which the caller must hold.
 */
void psb_gtt_remove_pages(struct drm_psb_private *pdev, const struct resource *res)
{
	resource_size_t npages, i;
	u32 __iomem *gtt_slot;
	u32 pte;

	/* Install scratch page for the resource */

	pte = psb_gtt_mask_pte(page_to_pfn(pdev->scratch_page), PSB_MMU_CACHED_MEMORY);

	npages = resource_size(res) >> PAGE_SHIFT;
	gtt_slot = psb_gtt_entry(pdev, res);

	for (i = 0; i < npages; ++i, ++gtt_slot)
		iowrite32(pte, gtt_slot);

	/* Make sure all the entries are set before we return */
	ioread32(gtt_slot - 1);
}

static void psb_gtt_alloc(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	init_rwsem(&dev_priv->gtt.sem);
}

void psb_gtt_takedown(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	if (dev_priv->gtt_map) {
		iounmap(dev_priv->gtt_map);
		dev_priv->gtt_map = NULL;
	}
	if (dev_priv->gtt_initialized) {
		pci_write_config_word(pdev, PSB_GMCH_CTRL,
				      dev_priv->gmch_ctrl);
		PSB_WVDC32(dev_priv->pge_ctl, PSB_PGETBL_CTL);
		(void) PSB_RVDC32(PSB_PGETBL_CTL);
	}
	if (dev_priv->vram_addr)
		iounmap(dev_priv->gtt_map);
}

int psb_gtt_init(struct drm_device *dev, int resume)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	unsigned gtt_pages;
	unsigned long stolen_size, vram_stolen_size;
	unsigned i, num_pages;
	unsigned pfn_base;
	struct psb_gtt *pg;

	int ret = 0;
	uint32_t pte;

	if (!resume) {
		mutex_init(&dev_priv->gtt_mutex);
		mutex_init(&dev_priv->mmap_mutex);
		psb_gtt_alloc(dev);
	}

	pg = &dev_priv->gtt;

	/* Enable the GTT */
	pci_read_config_word(pdev, PSB_GMCH_CTRL, &dev_priv->gmch_ctrl);
	pci_write_config_word(pdev, PSB_GMCH_CTRL,
			      dev_priv->gmch_ctrl | _PSB_GMCH_ENABLED);

	dev_priv->pge_ctl = PSB_RVDC32(PSB_PGETBL_CTL);
	PSB_WVDC32(dev_priv->pge_ctl | _PSB_PGETBL_ENABLED, PSB_PGETBL_CTL);
	(void) PSB_RVDC32(PSB_PGETBL_CTL);

	/* The root resource we allocate address space from */
	dev_priv->gtt_initialized = 1;

	pg->gtt_phys_start = dev_priv->pge_ctl & PAGE_MASK;

	/*
	 *	The video mmu has a hw bug when accessing 0x0D0000000.
	 *	Make gatt start at 0x0e000,0000. This doesn't actually
	 *	matter for us but may do if the video acceleration ever
	 *	gets opened up.
	 */
	pg->mmu_gatt_start = 0xE0000000;

	pg->gtt_start = pci_resource_start(pdev, PSB_GTT_RESOURCE);
	gtt_pages = pci_resource_len(pdev, PSB_GTT_RESOURCE)
								>> PAGE_SHIFT;
	/* CDV doesn't report this. In which case the system has 64 gtt pages */
	if (pg->gtt_start == 0 || gtt_pages == 0) {
		dev_dbg(dev->dev, "GTT PCI BAR not initialized.\n");
		gtt_pages = 64;
		pg->gtt_start = dev_priv->pge_ctl;
	}

	pg->gatt_start = pci_resource_start(pdev, PSB_GATT_RESOURCE);
	pg->gatt_pages = pci_resource_len(pdev, PSB_GATT_RESOURCE)
								>> PAGE_SHIFT;
	dev_priv->gtt_mem = &pdev->resource[PSB_GATT_RESOURCE];

	if (pg->gatt_pages == 0 || pg->gatt_start == 0) {
		static struct resource fudge;	/* Preferably peppermint */
		/* This can occur on CDV systems. Fudge it in this case.
		   We really don't care what imaginary space is being allocated
		   at this point */
		dev_dbg(dev->dev, "GATT PCI BAR not initialized.\n");
		pg->gatt_start = 0x40000000;
		pg->gatt_pages = (128 * 1024 * 1024) >> PAGE_SHIFT;
		/* This is a little confusing but in fact the GTT is providing
		   a view from the GPU into memory and not vice versa. As such
		   this is really allocating space that is not the same as the
		   CPU address space on CDV */
		fudge.start = 0x40000000;
		fudge.end = 0x40000000 + 128 * 1024 * 1024 - 1;
		fudge.name = "fudge";
		fudge.flags = IORESOURCE_MEM;
		dev_priv->gtt_mem = &fudge;
	}

	pci_read_config_dword(pdev, PSB_BSM, &dev_priv->stolen_base);
	vram_stolen_size = pg->gtt_phys_start - dev_priv->stolen_base
								- PAGE_SIZE;

	stolen_size = vram_stolen_size;

	dev_dbg(dev->dev, "Stolen memory base 0x%x, size %luK\n",
			dev_priv->stolen_base, vram_stolen_size / 1024);

	if (resume && (gtt_pages != pg->gtt_pages) &&
	    (stolen_size != pg->stolen_size)) {
		dev_err(dev->dev, "GTT resume error.\n");
		ret = -EINVAL;
		goto out_err;
	}

	pg->gtt_pages = gtt_pages;
	pg->stolen_size = stolen_size;
	dev_priv->vram_stolen_size = vram_stolen_size;

	/*
	 *	Map the GTT and the stolen memory area
	 */
	if (!resume)
		dev_priv->gtt_map = ioremap(pg->gtt_phys_start,
						gtt_pages << PAGE_SHIFT);
	if (!dev_priv->gtt_map) {
		dev_err(dev->dev, "Failure to map gtt.\n");
		ret = -ENOMEM;
		goto out_err;
	}

	if (!resume)
		dev_priv->vram_addr = ioremap_wc(dev_priv->stolen_base,
						 stolen_size);

	if (!dev_priv->vram_addr) {
		dev_err(dev->dev, "Failure to map stolen base.\n");
		ret = -ENOMEM;
		goto out_err;
	}

	/*
	 * Insert vram stolen pages into the GTT
	 */

	pfn_base = dev_priv->stolen_base >> PAGE_SHIFT;
	num_pages = vram_stolen_size >> PAGE_SHIFT;
	dev_dbg(dev->dev, "Set up %d stolen pages starting at 0x%08x, GTT offset %dK\n",
		num_pages, pfn_base << PAGE_SHIFT, 0);
	for (i = 0; i < num_pages; ++i) {
		pte = psb_gtt_mask_pte(pfn_base + i, PSB_MMU_CACHED_MEMORY);
		iowrite32(pte, dev_priv->gtt_map + i);
	}

	/*
	 * Init rest of GTT to the scratch page to avoid accidents or scribbles
	 */

	pfn_base = page_to_pfn(dev_priv->scratch_page);
	pte = psb_gtt_mask_pte(pfn_base, PSB_MMU_CACHED_MEMORY);
	for (; i < gtt_pages; ++i)
		iowrite32(pte, dev_priv->gtt_map + i);

	(void) ioread32(dev_priv->gtt_map + i - 1);
	return 0;

out_err:
	psb_gtt_takedown(dev);
	return ret;
}

int psb_gtt_restore(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct resource *r = dev_priv->gtt_mem->child;
	struct psb_gem_object *pobj;
	unsigned int restored = 0, total = 0, size = 0;

	/* On resume, the gtt_mutex is already initialized */
	mutex_lock(&dev_priv->gtt_mutex);
	psb_gtt_init(dev, 1);

	while (r != NULL) {
		/*
		 * TODO: GTT restoration needs a refactoring, so that we don't have to touch
		 *       struct psb_gem_object here. The type represents a GEM object and is
		 *       not related to the GTT itself.
		 */
		pobj = container_of(r, struct psb_gem_object, resource);
		if (pobj->pages) {
			psb_gtt_insert_pages(dev_priv, &pobj->resource, pobj->pages);
			size += pobj->resource.end - pobj->resource.start;
			restored++;
		}
		r = r->sibling;
		total++;
	}
	mutex_unlock(&dev_priv->gtt_mutex);
	DRM_DEBUG_DRIVER("Restored %u of %u gtt ranges (%u KB)", restored,
			 total, (size / 1024));

	return 0;
}
