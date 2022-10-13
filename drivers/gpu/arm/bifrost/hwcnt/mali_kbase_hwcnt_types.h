/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2018, 2020-2022 ARM Limited. All rights reserved.
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

/*
 * Hardware counter types.
 * Contains structures for describing the physical layout of hardware counter
 * dump buffers and enable maps within a system.
 *
 * Also contains helper functions for manipulation of these dump buffers and
 * enable maps.
 *
 * Through use of these structures and functions, hardware counters can be
 * enabled, copied, accumulated, and generally manipulated in a generic way,
 * regardless of the physical counter dump layout.
 *
 * Terminology:
 *
 * Hardware Counter System:
 *   A collection of hardware counter groups, making a full hardware counter
 *   system.
 * Hardware Counter Group:
 *   A group of Hardware Counter Blocks (e.g. a t62x might have more than one
 *   core group, so has one counter group per core group, where each group
 *   may have a different number and layout of counter blocks).
 * Hardware Counter Block:
 *   A block of hardware counters (e.g. shader block, tiler block).
 * Hardware Counter Block Instance:
 *   An instance of a Hardware Counter Block (e.g. an MP4 GPU might have
 *   4 shader block instances).
 *
 * Block Header:
 *   A header value inside a counter block. Headers don't count anything,
 *   so it is only valid to copy or zero them. Headers are always the first
 *   values in the block.
 * Block Counter:
 *   A counter value inside a counter block. Counters can be zeroed, copied,
 *   or accumulated. Counters are always immediately after the headers in the
 *   block.
 * Block Value:
 *   A catch-all term for block headers and block counters.
 *
 * Enable Map:
 *   An array of u64 bitfields, where each bit either enables exactly one
 *   block value, or is unused (padding).
 * Dump Buffer:
 *   An array of u64 values, where each u64 corresponds either to one block
 *   value, or is unused (padding).
 * Availability Mask:
 *   A bitfield, where each bit corresponds to whether a block instance is
 *   physically available (e.g. an MP3 GPU may have a sparse core mask of
 *   0b1011, meaning it only has 3 cores but for hardware counter dumps has the
 *   same dump buffer layout as an MP4 GPU with a core mask of 0b1111. In this
 *   case, the availability mask might be 0b1011111 (the exact layout will
 *   depend on the specific hardware architecture), with the 3 extra early bits
 *   corresponding to other block instances in the hardware counter system).
 * Metadata:
 *   Structure describing the physical layout of the enable map and dump buffers
 *   for a specific hardware counter system.
 *
 */

#ifndef _KBASE_HWCNT_TYPES_H_
#define _KBASE_HWCNT_TYPES_H_

#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>

/* Number of bytes in each bitfield */
#define KBASE_HWCNT_BITFIELD_BYTES (sizeof(u64))

/* Number of bits in each bitfield */
#define KBASE_HWCNT_BITFIELD_BITS (KBASE_HWCNT_BITFIELD_BYTES * BITS_PER_BYTE)

/* Number of bytes for each counter value.
 * Use 64-bit per counter in driver to avoid HW 32-bit register values
 * overflow after a long time accumulation.
 */
#define KBASE_HWCNT_VALUE_BYTES (sizeof(u64))

/* Number of bits in an availability mask (i.e. max total number of block
 * instances supported in a Hardware Counter System)
 */
#define KBASE_HWCNT_AVAIL_MASK_BITS (sizeof(u64) * BITS_PER_BYTE)

/* Minimum alignment of each block of hardware counters */
#define KBASE_HWCNT_BLOCK_BYTE_ALIGNMENT (KBASE_HWCNT_BITFIELD_BITS * KBASE_HWCNT_VALUE_BYTES)

/**
 * KBASE_HWCNT_ALIGN_UPWARDS() - Calculate next aligned value.
 * @value:     The value to align upwards.
 * @alignment: The alignment boundary.
 *
 * Return: Input value if already aligned to the specified boundary, or next
 * (incrementing upwards) aligned value.
 */
#define KBASE_HWCNT_ALIGN_UPWARDS(value, alignment)                                                \
	(value + ((alignment - (value % alignment)) % alignment))

/**
 * struct kbase_hwcnt_block_description - Description of one or more identical,
 *                                        contiguous, Hardware Counter Blocks.
 * @type:     The arbitrary identifier used to identify the type of the block.
 * @inst_cnt: The number of Instances of the block.
 * @hdr_cnt:  The number of 64-bit Block Headers in the block.
 * @ctr_cnt:  The number of 64-bit Block Counters in the block.
 */
struct kbase_hwcnt_block_description {
	u64 type;
	size_t inst_cnt;
	size_t hdr_cnt;
	size_t ctr_cnt;
};

/**
 * struct kbase_hwcnt_group_description - Description of one or more identical,
 *                                        contiguous Hardware Counter Groups.
 * @type:    The arbitrary identifier used to identify the type of the group.
 * @blk_cnt: The number of types of Hardware Counter Block in the group.
 * @blks:    Non-NULL pointer to an array of blk_cnt block descriptions,
 *           describing each type of Hardware Counter Block in the group.
 */
struct kbase_hwcnt_group_description {
	u64 type;
	size_t blk_cnt;
	const struct kbase_hwcnt_block_description *blks;
};

/**
 * struct kbase_hwcnt_description - Description of a Hardware Counter System.
 * @grp_cnt:    The number of Hardware Counter Groups.
 * @grps:       Non-NULL pointer to an array of grp_cnt group descriptions,
 *              describing each Hardware Counter Group in the system.
 * @avail_mask: Flat Availability Mask for all block instances in the system.
 * @clk_cnt:    The number of clock domains in the system. The maximum is 64.
 */
struct kbase_hwcnt_description {
	size_t grp_cnt;
	const struct kbase_hwcnt_group_description *grps;
	u64 avail_mask;
	u8 clk_cnt;
};

