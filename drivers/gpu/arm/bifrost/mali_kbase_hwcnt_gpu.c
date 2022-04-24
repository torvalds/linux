// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2018-2022 ARM Limited. All rights reserved.
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

#include "mali_kbase_hwcnt_gpu.h"
#include "mali_kbase_hwcnt_types.h"

#include <linux/err.h>

/** enum enable_map_idx - index into a block enable map that spans multiple u64 array elements
 */
enum enable_map_idx {
	EM_LO,
	EM_HI,
	EM_COUNT,
};

static void kbasep_get_fe_block_type(u64 *dst, enum kbase_hwcnt_set counter_set,
				     bool is_csf)
{
	switch (counter_set) {
	case KBASE_HWCNT_SET_PRIMARY:
		*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE;
		break;
	case KBASE_HWCNT_SET_SECONDARY:
		if (is_csf)
			*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE2;
		else
			*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE_UNDEFINED;
		break;
	case KBASE_HWCNT_SET_TERTIARY:
		if (is_csf)
			*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE3;
		else
			*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE_UNDEFINED;
		break;
	default:
		WARN_ON(true);
	}
}

static void kbasep_get_tiler_block_type(u64 *dst,
					enum kbase_hwcnt_set counter_set)
{
	switch (counter_set) {
	case KBASE_HWCNT_SET_PRIMARY:
		*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER;
		break;
	case KBASE_HWCNT_SET_SECONDARY:
	case KBASE_HWCNT_SET_TERTIARY:
		*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER_UNDEFINED;
		break;
	default:
		WARN_ON(true);
	}
}

static void kbasep_get_sc_block_type(u64 *dst, enum kbase_hwcnt_set counter_set,
				     bool is_csf)
{
	switch (counter_set) {
	case KBASE_HWCNT_SET_PRIMARY:
		*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC;
		break;
	case KBASE_HWCNT_SET_SECONDARY:
		*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC2;
		break;
	case KBASE_HWCNT_SET_TERTIARY:
		if (is_csf)
			*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC3;
		else
			*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC_UNDEFINED;
		break;
	default:
		WARN_ON(true);
	}
}

static void kbasep_get_memsys_block_type(u64 *dst,
					 enum kbase_hwcnt_set counter_set)
{
	switch (counter_set) {
	case KBASE_HWCNT_SET_PRIMARY:
		*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS;
		break;
	case KBASE_HWCNT_SET_SECONDARY:
		*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS2;
		break;
	case KBASE_HWCNT_SET_TERTIARY:
		*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS_UNDEFINED;
		break;
	default:
		WARN_ON(true);
	}
}

