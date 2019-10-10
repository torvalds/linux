/*
 *
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
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

#include "mali_kbase_hwcnt_types.h"
#include "mali_kbase.h"

/* Minimum alignment of each block of hardware counters */
#define KBASE_HWCNT_BLOCK_BYTE_ALIGNMENT \
	(KBASE_HWCNT_BITFIELD_BITS * KBASE_HWCNT_VALUE_BYTES)

/**
 * KBASE_HWCNT_ALIGN_UPWARDS() - Align a value to an alignment.
 * @value:     The value to align upwards.
 * @alignment: The alignment.
 *
 * Return: A number greater than or equal to value that is aligned to alignment.
 */
#define KBASE_HWCNT_ALIGN_UPWARDS(value, alignment) \
	(value + ((alignment - (value % alignment)) % alignment))

int kbase_hwcnt_metadata_create(
	const struct kbase_hwcnt_description *desc,
	const struct kbase_hwcnt_metadata **out_metadata)
{
	char *buf;
	struct kbase_hwcnt_metadata *metadata;
	struct kbase_hwcnt_group_metadata *grp_mds;
	size_t grp;
	size_t enable_map_count; /* Number of u64 bitfields (inc padding) */
	size_t dump_buf_count; /* Number of u32 values (inc padding) */
	size_t avail_mask_bits; /* Number of availability mask bits */

	size_t size;
	size_t offset;

	if (!desc || !out_metadata)
		return -EINVAL;

	/* Calculate the bytes needed to tightly pack the metadata */

	/* Top level metadata */
	size = 0;
	size += sizeof(struct kbase_hwcnt_metadata);

	/* Group metadata */
	size += sizeof(struct kbase_hwcnt_group_metadata) * desc->grp_cnt;

	/* Block metadata */
	for (grp = 0; grp < desc->grp_cnt; grp++) {
		size += sizeof(struct kbase_hwcnt_block_metadata) *
			desc->grps[grp].blk_cnt;
	}

	/* Single allocation for the entire metadata */
	buf = kmalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Use the allocated memory for the metadata and its members */

	/* Bump allocate the top level metadata */
	offset = 0;
	metadata = (struct kbase_hwcnt_metadata *)(buf + offset);
	offset += sizeof(struct kbase_hwcnt_metadata);

	/* Bump allocate the group metadata */
	grp_mds = (struct kbase_hwcnt_group_metadata *)(buf + offset);
	offset += sizeof(struct kbase_hwcnt_group_metadata) * desc->grp_cnt;

	enable_map_count = 0;
	dump_buf_count = 0;
	avail_mask_bits = 0;

	for (grp = 0; grp < desc->grp_cnt; grp++) {
		size_t blk;

		const struct kbase_hwcnt_group_description *grp_desc =
			desc->grps + grp;
		struct kbase_hwcnt_group_metadata *grp_md = grp_mds + grp;

		size_t group_enable_map_count = 0;
		size_t group_dump_buffer_count = 0;
		size_t group_avail_mask_bits = 0;

		/* Bump allocate this group's block metadata */
		struct kbase_hwcnt_block_metadata *blk_mds =
			(struct kbase_hwcnt_block_metadata *)(buf + offset);
		offset += sizeof(struct kbase_hwcnt_block_metadata) *
			grp_desc->blk_cnt;

		/* Fill in each block in the group's information */
		for (blk = 0; blk < grp_desc->blk_cnt; blk++) {
			const struct kbase_hwcnt_block_description *blk_desc =
				grp_desc->blks + blk;
			struct kbase_hwcnt_block_metadata *blk_md =
				blk_mds + blk;
			const size_t n_values =
				blk_desc->hdr_cnt + blk_desc->ctr_cnt;

			blk_md->type = blk_desc->type;
			blk_md->inst_cnt = blk_desc->inst_cnt;
			blk_md->hdr_cnt = blk_desc->hdr_cnt;
			blk_md->ctr_cnt = blk_desc->ctr_cnt;
			blk_md->enable_map_index = group_enable_map_count;
			blk_md->enable_map_stride =
				kbase_hwcnt_bitfield_count(n_values);
			blk_md->dump_buf_index = group_dump_buffer_count;
			blk_md->dump_buf_stride =
				KBASE_HWCNT_ALIGN_UPWARDS(
					n_values,
					(KBASE_HWCNT_BLOCK_BYTE_ALIGNMENT /
					 KBASE_HWCNT_VALUE_BYTES));
			blk_md->avail_mask_index = group_avail_mask_bits;

			group_enable_map_count +=
				blk_md->enable_map_stride * blk_md->inst_cnt;
			group_dump_buffer_count +=
				blk_md->dump_buf_stride * blk_md->inst_cnt;
			group_avail_mask_bits += blk_md->inst_cnt;
		}

		/* Fill in the group's information */
		grp_md->type = grp_desc->type;
		grp_md->blk_cnt = grp_desc->blk_cnt;
		grp_md->blk_metadata = blk_mds;
		grp_md->enable_map_index = enable_map_count;
		grp_md->dump_buf_index = dump_buf_count;
		grp_md->avail_mask_index = avail_mask_bits;

		enable_map_count += group_enable_map_count;
		dump_buf_count += group_dump_buffer_count;
		avail_mask_bits += group_avail_mask_bits;
	}

	/* Fill in the top level metadata's information */
	metadata->grp_cnt = desc->grp_cnt;
	metadata->grp_metadata = grp_mds;
	metadata->enable_map_bytes =
		enable_map_count * KBASE_HWCNT_BITFIELD_BYTES;
	metadata->dump_buf_bytes = dump_buf_count * KBASE_HWCNT_VALUE_BYTES;
	metadata->avail_mask = desc->avail_mask;

	WARN_ON(size != offset);
	/* Due to the block alignment, there should be exactly one enable map
	 * bit per 4 bytes in the dump buffer.
	 */
	WARN_ON(metadata->dump_buf_bytes !=
		(metadata->enable_map_bytes *
		 BITS_PER_BYTE * KBASE_HWCNT_VALUE_BYTES));

	*out_metadata = metadata;
	return 0;
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_metadata_create);

