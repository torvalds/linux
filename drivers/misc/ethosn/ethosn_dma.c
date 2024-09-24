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

#include "ethosn_dma.h"

#include "ethosn_device.h"
#include "ethosn_dma_carveout.h"
#include "ethosn_dma_iommu.h"

#include <linux/iommu.h>

static const struct ethosn_dma_allocator_ops *get_ops(
	struct ethosn_dma_sub_allocator *sub_allocator)
{
	if (sub_allocator)
		return sub_allocator->ops;
	else
		return NULL;
}

static struct ethosn_dma_sub_allocator **ethosn_get_sub_allocator_ref(
	struct ethosn_dma_allocator *top_allocator,
	enum ethosn_stream_type stream_type)
{
	if (!top_allocator)
		return NULL;

	BUG_ON(stream_type < ETHOSN_STREAM_FIRMWARE ||
	       stream_type > ETHOSN_STREAM_INTERMEDIATE_BUFFER);

	BUG_ON(top_allocator->type >= ETHOSN_ALLOCATOR_INVALID ||
	       top_allocator->type < ETHOSN_ALLOCATOR_MAIN);

	switch (top_allocator->type) {
	case ETHOSN_ALLOCATOR_MAIN: {
		struct ethosn_main_allocator *main_alloc = container_of(
			top_allocator, struct ethosn_main_allocator,
			allocator);
		switch (stream_type) {
		case ETHOSN_STREAM_FIRMWARE:
		case ETHOSN_STREAM_PLE_CODE:

			return &main_alloc->firmware;
		case ETHOSN_STREAM_WORKING_DATA:

			return &main_alloc->working_data;
		default:

			return NULL;
		}
	}
	case ETHOSN_ALLOCATOR_ASSET: {
		struct ethosn_asset_allocator *asset_alloc = container_of(
			top_allocator, struct ethosn_asset_allocator,
			allocator);
		switch (stream_type) {
		case ETHOSN_STREAM_COMMAND_STREAM:

			return &asset_alloc->command_stream;
		case ETHOSN_STREAM_WEIGHT_DATA:

			return &asset_alloc->weight_data;
		case ETHOSN_STREAM_IO_BUFFER:

			return &asset_alloc->io_buffer;
		case ETHOSN_STREAM_INTERMEDIATE_BUFFER:

			return &asset_alloc->intermediate_buffer;
		default:

			return NULL;
		}
	}
	case ETHOSN_ALLOCATOR_CARVEOUT: {
		struct ethosn_carveout_allocator *carveout_alloc = container_of(
			top_allocator, struct ethosn_carveout_allocator,
			allocator);

		return &carveout_alloc->carveout;
	}
	default:

		return NULL;
	}
}

struct ethosn_dma_sub_allocator *ethosn_get_sub_allocator(
	struct ethosn_dma_allocator *top_allocator,
	enum ethosn_stream_type stream_type)
{
	struct ethosn_dma_sub_allocator **ref = ethosn_get_sub_allocator_ref(
		top_allocator, stream_type);

	if (!ref)
		return NULL;

	if (!*ref)
		return NULL;

	return *ref;
}

struct ethosn_dma_allocator *ethosn_dma_top_allocator_create(struct device *dev,
							     enum
							     ethosn_alloc_type
							     type)
{
	switch (type) {
	case ETHOSN_ALLOCATOR_MAIN: {
		struct ethosn_main_allocator *main_alloc;

		main_alloc =
			devm_kzalloc(dev, sizeof(struct ethosn_main_allocator),
				     GFP_KERNEL);
		if (!main_alloc)
			return ERR_PTR(-ENOMEM);

		main_alloc->allocator.type = ETHOSN_ALLOCATOR_MAIN;

		return &main_alloc->allocator;
	}
	case ETHOSN_ALLOCATOR_ASSET: {
		struct ethosn_asset_allocator *asset_alloc;

		asset_alloc =
			devm_kzalloc(dev, sizeof(struct ethosn_asset_allocator),
				     GFP_KERNEL);
		if (!asset_alloc)
			return ERR_PTR(-ENOMEM);

		asset_alloc->allocator.type = ETHOSN_ALLOCATOR_ASSET;

		return &asset_alloc->allocator;
	}
	case ETHOSN_ALLOCATOR_CARVEOUT: {
		struct ethosn_carveout_allocator *carveout_alloc;

		carveout_alloc =
			devm_kzalloc(dev,
				     sizeof(struct ethosn_carveout_allocator),
				     GFP_KERNEL);
		if (!carveout_alloc)
			return ERR_PTR(-ENOMEM);

		carveout_alloc->allocator.type = ETHOSN_ALLOCATOR_CARVEOUT;

		return &carveout_alloc->allocator;
	}
	default:

		return ERR_PTR(-EINVAL);
	}
}

