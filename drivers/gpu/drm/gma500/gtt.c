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
uint32_t psb_gtt_mask_pte(uint32_t pfn, int type)
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

/* Acquires GTT mutex internally. */
void psb_gtt_insert_pages(struct drm_psb_private *pdev, const struct resource *res,
			  struct page **pages)
{
	resource_size_t npages, i;
	u32 __iomem *gtt_slot;
	u32 pte;

	mutex_lock(&pdev->gtt_mutex);

	/* Write our page entries into the GTT itself */

	npages = resource_size(res) >> PAGE_SHIFT;
	gtt_slot = psb_gtt_entry(pdev, res);

	for (i = 0; i < npages; ++i, ++gtt_slot) {
		pte = psb_gtt_mask_pte(page_to_pfn(pages[i]), PSB_MMU_CACHED_MEMORY);
		iowrite32(pte, gtt_slot);
	}

	/* Make sure all the entries are set before we return */
	ioread32(gtt_slot - 1);

	mutex_unlock(&pdev->gtt_mutex);
}

/* Acquires GTT mutex internally. */
void psb_gtt_remove_pages(struct drm_psb_private *pdev, const struct resource *res)
{
	resource_size_t npages, i;
	u32 __iomem *gtt_slot;
	u32 pte;

	mutex_lock(&pdev->gtt_mutex);

	/* Install scratch page for the resource */

	pte = psb_gtt_mask_pte(page_to_pfn(pdev->scratch_page), PSB_MMU_CACHED_MEMORY);

	npages = resource_size(res) >> PAGE_SHIFT;
	gtt_slot = psb_gtt_entry(pdev, res);

	for (i = 0; i < npages; ++i, ++gtt_slot)
		iowrite32(pte, gtt_slot);

	/* Make sure all the entries are set before we return */
	ioread32(gtt_slot - 1);

	mutex_unlock(&pdev->gtt_mutex);
}

static int psb_gtt_enable(struct drm_psb_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	int ret;

	ret = pci_read_config_word(pdev, PSB_GMCH_CTRL, &dev_priv->gmch_ctrl);
	if (ret)
		return pcibios_err_to_errno(ret);
	ret = pci_write_config_word(pdev, PSB_GMCH_CTRL, dev_priv->gmch_ctrl | _PSB_GMCH_ENABLED);
	if (ret)
		return pcibios_err_to_errno(ret);

	dev_priv->pge_ctl = PSB_RVDC32(PSB_PGETBL_CTL);
	PSB_WVDC32(dev_priv->pge_ctl | _PSB_PGETBL_ENABLED, PSB_PGETBL_CTL);

	(void)PSB_RVDC32(PSB_PGETBL_CTL);

	return 0;
}

static void psb_gtt_disable(struct drm_psb_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	pci_write_config_word(pdev, PSB_GMCH_CTRL, dev_priv->gmch_ctrl);
	PSB_WVDC32(dev_priv->pge_ctl, PSB_PGETBL_CTL);

	(void)PSB_RVDC32(PSB_PGETBL_CTL);
}

void psb_gtt_fini(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);

	iounmap(dev_priv->gtt_map);
	psb_gtt_disable(dev_priv);
	mutex_destroy(&dev_priv->gtt_mutex);
}

/* Clear GTT. Use a scratch page to avoid accidents or scribbles. */
static void psb_gtt_clear(struct drm_psb_private *pdev)
{
	resource_size_t pfn_base;
	unsigned long i;
	uint32_t pte;

	pfn_base = page_to_pfn(pdev->scratch_page);
	pte = psb_gtt_mask_pte(pfn_base, PSB_MMU_CACHED_MEMORY);

	for (i = 0; i < pdev->gtt.gtt_pages; ++i)
		iowrite32(pte, pdev->gtt_map + i);

	(void)ioread32(pdev->gtt_map + i - 1);
}