/**
 * kbasep_hwcnt_backend_gpu_metadata_create() - Create hardware counter metadata
 *                                              for the GPU.
 * @gpu_info:      Non-NULL pointer to hwcnt info for current GPU.
 * @is_csf:        true for CSF GPU, otherwise false.
 * @counter_set:   The performance counter set to use.
 * @metadata:      Non-NULL pointer to where created metadata is stored
 *                 on success.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_hwcnt_backend_gpu_metadata_create(
	const struct kbase_hwcnt_gpu_info *gpu_info, const bool is_csf,
	enum kbase_hwcnt_set counter_set,
	const struct kbase_hwcnt_metadata **metadata)
{
	struct kbase_hwcnt_description desc;
	struct kbase_hwcnt_group_description group;
	struct kbase_hwcnt_block_description
		blks[KBASE_HWCNT_V5_BLOCK_TYPE_COUNT];
	size_t non_sc_block_count;
	size_t sc_block_count;

	WARN_ON(!gpu_info);
	WARN_ON(!metadata);

	/* Calculate number of block instances that aren't shader cores */
	non_sc_block_count = 2 + gpu_info->l2_count;
	/* Calculate number of block instances that are shader cores */
	sc_block_count = fls64(gpu_info->core_mask);

	/*
	 * A system can have up to 64 shader cores, but the 64-bit
	 * availability mask can't physically represent that many cores as well
	 * as the other hardware blocks.
	 * Error out if there are more blocks than our implementation can
	 * support.
	 */
	if ((sc_block_count + non_sc_block_count) > KBASE_HWCNT_AVAIL_MASK_BITS)
		return -EINVAL;

	/* One Front End block */
	kbasep_get_fe_block_type(&blks[0].type, counter_set, is_csf);
	blks[0].inst_cnt = 1;
	blks[0].hdr_cnt = KBASE_HWCNT_V5_HEADERS_PER_BLOCK;
	blks[0].ctr_cnt = gpu_info->prfcnt_values_per_block -
			  KBASE_HWCNT_V5_HEADERS_PER_BLOCK;

	/* One Tiler block */
	kbasep_get_tiler_block_type(&blks[1].type, counter_set);
	blks[1].inst_cnt = 1;
	blks[1].hdr_cnt = KBASE_HWCNT_V5_HEADERS_PER_BLOCK;
	blks[1].ctr_cnt = gpu_info->prfcnt_values_per_block -
			  KBASE_HWCNT_V5_HEADERS_PER_BLOCK;

	/* l2_count memsys blks */
	kbasep_get_memsys_block_type(&blks[2].type, counter_set);
	blks[2].inst_cnt = gpu_info->l2_count;
	blks[2].hdr_cnt = KBASE_HWCNT_V5_HEADERS_PER_BLOCK;
	blks[2].ctr_cnt = gpu_info->prfcnt_values_per_block -
			  KBASE_HWCNT_V5_HEADERS_PER_BLOCK;

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
	kbasep_get_sc_block_type(&blks[3].type, counter_set, is_csf);
	blks[3].inst_cnt = sc_block_count;
	blks[3].hdr_cnt = KBASE_HWCNT_V5_HEADERS_PER_BLOCK;
	blks[3].ctr_cnt = gpu_info->prfcnt_values_per_block -
			  KBASE_HWCNT_V5_HEADERS_PER_BLOCK;

	WARN_ON(KBASE_HWCNT_V5_BLOCK_TYPE_COUNT != 4);

	group.type = KBASE_HWCNT_GPU_GROUP_TYPE_V5;
	group.blk_cnt = KBASE_HWCNT_V5_BLOCK_TYPE_COUNT;
	group.blks = blks;

	desc.grp_cnt = 1;
	desc.grps = &group;
	desc.clk_cnt = gpu_info->clk_cnt;

	/* The JM, Tiler, and L2s are always available, and are before cores */
	desc.avail_mask = (1ull << non_sc_block_count) - 1;
	/* Embed the core mask directly in the availability mask */
	desc.avail_mask |= (gpu_info->core_mask << non_sc_block_count);

	return kbase_hwcnt_metadata_create(&desc, metadata);
}

/**
 * kbasep_hwcnt_backend_jm_dump_bytes() - Get the raw dump buffer size for the
 *                                        GPU.
 * @gpu_info: Non-NULL pointer to hwcnt info for the GPU.
 *
 * Return: Size of buffer the GPU needs to perform a counter dump.
 */
static size_t
kbasep_hwcnt_backend_jm_dump_bytes(const struct kbase_hwcnt_gpu_info *gpu_info)
{
	WARN_ON(!gpu_info);

	return (2 + gpu_info->l2_count + fls64(gpu_info->core_mask)) *
	       gpu_info->prfcnt_values_per_block * KBASE_HWCNT_VALUE_HW_BYTES;
}

