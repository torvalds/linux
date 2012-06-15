/**************************************************************************
 *
 * Copyright © 2009-2011 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "vmwgfx_drv.h"
#include "drmP.h"
#include "ttm/ttm_bo_driver.h"

#define VMW_PPN_SIZE sizeof(unsigned long)

static int vmw_gmr2_bind(struct vmw_private *dev_priv,
			 struct page *pages[],
			 unsigned long num_pages,
			 int gmr_id)
{
	SVGAFifoCmdDefineGMR2 define_cmd;
	SVGAFifoCmdRemapGMR2 remap_cmd;
	uint32_t define_size = sizeof(define_cmd) + 4;
	uint32_t remap_size = VMW_PPN_SIZE * num_pages + sizeof(remap_cmd) + 4;
	uint32_t *cmd;
	uint32_t *cmd_orig;
	uint32_t i;

	cmd_orig = cmd = vmw_fifo_reserve(dev_priv, define_size + remap_size);
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	define_cmd.gmrId = gmr_id;
	define_cmd.numPages = num_pages;

	remap_cmd.gmrId = gmr_id;
	remap_cmd.flags = (VMW_PPN_SIZE > sizeof(*cmd)) ?
		SVGA_REMAP_GMR2_PPN64 : SVGA_REMAP_GMR2_PPN32;
	remap_cmd.offsetPages = 0;
	remap_cmd.numPages = num_pages;

	*cmd++ = SVGA_CMD_DEFINE_GMR2;
	memcpy(cmd, &define_cmd, sizeof(define_cmd));
	cmd += sizeof(define_cmd) / sizeof(uint32);

	*cmd++ = SVGA_CMD_REMAP_GMR2;
	memcpy(cmd, &remap_cmd, sizeof(remap_cmd));
	cmd += sizeof(remap_cmd) / sizeof(uint32);

	for (i = 0; i < num_pages; ++i) {
		if (VMW_PPN_SIZE <= 4)
			*cmd = page_to_pfn(*pages++);
		else
			*((uint64_t *)cmd) = page_to_pfn(*pages++);

		cmd += VMW_PPN_SIZE / sizeof(*cmd);
	}

	vmw_fifo_commit(dev_priv, define_size + remap_size);

	return 0;
}

static void vmw_gmr2_unbind(struct vmw_private *dev_priv,
			    int gmr_id)
{
	SVGAFifoCmdDefineGMR2 define_cmd;
	uint32_t define_size = sizeof(define_cmd) + 4;
	uint32_t *cmd;

	cmd = vmw_fifo_reserve(dev_priv, define_size);
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("GMR2 unbind failed.\n");
		return;
	}
	define_cmd.gmrId = gmr_id;
	define_cmd.numPages = 0;

	*cmd++ = SVGA_CMD_DEFINE_GMR2;
	memcpy(cmd, &define_cmd, sizeof(define_cmd));

	vmw_fifo_commit(dev_priv, define_size);
}

/**
 * FIXME: Adjust to the ttm lowmem / highmem storage to minimize
 * the number of used descriptors.
 */

static int vmw_gmr_build_descriptors(struct list_head *desc_pages,
				     struct page *pages[],
				     unsigned long num_pages)
{
	struct page *page, *next;
	struct svga_guest_mem_descriptor *page_virtual = NULL;
	struct svga_guest_mem_descriptor *desc_virtual = NULL;
	unsigned int desc_per_page;
	unsigned long prev_pfn;
	unsigned long pfn;
	int ret;

	desc_per_page = PAGE_SIZE /
	    sizeof(struct svga_guest_mem_descriptor) - 1;

	while (likely(num_pages != 0)) {
		page = alloc_page(__GFP_HIGHMEM);
		if (unlikely(page == NULL)) {
			ret = -ENOMEM;
			goto out_err;
		}

		list_add_tail(&page->lru, desc_pages);

		/*
		 * Point previous page terminating descriptor to this
		 * page before unmapping it.
		 */

		if (likely(page_virtual != NULL)) {
			desc_virtual->ppn = page_to_pfn(page);
			kunmap_atomic(page_virtual);
		}

		page_virtual = kmap_atomic(page);
		desc_virtual = page_virtual - 1;
		prev_pfn = ~(0UL);

		while (likely(num_pages != 0)) {
			pfn = page_to_pfn(*pages);

			if (pfn != prev_pfn + 1) {

				if (desc_virtual - page_virtual ==
				    desc_per_page - 1)
					break;

				(++desc_virtual)->ppn = cpu_to_le32(pfn);
				desc_virtual->num_pages = cpu_to_le32(1);
			} else {
				uint32_t tmp =
				    le32_to_cpu(desc_virtual->num_pages);
				desc_virtual->num_pages = cpu_to_le32(tmp + 1);
			}
			prev_pfn = pfn;
			--num_pages;
			++pages;
		}

		(++desc_virtual)->ppn = cpu_to_le32(0);
		desc_virtual->num_pages = cpu_to_le32(0);
	}

	if (likely(page_virtual != NULL))
		kunmap_atomic(page_virtual);

	return 0;
out_err:
	list_for_each_entry_safe(page, next, desc_pages, lru) {
		list_del_init(&page->lru);
		__free_page(page);
	}
	return ret;
}

