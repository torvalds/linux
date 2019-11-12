/*
 *
 * (C) COPYRIGHT 2018-2019 ARM Limited. All rights reserved.
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

#include "mali_kbase_hwcnt_gpu.h"
#include "mali_kbase_hwcnt_types.h"
#include "mali_kbase.h"
#ifdef CONFIG_MALI_BIFROST_NO_MALI
#include "backend/gpu/mali_kbase_model_dummy.h"
#endif

#define KBASE_HWCNT_V4_BLOCKS_PER_GROUP 8
#define KBASE_HWCNT_V4_SC_BLOCKS_PER_GROUP 4
#define KBASE_HWCNT_V4_MAX_GROUPS \
	(KBASE_HWCNT_AVAIL_MASK_BITS / KBASE_HWCNT_V4_BLOCKS_PER_GROUP)
#define KBASE_HWCNT_V4_HEADERS_PER_BLOCK 4
#define KBASE_HWCNT_V4_COUNTERS_PER_BLOCK 60
#define KBASE_HWCNT_V4_VALUES_PER_BLOCK \
	(KBASE_HWCNT_V4_HEADERS_PER_BLOCK + KBASE_HWCNT_V4_COUNTERS_PER_BLOCK)
/* Index of the PRFCNT_EN header into a V4 counter block */
#define KBASE_HWCNT_V4_PRFCNT_EN_HEADER 2

#define KBASE_HWCNT_V5_BLOCK_TYPE_COUNT 4
#define KBASE_HWCNT_V5_HEADERS_PER_BLOCK 4
#define KBASE_HWCNT_V5_COUNTERS_PER_BLOCK 60
#define KBASE_HWCNT_V5_VALUES_PER_BLOCK \
	(KBASE_HWCNT_V5_HEADERS_PER_BLOCK + KBASE_HWCNT_V5_COUNTERS_PER_BLOCK)
/* Index of the PRFCNT_EN header into a V5 counter block */
#define KBASE_HWCNT_V5_PRFCNT_EN_HEADER 2

/**
 * kbasep_hwcnt_backend_gpu_metadata_v4_create() - Create hardware counter
 *                                                 metadata for a v4 GPU.
 * @v4_info:  Non-NULL pointer to hwcnt info for a v4 GPU.
 * @metadata: Non-NULL pointer to where created metadata is stored on success.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_hwcnt_backend_gpu_metadata_v4_create(
	const struct kbase_hwcnt_gpu_v4_info *v4_info,
	const struct kbase_hwcnt_metadata **metadata)
{
	size_t grp;
	int errcode = -ENOMEM;
	struct kbase_hwcnt_description desc;
	struct kbase_hwcnt_group_description *grps;
	size_t avail_mask_bit;

	WARN_ON(!v4_info);
	WARN_ON(!metadata);

	/* Check if there are enough bits in the availability mask to represent
	 * all the hardware counter blocks in the system.
	 */
	if (v4_info->cg_count > KBASE_HWCNT_V4_MAX_GROUPS)
		return -EINVAL;

	grps = kcalloc(v4_info->cg_count, sizeof(*grps), GFP_KERNEL);
	if (!grps)
		goto clean_up;

	desc.grp_cnt = v4_info->cg_count;
	desc.grps = grps;

	for (grp = 0; grp < v4_info->cg_count; grp++) {
		size_t blk;
		size_t sc;
		const u64 core_mask = v4_info->cgs[grp].core_mask;
		struct kbase_hwcnt_block_description *blks = kcalloc(
			KBASE_HWCNT_V4_BLOCKS_PER_GROUP,
			sizeof(*blks),
			GFP_KERNEL);

		if (!blks)
			goto clean_up;

		grps[grp].type = KBASE_HWCNT_GPU_GROUP_TYPE_V4;
		grps[grp].blk_cnt = KBASE_HWCNT_V4_BLOCKS_PER_GROUP;
		grps[grp].blks = blks;

		for (blk = 0; blk < KBASE_HWCNT_V4_BLOCKS_PER_GROUP; blk++) {
			blks[blk].inst_cnt = 1;
			blks[blk].hdr_cnt =
				KBASE_HWCNT_V4_HEADERS_PER_BLOCK;
			blks[blk].ctr_cnt =
				KBASE_HWCNT_V4_COUNTERS_PER_BLOCK;
		}

		for (sc = 0; sc < KBASE_HWCNT_V4_SC_BLOCKS_PER_GROUP; sc++) {
			blks[sc].type = core_mask & (1ull << sc) ?
				KBASE_HWCNT_GPU_V4_BLOCK_TYPE_SHADER :
				KBASE_HWCNT_GPU_V4_BLOCK_TYPE_RESERVED;
		}

		blks[4].type = KBASE_HWCNT_GPU_V4_BLOCK_TYPE_TILER;
		blks[5].type = KBASE_HWCNT_GPU_V4_BLOCK_TYPE_MMU_L2;
		blks[6].type = KBASE_HWCNT_GPU_V4_BLOCK_TYPE_RESERVED;
		blks[7].type = (grp == 0) ?
			KBASE_HWCNT_GPU_V4_BLOCK_TYPE_JM :
			KBASE_HWCNT_GPU_V4_BLOCK_TYPE_RESERVED;

		WARN_ON(KBASE_HWCNT_V4_BLOCKS_PER_GROUP != 8);
	}

	/* Initialise the availability mask */
	desc.avail_mask = 0;
	avail_mask_bit = 0;

	for (grp = 0; grp < desc.grp_cnt; grp++) {
		size_t blk;
		const struct kbase_hwcnt_block_description *blks =
			desc.grps[grp].blks;
		for (blk = 0; blk < desc.grps[grp].blk_cnt; blk++) {
			WARN_ON(blks[blk].inst_cnt != 1);
			if (blks[blk].type !=
			    KBASE_HWCNT_GPU_V4_BLOCK_TYPE_RESERVED)
				desc.avail_mask |= (1ull << avail_mask_bit);

			avail_mask_bit++;
		}
	}

	errcode = kbase_hwcnt_metadata_create(&desc, metadata);

	/* Always clean up, as metadata will make a copy of the input args */