int kbase_hwcnt_jm_metadata_create(
	const struct kbase_hwcnt_gpu_info *gpu_info,
	enum kbase_hwcnt_set counter_set,
	const struct kbase_hwcnt_metadata **out_metadata,
	size_t *out_dump_bytes)
{
	int errcode;
	const struct kbase_hwcnt_metadata *metadata;
	size_t dump_bytes;

	if (!gpu_info || !out_metadata || !out_dump_bytes)
		return -EINVAL;

	/*
	 * For architectures where a max_config interface is available
	 * from the arbiter, the v5 dump bytes and the metadata v5 are
	 * based on the maximum possible allocation of the HW in the
	 * GPU cause it needs to be prepared for the worst case where
	 * all the available L2 cache and Shader cores are allocated.
	 */
	dump_bytes = kbasep_hwcnt_backend_jm_dump_bytes(gpu_info);
	errcode = kbasep_hwcnt_backend_gpu_metadata_create(
		gpu_info, false, counter_set, &metadata);
	if (errcode)
		return errcode;

	/*
	 * The physical dump size should be half of dump abstraction size in
	 * metadata since physical HW uses 32-bit per value but metadata
	 * specifies 64-bit per value.
	 */
	WARN_ON(dump_bytes * 2 != metadata->dump_buf_bytes);

	*out_metadata = metadata;
	*out_dump_bytes = dump_bytes;

	return 0;
}

void kbase_hwcnt_jm_metadata_destroy(const struct kbase_hwcnt_metadata *metadata)
{
	if (!metadata)
		return;

	kbase_hwcnt_metadata_destroy(metadata);
}

int kbase_hwcnt_csf_metadata_create(
	const struct kbase_hwcnt_gpu_info *gpu_info,
	enum kbase_hwcnt_set counter_set,
	const struct kbase_hwcnt_metadata **out_metadata)
{
	int errcode;
	const struct kbase_hwcnt_metadata *metadata;

	if (!gpu_info || !out_metadata)
		return -EINVAL;

	errcode = kbasep_hwcnt_backend_gpu_metadata_create(
		gpu_info, true, counter_set, &metadata);
	if (errcode)
		return errcode;

	*out_metadata = metadata;

	return 0;
}

void kbase_hwcnt_csf_metadata_destroy(
	const struct kbase_hwcnt_metadata *metadata)
{
	if (!metadata)
		return;

	kbase_hwcnt_metadata_destroy(metadata);
}

static bool is_block_type_shader(
	const u64 grp_type,
	const u64 blk_type,
	const size_t blk)
{
	bool is_shader = false;

	/* Warn on unknown group type */
	if (WARN_ON(grp_type != KBASE_HWCNT_GPU_GROUP_TYPE_V5))
		return false;

	if (blk_type == KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC ||
	    blk_type == KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC2 ||
	    blk_type == KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC3 ||
	    blk_type == KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC_UNDEFINED)
		is_shader = true;

	return is_shader;
}

static bool is_block_type_l2_cache(
	const u64 grp_type,
	const u64 blk_type)
{
	bool is_l2_cache = false;

	switch (grp_type) {
	case KBASE_HWCNT_GPU_GROUP_TYPE_V5:
		if (blk_type == KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS ||
		    blk_type == KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS2 ||
		    blk_type == KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS_UNDEFINED)
			is_l2_cache = true;
		break;
	default:
		/* Warn on unknown group type */
		WARN_ON(true);
	}

	return is_l2_cache;
}

