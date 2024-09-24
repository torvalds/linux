/*
 *
 * (C) COPYRIGHT 2021-2023 Arm Limited.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#include "ethosn_backport.h"

#if (KERNEL_VERSION(5, 10, 0) > LINUX_VERSION_CODE)
struct page *dma_alloc_pages(struct device *dev,
			     size_t size,
			     dma_addr_t *dma_handle,
			     enum dma_data_direction dir,
			     gfp_t gfp)
{
	struct page *page;

	if (size != PAGE_SIZE) {
		dev_dbg(dev,
			"Backport implementation only supports size equal to PAGE_SIZE=%lu\n",
			PAGE_SIZE);

		return NULL;
	}

	page = alloc_page(gfp);
	if (!page)
		return NULL;

	*dma_handle = dma_map_page(dev, page, 0,
				   size,
				   dir);

	return page;
}

void dma_free_pages(struct device *dev,
		    size_t size,
		    struct page *page,
		    dma_addr_t dma_handle,
		    enum dma_data_direction dir)
{
	if (dma_handle)
		dma_unmap_page(dev, dma_handle,
			       size, dir);

	if (page)
		__free_page(page);
}

#endif
void ethosn_iommu_put_domain_for_dev(struct device *dev,
				     struct iommu_domain *domain)
{
#if (KERNEL_VERSION(4, 20, 0) > LINUX_VERSION_CODE)

	return;
#else
	struct iommu_group *group;

	if (!dev)
		return;

	if (!domain)
		return;

	group = iommu_group_get(dev);
	if (!group)
		return;

	iommu_detach_group(domain, group);
	iommu_group_put(group);
	iommu_domain_free(domain);
#endif
}

struct iommu_domain *ethosn_iommu_get_domain_for_dev(struct device *dev)
{
	struct iommu_domain *domain = NULL;

#if (KERNEL_VERSION(4, 20, 0) > LINUX_VERSION_CODE)
	domain = iommu_get_domain_for_dev(dev);
#else
	struct iommu_group *group = iommu_group_get(dev);
	int ret;

	if (group) {
		domain = iommu_domain_alloc(dev->bus);
		if (domain) {
			ret = iommu_attach_group(domain, group);
			if (ret)
				domain = NULL;
		}

		iommu_group_put(group);
	}

#endif

	return domain;
}

int ethosn_bitmap_find_next_zero_area(struct device *dev,
				      void **bitmap,
				      size_t *bits,
				      int nr_pages,
				      unsigned long *start,
				      bool extend_bitmap)
{
#if (KERNEL_VERSION(4, 20, 0) > LINUX_VERSION_CODE)
	*start = bitmap_find_next_zero_area(*bitmap, *bits, 0,
					    nr_pages, 0);
	if (*start > *bits)
		return -ENOMEM;

#else

retry:
	*start = bitmap_find_next_zero_area(*bitmap, *bits, 0,
					    nr_pages, 0);

	if (*start > *bits) {
		size_t bitmap_size = 2 * (*bits / BITS_PER_BYTE);
		void *tmp_bitmap_ptr;

		if (!extend_bitmap)
			return -ENOMEM;

		tmp_bitmap_ptr = devm_kzalloc(dev, bitmap_size, GFP_KERNEL);
		if (!tmp_bitmap_ptr)
			return -ENOMEM;

		bitmap_copy(tmp_bitmap_ptr, *bitmap, *bits);
		*bits = bitmap_size * BITS_PER_BYTE;
		devm_kfree(dev, *bitmap);
		*bitmap = tmp_bitmap_ptr;
		goto retry;
	}

#endif

	return 0;
}

struct sg_table *ethosn_dma_buf_map_attachment(struct dma_buf_attachment
					       *attach)
{
#if (KERNEL_VERSION(5, 3, 0) > LINUX_VERSION_CODE)

	return dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
#else

	return dma_buf_map_attachment(attach, attach->dir);
#endif
}

#if (KERNEL_VERSION(4, 19, 0) > LINUX_VERSION_CODE)
struct device_node *of_get_compatible_child(const struct device_node *parent,
					    const char *compatible)
{
	struct device_node *current_child = NULL;

	for_each_available_child_of_node(parent, current_child) {
		if (of_device_is_compatible(current_child, compatible))
			break;
	}

	return current_child;
}

bool of_node_name_eq(const struct device_node *node,
		     const char *name)
{
	size_t len;

	if (!node || !name)
		return false;

	len = strlen(name);

	if (strncmp(name, node->name, len))
		return false;

	return true;
}

#endif
