// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2019-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
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
 */

#include "mali_kbase_csf_protected_memory.h"
#include <linux/protected_memory_allocator.h>

#if IS_ENABLED(CONFIG_OF)
#include <linux/of_platform.h>
#endif

int kbase_csf_protected_memory_init(struct kbase_device *const kbdev)
{
	int err = 0;

#if IS_ENABLED(CONFIG_OF)
	struct device_node *pma_node = of_parse_phandle(kbdev->dev->of_node,
					"protected-memory-allocator", 0);
	if (!pma_node) {
		dev_info(kbdev->dev, "Protected memory allocator not available\n");
	} else {
		struct platform_device *const pdev =
				of_find_device_by_node(pma_node);

		kbdev->csf.pma_dev = NULL;
		if (!pdev) {
			dev_err(kbdev->dev, "Platform device for Protected memory allocator not found\n");
		} else {
			kbdev->csf.pma_dev = platform_get_drvdata(pdev);
			if (!kbdev->csf.pma_dev) {
				dev_info(kbdev->dev, "Protected memory allocator is not ready\n");
				err = -EPROBE_DEFER;
			} else if (!try_module_get(kbdev->csf.pma_dev->owner)) {
				dev_err(kbdev->dev, "Failed to get Protected memory allocator module\n");
				err = -ENODEV;
			} else {
				dev_info(kbdev->dev, "Protected memory allocator successfully loaded\n");
			}
		}
		of_node_put(pma_node);
	}
#endif

	return err;
}

void kbase_csf_protected_memory_term(struct kbase_device *const kbdev)
{
	if (kbdev->csf.pma_dev)
		module_put(kbdev->csf.pma_dev->owner);
}

struct protected_memory_allocation **
		kbase_csf_protected_memory_alloc(
		struct kbase_device *const kbdev,
		struct tagged_addr *phys,
		size_t num_pages)
{
	size_t i;
	struct protected_memory_allocator_device *pma_dev =
		kbdev->csf.pma_dev;
	struct protected_memory_allocation **pma =
		kmalloc_array(num_pages, sizeof(*pma), GFP_KERNEL);

	if (WARN_ON(!pma_dev) || WARN_ON(!phys) || !pma)
		return NULL;

	for (i = 0; i < num_pages; i++) {
		pma[i] = pma_dev->ops.pma_alloc_page(pma_dev,
				KBASE_MEM_POOL_4KB_PAGE_TABLE_ORDER);
		if (!pma[i])
			break;

		phys[i] = as_tagged(pma_dev->ops.pma_get_phys_addr(pma_dev,
					pma[i]));
	}

	if (i != num_pages) {
		kbase_csf_protected_memory_free(kbdev, pma, i);
		return NULL;
	}

	return pma;
}

void kbase_csf_protected_memory_free(
		struct kbase_device *const kbdev,
		struct protected_memory_allocation **pma,
		size_t num_pages)
{
	size_t i;
	struct protected_memory_allocator_device *pma_dev =
		kbdev->csf.pma_dev;

	if (WARN_ON(!pma_dev) || WARN_ON(!pma))
		return;

	for (i = 0; i < num_pages; i++)
		pma_dev->ops.pma_free_page(pma_dev, pma[i]);

	kfree(pma);
}