int kbase_hwcnt_jm_dump_get(struct kbase_hwcnt_dump_buffer *dst, u64 *src,
			    const struct kbase_hwcnt_enable_map *dst_enable_map,
			    u64 pm_core_mask,
			    const struct kbase_hwcnt_curr_config *curr_config,
			    bool accumulate)
{
	const struct kbase_hwcnt_metadata *metadata;
	size_t grp, blk, blk_inst;
	const u64 *dump_src = src;
	size_t src_offset = 0;
	u64 core_mask = pm_core_mask;

	/* Variables to deal with the current configuration */
	int l2_count = 0;

	if (!dst || !src || !dst_enable_map ||
	    (dst_enable_map->metadata != dst->metadata))
		return -EINVAL;

	metadata = dst->metadata;

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
		const bool is_l2_cache = is_block_type_l2_cache(
			kbase_hwcnt_metadata_group_type(metadata, grp),
			blk_type);
		const bool is_undefined = kbase_hwcnt_is_block_type_undefined(
			kbase_hwcnt_metadata_group_type(metadata, grp), blk_type);
		bool hw_res_available = true;

		/*
		 * If l2 blocks is greater than the current allocated number of
		 * L2 slices, there is no hw allocated to that block.
		 */
		if (is_l2_cache) {
			l2_count++;
			if (l2_count > curr_config->num_l2_slices)
				hw_res_available = false;
			else
				hw_res_available = true;
		}
		/*
		 * For the shader cores, the current shader_mask allocated is
		 * always a subgroup of the maximum shader_mask, so after
		 * jumping any L2 cache not available the available shader cores
		 * will always have a matching set of blk instances available to
		 * accumulate them.
		 */
		else
			hw_res_available = true;

		/*
		 * Skip block if no values in the destination block are enabled.
		 */
		if (kbase_hwcnt_enable_map_block_enabled(
			dst_enable_map, grp, blk, blk_inst)) {
			u64 *dst_blk = kbase_hwcnt_dump_buffer_block_instance(
				dst, grp, blk, blk_inst);
			const u64 *src_blk = dump_src + src_offset;
			bool blk_powered;

			if (!is_shader_core) {
				/* Under the current PM system, counters will
				 * only be enabled after all non shader core
				 * blocks are powered up.
				 */
				blk_powered = true;
			} else {
				/* Check the PM core mask to see if the shader
				 * core is powered up.
				 */
				blk_powered = core_mask & 1;
			}

			if (blk_powered && !is_undefined && hw_res_available) {
				/* Only powered and defined blocks have valid data. */
				if (accumulate) {
					kbase_hwcnt_dump_buffer_block_accumulate(
						dst_blk, src_blk, hdr_cnt,
						ctr_cnt);
				} else {
					kbase_hwcnt_dump_buffer_block_copy(
						dst_blk, src_blk,
						(hdr_cnt + ctr_cnt));
				}
			} else {
				/* Even though the block might be undefined, the
				 * user has enabled counter collection for it.
				 * We should not propagate garbage data.
				 */
				if (accumulate) {
					/* No-op to preserve existing values */
				} else {
					/* src is garbage, so zero the dst */
					kbase_hwcnt_dump_buffer_block_zero(dst_blk,
									   (hdr_cnt + ctr_cnt));
				}
			}
		}

		/* Just increase the src_offset if the HW is available */
		if (hw_res_available)
			src_offset += (hdr_cnt + ctr_cnt);
		if (is_shader_core)
			core_mask = core_mask >> 1;
	}

	return 0;
}