static void psb_gtt_init_ranges(struct drm_psb_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct psb_gtt *pg = &dev_priv->gtt;
	resource_size_t gtt_phys_start, mmu_gatt_start, gtt_start, gtt_pages,
			gatt_start, gatt_pages;
	struct resource *gtt_mem;

	/* The root resource we allocate address space from */
	gtt_phys_start = dev_priv->pge_ctl & PAGE_MASK;

	/*
	 * The video MMU has a HW bug when accessing 0x0d0000000. Make
	 * GATT start at 0x0e0000000. This doesn't actually matter for
	 * us now, but maybe will if the video acceleration ever gets
	 * opened up.
	 */
	mmu_gatt_start = 0xe0000000;

	gtt_start = pci_resource_start(pdev, PSB_GTT_RESOURCE);
	gtt_pages = pci_resource_len(pdev, PSB_GTT_RESOURCE) >> PAGE_SHIFT;

	/* CDV doesn't report this. In which case the system has 64 gtt pages */
	if (!gtt_start || !gtt_pages) {
		dev_dbg(dev->dev, "GTT PCI BAR not initialized.\n");
		gtt_pages = 64;
		gtt_start = dev_priv->pge_ctl;
	}

	gatt_start = pci_resource_start(pdev, PSB_GATT_RESOURCE);
	gatt_pages = pci_resource_len(pdev, PSB_GATT_RESOURCE) >> PAGE_SHIFT;

	if (!gatt_pages || !gatt_start) {
		static struct resource fudge;	/* Preferably peppermint */

		/*
		 * This can occur on CDV systems. Fudge it in this case. We
		 * really don't care what imaginary space is being allocated
		 * at this point.
		 */
		dev_dbg(dev->dev, "GATT PCI BAR not initialized.\n");
		gatt_start = 0x40000000;
		gatt_pages = (128 * 1024 * 1024) >> PAGE_SHIFT;

		/*
		 * This is a little confusing but in fact the GTT is providing
		 * a view from the GPU into memory and not vice versa. As such
		 * this is really allocating space that is not the same as the
		 * CPU address space on CDV.
		 */
		fudge.start = 0x40000000;
		fudge.end = 0x40000000 + 128 * 1024 * 1024 - 1;
		fudge.name = "fudge";
		fudge.flags = IORESOURCE_MEM;

		gtt_mem = &fudge;
	} else {
		gtt_mem = &pdev->resource[PSB_GATT_RESOURCE];
	}

	pg->gtt_phys_start = gtt_phys_start;
	pg->mmu_gatt_start = mmu_gatt_start;
	pg->gtt_start = gtt_start;
	pg->gtt_pages = gtt_pages;
	pg->gatt_start = gatt_start;
	pg->gatt_pages = gatt_pages;
	dev_priv->gtt_mem = gtt_mem;
}

int psb_gtt_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct psb_gtt *pg = &dev_priv->gtt;
	int ret;

	mutex_init(&dev_priv->gtt_mutex);

	ret = psb_gtt_enable(dev_priv);
	if (ret)
		goto err_mutex_destroy;

	psb_gtt_init_ranges(dev_priv);

	dev_priv->gtt_map = ioremap(pg->gtt_phys_start, pg->gtt_pages << PAGE_SHIFT);
	if (!dev_priv->gtt_map) {
		dev_err(dev->dev, "Failure to map gtt.\n");
		ret = -ENOMEM;
		goto err_psb_gtt_disable;
	}

	psb_gtt_clear(dev_priv);

	return 0;

err_psb_gtt_disable:
	psb_gtt_disable(dev_priv);
err_mutex_destroy:
	mutex_destroy(&dev_priv->gtt_mutex);
	return ret;
}

int psb_gtt_resume(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct psb_gtt *pg = &dev_priv->gtt;
	unsigned int old_gtt_pages = pg->gtt_pages;
	int ret;

	/* Enable the GTT */
	ret = psb_gtt_enable(dev_priv);
	if (ret)
		return ret;

	psb_gtt_init_ranges(dev_priv);

	if (old_gtt_pages != pg->gtt_pages) {
		dev_err(dev->dev, "GTT resume error.\n");
		ret = -ENODEV;
		goto err_psb_gtt_disable;
	}

	psb_gtt_clear(dev_priv);

err_psb_gtt_disable:
	psb_gtt_disable(dev_priv);
	return ret;
}