clean_up:
	if (grps) {
		for (grp = 0; grp < v4_info->cg_count; grp++)
			kfree(grps[grp].blks);
		kfree(grps);
	}
	return errcode;
}

/**
 * kbasep_hwcnt_backend_gpu_v4_dump_bytes() - Get the raw dump buffer size for a
 *                                            V4 GPU.
 * @v4_info: Non-NULL pointer to hwcnt info for a v4 GPU.
 *
 * Return: Size of buffer the V4 GPU needs to perform a counter dump.
 */
static size_t kbasep_hwcnt_backend_gpu_v4_dump_bytes(
	const struct kbase_hwcnt_gpu_v4_info *v4_info)
{
	return v4_info->cg_count *
		KBASE_HWCNT_V4_BLOCKS_PER_GROUP *
		KBASE_HWCNT_V4_VALUES_PER_BLOCK *
		KBASE_HWCNT_VALUE_BYTES;
}

/**
 * kbasep_hwcnt_backend_gpu_metadata_v5_create() - Create hardware counter
 *                                                 metadata for a v5 GPU.
 * @v5_info:       Non-NULL pointer to hwcnt info for a v5 GPU.
 * @use_secondary: True if secondary performance counters should be used, else
 *                 false. Ignored if secondary counters are not supported.
 * @metadata:      Non-NULL pointer to where created metadata is stored
 *                 on success.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_hwcnt_backend_gpu_metadata_v5_create(
	const struct kbase_hwcnt_gpu_v5_info *v5_info,
	bool use_secondary,
	const struct kbase_hwcnt_metadata **metadata)
{
	struct kbase_hwcnt_description desc;
	struct kbase_hwcnt_group_description group;
	struct kbase_hwcnt_block_description
		blks[KBASE_HWCNT_V5_BLOCK_TYPE_COUNT];
	size_t non_sc_block_count;
	size_t sc_block_count;

	WARN_ON(!v5_info);
	WARN_ON(!metadata);

	/* Calculate number of block instances that aren't shader cores */
	non_sc_block_count = 2 + v5_info->l2_count;
	/* Calculate number of block instances that are shader cores */
	sc_block_count = fls64(v5_info->core_mask);

	/*
	 * A system can have up to 64 shader cores, but the 64-bit
	 * availability mask can't physically represent that many cores as well
	 * as the other hardware blocks.
	 * Error out if there are more blocks than our implementation can
	 * support.
	 */
	if ((sc_block_count + non_sc_block_count) > KBASE_HWCNT_AVAIL_MASK_BITS)
		return -EINVAL;

	/* One Job Manager block */
	blks[0].type = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_JM;
	blks[0].inst_cnt = 1;
	blks[0].hdr_cnt = KBASE_HWCNT_V5_HEADERS_PER_BLOCK;
	blks[0].ctr_cnt = KBASE_HWCNT_V5_COUNTERS_PER_BLOCK;

	/* One Tiler block */
	blks[1].type = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER;
	blks[1].inst_cnt = 1;
	blks[1].hdr_cnt = KBASE_HWCNT_V5_HEADERS_PER_BLOCK;
	blks[1].ctr_cnt = KBASE_HWCNT_V5_COUNTERS_PER_BLOCK;

	/* l2_count memsys blks */
	blks[2].type = use_secondary ?
		KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS2 :
		KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS;
	blks[2].inst_cnt = v5_info->l2_count;
	blks[2].hdr_cnt = KBASE_HWCNT_V5_HEADERS_PER_BLOCK;
	blks[2].ctr_cnt = KBASE_HWCNT_V5_COUNTERS_PER_BLOCK;

	/*
	 * There are as many shader cores in the system as there are bits set in
	 * the core mask. However, the dump buffer memory requirements need to
	 * take into account the fact that the core mask may be non-contiguous.
	 *
	 * For example, a system with a core mask of 0b1011 has the same dump
	 * buffer memory requirements as a system with 0b1111, but requires more
	 * memory than a system with 0b0111. However, core 2 of the system with
	 * 0b1011 doesn't physically exist, and the dump buffer memory that
	 * accounts for that core will never be written to when we do a counter
	 * dump.
	 *
	 * We find the core mask's last set bit to determine the memory
	 * requirements, and embed the core mask into the availability mask so
	 * we can determine later which shader cores physically exist.
	 */
	blks[3].type = use_secondary ?
		KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC2 :
		KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC;
	blks[3].inst_cnt = sc_block_count;
	blks[3].hdr_cnt = KBASE_HWCNT_V5_HEADERS_PER_BLOCK;
	blks[3].ctr_cnt = KBASE_HWCNT_V5_COUNTERS_PER_BLOCK;

	WARN_ON(KBASE_HWCNT_V5_BLOCK_TYPE_COUNT != 4);

	group.type = KBASE_HWCNT_GPU_GROUP_TYPE_V5;
	group.blk_cnt = KBASE_HWCNT_V5_BLOCK_TYPE_COUNT;
	group.blks = blks;

	desc.grp_cnt = 1;
	desc.grps = &group;

	/* The JM, Tiler, and L2s are always available, and are before cores */
	desc.avail_mask = (1ull << non_sc_block_count) - 1;
	/* Embed the core mask directly in the availability mask */
	desc.avail_mask |= (v5_info->core_mask << non_sc_block_count);

	return kbase_hwcnt_metadata_create(&desc, metadata);
}