int ethosn_dma_top_allocator_destroy(struct device *dev,
				     struct ethosn_dma_allocator
				     **top_allocator)
{
	if (!top_allocator)
		return 0;

	*top_allocator = NULL;

	dev_info(dev, "top_allocator destroyed");

	return 0;
}

int ethosn_dma_sub_allocator_create(struct device *dev,
				    struct ethosn_dma_allocator *top_allocator,
				    enum ethosn_stream_type stream_type,
				    dma_addr_t addr_base,
				    phys_addr_t speculative_page_addr,
				    bool is_smmu_available)
{
	struct ethosn_dma_sub_allocator **allocator;

	if (!top_allocator)
		return -EINVAL;

	allocator = ethosn_get_sub_allocator_ref(top_allocator, stream_type);
	if (!allocator)
		return -EINVAL;

	/*
	 * Address base only needed for SMMU because carveout will parse it from
	 * the device tree.
	 */
	if (is_smmu_available)
		*allocator =
			ethosn_dma_iommu_allocator_create(dev, stream_type,
							  addr_base,
							  speculative_page_addr);

	else
		*allocator = ethosn_dma_carveout_allocator_create(dev);

	if (IS_ERR(*allocator))
		return PTR_ERR(*allocator);

	return 0;
}

void ethosn_dma_sub_allocator_destroy(
	struct ethosn_dma_allocator *top_allocator,
	enum ethosn_stream_type stream_type)
{
	struct ethosn_dma_sub_allocator **sub_allocator;
	const struct ethosn_dma_allocator_ops *ops;

	sub_allocator =
		ethosn_get_sub_allocator_ref(top_allocator, stream_type);

	ops = sub_allocator ? get_ops(*sub_allocator) : NULL;

	if (!ops)
		return;

	ops->destroy(*sub_allocator);

	*sub_allocator = NULL;
}

struct ethosn_dma_info *ethosn_dma_alloc(
	struct ethosn_dma_allocator *top_allocator,
	const size_t size,
	enum ethosn_stream_type stream_type,
	gfp_t gfp,
	const char *debug_tag)
{
	struct ethosn_dma_sub_allocator *sub_allocator =
		ethosn_get_sub_allocator(top_allocator, stream_type);
	const struct ethosn_dma_allocator_ops *ops;
	struct ethosn_dma_info *dma_info = NULL;

	if (!sub_allocator)
		goto exit;

	ops = sub_allocator->ops;
	if (!ops)
		goto exit;

	if (size == 0) {
		dev_err(sub_allocator->dev,
			"Cannot allocate zero bytes for %s\n",
			debug_tag == NULL ? "(unknown)" : debug_tag);
		goto exit;
	}

	dma_info = ops->alloc(sub_allocator, size, gfp);

	if (IS_ERR_OR_NULL(dma_info)) {
		dev_err(sub_allocator->dev,
			"failed to dma_alloc %zu bytes for %s\n",
			size, debug_tag == NULL ? "(unknown)" : debug_tag);
		goto exit;
	}

	dma_info->stream_type = stream_type;

	dev_dbg(sub_allocator->dev,
		"DMA alloc for %s. handle=0x%pK, cpu_addr=0x%pK, size=%zu (0x%zx)\n",
		debug_tag == NULL ? "(unknown)" : debug_tag,
		dma_info, dma_info->cpu_addr, size, size);

	/* Zero the memory. This ensures the previous contents of the
	 * memory doesn't affect us (if the same physical memory
	 * is re-used). This means we get deterministic results in
	 * cases where parts of an intermediate buffer are read
	 * before being written.
	 */
	memset(dma_info->cpu_addr, 0, size);
	ops->sync_for_device(sub_allocator, dma_info);

exit:

	return dma_info;
}