static inline void vmw_gmr_free_descriptors(struct list_head *desc_pages)
{
	struct page *page, *next;

	list_for_each_entry_safe(page, next, desc_pages, lru) {
		list_del_init(&page->lru);
		__free_page(page);
	}
}

static void vmw_gmr_fire_descriptors(struct vmw_private *dev_priv,
				     int gmr_id, struct list_head *desc_pages)
{
	struct page *page;

	if (unlikely(list_empty(desc_pages)))
		return;

	page = list_entry(desc_pages->next, struct page, lru);

	mutex_lock(&dev_priv->hw_mutex);

	vmw_write(dev_priv, SVGA_REG_GMR_ID, gmr_id);
	wmb();
	vmw_write(dev_priv, SVGA_REG_GMR_DESCRIPTOR, page_to_pfn(page));
	mb();

	mutex_unlock(&dev_priv->hw_mutex);

}

/**
 * FIXME: Adjust to the ttm lowmem / highmem storage to minimize
 * the number of used descriptors.
 */

static unsigned long vmw_gmr_count_descriptors(struct page *pages[],
					unsigned long num_pages)
{
	unsigned long prev_pfn = ~(0UL);
	unsigned long pfn;
	unsigned long descriptors = 0;

	while (num_pages--) {
		pfn = page_to_pfn(*pages++);
		if (prev_pfn + 1 != pfn)
			++descriptors;
		prev_pfn = pfn;
	}

	return descriptors;
}

int vmw_gmr_bind(struct vmw_private *dev_priv,
		 struct page *pages[],
		 unsigned long num_pages,
		 int gmr_id)
{
	struct list_head desc_pages;
	int ret;

	if (likely(dev_priv->capabilities & SVGA_CAP_GMR2))
		return vmw_gmr2_bind(dev_priv, pages, num_pages, gmr_id);

	if (unlikely(!(dev_priv->capabilities & SVGA_CAP_GMR)))
		return -EINVAL;

	if (vmw_gmr_count_descriptors(pages, num_pages) >
	    dev_priv->max_gmr_descriptors)
		return -EINVAL;

	INIT_LIST_HEAD(&desc_pages);

	ret = vmw_gmr_build_descriptors(&desc_pages, pages, num_pages);
	if (unlikely(ret != 0))
		return ret;

	vmw_gmr_fire_descriptors(dev_priv, gmr_id, &desc_pages);
	vmw_gmr_free_descriptors(&desc_pages);

	return 0;
}


void vmw_gmr_unbind(struct vmw_private *dev_priv, int gmr_id)
{
	if (likely(dev_priv->capabilities & SVGA_CAP_GMR2)) {
		vmw_gmr2_unbind(dev_priv, gmr_id);
		return;
	}

	mutex_lock(&dev_priv->hw_mutex);
	vmw_write(dev_priv, SVGA_REG_GMR_ID, gmr_id);
	wmb();
	vmw_write(dev_priv, SVGA_REG_GMR_DESCRIPTOR, 0);
	mb();
	mutex_unlock(&dev_priv->hw_mutex);
}
