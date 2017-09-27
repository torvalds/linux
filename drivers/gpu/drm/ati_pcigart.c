/**
 * \file ati_pcigart.c
 * ATI PCI GART support
 *
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Wed Dec 13 21:52:19 2000 by gareth@valinux.com
 *
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/export.h>
#include <drm/drmP.h>

#include <drm/ati_pcigart.h>

# define ATI_PCIGART_PAGE_SIZE		4096	/**< PCI GART page size */

static int drm_ati_alloc_pcigart_table(struct drm_device *dev,
				       struct drm_ati_pcigart_info *gart_info)
{
	gart_info->table_handle = drm_pci_alloc(dev, gart_info->table_size,
						PAGE_SIZE);
	if (gart_info->table_handle == NULL)
		return -ENOMEM;

	return 0;
}

static void drm_ati_free_pcigart_table(struct drm_device *dev,
				       struct drm_ati_pcigart_info *gart_info)
{
	drm_pci_free(dev, gart_info->table_handle);
	gart_info->table_handle = NULL;
}

int drm_ati_pcigart_cleanup(struct drm_device *dev, struct drm_ati_pcigart_info *gart_info)
{
	struct drm_sg_mem *entry = dev->sg;
	unsigned long pages;
	int i;
	int max_pages;

	/* we need to support large memory configurations */
	if (!entry) {
		DRM_ERROR("no scatter/gather memory!\n");
		return 0;
	}

	if (gart_info->bus_addr) {

		max_pages = (gart_info->table_size / sizeof(u32));
		pages = (entry->pages <= max_pages)
		  ? entry->pages : max_pages;

		for (i = 0; i < pages; i++) {
			if (!entry->busaddr[i])
				break;
			pci_unmap_page(dev->pdev, entry->busaddr[i],
					 PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
		}

		if (gart_info->gart_table_location == DRM_ATI_GART_MAIN)
			gart_info->bus_addr = 0;
	}

	if (gart_info->gart_table_location == DRM_ATI_GART_MAIN &&
	    gart_info->table_handle) {
		drm_ati_free_pcigart_table(dev, gart_info);
	}

	return 1;
}
EXPORT_SYMBOL(drm_ati_pcigart_cleanup);

int drm_ati_pcigart_init(struct drm_device *dev, struct drm_ati_pcigart_info *gart_info)
{
	struct drm_local_map *map = &gart_info->mapping;
	struct drm_sg_mem *entry = dev->sg;
	void *address = NULL;
	unsigned long pages;
	u32 *pci_gart = NULL, page_base, gart_idx;
	dma_addr_t bus_address = 0;
	int i, j, ret = 0;
	int max_ati_pages, max_real_pages;

	if (!entry) {
		DRM_ERROR("no scatter/gather memory!\n");
		goto done;
	}

	if (gart_info->gart_table_location == DRM_ATI_GART_MAIN) {
		DRM_DEBUG("PCI: no table in VRAM: using normal RAM\n");

		if (pci_set_dma_mask(dev->pdev, gart_info->table_mask)) {
			DRM_ERROR("fail to set dma mask to 0x%Lx\n",
				  (unsigned long long)gart_info->table_mask);
			ret = 1;
			goto done;
		}

		ret = drm_ati_alloc_pcigart_table(dev, gart_info);
		if (ret) {
			DRM_ERROR("cannot allocate PCI GART page!\n");
			goto done;
		}

		pci_gart = gart_info->table_handle->vaddr;
		address = gart_info->table_handle->vaddr;
		bus_address = gart_info->table_handle->busaddr;
	} else {
		address = gart_info->addr;
		bus_address = gart_info->bus_addr;
		DRM_DEBUG("PCI: Gart Table: VRAM %08LX mapped at %08lX\n",
			  (unsigned long long)bus_address,
			  (unsigned long)address);
	}


	max_ati_pages = (gart_info->table_size / sizeof(u32));
	max_real_pages = max_ati_pages / (PAGE_SIZE / ATI_PCIGART_PAGE_SIZE);
	pages = (entry->pages <= max_real_pages)
	    ? entry->pages : max_real_pages;

	if (gart_info->gart_table_location == DRM_ATI_GART_MAIN) {
		memset(pci_gart, 0, max_ati_pages * sizeof(u32));
	} else {
		memset_io((void __iomem *)map->handle, 0, max_ati_pages * sizeof(u32));
	}

	gart_idx = 0;
	for (i = 0; i < pages; i++) {
		/* we need to support large memory configurations */
		entry->busaddr[i] = pci_map_page(dev->pdev, entry->pagelist[i],
						 0, PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
		if (pci_dma_mapping_error(dev->pdev, entry->busaddr[i])) {
			DRM_ERROR("unable to map PCIGART pages!\n");
			drm_ati_pcigart_cleanup(dev, gart_info);
			address = NULL;
			bus_address = 0;
			goto done;
		}
		page_base = (u32) entry->busaddr[i];

		for (j = 0; j < (PAGE_SIZE / ATI_PCIGART_PAGE_SIZE); j++) {
			u32 val;

			switch(gart_info->gart_reg_if) {
			case DRM_ATI_GART_IGP:
				val = page_base | 0xc;
				break;
			case DRM_ATI_GART_PCIE:
				val = (page_base >> 8) | 0xc;
				break;
			default:
			case DRM_ATI_GART_PCI:
				val = page_base;
				break;
			}
			if (gart_info->gart_table_location ==
			    DRM_ATI_GART_MAIN)
				pci_gart[gart_idx] = cpu_to_le32(val);
			else
				DRM_WRITE32(map, gart_idx * sizeof(u32), val);
			gart_idx++;
			page_base += ATI_PCIGART_PAGE_SIZE;
		}
	}
	ret = 1;

#if defined(__i386__) || defined(__x86_64__)
	wbinvd();
#else
	mb();
#endif

      done:
	gart_info->addr = address;
	gart_info->bus_addr = bus_address;
	return ret;
}
EXPORT_SYMBOL(drm_ati_pcigart_init);