void kbase_hwcnt_metadata_destroy(const struct kbase_hwcnt_metadata *metadata)
{
	kfree(metadata);
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_metadata_destroy);

int kbase_hwcnt_enable_map_alloc(
	const struct kbase_hwcnt_metadata *metadata,
	struct kbase_hwcnt_enable_map *enable_map)
{
	u64 *enable_map_buf;

	if (!metadata || !enable_map)
		return -EINVAL;

	enable_map_buf = kzalloc(metadata->enable_map_bytes, GFP_KERNEL);
	if (!enable_map_buf)
		return -ENOMEM;

	enable_map->metadata = metadata;
	enable_map->enable_map = enable_map_buf;
	return 0;
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_enable_map_alloc);

void kbase_hwcnt_enable_map_free(struct kbase_hwcnt_enable_map *enable_map)
{
	if (!enable_map)
		return;

	kfree(enable_map->enable_map);
	enable_map->enable_map = NULL;
	enable_map->metadata = NULL;
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_enable_map_free);

int kbase_hwcnt_dump_buffer_alloc(
	const struct kbase_hwcnt_metadata *metadata,
	struct kbase_hwcnt_dump_buffer *dump_buf)
{
	u32 *buf;

	if (!metadata || !dump_buf)
		return -EINVAL;

	buf = kmalloc(metadata->dump_buf_bytes, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	dump_buf->metadata = metadata;
	dump_buf->dump_buf = buf;
	return 0;
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_dump_buffer_alloc);

void kbase_hwcnt_dump_buffer_free(struct kbase_hwcnt_dump_buffer *dump_buf)
{
	if (!dump_buf)
		return;

	kfree(dump_buf->dump_buf);
	memset(dump_buf, 0, sizeof(*dump_buf));
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_dump_buffer_free);

int kbase_hwcnt_dump_buffer_array_alloc(
	const struct kbase_hwcnt_metadata *metadata,
	size_t n,
	struct kbase_hwcnt_dump_buffer_array *dump_bufs)
{
	struct kbase_hwcnt_dump_buffer *buffers;
	size_t buf_idx;
	unsigned int order;
	unsigned long addr;

