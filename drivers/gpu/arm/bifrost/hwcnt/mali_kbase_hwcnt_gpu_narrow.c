// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2021-2022 ARM Limited. All rights reserved.
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

#include "hwcnt/mali_kbase_hwcnt_gpu.h"
#include "hwcnt/mali_kbase_hwcnt_gpu_narrow.h"

#include <linux/bug.h>
#include <linux/err.h>
#include <linux/slab.h>

int kbase_hwcnt_gpu_metadata_narrow_create(const struct kbase_hwcnt_metadata_narrow **dst_md_narrow,
					   const struct kbase_hwcnt_metadata *src_md)
{
	struct kbase_hwcnt_description desc;
	struct kbase_hwcnt_group_description group;
	struct kbase_hwcnt_block_description blks[KBASE_HWCNT_V5_BLOCK_TYPE_COUNT];
	size_t prfcnt_values_per_block;
	size_t blk;
	int err;
	struct kbase_hwcnt_metadata_narrow *metadata_narrow;

	if (!dst_md_narrow || !src_md || !src_md->grp_metadata ||
	    !src_md->grp_metadata[0].blk_metadata)
		return -EINVAL;

	/* Only support 1 group count and KBASE_HWCNT_V5_BLOCK_TYPE_COUNT block
	 * count in the metadata.
	 */
	if ((kbase_hwcnt_metadata_group_count(src_md) != 1) ||
	    (kbase_hwcnt_metadata_block_count(src_md, 0) != KBASE_HWCNT_V5_BLOCK_TYPE_COUNT))
		return -EINVAL;

	/* Get the values count in the first block. */
	prfcnt_values_per_block = kbase_hwcnt_metadata_block_values_count(src_md, 0, 0);

	/* check all blocks should have same values count. */
	for (blk = 1; blk < KBASE_HWCNT_V5_BLOCK_TYPE_COUNT; blk++) {
		size_t val_cnt = kbase_hwcnt_metadata_block_values_count(src_md, 0, blk);
		if (val_cnt != prfcnt_values_per_block)
			return -EINVAL;
	}

	/* Only support 64 and 128 entries per block. */
	if ((prfcnt_values_per_block != 64) && (prfcnt_values_per_block != 128))
		return -EINVAL;

	metadata_narrow = kmalloc(sizeof(*metadata_narrow), GFP_KERNEL);
	if (!metadata_narrow)
		return -ENOMEM;

	/* Narrow to 64 entries per block to keep API backward compatibility. */
	prfcnt_values_per_block = 64;

	for (blk = 0; blk < KBASE_HWCNT_V5_BLOCK_TYPE_COUNT; blk++) {
		size_t blk_hdr_cnt = kbase_hwcnt_metadata_block_headers_count(src_md, 0, blk);
		blks[blk] = (struct kbase_hwcnt_block_description){
			.type = kbase_hwcnt_metadata_block_type(src_md, 0, blk),
			.inst_cnt = kbase_hwcnt_metadata_block_instance_count(src_md, 0, blk),
			.hdr_cnt = blk_hdr_cnt,
			.ctr_cnt = prfcnt_values_per_block - blk_hdr_cnt,
		};
	}

	group = (struct kbase_hwcnt_group_description){
		.type = kbase_hwcnt_metadata_group_type(src_md, 0),
		.blk_cnt = KBASE_HWCNT_V5_BLOCK_TYPE_COUNT,
		.blks = blks,
	};

	desc = (struct kbase_hwcnt_description){
		.grp_cnt = kbase_hwcnt_metadata_group_count(src_md),
		.avail_mask = src_md->avail_mask,
		.clk_cnt = src_md->clk_cnt,
		.grps = &group,
	};

	err = kbase_hwcnt_metadata_create(&desc, &metadata_narrow->metadata);
	if (!err) {
		/* Narrow down the buffer size to half as the narrowed metadata
		 * only supports 32-bit but the created metadata uses 64-bit for
		 * block entry.
		 */
		metadata_narrow->dump_buf_bytes = metadata_narrow->metadata->dump_buf_bytes >> 1;
		*dst_md_narrow = metadata_narrow;
	} else {
		kfree(metadata_narrow);
	}

	return err;
}