/**
 * struct kbase_hwcnt_block_metadata - Metadata describing the physical layout
 *                                     of a block in a Hardware Counter System's
 *                                     Dump Buffers and Enable Maps.
 * @type:              The arbitrary identifier used to identify the type of the
 *                     block.
 * @inst_cnt:          The number of Instances of the block.
 * @hdr_cnt:           The number of 64-bit Block Headers in the block.
 * @ctr_cnt:           The number of 64-bit Block Counters in the block.
 * @enable_map_index:  Index in u64s into the parent's Enable Map where the
 *                     Enable Map bitfields of the Block Instances described by
 *                     this metadata start.
 * @enable_map_stride: Stride in u64s between the Enable Maps of each of the
 *                     Block Instances described by this metadata.
 * @dump_buf_index:    Index in u64s into the parent's Dump Buffer where the
 *                     Dump Buffers of the Block Instances described by this
 *                     metadata start.
 * @dump_buf_stride:   Stride in u64s between the Dump Buffers of each of the
 *                     Block Instances described by this metadata.
 * @avail_mask_index:  Index in bits into the parent's Availability Mask where
 *                     the Availability Masks of the Block Instances described
 *                     by this metadata start.
 */
struct kbase_hwcnt_block_metadata {
	u64 type;
	size_t inst_cnt;
	size_t hdr_cnt;
	size_t ctr_cnt;
	size_t enable_map_index;
	size_t enable_map_stride;
	size_t dump_buf_index;
	size_t dump_buf_stride;
	size_t avail_mask_index;
};

/**
 * struct kbase_hwcnt_group_metadata - Metadata describing the physical layout
 *                                     of a group of blocks in a Hardware
 *                                     Counter System's Dump Buffers and Enable
 *                                     Maps.
 * @type:             The arbitrary identifier used to identify the type of the
 *                    group.
 * @blk_cnt:          The number of types of Hardware Counter Block in the
 *                    group.
 * @blk_metadata:     Non-NULL pointer to an array of blk_cnt block metadata,
 *                    describing the physical layout of each type of Hardware
 *                    Counter Block in the group.
 * @enable_map_index: Index in u64s into the parent's Enable Map where the
 *                    Enable Maps of the blocks within the group described by
 *                    this metadata start.
 * @dump_buf_index:   Index in u64s into the parent's Dump Buffer where the
 *                    Dump Buffers of the blocks within the group described by
 *                    metadata start.
 * @avail_mask_index: Index in bits into the parent's Availability Mask where
 *                    the Availability Masks of the blocks within the group
 *                    described by this metadata start.
 */
struct kbase_hwcnt_group_metadata {
	u64 type;
	size_t blk_cnt;
	const struct kbase_hwcnt_block_metadata *blk_metadata;
	size_t enable_map_index;
	size_t dump_buf_index;
	size_t avail_mask_index;
};

/**
 * struct kbase_hwcnt_metadata - Metadata describing the memory layout
 *                               of Dump Buffers and Enable Maps within a
 *                               Hardware Counter System.
 * @grp_cnt:          The number of Hardware Counter Groups.
 * @grp_metadata:     Non-NULL pointer to an array of grp_cnt group metadata,
 *                    describing the physical layout of each Hardware Counter
 *                    Group in the system.
 * @enable_map_bytes: The size in bytes of an Enable Map needed for the system.
 * @dump_buf_bytes:   The size in bytes of a Dump Buffer needed for the system.
 * @avail_mask:       The Availability Mask for the system.
 * @clk_cnt:          The number of clock domains in the system.
 */
struct kbase_hwcnt_metadata {
	size_t grp_cnt;
	const struct kbase_hwcnt_group_metadata *grp_metadata;
	size_t enable_map_bytes;
	size_t dump_buf_bytes;
	u64 avail_mask;
	u8 clk_cnt;
};

/**
 * struct kbase_hwcnt_enable_map - Hardware Counter Enable Map. Array of u64
 *                                 bitfields.
 * @metadata:   Non-NULL pointer to metadata used to identify, and to describe
 *              the layout of the enable map.
 * @hwcnt_enable_map: Non-NULL pointer of size metadata->enable_map_bytes to an
 *              array of u64 bitfields, each bit of which enables one hardware
 *              counter.
 * @clk_enable_map: An array of u64 bitfields, each bit of which enables cycle
 *              counter for a given clock domain.
 */
struct kbase_hwcnt_enable_map {
	const struct kbase_hwcnt_metadata *metadata;
	u64 *hwcnt_enable_map;
	u64 clk_enable_map;
};

/**
 * struct kbase_hwcnt_dump_buffer - Hardware Counter Dump Buffer.
 * @metadata: Non-NULL pointer to metadata used to identify, and to describe
 *            the layout of the Dump Buffer.
 * @dump_buf: Non-NULL pointer to an array of u64 values, the array size is
 *            metadata->dump_buf_bytes.
 * @clk_cnt_buf: A pointer to an array of u64 values for cycle count elapsed
 *               for each clock domain.
 */
struct kbase_hwcnt_dump_buffer {
	const struct kbase_hwcnt_metadata *metadata;
	u64 *dump_buf;
	u64 *clk_cnt_buf;
};

/**
 * struct kbase_hwcnt_dump_buffer_array - Hardware Counter Dump Buffer array.
 * @page_addr:  Address of allocated pages. A single allocation is used for all
 *              Dump Buffers in the array.
 * @page_order: The allocation order of the pages, the order is on a logarithmic
 *              scale.
 * @buf_cnt:    The number of allocated Dump Buffers.
 * @bufs:       Non-NULL pointer to the array of Dump Buffers.
 */
struct kbase_hwcnt_dump_buffer_array {
	unsigned long page_addr;
	unsigned int page_order;
	size_t buf_cnt;
	struct kbase_hwcnt_dump_buffer *bufs;
};

/**
 * kbase_hwcnt_metadata_create() - Create a hardware counter metadata object
 *                                 from a description.
 * @desc:     Non-NULL pointer to a hardware counter description.
 * @metadata: Non-NULL pointer to where created metadata will be stored on
 *            success.
 *
 * Return: 0 on success, else error code.
 */
int kbase_hwcnt_metadata_create(const struct kbase_hwcnt_description *desc,
				const struct kbase_hwcnt_metadata **metadata);

/**
 * kbase_hwcnt_metadata_destroy() - Destroy a hardware counter metadata object.
 * @metadata: Pointer to hardware counter metadata
 */
void kbase_hwcnt_metadata_destroy(const struct kbase_hwcnt_metadata *metadata);

/**
 * kbase_hwcnt_metadata_group_count() - Get the number of groups.
 * @metadata: Non-NULL pointer to metadata.
 *
 * Return: Number of hardware counter groups described by metadata.
 */
static inline size_t kbase_hwcnt_metadata_group_count(const struct kbase_hwcnt_metadata *metadata)
{
	if (WARN_ON(!metadata))
		return 0;

	return metadata->grp_cnt;
}