	if (!metadata || !dump_bufs)
		return -EINVAL;

	/* Allocate memory for the dump buffer struct array */
	buffers = kmalloc_array(n, sizeof(*buffers), GFP_KERNEL);
	if (!buffers)
		return -ENOMEM;

	/* Allocate pages for the actual dump buffers, as they tend to be fairly
	 * large.
	 */
	order = get_order(metadata->dump_buf_bytes * n);
	addr = __get_free_pages(GFP_KERNEL, order);

	if (!addr) {
		kfree(buffers);
		return -ENOMEM;
	}

	dump_bufs->page_addr = addr;
	dump_bufs->page_order = order;
	dump_bufs->buf_cnt = n;
	dump_bufs->bufs = buffers;

	/* Set the buffer of each dump buf */
	for (buf_idx = 0; buf_idx < n; buf_idx++) {
		const size_t offset = metadata->dump_buf_bytes * buf_idx;

		buffers[buf_idx].metadata = metadata;
		buffers[buf_idx].dump_buf = (u32 *)(addr + offset);
	}

	return 0;
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_dump_buffer_array_alloc);

void kbase_hwcnt_dump_buffer_array_free(
	struct kbase_hwcnt_dump_buffer_array *dump_bufs)
{
	if (!dump_bufs)
		return;

	kfree(dump_bufs->bufs);
	free_pages(dump_bufs->page_addr, dump_bufs->page_order);
	memset(dump_bufs, 0, sizeof(*dump_bufs));
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_dump_buffer_array_free);

void kbase_hwcnt_dump_buffer_zero(
	struct kbase_hwcnt_dump_buffer *dst,
	const struct kbase_hwcnt_enable_map *dst_enable_map)
{
	const struct kbase_hwcnt_metadata *metadata;
	size_t grp, blk, blk_inst;

	if (WARN_ON(!dst) ||
	    WARN_ON(!dst_enable_map) ||
	    WARN_ON(dst->metadata != dst_enable_map->metadata))
		return;

	metadata = dst->metadata;

	kbase_hwcnt_metadata_for_each_block(metadata, grp, blk, blk_inst) {
		u32 *dst_blk;
		size_t val_cnt;

		if (!kbase_hwcnt_enable_map_block_enabled(
			dst_enable_map, grp, blk, blk_inst))
			continue;

		dst_blk = kbase_hwcnt_dump_buffer_block_instance(
			dst, grp, blk, blk_inst);
		val_cnt = kbase_hwcnt_metadata_block_values_count(
			metadata, grp, blk);

		kbase_hwcnt_dump_buffer_block_zero(dst_blk, val_cnt);
	}
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_dump_buffer_zero);

void kbase_hwcnt_dump_buffer_zero_strict(
	struct kbase_hwcnt_dump_buffer *dst)
{
	if (WARN_ON(!dst))
		return;