struct ethosn_dma_info *ethosn_dma_firmware_from_protected(
	struct ethosn_dma_allocator *top_allocator,
	phys_addr_t start_addr,
	size_t size)
{
	struct ethosn_dma_sub_allocator *sub_allocator;
	const struct ethosn_dma_allocator_ops *ops;
	struct ethosn_dma_info *dma_info;

	sub_allocator = ethosn_get_sub_allocator(top_allocator,
						 ETHOSN_STREAM_FIRMWARE);
	if (!sub_allocator)
		return ERR_PTR(-EINVAL);

	ops = sub_allocator->ops;
	if (!ops)
		return ERR_PTR(-EINVAL);

	if (!ops->from_protected)
		return ERR_PTR(-EINVAL);

	dma_info = ops->from_protected(sub_allocator, start_addr, size);
	if (IS_ERR(dma_info)) {
		dev_err(sub_allocator->dev,
			"Failed to use protected memory addr=%pa size=%zu for firmware\n",
			&start_addr, size);
		goto error;
	}

	dma_info->stream_type = ETHOSN_STREAM_FIRMWARE;

error:

	return dma_info;
}

int ethosn_dma_map(struct ethosn_dma_allocator *top_allocator,
		   struct ethosn_dma_info *dma_info,
		   int prot)
{
	struct ethosn_dma_prot_range prot_range;

	if (IS_ERR_OR_NULL(dma_info))
		return -EINVAL;

	/* Forward to ethosn_dma_map_with_prot_ranges, specifying a
	 * single prot range that covers the whole allocation.
	 */
	prot_range.start = 0;
	prot_range.end = dma_info->size;
	prot_range.prot = prot;

	return ethosn_dma_map_with_prot_ranges(top_allocator, dma_info,
					       &prot_range, 1);
}

int ethosn_dma_map_with_prot_ranges(struct ethosn_dma_allocator *top_allocator,
				    struct ethosn_dma_info *dma_info,
				    struct ethosn_dma_prot_range *prot_ranges,
				    size_t num_prot_ranges)
{
	struct ethosn_dma_sub_allocator *sub_allocator;
	const struct ethosn_dma_allocator_ops *ops;
	int ret = -EINVAL;
	size_t end_of_prev_prot_range;
	size_t i;

	if (IS_ERR_OR_NULL(dma_info))
		goto exit;

	sub_allocator = ethosn_get_sub_allocator(top_allocator,
						 dma_info->stream_type);

	if (!sub_allocator)
		goto exit;

	/* Validate prot_ranges.
	 *   - Must be contiguous and cover the whole allocation.
	 *   - Must be aligned to page boundaries.
	 *   - Must be non-zero size
	 */
	end_of_prev_prot_range = 0;
	for (i = 0; i < num_prot_ranges; ++i) {
		const struct ethosn_dma_prot_range *const prot_range =
			&prot_ranges[i];

		if (prot_range->start != end_of_prev_prot_range) {
			dev_err(sub_allocator->dev,
				"Prot range %zu is non-contiguous.\n", i);

			return -EINVAL;
		}

		if (!PAGE_ALIGNED(prot_range->start)) {
			dev_err(sub_allocator->dev,
				"Prot range %zu is not aligned to PAGE_SIZE.\n",
				i);

			return -EINVAL;
		}

		if (prot_range->end - prot_range->start == 0) {
			dev_err(sub_allocator->dev,
				"Prot range %zu is zero size.\n", i);

			return -EINVAL;
		}

		if (prot_range->prot <= 0) {
			dev_err(sub_allocator->dev,
				"Prot value %d in prot range %zu is invalid.\n",
				prot_range->prot, i);

			return -EINVAL;
		}

		end_of_prev_prot_range = prot_range->end;
	}

	if (end_of_prev_prot_range != dma_info->size) {
		dev_err(sub_allocator->dev,
			"Final prot range does not cover remaining allocation.\n");

		return -EINVAL;
	}

	ops = sub_allocator->ops;
	if (!ops)
		goto exit;

	if (!ops->map)
		goto exit;

	ret = ops->map(sub_allocator, dma_info, prot_ranges, num_prot_ranges);

	if (ret)
		dev_err(sub_allocator->dev, "failed mapping dma on stream %d\n",
			dma_info->stream_type);
	else
		dev_dbg(sub_allocator->dev,
			"DMA mapped. handle=0x%pK, iova=0x%llx, prot=0x%x (+%zu others), stream=%u\n",
			dma_info, dma_info->iova_addr, prot_ranges[0].prot,
			num_prot_ranges - 1,
			dma_info->stream_type);

exit:

	return ret;
}