/**
 * kbase_hwcnt_metadata_group_type() - Get the arbitrary type of a group.
 * @metadata: Non-NULL pointer to metadata.
 * @grp:      Index of the group in the metadata.
 *
 * Return: Type of the group grp.
 */
static inline u64 kbase_hwcnt_metadata_group_type(const struct kbase_hwcnt_metadata *metadata,
						  size_t grp)
{
	if (WARN_ON(!metadata) || WARN_ON(grp >= metadata->grp_cnt))
		return 0;

	return metadata->grp_metadata[grp].type;
}

/**
 * kbase_hwcnt_metadata_block_count() - Get the number of blocks in a group.
 * @metadata: Non-NULL pointer to metadata.
 * @grp:      Index of the group in the metadata.
 *
 * Return: Number of blocks in group grp.
 */
static inline size_t kbase_hwcnt_metadata_block_count(const struct kbase_hwcnt_metadata *metadata,
						      size_t grp)
{
	if (WARN_ON(!metadata) || WARN_ON(grp >= metadata->grp_cnt))
		return 0;

	return metadata->grp_metadata[grp].blk_cnt;
}

/**
 * kbase_hwcnt_metadata_block_type() - Get the arbitrary type of a block.
 * @metadata: Non-NULL pointer to metadata.
 * @grp:      Index of the group in the metadata.
 * @blk:      Index of the block in the group.
 *
 * Return: Type of the block blk in group grp.
 */
static inline u64 kbase_hwcnt_metadata_block_type(const struct kbase_hwcnt_metadata *metadata,
						  size_t grp, size_t blk)
{
	if (WARN_ON(!metadata) || WARN_ON(grp >= metadata->grp_cnt) ||
	    WARN_ON(blk >= metadata->grp_metadata[grp].blk_cnt))
		return 0;

	return metadata->grp_metadata[grp].blk_metadata[blk].type;
}

/**
 * kbase_hwcnt_metadata_block_instance_count() - Get the number of instances of
 *                                               a block.
 * @metadata: Non-NULL pointer to metadata.
 * @grp:      Index of the group in the metadata.
 * @blk:      Index of the block in the group.
 *
 * Return: Number of instances of block blk in group grp.
 */
static inline size_t
kbase_hwcnt_metadata_block_instance_count(const struct kbase_hwcnt_metadata *metadata, size_t grp,
					  size_t blk)
{
	if (WARN_ON(!metadata) || WARN_ON(grp >= metadata->grp_cnt) ||
	    WARN_ON(blk >= metadata->grp_metadata[grp].blk_cnt))
		return 0;

	return metadata->grp_metadata[grp].blk_metadata[blk].inst_cnt;
}

/**
 * kbase_hwcnt_metadata_block_headers_count() - Get the number of counter
 *                                              headers.
 * @metadata: Non-NULL pointer to metadata.
 * @grp:      Index of the group in the metadata.
 * @blk:      Index of the block in the group.
 *
 * Return: Number of counter headers in each instance of block blk in group grp.
 */
static inline size_t
kbase_hwcnt_metadata_block_headers_count(const struct kbase_hwcnt_metadata *metadata, size_t grp,
					 size_t blk)
{
	if (WARN_ON(!metadata) || WARN_ON(grp >= metadata->grp_cnt) ||
	    WARN_ON(blk >= metadata->grp_metadata[grp].blk_cnt))
		return 0;

	return metadata->grp_metadata[grp].blk_metadata[blk].hdr_cnt;
}

/**
 * kbase_hwcnt_metadata_block_counters_count() - Get the number of counters.
 * @metadata: Non-NULL pointer to metadata.
 * @grp:      Index of the group in the metadata.
 * @blk:      Index of the block in the group.
 *
 * Return: Number of counters in each instance of block blk in group grp.
 */
static inline size_t
kbase_hwcnt_metadata_block_counters_count(const struct kbase_hwcnt_metadata *metadata, size_t grp,
					  size_t blk)
{
	if (WARN_ON(!metadata) || WARN_ON(grp >= metadata->grp_cnt) ||
	    WARN_ON(blk >= metadata->grp_metadata[grp].blk_cnt))
		return 0;

	return metadata->grp_metadata[grp].blk_metadata[blk].ctr_cnt;
}

/**
 * kbase_hwcnt_metadata_block_enable_map_stride() - Get the enable map stride.
 * @metadata: Non-NULL pointer to metadata.
 * @grp:      Index of the group in the metadata.
 * @blk:      Index of the block in the group.
 *
 * Return: enable map stride in each instance of block blk in group grp.
 */
static inline size_t
kbase_hwcnt_metadata_block_enable_map_stride(const struct kbase_hwcnt_metadata *metadata,
					     size_t grp, size_t blk)
{
	if (WARN_ON(!metadata) || WARN_ON(grp >= metadata->grp_cnt) ||
	    WARN_ON(blk >= metadata->grp_metadata[grp].blk_cnt))
		return 0;

	return metadata->grp_metadata[grp].blk_metadata[blk].enable_map_stride;
}

/**
 * kbase_hwcnt_metadata_block_values_count() - Get the number of values.
 * @metadata: Non-NULL pointer to metadata.
 * @grp:      Index of the group in the metadata.
 * @blk:      Index of the block in the group.
 *
 * Return: Number of headers plus counters in each instance of block blk
 *         in group grp.
 */
static inline size_t
kbase_hwcnt_metadata_block_values_count(const struct kbase_hwcnt_metadata *metadata, size_t grp,
					size_t blk)
{
	if (WARN_ON(!metadata) || WARN_ON(grp >= metadata->grp_cnt) ||
	    WARN_ON(blk >= metadata->grp_metadata[grp].blk_cnt))
		return 0;

	return kbase_hwcnt_metadata_block_counters_count(metadata, grp, blk) +
	       kbase_hwcnt_metadata_block_headers_count(metadata, grp, blk);
}

/**
 * kbase_hwcnt_metadata_for_each_block() - Iterate over each block instance in
 *                                         the metadata.
 * @md:       Non-NULL pointer to metadata.
 * @grp:      size_t variable used as group iterator.
 * @blk:      size_t variable used as block iterator.
 * @blk_inst: size_t variable used as block instance iterator.
 *
 * Iteration order is group, then block, then block instance (i.e. linearly
 * through memory).
 */