/**
 * kbasep_hwcnt_backend_gpu_v5_dump_bytes() - Get the raw dump buffer size for a
 *                                            V5 GPU.
 * @v5_info: Non-NULL pointer to hwcnt info for a v5 GPU.
 *
 * Return: Size of buffer the V5 GPU needs to perform a counter dump.
 */
static size_t kbasep_hwcnt_backend_gpu_v5_dump_bytes(
	const struct kbase_hwcnt_gpu_v5_info *v5_info)
{
	WARN_ON(!v5_info);
	return (2 + v5_info->l2_count + fls64(v5_info->core_mask)) *
		KBASE_HWCNT_V5_VALUES_PER_BLOCK *
		KBASE_HWCNT_VALUE_BYTES;
}

int kbase_hwcnt_gpu_info_init(
	struct kbase_device *kbdev,
	struct kbase_hwcnt_gpu_info *info)
{
	if (!kbdev || !info)
		return -EINVAL;

#ifdef CONFIG_MALI_BIFROST_NO_MALI
	/* NO_MALI uses V5 layout, regardless of the underlying platform. */
	info->type = KBASE_HWCNT_GPU_GROUP_TYPE_V5;
	info->v5.l2_count = KBASE_DUMMY_MODEL_MAX_MEMSYS_BLOCKS;
	info->v5.core_mask = (1ull << KBASE_DUMMY_MODEL_MAX_SHADER_CORES) - 1;
#else
	{
		const struct base_gpu_props *props = &kbdev->gpu_props.props;
		const size_t l2_count = props->l2_props.num_l2_slices;
		const size_t core_mask =
			props->coherency_info.group[0].core_mask;

		info->type = KBASE_HWCNT_GPU_GROUP_TYPE_V5;
		info->v5.l2_count = l2_count;
		info->v5.core_mask = core_mask;
	}
#endif
	return 0;
}

