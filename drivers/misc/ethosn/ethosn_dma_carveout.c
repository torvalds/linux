/*
 *
 * (C) COPYRIGHT 2018-2023 Arm Limited.
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

#include "ethosn_dma_carveout.h"

#include "ethosn_device.h"

#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/of_address.h>

/* This value was found experimentally by checking the smallest size which
 * the kernel module could successfully load and run a small inference on.
 */
#define CARVEOUT_MIN_SIZE ((resource_size_t)(4 * 1024 * 1024))

/* The NCU MCU provides the lower 29 bits worth of address space and uses
 * the address extension register to provide the upper bits. As we don't
 * currently support changing the address extension register at runtime, the
 * NCU MCU is limited to these 29 bits (512 MB).
 */
#define CARVEOUT_MAX_SIZE ((resource_size_t)(512 * 1024 * 1024))

/* Our current implementation of ethosn_set_addr_ext() means that the lower
 * 29 bits of the carveout base address is ignored, so the address must be
 * aligned otherwise the NPU would write to addresses below the base address.
 */
#define CARVEOUT_ALIGNMENT ((resource_size_t)(512 * 1024 * 1024))

struct ethosn_allocator_internal {
	struct ethosn_dma_sub_allocator allocator;

	struct device_node              *res_mem;
};

static struct ethosn_dma_info *carveout_alloc(
	struct ethosn_dma_sub_allocator *allocator,
	const size_t size,
	gfp_t gfp)
{
	struct ethosn_dma_info *dma_info;
	void *cpu_addr = NULL;
	dma_addr_t dma_addr = 0;

	/* FIXME:- We cannot allocate addresses at different 512MB offsets */
	/* for the different streams. */
	dma_info = devm_kzalloc(allocator->dev,
				sizeof(struct ethosn_dma_info),
				GFP_KERNEL);
	if (!dma_info)
		return ERR_PTR(-ENOMEM);

	if (size) {
		cpu_addr =
			dma_alloc_wc(allocator->dev, size, &dma_addr, gfp);
		if (!cpu_addr) {
			dev_dbg(allocator->dev,
				"failed to dma_alloc %zu bytes\n",
				size);
			devm_kfree(allocator->dev, dma_info);

			return ERR_PTR(-ENOMEM);
		}
	}

	*dma_info = (struct ethosn_dma_info) {
		.size = size,
		.cpu_addr = cpu_addr,
		.iova_addr = dma_addr,
		.imported = false
	};

	return dma_info;
}

static int carveout_map(struct ethosn_dma_sub_allocator *allocator,
			struct ethosn_dma_info *dma_info,
			struct ethosn_dma_prot_range *prot_ranges,
			size_t num_prot_ranges)
{
	return 0;
}

static void carveout_unmap(struct ethosn_dma_sub_allocator *allocator,
			   struct ethosn_dma_info *dma_info)
{}

static void carveout_free(struct ethosn_dma_sub_allocator *allocator,
			  struct ethosn_dma_info **dma_info)
{
	const dma_addr_t dma_addr = (*dma_info)->iova_addr;

	/* FIXME:- We cannot allocate addresses at different 512MB offsets */
	/* for the different streams. */
	if ((*dma_info)->size) {
		/* Clear any data before freeing the memory */
		memset((*dma_info)->cpu_addr, 0U, (*dma_info)->size);
		dma_free_wc(allocator->dev, (*dma_info)->size,
			    (*dma_info)->cpu_addr,
			    dma_addr);
	}

	memset(*dma_info, 0, sizeof(struct ethosn_dma_info));
	devm_kfree(allocator->dev, *dma_info);
	/* Clear the caller's pointer, so they aren't left with it dangling */
	*dma_info = (struct ethosn_dma_info *)NULL;
}

static void carveout_sync_for_device(struct ethosn_dma_sub_allocator *allocator,
				     struct ethosn_dma_info *dma_info)
{}

static void carveout_sync_for_cpu(struct ethosn_dma_sub_allocator *allocator,
				  struct ethosn_dma_info *dma_info)
{}

static int carveout_mmap(struct ethosn_dma_sub_allocator *allocator,
			 struct vm_area_struct *const vma,
			 const struct ethosn_dma_info *const dma_info)
{
	const size_t size = dma_info->size;
	void *const cpu_addr = dma_info->cpu_addr;
	const dma_addr_t dma_addr = dma_info->iova_addr;

	const dma_addr_t mmap_addr =
		((dma_addr >> PAGE_SHIFT) + vma->vm_pgoff) << PAGE_SHIFT;

	int ret;

	ret = dma_mmap_wc(allocator->dev, vma, cpu_addr, dma_addr, size);

	if (ret)
		dev_warn(allocator->dev,
			 "Failed to DMA map buffer. handle=0x%pK, addr=0x%llx, size=%lu\n",
			 dma_info, mmap_addr, vma->vm_end - vma->vm_start);
	else
		dev_dbg(allocator->dev,
			"DMA map. handle=0x%pK, addr=0x%llx, start=0x%lx, size=%lu\n",
			dma_info, mmap_addr, vma->vm_start,
			vma->vm_end - vma->vm_start);

	return ret;
}