#define kbase_hwcnt_metadata_for_each_block(md, grp, blk, blk_inst)                                \
	for ((grp) = 0; (grp) < kbase_hwcnt_metadata_group_count((md)); (grp)++)                   \
		for ((blk) = 0; (blk) < kbase_hwcnt_metadata_block_count((md), (grp)); (blk)++)    \
			for ((blk_inst) = 0;                                                       \
			     (blk_inst) <                                                          \
			     kbase_hwcnt_metadata_block_instance_count((md), (grp), (blk));        \
			     (blk_inst)++)

/**
 * kbase_hwcnt_metadata_block_avail_bit() - Get the bit index into the avail
 *                                          mask corresponding to the block.
 * @metadata: Non-NULL pointer to metadata.
 * @grp:      Index of the group in the metadata.
 * @blk:      Index of the block in the group.
 *
 * Return: The bit index into the avail mask for the block.
 */
static inline size_t
kbase_hwcnt_metadata_block_avail_bit(const struct kbase_hwcnt_metadata *metadata, size_t grp,
				     size_t blk)
{
	if (WARN_ON(!metadata) || WARN_ON(grp >= metadata->grp_cnt) ||
	    WARN_ON(blk >= metadata->grp_metadata[grp].blk_cnt))
		return 0;

	return metadata->grp_metadata[grp].avail_mask_index +
	       metadata->grp_metadata[grp].blk_metadata[blk].avail_mask_index;
}

/**
 * kbase_hwcnt_metadata_block_instance_avail() - Check if a block instance is
 *                                               available.
 * @metadata: Non-NULL pointer to metadata.
 * @grp:      Index of the group in the metadata.
 * @blk:      Index of the block in the group.
 * @blk_inst: Index of the block instance in the block.
 *
 * Return: true if the block instance is available, else false.
 */
static inline bool
kbase_hwcnt_metadata_block_instance_avail(const struct kbase_hwcnt_metadata *metadata, size_t grp,
					  size_t blk, size_t blk_inst)
{
	size_t bit;
	u64 mask;

	if (WARN_ON(!metadata))
		return false;

	bit = kbase_hwcnt_metadata_block_avail_bit(metadata, grp, blk) + blk_inst;
	mask = 1ull << bit;

	return (metadata->avail_mask & mask) != 0;
}

/**
 * kbase_hwcnt_enable_map_alloc() - Allocate an enable map.
 * @metadata:   Non-NULL pointer to metadata describing the system.
 * @enable_map: Non-NULL pointer to enable map to be initialised. Will be
 *              initialised to all zeroes (i.e. all counters disabled).
 *
 * Return: 0 on success, else error code.
 */
int kbase_hwcnt_enable_map_alloc(const struct kbase_hwcnt_metadata *metadata,
				 struct kbase_hwcnt_enable_map *enable_map);

/**
 * kbase_hwcnt_enable_map_free() - Free an enable map.
 * @enable_map: Enable map to be freed.
 *
 * Can be safely called on an all-zeroed enable map structure, or on an already
 * freed enable map.
 */
void kbase_hwcnt_enable_map_free(struct kbase_hwcnt_enable_map *enable_map);

/**
 * kbase_hwcnt_enable_map_block_instance() - Get the pointer to a block
 *                                           instance's enable map.
 * @map:      Non-NULL pointer to enable map.
 * @grp:      Index of the group in the metadata.
 * @blk:      Index of the block in the group.
 * @blk_inst: Index of the block instance in the block.
 *
 * Return: u64* to the bitfield(s) used as the enable map for the
 *         block instance.
 */
static inline u64 *kbase_hwcnt_enable_map_block_instance(const struct kbase_hwcnt_enable_map *map,
							 size_t grp, size_t blk, size_t blk_inst)
{
	if (WARN_ON(!map) || WARN_ON(!map->hwcnt_enable_map))
		return NULL;

	if (WARN_ON(!map->metadata) || WARN_ON(grp >= map->metadata->grp_cnt) ||
	    WARN_ON(blk >= map->metadata->grp_metadata[grp].blk_cnt) ||
	    WARN_ON(blk_inst >= map->metadata->grp_metadata[grp].blk_metadata[blk].inst_cnt))
		return map->hwcnt_enable_map;

	return map->hwcnt_enable_map + map->metadata->grp_metadata[grp].enable_map_index +
	       map->metadata->grp_metadata[grp].blk_metadata[blk].enable_map_index +
	       (map->metadata->grp_metadata[grp].blk_metadata[blk].enable_map_stride * blk_inst);
}

/**
 * kbase_hwcnt_bitfield_count() - Calculate the number of u64 bitfields required
 *                                to have at minimum one bit per value.
 * @val_cnt: Number of values.
 *
 * Return: Number of required bitfields.
 */
static inline size_t kbase_hwcnt_bitfield_count(size_t val_cnt)
{
	return (val_cnt + KBASE_HWCNT_BITFIELD_BITS - 1) / KBASE_HWCNT_BITFIELD_BITS;
}

/**
 * kbase_hwcnt_enable_map_block_disable_all() - Disable all values in a block.
 * @dst:      Non-NULL pointer to enable map.
 * @grp:      Index of the group in the metadata.
 * @blk:      Index of the block in the group.
 * @blk_inst: Index of the block instance in the block.
 */
static inline void kbase_hwcnt_enable_map_block_disable_all(struct kbase_hwcnt_enable_map *dst,
							    size_t grp, size_t blk, size_t blk_inst)
{
	size_t val_cnt;
	size_t bitfld_cnt;
	u64 *const block_enable_map =
		kbase_hwcnt_enable_map_block_instance(dst, grp, blk, blk_inst);

	if (WARN_ON(!dst))
		return;

	val_cnt = kbase_hwcnt_metadata_block_values_count(dst->metadata, grp, blk);
	bitfld_cnt = kbase_hwcnt_bitfield_count(val_cnt);

	memset(block_enable_map, 0, bitfld_cnt * KBASE_HWCNT_BITFIELD_BYTES);
}

/**
 * kbase_hwcnt_enable_map_disable_all() - Disable all values in the enable map.
 * @dst: Non-NULL pointer to enable map to zero.
 */