void ethosn_dma_unmap(struct ethosn_dma_allocator *top_allocator,
		      struct ethosn_dma_info *const dma_info)
{
	struct ethosn_dma_sub_allocator *sub_allocator;
	const struct ethosn_dma_allocator_ops *ops;

	if (IS_ERR_OR_NULL(dma_info))
		return;

	sub_allocator = ethosn_get_sub_allocator(top_allocator,
						 dma_info->stream_type);

	if (!sub_allocator)
		return;

	ops = sub_allocator->ops;
	if (!ops)
		return;

	if (!ops->unmap)
		return;

	ops->unmap(sub_allocator, dma_info);
}

struct ethosn_dma_info *ethosn_dma_alloc_and_map(
	struct ethosn_dma_allocator *top_allocator,
	const size_t size,
	int prot,
	enum ethosn_stream_type stream_type,
	gfp_t gfp,
	const char *debug_tag)
{
	struct ethosn_dma_sub_allocator *sub_allocator =
		ethosn_get_sub_allocator(top_allocator, stream_type);
	struct ethosn_dma_info *dma_info = NULL;
	int ret;

	if (!sub_allocator)
		return NULL;

	dma_info = ethosn_dma_alloc(top_allocator, size, stream_type, gfp,
				    debug_tag);
	if (IS_ERR_OR_NULL(dma_info))
		goto exit;

	ret = ethosn_dma_map(top_allocator, dma_info, prot);
	if (ret < 0) {
		dev_err(sub_allocator->dev, "failed to map stream %u\n",
			stream_type);
		goto exit_free_dma_info;
	}

exit:

	return dma_info;

exit_free_dma_info:
	ethosn_dma_release(top_allocator, &dma_info);

	return NULL;
}

void ethosn_dma_unmap_and_release(struct ethosn_dma_allocator *top_allocator,
				  struct ethosn_dma_info **dma_info)
{
	ethosn_dma_unmap(top_allocator, *dma_info);
	ethosn_dma_release(top_allocator, dma_info);
}

struct ethosn_dma_info *ethosn_dma_import(
	struct ethosn_dma_allocator *top_allocator,
	int fd,
	size_t size,
	enum ethosn_stream_type stream_type)
{
	struct ethosn_dma_sub_allocator *sub_allocator =
		ethosn_get_sub_allocator(top_allocator, stream_type);
	const struct ethosn_dma_allocator_ops *ops;
	struct ethosn_dma_info *dma_info = NULL;

	if (!sub_allocator)
		goto exit;

	ops = sub_allocator->ops;
	if (!ops)
		goto exit;

	dma_info = ops->import(sub_allocator, fd, size);

	if (IS_ERR_OR_NULL(dma_info)) {
		dev_err(sub_allocator->dev, "failed to dma_import %zu bytes\n",
			size);
		goto exit;
	}

	dma_info->stream_type = stream_type;

	dev_dbg(sub_allocator->dev,
		"DMA import. handle=0x%pK, cpu_addr=0x%pK, size=%zu\n",
		dma_info, dma_info->cpu_addr, dma_info->size);

exit:

	return dma_info;
}

void ethosn_dma_release(struct ethosn_dma_allocator *top_allocator,
			struct ethosn_dma_info **dma_info)
{
	struct ethosn_dma_sub_allocator *sub_allocator;
	const struct ethosn_dma_allocator_ops *ops;

	if (IS_ERR_OR_NULL(dma_info))
		return;

	if (IS_ERR_OR_NULL(*dma_info))
		return;

	sub_allocator = ethosn_get_sub_allocator(top_allocator,
						 (*dma_info)->stream_type);

