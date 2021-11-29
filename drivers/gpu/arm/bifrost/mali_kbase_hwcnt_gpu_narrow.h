/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2021 ARM Limited. All rights reserved.
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

#ifndef _KBASE_HWCNT_GPU_NARROW_H_
#define _KBASE_HWCNT_GPU_NARROW_H_

#include "mali_kbase_hwcnt_types.h"
#include <linux/types.h>

struct kbase_device;
struct kbase_hwcnt_metadata;
struct kbase_hwcnt_enable_map;
struct kbase_hwcnt_dump_buffer;

/**
 * struct kbase_hwcnt_metadata_narrow - Narrow metadata describing the physical
 *                                      layout of narrow dump buffers.
 *                                      For backward compatibility, the narrow
 *                                      metadata only supports 64 counters per
 *                                      block and 32-bit per block entry.
 * @metadata:       Non-NULL pointer to the metadata before narrow down to
 *                  32-bit per block entry, it has 64 counters per block and
 *                  64-bit per value.
 * @dump_buf_bytes: The size in bytes after narrow 64-bit to 32-bit per block
 *                  entry.
 */
struct kbase_hwcnt_metadata_narrow {
	const struct kbase_hwcnt_metadata *metadata;
	size_t dump_buf_bytes;
};

/**
 * struct kbase_hwcnt_dump_buffer_narrow - Hardware counter narrow dump buffer.
 * @md_narrow:   Non-NULL pointer to narrow metadata used to identify, and to
 *               describe the layout of the narrow dump buffer.
 * @dump_buf:    Non-NULL pointer to an array of u32 values, the array size
 *               is md_narrow->dump_buf_bytes.
 * @clk_cnt_buf: A pointer to an array of u64 values for cycle count elapsed
 *               for each clock domain.
 */
struct kbase_hwcnt_dump_buffer_narrow {
	const struct kbase_hwcnt_metadata_narrow *md_narrow;
	u32 *dump_buf;
	u64 *clk_cnt_buf;
};

/**
 * struct kbase_hwcnt_dump_buffer_narrow_array - Hardware counter narrow dump
 *                                               buffer array.
 * @page_addr:  Address of first allocated page. A single allocation is used for
 *              all narrow dump buffers in the array.
 * @page_order: The allocation order of the pages, the order is on a logarithmic
 *              scale.
 * @buf_cnt:    The number of allocated dump buffers.
 * @bufs:       Non-NULL pointer to the array of narrow dump buffer descriptors.
 */
struct kbase_hwcnt_dump_buffer_narrow_array {
	unsigned long page_addr;
	unsigned int page_order;
	size_t buf_cnt;
	struct kbase_hwcnt_dump_buffer_narrow *bufs;
};

/**
 * kbase_hwcnt_metadata_narrow_group_count() - Get the number of groups from
 *                                             narrow metadata.
 * @md_narrow: Non-NULL pointer to narrow metadata.
 *
 * Return: Number of hardware counter groups described by narrow metadata.
 */
static inline size_t kbase_hwcnt_metadata_narrow_group_count(
	const struct kbase_hwcnt_metadata_narrow *md_narrow)
{
	return kbase_hwcnt_metadata_group_count(md_narrow->metadata);
}

/**
 * kbase_hwcnt_metadata_narrow_group_type() - Get the arbitrary type of a group
 *                                            from narrow metadata.
 * @md_narrow: Non-NULL pointer to narrow metadata.
 * @grp:      Index of the group in the narrow metadata.
 *
 * Return: Type of the group grp.
 */
static inline u64 kbase_hwcnt_metadata_narrow_group_type(
	const struct kbase_hwcnt_metadata_narrow *md_narrow, size_t grp)
{
	return kbase_hwcnt_metadata_group_type(md_narrow->metadata, grp);
}

/**
 * kbase_hwcnt_metadata_narrow_block_count() - Get the number of blocks in a
 *                                             group from narrow metadata.
 * @md_narrow: Non-NULL pointer to narrow metadata.
 * @grp:       Index of the group in the narrow metadata.
 *
 * Return: Number of blocks in group grp.
 */
static inline size_t kbase_hwcnt_metadata_narrow_block_count(
	const struct kbase_hwcnt_metadata_narrow *md_narrow, size_t grp)
{
	return kbase_hwcnt_metadata_block_count(md_narrow->metadata, grp);
}

/**
 * kbase_hwcnt_metadata_narrow_block_instance_count() - Get the number of
 *                                                      instances of a block
 *                                                      from narrow metadata.
 * @md_narrow: Non-NULL pointer to narrow metadata.
 * @grp:       Index of the group in the narrow metadata.
 * @blk:       Index of the block in the group.
 *
 * Return: Number of instances of block blk in group grp.
 */