int kbase_hwcnt_gpu_metadata_create(
	const struct kbase_hwcnt_gpu_info *info,
	bool use_secondary,
	const struct kbase_hwcnt_metadata **out_metadata,
	size_t *out_dump_bytes)
{
	int errcode;
	const struct kbase_hwcnt_metadata *metadata;
	size_t dump_bytes;

	if (!info || !out_metadata || !out_dump_bytes)
		return -EINVAL;

	switch (info->type) {
	case KBASE_HWCNT_GPU_GROUP_TYPE_V4:
		dump_bytes = kbasep_hwcnt_backend_gpu_v4_dump_bytes(&info->v4);
		errcode = kbasep_hwcnt_backend_gpu_metadata_v4_create(
			&info->v4, &metadata);
		break;
	case KBASE_HWCNT_GPU_GROUP_TYPE_V5:
		dump_bytes = kbasep_hwcnt_backend_gpu_v5_dump_bytes(&info->v5);
		errcode = kbasep_hwcnt_backend_gpu_metadata_v5_create(
			&info->v5, use_secondary, &metadata);
		break;
	default:
		return -EINVAL;
	}
	if (errcode)
		return errcode;

	/*
	 * Dump abstraction size should be exactly the same size and layout as
	 * the physical dump size, for backwards compatibility.
	 */
	WARN_ON(dump_bytes != metadata->dump_buf_bytes);

	*out_metadata = metadata;
	*out_dump_bytes = dump_bytes;

	return 0;
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_gpu_metadata_create);

void kbase_hwcnt_gpu_metadata_destroy(
	const struct kbase_hwcnt_metadata *metadata)
{
	if (!metadata)
		return;

	kbase_hwcnt_metadata_destroy(metadata);
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_gpu_metadata_destroy);

static bool is_block_type_shader(
	const u64 grp_type,
	const u64 blk_type,
	const size_t blk)
{
	bool is_shader = false;

	switch (grp_type) {
	case KBASE_HWCNT_GPU_GROUP_TYPE_V4:
		/* blk-value in [0, KBASE_HWCNT_V4_SC_BLOCKS_PER_GROUP-1]
		 * corresponds to a shader, or its implementation
		 * reserved. As such, here we use the blk index value to
		 * tell the reserved case.
		 */
		if (blk_type == KBASE_HWCNT_GPU_V4_BLOCK_TYPE_SHADER ||
		    (blk < KBASE_HWCNT_V4_SC_BLOCKS_PER_GROUP &&
		     blk_type == KBASE_HWCNT_GPU_V4_BLOCK_TYPE_RESERVED))
			is_shader = true;
		break;
	case KBASE_HWCNT_GPU_GROUP_TYPE_V5:
		if (blk_type == KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC ||
		    blk_type == KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC2)
			is_shader = true;
		break;
	default:
		/* Warn on unknown group type */
		WARN_ON(true);
	}

	return is_shader;
}