void kbase_hwcnt_gpu_metadata_narrow_destroy(const struct kbase_hwcnt_metadata_narrow *md_narrow)
{
	if (!md_narrow)
		return;

	kbase_hwcnt_metadata_destroy(md_narrow->metadata);
	kfree(md_narrow);
}

int kbase_hwcnt_dump_buffer_narrow_alloc(const struct kbase_hwcnt_metadata_narrow *md_narrow,
					 struct kbase_hwcnt_dump_buffer_narrow *dump_buf)
{
	size_t dump_buf_bytes;
	size_t clk_cnt_buf_bytes;
	u8 *buf;

	if (!md_narrow || !dump_buf)
		return -EINVAL;

	dump_buf_bytes = md_narrow->dump_buf_bytes;
	clk_cnt_buf_bytes = sizeof(*dump_buf->clk_cnt_buf) * md_narrow->metadata->clk_cnt;

	/* Make a single allocation for both dump_buf and clk_cnt_buf. */
	buf = kmalloc(dump_buf_bytes + clk_cnt_buf_bytes, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	*dump_buf = (struct kbase_hwcnt_dump_buffer_narrow){
		.md_narrow = md_narrow,
		.dump_buf = (u32 *)buf,
		.clk_cnt_buf = (u64 *)(buf + dump_buf_bytes),
	};

	return 0;
}

void kbase_hwcnt_dump_buffer_narrow_free(struct kbase_hwcnt_dump_buffer_narrow *dump_buf_narrow)
{
	if (!dump_buf_narrow)
		return;

	kfree(dump_buf_narrow->dump_buf);
	*dump_buf_narrow = (struct kbase_hwcnt_dump_buffer_narrow){ .md_narrow = NULL,
								    .dump_buf = NULL,
								    .clk_cnt_buf = NULL };
}

int kbase_hwcnt_dump_buffer_narrow_array_alloc(
	const struct kbase_hwcnt_metadata_narrow *md_narrow, size_t n,
	struct kbase_hwcnt_dump_buffer_narrow_array *dump_bufs)
{
	struct kbase_hwcnt_dump_buffer_narrow *buffers;
	size_t buf_idx;
	unsigned int order;
	unsigned long addr;
	size_t dump_buf_bytes;
	size_t clk_cnt_buf_bytes;
	size_t total_dump_buf_size;

	if (!md_narrow || !dump_bufs)
		return -EINVAL;

	dump_buf_bytes = md_narrow->dump_buf_bytes;
	clk_cnt_buf_bytes = sizeof(*dump_bufs->bufs->clk_cnt_buf) * md_narrow->metadata->clk_cnt;

	/* Allocate memory for the dump buffer struct array */
	buffers = kmalloc_array(n, sizeof(*buffers), GFP_KERNEL);
	if (!buffers)
		return -ENOMEM;

	/* Allocate pages for the actual dump buffers, as they tend to be fairly
	 * large.
	 */
	order = get_order((dump_buf_bytes + clk_cnt_buf_bytes) * n);
	addr = __get_free_pages(GFP_KERNEL | __GFP_ZERO, order);

	if (!addr) {
		kfree(buffers);
		return -ENOMEM;
	}

	*dump_bufs = (struct kbase_hwcnt_dump_buffer_narrow_array){
		.page_addr = addr,
		.page_order = order,
		.buf_cnt = n,
		.bufs = buffers,
	};

	total_dump_buf_size = dump_buf_bytes * n;
	/* Set the buffer of each dump buf */
	for (buf_idx = 0; buf_idx < n; buf_idx++) {
		const size_t dump_buf_offset = dump_buf_bytes * buf_idx;
		const size_t clk_cnt_buf_offset =
			total_dump_buf_size + (clk_cnt_buf_bytes * buf_idx);

		buffers[buf_idx] = (struct kbase_hwcnt_dump_buffer_narrow){
			.md_narrow = md_narrow,
			.dump_buf = (u32 *)(addr + dump_buf_offset),
			.clk_cnt_buf = (u64 *)(addr + clk_cnt_buf_offset),
		};
	}

	return 0;
}

void kbase_hwcnt_dump_buffer_narrow_array_free(
	struct kbase_hwcnt_dump_buffer_narrow_array *dump_bufs)
{
	if (!dump_bufs)
		return;