static inline size_t kbase_hwcnt_metadata_narrow_block_instance_count(
	const struct kbase_hwcnt_metadata_narrow *md_narrow, size_t grp,
	size_t blk)
{
	return kbase_hwcnt_metadata_block_instance_count(md_narrow->metadata,
							 grp, blk);
}

/**
 * kbase_hwcnt_metadata_narrow_block_headers_count() - Get the number of counter
 *                                                     headers from narrow
 *                                                     metadata.
 * @md_narrow: Non-NULL pointer to narrow metadata.
 * @grp:       Index of the group in the narrow metadata.
 * @blk:       Index of the block in the group.
 *
 * Return: Number of counter headers in each instance of block blk in group grp.
 */
static inline size_t kbase_hwcnt_metadata_narrow_block_headers_count(
	const struct kbase_hwcnt_metadata_narrow *md_narrow, size_t grp,
	size_t blk)
{
	return kbase_hwcnt_metadata_block_headers_count(md_narrow->metadata,
							grp, blk);
}

/**
 * kbase_hwcnt_metadata_narrow_block_counters_count() - Get the number of
 *                                                      counters from narrow
 *                                                      metadata.
 * @md_narrow: Non-NULL pointer to narrow metadata.
 * @grp:       Index of the group in the narrow metadata.
 * @blk:       Index of the block in the group.
 *
 * Return: Number of counters in each instance of block blk in group grp.
 */
static inline size_t kbase_hwcnt_metadata_narrow_block_counters_count(
	const struct kbase_hwcnt_metadata_narrow *md_narrow, size_t grp,
	size_t blk)
{
	return kbase_hwcnt_metadata_block_counters_count(md_narrow->metadata,
							 grp, blk);
}

/**
 * kbase_hwcnt_metadata_narrow_block_values_count() - Get the number of values
 *                                                    from narrow metadata.
 * @md_narrow: Non-NULL pointer to narrow metadata.
 * @grp:       Index of the group in the narrow metadata.
 * @blk:       Index of the block in the group.
 *
 * Return: Number of headers plus counters in each instance of block blk
 *         in group grp.
 */
static inline size_t kbase_hwcnt_metadata_narrow_block_values_count(
	const struct kbase_hwcnt_metadata_narrow *md_narrow, size_t grp,
	size_t blk)
{
	return kbase_hwcnt_metadata_narrow_block_counters_count(md_narrow, grp,
								blk) +
	       kbase_hwcnt_metadata_narrow_block_headers_count(md_narrow, grp,
							       blk);
}

/**
 * kbase_hwcnt_dump_buffer_narrow_block_instance() - Get the pointer to a
 *                                                   narrowed block instance's
 *                                                   dump buffer.
 * @buf:      Non-NULL pointer to narrow dump buffer.
 * @grp:      Index of the group in the narrow metadata.
 * @blk:      Index of the block in the group.
 * @blk_inst: Index of the block instance in the block.
 *
 * Return: u32* to the dump buffer for the block instance.
 */
static inline u32 *kbase_hwcnt_dump_buffer_narrow_block_instance(
	const struct kbase_hwcnt_dump_buffer_narrow *buf, size_t grp,
	size_t blk, size_t blk_inst)
{
	return buf->dump_buf +
	       buf->md_narrow->metadata->grp_metadata[grp].dump_buf_index +
	       buf->md_narrow->metadata->grp_metadata[grp]
		       .blk_metadata[blk]
		       .dump_buf_index +
	       (buf->md_narrow->metadata->grp_metadata[grp]
			.blk_metadata[blk]
			.dump_buf_stride *
		blk_inst);
}

/**
 * kbase_hwcnt_gpu_metadata_narrow_create() - Create HWC metadata with HWC
 *                                            entries per block truncated to
 *                                            64 entries and block entry size
 *                                            narrowed down to 32-bit.
 *
 * @dst_md_narrow: Non-NULL pointer to where created narrow metadata is stored
 *                 on success.
 * @src_md:        Non-NULL pointer to the HWC metadata used as the source to
 *                 create dst_md_narrow.
 *
 * For backward compatibility of the interface to user clients, a new metadata
 * with entries per block truncated to 64 and block entry size narrowed down
 * to 32-bit will be created for dst_md_narrow.
 * The total entries per block in src_md must be 64 or 128, if it's other
 * values, function returns error since it's not supported.
 *
 * Return: 0 on success, else error code.
 */
int kbase_hwcnt_gpu_metadata_narrow_create(
	const struct kbase_hwcnt_metadata_narrow **dst_md_narrow,
	const struct kbase_hwcnt_metadata *src_md);

/**
 * kbase_hwcnt_gpu_metadata_narrow_destroy() - Destroy a hardware counter narrow
 *                                             metadata object.
 * @md_narrow: Pointer to hardware counter narrow metadata.
 */