int kbase_hwcnt_csf_dump_get(struct kbase_hwcnt_dump_buffer *dst, u64 *src,
			     const struct kbase_hwcnt_enable_map *dst_enable_map,
			     bool accumulate)
{
	const struct kbase_hwcnt_metadata *metadata;
	const u64 *dump_src = src;
	size_t src_offset = 0;
	size_t grp, blk, blk_inst;

	if (!dst || !src || !dst_enable_map ||
	    (dst_enable_map->metadata != dst->metadata))
		return -EINVAL;

	metadata = dst->metadata;

	kbase_hwcnt_metadata_for_each_block(metadata, grp, blk, blk_inst) {
		const size_t hdr_cnt = kbase_hwcnt_metadata_block_headers_count(
			metadata, grp, blk);
		const size_t ctr_cnt =
			kbase_hwcnt_metadata_block_counters_count(metadata, grp,
								  blk);
		const uint64_t blk_type = kbase_hwcnt_metadata_block_type(metadata, grp, blk);
		const bool is_undefined = kbase_hwcnt_is_block_type_undefined(
			kbase_hwcnt_metadata_group_type(metadata, grp), blk_type);

		/*
		 * Skip block if no values in the destination block are enabled.
		 */
		if (kbase_hwcnt_enable_map_block_enabled(dst_enable_map, grp,
							 blk, blk_inst)) {
			u64 *dst_blk = kbase_hwcnt_dump_buffer_block_instance(
				dst, grp, blk, blk_inst);
			const u64 *src_blk = dump_src + src_offset;

			if (!is_undefined) {
				if (accumulate) {
					kbase_hwcnt_dump_buffer_block_accumulate(dst_blk, src_blk,
										 hdr_cnt, ctr_cnt);
				} else {
					kbase_hwcnt_dump_buffer_block_copy(dst_blk, src_blk,
									   (hdr_cnt + ctr_cnt));
				}
			} else {
				/* Even though the block might be undefined, the
				 * user has enabled counter collection for it.
				 * We should not propagate garbage data.
				 */
				if (accumulate) {
					/* No-op to preserve existing values */
				} else {
					/* src is garbage, so zero the dst */
					kbase_hwcnt_dump_buffer_block_zero(dst_blk,
									   (hdr_cnt + ctr_cnt));
				}
			}
		}

		src_offset += (hdr_cnt + ctr_cnt);
	}

	return 0;
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
	u64 fe_bm[EM_COUNT] = { 0 };
	u64 shader_bm[EM_COUNT] = { 0 };
	u64 tiler_bm[EM_COUNT] = { 0 };
	u64 mmu_l2_bm[EM_COUNT] = { 0 };
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
		const u64 *blk_map = kbase_hwcnt_enable_map_block_instance(
			src, grp, blk, blk_inst);

		if ((enum kbase_hwcnt_gpu_group_type)grp_type ==
		    KBASE_HWCNT_GPU_GROUP_TYPE_V5) {
			const size_t map_stride =
				kbase_hwcnt_metadata_block_enable_map_stride(metadata, grp, blk);
			size_t map_idx;

			for (map_idx = 0; map_idx < map_stride; ++map_idx) {
				if (WARN_ON(map_idx >= EM_COUNT))
					break;

				switch ((enum kbase_hwcnt_gpu_v5_block_type)blk_type) {
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE_UNDEFINED:
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC_UNDEFINED:
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER_UNDEFINED:
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS_UNDEFINED:
					/* Nothing to do in this case. */
					break;
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE:
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE2:
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE3:
					fe_bm[map_idx] |= blk_map[map_idx];
					break;
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER:
					tiler_bm[map_idx] |= blk_map[map_idx];
					break;
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC:
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC2:
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC3:
					shader_bm[map_idx] |= blk_map[map_idx];
					break;
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS:
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS2:
					mmu_l2_bm[map_idx] |= blk_map[map_idx];
					break;
				default:
					WARN_ON(true);
				}
			}
		} else {
			WARN_ON(true);
		}
	}

	dst->fe_bm = kbase_hwcnt_backend_gpu_block_map_to_physical(fe_bm[EM_LO], fe_bm[EM_HI]);
	dst->shader_bm =
		kbase_hwcnt_backend_gpu_block_map_to_physical(shader_bm[EM_LO], shader_bm[EM_HI]);
	dst->tiler_bm =
		kbase_hwcnt_backend_gpu_block_map_to_physical(tiler_bm[EM_LO], tiler_bm[EM_HI]);
	dst->mmu_l2_bm =
		kbase_hwcnt_backend_gpu_block_map_to_physical(mmu_l2_bm[EM_LO], mmu_l2_bm[EM_HI]);
}

void kbase_hwcnt_gpu_set_to_physical(enum kbase_hwcnt_physical_set *dst,
				     enum kbase_hwcnt_set src)
{
	switch (src) {
	case KBASE_HWCNT_SET_PRIMARY:
		*dst = KBASE_HWCNT_PHYSICAL_SET_PRIMARY;
		break;
	case KBASE_HWCNT_SET_SECONDARY:
		*dst = KBASE_HWCNT_PHYSICAL_SET_SECONDARY;
		break;
	case KBASE_HWCNT_SET_TERTIARY:
		*dst = KBASE_HWCNT_PHYSICAL_SET_TERTIARY;
		break;
	default:
		WARN_ON(true);
	}
}

