/*
 *
 * (C) COPYRIGHT 2018, 2020 ARM Limited. All rights reserved.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#ifndef _KBASE_HWCNT_GPU_H_
#define _KBASE_HWCNT_GPU_H_

#include <linux/types.h>

struct kbase_device;
struct kbase_hwcnt_metadata;
struct kbase_hwcnt_enable_map;
struct kbase_hwcnt_dump_buffer;

/**
 * enum kbase_hwcnt_gpu_group_type - GPU hardware counter group types, used to
 *                                   identify metadata groups.
 * @KBASE_HWCNT_GPU_GROUP_TYPE_V5: GPU V5 group type.
 */
enum kbase_hwcnt_gpu_group_type {
	KBASE_HWCNT_GPU_GROUP_TYPE_V5 = 0x10,
};

/**
 * enum kbase_hwcnt_gpu_v5_block_type - GPU V5 hardware counter block types,
 *                                      used to identify metadata blocks.
 * @KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_JM:      Job Manager block.
 * @KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER:   Tiler block.
 * @KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC:      Shader Core block.
 * @KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC2:     Secondary Shader Core block.
 * @KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS:  Memsys block.
 * @KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS2: Secondary Memsys block.
 */
enum kbase_hwcnt_gpu_v5_block_type {
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_JM = 0x40,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC2,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS2,
};

/**
 * struct kbase_hwcnt_physical_enable_map - Representation of enable map
 *                                          directly used by GPU.
 * @fe_bm:     Front end (JM/CSHW) counters selection bitmask.
 * @shader_bm: Shader counters selection bitmask.
 * @tiler_bm:  Tiler counters selection bitmask.
 * @mmu_l2_bm: MMU_L2 counters selection bitmask.
 */
struct kbase_hwcnt_physical_enable_map {
	u32 fe_bm;
	u32 shader_bm;
	u32 tiler_bm;
	u32 mmu_l2_bm;
};

/**
 * struct kbase_hwcnt_gpu_v5_info - Information about hwcnt blocks on v5 GPUs.
 * @l2_count:   L2 cache count.
 * @core_mask:  Shader core mask. May be sparse.
 * @clk_cnt:    Number of clock domains available.
 */
struct kbase_hwcnt_gpu_v5_info {
	size_t l2_count;
	u64 core_mask;
	u8 clk_cnt;
};

/**
 * struct kbase_hwcnt_gpu_info - Tagged union with information about the current
 *                               GPU's hwcnt blocks.
 * @type: GPU type.
 * @v5:   Info filled in if a v5 GPU.
 */
struct kbase_hwcnt_gpu_info {
	enum kbase_hwcnt_gpu_group_type type;
	struct kbase_hwcnt_gpu_v5_info v5;
};

/**
 * kbase_hwcnt_gpu_info_init() - Initialise an info structure used to create the
 *                               hwcnt metadata.
 * @kbdev: Non-NULL pointer to kbase device.
 * @info:  Non-NULL pointer to data structure to be filled in.
 *
 * The initialised info struct will only be valid for use while kbdev is valid.
 */
int kbase_hwcnt_gpu_info_init(
	struct kbase_device *kbdev,
	struct kbase_hwcnt_gpu_info *info);

/**
 * kbase_hwcnt_gpu_metadata_create() - Create hardware counter metadata for the
 *                                     current GPU.
 * @info:           Non-NULL pointer to info struct initialised by
 *                  kbase_hwcnt_gpu_info_init.
 * @use_secondary:  True if secondary performance counters should be used, else
 *                  false. Ignored if secondary counters are not supported.
 * @out_metadata:   Non-NULL pointer to where created metadata is stored on
 *                  success.
 * @out_dump_bytes: Non-NULL pointer to where the size of the GPU counter dump
 *                  buffer is stored on success.
 *
 * Return: 0 on success, else error code.
 */
int kbase_hwcnt_gpu_metadata_create(
	const struct kbase_hwcnt_gpu_info *info,
	bool use_secondary,
	const struct kbase_hwcnt_metadata **out_metadata,
	size_t *out_dump_bytes);