static inline void kbase_hwcnt_enable_map_disable_all(struct kbase_hwcnt_enable_map *dst)
{
	if (WARN_ON(!dst) || WARN_ON(!dst->metadata))
		return;

	if (dst->hwcnt_enable_map != NULL)
		memset(dst->hwcnt_enable_map, 0, dst->metadata->enable_map_bytes);

	dst->clk_enable_map = 0;
}

/**
 * kbase_hwcnt_enable_map_block_enable_all() - Enable all values in a block.
 * @dst:      Non-NULL pointer to enable map.
 * @grp:      Index of the group in the metadata.
 * @blk:      Index of the block in the group.
 * @blk_inst: Index of the block instance in the block.
 */
static inline void kbase_hwcnt_enable_map_block_enable_all(struct kbase_hwcnt_enable_map *dst,
							   size_t grp, size_t blk, size_t blk_inst)
{
	size_t val_cnt;
	size_t bitfld_cnt;
	u64 *const block_enable_map =
		kbase_hwcnt_enable_map_block_instance(dst, grp, blk, blk_inst);
	size_t bitfld_idx;

	if (WARN_ON(!dst))
		return;

	val_cnt = kbase_hwcnt_metadata_block_values_count(dst->metadata, grp, blk);
	bitfld_cnt = kbase_hwcnt_bitfield_count(val_cnt);

	for (bitfld_idx = 0; bitfld_idx < bitfld_cnt; bitfld_idx++) {
		const u64 remaining_values = val_cnt - (bitfld_idx * KBASE_HWCNT_BITFIELD_BITS);
		u64 block_enable_map_mask = U64_MAX;

		if (remaining_values < KBASE_HWCNT_BITFIELD_BITS)
			block_enable_map_mask = (1ull << remaining_values) - 1;

		block_enable_map[bitfld_idx] = block_enable_map_mask;
	}
}

/**
 * kbase_hwcnt_enable_map_enable_all() - Enable all values in an enable
 *                                       map.
 * @dst: Non-NULL pointer to enable map.
 */
static inline void kbase_hwcnt_enable_map_enable_all(struct kbase_hwcnt_enable_map *dst)
{
	size_t grp, blk, blk_inst;

	if (WARN_ON(!dst) || WARN_ON(!dst->metadata))
		return;

	kbase_hwcnt_metadata_for_each_block(dst->metadata, grp, blk, blk_inst)
		kbase_hwcnt_enable_map_block_enable_all(dst, grp, blk, blk_inst);

	dst->clk_enable_map = (1ull << dst->metadata->clk_cnt) - 1;
}

/**
 * kbase_hwcnt_enable_map_copy() - Copy an enable map to another.
 * @dst: Non-NULL pointer to destination enable map.
 * @src: Non-NULL pointer to source enable map.
 *
 * The dst and src MUST have been created from the same metadata.
 */
static inline void kbase_hwcnt_enable_map_copy(struct kbase_hwcnt_enable_map *dst,
					       const struct kbase_hwcnt_enable_map *src)
{
	if (WARN_ON(!dst) || WARN_ON(!src) || WARN_ON(!dst->metadata) ||
	    WARN_ON(dst->metadata != src->metadata))
		return;

	if (dst->hwcnt_enable_map != NULL) {
		if (WARN_ON(!src->hwcnt_enable_map))
			return;

		memcpy(dst->hwcnt_enable_map, src->hwcnt_enable_map,
		       dst->metadata->enable_map_bytes);
	}

	dst->clk_enable_map = src->clk_enable_map;
}

/**
 * kbase_hwcnt_enable_map_union() - Union dst and src enable maps into dst.
 * @dst: Non-NULL pointer to destination enable map.
 * @src: Non-NULL pointer to source enable map.
 *
 * The dst and src MUST have been created from the same metadata.
 */
static inline void kbase_hwcnt_enable_map_union(struct kbase_hwcnt_enable_map *dst,
						const struct kbase_hwcnt_enable_map *src)
{
	if (WARN_ON(!dst) || WARN_ON(!src) || WARN_ON(!dst->metadata) ||
	    WARN_ON(dst->metadata != src->metadata))
		return;

	if (dst->hwcnt_enable_map != NULL) {
		size_t i;
		size_t const bitfld_count =
			dst->metadata->enable_map_bytes / KBASE_HWCNT_BITFIELD_BYTES;

		if (WARN_ON(!src->hwcnt_enable_map))
			return;

		for (i = 0; i < bitfld_count; i++)
			dst->hwcnt_enable_map[i] |= src->hwcnt_enable_map[i];
	}

	dst->clk_enable_map |= src->clk_enable_map;
}

/**
 * kbase_hwcnt_enable_map_block_enabled() - Check if any values in a block
 *                                          instance are enabled.
 * @enable_map: Non-NULL pointer to enable map.
 * @grp:        Index of the group in the metadata.
 * @blk:        Index of the block in the group.
 * @blk_inst:   Index of the block instance in the block.
 *
 * Return: true if any values in the block are enabled, else false.
 */
static inline bool
kbase_hwcnt_enable_map_block_enabled(const struct kbase_hwcnt_enable_map *enable_map, size_t grp,
				     size_t blk, size_t blk_inst)
{
	bool any_enabled = false;
	size_t val_cnt;
	size_t bitfld_cnt;
	const u64 *const block_enable_map =
		kbase_hwcnt_enable_map_block_instance(enable_map, grp, blk, blk_inst);
	size_t bitfld_idx;

	if (WARN_ON(!enable_map))
		return false;

	val_cnt = kbase_hwcnt_metadata_block_values_count(enable_map->metadata, grp, blk);
	bitfld_cnt = kbase_hwcnt_bitfield_count(val_cnt);

	for (bitfld_idx = 0; bitfld_idx < bitfld_cnt; bitfld_idx++) {
		const u64 remaining_values = val_cnt - (bitfld_idx * KBASE_HWCNT_BITFIELD_BITS);
		u64 block_enable_map_mask = U64_MAX;

		if (remaining_values < KBASE_HWCNT_BITFIELD_BITS)
			block_enable_map_mask = (1ull << remaining_values) - 1;

		any_enabled = any_enabled || (block_enable_map[bitfld_idx] & block_enable_map_mask);
	}

	return any_enabled;
}

/**
 * kbase_hwcnt_enable_map_any_enabled() - Check if any values are enabled.
 * @enable_map: Non-NULL pointer to enable map.
 *
 * Return: true if any values are enabled, else false.
 */