	if (!sub_allocator)
		return;

	ops = sub_allocator->ops;
	if (!ops)
		return;

	if ((*dma_info)->imported)
		ops->release(sub_allocator, dma_info);
	else
		ops->free(sub_allocator, dma_info);
}

int ethosn_dma_mmap(struct ethosn_dma_allocator *top_allocator,
		    struct vm_area_struct *const vma,
		    const struct ethosn_dma_info *const dma_info)
{
	struct ethosn_dma_sub_allocator *sub_allocator;
	const struct ethosn_dma_allocator_ops *ops;
	int ret = -EINVAL;

	if (IS_ERR_OR_NULL(dma_info)) {
		dev_err(top_allocator->dev,
			"%s: Invalid dma_info pointer\n", __func__);
		goto exit;
	}

	sub_allocator = ethosn_get_sub_allocator(top_allocator,
						 dma_info->stream_type);

	if (!sub_allocator) {
		dev_err(top_allocator->dev,
			"%s: Invalid sub_allocator\n", __func__);
		goto exit;
	}

	ops = sub_allocator->ops;
	if (!ops) {
		dev_err(top_allocator->dev, "%s: Invalid ops\n", __func__);
		goto exit;
	}

	if (!ops->mmap) {
		dev_err(top_allocator->dev, "%s: Invalid mmap\n", __func__);
		goto exit;
	}

	return ops->mmap(sub_allocator, vma, dma_info);

exit:

	return ret;
}

resource_size_t ethosn_dma_get_addr_size(
	struct ethosn_dma_allocator *top_allocator,
	enum ethosn_stream_type stream_type)
{
	struct ethosn_dma_sub_allocator *sub_allocator =
		ethosn_get_sub_allocator(top_allocator, stream_type);
	const struct ethosn_dma_allocator_ops *ops;
	int ret = -EINVAL;

	if (!sub_allocator)
		goto exit;

	ops = sub_allocator->ops;
	if (!ops)
		goto exit;

	if (!ops->get_addr_size)
		goto exit;

	return ops->get_addr_size(sub_allocator, stream_type);

exit:

	return ret;
}

dma_addr_t ethosn_dma_get_addr_base(struct ethosn_dma_allocator *top_allocator,
				    enum ethosn_stream_type stream_type)
{
	struct ethosn_dma_sub_allocator *sub_allocator =
		ethosn_get_sub_allocator(top_allocator, stream_type);
	const struct ethosn_dma_allocator_ops *ops;
	int ret = -EINVAL;

	if (!sub_allocator)
		goto exit;

	ops = sub_allocator->ops;
	if (!ops)
		goto exit;

	if (!ops->get_addr_base)
		goto exit;

	return ops->get_addr_base(sub_allocator, stream_type);

exit:

	return ret;
}

void ethosn_dma_sync_for_device(struct ethosn_dma_allocator *top_allocator,
				struct ethosn_dma_info *dma_info)
{
	struct ethosn_dma_sub_allocator *sub_allocator;
	const struct ethosn_dma_allocator_ops *ops;

	if (IS_ERR_OR_NULL(dma_info))
		return;

	sub_allocator = ethosn_get_sub_allocator(top_allocator,
						 dma_info->stream_type);

	if (!sub_allocator)
		return;

	ops = sub_allocator->ops;
	if (!ops)
		return;

	if (!ops->sync_for_device)
		return;

	ops->sync_for_device(sub_allocator, dma_info);
}

void ethosn_dma_sync_for_cpu(struct ethosn_dma_allocator *top_allocator,
			     struct ethosn_dma_info *dma_info)
{
	struct ethosn_dma_sub_allocator *sub_allocator;
	const struct ethosn_dma_allocator_ops *ops;

	if (IS_ERR_OR_NULL(dma_info))
		return;

	sub_allocator = ethosn_get_sub_allocator(top_allocator,
						 dma_info->stream_type);

	if (!sub_allocator)
		return;

	ops = sub_allocator->ops;
	if (!ops)
		return;

	if (!ops->sync_for_cpu)
		return;

	ops->sync_for_cpu(sub_allocator, dma_info);
}

/* Exported for use by test module */
EXPORT_SYMBOL(ethosn_dma_sync_for_cpu);