/**
 * kbase_hwcnt_gpu_metadata_destroy() - Destroy GPU hardware counter metadata.
 * @metadata: Pointer to metadata to destroy.
 */
void kbase_hwcnt_gpu_metadata_destroy(
	const struct kbase_hwcnt_metadata *metadata);

/**
 * kbase_hwcnt_gpu_dump_get() - Copy or accumulate enabled counters from the raw
 *                              dump buffer in src into the dump buffer
 *                              abstraction in dst.
 * @dst:            Non-NULL pointer to dst dump buffer.
 * @src:            Non-NULL pointer to src raw dump buffer, of same length
 *                  as returned in out_dump_bytes parameter of
 *                  kbase_hwcnt_gpu_metadata_create.
 * @dst_enable_map: Non-NULL pointer to enable map specifying enabled values.
 * @pm_core_mask:   PM state synchronized shaders core mask with the dump.
 * @accumulate:     True if counters in src should be accumulated into dst,
 *                  rather than copied.
 *
 * The dst and dst_enable_map MUST have been created from the same metadata as
 * returned from the call to kbase_hwcnt_gpu_metadata_create as was used to get
 * the length of src.
 *
 * Return: 0 on success, else error code.
 */
int kbase_hwcnt_gpu_dump_get(
	struct kbase_hwcnt_dump_buffer *dst,
	void *src,
	const struct kbase_hwcnt_enable_map *dst_enable_map,
	const u64 pm_core_mask,
	bool accumulate);

/**
 * kbase_hwcnt_gpu_enable_map_to_physical() - Convert an enable map abstraction
 *                                            into a physical enable map.
 * @dst: Non-NULL pointer to dst physical enable map.
 * @src: Non-NULL pointer to src enable map abstraction.
 *
 * The src must have been created from a metadata returned from a call to
 * kbase_hwcnt_gpu_metadata_create.
 *
 * This is a lossy conversion, as the enable map abstraction has one bit per
 * individual counter block value, but the physical enable map uses 1 bit for
 * every 4 counters, shared over all instances of a block.
 */
void kbase_hwcnt_gpu_enable_map_to_physical(
	struct kbase_hwcnt_physical_enable_map *dst,
	const struct kbase_hwcnt_enable_map *src);

/**
 * kbase_hwcnt_gpu_enable_map_from_physical() - Convert a physical enable map to
 *                                              an enable map abstraction.
 * @dst: Non-NULL pointer to dst enable map abstraction.
 * @src: Non-NULL pointer to src physical enable map.
 *
 * The dst must have been created from a metadata returned from a call to
 * kbase_hwcnt_gpu_metadata_create.
 *
 * This is a lossy conversion, as the physical enable map can technically
 * support counter blocks with 128 counters each, but no hardware actually uses
 * more than 64, so the enable map abstraction has nowhere to store the enable
 * information for the 64 non-existent counters.
 */
void kbase_hwcnt_gpu_enable_map_from_physical(
	struct kbase_hwcnt_enable_map *dst,
	const struct kbase_hwcnt_physical_enable_map *src);

/**
 * kbase_hwcnt_gpu_patch_dump_headers() - Patch all the performance counter
 *                                        enable headers in a dump buffer to
 *                                        reflect the specified enable map.
 * @buf:        Non-NULL pointer to dump buffer to patch.
 * @enable_map: Non-NULL pointer to enable map.
 *
 * The buf and enable_map must have been created from a metadata returned from
 * a call to kbase_hwcnt_gpu_metadata_create.
 *
 * This function should be used before handing off a dump buffer over the
 * kernel-user boundary, to ensure the header is accurate for the enable map
 * used by the user.
 */
void kbase_hwcnt_gpu_patch_dump_headers(
	struct kbase_hwcnt_dump_buffer *buf,
	const struct kbase_hwcnt_enable_map *enable_map);

#endif /* _KBASE_HWCNT_GPU_H_ */