void kbase_hwcnt_gpu_metadata_narrow_destroy(
	const struct kbase_hwcnt_metadata_narrow *md_narrow);

/**
 * kbase_hwcnt_dump_buffer_narrow_alloc() - Allocate a narrow dump buffer.
 * @md_narrow: Non-NULL pointer to narrow metadata.
 * @dump_buf:  Non-NULL pointer to narrow dump buffer to be initialised. Will be
 *             initialised to undefined values, so must be used as a copy
 *             destination, or cleared before use.
 *
 * Return: 0 on success, else error code.
 */
int kbase_hwcnt_dump_buffer_narrow_alloc(
	const struct kbase_hwcnt_metadata_narrow *md_narrow,
	struct kbase_hwcnt_dump_buffer_narrow *dump_buf);

/**
 * kbase_hwcnt_dump_buffer_narrow_free() - Free a narrow dump buffer.
 * @dump_buf: Dump buffer to be freed.
 *
 * Can be safely called on an all-zeroed narrow dump buffer structure, or on an
 * already freed narrow dump buffer.
 */
void kbase_hwcnt_dump_buffer_narrow_free(
	struct kbase_hwcnt_dump_buffer_narrow *dump_buf);

/**
 * kbase_hwcnt_dump_buffer_narrow_array_alloc() - Allocate an array of narrow
 *                                                dump buffers.
 * @md_narrow:  Non-NULL pointer to narrow metadata.
 * @n:          Number of narrow dump buffers to allocate
 * @dump_bufs:  Non-NULL pointer to a kbase_hwcnt_dump_buffer_narrow_array
 *              object to be initialised.
 *
 * A single zeroed contiguous page allocation will be used for all of the
 * buffers inside the object, where:
 * dump_bufs->bufs[n].dump_buf == page_addr + n * md_narrow.dump_buf_bytes
 *
 * Return: 0 on success, else error code.
 */
int kbase_hwcnt_dump_buffer_narrow_array_alloc(
	const struct kbase_hwcnt_metadata_narrow *md_narrow, size_t n,
	struct kbase_hwcnt_dump_buffer_narrow_array *dump_bufs);

/**
 * kbase_hwcnt_dump_buffer_narrow_array_free() - Free a narrow dump buffer
 *                                               array.
 * @dump_bufs: Narrow Dump buffer array to be freed.
 *
 * Can be safely called on an all-zeroed narrow dump buffer array structure, or
 * on an already freed narrow dump buffer array.
 */
void kbase_hwcnt_dump_buffer_narrow_array_free(
	struct kbase_hwcnt_dump_buffer_narrow_array *dump_bufs);

/**
 * kbase_hwcnt_dump_buffer_block_copy_strict_narrow() - Copy all enabled block
 *                                                      values from source to
 *                                                      destination.
 * @dst_blk: Non-NULL pointer to destination block obtained from a call to
 *           kbase_hwcnt_dump_buffer_narrow_block_instance.
 * @src_blk: Non-NULL pointer to source block obtained from a call to
 *           kbase_hwcnt_dump_buffer_block_instance.
 * @blk_em:  Non-NULL pointer to the block bitfield(s) obtained from a call to
 *           kbase_hwcnt_enable_map_block_instance.
 * @val_cnt: Number of values in the block.
 *
 * After the copy, any disabled values in destination will be zero, the enabled
 * values in destination will be saturated at U32_MAX if the corresponding
 * source value is bigger than U32_MAX, or copy the value from source if the
 * corresponding source value is less than or equal to U32_MAX.
 */
void kbase_hwcnt_dump_buffer_block_copy_strict_narrow(u32 *dst_blk,
						      const u64 *src_blk,
						      const u64 *blk_em,
						      size_t val_cnt);

/**
 * kbase_hwcnt_dump_buffer_copy_strict_narrow() - Copy all enabled values to a
 *                                                narrow dump buffer.
 * @dst_narrow:     Non-NULL pointer to destination dump buffer.
 * @src:            Non-NULL pointer to source dump buffer.
 * @dst_enable_map: Non-NULL pointer to enable map specifying enabled values.
 *
 * After the operation, all non-enabled values (including padding bytes) will be
 * zero. Slower than the non-strict variant.
 *
 * The enabled values in dst_narrow will be saturated at U32_MAX if the
 * corresponding source value is bigger than U32_MAX, or copy the value from
 * source if the corresponding source value is less than or equal to U32_MAX.
 */
void kbase_hwcnt_dump_buffer_copy_strict_narrow(
	struct kbase_hwcnt_dump_buffer_narrow *dst_narrow,
	const struct kbase_hwcnt_dump_buffer *src,
	const struct kbase_hwcnt_enable_map *dst_enable_map);

#endif /* _KBASE_HWCNT_GPU_NARROW_H_ */