static inline bool
kbase_hwcnt_enable_map_any_enabled(const struct kbase_hwcnt_enable_map *enable_map)
{
	size_t grp, blk, blk_inst;
	u64 clk_enable_map_mask;

	if (WARN_ON(!enable_map) || WARN_ON(!enable_map->metadata))
		return false;

	clk_enable_map_mask = (1ull << enable_map->metadata->clk_cnt) - 1;

	if (enable_map->metadata->clk_cnt > 0 && (enable_map->clk_enable_map & clk_enable_map_mask))
		return true;

	kbase_hwcnt_metadata_for_each_block(enable_map->metadata, grp, blk, blk_inst)
	{
		if (kbase_hwcnt_enable_map_block_enabled(enable_map, grp, blk, blk_inst))
			return true;
	}

	return false;
}

/**
 * kbase_hwcnt_enable_map_block_value_enabled() - Check if a value in a block
 *                                                instance is enabled.
 * @bitfld:  Non-NULL pointer to the block bitfield(s) obtained from a call to
 *           kbase_hwcnt_enable_map_block_instance.
 * @val_idx: Index of the value to check in the block instance.
 *
 * Return: true if the value was enabled, else false.
 */
static inline bool kbase_hwcnt_enable_map_block_value_enabled(const u64 *bitfld, size_t val_idx)
{
	const size_t idx = val_idx / KBASE_HWCNT_BITFIELD_BITS;
	const size_t bit = val_idx % KBASE_HWCNT_BITFIELD_BITS;
	const u64 mask = 1ull << bit;

	return (bitfld[idx] & mask) != 0;
}

/**
 * kbase_hwcnt_enable_map_block_enable_value() - Enable a value in a block
 *                                               instance.
 * @bitfld:  Non-NULL pointer to the block bitfield(s) obtained from a call to
 *           kbase_hwcnt_enable_map_block_instance.
 * @val_idx: Index of the value to enable in the block instance.
 */
static inline void kbase_hwcnt_enable_map_block_enable_value(u64 *bitfld, size_t val_idx)
{
	const size_t idx = val_idx / KBASE_HWCNT_BITFIELD_BITS;
	const size_t bit = val_idx % KBASE_HWCNT_BITFIELD_BITS;
	const u64 mask = 1ull << bit;

	bitfld[idx] |= mask;
}

/**
 * kbase_hwcnt_enable_map_block_disable_value() - Disable a value in a block
 *                                                instance.
 * @bitfld:  Non-NULL pointer to the block bitfield(s) obtained from a call to
 *           kbase_hwcnt_enable_map_block_instance.
 * @val_idx: Index of the value to disable in the block instance.
 */
static inline void kbase_hwcnt_enable_map_block_disable_value(u64 *bitfld, size_t val_idx)
{
	const size_t idx = val_idx / KBASE_HWCNT_BITFIELD_BITS;
	const size_t bit = val_idx % KBASE_HWCNT_BITFIELD_BITS;
	const u64 mask = 1ull << bit;

	bitfld[idx] &= ~mask;
}

/**
 * kbase_hwcnt_dump_buffer_alloc() - Allocate a dump buffer.
 * @metadata: Non-NULL pointer to metadata describing the system.
 * @dump_buf: Non-NULL pointer to dump buffer to be initialised. Will be
 *            initialised to undefined values, so must be used as a copy dest,
 *            or cleared before use.
 *
 * Return: 0 on success, else error code.
 */
int kbase_hwcnt_dump_buffer_alloc(const struct kbase_hwcnt_metadata *metadata,
				  struct kbase_hwcnt_dump_buffer *dump_buf);

/**
 * kbase_hwcnt_dump_buffer_free() - Free a dump buffer.
 * @dump_buf: Dump buffer to be freed.
 *
 * Can be safely called on an all-zeroed dump buffer structure, or on an already
 * freed dump buffer.
 */
void kbase_hwcnt_dump_buffer_free(struct kbase_hwcnt_dump_buffer *dump_buf);

/**
 * kbase_hwcnt_dump_buffer_array_alloc() - Allocate an array of dump buffers.
 * @metadata:  Non-NULL pointer to metadata describing the system.
 * @n:         Number of dump buffers to allocate
 * @dump_bufs: Non-NULL pointer to dump buffer array to be initialised.
 *
 * A single zeroed contiguous page allocation will be used for all of the
 * buffers inside the array, where:
 * dump_bufs[n].dump_buf == page_addr + n * metadata.dump_buf_bytes
 *
 * Return: 0 on success, else error code.
 */
int kbase_hwcnt_dump_buffer_array_alloc(const struct kbase_hwcnt_metadata *metadata, size_t n,
					struct kbase_hwcnt_dump_buffer_array *dump_bufs);

/**
 * kbase_hwcnt_dump_buffer_array_free() - Free a dump buffer array.
 * @dump_bufs: Dump buffer array to be freed.
 *
 * Can be safely called on an all-zeroed dump buffer array structure, or on an
 * already freed dump buffer array.
 */
void kbase_hwcnt_dump_buffer_array_free(struct kbase_hwcnt_dump_buffer_array *dump_bufs);

/**
 * kbase_hwcnt_dump_buffer_block_instance() - Get the pointer to a block
 *                                            instance's dump buffer.
 * @buf:      Non-NULL pointer to dump buffer.
 * @grp:      Index of the group in the metadata.
 * @blk:      Index of the block in the group.
 * @blk_inst: Index of the block instance in the block.
 *
 * Return: u64* to the dump buffer for the block instance.
 */
static inline u64 *kbase_hwcnt_dump_buffer_block_instance(const struct kbase_hwcnt_dump_buffer *buf,
							  size_t grp, size_t blk, size_t blk_inst)
{
	if (WARN_ON(!buf) || WARN_ON(!buf->dump_buf))
		return NULL;

	if (WARN_ON(!buf->metadata) || WARN_ON(grp >= buf->metadata->grp_cnt) ||
	    WARN_ON(blk >= buf->metadata->grp_metadata[grp].blk_cnt) ||
	    WARN_ON(blk_inst >= buf->metadata->grp_metadata[grp].blk_metadata[blk].inst_cnt))
		return buf->dump_buf;

	return buf->dump_buf + buf->metadata->grp_metadata[grp].dump_buf_index +
	       buf->metadata->grp_metadata[grp].blk_metadata[blk].dump_buf_index +
	       (buf->metadata->grp_metadata[grp].blk_metadata[blk].dump_buf_stride * blk_inst);
}