	kfree(dump_bufs->bufs);
	free_pages(dump_bufs->page_addr, dump_bufs->page_order);
	memset(dump_bufs, 0, sizeof(*dump_bufs));
}

void kbase_hwcnt_dump_buffer_block_copy_strict_narrow(u32 *dst_blk, const u64 *src_blk,
						      const u64 *blk_em, size_t val_cnt)
{
	size_t val;

	for (val = 0; val < val_cnt; val++) {
		bool val_enabled = kbase_hwcnt_enable_map_block_value_enabled(blk_em, val);
		u32 src_val = (src_blk[val] > U32_MAX) ? U32_MAX : (u32)src_blk[val];

		dst_blk[val] = val_enabled ? src_val : 0;
	}
}

void kbase_hwcnt_dump_buffer_copy_strict_narrow(struct kbase_hwcnt_dump_buffer_narrow *dst_narrow,
						const struct kbase_hwcnt_dump_buffer *src,
						const struct kbase_hwcnt_enable_map *dst_enable_map)
{
	const struct kbase_hwcnt_metadata_narrow *metadata_narrow;
	size_t grp;
	size_t clk;

	if (WARN_ON(!dst_narrow) || WARN_ON(!src) || WARN_ON(!dst_enable_map) ||
	    WARN_ON(dst_narrow->md_narrow->metadata == src->metadata) ||
	    WARN_ON(dst_narrow->md_narrow->metadata->grp_cnt != src->metadata->grp_cnt) ||
	    WARN_ON(src->metadata->grp_cnt != 1) ||
	    WARN_ON(dst_narrow->md_narrow->metadata->grp_metadata[0].blk_cnt !=
		    src->metadata->grp_metadata[0].blk_cnt) ||
	    WARN_ON(dst_narrow->md_narrow->metadata->grp_metadata[0].blk_cnt !=
		    KBASE_HWCNT_V5_BLOCK_TYPE_COUNT) ||
	    WARN_ON(dst_narrow->md_narrow->metadata->grp_metadata[0].blk_metadata[0].ctr_cnt >
		    src->metadata->grp_metadata[0].blk_metadata[0].ctr_cnt))
		return;

	/* Don't use src metadata since src buffer is bigger than dst buffer. */
	metadata_narrow = dst_narrow->md_narrow;

	for (grp = 0; grp < kbase_hwcnt_metadata_narrow_group_count(metadata_narrow); grp++) {
		size_t blk;
		size_t blk_cnt = kbase_hwcnt_metadata_narrow_block_count(metadata_narrow, grp);

		for (blk = 0; blk < blk_cnt; blk++) {
			size_t blk_inst;
			size_t blk_inst_cnt = kbase_hwcnt_metadata_narrow_block_instance_count(
				metadata_narrow, grp, blk);

			for (blk_inst = 0; blk_inst < blk_inst_cnt; blk_inst++) {
				/* The narrowed down buffer is only 32-bit. */
				u32 *dst_blk = kbase_hwcnt_dump_buffer_narrow_block_instance(
					dst_narrow, grp, blk, blk_inst);
				const u64 *src_blk = kbase_hwcnt_dump_buffer_block_instance(
					src, grp, blk, blk_inst);
				const u64 *blk_em = kbase_hwcnt_enable_map_block_instance(
					dst_enable_map, grp, blk, blk_inst);
				size_t val_cnt = kbase_hwcnt_metadata_narrow_block_values_count(
					metadata_narrow, grp, blk);
				/* Align upwards to include padding bytes */
				val_cnt = KBASE_HWCNT_ALIGN_UPWARDS(
					val_cnt, (KBASE_HWCNT_BLOCK_BYTE_ALIGNMENT /
						  KBASE_HWCNT_VALUE_BYTES));

				kbase_hwcnt_dump_buffer_block_copy_strict_narrow(dst_blk, src_blk,
										 blk_em, val_cnt);
			}
		}
	}

	for (clk = 0; clk < metadata_narrow->metadata->clk_cnt; clk++) {
		bool clk_enabled =
			kbase_hwcnt_clk_enable_map_enabled(dst_enable_map->clk_enable_map, clk);

		dst_narrow->clk_cnt_buf[clk] = clk_enabled ? src->clk_cnt_buf[clk] : 0;
	}
}