int kbase_hwcnt_gpu_dump_get(
	struct kbase_hwcnt_dump_buffer *dst,
	void *src,
	const struct kbase_hwcnt_enable_map *dst_enable_map,
	u64 pm_core_mask,
	bool accumulate)
{
	const struct kbase_hwcnt_metadata *metadata;
	const u32 *dump_src;
	size_t src_offset, grp, blk, blk_inst;
	size_t grp_prev = 0;
	u64 core_mask = pm_core_mask;

	if (!dst || !src || !dst_enable_map ||
	    (dst_enable_map->metadata != dst->metadata))
		return -EINVAL;

	metadata = dst->metadata;
	dump_src = (const u32 *)src;
	src_offset = 0;

	kbase_hwcnt_metadata_for_each_block(
		metadata, grp, blk, blk_inst) {
		const size_t hdr_cnt =
			kbase_hwcnt_metadata_block_headers_count(
				metadata, grp, blk);
		const size_t ctr_cnt =
			kbase_hwcnt_metadata_block_counters_count(
				metadata, grp, blk);
		const u64 blk_type = kbase_hwcnt_metadata_block_type(
			metadata, grp, blk);
		const bool is_shader_core = is_block_type_shader(
			kbase_hwcnt_metadata_group_type(metadata, grp),
			blk_type, blk);

		if (grp != grp_prev) {
			/* grp change would only happen with V4. V5 and
			 * further are envisaged to be single group
			 * scenario only. Here needs to drop the lower
			 * group core-mask by shifting right with
			 * KBASE_HWCNT_V4_SC_BLOCKS_PER_GROUP.
			 */
			core_mask = pm_core_mask >>
				KBASE_HWCNT_V4_SC_BLOCKS_PER_GROUP;
			grp_prev = grp;
		}

		/* Early out if no values in the dest block are enabled */
		if (kbase_hwcnt_enable_map_block_enabled(
			dst_enable_map, grp, blk, blk_inst)) {
			u32 *dst_blk = kbase_hwcnt_dump_buffer_block_instance(
				dst, grp, blk, blk_inst);
			const u32 *src_blk = dump_src + src_offset;

			if (!is_shader_core || (core_mask & 1)) {
				if (accumulate) {
					kbase_hwcnt_dump_buffer_block_accumulate(
						dst_blk, src_blk, hdr_cnt,
						ctr_cnt);
				} else {
					kbase_hwcnt_dump_buffer_block_copy(
						dst_blk, src_blk,
						(hdr_cnt + ctr_cnt));
				}
			} else if (!accumulate) {
				kbase_hwcnt_dump_buffer_block_zero(
					dst_blk, (hdr_cnt + ctr_cnt));
			}
		}

		src_offset += (hdr_cnt + ctr_cnt);
		if (is_shader_core)
			core_mask = core_mask >> 1;
	}

	return 0;
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_gpu_dump_get);

/**
 * kbasep_hwcnt_backend_gpu_block_map_to_physical() - Convert from a block
 *                                                    enable map abstraction to
 *                                                    a physical block enable
 *                                                    map.
 * @lo: Low 64 bits of block enable map abstraction.
 * @hi: High 64 bits of block enable map abstraction.
 *
 * The abstraction uses 128 bits to enable 128 block values, whereas the
 * physical uses just 32 bits, as bit n enables values [n*4, n*4+3].
 * Therefore, this conversion is lossy.
 *
 * Return: 32-bit physical block enable map.
 */
static inline u32 kbasep_hwcnt_backend_gpu_block_map_to_physical(
	u64 lo,
	u64 hi)
{
	u32 phys = 0;
	u64 dwords[2] = {lo, hi};
	size_t dword_idx;

	for (dword_idx = 0; dword_idx < 2; dword_idx++) {
		const u64 dword = dwords[dword_idx];
		u16 packed = 0;

		size_t hword_bit;

		for (hword_bit = 0; hword_bit < 16; hword_bit++) {
			const size_t dword_bit = hword_bit * 4;
			const u16 mask =
				((dword >> (dword_bit + 0)) & 0x1) |
				((dword >> (dword_bit + 1)) & 0x1) |
				((dword >> (dword_bit + 2)) & 0x1) |
				((dword >> (dword_bit + 3)) & 0x1);
			packed |= (mask << hword_bit);
		}
		phys |= ((u32)packed) << (16 * dword_idx);
	}
	return phys;
}

/**
 * kbasep_hwcnt_backend_gpu_block_map_from_physical() - Convert from a physical
 *                                                      block enable map to a
 *                                                      block enable map
 *                                                      abstraction.
 * @phys: Physical 32-bit block enable map
 * @lo:   Non-NULL pointer to where low 64 bits of block enable map abstraction
 *        will be stored.
 * @hi:   Non-NULL pointer to where high 64 bits of block enable map abstraction
 *        will be stored.
 */
static inline void kbasep_hwcnt_backend_gpu_block_map_from_physical(
	u32 phys,
	u64 *lo,
	u64 *hi)
{
	u64 dwords[2] = {0, 0};

	size_t dword_idx;

	for (dword_idx = 0; dword_idx < 2; dword_idx++) {
		const u16 packed = phys >> (16 * dword_idx);
		u64 dword = 0;

		size_t hword_bit;

		for (hword_bit = 0; hword_bit < 16; hword_bit++) {
			const size_t dword_bit = hword_bit * 4;
			const u64 mask = (packed >> (hword_bit)) & 0x1;

			dword |= mask << (dword_bit + 0);
			dword |= mask << (dword_bit + 1);
			dword |= mask << (dword_bit + 2);
			dword |= mask << (dword_bit + 3);
		}
		dwords[dword_idx] = dword;
	}
	*lo = dwords[0];
	*hi = dwords[1];
}