/**
 * kbase_hwcnt_dump_buffer_zero() - Zero all enabled values in dst.
 *                                  After the operation, all non-enabled values
 *                                  will be undefined.
 * @dst:            Non-NULL pointer to dump buffer.
 * @dst_enable_map: Non-NULL pointer to enable map specifying enabled values.
 *
 * The dst and dst_enable_map MUST have been created from the same metadata.
 */
void kbase_hwcnt_dump_buffer_zero(struct kbase_hwcnt_dump_buffer *dst,
				  const struct kbase_hwcnt_enable_map *dst_enable_map);

/**
 * kbase_hwcnt_dump_buffer_block_zero() - Zero all values in a block.
 * @dst_blk: Non-NULL pointer to dst block obtained from a call to
 *           kbase_hwcnt_dump_buffer_block_instance.
 * @val_cnt: Number of values in the block.
 */
static inline void kbase_hwcnt_dump_buffer_block_zero(u64 *dst_blk, size_t val_cnt)
{
	if (WARN_ON(!dst_blk))
		return;

	memset(dst_blk, 0, (val_cnt * KBASE_HWCNT_VALUE_BYTES));
}

/**
 * kbase_hwcnt_dump_buffer_zero_strict() - Zero all values in dst.
 *                                         After the operation, all values
 *                                         (including padding bytes) will be
 *                                         zero.
 *                                         Slower than the non-strict variant.
 * @dst: Non-NULL pointer to dump buffer.
 */
void kbase_hwcnt_dump_buffer_zero_strict(struct kbase_hwcnt_dump_buffer *dst);

/**
 * kbase_hwcnt_dump_buffer_zero_non_enabled() - Zero all non-enabled values in
 *                                              dst (including padding bytes and
 *                                              unavailable blocks).
 *                                              After the operation, all enabled
 *                                              values will be unchanged.
 * @dst:            Non-NULL pointer to dump buffer.
 * @dst_enable_map: Non-NULL pointer to enable map specifying enabled values.
 *
 * The dst and dst_enable_map MUST have been created from the same metadata.
 */
void kbase_hwcnt_dump_buffer_zero_non_enabled(struct kbase_hwcnt_dump_buffer *dst,
					      const struct kbase_hwcnt_enable_map *dst_enable_map);

/**
 * kbase_hwcnt_dump_buffer_block_zero_non_enabled() - Zero all non-enabled
 *                                                    values in a block.
 *                                                    After the operation, all
 *                                                    enabled values will be
 *                                                    unchanged.
 * @dst_blk: Non-NULL pointer to dst block obtained from a call to
 *           kbase_hwcnt_dump_buffer_block_instance.
 * @blk_em:  Non-NULL pointer to the block bitfield(s) obtained from a call to
 *           kbase_hwcnt_enable_map_block_instance.
 * @val_cnt: Number of values in the block.
 */
static inline void kbase_hwcnt_dump_buffer_block_zero_non_enabled(u64 *dst_blk, const u64 *blk_em,
								  size_t val_cnt)
{
	size_t val;

	if (WARN_ON(!dst_blk))
		return;

	for (val = 0; val < val_cnt; val++) {
		if (!kbase_hwcnt_enable_map_block_value_enabled(blk_em, val))
			dst_blk[val] = 0;
	}
}

/**
 * kbase_hwcnt_dump_buffer_copy() - Copy all enabled values from src to dst.
 *                                  After the operation, all non-enabled values
 *                                  will be undefined.
 * @dst:            Non-NULL pointer to dst dump buffer.
 * @src:            Non-NULL pointer to src dump buffer.
 * @dst_enable_map: Non-NULL pointer to enable map specifying enabled values.
 *
 * The dst, src, and dst_enable_map MUST have been created from the same
 * metadata.
 */
void kbase_hwcnt_dump_buffer_copy(struct kbase_hwcnt_dump_buffer *dst,
				  const struct kbase_hwcnt_dump_buffer *src,
				  const struct kbase_hwcnt_enable_map *dst_enable_map);

/**
 * kbase_hwcnt_dump_buffer_block_copy() - Copy all block values from src to dst.
 * @dst_blk: Non-NULL pointer to dst block obtained from a call to
 *           kbase_hwcnt_dump_buffer_block_instance.
 * @src_blk: Non-NULL pointer to src block obtained from a call to
 *           kbase_hwcnt_dump_buffer_block_instance.
 * @val_cnt: Number of values in the block.
 */
static inline void kbase_hwcnt_dump_buffer_block_copy(u64 *dst_blk, const u64 *src_blk,
						      size_t val_cnt)
{
	if (WARN_ON(!dst_blk) || WARN_ON(!src_blk))
		return;

	/* Copy all the counters in the block instance.
	 * Values of non-enabled counters are undefined.
	 */
	memcpy(dst_blk, src_blk, (val_cnt * KBASE_HWCNT_VALUE_BYTES));
}

/**
 * kbase_hwcnt_dump_buffer_copy_strict() - Copy all enabled values from src to
 *                                         dst.
 *                                         After the operation, all non-enabled
 *                                         values (including padding bytes) will
 *                                         be zero.
 *                                         Slower than the non-strict variant.
 * @dst:            Non-NULL pointer to dst dump buffer.
 * @src:            Non-NULL pointer to src dump buffer.
 * @dst_enable_map: Non-NULL pointer to enable map specifying enabled values.
 *
 * The dst, src, and dst_enable_map MUST have been created from the same
 * metadata.
 */
void kbase_hwcnt_dump_buffer_copy_strict(struct kbase_hwcnt_dump_buffer *dst,
					 const struct kbase_hwcnt_dump_buffer *src,
					 const struct kbase_hwcnt_enable_map *dst_enable_map);

/**
 * kbase_hwcnt_dump_buffer_block_copy_strict() - Copy all enabled block values
 *                                               from src to dst.
 *                                               After the operation, all
 *                                               non-enabled values will be
 *                                               zero.
 * @dst_blk: Non-NULL pointer to dst block obtained from a call to
 *           kbase_hwcnt_dump_buffer_block_instance.
 * @src_blk: Non-NULL pointer to src block obtained from a call to
 *           kbase_hwcnt_dump_buffer_block_instance.
 * @blk_em:  Non-NULL pointer to the block bitfield(s) obtained from a call to
 *           kbase_hwcnt_enable_map_block_instance.
 * @val_cnt: Number of values in the block.
 *
 * After the copy, any disabled values in dst will be zero.
 */