void kbase_hwcnt_gpu_enable_map_from_physical(
	struct kbase_hwcnt_enable_map *dst,
	const struct kbase_hwcnt_physical_enable_map *src)
{
	const struct kbase_hwcnt_metadata *metadata;

	u64 fe_bm[EM_COUNT] = { 0 };
	u64 shader_bm[EM_COUNT] = { 0 };
	u64 tiler_bm[EM_COUNT] = { 0 };
	u64 mmu_l2_bm[EM_COUNT] = { 0 };
	size_t grp, blk, blk_inst;

	if (WARN_ON(!src) || WARN_ON(!dst))
		return;

	metadata = dst->metadata;

	kbasep_hwcnt_backend_gpu_block_map_from_physical(src->fe_bm, &fe_bm[EM_LO], &fe_bm[EM_HI]);
	kbasep_hwcnt_backend_gpu_block_map_from_physical(src->shader_bm, &shader_bm[EM_LO],
							 &shader_bm[EM_HI]);
	kbasep_hwcnt_backend_gpu_block_map_from_physical(src->tiler_bm, &tiler_bm[EM_LO],
							 &tiler_bm[EM_HI]);
	kbasep_hwcnt_backend_gpu_block_map_from_physical(src->mmu_l2_bm, &mmu_l2_bm[EM_LO],
							 &mmu_l2_bm[EM_HI]);

	kbase_hwcnt_metadata_for_each_block(metadata, grp, blk, blk_inst) {
		const u64 grp_type = kbase_hwcnt_metadata_group_type(
			metadata, grp);
		const u64 blk_type = kbase_hwcnt_metadata_block_type(
			metadata, grp, blk);
		u64 *blk_map = kbase_hwcnt_enable_map_block_instance(
			dst, grp, blk, blk_inst);

		if ((enum kbase_hwcnt_gpu_group_type)grp_type ==
		    KBASE_HWCNT_GPU_GROUP_TYPE_V5) {
			const size_t map_stride =
				kbase_hwcnt_metadata_block_enable_map_stride(metadata, grp, blk);
			size_t map_idx;

			for (map_idx = 0; map_idx < map_stride; ++map_idx) {
				if (WARN_ON(map_idx >= EM_COUNT))
					break;

				switch ((enum kbase_hwcnt_gpu_v5_block_type)blk_type) {
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE_UNDEFINED:
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC_UNDEFINED:
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER_UNDEFINED:
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS_UNDEFINED:
					/* Nothing to do in this case. */
					break;
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE:
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE2:
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE3:
					blk_map[map_idx] = fe_bm[map_idx];
					break;
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER:
					blk_map[map_idx] = tiler_bm[map_idx];
					break;
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC:
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC2:
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC3:
					blk_map[map_idx] = shader_bm[map_idx];
					break;
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS:
				case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS2:
					blk_map[map_idx] = mmu_l2_bm[map_idx];
					break;
				default:
					WARN_ON(true);
				}
			}
		} else {
			WARN_ON(true);
		}
	}
}

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
		u64 *buf_blk = kbase_hwcnt_dump_buffer_block_instance(
			buf, grp, blk, blk_inst);
		const u64 *blk_map = kbase_hwcnt_enable_map_block_instance(
			enable_map, grp, blk, blk_inst);

		if ((enum kbase_hwcnt_gpu_group_type)grp_type ==
		    KBASE_HWCNT_GPU_GROUP_TYPE_V5) {
			const size_t map_stride =
				kbase_hwcnt_metadata_block_enable_map_stride(metadata, grp, blk);
			u64 prfcnt_bm[EM_COUNT] = { 0 };
			u32 prfcnt_en = 0;
			size_t map_idx;

			for (map_idx = 0; map_idx < map_stride; ++map_idx) {
				if (WARN_ON(map_idx >= EM_COUNT))
					break;

				prfcnt_bm[map_idx] = blk_map[map_idx];
			}

			prfcnt_en = kbase_hwcnt_backend_gpu_block_map_to_physical(prfcnt_bm[EM_LO],
										  prfcnt_bm[EM_HI]);

			buf_blk[KBASE_HWCNT_V5_PRFCNT_EN_HEADER] = prfcnt_en;
		} else {
			WARN_ON(true);
		}
	}
}