void kbase_hwcnt_gpu_enable_map_to_physical(
	struct kbase_hwcnt_physical_enable_map *dst,
	const struct kbase_hwcnt_enable_map *src)
{
	const struct kbase_hwcnt_metadata *metadata;

	u64 jm_bm = 0;
	u64 shader_bm = 0;
	u64 tiler_bm = 0;
	u64 mmu_l2_bm = 0;

	size_t grp, blk, blk_inst;

	if (WARN_ON(!src) || WARN_ON(!dst))
		return;

	metadata = src->metadata;

	kbase_hwcnt_metadata_for_each_block(
		metadata, grp, blk, blk_inst) {
		const u64 grp_type = kbase_hwcnt_metadata_group_type(
			metadata, grp);
		const u64 blk_type = kbase_hwcnt_metadata_block_type(
			metadata, grp, blk);
		const size_t blk_val_cnt =
			kbase_hwcnt_metadata_block_values_count(
				metadata, grp, blk);
		const u64 *blk_map = kbase_hwcnt_enable_map_block_instance(
			src, grp, blk, blk_inst);

		switch ((enum kbase_hwcnt_gpu_group_type)grp_type) {
		case KBASE_HWCNT_GPU_GROUP_TYPE_V4:
			WARN_ON(blk_val_cnt != KBASE_HWCNT_V4_VALUES_PER_BLOCK);
			switch ((enum kbase_hwcnt_gpu_v4_block_type)blk_type) {
			case KBASE_HWCNT_GPU_V4_BLOCK_TYPE_SHADER:
				shader_bm |= *blk_map;
				break;
			case KBASE_HWCNT_GPU_V4_BLOCK_TYPE_TILER:
				tiler_bm |= *blk_map;
				break;
			case KBASE_HWCNT_GPU_V4_BLOCK_TYPE_MMU_L2:
				mmu_l2_bm |= *blk_map;
				break;
			case KBASE_HWCNT_GPU_V4_BLOCK_TYPE_JM:
				jm_bm |= *blk_map;
				break;
			case KBASE_HWCNT_GPU_V4_BLOCK_TYPE_RESERVED:
				break;
			default:
				WARN_ON(true);
			}
			break;
		case KBASE_HWCNT_GPU_GROUP_TYPE_V5:
			WARN_ON(blk_val_cnt != KBASE_HWCNT_V5_VALUES_PER_BLOCK);
			switch ((enum kbase_hwcnt_gpu_v5_block_type)blk_type) {
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_JM:
				jm_bm |= *blk_map;
				break;
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER:
				tiler_bm |= *blk_map;
				break;
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC:
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC2:
				shader_bm |= *blk_map;
				break;
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS:
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS2:
				mmu_l2_bm |= *blk_map;
				break;
			default:
				WARN_ON(true);
			}
			break;
		default:
			WARN_ON(true);
		}
	}

	dst->jm_bm =
		kbasep_hwcnt_backend_gpu_block_map_to_physical(jm_bm, 0);
	dst->shader_bm =
		kbasep_hwcnt_backend_gpu_block_map_to_physical(shader_bm, 0);
	dst->tiler_bm =
		kbasep_hwcnt_backend_gpu_block_map_to_physical(tiler_bm, 0);
	dst->mmu_l2_bm =
		kbasep_hwcnt_backend_gpu_block_map_to_physical(mmu_l2_bm, 0);
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_gpu_enable_map_to_physical);