static inline void kbase_hwcnt_dump_buffer_block_copy_strict(u64 *dst_blk, const u64 *src_blk,
							     const u64 *blk_em, size_t val_cnt)
{
	size_t val;

	if (WARN_ON(!dst_blk) || WARN_ON(!src_blk))
		return;

	for (val = 0; val < val_cnt; val++) {
		bool val_enabled = kbase_hwcnt_enable_map_block_value_enabled(blk_em, val);

		dst_blk[val] = val_enabled ? src_blk[val] : 0;
	}
}

/**
 * kbase_hwcnt_dump_buffer_accumulate() - Copy all enabled headers and
 *                                        accumulate all enabled counters from
 *                                        src to dst.
 *                                        After the operation, all non-enabled
 *                                        values will be undefined.
 * @dst:            Non-NULL pointer to dst dump buffer.
 * @src:            Non-NULL pointer to src dump buffer.
 * @dst_enable_map: Non-NULL pointer to enable map specifying enabled values.
 *
 * The dst, src, and dst_enable_map MUST have been created from the same
 * metadata.
 */
void kbase_hwcnt_dump_buffer_accumulate(struct kbase_hwcnt_dump_buffer *dst,
					const struct kbase_hwcnt_dump_buffer *src,
					const struct kbase_hwcnt_enable_map *dst_enable_map);

/**
 * kbase_hwcnt_dump_buffer_block_accumulate() - Copy all block headers and
 *                                              accumulate all block counters
 *                                              from src to dst.
 * @dst_blk: Non-NULL pointer to dst block obtained from a call to
 *           kbase_hwcnt_dump_buffer_block_instance.
 * @src_blk: Non-NULL pointer to src block obtained from a call to
 *           kbase_hwcnt_dump_buffer_block_instance.
 * @hdr_cnt: Number of headers in the block.
 * @ctr_cnt: Number of counters in the block.
 */
static inline void kbase_hwcnt_dump_buffer_block_accumulate(u64 *dst_blk, const u64 *src_blk,
							    size_t hdr_cnt, size_t ctr_cnt)
{
	size_t ctr;

	if (WARN_ON(!dst_blk) || WARN_ON(!src_blk))
		return;

	/* Copy all the headers in the block instance.
	 * Values of non-enabled headers are undefined.
	 */
	memcpy(dst_blk, src_blk, hdr_cnt * KBASE_HWCNT_VALUE_BYTES);

	/* Accumulate all the counters in the block instance.
	 * Values of non-enabled counters are undefined.
	 */
	for (ctr = hdr_cnt; ctr < ctr_cnt + hdr_cnt; ctr++)
		dst_blk[ctr] += src_blk[ctr];
}

/**
 * kbase_hwcnt_dump_buffer_accumulate_strict() - Copy all enabled headers and
 *                                               accumulate all enabled counters
 *                                               from src to dst.
 *                                               After the operation, all
 *                                               non-enabled values (including
 *                                               padding bytes) will be zero.
 *                                               Slower than the non-strict
 *                                               variant.
 * @dst:            Non-NULL pointer to dst dump buffer.
 * @src:            Non-NULL pointer to src dump buffer.
 * @dst_enable_map: Non-NULL pointer to enable map specifying enabled values.
 *
 * The dst, src, and dst_enable_map MUST have been created from the same
 * metadata.
 */
void kbase_hwcnt_dump_buffer_accumulate_strict(struct kbase_hwcnt_dump_buffer *dst,
					       const struct kbase_hwcnt_dump_buffer *src,
					       const struct kbase_hwcnt_enable_map *dst_enable_map);

/**
 * kbase_hwcnt_dump_buffer_block_accumulate_strict() - Copy all enabled block
 *                                                     headers and accumulate
 *                                                     all block counters from
 *                                                     src to dst.
 *                                                     After the operation, all
 *                                                     non-enabled values will
 *                                                     be zero.
 * @dst_blk: Non-NULL pointer to dst block obtained from a call to
 *           kbase_hwcnt_dump_buffer_block_instance.
 * @src_blk: Non-NULL pointer to src block obtained from a call to
 *           kbase_hwcnt_dump_buffer_block_instance.
 * @blk_em:  Non-NULL pointer to the block bitfield(s) obtained from a call to
 *           kbase_hwcnt_enable_map_block_instance.
 * @hdr_cnt: Number of headers in the block.
 * @ctr_cnt: Number of counters in the block.
 */
static inline void kbase_hwcnt_dump_buffer_block_accumulate_strict(u64 *dst_blk, const u64 *src_blk,
								   const u64 *blk_em,
								   size_t hdr_cnt, size_t ctr_cnt)
{
	size_t ctr;

	if (WARN_ON(!dst_blk) || WARN_ON(!src_blk))
		return;

	kbase_hwcnt_dump_buffer_block_copy_strict(dst_blk, src_blk, blk_em, hdr_cnt);

	for (ctr = hdr_cnt; ctr < ctr_cnt + hdr_cnt; ctr++) {
		bool ctr_enabled = kbase_hwcnt_enable_map_block_value_enabled(blk_em, ctr);

		if (ctr_enabled)
			dst_blk[ctr] += src_blk[ctr];
		else
			dst_blk[ctr] = 0;
	}
}

/**
 * kbase_hwcnt_metadata_for_each_clock() - Iterate over each clock domain in the
 *                                         metadata.
 * @md:          Non-NULL pointer to metadata.
 * @clk:         size_t variable used as clock iterator.
 */
#define kbase_hwcnt_metadata_for_each_clock(md, clk) for ((clk) = 0; (clk) < (md)->clk_cnt; (clk)++)

/**
 * kbase_hwcnt_clk_enable_map_enabled() - Check if the given index is enabled
 *                                        in clk_enable_map.
 * @clk_enable_map: An enable map for clock domains.
 * @index:          Index of the enable map for clock domain.
 *
 * Return: true if the index of the clock domain is enabled, else false.
 */
static inline bool kbase_hwcnt_clk_enable_map_enabled(const u64 clk_enable_map, const size_t index)
{
	if (WARN_ON(index >= 64))
		return false;
	if (clk_enable_map & (1ull << index))
		return true;
	return false;
}

#endif /* _KBASE_HWCNT_TYPES_H_ */