	memset(dst->dump_buf, 0, dst->metadata->dump_buf_bytes);
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_dump_buffer_zero_strict);

void kbase_hwcnt_dump_buffer_zero_non_enabled(
	struct kbase_hwcnt_dump_buffer *dst,
	const struct kbase_hwcnt_enable_map *dst_enable_map)
{
	const struct kbase_hwcnt_metadata *metadata;
	size_t grp, blk, blk_inst;

	if (WARN_ON(!dst) ||
	    WARN_ON(!dst_enable_map) ||
	    WARN_ON(dst->metadata != dst_enable_map->metadata))
		return;

	metadata = dst->metadata;

	kbase_hwcnt_metadata_for_each_block(metadata, grp, blk, blk_inst) {
		u32 *dst_blk = kbase_hwcnt_dump_buffer_block_instance(
			dst, grp, blk, blk_inst);
		const u64 *blk_em = kbase_hwcnt_enable_map_block_instance(
			dst_enable_map, grp, blk, blk_inst);
		size_t val_cnt = kbase_hwcnt_metadata_block_values_count(
			metadata, grp, blk);

		/* Align upwards to include padding bytes */
		val_cnt = KBASE_HWCNT_ALIGN_UPWARDS(val_cnt,
			(KBASE_HWCNT_BLOCK_BYTE_ALIGNMENT /
			 KBASE_HWCNT_VALUE_BYTES));

		if (kbase_hwcnt_metadata_block_instance_avail(
			metadata, grp, blk, blk_inst)) {
			/* Block available, so only zero non-enabled values */
			kbase_hwcnt_dump_buffer_block_zero_non_enabled(
				dst_blk, blk_em, val_cnt);
		} else {
			/* Block not available, so zero the entire thing */
			kbase_hwcnt_dump_buffer_block_zero(dst_blk, val_cnt);
		}
	}
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_dump_buffer_zero_non_enabled);

void kbase_hwcnt_dump_buffer_copy(
	struct kbase_hwcnt_dump_buffer *dst,
	const struct kbase_hwcnt_dump_buffer *src,
	const struct kbase_hwcnt_enable_map *dst_enable_map)
{
	const struct kbase_hwcnt_metadata *metadata;
	size_t grp, blk, blk_inst;

	if (WARN_ON(!dst) ||
	    WARN_ON(!src) ||
	    WARN_ON(!dst_enable_map) ||
	    WARN_ON(dst == src) ||
	    WARN_ON(dst->metadata != src->metadata) ||
	    WARN_ON(dst->metadata != dst_enable_map->metadata))
		return;

	metadata = dst->metadata;

	kbase_hwcnt_metadata_for_each_block(metadata, grp, blk, blk_inst) {
		u32 *dst_blk;
		const u32 *src_blk;
		size_t val_cnt;

		if (!kbase_hwcnt_enable_map_block_enabled(
			dst_enable_map, grp, blk, blk_inst))
			continue;

		dst_blk = kbase_hwcnt_dump_buffer_block_instance(
			dst, grp, blk, blk_inst);
		src_blk = kbase_hwcnt_dump_buffer_block_instance(
			src, grp, blk, blk_inst);
		val_cnt = kbase_hwcnt_metadata_block_values_count(
			metadata, grp, blk);

		kbase_hwcnt_dump_buffer_block_copy(dst_blk, src_blk, val_cnt);
	}
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_dump_buffer_copy);

void kbase_hwcnt_dump_buffer_copy_strict(
	struct kbase_hwcnt_dump_buffer *dst,
	const struct kbase_hwcnt_dump_buffer *src,
	const struct kbase_hwcnt_enable_map *dst_enable_map)
{
	const struct kbase_hwcnt_metadata *metadata;
	size_t grp, blk, blk_inst;

	if (WARN_ON(!dst) ||
	    WARN_ON(!src) ||
	    WARN_ON(!dst_enable_map) ||
	    WARN_ON(dst == src) ||
	    WARN_ON(dst->metadata != src->metadata) ||
	    WARN_ON(dst->metadata != dst_enable_map->metadata))
		return;

	metadata = dst->metadata;

	kbase_hwcnt_metadata_for_each_block(metadata, grp, blk, blk_inst) {
		u32 *dst_blk = kbase_hwcnt_dump_buffer_block_instance(
			dst, grp, blk, blk_inst);
		const u32 *src_blk = kbase_hwcnt_dump_buffer_block_instance(
			src, grp, blk, blk_inst);
		const u64 *blk_em = kbase_hwcnt_enable_map_block_instance(
			dst_enable_map, grp, blk, blk_inst);
		size_t val_cnt = kbase_hwcnt_metadata_block_values_count(
			metadata, grp, blk);
		/* Align upwards to include padding bytes */
		val_cnt = KBASE_HWCNT_ALIGN_UPWARDS(val_cnt,
			(KBASE_HWCNT_BLOCK_BYTE_ALIGNMENT /
			 KBASE_HWCNT_VALUE_BYTES));

		kbase_hwcnt_dump_buffer_block_copy_strict(
			dst_blk, src_blk, blk_em, val_cnt);
	}
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_dump_buffer_copy_strict);

void kbase_hwcnt_dump_buffer_accumulate(
	struct kbase_hwcnt_dump_buffer *dst,
	const struct kbase_hwcnt_dump_buffer *src,
	const struct kbase_hwcnt_enable_map *dst_enable_map)
{
	const struct kbase_hwcnt_metadata *metadata;
	size_t grp, blk, blk_inst;

	if (WARN_ON(!dst) ||
	    WARN_ON(!src) ||
	    WARN_ON(!dst_enable_map) ||
	    WARN_ON(dst == src) ||
	    WARN_ON(dst->metadata != src->metadata) ||
	    WARN_ON(dst->metadata != dst_enable_map->metadata))
		return;

	metadata = dst->metadata;

	kbase_hwcnt_metadata_for_each_block(metadata, grp, blk, blk_inst) {
		u32 *dst_blk;
		const u32 *src_blk;
		size_t hdr_cnt;
		size_t ctr_cnt;

		if (!kbase_hwcnt_enable_map_block_enabled(
			dst_enable_map, grp, blk, blk_inst))
			continue;

		dst_blk = kbase_hwcnt_dump_buffer_block_instance(
			dst, grp, blk, blk_inst);
		src_blk = kbase_hwcnt_dump_buffer_block_instance(
			src, grp, blk, blk_inst);
		hdr_cnt = kbase_hwcnt_metadata_block_headers_count(
			metadata, grp, blk);
		ctr_cnt = kbase_hwcnt_metadata_block_counters_count(
			metadata, grp, blk);

		kbase_hwcnt_dump_buffer_block_accumulate(
			dst_blk, src_blk, hdr_cnt, ctr_cnt);
	}
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_dump_buffer_accumulate);

void kbase_hwcnt_dump_buffer_accumulate_strict(
	struct kbase_hwcnt_dump_buffer *dst,
	const struct kbase_hwcnt_dump_buffer *src,
	const struct kbase_hwcnt_enable_map *dst_enable_map)
{
	const struct kbase_hwcnt_metadata *metadata;
	size_t grp, blk, blk_inst;

	if (WARN_ON(!dst) ||
	    WARN_ON(!src) ||
	    WARN_ON(!dst_enable_map) ||
	    WARN_ON(dst == src) ||
	    WARN_ON(dst->metadata != src->metadata) ||
	    WARN_ON(dst->metadata != dst_enable_map->metadata))
		return;

	metadata = dst->metadata;

	kbase_hwcnt_metadata_for_each_block(metadata, grp, blk, blk_inst) {
		u32 *dst_blk = kbase_hwcnt_dump_buffer_block_instance(
			dst, grp, blk, blk_inst);
		const u32 *src_blk = kbase_hwcnt_dump_buffer_block_instance(
			src, grp, blk, blk_inst);
		const u64 *blk_em = kbase_hwcnt_enable_map_block_instance(
			dst_enable_map, grp, blk, blk_inst);
		size_t hdr_cnt = kbase_hwcnt_metadata_block_headers_count(
			metadata, grp, blk);
		size_t ctr_cnt = kbase_hwcnt_metadata_block_counters_count(
			metadata, grp, blk);
		/* Align upwards to include padding bytes */
		ctr_cnt = KBASE_HWCNT_ALIGN_UPWARDS(hdr_cnt + ctr_cnt,
			(KBASE_HWCNT_BLOCK_BYTE_ALIGNMENT /
			 KBASE_HWCNT_VALUE_BYTES) - hdr_cnt);

		kbase_hwcnt_dump_buffer_block_accumulate_strict(
			dst_blk, src_blk, blk_em, hdr_cnt, ctr_cnt);
	}
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_dump_buffer_accumulate_strict);