void kbase_hwcnt_gpu_enable_map_from_physical(
	struct kbase_hwcnt_enable_map *dst,
	const struct kbase_hwcnt_physical_enable_map *src)
{
	const struct kbase_hwcnt_metadata *metadata;

	u64 ignored_hi;
	u64 jm_bm;
	u64 shader_bm;
	u64 tiler_bm;
	u64 mmu_l2_bm;
	size_t grp, blk, blk_inst;

	if (WARN_ON(!src) || WARN_ON(!dst))
		return;

	metadata = dst->metadata;

	kbasep_hwcnt_backend_gpu_block_map_from_physical(
		src->jm_bm, &jm_bm, &ignored_hi);
	kbasep_hwcnt_backend_gpu_block_map_from_physical(
		src->shader_bm, &shader_bm, &ignored_hi);
	kbasep_hwcnt_backend_gpu_block_map_from_physical(
		src->tiler_bm, &tiler_bm, &ignored_hi);
	kbasep_hwcnt_backend_gpu_block_map_from_physical(
		src->mmu_l2_bm, &mmu_l2_bm, &ignored_hi);

	kbase_hwcnt_metadata_for_each_block(metadata, grp, blk, blk_inst) {
		const u64 grp_type = kbase_hwcnt_metadata_group_type(
			metadata, grp);
		const u64 blk_type = kbase_hwcnt_metadata_block_type(
			metadata, grp, blk);
		const size_t blk_val_cnt =
			kbase_hwcnt_metadata_block_values_count(
				metadata, grp, blk);
		u64 *blk_map = kbase_hwcnt_enable_map_block_instance(
			dst, grp, blk, blk_inst);

		switch ((enum kbase_hwcnt_gpu_group_type)grp_type) {
		case KBASE_HWCNT_GPU_GROUP_TYPE_V4:
			WARN_ON(blk_val_cnt != KBASE_HWCNT_V4_VALUES_PER_BLOCK);
			switch ((enum kbase_hwcnt_gpu_v4_block_type)blk_type) {
			case KBASE_HWCNT_GPU_V4_BLOCK_TYPE_SHADER:
				*blk_map = shader_bm;
				break;
			case KBASE_HWCNT_GPU_V4_BLOCK_TYPE_TILER:
				*blk_map = tiler_bm;
				break;
			case KBASE_HWCNT_GPU_V4_BLOCK_TYPE_MMU_L2:
				*blk_map = mmu_l2_bm;
				break;
			case KBASE_HWCNT_GPU_V4_BLOCK_TYPE_JM:
				*blk_map = jm_bm;
				break;
			case KBASE_HWCNT_GPU_V4_BLOCK_TYPE_RESERVED:
				break;
			default:
				WARN_ON(true);
			}
			break;
		case KBASE_HWCNT_GPU_GROUP_TYPE_V5:
			WARN_ON(blk_val_cnt != KBASE_HWCNT_V5_VALUES_PER_BLOCK);
			switch ((enum kbase_hwcnt_gpu_v5_block_type)blk_type) {
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_JM:
				*blk_map = jm_bm;
				break;
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER:
				*blk_map = tiler_bm;
				break;
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC:
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC2:
				*blk_map = shader_bm;
				break;
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS:
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS2:
				*blk_map = mmu_l2_bm;
				break;
			default:
				WARN_ON(true);
			}
			break;
		default:
			WARN_ON(true);
		}
	}
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_gpu_enable_map_from_physical);

void kbase_hwcnt_gpu_patch_dump_headers(
	struct kbase_hwcnt_dump_buffer *buf,
	const struct kbase_hwcnt_enable_map *enable_map)
{
	const struct kbase_hwcnt_metadata *metadata;
	size_t grp, blk, blk_inst;

	if (WARN_ON(!buf) || WARN_ON(!enable_map) ||
	    WARN_ON(buf->metadata != enable_map->metadata))
		return;

	metadata = buf->metadata;

	kbase_hwcnt_metadata_for_each_block(metadata, grp, blk, blk_inst) {
		const u64 grp_type =
			kbase_hwcnt_metadata_group_type(metadata, grp);
		u32 *buf_blk = kbase_hwcnt_dump_buffer_block_instance(
			buf, grp, blk, blk_inst);
		const u64 *blk_map = kbase_hwcnt_enable_map_block_instance(
			enable_map, grp, blk, blk_inst);
		const u32 prfcnt_en =
			kbasep_hwcnt_backend_gpu_block_map_to_physical(
				blk_map[0], 0);

		switch ((enum kbase_hwcnt_gpu_group_type)grp_type) {
		case KBASE_HWCNT_GPU_GROUP_TYPE_V4:
			buf_blk[KBASE_HWCNT_V4_PRFCNT_EN_HEADER] = prfcnt_en;
			break;
		case KBASE_HWCNT_GPU_GROUP_TYPE_V5:
			buf_blk[KBASE_HWCNT_V5_PRFCNT_EN_HEADER] = prfcnt_en;
			break;
		default:
			WARN_ON(true);
		}
	}
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_gpu_patch_dump_headers);