static dma_addr_t carveout_get_addr_base(
	struct ethosn_dma_sub_allocator *_allocator,
	enum ethosn_stream_type stream_type)
{
	struct ethosn_allocator_internal *allocator =
		container_of(_allocator, typeof(*allocator), allocator);
	struct resource r;

	if (!allocator->res_mem)
		return 0;

	if (of_address_to_resource(allocator->res_mem, 0, &r))
		return 0;
	else
		return r.start;
}

static resource_size_t carveout_get_addr_size(
	struct ethosn_dma_sub_allocator *_allocator,
	enum ethosn_stream_type stream_type)
{
	struct ethosn_allocator_internal *allocator =
		container_of(_allocator, typeof(*allocator), allocator);
	struct resource r;

	if (!allocator->res_mem)
		return 0;

	if (of_address_to_resource(allocator->res_mem, 0, &r))
		return 0;
	else
		return resource_size(&r);
}

static void carveout_allocator_destroy(
	struct ethosn_dma_sub_allocator *allocator)
{
	struct device *dev;

	if (!allocator)
		return;

	dev = allocator->dev;

	memset(allocator, 0, sizeof(struct ethosn_dma_sub_allocator));
	devm_kfree(dev, allocator);
}

struct ethosn_dma_sub_allocator *ethosn_dma_carveout_allocator_create(
	struct device *dev)
{
	static struct ethosn_dma_allocator_ops ops = {
		.destroy         = carveout_allocator_destroy,
		.alloc           = carveout_alloc,
		.map             = carveout_map,
		.unmap           = carveout_unmap,
		.free            = carveout_free,
		.sync_for_device = carveout_sync_for_device,
		.sync_for_cpu    = carveout_sync_for_cpu,
		.mmap            = carveout_mmap,
		.get_addr_base   = carveout_get_addr_base,
		.get_addr_size   = carveout_get_addr_size,
	};
	struct ethosn_allocator_internal *allocator;
	struct device_node *res_mem;
	struct resource res_mem_details;
	resource_size_t carveout_size;

#ifdef ETHOSN_TZMP1
	dev_err(dev,
		"Carveout is not allowed when kernel module is built with TZMP1 support\n");

	return ERR_PTR(-EINVAL);
#endif

	/* Iterates backwards device tree to find a memory-region phandle */
	do {
		res_mem = of_parse_phandle(dev->of_node, "memory-region", 0);
		if (res_mem)
			break;

		/* TODO: Check if parent is null in case of reaching root
		 * Maybe check against dev->bus->dev_root to make sure root node
		 * is reached
		 */
		dev = dev->parent;
	} while (dev);

	if (!res_mem)
		return ERR_PTR(-EINVAL);

	/* Print the details of the carveout region for debugging */
	if (of_address_to_resource(res_mem, 0, &res_mem_details))
		return ERR_PTR(-EINVAL);

	carveout_size = resource_size(&res_mem_details);

	dev_dbg(dev,
		"Allocating carveout memory region start=%#llx, size=%#llx.\n",
		res_mem_details.start, carveout_size);

	/* Validate that the memory region configured by the integrator meets
	 * our requirements.
	 */
	if (res_mem_details.start % CARVEOUT_ALIGNMENT != 0) {
		dev_err(dev,
			"Carveout memory region at %#llx has incorrect alignment. It must be aligned to a %#llx boundary.\n",
			res_mem_details.start, CARVEOUT_ALIGNMENT);

		return ERR_PTR(-EINVAL);
	}

	if (carveout_size < CARVEOUT_MIN_SIZE) {
		dev_err(dev,
			"Carveout memory region with size %#llx is too small. It must be no smaller than %#llx.\n",
			carveout_size, CARVEOUT_MIN_SIZE);

		return ERR_PTR(-EINVAL);
	}

	if (carveout_size > CARVEOUT_MAX_SIZE) {
		dev_err(dev,
			"Carveout memory region with size %#llx is too large. It must be no larger than %#llx.\n",
			carveout_size, CARVEOUT_MAX_SIZE);

		return ERR_PTR(-EINVAL);
	}

	if (!is_power_of_2(carveout_size)) {
		/* This is a restriction of the NCU MCU MPU.
		 */
		dev_err(dev,
			"Carveout memory region size %#llx is not a power-of-two. It must be a power-of-two.\n",
			carveout_size);

		return ERR_PTR(-EINVAL);
	}

	allocator = devm_kzalloc(dev,
				 sizeof(struct ethosn_allocator_internal),
				 GFP_KERNEL);

	if (!allocator)
		return ERR_PTR(-ENOMEM);

	allocator->res_mem = res_mem;
	allocator->allocator.dev = dev;
	allocator->allocator.ops = &ops;
	allocator->allocator.smmu_stream_id = 0;

	return &allocator->allocator;
}
